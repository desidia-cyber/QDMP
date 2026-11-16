// qdworldset.h -- the save-side OBJECT SETS that decide where the pair may travel, and
// the two rules that keep a per-player purchase and a shared count honest.
//
// THE GAP THIS CLOSES (COVERAGE-MATRIX GAP 3, 7, 8; CAMPAIGN-ROUTE Part A)
// -----------------------------------------------------------------------
// `FS_SaveSlot_C` carries four `Array<Object>` sets that are the fast-travel and rest
// network, and until now **only the counters crossed** (`TeleporterUnlockCount`,
// `CheckpointUnlockCount` ride the flag bank):
//
//   UnlockedTeleporters       Array<Object>  the fast-travel destinations
//   SavePointsVisited         Array<Object>  the rest points the save may return to
//   ShopTeleporterDestinations Array<Object> the shop mini-teleport pads
//   LastTouchedCheckpoint     Object         **per player -- NOT mirrored**
//
// So a destination one player unlocked did not exist in the other player's menu, and the
// pair had to walk everywhere together. `cha_Challenge_AllTeleporters` needs 10 teleporters
// AND 32 checkpoints **in both banks**, so it was unreachable for the pair as well.
//
// THE RULE
// --------
// **The unlock sets are a UNION, the respawn point is not.** A teleporter or rest point
// that either player reached is unlocked for both; whose checkpoint a LOAD restores stays
// that player's own, which is why `LastTouchedCheckpoint` (and the save identity fields
// beside it) are excluded by name and never appear on the wire. The death/respawn lane
// (`qdrespawn.h`) already keeps each player's own rest point; mirroring the *set* must not
// move it.
//
// Identity is the asset's own short name (`GR_Camp_Save`, `Teleporter_400_AUT`, ...).
// These entries are data assets, not placed actors: the same cooked asset exists on both
// machines under the same name, exactly like the item objects the inventory lane keys by
// path. A name is refused unless it is printable and slash-free, so a foreign or corrupt
// body cannot become an array element.
//
// WHAT ELSE LIVES HERE, AND WHY IN THIS HEADER
// --------------------------------------------
// The same save holds the shop's stock keys and the world's completion counters, and both
// have the identical failure shape: one machine's own act must not silently rewrite the
// other's. They are three lines of rule each, they are consulted by the flag lane, and
// they are tested beside the set union:
//
//   * `IsLocalShopStock`  -- a purchase is a per-player act ("loot/inventory local"):
//     `ShopPurchasedKey/*`, `ShopPurchases`, `ShopSales`, `NewItemsInShop` never cross, so
//     buying the last unique item does not empty the partner's shelf. Shop *unlocks*
//     (`ShopUnlockFinished`, `ShopkeepUnlock1..4`, `ShopProgressionKey/*`) still cross:
//     they are the pair's shared shop progression, and `ShopAmountSpent` still crosses
//     because `cha_Quest_Shop_Spend` is a shared goal.
//
//   * `IsUnionCounter`    -- `TotalChestsOpened` and `TotalSecretDoorsOpened` count DISTINCT
//     world events, and a distinct event happens on exactly one machine (a chest the
//     partner opened is applied on this machine through the object/actor lane WITHOUT its
//     payout, by design). A monotone "never lower" mirror therefore converges to
//     `max(a, b)`, which is wrong the moment both players open different chests:
//     `cha_Challenge_AllChests` = 108 needs the SUM. These keys are synced as deltas and
//     added on the host.
//
// Header-only and dependency-free: no socket, no UObject, no clock read. The board, the
// wire encoding, the exclusion lists and the counter arithmetic are all exercised by
// `test_gapworld.cpp` with no game.
#pragma once
#include <stdint.h>
#include <string.h>

#include "qdwire.h"

