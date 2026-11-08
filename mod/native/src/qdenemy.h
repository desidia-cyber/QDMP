// qdenemy.h -- enemy sync: who runs an enemy, and how its copy follows.
//
// THE MODEL (SYNC-DESIGN 2.6, refined by what the game actually does)
// --------------------------------------------------------------------
// The host simulates.  A peer's copy of an enemy the host is simulating is DRIVEN:
// its AI is off (the game's own `bIsAIDisabled`, actor tick off) and it follows the
// host's rows.  Identity is the authored FGuid of the enemy's SPAWNER
// (`EnemySpawner_C.GetSaveGUID()`, 14-combat-save-answers D4), never a pointer and
// never the enemy instance: one spawner owns one enemy (`CurrentManagedEnemy`).
//
// NOTE (SYNC-RESEARCH 5): `bIsAIDisabled` is native and is honoured, but the game's own
// way to stop an enemy is `EnemySpawner_C.SetEnemiesPaused(bool)`, which writes
// `EnemyBase_C.Paused` (+0x1F4E, a whole byte).  That is the lever to prefer; this
// header keeps the flag as the second one.
//
// One refinement the design did not have, forced by a measured fact: a spawner only
// spawns while ITS LOCAL player is in activation range (`PlayerIsInActivationRange`,
// `PlayerDistanceDespawnCheck` -- both Blueprint, both read `GetPlayerCharacter`, i.e.
// player 0 of that machine).  So when the players are apart, the host simply has no
// live enemy around the peer.  Freezing the peer's copies then would leave the peer in
// an area full of statues.  The rule is therefore per enemy:
//
//   host has a fresh row for this GUID   -> DRIVEN  (host simulates, peer follows)
//   host says this GUID is dead          -> KILL    (peer kills its copy through the
//                                                    game's own damage path; the host
//                                                    re-broadcasts recent deaths so a
//                                                    peer that was not looking learns)
//   otherwise                            -> LOCAL   (peer's own AI runs it; a kill the
//                                                    peer makes is sent to the host as
//                                                    a persistence record)
//
// HEALTH converges DOWNWARD ONLY, by applying the difference as real damage on the
// copy.  That is the game's own path, so hit reactions, the death animation, drops
// and the copy's own StoreEnemySaveState all happen on the peer exactly as they would
// for a local kill -- nothing is invented.  Never healing means a peer hit the host
// has not processed yet is not undone by an older row.
//
// Header-only and dependency-free, compiled into native/test like the other state
// machines.
#pragma once
#include <stdint.h>

namespace qd {
namespace enemy {

static const uint8_t kAlive  = 0x01;
static const uint8_t kDriven = 0x02;
static const uint8_t kBody   = 0x04;

inline bool SameGuid(const uint32_t a[4], const uint32_t b[4])
{
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}
inline bool ZeroGuid(const uint32_t g[4]) { return (g[0] | g[1] | g[2] | g[3]) == 0; }

// HP as a fraction of max, 0..65535 (WIRE.md 3.9).
inline uint16_t HpFrac(float hp, float maxHp)
{
    if (!(maxHp > 0.0f) || !(hp > 0.0f)) return 0;
    float f = hp / maxHp;
    if (f >= 1.0f) return 65535;
    int v = (int)(f * 65535.0f + 0.5f);
    if (v < 1) v = 1;                      // alive with a sliver is not "0 = dead"
    return (uint16_t)v;
}

// Damage to apply to a local copy so it matches the host.  0 = leave it.
//   * never heals (a peer hit in flight must not be undone by an older row);
//   * a 1 % dead band so float noise and regeneration do not produce a hit reaction
//     every frame;
//   * host dead -> overkill, so the copy dies through the game's death path.
inline float ConvergeDamage(float localHp, float localMax, uint16_t hostFrac, bool hostAlive)
{
    if (!(localMax > 0.0f) || !(localHp > 0.0f)) return 0.0f;
    if (!hostAlive) return localHp + localMax;
    float target = localMax * (float)hostFrac / 65535.0f;
    float d = localHp - target;
    if (d < localMax * 0.01f) return 0.0f;
    return d;
}

// A drop of the copy's HP that this mod did not cause is the local player's hit.
// Returned as a fraction of max (1/65535 units) so the host scales it by ITS max.
inline int32_t HitFrac(float prevHp, float curHp, float maxHp)
{
    if (!(maxHp > 0.0f) || !(prevHp > curHp)) return 0;
    float f = (prevHp - curHp) / maxHp;
    int32_t v = (int32_t)(f * 65535.0f + 0.5f);
    return v > 0 ? v : 0;
}

// ---------------------------------------------------------------------------
// HostView -- the peer's picture of what the host is simulating.
// ---------------------------------------------------------------------------
struct HostRow {
    bool     used;
    uint32_t guid[4];
    int32_t  px, py, pz;      // 1/8 cm
    int16_t  yaw;
    uint16_t hp;
    uint8_t  flags;
    uint32_t rx_ms;
};

enum Owner { LOCAL = 0, DRIVEN = 1, KILL = 2 };

class HostView {
public:
    static const int      kMax = 256;
    static const uint32_t kFreshMs   = 1000;   // a row this young means "host runs it"
    static const uint32_t kReleaseMs = 2000;   // a driven copy waits this long before
                                               // its own AI takes over (hysteresis:
                                               // the 20 Hz rows round-robin, and one
                                               // late datagram must not wake the AI)
    static const uint32_t kDeadStaleMs = 5000; // dead rows cycle in under 1 s

