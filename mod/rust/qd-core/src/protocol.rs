//! QDCOOP wire format, version 1.
//!
//! Implemented field-for-field from `mod/doc/WIRE.md`. The layout is the
//! authoritative artifact: `qd-core`'s tests write `mod/doc/vectors.txt` and the
//! native self-test consumes it, so a drift between this file and
//! `native/src/qdwire.h` fails a test instead of corrupting a session.
//!
//! Fixed properties, restated here because each one is load-bearing:
//!
//! * Little-endian throughout, no padding, every field packed (WIRE.md §0).
//! * The 16-byte envelope is byte-aligned so a relay can peek without a parse.
//! * One envelope per datagram, `body_len <= 1200`, never fragmented (WIRE.md §1).
//! * State kinds carry the *complete* set, never a delta (WIRE.md §2, rule E8).
//!
//! Fixes `analysis/03-dollsmp-reuse.md` §B.5 item 5: `Envelope::encode`
//! re-validates the body length rather than casting `len as u16`, so a hand-built
//! body cannot bypass the header check and wrap the length field.
//!
//! Deviation, recorded because WIRE.md does not settle it: §2 lists kind `0x30`
//! `LOG` but §3 defines no body for it. It is carried as an opaque body
//! ([`Log`]) so the relay can forward it verbatim; nothing interprets it.

use crate::bitstream::{BitReader, BitWriter};
use crate::error::{Error, Result};
use crate::quantize;

/// `QDC1`, the first four bytes of every datagram.
pub const MAGIC: [u8; 4] = *b"QDC1";
/// Envelope version this crate implements.
pub const VERSION: u8 = 1;
/// Fixed envelope size in bytes.
pub const HEADER_LEN: usize = 16;
/// Maximum body length (WIRE.md §1: `16 + 1200 = 1216 B` of UDP payload).
pub const MAX_BODY: usize = 1200;
/// At most 20 enemy rows per datagram at N=2 (WIRE.md §3.9 row budget).
pub const MAX_ENEMY_ROWS: usize = 20;
/// `DIALOGUE.slot` value meaning "no player holds the conversation".
pub const DIALOGUE_SLOT_FREE: u8 = 0xFF;

/// A 16-byte game `FGuid`, sent exactly as it sits in memory.
///
/// WIRE.md §0: `A:u32, B:u32, C:u32, D:u32` little-endian in memory, i.e. the
/// bytes read out of an `FS_EnemySaveState` key. No re-ordering and no string
/// form, because both sides can produce those bytes cheaply and neither can
/// produce the other's text form identically.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Default)]
pub struct Guid(pub [u8; 16]);

impl Guid {
    pub const ZERO: Guid = Guid([0u8; 16]);

    pub fn from_bytes(bytes: [u8; 16]) -> Self {
        Guid(bytes)
    }

    pub fn as_bytes(&self) -> &[u8; 16] {
        &self.0
    }

    /// Build from the four `u32` fields, writing each little-endian — which is
    /// exactly the memory layout WIRE.md §0 requires.
    pub fn from_fields(a: u32, b: u32, c: u32, d: u32) -> Self {
        let mut out = [0u8; 16];
        out[0..4].copy_from_slice(&a.to_le_bytes());
        out[4..8].copy_from_slice(&b.to_le_bytes());
        out[8..12].copy_from_slice(&c.to_le_bytes());
        out[12..16].copy_from_slice(&d.to_le_bytes());
        Guid(out)
    }

    pub fn fields(&self) -> (u32, u32, u32, u32) {
        let f = |i: usize| {
            u32::from_le_bytes([self.0[i], self.0[i + 1], self.0[i + 2], self.0[i + 3]])
        };
        (f(0), f(4), f(8), f(12))
    }
}

/// Envelope kind byte (WIRE.md §2).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[repr(u8)]
pub enum Kind {
    Hello = 0x01,
    Welcome = 0x02,
    Keepalive = 0x03,
    Ack = 0x04,
    PlayerRow = 0x10,
    WorldFlags = 0x11,
    ActorStates = 0x12,
    Inventory = 0x13,
    EnemyRows = 0x14,
    Intent = 0x20,
    DialogueState = 0x21,
    LevelUnion = 0x22,
    Log = 0x30,
}

impl Kind {
    pub fn from_u8(v: u8) -> Result<Self> {
        use Kind::*;
        Ok(match v {
            0x01 => Hello,
            0x02 => Welcome,
            0x03 => Keepalive,
            0x04 => Ack,
            0x10 => PlayerRow,
            0x11 => WorldFlags,
            0x12 => ActorStates,
            0x13 => Inventory,
            0x14 => EnemyRows,
            0x20 => Intent,
            0x21 => DialogueState,
            0x22 => LevelUnion,
            0x30 => Log,
            other => return Err(Error::UnknownKind(other)),
        })
    }

    pub fn as_u8(self) -> u8 {
        self as u8
    }

    pub fn name(self) -> &'static str {
        use Kind::*;
        match self {
            Hello => "hello",
            Welcome => "welcome",
            Keepalive => "keepalive",
            Ack => "ack",
            PlayerRow => "player_row",
            WorldFlags => "world_flags",
            ActorStates => "actor_states",
            Inventory => "inventory",
            EnemyRows => "enemy_rows",
            Intent => "intent",
            DialogueState => "dialogue_state",
            LevelUnion => "level_union",
            Log => "log",
        }
    }

    /// Reliable kinds (WIRE.md §2): retried until acked by the sender.
    pub fn is_reliable(self) -> bool {
        matches!(self, Kind::Hello | Kind::Welcome | Kind::Intent)
    }

    /// State kinds: unreliable, absolute, and superseded by the next one.
    ///
    /// Losing one costs a sample; the next repairs it. This is why nothing in the
    /// latest-wins lane is ever retained for retransmission (WIRE.md §4).
    pub fn is_latest_wins(self) -> bool {
        matches!(
            self,
            Kind::PlayerRow
                | Kind::WorldFlags
                | Kind::ActorStates
                | Kind::Inventory
                | Kind::EnemyRows
                | Kind::DialogueState
                | Kind::LevelUnion
        )
    }
}

/// `HELLO` body (WIRE.md §3.1).
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct Hello {
    pub proto: u8,
    /// First four bytes of the image SHA-256, little-endian.
    pub build_hash: u32,
    /// 0 = any, 1 = host, 2 = peer.
    pub role_wanted: u8,
    pub save_level: String,
    pub display_name: String,
}

/// `WELCOME` body (WIRE.md §3.2).
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct Welcome {
    /// 1 = host, 2 = peer.
    pub role_assigned: u8,
    pub session: u32,
    pub host_tick: u32,
    pub save_level: String,
    pub flags_rev: u32,
}

/// `ACK` body (WIRE.md §3.4). A single cumulative sequence number.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct Ack {
    pub seq: u32,
}

