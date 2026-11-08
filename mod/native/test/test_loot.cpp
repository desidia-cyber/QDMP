// test_loot.cpp -- the shared drop set (SYNC-GAPS §4).
//
// The one property that must never break: a tombstone is permanent.  If a body that
// still lists a drop can resurrect it, the item reappears on the other machine after
// it was picked up -- the exact "a drop taken on one stays on the floor on the other"
// failure this lane exists to remove.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "qdloot.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, name)                                                          \
    do {                                                                           \
        if (cond) { ++g_pass; }                                                    \
        else { ++g_fail; printf("FAIL %s  (%s:%d)\n", name, __FILE__, __LINE__); } \
    } while (0)

using namespace qd;
using namespace qd::loot;

static loot::Entry In(uint32_t hash, int32_t index, bool taken, const char* path)
{
    loot::Entry e;
    memset(&e, 0, sizeof(e));
    e.used = true;
    e.source_hash = hash;
    e.drop_index = index;
    e.taken = taken;
    CopyPath(e.item_path, kMaxPathLen, path);
    return e;
}

static void TestOffer()
{
    Ledger l;
    CHECK(l.Offer(123, 0, "/Game/Items/Tools/Lamp.Lamp"), "offer/a new drop is recorded");
    CHECK(!l.Offer(123, 0, "/Game/Items/Tools/Lamp.Lamp"), "offer/the same key is not new");
    CHECK(l.Offer(123, 1, "/Game/Items/Tools/Cup.Cup"), "offer/a different index is a different drop");
    CHECK(l.Count() == 2, "offer/both are in the set");
    CHECK(!l.Offer(0, 0, "x"), "offer/a zero source has no identity and is refused");
}

static void TestTakeIsPermanent()
{
    Ledger l;
    l.Offer(7, 2, "/Game/Items/KeyItems/AquaDoorKey_2.AquaDoorKey_2");
    CHECK(l.Take(7, 2), "take/the drop is tombstoned");
    CHECK(!l.Take(7, 2), "take/a second take is not a change");
    CHECK(l.IsTaken(7, 2), "take/it reads taken");

    // The other machine still lists the item; that must not bring it back.
    CHECK(!l.Offer(7, 2, "/Game/Items/KeyItems/AquaDoorKey_2.AquaDoorKey_2"),
          "take/re-observing the roll is not new");
    CHECK(l.IsTaken(7, 2), "take/the tombstone survives a re-observe");
    loot::Entry in[1] = { In(7, 2, false, "/Game/Items/KeyItems/AquaDoorKey_2.AquaDoorKey_2") };
    l.Merge(in, 1, true);
    CHECK(l.IsTaken(7, 2), "take/and survives a non-taken body");
}

static void TestTakeLeadsTheOffer()
{
    Ledger l;
    // The tombstone can arrive before the roll was ever published here.
    CHECK(l.Take(9, 0), "lead/a take for an unseen key is recorded");
    CHECK(l.IsTaken(9, 0), "lead/it is taken");
    CHECK(!l.Offer(9, 0, "/Game/Items/Tools/Lamp.Lamp"), "lead/the later roll is absorbed");
    CHECK(l.IsTaken(9, 0), "lead/and does not clear the tombstone");
}

static void TestMerge()
{
    Ledger l;
    l.Offer(1, 0, "/Game/Items/A.A");
    loot::Entry in[2] = {
        In(1, 0, false, "/Game/Items/B.B"),   // a path change
        In(2, 0, true, ""),                    // a new tombstone
    };
    int changed = l.Merge(in, 2, true);
    CHECK(changed == 2, "merge/two changes are reported");
    CHECK(Ledger::SameText(l.PathOf(1, 0), "/Game/Items/B.B"), "merge/the authoritative path wins");
    CHECK(l.IsTaken(2, 0), "merge/a new tombstone is applied");

    // A non-authoritative (peer) body may add a missing path but never override an
    // existing one, and a tombstone still wins.
    loot::Entry peer[1] = { In(1, 0, false, "/Game/Items/C.C") };
    CHECK(l.Merge(peer, 1, false) == 0, "merge/a peer does not override the host");
    CHECK(Ledger::SameText(l.PathOf(1, 0), "/Game/Items/B.B"), "merge/the host's path is kept");
    loot::Entry peer2[1] = { In(3, 0, false, "/Game/Items/D.D") };
    CHECK(l.Merge(peer2, 1, false) == 1, "merge/a peer may add a genuinely new drop");
}

