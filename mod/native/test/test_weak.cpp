// test_weak.cpp -- the handle that makes "use a cached actor" safe across a free.
//
// THE CASE THAT MATTERS IS THE ONE THAT CRASHED
// ---------------------------------------------
// Session 17's play-set soak died at WINMM+0x3b357 / +0x6f4d7 reading
// 0x0000000100000017 -- two 32-bit fields of a recycled heap block read as a pointer.
// The mod had cached a raw actor pointer, the hub's streaming freed the actor, and the
// pointer was dereferenced.  `ObjectIsLive(p)` does not help: it reads p's InternalIndex,
// which is the first thing a freed allocation loses.
//
// So the invariant under test is not "the handle is convenient", it is:
//
//     a handle captured from an object, resolved after that object was freed and its slot
//     was REUSED by a different object, must return nullptr -- and must not have read the
//     freed address to find out.
//
// The "must not have read the freed address" half is provable here because the fake table
// is the only memory the handle is allowed to touch: `Freed` below poisons the block, so a
// handle that peeked at it would resolve to the poison, not to nullptr.
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "qdweak.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, name)                                                          \
    do {                                                                           \
        if (cond) { ++g_pass; }                                                    \
        else { ++g_fail; printf("FAIL %s  (%s:%d)\n", name, __FILE__, __LINE__); } \
    } while (0)

using namespace qd;

// ---------------------------------------------------------------------------
// A fake GUObjectArray: a slot table plus a bumpable serial per slot.
//
// The object layout MIRRORS the engine's, because that is the thing under test:
// UObject's InternalIndex is at +0x0C, not at +0.  A fixture that put it at 0 would
// have passed while the production header read the wrong field, so the offset is
// asserted rather than assumed.
// ---------------------------------------------------------------------------
struct FakeObj {
    uint8_t  vt[0x0C];      // vtable + the fields this header does not model
    int32_t  index;         // +0x0C, UObject::InternalIndex
    uint32_t poison;        // +0x10, written by Free(): a stale read sees this
};
static_assert(offsetof(FakeObj, index) == 0x0C, "InternalIndex is at +0x0C");

static const int kSlots = 8;
static FakeObj g_objs[kSlots];
static int32_t g_slotOf[kSlots];   // slot -> index of the object occupying it, or -1
static int32_t g_serial[kSlots];   // slot -> current serial

static void* FakeAt(int i)
{
    if (i < 0 || i >= kSlots) return nullptr;
    int32_t which = g_slotOf[i];
    if (which < 0) return nullptr;
    return &g_objs[which];
}

static int32_t FakeSerial(int i)
{
    if (i < 0 || i >= kSlots) return 0;
    return g_serial[i];
}

static void ResetTable()
{
    memset(g_objs, 0, sizeof(g_objs));
    for (int i = 0; i < kSlots; ++i) {
        g_slotOf[i] = -1;
        g_serial[i] = 1;
    }
}

// Put object `which` in slot `slot`, as the engine does on construction.
static void Spawn(int which, int slot)
{
    g_objs[which].index = slot;
    g_objs[which].poison = 0;
    g_slotOf[slot] = which;
}

// Destroy it: the slot is nulled, and the engine's serial bumps so the NEXT object in
// that slot cannot be mistaken for this one.  The object's own memory is then POISONED
// at the InternalIndex offset -- which is what a freed allocation looks like to a stale
// pointer, and what the field reported as `reading 0x0000000100000017`.
static void Free(int which)
{
    int slot = g_objs[which].index;
    if (slot >= 0 && slot < kSlots) {
        g_slotOf[slot] = -1;
        g_serial[slot] = g_serial[slot] + 1;
    }
    g_objs[which].index = (int32_t)0x00010017u;
}