/// `PLAYER` body (WIRE.md §3.5). 30 bytes on the wire.
///
/// All kinematic fields are already in wire units; [`PlayerRow::from_world`] and
/// the accessors convert to and from the game's centimetres and degrees.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct PlayerRow {
    /// 0 = host, 1 = peer.
    pub slot: u8,
    pub sample_ms: u32,
    pub x: i32,
    pub y: i32,
    pub z: i32,
    /// Units of 1/65536 turn.
    pub yaw: i16,
    /// Units of 1/65536 turn.
    pub pitch: i16,
    /// Units of 1/8 cm/s, saturated to `i16`.
    pub vx: i16,
    pub vy: i16,
    pub vz: i16,
    /// Mirror of the game's movement mode enum; only 0 = none is mapped.
    pub move_mode: u8,
    pub action: u8,
    /// See [`player_flags`].
    pub flags: u8,
}

/// `PLAYER.flags` bits (WIRE.md §3.5).
pub mod player_flags {
    pub const GROUNDED: u8 = 1 << 0;
    pub const AIMING: u8 = 1 << 1;
    pub const SPRINTING: u8 = 1 << 2;
    pub const CROUCHED: u8 = 1 << 3;
    pub const DIALOGUE: u8 = 1 << 4;
    pub const MENU: u8 = 1 << 5;
}

/// `PLAYER.action` values (WIRE.md §3.5).
pub mod player_action {
    pub const IDLE: u8 = 0;
    pub const ATTACK: u8 = 1;
    pub const HURT: u8 = 2;
    pub const DYING: u8 = 3;
    pub const DEAD: u8 = 4;
}

/// `ENEMY.flags` bits (WIRE.md §3.9).
pub mod enemy_flags {
    pub const ALIVE: u8 = 1 << 0;
    /// Peer copy with AI disabled.
    pub const DRIVEN: u8 = 1 << 1;
    pub const HAS_BODY: u8 = 1 << 2;
}

/// `HELLO.role_wanted` / `WELCOME.role_assigned` values.
pub mod roles {
    pub const ANY: u8 = 0;
    pub const HOST: u8 = 1;
    pub const PEER: u8 = 2;
}

/// `PLAYER.slot` values.
pub mod slots {
    pub const HOST: u8 = 0;
    pub const PEER: u8 = 1;
}

impl PlayerRow {
    /// Build a row from the game's units, applying the WIRE.md quantisers.
    pub fn from_world(
        slot: u8,
        sample_ms: u32,
        position_cm: [f32; 3],
        angles_deg: [f32; 3],
        velocity_cm_s: [f32; 3],
        move_mode: u8,
        action: u8,
        flags: u8,
    ) -> Self {
        let p = quantize::position_to_wire3(position_cm);
        let a = quantize::angles_to_wire([angles_deg[1], angles_deg[0]]);
        let v = quantize::velocity_to_wire3(velocity_cm_s);
        Self {
            slot,
            sample_ms,
            x: p[0],
            y: p[1],
            z: p[2],
            yaw: a[0],
            pitch: a[1],
            vx: v[0],
            vy: v[1],
            vz: v[2],
            move_mode,
            action,
            flags,
        }
    }

    pub fn position_cm(&self) -> [f32; 3] {
        quantize::position_from_wire3([self.x, self.y, self.z])
    }

    /// `[pitch, yaw, roll]` in degrees. `PLAYER` carries no roll, so it is 0.
    pub fn angles_deg(&self) -> [f32; 3] {
        let a = quantize::angles_from_wire([self.pitch, self.yaw]);
        [a[0], a[1], 0.0]
    }

    pub fn velocity_cm_s(&self) -> [f32; 3] {
        quantize::velocity_from_wire3([self.vx, self.vy, self.vz])
    }

    pub fn has_flag(&self, flag: u8) -> bool {
        self.flags & flag != 0
    }
}

/// One `WORLD_FLAGS` entry (WIRE.md §3.6).
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct FlagEntry {
    pub name: String,
    pub value: i32,
}

/// `WORLD_FLAGS` body (WIRE.md §3.6). The complete set, ordered by key.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct WorldFlags {
    pub rev: u32,
    pub entries: Vec<FlagEntry>,
}

/// One `ACTOR_STATES` entry (WIRE.md §3.7).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct ActorEntry {
    pub actor: Guid,
    pub value: i32,
}

/// `ACTOR_STATES` body (WIRE.md §3.7).
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct ActorStates {
    pub rev: u32,
    pub entries: Vec<ActorEntry>,
}

/// One `INVENTORY` entry (WIRE.md §3.8).
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct ItemEntry {
    /// The soft object path the save stores, e.g.
    /// `/Game/Items/KeyItems/AquaDoorKey_2.AquaDoorKey_2`.
    pub item_path: String,
    pub count: i32,
}

/// `INVENTORY` body (WIRE.md §3.8).
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct Inventory {
    pub rev: u32,
    pub entries: Vec<ItemEntry>,
}

/// One `ENEMY` row (WIRE.md §3.9). 33 bytes on the wire.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct EnemyRow {
    pub id: Guid,
    pub x: i32,
    pub y: i32,
    pub z: i32,
    pub yaw: i16,
    pub hp: u16,
    /// See [`enemy_flags`].
    pub flags: u8,
}

/// `ENEMY` body (WIRE.md §3.9).
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct EnemyRows {
    pub rows: Vec<EnemyRow>,
}

/// `INTENT` payload, by kind (WIRE.md §3.10).
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum IntentPayload {
    /// kind 1
    SetFlag { name: String, value: i32, mode: u8 },
    /// kind 2
    GrantItem { item_path: String, count: i32 },
    /// kind 3
    ConsumeItem { item_path: String, count: i32 },
    /// kind 4
    DialogueAnswer { npc: Guid, line_index: u16, option_index: u16 },
    /// kind 5
    Checkpoint { save_point_path: String },
    /// kind 6
    ActorStateSet { actor: Guid, value: i32 },
}

impl IntentPayload {
    pub fn kind(&self) -> u8 {
        match self {
            IntentPayload::SetFlag { .. } => 1,
            IntentPayload::GrantItem { .. } => 2,
            IntentPayload::ConsumeItem { .. } => 3,
            IntentPayload::DialogueAnswer { .. } => 4,
            IntentPayload::Checkpoint { .. } => 5,
            IntentPayload::ActorStateSet { .. } => 6,
        }
    }
}

/// `INTENT` body (WIRE.md §3.10).
///
/// `based_on_rev` is the acceptance gate: the host applies the intent only if it is
/// at least the revision of the last state the host sent, which makes a double
/// pickup structurally impossible rather than unlikely.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Intent {
    pub based_on_rev: u32,
    pub payload: IntentPayload,
}

