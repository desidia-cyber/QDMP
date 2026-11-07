//! Bit-level reader/writer.
//!
//! The convention is unchanged from DollsMP (and from Unreal's own
//! `FBitReader`/`FBitWriter`): least-significant bit first within each byte, and
//! bit `i` of a written value lands at stream position `cursor + i`. At a
//! byte-aligned cursor that makes `write_u16/u32` little-endian, which is what
//! WIRE.md §0 requires, so the same primitives serve both the bit-packed and the
//! byte-aligned parts of the protocol.
//!
//! Two defects from `analysis/03-dollsmp-reuse.md` §B.5 are fixed here:
//!
//! * **Item 1** — a `bool` written as one bit cannot be spliced into a
//!   byte-framed container (`write_bytes` refuses an unaligned cursor), so the
//!   DollsMP `RpcPayload for bool` was unencodable and no test covered it. The
//!   fix is [`RpcPayload for bool`], which occupies exactly one byte; [`write_rpc`]
//!   / [`read_rpc`] are the length-prefixed container, and the test
//!   `rpc_bool_survives_a_byte_framed_container` is the test DollsMP lacked.
//! * **Item 5's sibling risk** — an unaligned byte write is now
//!   [`Error::UnalignedWrite`] carrying the bit position, rather than something a
//!   caller has to infer from a bare `NotByteAligned`.

use crate::error::{Error, Result};

/// Accumulates bits and produces a byte vector.
#[derive(Debug, Clone, Default)]
pub struct BitWriter {
    bytes: Vec<u8>,
    /// Number of bits already used in the final partial byte (0..8).
    used_bits: u8,
}

impl BitWriter {
    pub fn new() -> Self {
        Self { bytes: Vec::new(), used_bits: 0 }
    }

    pub fn with_capacity(bytes: usize) -> Self {
        Self { bytes: Vec::with_capacity(bytes), used_bits: 0 }
    }

    /// Total number of bits written so far.
    pub fn bit_len(&self) -> usize {
        if self.used_bits == 0 {
            self.bytes.len() * 8
        } else {
            (self.bytes.len() - 1) * 8 + self.used_bits as usize
        }
    }

    pub fn is_byte_aligned(&self) -> bool {
        self.used_bits == 0
    }

    pub fn is_empty(&self) -> bool {
        self.bit_len() == 0
    }

    /// Write the low `bits` bits of `value`, least-significant bit first.
    ///
    /// `bits` must be in `1..=64`. Bits above `bits` are masked off so a caller
    /// cannot corrupt a neighbouring field with a wider constant.
    pub fn write_bits(&mut self, value: u64, bits: u32) -> Result<()> {
        if bits == 0 || bits > 64 {
            return Err(Error::InvalidBitCount(bits));
        }
        let mut remaining = if bits == 64 { value } else { value & ((1u64 << bits) - 1) };
        let mut left: u32 = bits;
        while left > 0 {
            if self.used_bits == 0 {
                self.bytes.push(0);
            }
            let free: u32 = 8 - u32::from(self.used_bits);
            let take: u32 = free.min(left);
            let chunk_mask: u8 = if take == 8 { 0xFFu8 } else { (1u8 << take) - 1 };
            let chunk = (remaining as u8) & chunk_mask;
            let last = self.bytes.len() - 1;
            self.bytes[last] |= chunk << self.used_bits;
            self.used_bits = ((u32::from(self.used_bits) + take) % 8) as u8;
            remaining >>= take;
            left -= take;
        }
        Ok(())
    }

    pub fn write_bool(&mut self, v: bool) -> Result<()> {
        self.write_bits(u64::from(v), 1)
    }

    pub fn write_u8(&mut self, v: u8) -> Result<()> {
        self.write_bits(u64::from(v), 8)
    }

    pub fn write_u16(&mut self, v: u16) -> Result<()> {
        self.write_bits(u64::from(v), 16)
    }

    pub fn write_i16(&mut self, v: i16) -> Result<()> {
        self.write_bits(u64::from(v as u16), 16)
    }

    pub fn write_u32(&mut self, v: u32) -> Result<()> {
        self.write_bits(u64::from(v), 32)
    }

    pub fn write_i32(&mut self, v: i32) -> Result<()> {
        self.write_bits(u64::from(v as u32), 32)
    }

    pub fn write_u64(&mut self, v: u64) -> Result<()> {
        self.write_bits(v, 64)
    }

    pub fn write_f32(&mut self, v: f32) -> Result<()> {
        self.write_u32(v.to_bits())
    }

