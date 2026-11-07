// qdmotion.h -- snapshot interpolation for the remote player.
//
// DEPENDENCY-FREE ON PURPOSE
// --------------------------
// No <windows.h>, no game headers, no allocation.  This is the part of the mod that
// took the previous project longest to get right, and the corpus says so explicitly:
// dollsmp-game/src/smoothing.rs "encodes the interpolation decisions that took the
// previous project the longest to get right. Keep it whole and retune the constants".
// Keeping it dependency-free is what makes it testable off-target, which is the only
// way a timing rule can be trusted before it is run inside a game.
//
// THE CONSTANTS, AND WHERE THEY COME FROM
// ---------------------------------------
//   kRenderDelayMs  = 100    the previous project measured a stable ~150-161 ms
//                            one-way offset through its bridge and settled on a
//                            fixed 100 ms playout delay.  Interpolation needs a
//                            sample on each side of render time, so the peer is
//                            drawn slightly in the past.
//   kExtrapCapMs    = 250    beyond that, extrapolation error grows faster than the
//                            correction it avoids; freeze instead.
//   kRing           = 32     ~0.5 s of history at 60 Hz: enough to bridge a burst.
//   kSnapDistanceCm = 2000   a discontinuity this large is a teleport or a level
//                            change, and interpolating across it draws a fast slide
//                            through geometry.  Snap.
//   kFreezeMs       = 2000   "never destroy a puppet on silence ... freeze at 2 s,
//                            hide at 30 s, keep the actor valid."
//   kHideMs         = 30000
//
// The 100 ppm figure in the source material is a CLOCK DRIFT allowance, not a
// parameter: it is why Sample() takes `now_ms` and interpolates on the sender's own
// timeline rather than on arrival order.  A receiver clock running 100 ppm fast
// accumulates 8.6 ms of error per day; interpolating on the sample timeline makes
// that harmless instead of cumulative.
#pragma once
#include <stdint.h>
#include <string.h>

namespace qd {
namespace motion {

struct MoveSample {
    uint32_t ms;
    float    px, py, pz;    // world cm
    int16_t  yaw, pitch;    // 1/65536 turn
    uint8_t  flags;
};

struct MoveOut {
    float px, py, pz;
    float yaw_deg, pitch_deg;
    bool  valid;
    bool  snapped;        // a discontinuity was detected on this sample
    bool  extrapolating;  // past the newest sample but inside the cap
    bool  frozen;         // no sample for kFreezeMs
    bool  should_hide;    // no sample for kHideMs
};

class Interp {
public:
    static const int      kRing          = 32;
    static const uint32_t kRenderDelayMs = 100;
    static const uint32_t kExtrapCapMs   = 250;
    static const uint32_t kFreezeMs      = 2000;
    static const uint32_t kHideMs        = 30000;
    // `inline constexpr`, not `static const`: a floating-point static data member with
    // an in-class initialiser is C2864, and the alternative (out-of-line definition) is
    // not possible for a header-only type included from two translation units.
    static constexpr float kSnapDistanceCm = 2000.0f;
    static constexpr float kSnapAngleDeg   = 120.0f;

    Interp() { Reset(); }

    void Reset()
    {
        n_ = 0;
        head_ = 0;
        lastSnap_ = false;
        haveExtrapVelocity_ = false;
        vx_ = vy_ = vz_ = 0.0f;
    }

    int Count() const { return n_; }

    // Samples must be monotonic in ms.  An out-of-order or duplicate sample is
    // DISCARDED rather than re-sorted: a re-ordered datagram is stale by definition,
    // and splicing it in would move the render position backwards.  This is the
    // "state, not events" rule applied to a stream.
    void Push(const MoveSample& s)
    {
        if (n_ > 0) {
            const MoveSample& last = At(n_ - 1);
            if (s.ms <= last.ms) return;

            // Discontinuity detection.  Compared against the newest sample, not the
            // interpolated position: a 2000 cm jump between two consecutive samples
            // is a teleport, and no amount of smoothing makes it look right.
            float dx = s.px - last.px;
            float dy = s.py - last.py;
            float dz = s.pz - last.pz;
            float d2 = dx * dx + dy * dy + dz * dz;
            float ang = AngleDelta(last.yaw, s.yaw);
            lastSnap_ = (d2 > kSnapDistanceCm * kSnapDistanceCm) ||
                        (ang > kSnapAngleDeg || ang < -kSnapAngleDeg);
        } else {
            lastSnap_ = false;
        }

        // Keep the extrapolation velocity in the units the sample arrived in (cm/ms),
        // derived from the last two samples.  Derived rather than transmitted: the
        // previous project found that sending velocity costs bandwidth and buys
        // nothing, because the receiver needs the *last interval's* velocity anyway.
        if (n_ >= 1) {
            const MoveSample& last = At(n_ - 1);
            float dt = (float)(s.ms - last.ms);
            if (dt > 0.0f && dt < 1000.0f) {
                vx_ = (s.px - last.px) / dt;
                vy_ = (s.py - last.py) / dt;
                vz_ = (s.pz - last.pz) / dt;
                haveExtrapVelocity_ = true;
            }
        }

        if (n_ < kRing) {
            ring_[(head_ + n_) % kRing] = s;
            ++n_;
        } else {
            // Full: drop the oldest.  A ring, not a grow: the mod must not allocate
            // from a hook.
            ring_[head_] = s;
            head_ = (head_ + 1) % kRing;
        }
    }