/// `DIALOGUE` body (WIRE.md §3.11).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct DialogueState {
    /// Increments per state change; a lower `seq` is discarded.
    pub seq: u32,
    pub npc: Guid,
    /// Which player holds the conversation, 0 or 1; 0xFF = free.
    pub slot: u8,
    pub line: u16,
    pub entry: u16,
}

/// `LEVEL_UNION` body (WIRE.md §3.12).
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct LevelUnion {
    /// One bit per index into the sender's own visible-sublevel list, LSB-first
    /// within each byte — the same bit order as [`crate::bitstream`], which is the
    /// only order stated anywhere in this crate. WIRE.md does not name the order;
    /// LSB-first is chosen so the whole codebase has one convention.
    pub bits: Vec<u8>,
    /// Present on the first send only; the receiver keeps the last list per slot.
    pub names: Option<Vec<String>>,
}

impl LevelUnion {
    pub fn bit(&self, index: usize) -> bool {
        let byte = index / 8;
        match self.bits.get(byte) {
            Some(b) => b & (1 << (index % 8)) != 0,
            None => false,
        }
    }

    pub fn set_bit(&mut self, index: usize, value: bool) {
        let byte = index / 8;
        if value && byte >= self.bits.len() {
            self.bits.resize(byte + 1, 0);
        }
        if let Some(b) = self.bits.get_mut(byte) {
            if value {
                *b |= 1 << (index % 8);
            } else {
                *b &= !(1 << (index % 8));
            }
        }
    }
}

/// A `LOG` body.
///
/// WIRE.md §2 lists kind `0x30` but §3 defines no layout, so the body is carried
/// verbatim and never interpreted. This is the one place the crate invents
/// nothing: whatever the native side puts here is what crosses.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct Log {
    pub bytes: Vec<u8>,
}

/// A decoded body, tagged by its kind.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Body {
    Hello(Hello),
    Welcome(Welcome),
    Keepalive,
    Ack(Ack),
    PlayerRow(PlayerRow),
    WorldFlags(WorldFlags),
    ActorStates(ActorStates),
    Inventory(Inventory),
    EnemyRows(EnemyRows),
    Intent(Intent),
    DialogueState(DialogueState),
    LevelUnion(LevelUnion),
    Log(Log),
}

impl Body {
    pub fn kind(&self) -> Kind {
        match self {
            Body::Hello(_) => Kind::Hello,
            Body::Welcome(_) => Kind::Welcome,
            Body::Keepalive => Kind::Keepalive,
            Body::Ack(_) => Kind::Ack,
            Body::PlayerRow(_) => Kind::PlayerRow,
            Body::WorldFlags(_) => Kind::WorldFlags,
            Body::ActorStates(_) => Kind::ActorStates,
            Body::Inventory(_) => Kind::Inventory,
            Body::EnemyRows(_) => Kind::EnemyRows,
            Body::Intent(_) => Kind::Intent,
            Body::DialogueState(_) => Kind::DialogueState,
            Body::LevelUnion(_) => Kind::LevelUnion,
            Body::Log(_) => Kind::Log,
        }
    }
}

/// The fixed 16-byte datagram header (WIRE.md §1).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Header {
    pub ver: u8,
    pub kind: Kind,
    pub body_len: u16,
    pub session: u32,
    pub seq: u32,
}

impl Header {
    /// Serialize into exactly [`HEADER_LEN`] bytes, little-endian.
    pub fn encode(&self) -> Result<[u8; HEADER_LEN]> {
        if self.ver != VERSION {
            return Err(Error::UnsupportedVersion { ours: VERSION, theirs: self.ver });
        }
        if usize::from(self.body_len) > MAX_BODY {
            return Err(Error::LimitExceeded {
                what: "body",
                limit: MAX_BODY,
                got: usize::from(self.body_len),
            });
        }
        let mut out = [0u8; HEADER_LEN];
        out[0..4].copy_from_slice(&MAGIC);
        out[4] = self.ver;
        out[5] = self.kind.as_u8();
        out[6..8].copy_from_slice(&self.body_len.to_le_bytes());
        out[8..12].copy_from_slice(&self.session.to_le_bytes());
        out[12..16].copy_from_slice(&self.seq.to_le_bytes());
        Ok(out)
    }

    /// Parse and validate magic, version, kind and the body-length budget.
    pub fn decode(buf: &[u8]) -> Result<Self> {
        if buf.len() < HEADER_LEN {
            return Err(Error::TruncatedPacket { kind: "header", need: HEADER_LEN, got: buf.len() });
        }
        let magic = [buf[0], buf[1], buf[2], buf[3]];
        if magic != MAGIC {
            return Err(Error::BadMagic(magic));
        }
        let ver = buf[4];
        if ver != VERSION {
            return Err(Error::UnsupportedVersion { ours: VERSION, theirs: ver });
        }
        let kind = Kind::from_u8(buf[5])?;
        let body_len = u16::from_le_bytes([buf[6], buf[7]]);
        if usize::from(body_len) > MAX_BODY {
            return Err(Error::LimitExceeded {
                what: "body",
                limit: MAX_BODY,
                got: usize::from(body_len),
            });
        }
        let session = u32::from_le_bytes([buf[8], buf[9], buf[10], buf[11]]);
        let seq = u32::from_le_bytes([buf[12], buf[13], buf[14], buf[15]]);
        Ok(Header { ver, kind, body_len, session, seq })
    }
}

/// One complete datagram: envelope plus body.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Envelope {
    pub session: u32,
    pub seq: u32,
    pub body: Body,
}

impl Envelope {
    pub fn new(session: u32, seq: u32, body: Body) -> Self {
        Self { session, seq, body }
    }

    pub fn kind(&self) -> Kind {
        self.body.kind()
    }

    /// Encode the whole datagram.
    ///
    /// §B.5 item 5: the length is **re-validated** here rather than cast. In
    /// DollsMP a `Packet` with public fields could carry more than `MAX_PAYLOAD`
    /// and `len as u16` silently wrapped the header's length, producing a datagram
    /// that decoded into a different body.
    pub fn encode(&self) -> Result<Vec<u8>> {
        let body = encode_body(&self.body)?;
        if body.len() > MAX_BODY {
            return Err(Error::LimitExceeded {
                what: "body",
                limit: MAX_BODY,
                got: body.len(),
            });
        }
        let body_len = u16::try_from(body.len()).map_err(|_| Error::LimitExceeded {
            what: "body",
            limit: u16::MAX as usize,
            got: body.len(),
        })?;
        let header = Header {
            ver: VERSION,
            kind: self.body.kind(),
            body_len,
            session: self.session,
            seq: self.seq,
        }
        .encode()?;

        let mut out = Vec::with_capacity(HEADER_LEN + body.len());
        out.extend_from_slice(&header);
        out.extend_from_slice(&body);
        Ok(out)
    }

