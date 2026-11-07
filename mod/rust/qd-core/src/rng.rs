//! Deterministic RNG for anything both peers must agree on.
//!
//! Ported unchanged from `dollsmp-core::rng` (analysis/03 §B.1 marks it **AS-IS**:
//! it carries a published golden vector, which is exactly the property a shared
//! simulation needs). Anything the mod itself decides — cosmetic variance, test
//! fixtures, break-before-make pacing — must be reproducible across peers, so it
//! uses this generator rather than the host language's default RNG.
//!
//! PCG-XSH-RR 64/32: small, fast, well tested, and trivial to reimplement on the
//! native side.

/// PCG-XSH-RR 64/32 generator.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Pcg32 {
    state: u64,
    inc: u64,
}

impl Pcg32 {
    /// Create a generator from an explicit seed and stream id.
    pub fn new(seed: u64, stream: u64) -> Self {
        let mut g = Self { state: 0, inc: (stream << 1) | 1 };
        g.step();
        g.state = g.state.wrapping_add(seed);
        g.step();
        g
    }

    #[inline]
    fn step(&mut self) {
        self.state =
            self.state.wrapping_mul(6_364_136_223_846_793_005).wrapping_add(self.inc);
    }

    /// Next 32-bit output.
    pub fn next_u32(&mut self) -> u32 {
        let old = self.state;
        self.step();
        let xorshifted = (((old >> 18) ^ old) >> 27) as u32;
        let rot = (old >> 59) as u32;
        xorshifted.rotate_right(rot)
    }

    pub fn next_u64(&mut self) -> u64 {
        (u64::from(self.next_u32()) << 32) | u64::from(self.next_u32())
    }

    /// Uniform in `[0, bound)`.
    ///
    /// Lemire's multiply-shift with rejection, so the distribution is exactly
    /// uniform — a biased RNG would eventually desync a long session. Panics only
    /// on `bound == 0`, which is a caller bug, not a wire condition.
    pub fn next_bounded(&mut self, bound: u32) -> u32 {
        assert!(bound > 0, "bound must be non-zero");
        let mut x = self.next_u32();
        let mut m = u64::from(x) * u64::from(bound);
        let mut l = m as u32;
        if l < bound {
            let threshold = bound.wrapping_neg() % bound;
            while l < threshold {
                x = self.next_u32();
                m = u64::from(x) * u64::from(bound);
                l = m as u32;
            }
        }
        (m >> 32) as u32
    }

    pub fn next_f32(&mut self) -> f32 {
        // 24 mantissa bits give a uniform value in [0, 1).
        (self.next_u32() >> 8) as f32 / (1u32 << 24) as f32
    }

    pub fn next_bool(&mut self) -> bool {
        self.next_u32() & 1 == 1
    }

    pub fn state(&self) -> u64 {
        self.state
    }

    pub fn inc(&self) -> u64 {
        self.inc
    }
}

impl Default for Pcg32 {
    fn default() -> Self {
        Self::new(0, 0)
    }
}

/// A deterministic 64-bit hash of a stream of values (FNV-1a plus one avalanche
/// round), for building seeds from session parameters reproducibly.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct SeedMixer {
    h: u64,
}

impl SeedMixer {
    pub fn new() -> Self {
        Self { h: 0xcbf2_9ce4_8422_2325 }
    }

    pub fn add_u64(mut self, v: u64) -> Self {
        for i in 0..8 {
            self.h ^= (v >> (i * 8)) & 0xFF;
            self.h = self.h.wrapping_mul(0x0000_0100_0000_01b3);
        }
        self
    }

    pub fn add_u32(self, v: u32) -> Self {
        self.add_u64(u64::from(v))
    }

    pub fn add_str(self, s: &str) -> Self {
        let mut m = self;
        for b in s.as_bytes() {
            m.h ^= u64::from(*b);
            m.h = m.h.wrapping_mul(0x0000_0100_0000_01b3);
        }
        m
    }

    /// Finalize into a seed (not a stream id — those must be odd).
    pub fn finish(self) -> u64 {
        let mut x = self.h;
        x ^= x >> 33;
        x = x.wrapping_mul(0xff51_afd7_ed55_8ccd);
        x ^= x >> 33;
        x = x.wrapping_mul(0xc4ce_b9fe_1a85_ec53);
        x ^= x >> 33;
        x
    }
}

impl Default for SeedMixer {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn same_seed_same_sequence() {
        let mut a = Pcg32::new(12345, 7);
        let mut b = Pcg32::new(12345, 7);
        for _ in 0..1000 {
            assert_eq!(a.next_u32(), b.next_u32());
        }
    }

