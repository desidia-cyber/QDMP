// qddlgbox.h -- [view-fix2] the TEXT inside the game's own dialogue widget.
//
// THE DEFECT THIS EXISTS FOR
// --------------------------
// The spectator machine (B) brings up the game's own `DialogueScreen_C` widget and sets the
// manager's cursor fields (`CurrentDialogue`, `CurrentDialogueSequenceIndex`,
// `CurrentDialogueNPC`), but its box keeps showing the widget's DESIGN-TIME placeholder:
// measured on the rig, `dialogue.md`'s screenshots show "Bored Knight / The locked chest?
// Unfortunately Nu took the key with him into the mausoleum..." while the talker is having a
// different conversation (the user's own report, and the pair the coordinator inspected:
// inst2 = "Spirit of Erma, Matron of the Deep / For all my scrying and divination...",
// inst1 = the Bored Knight placeholder).  `ShowDialogueSequenceEntry` was called on the
// MANAGER, where it does not exist (it is the WIDGET's graph event), so nothing on the
// spectator ever populated the two text blocks.
//
// WHAT THIS MODULE DOES
// ---------------------
// Reads and writes the two blocks the widget itself fills on the talking machine:
//     `DialogueSequenceText`  (FB_AnimRichTextBlock)  the line
//     `NPCName`               (a text block inside `NPCNameBox`)  the speaker
// both names taken from the cooked package's own name table (`DialogueScreen.uasset`, 619
// names, `analysis/pak/code/pakread.py`), so the lookup is by the game's names, not a guess.
//
// Text crosses the FText boundary through the engine's own library functions
// (`UKismetTextLibrary::Conv_TextToString` / `Conv_StringToText`), which is what the game
// itself uses (`KismetTextLibrary` is an import of both `DialogueManager` and `DialogueScreen`).
// FText is never parsed or rebuilt by hand: the value the widget holds is copied as bytes when
// text moves between two widget blocks, and converted only for the log and the wire (UTF-8).
//
// WHERE THE TEXT COMES FROM (and why it is not resolved from the asset)
// --------------------------------------------------------------------
// The honest source is the machine that is having the conversation: its box is already
// showing exactly the right speaker and line, with the game's own format-key substitution
// (`ApplyFormatKeysToDialogue`) and its `bHideName` rule applied.  So B renders the TALKER'S
// OWN rendered text.  Resolving `FS_DialogueData` -> `Greetings[i]` -> `Entries[j]` -> FText on
// B would need the inner struct layouts of two cooked struct properties, and would still not
// reproduce the format-key pass; the text is a few hundred bytes once per line, and the wire
// body carries it next to the cursor it belongs to (see `DialogueViewBody.speaker/.line`).
//
// MEASURED SAFETY RULES
// ---------------------
//  * `Conv_StringToText` reads its `const FString&` and copies: passing a TArray header that
//    points at this module's own buffer is safe because nothing assigns to the parameter.
//  * `Conv_TextToString` RETURNS an FString the engine allocated.  Its storage is deliberately
//    left to the engine (only reached when the cursor changes, a few hundred bytes per line);
//    this file never calls FMemory::Free, which is not observable from the UObject surface.
//  * Every engine read is inside __try: a wrong offset must cost a log line, not the process.
#pragma once
#include <stdint.h>

