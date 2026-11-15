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
    Equipment = 0x15,
    Effect = 0x16,
    EffectMove = 0x17,
    Status = 0x18,
    EnemyDetail = 0x19,
    Intent = 0x20,
    DialogueState = 0x21,
    LevelUnion = 0x22,
    Travel = 0x23,
    Damage = 0x24,
    Cutscene = 0x25,
    Movers = 0x26,
    Loot = 0x27,
    Suspend = 0x28,
    Boss = 0x29,
    /// Placed world objects that are not `SavableActorStates` entries (WIRE.md §3.20).
    Objects = 0x2A,
    /// The class for an enemy key the receiver holds no actor for (WIRE.md §3.9c).
    EnemySpawn = 0x2B,
    /// A partner's body began/ended overlapping a placed event volume (WIRE.md §3.21).
    Trigger = 0x2C,
    /// The dialogue cursor the talker is showing, so the partner can display it (WIRE.md §3.22).
    DialogueView = 0x50,
    /// Where a rider stands ON a moving platform, in that platform's own axes
    /// (WIRE.md §3.23). The companion of [`Kind::Movers`]: `MOVERS` says where the
    /// platform is, this says where the person standing on it is.
    PlatformRide = 0x48,
    /// A player's death, respawn and rest, stated by the machine that OWNS that player
    /// (WIRE.md §3.24, `qdrespawn.h`). There is no intent for this lane: an intent is a
    /// request to the host, and a player's own health is not the host's to grant.
    RespawnDown = 0x54,
    RespawnUp = 0x55,
    RespawnRest = 0x56,
    /// One death paid one player's XP; the other pays its own the same amount
    /// (WIRE.md §3.26).
    KillXp = 0x40,
    /// "I saved; write your own slot now" (WIRE.md §3.27).
    Save = 0x41,
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
            0x15 => Equipment,
            0x16 => Effect,
            0x17 => EffectMove,
            0x18 => Status,
            0x19 => EnemyDetail,
            0x20 => Intent,
            0x21 => DialogueState,
            0x22 => LevelUnion,
            0x23 => Travel,
            0x24 => Damage,
            0x25 => Cutscene,
            0x26 => Movers,
            0x27 => Loot,
            0x28 => Suspend,
            0x29 => Boss,
            0x2A => Objects,
            0x2B => EnemySpawn,
            0x2C => Trigger,
            0x48 => PlatformRide,
            0x50 => DialogueView,
            0x54 => RespawnDown,
            0x55 => RespawnUp,
            0x56 => RespawnRest,
            0x40 => KillXp,
            0x41 => Save,
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
            Equipment => "equipment",
            Effect => "effect",
            EffectMove => "effect_move",
            Status => "status",
            EnemyDetail => "enemy_detail",
            Intent => "intent",
            DialogueState => "dialogue_state",
            LevelUnion => "level_union",
            Travel => "travel",
            Damage => "damage",
            Cutscene => "cutscene",
            Movers => "movers",
            Loot => "loot",
            Suspend => "suspend",
            Boss => "boss",
            Objects => "objects",
            EnemySpawn => "enemy_spawn",
            Trigger => "trigger",
            PlatformRide => "platform_ride",
            DialogueView => "dialogue_view",
            RespawnDown => "respawn_down",
            RespawnUp => "respawn_up",
            RespawnRest => "respawn_rest",
            KillXp => "kill_xp",
            Save => "save",
            Log => "log",
        }
    }

    /// Reliable kinds (WIRE.md §2): retried until acked by the sender.
    pub fn is_reliable(self) -> bool {
        matches!(
            self,
            Kind::Hello
                | Kind::Welcome
                | Kind::Intent
                | Kind::Effect
                | Kind::EnemyDetail
                | Kind::Travel
                | Kind::Damage
                | Kind::Suspend
                | Kind::Boss
                | Kind::Objects
                | Kind::EnemySpawn
                | Kind::Trigger
                | Kind::KillXp
                | Kind::Save
                | Kind::RespawnDown
                | Kind::RespawnUp
                | Kind::RespawnRest
        )
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
                | Kind::Equipment
                | Kind::EffectMove
                | Kind::Status
                | Kind::DialogueState
                | Kind::LevelUnion
                | Kind::Cutscene
                | Kind::Movers
                | Kind::Loot
                | Kind::PlatformRide
                | Kind::DialogueView
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

/// `PLAYER.action` (WIRE.md §3.5): bits 0-4 the kind below, bits 5-7 a counter that
/// bumps on every new move, so a repeat of the same kind still restarts on the peer.
pub mod player_action {
    pub const IDLE: u8 = 0;
    pub const ATTACK: u8 = 1;
    pub const HURT: u8 = 2;
    pub const DYING: u8 = 3;
    pub const DEAD: u8 = 4;
    pub const SLASH_RIGHT: u8 = 5;
    pub const SLASH_LEFT: u8 = 6;
    pub const SLASH_DOWN: u8 = 7;
    pub const THRUST: u8 = 8;
    pub const CHARGE: u8 = 9;
    pub const THROW: u8 = 10;
    pub const USE_ITEM: u8 = 11;
    pub const SWAP: u8 = 12;
    pub const CAST: u8 = 13;

    pub const KIND_MASK: u8 = 0x1F;
    pub const COUNTER_SHIFT: u8 = 5;

    /// The byte for `kind` at counter `seq` (mod 8).
    pub fn pack(kind: u8, seq: u8) -> u8 {
        ((seq & 7) << COUNTER_SHIFT) | (kind & KIND_MASK)
    }

    pub fn kind(byte: u8) -> u8 {
        byte & KIND_MASK
    }

    pub fn counter(byte: u8) -> u8 {
        byte >> COUNTER_SHIFT
    }

    #[cfg(test)]
    mod tests {
        use super::*;

        #[test]
        fn pack_matches_the_native_layout() {
            // qdavatar.h ActionByte(kind, seq) = ((seq & 7) << 5) | (kind & 0x1F)
            assert_eq!(pack(ATTACK, 1), 0x21);
            assert_eq!(pack(SWAP, 9), 0x2C);
            assert_eq!(kind(pack(CAST, 7)), CAST);
            assert_eq!(counter(pack(CAST, 7)), 7);
        }
    }
}

/// `ENEMY.flags` bits (WIRE.md §3.9).
pub mod enemy_flags {
    pub const ALIVE: u8 = 1 << 0;
    /// The receiver must freeze its copy because the sender runs this enemy.
    pub const DRIVEN: u8 = 1 << 1;
    pub const HAS_BODY: u8 = 1 << 2;
    /// The sender has handed this enemy to the receiver: the SENDER freezes its copy
    /// and the receiver's own AI runs it.
    pub const PEER_OWNS: u8 = 1 << 3;
    /// Bits 4-5 carry the identity source of the row's `id`; see [`enemy_id_source`].
    pub const SRC_SHIFT: u8 = 4;
    pub const SRC_MASK: u8 = 0x30;
}

/// `ENEMY.flags` identity source (WIRE.md §3.9, native `qdenemyid.h`).
///
/// The row's `id` used to be the authored `FGuid` of the enemy's spawner, which only
/// exists for enemies a spawner created: a placed enemy and a summoned one had no key
/// and therefore ran independently on each machine. Two deterministic fallbacks fix
/// that, and these bits say which one produced a given `id` — so a hash collision
/// between, say, a placed-path hash and a spawner GUID cannot merge two enemies, and a
/// live log can name the kind of key it is looking at.
pub mod enemy_id_source {
    /// An authored spawner GUID — unchanged, and the key `StoreEnemySaveState` persists.
    pub const SPAWNER: u8 = 0;
    /// A placed actor: FNV-1a 128 of its cooked `<world>/<level>/<object name>` path.
    pub const PLACED: u8 = 1;
    /// A runtime spawn: FNV-1a 128 of `<summoner key hex>#<ordinal>`.
    pub const SUMMON: u8 = 2;

    /// The source carried by a row's flags.
    pub fn of(flags: u8) -> u8 {
        (flags & super::enemy_flags::SRC_MASK) >> super::enemy_flags::SRC_SHIFT
    }

    /// `flags` with `src` written into the source bits, preserving the state bits.
    pub fn with(flags: u8, src: u8) -> u8 {
        (flags & !super::enemy_flags::SRC_MASK)
            | ((src << super::enemy_flags::SRC_SHIFT) & super::enemy_flags::SRC_MASK)
    }
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
///
/// `entries` is what THIS datagram carried. The header's `n` is the size of the
/// sender's whole container, which can be larger: the body is chunked across ticks
/// (WIRE.md §3.6) and `decode_body` stops when the body ends. The relay only
/// validates and forwards the bytes, so the container size is not kept; `encode`
/// writes a whole-container body (`n = entries.len()`).
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

/// `ACTOR_STATES` body (WIRE.md §3.7). Same chunking rule as [`WorldFlags`]: `entries`
/// is this datagram's prefix of the container, which is why a 117-entry set arrives as
/// several bodies that each decode on their own.
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

/// `INVENTORY` body (WIRE.md §3.8). Chunked like [`WorldFlags`].
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct Inventory {
    pub rev: u32,
    pub entries: Vec<ItemEntry>,
}

/// Maximum held items in one `EQUIPMENT` body (right hand, left hand, spare).
pub const MAX_EQUIP_ITEMS: usize = 4;

/// One held item (WIRE.md §3.13): what mesh is in which hand, and how it sits there.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct EquipItem {
    /// 0 = right hand, 1 = left hand.
    pub hand: u8,
    /// 1 = static mesh, 2 = skeletal mesh.
    pub mesh_kind: u8,
    /// "<package>.<object>" of the mesh asset.
    pub mesh_path: String,
    /// Mesh transform relative to its weapon root: location in 1/100 cm (3),
    /// rotation quaternion x,y,z,w in 1/1_000_000 (4), scale in 1/1000 (3).
    pub xform: [i32; 10],
}

/// `EQUIPMENT` body (WIRE.md §3.13): the sender's held items, absolute, latest-wins.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct Equipment {
    pub rev: u32,
    pub items: Vec<EquipItem>,
}

/// `STATUS` body (WIRE.md §3.15): the sender's visible status effects, absolute,
/// latest-wins.  `mask` bits are [`status_bit`] values; unknown bits are carried.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct Status {
    pub rev: u32,
    pub mask: u32,
    /// Optional tail (WIRE.md §3.15, vitals tail): the sender's own player HP.  `None` from an older
    /// sender whose body ends after `mask`.
    pub vitals: Option<Vitals>,
}

/// `STATUS` vitals tail (WIRE.md §3.15, vitals tail): whole HP points, clamped to `u16`.  This build
/// has no player stamina, so there is none to carry.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct Vitals {
    pub hp: u16,
    pub hp_max: u16,
    /// [`vitals_flag`] bits.
    pub flags: u8,
}

/// `STATUS` vitals `flags` bits.
pub mod vitals_flag {
    pub const VALID: u8 = 1;
    pub const DEAD: u8 = 2;
}

