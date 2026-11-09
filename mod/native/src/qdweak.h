// qdweak.h -- weak handles to UObjects, so a cached pointer can never be dereferenced
// after the engine freed it.
//
// THE BUG THIS EXISTS FOR (Session 17 R132, and the reason for Session 18)
// ----------------------------------------------------------------------
// The mod cached raw `void*`'s to actors and objects in globals and record tables
// (`g_localPawn`, `g_puppetActor`, `EnemyRec::actor`, `MirrorState`, the loot/mover/
// cutscene caches, ...) and then re-checked them with `ObjectIsLive(p)`, which is:
//
//     ObjectAt(ObjInternalIndex(p)) == p
//
// That test dereferences `p` to read its InternalIndex.  `p` is the thing that may
// already be freed.  So "check liveness, then use" is not a guard at all -- both
// steps read the same freed allocation, and the hub's streaming churn frees and
// reallocates actors constantly.  The soak died on exactly that shape:
//
//     EXCEPTION_ACCESS_VIOLATION reading 0x0000000100000017
//       WINMM+0x3b357  -> EnemySpawnPump      (reads EnemyRec::actor)
//       WINMM+0x6f4d7  -> ScanLocalEquip
//       WINMM+0x37988  -> EnemyIdentity
//   0x0000000100000017 is two 32-bit fields of a recycled heap block read as a pointer.
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
//     p = table[index];                      // the slot is null after destruction
//     if (!p) return nullptr;
//     if (table_serial(index) != serial) return nullptr;   // the slot was reused
//     return p;
//
// A freed pointer is never read -- not even to learn its index -- which is the whole
// point.  `Ref` is a drop-in for `void*`: it converts to `void*` by RESOLVING, and
// converts from `void*` by CAPTURING.  A use site that reads a cached pointer as a
// pointer gets the validating behaviour for free; the only sites that change are ones
// that need the handle itself (identity comparisons across a realloc, diagnostics).
//
// WHY THE SOURCE IS INJECTED
// --------------------------
// The game's table lives at a fixed RVA inside the game image, so reading it from a
// unit test would fault.  `Source` is two function pointers; proxy.cpp installs the
// real GUObjectArray readers at substrate init, and a test installs a fake table.  That
// is what makes the reuse-after-free case testable at all: the test can free the slot,
// reuse it, and prove the handle refuses it -- none of which is observable in a live run
// except as a crash.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdint.h>

namespace qd {
namespace weak {

// UObject::InternalIndex.  The same value qdsubstrate.h calls kObjInternalIndex; stated
// again here because this header must compile without the substrate (the tests include
// it standalone) and the two are pinned together by test_weak.cpp.
static const int kIndexOff = 0x0C;

// FUObjectItem is { UObject* Object; int32 Flags; int32 ClusterRootIndex; int32
// SerialNumber } with a 0x18 stride (qdsubstrate.h pins the stride and the chunk table).
static const int kItemSerialOff = 0x10;
static const int kItemStride    = 0x18;
static const int kObjectsPerChunk = 65536;

// What an object is called, independent of where it is.
struct Handle {
    int32_t index;
    int32_t serial;
};

// How to reach the engine's object table.  `serial` is allowed to answer 0 for an
// unknown index; `index` is the authority on existence, and a null return from `at`
// means "the slot is empty" (which is what UE leaves behind after destruction).
struct Source {
    void*  (*at)(int index);
    int32_t (*serial)(int index);
};

// Process-wide source.  Null until installed: `Ref::Get` returns nullptr rather than
// guessing, so a missing install is a lane that does nothing, never a read of the
// game's unmapped RVA from a test binary.
inline Source& WeakSource()
{
    // A function-local static of a POD with constant initialisation: no thread-safe
    // init guard is emitted, which matters because this runs inside DllMain-adjacent
    // code paths where the CRT's guard machinery is not yet usable.
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

// The object's slot, read off a live object.  Guarded: the pointer may already be
// garbage, and a capture from garbage must yield an INVALID handle rather than a fault.
// `serial` comes from the table (via the Source), never from the object, so capture also
// works when the engine has already bumped it.
inline Handle Capture(const void* o)
{
    Handle h = {-1, 0};
    if (!o || !SourceReady()) return h;
    __try {
        int32_t i = *(const int32_t*)((const uint8_t*)o + kIndexOff);
        // A negative index means the slot was already released (the engine writes -1 to
        // a destroyed object's InternalIndex in this build's teardown path); a huge one
        // is a recycled block, not an object.
        if (i < 0 || i > 0x7FFFFFF) return h;
        h.index = i;
        h.serial = WeakSource().serial(i);
        // The slot must still name this very object: a capture from a stale address
        // whose index now belongs to someone else is not a handle to anything.
        if (WeakSource().at(i) != o) {
            h.index = -1;
            h.serial = 0;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        h.index = -1;
        h.serial = 0;
    }
    return h;
}

// Resolve a handle.  Never reads the address the handle was captured from; every load
// here is bounded by the table itself.
inline void* Resolve(const Handle& h)
{
    if (h.index < 0 || !SourceReady()) return nullptr;
    __try {
        void* o = WeakSource().at(h.index);
        if (!o) return nullptr;                       // destroyed: UE nulls the slot
        if (WeakSource().serial(h.index) != h.serial) return nullptr;   // slot reused
        return o;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

inline bool Same(const Handle& a, const Handle& b)
{
    return a.index >= 0 && a.index == b.index && a.serial == b.serial;
}

// A weak pointer that reads like a `void*`.
//
//   weak::Ref r;            // empty
//   r = someActor;          // captures {index, serial}
//   r = nullptr;            // empties
//   void* p = r;            // RESOLVES: null if the object died or its slot was reused
//
// There is deliberately no way to get at the captured address for dereference: the
// class exists because that value stops meaning anything the moment the engine frees
// it.  `Captured()` is for logging and for "did the row change" comparisons only.
class Ref {
public:
    Ref() {}
    Ref(void* o) { Set(o); }
    Ref(const Ref&) = default;
    Ref& operator=(const Ref&) = default;

    Ref& operator=(void* o) { Set(o); return *this; }

    void Set(void* o) { m_h = Capture(o); m_captured = (m_h.index >= 0) ? o : nullptr; }
    void Reset() { m_h.index = -1; m_h.serial = 0; m_captured = nullptr; }

    // The validating read.  Every use site goes through this.
    void* Get() const { return Resolve(m_h); }
    operator void*() const { return Get(); }

    bool Valid() const { return Resolve(m_h) != nullptr; }
    Handle Raw() const { return m_h; }

    // Diagnostics only, never dereferenced.  Equal to the live pointer while the object
    // is alive; a stale address afterwards, which is exactly what a log line wants.
    void* Captured() const { return m_captured; }

    // Identity without resolving: two refs name the same OBJECT iff their handles
    // match, even if one of them has since been freed.
    bool SameObject(const Ref& o) const { return Same(m_h, o.m_h); }

private:
    Handle m_h = {-1, 0};
    void*  m_captured = nullptr;
};

} // namespace weak
} // namespace qd
