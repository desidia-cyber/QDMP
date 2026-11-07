// qdsave.h -- the save container: acquisition, reading, and the write path.
//
// WHAT IS MEASURED AND WHAT IS DISCOVERED
// ---------------------------------------
// Measured (read out of the game's own code, not inferred):
//   * `GetActiveBaseSave(WorldContext)` at 0x00E2D100 resolves the live
//     `FS_SaveSlot_C` object.  Verified from its disassembly: it takes one
//     UObject*, walks to the game instance, then returns the cached save from
//     +0x210.  `StoreEnemySaveState` and `GetNamedSaveData` both call it, so its
//     argument is "any world-context object", not specifically a world.
//   * `GetNamedSaveData(WorldContext, FName key, bool* outWasSet) -> int32` at
//     0x00E2F6D0.  Its prologue stores r8 (out bool), then rcx is left untouched
//     for the call to GetActiveBaseSave -- so the argument order is
//     (worldctx, key, out) and the return value is the int32 in eax.
//   * `StoreEnemySaveState(this, const FGuid*, float respawn, float remainingHP)`
//     at 0x00E3BF10.  Its body loads xmm6/xmm7 from xmm3/xmm2 and passes the GUID
//     in rdx, so the two floats are the values, in that order.
// *Discovered at runtime, then verified against the above*: the property layout,
// the `NamedSaveData` member offset, and the TMap element stride.  Each has an
// oracle; none is assumed.
//
// WHY THE WRITE PATH GOES THROUGH PROCESS_EVENT
// ---------------------------------------------
// There is no native `SaveNamedData`.  `SaveFunctions_C` is a
// BlueprintFunctionLibrary and the writer is compiled bytecode (analysis
// pak/blueprint-surface.md parses its 24 exported UFUNCTIONs).  Searching for an
// RVA would be searching for something that does not exist.  So the mod calls the
// game's own UFunction -- and proves it worked by reading the value back through
// the native getter.  That read-back is the acceptance test; "the call returned"
// is not evidence.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdint.h>

#include "qdlog.h"
#include "qdreflect.h"
#include "qdsubstrate.h"

namespace qd {

// FGuid is four int32 in memory, in the order the game stores them.  The wire
// carries these 16 bytes raw (see WIRE.md section 0).
struct QdGuid {
    uint32_t a, b, c, d;
    bool operator==(const QdGuid& o) const { return a == o.a && b == o.b && c == o.c && d == o.d; }
    bool IsZero() const { return (a | b | c | d) == 0; }
};

// ---------------------------------------------------------------------------
// Container names.  Kept in one place because three subsystems refer to them.
// ---------------------------------------------------------------------------
static const char* kClsSaveSlot      = "FS_SaveSlot_C";
static const char* kCdoSaveFunctions = "Default__SaveFunctions_C";
static const char* kPropNamedSaveData = "NamedSaveData";
static const char* kPropSavableActorStates = "SavableActorStates";
static const char* kPropInventoryContents  = "InventoryContents";
static const char* kFnGetNamedSaveData = "GetNamedSaveData";
static const char* kFnSaveNamedData    = "SaveNamedData";

// ---------------------------------------------------------------------------
// Direct native entry points
// ---------------------------------------------------------------------------
typedef void* (*FnGetActiveBaseSave)(void* worldContext);
typedef int32_t (*FnGetNamedSaveData)(void* worldContext, QdName key, bool* outWasSet);
typedef void (*FnStoreEnemySaveState)(void* self, const QdGuid* guid,
                                      float respawnTime, float remainingHP);

inline void* GetActiveBaseSave(void* worldContext)
{
    FnGetActiveBaseSave fn = (FnGetActiveBaseSave)Ptr<void*>(0x00E2D100u);
    return fn ? fn(worldContext) : nullptr;
}

// SEH guard around the first call into an unverified game function.
//
// The corpus warns that an access violation here cannot be caught by a scripting
// layer's pcall, and that "reflection can return a valid-looking object of the wrong
// type; driving it is an access violation".  During bring-up, converting that
// violation into a logged failure is worth more than a clean stack: it turns a
// crashed run into a measurement.
//
// Kept as a separate function with no C++ objects in it, because __try cannot coexist
// with object unwinding in the same frame (C2712).
inline void* GetPlayerPawnGuarded(void* world, int* faulted)
{
    *faulted = 0;
    __try {
        typedef void* (*FnGetPawn)(void*, int32_t);
        FnGetPawn gp = (FnGetPawn)Ptr<void*>(0x028E0640u);
        return gp ? gp(world, 0) : nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        *faulted = 1;
        return nullptr;
    }
}

inline void* GetActiveBaseSaveGuarded(void* ctx, int* faulted)
{
    *faulted = 0;
    __try {
        return GetActiveBaseSave(ctx);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        *faulted = 1;
        return nullptr;
    }
}

inline int32_t GetNamedSaveDataNative(void* worldContext, QdName key, bool* outWasSet)
{
    FnGetNamedSaveData fn = (FnGetNamedSaveData)Ptr<void*>(0x00E2F6D0u);
    if (!fn) return 0;
    return fn(worldContext, key, outWasSet);
}

// Host-authoritative enemy persistence.  Writing this makes both machines agree
// on `bValid`/`RespawnTime`/`RemainingHP`, which is the whole of enemy
// persistence sync -- the record is three scalars.
inline void StoreEnemySaveState(void* spawner, const QdGuid& guid,
                               float respawnTime, float remainingHP)
{
    FnStoreEnemySaveState fn = (FnStoreEnemySaveState)Ptr<void*>(0x00E3BF10u);
    if (fn) fn(spawner, &guid, respawnTime, remainingHP);
}

// ---------------------------------------------------------------------------
// The save facade
// ---------------------------------------------------------------------------
// One instance, initialised once per level.  Every field is a cached *discovery*
// result, and every one is re-validated on level change because the corpus rule is
// "never trust a cached handle across a map change" -- and the object graph is
// rebuilt, so a cached UClass pointer becomes a dangling address.
struct SaveLayer {
    void*        world;           // GWorld, refreshed each init
    void*        save;            // FS_SaveSlot_C*
    void*        saveFunctions;   // Default__SaveFunctions_C
    void*        fnSaveNamedData; // UFunction* for the writer, if found

