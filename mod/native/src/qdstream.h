// qdstream.h -- the level-streaming union.
//
// THE PROBLEM, MEASURED
// ---------------------
// Queen's Domain is ONE persistent level (`Ruins_Level_00`) with ~40 streamed
// sublevels, driven by trigger volumes that push level lists onto a stack
// (`AFS_LevelManager`, `LevelListStack`, `AddLevelListToStack`).  With two players the
// loaded set must be the UNION of what is around both of them, and a level must not
// unload because the *other* player walked out of it.
//
// This was measured to fail in a specific way in the previous project: a naive
// add-then-flush replaced a 21-level set with 11.  So the rule here is stronger than
// "compute a union" -- the wanted set must never shrink while both source lists are
// unchanged, and the caller must be able to distinguish "the peer's list is stale"
// from "the peer's list is empty".  An empty peer list and an absent peer list look
// identical if you only compare sets, and treating them the same is how a partner
// standing in a level gets that level unloaded underneath them.
//
// Also from the corpus: SHOW, not just load.  A loaded-but-hidden level's actors are
// absent -- measured with the shop merchant, which is why the shop looked broken.
#pragma once
#include <stdint.h>

namespace qd {
namespace stream {

static const int kMaxLevels  = 64;   // 42 sublevels measured, plus headroom
static const int kMaxNameLen = 64;
static const int kSlots      = 3;    // local + two peer slots

// A set of level names.  Names, not indices: each machine's list order is its own,
// and a shared numeric index would have to be negotiated -- one more thing that can
// disagree.  The wire format solves the same problem the same way (WIRE.md 3.12).
struct LevelSet {
    uint16_t    n;
    const char* name[kMaxLevels];
};

// A slot's list, with the timestamp that lets staleness be distinguished from
// emptiness.
struct SlotList {
    bool     present;
    uint32_t updated_ms;
    int      n;
    char     name[kMaxLevels][kMaxNameLen];
};

class UnionGuard {
public:
    static const uint32_t kPeerStaleMs = 30000;   // 30 s: "gone", not "standing still"

    UnionGuard() { Reset(); }

    void Reset()
    {
        for (int s = 0; s < kSlots; ++s) ClearSlot(&slot_[s]);
        overflow_ = false;
        for (int i = 0; i < kMaxLevels; ++i) seen_[i] = 0;
        seenN_ = 0;
    }

    // ---- inputs -----------------------------------------------------------
    // Names are copied; the caller's storage may go away after the call.
    void OnLocalList(const char* const* names, int n, uint32_t now_ms)
    {
        FillSlot(&slot_[0], names, n, now_ms);
    }

    void OnPeerList(uint8_t slot, const char* const* names, int n, uint32_t now_ms)
    {
        int s = 1 + (int)slot;
        if (s < 1 || s >= kSlots) return;
        FillSlot(&slot_[s], names, n, now_ms);
    }

    void OnPeerGone(uint8_t slot)
    {
        int s = 1 + (int)slot;
        if (s < 1 || s >= kSlots) return;
        ClearSlot(&slot_[s]);
    }

    // ---- outputs ----------------------------------------------------------
    // The union of every live list.  A stale peer list is EXCLUDED (that player is
    // gone) -- excluded by staleness alone, never by being empty, so a partner who is
    // merely standing still keeps their levels.
    LevelSet Wanted(uint32_t now_ms) const
    {
        LevelSet out;
        out.n = 0;
        for (int i = 0; i < kMaxLevels; ++i) out.name[i] = nullptr;
        for (int s = 0; s < kSlots; ++s) {
            const SlotList& l = slot_[s];
            if (!l.present) continue;
            if (s != 0 && IsStale(l, now_ms)) continue;
            for (int i = 0; i < l.n; ++i) {
                if (Contains(out, l.name[i])) continue;
                if (out.n >= kMaxLevels) break;
                out.name[out.n++] = l.name[i];
            }
        }
        return out;
    }