/// `STATUS.mask` bits (WIRE.md §3.15).
pub mod status_bit {
    pub const MAGIC_FIRE: u32 = 1 << 0;
    pub const MAGIC_POISON: u32 = 1 << 1;
    pub const POISON: u32 = 1 << 2;
    pub const DISEASE: u32 = 1 << 3;
    pub const WEAKEN: u32 = 1 << 4;
    pub const HEAVY: u32 = 1 << 5;
    pub const BINDING: u32 = 1 << 6;
    pub const BERSERK: u32 = 1 << 7;
    pub const IRONSKIN: u32 = 1 << 8;
    pub const RADIANT: u32 = 1 << 9;
}

/// `EFFECT.kind` values (WIRE.md §3.14).
pub mod effect_kind {
    /// A projectile was spawned (reliable).
    pub const PROJECTILE_SPAWNED: u8 = 1;
    /// It stopped or was destroyed, at the given position (reliable).
    pub const PROJECTILE_ENDED: u8 = 2;
    /// Where it is now (unreliable, ~15 Hz, superseded by the next).
    pub const PROJECTILE_MOVED: u8 = 3;
    /// A melee swing's arc FX: `class_path` = the Niagara system, `pitch` = the arc's
    /// roll, `yaw` = its yaw relative to the heading, `vx`/`vy` = two RGBA8 colours,
    /// `life_ms` = its lifetime (reliable).
    pub const MELEE_ARC: u8 = 4;
}

/// `EFFECT` / `EFFECT_MOVE` body (WIRE.md §3.14): one visual event from the sender's
/// world.  Spawn and end travel as `EFFECT` (reliable); position updates as
/// `EFFECT_MOVE` (latest-wins), so each kind has one reliability class.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct Effect {
    /// See [`effect_kind`].
    pub kind: u8,
    /// The sender's handle for the projectile, shared by its spawn/move/end.
    pub id: u16,
    /// "<package>.<object>" of the actor class; empty except for a spawn.
    pub class_path: String,
    /// Position, 1/8 cm.
    pub x: i32,
    pub y: i32,
    pub z: i32,
    /// 1/65536 turn.
    pub pitch: i16,
    pub yaw: i16,
    /// Velocity, 1/8 cm/s (i32: projectiles outrun the i16 of `PLAYER`).
    pub vx: i32,
    pub vy: i32,
    pub vz: i32,
    pub life_ms: u16,
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

/// `ENEMY_DETAIL` body (WIRE.md §3.9a).
///
/// A row is a 20 Hz sample; this is the event lane for the same enemy: which montage
/// is playing and which status effects are on it. The montage is named by asset path,
/// not by index — both machines load the same cooked assets but discover them in
/// their own order, so an index would select a different animation on each side.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct EnemyDetail {
    pub id: Guid,
    /// See [`enemy_anim`].
    pub anim: u8,
    /// Bumps on every new montage, so the receiver can tell a repeat from a new one.
    /// Wraps 255 → 1; a receiver keys "already applied" on `(seq, montage_path)`.
    pub seq: u8,
    /// The same bits as [`Status`] `mask`.
    pub status: u16,
    /// The boss phase the owner observed, 0 when the enemy is not a boss or when the
    /// build exposes no shared phase (SYNC-RESEARCH §6: no cooked package carries one).
    pub boss: u8,
    /// Montage asset path, empty when nothing is playing.
    pub montage_path: String,
}

/// `ENEMY_DETAIL.anim` values (WIRE.md §3.9a).
pub mod enemy_anim {
    pub const NONE: u8 = 0;
    pub const ATTACK: u8 = 1;
    pub const HURT: u8 = 2;
    pub const DIE: u8 = 3;
    pub const ROAR: u8 = 4;
    pub const CAST: u8 = 5;
    pub const MOVE: u8 = 6;
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
    /// kind 7: the peer hit a host-driven enemy copy. `spawner` is the enemy's key
    /// (its spawner's authored GUID); `damage_frac` is the damage as a fraction of max
    /// HP in 1/65535 units (may exceed 65535 for an overkill), so the host scales it
    /// by its own max.
    EnemyHit { spawner: Guid, damage_frac: i32 },
    /// kind 8: the peer wrote a persistence record for an enemy it ran itself (the
    /// host had no live copy). The three scalars of `StoreEnemySaveState`, in 1/100.
    EnemyRecord { spawner: Guid, respawn_centi: i32, hp_centi: i32 },
    /// kind 9: the peer opened/activated a placed world object the GUID-keyed lane cannot
    /// carry (WIRE.md §3.20). `key` is the placed-object key, `shape` the class family,
    /// `state` 0/1 — the same three fields the `OBJECTS` row carries.
    ObjectState { key: Guid, shape: u8, state: u8 },
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
            IntentPayload::EnemyHit { .. } => 7,
            IntentPayload::EnemyRecord { .. } => 8,
            IntentPayload::ObjectState { .. } => 9,
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

/// `DIALOGUE_VIEW` body (WIRE.md §3.22).
///
/// The cursor the talking machine's `DialogueManager` is showing, so the partner can drive
/// its OWN game's dialogue widget with it.  The lines are not here: they live in the cooked
/// `FS_DialogueData` asset, identified by its object name, which both machines load from
/// the same package.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct DialogueView {
    /// Increments per state change; a lower `seq` is discarded.
    pub seq: u32,
    /// Stable id of the NPC actor (the same 128-bit placed-object key `DIALOGUE` uses).
    pub npc: Guid,
    /// 0 or 1 while talking; 0xFF = the conversation ended.
    pub slot: u8,
    /// bit0 options on screen, bit1 a choice was made, bit2 hide the speaker's name.
    pub flags: u8,
    /// The talker's `CurrentDialogueSequenceIndex`.
    pub sequence: u16,
    /// The entry index inside that sequence (0 when the build does not expose one).
    pub entry: u16,
    /// Every option the talker can pick from (0 = no option list on screen).
    pub options: u16,
    /// The option the talker picked; 0xFFFF = none yet.
    pub chosen: u16,
    /// The `FS_DialogueData` object's own name.
    pub asset: String,
}

/// `DIALOGUE_VIEW.flags` (WIRE.md §3.22).
pub mod dialogue_view_flags {
    pub const OPTIONS: u8 = 0x01;
    pub const CHOSEN: u8 = 0x02;
    pub const HIDE_NAME: u8 = 0x04;
}

/// The "no option picked" sentinel both sides use for `DIALOGUE_VIEW.chosen`.
pub const DIALOGUE_VIEW_NO_OPTION: u16 = 0xFFFF;

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

/// `TRAVEL` body (WIRE.md §3.13).
///
/// The world is announced, not inferred: the host publishes the map it is in and the
/// peer compares, so a peer that is somewhere else says so instead of applying level
/// names that belong to another world.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct Travel {
    /// Bumps on every change.
    pub seq: u32,
    /// 1 = host, 2 = peer (see [`roles`]).
    pub role: u8,
    /// Where this announcement sits in the hard-transition handshake (see
    /// [`travel_phase`]). `NONE` is the ordinary in-world announcement.
    pub phase: u8,
    /// Short map name, e.g. `100_GR`.
    pub map: String,
    /// The destination the sender just took: the placed actor carrying the game's own
    /// `LocationData`, named so the other machine can resolve the same actor. For a
    /// hard transition, the destination map package path. Empty when the sender is not
    /// travelling.
    pub dest: String,
}

/// `TRAVEL.phase` (WIRE.md §3.15, vitals tail).
///
/// The in-world half is a single announcement. A hard transition is a handshake
/// because both machines must arrive before either resumes applying absolute state:
/// `HARD_BEGIN` names the map, `ARRIVED` is sent by each machine once its own world is
/// that map, and `RESUME` is the host releasing the pair.
pub mod travel_phase {
    pub const NONE: u8 = 0;
    pub const HARD_BEGIN: u8 = 1;
    pub const ARRIVED: u8 = 2;
    pub const RESUME: u8 = 3;
}

/// `CUTSCENE` body (WIRE.md §3.16).
///
/// A cutscene is per-machine Blueprint state; there is no host-side switch that makes
/// the other machine play one. The sender publishes which manager it is running, and
/// the receiver either resolves the same manager in its own cooked world
/// (`BeginCutsceneWithManager`) or is held consistently with the game's own
/// cutscene-mode switch until the sender reports `ENDED`.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct Cutscene {
    /// Bumps on every change; an older state is discarded.
    pub seq: u32,
    /// 0 = host, 1 = peer, 0xFF = free.
    pub slot: u8,
    /// See [`cutscene_state`].
    pub state: u8,
    /// Short name of the manager/sequence actor; empty when none.
    pub name: String,
}

pub mod cutscene_state {
    pub const NONE: u8 = 0;
    pub const PLAYING: u8 = 1;
    pub const ENDED: u8 = 2;
}

/// One row of `MOVERS` (WIRE.md §3.17): a moving world object's transform.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct MoverRow {
    /// Stable 128-bit id for the placed actor (FNV-1a over its path, the same scheme
    /// the dialogue identity uses).
    pub id: Guid,
    /// 1/8 cm.
    pub x: i32,
    pub y: i32,
    pub z: i32,
    /// 1/65536 turn.
    pub yaw: i16,
    pub pitch: i16,
    /// See [`mover_flags`].
    pub flags: u8,
}

pub mod mover_flags {
    /// The owner moved it this frame (as opposed to a settled position).
    pub const MOVING: u8 = 1;
    /// The sender is the authority for this mover.
    pub const OWNER: u8 = 2;
}

/// One row of `PLATFORM_RIDE` (WIRE.md §3.23): where a rider stands ON a platform.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct PlatformRideRow {
    /// The platform's stable 128-bit id — the same key `MOVERS` uses.
    pub id: Guid,
    /// The rider's place in the platform's own axes, 1/8 cm.
    pub ox: i32,
    pub oy: i32,
    pub oz: i32,
    /// The rider's heading RELATIVE to the platform, 1/65536 turn.
    pub oyaw: i16,
    /// See [`platform_ride_flags`].
    pub flags: u8,
}

pub mod platform_ride_flags {
    /// The sender's player is on this platform.
    pub const ON: u8 = 1;
    /// ... and the platform is moving.
    pub const MOVING: u8 = 2;
}

/// `PLATFORM_RIDE` body (WIRE.md §3.23).
///
/// The rider's place is carried **in the platform's axes**, never as a world position: a
/// rider's "position minus the platform's" jumps 50–900 cm from one sample to the next
/// because the pawn and the platform are sampled at different moments of the sender's
/// frame, while a standing passenger's place on the platform does not change at all. The
/// receiver reconstructs from its own copy of the platform.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct PlatformRide {
    /// The sender's clock; a reordered body is discarded.
    pub ms: u32,
    pub rows: Vec<PlatformRideRow>,
}

/// `MOVERS` body (WIRE.md §3.17). Owner-authoritative absolute state.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct Movers {
    pub rev: u32,
    pub rows: Vec<MoverRow>,
}

/// `LF_SPAWN`: the entry carries a point, so a machine that holds no copy of this drop
/// materialises it there. See [`LootEntry`].
pub mod loot_flags {
    pub const SPAWN: u8 = 1;
}

