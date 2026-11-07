//! The WIRE.md §3.5 quantisers.
//!
//! WIRE.md fixes the *units* rather than a codec object: position and velocity are
//! integers in 1/8 cm, angles are integers in 1/65536 of a turn. DollsMP's
//! `Quantizer`/`PositionBounds` machinery is deliberately not ported — it is a
//! per-world bounded quantiser, and WIRE.md instead chose unbounded integers with
//! a fixed unit, which is both simpler and has no "the bounds were wrong for this
//! map" failure mode.
//!
//! Every function here reports the bound it actually meets, and the tests assert
//! against those numbers rather than against a moving tolerance:
//!
//! | quantity | wire unit | documented worst-case error |
//! |---|---|---|
//! | position | 1/8 cm | 1/16 cm (0.0625 cm) |
//! | angle | 1/65536 turn | 180/65536 deg (0.0027466 deg) |
//! | velocity | 1/8 cm/s | 1/16 cm/s (0.0625 cm/s), until it saturates at ±4095.875 cm/s |
//!
//! Decided by reading WIRE.md §3.5 and verified by the sweep tests below, which
//! also fail if the observed maximum error is suspiciously far *below* the bound
//! (a quantiser that has stopped quantising would pass a one-sided bound).

/// Wire units per centimetre for position. WIRE.md §3.5: "units of 1/8 cm".
pub const POSITION_UNITS_PER_CM: f32 = 8.0;
/// Size of one position wire unit, in centimetres.
pub const POSITION_STEP_CM: f32 = 1.0 / POSITION_UNITS_PER_CM;
/// Half a position wire unit: the worst-case round-trip error.
pub const POSITION_MAX_ERROR_CM: f32 = POSITION_STEP_CM * 0.5;
/// Largest position wire value the encoder produces, in wire units.
///
/// WIRE.md fixes the unit but not the saturation point, so this is chosen to match
/// `qdwire.h::QuantizePos` exactly: the native clamps at ±2 147 483 000 rather than
/// at `INT32_MAX`, because 2^31 is not representable in a float and the C++
/// conversion would be undefined. Clamping at the same value is what keeps the two
/// encoders byte-identical at the extreme, which is the only reason the clamp
/// exists at all.
pub const POSITION_MAX_WIRE: i32 = 2_147_483_000;

/// Wire units per full turn for angles. WIRE.md §3.5: "units of 1/65536 turn".
pub const ANGLE_UNITS_PER_TURN: f32 = 65536.0;
/// Size of one angle wire unit, in degrees.
pub const ANGLE_STEP_DEG: f32 = 360.0 / ANGLE_UNITS_PER_TURN;
/// Half an angle wire unit: the worst-case round-trip error.
pub const ANGLE_MAX_ERROR_DEG: f32 = ANGLE_STEP_DEG * 0.5;

/// Wire units per cm/s for velocity. WIRE.md §3.5: "units of 1/8 cm/s".
pub const VELOCITY_UNITS_PER_CM_S: f32 = 8.0;
/// Half a velocity wire unit: the worst-case round-trip error.
pub const VELOCITY_MAX_ERROR_CM_S: f32 = 0.5 / VELOCITY_UNITS_PER_CM_S;
/// Largest velocity representable in an `i16` at 1/8 cm/s, in cm/s.
pub const VELOCITY_MAX_ABS_CM_S: f32 = 32_767.0 / VELOCITY_UNITS_PER_CM_S;

/// Quantise a position in centimetres to WIRE.md's `i32` at 1/8 cm.
///
/// Saturates rather than wrapping: an object that flew off the map must read as
/// "very far away", never as "at the origin". `NaN` has no direction and maps to
/// zero.
///
/// The arithmetic is done in `f64` and the clamp is [`POSITION_MAX_WIRE`], both to
/// match `qdwire.h::QuantizePos` byte for byte. For any `f32` input the product is
/// exact in either width, so this changes no in-range result; it only makes the
/// extreme agree, where a `float → int32` conversion at 2^31 is undefined in C++.
pub fn position_to_wire(cm: f32) -> i32 {
    if cm.is_nan() {
        return 0;
    }
    let limit = f64::from(POSITION_MAX_WIRE);
    let scaled = f64::from(cm) * f64::from(POSITION_UNITS_PER_CM);
    let clamped = if scaled > limit {
        limit
    } else if scaled < -limit {
        -limit
    } else {
        scaled
    };
    // Round half away from zero; the value is now inside the `i32` range.
    clamped.round() as i32
}