    /// LEB128-style unsigned varint, usable at any bit offset (it writes eight
    /// bits at a time).
    pub fn write_varint(&mut self, mut v: u64) -> Result<()> {
        loop {
            let mut byte = (v & 0x7F) as u8;
            v >>= 7;
            if v != 0 {
                byte |= 0x80;
            }
            self.write_u8(byte)?;
            if v == 0 {
                return Ok(());
            }
        }
    }

    /// Zig-zag encoded signed varint (small magnitudes stay small).
    pub fn write_svarint(&mut self, v: i64) -> Result<()> {
        let zz = ((v << 1) ^ (v >> 63)) as u64;
        self.write_varint(zz)
    }

    /// Write raw bytes.
    ///
    /// Must be byte-aligned. Returning a typed error that names the bit position
    /// is the point: DollsMP's version returned a bare "not byte aligned" and the
    /// defect was found by reading the code, not by a failing test.
    pub fn write_bytes(&mut self, data: &[u8]) -> Result<()> {
        if self.used_bits != 0 {
            return Err(Error::UnalignedWrite { bit_pos: self.bit_len() });
        }
        self.bytes.extend_from_slice(data);
        Ok(())
    }

    /// Pad the current byte with zero bits.
    pub fn align(&mut self) {
        self.used_bits = 0;
    }

    /// Consume the writer, returning the packed bytes.
    pub fn finish(self) -> Vec<u8> {
        self.bytes
    }

    pub fn as_slice(&self) -> &[u8] {
        &self.bytes
    }
}

/// Reads bits written by [`BitWriter`].
#[derive(Debug, Clone)]
pub struct BitReader<'a> {
    data: &'a [u8],
    /// Absolute bit position.
    pos: usize,
}

impl<'a> BitReader<'a> {
    pub fn new(data: &'a [u8]) -> Self {
        Self { data, pos: 0 }
    }

    pub fn bit_len(&self) -> usize {
        self.data.len() * 8
    }

    pub fn bit_pos(&self) -> usize {
        self.pos
    }

    pub fn byte_pos(&self) -> usize {
        self.pos / 8
    }

    pub fn is_byte_aligned(&self) -> bool {
        self.pos % 8 == 0
    }

    pub fn remaining_bits(&self) -> usize {
        self.bit_len().saturating_sub(self.pos)
    }

    pub fn is_empty(&self) -> bool {
        self.remaining_bits() == 0
    }

    pub fn read_bits(&mut self, bits: u32) -> Result<u64> {
        if bits == 0 || bits > 64 {
            return Err(Error::InvalidBitCount(bits));
        }
        if self.remaining_bits() < bits as usize {
            return Err(Error::UnexpectedEof {
                needed: bits as usize,
                available: self.remaining_bits(),
            });
        }
        let mut out: u64 = 0;
        let mut produced = 0u32;
        while produced < bits {
            let byte_idx = self.pos / 8;
            let bit_off = (self.pos % 8) as u32;
            let byte = u64::from(self.data[byte_idx]);
            let avail = 8 - bit_off;
            let take = avail.min(bits - produced);
            let mask = (1u64 << take) - 1;
            let chunk = (byte >> bit_off) & mask;
            out |= chunk << produced;
            self.pos += take as usize;
            produced += take;
        }
        Ok(out)
    }

    pub fn read_bool(&mut self) -> Result<bool> {
        Ok(self.read_bits(1)? != 0)
    }

    pub fn read_u8(&mut self) -> Result<u8> {
        Ok(self.read_bits(8)? as u8)
    }

    pub fn read_u16(&mut self) -> Result<u16> {
        Ok(self.read_bits(16)? as u16)
    }

    pub fn read_i16(&mut self) -> Result<i16> {
        Ok(self.read_bits(16)? as u16 as i16)
    }

    pub fn read_u32(&mut self) -> Result<u32> {
        Ok(self.read_bits(32)? as u32)
    }

    pub fn read_i32(&mut self) -> Result<i32> {
        Ok(self.read_bits(32)? as u32 as i32)
    }

    pub fn read_u64(&mut self) -> Result<u64> {
        self.read_bits(64)
    }

    pub fn read_f32(&mut self) -> Result<f32> {
        Ok(f32::from_bits(self.read_u32()?))
    }

