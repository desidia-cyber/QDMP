// qdreflect.h -- reflective Blueprint invocation.
//
// WHY THIS EXISTS AT ALL
// ----------------------
// The write-side APIs (NamedSaveData write, inventory grant, actor movement) are
// BLUEPRINT functions: `SaveFunctions_C` is a BlueprintFunctionLibrary whose exported
// UFUNCTIONs include SaveNamedData and GetNamedSaveData (analysis
// pak/blueprint-surface.md).  The honest way to call them is the way the game does:
// resolve the UFunction and dispatch through ProcessEvent.
//
// THE LAYOUT, AND HOW IT IS PROVEN
// --------------------------------
// UE4.27 (this is 4.27.2) moved properties out of the UObject graph: a property is an
// FField, not a UObject.  The relevant 4.27 layouts:
//
//   UStruct   +0x40 SuperStruct   +0x48 Children (UField*)   +0x50 ChildProperties (FField*)
//             +0x58 PropertiesSize
//   UField    +0x28 Next
//   FField    +0x08 ClassPrivate  +0x20 Next  +0x28 NamePrivate (FName)
//   FProperty +0x38 ArrayDim  +0x3C ElementSize  +0x40 PropertyFlags (u64)
//             +0x4C Offset_Internal
//   UFunction +0xB0 FunctionFlags  +0xB4 NumParms (u8)  +0xB6 ParmsSize (u16)
//             +0xB8 ReturnValueOffset (u16)
//
// The first version of this file got three things wrong, and together they made the
// write path impossible to enable:
//   * it read property names at the UObject name offset (+0x18) instead of +0x28, so
//     no named argument could ever match;
//   * its candidate search stepped PropertyLinkNext by 8 from Offset_Internal+8, which
//     skips 4.27's real value (0x58 = 0x4C + 0xC), so discovery could never succeed;
//   * its oracle was a 3-field param block that does not match any SaveFunctions_C
//     signature (Blueprint library functions carry __WorldContext).
//
// The oracle now is the engine's own bookkeeping: for a UFunction, the number of
// properties carrying CPF_Parm must equal NumParms, every parameter must fit inside
// ParmsSize, and the CPF_ReturnParm property must sit at ReturnValueOffset.  Those
// three facts are written by the engine when it links the function, independently of
// how we walk it, so a wrong offset cannot satisfy them by accident across several
// functions.  A small candidate set (4.27, then the 4.25/4.26 shape) is tried; if none
// validates, every write path refuses.
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
// FName::FName(const WIDECHAR*) at 0x00FABF20 is a member function: `this` in rcx.
// It interns into the pool, which is what the engine does for every literal.
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

