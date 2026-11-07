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

} // namespace stream
} // namespace qd