/// One `LOOT` entry (WIRE.md §3.18).
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct LootEntry {
    /// The drop's identity: FNV-1a over the DEATH that caused it (Session 22, R129).
    /// The game rolls its own drop on each machine and the two rolls are neither the
    /// same item nor the same point, so the roll cannot be the key; the death can.
    pub source_hash: u32,
    /// Which drop of that death this is (spawn order within it).
    pub drop_index: i32,
    /// A tombstone: this drop was taken and must disappear on both machines.
    pub taken: bool,
    /// See [`loot_flags`].
    pub flags: u8,
    /// Facing, `QuantizeAngle` units.
    pub yaw: i16,
    /// The point the drop occupies, `QuantizePos` units.
    pub px: i32,
    pub py: i32,
    pub pz: i32,
    /// The item the roll produced, as a soft object path; empty for a tombstone.
    pub item_path: String,
}

/// `LOOT` body (WIRE.md §3.18). A shared drop set plus tombstones.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct Loot {
    pub rev: u32,
    pub entries: Vec<LootEntry>,
}

/// `SUSPEND` body (WIRE.md §3.19).
///
/// The game writes a timed suspend save on its own schedule, gated by
/// `BlockSuspendSaves`. In a live session the peer holds the block so its own save
/// cannot advance past the host, and a suspend load it does take tells the host to
/// re-apply its absolute state.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct Suspend {
    pub seq: u32,
    /// See [`suspend_state`].
    pub state: u8,
    /// The suspend slot name when known, otherwise empty.
    pub slot: String,
}

pub mod suspend_state {
    pub const CLEAR: u8 = 0;
    pub const HOLD: u8 = 1;
    pub const LOADED: u8 = 2;
    pub const REAPPLIED: u8 = 3;
}

/// `DAMAGE` body (WIRE.md §3.14a).
///
/// Host → peer, for the one case ownership-by-proximity cannot cover: an enemy the host
/// runs attacking the partner across the distance. Only the partner can apply damage to
/// its own pawn (the payload goes through `TryApplyDamage` on a `FB_Damageable` the host
/// has no object for), so the host describes the hit and the peer performs it.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct Damage {
    /// The attacking enemy's key; all zero for an environmental hit.
    pub attacker: Guid,
    /// 1/100 of the game's damage unit.
    pub amount_centi: i32,
    pub flags: u8,
}

/// `DAMAGE.flags` bits (WIRE.md §3.14a).
pub mod damage_flags {
    pub const MELEE: u8 = 1 << 0;
    pub const RANGED: u8 = 1 << 1;
    /// The sender could not resolve the damage source, so the amount is an estimate.
    pub const GUESSED: u8 = 1 << 2;
}

/// `BOSS` body (WIRE.md §3.9b).
///
/// The two per-boss facts that are a local call on each machine, and therefore have to be
/// driven rather than inferred: the health bar (`PlayerStatsAndUtils_C.ActivateBossHPBar`,
/// whose `__WorldContext` is the caller's own world) and the arena exit
/// (`EnemySpawner_C.PlayerTeleportedOutOfBossArena`, a zero-argument spawner event).
/// Boss *phases* are not expressible — no cooked package carries a phase field — so
/// `phase` is only ever the owner's observed ordinal, and 0 means "none".
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct Boss {
    /// The boss enemy's key; zero when the body names no particular boss.
    pub id: Guid,
    /// See [`boss_flags`].
    pub flags: u8,
    /// The owner's observed phase, 0 when none.
    pub phase: u8,
    /// The boss's HP as the same 0..65535 fraction an `ENEMY` row carries.
    pub hp: u16,
}

/// `BOSS.flags` bits (WIRE.md §3.9b).
pub mod boss_flags {
    /// Raise the boss health bar on the receiver's own player for its own copy.
    pub const BAR_ON: u8 = 1 << 0;
    /// Hide the bar again (the fight ended, or the copy died).
    pub const BAR_OFF: u8 = 1 << 1;
    /// A player left the boss arena: raise the receiver's own spawner exit event.
    pub const ARENA_EXIT: u8 = 1 << 2;
}

/// One `OBJECTS` row (WIRE.md §3.20). 18 bytes on the wire.
///
/// The lane for interactables that are NOT `SavableActorStates` entries — the chest
/// measured live has no GUID at all, so the GUID-keyed `ACTOR_STATES` lane cannot carry
/// it. `key` is the placed actor's cooked `<world>/<level>/<name>` path hashed with the
/// object seed, `shape` is the class family (see [`object_shape`]), `state` is 0/1.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct ObjectRow {
    pub key: Guid,
    pub shape: u8,
    pub state: u8,
}

/// `OBJECTS` body (WIRE.md §3.20). The host's absolute set of world-object states.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct Objects {
    pub rev: u32,
    pub entries: Vec<ObjectRow>,
}

/// `OBJECTS.shape` values (WIRE.md §3.20).
pub mod object_shape {
    pub const NONE: u8 = 0;
    pub const CHEST: u8 = 1;
    pub const DOOR: u8 = 2;
    pub const PICKUP: u8 = 3;
    pub const BARRICADE: u8 = 4;
    pub const ACTIVATABLE: u8 = 5;
}

/// Most rows one `OBJECTS` datagram may carry (WIRE.md §3.20): 1200-byte body, 6-byte
/// header, 18-byte rows.
pub const MAX_OBJECT_ROWS: usize = 66;

/// `ENEMY_SPAWN` body (WIRE.md §3.9c).
///
/// A spawner only spawns while *its own* machine's player is inside its activation range,
/// so an enemy whose spawner activated on one machine does not exist at all on the other.
/// Every other enemy lane is addressed to a *copy* — rows, detail, boss, damage — so on the
/// machine with no actor there is nothing to follow and nothing to hit. The rows already
/// say a key is alive and where; what a row cannot carry is which CLASS to construct, and it
/// cannot be derived (a summon's key hashes its summoner and a spawn point, a placed key
/// hashes a path), so the class asset path rides the wire.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct EnemySpawn {
    /// The enemy's key, the same one an `ENEMY` row carries.
    pub id: Guid,
    /// See [`enemy_spawn_flags`].
    pub flags: u8,
    /// Position in the same 1/8 cm quantization an `ENEMY` row uses.
    pub px: i32,
    pub py: i32,
    pub pz: i32,
    pub yaw: i16,
    /// HP as a 0..65535 fraction of max, like a row.
    pub hp: u16,
    /// The enemy actor's class asset path, `"<package>.<object>"`.
    pub class_path: String,
}

/// `ENEMY_SPAWN.flags`.
pub mod enemy_spawn_flags {
    /// The receiver should hold this as a mirrored copy: AI disabled, driven by the owner's
    /// rows, never persisted by the local spawner or save.
    pub const MIRROR: u8 = 0x01;
}

/// `TRIGGER` body (WIRE.md §3.21).
///
/// A placed `ActivationTrigger_C` answers only its own machine's player, so a boss fight or
/// a one-time event started on one machine only. The sender reports the event the game
/// itself dispatched (the trigger's bound overlap event, with the local player as the other
/// actor); the receiver dispatches the same event on its own copy with its own player, so
/// the trigger's own gates decide.
#[derive(Debug, Clone, PartialEq, Default)]
pub struct Trigger {
    /// The trigger's placed-path key.
    pub key: Guid,
    /// See [`trigger_event`].
    pub event: u8,
    /// See [`trigger_volume`].
    pub volume: u8,
    /// See [`trigger_flags`].
    pub flags: u8,
    /// The sender's body when it entered, in centimetres.
    pub px: f32,
    pub py: f32,
    pub pz: f32,
    /// The trigger's instance name, for the logs on both sides.
    pub name: String,
}

// `Eq` is sound here although the positions are floats: `decode_body` refuses every
// non-finite coordinate, so a decoded `Trigger` never holds a NaN and `==` is reflexive.
impl Eq for Trigger {}

/// `TRIGGER.event`.
pub mod trigger_event {
    pub const BEGIN: u8 = 1;
    pub const END: u8 = 2;
}

/// `TRIGGER.volume`: which volume dispatched it. `BOX`/`SPHERE` are a placed
/// `ActivationTrigger_C`'s two volumes; `TRAP_ON`/`TRAP_OFF` are an `EnemySpawner_C`'s own
/// trap pair, which carry exactly one event each (begin and end respectively).
pub mod trigger_volume {
    pub const BOX: u8 = 0;
    pub const SPHERE: u8 = 1;
    pub const TRAP_ON: u8 = 2;
    pub const TRAP_OFF: u8 = 3;
}

/// `TRIGGER.flags`.
pub mod trigger_flags {
    /// A target is a spawner whose enemy data says Boss: the receiver pulls its player in.
    pub const BOSS: u8 = 0x01;
}

/// `RESPAWN_DOWN` body (WIRE.md §3.24).
///
/// The machine that OWNS a player says its player died. Positions are the wire's own 1/8 cm
/// integers, exactly like [`PlayerRow`]: one encoding for "where a player is" in the whole
/// protocol. `pct` must equal [`respawn_penalty::pct_for`]`(deaths)`, and a body where the
/// two disagree is refused rather than applied — applying it would either heal a penalised
/// player or penalise a rested one.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct RespawnDown {
    /// Deaths this player now carries (0..`MAX_STACKS`).
    pub deaths: u8,
    /// The maximum-health percentage after this death.
    pub pct: u8,
    /// Per-lane counter: the newest is applied and a duplicate is dropped (a second UP would
    /// teleport the puppet back to a rest point the player has since left).
    pub seq: u8,
    /// Where the body went down.
    pub px: i32,
    pub py: i32,
    pub pz: i32,
}

/// `RESPAWN_UP` body (WIRE.md §3.24): the player is back, at its own last rest point.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct RespawnUp {
    pub deaths: u8,
    pub pct: u8,
    pub seq: u8,
    /// The rest point the body came back to.
    pub px: i32,
    pub py: i32,
    pub pz: i32,
    /// 1/65536 turn.
    pub yaw: i16,
    /// The restored health and the (penalised) maximum, whole points.
    pub hp: u16,
    pub hp_max: u16,
    /// The rest point's `<actor>/<level>` key, for the logs on both sides.
    pub name: String,
}

/// `RESPAWN_REST` body (WIRE.md §3.24): a rest cleared the stacks. Always `0` deaths and
/// `100` percent — a rest body that carries a stack would clear a penalty on the receiver
/// that the sender still has.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct RespawnRest {
    pub deaths: u8,
    pub pct: u8,
    pub seq: u8,
    pub px: i32,
    pub py: i32,
    pub pz: i32,
    pub name: String,
}

/// The death penalty ladder (WIRE.md §3.24, `qdrespawn.h`): each death costs 10 % of the
/// BASE maximum, stacking, to a floor of 10 %. A rest restores 100 % and clears it.
pub mod respawn_penalty {
    pub const STEP_PCT: u8 = 10;
    pub const FLOOR_PCT: u8 = 10;
    /// The stack at which the ladder reaches the floor; a tenth death changes nothing.
    pub const MAX_STACKS: u8 = (100 - FLOOR_PCT) / STEP_PCT;