namespace qd {
namespace worldset {

// ---------------------------------------------------------------------------
// The sets
// ---------------------------------------------------------------------------
// SET_* is a WIRE VALUE (one byte in the header) and must not be reordered without a
// version bump. SET_COUNT is not a set: it is the bound the code uses.
enum SetId : uint8_t {
    SET_NONE        = 0,
    SET_TELEPORTERS = 1,   // UnlockedTeleporters
    SET_SAVEPOINTS  = 2,   // SavePointsVisited
    SET_SHOP_TP     = 3,   // ShopTeleporterDestinations
    SET_COUNT       = 4
};

inline const char* SetName(uint8_t set)
{
    switch (set) {
    case SET_TELEPORTERS: return "teleporters";
    case SET_SAVEPOINTS:  return "savepoints";
    case SET_SHOP_TP:     return "shop-teleporters";
    default:              return "?";
    }
}

// The property each set names on `FS_SaveSlot_C`.  These are the save's own member names,
// measured from the cooked class (`analysis/pak/blueprint-surface.md`, FS_SaveSlot) and
// from the decoded save (`analysis/04-sync-surface.md` §2.4: `SavePointsVisited`
// `Array<Object>`, `LastTouchedCheckpoint` `Object`).
inline const char* SetProperty(uint8_t set)
{
    switch (set) {
    case SET_TELEPORTERS: return "UnlockedTeleporters";
    case SET_SAVEPOINTS:  return "SavePointsVisited";
    case SET_SHOP_TP:     return "ShopTeleporterDestinations";
    default:              return "";
    }
}

inline bool SetValid(uint8_t set) { return set >= SET_TELEPORTERS && set < SET_COUNT; }

// ---------------------------------------------------------------------------
// What must NEVER be mirrored, stated as a rule rather than left to a caller
// ---------------------------------------------------------------------------
// These are the save's per-player and per-machine fields.  They are named here so the
// engine half has ONE list to consult and a new caller cannot quietly mirror a respawn
// point: `LastTouchedCheckpoint` is what a load restores, and item 1 of the campaign brief
// is explicit that "each player's own respawn point stays their own".
inline bool IsPerPlayerProperty(const char* prop)
{
    if (!prop) return false;
    // Read the exact names once; a prefix test would catch a future `LastTouchedCheckpointX`
    // and hide a real field.
    static const char* const perPlayer[] = {
        "LastTouchedCheckpoint", "LastCheckpoint", "SavePointID", "SaveLocation",
        "SaveLevel", "SavedArea", "CurrentPlayerHealth",
    };
    for (unsigned i = 0; i < sizeof(perPlayer) / sizeof(perPlayer[0]); ++i) {
        const char* a = prop;
        const char* b = perPlayer[i];
        int k = 0;
        for (; a[k] && b[k]; ++k) if (a[k] != b[k]) break;
        if (!a[k] && !b[k]) return true;
    }
    return false;
}

// A set name is an asset's short name: printable ASCII, no separator, bounded.  The wire
// carries it as a length-prefixed string, so a longer name is refused rather than cut --
// a cut name resolves to a different asset.
static const int kKeyCap = 64;

inline bool PlausibleKey(const char* s, int* lenOut = nullptr)
{
    if (!s || !s[0]) return false;
    int n = 0;
    for (; s[n]; ++n) {
        unsigned char c = (unsigned char)s[n];
        if (c < 0x21 || c > 0x7e) return false;   // no space, no control, no high byte
        if (c == '/' || c == '\\' || c == '.' || c == ':') return false;  // not a path
        if (n >= kKeyCap - 1) return false;
    }
    if (lenOut) *lenOut = n;
    return n > 0;
}

inline bool KeyEq(const char* a, const char* b)
{
    if (!a || !b) return a == b;
    for (int i = 0; a[i] || b[i]; ++i) if (a[i] != b[i]) return false;
    return true;
}

inline void CopyKey(char* dst, int cap, const char* src)
{
    if (!dst || cap <= 0) return;
    int i = 0;
    if (src) for (; src[i] && i < cap - 1; ++i) dst[i] = src[i];
    dst[i] = 0;
}

// ---------------------------------------------------------------------------
// Union -- one monotone set per save array
// ---------------------------------------------------------------------------
// Monotone by construction: a key is added, never removed, because an unlock is a fact the
// game never takes back and a removal would have to be a decision nobody can make from one
// machine.  The bound is above the measured universe with headroom (10 teleporters, 31
// checkpoints, 3 shop pads in the shipping world; the pak carries 43 teleporter placements
// and 34 checkpoint placements including the dev/UI maps) and an overflow is REPORTED, not
// silently dropped.
static const int kMaxEntries = 128;

class Union {
public:
    Union() { Reset(); }

