# QDCOOP Rust transport

The Rust half of the Queen's Domain co-op transport. Two crates, no external
dependencies, `cargo test` green.

| crate | what it is |
|---|---|
| `qd-core` | The canonical implementation of [`../doc/WIRE.md`](../doc/WIRE.md) version 1: envelope, every body, the quantisers, the reliability primitives and the flag bank. Dependency-free and game-agnostic, so the whole protocol is testable with no game running. |
| `qd-relay` | The runnable relay: one UDP socket, one session, two peers. Holds session facts (who is host, what the session id is) and forwards datagrams. Simulates nothing. |

## Build and test

```powershell
cd mod\rust
cargo build --release
cargo test
```

`cargo test` also rewrites [`../doc/vectors.txt`](../doc/vectors.txt), which the
native self-test consumes. A drift between `qd-core` and `native/src/qdwire.h`
therefore fails a test instead of corrupting a session.

## Run the relay

```powershell
# loopback default
cargo run -p qd-relay

# a chosen interface and port
cargo run -p qd-relay -- --bind 0.0.0.0:47800

# LAN / direct-IP testing: pin both endpoints, no discovery needed
cargo run -p qd-relay -- --bind 0.0.0.0:47800 --two 192.168.1.10:47801 --two 192.168.1.11:47801
```

The first peer to send `HELLO` becomes host. One line is printed per session
transition (session opened, peer admitted, refused, timed out, session closed),
plus one line per malformed datagram. Nothing else.

A silent peer holds its slot for `PEER_TIMEOUT` (30 s) and then loses it. A reap
is logged but is not treated as evidence that the player left; the session is
only closed when no peer remains. This is DollsMP's rule, kept on purpose: tearing
down a puppet because a timeout fired is a glitch manufactured by bookkeeping.

## Where each module came from

| `qd-core` module | DollsMP source | Treatment |
|---|---|---|
| `bitstream.rs` | `dollsmp-core/src/bitstream.rs` | Ported; adds a byte-aligned `bool` payload (§B.5 item 1) and typed alignment errors that name the bit position. |
| `protocol.rs` | `dollsmp-core/src/protocol.rs` | Rewritten against WIRE.md. `DMP1`/18-byte header/`PacketKind` replaced by `QDC1`/16-byte envelope and the QDCOOP kind table; `encode` re-validates the body cap (§B.5 item 5). |
| `quantize.rs` | `dollsmp-core/src/quantize.rs` | Rewritten: WIRE.md fixes the *units* (1/8 cm, 1/65536 turn, saturated 1/8 cm/s) instead of a per-world bounded quantiser, which removes the "the bounds were wrong for this map" failure mode. The `max_error` reasoning (half a step, and asserting the bound) is kept. |
| `reliability.rs` | `dollsmp-core/src/reliability.rs`, `dollsmp-core/src/desync.rs`, `dollsmp-net/src/lib.rs` | `SequenceBuffer` and `RttEstimator` ported; the retransmit/ack tracker is rebuilt as one container (§B.5 items 2 and 3) and the desync detector counts one strike per tick (§B.5 item 4). The 32-bit ack bitset is dropped: the wire has one `u32` and no bitmap. |
| `rng.rs` | `dollsmp-core/src/rng.rs` | As-is. Same published golden vector (seed 42 / stream 54). |
| `flags.rs` | `native/src/qdflags.h` (mirror), DollsMP had no equivalent as pure data | The monotonic host-minted flag bank described by WIRE.md §3.6/§3.10 and `sync-docs-audit.md` §1.2 (rules E1b/E2/E4), checked transition by transition against the native header. |
| `error.rs` | `dollsmp-core/src/error.rs` | Rewritten for the QDCOOP failure set; still `Send + Sync`, still renders a sentence per variant. |
| `tests/wire_vectors.rs` | new | One golden instance of every body plus every `INTENT` variant, and the writer for `doc/vectors.txt`. |
| `qd-relay` | `dollsmp-server/src/relay.rs`, `dollsmp-server/src/lib.rs` | The socket loop, the "first to join is host" rule and the reaping discipline are kept; the client-message layer (`DMPC`, `ClientMsg`, join barrier, impairment) is not ported because WIRE.md already defines the wire and this relay is a router, not a shard of the game. |

## The five defects that had to be fixed first

`analysis/03-dollsmp-reuse.md` §B.5 lists eleven; the five that the task required
are fixed here, each with a test that fails against the DollsMP shape:

| # | Defect | Fix | Test |
|---|---|---|---|
| 1 | `RpcPayload for bool` cannot be encoded through a byte-framed container | `bool` occupies one byte; `write_rpc`/`read_rpc` are the container | `rpc_bool_survives_a_byte_framed_container`, `bool_payload_is_exactly_one_byte` |
| 2 | `Connection::tick` evicted from the tracker but not from `retransmit` | one `pending` vector holds payload *and* timers, so an eviction cannot be half-applied | `aged_packet_is_abandoned_and_cannot_leak`, `ack_removes_the_payload_not_just_the_timer` |
| 3 | `make_header` conflated "peer acked us" with "which inbound sequence to ack" | `ack_seq(&inbound)` is a pure function of the receive buffer; `peer_has_acked_us()` is a separate outbound fact | `ack_seq_follows_the_inbound_buffer_only` |
| 4 | `DesyncDetector::compare_tick` counted a strike per call, not per tick | one strike per tick value; a repeated or stale tick is a no-op | `desync_counts_one_strike_per_tick`, `desync_ignores_a_stale_tick` |
| 5 | `Packet` fields public and `encode` cast `payload.len() as u16` | `encode` re-validates `MAX_BODY` before converting, and rejects rather than truncating | `encode_refuses_a_body_over_the_cap`, `a_body_of_exactly_the_cap_is_accepted` |

