//! Reliability primitives over an unreliable datagram transport.
//!
//! WIRE.md §4 splits the lanes in two, and only one of them needs anything here:
//!
//! * `PLAYER`, `WORLD_FLAGS`, `ACTOR_STATES`, `INVENTORY`, `ENEMY`,
//!   `DIALOGUE`, `LEVEL_UNION` are **unreliable and absolute**. A lost datagram is
//!   repaired by the next one, so there is deliberately no per-packet bookkeeping
//!   for them — "latest wins" is a property of the body (`rev`, `seq`), not of the
//!   transport.
//! * `HELLO`, `WELCOME` and `INTENT` are **reliable**. Those go through
//!   [`ReliableTracker`].
//!
//! Three of the defects in `analysis/03-dollsmp-reuse.md` §B.5 are fixed here.
//!
//! **Item 2 — retransmit entries leaked.** DollsMP kept the payloads in
//! `Connection.retransmit` and the timers in `AckTracker.pending`, and `tick`
//! evicted aged entries from only the second, so a packet was neither resent nor
//! abandoned and `pending_reliable()` never reached zero. Here there is exactly
//! **one** container ([`ReliableTracker::pending`]) holding both the bytes and the
//! timers, so an eviction cannot be half-applied. The regression test is
//! `aged_packet_is_abandoned_and_cannot_leak`.
//!
//! **Item 3 — two meanings of "ack".** `make_header` used "the peer has acked us"
//! (`have_ack()`) to decide "which inbound sequence do I ack", which conflated our
//! outbound evidence with our inbound receive state. [`ack_seq`] is now a pure
//! function of the inbound [`SequenceBuffer`], and
//! [`ReliableTracker::peer_has_acked_us`] is a separate, outbound-only fact.
//!
//! **Item 4 — a strike per call, not per tick.** [`DesyncDetector::compare_tick`]
//! now counts at most one strike per tick value, so comparing the same tick twice
//! (once from a periodic digest pass, once from a handler) cannot manufacture a
//! desync.

use crate::error::{Error, Result};

/// Size of the inbound dedup window, in sequence numbers.
pub const ACK_WINDOW: usize = 64;

/// True when `a` is greater than `b` in wrapping 32-bit sequence space.
pub fn seq_greater(a: u32, b: u32) -> bool {
    a.wrapping_sub(b) < 0x8000_0000 && a != b
}

/// Signed distance from `b` to `a` in wrapping sequence space.
pub fn seq_diff(a: u32, b: u32) -> i64 {
    let d = a.wrapping_sub(b);
    if d < 0x8000_0000 {
        i64::from(d)
    } else {
        i64::from(d) - 0x1_0000_0000
    }
}

/// Tracks which inbound sequences we have seen, for dedup and the cumulative ack.
#[derive(Debug, Clone)]
pub struct SequenceBuffer {
    base: u32,
    /// Bit `i` set means `base + i` has been received.
    received: u64,
    have_base: bool,
}

impl Default for SequenceBuffer {
    fn default() -> Self {
        Self::new()
    }
}

impl SequenceBuffer {
    pub fn new() -> Self {
        Self { base: 0, received: 0, have_base: false }
    }

    pub fn base(&self) -> u32 {
        self.base
    }

    pub fn has_base(&self) -> bool {
        self.have_base
    }

