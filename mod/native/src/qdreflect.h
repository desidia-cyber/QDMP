// qdreflect.h -- reflective Blueprint invocation.
//
// WHY THIS EXISTS AT ALL
// ----------------------
// The four write-side APIs the analysis could not pin (NamedSaveData write,
// inventory grant, dialogue action invocation, travel) have one property in
// common: they are BLUEPRINT functions.  `SaveFunctions_C` is a
// BlueprintFunctionLibrary whose 24 exported UFUNCTIONs are compiled bytecode; the
// dialogue actions "dispatch into Blueprint and stop there" (analysis
// 13-dialogue-ui-answers.md: `FinishAction` calls ProcessEvent at 0x00E2BEDE).
// There is no native implementation to hook, so hunting for an RVA is the wrong
// instrument -- the honest answer is to call the game's own UFunction through
// ProcessEvent, exactly as the game itself does.
//
// The game shows the pattern.  At 0x00E60250 it builds a param block
// {FName Key @0; bool WasSet @8; int32 ReturnValue @0xC}, resolves the UFunction
// with UObject::FindFunction (0x011A0050) and dispatches through the object's
// ProcessEvent slot (+0x220).  That layout is MEASURED -- it is read out of the
// game's own stack frame -- and it is used here as the ORACLE that validates the
// property-walk offsets below.
//
// WHY THE OFFSETS ARE DISCOVERED AND NOT WRITTEN DOWN
// ---------------------------------------------------
// UStruct/FProperty member offsets are build-specific and were not part of the RE
// pass.  Typing them from a different UE4.27 build is exactly the failure the
// corpus names the dominant error mode: "not a wrong NUMBER, but a right number
// applied at the wrong ADDRESS".  So this header searches a bounded set of
// candidate offsets and accepts only the combination that reproduces the measured
// GetNamedSaveData layout byte for byte.  If no combination does, the write path
// reports REFUSE and stays off; it does not guess.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdint.h>

#include "qdsubstrate.h"