    HostView() { Reset(); }
    void Reset() { for (int i = 0; i < kMax; ++i) r_[i].used = false; overflow_ = false; }

    void OnRow(const uint32_t guid[4], int32_t px, int32_t py, int32_t pz, int16_t yaw,
               uint16_t hp, uint8_t flags, uint32_t now_ms)
    {
        if (ZeroGuid(guid)) return;
        HostRow* r = Find(guid);
        if (!r) r = Alloc(guid, now_ms);
        if (!r) { overflow_ = true; return; }
        r->px = px; r->py = py; r->pz = pz; r->yaw = yaw;
        r->hp = hp; r->flags = flags; r->rx_ms = now_ms;
    }

    const HostRow* Get(const uint32_t guid[4]) const
    {
        for (int i = 0; i < kMax; ++i) if (r_[i].used && SameGuid(r_[i].guid, guid)) return &r_[i];
        return nullptr;
    }

    Owner Decide(const uint32_t guid[4], bool currentlyDriven, uint32_t now_ms) const
    {
        const HostRow* r = Get(guid);
        if (!r) return LOCAL;
        uint32_t age = (now_ms > r->rx_ms) ? now_ms - r->rx_ms : 0;
        // A death is re-broadcast continuously by the host (KillLog), so it is
        // refreshed like a live row and goes stale like one.  Stale = the host forgot
        // it: it rested (ClearAllEnemySaveStates) and the enemy may live again.  A
        // death that never went stale would make the peer kill a respawned enemy
        // forever.
        if (!(r->flags & kAlive)) return age <= kDeadStaleMs ? KILL : LOCAL;
        if (age <= kFreshMs) return DRIVEN;
        if (currentlyDriven && age <= kReleaseMs) return DRIVEN;
        return LOCAL;
    }

    bool Overflowed() const { return overflow_; }
    int Count() const { int n = 0; for (int i = 0; i < kMax; ++i) n += r_[i].used ? 1 : 0; return n; }

private:
    HostRow* Find(const uint32_t g[4])
    {
        for (int i = 0; i < kMax; ++i) if (r_[i].used && SameGuid(r_[i].guid, g)) return &r_[i];
        return nullptr;
    }
    HostRow* Alloc(const uint32_t g[4], uint32_t now_ms)
    {
        int victim = -1;
        uint32_t oldest = 0;
        for (int i = 0; i < kMax; ++i) {
            if (!r_[i].used) { victim = i; break; }
            // Full: recycle the row that has been quiet longest, once it is well past
            // both staleness windows (it no longer decides anything).
            uint32_t age = (now_ms > r_[i].rx_ms) ? now_ms - r_[i].rx_ms : 0;
            if (age > 60000 && age > oldest) { oldest = age; victim = i; }
        }
        if (victim < 0) return nullptr;
        HostRow& r = r_[victim];
        r.used = true;
        for (int k = 0; k < 4; ++k) r.guid[k] = g[k];
        r.flags = 0;
        r.rx_ms = now_ms;
        return &r;
    }

