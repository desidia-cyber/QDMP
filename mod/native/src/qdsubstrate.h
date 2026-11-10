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
// GObjects
// ---------------------------------------------------------------------------
// GUObjectArray at 0x04A64250.  In UE4.27 `ObjObjects` is a FChunkedFixedUObjectArray
// at +0x10:
//   +0x10 FUObjectItem** Objects       -- a table of CHUNK pointers
//   +0x18 FUObjectItem*  PreAllocated
//   +0x20 int32 MaxElements, +0x24 int32 NumElements
//   +0x28 int32 MaxChunks,   +0x2C int32 NumChunks
// Each chunk holds 65536 FUObjectItem of 0x18 bytes, with the UObject* at item+0.
//
// THE BUG THIS REPLACES.  Earlier versions indexed `Objects` as a flat UObject*[]:
// `arr[i]`.  `Objects` is the chunk table, so arr[0] and arr[1] were the two chunk
// pointers and arr[2..] ran off the end of a small allocation into the heap.  That is
// exactly the "measurement" in LIVE-TEST-LOG R12 (two live pointers, then zeros, uneven
// deltas) and the "walking GObjects terminates the game" of R13: the walk read half a
// megabyte of unrelated heap and decoded whatever it found as objects.  The 0x18
// stride constant had been measured correctly and was simply never used.
static const uint32_t kGUObjectArrayRva = 0x04A64250u;
static const int kUObjectArrayObjectsOff   = 0x10;
static const int kUObjectArrayNumOff       = 0x24;
static const int kUObjectArrayNumChunksOff = 0x2C;
static const int kFUObjectItemStride       = 0x18;
static const int kFUObjectItemSerialOff    = 0x10;
static const int kObjectsPerChunk          = 65536;

inline uint8_t** ObjectChunks()
{
    return *(uint8_t***)((uintptr_t)Ptr<uint8_t>(kGUObjectArrayRva) + kUObjectArrayObjectsOff);
}

inline int ObjectCount()
{
    return *(int*)((uintptr_t)Ptr<uint8_t>(kGUObjectArrayRva) + kUObjectArrayNumOff);
}

inline int ObjectChunkCount()
{
    return *(int*)((uintptr_t)Ptr<uint8_t>(kGUObjectArrayRva) + kUObjectArrayNumChunksOff);
}

// The object at index i, or null.  Bounds-checked against both the element count and
// the chunk count, so a bad index can never leave the chunk table.
inline void* ObjectAt(int i)
{
    if (i < 0 || i >= ObjectCount()) return nullptr;
    int c = i / kObjectsPerChunk;
    if (c >= ObjectChunkCount()) return nullptr;
    uint8_t** chunks = ObjectChunks();
    if (!chunks) return nullptr;
    uint8_t* chunk = chunks[c];
    if (!chunk) return nullptr;
    return *(void**)(chunk + (size_t)(i % kObjectsPerChunk) * kFUObjectItemStride);
}

// The serial number of a SLOT.  The engine bumps it every time the slot is reused, which
// is what makes a {slot, serial} pair identify one object for its lifetime even after the
// object is freed -- the basis of qdweak.h.  FUObjectItem is
// { UObject* Object; int32 Flags; int32 ClusterRootIndex; int32 SerialNumber } at a 0x18
// stride, so the serial is item+0x10.
inline int32_t ObjectSerialAt(int i)
{
    if (i < 0 || i >= ObjectCount()) return 0;
    int c = i / kObjectsPerChunk;
    if (c >= ObjectChunkCount()) return 0;
    uint8_t** chunks = ObjectChunks();
    if (!chunks) return 0;
    uint8_t* chunk = chunks[c];
    if (!chunk) return 0;
    return *(int32_t*)(chunk + (size_t)(i % kObjectsPerChunk) * kFUObjectItemStride +
                       kFUObjectItemSerialOff);
}