    LayoutResult layout;          // validated param layout (oracle: GetNamedSaveData)
    int          nsdOffset;       // NamedSaveData member offset in the save object
    int          actorStatesOffset;
    int          inventoryOffset;

    int          mapDataOff;      // TMap: TArray<Element> data pointer
    int          mapNumOff;       // TMap: number of elements
    int          mapStride;       // sizeof(TPair<FName,int32>)
    int          mapValueOff;     // offset of the int32 within an element

    bool         readOk;
    bool         writeOk;
    bool         enumerateOk;
    char         note[192];

    void Reset()
    {
        world = save = saveFunctions = fnSaveNamedData = nullptr;
        layout.valid = false;
        layout.props = 0;
        layout.parms_size = 0;
        nsdOffset = actorStatesOffset = inventoryOffset = -1;
        mapDataOff = mapNumOff = mapStride = mapValueOff = -1;
        readOk = writeOk = enumerateOk = false;
        note[0] = 0;
    }

    void SetNote(const char* s)
    {
        int i = 0;
        for (; s[i] && i < (int)sizeof(note) - 1; ++i) note[i] = s[i];
        note[i] = 0;
    }
};

inline SaveLayer& Save() { static SaveLayer s; return s; }

// Walk the save object's class and return the offset of a named property, or -1.
inline int FindPropertyOffset(const LayoutResult& layout, void* object, const char* name)
{
    if (!layout.valid || !object) return -1;
    // The layout's PropertyLink offset was validated on a UFunction, which is a
    // UStruct; a UClass is a UStruct too, so the same offset applies.  That is an
    // inference -- and it is checked by the fact that a missing property here makes
    // the write path refuse rather than mis-write.
    LayoutResult cls;
    cls.props = 0;
    cls.parms_size = 0;
    if (!WalkProperties(ObjClass(object), layout.cand, &cls)) return -1;
    for (int i = 0; i < cls.props; ++i) {
        if (NameEquals(cls.p[i].name, name)) return cls.p[i].offset;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// TMap decoding
// ---------------------------------------------------------------------------
// UE4.27 TMap is `TSet<TPair<Key,Value>>`:
//   TArray<Element> Data            -> { void* Data; int32 Num; int32 Max; }
//   TBitArray AllocationFlags       -> { void* Data; int32 NumBits; int32 MaxBits; }
//   int32 FirstFreeIndex; int32 NumFreeIndices;
//   int32 HashSize; FHashElementId* Hash;
// so the element array pointer is at +0 and the count at +8 of the map's storage.
//
// This is a LEARNED layout, not a documented one, so it is accepted only after a
// cross-check against the native getter: for N sampled keys, the decoded value must
// equal GetNamedSaveData's answer.  A wrong stride produces plausible garbage --
// which is exactly why the check exists.  (The previous project measured 0x14
// giving 0/64 and 0x18 giving 64/64 on the same object graph.)
struct MapProbe {
    bool     valid;
    int      dataOff;
    int      numOff;
    int      stride;
    int      valueOff;
    int      checked;
    int      agreed;
};

// Candidate element sizes for a UE4.27 TMap<FName,int32>-shaped pair, and the
// offsets of the int32 inside each.  These are NOT guesses that get used: every
// candidate is cross-checked against the native getter below and rejected unless it
// reproduces the getter's answers on at least two independent keys.
//
// Why the set is this wide: the corpus's own figures disagree.
//   * `analysis/10-gaps-register.md` C4 records `EnemySaveStates` as a fixed
//     157-byte stride (a different container: 16-byte GUID key + a 3-field record).
//   * `mod/doc/SYNC-DESIGN.md` measures `InventoryContents` at `save+0x28` with
//     element stride 0x18 and the int32 at +8, by disassembling `0x00E2EA40`.
//   * The "12-byte stride" figure in the earlier notes is the DROP CACHE key
//     `{FName sourceId, int32 dropIndex}`, not a save-container element.
// 12, 16 and 24 are all therefore plausible for SOME container, which is exactly the
// situation where a single assumed stride is the wrong instrument.
inline MapProbe ProbeMap(void* world, void* save, int mapOffset, int maxProbes)
{
    MapProbe p = {false, 0, 8, 0, 0, 0, 0};
    if (!save || mapOffset < 0) return p;

    const uint8_t* base = (const uint8_t*)save + mapOffset;
    void*   arr = *(void**)base;
    int32_t num = *(int32_t*)(base + 8);
    if (!arr || num <= 0 || num > 100000) return p;

    static const int strides[] = {12, 16, 24, 20, 32};
    static const int voffs[]   = {8, 12, 16, 20};
    for (int si = 0; si < (int)(sizeof(strides) / sizeof(strides[0])); ++si) {
        int stride = strides[si];
        for (int vi = 0; vi < (int)(sizeof(voffs) / sizeof(voffs[0])); ++vi) {
            int voff = voffs[vi];
            if (voff + 4 > stride) continue;
            int agree = 0;
            int checked = 0;
            int n = num < maxProbes ? num : maxProbes;
            for (int i = 0; i < n; ++i) {
                const uint8_t* e = (const uint8_t*)arr + (size_t)i * stride;
                QdName key = *(const QdName*)e;
                if (key.comparison_index == 0) continue;
                char name[80];
                if (FNameToString(key, name, sizeof(name)) <= 0) continue;
                bool wasSet = false;
                int32_t viaGetter = GetNamedSaveDataNative(world, key, &wasSet);
                if (!wasSet) continue;
                int32_t viaMap = *(const int32_t*)(e + voff);
                ++checked;
                if (viaGetter == viaMap) ++agree;
            }
            // Require at least two independent keys to agree, and no disagreement.
            // One agreeing key is a coincidence often enough to matter: many flags are
            // 0 or 1, so a wrong offset lands on a correct-looking value about half the
            // time on a small map.
            if (checked >= 2 && agree == checked) {
                p.valid = true;
                p.stride = stride;
                p.valueOff = voff;
                p.checked = checked;
                p.agreed = agree;
                return p;
            }
        }
    }
    p.checked = -1;
    return p;
}

// ---------------------------------------------------------------------------
// Initialisation
// ---------------------------------------------------------------------------
inline bool SaveInit(void* world)
{
    SaveLayer& s = Save();
    s.Reset();
    s.world = world;
    if (!world) { s.SetNote("no world"); return false; }

    // 1. The save object.
    //
    // THE CONTEXT MUST BE AN ACTOR, NOT THE WORLD.  `GetActiveBaseSave` is a
    // UGameplayStatics-style helper: it does `ctx->vtable[0x160]()` and then reads
    // +0x180 and +0x210 from the results.  Every caller in the game passes an actor
    // (a spawner).  Passing GWorld made the process die inside that call -- the first
    // live run that got this far ended there with no further log line, which is what
    // pointed at it.
    //
    // So: require a player pawn, and do not call at all without one. At the main menu
    // there is no pawn and no save, and that is the correct answer, not a failure path.
    QDLOG(LOG_DEBUG, "    SaveInit: GetPlayerPawn");
    int pawnFaulted = 0;
    void* pawn = GetPlayerPawnGuarded(world, &pawnFaulted);
    QDLOG(LOG_DEBUG, "    SaveInit: pawn=", hex((uintptr_t)pawn), " faulted=", dec(pawnFaulted));
    if (pawnFaulted) {
        s.SetNote("GetPlayerPawn faulted");
        QDLOG(LOG_ERROR, "GetPlayerPawn faulted (SEH)");
        return false;
    }
    if (!pawn) { s.SetNote("no player pawn yet"); return false; }

    QDLOG(LOG_DEBUG, "    SaveInit: GetActiveBaseSave");
    int faulted = 0;
    void* save = GetActiveBaseSaveGuarded(pawn, &faulted);
    QDLOG(LOG_DEBUG, "    SaveInit: save=", hex((uintptr_t)save), " faulted=", dec(faulted));
    if (faulted) {
        s.SetNote("GetActiveBaseSave faulted on the pawn context");
        QDLOG(LOG_ERROR, "GetActiveBaseSave faulted (SEH) -- context shape is wrong");
        return false;
    }
    if (!save) { s.SetNote("GetActiveBaseSave returned null"); return false; }
    if (!ObjNameIs(ObjClass(save), kClsSaveSlot)) {
        char d[160];
        ObjDescribe(save, d, sizeof(d));
        QDLOG(LOG_ERROR, "save object is not ", kClsSaveSlot, ": ", d);
        s.SetNote("save object class mismatch");
        return false;
    }
    s.save = save;
    s.readOk = true;

    // 2. The param layout, validated against the measured GetNamedSaveData frame.
    // The object-graph scan is OPT-IN and off by default.
    //
    // MEASURED: walking GUObjectArray to find a UFunction terminates the game cleanly --
    // no Application Error record, no crash dump, just the process gone. Fault-contained
    // per slot (SlotMatches wraps each probe in __try/__except) it still ends the same
    // way, so this is not a memory-violation problem that a guard can fix. Nothing in the
    // analysis needs the scan: reads go through the native getter at 0x00E2F6D0, which
    // takes a world context and a key and needs no UFunction at all.
    if (!Cfg().object_scan) {
        QDLOG(LOG_INFO, "object-graph scan is off ([Rules] ObjectScan=0); reads use the "
                        "native getter, so the mirror works and writes stay off");
        s.SetNote("object scan disabled");
        return true;
    }
    QDLOG(LOG_DEBUG, "    SaveInit: FindFunction(GetNamedSaveData)");
    // MEASURED: `GetNamedSaveData` is NOT a function of FS_SaveSlot_C on this build.
    // Asking for it with the engine's checked lookup aborts the game; asking with a
    // scan simply misses. So: try the save class, then the function library the
    // analysis says owns it (SaveFunctions_C), and if neither has it, log the real
    // surface of the save class rather than guessing a spelling.
    void* fnGet = FindFunction(save, kFnGetNamedSaveData);
    if (!fnGet) {
        QDLOG(LOG_INFO, "GetNamedSaveData is not on ", kClsSaveSlot,
              "; trying the SaveFunctions library");
        void* cdo0 = FindCdo("SaveFunctions_C");
        if (cdo0) fnGet = FindFunction(cdo0, kFnGetNamedSaveData);
    }
    QDLOG(LOG_DEBUG, "    SaveInit: fnGet=", hex((uintptr_t)fnGet));
    if (!fnGet) {
        QDLOG(LOG_WARN, "no UFunction ", kFnGetNamedSaveData, " found on the save class "
                        "or SaveFunctions_C; the parameter-layout oracle is unavailable, "
                        "so reads work but writes stay off");
        LogFunctionsOf(save, "FS_SaveSlot_C", 40);
        s.SetNote("no GetNamedSaveData UFunction");
        // Not fatal: the native getter needs no UFunction, so the mirror still works.
        return true;
    }
    QDLOG(LOG_DEBUG, "    SaveInit: DiscoverLayout");
    s.layout = DiscoverLayout(fnGet);
    QDLOG(LOG_DEBUG, "    SaveInit: DiscoverLayout valid=", dec(s.layout.valid ? 1 : 0));
    if (!s.layout.valid) {
        s.SetNote(s.layout.note);
        QDLOG(LOG_ERROR, "property layout discovery failed: ", s.layout.note);
        return true;   // reads still work; writes refuse
    }
    QDLOG(LOG_INFO, "layout discovered: PropertyLink=", hex(s.layout.cand.prop_link),
          " Next=", hex(s.layout.cand.prop_next),
          " ElemSize=", hex(s.layout.cand.elem_size),
          " Offset=", hex(s.layout.cand.offset_internal),
          " Flags=", hex(s.layout.cand.flags),
          " parms=", dec(s.layout.parms_size));
    LogSignature(kFnGetNamedSaveData, s.layout);

    // 3. The containers.
    QDLOG(LOG_DEBUG, "    SaveInit: container offsets");
    s.nsdOffset           = FindPropertyOffset(s.layout, save, kPropNamedSaveData);
    s.actorStatesOffset   = FindPropertyOffset(s.layout, save, kPropSavableActorStates);
    s.inventoryOffset     = FindPropertyOffset(s.layout, save, kPropInventoryContents);
    QDLOG(LOG_INFO, "container offsets: NamedSaveData=", dec(s.nsdOffset),
          " SavableActorStates=", dec(s.actorStatesOffset),
          " InventoryContents=", dec(s.inventoryOffset));

    // 4. Map decoding, cross-checked against the getter.
    QDLOG(LOG_DEBUG, "    SaveInit: ProbeMap");
    MapProbe mp = ProbeMap(world, save, s.nsdOffset, 8);
    if (mp.valid) {
        s.mapDataOff = mp.dataOff;
        s.mapNumOff  = mp.numOff;
        s.mapStride  = mp.stride;
        s.mapValueOff = mp.valueOff;
        s.enumerateOk = true;
        QDLOG(LOG_INFO, "NamedSaveData map: stride=", dec(mp.stride),
              " valueOff=", dec(mp.valueOff),
              " cross-check ", dec(mp.agreed), "/", dec(mp.checked), " keys agree");
    } else {
        QDLOG(LOG_WARN, "NamedSaveData map stride not confirmed; enumeration is off. "
                        "The getter still works, so flag apply/read is unaffected.");
    }

    // 5. The writer.
    QDLOG(LOG_DEBUG, "    SaveInit: FindCdo(SaveFunctions_C)");
    void* cdo = FindCdo("SaveFunctions_C");
    if (!cdo) {
        QDLOG(LOG_ERROR, "Default__SaveFunctions_C not found; the write path is off");
    } else {
        s.saveFunctions = cdo;
        s.fnSaveNamedData = FindFunction(cdo, kFnSaveNamedData);
        if (!s.fnSaveNamedData) {
            QDLOG(LOG_ERROR, "no UFunction ", kFnSaveNamedData, " on ", kCdoSaveFunctions);
        } else {
            // Its signature is different from the getter's, so it gets its own walk
            // -- but the LAYOUT is the one already validated, because the layout is
            // a property of FProperty, not of the function.
            LayoutResult wl;
            wl.props = 0;
            wl.parms_size = 0;
            if (WalkProperties(s.fnSaveNamedData, s.layout.cand, &wl) && wl.props > 0) {
                LogSignature(kFnSaveNamedData, wl);
                s.writeOk = true;
            } else {
                QDLOG(LOG_ERROR, "could not walk ", kFnSaveNamedData, " parameters");
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Reads
// ---------------------------------------------------------------------------
inline bool NsdRead(const char* key, int32_t* out, bool* wasSet)
{
    SaveLayer& s = Save();
    if (!s.readOk || !s.world) return false;
    QdName n;
    if (!FNameFromAnsi(key, &n)) return false;
    bool set = false;
    int32_t v = GetNamedSaveDataNative(s.world, n, &set);
    if (out) *out = v;
    if (wasSet) *wasSet = set;
    return set;
}

// Enumerate the map.  Only valid when `enumerateOk`; a caller that ignores the
// return value will see zero entries rather than wrong ones.
typedef void (*NsdVisitor)(const char* key, int32_t value, void* user);

inline int NsdEnumerate(NsdVisitor visit, void* user)
{
    SaveLayer& s = Save();
    if (!s.enumerateOk || !s.save) return -1;
    const uint8_t* base = (const uint8_t*)s.save + s.nsdOffset;
    const uint8_t* arr = *(const uint8_t**)base;
    int32_t num = *(const int32_t*)(base + 8);
    if (!arr || num <= 0) return 0;
    int n = 0;
    for (int32_t i = 0; i < num; ++i) {
        const uint8_t* e = arr + (size_t)i * s.mapStride;
        QdName key = *(const QdName*)e;
        if (key.comparison_index == 0) continue;
        char name[80];
        if (FNameToString(key, name, sizeof(name)) <= 0) continue;
        int32_t v = *(const int32_t*)(e + s.mapValueOff);
        if (visit) visit(name, v, user);
        ++n;
    }
    return n;
}

// ---------------------------------------------------------------------------
// The write
// ---------------------------------------------------------------------------
// Returns true only if the value was read back equal after the call.  Three gates:
//   (1) the writer UFunction exists and its params could be walked,
//   (2) the argument names are present in the signature,
//   (3) the read-back matches.
// A false return leaves the game's state as it was in every case except a call that
// succeeded but wrote something other than what we asked -- which the read-back
// reports and which the next host state push repairs.
inline bool NsdWrite(const char* key, int32_t value, char* err, int errCap)
{
    SaveLayer& s = Save();
    if (err && errCap > 0) err[0] = 0;
    if (!s.writeOk || !s.saveFunctions || !s.fnSaveNamedData) {
        if (err) { const char* m = "write path unavailable"; for (int i = 0; m[i] && i < errCap - 1; ++i) err[i] = m[i]; }
        return false;
    }

    QdName n;
    if (!FNameFromAnsi(key, &n)) {
        if (err) { const char* m = "cannot intern key name"; for (int i = 0; m[i] && i < errCap - 1; ++i) err[i] = m[i]; }
        return false;
    }

    LayoutResult wl;
    wl.props = 0;
    wl.parms_size = 0;
    if (!WalkProperties(s.fnSaveNamedData, s.layout.cand, &wl) || wl.props == 0) {
        if (err) { const char* m = "cannot walk writer params"; for (int i = 0; m[i] && i < errCap - 1; ++i) err[i] = m[i]; }
        return false;
    }

    // The WorldContext parameter's name differs by Blueprint; try the two shapes the
    // game itself uses for library functions.
    const char* wcName = nullptr;
    if (FindProp(wl, "WorldContext")) wcName = "WorldContext";
    else if (FindProp(wl, "__WorldContext")) wcName = "__WorldContext";
    else if (FindProp(wl, "WorldContextObject")) wcName = "WorldContextObject";

    const char* keyName = nullptr;
    if (FindProp(wl, "Key")) keyName = "Key";
    else if (FindProp(wl, "DataName")) keyName = "DataName";
    else if (FindProp(wl, "Name")) keyName = "Name";

    const char* valName = nullptr;
    if (FindProp(wl, "Value")) valName = "Value";
    else if (FindProp(wl, "NewValue")) valName = "NewValue";
    else if (FindProp(wl, "Amount")) valName = "Amount";

    if (!keyName || !valName) {
        LogSignature(kFnSaveNamedData, wl);
        if (err) { const char* m = "writer signature has no Key/Value parameter"; for (int i = 0; m[i] && i < errCap - 1; ++i) err[i] = m[i]; }
        return false;
    }

    ArgList args;
    if (wcName) args.AddObject(wcName, s.world);
    args.AddFName(keyName, n);
    args.AddInt(valName, value);

    ReadBack rb;
    char invErr[160];
    if (!Invoke(s.saveFunctions, s.fnSaveNamedData, wl, args, &rb, invErr, sizeof(invErr))) {
        if (err && errCap > 0) {
            int i = 0;
            for (; invErr[i] && i < errCap - 1; ++i) err[i] = invErr[i];
            err[i] = 0;
        }
        return false;
    }

    // The read-back.  This is the acceptance test.
    bool set = false;
    int32_t got = GetNamedSaveDataNative(s.world, n, &set);
    if (set && got == value) return true;

    if (err) {
        char* p = err;
        const char* m = "read-back mismatch after write";
        for (int i = 0; m[i] && p < err + errCap - 1; ++i) *p++ = m[i];
        *p = 0;
    }
    QDLOG(LOG_WARN, "NsdWrite read-back mismatch key=", key,
          " wrote=", dec(value), " got=", dec(got), " set=", dec(set ? 1 : 0));
    return false;
}

} // namespace qd



