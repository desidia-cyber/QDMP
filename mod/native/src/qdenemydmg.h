// qdenemydmg.h -- the decision to describe a host-run enemy's swing to the partner.
//
// THE GAP THIS CLOSES (SYNC-GAPS G4, SYNC-RESEARCH 1c)
// ---------------------------------------------------
// Ownership follows the nearer player (qdenemyown.h), so an enemy standing next to the
// partner is run BY the partner and its attack lands through the game's own path.  The
// case this header covers is the other one: an enemy the HOST runs (the partner is
// beyond its spawner's activation range, or ownership is mid-hand-off) attacking
// across the distance.  The host sees the attack -- it owns the enemy and `E_ATTACK`
// is already classified into the ENEMY_DETAIL lane -- but it cannot apply the damage,
// because damage goes through `TryApplyDamage` on an `FB_Damageable` and the host has
// no object for the partner's pawn.
//
// So the host DESCRIBES the hit (wire::K_DAMAGE) and the peer PERFORMS it on its own
// pawn.  This header is the part of that which can be decided without the game:
//
//   * whether the peer was close enough to the swing to have been hit,
//   * whether this montage occurrence has already been described,
//   * and a bound on how many descriptions go out per second.
//
// WHY IT IS PURE
// --------------
// All three are rules, not engine calls.  Getting them wrong is invisible in a review
// and expensive in a game run: a per-frame send is a packet flood, a per-swing send
// that repeats is double damage, and a range test with the wrong operator either makes
// the partner invulnerable or lets a monster hit across a room.  Unit tests
// (test_enemydmg.cpp) pin each of those to a number.
//
// THE CONSTANTS, AND WHY THEY ARE THESE
// -------------------------------------
//   kRangeSlackCm     50   The peer's drawn body is 100 ms in the past (WIRE.md 5,
//                          qdmotion.h kRenderDelayMs).  A player at a 500 cm/s run is
//                          therefore drawn ~50 cm behind the true position.  The swing
//                          is judged on the drawn position, so a body exactly at the
//                          edge of the reach is treated as inside it; without the slack
//                          a hit the game would have landed disappears because the
//                          other machine rendered the victim half a metre early.
//   kFallbackRangeCm 300   Used only when no range can be resolved at all.  It is just
//                          above the 250 cm `AttackStopFollowRange` measured on
//                          SmallRobot_Base_Data (the distance at which the AI stops
//                          following to swing), so an unresolved melee attack decides at
//                          roughly the distance the enemy actually attacks from.  A log
//                          line marks this as an approximation; it is never silent.
//   kFallbackDamage   50   Used only when neither the attack getter nor the Data asset
//                          yields a number.  A typical enemy carries tens of points
//                          (SmallRobot_Base_Data: BaseDamage 67 against 415 HP), so 50
//                          is deliberately small: an unresolvable attacker must not be
//                          able to one-shot the partner.  Sent with kDamageGuessed.
//   kMaxPerSec        30   The detail lane reads montages on a 20 Hz pump
//                          (EnemySendPump), so 30 descriptions in a second cannot be
//                          produced by one enemy and is above the rate a normal fight
//                          generates.  It is the safety valve for a pathological fight
//                          (dozens of local enemies starting swings in one tick), not
//                          the expected rate; a drop is counted and logged.
//   kRateWindowMs   1000   The window the bound is stated over: "per second" has to
//                          mean a rolling second, not a bucket boundary, or two bursts
//                          either side of a clock tick pass.
//   kRecycleMs     60000   A record this old is past every window above and decides
//                          nothing; recycling it is how a long session with many
//                          spawners does not run out of records.
#pragma once
#include <math.h>
#include <stdint.h>