static void TestHashAndSnapshot()
{
    CHECK(HashSource("Chest_A") == HashSource("Chest_A"), "hash/the same name is the same hash");
    CHECK(HashSource("Chest_A") != HashSource("Chest_B"), "hash/different names differ");
    CHECK(HashSource("") != 0, "hash/an empty name still has a non-zero hash");

    Ledger l;
    l.Offer(1, 0, "a");
    l.Offer(2, 0, "b");
    l.Take(1, 0);
    loot::Entry out[kMaxDrops];
    int n = l.Snapshot(out, kMaxDrops);
    CHECK(n == 2, "snapshot/the whole set is emitted");
    CHECK(l.TakenCount() == 1, "snapshot/one tombstone");
    CHECK(l.Revision() > 1, "snapshot/a change bumps the revision");
}

static void TestOverflowIsReported()
{
    Ledger l;
    for (int i = 0; i < kMaxDrops + 3; ++i) l.Offer((uint32_t)(i + 1), 0, "p");
    CHECK(l.Overflowed(), "overflow/more drops than the table is reported, not silent");
}

// The build's drop actor exposes no {source, item} property (R62), so the derived key is
// the lane's identity now.  Its whole requirement is that two machines compute the SAME
// string from the same class and spawn point, and that genuinely different drops do not.
static void TestDerivedKey()
{
    CHECK(QuantizeCm(149.0f, 100) == 1, "derived/149 cm rounds to the 1 m bucket");
    CHECK(QuantizeCm(151.0f, 100) == 2, "derived/151 cm rounds to the 2 m bucket");
    CHECK(QuantizeCm(-149.0f, 100) == -1, "derived/negative rounds away from zero");
    CHECK(QuantizeCm(49.0f, 100) == 0, "derived/the origin bucket is zero");

    const int32_t q1[3] = {1, 23, 0};
    const int32_t q2[3] = {12, 3, 0};
    char a[320], b[320], c[320];
    DerivedKeyText("/Game/Items/Potion.Potion_C", q1, 0, a, sizeof(a));
    DerivedKeyText("/Game/Items/Potion.Potion_C", q2, 0, b, sizeof(b));
    CHECK(strcmp(a, b) != 0, "derived/'1,23' and '12,3' cannot collide");
    CHECK(strcmp(a, "/Game/Items/Potion.Potion_C@1,23,0#0") == 0,
          "derived/the spelling is exact");
    DerivedKeyText("/Game/Items/Potion.Potion_C", q1, 0, c, sizeof(c));
    CHECK(strcmp(a, c) == 0, "derived/the same inputs produce the same key on both machines");
    DerivedKeyText("/Game/Items/Potion.Potion_C", q1, 1, c, sizeof(c));
    CHECK(strcmp(a, c) != 0, "derived/a coincidence index separates identical drops");
    DerivedKeyText("/Game/Items/Potion.Potion_C", q1, 0, c, sizeof(c));
    CHECK(HashSource(a) == HashSource(c), "derived/the hash is stable across calls");
    CHECK(HashSource(a) != HashSource("/Game/Items/Other.Other_C@1,23,0#0"),
          "derived/a different class is a different key");
    CHECK(strncmp(a, "/Game/Items/Potion.Potion_C@", 28) == 0,
          "derived/the class path appears verbatim");
}

static void TestDropOrdinals()
{
    DropOrdinals o;
    const int32_t p[3] = {5, -7, 100};
    const int32_t other[3] = {5, -7, 101};
    uint32_t h = HashSource("/Game/Items/Potion.Potion_C");
    CHECK(o.Claim(h, p, 1000) == 0, "ordinals/the first drop at a point is 0");
    CHECK(o.Claim(h, p, 1000) == 1, "ordinals/a coincident second drop is 1");
    CHECK(o.Claim(h, other, 1000) == 0, "ordinals/a different point restarts at 0");
    o.Reset();
    CHECK(o.Claim(h, p, 1000) == 0, "ordinals/a world change resets the table");
}

int main()
{
    printf("=== qd loot tests ===\n");
    TestOffer();
    TestTakeIsPermanent();
    TestTakeLeadsTheOffer();
    TestMerge();
    TestHashAndSnapshot();
    TestOverflowIsReported();
    TestDerivedKey();
    TestDropOrdinals();
    printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
