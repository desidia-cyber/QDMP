//! Error type shared by `qd-core` and `qd-relay`.
//!
//! Every variant is reachable from a *decoder*, which means every variant can be
//! produced by a hostile datagram. None of them panics; the relay logs and drops
//! (see `qd-relay`'s `handle_datagram`).

use core::fmt;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Error {
    /// A bit count outside `1..=64` was requested.
    InvalidBitCount(u32),
    /// The reader ran out of data.
    UnexpectedEof { needed: usize, available: usize },
    /// A byte-oriented write was attempted at a non-byte-aligned position.
    ///
    /// Explicit and typed rather than a panic or a silent misalignment: the
    /// DollsMP defect this replaces (analysis/03 §B.5 item 1) reached production
    /// because a bit-width `bool` writer was spliced into a byte-framed container
    /// and nothing said so at the call site.
    UnalignedWrite { bit_pos: usize },
    /// A byte-oriented read was attempted at a non-byte-aligned position.
    UnalignedRead { bit_pos: usize },
    /// A varint did not terminate within 64 bits.
    VarintOverflow,
    /// The datagram does not start with `QDC1`.
    BadMagic([u8; 4]),
    /// The envelope version is not the one this crate implements.
    UnsupportedVersion { ours: u8, theirs: u8 },
    /// A datagram was shorter than its own header claims.
    TruncatedPacket { kind: &'static str, need: usize, got: usize },
    /// A kind byte we do not know (WIRE.md §2).
    UnknownKind(u8),
    /// A `INTENT` payload kind we do not know (WIRE.md §3.10).
    UnknownIntentKind(u8),
    /// A string field was not valid UTF-8.
    BadString,
    /// A collection or field exceeded a bound that keeps a hostile peer cheap.
    LimitExceeded { what: &'static str, limit: usize, got: usize },
    /// A body / payload declared more bytes than it consumed, or fewer.
    TrailingBytes { what: &'static str, got: usize },
    /// A sequence number arrived outside the acceptable window.
    SequenceOutOfWindow { seq: u32, base: u32, window: usize },
    /// Handshake-level refusal that has no more specific variant.
    Handshake(&'static str),
    /// A value outside the range the wire format allows.
    OutOfRange { what: &'static str, value: i64 },
    /// The connection was closed.
    Closed,
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Error::InvalidBitCount(b) => write!(f, "invalid bit count {b} (must be 1..=64)"),
            Error::UnexpectedEof { needed, available } => {
                write!(f, "unexpected end of data: needed {needed} bits, {available} available")
            }
            Error::UnalignedWrite { bit_pos } => {
                write!(f, "byte write at bit position {bit_pos}, which is not byte-aligned")
            }
            Error::UnalignedRead { bit_pos } => {
                write!(f, "byte read at bit position {bit_pos}, which is not byte-aligned")
            }
            Error::VarintOverflow => write!(f, "varint did not terminate within 64 bits"),
            Error::BadMagic(m) => write!(f, "bad magic {m:02X?}, expected QDC1"),
            Error::UnsupportedVersion { ours, theirs } => {
                write!(f, "unsupported wire version: ours {ours}, theirs {theirs}")
            }
            Error::TruncatedPacket { kind, need, got } => {
                write!(f, "truncated {kind}: need {need} bytes, got {got}")
            }
            Error::UnknownKind(k) => write!(f, "unknown packet kind {k:#04X}"),
            Error::UnknownIntentKind(k) => write!(f, "unknown intent kind {k}"),
            Error::BadString => write!(f, "string field is not valid UTF-8"),
            Error::LimitExceeded { what, limit, got } => {
                write!(f, "{what} exceeds limit: {got} > {limit}")
            }
            Error::TrailingBytes { what, got } => {
                write!(f, "{what} has {got} byte(s) left over after decoding")
            }
            Error::SequenceOutOfWindow { seq, base, window } => {
                write!(f, "sequence {seq} outside window base={base} window={window}")
            }
            Error::Handshake(m) => write!(f, "handshake failed: {m}"),
            Error::OutOfRange { what, value } => write!(f, "{what} out of range: {value}"),
            Error::Closed => write!(f, "connection closed"),
        }
    }
}

impl std::error::Error for Error {}

pub type Result<T> = core::result::Result<T, Error>;

#[cfg(test)]
mod tests {
    use super::*;

    /// These strings end up in relay logs, so a `Debug` fallback is not good
    /// enough: every variant must render as a sentence.
    #[test]
    fn display_covers_every_variant() {
        let all = [
            Error::InvalidBitCount(0),
            Error::UnexpectedEof { needed: 8, available: 0 },
            Error::UnalignedWrite { bit_pos: 3 },
            Error::UnalignedRead { bit_pos: 5 },
            Error::VarintOverflow,
            Error::BadMagic(*b"XDC1"),
            Error::UnsupportedVersion { ours: 1, theirs: 2 },
            Error::TruncatedPacket { kind: "body", need: 30, got: 2 },
            Error::UnknownKind(0x77),
            Error::UnknownIntentKind(9),
            Error::BadString,
            Error::LimitExceeded { what: "body", limit: 1200, got: 1201 },
            Error::TrailingBytes { what: "player_row", got: 4 },
            Error::SequenceOutOfWindow { seq: 1, base: 2, window: 64 },
            Error::Handshake("build_hash mismatch"),
            Error::OutOfRange { what: "intent mode", value: 7 },
            Error::Closed,
        ];
        for e in all {
            let s = e.to_string();
            assert!(!s.is_empty(), "empty display for {e:?}");
            assert!(!s.contains("Error::"), "raw Debug leaked into display: {s}");
        }
    }

    #[test]
    fn error_is_send_sync() {
        fn assert_send_sync<T: Send + Sync>() {}
        assert_send_sync::<Error>();
    }
}