    pub fn pct_for(deaths: u8) -> u8 {
        let d = if deaths > MAX_STACKS { MAX_STACKS } else { deaths };
        let p = 100 - STEP_PCT * d;
        if p < FLOOR_PCT {
            FLOOR_PCT
        } else {
            p
        }
    }
}

/// `KILL_XP` body (WIRE.md §3.26). One death paid one machine's player `xp`; the other
/// machine pays its own player the same amount if its own game did not. `xp` is what the
/// sender MEASURED, never a value derived from the enemy's data.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct KillXp {
    /// The enemy's key — the same identity every other enemy lane uses.
    pub id: Guid,
    /// The death's ordinal for that key, so a retransmit is not a second award.
    pub seq: u16,
    /// The measured award. Never zero: an award of nothing is not an award.
    pub xp: u32,
}

/// `SAVE` body (WIRE.md §3.27). "I saved; write your own slot now."
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct Save {
    /// The sender's own save counter — monotone, so a retransmit is one request.
    pub seq: u32,
    /// 1 = host, 2 = peer, 0 = unspecified. Diagnostics only.
    pub role: u8,
    /// Which of the receiver's OWN writers must run (see [`save_route`]). A route, never
    /// a save file: this build's player-facing save is the checkpoint REST, which writes
    /// `World0_Suspend_N.sav`, so a partner answering with a main-slot write would have
    /// saved a different file than the one its own game resumes from (round 3 live).
    pub route: u8,
}

/// `SAVE.role` (WIRE.md §3.27).
pub mod save_role {
    pub const HOST: u8 = 1;
    pub const PEER: u8 = 2;
}