namespace dlgbox {

// The two blocks, by the names in the cooked `DialogueScreen` package.
static const char* const kLineBlock = "DialogueSequenceText";
static const char* const kNameBlock = "NPCName";

static const int kMaxText   = 512;   // a dialogue line in this content is < 300 chars
static const int kMaxWidget = 256;

struct Block {
    void* w;
    int   idx;          // its GUObjectArray slot: the engine creates the CDO's design-time
                        // blocks at LOAD and a widget instance's own blocks later, so a
                        // higher index is the live one.  Measured: the talker's screen draws
                        // BOTH the design-time string and the real line on top of each other,
                        // which is exactly two blocks of the same role coexisting.
    char  name[64];
    char  cls[80];
    char  text[kMaxText];
};

// ---- UTF-8 <-> wide (the wire and the log are UTF-8; FString is UTF-16) ----
inline int Utf8ToWide(const char* s, wchar_t* out, int cap)
{
    int o = 0;
    if (!s) { if (cap > 0) out[0] = 0; return 0; }
    for (int i = 0; s[i];) {
        unsigned char c = (unsigned char)s[i];
        unsigned int cp;
        int n;
        if (c < 0x80) { cp = c; n = 0; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; n = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 2; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; n = 3; }
        else { cp = '?'; n = 0; }
        ++i;
        for (int k = 0; k < n; ++k, ++i) {
            unsigned char cc = (unsigned char)s[i];
            if ((cc & 0xC0) != 0x80) break;
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (o + 1 >= cap) break;
        out[o++] = (wchar_t)(cp > 0xFFFF ? '?' : cp);
    }
    if (cap > 0) out[o < cap ? o : cap - 1] = 0;
    return o;
}

inline int WideToUtf8(const wchar_t* w, int n, char* out, int cap)
{
    int o = 0;
    if (!out || cap <= 0) return 0;
    for (int i = 0; i < n && w[i]; ++i) {
        unsigned int cp = (unsigned int)w[i];
        if (cp < 0x80) { if (o + 1 >= cap) break; out[o++] = (char)cp; }
        else if (cp < 0x800) {
            if (o + 2 >= cap) break;
            out[o++] = (char)(0xC0 | (cp >> 6));
            out[o++] = (char)(0x80 | (cp & 0x3F));
        } else {
            if (o + 3 >= cap) break;
            out[o++] = (char)(0xE0 | (cp >> 12));
            out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[o++] = (char)(0x80 | (cp & 0x3F));
        }
    }
    out[o] = 0;
    return o;
}

// The RETURN parameter of a given property type.  `FindPropOfType(sig, t, CPF_Parm)` cannot
// be used for this: the return value carries CPF_Parm as well, so it would be the one entry
// the skip filter removed.
inline const PropDesc* ReturnProp(const LayoutResult& sig, const char* type)
{
    for (int i = 0; i < sig.props; ++i) {
        if ((sig.p[i].flags & CPF_ReturnParm) && NameEquals(sig.p[i].type, type)) return &sig.p[i];
    }
    return nullptr;
}

inline void* TextLibrary()
{
    static void* cdo = nullptr;
    if (!cdo || !ObjectIsLive(cdo)) cdo = FindCdo("KismetTextLibrary");
    return cdo;
}

// A widget's FText (already inside the widget's memory) -> UTF-8.
inline bool ToUtf8(const void* ftext, int flen, char* out, int cap)
{
    if (out && cap > 0) out[0] = 0;
    void* cdo = TextLibrary();
    if (!cdo || !ftext || !out || cap <= 0) return false;
    static Fn f = {};
    if (!f.Resolve(cdo, "Conv_TextToString")) return false;
    const PropDesc* p = FindPropOfType(f.sig, "TextProperty");
    const PropDesc* r = ReturnProp(f.sig, "StrProperty");
    if (!p || !r || p->size < 8 || flen < p->size) return false;
    Call c(f);
    c.Set(p->name, ftext, p->size);
    if (!c.Run(cdo)) return false;
    struct FS { wchar_t* data; int32_t num; int32_t max; } fs = {};
    __try {
        fs = *(FS*)(c.buf + r->offset);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    if (!fs.data || fs.num <= 0 || fs.num > 8192) return false;
    WideToUtf8(fs.data, fs.num - 1, out, cap);
    return true;
}

// UTF-8 -> an FText value in `out` (the property's own size, 16 bytes on this build).
inline bool FromUtf8(const char* s, uint8_t* out, int outLen)
{
    if (!out || outLen < 8) return false;
    memset(out, 0, (size_t)outLen);
    void* cdo = TextLibrary();
    if (!cdo || !s) return false;
    static Fn f = {};
    if (!f.Resolve(cdo, "Conv_StringToText")) return false;
    const PropDesc* p = FindPropOfType(f.sig, "StrProperty");
    const PropDesc* r = ReturnProp(f.sig, "TextProperty");
    if (!p || !r || p->size < 16 || r->size > outLen || r->size < 8) return false;
    static wchar_t wide[1024];
    int n = Utf8ToWide(s, wide, 1024);
    struct FS { wchar_t* data; int32_t num; int32_t max; } fs;
    fs.data = wide; fs.num = n + 1; fs.max = n + 1;      // read-only for the callee
    Call c(f);
    c.Set(p->name, &fs, 16);
    if (!c.Run(cdo)) return false;
    __try {
        memcpy(out, c.buf + r->offset, (size_t)r->size);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}

// ---- the widget tree -----------------------------------------------------
inline void* TreeOf(void* userWidget)
{
    return userWidget ? ObjPropC(userWidget, "WidgetTree") : nullptr;
}

inline void Describe(void* w, char* name, int ncap, char* cls, int ccap)
{
    if (name && ncap > 0) { name[0] = 0; FNameToString(ObjNameRaw(w), name, ncap); }
    if (cls && ccap > 0) { cls[0] = 0; FNameToString(ObjNameRaw(ObjClass(w)), cls, ccap); }
}

// Direct read of a widget's `Text` property (an FText, no call).
inline int TextOf(void* w, char* out, int cap)
{
    if (out && cap > 0) out[0] = 0;
    if (!w) return 0;
    int size = 0;
    int off = FindPropertyOffsetIn(ObjClass(w), "Text", &size);
    if (off < 0 || size < 8 || size > 64) return 0;
    bool ok = false;
    __try { ok = ToUtf8((uint8_t*)w + off, size, out, cap); }
    __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    return ok ? 1 : 0;
}

// Look a widget up by object name anywhere in GUObjectArray.  A name match alone is not
// enough: the widget Blueprint's CDO tree holds same-named blocks, so an object whose outer
// chain reaches a `Default__` object is skipped.
inline void* ByName(const char* name)
{
    if (!name || !name[0]) return nullptr;
    void* textCls = FindClassByName("Text");
    for (int i = 0; i < ObjectCount(); ++i) {
        void* o = ObjectAtSafe(i);
        if (!o || !LooksLikeUObject(o)) continue;
        char on[96];
        if (FNameToString(ObjNameRaw(o), on, sizeof(on)) <= 0) continue;
        if (strcmp(on, name) != 0) continue;
        if (textCls) {
            bool sub = false;
            __try { sub = IsSubclassOf(ObjClass(o), textCls); }
            __except (EXCEPTION_EXECUTE_HANDLER) { sub = false; }
            if (!sub) continue;
        }
        bool inCdo = false;
        void* outer = ObjOuter(o);
        for (int d = 0; outer && d < 16; ++d) {
            char cn[96];
            if (FNameToString(ObjNameRaw(outer), cn, sizeof(cn)) > 0 &&
                strncmp(cn, "Default__", 9) == 0) { inCdo = true; break; }
            outer = ObjOuter(outer);
        }
        if (inCdo) continue;
        return o;
    }
    return nullptr;
}

// Is this object inside the given widget's own outer chain?  That is what distinguishes the
// LIVE block from a same-named block in the class's design-time tree, and it is the
// distinction the first two runs missed: measured (`_test/sview-fix2/shots/view-fix2-npc1-1-inst2.png`)
// the talker's screen draws BOTH strings on top of each other, so the object called
// `DialogueSequenceText` that a bare name search finds is the design-time one, while the line
// actually being read comes from another object with the same role.
inline bool InsideOuter(void* o, const void* root, int maxDepth)
{
    for (void* p = ObjOuter(o); p && maxDepth-- > 0; p = ObjOuter(p)) {
        if (p == root) return true;
    }
    return false;
}

// Every widget the LIVE widget owns or contains.  Routes, in order:
//   1. an Outer scan from the widget and its tree, in widening passes -- the only route that
//      cannot be defeated by a property offset, because a UUserWidget's sub-widgets are all
//      outered into it;
//   2. `WidgetTree.AllWidgets`, for a build that exposes it;
//   3. by object name in GUObjectArray, restricted to objects inside the live widget.
inline void Take(void* w, Block* out, int cap, int* n)
{
    if (!w || *n >= cap || !LooksLikeUObject(w)) return;
    for (int k = 0; k < *n; ++k) if (out[k].w == w) return;
    Block& b = out[*n];
    b.w = w;
    b.idx = -1;
    __try { b.idx = *(int32_t*)((const uint8_t*)w + 0x0C); }   // UObject::InternalIndex
    __except (EXCEPTION_EXECUTE_HANDLER) { b.idx = -1; }
    Describe(w, b.name, sizeof(b.name), b.cls, sizeof(b.cls));
    TextOf(w, b.text, sizeof(b.text));
    ++(*n);
}

inline int CollectByOuter(void* userWidget, void* tree, Block* out, int cap, int have)
{
    if (!userWidget) return have;
    void* widgetCls = FindClassByName("Widget");
    int total = ObjectCount();
    for (int pass = 0; pass < 6; ++pass) {
        int before = have;
        for (int i = 0; i < total && have < cap; ++i) {
            void* o = ObjectAtSafe(i);
            if (!o || !LooksLikeUObject(o)) continue;
            void* outer = ObjOuter(o);
            // pass 0: children of the user widget or its tree; later passes: children of
            // whatever the previous pass found (so a nested panel's children are reached).
            bool parent = (outer == userWidget) || (tree && outer == tree);
            if (!parent && pass > 0) {
                for (int k = 0; k < have; ++k)
                    if (out[k].w == outer) { parent = true; break; }
            }
            if (!parent) continue;
            if (widgetCls) {
                bool isW = false;
                __try { isW = IsSubclassOf(ObjClass(o), widgetCls); }
                __except (EXCEPTION_EXECUTE_HANDLER) { isW = false; }
                if (!isW) continue;
            }
            Take(o, out, cap, &have);
        }
        if (have == before) break;      // nothing new: the tree is exhausted
    }
    return have;
}

inline int Collect(void* userWidget, Block* out, int cap)
{
    if (!out || cap <= 0) return 0;
    void* tree = TreeOf(userWidget);
    int n = 0;
    Take(userWidget, out, cap, &n);
    // ROUTE 1 FIRST, ALWAYS.  Run 2 short-circuited here (`if (n <= 1)`) after the by-name
    // route had already returned the design-time blocks, so the real tree was never walked.
    n = CollectByOuter(userWidget, tree, out, cap, n);
    if (tree && ObjectIsLive(tree)) {
        int size = 0;
        int off = FindPropertyOffsetIn(ObjClass(tree), "AllWidgets", &size);
        if (off >= 0) {
            __try {
                void** data = *(void***)((uint8_t*)tree + off);
                int32_t num = *(int32_t*)((uint8_t*)tree + off + 8);
                if (num > 0 && num < 4096 && data) {
                    for (int i = 0; i < num && n < cap; ++i) Take(data[i], out, cap, &n);
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
    }
    // Route 3: the two blocks by name, but ONLY the instance that lives inside this widget.
    {
        const char* names[2] = {kLineBlock, kNameBlock};
        for (int k = 0; k < 2 && n < cap; ++k) {
            void* w = ByName(names[k]);
            if (!w) continue;
            if (!InsideOuter(w, userWidget, 16) && !(tree && InsideOuter(w, tree, 16))) continue;
            Take(w, out, cap, &n);
        }
    }
    if (n > 1) return n;
    // Last: walk RootWidget's panels.  `UPanelWidget::GetChildrenCount` / `GetChildAt`.
    void* root = tree ? ObjPropC(tree, "RootWidget") : nullptr;
    static void* panelCls = nullptr;
    if (!panelCls || !ObjectIsLive(panelCls)) panelCls = FindClassByName("PanelWidget");
    static Fn fCount = {}, fAt = {};
    struct W { void* w; int depth; };
    W stack[64];
    int sp = 0;
    if (root && ObjectIsLive(root)) stack[sp++] = W{root, 0};
    while (sp > 0 && n < cap) {
        W cur = stack[--sp];
        Block& b = out[n];
        b.w = cur.w;
        Describe(cur.w, b.name, sizeof(b.name), b.cls, sizeof(b.cls));
        TextOf(cur.w, b.text, sizeof(b.text));
        ++n;
        if (cur.depth >= 8 || !panelCls) continue;
        bool panel = false;
        __try { panel = IsSubclassOf(ObjClass(cur.w), panelCls); }
        __except (EXCEPTION_EXECUTE_HANDLER) { panel = false; }
        if (!panel) continue;
        int cnt = 0;
        if (fCount.Resolve(cur.w, "GetChildrenCount")) {
            Call c(fCount);
            if (c.Run(cur.w) && c.Ret()) {
                __try { cnt = *(int32_t*)c.Ret(); } __except (EXCEPTION_EXECUTE_HANDLER) { cnt = 0; }
            }
        }
        if (cnt <= 0 || cnt > 64) continue;
        for (int i = cnt - 1; i >= 0 && sp < 64; --i) {
            if (!fAt.Resolve(cur.w, "GetChildAt")) break;
            void* child = nullptr;
            {
                Call c(fAt);
                const PropDesc* ip = FindPropOfType(fAt.sig, "IntProperty");
                if (ip) c.I32(ip->name, i);
                if (c.Run(cur.w) && c.Ret()) {
                    __try { child = *(void**)c.Ret(); } __except (EXCEPTION_EXECUTE_HANDLER) { child = nullptr; }
                }
            }
            if (child && LooksLikeUObject(child)) stack[sp++] = W{child, cur.depth + 1};
        }
    }
    return n;
}

// The collection is cached: `Collect` walks GUObjectArray, and the applier runs at 10 Hz and
// asks for the blocks several times per pass.  The cache is keyed on the widget AND on time,
// because a widget Blueprint only creates its children once but the object table is recycled
// under streaming churn -- a name that matched a freed block must not be written into.
struct Cache {
    void*     widget;
    uint32_t  atMs;
    int       n;
    Block     b[kMaxWidget];
};
inline Cache& Cached(void* userWidget, uint32_t now)
{
    static Cache c = {};
    const bool stale = (c.widget != userWidget) || (now - c.atMs > 1000) || c.n <= 0;
    if (stale && userWidget) {
        c.widget = userWidget;
        c.atMs = now;
        c.n = Collect(userWidget, c.b, kMaxWidget);
    }
    return c;
}

// THE BLOCKS BELONGING TO THE WIDGET THE GAME IS SHOWING.
//
// MEASURED (run 2): the talker's screen drew BOTH strings on top of each other — the real
// line "For all my scrying and divination…" AND the design-time preview "…mausoleum. Such
// bitter luck…".  That is two `DialogueScreen_C` instances in the viewport: the game's own
// (created when the conversation started) and one created by a READ-ONLY dump lever that had
// called `DlgEnsureWidget` to get something to look at.  So a block is the RIGHT one when it
// lives inside the widget the manager currently points at, and the design-time preview lives
// in the other instance's tree.
//
// `InsideOuter` is what makes that distinction; `idx` (its GUObjectArray slot) breaks the tie
// when both are inside, because the engine creates the class's design-time tree at load time
// and a widget instance's own blocks later.
inline int FindAll(void* userWidget, const char* name, void** out, int cap, uint32_t now)
{
    Cache& c = Cached(userWidget, now);
    int pick[32];
    int n = 0;
    const int limit = (cap < 32) ? cap : 32;
    for (int i = 0; i < c.n && n < limit; ++i)
        if (c.b[i].name[0] && strcmp(c.b[i].name, name) == 0) out[n++] = c.b[i].w;
    if (n == 0) {
        for (int i = 0; i < c.n && n < limit; ++i) {
            if (!c.b[i].name[0]) continue;
            if (strstr(c.b[i].name, name)) out[n++] = c.b[i].w;
        }
    }
    // Rank: inside the live widget first, then by GUObjectArray slot descending (newest).
    for (int i = 0; i < n; ++i) {
        pick[i] = -1;
        for (int k = 0; k < c.n; ++k) if (c.b[k].w == out[i]) { pick[i] = c.b[k].idx; break; }
        if (!InsideOuter(out[i], userWidget, 16)) pick[i] -= 100000;
    }
    for (int i = 1; i < n; ++i) {
        void* wv = out[i];
        int iv = pick[i];
        int j = i - 1;
        while (j >= 0 && pick[j] < iv) { out[j + 1] = out[j]; pick[j + 1] = pick[j]; --j; }
        out[j + 1] = wv;
        pick[j + 1] = iv;
    }
    return n;
}

inline void* Find(void* userWidget, const char* name, uint32_t now)
{
    void* one[16];
    int n = FindAll(userWidget, name, one, 16, now);
    return n > 0 ? one[0] : nullptr;
}

// Write `s` into EVERY block of that name.  Returns a bitmask of what worked per block:
// bit0 = the widget's own `SetText` ran, bit1 = the `Text` property was written directly.
inline int SetText(void* userWidget, const char* name, const char* s, uint32_t now)
{
    void* blocks[16];
    int nb = FindAll(userWidget, name, blocks, 16, now);
    if (nb == 0) return 0;
    uint8_t ft[32];
    if (!FromUtf8(s, ft, sizeof(ft))) return 0;
    int result = 0;
    for (int k = 0; k < nb; ++k) {
        void* w = blocks[k];
        int how = 0;
        Fn f = {};
        if (f.Resolve(w, "SetText")) {
            const PropDesc* p = FindPropOfType(f.sig, "TextProperty");
            if (p && p->size >= 8 && p->size <= (int)sizeof(ft)) {
                Call c(f);
                c.Set(p->name, ft, p->size);
                if (c.Run(w)) how = 1;
            }
        }
        if (!how) {
            int size = 0;
            int off = FindPropertyOffsetIn(ObjClass(w), "Text", &size);
            if (off >= 0 && size >= 8 && size <= (int)sizeof(ft)) {
                __try { memcpy((uint8_t*)w + off, ft, (size_t)size); how = 2; }
                __except (EXCEPTION_EXECUTE_HANDLER) { how = 0; }
            }
        }
        if (how) result |= (k == 0 ? how : how << 2);
    }
    return result;
}

// The rich-text block prints its line letter by letter; `StopPrintAndShowText` is the widget's
// own "show it all now" (its export is in the DialogueScreen name table).  Run on EVERY block
// of that role, because the ranking in `FindAll` prefers the live one but a build may have
// more than one.
inline bool ShowWholeLine(void* userWidget, const char* name, uint32_t now)
{
    void* blocks[16];
    int nb = FindAll(userWidget, name, blocks, 16, now);
    bool any = false;
    for (int k = 0; k < nb; ++k) {
        static Fn f = {};
        if (f.Resolve(blocks[k], "StopPrintAndShowText")) {
            Call c(f);
            if (f.sig.props == 0 && c.Run(blocks[k])) any = true;
            continue;
        }
        void* screen = Find(userWidget, "DialogueScreen", now);
        if (!screen || !f.Resolve(screen, "StopPrintAndShowText")) continue;
        Call c(f);
        if (f.sig.props == 0 && c.Run(screen)) any = true;
        break;      // one call on the screen covers the whole widget
    }
    return any;
}

// The speaker and line the box is showing right now, read from the BEST-ranked block of each
// role: inside the live widget first, then newest.  Returns bit0 = a speaker was read, bit1 =
// a line.
//
// READ ONE BLOCK, NOT ALL OF THEM.  A join of every candidate would make the read-back
// comparison (`exact=`) fail on a build that has a design-time twin even when the write went
// to the right block, so the log would say "not exact" while the screen was correct.  The
// other candidates are reported separately, by `Dump`.
inline int ReadBox(void* userWidget, char* speaker, int scap, char* line, int lcap, uint32_t now)
{
    if (speaker && scap > 0) speaker[0] = 0;
    if (line && lcap > 0) line[0] = 0;
    if (!userWidget) return 0;
    void* blocks[16];
    int got = 0;
    int nb = FindAll(userWidget, kNameBlock, blocks, 16, now);
    if (nb > 0 && TextOf(blocks[0], speaker, scap)) got |= 1;
    nb = FindAll(userWidget, kLineBlock, blocks, 16, now);
    if (nb > 0 && TextOf(blocks[0], line, lcap)) got |= 2;
    return got;
}

// Every block of a role and what each holds — the diagnostic that says whether a second,
// older block is still carrying the design-time text.
inline void DumpRole(void* userWidget, const char* name, uint32_t now)
{
    void* blocks[16];
    int nb = FindAll(userWidget, name, blocks, 16, now);
    for (int k = 0; k < nb && k < 6; ++k) {
        char t[kMaxText] = "";
        TextOf(blocks[k], t, sizeof(t));
        char cn[80] = "";
        FNameToString(ObjNameRaw(ObjClass(blocks[k])), cn, sizeof(cn));
        QDLOG(LOG_INFO, "    role ", name, "[", dec(k), "] [", cn, "] inside=",
              dec(InsideOuter(blocks[k], userWidget, 16) ? 1 : 0), " text='", t, "'");
    }
}

// The whole tree with its text, for the log.  `label` names the machine in the line.
inline void Dump(void* userWidget, const char* label, uint32_t now)
{
    Cache& c = Cached(userWidget, now);
    QDLOG(LOG_INFO, "  box ", (label ? label : "-"), ": ", dec(c.n),
          " widget(s) found");
    for (int i = 0; i < c.n && i < 40; ++i) {
        if (!c.b[i].text[0] && !strstr(c.b[i].cls, "Text") && !strstr(c.b[i].cls, "Rich"))
            continue;
        QDLOG(LOG_INFO, "    box.w ", c.b[i].name, " [", c.b[i].cls, "] text='",
              c.b[i].text, "'");
    }
}

} // namespace dlgbox