namespace qd {

// ---------------------------------------------------------------------------
// FName construction
// ---------------------------------------------------------------------------
// FName::FName(const WIDECHAR*) at 0x00FABF20 is a member function: `this` in rcx
// (verified from its prologue, which stores rdx -- the name -- then moves rcx into
// rbx).  It interns into the pool, which is what the engine does for every literal.
typedef QdName* (*FnFNameCtor)(QdName* self, const wchar_t* name);

inline bool FNameFromAnsi(const char* s, QdName* out)
{
    if (!s || !out) return false;
    wchar_t w[192];
    int n = MultiByteToWideChar(CP_ACP, 0, s, -1, w, (int)(sizeof(w) / sizeof(w[0])));
    if (n <= 0) return false;
    FnFNameCtor ctor = (FnFNameCtor)Ptr<void*>(0x00FABF20u);
    if (!ctor) return false;
    ctor(out, w);
    return out->comparison_index != 0;
}

// ---------------------------------------------------------------------------
// UFunction lookup
// ---------------------------------------------------------------------------
// UObject::FindFunction (0x011A0050) is FindFunctionChecked.
//
// MEASURED THE HARD WAY: calling it with a name the class does not have aborts the
// game with
//     LowLevelFatalError [Line: 1334] Failed to find function <name> in <class>
// which surfaces to the player as a crash dialog. It is not a query, it is an
// assertion.
//
// So the mod does not use it. Instead it walks GObjects for a UFunction whose name
// matches and whose Outer is the class in question -- the same linear scan that finds
// the SaveFunctions CDO, using only FNameToString (proven correct on this build) and
// the object graph. A miss returns null and the caller decides.
inline void* FindFunctionSafe(void* owner, const char* fnName)
{
    if (!owner || !fnName) return nullptr;
    void* cls = ObjClass(owner);
    if (!cls) return nullptr;
    void** arr = ObjectsArray();
    int n = ObjectCount();
    if (!arr) return nullptr;
    // Measured: this array is sparse and holds UObject* slots, and a slot can point at
    // memory that is no longer mapped. SlotMatches contains the fault, so one bad slot
    // costs one slot instead of the process.
    for (int k = 0; k < n; ++k) {
        void* m = nullptr;
        SlotMatches(arr[k], fnName, "Function", cls, &m);
        if (m) return m;
    }
    return nullptr;
}

// Kept as the name call sites use: the safe scan is the only lookup.
inline void* FindFunction(void* obj, const char* fnName) { return FindFunctionSafe(obj, fnName); }

// Log the reflected functions available on an object's class. Used when a lookup misses:
// the useful next step is to see the real surface, not to guess a spelling.
inline int LogFunctionsOf(void* owner, const char* label, int limit)
{
    if (!owner) return 0;
    void* cls = ObjClass(owner);
    void** arr = ObjectsArray();
    int n = ObjectCount();
    if (!arr || !cls) return 0;
    int shown = 0;
    int total = 0;
    for (int i = 0; i < n; ++i) {
        void* o = arr[i];
        if (!o) continue;
        bool readable = false;
        __try {
            readable = LooksLikeUObject(o) && ObjOuter(o) == cls &&
                       ObjNameIs(ObjClass(o), "Function");
        } __except (EXCEPTION_EXECUTE_HANDLER) { readable = false; }
        if (!readable) continue;
        ++total;
        if (shown < limit) {
            char nm[96];
            if (FNameToString(ObjNameRaw(o), nm, sizeof(nm)) > 0) {
                QDLOG(LOG_INFO, "    fn ", label, ".", nm);
                ++shown;
            }
        }
    }
    QDLOG(LOG_INFO, "  ", label, " has ", dec(total), " UFUNCTIONs (showing ", dec(shown), ")");
    return total;
}

// ---------------------------------------------------------------------------
// Property walk
// ---------------------------------------------------------------------------
struct PropDesc {
    char     name[64];
    int32_t  offset;
    int32_t  size;
    uint32_t flags_lo;
};

static const int kMaxProps = 24;
static const int kMaxPropName = 64;

// Candidate layouts.  The search is bounded and ordered by likelihood, and the
// oracle below decides -- there is no "best guess".
struct LayoutCandidates {
    int prop_link;        // UStruct::PropertyLink
    int prop_next;        // FProperty::PropertyLinkNext
    int elem_size;        // FProperty::ElementSize
    int offset_internal;  // FProperty::Offset_Internal
    int flags;            // FProperty::PropertyFlags (low 32 bits)
};

struct LayoutResult {
    bool     valid;
    int      props;
    int      parms_size;
    PropDesc p[kMaxProps];
    LayoutCandidates cand;   // the combination the oracle accepted
    char     note[160];      // why it failed, when it did
};

// A UStruct's FunctionFlags is at +0xB0 (measured, analysis A9), so UStruct itself
// spans [0x28, 0xB0) and every candidate below is inside that range.  UObject ends
// at 0x28 (vtable 8, ObjectFlags 4, InternalIndex 4, Class 8, Name 8, Outer 8).
inline bool WalkProperties(void* ustruct, const LayoutCandidates& c, LayoutResult* out)
{
    out->props = 0;
    out->parms_size = 0;
    if (!LooksLikeUObject(ustruct)) return false;
    void* prop = *(void**)((const uint8_t*)ustruct + c.prop_link);
    int guard = 0;
    while (prop && out->props < kMaxProps && guard++ < kMaxProps * 2) {
        // Before ANY field of the property is read: is it an object at all?
        if (!LooksLikeUObject(prop)) return false;
        PropDesc& d = out->p[out->props];
        QdName nm = *(const QdName*)((const uint8_t*)prop + kObjName);
        int len = FNameToString(nm, d.name, kMaxPropName);
        if (len < 0) d.name[0] = 0;
        d.size   = *(const int32_t*)((const uint8_t*)prop + c.elem_size);
        d.offset = *(const int32_t*)((const uint8_t*)prop + c.offset_internal);
        d.flags_lo = *(const uint32_t*)((const uint8_t*)prop + c.flags);

        // Sanity: a real property has a small element size and a small positive
        // offset.  A wrong candidate offset produces plausible garbage here, which
        // is exactly why the oracle below is a full-signature match and not a
        // "looks reasonable" test.
        if (d.size <= 0 || d.size > 4096) return false;
        if (d.offset < 0 || d.offset > 4096) return false;
        if (d.offset + d.size > out->parms_size) out->parms_size = d.offset + d.size;
        ++out->props;
        prop = *(void**)((const uint8_t*)prop + c.prop_next);
    }
    return out->props > 0;
}

// The oracle: GetNamedSaveData's param block as the game's own caller builds it at
// 0x00E60250 -- {FName @0 (8 bytes); bool @8 (1); int32 @0xC (4)}.  A candidate
// triple is accepted only if walking that UFunction's properties reproduces those
// three (offset, size) pairs in ascending order.
inline bool LayoutMatchesOracle(const LayoutResult& r)
{
    if (r.props != 3) return false;
    if (r.p[0].offset != 0  || r.p[0].size != 8) return false;
    if (r.p[1].offset != 8  || r.p[1].size != 1) return false;
    if (r.p[2].offset != 12 || r.p[2].size != 4) return false;
    return r.parms_size >= 16 && r.parms_size <= 32;
}

// Discover the layout once, against a live UFunction.  Returns a copy so the
// caller never holds a pointer into mutable static state across a level change.
inline LayoutResult DiscoverLayout(void* probeFunction)
{
    LayoutResult res;
    res.valid = false;
    res.props = 0;
    res.parms_size = 0;
    res.cand.prop_link = res.cand.prop_next = res.cand.elem_size = 0;
    res.cand.offset_internal = res.cand.flags = 0;
    res.note[0] = 0;

    if (!probeFunction) {
        const char* m = "no probe UFunction";
        for (int i = 0; m[i] && i < (int)sizeof(res.note) - 1; ++i) res.note[i] = m[i];
        return res;
    }

    // Candidate ranges bracket the known 4.27 shapes:
    //   UStruct : Next 0x28, SuperStruct 0x30, Children 0x38, ChildProperties 0x40,
    //             PropertiesSize 0x48, MinAlignment 0x4C, Script 0x50,
    //             PropertyLink 0x60, RefLink 0x68, DestructorLink 0x70,
    //             PostConstructLink 0x78, then UFunction's own fields to 0xB0.
    // FProperty : ArrayDim 0x30, ElementSize 0x34, PropertyFlags 0x38,
    //             RepIndex 0x40, BlueprintReplicationCondition 0x42,
    //             Offset_Internal 0x44, ...
    // Those numbers are the hypothesis.  The oracle is the fact.
    for (int pl = 0x40; pl <= 0xA8; pl += 8) {
        for (int oi = 0x28; oi <= 0x50; oi += 4) {
            for (int es = 0x20; es <= 0x40; es += 4) {
                for (int fl = 0x30; fl <= 0x44; fl += 4) {
                    for (int pn = oi + 0x08; pn <= oi + 0x30; pn += 8) {
                        LayoutCandidates c;
                        c.prop_link = pl; c.prop_next = pn;
                        c.elem_size = es; c.offset_internal = oi; c.flags = fl;
                        LayoutResult r;
                        if (!WalkProperties(probeFunction, c, &r)) continue;
                        if (!LayoutMatchesOracle(r)) continue;
                        r.valid = true;
                        r.cand = c;
                        return r;
                    }
                }
            }
        }
    }
    const char* m = "no candidate layout reproduced GetNamedSaveData {FName@0,bool@8,i32@12}";
    for (int i = 0; m[i] && i < (int)sizeof(res.note) - 1; ++i) res.note[i] = m[i];
    return res;
}

// ---------------------------------------------------------------------------
// Argument packing
// ---------------------------------------------------------------------------
// Arguments are named, never positional.  Positional packing silently sends the
// right values to the wrong fields when a Blueprint's pin order changes; a missing
// NAME is a diagnosable failure.
struct Arg {
    char     name[48];
    enum Kind { I32, BOOL, FNAME, OBJECT, TEXT } kind;
    int32_t  i32;
    uint8_t  b;
    QdName   fname;
    void*    obj;
    char     text[192];
};

struct ArgList {
    static const int kMax = 10;
    Arg a[kMax];
    int n;
    ArgList() : n(0) {}