// The object at index i, IF the slot is safe to dereference.  Null otherwise.
//
// WHY THIS EXISTS (Session 18, R139).  `ObjectAt(i)` returns whatever pointer sits in slot i,
// and UE only NULLS a slot when it destroys the object -- so between destruction and that write
// the slot can hold an address whose memory has been freed and possibly reused.  Reading that
// object's Class (or anything else in it) is then a read of freed memory.  The soak crashed on
// exactly that instruction:
//
//   WINMM+0x6fde1  ScanLocalEquip+0x371   mov rcx,[rax+0x10]   <- ObjClass(o), rax = the slot
//   EXCEPTION_ACCESS_VIOLATION reading 0x0000017efb13d040
//
// Every GObjects walk in this module needs the same protection, and the ones that had it said
// so in a local comment while the ones that did not looked identical.  So the guard lives HERE,
// once, and walks call `ObjectAtSafe`.  The two checks are cheap and independent:
//
//   * `LooksLikeUObject` -- aligned, non-null vtable inside the game image.  A recycled block
//     usually fails this, and it is the one check that does not trust the object's contents.
//   * `ObjectIsLive` -- the slot the object claims must still name it.  This is the check that
//     catches "slot 900 still says 0x...1000 but 0x...1000 now says it lives in slot 1200".
//
// Both are `__try`-guarded inside, so a garbage pointer costs a `false`, not the process.

