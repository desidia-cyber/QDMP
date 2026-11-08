// qdloot.h -- one shared drop set (SYNC-GAPS §4, "Loot pickups").
//
// THE FACTS
// ---------
// `UFS_ItemDropSystem::RollItemDrop` (0x00E392F0) is keyed by `{FName sourceId, int32
// dropIndex}` and caches the result in a transient map on the save object.  There is NO
// player dimension in that key: on one machine, a drop is shared by construction.  Two
// machines therefore roll twice, and the failure is visible: a drop taken on one stays
// on the floor on the other, and a quest key can be doubled.
//
// THE MODEL
// ---------
// The drop's identity is the game's own key: `{source hash, drop index}`.  The set of
// keys and the item each produced is shared.  Two rules make it converge:
//
//   * a TOMBSTONE wins for ever.  Once either player takes a drop, a later body that
//     still lists the item must not resurrect it.  That is one line of code and the
//     whole point of the class.
//   * the body is ABSOLUTE, so a lost datagram is repaired by the next 1 Hz pass.  A
//     delta would need the lost baseline.
//
// The host is authoritative: a peer's roll is a proposal merged into the host's set,
// and the host's published body is the resolution.  The peer still spawns its own drop
// local to the same rule, so the two copies agree on the item before either is picked
// up; when one is taken the tombstone removes it on the other side.
//
// Pure: `test_loot.cpp` drives add / take / resurrect-refusal / overflow.
#pragma once
#include <stdint.h>

namespace qd {
namespace loot {

static const int kMaxDrops   = 96;
static const int kMaxPathLen = 96;     // wire path is 200 bytes; the ledger is narrower

struct Entry {
    bool     used;
    uint32_t source_hash;
    int32_t  drop_index;
    bool     taken;
    char     item_path[kMaxPathLen];
};

// A stable hash of the game's drop source id (an `FName`).  The name is interned from
// the same cooked package on both machines, so the string -- and therefore the hash --
// agrees without negotiating.
inline uint32_t HashSource(const char* name)
{
    uint32_t h = 2166136261u;
    for (int i = 0; name && name[i]; ++i) {
        h ^= (uint8_t)name[i];
        h *= 16777619u;
    }
    return h ? h : 1u;      // 0 is reserved for "no source"
}

// ---------------------------------------------------------------------------
// A derived key for a build whose drop actor carries no {source, item} property
// ---------------------------------------------------------------------------
// Measured (LIVE-TEST-LOG R62): the spawned drop is an `ItemPickup_Base` subclass with
// `IsADrop=1`, but `DropSource`, `Dropper`, `ItemData` and their spellings are all absent.
// The lane still needs one shared identity, so it is derived from what BOTH machines do
// share: the item class (the same cooked package) and the point where the drop appeared.
// The position is quantized so the two copies' small spawn difference does not split the
// key, and `dup` separates identical drops that really do coincide (they are
// interchangeable, so an arbitrary numbering is safe there and nowhere else).
inline int32_t QuantizeCm(float cm, int32_t bucket)
{
    if (bucket <= 0) bucket = 1;
    float q = cm / (float)bucket;
    return (int32_t)(q >= 0.0f ? q + 0.5f : q - 0.5f);
}

// Spell "<class>@qx,qy,qz#dup".  Separators are explicit, so "1,23" and "12,3" cannot
// collide, and the format is CRT-free because this runs on the game thread's drop scan.
inline void DerivedKeyText(const char* classPath, const int32_t q[3], uint16_t dup,
                           char* out, int cap)
{
    if (!out || cap <= 0) { return; }
    int w = 0;
    for (int i = 0; classPath && classPath[i] && w < cap - 40; ++i) out[w++] = classPath[i];
    out[w++] = '@';
    for (int i = 0; i < 3 && w < cap - 16; ++i) {
        if (i) out[w++] = ',';
        int32_t v = q[i];
        if (v < 0) { out[w++] = '-'; v = -v; }
        char d[12];
        int n = 0;
        if (v == 0) d[n++] = '0';
        while (v) { d[n++] = (char)('0' + (v % 10)); v /= 10; }
        while (n) out[w++] = d[--n];
    }
    out[w++] = '#';
    {
        char d[12];
        int n = 0;
        uint32_t u = dup;
        if (u == 0) d[n++] = '0';
        while (u) { d[n++] = (char)('0' + (u % 10)); u /= 10; }
        while (n && w < cap - 1) out[w++] = d[--n];
    }
    out[w < cap ? w : cap - 1] = 0;
}

// Coincident identical drops.  Bounded and recycled like the enemy summons' table.
class DropOrdinals {
public:
    static const int      kMax = 64;
    static const uint32_t kRecycleMs = 120000;

