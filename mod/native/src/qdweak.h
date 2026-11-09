// qdweak.h -- weak handles to UObjects, so a cached pointer can never be dereferenced
// after the engine freed it.
//
// THE BUG THIS EXISTS FOR (Session 17 R132, and the reason for Session 18)
// ----------------------------------------------------------------------
// The mod cached raw `void*`'s to actors and objects in globals and record tables
// (`g_localPawn`, `g_puppetActor`, `EnemyRec::actor`, the loot/mover/cutscene caches,
// ...) and then re-checked them with `ObjectIsLive(p)`, which is:
//
//     ObjectAt(ObjInternalIndex(p)) == p
//
// That test dereferences `p` to read its InternalIndex.  `p` is the thing that may
// already be freed.  So "check liveness, then use" is not a guard at all -- both steps
// read the same freed allocation, and the hub's streaming churn frees and reallocates
// actors constantly.  The soak died on exactly that shape:
//
//     EXCEPTION_ACCESS_VIOLATION reading 0x0000000100000017 / 0x000001951e4d50bc
//       WINMM+0x3b357  -> EnemySpawnPump      (reads EnemyRec::actor)
//       WINMM+0x6f4d7  -> ScanLocalEquip
//       WINMM+0x37988  -> EnemyIdentity
//
// 0x0000000100000017 is two 32-bit fields of a recycled heap block read as a pointer.
// (Session 18 resolved those with the /MAP the build had all along -- see
// tools/map_lookup.py; before that they were only offsets in a log.)
//
// THE PATTERN
// -----------
// The engine's own answer is TWeakObjectPtr: identify an object by WHERE IT LIVES IN
// GUObjectArray, not by its address.  A UObject's slot is `InternalIndex` (+0x0C), and
// the FUObjectItem holding that slot carries a `SerialNumber` (+0x10) that the engine
// BUMPS every time the slot is reused.  So {index, serial} names one object for its
// lifetime: after the object dies its slot is either null or holds a different object
// with a different serial.
//
// Resolution is therefore INDEX-FIRST and never touches the cached address:
//
//     p = table[index];                      // UE nulls the slot on destruction
//     if (!p) return nullptr;
//     if (table_serial(index) != serial) return nullptr;   // the slot was reused
//     return p;
//
// A freed pointer is never read -- not even to learn its index -- which is the whole
// point.  `Ref` is a drop-in for `void*`: it converts to `void*` by RESOLVING, and
// converts from `void*` by CAPTURING.  A use site that reads a cached pointer as a
// pointer therefore gets validating behaviour for free.
//
// WHY ZERO MEANS EMPTY
// --------------------
// These tables are static arrays and several are memset or value-initialised, so the
// empty state has to be all-zero bytes.  A `{index = 0}` encoding would make a
// zeroed record claim GUObjectArray slot 0 -- a real slot -- and "empty" would resolve
// to somebody else's object.  The index is therefore stored BIASED (`index + 1`), so
// zero is unambiguously empty and a wiped record is empty rather than aliased.
//
// WHY THE SOURCE IS INJECTED
// --------------------------
// The engine table lives at a fixed RVA inside the game image, so reading it from a
// unit test would fault.  `Source` is two function pointers; proxy.cpp installs the real
// GUObjectArray readers at substrate init and a test installs a fake table.  That is what
// makes the reuse-after-free case testable at all: a test can free the slot, reuse it and
// prove the handle refuses it -- none of which is observable in a live run except as a
// crash.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdint.h>