    void Reset() { n_ = 0; overflow_ = false; for (int i = 0; i < kMaxEntries; ++i) k_[i][0] = 0; }

    // Returns true when the key was NOT already present (i.e. this call added it).
    bool Merge(const char* key)
    {
        if (!PlausibleKey(key)) return false;
        if (Has(key)) return false;
        if (n_ >= kMaxEntries) { overflow_ = true; return false; }
        CopyKey(k_[n_], kKeyCap, key);
        ++n_;
        return true;
    }

    bool Has(const char* key) const
    {
        if (!key) return false;
        for (int i = 0; i < n_; ++i) if (KeyEq(k_[i], key)) return true;
        return false;
    }

    int  Count() const { return n_; }
    bool Overflowed() const { return overflow_; }
    const char* At(int i) const { return (i >= 0 && i < n_) ? k_[i] : nullptr; }

    // Lexicographic order, so the wire encoding of one set is deterministic and two
    // machines' diffs line up.  Insertion order would make a join's body depend on which
    // key the local game happened to add first.
    const char* SortedAt(int i) const
    {
        if (i < 0 || i >= n_) return nullptr;
        int want = i;
        for (int a = 0; a < n_; ++a) {
            int rank = 0;
            for (int b = 0; b < n_; ++b) {
                if (b == a) continue;
                if (KeyLess(k_[b], k_[a])) ++rank;
                else if (KeyEq(k_[b], k_[a]) && b < a) ++rank;
            }
            if (rank == want) return k_[a];
        }
        return nullptr;
    }

private:
    static bool KeyLess(const char* a, const char* b)
    {
        for (int i = 0;; ++i) {
            unsigned char x = (unsigned char)a[i], y = (unsigned char)b[i];
            if (x == y) { if (x == 0) return false; continue; }
            return x < y;
        }
    }

