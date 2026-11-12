// qdflags.h -- the monotonic progression flag bank.
//
// WHAT THIS MIRRORS
// -----------------
// `NamedSaveData` is `Map<FName, int32>` on `FS_SaveSlot_C`.  The operator's own
// 11-minute save holds ELEVEN entries, and every one is a story fact or a counter:
//
//   Arjaot_State=1  RustySwordKnightState=1  RustySwordKnightGaveSword=1
//   BrokeBarricade=1  CheckpointUnlockCount=1  TotalAchievementWeapons=1
//   TotalAchievementTreasures=0  CompletedChallenges=0  IntroMusicState=0
//   FullEnergyCellsCount=0  TiniScytheHammerAP=0
//
// That is the entire progression payload for that slice of the game.  The enumerated
// universe is 58 `cha_*` achievement keys plus stat fields, and keys are COMPOSED
// (`<instance>` + `<property>`, so `RustyBrokeBarricade` = instance prefix +
// `BrokeBarricade`).  Which is why this is a name-indexed bank rather than a struct.
//
// THE THREE RULES, AND WHY EACH IS STRUCTURAL
// -------------------------------------------
// 1. NEVER LOWER.  Progression is monotonic.  A peer write that would regress is
//    refused, and the refusal is visible -- a silent clamp is how a player loses a
//    boss kill they earned.
//
// 2. THE HOST IS THE ONLY WRITER.  `SetLocal` is the host's path and may set any
//    value, including a reset, because the host IS the authority.  `ApplyRemote` and
//    `ApplyIntent` are the peer path and may only raise.
//
// 3. AN INTENT IS ACCEPTED ONLY IF IT IS CURRENT.  `based_on_rev >= last_sent_rev`.
//    This is what makes double-application STRUCTURALLY impossible rather than
//    merely unlikely: a peer that did not see the host's latest state cannot act on
//    its stale view.  It is the previous project's rule E4, kept verbatim.
#pragma once
#include <stdint.h>

namespace qd {
namespace flags {

// A full playthrough's bank, with headroom.  The module's flag mirror and the host's push
// table are sized by this same constant (proxy.cpp kMirrorCap), so the three can never
// disagree about how many keys a save may hold.
static const int kMaxFlags = 1024;
static const int kMaxName  = 64;

class FlagBank {
public:
    FlagBank() { Clear(); }

    void Clear()
    {
        n_ = 0;
        rev_ = 1;
        dirty_ = false;
        for (int i = 0; i < kMaxFlags; ++i) {
            t_[i].used = false;
            t_[i].name[0] = 0;
            t_[i].value = 0;
        }
    }

    int Size() const { return n_; }
    uint32_t Revision() const { return rev_; }
    bool Dirty() const { return dirty_; }
    void ClearDirty() { dirty_ = false; }

    int32_t Get(const char* name, bool* found) const
    {
        int i = Find(name);
        if (i < 0) { if (found) *found = false; return 0; }
        if (found) *found = true;
        return t_[i].value;
    }

    // ---- host path ---------------------------------------------------------
    // `absolute` == true sets the value outright (a reset is legitimate for the
    // authority).  `absolute` == false is additive and can only raise -- used for
    // counters such as CheckpointUnlockCount, where the game's own node is
    // `NSDCM_Add`.
    bool SetLocal(const char* name, int32_t value, bool absolute)
    {
        if (!name) return false;
        int i = Find(name);
        if (i < 0) {
            i = Insert(name);
            if (i < 0) return false;      // full: reported by CapReached()
            t_[i].value = (absolute ? value : value);
            MarkDirty();
            return true;
        }
        int32_t next = absolute ? value : (t_[i].value + value);
        if (!absolute && next < t_[i].value) next = t_[i].value;   // additive never lowers
        if (next == t_[i].value) return false;
        t_[i].value = next;
        MarkDirty();
        return true;
    }

