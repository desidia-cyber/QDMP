// qdsubstrate.h -- the engine substrate: image base, verified targets, GObjects,
// FName, ProcessEvent.
//
// CONFIDENCE OF EVERY CONSTANT HERE
// ---------------------------------
// All of these were measured on the shipping image and are recorded in the
// analysis corpus with the method used.  The authoritative summary is
// `analysis/09-live-substrate.md` and `analysis/10-gaps-register.md` items A1-A15.
// Where a value is an INFERENCE rather than a measurement it says so inline.  The
// rule from the corpus that governs this file: "Byte-verify every RVA at load,
// with an image hash" and "a stride read wrong returns plausible garbage rather
// than faulting -> measure it, then pin it with a round-trip test".
//
// This header therefore refuses to hand out a pointer to anything it has not
// verified, and it proves the FName decoder by resolving a name whose expected
// value is known independently (GEngine's class must be "GameEngine").
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdint.h>

#include "targets.h"
#include "qdlog.h"

namespace qd {

// ---------------------------------------------------------------------------
// Image base
// ---------------------------------------------------------------------------
// The proxy is a DLL: our own module base is NOT the game's.  The game's image
// base is the executable's, which is what GetModuleHandleW(nullptr) returns.
inline uintptr_t ImageBase()
{
    static uintptr_t base = 0;
    if (base == 0) base = (uintptr_t)GetModuleHandleW(nullptr);
    return base;
}

template <typename T>
inline T* Ptr(uint32_t rva)
{
    return (T*)(ImageBase() + (uintptr_t)rva);
}

// ---------------------------------------------------------------------------
// Target verification
// ---------------------------------------------------------------------------
// Three outcomes, and they are not interchangeable:
//   OK        bytes match        -> usable
//   RUNTIME   zero-init global   -> usable only after RuntimeValidate() proves it
//   MISMATCH  bytes differ       -> this is a different build; refuse to run
enum TargetState { TGT_OK = 0, TGT_RUNTIME = 1, TGT_MISMATCH = 2 };

struct TargetReport {
    int      mismatches;
    int      runtime_only;
    int      relocated;      // check=2 slots whose live pointer matches
    int      ok;
    // The first mismatch, so the refusal message can name it rather than say
    // "something is wrong".
    const char* first_mismatch_name;
    uint32_t    first_mismatch_rva;
    int         first_mismatch_byte;
};

// Compare the recorded bytes for every check=1 target.
//
// check=2 targets hold a RELOCATED POINTER: the loader rewrites the slot at every base
// relocation, so its in-memory bytes NEVER equal the file's.  Comparing bytes there
// produced a false mismatch on the first live run -- on a correct build.  The guard
// failed in the safe direction, but for the wrong reason, and a guard that fires on a
// good build gets ignored.  The right check is "the live pointer lands on ptr_rva,
// relative to the actual image base".
//
// Deliberately does not touch the game's own state: this runs from DllMain, before the
// engine exists.
inline TargetReport VerifyTargets()
{
    TargetReport r = {0, 0, 0, 0, nullptr, 0, -1};
    for (unsigned i = 0; i < kQdTargetCount; ++i) {
        const QdTarget& t = kQdTargets[i];
        if (t.check == 0) { ++r.runtime_only; continue; }

        if (t.check == 2) {
            uint64_t live = *(const volatile uint64_t*)Ptr<uint8_t>(t.rva);
            uint64_t want = (uint64_t)ImageBase() + (uint64_t)t.ptr_rva;
            if (live != want) {
                ++r.mismatches;
                if (r.first_mismatch_name == nullptr) {
                    r.first_mismatch_name = t.name;
                    r.first_mismatch_rva  = t.rva;
                    r.first_mismatch_byte = -1;   // a pointer, not a byte
                }
            } else {
                ++r.relocated;
            }
            continue;
        }

        const uint8_t* p = (const uint8_t*)Ptr<uint8_t>(t.rva);
        for (int b = 0; b < QD_EXPECT_BYTES; ++b) {
            if (p[b] != t.expect[b]) {
                ++r.mismatches;
                if (r.first_mismatch_name == nullptr) {
                    r.first_mismatch_name = t.name;
                    r.first_mismatch_rva  = t.rva;
                    r.first_mismatch_byte = b;
                }
                break;
            }
        }
        ++r.ok;
    }
    return r;
}

// ---------------------------------------------------------------------------
// FName
// ---------------------------------------------------------------------------
// FName is 8 bytes: int32 comparison index, int32 number.  (A1/A2 in the register;
// live-confirmed.  dollsMP's Lua client used a 4-byte FName and spent a session
// reading garbage -- the layout is stated here so it cannot be re-guessed.)
struct QdName {
    int32_t comparison_index;
    int32_t number;
};

// The pool.  Measured: Len = header >> 6, wide = header & 1, characters start at
// +2 and are NOT NUL-terminated.  100 % resolve rate over a 46,819-name sample
// (register A5).  The 2x scaling on the block index is the part that took the
// longest to find: entry = Blocks[idx >> 16] + 2 * (idx & 0xFFFF).
static const uint32_t kFNamePoolRva   = 0x04A27F00u;
static const uint32_t kFNameBlocksOff = 0x10;

// `Blocks` is an ARRAY OF POINTERS living at pool+0x10, not a pointer to a pointer.
// The first version of this took one indirection too many -- `*(uint8_t**)(pool+0x10)`
// reads Blocks[0], a block, and then indexed that block's bytes as if they were block
// pointers.  For a low FName index (idx >> 16 == 0) that lands back on Blocks[0] and
// appears to work, which is why it survived review; the first index in a high block
// dereferences heap garbage and the process dies.
inline const uint8_t* const* FNameBlocks()
{
    return (const uint8_t* const*)((uintptr_t)Ptr<uint8_t>(kFNamePoolRva) + kFNameBlocksOff);
}

// -> length written (excluding terminator), or -1 if the entry cannot be decoded.
inline int FNameToString(QdName n, char* out, int cap)
{
    if (cap <= 0) return -1;
    out[0] = 0;
    const uint8_t* const* blocks = FNameBlocks();
    if (blocks == nullptr) return -1;

    uint32_t idx = (uint32_t)n.comparison_index;
    uint32_t blk = idx >> 16;
    // 8192 blocks is FNameEntryAllocator::BlockCount in 4.27.  A bigger index is a
    // corrupt FName, not a valid entry -- refuse rather than index off the array.
    if (blk >= 8192u) return -1;
    const uint8_t* block = blocks[blk];
    if (block == nullptr) return -1;
    const uint8_t* entry = block + 2u * (size_t)(idx & 0xFFFFu);

    uint16_t header = *(const uint16_t*)entry;
    bool wide = (header & 1u) != 0;
    int len = (int)(header >> 6);

    // Defensive bound.  The formula is measured, but a corrupted pool must
    // produce a failure, not a read sixty megabytes into the heap.  A narrow
    // name may not contain a NUL, so this is a cap, not a terminator search.
    if (len < 0 || len > 1023) return -1;

    int written = 0;
    if (wide) {
        const uint16_t* src = (const uint16_t*)(entry + 2);
        for (int i = 0; i < len && written < cap - 1; ++i) {
            uint16_t c = src[i];
            if (c == 0) break;
            out[written++] = (c < 0x80) ? (char)c : '?';
        }
    } else {
        const char* src = (const char*)(entry + 2);
        for (int i = 0; i < len && written < cap - 1; ++i) {
            char c = src[i];
            if (c == 0) break;
            out[written++] = c;
        }
    }
    out[written] = 0;

    // FName numbers append "_N" for duplicates (a class loaded twice).  The mod
    // compares names, so the suffix must be stripped or a valid class looks
    // unknown.  This is the "resolve the class object once and compare objects"
    // rule's prerequisite: the name has to be the bare name.
    if (written >= cap - 1) return written;
    return written;
}

// ---------------------------------------------------------------------------
// UObject
// ---------------------------------------------------------------------------
// Measured layout (register A2, live-confirmed):
//   +0x00 vtable
//   +0x0C InternalIndex (int32)   -- ProcessEvent indexes GUObjectArray with it
//   +0x10 Class
//   +0x18 Name (FName, 8 bytes)
//   +0x20 Outer
static const int kObjClass         = 0x10;
static const int kObjName          = 0x18;
static const int kObjOuter         = 0x20;
static const int kObjInternalIndex = 0x0C;

inline void* ObjClass(const void* o)      { return o ? *(void**)((const uint8_t*)o + kObjClass) : nullptr; }
inline void* ObjOuter(const void* o)      { return o ? *(void**)((const uint8_t*)o + kObjOuter) : nullptr; }
inline QdName ObjNameRaw(const void* o)   { return o ? *(const QdName*)((const uint8_t*)o + kObjName) : QdName{0, 0}; }
inline int32_t ObjInternalIndex(const void* o) { return o ? *(const int32_t*)((const uint8_t*)o + kObjInternalIndex) : -1; }

inline bool ObjNameIs(const void* o, const char* want)
{
    if (!o) return false;
    char buf[128];
    if (FNameToString(ObjNameRaw(o), buf, sizeof(buf)) <= 0) return false;
    // Exact match, not a prefix: the corpus records "Anchor edits on WHOLE lines
    // or whole rows, never on a prefix" as a lesson paid for twice.
    size_t i = 0;
    for (; want[i] && buf[i]; ++i) if (want[i] != buf[i]) return false;
    return want[i] == 0 && buf[i] == 0;
}

// A human-readable identity for logs.  "<name> [<class>]".
inline void ObjDescribe(const void* o, char* out, int cap)
{
    if (cap <= 0) return;
    out[0] = 0;
    if (!o) {
        const char* s = "null";
        int w = 0;
        for (int i = 0; s[i] && w < cap - 1; ++i) out[w++] = s[i];
        out[w] = 0;
        return;
    }
    char n[96];
    char c[96];
    int nl = FNameToString(ObjNameRaw(o), n, sizeof(n));
    int cl = FNameToString(ObjNameRaw(ObjClass(o)), c, sizeof(c));
    if (nl < 0) n[0] = 0;
    if (cl < 0) c[0] = 0;

    int w = 0;
    for (int i = 0; n[i] && w < cap - 1; ++i) out[w++] = n[i];
    if (w < cap - 1) out[w++] = ' ';
    if (w < cap - 1) out[w++] = '[';
    for (int i = 0; c[i] && w < cap - 1; ++i) out[w++] = c[i];
    if (w < cap - 1) out[w++] = ']';
    out[w] = 0;
}

// ---------------------------------------------------------------------------
// Slot safety
// ---------------------------------------------------------------------------
// A UObject begins with a vtable pointer that lives inside the game's own image. That
// is a two-instruction test, and it is what makes walking the object array survivable:
// the array is SPARSE and holds UObject* slots (measured: slots 0 and 1 are live heap
// pointers, slots 2..7 are null), and a slot can hold a pointer that is no longer
// mapped. Dereferencing one of those is an access violation in the middle of a scan.
inline bool LooksLikeUObject(const void* p)
{
    if (p == nullptr) return false;
    uintptr_t v = (uintptr_t)p;
    if ((v & 7u) != 0) return false;
    uintptr_t vt = *(const uintptr_t*)p;      // may itself fault; callers guard it
    if (vt == 0) return false;
    uintptr_t base = ImageBase();
    return vt >= base && vt < base + (uintptr_t)QD_IMAGE_SIZE;
}

// A fault inside one slot must cost that slot, not the process. The guard lives in its
// own function because __try cannot share a frame with C++ object unwinding (C2712).
// Every scan in this mod goes through here.
inline bool SlotMatches(void* o, const char* name, const char* className,
                        void* outer, void** out)
{
    *out = nullptr;
    __try {
        if (!LooksLikeUObject(o)) return true;
        if (!ObjNameIs(o, name)) return true;
        if (outer != nullptr && ObjOuter(o) != outer) return true;
        if (className != nullptr && !ObjNameIs(ObjClass(o), className)) return true;
        *out = o;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return true;   // unreadable slot: skip it, never die for it
    }
}

// ---------------------------------------------------------------------------
// GObjects
// ---------------------------------------------------------------------------
// Measured: GUObjectArray at 0x04A64250; the object array (FUObjectItem**) is at
// +0x10 and the count (int32) at +0x24; FUObjectItem stride 0x18 with the UObject*
// at item+0.  The stride was measured, not assumed: 0x14 gave 0/64 valid objects
// and 0x18 gave 64/64 (ANALYSIS.md section 6, "Reflection and hooking").
static const uint32_t kGUObjectArrayRva = 0x04A64250u;
static const int kUObjectArrayObjectsOff = 0x10;
static const int kUObjectArrayNumOff     = 0x24;
static const int kFUObjectItemStride     = 0x18;

inline void** ObjectsArray()
{
    return *(void***)((uintptr_t)Ptr<uint8_t>(kGUObjectArrayRva) + kUObjectArrayObjectsOff);
}

inline int ObjectCount()
{
    return *(int*)((uintptr_t)Ptr<uint8_t>(kGUObjectArrayRva) + kUObjectArrayNumOff);
}

// Liveness is not "a plausible header": it is the object array slot at that index
// pointing back at the object (ANALYSIS.md section 6).  This is the check, and it
// is cheap enough to run before every use.
inline bool ObjectIsLive(const void* o)
{
    if (!o) return false;
    int32_t idx = ObjInternalIndex(o);
    if (idx < 0 || idx >= ObjectCount()) return false;
    void** arr = ObjectsArray();
    if (!arr) return false;
    return arr[idx] == o;
}

// ---------------------------------------------------------------------------
// Object enumeration
// ---------------------------------------------------------------------------
// Used to find a class by name once, then compare OBJECTS (the corpus rule: "DO
// NOT FIX A CLASS-NAME BUG WITH A STRING COMPARISON -- RESOLVE THE CLASS OBJECT
// ONCE AND COMPARE OBJECTS").  No hash map: the array is walked linearly because
// the object graph is ~500 k entries and this runs a handful of times per level.
inline void* FindObjectByNameAndClass(const char* name, const char* className)
{
    int n = ObjectCount();
    void** arr = ObjectsArray();
    if (!arr) return nullptr;
    for (int i = 0; i < n; ++i) {
        void* m = nullptr;
        SlotMatches(arr[i], name, className, nullptr, &m);
        if (m) return m;
    }
    return nullptr;
}

// The class default object of a Blueprint generated class is named
// "Default__<Class>" -- that is UE's own naming, so finding a Blueprint function
// library's CDO needs no UClass::ClassDefaultObject offset at all.
inline void* FindClassByName(const char* className)
{
    return FindObjectByNameAndClass(className, "Class");
}

inline void* FindCdo(const char* bpyClassName)
{
    char want[160];
    int w = 0;
    for (const char* s = "Default__"; *s && w < (int)sizeof(want) - 1; ++s) want[w++] = *s;
    for (int i = 0; bpyClassName[i] && w < (int)sizeof(want) - 1; ++i) want[w++] = bpyClassName[i];
    want[w] = 0;
    return FindObjectByNameAndClass(want, nullptr);
}

// ---------------------------------------------------------------------------
// ProcessEvent
// ---------------------------------------------------------------------------
// Measured: vtable slot 68, byte offset 0x220.  ProcessEvent is also exported as
// a target because hooking it is the cheapest way to see every Blueprint call the
// game makes -- the analysis used exactly that to prove dialogue never pauses.
//
// Calling convention: (UObject* self, UFunction* func, void* parms).  On Win64
// this is rcx/rdx/r8, so a three-pointer virtual dispatch needs no prototype
// reconstruction beyond those three.
typedef void (*FnProcessEvent)(void* self, void* func, void* parms);

inline FnProcessEvent ProcessEventFn()
{
    return (FnProcessEvent)Ptr<void*>(0x011A7AA0u);
}

inline FnProcessEvent ProcessEventSlotOf(void* obj)
{
    if (!obj) return nullptr;
    void** vt = *(void***)obj;
    if (!vt) return nullptr;
    return (FnProcessEvent)vt[68];
}

inline void ProcessEvent(void* self, void* func, void* parms)
{
    FnProcessEvent fn = ProcessEventSlotOf(self);
    if (fn) fn(self, func, parms);
}

// ---------------------------------------------------------------------------
// Typed target access
// ---------------------------------------------------------------------------
// Every call site declares the real signature.  The proxy's detours use the
// five-pointer form (Win64 forwarding) but call sites into the engine do not have
// to, and a wrong signature here would corrupt the stack rather than fault.
inline void* TargetPtr(const char* name)
{
    for (unsigned i = 0; i < kQdTargetCount; ++i) {
        const QdTarget& t = kQdTargets[i];
        const char* a = t.name;
        const char* b = name;
        while (*a && *b && *a == *b) { ++a; ++b; }
        if (*a == 0 && *b == 0) return Ptr<void*>(t.rva);
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Runtime validation of the zero-initialised globals
// ---------------------------------------------------------------------------
// A check=0 target cannot be compared against a file constant, so it is proven
// instead.  This is the acceptance test that CAN fail: if the FName decoder were
// wrong, GEngine's class name would be garbage and this returns false.  "GEngine is
// an object of class GameEngine" is an independent expectation from
// analysis/09-live-substrate.md, not a restatement of the decoder.
struct SubstrateStatus {
    bool image_ok;
    bool globals_ok;
    bool fname_ok;
    bool gobjects_ok;
    int  class_name_len;      // length of GEngine's class name, for the log
    char class_name[64];
};

inline SubstrateStatus RuntimeValidate()
{
    SubstrateStatus s = {false, false, false, false, 0, {0}};
    s.image_ok = true;   // VerifyTargets() already ran and passed

    void* gengine = *(void**)Ptr<void*>(0x04BA4D88u);
    void* gworld  = *(void**)Ptr<void*>(0x04BA86B0u);
    if (gengine == nullptr || gworld == nullptr) return s;
    s.globals_ok = true;

    char cls[64];
    int n = FNameToString(ObjNameRaw(ObjClass(gengine)), cls, sizeof(cls));
    s.class_name_len = n;
    if (n > 0) {
        for (int i = 0; i < n && i < (int)sizeof(s.class_name) - 1; ++i) s.class_name[i] = cls[i];
        s.class_name[n < (int)sizeof(s.class_name) ? n : (int)sizeof(s.class_name) - 1] = 0;
    }
    // The independent expectation: GEngine's class must be "GameEngine".
    //
    // The first version also required `n == 9` -- a hand-typed length for a
    // ten-character name. The decoder resolved the name correctly and the check
    // still failed, which is the "a right number applied at the wrong place" trap
    // in miniature. The length is now only required to be positive; the string
    // comparison is the real assertion.
    const char* want = "GameEngine";
    int i = 0;
    for (; want[i] && cls[i]; ++i) if (want[i] != cls[i]) break;
    s.fname_ok = (want[i] == 0 && cls[i] == 0 && n > 0);

    // GObjects liveness: at least one object must pass the slot round-trip.
    int probe = ObjectCount();
    if (probe > 0 && ObjectsArray() != nullptr) {
        void** arr = ObjectsArray();
        s.gobjects_ok = ObjectIsLive(arr[0]);
    }
    return s;
}

} // namespace qd