    DropOrdinals() { Reset(); }

    void Reset()
    {
        for (int i = 0; i < kMax; ++i) r_[i].used = false;
        overflow_ = 0;
    }

    uint16_t Claim(uint32_t classHash, const int32_t q[3], uint32_t now_ms)
    {
        Rec* r = Touch(classHash, q, now_ms);
        if (!r) { ++overflow_; return 0; }
        uint16_t v = r->n;
        if (r->n != 0xFFFF) ++r->n;
        return v;
    }

    uint32_t Overflows() const { return overflow_; }

private:
    struct Rec {
        bool     used;
        uint32_t classHash;
        int32_t  q[3];
        uint16_t n;
        uint32_t touchMs;
    };

    Rec* Touch(uint32_t classHash, const int32_t q[3], uint32_t now_ms)
    {
        if (classHash == 0) return nullptr;
        for (int i = 0; i < kMax; ++i) {
            if (!r_[i].used) continue;
            if (r_[i].classHash != classHash) continue;
            if (r_[i].q[0] != q[0] || r_[i].q[1] != q[1] || r_[i].q[2] != q[2]) continue;
            r_[i].touchMs = now_ms;
            return &r_[i];
        }
        int victim = -1;
        uint32_t oldest = 0;
        for (int i = 0; i < kMax; ++i) {
            if (!r_[i].used) { victim = i; break; }
            uint32_t age = (uint32_t)(now_ms - r_[i].touchMs);
            if (age > kRecycleMs && age > oldest) { oldest = age; victim = i; }
        }
        if (victim < 0) return nullptr;
        Rec& r = r_[victim];
        r.used = true;
        r.classHash = classHash;
        for (int k = 0; k < 3; ++k) r.q[k] = q[k];
        r.n = 0;
        r.touchMs = now_ms;
        return &r;
    }

    Rec      r_[kMax];
    uint32_t overflow_;
};

inline bool SameKey(const Entry& e, uint32_t source_hash, int32_t drop_index)
{
    return e.source_hash == source_hash && e.drop_index == drop_index;
}

inline void CopyPath(char* dst, int cap, const char* src)
{
    if (!dst || cap <= 0) return;
    int i = 0;
    if (src) for (; src[i] && i < cap - 1; ++i) dst[i] = src[i];
    dst[i] = 0;
}

class Ledger {
public:
    Ledger() { Reset(); }

    void Reset()
    {
        for (int i = 0; i < kMaxDrops; ++i) {
            e_[i].used = false;
            e_[i].taken = false;
            e_[i].item_path[0] = 0;
        }
        rev_ = 1;
        overflow_ = false;
    }

    // A roll produced an item.  Returns true when it was new.  An entry that already
    // exists is not changed, and a TAKEN entry stays taken: re-observing a roll after
    // the drop was picked up must not bring it back.
    bool Offer(uint32_t source_hash, int32_t drop_index, const char* item_path)
    {
        if (source_hash == 0) return false;
        Entry* e = Find(source_hash, drop_index);
        if (e) return false;
        e = Alloc();
        if (!e) return false;
        e->used = true;
        e->source_hash = source_hash;
        e->drop_index = drop_index;
        e->taken = false;
        CopyPath(e->item_path, kMaxPathLen, item_path);
        Bump();
        return true;
    }

    // A player picked it up.  Returns true when this is the first tombstone for it.
    // A key we never saw still gets a tombstone, because the other machine may not have
    // published its roll yet and the tombstone must be able to lead.
    bool Take(uint32_t source_hash, int32_t drop_index)
    {
        if (source_hash == 0) return false;
        Entry* e = Find(source_hash, drop_index);
        if (!e) {
            e = Alloc();
            if (!e) return false;
            e->used = true;
            e->source_hash = source_hash;
            e->drop_index = drop_index;
            e->taken = false;
            e->item_path[0] = 0;
        }
        if (e->taken) return false;
        e->taken = true;
        Bump();
        return true;
    }