    // ---- peer path ---------------------------------------------------------
    // Applies a value the host broadcast.  Never lowers.  `rev` is recorded so a
    // later intent can be judged against what the peer had actually seen.
    bool ApplyRemote(const char* name, int32_t value, uint32_t rev)
    {
        if (!name) return false;
        if (rev != 0 && rev < rev_) return false;     // stale broadcast
        int i = Find(name);
        if (i < 0) {
            i = Insert(name);
            if (i < 0) return false;
            t_[i].value = value;
            if (rev > rev_) rev_ = rev;
            return true;
        }
        if (value < t_[i].value) return false;        // never lower
        if (value == t_[i].value) {
            if (rev > rev_) rev_ = rev;
            return false;
        }
        t_[i].value = value;
        if (rev > rev_) rev_ = rev;
        return true;
    }

    // ---- the acceptance gate ----------------------------------------------
    // `based_on_rev >= last_sent_rev` is the structural anti-double-apply rule.
    // A stale view cannot commit.
    bool ApplyIntent(const char* name, int32_t value, bool absolute,
                     uint32_t based_on_rev, uint32_t last_sent_rev)
    {
        if (!name) return false;
        if (based_on_rev < last_sent_rev) return false;   // stale intent

        int i = Find(name);
        if (i < 0) {
            // An intent may CREATE a key (the game composes keys per instance), but
            // only with a value that is not a regression from nothing.
            i = Insert(name);
            if (i < 0) return false;
            t_[i].value = value;
            MarkDirty();
            return true;
        }

        int32_t next = absolute ? value : (t_[i].value + value);
        if (!absolute && next < t_[i].value) next = t_[i].value;
        if (next < t_[i].value) return false;             // absolute lowering is refused
        if (next == t_[i].value) return false;
        t_[i].value = next;
        MarkDirty();
        return true;
    }

    // ---- ordered enumeration ----------------------------------------------
    // Sorted by name so the wire encoding is deterministic: an unordered map would
    // make the encoder's output depend on insertion order, which makes a golden
    // vector meaningless and a diff between two machines noisy.
    bool At(int index, const char** name, int32_t* value) const
    {
        if (index < 0 || index >= n_) return false;
        // Selection sort on demand would be O(n^2) per call; instead the table is kept
        // in insertion order and At() walks it in NAME order by counting predecessors.
        // For 512 entries at 1 Hz this is cheap and needs no scratch memory.
        int want = index;
        int seen = 0;
        for (int i = 0; i < n_; ++i) {
            int rank = 0;
            for (int j = 0; j < n_; ++j) {
                if (j == i) continue;
                if (NameLess(t_[j].name, t_[i].name)) ++rank;
                else if (NameEquals(t_[j].name, t_[i].name) && j < i) ++rank;
            }
            if (rank == want) {
                if (name) *name = t_[i].name;
                if (value) *value = t_[i].value;
                ++seen;
                return true;
            }
        }
        (void)seen;
        return false;
    }

    bool CapReached() const { return n_ >= kMaxFlags; }

private:
    struct Entry {
        bool    used;
        char    name[kMaxName];
        int32_t value;
    };

    void MarkDirty() { dirty_ = true; ++rev_; if (rev_ == 0) rev_ = 1; }

    int Find(const char* name) const
    {
        for (int i = 0; i < n_; ++i) {
            if (t_[i].used && NameEquals(t_[i].name, name)) return i;
        }
        return -1;
    }

    int Insert(const char* name)
    {
        if (n_ >= kMaxFlags) return -1;
        Entry& e = t_[n_];
        e.used = true;
        int k = 0;
        for (; name[k] && k < kMaxName - 1; ++k) e.name[k] = name[k];
        e.name[k] = 0;
        e.value = 0;
        return n_++;
    }

    static bool NameEquals(const char* a, const char* b)
    {
        for (int i = 0; a[i] || b[i]; ++i) if (a[i] != b[i]) return false;
        return true;
    }

    static bool NameLess(const char* a, const char* b)
    {
        for (int i = 0;; ++i) {
            if (a[i] == b[i]) {
                if (a[i] == 0) return false;
                continue;
            }
            return (unsigned char)a[i] < (unsigned char)b[i];
        }
    }

    Entry    t_[kMaxFlags];
    int      n_;
    uint32_t rev_;
    bool     dirty_;
};

} // namespace flags
} // namespace qd
