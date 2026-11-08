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
    // The shape this build actually declares (measured): the world context sits between
    // the key and the value, and the value precedes the bool.
    static const struct { PKind kind; int32_t offset; int32_t size; } kWant4[4] = {
        { PK_NAME,   0,  8 },
        { PK_OBJECT, 8,  8 },
        { PK_INT,    16, 4 },
        { PK_BOOL,   20, 1 },
    };

    // FOUR parameters are also a legitimate shape on this build: the Blueprint library
    // world context is a real parameter, and this build declares Value before WasSet
    // (LIVE-TEST-LOG Session 6).  Both shapes pin the count, the order, the offsets and
    // the sizes, so neither weakens the check -- only the shape it expects.
    const int n = (l.n == 3) ? 3 : (l.n == 4 ? 4 : 0);
    if (!n) {
        char d[160];
        DescribeLayout(l, d, sizeof(d));
        ErrSet(err, cap, "expected 3 or 4 parameters; frame is ", d);
        return false;
    }
    for (int i = 0; i < n; ++i) {
        const bool ok = (n == 3)
            ? (l.p[i].kind == kWant[i].kind && l.p[i].offset == kWant[i].offset &&
               l.p[i].size == kWant[i].size)
            : (l.p[i].kind == kWant4[i].kind && l.p[i].offset == kWant4[i].offset &&
               l.p[i].size == kWant4[i].size);
        if (!ok) {
            char d[160];
            DescribeLayout(l, d, sizeof(d));
            ErrSet(err, cap, n == 3
                ? "frame does not reproduce {FName@0, bool@8, i32@12}: "
                : "frame does not reproduce {FName@0, object@8, i32@16, bool@20}: ", d);
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
    void* fnConsume;

    wr::ParamLayout getLay;
    wr::ParamLayout setLay;
    wr::ParamLayout addLay;
    wr::ParamLayout consumeLay;

    bool  getOk;
    bool  setOk;
    bool  addOk;
    bool  consumeOk;
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
    w.fnConsume = FindFunction(cdo, "TryConsumingItemFromInventory");
    QDLOG(LOG_INFO, "write api: GetNamedSaveData=", hex((uintptr_t)w.fnGet),
          " SaveNamedData=", hex((uintptr_t)w.fnSet),
          " AddItemToInventory=", hex((uintptr_t)w.fnAdd),
          " TryConsumingItemFromInventory=", hex((uintptr_t)w.fnConsume));

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
        void* fns[4] = { w.fnGet, w.fnSet, w.fnAdd, w.fnConsume };
        int n = w.fnAdd ? (w.fnConsume ? 4 : 3) : 2;
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
    if (w.fnConsume) w.consumeOk = SigOf(w.fnConsume, &w.consumeLay,
                                         "TryConsumingItemFromInventory");

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

// The last path segment: `/Game/Items/KeyItems/AquaDoorKey_2_Data.AquaDoorKey_2_Data`
// names the object `AquaDoorKey_2_Data`.  That object is an `FS_ItemData_C` asset, which
// is what the game's own map operations key on (see ResolveItemObject below).
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

// The inventory's TMap is keyed by the `FS_ItemData_C` asset object, and
// `AddItemToInventory` gates on `IsValidSoftObjectReference(Item)` before it does
// anything else.  MEASURED from the cooked package (SaveFunctions.uasset, 1654-byte
// script): the very first tokens are `NewItem` <- `IsValidSoftObjectReference` and the
// whole body sits behind its result -- so a soft record with a correct path and a ZERO
// weak pointer is still rejected when the engine has not resolved that path yet, and the
// add silently does nothing while ProcessEvent returns success.
//
// The item objects are loaded (`FS_ItemData_C` assets), so the weak pointer can be
// filled exactly as the engine would fill it: TWeakObjectPtr is
// { int32 ObjectIndex; int32 ObjectSerialNumber }, and both are readable off the live
// UObject (InternalIndex at +0x0C, serial from the FUObjectItem's SerialNumber).  Filling
// it makes `IsValidSoftObjectReference` true without depending on a resolve, and the path
// stays as the fallback identity.
//
// The record's field offsets.  These are the measured ones -- see the block comment on
// PutSoftItem below for the three independent confirmations in this tree, and the two
// test_*.cpp suites that pin them.
static const int kSoftWeakOff     = 0x00;   // FWeakObjectPtr
static const int kSoftPathNameOff = 0x10;   // FSoftObjectPath::AssetPathName (FName)
static const int kSoftSubPathOff  = 0x18;   // FSoftObjectPath::SubPathString (FString)
static const int kSoftMinSize     = 0x28;   // 40: the size this build reflects
inline bool IsClassObject(const void* o)
{
    char cn[64];
    if (!o || FNameToString(ObjNameRaw(ObjClass(o)), cn, sizeof(cn)) <= 0) return false;
    for (int i = 0; cn[i]; ++i) {
        if (cn[i] == 'C' && cn[i + 1] == 'l' && cn[i + 2] == 'a' && cn[i + 3] == 's' &&
            (cn[i + 4] == 's' || cn[i + 4] == 0))
            return true;
    }
    return false;
}

inline void* ResolveItemObject(const char* path)
{
    char nm[96];
    if (!ObjectNameFromPath(path, nm, sizeof(nm))) return nullptr;
    if (FindClassByName("FS_ItemData_C")) {
        void* o = FindObjectByNameAndClass(nm, "FS_ItemData_C", nullptr);
        if (o) return o;
    }
    // Fall back to any object of that name, but never to a class: a class is not a key
    // the map could hold, and returning one turns "unresolved" into a silent 0.
    void* o = FindObjectByNameAndClass(nm, nullptr);
    if (IsClassObject(o)) return nullptr;
    return o;
}

// The serial number half of the weak pointer.  FUObjectItem is
// { UObject* Object; int32 Flags; int32 ClusterRootIndex; int32 SerialNumber; } with a
// 0x18 stride (qdsubstrate's chunk walk pins the stride and the chunk table).  TWeakObjectPtr
// compares exactly these two ints, so reading them off the live object reproduces the weak
// pointer the engine would have cached for this asset.
inline int32_t ObjectSerialNumber(const void* o)
{
    if (!o) return 0;
    __try {
        uint8_t** chunks = ObjectChunks();
        if (!chunks) return 0;
        int i = ObjInternalIndex(o);
        if (i < 0) return 0;
        const uint8_t* chunk = chunks[i / kObjectsPerChunk];
        if (!chunk) return 0;
        const uint8_t* item = chunk + (size_t)(i % kObjectsPerChunk) * kFUObjectItemStride;
        return *(const int32_t*)(item + 0x10);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// Write the 8-byte TWeakObjectPtr[ObjectIndex, SerialNumber] into the soft record.
inline bool PutWeakObject(uint8_t* record, void* itemObj)
{
    if (!record || !itemObj) return false;
    int32_t idx = ObjInternalIndex(itemObj);
    int32_t serial = ObjectSerialNumber(itemObj);
    if (idx < 0) return false;
    memcpy(record + kSoftWeakOff, &idx, 4);
    memcpy(record + kSoftWeakOff + 4, &serial, 4);
    return ObjectIsLive(itemObj);
}

// A `SoftObjectProperty` in this build is a 40-byte `FSoftObjectPtr`, and the FName
// part is NOT at offset 0.  MEASURED three times against the engine's own consumers in
// this very tree, all of which read/write it at +0x10:
//   * qdavatar.h `LoadAsset` (KismetSystemLibrary::LoadAsset_Blocking) writes the FName
//     to `p + 16` and documents the class as
//     "FWeakObjectPtr(8), TagAtLastTest(4)+pad(4), then FSoftObjectPath{...} -- 40 bytes";
//   * proxy.cpp `DevGive` (this same AddItemToInventory) writes the FName to `at + 16`;
//   * proxy.cpp `LevelNameOf` reads TSoftObjectPtr<UWorld>::AssetPathName at +0x10.
//
//   +0x00  FWeakObjectPtr    int32 ObjectIndex, int32 ObjectSerialNumber   (8)
//   +0x08  int32 TagAtLastTest + 4 bytes padding                            (8)
//   +0x10  FSoftObjectPath   FName AssetPathName                            (8)
//   +0x18                   FString SubPathString { Data*, Num, Max }       (16)
//   total 0x28 = 40
//
// The previous version wrote the FName at +0 and the FString at +8/+16/+20, i.e. 16
// bytes too low.  That left the path's AssetPathName reading 32 bits of the sub-path
// length in its comparison index and wrote a live pointer into the TagAtLastTest
// padding, so the soft reference resolved to nothing -- the game then added its null
// key (or skipped the add) and the native counter never moved, which is exactly
// LIVE-TEST-LOG Session 6's `count did not rise by the requested amount: 0 -> 0`.
// The two other sites in this tree were already right, which is why the differential
// is visible: the same function, the same payload kind, one offset apart.
//
// The offsets themselves are the `kSoft*` constants declared above, next to
// PutWeakObject, which fills the first field.

// `SetPath` splits `/Pkg/Asset.Object` at the LAST dot, so the asset FName must be the
// package path and the sub-path the object name; interning the whole string into the
// FName would not resolve (it would name a package `/Pkg/Asset.Object` that does not
// exist).  The wide buffer is supplied by the caller and outlives the synchronous
// ProcessEvent call.
inline bool PutSoftItem(wr::Frame& f, const wr::Field* p, const char* path,
                        void* itemObj, wchar_t* wide, int wideCap, char* err, int cap)
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
    // The weak pointer first: `IsValidSoftObjectReference` reads it, and it is the one
    // form that needs no resolve.  A failure here is not fatal -- the path below is the
    // fallback identity -- but it is worth knowing, so the caller logs it.
    bool weak = PutWeakObject(raw, itemObj);
    *(QdName*)(raw + kSoftPathNameOff) = an;              // +0x10 AssetPathName
    *(wchar_t**)(raw + kSoftSubPathOff) = sub[0] ? wide : nullptr;        // +0x18 Data
    *(int32_t*)(raw + kSoftSubPathOff + 8) = sub[0] ? wn : 0;             // +0x20 Num
    *(int32_t*)(raw + kSoftSubPathOff + 12) = sub[0] ? wideCap : 0;       // +0x24 Max
    if (!weak && !sub[0] && p->kind == wr::PK_SOFT) {
        // No weak pointer and no sub-path means nothing resolves: refuse rather than
        // hand the game an empty reference that returns success and adds nothing.
        ErrSet(err, cap, "the item soft reference has neither a live object nor a sub-path: ",
               p->name);
        return false;
    }
    int len = p->size < (int)sizeof(raw) ? p->size : (int)sizeof(raw);
    if (len < kSoftMinSize) {
        ErrSet(err, cap, "SoftObjectProperty is smaller than an FSoftObjectPtr: ", p->name);
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
        return PutSoftItem(f, p, path, itemObj, wide, wideCap, err, cap);
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

// The item pin, chosen by form and then by name.  This build declares
// `SoftObjectProperty Item` (measured: `Item : SoftObjectProperty @0 size=40`), so the
// soft form is what will be selected here; the object form is kept first in the list
// because a build that declares `ObjectProperty Item` wants the resolved `FS_ItemData_C`
// pointer directly, and an object pin fed a soft record would read a pointer out of the
// weak field.  Both are now correct, so the order only has to match the declaration.
inline const wr::Field* ItemParam(const wr::ParamLayout& l,
                                  const wr::Field* ctx, const wr::Field* count)
{
    static const wr::PKind kForms[] = {
        wr::PK_OBJECT, wr::PK_SOFT, wr::PK_NAME, wr::PK_STR
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

// Nothing else fires after a successful add.  MEASURED from the cooked package: the
// game's own `AddItemToInventory` script (`SaveFunctions.uasset`, 1654-byte script) ends
// with `CallFunc_GetGameInstance_ReturnValue` ->
// `K2Node_DynamicCast_AsGame_Instance_Utils` -> `GI_NotifyInventoryChanged` and nothing
// else -- it does NOT call `FullyRefreshInventoryTagCount`, and that function is not even
// owned by `SaveFunctions_C` (it is an import of the `Function` class, resolved at the
// call site that wants it).  So a grant made through ProcessEvent has already run the
// game's refresh and a second copy would be a second broadcast for one pickup.  The tag
// count is refreshed by whatever asked for the item (the pickup's own Interact path),
// which is exactly the caller this lane stands in for.
//
// What is left is the ordering fact the lane depends on: the counter is written by
// `Map_Find`/`Add_IntInt` under `InventoryContents`, so the read-back must happen after
// the ProcessEvent returns -- which it does, and which the `read back a -> b` line
// proves.
// The most recent item this machine granted through the game's own AddItemToInventory.
// The G3 consumption half needs the exact path (a soft-object path is not something a
// scenario can reconstruct), so the writer remembers the one it just proved.
inline char g_qdLastGrant[192] = "";

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
        {
            int i = 0;
            for (; itemPath && itemPath[i] && i < (int)sizeof(g_qdLastGrant) - 1; ++i)
                g_qdLastGrant[i] = itemPath[i];
            g_qdLastGrant[i] = 0;
        }
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

// The removal half of the inventory lane (SYNC-GAPS G3's consumption caveat).  The
// game's own consumer is `SaveFunctions_C.TryConsumingItemFromInventory(Item, Count,
// __WorldContext)`, which checks the count and subtracts with Map_Add -- the same
// container the grant writer uses.  It is called with the amount the host actually
// holds (clamped), and the native counter is the oracle: it must fall by exactly the
// requested amount.  A partial removal is a SUCCESS with `removed < count`: the host
// cannot remove what it does not have, and the caller commits the peer's cumulative
// value so the intent settles rather than retrying forever.
inline bool WrConsumeItem(const char* itemPath, int32_t count, int32_t* removed,
                          char* err, int errCap)
{
    if (err && errCap > 0) err[0] = 0;
    if (removed) *removed = 0;
    if (!itemPath || !itemPath[0]) {
        ErrSet(err, errCap, "empty item path", "");
        return false;
    }
    if (count <= 0) {
        ErrSet(err, errCap, "removal count must be positive", "");
        return false;
    }
    // Same rule as the grant: resolve through WrReady but gate on this writer's own
    // signature and its native read-back, not on section 1's NamedSaveData oracle.
    WrReady();

    WriteLayer& w = Wl();
    if (!Lay().valid) {
        ErrSet(err, errCap, "property layout not validated: ", Lay().note);
        return false;
    }
    if (!w.fnConsume) {
        ErrSet(err, errCap, "TryConsumingItemFromInventory not found on SaveFunctions_C", "");
        return false;
    }
    if (!w.consumeOk) {
        ErrSet(err, errCap, "TryConsumingItemFromInventory signature unavailable", "");
        return false;
    }

    void* world = GWorldPtr();
    if (!world) { ErrSet(err, errCap, "no world", ""); return false; }
    if (!ActiveSaveNoCreate(world)) {
        ErrSet(err, errCap, "no active save object", "");
        return false;
    }

    const wr::Field* pCtx = wr::WorldContextInput(w.consumeLay);
    const wr::Field* pCount = wr::InputNamed(w.consumeLay, wr::PK_INT, "Count");
    if (!pCount) pCount = wr::InputNamed(w.consumeLay, wr::PK_INT, "Amount");
    if (!pCount) pCount = wr::FirstInput(w.consumeLay, wr::PK_INT);
    const wr::Field* pItem = ItemParam(w.consumeLay, pCtx, pCount);
    if (!pCount) { ErrSet(err, errCap, "TryConsumingItemFromInventory has no int count input", ""); return false; }
    if (!pItem)  { ErrSet(err, errCap, "TryConsumingItemFromInventory has no item input", ""); return false; }

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
        ErrSet(err, errCap, "cannot read the pre-removal count", "");
        return false;
    }
    // Nothing to remove is not a failure: the peer's cumulative use is as applied as
    // this host can make it, and the caller's ledger records it so the retry ends.
    if (before <= 0) {
        QDLOG(LOG_INFO, "write api: TryConsumingItemFromInventory ", itemPath, " -",
              dec(count), " but the host holds none (settled)");
        return true;
    }
    int32_t amount = count < before ? count : before;

    wr::Frame f;
    if (!wr::FrameInit(f, w.consumeLay, (int)sizeof(f.bytes))) {
        ErrSet(err, errCap, "TryConsumingItemFromInventory frame: ", f.why);
        return false;
    }
    if (pCtx && !wr::FramePutObject(f, pCtx->name, world)) {
        ErrSet(err, errCap, "TryConsumingItemFromInventory frame: ", f.why);
        return false;
    }
    wchar_t wide[192];
    if (!PutItemArg(f, pItem, itemPath, itemObj, wide, (int)(sizeof(wide) / sizeof(wide[0])),
                    err, errCap)) {
        return false;
    }
    if (!wr::FramePutInt(f, pCount->name, amount)) {
        ErrSet(err, errCap, "TryConsumingItemFromInventory frame: ", f.why);
        return false;
    }

    if (!ProcessEventGuarded(w.cdo, w.fnConsume, f.bytes)) {
        ErrSet(err, errCap, "TryConsumingItemFromInventory ProcessEvent faulted", "");
        QDLOG(LOG_ERROR, "write api: TryConsumingItemFromInventory ProcessEvent faulted for ",
              itemPath);
        return false;
    }

    int32_t after = 0;
    if (!InventoryCountNative(world, itemObj, &after)) {
        ErrSet(err, errCap, "cannot read the post-removal count", "");
        return false;
    }
    int64_t delta = (int64_t)after - (int64_t)before;
    if (delta == -(int64_t)amount) {
        if (removed) *removed = amount;
        QDLOG(LOG_INFO, "write api: TryConsumingItemFromInventory ", itemPath, " -",
              dec(amount), " read back ", dec(before), " -> ", dec(after),
              (amount < count ? " (host held less than the peer asked; settled)" : ""));
        return true;
    }

    char m[192];
    char* d = m;
    const char* e = m + sizeof(m) - 1;
    d = detail::Cat(d, e, "count did not fall by the requested amount: ");
    d = detail::CatDec(d, e, before);
    d = detail::Cat(d, e, " -> ");
    d = detail::CatDec(d, e, after);
    d = detail::Cat(d, e, " (wanted -");
    d = detail::CatDec(d, e, amount);
    d = detail::Cat(d, e, ")");
    *d = 0;
    ErrSet(err, errCap, m, "");
    QDLOG(LOG_WARN, "write api: TryConsumingItemFromInventory ", itemPath, ": ", m);
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

// The actor's own GUIDs.  `CurrentGUID` is a LOCAL inside `CheckGUID`/`Save Data Index to
// World Save`, not a class property (the cooked class tag list is
// GUIDs:Array<FGuid>, SaveDataCount:int, MySaveData:Array<SavableActorState>), so the
// old lookup on a `CurrentGUID` property could never match.  The durable identity is the
// authored GUID in `GUIDs`, and the index of that GUID is exactly the `Index` the game's
// own writer takes.
inline bool FindGuidIndexInActor(void* actor, const QdGuid& guid, int* index)
{
    if (!actor || !index) return false;
    int sz = 0;
    int off = FindPropertyOffsetIn(ObjClass(actor), "GUIDs", &sz);
    (void)sz;
    if (off < 0) return false;
    __try {
        const uint8_t* arr = *(const uint8_t* const*)((const uint8_t*)actor + off);
        int32_t num = *(const int32_t*)((const uint8_t*)actor + off + 8);
        if (!arr || num <= 0 || num > 64) return false;
        // `GUIDs` is TArray<FGuid>: Data at +0, Num at +8, elements are 16 raw bytes.
        for (int32_t i = 0; i < num; ++i) {
            QdGuid g;
            memcpy(&g, arr + (size_t)i * 16, sizeof(g));
            if (g == guid) { *index = (int)i; return true; }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return false;
}

// A live SavableActor carrying this placement GUID.  Every savable actor (doors, chests,
// barricades, breakable walls, spawners) derives from SavableActor_C and keeps its
// authored GUIDs in that array; the map is keyed by one of them.
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
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            continue;
        }
        int idx = 0;
        if (FindGuidIndexInActor(o, guid, &idx)) return o;
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

// ===========================================================================
// Part 6 -- SavableActorStates: the measured container and its writer
// ===========================================================================
// WHAT WAS MEASURED, AND WHERE
// ----------------------------
// * The container is a reflected `MapProperty` on `FS_SaveSlot_C`, keyed by a 16-byte
//   `StructProperty` (FGuid) with a 4-byte `IntProperty` value.  The class tag dump in
//   SYNC-RESEARCH section A.8 is the artefact.  So the member offset is DISCOVERED by
//   `FindPropertyOffsetIn`, not a hardcoded `save+0x168`; that lead was never a
//   measurement of this container.
//
// * The geometry is the engine's own TMap geometry, read from `TMap::Find` (0x007FB070,
//   the instantiation the measured `GetInventoryItemCount` calls) and from the
//   GUID-keyed `TMap::Add` instantiation (0x00E19FC0, the one `StoreEnemySaveState`
//   uses).  Key at +0x00, value at +0x10, HashNextId at +0x14, HashIndex at +0x18,
//   element stride 0x1C.  `HashNextId` sits at `sizeof(TPair)`, and the stride is
//   `sizeof(TPair)+8`: FGuid(16)+int32(4) = 20, +4+4 = 28.  The enemy writer proves the
//   rule with a different value size (16+12 -> 36).
//
// * Enumerating through the hash buckets and each element's HashNextId visits only LIVE
//   elements.  A free slot in the sparse array is on the free list and has no bucket, so
//   this needs no bitset heuristic (the flag lane's reject-each-element rule is the
//   alternative, and it works there only because an invalid FName is detectable).
//
// THE WRITE, IN THE ORDER THE DESIGN ASKS FOR
// -------------------------------------------
// (a) There is no Blueprint setter on the save object family that takes a GUID and a
//     value: `SaveFunctions_C`'s 24 exports contain nothing of the kind, and
//     `SavableActorStates` is written by `SavableActor_C` itself.
// (b) `SavableActor_C.Save Data Index to World Save(Index, Data)` is the game's own
//     writer.  Its bytecode does `Map_Add(GetCurrentlyActiveSave().SavableActorStates,
//     GUIDs[Index], Data)` and `Array_Set(MySaveData, Index, ...)`; the parameter tags
//     are exactly `Index:int, Data:int` (SYNC-RESEARCH section A.8).  It therefore
//     INSERTs as well as updates, and it keeps the actor's cached array in step.  This
//     is the preferred path whenever the owning actor is live.
// (c) A key that exists but whose actor is not loaded can still be updated in place
//     through the measured TSet geometry.  That is the fallback, used only when (b) is
//     unavailable, and it is refused for a key that does not exist: a raw insert would
//     have to grow the sparse array through the engine's allocator and re-bucket the
//     hash, which is not something to hand-roll into a running game.
//
// WHAT IS NOT DONE, AND WHY
// -------------------------
// The visible refresh pair `CacheSaveData` + `Restore State From Save Data` is NOT
// called.  `CacheSaveData` uses `Array_Add`, the package imports no clear/remove, and
// calling it repeatedly would grow `MySaveData`; the one-shot writer above updates the
// cached entry with `Array_Set`, which is safe.  Whether the in-level actor reacts to
// that alone is the live question G2 leaves open.  A write the mod cannot verify must
// not be dressed up as one, and the read-back below is the verification that does run.

// sizeof(TMap) on this build: the cooked MapProperty tag declares size=80, and the
// engine header fields below end exactly at 0x50.  Used only as a sanity check.
static const int kActorStateMapSize = 0x50;

// TMap / TSet header, from TMap::Find (0x007FB070):
//   +0x00 Data pointer        +0x08 ArrayNum           +0x0C ArrayMax
//   +0x34 NumFreeIndices      +0x38 inline hash array  +0x40 hash array pointer
//   +0x48 HashSize
static const int kAsData       = 0x00;
static const int kAsNum        = 0x08;
static const int kAsMax        = 0x0C;
static const int kAsNumFree    = 0x34;
static const int kAsHashInline = 0x38;
static const int kAsHashPtr    = 0x40;
static const int kAsHashSize   = 0x48;

// The element, from the same walk and from TMap::Add (0x00E19FC0).
static const int kAsStride    = 0x1C;   // FGuid 16 + int32 4 + HashNextId 4 + HashIndex 4
static const int kAsKeyOff    = 0x00;
static const int kAsValueOff  = 0x10;
static const int kAsHashNext  = 0x14;

static const int32_t kAsNoIndex = -1;

struct ActorStateMap {
    bool           ok;
    void*          save;
    int            offset;
    int            size;
    int32_t        num;
    int32_t        numFree;
    int32_t        hashSize;
    const uint8_t* data;
    const uint32_t* buckets;
    char           why[160];
};

inline bool ReadActorStateHeaderGuarded(const uint8_t* base, ActorStateMap* m)
{
    __try {
        m->data     = *(const uint8_t* const*)(base + kAsData);
        m->num      = *(const int32_t*)(base + kAsNum);
        m->numFree  = *(const int32_t*)(base + kAsNumFree);
        m->hashSize = *(const int32_t*)(base + kAsHashSize);
        m->buckets  = *(const uint32_t* const*)(base + kAsHashPtr);
        if (!m->buckets) m->buckets = (const uint32_t*)(base + kAsHashInline);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return true;
}

// Locate the container on the active save and validate its header.  The validation is
// what makes a wrong property offset fail closed instead of walking garbage: a non-empty
// TSet always has a power-of-two HashSize, `0 <= NumFreeIndices <= Num`, a Data pointer
// when Num > 0, and every bucket either -1 or an index below Num.
inline bool ActorStateMapLocate(ActorStateMap* m)
{
    if (!m) return false;
    m->ok = false;
    m->why[0] = 0;
    m->save = nullptr;
    m->offset = -1;
    m->size = 0;
    m->num = m->numFree = m->hashSize = 0;
    m->data = nullptr;
    m->buckets = nullptr;

    if (!ReflectionReady()) {
        CopyNote(m->why, sizeof(m->why), "property layout not validated");
        return false;
    }
    void* world = GWorldPtr();
    if (!world) { CopyNote(m->why, sizeof(m->why), "no world"); return false; }
    void* save = ActiveSaveNoCreate(world);
    if (!save) { CopyNote(m->why, sizeof(m->why), "no active save object"); return false; }

    int sz = 0;
    int off = FindPropertyOffsetIn(ObjClass(save), "SavableActorStates", &sz);
    if (off < 0) {
        CopyNote(m->why, sizeof(m->why), "reflected property SavableActorStates not found");
        return false;
    }
    // A different property size does not necessarily invalidate the header fields, but
    // it is worth naming: the whole geometry below was measured for the 0x50-byte
    // TMap/FScriptMap this build serialises (the cooked MapProperty tag says size=80).
    if (sz != kActorStateMapSize) {
        QDLOG(LOG_WARN, "actor-state: SavableActorStates property size ", dec(sz),
              " (measured ", dec(kActorStateMapSize), "); validating the header anyway");
    }

    ActorStateMap probe = {};
    probe.save = save;
    probe.offset = off;
    probe.size = sz;
    const uint8_t* base = (const uint8_t*)save + off;
    if (!ReadActorStateHeaderGuarded(base, &probe)) {
        CopyNote(m->why, sizeof(m->why), "container header unreadable");
        return false;
    }

    if (probe.num < 0 || probe.num > 300000) {
        CopyNote(m->why, sizeof(m->why), "ArrayNum out of range (wrong offset?)");
        return false;
    }
    if (probe.numFree < 0 || probe.numFree > probe.num) {
        CopyNote(m->why, sizeof(m->why), "NumFreeIndices out of range (wrong offset?)");
        return false;
    }
    if (probe.num > 0 && !probe.data) {
        CopyNote(m->why, sizeof(m->why), "no element array for a non-empty map");
        return false;
    }
    if (probe.hashSize < 0 || probe.hashSize > (1 << 20) ||
        (probe.hashSize & (probe.hashSize - 1)) != 0) {
        CopyNote(m->why, sizeof(m->why), "HashSize is not a power of two (wrong offset?)");
        return false;
    }
    if (probe.num > probe.numFree && probe.hashSize <= 0) {
        CopyNote(m->why, sizeof(m->why), "non-empty map with no hash buckets");
        return false;
    }
    if (probe.hashSize > 0) {
        bool bad = false;
        __try {
            for (int32_t b = 0; b < probe.hashSize; ++b) {
                int32_t idx = (int32_t)probe.buckets[b];
                if (idx != kAsNoIndex && (idx < 0 || idx >= probe.num)) { bad = true; break; }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            bad = true;
        }
        if (bad) {
            CopyNote(m->why, sizeof(m->why), "a hash bucket points outside the element array");
            return false;
        }
    }

    probe.ok = true;
    *m = probe;
    return true;
}

// The element for `guid`, or null.  Walks the hash buckets and each bucket's chain --
// the same walk TMap::Find does, except that it compares all live elements rather than
// hashing the key, because the container is small and hashing a GUID is engine code we
// do not need to reproduce.
inline const uint8_t* ActorStateFindGuarded(const ActorStateMap& m, const QdGuid& guid)
{
    if (!m.ok || !m.data) return nullptr;
    const uint8_t* found = nullptr;
    __try {
        for (int32_t b = 0; b < m.hashSize && !found; ++b) {
            int32_t idx = (int32_t)m.buckets[b];
            int guard = 0;
            while (idx != kAsNoIndex && guard++ < 300000) {
                if (idx < 0 || idx >= m.num) break;
                const uint8_t* e = m.data + (size_t)idx * kAsStride;
                QdGuid g;
                memcpy(&g, e + kAsKeyOff, sizeof(g));
                if (g == guid) { found = e; break; }
                idx = *(const int32_t*)(e + kAsHashNext);
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
    return found;
}

inline bool ActorStateElementValueGuarded(const uint8_t* elem, int32_t* out)
{
    __try {
        *out = *(const int32_t*)(elem + kAsValueOff);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return true;
}

inline bool ActorStateWriteValueGuarded(const uint8_t* elem, int32_t value)
{
    __try {
        *(int32_t*)((uint8_t*)elem + kAsValueOff) = value;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return true;
}

typedef void (*ActorStateVisitor)(const QdGuid& guid, int32_t value, void* user);

inline int ActorStateEnumerateGuarded(const ActorStateMap& m, ActorStateVisitor visit,
                                      void* user)
{
    if (!m.ok) return -1;
    // An empty container has no element array and no buckets; that is a valid empty set,
    // not an unreadable one -- publishing it is how the lane proves it is alive.
    if (m.num <= 0) return 0;
    if (!m.data) return -1;
    int n = 0;
    __try {
        for (int32_t b = 0; b < m.hashSize; ++b) {
            int32_t idx = (int32_t)m.buckets[b];
            int guard = 0;
            while (idx != kAsNoIndex && guard++ < 300000) {
                if (idx < 0 || idx >= m.num) return -1;
                const uint8_t* e = m.data + (size_t)idx * kAsStride;
                QdGuid g;
                memcpy(&g, e + kAsKeyOff, sizeof(g));
                int32_t v = *(const int32_t*)(e + kAsValueOff);
                if (visit) visit(g, v, user);
                ++n;
                idx = *(const int32_t*)(e + kAsHashNext);
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
    return n;
}

inline int ActorStateEnumerate(ActorStateVisitor visit, void* user)
{
    ActorStateMap m;
    if (!ActorStateMapLocate(&m)) return -1;
    return ActorStateEnumerateGuarded(m, visit, user);
}

inline bool ActorStateRead(const QdGuid& guid, int32_t* out)
{
    ActorStateMap m;
    if (!ActorStateMapLocate(&m)) return false;
    const uint8_t* e = ActorStateFindGuarded(m, guid);
    if (!e) return false;
    int32_t v = 0;
    if (!ActorStateElementValueGuarded(e, &v)) return false;
    if (out) *out = v;
    return true;
}

// ---------------------------------------------------------------------------
// The game's own writer, and its two read-backs
// ---------------------------------------------------------------------------
inline bool WriteActorStateViaActorGuarded(void* actor, int index, int32_t value,
                                           char* err, int cap)
{
    void* fn = FindFunction(actor, "Save Data Index to World Save");
    if (!fn) {
        ErrSet(err, cap, "SavableActor has no Save Data Index to World Save", "");
        return false;
    }
    LayoutResult sig = Signature(fn);
    if (!sig.valid) {
        ErrSet(err, cap, "writer signature: ", sig.note);
        return false;
    }
    // Choose the two int pins by what the function actually declares: `Index` and
    // `Data` in this cook, but a recompile that renames a pin must fail loudly rather
    // than write one value into the other slot.
    const PropDesc* pIndex = FindProp(sig, "Index");
    if (!pIndex) pIndex = FindPropPrefix(sig, "Index");
    const PropDesc* pData = FindProp(sig, "Data");
    if (!pData) pData = FindPropPrefix(sig, "Data");
    if (!pIndex || !pData || pIndex == pData) {
        ErrSet(err, cap, "writer has no distinct Index/Data int pins", "");
        return false;
    }
    ArgList args;
    args.AddInt(pIndex->name, index);
    args.AddInt(pData->name, value);
    ReadBack rb;
    return Invoke(actor, fn, sig, args, &rb, err, cap);
}

// `GetSavableActorDataAtIndex(Index)` returns what `CacheSaveData` put in
// `MySaveData[Index]`.  It is the game's own view of the value and therefore the second,
// independent read-back: the first is the container element itself.
inline bool ReadActorStateViaGetterGuarded(void* actor, int index, int32_t* value,
                                           bool* isSet, char* err, int cap)
{
    void* fn = FindFunction(actor, "GetSavableActorDataAtIndex");
    if (!fn) {
        ErrSet(err, cap, "SavableActor has no GetSavableActorDataAtIndex", "");
        return false;
    }
    LayoutResult sig = Signature(fn);
    if (!sig.valid) {
        ErrSet(err, cap, "getter signature: ", sig.note);
        return false;
    }
    const PropDesc* pIndex = FindProp(sig, "Index");
    if (!pIndex) pIndex = FindPropPrefix(sig, "Index");
    if (!pIndex) {
        ErrSet(err, cap, "getter has no Index input", "");
        return false;
    }
    ArgList args;
    args.AddInt(pIndex->name, index);

    alignas(16) uint8_t outBuf[1024];
    ReadBack rb;
    if (!Invoke(actor, fn, sig, args, &rb, err, cap, outBuf)) return false;

    const PropDesc* pVal = FindProp(sig, "Value");
    if (!pVal) pVal = FindPropPrefix(sig, "Value");
    const PropDesc* pSet = FindProp(sig, "IsSet");
    if (!pSet) pSet = FindPropPrefix(sig, "IsSet");
    if (!pVal || pVal->size < 4) {
        ErrSet(err, cap, "getter has no readable Value output", "");
        return false;
    }
    *value = *(const int32_t*)(outBuf + pVal->offset);
    if (isSet) *isSet = pSet && pSet->size >= 1 ? (outBuf[pSet->offset] != 0) : true;
    return true;
}

// One live savable actor and the index of a GUID inside its `GUIDs` array.  Built in one
// GObjects pass for a body: resolving every entry separately would be one class walk per
// GUID per datagram, which on the host's full set is a frame hitch for a value that
// changes once a second.
struct ActorStateOwner {
    QdGuid guid;
    void*  actor;
    int    index;
};

inline int BuildActorStateOwnersGuarded(void* base, ActorStateOwner* out, int cap)
{
    int n = 0;
    int count = ObjectCount();
    for (int i = 0; i < count && n < cap; ++i) {
        void* o = ObjectAt(i);
        if (!o) continue;
        __try {
            void* cls = ObjClass(o);
            if (!cls || !IsSubclassOf(cls, base)) continue;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            continue;
        }
        int sz = 0;
        int off = FindPropertyOffsetIn(ObjClass(o), "GUIDs", &sz);
        (void)sz;
        if (off < 0) continue;
        __try {
            const uint8_t* arr = *(const uint8_t* const*)((const uint8_t*)o + off);
            int32_t num = *(const int32_t*)((const uint8_t*)o + off + 8);
            if (!arr || num <= 0 || num > 64) continue;
            for (int32_t k = 0; k < num && n < cap; ++k) {
                memcpy(&out[n].guid, arr + (size_t)k * 16, sizeof(QdGuid));
                out[n].actor = o;
                out[n].index = (int)k;
                ++n;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    return n;
}

inline int BuildActorStateOwners(ActorStateOwner* out, int cap)
{
    if (!out || cap <= 0) return 0;
    void* base = FindClassByName("SavableActor_C");
    if (!base) return 0;
    return BuildActorStateOwnersGuarded(base, out, cap);
}

inline bool EncodeGuidForLog(const QdGuid& g, char* out, int cap)
{
    if (!out || cap <= 0) return false;
    char tmp[80];
    int w = 0;
    const char* hex = "0123456789abcdef";
    uint32_t parts[4] = { g.a, g.b, g.c, g.d };
    for (int p = 0; p < 4; ++p) {
        for (int s = 28; s >= 0; s -= 4) {
            if (w < (int)sizeof(tmp) - 1) tmp[w++] = hex[(parts[p] >> s) & 0xF];
        }
        if (p != 3 && w < (int)sizeof(tmp) - 1) tmp[w++] = '-';
    }
    tmp[w] = 0;
    CopyNote(out, cap, tmp);
    return true;
}

// The write.  `actorHint`/`indexHint` let a body-level caller reuse the owner pass;
// pass nullptr/-1 and the actor is resolved here.  Read-back is mandatory: "the call
// returned" is not evidence (DollsMP F-12.17), and a successful ProcessEvent with the
// wrong frame changes nothing.
inline bool WrSetActorState(const QdGuid& guid, int32_t value, void* actorHint,
                            int indexHint, char* err, int errCap, bool* changed = nullptr)
{
    if (err && errCap > 0) err[0] = 0;
    if (changed) *changed = false;
    if (guid.IsZero()) {
        ErrSet(err, errCap, "zero GUID", "");
        return false;
    }

    // The pre-state is read first, so a key that the game never created can be refused
    // rather than inserted by hand.
    int32_t before = 0;
    bool present = ActorStateRead(guid, &before);

    // Already equal: there is nothing to write and nothing to verify.  This is the common
    // case when a peer joins a set it has already loaded, and skipping it keeps a
    // 60-entry first body from becoming 60 ProcessEvent round trips for no state change.
    if (present && before == value) {
        QDLOG(LOG_DEBUG, "actor-state: guid already holds ", dec(value), "; nothing to write");
        if (err && errCap > 0) err[0] = 0;
        return true;
    }

    void* actor = actorHint;
    int index = indexHint;
    if (!actor || index < 0) {
        actor = FindSavableActorByGuid(guid);
        if (actor && !FindGuidIndexInActor(actor, guid, &index)) {
            actor = nullptr;
            index = -1;
        }
    }

    // (b) the game's own writer: inserts if needed and updates MySaveData.
    if (actor && index >= 0) {
        char werr[160];
        if (WriteActorStateViaActorGuarded(actor, index, value, werr, sizeof(werr))) {
            int32_t after = 0;
            bool haveAfter = ActorStateRead(guid, &after);
            if (!haveAfter || after != value) {
                char gbuf[80];
                EncodeGuidForLog(guid, gbuf, sizeof(gbuf));
                ErrSet(err, errCap,
                       "writer returned but the container did not change: wrote ",
                       "");
                QDLOG(LOG_ERROR, "actor-state: game writer for guid=", gbuf,
                      " value=", dec(value), " read back ", dec(haveAfter ? after : -999999),
                      " (WRITE FAILED)");
                return false;
            }

            // The second oracle: the actor's cached value, which the writer just set.
            int32_t cached = 0;
            bool isSet = false;
            char gerr[160];
            bool haveCached = ReadActorStateViaGetterGuarded(actor, index, &cached, &isSet,
                                                             gerr, sizeof(gerr));
            if (haveCached && (cached != value || !isSet)) {
                char gbuf[80];
                EncodeGuidForLog(guid, gbuf, sizeof(gbuf));
                QDLOG(LOG_WARN, "actor-state: container guid=", gbuf, " value=", dec(after),
                      " but cached=", dec(cached), " isSet=", dec(isSet ? 1 : 0),
                      " at index ", dec(index));
            }

            char gbuf[80];
            EncodeGuidForLog(guid, gbuf, sizeof(gbuf));
            QDLOG(LOG_INFO, "actor-state: wrote guid=", gbuf, " value=", dec(value),
                  " via SavableActor.Save Data Index to World Save index=", dec(index),
                  " read back container=", dec(after),
                  (haveCached ? (isSet ? " cached=ok" : " cached=NOT SET") : " cached=n/a"));
            if (changed) *changed = true;
            return true;
        }
        // Fall through to the in-place path only if the key exists; the reason is kept
        // so a failure is not reported as the other failure.
        CopyNote(err, errCap, werr);
    }

    // (c) in-place update of an existing key.  This cannot create a key: a raw insert
    // would need the engine allocator and a hash re-bucket, and a wrong stride here
    // would corrupt the container rather than fail.
    if (!present) {
        char gbuf[80];
        EncodeGuidForLog(guid, gbuf, sizeof(gbuf));
        ErrSet(err, errCap,
               "GUID not present in SavableActorStates and no live SavableActor owns it; "
               "refusing to insert without the game's Map_Add path: ", gbuf);
        return false;
    }

    ActorStateMap m;
    if (!ActorStateMapLocate(&m)) {
        ErrSet(err, errCap, "container not located: ", m.why);
        return false;
    }
    const uint8_t* e = ActorStateFindGuarded(m, guid);
    if (!e) {
        ErrSet(err, errCap, "container changed between read and write", "");
        return false;
    }
    if (!ActorStateWriteValueGuarded(e, value)) {
        ErrSet(err, errCap, "in-place value store faulted", "");
        return false;
    }
    int32_t after = 0;
    bool haveAfter = ActorStateRead(guid, &after);
    if (!haveAfter || after != value) {
        char gbuf[80];
        EncodeGuidForLog(guid, gbuf, sizeof(gbuf));
        QDLOG(LOG_ERROR, "actor-state: in-place store for guid=", gbuf, " value=",
              dec(value), " read back ", dec(haveAfter ? after : -999999),
              " (WRITE FAILED)");
        ErrSet(err, errCap, "in-place store did not read back", "");
        return false;
    }
    char gbuf[80];
    EncodeGuidForLog(guid, gbuf, sizeof(gbuf));
    QDLOG(LOG_INFO, "actor-state: wrote guid=", gbuf, " value=", dec(value),
          " in place (no live SavableActor), read back ", dec(after));
    if (err && errCap > 0) err[0] = 0;
    if (changed) *changed = true;
    return true;
}

// ===========================================================================
// Part 7 -- G2 visibility: one safe refresh of the in-level SavableActor
// ===========================================================================
// The container and the actor's cached `MySaveData` converge through the writer, but
// the actor only READS that cache on the game's load path: `RestoreFromSave()` does
// `CacheSaveData(); Restore State From Save Data(MySaveData)`.  Calling it repeatedly
// is unsafe because `CacheSaveData` uses `Array_Add`, so every call appends another
// copy of the GUIDs and the array grows without bound.
//
// The safe one-shot is therefore: clear the logical length of `MySaveData` (leave its
// allocation alone), call the game's own `RestoreFromSave`, then read the result back.
// `CacheSaveData` rebuilds exactly one entry per authored GUID from the container, and
// `Restore State From Save Data` runs the subclass' own override -- the same code the
// game runs on every load -- which is what makes a door/chest visibly open and become
// non-interactable.  No effects are invented and no loot path is called: the override
// is the load path, not the interact path.
//
// Everything here is a __try-guarded memory access, and the resolution is logged once
// (the refresh itself logs one line per changed GUID, which is the live evidence).

inline bool FindActorStateOwner(const QdGuid& guid, void** actor, int* index)
{
    if (actor) *actor = nullptr;
    if (index) *index = -1;
    void* a = FindSavableActorByGuid(guid);
    if (!a) return false;
    int idx = -1;
    if (!FindGuidIndexInActor(a, guid, &idx)) return false;
    if (actor) *actor = a;
    if (index) *index = idx;
    return true;
}

inline int ReadActorArrayNum(void* actor, const char* prop)
{
    if (!actor || !prop) return -1;
    int sz = 0;
    int off = FindPropertyOffsetIn(ObjClass(actor), prop, &sz);
    if (off < 0 || sz < 16) return -1;
    __try {
        int32_t num = *(const int32_t*)((const uint8_t*)actor + off + 8);
        if (num < 0 || num > 1000000) return -1;
        return num;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// Set the TArray's logical length to zero without touching its allocation.  The element
// type is a Blueprint `SavableActorState` (one bool), so there is nothing to destruct;
// CacheSaveData's Array_Add overwrites the abandoned elements in place.  Validating the
// header first is what makes a wrong property offset fail instead of corrupting memory.
inline bool ClearActorSaveData(void* actor, int* before, char* err, int cap)
{
    if (before) *before = -1;
    if (!actor) { ErrSet(err, cap, "no actor", ""); return false; }
    int sz = 0;
    int off = FindPropertyOffsetIn(ObjClass(actor), "MySaveData", &sz);
    if (off < 0) {
        ErrSet(err, cap, "SavableActor has no MySaveData array", "");
        return false;
    }
    if (sz < 16) {
        ErrSet(err, cap, "MySaveData is not a TArray", "");
        return false;
    }
    bool ok = false;
    int32_t num = -1;
    __try {
        const void* data = *(const void* const*)((const uint8_t*)actor + off);
        int32_t n = *(const int32_t*)((const uint8_t*)actor + off + 8);
        int32_t m = *(const int32_t*)((const uint8_t*)actor + off + 12);
        if (n >= 0 && m >= 0 && n <= m && m <= 1000000 && (m == 0 || data)) {
            num = n;
            *(int32_t*)((uint8_t*)actor + off + 8) = 0;
            ok = true;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    if (!ok) {
        ErrSet(err, cap, "MySaveData header is implausible or unreadable", "");
        return false;
    }
    if (before) *before = (int)num;
    return true;
}

// Write a bool property directly.  Used only for the interaction gate (`Opened`), after
// the game's own restore has run: the container says the chest is open, so a chest whose
// Blueprint restore left the gate false must not stay lootable.  A direct property write
// is how a Blueprint bool is stored; there is no setter to bypass.
inline bool SetActorBoolGuarded(void* actor, const char* prop, bool value)
{
    if (!actor || !prop) return false;
    int sz = 0;
    int off = FindPropertyOffsetIn(ObjClass(actor), prop, &sz);
    if (off < 0 || sz < 1) return false;
    __try {
        *(uint8_t*)((uint8_t*)actor + off) = value ? 1 : 0;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Restore the logical length after a failed refresh.  The elements are still in the
// allocation, so this puts the previous cache back rather than leaving the actor
// forgetful until its next load.
inline bool SetActorArrayNum(void* actor, const char* prop, int num)
{
    if (!actor || !prop || num < 0) return false;
    int sz = 0;
    int off = FindPropertyOffsetIn(ObjClass(actor), prop, &sz);
    if (off < 0 || sz < 16) return false;
    __try {
        int32_t m = *(const int32_t*)((const uint8_t*)actor + off + 12);
        if (num > m || m > 1000000) return false;
        *(int32_t*)((uint8_t*)actor + off + 8) = num;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Refresh one live SavableActor after `WrSetActorState` changed its container entry.
// `value` is the value just written, so the read-backs can assert the actor now agrees
// with the container.  Returns false when the game's own getter still disagrees; the
// array-count and `Opened` checks are diagnostics, because actors that never had an
// `Opened` property (e.g. a barricade) are still refreshed correctly.
inline bool WrRefreshActorState(void* actor, int index, int32_t value,
                                char* err, int cap)
{
    if (err && cap > 0) err[0] = 0;
    if (!actor) { ErrSet(err, cap, "no live SavableActor", ""); return false; }

    void* fn = FindFunction(actor, "RestoreFromSave");
    static bool loggedResolve = false;
    if (!fn) {
        if (!loggedResolve) {
            QDLOG(LOG_WARN, "actor-state refresh: no RestoreFromSave on the actor class; "
                            "visibility will not follow the container");
            loggedResolve = true;
        }
        ErrSet(err, cap, "RestoreFromSave not found", "");
        return false;
    }
    LayoutResult sig = Signature(fn);
    if (!sig.valid) {
        if (!loggedResolve) {
            QDLOG(LOG_WARN, "actor-state refresh: RestoreFromSave signature: ", sig.note);
            loggedResolve = true;
        }
        ErrSet(err, cap, "RestoreFromSave signature: ", sig.note);
        return false;
    }
    if (sig.props != 0) {
        if (!loggedResolve) {
            QDLOG(LOG_WARN, "actor-state refresh: RestoreFromSave has ", dec(sig.props),
                  " parameters; refusing to guess a frame");
            loggedResolve = true;
        }
        ErrSet(err, cap, "RestoreFromSave has parameters", "");
        return false;
    }
    if (sig.parms_size < 0 || sig.parms_size > wr::kMaxFrame) {
        if (!loggedResolve) {
            QDLOG(LOG_WARN, "actor-state refresh: RestoreFromSave frame is ",
                  dec(sig.parms_size), " bytes; refusing");
            loggedResolve = true;
        }
        ErrSet(err, cap, "RestoreFromSave frame is out of range", "");
        return false;
    }
    if (!loggedResolve) {
        QDLOG(LOG_INFO, "actor-state refresh: RestoreFromSave resolved at ",
              hex((uintptr_t)fn), " frame=", dec(sig.parms_size),
              " (MySaveData cleared, cache rebuilt, state re-applied)");
        loggedResolve = true;
    }

    int before = -1;
    if (!ClearActorSaveData(actor, &before, err, cap)) return false;

    ArgList args;
    ReadBack rb;
    if (!Invoke(actor, fn, sig, args, &rb, err, cap)) {
        // The cache length was cleared for the rebuild.  If the game's own function did
        // not run, put the previous length back so the actor keeps the state it had.
        if (before >= 0) SetActorArrayNum(actor, "MySaveData", before);
        QDLOG(LOG_WARN, "actor-state refresh: RestoreFromSave failed: ", err);
        return false;
    }

    int cacheNum = ReadActorArrayNum(actor, "MySaveData");
    int guidNum  = ReadActorArrayNum(actor, "GUIDs");
    int32_t cached = 0;
    bool isSet = false;
    char gerr[160];
    bool haveCached = ReadActorStateViaGetterGuarded(actor, index, &cached, &isSet,
                                                     gerr, sizeof(gerr));
    bool opened = false;
    bool haveOpened = ReadActorBool(actor, "Opened", &opened);
    bool expectSet = (value != 0);
    bool forcedOpen = false;
    // The load path should have set the gate; if it did not, the container still says
    // this chest/door is open.  Forcing the bool is what makes "a chest looted by one
    // player cannot be looted again by the other" a property of the state, not of the
    // subclass' override happening to be complete.  The log names it `forced=1` so the
    // live run can tell the restore from the backstop.
    if (expectSet && haveOpened && !opened) {
        if (SetActorBoolGuarded(actor, "Opened", true)) {
            opened = true;
            forcedOpen = true;
        }
    }

    bool valueOk = !haveCached || (isSet == expectSet);
    bool cacheOk = (cacheNum < 0 || guidNum < 0 || cacheNum == guidNum);
    bool visibleOk = !haveOpened || (opened == expectSet);
    bool allOk = valueOk && cacheOk && visibleOk;

    char desc[160];
    ObjDescribe(actor, desc, sizeof(desc));
    QDLOG(allOk ? LOG_INFO : LOG_WARN,
          "actor-state refresh ", (allOk ? "ok" : "INCOMPLETE"), " ", desc,
          " index=", dec(index), " value=", dec(value),
          " mySaveData=", dec(before < 0 ? 0 : before), "->", dec(cacheNum),
          " guids=", dec(guidNum),
          (haveCached ? (isSet ? " cached=ok" : " cached=NOT SET") : " cached=n/a"),
          (haveOpened ? (opened ? " opened=1" : " opened=0") : " opened=n/a"),
          (forcedOpen ? " forced=1" : ""));

    if (!valueOk) {
        ErrSet(err, cap, "actor getter still disagrees after RestoreFromSave", "");
        return false;
    }
    if (err && cap > 0) err[0] = 0;
    return true;
}

inline bool WrSetActorState(const QdGuid& guid, int32_t value, char* err, int errCap)
{
    return WrSetActorState(guid, value, nullptr, -1, err, errCap);
}

} // namespace qd
