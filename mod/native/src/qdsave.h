// qdsave.h -- the save container: acquisition, reading, and the write path.
//
// WHAT IS MEASURED AND WHAT IS DISCOVERED
// ---------------------------------------
// Measured (read out of the game's own code, not inferred):
//   * `GetActiveBaseSave(WorldContext)` at 0x00E2D100 returns GameInstance+0x210 --
//     and CREATES a save there when it is empty.  The mod therefore never calls it;
//     it reads GWorld->OwningGameInstance(+0x180)->+0x210 directly (see below).  `StoreEnemySaveState` and `GetNamedSaveData` both call it, so its
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
typedef int32_t (*FnGetNamedSaveData)(void* worldContext, QdName key, bool* outWasSet);
typedef void (*FnStoreEnemySaveState)(void* self, const QdGuid* guid,
                                      float respawnTime, float remainingHP);

// ---------------------------------------------------------------------------
// The active save, read WITHOUT side effects
// ---------------------------------------------------------------------------
// `GetActiveBaseSave` (0x00E2D100) is NOT a getter.  Disassembled:
//
//   00E2D112  call [ctx->vtable+0x160]      ; GetWorld()
//   00E2D121  mov  rbx, [world+0x180]       ; OwningGameInstance
//   00E2D136  ...IsA(GameInstance class)...
//   00E2D15C  cmp  qword [rbx+0x210], 0     ; active save present?
//   00E2D164  jne  -> return it
//   ...       otherwise CREATE a save object, store it at [rbx+0x210], init it
//
// The first versions called it every two seconds from the main menu.  So the mod itself
// planted a fresh, empty save on the GameInstance before the player chose one -- which
// is the "transient FS_SaveSlot_C at the main menu" of LIVE-TEST-LOG R18, and the most
// likely reason a run that clicked Play never showed the loaded save's keys (R19: the
// game found an active save already present and used the blank one).
//
// The native getter (0x00E2F6D0) calls GetActiveBaseSave internally, so it is only
// ever called when [GI+0x210] is already non-null, which makes that call a pure read.
static const int kWorldOwningGameInstance = 0x180;
static const int kGameInstanceActiveSave  = 0x210;

inline void* GWorldPtr() { return *(void**)Ptr<void*>(0x04BA86B0u); }

inline void* GameInstanceOf(void* world)
{
    if (!world) return nullptr;
    void* gi = *(void**)((uint8_t*)world + kWorldOwningGameInstance);
    return LooksLikeUObject(gi) ? gi : nullptr;
}

inline void* ActiveSaveNoCreate(void* world)
{
    void* gi = GameInstanceOf(world);
    if (!gi) return nullptr;
    void* s = *(void**)((uint8_t*)gi + kGameInstanceActiveSave);
    return LooksLikeUObject(s) ? s : nullptr;
}

inline int32_t GetNamedSaveDataNativeGuarded(void* worldContext, QdName key, bool* outWasSet,
                                             int* faulted)
{
    *faulted = 0;
    __try {
        FnGetNamedSaveData fn = (FnGetNamedSaveData)Ptr<void*>(0x00E2F6D0u);
        return fn(worldContext, key, outWasSet);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        *faulted = 1;
        return 0;
    }
}

// The safe form: refuses (returns 0, *outWasSet=false) when there is no active save,
// because calling through would create one.
inline int32_t GetNamedSaveDataNative(void* world, QdName key, bool* outWasSet)
{
    *outWasSet = false;
    if (!ActiveSaveNoCreate(world)) return 0;
    int faulted = 0;
    int32_t v = GetNamedSaveDataNativeGuarded(world, key, outWasSet, &faulted);
    if (faulted) { *outWasSet = false; return 0; }
    return v;
}

// Host-authoritative enemy persistence: the record is three scalars.
inline void StoreEnemySaveState(void* spawner, const QdGuid& guid,
                               float respawnTime, float remainingHP)
{
    FnStoreEnemySaveState fn = (FnStoreEnemySaveState)Ptr<void*>(0x00E3BF10u);
    if (fn) fn(spawner, &guid, respawnTime, remainingHP);
}