// Liveness: the object's InternalIndex must lead back to the object itself.
// THIS IS NOT A GUARD FOR A CACHED POINTER.  It reads the object's own InternalIndex to
// find the slot, so it dereferences the very allocation that may be freed -- measured as
// Session 17's `EXCEPTION_ACCESS_VIOLATION reading 0x0000000100000017`.  It is only
// meaningful for a pointer obtained THIS frame from the engine.  Anything held across
// frames goes through qdweak.h (`weak::Ref`), which stores the slot instead of the
// address.  The __try keeps a stale call from killing the process, but callers must not
// rely on the answer being "no": a freed block can hold a plausible index.
inline bool ObjectIsLive(const void* o)
{
    if (!o) return false;
    __try {
        int i = ObjInternalIndex(o);
        if (i < 0) return false;
        return ObjectAt(i) == o;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// ---------------------------------------------------------------------------
// Slot safety
// ---------------------------------------------------------------------------
// With correct chunk indexing every non-null slot is a live UObject (UE nulls the slot
// when an object is destroyed), so these checks are belt and braces.  The vtable test
// is cheap and catches a slot that is mid-construction.
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

// A slot whose object is safe to dereference, or null.
//
// See the comment on the shared guard above `ObjectIsLive`: this is that guard, in one place,
// for every GObjects walk in the module.  It has to come AFTER both checks it uses.
inline void* ObjectAtSafe(int i)
{
    void* o = ObjectAt(i);
    if (!o) return nullptr;
    if (!LooksLikeUObject(o)) return nullptr;
    if (!ObjectIsLive(o)) return nullptr;
    return o;
}

// ---------------------------------------------------------------------------
// May this object be the TARGET of a call?
// ---------------------------------------------------------------------------
// Session 19 / R140.  The 330 s death faults inside the game's own code, on a list of
// objects the engine is destroying; our module appears only as the return address of
// the tick it forwards.  So the module cannot fix that engine list directly -- what it
// CAN do is stop handing the engine state it did not validate: an actor that is pending
// kill, already being destroyed, or whose level is being streamed out must not be the
// target of ProcessEvent/Destroy/SetHidden/spawn.
//
// THE BIT IS THE ENGINE'S OWN.  UObject::ProcessEvent's prologue is, measured in this
// image at 0x011A7AA0:
//
//     mov eax,[FUObjectItem.Flags]   ; item+0x08, the slot's own flags
//     shr eax,0x1d                   ; >> 29
//     test al,1
//     jne  <return>                  ; refuse
//
// So bit 29 is not a guess: it is the engine's "do not call into this object" bit, and
// using the same one means the module refuses exactly the calls the engine would.
// RF_BeginDestroyed / RF_FinishDestroyed (UObject::ObjectFlags, +0x08) are the second,
// independent statement that destruction has started.
static const int      kFUObjectItemFlagsOff = 0x08;
static const uint32_t kItemFlagCallDenied   = 1u << 29;
static const int      kObjFlagsOff          = 0x08;
static const uint32_t kObjFlagBeginDestroyed  = 0x00008000u;
static const uint32_t kObjFlagFinishDestroyed = 0x00010000u;

// The slot's flags, index-first like everything else that reads GUObjectArray.
inline uint32_t ObjectItemFlagsAt(int i)
{
    if (i < 0 || i >= ObjectCount()) return 0;
    int c = i / kObjectsPerChunk;
    if (c >= ObjectChunkCount()) return 0;
    uint8_t** chunks = ObjectChunks();
    if (!chunks) return 0;
    uint8_t* chunk = chunks[c];
    if (!chunk) return 0;
    return *(uint32_t*)(chunk + (size_t)(i % kObjectsPerChunk) * kFUObjectItemStride +
                        kFUObjectItemFlagsOff);
}

// The pure part of the decision, so the rule can be stated and tested without a live
// GUObjectArray.  `itemFlags` is FUObjectItem::Flags, `objFlags` UObject::ObjectFlags.
inline bool CallDeniedByFlags(uint32_t itemFlags, uint32_t objFlags)
{
    if (itemFlags & kItemFlagCallDenied) return true;
    return (objFlags & (kObjFlagBeginDestroyed | kObjFlagFinishDestroyed)) != 0;
}

// Live AND not on a destruction path.  Every call into a game object goes through this
// (qdreflect.h's ProcessEventGuarded), and the actor-specific callers add the level
// check on top (proxy.cpp's ActorCallReady).
inline bool ObjectCallable(const void* o)
{
    if (!o || !ObjectIsLive(o)) return false;
    __try {
        int i = ObjInternalIndex(o);
        if (i < 0) return false;
        return !CallDeniedByFlags(ObjectItemFlagsAt(i), *(const uint32_t*)((const uint8_t*)o + kObjFlagsOff));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Class objects: native classes are of class "Class", Blueprint classes of class
// "BlueprintGeneratedClass" (widgets/anim BPs have their own).  The first version
// demanded exactly "Class", so FS_Full_PlayerChar_C -- a Blueprint -- could never be
// found and the puppet could never resolve its class.
inline bool IsClassClassName(const char* n)
{
    static const char* kAccept[] = {"Class", "BlueprintGeneratedClass",
                                    "WidgetBlueprintGeneratedClass",
                                    "AnimBlueprintGeneratedClass"};
    for (unsigned k = 0; k < sizeof(kAccept) / sizeof(kAccept[0]); ++k) {
        const char* a = kAccept[k];
        int i = 0;
        for (; a[i] && n[i] && a[i] == n[i]; ++i) {}
        if (a[i] == 0 && n[i] == 0) return true;
    }
    return false;
}

// className == "*Class" means "any class-of-classes" (see IsClassClassName).
inline bool SlotMatches(void* o, const char* name, const char* className,
                        void* outer, void** out)
{
    *out = nullptr;
    __try {
        if (!LooksLikeUObject(o)) return true;
        if (!ObjNameIs(o, name)) return true;
        if (outer != nullptr && ObjOuter(o) != outer) return true;
        if (className != nullptr) {
            if (className[0] == '*') {
                char cn[64];
                if (FNameToString(ObjNameRaw(ObjClass(o)), cn, sizeof(cn)) <= 0) return true;
                if (!IsClassClassName(cn)) return true;
            } else if (!ObjNameIs(ObjClass(o), className)) {
                return true;
            }
        }
        *out = o;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return true;   // unreadable slot: skip it, never die for it
    }
}

// ---------------------------------------------------------------------------
// Object enumeration
// ---------------------------------------------------------------------------
// Linear walk; a few milliseconds over ~60k objects.  Callers cache the result and
// re-check it with ObjectIsLive.
inline void* FindObjectByNameAndClass(const char* name, const char* className,
                                      void* outer = nullptr)
{
    int n = ObjectCount();
    for (int i = 0; i < n; ++i) {
        void* o = ObjectAt(i);
        if (!o) continue;
        void* m = nullptr;
        SlotMatches(o, name, className, outer, &m);
        if (m) return m;
    }
    return nullptr;
}

inline void* FindClassByName(const char* className)
{
    return FindObjectByNameAndClass(className, "*Class");
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

// UStruct::SuperStruct.  4.27 shipping builds carry FStructBaseChain at UStruct+0x30
// (array) / +0x38 (count) -- confirmed in this image: GetActiveBaseSave's IsA test at
// 0x00E2D136 reads class+0x30 and class+0x38 -- so SuperStruct follows at +0x40.
static const int kStructSuper = 0x40;
inline void* SuperOf(const void* ustruct)
{
    return ustruct ? *(void**)((const uint8_t*)ustruct + kStructSuper) : nullptr;
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

    // GObjects round trip on an object found independently of the array: GEngine's
    // InternalIndex must lead back to GEngine, and its class to the same class object.
    // (The old check looked at arr[0] of the chunk table and passed by coincidence.)
    s.gobjects_ok = ObjectIsLive(gengine) && ObjectIsLive(ObjClass(gengine));
    return s;
}

} // namespace qd




