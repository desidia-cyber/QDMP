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