    // Merge an inbound absolute body.  `authoritative` says the sender is the host.
    // Rules: a tombstone always wins; a non-authoritative entry never overrides an
    // authoritative one; an existing path is never replaced by an empty one.
    // Returns the number of entries that changed.
    int Merge(const Entry* in, int n, bool authoritative)
    {
        int changed = 0;
        for (int i = 0; i < n; ++i) {
            const Entry& s = in[i];
            if (!s.used || s.source_hash == 0) continue;
            Entry* e = Find(s.source_hash, s.drop_index);
            if (!e) {
                e = Alloc();
                if (!e) break;
                e->used = true;
                e->source_hash = s.source_hash;
                e->drop_index = s.drop_index;
                e->taken = s.taken;
                CopyPath(e->item_path, kMaxPathLen, s.item_path);
                ++changed;
                continue;
            }
            if (s.taken && !e->taken) { e->taken = true; ++changed; continue; }
            if (e->taken) continue;                       // never resurrect
            if (!authoritative && e->item_path[0]) continue;
            if (s.item_path[0] && !SameText(e->item_path, s.item_path)) {
                CopyPath(e->item_path, kMaxPathLen, s.item_path);
                ++changed;
            }
        }
        if (changed) Bump();
        return changed;
    }

    // ---- queries ----------------------------------------------------------
    bool Contains(uint32_t source_hash, int32_t drop_index) const
    {
        return Find(source_hash, drop_index) != nullptr;
    }
    bool IsTaken(uint32_t source_hash, int32_t drop_index) const
    {
        const Entry* e = Find(source_hash, drop_index);
        return e && e->taken;
    }
    const char* PathOf(uint32_t source_hash, int32_t drop_index) const
    {
        const Entry* e = Find(source_hash, drop_index);
        return e ? e->item_path : "";
    }
    int Count() const
    {
        int n = 0;
        for (int i = 0; i < kMaxDrops; ++i) n += e_[i].used ? 1 : 0;
        return n;
    }
    int TakenCount() const
    {
        int n = 0;
        for (int i = 0; i < kMaxDrops; ++i) n += (e_[i].used && e_[i].taken) ? 1 : 0;
        return n;
    }
    bool Overflowed() const { return overflow_; }
    uint32_t Revision() const { return rev_; }

    // Fill `out` with the whole set (the absolute body).
    int Snapshot(Entry* out, int cap) const
    {
        int n = 0;
        for (int i = 0; i < kMaxDrops && n < cap; ++i) {
            if (e_[i].used) out[n++] = e_[i];
        }
        return n;
    }

    static bool SameText(const char* a, const char* b)
    {
        int i = 0;
        for (; a[i] || b[i]; ++i) if (a[i] != b[i]) return false;
        return true;
    }

private:
    Entry* Find(uint32_t source_hash, int32_t drop_index)
    {
        for (int i = 0; i < kMaxDrops; ++i)
            if (e_[i].used && SameKey(e_[i], source_hash, drop_index)) return &e_[i];
        return nullptr;
    }
    const Entry* Find(uint32_t source_hash, int32_t drop_index) const
    {
        for (int i = 0; i < kMaxDrops; ++i)
            if (e_[i].used && SameKey(e_[i], source_hash, drop_index)) return &e_[i];
        return nullptr;
    }
    Entry* Alloc()
    {
        for (int i = 0; i < kMaxDrops; ++i) {
            if (e_[i].used) continue;
            e_[i].used = true;
            e_[i].taken = false;
            e_[i].item_path[0] = 0;
            return &e_[i];
        }
        overflow_ = true;
        return nullptr;
    }
    void Bump() { rev_ = rev_ == 0xFFFFFFFFu ? 1u : rev_ + 1u; }

    Entry    e_[kMaxDrops];
    uint32_t rev_;
    bool     overflow_;
};

} // namespace loot
} // namespace qd
