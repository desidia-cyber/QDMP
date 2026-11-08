// qdenemybw.h -- keeping ENEMY_DETAIL bounded when a fight has many enemies.
//
// THE GAP THIS CLOSES (SYNC-GAPS 3, "Bandwidth with many enemies is unmeasured")
// -----------------------------------------------------------------------------
// ENEMY_DETAIL is sent on change only, but a large fight is exactly when montages change
// most: twenty enemies swinging means twenty events in the frame they swing.  The
// reliable table has 16 slots, and the previous fix -- a flat 10-per-second token
// bucket -- stops the flood but does nothing about ORDER: with 120 enemies in range the
// enemy that changed first wins the token, whether or not the partner can see it, while
// a boss five metres from the partner waits.
//
// This header replaces that bucket with three rules, each of which is a unit test:
//
//   1. RELEVANCE.  An enemy's detail only matters to the machine that draws it, so a
//      change is scored by the distance from the RECEIVER's player.  Near is sent
//      first and at the highest rate; an enemy beyond the far band is skipped unless
//      the event is critical, because nobody is close enough to see an animation on it.
//
//   2. PER-ENEMY RATE.  A montage that flaps (a locomotion blend cycling every few
//      frames) must not produce a packet per frame.  Each enemy has a minimum interval
//      between two sends, which is shorter for critical events than for cosmetic ones.
//
//   3. A GLOBAL BUDGET.  Never more than kMaxPerSec details in any rolling second, for
//      all enemies together.  A dropped one is NOT lost: the caller keeps the pending
//      change and retries next pump, so the lane degrades to "later" rather than "never".
//
// CRITICAL vs COSMETIC.  Attack, hurt, death, cast and roar are things a player judges
// the game by (the swing that hit them, the telegraph they had to dodge, the boss
// going down).  Movement is ambience.  A critical event bypasses its band's skip and
// uses the short interval; a cosmetic one respects both.
//
// WHY THE DECISION IS PURE
// ------------------------
// Every rule above is arithmetic over positions and timestamps.  Getting one wrong is
// invisible in review and expensive in a game run (packet storm, or a boss whose death
// animation never arrives), so the whole scheduler is a function of a candidate list and
// is enumerated in test_enemybw.cpp without a game.
#pragma once
#include <stdint.h>

namespace qd {
namespace enemybw {

// Distance bands, measured from the RECEIVER's player.  The figures follow the game's
// own stream/despawn scale rather than the network: an enemy at 30 m is fully in view
// and its animation is the thing the player is watching, at 80 m it is a silhouette, and
// at 150 m it is outside anything a player notices.
static const float kNearCm = 3000.0f;
static const float kMidCm  = 8000.0f;
static const float kFarCm  = 15000.0f;

// Never more than this many details in one rolling second, across all enemies.  The
// reliable table holds 16 entries and a detail is retransmitted until acked, so 10/s
// leaves most of the table for ACTOR_STATES/INTENT/TRAVEL -- the lanes that must not
// yield.  At 10/s a detail queue that backs up behind other traffic still drains.
static const int      kMaxPerSec = 10;
static const uint32_t kWindowMs  = 1000;

// Bounded table, recycled like every other record table in this codebase.
static const int      kMaxRecords = 256;
static const uint32_t kRecycleMs  = 60000;

enum Band : uint8_t { B_NEAR = 0, B_MID = 1, B_FAR = 2, B_OUT = 3 };

inline Band Classify(float distCm)
{
    if (!(distCm >= 0.0f)) return B_OUT;          // NaN is "unknown": not close
    if (distCm <= kNearCm) return B_NEAR;
    if (distCm <= kMidCm)  return B_MID;
    if (distCm <= kFarCm)  return B_FAR;
    return B_OUT;
}

// Minimum milliseconds between two sends of the same enemy, by band and urgency.
// Cosmetic (locomotion) changes are slow everywhere: a walking cycle re-classified every
// pump would otherwise fill the budget with one enemy.
inline uint32_t MinIntervalMs(Band b, bool critical)
{
    switch (b) {
    case B_NEAR: return critical ? 100 : 400;
    case B_MID:  return critical ? 200 : 800;
    case B_FAR:  return critical ? 500 : 2000;
    case B_OUT:
    default:     return critical ? 2000 : 0xFFFFFFFFu;   // cosmetic far away: never
    }
}

// Everything the decision needs about one enemy that has a change to report.
struct Cand {
    const uint32_t* key;      // 4 words; identifies the record
    float           dist_cm;  // from the receiver's player
    bool            critical; // attack/hurt/die/cast/roar rather than movement
    bool            haveLast; // has this enemy been sent before?
    uint32_t        lastMs;   // when, if haveLast
};

// Why a candidate was skipped, so the caller can log a reason rather than "dropped".
enum Skip : uint8_t {
    SK_NONE = 0,
    SK_INTERVAL,   // too soon for this enemy in this band
    SK_FAR,        // cosmetic and outside the far band
    SK_BUDGET,     // the rolling-second budget was exhausted
    SK_OVERFLOW,   // the record table was full
};

// Result of one selection pass.
struct Pick {
    int    index;   // into the caller's candidate array
    Skip   skip;    // SK_NONE when selected
};

class Scheduler {
public:
    Scheduler() { Reset(); }

    void Reset()
    {
        for (int i = 0; i < kMaxRecords; ++i) r_[i].used = false;
        for (int i = 0; i < kMaxPerSec; ++i) sent_[i] = 0;
        sentN_ = 0;
        sentHead_ = 0;
        dropped_ = 0;
        skippedFar_ = 0;
        overflow_ = 0;
    }