namespace qd {
namespace enemydmg {

static constexpr float kRangeSlackCm    = 50.0f;
static constexpr float kFallbackRangeCm = 300.0f;
static constexpr float kFallbackDamage  = 50.0f;

static const int      kMaxPerSec   = 30;
static const uint32_t kRateWindowMs = 1000;
static const uint32_t kRecycleMs   = 60000;
static const int      kMaxRecords  = 256;   // the enemy table's own cap (g_en[256])
static const int32_t  kMaxCenti    = 1000000;   // 10000 game units: a sanity clamp

inline bool ZeroGuid(const uint32_t g[4]) { return (g[0] | g[1] | g[2] | g[3]) == 0; }
inline bool SameGuid(const uint32_t a[4], const uint32_t b[4])
{
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

inline float Dist3(const float a[3], const float b[3])
{
    float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

// ---------------------------------------------------------------------------
// The range decision
// ---------------------------------------------------------------------------
// Everything the send path knows at the moment a new attack montage is classified.
// The booleans are inputs rather than globals so a test can hold every one of them
// still and vary exactly one: the unit tests are the only place the combination can be
// enumerated without a second machine.
struct Context {
    bool  host_owns;       // enemyown::O_LOCAL on the host
    bool  peer_in_world;   // the partner's PLAYER row does not say "menu" (g_peerInGameplay)
    bool  worlds_agree;    // stream::MapBoard::PeerStateApplies
    bool  have_enemy_pos;  // the local actor's transform was readable
    bool  have_peer_pos;   // the interpolated body exists and is not frozen
    float range_cm;        // the measured attack range, or the fallback constant
};

// True only when the host should describe this swing.  Each condition is a failure the
// live run must be able to attribute, so the caller logs which one refused; this
// function only decides.
inline bool ShouldSend(const Context& c, const float enemy[3], const float peer[3])
{
    if (!c.host_owns) return false;
    if (!c.peer_in_world) return false;
    if (!c.worlds_agree) return false;
    if (!c.have_enemy_pos || !c.have_peer_pos) return false;
    // A non-positive range is the "could not resolve" state, not a reach of zero: the
    // caller substitutes kFallbackRangeCm before asking, so reaching here means the
    // caller has no usable range and must not send.
    if (!(c.range_cm > 0.0f)) return false;
    return Dist3(enemy, peer) <= c.range_cm + kRangeSlackCm;
}

// Damage as the wire carries it: 1/100 of the game's unit.  Round to nearest;
// non-positive is "no hit" and returns 0 so the caller drops it rather than sending a
// zero that the receiver would have to interpret.
inline int32_t Centi(float damage)
{
    if (!(damage > 0.0f)) return 0;
    float v = damage * 100.0f;
    if (v > (float)kMaxCenti) v = (float)kMaxCenti;
    return (int32_t)(v + 0.5f);
}

// ---------------------------------------------------------------------------
// Per-swing de-duplication and the sustained-rate bound
// ---------------------------------------------------------------------------
// The trigger is a NEW montage: the owner sees a different active montage and the
// detail lane already bumps a per-enemy sequence counter for it.  Keying on
// (guid, seq) makes "one K_DAMAGE per swing" a fact rather than a hope: the detail
// lane can classify the same montage again (a status byte changed, the pump re-ran)
// without producing a second hit, and the counter's wrap from 255 to 1 is harmless
// because a new montage always changes the value.
//
// The second rule is a rolling-second bound.  A reliable datagram per swing is cheap,
// but an unbounded producer is the shape of a packet storm when something goes wrong
// (a moving montage misclassified as an attack, or a fight with hundreds of enemies).
class SwingGate {
public:
    SwingGate() { Reset(); }

    void Reset()
    {
        for (int i = 0; i < kMaxRecords; ++i) r_[i].used = false;
        for (int i = 0; i < kMaxPerSec; ++i) sent_[i] = 0;
        sentN_ = 0;
        sentHead_ = 0;
        overflow_ = 0;
        repeatDrops_ = 0;
        rateDrops_ = 0;
    }

    // True exactly once for a new (guid, seq).  False also covers the rate bound.
    bool Admit(const uint32_t guid[4], uint8_t seq, uint32_t now_ms)
    {
        if (ZeroGuid(guid)) return false;   // no attacker: nothing to describe

        Rec* r = Touch(guid, now_ms);
        if (!r) { ++overflow_; return false; }
        if (r->used && r->haveSeq && r->seq == seq) { ++repeatDrops_; return false; }

        if (InWindow(now_ms) >= kMaxPerSec) { ++rateDrops_; return false; }

        // Mark only an admitted swing: a rate-dropped one was not described, so a
        // retry (should a future caller make one) is not a duplicate.
        r->haveSeq = true;
        r->seq = seq;
        sent_[sentHead_] = now_ms;
        sentHead_ = (sentHead_ + 1) % kMaxPerSec;
        if (sentN_ < kMaxPerSec) ++sentN_;
        return true;
    }

    uint32_t RepeatDrops() const { return repeatDrops_; }
    uint32_t RateDrops()   const { return rateDrops_; }
    uint32_t Overflows()   const { return overflow_; }
    int      Count() const { int n = 0; for (int i = 0; i < kMaxRecords; ++i) n += r_[i].used ? 1 : 0; return n; }

private:
    struct Rec {
        bool     used;
        bool     haveSeq;
        uint32_t guid[4];
        uint8_t  seq;
        uint32_t touchMs;
    };

    // How many sends fall inside the rolling window.  The ring holds at most
    // kMaxPerSec times, so a full ring whose timestamps are all inside the window is
    // the cap; iterating all slots is simpler than maintaining a second cursor and is
    // bounded by a compile-time constant.
    int InWindow(uint32_t now_ms) const
    {
        int n = 0;
        for (int i = 0; i < sentN_; ++i)
            if ((uint32_t)(now_ms - sent_[i]) < kRateWindowMs) ++n;
        return n;
    }

    Rec* Find(const uint32_t g[4])
    {
        for (int i = 0; i < kMaxRecords; ++i)
            if (r_[i].used && SameGuid(r_[i].guid, g)) return &r_[i];
        return nullptr;
    }

    Rec* Touch(const uint32_t g[4], uint32_t now_ms)
    {
        Rec* r = Find(g);
        if (r) { r->touchMs = now_ms; return r; }

        int victim = -1;
        uint32_t oldest = 0;
        for (int i = 0; i < kMaxRecords; ++i) {
            if (!r_[i].used) { victim = i; break; }
            uint32_t age = (uint32_t)(now_ms - r_[i].touchMs);
            if (age > kRecycleMs && age > oldest) { oldest = age; victim = i; }
        }
        if (victim < 0) return nullptr;
        r = &r_[victim];
        r->used = true;
        r->haveSeq = false;
        r->seq = 0;
        for (int k = 0; k < 4; ++k) r->guid[k] = g[k];
        r->touchMs = now_ms;
        return r;
    }

    Rec      r_[kMaxRecords];
    uint32_t sent_[kMaxPerSec];
    int      sentN_;
    int      sentHead_;
    uint32_t overflow_;
    uint32_t repeatDrops_;
    uint32_t rateDrops_;
};

} // namespace enemydmg
} // namespace qd