    /// Insert a sequence number.
    ///
    /// `Ok(true)` — new, deliver it. `Ok(false)` — duplicate, drop it.
    /// `Err(SequenceOutOfWindow)` — so old it cannot be deduplicated honestly.
    pub fn insert(&mut self, seq: u32) -> Result<bool> {
        if !self.have_base {
            self.have_base = true;
            self.base = seq;
            self.received = 1;
            return Ok(true);
        }
        let d = seq_diff(seq, self.base);
        if d < 0 {
            if d < -((ACK_WINDOW - 1) as i64) {
                return Err(Error::SequenceOutOfWindow {
                    seq,
                    base: self.base,
                    window: ACK_WINDOW,
                });
            }
            let bit = (d + ACK_WINDOW as i64) as u32;
            let mask = 1u64 << bit;
            if self.received & mask != 0 {
                return Ok(false);
            }
            self.received |= mask;
            return Ok(true);
        }
        if d >= ACK_WINDOW as i64 {
            // Far ahead: slide the window, treating the skipped range as lost. A
            // sequence that sat at old bit `b` moves to `b - (d - (window-1))`,
            // which is a right shift that discards exactly what fell out.
            let shift = (d - (ACK_WINDOW as i64 - 1)) as u32;
            if shift >= 64 {
                self.received = 0;
            } else {
                self.received >>= shift;
            }
            self.base = seq.wrapping_sub(ACK_WINDOW as u32 - 1);
            self.received |= 1u64 << (ACK_WINDOW - 1);
            return Ok(true);
        }
        let mask = 1u64 << (d as u32);
        if self.received & mask != 0 {
            return Ok(false);
        }
        self.received |= mask;
        Ok(true)
    }

    pub fn has(&self, seq: u32) -> bool {
        if !self.have_base {
            return false;
        }
        let d = seq_diff(seq, self.base);
        if d < 0 {
            if d < -((ACK_WINDOW - 1) as i64) {
                return false;
            }
            return self.received & (1u64 << ((d + ACK_WINDOW as i64) as u32)) != 0;
        }
        if d >= ACK_WINDOW as i64 {
            return false;
        }
        self.received & (1u64 << (d as u32)) != 0
    }

    /// Highest sequence such that everything from the base up to it arrived.
    pub fn contiguous_end(&self) -> Option<u32> {
        if !self.have_base {
            return None;
        }
        let mut n = 0u32;
        while n < ACK_WINDOW as u32 && self.received & (1u64 << n) != 0 {
            n += 1;
        }
        Some(self.base.wrapping_add(n.saturating_sub(1)))
    }
}

/// The sequence to put in an `ACK` body.
///
/// This is the fix for §B.5 item 3: the value is derived **only** from what we
/// have received. WIRE.md §1 says `seq` starts at 1, so 0 is a free sentinel for
/// "nothing received yet" and is never a real acknowledgement.
pub fn ack_seq(inbound: &SequenceBuffer) -> u32 {
    inbound.contiguous_end().unwrap_or(0)
}

/// Smoothed round-trip time and jitter (RFC 6298 shape, fixed-point integer math
/// so it is deterministic across platforms).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RttEstimator {
    /// Smoothed RTT in milliseconds, scaled by 8.
    srtt_x8: u32,
    /// RTT variance in milliseconds, scaled by 8.
    rttvar_x8: u32,
    have_sample: bool,
    min_rtt_ms: u32,
    max_rtt_ms: u32,
}

impl Default for RttEstimator {
    fn default() -> Self {
        Self::new()
    }
}

impl RttEstimator {
    pub fn new() -> Self {
        Self {
            srtt_x8: 0,
            rttvar_x8: 0,
            have_sample: false,
            min_rtt_ms: u32::MAX,
            max_rtt_ms: 0,
        }
    }

    pub fn add_sample(&mut self, rtt_ms: u32) {
        self.min_rtt_ms = self.min_rtt_ms.min(rtt_ms);
        self.max_rtt_ms = self.max_rtt_ms.max(rtt_ms);
        if !self.have_sample {
            self.srtt_x8 = rtt_ms.saturating_mul(8);
            self.rttvar_x8 = rtt_ms.saturating_mul(4);
            self.have_sample = true;
            return;
        }
        let srtt = self.srtt_x8 / 8;
        let diff = srtt.abs_diff(rtt_ms);
        self.rttvar_x8 = (self.rttvar_x8 * 3 / 4) + (diff.saturating_mul(8) / 4);
        self.srtt_x8 = (self.srtt_x8 * 7 / 8) + rtt_ms.saturating_mul(8) / 8;
    }