    MoveOut Sample(uint32_t now_ms) const
    {
        MoveOut o;
        o.px = o.py = o.pz = 0.0f;
        o.yaw_deg = o.pitch_deg = 0.0f;
        o.valid = false;
        o.snapped = lastSnap_;
        o.extrapolating = false;
        o.frozen = false;
        o.should_hide = false;

        if (n_ == 0) return o;

        const MoveSample& newest = At(n_ - 1);
        const MoveSample& oldest = At(0);
        uint32_t age = (now_ms > newest.ms) ? (now_ms - newest.ms) : 0;

        o.frozen = age >= kFreezeMs;
        o.should_hide = age >= kHideMs;

        // Render time in the sender's timeline.  Saturating subtraction: a sender
        // whose clock is ahead of ours (or a `now` captured before the sample was
        // stamped) must not wrap into a huge value and freeze the body.
        uint32_t target = (now_ms > kRenderDelayMs) ? (now_ms - kRenderDelayMs) : 0;

        if (n_ == 1 || target <= oldest.ms) {
            o.px = oldest.px; o.py = oldest.py; o.pz = oldest.pz;
            o.yaw_deg = AngleToDeg(oldest.yaw);
            o.pitch_deg = AngleToDeg(oldest.pitch);
            o.valid = true;
            o.frozen = false;
            o.should_hide = false;
            return o;
        }

        if (target >= newest.ms) {
            // Extrapolate, but no further than the cap.  The first version of this
            // function extrapolated from "now" instead of from render time, which
            // turned every late packet into a visible correction -- recorded as the
            // single most expensive movement mistake in the source corpus.
            uint32_t past = target - newest.ms;
            if (past > kExtrapCapMs) past = kExtrapCapMs;
            o.px = newest.px + vx_ * (float)past;
            o.py = newest.py + vy_ * (float)past;
            o.pz = newest.pz + vz_ * (float)past;
            o.yaw_deg = AngleToDeg(newest.yaw);
            o.pitch_deg = AngleToDeg(newest.pitch);
            o.valid = true;
            o.extrapolating = (past > 0);
            // While extrapolating, "frozen" still wins: a body with no data for 2 s
            // must stop being driven even if the extrapolation is short.
            o.frozen = age >= kFreezeMs;
            return o;
        }

        // Interpolate between the two bracketing samples.
        int i = 0;
        while (i < n_ - 1 && At(i + 1).ms <= target) ++i;
        const MoveSample& a = At(i);
        const MoveSample& b = At(i + 1);
        uint32_t span = (b.ms > a.ms) ? (b.ms - a.ms) : 1;
        float t = (float)(target - a.ms) / (float)span;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;

        o.px = a.px + (b.px - a.px) * t;
        o.py = a.py + (b.py - a.py) * t;
        o.pz = a.pz + (b.pz - a.pz) * t;

        // Angles take the short way round the circle.  Without this, crossing the
        // +/-180 boundary spins the body a full turn in one frame.
        int32_t da = ShortestTurn(a.yaw, b.yaw);
        int32_t yaw = a.yaw + (int32_t)((float)da * t);
        int32_t dp = ShortestTurn(a.pitch, b.pitch);
        int32_t pitch = a.pitch + (int32_t)((float)dp * t);
        o.yaw_deg = AngleToDeg((int16_t)(uint16_t)(uint32_t)yaw);
        o.pitch_deg = AngleToDeg((int16_t)(uint16_t)(uint32_t)pitch);

        o.valid = true;
        return o;
    }

private:
    const MoveSample& At(int i) const { return ring_[(head_ + i) % kRing]; }

    static float AngleToDeg(int16_t units) { return (float)units * 360.0f / 65536.0f; }

    // Signed angular units in (-32768, 32768].
    static int32_t ShortestTurn(int16_t from, int16_t to)
    {
        int32_t d = (int16_t)(uint16_t)((uint16_t)to - (uint16_t)from);
        return d;
    }

    static float AngleDelta(int16_t a, int16_t b)
    {
        return AngleToDeg((int16_t)ShortestTurn(a, b));
    }

    MoveSample ring_[kRing];
    int  n_;
    int  head_;
    bool lastSnap_;
    bool haveExtrapVelocity_;
    float vx_, vy_, vz_;
};

} // namespace motion
} // namespace qd