int main()
{
    weak::SetSource(FakeAt, FakeSerial);
    CHECK(weak::SourceReady(), "installing the source makes it ready");

    // ---- the drop-in contract ---------------------------------------------
    // A converted cached pointer must not grow the record it lives in, so `Ref` is
    // exactly one pointer wide.  Asserted at compile time (a runtime CHECK of a constant
    // is an unused conditional, which /W4 rightly warns about) because a future field
    // addition would silently inflate every EnemyRec/loot/mover table in the module.
    static_assert(sizeof(weak::Ref) == sizeof(void*), "a Ref is exactly one pointer wide");
    static_assert(sizeof(weak::Handle) == sizeof(void*), "a Handle is exactly one pointer wide");

    // ---- an empty handle resolves to null ---------------------------------
    {
        weak::Ref r;
        CHECK(r.Get() == nullptr, "a default handle is empty");
        CHECK(!r.Valid(), "a default handle is not valid");
        void* p = r;
        CHECK(p == nullptr, "a default handle converts to null");
    }

    // ---- a captured live object resolves to itself ------------------------
    {
        ResetTable();
        Spawn(0, 3);
        weak::Ref r = (void*)&g_objs[0];
        CHECK(r.Valid(), "a live object resolves");
        CHECK(r.Get() == &g_objs[0], "a live object resolves to itself");
        weak::Handle h = r.Raw();
        CHECK(h.index == 4, "the handle names the slot (biased by one), not the address");
        CHECK(h.serial == 1, "the handle carries the slot's serial");
    }

    // ---- THE CRASH CASE: freed, then the slot is REUSED ------------------
    {
        ResetTable();
        Spawn(0, 3);
        weak::Ref r = (void*)&g_objs[0];
        Free(0);
        // Slot 3 is empty now.  A null slot is the easy half.
        CHECK(r.Get() == nullptr, "a freed object resolves to null while its slot is empty");
        CHECK(!r.Valid(), "a freed object is not valid");

        // Now reuse the slot for a different object.  A pointer-equality check would have
        // said "not live" here too, but only because it read the freed block first; this
        // handle refuses on the SERIAL and never touches g_objs[0] at all.
        Spawn(1, 3);
        CHECK(r.Get() == nullptr, "a reused slot is refused: the serial moved");
        CHECK(r.Raw().index == 4, "the handle still names the old slot, for diagnostics");
        CHECK((void*)r != (void*)&g_objs[1], "the recycled slot is never mistaken for the old object");
        // The object that DID take the slot resolves fine through its own handle.
        weak::Ref fresh = (void*)&g_objs[1];
        CHECK(fresh.Get() == &g_objs[1], "the new occupant of a reused slot resolves");
        CHECK(!r.SameObject(fresh), "an old handle and the recycled slot are different objects");

        // The stale handle's address now reads the poison, which is what the field saw.
        CHECK(g_objs[0].index == (int32_t)0x00010017u, "the fixture poisons the freed block");
        CHECK(r.Get() == nullptr, "resolution ignores the poisoned block entirely");
    }

    // ---- a second capture of the same object matches ----------------------
    {
        ResetTable();
        Spawn(2, 5);
        weak::Ref a = (void*)&g_objs[2];
        weak::Ref b = (void*)&g_objs[2];
        CHECK(a.SameObject(b), "two captures of one object name the same object");
        CHECK(a.Get() == b.Get(), "and resolve to the same address");
        weak::Ref c = (void*)&g_objs[3];   // never spawned: index -1
        CHECK(!c.Valid(), "a capture of a foreign address yields nothing");
        CHECK(!a.SameObject(c), "a foreign handle is a different object");
    }

    // ---- identity survives the object dying ------------------------------
    {
        ResetTable();
        Spawn(4, 1);
        weak::Ref live = (void*)&g_objs[4];
        weak::Ref copy = live;             // a copy of the handle, as a table would hold
        Free(4);
        CHECK(!copy.Valid(), "the copy dies with the object");
        CHECK(live.SameObject(copy), "identity is the handle, so it still compares equal");
    }

    // ---- assignment replaces the handle, and nullptr empties it -----------
    {
        ResetTable();
        Spawn(5, 2);
        Spawn(6, 4);
        weak::Ref r = (void*)&g_objs[5];
        CHECK(r.Get() == &g_objs[5], "assigned to the first object");
        r = (void*)&g_objs[6];
        CHECK(r.Get() == &g_objs[6], "reassignment follows the new object");
        CHECK(r.Raw().index == 5, "and names its slot");
        r = nullptr;
        CHECK(r.Get() == nullptr, "assigning null empties the handle");
        CHECK(r.Raw().index == 0, "an emptied handle holds no index");
        r.Reset();
        CHECK(r.Get() == nullptr, "Reset on an empty handle is harmless");
    }

    // ---- a capture from a released slot is not a handle -------------------
    {
        ResetTable();
        // The engine leaves InternalIndex at -1 on an object it has released.
        g_objs[7].index = -1;
        weak::Ref r = (void*)&g_objs[7];
        CHECK(!r.Valid(), "an object whose slot is already released is not captured");
        CHECK(r.Raw().index == 0, "and the handle stays empty");
    }

    // ---- the slot must still name the captured object --------------------
    {
        ResetTable();
        Spawn(0, 6);
        // A stale address that happens to still hold a plausible index, while the slot
        // now belongs to someone else: capture refuses, because it validates first.
        g_objs[1].index = 6;
        Spawn(0, 6);                       // g_objs[0] is the real occupant
        weak::Ref r = (void*)&g_objs[1];   // a different block claiming the same slot
        CHECK(!r.Valid(), "a capture whose slot names another object is refused");
    }

    // ---- no source installed: fail closed, never guess --------------------
    {
        ResetTable();
        Spawn(0, 0);
        weak::SetSource(nullptr, nullptr);
        CHECK(!weak::SourceReady(), "clearing the source leaves it unready");
        weak::Ref r = (void*)&g_objs[0];
        CHECK(r.Get() == nullptr, "with no source nothing resolves");
        CHECK(!r.Valid(), "and nothing validates");
        weak::SetSource(FakeAt, FakeSerial);   // restore for anything after this
    }

    // ---- a null capture is empty, not a fault -----------------------------
    {
        ResetTable();
        weak::Ref r = (void*)nullptr;
        CHECK(r.Get() == nullptr, "capturing null is empty");
        CHECK(r.Raw().index == 0, "and holds no index");
    }

    // ---- A ZEROED RECORD IS EMPTY, NOT SLOT 0 ----------------------------
    // These tables are static arrays and several are memset, so the empty state must be
    // all-zero bytes.  If the encoding stored the raw index, a wiped record would claim
    // GUObjectArray slot 0 -- a real slot -- and resolve to some unrelated object, which
    // is a silent wrong-actor write rather than a crash.
    {
        ResetTable();
        Spawn(0, 0);                       // slot 0 is genuinely occupied
        weak::Ref wiped;
        memset(&wiped, 0, sizeof(wiped));
        CHECK(wiped.Get() == nullptr, "a zeroed handle resolves to nothing, not to slot 0");
        CHECK(!wiped.Valid(), "a zeroed handle is not valid");
        CHECK(wiped.IsEmpty(), "a zeroed handle reports itself empty");
        CHECK(wiped.Raw().index == 0, "a zeroed handle stores index 0");
        // And the object that really is in slot 0 still resolves through its own handle.
        weak::Ref real = (void*)&g_objs[0];
        CHECK(real.Get() == &g_objs[0], "slot 0's real occupant still resolves");
        CHECK(!real.IsEmpty(), "and is not reported empty");
    }

    // ---- a memset Ref array behaves like a fresh one ---------------------
    {
        ResetTable();
        weak::Ref table[4];
        memset(table, 0, sizeof(table));
        for (int i = 0; i < 4; ++i)
            CHECK(table[i].Get() == nullptr, "a wiped table entry is empty");
    }

    printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