    /// Decode a whole datagram.
    ///
    /// Extra bytes past the declared body are rejected rather than ignored: a
    /// datagram whose length disagrees with its own header is a bug or an attack,
    /// and silently forwarding it is how a relay launders corruption.
    pub fn decode(buf: &[u8]) -> Result<Self> {
        let header = Header::decode(buf)?;
        let need = HEADER_LEN + usize::from(header.body_len);
        if buf.len() < need {
            return Err(Error::TruncatedPacket { kind: "body", need, got: buf.len() });
        }
        if buf.len() > need {
            return Err(Error::TrailingBytes { what: "datagram", got: buf.len() - need });
        }
        let body = decode_body(header.kind, &buf[HEADER_LEN..need])?;
        Ok(Envelope { session: header.session, seq: header.seq, body })
    }
}

/// `str`: `u16 byte_len` followed by UTF-8 bytes (WIRE.md §0). No NUL.
fn write_str(w: &mut BitWriter, s: &str) -> Result<()> {
    let bytes = s.as_bytes();
    if bytes.len() > u16::MAX as usize {
        return Err(Error::LimitExceeded {
            what: "string",
            limit: u16::MAX as usize,
            got: bytes.len(),
        });
    }
    w.write_u16(bytes.len() as u16)?;
    w.write_bytes(bytes)
}

/// Read a `str`.
///
/// The declared length is checked against the *remaining body bytes* before it is
/// used, so a hostile `0xFFFF` is an `UnexpectedEof` and never an allocation of
/// that size.
fn read_str(r: &mut BitReader<'_>) -> Result<String> {
    let len = usize::from(r.read_u16()?);
    let bytes = r.read_bytes(len)?;
    core::str::from_utf8(bytes).map(str::to_owned).map_err(|_| Error::BadString)
}

/// `bytes`: `u16 byte_len` followed by raw bytes (used by `LEVEL_UNION.bits`).
fn write_raw(w: &mut BitWriter, data: &[u8]) -> Result<()> {
    if data.len() > u16::MAX as usize {
        return Err(Error::LimitExceeded {
            what: "byte field",
            limit: u16::MAX as usize,
            got: data.len(),
        });
    }
    w.write_u16(data.len() as u16)?;
    w.write_bytes(data)
}

fn read_raw(r: &mut BitReader<'_>) -> Result<Vec<u8>> {
    let len = usize::from(r.read_u16()?);
    Ok(r.read_bytes(len)?.to_vec())
}

fn write_guid(w: &mut BitWriter, g: &Guid) -> Result<()> {
    w.write_bytes(&g.0)
}

fn read_guid(r: &mut BitReader<'_>) -> Result<Guid> {
    let bytes = r.read_bytes(16)?;
    let mut out = [0u8; 16];
    out.copy_from_slice(bytes);
    Ok(Guid(out))
}

/// Encode a body into its wire bytes (no envelope).
pub fn encode_body(body: &Body) -> Result<Vec<u8>> {
    let mut w = BitWriter::with_capacity(64);
    match body {
        Body::Hello(h) => {
            if h.role_wanted > roles::PEER {
                return Err(Error::OutOfRange {
                    what: "hello role_wanted",
                    value: i64::from(h.role_wanted),
                });
            }
            w.write_u8(h.proto)?;
            w.write_u32(h.build_hash)?;
            w.write_u8(h.role_wanted)?;
            write_str(&mut w, &h.save_level)?;
            // WIRE.md §3.1 spells this `u16 display_name_len` + `bytes`, which is
            // byte-identical to `str` for a UTF-8 name; validating it as text is
            // the safe choice because a non-UTF-8 name has no display meaning.
            write_str(&mut w, &h.display_name)?;
        }
        Body::Welcome(m) => {
            if m.role_assigned != roles::HOST && m.role_assigned != roles::PEER {
                return Err(Error::OutOfRange {
                    what: "welcome role_assigned",
                    value: i64::from(m.role_assigned),
                });
            }
            w.write_u8(m.role_assigned)?;
            w.write_u32(m.session)?;
            w.write_u32(m.host_tick)?;
            write_str(&mut w, &m.save_level)?;
            w.write_u32(m.flags_rev)?;
        }
        Body::Keepalive => {}
        Body::Ack(a) => w.write_u32(a.seq)?,
        Body::PlayerRow(p) => {
            if p.slot > slots::PEER {
                return Err(Error::OutOfRange {
                    what: "player slot",
                    value: i64::from(p.slot),
                });
            }
            w.write_u8(p.slot)?;
            w.write_u32(p.sample_ms)?;
            w.write_i32(p.x)?;
            w.write_i32(p.y)?;
            w.write_i32(p.z)?;
            w.write_i16(p.yaw)?;
            w.write_i16(p.pitch)?;
            w.write_i16(p.vx)?;
            w.write_i16(p.vy)?;
            w.write_i16(p.vz)?;
            w.write_u8(p.move_mode)?;
            w.write_u8(p.action)?;
            w.write_u8(p.flags)?;
        }
        Body::WorldFlags(f) => {
            w.write_u32(f.rev)?;
            write_count(&mut w, f.entries.len())?;
            for e in &f.entries {
                write_str(&mut w, &e.name)?;
                w.write_i32(e.value)?;
            }
        }
        Body::ActorStates(s) => {
            w.write_u32(s.rev)?;
            write_count(&mut w, s.entries.len())?;
            for e in &s.entries {
                write_guid(&mut w, &e.actor)?;
                w.write_i32(e.value)?;
            }
        }
        Body::Inventory(i) => {
            w.write_u32(i.rev)?;
            write_count(&mut w, i.entries.len())?;
            for e in &i.entries {
                write_str(&mut w, &e.item_path)?;
                w.write_i32(e.count)?;
            }
        }
        Body::EnemyRows(e) => {
            if e.rows.len() > MAX_ENEMY_ROWS {
                return Err(Error::LimitExceeded {
                    what: "enemy rows per datagram",
                    limit: MAX_ENEMY_ROWS,
                    got: e.rows.len(),
                });
            }
            w.write_u16(e.rows.len() as u16)?;
            for row in &e.rows {
                write_guid(&mut w, &row.id)?;
                w.write_i32(row.x)?;
                w.write_i32(row.y)?;
                w.write_i32(row.z)?;
                w.write_i16(row.yaw)?;
                w.write_u16(row.hp)?;
                w.write_u8(row.flags)?;
            }
        }
        Body::Intent(it) => {
            w.write_u8(it.payload.kind())?;
            w.write_u32(it.based_on_rev)?;
            match &it.payload {
                IntentPayload::SetFlag { name, value, mode } => {
                    if *mode != crate::flags::MODE_ADDITIVE
                        && *mode != crate::flags::MODE_ABSOLUTE
                    {
                        return Err(Error::OutOfRange {
                            what: "intent mode",
                            value: i64::from(*mode),
                        });
                    }
                    write_str(&mut w, name)?;
                    w.write_i32(*value)?;
                    w.write_u8(*mode)?;
                }
                IntentPayload::GrantItem { item_path, count }
                | IntentPayload::ConsumeItem { item_path, count } => {
                    write_str(&mut w, item_path)?;
                    w.write_i32(*count)?;
                }
                IntentPayload::DialogueAnswer { npc, line_index, option_index } => {
                    write_guid(&mut w, npc)?;
                    w.write_u16(*line_index)?;
                    w.write_u16(*option_index)?;
                }
                IntentPayload::Checkpoint { save_point_path } => {
                    write_str(&mut w, save_point_path)?;
                }
                IntentPayload::ActorStateSet { actor, value } => {
                    write_guid(&mut w, actor)?;
                    w.write_i32(*value)?;
                }
            }
        }
        Body::DialogueState(d) => {
            w.write_u32(d.seq)?;
            write_guid(&mut w, &d.npc)?;
            w.write_u8(d.slot)?;
            w.write_u16(d.line)?;
            w.write_u16(d.entry)?;
        }
        Body::LevelUnion(u) => {
            write_raw(&mut w, &u.bits)?;
            match &u.names {
                None => w.write_u8(0)?,
                Some(names) => {
                    w.write_u8(1)?;
                    write_count(&mut w, names.len())?;
                    for n in names {
                        write_str(&mut w, n)?;
                    }
                }
            }
        }
        Body::Log(l) => w.write_bytes(&l.bytes)?,
    }

    if !w.is_byte_aligned() {
        // Every WIRE.md field is a whole number of bytes, so this is an
        // implementation bug, not a peer condition — but it must be loud rather
        // than silent (§B.5 item 1's lesson).
        return Err(Error::UnalignedWrite { bit_pos: w.bit_len() });
    }
    Ok(w.finish())
}