    pub fn read_varint(&mut self) -> Result<u64> {
        let mut result: u64 = 0;
        let mut shift = 0u32;
        loop {
            if shift > 63 {
                return Err(Error::VarintOverflow);
            }
            let byte = self.read_u8()?;
            result |= u64::from(byte & 0x7F) << shift;
            if byte & 0x80 == 0 {
                return Ok(result);
            }
            shift += 7;
        }
    }

    pub fn read_svarint(&mut self) -> Result<i64> {
        let zz = self.read_varint()?;
        Ok(((zz >> 1) as i64) ^ -((zz & 1) as i64))
    }

    /// Borrow `len` raw bytes. Must be byte-aligned.
    ///
    /// The returned slice is bounded by the input, so a declared length is never
    /// trusted enough to drive an allocation.
    pub fn read_bytes(&mut self, len: usize) -> Result<&'a [u8]> {
        if self.pos % 8 != 0 {
            return Err(Error::UnalignedRead { bit_pos: self.pos });
        }
        let need = len
            .checked_mul(8)
            .ok_or(Error::LimitExceeded { what: "byte field", limit: usize::MAX / 8, got: len })?;
        if self.remaining_bits() < need {
            return Err(Error::UnexpectedEof { needed: need, available: self.remaining_bits() });
        }
        let start = self.pos / 8;
        self.pos += need;
        Ok(&self.data[start..start + len])
    }

    /// Advance to the next byte boundary.
    pub fn align(&mut self) {
        let rem = self.pos % 8;
        if rem != 0 {
            self.pos += 8 - rem;
        }
    }
}

/// A value that can be encoded into a self-delimiting byte payload.
///
/// The container ([`write_rpc`]) prefixes the payload with a varint byte length
/// and splices it in with `write_bytes`, so every implementation **must** start
/// and end byte-aligned. `bool` therefore uses a whole byte, not one bit: that is
/// the entire fix for §B.5 item 1.
pub trait RpcPayload: Sized {
    fn encode_payload(&self, w: &mut BitWriter) -> Result<()>;
    fn decode_payload(r: &mut BitReader<'_>) -> Result<Self>;
}

impl RpcPayload for bool {
    fn encode_payload(&self, w: &mut BitWriter) -> Result<()> {
        w.write_u8(u8::from(*self))
    }

    fn decode_payload(r: &mut BitReader<'_>) -> Result<Self> {
        match r.read_u8()? {
            0 => Ok(false),
            1 => Ok(true),
            v => Err(Error::OutOfRange { what: "bool payload", value: i64::from(v) }),
        }
    }
}

macro_rules! rpc_scalar {
    ($ty:ty, $write:ident, $read:ident) => {
        impl RpcPayload for $ty {
            fn encode_payload(&self, w: &mut BitWriter) -> Result<()> {
                w.$write(*self)
            }

            fn decode_payload(r: &mut BitReader<'_>) -> Result<Self> {
                r.$read()
            }
        }
    };
}

rpc_scalar!(u8, write_u8, read_u8);
rpc_scalar!(u16, write_u16, read_u16);
rpc_scalar!(i16, write_i16, read_i16);
rpc_scalar!(u32, write_u32, read_u32);
rpc_scalar!(i32, write_i32, read_i32);
rpc_scalar!(u64, write_u64, read_u64);
rpc_scalar!(f32, write_f32, read_f32);

impl RpcPayload for String {
    fn encode_payload(&self, w: &mut BitWriter) -> Result<()> {
        let bytes = self.as_bytes();
        if bytes.len() > u16::MAX as usize {
            return Err(Error::LimitExceeded {
                what: "rpc string",
                limit: u16::MAX as usize,
                got: bytes.len(),
            });
        }
        w.write_u16(bytes.len() as u16)?;
        w.write_bytes(bytes)
    }