namespace qd {
namespace weak {

// UObject::InternalIndex.  The same value qdsubstrate.h calls kObjInternalIndex; stated
// again because this header must compile without the substrate (the tests include it
// standalone) and the two are held together by test_weak.cpp's fixture layout.
static const int kIndexOff = 0x0C;

// FUObjectItem is { UObject* Object; int32 Flags; int32 ClusterRootIndex; int32
// SerialNumber } with a 0x18 stride (qdsubstrate.h pins the stride and the chunk table).
static const int kItemSerialOff  = 0x10;
static const int kItemStride     = 0x18;
static const int kObjectsPerChunk = 65536;

// What an object is called, independent of where it is.
struct Handle {
    int32_t index;    // biased: 0 = empty, otherwise GUObjectArray slot + 1
    int32_t serial;
};

// How to reach the engine's object table.  `at` returns the object in a slot, or null
// when the slot is empty -- which is what UE leaves behind after destruction.  `serial`
// may answer 0 for an unknown slot; `at` is the authority on existence.
struct Source {
    void*   (*at)(int index);
    int32_t (*serial)(int index);
};

// Process-wide source.  Null until installed: resolution returns nullptr rather than
// guessing, so a missing install is a lane that does nothing, never a read of the game's
// unmapped RVA from a test binary.
inline Source& WeakSource()
{
    // A function-local static of a POD with constant initialisation: no thread-safe
    // init guard is emitted, which matters because this is used from code paths that run
    // beside DllMain where the CRT's guard machinery is not yet usable.
    static Source s = {nullptr, nullptr};
    return s;
}

inline void SetSource(void* (*at)(int), int32_t (*serial)(int))
{
    WeakSource().at = at;
    WeakSource().serial = serial;
}

inline bool SourceReady()
{
    const Source& s = WeakSource();
    return s.at != nullptr && s.serial != nullptr;
}

// An empty handle.  All-zero, so a zero-initialised record is empty.
inline Handle Empty() { Handle h = {0, 0}; return h; }

// The object's slot, read off a live object.  Guarded: the pointer may already be
// garbage, and a capture from garbage yields an EMPTY handle rather than a fault.  The
// serial comes from the table (via the Source), never from the object.
inline Handle Capture(const void* o)
{
    if (!o || !SourceReady()) return Empty();
    Handle h = Empty();
    __try {
        int32_t i = *(const int32_t*)((const uint8_t*)o + kIndexOff);
        if (i < 0) return Empty();       // the engine released this slot already
        // The slot must still name this very object.  A capture from a stale address
        // whose slot now belongs to someone else is not a handle to anything.
        if (WeakSource().at(i) != o) return Empty();
        h.index = i + 1;
        h.serial = WeakSource().serial(i);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        h = Empty();
    }
    return h;
}

// Resolve a handle.  Never reads the address the handle was captured from; every load
// is bounded by the table itself, which the Source bounds-checks.
inline void* Resolve(const Handle& h)
{
    if (h.index <= 0 || !SourceReady()) return nullptr;
    __try {
        int i = (int)(h.index - 1);
        void* o = WeakSource().at(i);
        if (!o) return nullptr;                                    // destroyed
        if (WeakSource().serial(i) != h.serial) return nullptr;    // slot reused
        return o;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

inline bool Same(const Handle& a, const Handle& b)
{
    return a.index > 0 && a.index == b.index && a.serial == b.serial;
}

// A weak pointer that reads like a `void*`.
//
//   weak::Ref r;            // empty
//   r = someActor;          // captures {slot, serial}
//   r = nullptr;            // empties
//   void* p = r;            // RESOLVES: null if the object died or its slot was reused
//
// It is deliberately the SAME SIZE as a `void*` (8 bytes, no more), so converting a
// cached pointer field does not grow a record table and an array of these is still one
// pointer per entry.  There is also deliberately no way to get at the address the handle
// was captured from: that value stops meaning anything the moment the engine frees it,
// and a getter would be an invitation to the exact dereference this class removes.
class Ref {
public:
    Ref() {}
    Ref(void* o) { Set(o); }
    Ref& operator=(void* o) { Set(o); return *this; }

    void Set(void* o) { m_h = Capture(o); }
    void Reset() { m_h = Empty(); }

    // The validating read.  Every use site goes through this.
    void* Get() const { return Resolve(m_h); }
    operator void*() const { return Get(); }

    bool Valid() const { return Resolve(m_h) != nullptr; }
    bool IsEmpty() const { return m_h.index <= 0; }
    Handle Raw() const { return m_h; }

    // Identity without resolving: two refs name the same OBJECT iff their handles match,
    // even if the object has since been freed and its slot reused.
    bool SameObject(const Ref& o) const { return Same(m_h, o.m_h); }

private:
    Handle m_h = {0, 0};
};

// A handle is exactly one pointer wide, so `Ref` can stand in for `void*` in a table
// without changing the table's footprint.  The two assertions below are the contract the
// conversion of every cached pointer relies on.
static_assert(sizeof(Handle) == sizeof(void*), "a Handle must be one pointer wide");
static_assert(sizeof(Ref) == sizeof(void*), "Ref must be a drop-in for void* in size");

} // namespace weak
} // namespace qd