    #[test]
    fn different_seed_diverges() {
        let mut a = Pcg32::new(1, 0);
        let mut b = Pcg32::new(2, 0);
        let sa: Vec<u32> = (0..32).map(|_| a.next_u32()).collect();
        let sb: Vec<u32> = (0..32).map(|_| b.next_u32()).collect();
        assert_ne!(sa, sb);
    }

    #[test]
    fn different_stream_diverges() {
        let mut a = Pcg32::new(99, 0);
        let mut b = Pcg32::new(99, 1);
        let sa: Vec<u32> = (0..32).map(|_| a.next_u32()).collect();
        let sb: Vec<u32> = (0..32).map(|_| b.next_u32()).collect();
        assert_ne!(sa, sb);
    }

    #[test]
    fn stream_id_is_forced_odd() {
        // PCG requires an odd increment; an even stream id must be corrected
        // rather than silently producing a degenerate cycle.
        let g = Pcg32::new(0, 8);
        assert_eq!(g.inc() & 1, 1);
    }

    /// Golden vector for the canonical PCG32 reference (seed 42, stream 54).
    ///
    /// The first six match the published `pcg32-demo` output, which is what proves
    /// this is real PCG-XSH-RR and not a lookalike. Intentionally brittle: if it
    /// ever changes, every recorded session becomes non-reproducible.
    #[test]
    fn golden_vector_matches_the_reference_implementation() {
        let mut g = Pcg32::new(42, 54);
        let got: Vec<u32> = (0..8).map(|_| g.next_u32()).collect();
        assert_eq!(
            got,
            vec![
                0xA15C_02B7,
                0x7B47_F409,
                0xBA1D_3330,
                0x83D2_F293,
                0xBFA4_784B,
                0xCBED_606E,
                0xBFC6_A3AD,
                0x812F_FF6D,
            ]
        );
    }

    #[test]
    fn bounded_respects_bound() {
        let mut g = Pcg32::new(7, 7);
        for _ in 0..10_000 {
            let v = g.next_bounded(10);
            assert!(v < 10, "got {v}");
        }
    }

    #[test]
    fn bounded_one_always_zero() {
        let mut g = Pcg32::new(1, 1);
        for _ in 0..100 {
            assert_eq!(g.next_bounded(1), 0);
        }
    }

    #[test]
    fn bounded_is_roughly_uniform() {
        let mut g = Pcg32::new(0xDEAD, 0xBEEF);
        let mut counts = [0u32; 8];
        const N: u32 = 80_000;
        for _ in 0..N {
            counts[g.next_bounded(8) as usize] += 1;
        }
        let expect = N / 8;
        for (i, c) in counts.iter().enumerate() {
            let dev = (*c as i64 - expect as i64).abs() as f64 / expect as f64;
            assert!(dev < 0.05, "bucket {i} deviated {:.1}%", dev * 100.0);
        }
    }

    #[test]
    fn f32_is_in_unit_interval() {
        let mut g = Pcg32::new(3, 3);
        for _ in 0..10_000 {
            let v = g.next_f32();
            assert!((0.0..1.0).contains(&v), "got {v}");
        }
    }

    #[test]
    fn u64_uses_both_halves() {
        let mut g = Pcg32::new(11, 13);
        let mut seen_high = false;
        for _ in 0..100 {
            if g.next_u64() >> 32 != 0 {
                seen_high = true;
            }
        }
        assert!(seen_high, "high half never populated");
    }

    #[test]
    fn seed_mixer_is_deterministic() {
        let a = SeedMixer::new().add_str("Hod").add_u32(1).add_u64(999).finish();
        let b = SeedMixer::new().add_str("Hod").add_u32(1).add_u64(999).finish();
        assert_eq!(a, b);
    }

    #[test]
    fn seed_mixer_is_order_sensitive() {
        let a = SeedMixer::new().add_u32(1).add_u32(2).finish();
        let b = SeedMixer::new().add_u32(2).add_u32(1).finish();
        assert_ne!(a, b);
    }

    #[test]
    fn seed_mixer_avalanches_similar_inputs() {
        let a = SeedMixer::new().add_u32(1000).finish();
        let b = SeedMixer::new().add_u32(1001).finish();
        let diff = (a ^ b).count_ones();
        assert!(diff > 16, "weak avalanche: only {diff} bits differ");
    }

    #[test]
    fn rng_drives_deterministic_simulation() {
        // The property the netcode actually depends on: two peers replaying the
        // same decisions from the same seed stay in lockstep.
        fn simulate(seed: u64) -> Vec<u32> {
            let mut g = Pcg32::new(seed, 1);
            (0..256).map(|_| g.next_bounded(100)).collect()
        }
        assert_eq!(simulate(0xABCD), simulate(0xABCD));
        assert_ne!(simulate(0xABCD), simulate(0xABCE));
    }
}