// ---------------------------------------------------------------------------
// The save facade
// ---------------------------------------------------------------------------
struct SaveLayer {
    void*        world;           // GWorld at init
    void*        save;            // the active save object
    void*        saveFunctions;   // Default__SaveFunctions_C
    void*        fnGetNamedSaveData;
    void*        fnSaveNamedData;

    LayoutResult getSig;          // GetNamedSaveData signature
    LayoutResult setSig;          // SaveNamedData signature
    int          nsdOffset;       // NamedSaveData member offset in the save object
    int          actorStatesOffset;
    int          inventoryOffset;

    int          mapStride;       // TSet element size for TPair<FName,int32>
    int          mapValueOff;     // offset of the int32 within an element

    bool         readOk;
    bool         writeOk;
    bool         enumerateOk;
    char         note[192];

    void Reset()
    {
        world = save = saveFunctions = fnGetNamedSaveData = fnSaveNamedData = nullptr;
        getSig.valid = setSig.valid = false;
        getSig.props = setSig.props = 0;
        nsdOffset = actorStatesOffset = inventoryOffset = -1;
        mapStride = mapValueOff = -1;
        readOk = writeOk = enumerateOk = false;
        note[0] = 0;
    }

    void SetNote(const char* s) { CopyNote(note, sizeof(note), s); }
};

inline SaveLayer& Save() { static SaveLayer s; return s; }

// ---------------------------------------------------------------------------
// TMap decoding
// ---------------------------------------------------------------------------
// UE4.27 TMap is a TSet<TPair<K,V>> whose storage starts with a TSparseArray: the
// element array pointer at +0 and its Num at +8.  A TSetElement is
// {TPair (FName 8 + int32 4), HashNextId int32, HashIndex int32} = 20 bytes, but the
// stride is still LEARNED, then accepted only if it reproduces the native getter on
// at least two keys with no disagreement.
struct MapProbe {
    bool valid;
    int  stride;
    int  valueOff;
    int  checked;
    int  agreed;
};