    bool ShouldShow(const char* name, uint32_t now_ms) const
    {
        for (int s = 0; s < kSlots; ++s) {
            const SlotList& l = slot_[s];
            if (!l.present) continue;
            if (s != 0 && IsStale(l, now_ms)) continue;
            for (int i = 0; i < l.n; ++i) {
                if (SameName(l.name[i], name)) return true;
            }
        }
        return false;
    }

    // Wanted by a live PEER list (the local slot excluded).  This is the question the
    // visibility veto asks: "may this level be hidden?"  The local slot is the game's
    // own wish and the game is allowed to act on it; only the partner's need overrides.
    bool PeerWants(const char* name, uint32_t now_ms) const
    {
        for (int s = 1; s < kSlots; ++s) {
            const SlotList& l = slot_[s];
            if (!l.present || IsStale(l, now_ms)) continue;
            for (int i = 0; i < l.n; ++i) {
                if (SameName(l.name[i], name)) return true;
            }
        }
        return false;
    }

    bool AnyPeerLive(uint32_t now_ms) const
    {
        for (int s = 1; s < kSlots; ++s) {
            if (slot_[s].present && !IsStale(slot_[s], now_ms)) return true;
        }
        return false;
    }

    bool PeerListStale(uint32_t now_ms) const
    {
        for (int s = 1; s < kSlots; ++s) {
            if (!slot_[s].present) continue;
            if (IsStale(slot_[s], now_ms)) return true;
        }
        return false;
    }

    bool Overflowed() const { return overflow_; }

    int LocalCount() const { return slot_[0].n; }

    // The add-only property, checked rather than asserted in prose.  Returns the
    // number of names that were wanted last time and are not wanted now, while no
    // input changed.  A nonzero result means the merge dropped a level -- the exact
    // defect the previous project measured as 21 -> 11.
    int Regressions(uint32_t now_ms)
    {
        LevelSet now = Wanted(now_ms);
        int lost = 0;
        for (int i = 0; i < seenN_; ++i) {
            bool found = false;
            for (int j = 0; j < (int)now.n; ++j) {
                if (SameName(now.name[j], prev_[i])) { found = true; break; }
            }
            if (!found) ++lost;
        }
        // Remember this frame's set.
        seenN_ = 0;
        for (int i = 0; i < (int)now.n && seenN_ < kMaxLevels; ++i) {
            int k = 0;
            for (; now.name[i][k] && k < kMaxNameLen - 1; ++k) prev_[seenN_][k] = now.name[i][k];
            prev_[seenN_][k] = 0;
            ++seenN_;
        }
        return lost;
    }

private:
    static void ClearSlot(SlotList* l)
    {
        l->present = false;
        l->updated_ms = 0;
        l->n = 0;
        l->name[0][0] = 0;
    }

    void FillSlot(SlotList* l, const char* const* names, int n, uint32_t now_ms)
    {
        l->present = true;
        l->updated_ms = now_ms;
        l->n = 0;
        for (int i = 0; i < n; ++i) {
            if (!names[i]) continue;
            if (l->n >= kMaxLevels) { overflow_ = true; break; }
            int k = 0;
            for (; names[i][k] && k < kMaxNameLen - 1; ++k) l->name[l->n][k] = names[i][k];
            l->name[l->n][k] = 0;
            ++l->n;
        }
    }

    static bool IsStale(const SlotList& l, uint32_t now_ms)
    {
        if (!l.present) return true;
        uint32_t age = (now_ms > l.updated_ms) ? (now_ms - l.updated_ms) : 0;
        return age > kPeerStaleMs;
    }

    static bool SameName(const char* a, const char* b)
    {
        for (int i = 0; a[i] || b[i]; ++i) if (a[i] != b[i]) return false;
        return true;
    }

    static bool Contains(const LevelSet& s, const char* name)
    {
        for (int i = 0; i < (int)s.n; ++i) {
            if (s.name[i] && SameName(s.name[i], name)) return true;
        }
        return false;
    }