    char k_[kMaxEntries][kKeyCap];
    int  n_;
    bool overflow_;
};

// ---------------------------------------------------------------------------
// The wire (K_WORLD_SET 0x60)
// ---------------------------------------------------------------------------
// Body: set(u8), rev(u32), n(u16 = the sender's container size), then rows of
// str(key).  Split ACROSS TICKS like every other container here (WIRE.md 3.6: a
// continuation is a CURSOR, never a fragment) -- a set that does not fit one datagram
// continues next tick, and the receiver merges whatever arrives.
//
// `rev` is the sender's own set revision (bumped when the set changes), so a receiver can
// log "the host's set is at revision N" and a stale retransmission is recognisable.
struct SetHeader {
    uint8_t  set;
    uint32_t rev;
    uint16_t n;      // entries on the sender, not in this datagram
};

inline bool WriteSetHeader(wire::Writer& w, uint8_t set, uint32_t rev, uint16_t n)
{
    if (!SetValid(set)) return false;
    w.U8(set);
    w.U32(rev);
    w.U16(n);
    return w.ok;
}

struct SetRow { char key[kKeyCap]; };

// Writes up to the body cap starting at `first`.  Returns the index of the first row NOT
// written (== n when the set is finished), so the caller keeps the cursor.  A `first` past
// the end CLAMPS to the end rather than being echoed back: the contract is "the first row
// not written", and a caller that restarts its cursor relies on that number.
inline int WriteSetChunk(wire::Writer& w, const Union& set, int first)
{
    int i = first < 0 ? 0 : first;
    if (i > set.Count()) i = set.Count();
    for (; i < set.Count(); ++i) {
        const char* k = set.SortedAt(i);
        if (!k) break;
        int len = 0;
        if (!PlausibleKey(k, &len)) break;
        if (!w.Room(2 + len)) break;
        w.Str(k);
    }
    return i;
}

inline bool ReadSetHeader(wire::Reader& r, SetHeader* h)
{
    memset(h, 0, sizeof(*h));
    h->set = r.U8();
    h->rev = r.U32();
    h->n = r.U16();
    if (!r.ok) return false;
    return SetValid(h->set) && h->n <= (uint16_t)kMaxEntries;
}

// Reads rows until the body ends (or `max` rows).  Returns the number read; each accepted
// row has passed `PlausibleKey`, so a malformed entry ends the walk rather than becoming a
// set member.
inline int ReadSetChunk(wire::Reader& r, SetRow* out, int max)
{
    int n = 0;
    while (n < max && r.ok && r.pos < r.len) {
        int got = r.Str(out[n].key, kKeyCap);
        if (got < 0) return n;                    // truncated / over-long
        if (!r.ok) return n;
        if (!PlausibleKey(out[n].key)) return n;  // the rest cannot be trusted
        ++n;
    }
    return n;
}

// ---------------------------------------------------------------------------
// UNION COUNTERS (item 5) -- keys whose truth is the SUM, not the MAX
// ---------------------------------------------------------------------------
// `TotalChestsOpened` / `TotalSecretDoorsOpened` / `ShopAmountSpent` count distinct acts
// that happen on ONE machine each.  The chest the partner opened is applied here by the
// object lane WITHOUT its payout (`qdobjectstate.h` says so explicitly: "the global
// counters are NOT touched here"), so it never increments locally.  A monotone-absolute
// mirror converges to max(a, b) and loses the other player's acts.  These keys therefore
// travel as DELTAS and the host ADDS them.
//
// Deliberately NOT in this list, and each for a measured reason:
//   * `TeleporterUnlockCount` / `CheckpointUnlockCount` are derived from the mirrored SETS
//     (their size is the exact answer, and two players unlocking the SAME destination must
//     count once);
//   * `TotalAchievementWeapons`/`...Treasures`/`...Throwables`/`...Accessories` count the
//     LOCAL collection, and inventories are per-player by design, so a sum would double
//     count what both players hold.
inline bool IsUnionCounter(const char* name)
{
    if (!name) return false;
    static const char* const keys[] = {
        "TotalChestsOpened", "TotalSecretDoorsOpened", "ShopAmountSpent",
    };
    for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) if (KeyEq(name, keys[i])) return true;
    return false;
}

// The delta to send: never negative (a lowered local value is the game resetting its own
// counter, which is not a rise to contribute).  Returning 0 means "nothing to add".
inline int32_t CounterDelta(int32_t localNow, int32_t lastReported)
{
    if (localNow <= lastReported) return 0;
    return localNow - lastReported;
}

// What the receiver writes when it accepts a delta.  Bounded so a corrupt body cannot
// overflow the bank.
inline int32_t CounterNewValue(int32_t have, int32_t delta)
{
    if (delta <= 0) return have;
    if (have > 100000000 - delta) return 100000000;
    return have + delta;
}

// ---------------------------------------------------------------------------
// SHOP STOCK (item 4) -- what stays on the machine that bought it
// ---------------------------------------------------------------------------
// "A purchase is local (inventory local by design)": each player buys for themselves with
// their own coins.  What must NOT travel is the STOCK record, or buying the last unique
// relic removes it from the partner's shelf before the partner ever saw it.
//
// The prefix test is deliberate here (unlike `IsPerPlayerProperty`): the stock keys are
// COMPOSED (`<instance><Property>`, the same composition `qdflags.h` documents for the
// whole bank), so the instance prefix is not knowable and the property suffix is the
// stable part.  The names come from `analysis/23-quest-progression-sync-map.md` §4 and
// `analysis/24-quest-closeout.md` §4b.
inline bool HasSuffix(const char* s, const char* suffix)
{
    if (!s || !suffix) return false;
    int n = 0, m = 0;
    for (; s[n]; ++n) {}
    for (; suffix[m]; ++m) {}
    if (m > n) return false;
    for (int i = 0; i < m; ++i) if (s[n - m + i] != suffix[i]) return false;
    return true;
}

inline bool IsLocalShopStock(const char* name)
{
    if (!name || !name[0]) return false;
    static const char* const suffixes[] = {
        "ShopPurchasedKey",   // the stock row a purchase consumes
        "ShopPurchases",      // the purchase log the shop menu reads
        "ShopSales",          // the sell-side log (same argument)
        "NewItemsInShop",     // the "!" badge, per-player UI
    };
    for (unsigned i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); ++i)
        if (HasSuffix(name, suffixes[i])) return true;
    return false;
}

// A flag that must not cross at all, from either side of the lane.  One predicate so the
// push side, the apply side and the intent side cannot disagree about the same key.
inline bool FlagStaysLocal(const char* name) { return IsLocalShopStock(name); }

} // namespace worldset
} // namespace qd
