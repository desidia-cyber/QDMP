//! # qd-core
//!
//! The canonical implementation of `mod/doc/WIRE.md` version 1: the QDCOOP
//! transport format for the Queen's Domain two-player co-op mod.
//!
//! This crate has **no dependencies** and knows nothing about Unreal or about
//! sockets. That is deliberate, and it is why the DollsMP core ported at all: the
//! protocol can be tested exhaustively on any machine, with no game running, and
//! the native C++ side can reproduce every byte from the same document plus
//! `doc/vectors.txt`.
//!
//! ## What is where
//!
//! ```text
//!   protocol     envelope + every body (WIRE.md §1-§3), byte-exact
//!   bitstream    LSB-first bit reader/writer; byte-aligned helpers
//!   quantize     position 1/8 cm, angle 1/65536 turn, velocity 1/8 cm/s
//!   reliability  reliable-lane retransmit/ack tracker; inbound dedup; desync
//!   flags        the monotonic host-minted flag bank (mirror of qdflags.h)
//!   rng          PCG-XSH-RR with a published golden vector
//!   error        every failure a decoder can produce, none of them a panic
//! ```
//!
//! ## The invariants this crate exists to hold
//!
//! * **State, not events.** Every `WORLD_FLAGS`/`ACTOR_STATES`/`INVENTORY` body
//!   is the complete current set. A peer with no memory converges within a
//!   second (WIRE.md §2, rule E8).
//! * **No `x += d` on the wire.** Every effect is a set insertion, an absolute
//!   assignment, or a transition guarded by a monotonic counter (WIRE.md §4,
//!   rule E2).
//! * **A lost or duplicated datagram changes nothing** — which is only true if the
//!   decoder is total. It is: [`protocol::Envelope::decode`] returns an error for
//!   every malformed input and never panics.

#![forbid(unsafe_code)]
#![warn(missing_debug_implementations)]

pub mod bitstream;
pub mod error;
pub mod flags;
pub mod protocol;
pub mod quantize;
pub mod reliability;
pub mod rng;

pub use bitstream::{read_rpc, write_rpc, BitReader, BitWriter, RpcPayload};
pub use error::{Error, Result};
pub use flags::{FlagBank, FlagMode, MAX_FLAGS, MAX_NAME_BYTES};
pub use protocol::{
    decode_body, encode_body, Ack, ActorEntry, ActorStates, Body, DialogueState, EnemyRow,
    EnemyRows, Envelope, FlagEntry, Guid, Header, Hello, Intent, IntentPayload, Inventory,
    ItemEntry, Kind, LevelUnion, Log, PlayerRow, Welcome, WorldFlags, HEADER_LEN, MAGIC,
    MAX_BODY, MAX_ENEMY_ROWS, VERSION,
};
pub use quantize::{
    angle_from_wire, angle_to_wire, position_from_wire, position_to_wire, velocity_from_wire,
    velocity_to_wire, wrap_degrees, ANGLE_MAX_ERROR_DEG, POSITION_MAX_ERROR_CM,
    VELOCITY_MAX_ERROR_CM_S,
};
pub use reliability::{
    ack_seq, seq_diff, seq_greater, AckOutcome, DesyncConfig, DesyncDetector, PendingSend,
    ReliableTracker, RttEstimator, SequenceBuffer, TickOutcome, TrackerConfig, ACK_WINDOW,
};
pub use rng::{Pcg32, SeedMixer};

/// Name used in logs and in the handshake.
pub const MOD_NAME: &str = "QDCOOP";

/// Wire version this crate implements (WIRE.md §1: `ver = 1`).
pub const WIRE_VERSION: u8 = protocol::VERSION;

/// Version of the crate itself, not of the wire format.
pub const MOD_VERSION: &str = env!("CARGO_PKG_VERSION");