/// `SAVE.route` (WIRE.md §3.27). 0 is accepted as "unspecified" and means `SLOT`, so a
/// 5-byte body from a sender that predates this field still mirrors.
pub mod save_route {
    pub const UNSPECIFIED: u8 = 0;
    pub const SLOT: u8 = 1;
    pub const SUSPEND: u8 = 2;
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
    Equipment(Equipment),
    Effect(Effect),
    EffectMove(Effect),
    Status(Status),
    EnemyDetail(EnemyDetail),
    Intent(Intent),
    DialogueState(DialogueState),
    LevelUnion(LevelUnion),
    Travel(Travel),
    Damage(Damage),
    Cutscene(Cutscene),
    Movers(Movers),
    Loot(Loot),
    Suspend(Suspend),
    Boss(Boss),
    Objects(Objects),
    EnemySpawn(EnemySpawn),
    Trigger(Trigger),
    PlatformRide(PlatformRide),
    DialogueView(DialogueView),
    RespawnDown(RespawnDown),
    RespawnUp(RespawnUp),
    RespawnRest(RespawnRest),
    KillXp(KillXp),
    Save(Save),
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
            Body::Equipment(_) => Kind::Equipment,
            Body::Effect(_) => Kind::Effect,
            Body::EffectMove(_) => Kind::EffectMove,
            Body::Status(_) => Kind::Status,
            Body::EnemyDetail(_) => Kind::EnemyDetail,
            Body::Intent(_) => Kind::Intent,
            Body::DialogueState(_) => Kind::DialogueState,
            Body::LevelUnion(_) => Kind::LevelUnion,
            Body::Travel(_) => Kind::Travel,
            Body::Damage(_) => Kind::Damage,
            Body::Cutscene(_) => Kind::Cutscene,
            Body::Movers(_) => Kind::Movers,
            Body::PlatformRide(_) => Kind::PlatformRide,
            Body::Loot(_) => Kind::Loot,
            Body::Suspend(_) => Kind::Suspend,
            Body::Boss(_) => Kind::Boss,
            Body::Objects(_) => Kind::Objects,
            Body::EnemySpawn(_) => Kind::EnemySpawn,
            Body::Trigger(_) => Kind::Trigger,
            Body::DialogueView(_) => Kind::DialogueView,
            Body::RespawnDown(_) => Kind::RespawnDown,
            Body::RespawnUp(_) => Kind::RespawnUp,
            Body::RespawnRest(_) => Kind::RespawnRest,
            Body::KillXp(_) => Kind::KillXp,
            Body::Save(_) => Kind::Save,
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
        Body::Equipment(e) => {
            if e.items.len() > MAX_EQUIP_ITEMS {
                return Err(Error::LimitExceeded {
                    what: "equipment items",
                    limit: MAX_EQUIP_ITEMS,
                    got: e.items.len(),
                });
            }
            w.write_u32(e.rev)?;
            w.write_u8(e.items.len() as u8)?;
            for it in &e.items {
                w.write_u8(it.hand)?;
                w.write_u8(it.mesh_kind)?;
                write_str(&mut w, &it.mesh_path)?;
                for v in it.xform {
                    w.write_i32(v)?;
                }
            }
        }
        Body::Status(st) => {
            w.write_u32(st.rev)?;
            w.write_u32(st.mask)?;
            if let Some(v) = st.vitals {
                w.write_u16(v.hp)?;
                w.write_u16(v.hp_max)?;
                w.write_u8(v.flags)?;
            }
        }
        Body::EnemyDetail(d) => {
            write_guid(&mut w, &d.id)?;
            w.write_u8(d.anim)?;
            w.write_u8(d.seq)?;
            w.write_u16(d.status)?;
            w.write_u8(d.boss)?;
            write_str(&mut w, &d.montage_path)?;
        }
        Body::Effect(e) | Body::EffectMove(e) => {
            w.write_u8(e.kind)?;
            w.write_u16(e.id)?;
            write_str(&mut w, &e.class_path)?;
            w.write_i32(e.x)?;
            w.write_i32(e.y)?;
            w.write_i32(e.z)?;
            w.write_i16(e.pitch)?;
            w.write_i16(e.yaw)?;
            w.write_i32(e.vx)?;
            w.write_i32(e.vy)?;
            w.write_i32(e.vz)?;
            w.write_u16(e.life_ms)?;
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
                IntentPayload::EnemyHit { spawner, damage_frac } => {
                    write_guid(&mut w, spawner)?;
                    w.write_i32(*damage_frac)?;
                }
                IntentPayload::EnemyRecord { spawner, respawn_centi, hp_centi } => {
                    write_guid(&mut w, spawner)?;
                    w.write_i32(*respawn_centi)?;
                    w.write_i32(*hp_centi)?;
                }
                IntentPayload::ObjectState { key, shape, state } => {
                    write_guid(&mut w, key)?;
                    w.write_u8(*shape)?;
                    w.write_u8(*state)?;
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
        Body::Travel(t) => {
            w.write_u32(t.seq)?;
            w.write_u8(t.role)?;
            w.write_u8(t.phase)?;
            write_str(&mut w, &t.map)?;
            write_str(&mut w, &t.dest)?;
        }
        Body::Damage(d) => {
            write_guid(&mut w, &d.attacker)?;
            w.write_i32(d.amount_centi)?;
            w.write_u8(d.flags)?;
        }
        Body::Cutscene(c) => {
            w.write_u32(c.seq)?;
            w.write_u8(c.slot)?;
            w.write_u8(c.state)?;
            write_str(&mut w, &c.name)?;
        }
        Body::Movers(m) => {
            w.write_u32(m.rev)?;
            write_count(&mut w, m.rows.len())?;
            for r in &m.rows {
                write_guid(&mut w, &r.id)?;
                w.write_i32(r.x)?;
                w.write_i32(r.y)?;
                w.write_i32(r.z)?;
                w.write_i16(r.yaw)?;
                w.write_i16(r.pitch)?;
                w.write_u8(r.flags)?;
            }
        }
        Body::PlatformRide(p) => {
            w.write_u32(p.ms)?;
            write_count(&mut w, p.rows.len())?;
            for r in &p.rows {
                write_guid(&mut w, &r.id)?;
                w.write_i32(r.ox)?;
                w.write_i32(r.oy)?;
                w.write_i32(r.oz)?;
                w.write_i16(r.oyaw)?;
                w.write_u8(r.flags)?;
            }
        }
        Body::Loot(l) => {
            w.write_u32(l.rev)?;
            write_count(&mut w, l.entries.len())?;
            for e in &l.entries {
                w.write_u32(e.source_hash)?;
                w.write_i32(e.drop_index)?;
                w.write_u8(u8::from(e.taken))?;
                w.write_u8(e.flags)?;
                w.write_i16(e.yaw)?;
                w.write_i32(e.px)?;
                w.write_i32(e.py)?;
                w.write_i32(e.pz)?;
                write_str(&mut w, &e.item_path)?;
            }
        }
        Body::Suspend(s) => {
            w.write_u32(s.seq)?;
            w.write_u8(s.state)?;
            write_str(&mut w, &s.slot)?;
        }
        Body::Boss(b) => {
            write_guid(&mut w, &b.id)?;
            w.write_u8(b.flags)?;
            w.write_u8(b.phase)?;
            w.write_u16(b.hp)?;
        }
        Body::Objects(o) => {
            if o.entries.len() > MAX_OBJECT_ROWS {
                return Err(Error::LimitExceeded {
                    what: "object rows per datagram",
                    limit: MAX_OBJECT_ROWS,
                    got: o.entries.len(),
                });
            }
            w.write_u32(o.rev)?;
            write_count(&mut w, o.entries.len())?;
            for e in &o.entries {
                write_guid(&mut w, &e.key)?;
                w.write_u8(e.shape)?;
                w.write_u8(e.state)?;
            }
        }
        Body::EnemySpawn(s) => {
            write_guid(&mut w, &s.id)?;
            w.write_u8(s.flags)?;
            w.write_i32(s.px)?;
            w.write_i32(s.py)?;
            w.write_i32(s.pz)?;
            w.write_i16(s.yaw)?;
            w.write_u16(s.hp)?;
            write_str(&mut w, &s.class_path)?;
        }
        Body::Trigger(t) => {
            write_guid(&mut w, &t.key)?;
            w.write_u8(t.event)?;
            w.write_u8(t.volume)?;
            w.write_u8(t.flags)?;
            w.write_u32(t.px.to_bits())?;
            w.write_u32(t.py.to_bits())?;
            w.write_u32(t.pz.to_bits())?;
            write_str(&mut w, &t.name)?;
        }
        Body::DialogueView(d) => {
            w.write_u32(d.seq)?;
            write_guid(&mut w, &d.npc)?;
            w.write_u8(d.slot)?;
            w.write_u8(d.flags)?;
            w.write_u16(d.sequence)?;
            w.write_u16(d.entry)?;
            w.write_u16(d.options)?;
            w.write_u16(d.chosen)?;
            write_str(&mut w, &d.asset)?;
        }
        Body::RespawnDown(b) => {
            check_respawn_stack(b.deaths, b.pct)?;
            w.write_u8(b.deaths)?;
            w.write_u8(b.pct)?;
            w.write_u8(b.seq)?;
            w.write_i32(b.px)?;
            w.write_i32(b.py)?;
            w.write_i32(b.pz)?;
        }
        Body::RespawnUp(b) => {
            check_respawn_stack(b.deaths, b.pct)?;
            if b.hp > b.hp_max {
                return Err(Error::OutOfRange {
                    what: "respawn hp above hp_max",
                    value: i64::from(b.hp),
                });
            }
            w.write_u8(b.deaths)?;
            w.write_u8(b.pct)?;
            w.write_u8(b.seq)?;
            w.write_i32(b.px)?;
            w.write_i32(b.py)?;
            w.write_i32(b.pz)?;
            w.write_i16(b.yaw)?;
            w.write_u16(b.hp)?;
            w.write_u16(b.hp_max)?;
            write_str(&mut w, &b.name)?;
        }
        Body::RespawnRest(b) => {
            check_respawn_stack(b.deaths, b.pct)?;
            if b.deaths != 0 {
                return Err(Error::OutOfRange {
                    what: "respawn rest with stacks",
                    value: i64::from(b.deaths),
                });
            }
            w.write_u8(b.deaths)?;
            w.write_u8(b.pct)?;
            w.write_u8(b.seq)?;
            w.write_i32(b.px)?;
            w.write_i32(b.py)?;
            w.write_i32(b.pz)?;
            write_str(&mut w, &b.name)?;
        }
        Body::KillXp(k) => {
            write_guid(&mut w, &k.id)?;
            w.write_u16(k.seq)?;
            w.write_u32(k.xp)?;
        }
        Body::Save(s) => {
            w.write_u32(s.seq)?;
            w.write_u8(s.role)?;
            w.write_u8(s.route)?;
        }
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

/// The death lane's one cross-field rule (WIRE.md §3.24): a stack and a percentage that
/// disagree describe a state neither machine can have, so a body carrying them is refused
/// rather than applied. Shared by the encoder and the decoder so the two cannot drift.
fn check_respawn_stack(deaths: u8, pct: u8) -> Result<()> {
    if deaths > respawn_penalty::MAX_STACKS {
        return Err(Error::OutOfRange { what: "respawn stacks", value: i64::from(deaths) });
    }
    if pct != respawn_penalty::pct_for(deaths) {
        return Err(Error::OutOfRange { what: "respawn pct", value: i64::from(pct) });
    }
    Ok(())
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
        // The three absolute containers below are CHUNKED (WIRE.md §3.6): the header's
        // `n` is the size of the sender's whole container, while one datagram carries a
        // PREFIX of it and the rest follows on later ticks.  So the loop stops when the
        // body ends, not at `n` -- reading `n` entries is the bug this fix removes: the
        // everyday 117-entry SavableActorStates on this build is three datagrams, and
        // every one of them was dropped here as "unexpected end of data", which is why
        // no peer ever logged `actor states applied=`.
        //
        // A body that ends in the MIDDLE of an entry is still a framing error: the
        // sender never splits an entry across datagrams.
        Kind::WorldFlags => {
            let rev = r.read_u32()?;
            let n = r.read_u16()? as usize;
            let mut entries = Vec::new();
            for _ in 0..n {
                if r.is_empty() {
                    break;
                }
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
                if r.is_empty() {
                    break;
                }
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
                if r.is_empty() {
                    break;
                }
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
                // [live-ip2] Same rule as OBJECTS below: a sender whose set has more rows
                // than one datagram holds caps the body at `MAX_ENEMY_ROWS` (qdwire.h
                // `WriteEnemyChunk`) while the header still says how big the set is, so a
                // chunk carries fewer rows than `n`.  Without this stop the RELAY rejected
                // those datagrams and neither peer saw them (measured: 301 `malformed
                // datagram ... (kind 0x14)` in one run).
                if r.is_empty() {
                    break;
                }
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
        Kind::Equipment => {
            let rev = r.read_u32()?;
            let n = usize::from(r.read_u8()?);
            if n > MAX_EQUIP_ITEMS {
                return Err(Error::LimitExceeded { what: "equipment items", limit: MAX_EQUIP_ITEMS, got: n });
            }
            let mut items = Vec::with_capacity(n);
            for _ in 0..n {
                let hand = r.read_u8()?;
                let mesh_kind = r.read_u8()?;
                let mesh_path = read_str(&mut r)?;
                let mut xform = [0i32; 10];
                for v in xform.iter_mut() {
                    *v = r.read_i32()?;
                }
                items.push(EquipItem { hand, mesh_kind, mesh_path, xform });
            }
            Body::Equipment(Equipment { rev, items })
        }
        Kind::Status => {
            let rev = r.read_u32()?;
            let mask = r.read_u32()?;
            let vitals = if r.is_empty() {
                None
            } else {
                Some(Vitals { hp: r.read_u16()?, hp_max: r.read_u16()?, flags: r.read_u8()? })
            };
            Body::Status(Status { rev, mask, vitals })
        }
        Kind::EnemyDetail => Body::EnemyDetail(EnemyDetail {
            id: read_guid(&mut r)?,
            anim: r.read_u8()?,
            seq: r.read_u8()?,
            status: r.read_u16()?,
            boss: r.read_u8()?,
            montage_path: read_str(&mut r)?,
        }),
        Kind::Effect | Kind::EffectMove => {
            let e = Effect {
                kind: r.read_u8()?,
                id: r.read_u16()?,
                class_path: read_str(&mut r)?,
                x: r.read_i32()?,
                y: r.read_i32()?,
                z: r.read_i32()?,
                pitch: r.read_i16()?,
                yaw: r.read_i16()?,
                vx: r.read_i32()?,
                vy: r.read_i32()?,
                vz: r.read_i32()?,
                life_ms: r.read_u16()?,
            };
            if kind == Kind::Effect { Body::Effect(e) } else { Body::EffectMove(e) }
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
                7 => IntentPayload::EnemyHit {
                    spawner: read_guid(&mut r)?,
                    damage_frac: r.read_i32()?,
                },
                8 => IntentPayload::EnemyRecord {
                    spawner: read_guid(&mut r)?,
                    respawn_centi: r.read_i32()?,
                    hp_centi: r.read_i32()?,
                },
                9 => {
                    let key = read_guid(&mut r)?;
                    let shape = r.read_u8()?;
                    let state = r.read_u8()?;
                    if shape > object_shape::ACTIVATABLE {
                        return Err(Error::OutOfRange {
                            what: "object shape",
                            value: i64::from(shape),
                        });
                    }
                    if state > 1 {
                        return Err(Error::OutOfRange {
                            what: "object state",
                            value: i64::from(state),
                        });
                    }
                    IntentPayload::ObjectState { key, shape, state }
                }
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
        Kind::Travel => Body::Travel(Travel {
            seq: r.read_u32()?,
            role: r.read_u8()?,
            phase: r.read_u8()?,
            map: read_str(&mut r)?,
            dest: read_str(&mut r)?,
        }),
        Kind::Damage => Body::Damage(Damage {
            attacker: read_guid(&mut r)?,
            amount_centi: r.read_i32()?,
            flags: r.read_u8()?,
        }),
        Kind::Cutscene => {
            let seq = r.read_u32()?;
            let slot = r.read_u8()?;
            if slot != DIALOGUE_SLOT_FREE && slot > slots::PEER {
                return Err(Error::OutOfRange { what: "cutscene slot", value: i64::from(slot) });
            }
            let state = r.read_u8()?;
            if state > cutscene_state::ENDED {
                return Err(Error::OutOfRange { what: "cutscene state", value: i64::from(state) });
            }
            Body::Cutscene(Cutscene { seq, slot, state, name: read_str(&mut r)? })
        }
        Kind::Movers => {
            let rev = r.read_u32()?;
            let n = r.read_u16()? as usize;
            let mut rows = Vec::with_capacity(n.min(MAX_ENEMY_ROWS));
            // Same prefix rule as the other absolute containers (WIRE.md 3.6/3.17).
            for _ in 0..n {
                if r.is_empty() {
                    break;
                }
                rows.push(MoverRow {
                    id: read_guid(&mut r)?,
                    x: r.read_i32()?,
                    y: r.read_i32()?,
                    z: r.read_i32()?,
                    yaw: r.read_i16()?,
                    pitch: r.read_i16()?,
                    flags: r.read_u8()?,
                });
            }
            Body::Movers(Movers { rev, rows })
        }
        Kind::PlatformRide => {
            let ms = r.read_u32()?;
            let n = r.read_u16()? as usize;
            let mut rows = Vec::with_capacity(n.min(4));
            // Same prefix rule as the other absolute containers (WIRE.md 3.6/3.17).
            for _ in 0..n {
                if r.is_empty() {
                    break;
                }
                rows.push(PlatformRideRow {
                    id: read_guid(&mut r)?,
                    ox: r.read_i32()?,
                    oy: r.read_i32()?,
                    oz: r.read_i32()?,
                    oyaw: r.read_i16()?,
                    flags: r.read_u8()?,
                });
            }
            Body::PlatformRide(PlatformRide { ms, rows })
        }
        Kind::Loot => {
            let rev = r.read_u32()?;
            let n = r.read_u16()? as usize;
            let mut entries = Vec::with_capacity(n.min(MAX_ENEMY_ROWS));
            // `n` is the sender's LEDGER size; the datagram carries a prefix (WIRE.md 3.6).
            // Measured Session 9: a 68-entry ledger is 5 datagrams, and reading `n` entries
            // made every one of them "malformed" -- the lane's tombstones never crossed.
            for _ in 0..n {
                if r.is_empty() {
                    break;
                }
                entries.push(LootEntry {
                    source_hash: r.read_u32()?,
                    drop_index: r.read_i32()?,
                    taken: r.read_u8()? != 0,
                    flags: r.read_u8()?,
                    yaw: r.read_i16()?,
                    px: r.read_i32()?,
                    py: r.read_i32()?,
                    pz: r.read_i32()?,
                    item_path: read_str(&mut r)?,
                });
            }
            Body::Loot(Loot { rev, entries })
        }
        Kind::Suspend => {
            let seq = r.read_u32()?;
            let state = r.read_u8()?;
            if state > suspend_state::REAPPLIED {
                return Err(Error::OutOfRange { what: "suspend state", value: i64::from(state) });
            }
            Body::Suspend(Suspend { seq, state, slot: read_str(&mut r)? })
        }
        Kind::Boss => Body::Boss(Boss {
            id: read_guid(&mut r)?,
            flags: r.read_u8()?,
            phase: r.read_u8()?,
            hp: r.read_u16()?,
        }),
        Kind::Objects => {
            let rev = r.read_u32()?;
            let n = r.read_u16()? as usize;
            let mut entries = Vec::new();
            for _ in 0..n {
                // [live-ip2] MEASURED (round 3, `_test/*/out/relay-*.log`): `n` is the WHOLE
                // set size, not this datagram's share (WIRE.md §3.20), so a set larger than
                // one 1200-byte body arrives as several datagrams each carrying fewer rows
                // than `n`.  Without this stop the decoder demanded `n` full rows from every
                // chunk, failed, and the RELAY dropped the datagram before either peer saw
                // it: 624 `malformed datagram ... (kind 0x2a): needed 128 bits, 0 available`
                // in one run.  ACTOR_STATES and INVENTORY already had this guard; OBJECTS
                // did not, which is why the host -> peer object leg never crossed.
                if r.is_empty() {
                    break;
                }
                let key = read_guid(&mut r)?;
                let shape = r.read_u8()?;
                let state = r.read_u8()?;
                if shape > object_shape::ACTIVATABLE {
                    return Err(Error::OutOfRange { what: "object shape", value: i64::from(shape) });
                }
                if state > 1 {
                    return Err(Error::OutOfRange { what: "object state", value: i64::from(state) });
                }
                entries.push(ObjectRow { key, shape, state });
            }
            Body::Objects(Objects { rev, entries })
        }
        Kind::EnemySpawn => {
            let id = read_guid(&mut r)?;
            let flags = r.read_u8()?;
            if flags & !enemy_spawn_flags::MIRROR != 0 {
                return Err(Error::OutOfRange { what: "enemy spawn flags", value: i64::from(flags) });
            }
            let px = r.read_i32()?;
            let py = r.read_i32()?;
            let pz = r.read_i32()?;
            let yaw = r.read_i16()?;
            let hp = r.read_u16()?;
            let class_path = read_str(&mut r)?;
            if class_path.is_empty() {
                // An empty path is not a class: refuse it here rather than at the spawn,
                // so a malformed body costs a datagram and not an engine call.
                return Err(Error::OutOfRange { what: "enemy spawn class path", value: 0 });
            }
            Body::EnemySpawn(EnemySpawn { id, flags, px, py, pz, yaw, hp, class_path })
        }
        Kind::Trigger => {
            let key = read_guid(&mut r)?;
            let event = r.read_u8()?;
            if event != trigger_event::BEGIN && event != trigger_event::END {
                return Err(Error::OutOfRange { what: "trigger event", value: i64::from(event) });
            }
            let volume = r.read_u8()?;
            let paired = match volume {
                trigger_volume::BOX | trigger_volume::SPHERE => true,
                trigger_volume::TRAP_ON => event == trigger_event::BEGIN,
                trigger_volume::TRAP_OFF => event == trigger_event::END,
                _ => false,
            };
            if !paired {
                return Err(Error::OutOfRange { what: "trigger volume", value: i64::from(volume) });
            }
            let flags = r.read_u8()?;
            if flags & !trigger_flags::BOSS != 0 {
                return Err(Error::OutOfRange { what: "trigger flags", value: i64::from(flags) });
            }
            let px = f32::from_bits(r.read_u32()?);
            let py = f32::from_bits(r.read_u32()?);
            let pz = f32::from_bits(r.read_u32()?);
            for v in [px, py, pz] {
                // The receiver may move its player to this point: a NaN or a coordinate
                // past any map is refused here, not at the teleport.
                if !v.is_finite() || v.abs() >= 1e9 {
                    return Err(Error::OutOfRange { what: "trigger position", value: 0 });
                }
            }
            let name = read_str(&mut r)?;
            Body::Trigger(Trigger { key, event, volume, flags, px, py, pz, name })
        }
        Kind::DialogueView => {
            let seq = r.read_u32()?;
            let npc = read_guid(&mut r)?;
            let slot = r.read_u8()?;
            if slot != DIALOGUE_SLOT_FREE && slot > slots::PEER {
                return Err(Error::OutOfRange { what: "dialogue_view slot", value: i64::from(slot) });
            }
            let flags = r.read_u8()?;
            if flags & !(dialogue_view_flags::OPTIONS
                | dialogue_view_flags::CHOSEN
                | dialogue_view_flags::HIDE_NAME)
                != 0
            {
                return Err(Error::OutOfRange { what: "dialogue_view flags", value: i64::from(flags) });
            }
            Body::DialogueView(DialogueView {
                seq,
                npc,
                slot,
                flags,
                sequence: r.read_u16()?,
                entry: r.read_u16()?,
                options: r.read_u16()?,
                chosen: r.read_u16()?,
                asset: read_str(&mut r)?,
            })
        }
        Kind::RespawnDown => {
            let deaths = r.read_u8()?;
            let pct = r.read_u8()?;
            check_respawn_stack(deaths, pct)?;
            Body::RespawnDown(RespawnDown {
                deaths,
                pct,
                seq: r.read_u8()?,
                px: r.read_i32()?,
                py: r.read_i32()?,
                pz: r.read_i32()?,
            })
        }
        Kind::RespawnUp => {
            let deaths = r.read_u8()?;
            let pct = r.read_u8()?;
            check_respawn_stack(deaths, pct)?;
            let seq = r.read_u8()?;
            let px = r.read_i32()?;
            let py = r.read_i32()?;
            let pz = r.read_i32()?;
            let yaw = r.read_i16()?;
            let hp = r.read_u16()?;
            let hp_max = r.read_u16()?;
            if hp > hp_max {
                return Err(Error::OutOfRange {
                    what: "respawn hp above hp_max",
                    value: i64::from(hp),
                });
            }
            Body::RespawnUp(RespawnUp { deaths, pct, seq, px, py, pz, yaw, hp, hp_max, name: read_str(&mut r)? })
        }
        Kind::RespawnRest => {
            let deaths = r.read_u8()?;
            let pct = r.read_u8()?;
            check_respawn_stack(deaths, pct)?;
            if deaths != 0 {
                return Err(Error::OutOfRange {
                    what: "respawn rest with stacks",
                    value: i64::from(deaths),
                });
            }
            Body::RespawnRest(RespawnRest {
                deaths,
                pct,
                seq: r.read_u8()?,
                px: r.read_i32()?,
                py: r.read_i32()?,
                pz: r.read_i32()?,
                name: read_str(&mut r)?,
            })
        }
        Kind::KillXp => {
            let id = read_guid(&mut r)?;
            let seq = r.read_u16()?;
            let xp = r.read_u32()?;
            // An award of nothing is not an award, and an implausible one is a framing
            // error: refuse both here rather than let the native side pay for it.
            if xp == 0 || xp > 1_000_000 {
                return Err(Error::OutOfRange { what: "kill_xp award", value: i64::from(xp) });
            }
            Body::KillXp(KillXp { id, seq, xp })
        }
        Kind::Save => {
            let seq = r.read_u32()?;
            let role = r.read_u8()?;
            if role > save_role::PEER {
                return Err(Error::OutOfRange { what: "save role", value: i64::from(role) });
            }
            // A 5-byte body (seq+role) predates the route field and means the main slot;
            // refusing it would lose a partner's save over a diagnostic choice.
            let route = if r.is_empty() { save_route::SLOT } else { r.read_u8()? };
            let route = if route == save_route::UNSPECIFIED { save_route::SLOT } else { route };
            if route > save_route::SUSPEND {
                return Err(Error::OutOfRange { what: "save route", value: i64::from(route) });
            }
            Body::Save(Save { seq, role, route })
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

    #[test]
    fn kill_xp_and_save_round_trip() {
        // The two [progression] kinds. The native encoder in mod/native/src/qdwire.h is the
        // second implementation; this is the one the relay validates with, so a mismatch
        // here is a datagram the relay rejects rather than a silent drift.
        let k = KillXp { id: sample_guid(), seq: 3, xp: 40 };
        let bytes = encode_body(&Body::KillXp(k)).unwrap();
        assert_eq!(bytes.len(), 22);
        assert_eq!(decode_body(Kind::KillXp, &bytes).unwrap(), Body::KillXp(k));
        // An award of nothing and an implausible award are refused, not carried.
        let zero = KillXp { id: sample_guid(), seq: 0, xp: 0 };
        assert!(decode_body(Kind::KillXp, &encode_body(&Body::KillXp(zero)).unwrap()).is_err());

        let s = Save { seq: 7, role: save_role::PEER, route: save_route::SUSPEND };
        let bytes = encode_body(&Body::Save(s)).unwrap();
        assert_eq!(bytes.len(), 6);
        assert_eq!(decode_body(Kind::Save, &bytes).unwrap(), Body::Save(s));
        // A role outside the two the build knows is a framing error.
        let bad = Save { seq: 1, role: 9, route: save_route::SLOT };
        assert!(decode_body(Kind::Save, &encode_body(&Body::Save(bad)).unwrap()).is_err());
        // A route outside the two the build knows is a framing error too.
        let bad_route = Save { seq: 1, role: save_role::HOST, route: 9 };
        assert!(decode_body(Kind::Save, &encode_body(&Body::Save(bad_route)).unwrap()).is_err());
        // A 5-byte body predates `route` and means the main slot, not a refusal.
        let short = bytes[..5].to_vec();
        match decode_body(Kind::Save, &short).unwrap() {
            Body::Save(b) => assert_eq!(b.route, save_route::SLOT),
            other => panic!("expected save, got {other:?}"),
        }
    }

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
            Body::Equipment(Equipment {
                rev: 2,
                items: vec![EquipItem {
                    hand: 0,
                    mesh_kind: 1,
                    mesh_path: "/Game/Items/Weapons/RustedSword/RustySword.RustySword".into(),
                    xform: [100, -250, 0, 0, 0, 707107, 707107, 1000, 1000, 1000],
                }],
            }),
            Body::Equipment(Equipment { rev: 3, items: vec![] }),
            Body::Effect(Effect {
                kind: effect_kind::PROJECTILE_SPAWNED,
                id: 7,
                class_path: "/Game/Items/Weapons/Daggers/Fireball/Fireball_Projectile_BP.Fireball_Projectile_BP_C".into(),
                x: 32_680,
                y: -15_672,
                z: 1_560,
                pitch: -1_200,
                yaw: -7_300,
                vx: 6_528,
                vy: -5_648,
                vz: -3_496,
                life_ms: 6_000,
            }),
            Body::EffectMove(Effect {
                kind: effect_kind::PROJECTILE_MOVED,
                id: 7,
                x: 40_000,
                y: -21_000,
                z: 1_300,
                vx: 6_528,
                vy: -5_648,
                ..Default::default()
            }),
            Body::Effect(Effect { kind: effect_kind::PROJECTILE_ENDED, id: 7, x: 59_584, y: -29_744, z: 5_784, ..Default::default() }),
            Body::Status(Status { rev: 4, mask: status_bit::MAGIC_FIRE | status_bit::POISON, vitals: None }),
            Body::Status(Status {
                rev: 5,
                mask: 0,
                vitals: Some(Vitals { hp: 37, hp_max: 120, flags: vitals_flag::VALID }),
            }),
            Body::Travel(Travel {
                seq: 0x123,
                role: roles::HOST,
                phase: travel_phase::NONE,
                map: "100_GR".into(),
                dest: "Teleporter_500_AUT_02/100_GR".into(),
            }),
            Body::Travel(Travel {
                seq: 0x124,
                role: roles::HOST,
                phase: travel_phase::HARD_BEGIN,
                map: "900_SipehaWorld".into(),
                dest: "/Game/RuinsLevel/Maps/900_SipehaWorld.900_SipehaWorld".into(),
            }),
            Body::EnemyDetail(EnemyDetail {
                id: sample_guid(),
                anim: enemy_anim::ATTACK,
                seq: 7,
                status: status_bit::POISON as u16,
                boss: 0,
                montage_path: "/Game/Anim/Enemy/Slash_Montage".into(),
            }),
            Body::EnemyDetail(EnemyDetail {
                id: sample_guid(),
                anim: enemy_anim::NONE,
                seq: 8,
                status: 0,
                boss: 2,
                montage_path: String::new(),
            }),
            Body::Damage(Damage { attacker: sample_guid(), amount_centi: 3500, flags: damage_flags::MELEE }),
            Body::Cutscene(Cutscene {
                seq: 2,
                slot: slots::HOST,
                state: cutscene_state::PLAYING,
                name: "SipehaTransitionCutsceneManager".into(),
            }),
            Body::Movers(Movers {
                rev: 5,
                rows: vec![MoverRow {
                    id: sample_guid(),
                    x: 100,
                    y: -200,
                    z: 50,
                    yaw: 16384,
                    pitch: -4096,
                    flags: mover_flags::MOVING | mover_flags::OWNER,
                }],
            }),
            Body::Loot(Loot {
                rev: 9,
                entries: vec![
                    LootEntry {
                        source_hash: 0xDEAD_BEEF,
                        drop_index: 2,
                        taken: true,
                        flags: 0,
                        yaw: 0,
                        px: 0,
                        py: 0,
                        pz: 0,
                        item_path: String::new(),
                    },
                    LootEntry {
                        source_hash: 0x0BAD_F00D,
                        drop_index: 0,
                        taken: false,
                        flags: loot_flags::SPAWN,
                        yaw: -8192,
                        px: -123_456,
                        py: 78_910,
                        pz: -7,
                        item_path: "/Game/Items/Consumables/Herb/Herb_Item_BP.Herb_Item_BP_C"
                            .into(),
                    },
                ],
            }),
            Body::Suspend(Suspend {
                seq: 3,
                state: suspend_state::HOLD,
                slot: "World0_Suspend_1".into(),
            }),
            Body::Boss(Boss {
                id: sample_guid(),
                flags: boss_flags::BAR_ON,
                phase: 2,
                hp: 40_000,
            }),
            Body::Boss(Boss {
                id: Guid::default(),
                flags: boss_flags::BAR_OFF | boss_flags::ARENA_EXIT,
                phase: 0,
                hp: 0,
            }),
            Body::Objects(Objects {
                rev: 7,
                entries: vec![ObjectRow {
                    key: sample_guid(),
                    shape: object_shape::CHEST,
                    state: 1,
                }],
            }),
            Body::EnemySpawn(EnemySpawn {
                id: sample_guid(),
                flags: enemy_spawn_flags::MIRROR,
                px: 100,
                py: -200,
                pz: 50,
                yaw: 16384,
                hp: 40_000,
                class_path: "/Game/Enemies/Dragon/Dragon_Green_BP.Dragon_Green_BP_C".into(),
            }),
            Body::Trigger(Trigger {
                key: sample_guid(),
                event: trigger_event::BEGIN,
                volume: trigger_volume::BOX,
                flags: trigger_flags::BOSS,
                px: -27846.5,
                py: -15403.25,
                pz: -15339.0,
                name: "ActivationTrigger11".into(),
            }),
            Body::Trigger(Trigger {
                key: sample_guid(),
                event: trigger_event::END,
                volume: trigger_volume::TRAP_OFF,
                flags: 0,
                px: 1.0,
                py: 2.0,
                pz: 3.0,
                name: "WhaleKingSpawner".into(),
            }),
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

    /// WIRE.md §3.6: `n` is the size of the sender's container, and one datagram carries
    /// a PREFIX of it. Reading `n` entries unconditionally was measured on the rig: a
    /// 117-entry SavableActorStates is two or three datagrams, and every one of them was
    /// refused here as "unexpected end of data", so the relay dropped the whole
    /// actor-state lane while every other body forwarded. This pins the prefix rule and
    /// the fact that a body cut INSIDE an entry is still a framing error.
    #[test]
    fn a_chunked_container_body_decodes_the_entries_it_carries() {
        // n = 117 (the whole container), two entries present.
        let mut b: Vec<u8> = Vec::new();
        b.extend_from_slice(&9u32.to_le_bytes());
        b.extend_from_slice(&117u16.to_le_bytes());
        for i in 0..2u32 {
            b.extend_from_slice(sample_guid().as_bytes());
            b.extend_from_slice(&(i as i32).to_le_bytes());
        }
        let body = decode_body(Kind::ActorStates, &b).expect("a chunk must decode");
        match body {
            Body::ActorStates(s) => {
                assert_eq!(s.rev, 9);
                assert_eq!(s.entries.len(), 2);
                assert_eq!(s.entries[1].value, 1);
            }
            other => panic!("expected actor states, got {other:?}"),
        }

        // The same rule for the other two chunked containers.
        let mut f: Vec<u8> = Vec::new();
        f.extend_from_slice(&5u32.to_le_bytes());
        f.extend_from_slice(&40u16.to_le_bytes());
        f.extend_from_slice(&1u16.to_le_bytes());
        f.extend_from_slice(b"A");
        f.extend_from_slice(&7i32.to_le_bytes());
        match decode_body(Kind::WorldFlags, &f).expect("a flags chunk must decode") {
            Body::WorldFlags(w) => assert_eq!(w.entries.len(), 1),
            other => panic!("expected world flags, got {other:?}"),
        }

        let mut i: Vec<u8> = Vec::new();
        i.extend_from_slice(&3u32.to_le_bytes());
        i.extend_from_slice(&9u16.to_le_bytes());
        i.extend_from_slice(&9u16.to_le_bytes());
        i.extend_from_slice(b"/Game/A.B");
        i.extend_from_slice(&2i32.to_le_bytes());
        match decode_body(Kind::Inventory, &i).expect("an inventory chunk must decode") {
            Body::Inventory(v) => assert_eq!(v.entries.len(), 1),
            other => panic!("expected inventory, got {other:?}"),
        }

        // Cut in the MIDDLE of the second entry: still malformed, not a silent short read.
        let mut truncated = b;
        truncated.truncate(truncated.len() - 3);
        assert!(decode_body(Kind::ActorStates, &truncated).is_err());
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
        // 9 is the placed-object intent now, so the unknown value has moved to 10: this
        // test's point is that a kind the receiver has no layout for is refused rather
        // than decoded as whatever the bytes happen to line up with.
        let mut w = BitWriter::new();
        w.write_u8(10).unwrap();
        w.write_u32(1).unwrap();
        let bytes = w.finish();
        assert!(matches!(decode_body(Kind::Intent, &bytes), Err(Error::UnknownIntentKind(10))));
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
        for k in [Kind::Hello, Kind::Welcome, Kind::Intent, Kind::Effect] {
            assert!(k.is_reliable(), "{k:?} must be reliable");
            assert!(!k.is_latest_wins());
        }
        for k in [
            Kind::PlayerRow,
            Kind::WorldFlags,
            Kind::ActorStates,
            Kind::Inventory,
            Kind::EnemyRows,
            Kind::Equipment,
            Kind::EffectMove,
            Kind::Status,
            Kind::DialogueState,
            Kind::LevelUnion,
            Kind::DialogueView,
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
    fn dialogue_view_rejects_a_bad_slot_and_unknown_flags() {
        let body = Body::DialogueView(DialogueView {
            seq: 1,
            slot: slots::PEER,
            flags: dialogue_view_flags::OPTIONS,
            asset: "Merchant_Dialogue".to_owned(),
            ..Default::default()
        });
        let bytes = encode_body(&body).unwrap();
        assert_eq!(decode_body(Kind::DialogueView, &bytes).unwrap(), body);

        // Slot: only 0, 1 and "free" mean anything.
        let mut bad_slot = bytes.clone();
        bad_slot[4 + 16] = 2;               // seq(4) + guid(16), then slot
        assert!(matches!(
            decode_body(Kind::DialogueView, &bad_slot),
            Err(Error::OutOfRange { what: "dialogue_view slot", .. })
        ));

        // Flags: a bit this build cannot interpret is refused, not ignored.
        let mut bad_flags = bytes;
        bad_flags[4 + 16 + 1] = 0x80;
        assert!(matches!(
            decode_body(Kind::DialogueView, &bad_flags),
            Err(Error::OutOfRange { what: "dialogue_view flags", .. })
        ));
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

    #[test]
    fn enemy_identity_source_rides_the_spare_flag_bits() {
        // The pair (source, id) is the identity, which is what lets a placed-path hash
        // and a spawner GUID that happen to collide stay two different enemies.  The
        // four existing state bits must be untouched by the source, or ownership and
        // liveness would corrupt each other.
        let states = enemy_flags::ALIVE | enemy_flags::DRIVEN | enemy_flags::HAS_BODY
            | enemy_flags::PEER_OWNS;
        for src in [
            enemy_id_source::SPAWNER,
            enemy_id_source::PLACED,
            enemy_id_source::SUMMON,
        ] {
            let f = enemy_id_source::with(states, src);
            assert_eq!(enemy_id_source::of(f), src);
            assert_eq!(f & !enemy_flags::SRC_MASK, states, "state bits changed");
        }

        // The three sources must be distinct byte values, or a placed key and a spawner
        // key would be indistinguishable on the wire.
        assert_ne!(
            enemy_id_source::with(0, enemy_id_source::SPAWNER),
            enemy_id_source::with(0, enemy_id_source::PLACED)
        );
        assert_ne!(
            enemy_id_source::with(0, enemy_id_source::PLACED),
            enemy_id_source::with(0, enemy_id_source::SUMMON)
        );

        // Re-tagging an already-tagged row replaces the source and keeps the state.
        let f = enemy_id_source::with(enemy_id_source::with(states, enemy_id_source::PLACED), enemy_id_source::SPAWNER);
        assert_eq!(enemy_id_source::of(f), enemy_id_source::SPAWNER);
        assert_eq!(f & !enemy_flags::SRC_MASK, states);
    }

    #[test]
    fn boss_body_is_twenty_bytes_and_roundtrips() {
        let b = Boss {
            id: sample_guid(),
            flags: boss_flags::BAR_ON | boss_flags::ARENA_EXIT,
            phase: 3,
            hp: 12_345,
        };
        let bytes = encode_body(&Body::Boss(b)).unwrap();
        // 16 guid + 1 flags + 1 phase + 2 hp
        assert_eq!(bytes.len(), 20);
        assert_eq!(decode_body(Kind::Boss, &bytes).unwrap(), Body::Boss(b));
    }

    #[test]
    fn trigger_refuses_an_impossible_volume_event_pair_and_a_bad_position() {
        let good = Trigger {
            key: sample_guid(),
            event: trigger_event::BEGIN,
            volume: trigger_volume::TRAP_ON,
            flags: trigger_flags::BOSS,
            px: 10.0,
            py: 20.0,
            pz: 30.0,
            name: "T".into(),
        };
        let bytes = encode_body(&Body::Trigger(good.clone())).unwrap();
        assert_eq!(decode_body(Kind::Trigger, &bytes).unwrap(), Body::Trigger(good.clone()));
        assert!(Kind::Trigger.is_reliable());
        // A begin on the deactivate trap names an event the spawner does not have.
        let mut bad = bytes.clone();
        bad[17] = trigger_volume::TRAP_OFF;
        assert!(matches!(decode_body(Kind::Trigger, &bad), Err(Error::OutOfRange { .. })));
        // An unknown volume.
        let mut bad = bytes.clone();
        bad[17] = 4;
        assert!(matches!(decode_body(Kind::Trigger, &bad), Err(Error::OutOfRange { .. })));
        // A NaN x: the receiver may move its player there, so it is refused at decode.
        let mut bad = bytes.clone();
        bad[19..23].copy_from_slice(&f32::NAN.to_bits().to_le_bytes());
        assert!(matches!(decode_body(Kind::Trigger, &bad), Err(Error::OutOfRange { .. })));
        // An unknown flag bit.
        let mut bad = bytes;
        bad[18] = 0x80;
        assert!(matches!(decode_body(Kind::Trigger, &bad), Err(Error::OutOfRange { .. })));
    }

    #[test]
    fn platform_ride_round_trips_and_is_latest_wins() {
        // The rider lane is a STATE lane: a lost sample is repaired by the next one, and a
        // stale one must never win (it would put the partner back on a platform they left).
        assert!(!Kind::PlatformRide.is_reliable());
        assert!(Kind::PlatformRide.is_latest_wins());
        assert_eq!(Kind::PlatformRide.as_u8(), 0x48);
        assert_eq!(Kind::from_u8(0x48).unwrap(), Kind::PlatformRide);
        assert_eq!(Kind::PlatformRide.name(), "platform_ride");

        let b = PlatformRide {
            ms: 1234,
            rows: vec![PlatformRideRow {
                id: sample_guid(),
                ox: 120,
                oy: -64,
                oz: 8,
                oyaw: 16384,
                flags: platform_ride_flags::ON | platform_ride_flags::MOVING,
            }],
        };
        let bytes = encode_body(&Body::PlatformRide(b.clone())).unwrap();
        // 4 (ms) + 2 (n) + 16 + 12 + 2 + 1 = 37.
        assert_eq!(bytes.len(), 37);
        assert_eq!(
            decode_body(Kind::PlatformRide, &bytes).unwrap(),
            Body::PlatformRide(b.clone())
        );
        // A body that ends mid-row is a framing error, not a short read.
        assert!(decode_body(Kind::PlatformRide, &bytes[..30]).is_err());
    }

    #[test]
    fn boss_is_reliable_and_not_latest_wins() {
        // A boss bar that fails to appear because a datagram was lost is exactly the
        // failure a player reports as "the boss fight is broken", so this lane is
        // reliable -- and therefore must not also be in the superseded lane.
        assert!(Kind::Boss.is_reliable());
        assert!(!Kind::Boss.is_latest_wins());
    }

    #[test]
    fn enemy_spawn_field_order_is_id_flags_pos_yaw_hp_path() {
        // The class path is the only thing a row cannot carry, so its position and its
        // length prefix are what a receiver depends on: a shifted field would construct a
        // copy of the wrong class at the wrong place, which reads as "the partner sees a
        // different enemy" rather than as a framing bug.
        let s = EnemySpawn {
            id: sample_guid(),
            flags: enemy_spawn_flags::MIRROR,
            px: 0x1122_3344,
            py: -2,
            pz: 3,
            yaw: 0x0102,
            hp: 0x0304,
            class_path: "P.C".into(),
        };
        let bytes = encode_body(&Body::EnemySpawn(s.clone())).unwrap();
        // 16 id + 1 flags + 12 pos + 2 yaw + 2 hp + 2 len + 3 path
        assert_eq!(bytes.len(), 38);
        assert_eq!(bytes[0..16], [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15]);
        assert_eq!(bytes[16], enemy_spawn_flags::MIRROR);
        assert_eq!(bytes[17..21], [0x44, 0x33, 0x22, 0x11]);
        assert_eq!(bytes[29..31], [0x02, 0x01]);          // yaw, little endian
        assert_eq!(bytes[31..33], [0x04, 0x03]);          // hp, little endian
        assert_eq!(bytes[33..35], [3, 0]);                // path length
        assert_eq!(&bytes[35..38], b"P.C");
        assert_eq!(decode_body(Kind::EnemySpawn, &bytes).unwrap(), Body::EnemySpawn(s));
    }

    #[test]
    fn enemy_spawn_is_reliable_and_rejects_an_empty_class_path() {
        assert!(Kind::EnemySpawn.is_reliable());
        assert!(!Kind::EnemySpawn.is_latest_wins());
        // An empty path is not a class: refusing it at the decoder keeps a malformed body
        // from reaching an engine call.
        let s = EnemySpawn { class_path: String::new(), ..EnemySpawn::default() };
        let bytes = encode_body(&Body::EnemySpawn(s)).unwrap();
        assert!(decode_body(Kind::EnemySpawn, &bytes).is_err());
    }

    #[test]
    fn object_row_is_eighteen_bytes() {
        let o = Objects {
            rev: 7,
            entries: vec![ObjectRow {
                key: sample_guid(),
                shape: object_shape::CHEST,
                state: 1,
            }],
        };
        let bytes = encode_body(&Body::Objects(o)).unwrap();
        // 4 rev + 2 n + 16 key + 1 shape + 1 state
        assert_eq!(bytes.len(), 24);
        assert_eq!(bytes[6..22], [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15]);
        assert_eq!(bytes[22], object_shape::CHEST);
        assert_eq!(bytes[23], 1);
    }

    #[test]
    fn objects_is_reliable_and_not_latest_wins() {
        // An interaction is a one-shot event: a lost OBJECTS body leaves a chest closed on
        // the wrong machine with nothing to notice it, so the lane is reliable and must not
        // also sit in the superseded lane (WIRE.md §2/§4).
        assert!(Kind::Objects.is_reliable());
        assert!(!Kind::Objects.is_latest_wins());
    }

    #[test]
    fn an_unknown_object_shape_is_refused_rather_than_applied() {
        // A row the receiver cannot interpret must not be applied with a guessed property
        // name, so the decoder refuses it and the caller logs the refusal.
        let mut bytes = encode_body(&Body::Objects(Objects {
            rev: 1,
            entries: vec![ObjectRow { key: sample_guid(), shape: object_shape::CHEST, state: 0 }],
        }))
        .unwrap();
        bytes[22] = 9;
        assert!(decode_body(Kind::Objects, &bytes).is_err());
        bytes[22] = object_shape::CHEST;
        bytes[23] = 2;
        assert!(decode_body(Kind::Objects, &bytes).is_err());
    }

    #[test]
    fn an_object_intent_carries_key_shape_and_state() {
        let payload = IntentPayload::ObjectState {
            key: sample_guid(),
            shape: object_shape::CHEST,
            state: 1,
        };
        assert_eq!(payload.kind(), 9);
        let intent = Intent { based_on_rev: 13, payload };
        let bytes = encode_body(&Body::Intent(intent.clone())).unwrap();
        // 1 kind + 4 rev + 16 key + 1 shape + 1 state
        assert_eq!(bytes.len(), 23);
        assert_eq!(decode_body(Kind::Intent, &bytes).unwrap(), Body::Intent(intent));
    }

    #[test]
    fn enemy_detail_field_order_is_id_anim_seq_status_boss_path() {
        // Pinned byte by byte because the native decoder walks the same order and a
        // transposition (status before seq, or boss before status) would decode as
        // plausible garbage rather than fail.
        let d = EnemyDetail {
            id: Guid::from_bytes([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15]),
            anim: 1,
            seq: 7,
            status: 0x0005,
            boss: 2,
            montage_path: "A".into(),
        };
        let bytes = encode_body(&Body::EnemyDetail(d)).unwrap();
        assert_eq!(&bytes[0..16], &[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15]);
        assert_eq!(bytes[16], 1, "anim");
        assert_eq!(bytes[17], 7, "seq");
        assert_eq!(&bytes[18..20], &[0x05, 0x00], "status is little-endian");
        assert_eq!(bytes[20], 2, "boss phase");
        assert_eq!(&bytes[21..23], &[0x01, 0x00], "path length");
        assert_eq!(bytes[23], b'A');
    }

    /// The death lane (WIRE.md §3.24): the ladder, the three bodies, and what they refuse.
    #[test]
    fn respawn_ladder_and_bodies() {
        use respawn_penalty as pen;
        assert_eq!(pen::MAX_STACKS, 9);
        assert_eq!(pen::pct_for(0), 100);
        assert_eq!(pen::pct_for(1), 90);
        assert_eq!(pen::pct_for(2), 80);
        assert_eq!(pen::pct_for(9), 10);
        assert_eq!(pen::pct_for(10), 10, "a tenth death cannot go below the floor");
        assert_eq!(pen::pct_for(255), 10);

        let down = RespawnDown { deaths: 2, pct: 80, seq: 7, px: -222_772, py: -123_226, pz: -122_712 };
        let bytes = encode_body(&Body::RespawnDown(down.clone())).unwrap();
        // deaths, pct, seq then three little-endian i32.
        assert_eq!(&bytes[0..3], &[2, 80, 7]);
        assert_eq!(bytes.len(), 15);
        assert_eq!(decode_body(Kind::RespawnDown, &bytes).unwrap(), Body::RespawnDown(down.clone()));

        let up = RespawnUp {
            deaths: 2,
            pct: 80,
            seq: 8,
            px: 32_080,
            py: -13_880,
            pz: 408,
            yaw: -17_294,
            hp: 320,
            hp_max: 320,
            name: "Checkpoint17/100_GR".into(),
        };
        let bytes = encode_body(&Body::RespawnUp(up.clone())).unwrap();
        assert_eq!(decode_body(Kind::RespawnUp, &bytes).unwrap(), Body::RespawnUp(up));

        let rest = RespawnRest {
            deaths: 0,
            pct: 100,
            seq: 9,
            px: 32_080,
            py: -13_880,
            pz: 408,
            name: "Checkpoint17/100_GR".into(),
        };
        let bytes = encode_body(&Body::RespawnRest(rest.clone())).unwrap();
        assert_eq!(decode_body(Kind::RespawnRest, &bytes).unwrap(), Body::RespawnRest(rest));

        // The kinds say which body it is, so a body does not repeat the kind byte.
        assert_eq!(Kind::RespawnDown.as_u8(), 0x54);
        assert_eq!(Kind::RespawnUp.as_u8(), 0x55);
        assert_eq!(Kind::RespawnRest.as_u8(), 0x56);
        assert!(Kind::RespawnUp.is_reliable());
        assert_eq!(Kind::from_u8(0x54).unwrap(), Kind::RespawnDown);

        // A stack and a percentage that disagree describe a state neither machine can have.
        let mut bad = down.clone();
        bad.pct = 100;
        assert!(encode_body(&Body::RespawnDown(bad)).is_err());
        // ... and the decoder refuses it too, not just the encoder.
        let mut wire = encode_body(&Body::RespawnDown(down.clone())).unwrap();
        wire[1] = 100;
        assert!(decode_body(Kind::RespawnDown, &wire).is_err());
        // A rest that carries a stack would clear a penalty the sender still has.
        let bad_rest = RespawnRest { deaths: 3, pct: 70, ..Default::default() };
        assert!(encode_body(&Body::RespawnRest(bad_rest)).is_err());
        // hp above hp_max is refused.
        let bad_up = RespawnUp { hp: 400, hp_max: 320, ..Default::default() };
        assert!(encode_body(&Body::RespawnUp(bad_up)).is_err());
    }
}