    fn decode_payload(r: &mut BitReader<'_>) -> Result<Self> {
        let len = r.read_u16()? as usize;
        let bytes = r.read_bytes(len)?;
        core::str::from_utf8(bytes).map(str::to_owned).map_err(|_| Error::BadString)
    }
}

/// Write a length-prefixed payload.
pub fn write_rpc<T: RpcPayload>(w: &mut BitWriter, v: &T) -> Result<()> {
    let mut inner = BitWriter::new();
    v.encode_payload(&mut inner)?;
    if !inner.is_byte_aligned() {
        return Err(Error::UnalignedWrite { bit_pos: inner.bit_len() });
    }
    let bytes = inner.finish();
    if bytes.len() > u16::MAX as usize {
        return Err(Error::LimitExceeded {
            what: "rpc payload",
            limit: u16::MAX as usize,
            got: bytes.len(),
        });
    }
    w.write_varint(bytes.len() as u64)?;
    w.write_bytes(&bytes)
}

/// Read a length-prefixed payload written by [`write_rpc`].
///
/// The payload must be consumed exactly: a declared length that does not match the
/// value's own size is a framing error, not something to paper over.
pub fn read_rpc<T: RpcPayload>(r: &mut BitReader<'_>) -> Result<T> {
    let len = r.read_varint()?;
    let len = usize::try_from(len)
        .map_err(|_| Error::LimitExceeded { what: "rpc payload", limit: usize::MAX, got: 0 })?;
    let bytes = r.read_bytes(len)?;
    let mut inner = BitReader::new(bytes);
    let v = T::decode_payload(&mut inner)?;
    if inner.remaining_bits() != 0 {
        return Err(Error::TrailingBytes { what: "rpc payload", got: inner.remaining_bits() / 8 });
    }
    Ok(v)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn roundtrip_single_values() {
        let mut w = BitWriter::new();
        w.write_u8(0xAB).unwrap();
        w.write_u16(0x1234).unwrap();
        w.write_u32(0xDEAD_BEEF).unwrap();
        w.write_bool(true).unwrap();
        w.write_bool(false).unwrap();
        let buf = w.finish();

        let mut r = BitReader::new(&buf);
        assert_eq!(r.read_u8().unwrap(), 0xAB);
        assert_eq!(r.read_u16().unwrap(), 0x1234);
        assert_eq!(r.read_u32().unwrap(), 0xDEAD_BEEF);
        assert!(r.read_bool().unwrap());
        assert!(!r.read_bool().unwrap());
    }

    #[test]
    fn signed_roundtrip_including_extremes() {
        let mut w = BitWriter::new();
        for v in [0i32, 1, -1, i32::MIN, i32::MAX, -2000, 268_435_455] {
            w.write_i32(v).unwrap();
        }
        for v in [0i16, 1, -1, i16::MIN, i16::MAX, -4096] {
            w.write_i16(v).unwrap();
        }
        let buf = w.finish();
        let mut r = BitReader::new(&buf);
        for v in [0i32, 1, -1, i32::MIN, i32::MAX, -2000, 268_435_455] {
            assert_eq!(r.read_i32().unwrap(), v);
        }
        for v in [0i16, 1, -1, i16::MIN, i16::MAX, -4096] {
            assert_eq!(r.read_i16().unwrap(), v);
        }
    }

    #[test]
    fn unaligned_bit_packing_is_lsb_first() {
        let mut w = BitWriter::new();
        w.write_bits(0b101, 3).unwrap();
        w.write_bits(0b11, 2).unwrap();
        w.write_bits(0b1, 1).unwrap();
        let bits = w.bit_len();
        let buf = w.finish();
        // LSB first: 101 11 1 -> 1 0 1 1 1 1 = 0b0011_1101
        assert_eq!(buf[0], 0b0011_1101);
        assert_eq!(bits, 6);
    }

    #[test]
    fn write_bits_masks_high_bits() {
        let mut w = BitWriter::new();
        w.write_bits(0xFFFF, 4).unwrap();
        w.write_bits(0x0, 4).unwrap();
        assert_eq!(w.finish()[0], 0x0F);
    }

    #[test]
    fn byte_aligned_multibyte_writes_are_little_endian() {
        // WIRE.md §0: this is the property the whole body codec rests on.
        let mut w = BitWriter::new();
        w.write_u16(0x1234).unwrap();
        w.write_u32(0x0102_0304).unwrap();
        assert_eq!(w.finish(), vec![0x34, 0x12, 0x04, 0x03, 0x02, 0x01]);
    }

    #[test]
    fn varint_roundtrip_boundaries() {
        for v in [0u64, 1, 127, 128, 255, 256, 16383, 16384, u32::MAX as u64, u64::MAX] {
            let mut w = BitWriter::new();
            w.write_varint(v).unwrap();
            let buf = w.finish();
            let mut r = BitReader::new(&buf);
            assert_eq!(r.read_varint().unwrap(), v, "varint {v} roundtrip");
        }
    }

    #[test]
    fn svarint_roundtrip() {
        for v in [0i64, -1, 1, -64, 63, i32::MIN as i64, i32::MAX as i64, i64::MIN, i64::MAX] {
            let mut w = BitWriter::new();
            w.write_svarint(v).unwrap();
            let buf = w.finish();
            let mut r = BitReader::new(&buf);
            assert_eq!(r.read_svarint().unwrap(), v, "svarint {v} roundtrip");
        }
    }

    #[test]
    fn svarint_small_values_are_compact() {
        let mut w = BitWriter::new();
        w.write_svarint(-1).unwrap();
        assert_eq!(w.finish().len(), 1);
        let mut w = BitWriter::new();
        w.write_svarint(63).unwrap();
        assert_eq!(w.finish().len(), 1);
        let mut w = BitWriter::new();
        w.write_svarint(64).unwrap();
        assert_eq!(w.finish().len(), 2);
    }

    #[test]
    fn float_roundtrip_is_bit_exact() {
        for v in [0.0f32, -0.0, 1.0, -1.0, 1e-30, 1e30, f32::MIN, f32::MAX, f32::EPSILON] {
            let mut w = BitWriter::new();
            w.write_f32(v).unwrap();
            let buf = w.finish();
            let mut r = BitReader::new(&buf);
            assert_eq!(r.read_f32().unwrap().to_bits(), v.to_bits());
        }
    }

    #[test]
    fn read_past_end_errors() {
        let buf = [0u8; 1];
        let mut r = BitReader::new(&buf);
        assert!(r.read_u16().is_err());
        r.read_u8().unwrap();
        assert!(r.read_bool().is_err());
    }

    #[test]
    fn invalid_bit_count_rejected() {
        let mut w = BitWriter::new();
        assert!(w.write_bits(1, 0).is_err());
        assert!(w.write_bits(1, 65).is_err());
        let buf = [0u8; 8];
        let mut r = BitReader::new(&buf);
        assert!(r.read_bits(0).is_err());
        assert!(r.read_bits(65).is_err());
    }

    #[test]
    fn write_bytes_reports_unaligned_position_as_a_typed_error() {
        let mut w = BitWriter::new();
        w.write_bits(1, 1).unwrap();
        match w.write_bytes(&[1, 2, 3]) {
            Err(Error::UnalignedWrite { bit_pos }) => assert_eq!(bit_pos, 1),
            other => panic!("expected UnalignedWrite, got {other:?}"),
        }
        // Nothing was written: a refused write must not leave a partial field.
        assert_eq!(w.bit_len(), 1);
        w.align();
        w.write_bytes(&[1, 2, 3]).unwrap();
    }

    #[test]
    fn read_bytes_reports_unaligned_position_as_a_typed_error() {
        let buf = [0xFFu8; 4];
        let mut r = BitReader::new(&buf);
        r.read_bits(3).unwrap();
        match r.read_bytes(1) {
            Err(Error::UnalignedRead { bit_pos }) => assert_eq!(bit_pos, 3),
            other => panic!("expected UnalignedRead, got {other:?}"),
        }
    }

    #[test]
    fn align_is_idempotent_and_pads() {
        let mut w = BitWriter::new();
        w.write_bits(0b1, 1).unwrap();
        w.align();
        w.align();
        w.write_u8(0xFF).unwrap();
        assert_eq!(w.finish(), vec![0x01, 0xFF]);
    }

    #[test]
    fn mixed_stream_roundtrip() {
        let mut w = BitWriter::new();
        w.write_bool(true).unwrap();
        w.write_bits(0x3F, 6).unwrap();
        w.write_varint(300).unwrap();
        w.write_bits(0x1, 1).unwrap();
        w.write_f32(3.5).unwrap();
        let buf = w.finish();

        let mut r = BitReader::new(&buf);
        assert!(r.read_bool().unwrap());
        assert_eq!(r.read_bits(6).unwrap(), 0x3F);
        assert_eq!(r.read_varint().unwrap(), 300);
        assert_eq!(r.read_bits(1).unwrap(), 1);
        assert_eq!(r.read_f32().unwrap(), 3.5);
    }

    #[test]
    fn bit_len_tracks_partial_bytes() {
        let mut w = BitWriter::new();
        assert_eq!(w.bit_len(), 0);
        w.write_bits(1, 1).unwrap();
        assert_eq!(w.bit_len(), 1);
        w.write_bits(1, 7).unwrap();
        assert_eq!(w.bit_len(), 8);
        w.write_bits(1, 3).unwrap();
        assert_eq!(w.bit_len(), 11);
        assert!(!w.is_byte_aligned());
        w.align();
        assert!(w.is_byte_aligned());
    }

    /// The test DollsMP never had (analysis/03 §B.5 item 1). A `bool` must survive
    /// a byte-framed container even when the container is preceded by a bit field.
    #[test]
    fn rpc_bool_survives_a_byte_framed_container() {
        let mut w = BitWriter::new();
        w.write_bits(0b101, 3).unwrap();
        w.align();

        write_rpc(&mut w, &true).unwrap();
        write_rpc(&mut w, &false).unwrap();
        // If `bool` were one bit, this next call would be UnalignedWrite and the
        // whole container would be unusable.
        w.write_bytes(&[0xAA]).unwrap();

        let buf = w.finish();
        let mut r = BitReader::new(&buf);
        // Skip the leading 3-bit field, then align as the writer did: the
        // container is byte-framed, so both sides must agree on the boundary.
        assert_eq!(r.read_bits(3).unwrap(), 0b101);
        r.align();
        assert!(read_rpc::<bool>(&mut r).unwrap());
        assert!(!read_rpc::<bool>(&mut r).unwrap());
        assert_eq!(r.read_bytes(1).unwrap(), &[0xAA]);
        assert!(r.is_empty());
    }

    #[test]
    fn bool_payload_is_exactly_one_byte() {
        let mut w = BitWriter::new();
        write_rpc(&mut w, &true).unwrap();
        // varint length 1, then the byte 1
        assert_eq!(w.finish(), vec![1, 1]);
    }

    #[test]
    fn rpc_roundtrips_every_scalar() {
        let mut w = BitWriter::new();
        write_rpc(&mut w, &true).unwrap();
        write_rpc(&mut w, &0x5Au8).unwrap();
        write_rpc(&mut w, &0x1234u16).unwrap();
        write_rpc(&mut w, &-2i16).unwrap();
        write_rpc(&mut w, &0xDEAD_BEEFu32).unwrap();
        write_rpc(&mut w, &-7i32).unwrap();
        write_rpc(&mut w, &0x0102_0304_0506_0708u64).unwrap();
        write_rpc(&mut w, &1.5f32).unwrap();
        write_rpc(&mut w, &String::from("Lamp")).unwrap();
        let buf = w.finish();

        let mut r = BitReader::new(&buf);
        assert!(read_rpc::<bool>(&mut r).unwrap());
        assert_eq!(read_rpc::<u8>(&mut r).unwrap(), 0x5A);
        assert_eq!(read_rpc::<u16>(&mut r).unwrap(), 0x1234);
        assert_eq!(read_rpc::<i16>(&mut r).unwrap(), -2);
        assert_eq!(read_rpc::<u32>(&mut r).unwrap(), 0xDEAD_BEEF);
        assert_eq!(read_rpc::<i32>(&mut r).unwrap(), -7);
        assert_eq!(read_rpc::<u64>(&mut r).unwrap(), 0x0102_0304_0506_0708);
        assert_eq!(read_rpc::<f32>(&mut r).unwrap(), 1.5);
        assert_eq!(read_rpc::<String>(&mut r).unwrap(), "Lamp");
        assert!(r.is_empty());
    }

    #[test]
    fn rpc_rejects_a_length_that_does_not_match_the_value() {
        // Declares 3 bytes for a u8 and pads: the value decodes but leaves bytes.
        let mut w = BitWriter::new();
        w.write_varint(3).unwrap();
        w.write_bytes(&[1, 2, 3]).unwrap();
        let buf = w.finish();
        let mut r = BitReader::new(&buf);
        assert!(matches!(read_rpc::<u8>(&mut r), Err(Error::TrailingBytes { .. })));
    }

    #[test]
    fn rpc_rejects_an_out_of_range_bool() {
        let mut w = BitWriter::new();
        w.write_varint(1).unwrap();
        w.write_bytes(&[2]).unwrap();
        let buf = w.finish();
        let mut r = BitReader::new(&buf);
        assert!(matches!(read_rpc::<bool>(&mut r), Err(Error::OutOfRange { .. })));
    }

    #[test]
    fn rpc_rejects_invalid_utf8() {
        // A `String` payload is its own u16 length plus bytes.
        let mut payload = BitWriter::new();
        payload.write_u16(2).unwrap();
        payload.write_bytes(&[0xFF, 0xFE]).unwrap();
        let inner = payload.finish();

        let mut w = BitWriter::new();
        w.write_varint(inner.len() as u64).unwrap();
        w.write_bytes(&inner).unwrap();
        let buf = w.finish();
        let mut r = BitReader::new(&buf);
        assert_eq!(read_rpc::<String>(&mut r), Err(Error::BadString));
    }
}