/// Dequantise a position wire value back to centimetres.
pub fn position_from_wire(q: i32) -> f32 {
    q as f32 / POSITION_UNITS_PER_CM
}

/// Quantise an angle in degrees to WIRE.md's `i16` at 1/65536 turn.
///
/// `+180` and `-180` are the same orientation and both quantise to the same wire
/// value (32768, which reads back as -180); consumers must compare angles with
/// [`wrap_degrees`], not with `==`. Inputs are expected normalised to
/// [-180, 180], which is where the 1/65536-turn bound is stated and tested.
pub fn angle_to_wire(deg: f32) -> i16 {
    if !deg.is_finite() {
        return 0;
    }
    // In `f64`, matching `qdwire.h::QuantizeAngle`. This is not fussiness: at
    // |deg| ~ 360 the f32 path carries ~0.002 of a unit of error, which is enough
    // to round the other way for a value that lands within that distance of a
    // half-unit boundary. Doing it in the same width as the native side makes the
    // two encoders return the same `i16` for every `f32` input.
    let t = f64::from(deg) * f64::from(ANGLE_UNITS_PER_TURN) / 360.0;
    let units = t.round();
    // The two's-complement reinterpretation of the 16-bit turn fraction is the
    // wrap: +180 and -180 both reach 32768, which reads back as -180.
    let wrapped = units.rem_euclid(f64::from(ANGLE_UNITS_PER_TURN));
    (wrapped as i64 as u16) as i16
}

/// Dequantise an angle wire value back to degrees, in [-180, 180).
pub fn angle_from_wire(q: i16) -> f32 {
    q as f32 * ANGLE_STEP_DEG
}

/// Quantise a velocity in cm/s to WIRE.md's saturated `i16` at 1/8 cm/s.
pub fn velocity_to_wire(cm_s: f32) -> i16 {
    if cm_s.is_nan() {
        return 0;
    }
    let scaled = cm_s * VELOCITY_UNITS_PER_CM_S;
    if scaled >= i16::MAX as f32 {
        return i16::MAX;
    }
    if scaled <= i16::MIN as f32 {
        return i16::MIN;
    }
    scaled.round() as i16
}

/// Dequantise a velocity wire value back to cm/s.
pub fn velocity_from_wire(q: i16) -> f32 {
    q as f32 / VELOCITY_UNITS_PER_CM_S
}

/// Normalise an angle difference into `(-180, 180]`.
pub fn wrap_degrees(deg: f32) -> f32 {
    if !deg.is_finite() {
        return 0.0;
    }
    let mut d = (deg + 180.0) % 360.0;
    if d <= 0.0 {
        d += 360.0;
    }
    d - 180.0
}

pub fn position_to_wire3(p: [f32; 3]) -> [i32; 3] {
    [position_to_wire(p[0]), position_to_wire(p[1]), position_to_wire(p[2])]
}

pub fn position_from_wire3(q: [i32; 3]) -> [f32; 3] {
    [position_from_wire(q[0]), position_from_wire(q[1]), position_from_wire(q[2])]
}

pub fn velocity_to_wire3(v: [f32; 3]) -> [i16; 3] {
    [velocity_to_wire(v[0]), velocity_to_wire(v[1]), velocity_to_wire(v[2])]
}

pub fn velocity_from_wire3(q: [i16; 3]) -> [f32; 3] {
    [velocity_from_wire(q[0]), velocity_from_wire(q[1]), velocity_from_wire(q[2])]
}

pub fn angles_to_wire(deg: [f32; 2]) -> [i16; 2] {
    [angle_to_wire(deg[0]), angle_to_wire(deg[1])]
}

