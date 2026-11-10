// qdboss.h -- who may hold the one shared boss bar, and whose word decides liveness.
//
// THE MEASURED FAILURE (Session 19, R143; the user's own report)
// -------------------------------------------------------------
// The game has ONE boss HP bar and `ActivateBossHPBar` binds it to the actor it is given.
// Session 19 made the HOST the single selector, so both machines would name the same key.
// The live pair still showed two different bars, and the logs said why:
//
//   host:  boss: sent guid=03FDBC21 ... bar on hp=65535 (repair)     every 3 s
//   peer:  boss: no local copy for guid=03FDBC21; the bar cannot be raised here
//   peer:  boss: guid=17181166 is a boss                             (its own dragon)
//
// `03FDBC21` (Boner_Shrineboss_BP_C) existed only on the host: it was outside the 25 m
// mirror-relevance radius, so the peer never got a copy, and the host's selector had no
// idea its choice was unshowable.  The peer's own `17181166` was the dragon the host
// already held as a standing death -- an owner/non-owner liveness disagreement on one
// shared key.  So the selection step, not the binding, was wrong: a bar the partner
// cannot show is not a shared bar, and a key's liveness must be read from the machine
// that RUNS it.
//
// THE RULE THIS HEADER IMPLEMENTS
// -------------------------------
//   * LIVENESS IS THE OWNER'S.  `g_own.Running(key)` says which machine simulates an
//     enemy.  That machine's own answer (dead flag / HP) is the fact; the other
//     machine's local copy is a follower and must never contradict it.  With no owner
//     row yet, the follower answers "not alive" rather than guessing.
//
//   * ONE BAR, AND ONLY A SHOWABLE KEY.  The host ASKS "do you hold this key alive?"
//     before it raises anything.  An ACK makes the key the shared bar; a NACK (or a
//     timeout) makes it ineligible for a while, and the host selects the next candidate.
//     If nothing is showable on both machines, NO bar is raised -- which is the honest
//     agreement, not a bar on one screen and nothing on the other.
//
// WHY A REFUSAL EXPIRES
// ---------------------
// A key the peer lacks today may appear later (its spawner activates, or a mirror is
// constructed).  A permanent refusal would keep a real fight dark for the session; a
// short one retries the same question cheaply.  An ACK is not retried -- it is a fact
// about the key, cleared when the key dies or the session/world resets.
//
// Header-only and dependency-free, compiled into native/test like the other state
// machines.
#pragma once
#include <stdint.h>

namespace qd {
namespace boss {

inline bool SameKey(const uint32_t a[4], const uint32_t b[4])
{
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}
inline bool ZeroKey(const uint32_t g[4]) { return (g[0] | g[1] | g[2] | g[3]) == 0; }

// ---------------------------------------------------------------------------
// Liveness authority
// ---------------------------------------------------------------------------
// `ownerHere`      this machine runs the enemy (`enemyown::OwnerSet::Running`).
// `localAlive`     this machine's own read of its actor (meaningful only when ownerHere).
// `haveOwnerRow`   the partner (the owner) has published a row for the key.
// `ownerSaysAlive` that row's `kAlive` bit.
//
// A follower with no row is NOT alive: an enemy nothing is describing cannot be shown,
// and a stale "alive" from before the owner's death is exactly the divergence R143
// measured.  A follower WITH a row takes the owner's bit verbatim -- the local copy may
// be mid-respawn and is not evidence.
inline bool AliveByAuthority(bool ownerHere, bool localAlive,
                             bool haveOwnerRow, bool ownerSaysAlive)
{
    if (ownerHere) return localAlive;
    return haveOwnerRow && ownerSaysAlive;
}

// ---------------------------------------------------------------------------
// The ask/ack board (host side)
// ---------------------------------------------------------------------------
// Bounded and tiny: the game shows one bar, so a host asks about at most a handful of
// candidates before it settles.  An ACK and a REFUSAL are separate facts for the same
// key and both are kept; `Clear` forgets both when a key dies.
class AckBoard {
public:
    static const int      kMax = 16;
    // Long enough for the peer to construct a mirror it was already owed, short enough
    // that a fight started after a stream-in is picked up inside one engagement.
    static const uint32_t kRefuseMs = 5000;

    AckBoard() { Reset(); }