    pub fn srtt_ms(&self) -> u32 {
        if !self.have_sample {
            return 0;
        }
        self.srtt_x8 / 8
    }

    pub fn jitter_ms(&self) -> u32 {
        if !self.have_sample {
            return 0;
        }
        self.rttvar_x8 / 8
    }

    pub fn min_rtt_ms(&self) -> u32 {
        if self.min_rtt_ms == u32::MAX {
            0
        } else {
            self.min_rtt_ms
        }
    }

    pub fn max_rtt_ms(&self) -> u32 {
        self.max_rtt_ms
    }

    /// `srtt + 4*rttvar`, floored at 100 ms and capped at 2 s so one bad sample
    /// cannot stall a session.
    pub fn rto_ms(&self) -> u64 {
        if !self.have_sample {
            return 200;
        }
        let rto = u64::from(self.srtt_ms()) + 4 * u64::from(self.jitter_ms());
        rto.clamp(100, 2000)
    }
}

/// Tunables for [`ReliableTracker`].
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct TrackerConfig {
    /// Retransmit attempts before a packet is abandoned.
    pub max_attempts: u16,
    /// Age at which an unacknowledged packet is abandoned regardless of attempts.
    pub max_age_ms: u64,
}

impl Default for TrackerConfig {
    fn default() -> Self {
        // 12 attempts and 5 s is DollsMP's shipped pair; both are generous for a
        // two-player LAN session and both matter only when the peer is gone, in
        // which case the liveness timeout fires first.
        Self { max_attempts: 12, max_age_ms: 5_000 }
    }
}

/// One reliable packet that has been sent and not yet acknowledged.
///
/// The payload lives here, not in a parallel vector, which is the structural fix
/// for §B.5 item 2.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct PendingSend {
    pub seq: u32,
    pub payload: Vec<u8>,
    pub first_sent_ms: u64,
    pub last_sent_ms: u64,
    pub attempts: u16,
}

/// Result of feeding an `ACK` body to the tracker.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct AckOutcome {
    /// Sequences that stopped needing retransmission because of this ack.
    pub newly_acked: Vec<u32>,
    /// True when this ack was newer than any ack seen before.
    pub advanced: bool,
}

/// Result of one [`ReliableTracker::tick`].
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct TickOutcome {
    /// Packets to re-send now, in send order.
    pub retransmit: Vec<(u32, Vec<u8>)>,
    /// Sequences given up on. They are already gone from `pending`.
    pub abandoned: Vec<u32>,
}

/// Outbound half of a reliable lane: sequence allocation, retransmission and
/// abandonment.
///
/// The acknowledgement is **cumulative** because that is all the wire provides:
/// WIRE.md §3.4 puts a single `u32 seq` in the `ACK` body and no bitmap, so
/// DollsMP's 32-entry ack-bitset has no field here and is deliberately not ported.
#[derive(Debug, Clone)]
pub struct ReliableTracker {
    next_seq: u32,
    pending: Vec<PendingSend>,
    cfg: TrackerConfig,
    rtt: RttEstimator,
    highest_acked: Option<u32>,
    peer_has_acked_us: bool,
    sent: u64,
    retransmitted: u64,
    abandoned: u64,
}

impl Default for ReliableTracker {
    fn default() -> Self {
        Self::new(TrackerConfig::default())
    }
}

impl ReliableTracker {
    pub fn new(cfg: TrackerConfig) -> Self {
        Self {
            // WIRE.md §1: "seq (per-sender, monotonic, starts at 1)".
            next_seq: 1,
            pending: Vec::new(),
            cfg,
            rtt: RttEstimator::new(),
            highest_acked: None,
            peer_has_acked_us: false,
            sent: 0,
            retransmitted: 0,
            abandoned: 0,
        }
    }

