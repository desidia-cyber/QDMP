// qdwrite.h -- the two reflective write APIs, and the read-back oracles that decide
// whether they may run at all.
//
// WHY THIS IS BESIDE qdsave.h AND NOT INSIDE IT
// ---------------------------------------------
// qdsave.h's NsdWrite already does a no-op round trip (LIVE-TEST-LOG R21).  It is
// proven, and it is the fallback.  What it does NOT do is prove that the property
// offsets it used are the engine's own: it picks a parameter by TYPE and trusts the
// walk.  This header is the verified upgrade -- the same ProcessEvent dispatch, but
// gated on a measured frame that the walk must reproduce.  Rewriting the proven path
// would trade a working fallback for an unproven one, so both live here and the flag
// plumbing chooses.
//
// THE TWO ORACLES, AND WHY THEY ARE DIFFERENT
// -------------------------------------------
// API-GAPS section 1 proves the NamedSaveData writer differently from section 2 proving
// the inventory writer, and the difference is not stylistic:
//
//   * NamedSaveData: the measured frame {FName@0, bool@8, i32@0xC} belongs to the
//     reflective helper at 0x00E60250.  The property-chain walk is accepted only if
//     the identical walk over `GetNamedSaveData` reproduces that frame byte for byte.
//     A walk that is off by one field, by four bytes, or by declaration order cannot
//     satisfy it by accident.  If it does NOT reproduce it, the walk is not trusted and
//     every write is refused.  That refusal is the correct outcome: a wrong
//     ProcessEvent frame returns successfully and changes nothing (DollsMP F-12.17).
//
//   * Inventory: there is no measured frame for `AddItemToInventory`.  The oracle is
//     the native counter at 0x00E2EA40, read before and after the call; the count must
//     rise by exactly the requested amount.  A frame that is off by a field moves the
//     wrong number and the read-back catches it.
//
// A third container, `SavableActorStates`, has neither a measured offset nor a
// measured writer (save+0x168 is a LOW/MEDIUM lead).  Section C below therefore
// implements only the READ-BACK ORACLE the design asks for -- a probe that reads a
// per-actor value through the game's own reflective getter and logs it.  It contains
// no write at all, deliberately: a guessed write to an unproven container is the one
// mistake this document exists to prevent.
//
// WHAT IS UNIT-TESTABLE AND WHY IT IS SEPARATED
// ---------------------------------------------
// The layout maths (walking a parameter list into offsets, validating it against an
// expected frame, placing a value at its declared offset, refusing an overrun) is the
// part that must be right before a game is involved.  It is pure: no UObject, no
// ProcessEvent, no globals.  test_writeapi.cpp drives it with the measured frame and
// with deliberately broken ones.  The live half only feeds it recovered layouts.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdint.h>
#include <string.h>

#include "qdlog.h"
#include "qdreflect.h"
#include "qdsave.h"