    // Select which of `n` candidates to send this tick.  `out` receives exactly the
    // selected indices, nearest/most-urgent first; it must hold at least `n` entries and
    // the return value is how many were written.  `why` (optional, n entries) receives
    // the reason for every candidate, selected or not, for the caller's log.
    int Select(const Cand* c, int n, uint32_t now_ms, int* out, Skip* why)
    {
        if (n <= 0) return 0;
        if (n > kMaxCand) n = kMaxCand;

        // Order: critical first, then nearest.  A stable insertion sort over <=256
        // entries is cheaper than adding a sort dependency and is deterministic, which
        // matters because two runs must produce the same selection for the same input.
        int order[kMaxCand];
        for (int i = 0; i < n; ++i) order[i] = i;
        for (int i = 1; i < n; ++i) {
            int v = order[i];
            int j = i - 1;
            while (j >= 0 && Before(c[v], c[order[j]])) { order[j + 1] = order[j]; --j; }
            order[j + 1] = v;
        }

        int avail = kMaxPerSec - InWindow(now_ms);
        if (avail < 0) avail = 0;
        int picked = 0;

        for (int i = 0; i < n; ++i) {
            int idx = order[i];
            if (why) why[idx] = SK_NONE;
            const Cand& x = c[idx];

            Rec* r = Touch(x.key, now_ms);
            if (!r) {
                if (why) why[idx] = SK_OVERFLOW;
                ++overflow_;
                continue;
            }

            Band b = Classify(x.dist_cm);
            uint32_t minGap = MinIntervalMs(b, x.critical);
            // The caller may state when it last sent (`haveLast`), otherwise the
            // scheduler's own record is used; with neither, this enemy has never been
            // sent and the interval does not apply.
            bool everSent = x.haveLast || r->haveSent;
            uint32_t since = 0xFFFFFFFFu;
            if (everSent) {
                uint32_t last = x.haveLast ? x.lastMs : r->lastMs;
                since = (uint32_t)(now_ms - last);
            }

            if (!x.critical && b == B_OUT) {
                if (why) why[idx] = SK_FAR;
                ++skippedFar_;
                continue;
            }
            // The per-enemy interval.  A critical event is NOT exempt from it: a death
            // that re-classifies every pump is a classification bug, and letting it
            // through unchecked is exactly the flood this header exists to prevent.  Its
            // gap is just much shorter (MinIntervalMs), so a real fight still shows every
            // swing while a stuck classification is throttled.
            if (everSent && since < minGap) {
                if (why) why[idx] = SK_INTERVAL;
                continue;
            }
            if (avail <= 0) {
                if (why) why[idx] = SK_BUDGET;
                ++dropped_;
                continue;
            }

            // Selected.
            --avail;
            out[picked++] = idx;
            r->haveSent = true;
            r->lastMs = now_ms;
            sent_[sentHead_] = now_ms;
            sentHead_ = (sentHead_ + 1) % (kMaxPerSec > 0 ? kMaxPerSec : 1);
            if (sentN_ < kMaxPerSec) ++sentN_;
        }
        return picked;
    }

    int      CandidateCap() const { return kMaxCand; }
    uint32_t BudgetDrops()  const { return dropped_; }
    uint32_t FarSkips()     const { return skippedFar_; }
    uint32_t Overflows()    const { return overflow_; }
    int      Count() const { int n = 0; for (int i = 0; i < kMaxRecords; ++i) n += r_[i].used ? 1 : 0; return n; }

    // How many sends fall inside the rolling window.  Public so a test can assert the
    // bound directly instead of inferring it from a selection.
    int InWindow(uint32_t now_ms) const
    {
        int n = 0;
        for (int i = 0; i < sentN_; ++i)
            if ((uint32_t)(now_ms - sent_[i]) < kWindowMs) ++n;
        return n;
    }

private:
    static const int kMaxCand = 256;

    struct Rec {
        bool     used;
        bool     haveSent;
        uint32_t key[4];
        uint32_t lastMs;
        uint32_t touchMs;
    };

    static bool Before(const Cand& a, const Cand& b)
    {
        if (a.critical != b.critical) return a.critical;
        return a.dist_cm < b.dist_cm;
    }

    Rec* Find(const uint32_t* g)
    {
        for (int i = 0; i < kMaxRecords; ++i) {
            if (!r_[i].used) continue;
            if (r_[i].key[0] == g[0] && r_[i].key[1] == g[1] &&
                r_[i].key[2] == g[2] && r_[i].key[3] == g[3]) return &r_[i];
        }
        return nullptr;
    }

    Rec* Touch(const uint32_t* g, uint32_t now_ms)
    {
        if (!g) return nullptr;
        if (!(g[0] | g[1] | g[2] | g[3])) return nullptr;   // no key, no record
        if (Rec* r = Find(g)) { r->touchMs = now_ms; return r; }
        int victim = -1;
        uint32_t oldest = 0;
        for (int i = 0; i < kMaxRecords; ++i) {
            if (!r_[i].used) { victim = i; break; }
            uint32_t age = (uint32_t)(now_ms - r_[i].touchMs);
            if (age > kRecycleMs && age > oldest) { oldest = age; victim = i; }
        }
        if (victim < 0) return nullptr;
        Rec& r = r_[victim];
        r.used = true;
        r.haveSent = false;
        r.lastMs = 0;
        for (int k = 0; k < 4; ++k) r.key[k] = g[k];
        r.touchMs = now_ms;
        return &r;
    }

    Rec      r_[kMaxRecords];
    uint32_t sent_[kMaxPerSec];
    int      sentN_;
    int      sentHead_;
    uint32_t dropped_;
    uint32_t skippedFar_;
    uint32_t overflow_;
};

} // namespace enemybw
} // namespace qd