pub fn angles_from_wire(q: [i16; 2]) -> [f32; 2] {
    [angle_from_wire(q[0]), angle_from_wire(q[1])]
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn position_unit_is_one_eighth_centimetre() {
        assert_eq!(position_to_wire(0.0), 0);
        assert_eq!(position_to_wire(1.0), 8);
        assert_eq!(position_to_wire(-1.0), -8);
        assert_eq!(position_from_wire(8), 1.0);
        // Adjacent wire values are 0.125 cm apart.
        assert!((position_from_wire(1) - position_from_wire(0) - 0.125).abs() < 1e-6);
    }

    #[test]
    fn position_roundtrip_error_never_exceeds_the_documented_bound() {
        // Sweep 5 km of range (any Queen's Domain map) at a step that is not a
        // multiple of the wire unit, so every rounding case is hit.
        let mut cm = -500_000.0f32;
        while cm <= 500_000.0 {
            let q = position_to_wire(cm);
            let back = position_from_wire(q);
            let err = (back - cm).abs();
            assert!(
                err <= POSITION_MAX_ERROR_CM + 1e-4,
                "position {cm} -> {back} error {err} exceeds {POSITION_MAX_ERROR_CM}"
            );
            cm += 3.7;
        }
    }

    /// The bound is *tight*, not merely satisfied: points offset by half a wire
    /// unit must quantise away from the nearest grid point by exactly 1/16 cm.
    /// Without this, a quantiser that rounded to the nearest centimetre would pass
    /// the bound test above by being wrong in a different way.
    #[test]
    fn position_worst_case_is_exactly_half_a_wire_unit() {
        let mut worst: f32 = 0.0;
        for k in -20_000i64..=20_000 {
            // (2k+1)/16 cm = a half-integer number of wire units, computed from an
            // exactly representable constant so the expectation is not itself noisy.
            let cm = (k as f32 + 0.5) * 0.125;
            let err = (position_from_wire(position_to_wire(cm)) - cm).abs();
            worst = worst.max(err);
        }
        assert!((0.0624..=0.0626).contains(&worst), "worst-case position error {worst}");
    }

    #[test]
    fn position_saturates_and_handles_non_finite() {
        // The clamp value is the native one (qdwire.h::QuantizePos), so the two
        // encoders agree at the extreme; see `POSITION_MAX_WIRE`.
        assert_eq!(position_to_wire(f32::INFINITY), POSITION_MAX_WIRE);
        assert_eq!(position_to_wire(f32::NEG_INFINITY), -POSITION_MAX_WIRE);
        assert_eq!(position_to_wire(f32::MAX), POSITION_MAX_WIRE);
        assert_eq!(position_to_wire(1e12), POSITION_MAX_WIRE);
        assert_eq!(position_to_wire(-1e12), -POSITION_MAX_WIRE);
        assert_eq!(position_to_wire(f32::NAN), 0);
        // 2_147_483_000 wire units is 2684 km, not the "268 m" WIRE.md §3.5 says.
        assert!((position_from_wire(POSITION_MAX_WIRE) / 100_000.0 - 2684.0).abs() < 1.0);
    }

    #[test]
    fn position_large_magnitude_does_not_wrap() {
        // Saturation, never wraparound: a wrapped position is a teleport.
        let mut prev = i64::from(position_to_wire(-1e9));
        let mut cm = -1e9f32;
        while cm < 1e9 {
            let q = i64::from(position_to_wire(cm));
            assert!(q >= prev, "position quantisation went backwards at {cm}");
            prev = q;
            cm += 10_000.0;
        }
    }

    #[test]
    fn angle_unit_is_one_65536th_of_a_turn() {
        assert_eq!(angle_to_wire(0.0), 0);
        assert_eq!(angle_to_wire(90.0), 16384);
        assert_eq!(angle_to_wire(-90.0), -16384);
        assert!((ANGLE_STEP_DEG - 0.0054931640625).abs() < 1e-12);
    }

    #[test]
    fn angle_roundtrip_error_never_exceeds_the_documented_bound() {
        // A full turn at a step that is not a multiple of the wire unit.
        let mut deg = -180.0f32;
        while deg <= 180.0 {
            let back = angle_from_wire(angle_to_wire(deg));
            let err = wrap_degrees(back - deg).abs();
            assert!(
                err <= ANGLE_MAX_ERROR_DEG + 1e-4,
                "angle {deg} -> {back} error {err} exceeds {ANGLE_MAX_ERROR_DEG}"
            );
            deg += 0.37;
        }
    }

    /// Half a wire unit, exactly, at every half-unit offset.
    #[test]
    fn angle_worst_case_is_exactly_half_a_wire_unit() {
        let mut worst: f32 = 0.0;
        for k in -32_768i64..=32_767 {
            let deg = (k as f32 + 0.5) * ANGLE_STEP_DEG;
            let back = angle_from_wire(angle_to_wire(deg));
            worst = worst.max(wrap_degrees(back - deg).abs());
        }
        assert!(
            (ANGLE_MAX_ERROR_DEG - 1e-4..=ANGLE_MAX_ERROR_DEG + 1e-4).contains(&worst),
            "worst-case angle error {worst}"
        );
    }

    #[test]
    fn angle_full_turn_and_half_turn_are_equivalent() {
        assert_eq!(angle_to_wire(0.0), angle_to_wire(360.0));
        assert_eq!(angle_to_wire(0.0), angle_to_wire(-360.0));
        assert_eq!(angle_to_wire(45.0), angle_to_wire(405.0));
        // +180 and -180 are one orientation; both land on the same wire value.
        assert_eq!(angle_to_wire(180.0), angle_to_wire(-180.0));
        assert_eq!(angle_from_wire(angle_to_wire(180.0)), -180.0);
    }

    #[test]
    fn angle_handles_non_finite() {
        assert_eq!(angle_to_wire(f32::NAN), 0);
        assert_eq!(angle_to_wire(f32::INFINITY), 0);
        assert_eq!(wrap_degrees(f32::NAN), 0.0);
    }

    #[test]
    fn wrap_degrees_normalises() {
        assert!((wrap_degrees(190.0) - -170.0).abs() < 1e-3);
        assert!((wrap_degrees(-190.0) - 170.0).abs() < 1e-3);
        // The range is (-180, 180]: a whole extra turn is the same orientation.
        assert!((wrap_degrees(540.0) - 180.0).abs() < 1e-3);
        assert!((wrap_degrees(0.0)).abs() < 1e-6);
        assert!((wrap_degrees(180.0) - 180.0).abs() < 1e-6);
        assert!((wrap_degrees(-180.0) - 180.0).abs() < 1e-6);
    }

    #[test]
    fn velocity_unit_and_saturation() {
        assert_eq!(velocity_to_wire(0.0), 0);
        assert_eq!(velocity_to_wire(1.0), 8);
        assert_eq!(velocity_to_wire(-1.0), -8);
        assert_eq!(velocity_to_wire(1e9), i16::MAX);
        assert_eq!(velocity_to_wire(-1e9), i16::MIN);
        assert_eq!(velocity_to_wire(f32::INFINITY), i16::MAX);
        assert_eq!(velocity_to_wire(f32::NEG_INFINITY), i16::MIN);
        assert_eq!(velocity_to_wire(f32::NAN), 0);
        assert_eq!(velocity_from_wire(velocity_to_wire(1e9)), VELOCITY_MAX_ABS_CM_S);
        assert!((VELOCITY_MAX_ABS_CM_S - 4095.875).abs() < 1e-6);
    }

    #[test]
    fn velocity_roundtrip_error_never_exceeds_the_documented_bound() {
        let mut v = -4000.0f32;
        while v <= 4000.0 {
            let back = velocity_from_wire(velocity_to_wire(v));
            let err = (back - v).abs();
            assert!(
                err <= VELOCITY_MAX_ERROR_CM_S + 1e-4,
                "velocity {v} -> {back} error {err} exceeds {VELOCITY_MAX_ERROR_CM_S}"
            );
            v += 1.3;
        }
    }

    /// Half a wire unit, exactly, at every half-unit offset inside the saturating
    /// range.
    #[test]
    fn velocity_worst_case_is_exactly_half_a_wire_unit() {
        let mut worst: f32 = 0.0;
        for k in -32_767i64..=32_766 {
            let v = (k as f32 + 0.5) * 0.125;
            let err = (velocity_from_wire(velocity_to_wire(v)) - v).abs();
            worst = worst.max(err);
        }
        assert!((0.0624..=0.0626).contains(&worst), "worst-case velocity error {worst}");
    }

    #[test]
    fn tuple_helpers_roundtrip() {
        let p = [1234.5f32, -9876.25, 543.125];
        let back = position_from_wire3(position_to_wire3(p));
        for i in 0..3 {
            assert!((back[i] - p[i]).abs() <= POSITION_MAX_ERROR_CM + 1e-4, "{p:?} -> {back:?}");
        }

        let v = [300.0f32, -1500.5, 800.25];
        let back = velocity_from_wire3(velocity_to_wire3(v));
        for i in 0..3 {
            assert!((back[i] - v[i]).abs() <= VELOCITY_MAX_ERROR_CM_S + 1e-4);
        }

        let a = [45.0f32, -170.5];
        let back = angles_from_wire(angles_to_wire(a));
        for i in 0..2 {
            assert!(wrap_degrees(back[i] - a[i]).abs() <= ANGLE_MAX_ERROR_DEG + 1e-4);
        }
    }
}
