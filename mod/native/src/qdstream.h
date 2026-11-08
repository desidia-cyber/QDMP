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

inline bool HasNamePrefix(const char* s, const char* prefix)
{
    int i = 0;
    for (; prefix[i]; ++i) if (s[i] != prefix[i]) return false;
    return true;
}

// A level name the union may publish.  The refusal is as load-bearing as the
// acceptance: the WorldAsset soft path at +0x10 is unverified, so its read can be
// an empty string, "None", or the ULevelStreaming wrapper's own object name.  Any
// of those published in the union makes the partner protect a level that does not
// exist, and that failure looks exactly like "the level did not stream".  Only the
// characters that occur in cooked package short names are allowed, so a whole path
// that escaped ShortLevelName is refused rather than sent.
inline bool PlausibleLevelName(const char* name)
{
    if (!name || !name[0]) return false;
    if (name[0] == '/' || name[0] == '.') return false;
    if (HasNamePrefix(name, "Default__")) return false;
    if (HasNamePrefix(name, "LevelStreaming")) return false;
    if (HasNamePrefix(name, "None") && !name[4]) return false;
    int n = 0;
    for (; name[n]; ++n) {
        if (n >= kMaxNameLen - 1) return false;
        char c = name[n];
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) return false;
    }
    return n > 0;
}

// ---------------------------------------------------------------------------
// MapBoard -- which world each machine is in (SYNC-GAPS G6).
// ---------------------------------------------------------------------------
// A hard transition (a cutscene map, `900_SipehaWorld`, `990_B33sment`) loads a
// different UWorld.  While the two machines are in different worlds every ABSOLUTE
// body means something different: a level name from the partner's map matches
// nothing here, an enemy row addresses an actor that does not exist here, and a
// dialogue cursor points into another conversation.  Applying any of them is worse
// than dropping it, so the rule is ANNOUNCEMENT PLUS REFUSAL, not travel.
//
// API-GAPS 4 is explicit that hard travel is refused in co-op v1, so this class
// deliberately has no "make the partner follow" path.  It detects the split, the
// caller logs it once, and the application sites ask PeerStateApplies() before
// touching cross-world state.  The session is never torn down: a cutscene can load
// and return, and a transition is not instantaneous.
//
// The debounce is load-bearing.  During an ordinary level load the old world is
// current for a frame or two and the new world's name is already known, so the two
// names disagree briefly on every transition.  kDivergeMs is 3 s: measured streamed
// sublevel loads are under a second, and the cutscene hand-off is a handful of
// frames; three seconds is past both and still far below the point where a player
// would call the session broken.
class MapBoard {
public:
    static const uint32_t kDivergeMs = 3000;

    MapBoard() { Reset(); }

    void Reset()
    {
        local_[0] = 0;
        peer_[0] = 0;
        differing_ = false;
        since_ms_ = 0;
    }

    // Returns true when the name changed.  The caller announces a change at once;
    // the heartbeat covers an announcement that was lost.
    bool OnLocal(const char* map, uint32_t now_ms)
    {
        bool changed = Copy(local_, map);
        if (changed && Differs()) since_ms_ = now_ms;   // a new mismatch restarts the clock
        Recompute(now_ms);
        return changed;
    }

    bool OnPeer(const char* map, uint32_t now_ms)
    {
        bool changed = Copy(peer_, map);
        if (changed && Differs()) since_ms_ = now_ms;
        Recompute(now_ms);
        return changed;
    }

    // True once the two names have disagreed for kDivergeMs.  A difference that
    // clears inside the window was a load, not a split.
    bool Diverged(uint32_t now_ms) const
    {
        if (!differing_ || !Differs()) return false;
        return (uint32_t)(now_ms - since_ms_) >= kDivergeMs;
    }

    // The question every application site asks.  False means the partner's body
    // describes the other world and must be dropped, never "converted".
    bool PeerStateApplies(uint32_t now_ms) const { return !Diverged(now_ms); }

    bool BothKnown() const { return local_[0] != 0 && peer_[0] != 0; }
    bool NamesDiffer() const { return Differs(); }
    const char* Local() const { return local_; }
    const char* Peer() const { return peer_; }

private:
    static bool Same(const char* a, const char* b)
    {
        int i = 0;
        for (; a[i] || b[i]; ++i) if (a[i] != b[i]) return false;
        return true;
    }

    static bool Copy(char* dst, const char* src)
    {
        bool changed = false;
        int i = 0;
        if (!src) src = "";
        for (; src[i] && i < kMaxNameLen - 1; ++i) {
            if (dst[i] != src[i]) changed = true;
            dst[i] = src[i];
        }
        if (dst[i] != 0) changed = true;
        dst[i] = 0;
        return changed;
    }

    bool Differs() const { return local_[0] && peer_[0] && !Same(local_, peer_); }

    void Recompute(uint32_t now_ms)
    {
        if (!Differs()) { differing_ = false; since_ms_ = 0; }
        else if (!differing_) { differing_ = true; since_ms_ = now_ms; }
    }

    char     local_[kMaxNameLen];
    char     peer_[kMaxNameLen];
    bool     differing_;
    uint32_t since_ms_;
};

} // namespace stream
} // namespace qd