inline MapProbe ProbeMap(void* world, void* save, int mapOffset, int maxProbes)
{
    MapProbe p = {false, 0, 0, 0, 0};
    if (!save || mapOffset < 0) return p;

    const uint8_t* base = (const uint8_t*)save + mapOffset;
    void*   arr = *(void**)base;
    int32_t num = *(int32_t*)(base + 8);
    if (!arr || num <= 0 || num > 100000) return p;

    static const int strides[] = {20, 24, 16, 12, 32};
    static const int voffs[]   = {8, 12, 16};
    for (int si = 0; si < (int)(sizeof(strides) / sizeof(strides[0])); ++si) {
        int stride = strides[si];
        for (int vi = 0; vi < (int)(sizeof(voffs) / sizeof(voffs[0])); ++vi) {
            int voff = voffs[vi];
            if (voff + 4 > stride) continue;
            int agree = 0, checked = 0;
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
                ++checked;
                if (viaGetter == *(const int32_t*)(e + voff)) ++agree;
            }
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
// Reflection bring-up (once per save object)
// ---------------------------------------------------------------------------
// Discovers the FProperty layout against SaveFunctions_C's own UFunctions and resolves
// the getter/setter signatures.  Every step logs; every failure leaves writes off.
inline void SaveReflect(SaveLayer& s)
{
    void* cdo = FindCdo("SaveFunctions_C");
    if (!cdo) {
        QDLOG(LOG_ERROR, "Default__SaveFunctions_C not found; writes stay off");
        return;
    }
    s.saveFunctions = cdo;
    s.fnGetNamedSaveData = FindFunction(cdo, kFnGetNamedSaveData);
    s.fnSaveNamedData    = FindFunction(cdo, kFnSaveNamedData);
    void* fnAdd          = FindFunction(cdo, "AddItemToInventory");
    void* fnAdv          = FindFunction(cdo, "SaveNamedData_Advanced");
    QDLOG(LOG_INFO, "SaveFunctions_C: GetNamedSaveData=", hex((uintptr_t)s.fnGetNamedSaveData),
          " SaveNamedData=", hex((uintptr_t)s.fnSaveNamedData),
          " AddItemToInventory=", hex((uintptr_t)fnAdd));
    if (!s.fnGetNamedSaveData || !s.fnSaveNamedData) LogFunctionsOf(cdo, "SaveFunctions_C", 40);

    if (!Lay().valid) {
        void* fns[4] = {s.fnGetNamedSaveData, s.fnSaveNamedData, fnAdd, fnAdv};
        if (!DiscoverLayout(fns, 4)) {
            QDLOG(LOG_ERROR, "property layout: FAIL (", Lay().note, ")");
            return;
        }
        const Layout& L = Lay();
        QDLOG(LOG_INFO, "property layout: PASS on ", dec(L.validated_on), " functions"
              " ChildProps=", hex(L.child_props), " Name=", hex(L.field_name),
              " ElemSize=", hex(L.elem_size), " Flags=", hex(L.flags),
              " Offset=", hex(L.offset_internal));
    }

    s.getSig = Signature(s.fnGetNamedSaveData);
    s.setSig = Signature(s.fnSaveNamedData);
    if (s.getSig.valid) LogSignature(kFnGetNamedSaveData, s.getSig);
    if (s.setSig.valid) LogSignature(kFnSaveNamedData, s.setSig);
    else QDLOG(LOG_ERROR, "SaveNamedData signature: ", s.setSig.note);

    // The writer needs a NameProperty key and an IntProperty value among its inputs.
    if (s.setSig.valid &&
        FindPropOfType(s.setSig, "NameProperty", CPF_OutParm | CPF_ReturnParm) &&
        FindPropOfType(s.setSig, "IntProperty", CPF_OutParm | CPF_ReturnParm)) {
        s.writeOk = true;
    } else if (s.setSig.valid) {
        QDLOG(LOG_ERROR, "SaveNamedData has no (NameProperty, IntProperty) input pair; writes off");
    }
}

// ---------------------------------------------------------------------------
// Initialisation -- called when the active save object changes
// ---------------------------------------------------------------------------
inline bool SaveInit(void* world, void* save)
{
    SaveLayer& s = Save();
    s.Reset();
    s.world = world;
    if (!world) { s.SetNote("no world"); return false; }
    if (!save)  { s.SetNote("no active save on the GameInstance yet"); return false; }

    char d[160];
    ObjDescribe(save, d, sizeof(d));
    if (!ObjNameIs(ObjClass(save), kClsSaveSlot)) {
        // Not fatal: a subclass of the base save is still read through the getter.
        QDLOG(LOG_WARN, "active save is not ", kClsSaveSlot, ": ", d);
    }
    s.save = save;
    s.readOk = true;
    QDLOG(LOG_INFO, "active save: ", d);

    SaveReflect(s);

    if (Lay().valid) {
        void* cls = ObjClass(save);
        s.nsdOffset         = FindPropertyOffsetIn(cls, kPropNamedSaveData);
        s.actorStatesOffset = FindPropertyOffsetIn(cls, kPropSavableActorStates);
        s.inventoryOffset   = FindPropertyOffsetIn(cls, kPropInventoryContents);
        QDLOG(LOG_INFO, "container offsets: NamedSaveData=", dec(s.nsdOffset),
              " SavableActorStates=", dec(s.actorStatesOffset),
              " InventoryContents=", dec(s.inventoryOffset));
        if (s.nsdOffset < 0) LogPropertiesOf(cls, "save", 80);
        MapProbe mp = ProbeMap(world, save, s.nsdOffset, 16);
        if (mp.valid) {
            s.mapStride = mp.stride;
            s.mapValueOff = mp.valueOff;
            s.enumerateOk = true;
            QDLOG(LOG_INFO, "NamedSaveData map: stride=", dec(mp.stride),
                  " valueOff=", dec(mp.valueOff),
                  " cross-check ", dec(mp.agreed), "/", dec(mp.checked), " keys agree");
        } else {
            QDLOG(LOG_WARN, "NamedSaveData map stride not confirmed; enumeration off "
                            "(single-key reads unaffected)");
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Reads
// ---------------------------------------------------------------------------
inline bool NsdReadName(QdName n, int32_t* out, bool* wasSet)
{
    SaveLayer& s = Save();
    if (!s.readOk) return false;
    void* world = GWorldPtr();
    if (!world) return false;
    bool set = false;
    int32_t v = GetNamedSaveDataNative(world, n, &set);
    if (out) *out = v;
    if (wasSet) *wasSet = set;
    return set;
}

inline bool NsdRead(const char* key, int32_t* out, bool* wasSet)
{
    QdName n;
    if (!FNameFromAnsi(key, &n)) return false;
    return NsdReadName(n, out, wasSet);
}

typedef void (*NsdVisitor)(const char* key, int32_t value, void* user);

inline int NsdEnumerateGuarded(const uint8_t* arr, int32_t num, int stride, int voff,
                               NsdVisitor visit, void* user)
{
    int n = 0;
    __try {
        for (int32_t i = 0; i < num; ++i) {
            const uint8_t* e = arr + (size_t)i * stride;
            QdName key = *(const QdName*)e;
            if (key.comparison_index == 0) continue;
            char name[80];
            if (FNameToString(key, name, sizeof(name)) <= 0) continue;
            int32_t v = *(const int32_t*)(e + voff);
            if (visit) visit(name, v, user);
            ++n;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
    return n;
}

inline int NsdEnumerate(NsdVisitor visit, void* user)
{
    SaveLayer& s = Save();
    if (!s.enumerateOk || !s.save) return -1;
    if (ActiveSaveNoCreate(GWorldPtr()) != s.save) return -1;   // replaced under us
    const uint8_t* base = (const uint8_t*)s.save + s.nsdOffset;
    const uint8_t* arr = *(const uint8_t**)base;
    int32_t num = *(const int32_t*)(base + 8);
    if (!arr || num <= 0) return 0;
    if (num > 100000) return -1;
    return NsdEnumerateGuarded(arr, num, s.mapStride, s.mapValueOff, visit, user);
}

// ---------------------------------------------------------------------------
// The write
// ---------------------------------------------------------------------------
// SaveFunctions_C.SaveNamedData through ProcessEvent, parameters chosen by TYPE:
// the NameProperty input is the key, the IntProperty input the value, an
// ObjectProperty input (the Blueprint library's __WorldContext) gets GWorld.  Any other
// input stays zero, which for a bool "mode"/"only if" pin is the plain-set default.
// Accepted only if the native getter reads the value back.
inline bool NsdWrite(const char* key, int32_t value, char* err, int errCap)
{
    SaveLayer& s = Save();
    if (err && errCap > 0) err[0] = 0;
    if (!s.writeOk || !s.saveFunctions || !s.fnSaveNamedData) {
        ErrSet(err, errCap, "write path unavailable");
        return false;
    }
    void* world = GWorldPtr();
    if (!world || ActiveSaveNoCreate(world) != s.save) {
        ErrSet(err, errCap, "active save changed; refusing until re-init");
        return false;
    }

    QdName n;
    if (!FNameFromAnsi(key, &n)) { ErrSet(err, errCap, "cannot intern key name"); return false; }

    const uint64_t kSkip = CPF_OutParm | CPF_ReturnParm;
    const PropDesc* pKey = FindPropOfType(s.setSig, "NameProperty", kSkip);
    const PropDesc* pVal = FindPropOfType(s.setSig, "IntProperty", kSkip);
    const PropDesc* pCtx = FindPropOfType(s.setSig, "ObjectProperty", kSkip);
    if (!pKey || !pVal) { ErrSet(err, errCap, "writer signature has no key/value"); return false; }

    ArgList args;
    if (pCtx) args.AddObject(pCtx->name, world);
    args.AddFName(pKey->name, n);
    args.AddInt(pVal->name, value);

    ReadBack rb;
    if (!Invoke(s.saveFunctions, s.fnSaveNamedData, s.setSig, args, &rb, err, errCap)) {
        return false;
    }

    bool set = false;
    int32_t got = GetNamedSaveDataNative(world, n, &set);
    if (set && got == value) return true;

    ErrSet(err, errCap, "read-back mismatch after write");
    QDLOG(LOG_WARN, "NsdWrite read-back mismatch key=", key,
          " wrote=", dec(value), " got=", dec(got), " set=", dec(set ? 1 : 0));
    return false;
}

} // namespace qd