    pub fn config(&self) -> TrackerConfig {
        self.cfg
    }

    pub fn next_seq(&self) -> u32 {
        self.next_seq
    }

    pub fn pending(&self) -> &[PendingSend] {
        &self.pending
    }

    pub fn pending_len(&self) -> usize {
        self.pending.len()
    }

    pub fn rto_ms(&self) -> u64 {
        self.rtt.rto_ms()
    }

    pub fn rtt(&self) -> &RttEstimator {
        &self.rtt
    }

    /// Whether the peer has ever acknowledged anything we sent.
    ///
    /// This is outbound evidence only. It must never be used to decide what to put
    /// in our own ack field — that is [`ack_seq`]'s job (§B.5 item 3).
    pub fn peer_has_acked_us(&self) -> bool {
        self.peer_has_acked_us
    }

    pub fn highest_acked(&self) -> Option<u32> {
        self.highest_acked
    }

    pub fn sent_count(&self) -> u64 {
        self.sent
    }

    pub fn retransmitted_count(&self) -> u64 {
        self.retransmitted
    }

    pub fn abandoned_count(&self) -> u64 {
        self.abandoned
    }

    /// Allocate the next outgoing sequence number.
    pub fn allocate(&mut self) -> u32 {
        let s = self.next_seq;
        self.next_seq = self.next_seq.wrapping_add(1);
        if self.next_seq == 0 {
            // seq 0 is reserved as "nothing acknowledged", so skip it on wrap.
            self.next_seq = 1;
        }
        s
    }

    /// Record a reliable packet as sent and return its sequence number.
    pub fn send(&mut self, payload: Vec<u8>, now_ms: u64) -> u32 {
        let seq = self.allocate();
        self.pending.push(PendingSend {
            seq,
            payload,
            first_sent_ms: now_ms,
            last_sent_ms: now_ms,
            attempts: 1,
        });
        self.sent += 1;
        seq
    }

    /// Apply an `ACK` body. `acked` means "I have everything up to and including
    /// this sequence".
    pub fn on_ack(&mut self, acked: u32, now_ms: u64) -> AckOutcome {
        let mut outcome = AckOutcome::default();
        if acked == 0 {
            // The sentinel from `ack_seq` when the peer has received nothing.
            return outcome;
        }
        outcome.advanced = match self.highest_acked {
            None => true,
            Some(h) => seq_greater(acked, h),
        };
        if outcome.advanced {
            self.highest_acked = Some(acked);
            self.peer_has_acked_us = true;
        }

        let mut kept = Vec::with_capacity(self.pending.len());
        for p in std::mem::take(&mut self.pending) {
            if !seq_greater(p.seq, acked) {
                if p.attempts == 1 {
                    // An un-retransmitted packet gives an unbiased RTT sample;
                    // a retransmitted one cannot (Karn's rule).
                    let sample = now_ms.saturating_sub(p.first_sent_ms);
                    self.rtt.add_sample(sample.min(u64::from(u32::MAX)) as u32);
                }
                outcome.newly_acked.push(p.seq);
            } else {
                kept.push(p);
            }
        }
        self.pending = kept;
        outcome
    }

    /// Drive timers: resend what is due, abandon what is hopeless.
    ///
    /// Every removal happens on the single `pending` vector, so a packet can never
    /// be simultaneously "evicted from the timers" and "still queued for
    /// retransmission" — the DollsMP failure in §B.5 item 2.
    pub fn tick(&mut self, now_ms: u64) -> TickOutcome {
        let rto = self.rtt.rto_ms();
        let mut outcome = TickOutcome::default();
        let mut kept = Vec::with_capacity(self.pending.len());

        for mut p in std::mem::take(&mut self.pending) {
            let age = now_ms.saturating_sub(p.first_sent_ms);
            if age >= self.cfg.max_age_ms || p.attempts >= self.cfg.max_attempts {
                outcome.abandoned.push(p.seq);
                self.abandoned += 1;
                continue;
            }
            if now_ms.saturating_sub(p.last_sent_ms) >= rto {
                outcome.retransmit.push((p.seq, p.payload.clone()));
                p.last_sent_ms = now_ms;
                p.attempts = p.attempts.saturating_add(1);
                self.retransmitted += 1;
            }
            kept.push(p);
        }
        self.pending = kept;
        outcome
    }