    Arg* push(const char* name, Arg::Kind k)
    {
        if (n >= kMax) return nullptr;
        Arg& x = a[n++];
        int i = 0;
        for (; name[i] && i < (int)sizeof(x.name) - 1; ++i) x.name[i] = name[i];
        x.name[i] = 0;
        x.kind = k;
        x.i32 = 0; x.b = 0; x.obj = nullptr;
        x.fname.comparison_index = 0; x.fname.number = 0;
        x.text[0] = 0;
        return &x;
    }

    void AddInt(const char* name, int32_t v)   { if (Arg* x = push(name, Arg::I32))   x->i32 = v; }
    void AddBool(const char* name, bool v)     { if (Arg* x = push(name, Arg::BOOL))  x->b = v ? 1 : 0; }
    void AddObject(const char* name, void* v)  { if (Arg* x = push(name, Arg::OBJECT)) x->obj = v; }
    void AddText(const char* name, const char* v)
    {
        Arg* x = push(name, Arg::TEXT);
        if (!x) return;
        int i = 0;
        for (; v[i] && i < (int)sizeof(x->text) - 1; ++i) x->text[i] = v[i];
        x->text[i] = 0;
    }
    void AddFName(const char* name, const QdName& v) { if (Arg* x = push(name, Arg::FNAME)) x->fname = v; }
};

// A read-back handle.  The corpus rule: "`called=yes` is not evidence the call ran.
// Read back the object that was supposed to change."  Every invoke that matters
// uses this.
struct ReadBack {
    int32_t value;
    bool    was_set;
};

inline bool NameEquals(const char* a, const char* b)
{
    int i = 0;
    for (; a[i] && b[i]; ++i) if (a[i] != b[i]) return false;
    return a[i] == 0 && b[i] == 0;
}

inline const PropDesc* FindProp(const LayoutResult& layout, const char* name)
{
    for (int i = 0; i < layout.props; ++i) {
        if (NameEquals(layout.p[i].name, name)) return &layout.p[i];
    }
    return nullptr;
}

// Invoke `fnName` on `obj` with the named arguments, using `layout` to place them.
// `out` (optional) receives the int32 property named ReturnValue and the bool named
// WasSet, which is what the save accessors return.
//
// Returns false and leaves the object untouched if any argument name is absent from
// the function's signature.  That failure mode is the one that matters: a silently
// dropped argument to SaveNamedData would write the wrong value to the right key.
inline bool Invoke(void* obj, void* function, const LayoutResult& layout,
                   const ArgList& args, ReadBack* out, char* err, int errCap)
{
    if (err && errCap > 0) err[0] = 0;
    if (!obj || !function) {
        if (err) { const char* m = "null object or function"; for (int i = 0; m[i] && i < errCap - 1; ++i) err[i] = m[i]; }
        return false;
    }
    if (!layout.valid) {
        if (err) { const char* m = "no validated property layout"; for (int i = 0; m[i] && i < errCap - 1; ++i) err[i] = m[i]; }
        return false;
    }

    // 256 bytes covers every Blueprint signature in this game (the largest seen is
    // a few pointers plus two names); the bound is a refusal, not a truncation.
    alignas(16) uint8_t buf[256];
    int need = layout.parms_size;
    if (need <= 0 || need > (int)sizeof(buf)) {
        if (err) { const char* m = "param block size out of range"; for (int i = 0; m[i] && i < errCap - 1; ++i) err[i] = m[i]; }
        return false;
    }
    for (int i = 0; i < need; ++i) buf[i] = 0;

    for (int i = 0; i < args.n; ++i) {
        const Arg& a = args.a[i];
        const PropDesc* d = FindProp(layout, a.name);
        if (!d) {
            if (err) {
                char* p = err;
                const char* m = "no such parameter: ";
                for (int k = 0; m[k] && p < err + errCap - 1; ++k) *p++ = m[k];
                for (int k = 0; a.name[k] && p < err + errCap - 1; ++k) *p++ = a.name[k];
                *p = 0;
            }
            return false;
        }
        if (d->offset < 0 || d->offset + d->size > need) {
            if (err) { const char* m = "param offset outside the block"; for (int k = 0; m[k] && k < errCap - 1; ++k) err[k] = m[k]; }
            return false;
        }
        uint8_t* dst = buf + d->offset;
        switch (a.kind) {
        case Arg::I32:
            if (d->size < 4) return false;
            *(int32_t*)dst = a.i32;
            break;
        case Arg::BOOL:
            *(uint8_t*)dst = a.b;
            break;
        case Arg::FNAME:
            if (d->size < 8) return false;
            *(QdName*)dst = a.fname;
            break;
        case Arg::OBJECT:
            if (d->size < 8) return false;
            *(void**)dst = a.obj;
            break;
        case Arg::TEXT: {
            // FString is {TCHAR* Data; int32 Num; int32 Max}.  We never own the
            // allocation: this form is only used for functions the analysis does
            // not call yet, and it is left unpopulated on purpose rather than
            // guessing an FString allocator.
            if (err) { const char* m = "TEXT arguments are not supported yet"; for (int k = 0; m[k] && k < errCap - 1; ++k) err[k] = m[k]; }
            return false;
        }
        }
    }

    ProcessEvent(obj, function, buf);

    if (out) {
        out->value = 0;
        out->was_set = false;
        if (const PropDesc* rv = FindProp(layout, "ReturnValue")) {
            if (rv->size >= 4) out->value = *(int32_t*)(buf + rv->offset);
        }
        if (const PropDesc* ws = FindProp(layout, "WasSet")) {
            out->was_set = *(uint8_t*)(buf + ws->offset) != 0;
        }
    }
    return true;
}

// Dump a function's signature into the log.  Used when an invoke fails so the
// operator sees the real parameter list instead of a guess.
inline void LogSignature(const char* label, const LayoutResult& l)
{
    QDLOG(LOG_INFO, "signature ", label, " props=", dec(l.props), " parms=", dec(l.parms_size));
    for (int i = 0; i < l.props; ++i) {
        QDLOG(LOG_DEBUG, "    ", l.p[i].name, " @", dec(l.p[i].offset),
              " size=", dec(l.p[i].size), " flags=", hex32(l.p[i].flags_lo));
    }
}

} // namespace qd
