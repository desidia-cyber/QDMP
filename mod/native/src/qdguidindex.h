// qdguidindex.h -- a hashed index over a fixed record array keyed by a 128-bit enemy GUID.
//
// WHY (Session 29, measured run 36): the ownership table and the row view were linear
// arrays of 256 records, and the castle runs ~600 enemies.  Both machines logged
// "ownership table full" 13 s before the Queen appeared; she got no record, `Tick`
// answered "run it here" on BOTH machines, and the double run that the handshake resolves
// for every recorded enemy was never seen for her -- two Queens, two HP bars.  The tables
// are now 1024 records, and a linear scan of 1024 per enemy per tick is too slow, so the
// lookup goes through this open-addressed index (load factor <= 0.5).
//
// Records are never removed one at a time: a table either RECYCLES a record (its GUID
// changes -- rare, only when full) or is Reset.  Both rebuild the index, so the index never
// needs tombstones.  Header-only and dependency-free (compiled into native/test).
#pragma once
#include <stdint.h>

namespace qd {

template <int kSlots>
class GuidIndex {
public:
    static_assert((kSlots & (kSlots - 1)) == 0, "kSlots must be a power of two");
    GuidIndex() { Clear(); }
    void Clear() { for (int i = 0; i < kSlots; ++i) slot_[i] = -1; }

    static uint32_t Hash(const uint32_t g[4])
    {
        uint32_t h = g[0] * 2654435761u ^ g[1] * 2246822519u ^ g[2] * 3266489917u ^ g[3];
        return h ^ (h >> 15);
    }

    // `guidOf(i)` returns the GUID array of record i.  Returns the record index or -1.
    template <class GuidOf>
    int Find(const uint32_t g[4], GuidOf guidOf) const
    {
        uint32_t h = Hash(g) & (kSlots - 1);
        for (int n = 0; n < kSlots; ++n) {
            int16_t s = slot_[h];
            if (s < 0) return -1;
            const uint32_t* k = guidOf(s);
            if (k[0] == g[0] && k[1] == g[1] && k[2] == g[2] && k[3] == g[3]) return s;
            h = (h + 1) & (kSlots - 1);
        }
        return -1;
    }

    void Insert(const uint32_t g[4], int rec)
    {
        uint32_t h = Hash(g) & (kSlots - 1);
        for (int n = 0; n < kSlots; ++n) {
            if (slot_[h] < 0) { slot_[h] = (int16_t)rec; return; }
            h = (h + 1) & (kSlots - 1);
        }
    }

private:
    int16_t slot_[kSlots];
};

}  // namespace qd