namespace qd {

// ===========================================================================
// Part 1 -- the pure layout and frame maths
// ===========================================================================
namespace wr {

// A Blueprint function carries its locals as properties too, so a parameter list is
// always the CPF_Parm subset.  This cap is far above any SaveFunctions_C signature; a
// list that exceeds it is refused rather than truncated, because truncation is how the
// first version of qdreflect lost NamedSaveData at 32 properties.
static const int kMaxParams = 48;
static const int kParamName = 48;
static const int kMaxFrame  = 1024;

// The FProperty subclasses that matter to these three call sites.  The tag, not the
// reflected class pointer, is what the pure half may depend on.
enum PKind {
    PK_UNKNOWN = 0,
    PK_NAME,     // NameProperty    -- FName, 8 bytes
    PK_BOOL,     // BoolProperty    -- 1 byte
    PK_INT,      // IntProperty     -- int32, 4 bytes
    PK_OBJECT,   // ObjectProperty / ClassProperty -- UObject*, 8 bytes
    PK_SOFT,     // SoftObjectProperty -- FSoftObjectPtr, 24 or 32 bytes
    PK_STR,      // StrProperty     -- FString, 16 bytes on Win64
    PK_STRUCT,   // StructProperty  -- value inline, size from ElementSize
    PK_FLOAT,    // FloatProperty
    PK_OTHER
};

struct Field {
    char     name[kParamName];
    PKind    kind;
    int32_t  offset;
    int32_t  size;
    uint64_t flags;
};

struct ParamLayout {
    int   n;
    int   parms_size;
    Field p[kMaxParams];
};

inline PKind KindOfType(const char* type)
{
    if (!type) return PK_UNKNOWN;
    if (NameEquals(type, "NameProperty"))   return PK_NAME;
    if (NameEquals(type, "BoolProperty"))   return PK_BOOL;
    if (NameEquals(type, "IntProperty"))    return PK_INT;
    if (NameEquals(type, "ObjectProperty") ||
        NameEquals(type, "ClassProperty") ||
        NameEquals(type, "WeakObjectProperty") ||
        NameEquals(type, "InterfaceProperty")) return PK_OBJECT;
    if (NameEquals(type, "SoftObjectProperty") ||
        NameEquals(type, "SoftClassProperty")) return PK_SOFT;
    if (NameEquals(type, "StrProperty"))    return PK_STR;
    if (NameEquals(type, "StructProperty")) return PK_STRUCT;
    if (NameEquals(type, "FloatProperty"))  return PK_FLOAT;
    return PK_OTHER;
}

// The label used in the oracle log.  Short on purpose: the measured frame is quoted as
// {FName@0, bool@8, i32@0xC} in API-GAPS, and the log has to be comparable to it at a
// glance rather than through a decoder ring.
inline const char* KindLabel(PKind k)
{
    switch (k) {
    case PK_NAME:   return "FName";
    case PK_BOOL:   return "bool";
    case PK_INT:    return "i32";
    case PK_OBJECT: return "object";
    case PK_SOFT:   return "softref";
    case PK_STR:    return "str";
    case PK_STRUCT: return "struct";
    case PK_FLOAT:  return "f32";
    case PK_OTHER:  return "other";
    default:        return "?";
    }
}

inline bool NamePrefix(const char* n, const char* p)
{
    int i = 0;
    for (; p[i] && n[i] == p[i]; ++i) {}
    return p[i] == 0;
}

inline bool NameContains(const char* n, const char* w)
{
    for (int i = 0; n[i]; ++i) {
        int k = 0;
        for (; w[k] && n[i + k] == w[k]; ++k) {}
        if (w[k] == 0) return true;
    }
    return false;
}

inline const Field* FindField(const ParamLayout& l, const char* name)
{
    for (int i = 0; i < l.n; ++i) {
        if (NameEquals(l.p[i].name, name)) return &l.p[i];
    }
    return nullptr;
}

// The first input of a kind.  Out and return parameters are skipped: writing into
// them would overwrite what the Blueprint is about to hand back.
inline const Field* FirstInput(const ParamLayout& l, PKind kind,
                               const Field* skip = nullptr)
{
    for (int i = 0; i < l.n; ++i) {
        const Field& p = l.p[i];
        if (&p == skip) continue;
        if (p.flags & (CPF_OutParm | CPF_ReturnParm)) continue;
        if (p.kind == kind) return &p;
    }
    return nullptr;
}

inline const Field* InputNamed(const ParamLayout& l, PKind kind, const char* name)
{
    for (int i = 0; i < l.n; ++i) {
        const Field& p = l.p[i];
        if (p.flags & (CPF_OutParm | CPF_ReturnParm)) continue;
        if (p.kind == kind && NamePrefix(p.name, name)) return &p;
    }
    return nullptr;
}

// A BlueprintFunctionLibrary pins its world context as `__WorldContext`.  Choosing it
// by name first matters for the inventory call, which has a real object parameter
// (the item) as well; taking the first object parameter there would pass the item as
// the world context.  An unnamed single object parameter is ambiguous and is left
// alone; only when a second object parameter proves which one is surplus is the first
// treated as the context.
inline const Field* WorldContextInput(const ParamLayout& l)
{
    const Field* first = nullptr;
    int objects = 0;
    for (int i = 0; i < l.n; ++i) {
        const Field& p = l.p[i];
        if (p.flags & (CPF_OutParm | CPF_ReturnParm)) continue;
        if (p.kind != PK_OBJECT) continue;
        if (NameContains(p.name, "WorldContext") || NamePrefix(p.name, "__World"))
            return &p;
        if (!first) first = &p;
        ++objects;
    }
    return objects > 1 ? first : nullptr;
}

inline const Field* ReturnField(const ParamLayout& l)
{
    for (int i = 0; i < l.n; ++i) {
        if (l.p[i].flags & CPF_ReturnParm) return &l.p[i];
    }
    return nullptr;
}

inline char* PutStr(char* d, const char* e, const char* s) { return detail::Cat(d, e, s); }
inline char* PutDec(char* d, const char* e, int64_t v)     { return detail::CatDec(d, e, v); }

// A compact rendering of a recovered frame: {FName@0 size 8, bool@8 size 1, ...}.
// Written with the log's own bounded appenders so it cannot allocate or overflow.
inline void DescribeLayout(const ParamLayout& l, char* out, int cap)
{
    if (!out || cap <= 0) return;
    char* d = out;
    const char* e = out + cap - 1;
    d = PutStr(d, e, "{");
    for (int i = 0; i < l.n; ++i) {
        if (i) d = PutStr(d, e, ", ");
        d = PutStr(d, e, KindLabel(l.p[i].kind));
        d = PutStr(d, e, "@");
        d = PutDec(d, e, l.p[i].offset);
        d = PutStr(d, e, " size ");
        d = PutDec(d, e, l.p[i].size);
    }
    d = PutStr(d, e, "}");
    *d = 0;
}

// --- the oracle -------------------------------------------------------------
// API-GAPS section 1, step 3.  The three fields are compared in declaration order as
// well as by offset: "differs by order" is a real failure mode for a property chain
// whose declaration order was changed by a Blueprint recompile, and a frame built in
// the wrong order moves two values into each other's slots.
inline bool MatchesNameDataOracle(const ParamLayout& l, char* err, int cap)
{
    static const struct { PKind kind; int32_t offset; int32_t size; } kWant[3] = {
        { PK_NAME, 0,  8 },
        { PK_BOOL, 8,  1 },
        { PK_INT,  12, 4 },
    };

    if (l.n != 3) {
        char d[160];
        DescribeLayout(l, d, sizeof(d));
        ErrSet(err, cap, "expected {FName@0, bool@8, i32@12}; frame is ", d);
        return false;
    }
    for (int i = 0; i < 3; ++i) {
        if (l.p[i].kind != kWant[i].kind ||
            l.p[i].offset != kWant[i].offset ||
            l.p[i].size != kWant[i].size) {
            char d[160];
            DescribeLayout(l, d, sizeof(d));
            ErrSet(err, cap, "frame does not reproduce {FName@0, bool@8, i32@12}: ", d);
            return false;
        }
    }
    if (err && cap > 0) err[0] = 0;
    return true;
}

// --- the frame --------------------------------------------------------------
// A frame is a fixed buffer plus the layout it was built from.  Every write goes
// through the layout's declared offset, so a caller can never place a value at a
// literal offset of its own choosing.  Three refusals are structural:
//   * ParmsSize larger than the buffer,
//   * a parameter that extends past ParmsSize,
//   * a value larger than the parameter it is written into.
struct Frame {
    alignas(16) uint8_t bytes[kMaxFrame];
    int            parms_size;
    int            capacity;
    const ParamLayout* layout;
    bool           ok;
    char           why[128];
};

inline bool FrameInit(Frame& f, const ParamLayout& l, int capacity)
{
    f.parms_size = l.parms_size;
    f.capacity = capacity;
    f.layout = &l;
    f.ok = false;
    f.why[0] = 0;
    memset(f.bytes, 0, sizeof(f.bytes));

    if (capacity < 0 || capacity > kMaxFrame) {
        ErrSet(f.why, sizeof(f.why), "capacity is outside the frame buffer", "");
        return false;
    }
    if (l.parms_size < 0 || l.parms_size > capacity) {
        ErrSet(f.why, sizeof(f.why), "ParmsSize overruns the frame buffer", "");
        return false;
    }
    for (int i = 0; i < l.n; ++i) {
        const Field& p = l.p[i];
        if (p.offset < 0 || p.size < 0 || p.offset + p.size > l.parms_size) {
            ErrSet(f.why, sizeof(f.why), "parameter lies outside ParmsSize: ", p.name);
            return false;
        }
    }
    f.ok = true;
    return true;
}

inline bool FramePut(Frame& f, const char* name, const void* src, int len)
{
    if (!f.ok || !f.layout) {
        ErrSet(f.why, sizeof(f.why), "frame is not initialised", "");
        return false;
    }
    const Field* p = FindField(*f.layout, name);
    if (!p) {
        ErrSet(f.why, sizeof(f.why), "no such parameter: ", name);
        f.ok = false;
        return false;
    }
    if (len < 0 || len > p->size) {
        ErrSet(f.why, sizeof(f.why), "value larger than the parameter: ", name);
        f.ok = false;
        return false;
    }
    if (p->offset + len > f.parms_size || p->offset + len > f.capacity) {
        ErrSet(f.why, sizeof(f.why), "write overruns the frame: ", name);
        f.ok = false;
        return false;
    }
    if (len > 0) memcpy(f.bytes + p->offset, src, (size_t)len);
    return true;
}

inline bool FramePutInt(Frame& f, const char* name, int32_t v)
{
    return FramePut(f, name, &v, 4);
}
inline bool FramePutBool(Frame& f, const char* name, bool v)
{
    uint8_t b = v ? 1 : 0;
    return FramePut(f, name, &b, 1);
}
inline bool FramePutName(Frame& f, const char* name, const QdName& v)
{
    return FramePut(f, name, &v, 8);
}
inline bool FramePutObject(Frame& f, const char* name, void* v)
{
    return FramePut(f, name, &v, 8);
}

// Convert a recovered UFunction signature into the pure layout.  The signature has
// already been validated against NumParms/ParmsSize/ReturnValueOffset by
// qdreflect's Signature(); this only re-tags the properties so the maths above can be
// tested without the game.
inline bool FromReflected(const LayoutResult& r, ParamLayout* out,
                          char* err, int cap)
{
    if (!out) {
        ErrSet(err, cap, "null layout", "");
        return false;
    }
    out->n = 0;
    out->parms_size = r.parms_size;
    if (!r.valid) {
        ErrSet(err, cap, "reflected signature is invalid: ", r.note);
        return false;
    }
    if (r.props > kMaxParams) {
        ErrSet(err, cap, "more parameters than the write path supports", "");
        return false;
    }
    for (int i = 0; i < r.props; ++i) {
        Field& p = out->p[out->n];
        int k = 0;
        for (; r.p[i].name[k] && k < kParamName - 1; ++k) p.name[k] = r.p[i].name[k];
        p.name[k] = 0;
        p.kind = KindOfType(r.p[i].type);
        p.offset = r.p[i].offset;
        p.size = r.p[i].size;
        p.flags = r.p[i].flags;
        ++out->n;
    }
    return true;
}

} // namespace wr

// ===========================================================================
// Part 2 -- live resolution, shared by both writers
// ===========================================================================
// The FProperty layout is discovered once, against the engine's own UFunction
// bookkeeping (qdreflect's DiscoverLayout), and every recovered list is converted
// through the same walk.  Nothing below reads a property at a typed offset.
struct WriteLayer {
    bool  tried;
    void* cdo;
    void* fnGet;
    void* fnSet;
    void* fnAdd;