    /// Drop every pending packet (a resync or a disconnect).
    pub fn clear(&mut self) {
        self.pending.clear();
    }
}

/// Configuration for the divergence detector.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct DesyncConfig {
    /// Consecutive mismatching ticks before a desync is declared. More than one,
    /// because a single mismatched tick is usually a packet still in flight.
    pub strike_threshold: u32,
}

impl Default for DesyncConfig {
    fn default() -> Self {
        Self { strike_threshold: 3 }
    }
}

/// Detects sustained divergence between the two peers' digest of the same tick.
#[derive(Debug, Clone)]
pub struct DesyncDetector {
    cfg: DesyncConfig,
    strikes: u32,
    detected: bool,
    last_tick: Option<u32>,
}

impl Default for DesyncDetector {
    fn default() -> Self {
        Self::new(DesyncConfig::default())
    }
}

impl DesyncDetector {
    pub fn new(cfg: DesyncConfig) -> Self {
        Self { cfg, strikes: 0, detected: false, last_tick: None }
    }

    /// Compare one tick's digests; returns whether they agree.
    ///
    /// At most one strike is counted per tick value (§B.5 item 4): a repeated call
    /// for the same tick returns the same verdict without touching the strike
    /// count, and a stale tick (older than the last one compared) is ignored
    /// entirely, because it describes a moment already accounted for.
    pub fn compare_tick(&mut self, tick: u32, local_hash: u64, remote_hash: u64) -> bool {
        let agree = local_hash == remote_hash;
        if let Some(prev) = self.last_tick {
            if prev == tick || !seq_greater(tick, prev) {
                return agree;
            }
        }
        self.last_tick = Some(tick);
        if agree {
            // Agreement clears accumulated strikes: a mismatch caused by an
            // in-flight packet must not eventually trip a desync.
            self.strikes = 0;
        } else {
            self.strikes = self.strikes.saturating_add(1);
            if self.strikes >= self.cfg.strike_threshold {
                self.detected = true;
            }
        }
        agree
    }

    pub fn strikes(&self) -> u32 {
        self.strikes
    }

    pub fn is_desynced(&self) -> bool {
        self.detected
    }

    pub fn last_tick(&self) -> Option<u32> {
        self.last_tick
    }