    void Reset()
    {
        for (int i = 0; i < kMax; ++i) { a_[i].used = false; n_[i].used = false; }
    }

    void OnAck(const uint32_t key[4])
    {
        if (ZeroKey(key)) return;
        Slot* s = TouchA(key);
        if (s) s->acked = true;
        Slot* n = FindN(key);
        if (n) n->used = false;             // a later ACK supersedes an old refusal
    }

    void OnNack(const uint32_t key[4], uint32_t now)
    {
        if (ZeroKey(key)) return;
        Slot* n = TouchN(key, now);
        if (n) { n->refusedAt = now; }
        Slot* a = FindA(key);
        if (a) a->acked = false;            // the peer no longer holds it
    }

    bool Acked(const uint32_t key[4]) const
    {
        const Slot* s = FindA(key);
        return s && s->acked;
    }

    bool Refused(const uint32_t key[4], uint32_t now) const
    {
        const Slot* s = FindN(key);
        if (!s) return false;
        return (uint32_t)(now - s->refusedAt) <= kRefuseMs;
    }

    void Clear(const uint32_t key[4])
    {
        Slot* a = FindA(key); if (a) a->used = false;
        Slot* n = FindN(key); if (n) n->used = false;
    }

    int AckedCount() const
    {
        int n = 0;
        for (int i = 0; i < kMax; ++i) n += (a_[i].used && a_[i].acked) ? 1 : 0;
        return n;
    }
    int RefusedCount(uint32_t now) const
    {
        int n = 0;
        for (int i = 0; i < kMax; ++i)
            if (n_[i].used && (uint32_t)(now - n_[i].refusedAt) <= kRefuseMs) ++n;
        return n;
    }

private:
    struct Slot {
        bool     used;
        uint32_t key[4];
        bool     acked;       // ack table only
        uint32_t refusedAt;   // nack table only
    };

    Slot* FindA(const uint32_t k[4])
    {
        for (int i = 0; i < kMax; ++i)
            if (a_[i].used && SameKey(a_[i].key, k)) return &a_[i];
        return nullptr;
    }
    const Slot* FindA(const uint32_t k[4]) const
    {
        for (int i = 0; i < kMax; ++i)
            if (a_[i].used && SameKey(a_[i].key, k)) return &a_[i];
        return nullptr;
    }
    Slot* FindN(const uint32_t k[4])
    {
        for (int i = 0; i < kMax; ++i)
            if (n_[i].used && SameKey(n_[i].key, k)) return &n_[i];
        return nullptr;
    }
    const Slot* FindN(const uint32_t k[4]) const
    {
        for (int i = 0; i < kMax; ++i)
            if (n_[i].used && SameKey(n_[i].key, k)) return &n_[i];
        return nullptr;
    }

    // An ACK is never evicted by a refusal; the two tables are separate so a burst of
    // asks cannot forget the one key that was confirmed.
    Slot* TouchA(const uint32_t k[4])
    {
        if (Slot* s = FindA(k)) return s;
        for (int i = 0; i < kMax; ++i) {
            if (a_[i].used) continue;
            a_[i].used = true; a_[i].acked = false;
            for (int j = 0; j < 4; ++j) a_[i].key[j] = k[j];
            return &a_[i];
        }
        return nullptr;
    }
    Slot* TouchN(const uint32_t k[4], uint32_t now)
    {
        if (Slot* s = FindN(k)) return s;
        for (int i = 0; i < kMax; ++i) {
            if (n_[i].used) continue;
            n_[i].used = true; n_[i].refusedAt = now;
            for (int j = 0; j < 4; ++j) n_[i].key[j] = k[j];
            return &n_[i];
        }
        // Full: reuse the oldest refusal.  An ACK table entry is never a victim here.
        int victim = -1; uint32_t oldest = 0;
        for (int i = 0; i < kMax; ++i) {
            uint32_t age = (uint32_t)(now - n_[i].refusedAt);
            if (age >= oldest) { oldest = age; victim = i; }
        }
        if (victim < 0) return nullptr;
        n_[victim].used = true; n_[victim].refusedAt = now;
        for (int j = 0; j < 4; ++j) n_[victim].key[j] = k[j];
        return &n_[victim];
    }

    Slot     a_[kMax];
    Slot     n_[kMax];
};

} // namespace boss
} // namespace qd