    wr::ParamLayout getLay;
    wr::ParamLayout setLay;
    wr::ParamLayout addLay;

    bool  getOk;
    bool  setOk;
    bool  addOk;
    bool  oracleOk;
    bool  ready;
    char  note[192];
};

inline WriteLayer& Wl()
{
    static WriteLayer w = {};
    return w;
}

inline bool SigOf(void* fn, wr::ParamLayout* out, const char* label)
{
    LayoutResult r = Signature(fn);
    char err[192];
    if (!wr::FromReflected(r, out, err, sizeof(err))) {
        QDLOG(LOG_ERROR, "write api: ", label, " signature: ",
              (r.valid ? err : r.note));
        return false;
    }
    LogSignature(label, r);
    return true;
}

// Resolve the CDO and the three UFunctions, then run the section 1 oracle.  `tried`
// is left true once the CDO exists: a failure at this point (a function missing, or a
// frame that does not match) is a property of this build, not a transient state, and
// re-walking GObjects every frame would only hide it behind log noise.
inline bool ResolveWrite(WriteLayer& w, void* cdo)
{
    w.tried = true;
    w.cdo = cdo;
    w.getOk = w.setOk = w.addOk = w.oracleOk = w.ready = false;
    w.note[0] = 0;

    w.fnGet = FindFunction(cdo, "GetNamedSaveData");
    w.fnSet = FindFunction(cdo, "SaveNamedData");
    w.fnAdd = FindFunction(cdo, "AddItemToInventory");
    QDLOG(LOG_INFO, "write api: GetNamedSaveData=", hex((uintptr_t)w.fnGet),
          " SaveNamedData=", hex((uintptr_t)w.fnSet),
          " AddItemToInventory=", hex((uintptr_t)w.fnAdd));

    if (!w.fnGet || !w.fnSet) {
        CopyNote(w.note, sizeof(w.note),
                 "SaveFunctions_C is missing GetNamedSaveData or SaveNamedData");
        QDLOG(LOG_ERROR, "write api: ", w.note, "; writes stay off");
        LogFunctionsOf(cdo, "SaveFunctions_C", 40);
        return false;
    }

    // The walk itself is proven against the engine's own NumParms/ParmsSize/
    // ReturnValueOffset before any recovered offset is used.  A failure here is the
    // "write path unavailable" state, never a crash and never a guess.
    if (!Lay().valid) {
        void* fns[3] = { w.fnGet, w.fnSet, w.fnAdd };
        int n = w.fnAdd ? 3 : 2;
        if (!DiscoverLayout(fns, n)) {
            CopyNote(w.note, sizeof(w.note), Lay().note);
            QDLOG(LOG_ERROR, "write api: property layout FAIL (", Lay().note,
                  "); writes refused");
            return false;
        }
    }

    w.getOk = SigOf(w.fnGet, &w.getLay, "GetNamedSaveData");
    w.setOk = SigOf(w.fnSet, &w.setLay, "SaveNamedData");
    if (w.fnAdd) w.addOk = SigOf(w.fnAdd, &w.addLay, "AddItemToInventory");

    if (w.getOk) {
        char desc[192];
        char why[192];
        wr::DescribeLayout(w.getLay, desc, sizeof(desc));
        w.oracleOk = wr::MatchesNameDataOracle(w.getLay, why, sizeof(why));
        QDLOG(w.oracleOk ? LOG_INFO : LOG_ERROR,
              "NAMED-SAVE ORACLE ", (w.oracleOk ? "PASS" : "FAIL"),
              ": GetNamedSaveData frame = ", desc);
        if (!w.oracleOk) {
            QDLOG(LOG_ERROR, "write api: ", why,
                  "; SaveNamedData offsets cannot be trusted, so writes are refused");
            CopyNote(w.note, sizeof(w.note),
                     "NamedSaveData oracle did not reproduce the measured frame");
        }
    } else {
        CopyNote(w.note, sizeof(w.note),
                 "GetNamedSaveData signature unavailable; the oracle cannot run");
    }

    w.ready = w.oracleOk && w.setOk;
    if (!w.ready && w.note[0] == 0) {
        CopyNote(w.note, sizeof(w.note), "SaveNamedData layout not proven");
    }
    QDLOG(w.ready ? LOG_INFO : LOG_WARN, "write api: ready=", dec(w.ready ? 1 : 0),
          " oracle=", dec(w.oracleOk ? 1 : 0), " set=", dec(w.setOk ? 1 : 0),
          " add=", dec(w.addOk ? 1 : 0));
    return w.ready;
}

// True only when the measured frame has been reproduced AND SaveNamedData's own
// signature validated.  Callers treat false as "refuse the write", never as "retry
// differently": the note says which proof is missing.
//
// The CDO lookup is the only GObjects scan, and it happens only while the CDO has not
// been found.  At the main menu the SaveFunctions package may not be loaded yet, so a
// missing CDO is a transient state and is retried; a found CDO with a failing oracle is
// definitive and is not.
inline bool WrReady()
{
    WriteLayer& w = Wl();
    if (w.tried && w.cdo && ObjectIsLive(w.cdo)) return w.ready;

    void* cdo = FindCdo("SaveFunctions_C");
    if (!cdo) {
        w.ready = false;
        CopyNote(w.note, sizeof(w.note), "Default__SaveFunctions_C not found");
        return false;
    }
    return ResolveWrite(w, cdo);
}

// ===========================================================================
// Part 3 -- API-GAPS section 1: the NamedSaveData write
// ===========================================================================
// Steps 1-5 are implemented here: resolve the CDO by name, find the writer, recover
// the layout and gate it on the section 1 oracle, build the frame, dispatch through
// ProcessEvent, and require the native getter (0x00E2F6D0's safe wrapper) to read back
// `wasSet == true` and the value written.
//
// Step 6 (bind FS_NamedSaveDataChange and require it to fire) is NOT here.  It needs a
// delegate hook, and a multiprocess delegate binding that is not proven live belongs in
// the flag plumbing stream, not in a header that can be unit-tested.  The read-back is
// the memory proof; the delegate is the proof that the game noticed.
inline bool WrSetFlag(const char* key, int32_t value, char* err, int errCap)
{
    if (err && errCap > 0) err[0] = 0;
    if (!key || !key[0]) { ErrSet(err, errCap, "empty key", ""); return false; }
    if (!WrReady()) {
        ErrSet(err, errCap, "write path unavailable: ", Wl().note);
        return false;
    }

    WriteLayer& w = Wl();
    void* world = GWorldPtr();
    if (!world) { ErrSet(err, errCap, "no world", ""); return false; }
    if (!ActiveSaveNoCreate(world)) {
        // The native getter would CREATE a save if called with none present
        // (qdsave.h).  Refusing here keeps a mid-session write from planting a blank.
        ErrSet(err, errCap, "no active save object", ""); return false;
    }

    QdName n;
    if (!FNameFromAnsi(key, &n)) {
        ErrSet(err, errCap, "cannot intern the key name", "");
        return false;
    }

    const wr::Field* pKey = wr::InputNamed(w.setLay, wr::PK_NAME, "Key");
    if (!pKey) pKey = wr::FirstInput(w.setLay, wr::PK_NAME);
    const wr::Field* pVal = wr::InputNamed(w.setLay, wr::PK_INT, "Value");
    if (!pVal) pVal = wr::FirstInput(w.setLay, wr::PK_INT);
    const wr::Field* pCtx = wr::WorldContextInput(w.setLay);
    if (!pKey) { ErrSet(err, errCap, "SaveNamedData has no NameProperty input", ""); return false; }
    if (!pVal) { ErrSet(err, errCap, "SaveNamedData has no IntProperty input", ""); return false; }

    wr::Frame f;
    if (!wr::FrameInit(f, w.setLay, (int)sizeof(f.bytes))) {
        ErrSet(err, errCap, "SaveNamedData frame: ", f.why);
        return false;
    }
    if (pCtx && !wr::FramePutObject(f, pCtx->name, world)) {
        ErrSet(err, errCap, "SaveNamedData frame: ", f.why);
        return false;
    }
    if (!wr::FramePutName(f, pKey->name, n) ||
        !wr::FramePutInt(f, pVal->name, value)) {
        ErrSet(err, errCap, "SaveNamedData frame: ", f.why);
        return false;
    }

    if (!ProcessEventGuarded(w.cdo, w.fnSet, f.bytes)) {
        ErrSet(err, errCap, "SaveNamedData ProcessEvent faulted", "");
        QDLOG(LOG_ERROR, "write api: SaveNamedData ProcessEvent faulted for key=", key);
        return false;
    }

    // "The call returned" is not evidence.  The native getter is the oracle.
    bool set = false;
    int32_t got = GetNamedSaveDataNative(world, n, &set);
    if (set && got == value) {
        QDLOG(LOG_INFO, "write api: SaveNamedData ", key, "=", dec(value),
              " read back equal (wasSet=1)");
        return true;
    }

    char m[160];
    char* d = m;
    const char* e = m + sizeof(m) - 1;
    d = detail::Cat(d, e, "read-back mismatch: wrote ");
    d = detail::CatDec(d, e, value);
    d = detail::Cat(d, e, " got ");
    d = detail::CatDec(d, e, got);
    d = detail::Cat(d, e, " wasSet ");
    d = detail::CatDec(d, e, set ? 1 : 0);
    *d = 0;
    ErrSet(err, errCap, m, "");
    QDLOG(LOG_WARN, "write api: SaveNamedData ", key, ": ", m);
    return false;
}

// ===========================================================================
// Part 4 -- API-GAPS section 2: the inventory grant
// ===========================================================================
// The argument form is decided from the reflected types and from what the Blueprint
// itself does.  `SaveFunctions_C.AddItemToInventory` imports
// `Conv_SoftObjectReferenceToObject` and `GetInventoryItemCountSoft`, so its item pin is
// a SOFT OBJECT REFERENCE, not a path string and not a bare object: the count is a
// separate int.  That is the "(item, count)" shape section 2 anticipated from the two
// readers.
//
// The read-back uses the object-keyed native counter (0x00E2EA40), because its impl is
// measured and takes a plain UObject*.  The soft variant exists only as an exec thunk
// (0x00E613E0), which takes an FFrame and cannot be called as a plain function; calling
// that thunk wrongly would corrupt the stack rather than fail.  So the item's object is
// resolved from the path and the counter is keyed on that same object.
typedef int32_t (*FnGetInventoryItemCount)(void* worldContext, void* item);

inline int32_t InventoryCountGuarded(void* world, void* item, int* faulted)
{
    *faulted = 0;
    __try {
        FnGetInventoryItemCount fn = (FnGetInventoryItemCount)Ptr<void*>(0x00E2EA40u);
        return fn(world, item);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        *faulted = 1;
        return 0;
    }
}

inline bool InventoryCountNative(void* world, void* item, int32_t* out)
{
    if (!out || !world || !item) return false;
    // Same rule as the named getter: 0x00E2EA40 calls GetActiveBaseSave, which creates a
    // save when none is present.  A read must never be the thing that plants one.
    if (!ActiveSaveNoCreate(world)) return false;
    int faulted = 0;
    int32_t v = InventoryCountGuarded(world, item, &faulted);
    if (faulted) return false;
    *out = v;
    return true;
}

// The last path segment: `/Game/Items/KeyItems/AquaDoorKey_2.AquaDoorKey_2` names the
// object `AquaDoorKey_2`.  A Blueprint item asset's UClass carries that name, and the
// game's own `TMap<UObject*, int32>` is keyed by the object the soft reference resolves
// to -- so prefer the class, then any object of that name.
inline bool ObjectNameFromPath(const char* path, char* out, int cap)
{
    if (!path || !out || cap <= 0) return false;
    const char* name = path;
    for (const char* p = path; *p; ++p) {
        if (*p == '.' || *p == '/') name = p + 1;
    }
    int n = 0;
    for (; name[n] && n < cap - 1; ++n) out[n] = name[n];
    out[n] = 0;
    return n > 0;
}

inline void* ResolveItemObject(const char* path)
{
    char nm[96];
    if (!ObjectNameFromPath(path, nm, sizeof(nm))) return nullptr;
    void* o = FindClassByName(nm);
    if (!o) o = FindObjectByNameAndClass(nm, nullptr);
    return o;
}

// FSoftObjectPath is { FName AssetPathName; FString SubPathString; } and FString is
// { wchar_t* Data; int32 Num; int32 Max }.  `SetPath` splits `/Pkg/Asset.Object` at the
// LAST dot, so the asset FName must be the package path and the sub-path the object
// name; interning the whole string into the FName would not resolve.  The wide buffer
// is supplied by the caller and outlives the synchronous ProcessEvent call.
inline bool PutSoftItem(wr::Frame& f, const wr::Field* p, const char* path,
                        wchar_t* wide, int wideCap, char* err, int cap)
{
    const char* dot = nullptr;
    for (const char* q = path; *q; ++q) if (*q == '.') dot = q;

    char asset[192];
    char sub[192];
    sub[0] = 0;
    int alen = 0;
    const char* aEnd = dot ? dot : path;
    for (const char* q = path; q < aEnd && alen < (int)sizeof(asset) - 1; ++q) {
        asset[alen++] = *q;
    }
    asset[alen] = 0;
    if (dot) {
        int sl = 0;
        for (const char* q = dot + 1; *q && sl < (int)sizeof(sub) - 1; ++q) sub[sl++] = *q;
        sub[sl] = 0;
    }

    QdName an;
    if (!FNameFromAnsi(asset, &an)) {
        ErrSet(err, cap, "cannot intern the item asset path: ", asset);
        return false;
    }

    int wn = 0;
    if (sub[0]) {
        wn = MultiByteToWideChar(CP_ACP, 0, sub, -1, wide, wideCap);
        if (wn <= 0) {
            ErrSet(err, cap, "cannot widen the item sub-path: ", sub);
            return false;
        }
    }

    alignas(16) uint8_t raw[64];
    memset(raw, 0, sizeof(raw));
    *(QdName*)raw = an;                                   // +0x00 AssetPathName
    *(wchar_t**)(raw + 8) = sub[0] ? wide : nullptr;      // +0x08 FString::Data
    *(int32_t*)(raw + 16) = sub[0] ? wn : 0;              // +0x10 FString::Num
    *(int32_t*)(raw + 20) = sub[0] ? wideCap : 0;         // +0x14 FString::Max
    int len = p->size < (int)sizeof(raw) ? p->size : (int)sizeof(raw);
    if (len < 8) {
        ErrSet(err, cap, "SoftObjectProperty is smaller than an FName: ", p->name);
        return false;
    }
    if (!wr::FramePut(f, p->name, raw, len)) {
        ErrSet(err, cap, "inventory item frame: ", f.why);
        return false;
    }
    return true;
}

inline bool PutItemArg(wr::Frame& f, const wr::Field* p, const char* path,
                       void* itemObj, wchar_t* wide, int wideCap,
                       char* err, int cap)
{
    if (p->kind == wr::PK_OBJECT) {
        if (!wr::FramePutObject(f, p->name, itemObj)) {
            ErrSet(err, cap, "inventory item frame: ", f.why);
            return false;
        }
        return true;
    }
    if (p->kind == wr::PK_SOFT) {
        return PutSoftItem(f, p, path, wide, wideCap, err, cap);
    }
    if (p->kind == wr::PK_NAME) {
        QdName n;
        if (!FNameFromAnsi(path, &n)) {
            ErrSet(err, cap, "cannot intern the item path", "");
            return false;
        }
        if (!wr::FramePutName(f, p->name, n)) {
            ErrSet(err, cap, "inventory item frame: ", f.why);
            return false;
        }
        return true;
    }
    if (p->kind == wr::PK_STR) {
        int wn = MultiByteToWideChar(CP_ACP, 0, path, -1, wide, wideCap);
        if (wn <= 0) {
            ErrSet(err, cap, "cannot widen the item path", "");
            return false;
        }
        struct QdFString { wchar_t* data; int32_t num; int32_t max; } s = { wide, wn, wideCap };
        if (!wr::FramePut(f, p->name, &s, (int)sizeof(s))) {
            ErrSet(err, cap, "inventory item frame: ", f.why);
            return false;
        }
        return true;
    }
    ErrSet(err, cap, "unsupported item argument type at ", p->name);
    return false;
}

// The item pin, chosen by form and then by name.  The soft reference comes first
// because the Blueprint converts one; a name/string form is accepted only if that is
// what the signature actually declares (in which case the read-back still keys on the
// resolved object).
inline const wr::Field* ItemParam(const wr::ParamLayout& l,
                                  const wr::Field* ctx, const wr::Field* count)
{
    static const wr::PKind kForms[] = {
        wr::PK_SOFT, wr::PK_OBJECT, wr::PK_NAME, wr::PK_STR
    };
    for (unsigned k = 0; k < sizeof(kForms) / sizeof(kForms[0]); ++k) {
        for (int i = 0; i < l.n; ++i) {
            const wr::Field& p = l.p[i];
            if (&p == ctx || &p == count) continue;
            if (p.flags & (CPF_OutParm | CPF_ReturnParm)) continue;
            if (p.kind == kForms[k] && wr::NameContains(p.name, "Item")) return &p;
        }
        for (int i = 0; i < l.n; ++i) {
            const wr::Field& p = l.p[i];
            if (&p == ctx || &p == count) continue;
            if (p.flags & (CPF_OutParm | CPF_ReturnParm)) continue;
            if (p.kind == kForms[k]) return &p;
        }
    }
    return nullptr;
}

inline bool WrGrantItem(const char* itemPath, int32_t count, char* err, int errCap)
{
    if (err && errCap > 0) err[0] = 0;
    if (!itemPath || !itemPath[0]) {
        ErrSet(err, errCap, "empty item path", "");
        return false;
    }
    if (count <= 0) {
        ErrSet(err, errCap, "grant count must be positive", "");
        return false;
    }
    // Resolve, but do NOT gate on WrReady(): that verdict is section 1's frame oracle,
    // and this write's oracle is the native counter below.  A mismatched NamedSaveData
    // frame must not disable a writer that is proven a different way.
    WrReady();

    WriteLayer& w = Wl();
    if (!Lay().valid) {
        ErrSet(err, errCap, "property layout not validated: ", Lay().note);
        return false;
    }
    if (!w.fnAdd) {
        ErrSet(err, errCap, "AddItemToInventory not found on SaveFunctions_C", "");
        return false;
    }
    if (!w.addOk) {
        ErrSet(err, errCap, "AddItemToInventory signature unavailable", "");
        return false;
    }

    void* world = GWorldPtr();
    if (!world) { ErrSet(err, errCap, "no world", ""); return false; }
    if (!ActiveSaveNoCreate(world)) {
        ErrSet(err, errCap, "no active save object", "");
        return false;
    }

    const wr::Field* pCtx = wr::WorldContextInput(w.addLay);
    const wr::Field* pCount = wr::InputNamed(w.addLay, wr::PK_INT, "Count");
    if (!pCount) pCount = wr::InputNamed(w.addLay, wr::PK_INT, "Amount");
    if (!pCount) pCount = wr::FirstInput(w.addLay, wr::PK_INT);
    const wr::Field* pItem = ItemParam(w.addLay, pCtx, pCount);
    if (!pCount) { ErrSet(err, errCap, "AddItemToInventory has no int count input", ""); return false; }
    if (!pItem)  { ErrSet(err, errCap, "AddItemToInventory has no item input", ""); return false; }

    // The read-back key.  Without the object there is no oracle, and an unverifiable
    // grant is refused rather than made: a wrong-class ProcessEvent succeeds and
    // changes nothing, so "it returned" would be indistinguishable from success.
    void* itemObj = ResolveItemObject(itemPath);
    if (!itemObj) {
        char m[192];
        char* d = m;
        const char* e = m + sizeof(m) - 1;
        d = detail::Cat(d, e, "cannot resolve the item object for read-back: ");
        d = detail::Cat(d, e, itemPath);
        *d = 0;
        ErrSet(err, errCap, m, "");
        return false;
    }

    int32_t before = 0;
    if (!InventoryCountNative(world, itemObj, &before)) {
        ErrSet(err, errCap, "cannot read the pre-grant count", "");
        return false;
    }

    wr::Frame f;
    if (!wr::FrameInit(f, w.addLay, (int)sizeof(f.bytes))) {
        ErrSet(err, errCap, "AddItemToInventory frame: ", f.why);
        return false;
    }
    if (pCtx && !wr::FramePutObject(f, pCtx->name, world)) {
        ErrSet(err, errCap, "AddItemToInventory frame: ", f.why);
        return false;
    }
    // The wide buffer lives until after ProcessEvent returns, which is all a synchronous
    // FString/FSoftObjectPath needs.
    wchar_t wide[192];
    if (!PutItemArg(f, pItem, itemPath, itemObj, wide, (int)(sizeof(wide) / sizeof(wide[0])),
                    err, errCap)) {
        return false;
    }
    if (!wr::FramePutInt(f, pCount->name, count)) {
        ErrSet(err, errCap, "AddItemToInventory frame: ", f.why);
        return false;
    }

    if (!ProcessEventGuarded(w.cdo, w.fnAdd, f.bytes)) {
        ErrSet(err, errCap, "AddItemToInventory ProcessEvent faulted", "");
        QDLOG(LOG_ERROR, "write api: AddItemToInventory ProcessEvent faulted for ", itemPath);
        return false;
    }

    int32_t after = 0;
    if (!InventoryCountNative(world, itemObj, &after)) {
        ErrSet(err, errCap, "cannot read the post-grant count", "");
        return false;
    }
    int64_t delta = (int64_t)after - (int64_t)before;
    if (delta == count) {
        QDLOG(LOG_INFO, "write api: AddItemToInventory ", itemPath, " +", dec(count),
              " read back ", dec(before), " -> ", dec(after));
        return true;
    }

    char m[192];
    char* d = m;
    const char* e = m + sizeof(m) - 1;
    d = detail::Cat(d, e, "count did not rise by the requested amount: ");
    d = detail::CatDec(d, e, before);
    d = detail::Cat(d, e, " -> ");
    d = detail::CatDec(d, e, after);
    d = detail::Cat(d, e, " (wanted +");
    d = detail::CatDec(d, e, count);
    d = detail::Cat(d, e, ")");
    *d = 0;
    ErrSet(err, errCap, m, "");
    QDLOG(LOG_WARN, "write api: AddItemToInventory ", itemPath, ": ", m);
    return false;
}

// ===========================================================================
// Part 5 -- API-GAPS section 2 / SYNC-GAPS G2: the SavableActorStates ORACLE
// ===========================================================================
// THIS SECTION CONTAINS NO WRITE.  `SavableActorStates` is a Map<FGuid,int32>, but its
// member offset is not measured: `save+0x168` is a LOW/MEDIUM lead, and API-GAPS says
// explicitly not to write it without an oracle.  The oracle is what a later live pass
// needs in order to FIND the container: read a known GUID's value through the game's
// own reflective getter, then search memory for a map that returns the same value.
//
// The getter is `SavableActor_C.GetSavableActorDataAtIndex`, a UFUNCTION on the actor
// (analysis/pak/blueprint-surface.md, "SavableActor": 12 exports).  It is resolved on
// the live actor that carries the GUID, its frame is recovered by the SAME property walk
// as the writers, and the answer is read from the return/out parameter or, if the
// Blueprint stores it, from the actor's `Value`/`ValueAsBool` property.
//
// The property layout must already be validated (it comes from the same discovery as
// the writers).  Without it no offset is trustworthy, and the probe refuses.

inline bool ReflectionReady()
{
    if (Lay().valid) return true;
    // ResolveWrite proves the FProperty walk as a side effect.  It may also fail its own
    // NamedSaveData oracle; that does not matter here -- the walk is what this needs.
    WrReady();
    return Lay().valid;
}

inline bool IsSubclassOf(const void* cls, const void* base)
{
    for (const void* c = cls; c; c = SuperOf(c)) {
        if (c == base) return true;
    }
    return false;
}

inline bool ReadActorInt(void* obj, const char* prop, int32_t* out)
{
    int sz = 0;
    int off = FindPropertyOffsetIn(ObjClass(obj), prop, &sz);
    if (off < 0 || sz < 4) return false;
    *out = *(const int32_t*)((const uint8_t*)obj + off);
    return true;
}

inline bool ReadActorBool(void* obj, const char* prop, bool* out)
{
    int sz = 0;
    int off = FindPropertyOffsetIn(ObjClass(obj), prop, &sz);
    if (off < 0 || sz < 1) return false;
    *out = *(const uint8_t*)((const uint8_t*)obj + off) != 0;
    return true;
}

// A live SavableActor carrying this placement GUID.  `CurrentGUID` is the actor's own
// current key -- the design rule is to key on that runtime value, never on `GUIDs[0]`.
// `GUIDs` is deliberately NOT inspected here: it is an ArrayProperty, whose
// ElementSize is the 16-byte TArray header, not an inline GUID, so walking it at a
// 16-byte stride would read Data/Num/Max as a GUID and "find" a false match.
inline void* FindSavableActorByGuid(const QdGuid& guid)
{
    void* base = FindClassByName("SavableActor_C");
    if (!base) return nullptr;

    int n = ObjectCount();
    for (int i = 0; i < n; ++i) {
        void* o = ObjectAt(i);
        if (!o) continue;
        __try {
            void* cls = ObjClass(o);
            if (!cls || !IsSubclassOf(cls, base)) continue;

            int sz = 0;
            int off = FindPropertyOffsetIn(cls, "CurrentGUID", &sz);
            if (off >= 0 && sz >= 16) {
                QdGuid g = *(const QdGuid*)((const uint8_t*)o + off);
                if (g == guid) return o;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            // An unreadable slot is skipped; a probe must never die for one.
        }
    }
    return nullptr;
}

// Read the getter's answer from the parameter frame.  A BlueprintPure returning a
// `SavableActorState` struct puts its `IsSet` bool at the struct's first byte, which is
// why the struct case reads one byte: the map's value is the same 0/1.
inline bool ReadGetterValue(const wr::ParamLayout& lay, const wr::Frame& f,
                            int32_t* out, const char** via)
{
    const wr::Field* ret = wr::ReturnField(lay);
    if (ret) {
        if (ret->kind == wr::PK_INT) {
            *out = *(const int32_t*)(f.bytes + ret->offset);
            *via = "return:i32";
            return true;
        }
        if (ret->kind == wr::PK_BOOL) {
            *out = f.bytes[ret->offset] ? 1 : 0;
            *via = "return:bool";
            return true;
        }
        if (ret->kind == wr::PK_STRUCT && ret->size >= 1) {
            *out = f.bytes[ret->offset] ? 1 : 0;
            *via = "return:struct.IsSet";
            return true;
        }
    }
    for (int i = 0; i < lay.n; ++i) {
        const wr::Field& p = lay.p[i];
        if (!(p.flags & CPF_OutParm)) continue;
        if (p.kind == wr::PK_INT && wr::NameContains(p.name, "Value")) {
            *out = *(const int32_t*)(f.bytes + p.offset);
            *via = "out:Value";
            return true;
        }
        if (p.kind == wr::PK_BOOL && wr::NameContains(p.name, "IsSet")) {
            *out = f.bytes[p.offset] ? 1 : 0;
            *via = "out:IsSet";
            return true;
        }
    }
    return false;
}

inline bool WrProbeActorState(const QdGuid& guid, int32_t* out, char* err, int cap)
{
    if (err && cap > 0) err[0] = 0;
    if (out) *out = 0;
    if (guid.IsZero()) { ErrSet(err, cap, "zero GUID", ""); return false; }
    if (!ReflectionReady()) {
        ErrSet(err, cap, "property layout not validated: ", Lay().note);
        return false;
    }

    void* actor = FindSavableActorByGuid(guid);
    if (!actor) {
        ErrSet(err, cap, "no live SavableActor carries that GUID", "");
        QDLOG(LOG_WARN, "actor-state oracle: no live SavableActor for guid=",
              hex32(guid.a), "-", hex32(guid.b), "-", hex32(guid.c), "-", hex32(guid.d));
        return false;
    }
    char desc[160];
    ObjDescribe(actor, desc, sizeof(desc));

    void* fn = FindFunction(actor, "GetSavableActorDataAtIndex");
    if (!fn) {
        ErrSet(err, cap, "reflective getter GetSavableActorDataAtIndex not found", "");
        QDLOG(LOG_ERROR, "actor-state oracle: ", desc,
              " has no GetSavableActorDataAtIndex; the log lists what it does have");
        LogFunctionsOf(actor, "SavableActor", 24);
        return false;
    }

    LayoutResult sig = Signature(fn);
    if (!sig.valid) {
        ErrSet(err, cap, "getter signature: ", sig.note);
        QDLOG(LOG_ERROR, "actor-state oracle: getter signature: ", sig.note);
        return false;
    }
    wr::ParamLayout lay;
    char lerr[192];
    if (!wr::FromReflected(sig, &lay, lerr, sizeof(lerr))) {
        ErrSet(err, cap, "getter frame: ", lerr);
        return false;
    }
    wr::Frame f;
    if (!wr::FrameInit(f, lay, (int)sizeof(f.bytes))) {
        ErrSet(err, cap, "getter frame: ", f.why);
        return false;
    }

    void* world = GWorldPtr();
    const wr::Field* pCtx = wr::WorldContextInput(lay);
    if (pCtx && world) wr::FramePutObject(f, pCtx->name, world);

    // Which of the actor's authored slots to read.  The value is a property read, not an
    // assumption about the getter's arity: a getter that takes no index simply ignores
    // this because no int input matched.
    const wr::Field* pIndex = wr::InputNamed(lay, wr::PK_INT, "Index");
    if (!pIndex) pIndex = wr::InputNamed(lay, wr::PK_INT, "CurrentIndex");
    if (pIndex) {
        int32_t iv = 0;
        if (!ReadActorInt(actor, "Index", &iv)) ReadActorInt(actor, "CurrentIndex", &iv);
        wr::FramePutInt(f, pIndex->name, iv);
    }

    // Some generations pass the GUID in rather than reading it from the actor.
    for (int i = 0; i < lay.n; ++i) {
        const wr::Field& p = lay.p[i];
        if (p.kind != wr::PK_STRUCT || p.size < 16) continue;
        if (p.flags & (CPF_OutParm | CPF_ReturnParm)) continue;
        if (!wr::NameContains(p.name, "GUID") && !wr::NameContains(p.name, "Guid")) continue;
        wr::FramePut(f, p.name, &guid, 16);
    }

    if (!ProcessEventGuarded(actor, fn, f.bytes)) {
        ErrSet(err, cap, "getter ProcessEvent faulted", "");
        QDLOG(LOG_ERROR, "actor-state oracle: getter ProcessEvent faulted for ", desc);
        return false;
    }

    int32_t v = 0;
    const char* via = "none";
    bool have = ReadGetterValue(lay, f, &v, &via);
    if (!have && ReadActorInt(actor, "Value", &v)) {
        via = "actor.Value";
        have = true;
    }
    if (!have && ReadActorInt(actor, "ValueInt", &v)) {
        via = "actor.ValueInt";
        have = true;
    }
    if (!have) {
        bool b = false;
        if (ReadActorBool(actor, "ValueAsBool", &b) ||
            ReadActorBool(actor, "IsSet", &b)) {
            v = b ? 1 : 0;
            via = "actor.bool";
            have = true;
        }
    }

    if (!have) {
        ErrSet(err, cap, "getter returned no readable value; see the frame log", "");
        QDLOG(LOG_WARN, "actor-state oracle: ", desc, " getter produced no readable value");
        LogSignature("GetSavableActorDataAtIndex", sig);
        return false;
    }

    if (out) *out = v;
    QDLOG(LOG_INFO, "actor-state oracle: guid=", hex32(guid.a), "-", hex32(guid.b), "-",
          hex32(guid.c), "-", hex32(guid.d), " value=", dec(v), " via=", via,
          " actor=", desc);
    return true;
}

} // namespace qd