    /// Clear after a successful resync. The tick watermark is kept: re-comparing
    /// an already-compared tick must stay a no-op.
    pub fn clear(&mut self) {
        self.strikes = 0;
        self.detected = false;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn seq_ordering_wraps_correctly() {
        assert!(seq_greater(1, 0));
        assert!(!seq_greater(0, 1));
        assert!(!seq_greater(5, 5));
        assert!(seq_greater(0, u32::MAX));
        assert!(!seq_greater(u32::MAX, 0));
        assert_eq!(seq_diff(0, u32::MAX), 1);
        assert_eq!(seq_diff(u32::MAX, 0), -1);
    }

    #[test]
    fn seq_diff_is_signed_and_bounded() {
        assert_eq!(seq_diff(10, 5), 5);
        assert_eq!(seq_diff(5, 10), -5);
        assert_eq!(seq_diff(0, 0), 0);
        for d in [-2_000_000_000i64, -1, 0, 1, 2_000_000_000] {
            let a = (d as i32) as u32;
            assert_eq!(seq_diff(a, 0), i64::from(d as i32));
        }
    }

    #[test]
    fn allocate_starts_at_one_and_skips_zero_on_wrap() {
        // WIRE.md §1: seq starts at 1, and 0 is the "nothing acknowledged"
        // sentinel, so it must not be handed out.
        let mut t = ReliableTracker::default();
        assert_eq!(t.allocate(), 1);
        assert_eq!(t.allocate(), 2);
        t.next_seq = u32::MAX;
        assert_eq!(t.allocate(), u32::MAX);
        assert_eq!(t.allocate(), 1);
    }

    #[test]
    fn ack_is_cumulative_and_clears_the_payload() {
        let mut t = ReliableTracker::default();
        for s in ["a", "b", "c"] {
            t.send(s.as_bytes().to_vec(), 0);
        }
        let out = t.on_ack(2, 10);
        assert_eq!(out.newly_acked, vec![1, 2]);
        assert!(out.advanced);
        assert_eq!(t.pending_len(), 1);
        assert_eq!(t.pending()[0].seq, 3);
    }

    #[test]
    fn duplicate_ack_does_not_duplicate_newly_acked() {
        let mut t = ReliableTracker::default();
        t.send(b"x".to_vec(), 0);
        assert_eq!(t.on_ack(1, 5).newly_acked, vec![1]);
        let second = t.on_ack(1, 6);
        assert!(second.newly_acked.is_empty());
        assert!(!second.advanced);
    }

    #[test]
    fn stale_ack_does_not_move_us_backwards() {
        let mut t = ReliableTracker::default();
        t.send(b"x".to_vec(), 0);
        t.send(b"y".to_vec(), 0);
        assert!(t.on_ack(2, 5).advanced);
        assert!(!t.on_ack(1, 6).advanced);
        assert_eq!(t.highest_acked(), Some(2));
    }

    #[test]
    fn ack_zero_is_the_no_ack_sentinel() {
        let mut t = ReliableTracker::default();
        t.send(b"x".to_vec(), 0);
        let out = t.on_ack(0, 5);
        assert!(out.newly_acked.is_empty());
        assert!(!out.advanced);
        assert!(!t.peer_has_acked_us(), "0 must not count as the peer acking us");
        assert_eq!(t.pending_len(), 1);
    }

    #[test]
    fn timeout_resends_the_same_sequence() {
        let mut t = ReliableTracker::new(TrackerConfig { max_attempts: 4, max_age_ms: 60_000 });
        let seq = t.send(b"hello".to_vec(), 0);
        // Before the RTO (200 ms with no sample) nothing is due.
        assert!(t.tick(100).retransmit.is_empty());
        let out = t.tick(250);
        assert_eq!(out.retransmit.len(), 1);
        assert_eq!(out.retransmit[0].0, seq);
        assert_eq!(out.retransmit[0].1, b"hello");
        assert_eq!(t.pending()[0].attempts, 2);
    }

    #[test]
    fn attempts_are_capped_and_the_packet_is_abandoned() {
        let mut t = ReliableTracker::new(TrackerConfig { max_attempts: 3, max_age_ms: 60_000 });
        t.send(b"hello".to_vec(), 0);
        let mut now = 0;
        let mut abandoned = Vec::new();
        for _ in 0..10 {
            now += 300;
            abandoned.extend(t.tick(now).abandoned);
        }
        assert_eq!(abandoned.len(), 1);
        assert_eq!(t.pending_len(), 0, "an abandoned packet must not stay pending");
        assert_eq!(t.abandoned_count(), 1);
    }

    /// §B.5 item 2, as a test. In DollsMP the aged-out entry was removed from the
    /// timer table but stayed in the retransmit table, so it was neither resent nor
    /// abandoned and `pending_reliable()` never dropped. Here the payload and the
    /// timer are the same object, so this cannot happen.
    #[test]
    fn aged_packet_is_abandoned_and_cannot_leak() {
        let mut t = ReliableTracker::new(TrackerConfig { max_attempts: 100, max_age_ms: 500 });
        let seq = t.send(b"voice".to_vec(), 0);
        let out = t.tick(500);
        assert!(out.retransmit.is_empty(), "an aged packet must not be resent");
        assert_eq!(out.abandoned, vec![seq]);
        assert_eq!(t.pending_len(), 0, "the payload must be gone, not merely untracked");
        // And nothing can resurface later.
        assert!(t.tick(1_000).retransmit.is_empty());
        assert_eq!(t.pending_len(), 0);
    }

    #[test]
    fn ack_removes_the_payload_not_just_the_timer() {
        let mut t = ReliableTracker::new(TrackerConfig { max_attempts: 100, max_age_ms: 60_000 });
        t.send(b"intent".to_vec(), 0);
        assert_eq!(t.pending_len(), 1);
        t.on_ack(1, 20);
        assert_eq!(t.pending_len(), 0);
        assert!(t.tick(10_000).retransmit.is_empty());
    }

    #[test]
    fn rtt_sample_is_taken_only_on_a_first_attempt() {
        let mut t = ReliableTracker::new(TrackerConfig { max_attempts: 100, max_age_ms: 60_000 });
        t.send(b"a".to_vec(), 100);
        t.on_ack(1, 140);
        assert_eq!(t.rtt().srtt_ms(), 40);

        // Retransmitted: Karn's rule, so no sample.
        let seq = {
            let mut t2 = ReliableTracker::new(TrackerConfig { max_attempts: 100, max_age_ms: 60_000 });
            let s = t2.send(b"b".to_vec(), 0);
            t2.tick(300);
            let _ = t2.on_ack(s, 400);
            t2.rtt().srtt_ms()
        };
        assert_eq!(seq, 0, "a retransmitted packet must not produce an RTT sample");
    }

    #[test]
    fn rtt_estimator_converges_and_is_bounded() {
        let mut e = RttEstimator::new();
        assert_eq!(e.rto_ms(), 200, "unsampled default");
        for _ in 0..200 {
            e.add_sample(50);
        }
        assert!((e.srtt_ms() as i64 - 50).abs() <= 1, "srtt={}", e.srtt_ms());
        assert!(e.jitter_ms() <= 2, "jitter={}", e.jitter_ms());
        for _ in 0..50 {
            e.add_sample(5000);
        }
        assert!(e.rto_ms() <= 2000, "rto must be capped, got {}", e.rto_ms());
        assert!(e.rto_ms() >= 100, "rto must be floored");
    }

    #[test]
    fn sequence_buffer_rejects_duplicates() {
        let mut b = SequenceBuffer::new();
        assert!(b.insert(1).unwrap());
        assert!(!b.insert(1).unwrap());
        assert!(b.insert(2).unwrap());
        assert!(!b.insert(2).unwrap());
    }

    #[test]
    fn sequence_buffer_handles_reorder_and_remembers_behind_the_base() {
        let mut b = SequenceBuffer::new();
        for s in [5u32, 3, 4, 6] {
            assert!(b.insert(s).unwrap(), "seq {s} should be new");
        }
        assert!(b.has(3) && b.has(4) && b.has(5) && b.has(6));
        assert!(!b.has(2));
        assert!(!b.insert(3).unwrap(), "a reordered retransmit must be deduplicated");
    }

    #[test]
    fn sequence_buffer_survives_wrap() {
        let mut b = SequenceBuffer::new();
        let mut seq = u32::MAX - 3;
        for _ in 0..10 {
            assert!(b.insert(seq).unwrap(), "seq {seq} across wrap");
            seq = seq.wrapping_add(1);
        }
        assert!(b.has(0), "seq 0 must be tracked after wrapping");
        assert!(b.has(u32::MAX));
        // 10 contiguous values starting at 2^32-4 end at 5.
        assert_eq!(b.contiguous_end(), Some(5));
    }

    #[test]
    fn sequence_buffer_slides_on_a_large_jump() {
        let mut b = SequenceBuffer::new();
        b.insert(1).unwrap();
        assert!(b.insert(1000).unwrap());
        assert_eq!(b.base(), 1000 - (ACK_WINDOW as u32 - 1));
        assert!(b.has(1000));
        assert!(!b.has(1), "ancient history should be gone");
    }

    #[test]
    fn sequence_buffer_rejects_too_old() {
        let mut b = SequenceBuffer::new();
        b.insert(1000).unwrap();
        assert!(matches!(b.insert(1), Err(Error::SequenceOutOfWindow { .. })));
    }

    /// §B.5 item 3: the ack field follows the inbound buffer only.
    #[test]
    fn ack_seq_follows_the_inbound_buffer_only() {
        let mut b = SequenceBuffer::new();
        assert_eq!(ack_seq(&b), 0, "nothing received: the sentinel");

        b.insert(1).unwrap();
        assert_eq!(ack_seq(&b), 1);
        b.insert(3).unwrap();
        assert_eq!(ack_seq(&b), 1, "a gap must not be acknowledged");
        b.insert(2).unwrap();
        assert_eq!(ack_seq(&b), 3, "the contiguous run is what gets acknowledged");

        // Outbound evidence is a separate fact and must not influence it.
        let mut t = ReliableTracker::default();
        t.send(b"x".to_vec(), 0);
        assert!(!t.peer_has_acked_us());
        assert_eq!(ack_seq(&b), 3);
    }

    #[test]
    fn desync_counts_one_strike_per_tick() {
        let mut d = DesyncDetector::new(DesyncConfig { strike_threshold: 10 });
        assert!(!d.compare_tick(1, 0xAA, 0xBB));
        assert_eq!(d.strikes(), 1);
        // Comparing the same tick again must not add a strike (§B.5 item 4).
        assert!(!d.compare_tick(1, 0xAA, 0xBB));
        assert_eq!(d.strikes(), 1);
        assert!(!d.compare_tick(1, 0xAA, 0xBB));
        assert_eq!(d.strikes(), 1);
        assert!(!d.is_desynced());

        assert!(!d.compare_tick(2, 0xAA, 0xBB));
        assert_eq!(d.strikes(), 2);
        assert!(!d.compare_tick(3, 0xAA, 0xBB));
        assert_eq!(d.strikes(), 3);
    }

    #[test]
    fn desync_detects_after_the_threshold_and_clears_on_agreement() {
        let mut d = DesyncDetector::new(DesyncConfig { strike_threshold: 2 });
        d.compare_tick(1, 1, 2);
        assert!(!d.is_desynced());
        d.compare_tick(2, 1, 2);
        assert!(d.is_desynced(), "two sustained mismatches must trip the threshold");

        d.clear();
        assert!(!d.is_desynced());
        // The watermark survives `clear`, so re-reporting tick 2 is a no-op.
        assert!(!d.compare_tick(2, 1, 2));
        assert_eq!(d.strikes(), 0);

        d.compare_tick(3, 7, 7);
        assert_eq!(d.strikes(), 0);
    }

    #[test]
    fn desync_ignores_a_stale_tick() {
        let mut d = DesyncDetector::new(DesyncConfig { strike_threshold: 2 });
        d.compare_tick(10, 1, 2);
        assert_eq!(d.strikes(), 1);
        // An older tick describes a moment already accounted for.
        d.compare_tick(9, 1, 2);
        assert_eq!(d.strikes(), 1);
        assert_eq!(d.last_tick(), Some(10));
    }

    #[test]
    fn empty_tracker_is_quiescent() {
        let mut t = ReliableTracker::default();
        assert!(t.tick(1_000_000).retransmit.is_empty());
        assert_eq!(t.pending_len(), 0);
        assert!(!t.peer_has_acked_us());
        assert_eq!(t.config(), TrackerConfig::default());
    }
}