inline bool NameEquals(const char* a, const char* b)
{
    int i = 0;
    for (; a[i] && b[i]; ++i) if (a[i] != b[i]) return false;
    return a[i] == 0 && b[i] == 0;
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
static const uint64_t CPF_Parm        = 0x0000000000000080ull;
static const uint64_t CPF_OutParm     = 0x0000000000000100ull;
static const uint64_t CPF_ReturnParm  = 0x0000000000000400ull;

static const int kStructChildren   = 0x48;
static const int kUFieldNext       = 0x28;
static const int kFuncFlags        = 0xB0;
static const int kFuncNumParms     = 0xB4;
static const int kFuncParmsSize    = 0xB6;
static const int kFuncRetOffset    = 0xB8;

struct Layout {
    bool valid;
    int  child_props;     // UStruct::ChildProperties
    int  field_next;      // FField::Next
    int  field_name;      // FField::NamePrivate
    int  array_dim;       // FProperty::ArrayDim
    int  elem_size;       // FProperty::ElementSize
    int  flags;           // FProperty::PropertyFlags
    int  offset_internal; // FProperty::Offset_Internal
    int  validated_on;    // how many functions agreed
    char note[160];
};

inline Layout& Lay()
{
    static Layout l = {false, 0, 0, 0, 0, 0, 0, 0, 0, {0}};
    return l;
}

inline void CopyNote(char* dst, int cap, const char* src)
{
    int i = 0;
    for (; src[i] && i < cap - 1; ++i) dst[i] = src[i];
    dst[i] = 0;
}

struct PropDesc {
    char     name[64];
    char     type[32];   // FFieldClass name: "IntProperty", "NameProperty", ...
    int32_t  offset;
    int32_t  size;       // ElementSize * ArrayDim
    uint64_t flags;
};

// Large enough for a whole Blueprint class (FS_SaveSlot_C alone has 30+); a walk that
// overflows FAILS rather than truncating, which is what hid NamedSaveData at 32.
static const int kMaxProps = 160;

// A function signature, or a struct's own properties.
struct LayoutResult {
    bool     valid;
    int      props;
    int      parms_size;   // UFunction::ParmsSize (functions) or PropertiesSize
    int      ret_offset;   // UFunction::ReturnValueOffset, 0xFFFF if none
    PropDesc p[kMaxProps];
    char     note[160];
};

// Walk the FField chain of a struct with a given candidate layout.  `parmsOnly` keeps
// only CPF_Parm properties (a Blueprint function also carries its locals as
// properties).  Fault-contained: a wrong candidate offset must cost a candidate, not
// the process.
inline bool WalkFieldsRaw(const void* ustruct, const Layout& c, bool parmsOnly,
                          LayoutResult* out)
{
    out->props = 0;
    __try {
        const uint8_t* f = *(const uint8_t* const*)((const uint8_t*)ustruct + c.child_props);
        int guard = 0;
        while (f && guard++ < 512) {
            // An FField starts with a vtable in the image, like a UObject.
            if (!LooksLikeUObject(f)) return false;
            int32_t dim  = *(const int32_t*)(f + c.array_dim);
            int32_t esz  = *(const int32_t*)(f + c.elem_size);
            uint64_t fl  = *(const uint64_t*)(f + c.flags);
            int32_t off  = *(const int32_t*)(f + c.offset_internal);
            if (dim < 1 || dim > 4096 || esz < 0 || esz > 65536 || off < 0 || off > 1 << 20)
                return false;
            if (!parmsOnly || (fl & CPF_Parm)) {
                if (out->props >= kMaxProps) return false;
                PropDesc& d = out->p[out->props++];
                QdName nm = *(const QdName*)(f + c.field_name);
                if (FNameToString(nm, d.name, sizeof(d.name)) <= 0) return false;
                // FField::ClassPrivate (+0x08) -> FFieldClass, whose first member is
                // its FName ("IntProperty", "ObjectProperty", ...).
                d.type[0] = 0;
                const uint8_t* fc = *(const uint8_t* const*)(f + 0x08);
                if (fc && FNameToString(*(const QdName*)fc, d.type, sizeof(d.type)) <= 0)
                    d.type[0] = 0;
                d.offset = off;
                d.size = esz * dim;
                d.flags = fl;
            }
            f = *(const uint8_t* const*)(f + c.field_next);
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The engine-bookkeeping oracle for one UFunction.
inline bool SignatureMatchesFunction(const void* fn, const LayoutResult& r)
{
    int numParms = *(const uint8_t*)((const uint8_t*)fn + kFuncNumParms);
    int parmsSize = *(const uint16_t*)((const uint8_t*)fn + kFuncParmsSize);
    int retOff = *(const uint16_t*)((const uint8_t*)fn + kFuncRetOffset);
    if (r.props != numParms) return false;
    bool retSeen = false;
    for (int i = 0; i < r.props; ++i) {
        const PropDesc& d = r.p[i];
        if (d.size <= 0 || d.offset + d.size > parmsSize) return false;
        if (d.flags & CPF_ReturnParm) {
            if (d.offset != retOff) return false;
            retSeen = true;
        }
    }
    if (retOff != 0xFFFF && !retSeen) return false;
    return true;
}

// Discover the layout against several live UFunctions.  `fns` must hold functions
// with at least one parameter; at least two must agree for the layout to be accepted.
inline bool DiscoverLayout(void* const* fns, int nfns)
{
    Layout& L = Lay();
    if (L.valid) return true;
    static const Layout kCands[] = {
        // 4.27 (FField name at +0x28, FProperty fields after the 0x38-byte FField)
        {false, 0x50, 0x20, 0x28, 0x38, 0x3C, 0x40, 0x4C, 0, {0}},
        // 4.25/4.26 variants seen in the wild (FField without FlagsPrivate padding)
        {false, 0x50, 0x20, 0x28, 0x30, 0x34, 0x38, 0x44, 0, {0}},
    };
    for (unsigned ci = 0; ci < sizeof(kCands) / sizeof(kCands[0]); ++ci) {
        Layout c = kCands[ci];
        int agree = 0, tried = 0;
        for (int i = 0; i < nfns; ++i) {
            if (!fns[i]) continue;
            int numParms = *(const uint8_t*)((const uint8_t*)fns[i] + kFuncNumParms);
            if (numParms == 0) continue;
            ++tried;
            LayoutResult r;
            if (!WalkFieldsRaw(fns[i], c, true, &r)) break;
            if (!SignatureMatchesFunction(fns[i], r)) break;
            ++agree;
        }
        if (tried >= 2 && agree == tried) {
            L = c;
            L.valid = true;
            L.validated_on = agree;
            CopyNote(L.note, sizeof(L.note), "validated against UFunction NumParms/ParmsSize/ReturnValueOffset");
            return true;
        }
    }
    CopyNote(L.note, sizeof(L.note),
             "no candidate layout reproduced the engine's own NumParms/ParmsSize/ReturnValueOffset");
    return false;
}

// The parameter list of a UFunction, with ParmsSize from the function itself.
inline LayoutResult Signature(void* fn)
{
    LayoutResult r;
    r.valid = false;
    r.props = 0;
    r.parms_size = 0;
    r.ret_offset = 0xFFFF;
    r.note[0] = 0;
    const Layout& L = Lay();
    if (!fn) { CopyNote(r.note, sizeof(r.note), "null function"); return r; }
    if (!L.valid) { CopyNote(r.note, sizeof(r.note), "layout not validated"); return r; }
    if (!WalkFieldsRaw(fn, L, true, &r)) { CopyNote(r.note, sizeof(r.note), "walk failed"); return r; }
    if (!SignatureMatchesFunction(fn, r)) {
        CopyNote(r.note, sizeof(r.note), "signature disagrees with NumParms/ParmsSize");
        return r;
    }
    r.parms_size = *(const uint16_t*)((const uint8_t*)fn + kFuncParmsSize);
    r.ret_offset = *(const uint16_t*)((const uint8_t*)fn + kFuncRetOffset);
    r.valid = true;
    return r;
}

// Offset of a named property on a struct or class, searching up the SuperStruct chain.
// -1 if absent or the layout is unproven.  `outSize` (optional) receives its size.
inline int FindPropertyOffsetIn(const void* ustruct, const char* name, int* outSize = nullptr)
{
    const Layout& L = Lay();
    if (!L.valid || !ustruct || !name) return -1;
    // Streams the FField chain instead of collecting it: the player Blueprint has more
    // than kMaxProps properties, and the collecting walk FAILS on overflow, which made
    // every lookup on the pawn (its "Arms" mesh among them) silently miss.
    const void* s = ustruct;
    for (int depth = 0; s && depth < 32; ++depth) {
        __try {
            const uint8_t* f = *(const uint8_t* const*)((const uint8_t*)s + L.child_props);
            int guard = 0;
            while (f && guard++ < 4096) {
                if (!LooksLikeUObject(f)) break;
                char nm[64];
                if (FNameToString(*(const QdName*)(f + L.field_name), nm, sizeof(nm)) > 0 &&
                    NameEquals(nm, name)) {
                    int32_t dim = *(const int32_t*)(f + L.array_dim);
                    int32_t esz = *(const int32_t*)(f + L.elem_size);
                    int32_t off = *(const int32_t*)(f + L.offset_internal);
                    if (dim < 1 || esz < 0 || off < 0 || off > 1 << 20) return -1;
                    if (outSize) *outSize = esz * dim;
                    return off;
                }
                f = *(const uint8_t* const*)(f + L.field_next);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        s = SuperOf(s);
    }
    return -1;
}

// ---------------------------------------------------------------------------
// UFunction lookup
// ---------------------------------------------------------------------------
// UObject::FindFunction (0x011A0050) is FindFunctionChecked and aborts the game on a
// miss (LIVE-TEST-LOG R10), so it is never called.  Instead: walk UStruct::Children of
// the class and each superclass.  Every child is verified to be a UFunction whose Outer
// is that class, so a wrong Children offset yields "not found", never a wrong object.
inline bool IsFunctionObject(const void* o)
{
    char cn[48];
    if (FNameToString(ObjNameRaw(ObjClass(o)), cn, sizeof(cn)) <= 0) return false;
    return NameEquals(cn, "Function") || NameEquals(cn, "DelegateFunction");
}

inline void* FindFunctionInClass(const void* cls, const char* fnName)
{
    __try {
        const void* c = cls;
        for (int depth = 0; c && depth < 32; ++depth) {
            const void* f = *(const void* const*)((const uint8_t*)c + kStructChildren);
            int guard = 0;
            while (f && guard++ < 4096) {
                if (!LooksLikeUObject(f)) break;
                if (ObjOuter(f) != c) break;           // wrong offset -> stop, not guess
                if (IsFunctionObject(f) && ObjNameIs(f, fnName)) return (void*)f;
                f = *(const void* const*)((const uint8_t*)f + kUFieldNext);
            }
            c = SuperOf(c);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return nullptr;
}

// Lookup on an object's class.  Falls back to a GObjects scan (UFunctions are objects,
// with the owning class as Outer) if the Children walk finds nothing.
inline void* FindFunction(void* obj, const char* fnName)
{
    if (!obj || !fnName) return nullptr;
    void* cls = ObjClass(obj);
    if (!cls) return nullptr;
    void* f = FindFunctionInClass(cls, fnName);
    if (f) return f;
    for (void* c = cls; c; c = SuperOf(c)) {
        f = FindObjectByNameAndClass(fnName, "Function", c);
        if (f) return f;
    }
    return nullptr;
}

// Log the reflected functions on an object's class (own class only).  Used when a
// lookup misses: the useful next step is to see the real surface, not to guess.
inline int LogFunctionsOf(void* owner, const char* label, int limit)
{
    if (!owner) return 0;
    const void* cls = ObjClass(owner);
    int total = 0, shown = 0;
    __try {
        const void* f = *(const void* const*)((const uint8_t*)cls + kStructChildren);
        int guard = 0;
        while (f && guard++ < 4096 && LooksLikeUObject(f) && ObjOuter(f) == cls) {
            if (IsFunctionObject(f)) {
                ++total;
                if (shown < limit) {
                    char nm[96];
                    if (FNameToString(ObjNameRaw(f), nm, sizeof(nm)) > 0) {
                        QDLOG(LOG_INFO, "    fn ", label, ".", nm);
                        ++shown;
                    }
                }
            }
            f = *(const void* const*)((const uint8_t*)f + kUFieldNext);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    QDLOG(LOG_INFO, "  ", label, " has ", dec(total), " UFUNCTIONs (showing ", dec(shown), ")");
    return total;
}

// ---------------------------------------------------------------------------
// Argument packing
// ---------------------------------------------------------------------------
// Arguments are named, never positional.  A missing NAME is a diagnosable failure;
// a positional slip writes the right value to the wrong field.
struct Arg {
    char     name[48];
    enum Kind { I32, BOOL, FNAME, OBJECT, RAW } kind;
    int32_t  i32;
    uint8_t  b;
    QdName   fname;
    void*    obj;
    uint8_t  raw[32];
    int      raw_len;
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
        x.i32 = 0; x.b = 0; x.obj = nullptr; x.raw_len = 0;
        x.fname.comparison_index = 0; x.fname.number = 0;
        return &x;
    }

    void AddInt(const char* name, int32_t v)   { if (Arg* x = push(name, Arg::I32))   x->i32 = v; }
    void AddBool(const char* name, bool v)     { if (Arg* x = push(name, Arg::BOOL))  x->b = v ? 1 : 0; }
    void AddObject(const char* name, void* v)  { if (Arg* x = push(name, Arg::OBJECT)) x->obj = v; }
    void AddFName(const char* name, const QdName& v) { if (Arg* x = push(name, Arg::FNAME)) x->fname = v; }
    void AddRaw(const char* name, const void* p, int len)
    {
        if (len <= 0 || len > 32) return;
        Arg* x = push(name, Arg::RAW);
        if (!x) return;
        memcpy(x->raw, p, (size_t)len);
        x->raw_len = len;
    }
    // FVector {X,Y,Z}; FRotator {Pitch,Yaw,Roll} -- 12 bytes each in 4.27.
    void AddVector(const char* name, float x, float y, float z)
    {
        float v[3] = {x, y, z};
        AddRaw(name, v, 12);
    }
    void AddRotator(const char* name, float pitch, float yaw, float roll)
    {
        float v[3] = {pitch, yaw, roll};
        AddRaw(name, v, 12);
    }
};

// What came back.  "called=yes is not evidence the call ran" -- callers read back.
struct ReadBack {
    int32_t value;
    bool    was_set;
};

inline const PropDesc* FindProp(const LayoutResult& layout, const char* name)
{
    for (int i = 0; i < layout.props; ++i) {
        if (NameEquals(layout.p[i].name, name)) return &layout.p[i];
    }
    return nullptr;
}

// First parameter of a given property type, skipping any with a flag in `skipFlags`.
inline const PropDesc* FindPropOfType(const LayoutResult& layout, const char* type,
                                      uint64_t skipFlags = 0)
{
    for (int i = 0; i < layout.props; ++i) {
        if ((layout.p[i].flags & skipFlags) != 0) continue;
        if (NameEquals(layout.p[i].type, type)) return &layout.p[i];
    }
    return nullptr;
}

// First parameter whose name starts with `prefix` (Blueprint pins are sometimes
// suffixed, e.g. "Value_12_ABCD...").
inline const PropDesc* FindPropPrefix(const LayoutResult& layout, const char* prefix)
{
    for (int i = 0; i < layout.props; ++i) {
        const char* n = layout.p[i].name;
        int k = 0;
        while (prefix[k] && n[k] == prefix[k]) ++k;
        if (prefix[k] == 0 && (n[k] == 0 || n[k] == '_')) return &layout.p[i];
    }
    return nullptr;
}

inline void ErrSet(char* err, int cap, const char* a, const char* b = "")
{
    if (!err || cap <= 0) return;
    int w = 0;
    for (int i = 0; a[i] && w < cap - 1; ++i) err[w++] = a[i];
    for (int i = 0; b[i] && w < cap - 1; ++i) err[w++] = b[i];
    err[w] = 0;
}

// ProcessEvent with a fault guard.  A Blueprint that faults inside the VM is a crash
// either way, but a fault in OUR argument handling must be reported, not fatal.
inline bool ProcessEventGuarded(void* obj, void* fn, void* parms)
{
    __try {
        ProcessEvent(obj, fn, parms);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Invoke `function` on `obj` with named arguments.  `sig` must come from Signature().
// `outBuf` (optional, >= sig.parms_size) receives the whole param block after the call
// so callers can read any out parameter.  Returns false, leaving the game untouched,
// if any argument name is absent from the signature.
inline bool Invoke(void* obj, void* function, const LayoutResult& sig,
                   const ArgList& args, ReadBack* out, char* err, int errCap,
                   uint8_t* outBuf = nullptr)
{
    if (err && errCap > 0) err[0] = 0;
    if (!obj || !function) { ErrSet(err, errCap, "null object or function"); return false; }
    if (!sig.valid) { ErrSet(err, errCap, "no validated signature: ", sig.note); return false; }

    alignas(16) uint8_t buf[1024];
    int need = sig.parms_size;
    if (need < 0 || need > (int)sizeof(buf)) { ErrSet(err, errCap, "param block size out of range"); return false; }
    memset(buf, 0, sizeof(buf));

    for (int i = 0; i < args.n; ++i) {
        const Arg& a = args.a[i];
        const PropDesc* d = FindProp(sig, a.name);
        if (!d) { ErrSet(err, errCap, "no such parameter: ", a.name); return false; }
        uint8_t* dst = buf + d->offset;
        switch (a.kind) {
        case Arg::I32:
            if (d->size < 4) { ErrSet(err, errCap, "int into small param: ", a.name); return false; }
            *(int32_t*)dst = a.i32;
            break;
        case Arg::BOOL:
            *(uint8_t*)dst = a.b;
            break;
        case Arg::FNAME:
            if (d->size < 8) { ErrSet(err, errCap, "name into small param: ", a.name); return false; }
            *(QdName*)dst = a.fname;
            break;
        case Arg::OBJECT:
            if (d->size < 8) { ErrSet(err, errCap, "object into small param: ", a.name); return false; }
            *(void**)dst = a.obj;
            break;
        case Arg::RAW:
            if (d->size < a.raw_len) { ErrSet(err, errCap, "raw value larger than param: ", a.name); return false; }
            memcpy(dst, a.raw, (size_t)a.raw_len);
            break;
        }
    }

    if (!ProcessEventGuarded(obj, function, buf)) {
        ErrSet(err, errCap, "ProcessEvent faulted");
        return false;
    }

    if (out) {
        out->value = 0;
        out->was_set = false;
        if (sig.ret_offset != 0xFFFF && sig.ret_offset + 4 <= need) {
            out->value = *(int32_t*)(buf + sig.ret_offset);
        } else if (const PropDesc* ov = FindPropOfType(sig, "IntProperty")) {
            // No return value: Blueprint functions hand results back through out
            // params (GetNamedSaveData -> "Value").  Take the first int out param.
            for (int i = 0; i < sig.props; ++i) {
                const PropDesc& d = sig.p[i];
                if ((d.flags & CPF_OutParm) && NameEquals(d.type, "IntProperty")) {
                    out->value = *(int32_t*)(buf + d.offset);
                    break;
                }
            }
            (void)ov;
        }
        if (const PropDesc* ws = FindPropPrefix(sig, "WasSet")) {
            out->was_set = *(uint8_t*)(buf + ws->offset) != 0;
        }
    }
    if (outBuf) memcpy(outBuf, buf, (size_t)need);
    return true;
}

// ---------------------------------------------------------------------------
// Cached function + raw parameter block
// ---------------------------------------------------------------------------
// For calls made every frame (bone posing) the signature walk must happen once, and
// for parameters wider than ArgList's raw slots (FTransform, FSoftObjectPtr) the caller
// writes the block directly.  Fields are still located BY NAME from the signature.
struct Fn {
    void*        fn;
    bool         tried;
    LayoutResult sig;

    bool Resolve(void* objOrNull, const char* name, const void* cls = nullptr)
    {
        if (fn && sig.valid) return true;
        if (tried) return false;
        tried = true;
        if (cls) fn = FindFunctionInClass(cls, name);
        else fn = FindFunction(objOrNull, name);
        sig = Signature(fn);
        if (!sig.valid) {
            QDLOG(LOG_WARN, "fn ", name, ": ", (fn ? sig.note : "not found"));
            fn = nullptr;
            return false;
        }
        return true;
    }
    int Off(const char* param) const
    {
        const PropDesc* d = FindProp(sig, param);
        if (!d) d = FindPropPrefix(sig, param);
        return d ? d->offset : -1;
    }
    int RetOff() const { return sig.ret_offset == 0xFFFF ? -1 : sig.ret_offset; }
};

struct Call {
    alignas(16) uint8_t buf[1024];
    const Fn& f;
    bool bad;
    explicit Call(const Fn& fn) : f(fn), bad(!fn.fn || !fn.sig.valid || fn.sig.parms_size > 1024)
    {
        memset(buf, 0, sizeof(buf));
    }
    uint8_t* At(const char* param, int len)
    {
        int o = f.Off(param);
        if (o < 0 || o + len > f.sig.parms_size) { bad = true; return nullptr; }
        return buf + o;
    }
    void Set(const char* param, const void* v, int len) { if (uint8_t* p = At(param, len)) memcpy(p, v, (size_t)len); }
    void Obj(const char* param, void* v)    { Set(param, &v, 8); }
    void I32(const char* param, int32_t v)  { Set(param, &v, 4); }
    void F32(const char* param, float v)    { Set(param, &v, 4); }
    void U8(const char* param, uint8_t v)   { Set(param, &v, 1); }
    void Name(const char* param, const QdName& v) { Set(param, &v, 8); }
    bool Run(void* obj)
    {
        if (bad || !obj) return false;
        return ProcessEventGuarded(obj, f.fn, buf);
    }
    uint8_t* Ret() { int o = f.RetOff(); return o >= 0 ? buf + o : nullptr; }
};

// Dump a struct's own properties (and its supers'), for when a lookup misses.
inline void LogPropertiesOf(const void* ustruct, const char* label, int limit)
{
    const Layout& L = Lay();
    if (!L.valid) return;
    int shown = 0;
    for (const void* s = ustruct; s && shown < limit; s = SuperOf(s)) {
        LayoutResult r;
        if (!WalkFieldsRaw(s, L, false, &r)) break;
        for (int i = 0; i < r.props && shown < limit; ++i, ++shown) {
            QDLOG(LOG_INFO, "    prop ", label, ".", r.p[i].name, " : ", r.p[i].type,
                  " @", dec(r.p[i].offset), " size=", dec(r.p[i].size));
        }
    }
}

// Dump a function's signature into the log.
inline void LogSignature(const char* label, const LayoutResult& l)
{
    QDLOG(LOG_INFO, "signature ", label, " props=", dec(l.props), " parms=", dec(l.parms_size),
          " ret@", dec(l.ret_offset == 0xFFFF ? -1 : l.ret_offset));
    for (int i = 0; i < l.props; ++i) {
        QDLOG(LOG_INFO, "    ", l.p[i].name, " : ", l.p[i].type, " @", dec(l.p[i].offset),
              " size=", dec(l.p[i].size), " flags=", hex32((uint32_t)l.p[i].flags));
    }
}

} // namespace qd