fn write_count(w: &mut BitWriter, n: usize) -> Result<()> {
    let n = u16::try_from(n)
        .map_err(|_| Error::LimitExceeded { what: "entry count", limit: u16::MAX as usize, got: n })?;
    w.write_u16(n)
}

/// Decode a body of a known kind.
///
/// The body must be consumed exactly; anything left over is a framing error.
pub fn decode_body(kind: Kind, bytes: &[u8]) -> Result<Body> {
    // `LOG` has no declared layout (WIRE.md defines none), so the whole body is
    // the message and there is nothing to leave over.
    if kind == Kind::Log {
        return Ok(Body::Log(Log { bytes: bytes.to_vec() }));
    }
    let mut r = BitReader::new(bytes);
    let body = match kind {
        Kind::Hello => {
            let proto = r.read_u8()?;
            let build_hash = r.read_u32()?;
            let role_wanted = r.read_u8()?;
            if role_wanted > roles::PEER {
                return Err(Error::OutOfRange {
                    what: "hello role_wanted",
                    value: i64::from(role_wanted),
                });
            }
            Body::Hello(Hello {
                proto,
                build_hash,
                role_wanted,
                save_level: read_str(&mut r)?,
                display_name: read_str(&mut r)?,
            })
        }
        Kind::Welcome => {
            let role_assigned = r.read_u8()?;
            if role_assigned != roles::HOST && role_assigned != roles::PEER {
                return Err(Error::OutOfRange {
                    what: "welcome role_assigned",
                    value: i64::from(role_assigned),
                });
            }
            Body::Welcome(Welcome {
                role_assigned,
                session: r.read_u32()?,
                host_tick: r.read_u32()?,
                save_level: read_str(&mut r)?,
                flags_rev: r.read_u32()?,
            })
        }
        Kind::Keepalive => Body::Keepalive,
        Kind::Ack => Body::Ack(Ack { seq: r.read_u32()? }),
        Kind::PlayerRow => {
            let slot = r.read_u8()?;
            if slot > slots::PEER {
                return Err(Error::OutOfRange { what: "player slot", value: i64::from(slot) });
            }
            Body::PlayerRow(PlayerRow {
                slot,
                sample_ms: r.read_u32()?,
                x: r.read_i32()?,
                y: r.read_i32()?,
                z: r.read_i32()?,
                yaw: r.read_i16()?,
                pitch: r.read_i16()?,
                vx: r.read_i16()?,
                vy: r.read_i16()?,
                vz: r.read_i16()?,
                move_mode: r.read_u8()?,
                action: r.read_u8()?,
                flags: r.read_u8()?,
            })
        }
        Kind::WorldFlags => {
            let rev = r.read_u32()?;
            let n = r.read_u16()? as usize;
            let mut entries = Vec::new();
            for _ in 0..n {
                let name = read_str(&mut r)?;
                let value = r.read_i32()?;
                entries.push(FlagEntry { name, value });
            }
            Body::WorldFlags(WorldFlags { rev, entries })
        }
        Kind::ActorStates => {
            let rev = r.read_u32()?;
            let n = r.read_u16()? as usize;
            let mut entries = Vec::new();
            for _ in 0..n {
                let actor = read_guid(&mut r)?;
                let value = r.read_i32()?;
                entries.push(ActorEntry { actor, value });
            }
            Body::ActorStates(ActorStates { rev, entries })
        }
        Kind::Inventory => {
            let rev = r.read_u32()?;
            let n = r.read_u16()? as usize;
            let mut entries = Vec::new();
            for _ in 0..n {
                let item_path = read_str(&mut r)?;
                let count = r.read_i32()?;
                entries.push(ItemEntry { item_path, count });
            }
            Body::Inventory(Inventory { rev, entries })
        }
        Kind::EnemyRows => {
            let n = r.read_u16()? as usize;
            if n > MAX_ENEMY_ROWS {
                return Err(Error::LimitExceeded {
                    what: "enemy rows per datagram",
                    limit: MAX_ENEMY_ROWS,
                    got: n,
                });
            }
            let mut rows = Vec::new();
            for _ in 0..n {
                rows.push(EnemyRow {
                    id: read_guid(&mut r)?,
                    x: r.read_i32()?,
                    y: r.read_i32()?,
                    z: r.read_i32()?,
                    yaw: r.read_i16()?,
                    hp: r.read_u16()?,
                    flags: r.read_u8()?,
                });
            }
            Body::EnemyRows(EnemyRows { rows })
        }
        Kind::Intent => {
            let kind_byte = r.read_u8()?;
            let based_on_rev = r.read_u32()?;
            let payload = match kind_byte {
                1 => {
                    let name = read_str(&mut r)?;
                    let value = r.read_i32()?;
                    let mode = r.read_u8()?;
                    if mode != crate::flags::MODE_ADDITIVE && mode != crate::flags::MODE_ABSOLUTE {
                        return Err(Error::OutOfRange {
                            what: "intent mode",
                            value: i64::from(mode),
                        });
                    }
                    IntentPayload::SetFlag { name, value, mode }
                }
                2 => IntentPayload::GrantItem {
                    item_path: read_str(&mut r)?,
                    count: r.read_i32()?,
                },
                3 => IntentPayload::ConsumeItem {
                    item_path: read_str(&mut r)?,
                    count: r.read_i32()?,
                },
                4 => IntentPayload::DialogueAnswer {
                    npc: read_guid(&mut r)?,
                    line_index: r.read_u16()?,
                    option_index: r.read_u16()?,
                },
                5 => IntentPayload::Checkpoint { save_point_path: read_str(&mut r)? },
                6 => IntentPayload::ActorStateSet {
                    actor: read_guid(&mut r)?,
                    value: r.read_i32()?,
                },
                other => return Err(Error::UnknownIntentKind(other)),
            };
            Body::Intent(Intent { based_on_rev, payload })
        }
        Kind::DialogueState => Body::DialogueState(DialogueState {
            seq: r.read_u32()?,
            npc: read_guid(&mut r)?,
            slot: r.read_u8()?,
            line: r.read_u16()?,
            entry: r.read_u16()?,
        }),
        Kind::LevelUnion => {
            let bits = read_raw(&mut r)?;
            let has_names = r.read_u8()?;
            let names = match has_names {
                0 => None,
                1 => {
                    let n = r.read_u16()? as usize;
                    let mut names = Vec::new();
                    for _ in 0..n {
                        names.push(read_str(&mut r)?);
                    }
                    Some(names)
                }
                other => {
                    return Err(Error::OutOfRange {
                        what: "level_union has_names",
                        value: i64::from(other),
                    })
                }
            };
            Body::LevelUnion(LevelUnion { bits, names })
        }
        Kind::Log => {
            // The whole body is the message; nothing to parse.
            Body::Log(Log { bytes: bytes.to_vec() })
        }
    };

    if !r.is_empty() {
        return Err(Error::TrailingBytes { what: kind.name(), got: r.remaining_bits() / 8 });
    }
    Ok(body)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::flags;

    fn sample_guid() -> Guid {
        Guid::from_bytes([
            0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
        ])
    }

    fn all_bodies() -> Vec<Body> {
        vec![
            Body::Hello(Hello {
                proto: 1,
                build_hash: 0x1122_3344,
                role_wanted: roles::PEER,
                save_level: "/Game/Maps/Coop".into(),
                display_name: "Player2".into(),
            }),
            Body::Welcome(Welcome {
                role_assigned: roles::HOST,
                session: 7,
                host_tick: 123_456,
                save_level: "/Game/Maps/Coop".into(),
                flags_rev: 42,
            }),
            Body::Keepalive,
            Body::Ack(Ack { seq: 0x0102 }),
            Body::PlayerRow(PlayerRow {
                slot: slots::PEER,
                sample_ms: 1500,
                x: 1000,
                y: -2000,
                z: 300,
                yaw: 8192,
                pitch: -4096,
                vx: 100,
                vy: -100,
                vz: 0,
                move_mode: 3,
                action: player_action::ATTACK,
                flags: player_flags::GROUNDED | player_flags::SPRINTING,
            }),
            Body::WorldFlags(WorldFlags {
                rev: 5,
                entries: vec![
                    FlagEntry { name: "CheckpointReached".into(), value: 7 },
                    FlagEntry { name: "DoorA".into(), value: 1 },
                ],
            }),
            Body::ActorStates(ActorStates {
                rev: 9,
                entries: vec![ActorEntry { actor: sample_guid(), value: -1 }],
            }),
            Body::Inventory(Inventory {
                rev: 3,
                entries: vec![
                    ItemEntry {
                        item_path: "/Game/Items/KeyItems/AquaDoorKey_2.AquaDoorKey_2".into(),
                        count: 1,
                    },
                    ItemEntry { item_path: "/Game/Items/Tools/Lamp.Lamp".into(), count: 4 },
                ],
            }),
            Body::EnemyRows(EnemyRows {
                rows: vec![
                    EnemyRow {
                        id: sample_guid(),
                        x: 100,
                        y: -200,
                        z: 50,
                        yaw: 16384,
                        hp: 80,
                        flags: enemy_flags::ALIVE | enemy_flags::DRIVEN | enemy_flags::HAS_BODY,
                    },
                    EnemyRow {
                        id: Guid::from_bytes([
                            16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
                        ]),
                        x: -300,
                        y: 400,
                        z: -25,
                        yaw: -16384,
                        hp: 0,
                        flags: enemy_flags::DRIVEN,
                    },
                ],
            }),
            Body::Intent(Intent {
                based_on_rev: 5,
                payload: IntentPayload::SetFlag {
                    name: "DoorA".into(),
                    value: 1,
                    mode: flags::MODE_ABSOLUTE,
                },
            }),
            Body::Intent(Intent {
                based_on_rev: 6,
                payload: IntentPayload::GrantItem {
                    item_path: "/Game/Items/Tools/Lamp.Lamp".into(),
                    count: 2,
                },
            }),
            Body::Intent(Intent {
                based_on_rev: 7,
                payload: IntentPayload::ConsumeItem {
                    item_path: "/Game/Items/KeyItems/AquaDoorKey_2.AquaDoorKey_2".into(),
                    count: 1,
                },
            }),
            Body::Intent(Intent {
                based_on_rev: 8,
                payload: IntentPayload::DialogueAnswer {
                    npc: sample_guid(),
                    line_index: 3,
                    option_index: 1,
                },
            }),
            Body::Intent(Intent {
                based_on_rev: 9,
                payload: IntentPayload::Checkpoint {
                    save_point_path: "/Game/Maps/Coop:PlayerStart_1".into(),
                },
            }),
            Body::Intent(Intent {
                based_on_rev: 10,
                payload: IntentPayload::ActorStateSet { actor: sample_guid(), value: 42 },
            }),
            Body::DialogueState(DialogueState {
                seq: 4,
                npc: sample_guid(),
                slot: slots::PEER,
                line: 12,
                entry: 3,
            }),
            Body::LevelUnion(LevelUnion {
                bits: vec![0b0000_0101, 0b0000_0010],
                names: Some(vec!["L_Main".into(), "L_Dungeon".into(), "L_Boss".into()]),
            }),
            Body::LevelUnion(LevelUnion { bits: vec![0b0000_0101, 0b0000_0010], names: None }),
            Body::Log(Log { bytes: b"hello from host".to_vec() }),
        ]
    }

    #[test]
    fn every_body_roundtrips_through_bytes_and_envelope() {
        for body in all_bodies() {
            let bytes = encode_body(&body).unwrap();
            let decoded = decode_body(body.kind(), &bytes).unwrap();
            assert_eq!(decoded, body, "body-only roundtrip failed for {:?}", body.kind());

            let env = Envelope::new(0x0A0B_0C0D, 77, body.clone());
            let datagram = env.encode().unwrap();
            let back = Envelope::decode(&datagram).unwrap();
            assert_eq!(back, env, "envelope roundtrip failed for {:?}", body.kind());
        }
    }

    #[test]
    fn player_row_is_thirty_bytes() {
        let row = PlayerRow {
            slot: 1,
            sample_ms: 1,
            x: 1,
            y: 2,
            z: 3,
            yaw: 4,
            pitch: 5,
            vx: 6,
            vy: 7,
            vz: 8,
            move_mode: 9,
            action: 10,
            flags: 11,
        };
        assert_eq!(encode_body(&Body::PlayerRow(row)).unwrap().len(), 30);
    }

    #[test]
    fn enemy_row_is_thirty_three_bytes() {
        let row = EnemyRow { id: sample_guid(), ..Default::default() };
        let bytes = encode_body(&Body::EnemyRows(EnemyRows { rows: vec![row] })).unwrap();
        // 2-byte count + one 33-byte row.
        assert_eq!(bytes.len(), 2 + 33);
    }

    #[test]
    fn header_layout_matches_wire_md() {
        let row = match all_bodies().into_iter().nth(4).unwrap() {
            Body::PlayerRow(p) => p,
            other => panic!("expected a player row, got {:?}", other.kind()),
        };
        let env = Envelope::new(0x0A0B_0C0D, 0x0102_0304, Body::PlayerRow(row));
        let datagram = env.encode().unwrap();
        assert_eq!(
            &datagram[..HEADER_LEN],
            &[
                0x51, 0x44, 0x43, 0x31, // QDC1
                0x01, // ver
                0x10, // kind PLAYER
                0x1E, 0x00, // body_len 30
                0x0D, 0x0C, 0x0B, 0x0A, // session
                0x04, 0x03, 0x02, 0x01, // seq
            ]
        );
        assert_eq!(datagram.len(), HEADER_LEN + 30);
    }

    #[test]
    fn bad_magic_is_rejected() {
        let mut d = Envelope::new(0, 1, Body::Keepalive).encode().unwrap();
        d[0] = b'X';
        assert!(matches!(Envelope::decode(&d), Err(Error::BadMagic(_))));
    }

    #[test]
    fn wrong_version_is_rejected() {
        let mut d = Envelope::new(0, 1, Body::Keepalive).encode().unwrap();
        d[4] = 2;
        match Envelope::decode(&d) {
            Err(Error::UnsupportedVersion { ours, theirs }) => {
                assert_eq!((ours, theirs), (VERSION, 2));
            }
            other => panic!("expected UnsupportedVersion, got {other:?}"),
        }
    }

    #[test]
    fn unknown_kind_is_rejected() {
        let mut d = Envelope::new(0, 1, Body::Keepalive).encode().unwrap();
        d[5] = 0x77;
        assert!(matches!(Envelope::decode(&d), Err(Error::UnknownKind(0x77))));
    }

    #[test]
    fn short_buffers_are_rejected_at_every_length() {
        let d = Envelope::new(0, 1, Body::Ack(Ack { seq: 1 })).encode().unwrap();
        for n in 0..d.len() {
            let err = Envelope::decode(&d[..n]);
            assert!(err.is_err(), "a {n}-byte prefix must not decode");
            // In particular, no slice indexing may panic on the way out.
            let _ = err.unwrap_err().to_string();
        }
    }

    /// §B.5 item 5. A hand-built body over the cap must be refused by `encode`,
    /// not silently truncate its length field.
    #[test]
    fn encode_refuses_a_body_over_the_cap() {
        // 21 rows is both over the row budget and over 1200 bytes; the point of
        // this test is that *some* deterministic error comes back rather than a
        // datagram with a wrapped length.
        let rows: Vec<EnemyRow> = (0..21u32)
            .map(|i| EnemyRow { id: Guid::from_fields(i, 0, 0, 0), ..Default::default() })
            .collect();
        let env = Envelope::new(0, 1, Body::EnemyRows(EnemyRows { rows }));
        assert!(matches!(env.encode(), Err(Error::LimitExceeded { .. })));

        // A body that is legal by the row budget but pushed over 1200 bytes by a
        // long log must also be refused by the length check, not cast.
        let env = Envelope::new(0, 1, Body::Log(Log { bytes: vec![0u8; MAX_BODY + 1] }));
        match env.encode() {
            Err(Error::LimitExceeded { what, limit, got }) => {
                assert_eq!((what, limit, got), ("body", MAX_BODY, MAX_BODY + 1));
            }
            other => panic!("expected LimitExceeded, got {other:?}"),
        }
    }

    #[test]
    fn a_body_of_exactly_the_cap_is_accepted() {
        let env = Envelope::new(0, 1, Body::Log(Log { bytes: vec![0xAB; MAX_BODY] }));
        let d = env.encode().unwrap();
        assert_eq!(d.len(), HEADER_LEN + MAX_BODY);
        assert_eq!(Envelope::decode(&d).unwrap(), env);
    }

    #[test]
    fn declared_body_len_over_the_cap_is_rejected_before_reading() {
        let mut d = Envelope::new(0, 1, Body::Keepalive).encode().unwrap();
        d[6..8].copy_from_slice(&1201u16.to_le_bytes());
        assert!(matches!(Envelope::decode(&d), Err(Error::LimitExceeded { .. })));
    }

    #[test]
    fn trailing_bytes_are_rejected() {
        let mut d = Envelope::new(0, 1, Body::Ack(Ack { seq: 1 })).encode().unwrap();
        d.push(0);
        assert!(matches!(Envelope::decode(&d), Err(Error::TrailingBytes { .. })));
    }

    #[test]
    fn a_body_longer_than_its_fields_is_rejected() {
        let mut bytes = encode_body(&Body::Ack(Ack { seq: 1 })).unwrap();
        bytes.push(0xFF);
        assert!(matches!(decode_body(Kind::Ack, &bytes), Err(Error::TrailingBytes { .. })));
    }

    #[test]
    fn keepalive_must_be_empty() {
        assert_eq!(encode_body(&Body::Keepalive).unwrap(), Vec::<u8>::new());
        assert!(matches!(
            decode_body(Kind::Keepalive, &[0]),
            Err(Error::TrailingBytes { .. })
        ));
    }

    #[test]
    fn enemy_rows_over_the_budget_are_rejected_on_decode() {
        let mut bytes = vec![0u8; 2];
        bytes[0..2].copy_from_slice(&21u16.to_le_bytes());
        bytes.extend(std::iter::repeat(0u8).take(21 * 33));
        assert!(matches!(
            decode_body(Kind::EnemyRows, &bytes),
            Err(Error::LimitExceeded { .. })
        ));
    }

    #[test]
    fn string_length_is_bounded_by_the_body_not_trusted() {
        // A `str` claiming 1000 bytes in a 4-byte body is an EOF, not an
        // allocation of 1000 bytes.
        let bytes = [0xE8, 0x03, 0x41];
        assert!(matches!(decode_body(Kind::WorldFlags, &bytes), Err(Error::UnexpectedEof { .. })));

        // An over-long `str` inside a legal body is caught the same way.
        let mut w = BitWriter::new();
        w.write_u32(0).unwrap(); // rev
        w.write_u16(1).unwrap(); // one entry
        w.write_u16(0xFFFF).unwrap(); // a name claiming 65535 bytes
        let bytes = w.finish();
        assert!(matches!(decode_body(Kind::WorldFlags, &bytes), Err(Error::UnexpectedEof { .. })));
    }

    #[test]
    fn invalid_utf8_in_a_string_is_rejected() {
        let mut w = BitWriter::new();
        w.write_u32(0).unwrap(); // rev
        w.write_u16(1).unwrap(); // one entry
        w.write_u16(2).unwrap(); // its name is two bytes...
        w.write_bytes(&[0xFF, 0xFE]).unwrap(); // ...which are not UTF-8
        let bytes = w.finish();
        assert_eq!(decode_body(Kind::WorldFlags, &bytes), Err(Error::BadString));
    }

    #[test]
    fn unknown_intent_kind_is_rejected() {
        let mut w = BitWriter::new();
        w.write_u8(9).unwrap();
        w.write_u32(1).unwrap();
        let bytes = w.finish();
        assert!(matches!(decode_body(Kind::Intent, &bytes), Err(Error::UnknownIntentKind(9))));
    }

    #[test]
    fn invalid_intent_mode_is_rejected_both_ways() {
        let bad = Body::Intent(Intent {
            based_on_rev: 1,
            payload: IntentPayload::SetFlag { name: "A".into(), value: 1, mode: 2 },
        });
        assert!(matches!(encode_body(&bad), Err(Error::OutOfRange { .. })));

        let mut w = BitWriter::new();
        w.write_u8(1).unwrap();
        w.write_u32(1).unwrap();
        write_str(&mut w, "A").unwrap();
        w.write_i32(1).unwrap();
        w.write_u8(2).unwrap();
        let bytes = w.finish();
        assert!(matches!(decode_body(Kind::Intent, &bytes), Err(Error::OutOfRange { .. })));
    }

    #[test]
    fn out_of_range_slot_and_role_are_rejected() {
        let mut row = encode_body(&Body::PlayerRow(PlayerRow { slot: 0, ..Default::default() })).unwrap();
        row[0] = 2;
        assert!(matches!(decode_body(Kind::PlayerRow, &row), Err(Error::OutOfRange { .. })));

        let mut hello = encode_body(&Body::Hello(Hello {
            proto: 1,
            role_wanted: 0,
            ..Default::default()
        }))
        .unwrap();
        hello[5] = 3;
        assert!(matches!(decode_body(Kind::Hello, &hello), Err(Error::OutOfRange { .. })));
    }

    #[test]
    fn kind_classification_matches_the_wire_table() {
        for k in [Kind::Hello, Kind::Welcome, Kind::Intent] {
            assert!(k.is_reliable(), "{k:?} must be reliable");
            assert!(!k.is_latest_wins());
        }
        for k in [
            Kind::PlayerRow,
            Kind::WorldFlags,
            Kind::ActorStates,
            Kind::Inventory,
            Kind::EnemyRows,
            Kind::DialogueState,
            Kind::LevelUnion,
        ] {
            assert!(k.is_latest_wins(), "{k:?} must be latest-wins");
            assert!(!k.is_reliable());
        }
        // A kind that is both would be ambiguous to the reliability layer.
        for k in [Kind::Hello, Kind::Welcome, Kind::Keepalive, Kind::Ack, Kind::Intent, Kind::Log] {
            assert!(!(k.is_reliable() && k.is_latest_wins()));
        }
    }

    #[test]
    fn guide_bytes_preserve_memory_order() {
        // WIRE.md §0: the 16 bytes are the FGuid's memory layout, i.e. four
        // little-endian u32 fields, with no re-ordering.
        let g = Guid::from_fields(0x0102_0304, 0x0506_0708, 0x090A_0B0C, 0x0D0E_0F10);
        assert_eq!(
            *g.as_bytes(),
            [0x04, 0x03, 0x02, 0x01, 0x08, 0x07, 0x06, 0x05, 0x0C, 0x0B, 0x0A, 0x09, 0x10, 0x0F, 0x0E, 0x0D]
        );
        assert_eq!(g.fields(), (0x0102_0304, 0x0506_0708, 0x090A_0B0C, 0x0D0E_0F10));
    }

    #[test]
    fn level_union_bits_are_lsb_first() {
        let mut u = LevelUnion::default();
        u.set_bit(0, true);
        u.set_bit(2, true);
        assert_eq!(u.bits, vec![0b0000_0101]);
        assert!(u.bit(0) && !u.bit(1) && u.bit(2));
        u.set_bit(9, true);
        assert_eq!(u.bits.len(), 2);
        assert!(u.bit(9));
        u.set_bit(9, false);
        assert!(!u.bit(9));
        // Out-of-range reads are false, never a panic.
        assert!(!u.bit(10_000));
    }

    #[test]
    fn player_row_converts_world_units_within_the_quantiser_bounds() {
        let row = PlayerRow::from_world(
            slots::HOST,
            1000,
            [1234.5, -9876.25, 543.125],
            [12.0, 45.0, 0.0],
            [300.0, -1500.5, 0.0],
            1,
            player_action::IDLE,
            player_flags::GROUNDED,
        );
        assert_eq!(row.slot, 0);
        assert_eq!(row.yaw, quantize::angle_to_wire(45.0));
        assert_eq!(row.pitch, quantize::angle_to_wire(12.0));
        let p = row.position_cm();
        for i in 0..3 {
            let want = [1234.5f32, -9876.25, 543.125][i];
            assert!((p[i] - want).abs() <= quantize::POSITION_MAX_ERROR_CM + 1e-4);
        }
        let a = row.angles_deg();
        assert!((quantize::wrap_degrees(a[0] - 12.0)).abs() <= quantize::ANGLE_MAX_ERROR_DEG + 1e-4);
        assert!((quantize::wrap_degrees(a[1] - 45.0)).abs() <= quantize::ANGLE_MAX_ERROR_DEG + 1e-4);
        assert!(row.has_flag(player_flags::GROUNDED));
        assert!(!row.has_flag(player_flags::MENU));
    }

    #[test]
    fn body_kind_is_derived_not_stored() {
        for body in all_bodies() {
            let bytes = encode_body(&body).unwrap();
            let decoded = decode_body(body.kind(), &bytes).unwrap();
            assert_eq!(decoded.kind(), body.kind());
        }
    }
}