## `doc/vectors.txt`

```
# <name> | <hex bytes>
player_row | 10 01 dc 05 ...
world_flags | 11 05 00 ...
```

The first byte of each line is the envelope `kind`; the 16-byte envelope header is
**not** included (its layout is pinned separately by `envelope_header_layout`).
Line order is fixed and every list body is built through a `BTreeMap`, so the file
is byte-stable across machines and runs.

The expected bytes were derived from WIRE.md by a separate implementation of the
field layout (a throwaway script reading the document), not by the encoder under
test; `body_encodings_match_the_specification` is therefore a real cross-check
rather than a self-consistency check.

## Known cross-implementation disagreement: `native/src/qdwire.h`

`qd-core` implements WIRE.md literally. `native/src/qdwire.h` currently does not, so
the two encoders disagree on four bodies and the vector cross-check cannot pass until
one side moves. This is recorded here rather than hidden because it is exactly the
drift the vector file exists to catch.

| body | WIRE.md §3 | `qdwire.h` today |
|---|---|---|
| `WORLD_FLAGS` (0x11) | `u32 rev, u16 n`, then `n × {str, i32}` | `u32 rev, u16 total, u16 produced`, then entries (`WriteFlagsHeader`) |
| `ACTOR_STATES` (0x12) | `u32 rev, u16 n`, then `n × {guid, i32}` | `u32 rev, u16 total, u16 produced`, then entries |
| `ENEMY` (0x14) | `u16 n`, then `n` rows | `u16 total, u16 produced`, then rows (`WriteEnemyHeader`) |
| `LEVEL_UNION` (0x22) | `u16 bits_len, bytes bits, u8 has_names, [u16 n, n × str]` | `u8 has_names, u16 bits_len`, then bits (`WriteLevelUnionHeader`) |

Every other body — envelope, `HELLO`, `WELCOME`, `KEEPALIVE`, `ACK`, `PLAYER`,
`INTENT` (all six kinds), `DIALOGUE` — agrees byte for byte, as do the quantiser
constants, the position clamp (2 147 483 000) and the double-precision arithmetic in
`QuantizePos`/`QuantizeAngle`.

The native side's `total`/`produced` pair is trying to solve a real gap: WIRE.md §1
says an oversized container is "split across ticks", and `total`/`produced` is how a
receiver knows it is looking at a continuation. But WIRE.md's §3.6/§3.7/§3.9 bodies
define only `n`, and nothing in `doc/` defines the chunk cursor. So this is a
document bug, not simply an implementation bug: whichever way it is resolved, WIRE.md
and both encoders have to change together. `qd-core` follows the document as written.

## Deliberate deviations and open points

Recorded rather than hidden, in the spirit of WIRE.md §6.

1. **`LOG` (0x30) has no body in WIRE.md.** §2 lists the kind; §3 defines no
   layout. `qd-core` carries it as an opaque byte body (`Log`) and interprets
   nothing, so whatever the native side writes crosses unchanged.
2. **The relay does not author `WELCOME`.** WIRE.md §3.2 makes `WELCOME` host →
   peer, so a relay-authored Welcome would be a second writer of a session fact.
   The relay assigns and logs the role; the host learns its own role out of band
   (the launcher knows whether it was started as host) or from the first-place
   rule.
3. **`ACK` is cumulative.** WIRE.md §3.4 puts a single `u32 seq` in the body and no
   bitmap, so "acknowledged" means "everything up to and including this sequence".
   DollsMP's 32-entry ack-bitset has no field on this wire. `seq` starts at 1
   (WIRE.md §1), so 0 is used as the "nothing received yet" sentinel and is never
   handed out by the tracker.
4. **`LEVEL_UNION.bits` is LSB-first within each byte.** WIRE.md does not state the
   bit order inside the byte. LSB-first is chosen so the whole crate has exactly
   one bit order, the one in `bitstream.rs`. The native side must match.
5. **`HELLO.display_name` is validated as UTF-8.** WIRE.md §3.1 writes it as
   `u16 len` + `bytes` rather than `str`. The encoding is identical for any UTF-8
   name, and rejecting a non-UTF-8 display name is safer than carrying bytes that
   cannot be displayed.
6. **WIRE.md §3.5 says the `i32` position gives "268 m of range".** It does not:
   `i32` at 1/8 cm is 268 435 455.875 cm ≈ 2684 km. The format as written is
   implemented; the range note is wrong by a factor of 10⁴. No behaviour depends on
   the note.
7. **`flags.rs` is a mirror of `native/src/qdflags.h`.** That header now exists (it
   appeared in the tree while this crate was being written; the first directory
   listing did not contain it), and every transition in `flags.rs` was checked against
   it: `SetLocal` / `ApplyRemote` / `ApplyIntent`, a revision that starts at **1** and
   never becomes 0, "return before adopting `rev`" on a refused value, no dirty flag
   raised by a remote value, a 512-key cap, and name order by unsigned byte
   comparison. One edge case differs: a name longer than 63 bytes is truncated at a
   UTF-8 boundary here rather than at a raw byte offset, and lookups normalise the same
   way. For the ASCII save keys this wire carries the two are identical; for a
   multi-byte name the native would store half a code point and never match it again.
8. **The relay enforces at most one host.** A second `HELLO` with
   `role_wanted = host` is refused rather than admitted as a peer: two authorities
   in one key space is the failure rule E1b is about. `role_wanted = any` keeps
   DollsMP's rule (first peer in becomes host, a later one never does).