    HostRow r_[kMax];
    bool    overflow_;
};

// ---------------------------------------------------------------------------
// KillLog -- the host's recent deaths, re-broadcast as dead rows.
// ---------------------------------------------------------------------------
// A live row stops when the enemy dies, so without this a peer that was not looking
// (area not loaded, datagram lost) would never learn of the death and would later run
// that enemy alive.  Captured from the host's own StoreEnemySaveState(hp = 0).
class KillLog {
public:
    static const int kMax = 64;
    KillLog() { Reset(); }
    void Reset() { n_ = 0; next_ = 0; }

    void Add(const uint32_t guid[4])
    {
        for (int i = 0; i < n_; ++i) if (SameGuid(g_[i], guid)) return;
        int slot = (n_ < kMax) ? n_++ : next_;
        next_ = (slot + 1) % kMax;
        for (int k = 0; k < 4; ++k) g_[slot][k] = guid[k];
    }
    // A respawn (rest, or the host saw it alive again) removes it.
    void Remove(const uint32_t guid[4])
    {
        for (int i = 0; i < n_; ++i) {
            if (!SameGuid(g_[i], guid)) continue;
            for (int j = i; j + 1 < n_; ++j) for (int k = 0; k < 4; ++k) g_[j][k] = g_[j + 1][k];
            --n_;
            if (next_ >= n_) next_ = 0;
            return;
        }
    }
    void Clear() { Reset(); }
    int Count() const { return n_; }
    const uint32_t* At(int i) const { return (i >= 0 && i < n_) ? g_[i] : nullptr; }

private:
    uint32_t g_[kMax][4];
    int n_, next_;
};

// ---------------------------------------------------------------------------
// RowCursor -- round-robin over a live set larger than one datagram.
// ---------------------------------------------------------------------------
// WIRE.md 3.9: at most 20 rows per datagram, never split a row, the rest go next tick.
class RowCursor {
public:
    RowCursor() : next_(0) {}
    // First index to send this tick and how many.
    int Take(int total, int maxRows, int* count)
    {
        if (total <= 0) { *count = 0; next_ = 0; return 0; }
        if (next_ >= total) next_ = 0;
        int first = next_;
        int n = total - first;
        if (n > maxRows) n = maxRows;
        *count = n;
        next_ = first + n;
        return first;
    }
private:
    int next_;
};

// ---------------------------------------------------------------------------
// Following a host row
// ---------------------------------------------------------------------------
// Rows arrive at 20 Hz / (rows / 20); the copy eases toward the latest one and snaps
// when it is far off (a teleport, a respawn, a first row).
static const float kSnapCm = 600.0f;

inline void Follow(float cur[3], const float target[3], float dtSec)
{
    float dx = target[0] - cur[0], dy = target[1] - cur[1], dz = target[2] - cur[2];
    float d2 = dx * dx + dy * dy + dz * dz;
    if (d2 > kSnapCm * kSnapCm || dtSec <= 0.0f) {
        cur[0] = target[0]; cur[1] = target[1]; cur[2] = target[2];
        return;
    }
    // Critically damped toward the target with a ~100 ms time constant, the same
    // render delay WIRE.md 5 fixes for players.
    float a = dtSec / (dtSec + 0.1f);
    cur[0] += dx * a; cur[1] += dy * a; cur[2] += dz * a;
}

inline float FollowYaw(float cur, float target, float dtSec)
{
    float d = target - cur;
    while (d > 180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    float a = (dtSec <= 0.0f) ? 1.0f : dtSec / (dtSec + 0.1f);
    return cur + d * a;
}

} // namespace enemy
} // namespace qd