    SlotList slot_[kSlots];
    bool     overflow_ = false;
    uint8_t  seen_[kMaxLevels];
    int      seenN_ = 0;
    char     prev_[kMaxLevels][kMaxNameLen];
};

// ---------------------------------------------------------------------------
// ForceBook -- which levels this machine is holding visible FOR THE PARTNER.
// ---------------------------------------------------------------------------
// Two rules, both from the failure being avoided:
//
//   * A forced level is NEVER reported as the local player's own wish.  If it were,
//     each machine would publish the other's levels back and the union could never
//     shrink again (the two lists would hold each other up forever).
//   * Release is delayed (kReleaseMs).  A partner walking along a level boundary
//     flips its list in and out; hiding on every flip is exactly the "level removed,
//     every enemy in it loses its brain" churn the union exists to prevent.
class ForceBook {
public:
    static const uint32_t kReleaseMs = 5000;
    static const int      kMax = kMaxLevels * 2;

    ForceBook() { Reset(); }
    void Reset() { for (int i = 0; i < kMax; ++i) e_[i].used = false; }

    // The partner needs `name` now.  Returns true if it was not forced before (the
    // caller must make it visible).
    bool Need(const char* name, uint32_t now_ms)
    {
        Entry* e = Find(name);
        if (e) { e->last_need_ms = now_ms; return false; }
        e = Alloc(name);
        if (!e) return false;               // full: reported by Full(), never silent
        e->last_need_ms = now_ms;
        return true;
    }

    bool IsForced(const char* name) const { return FindC(name) != nullptr; }

    // Called for every forced level each pump.  Returns true when the caller should
    // hide it now: not needed by the partner for kReleaseMs AND not wanted by the
    // local game.  A level the local player walked into simply stops being "forced":
    // the game owns it again and will hide it itself when it is done with it.
    bool ShouldRelease(const char* name, bool peerWants, bool localWants, uint32_t now_ms)
    {
        Entry* e = Find(name);
        if (!e) return false;
        if (peerWants) { e->last_need_ms = now_ms; return false; }
        if (localWants) { e->used = false; return false; }
        uint32_t idle = (now_ms > e->last_need_ms) ? now_ms - e->last_need_ms : 0;
        if (idle < kReleaseMs) return false;
        e->used = false;
        return true;
    }

    int Count() const { int n = 0; for (int i = 0; i < kMax; ++i) n += e_[i].used ? 1 : 0; return n; }
    bool Full() const { return Count() >= kMax; }

private:
    struct Entry { bool used; uint32_t last_need_ms; char name[kMaxNameLen]; };

    static bool Same(const char* a, const char* b)
    {
        for (int i = 0; a[i] || b[i]; ++i) if (a[i] != b[i]) return false;
        return true;
    }
    Entry* Find(const char* n) { for (int i = 0; i < kMax; ++i) if (e_[i].used && Same(e_[i].name, n)) return &e_[i]; return nullptr; }
    const Entry* FindC(const char* n) const { for (int i = 0; i < kMax; ++i) if (e_[i].used && Same(e_[i].name, n)) return &e_[i]; return nullptr; }
    Entry* Alloc(const char* n)
    {
        for (int i = 0; i < kMax; ++i) {
            if (e_[i].used) continue;
            e_[i].used = true;
            int k = 0;
            for (; n[k] && k < kMaxNameLen - 1; ++k) e_[i].name[k] = n[k];
            e_[i].name[k] = 0;
            return &e_[i];
        }
        return nullptr;
    }

    Entry e_[kMax];
};

// Short, machine-independent level name from a package path:
// "/Game/Maps/Ruins/100_GR.100_GR" -> "100_GR".  Both machines run the same cooked
// world, so the short name is the same on both and needs no negotiation.
inline void ShortLevelName(const char* path, char* out, int cap)
{
    if (!out || cap <= 0) return;
    out[0] = 0;
    if (!path) return;
    int start = 0, end = 0;
    for (int i = 0; path[i]; ++i) { if (path[i] == '/') start = i + 1; end = i + 1; }
    for (int i = start; i < end; ++i) if (path[i] == '.') { end = i; break; }
    int k = 0;
    for (int i = start; i < end && k < cap - 1; ++i) out[k++] = path[i];
    out[k] = 0;
}

} // namespace stream
} // namespace qd
