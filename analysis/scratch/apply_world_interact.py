#!/usr/bin/env python3
"""Apply the world-interact wiring to the SHARED files.

Run from the sync-fix worktree root while holding the BUILD lock:
    python analysis/scratch/apply_world_interact.py

Every edit is an exact, unique string replacement; each anchor must occur exactly
once or the script aborts without writing anything.  This is the same set of edits
the owner would make with the editor tool -- kept in one place so the lock is held
for seconds rather than many round trips, and so the coordinator can audit exactly
what changed in the shared files.
"""
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SRC = os.path.join(ROOT, "mod", "native", "src")
PROXY = os.path.join(SRC, "proxy.cpp")
CONF = os.path.join(SRC, "qdconfig.h")
INI = os.path.join(ROOT, "mod", "qdcoop.ini")

EDITS = []   # (path, old, new, label)


def E(path, old, new, label):
    EDITS.append((path, old, new, label))


# ---------------------------------------------------------------------------
# proxy.cpp
# ---------------------------------------------------------------------------
E(PROXY, '#include "qdobjectstate.h"',
        '#include "qdobjectstate.h"\n#include "qdinteract.h"',
        "include qdinteract.h")

E(PROXY,
"""bool ObjectApplyToActor(void* actor, objects::Shape sh, uint8_t want,
                        char* how, int howCap, char* err, int errCap);
""",
"""bool ObjectApplyToActor(void* actor, objects::Shape sh, uint8_t want,
                        char* how, int howCap, char* err, int errCap);
int  ObjectCallStep(void* actor, const interact::Step& step, uint8_t want,
                    char* why, int cap);
const char* ObjectAnimPlan(void* actor, objects::Shape sh, uint8_t want,
                           char* why, int cap);
""",
        "forward declarations")

E(PROXY,
'          " objects=", dec(c.object_sync),\n',
'          " objects=", dec(c.object_sync),\n'
'          " interact=", dec(c.interact_sync),\n',
        "switches log")

E(PROXY,
"        if (!objects::ApplyAllowed(want, local, haveLocal)) {",
"        if (!interact::ApplyAllowed(want, local, haveLocal,\n"
"                                    (objects::Shape)g_objActors[idx].shape)) {",
        "object intent direction")

E(PROXY,
"        if (!objects::ApplyAllowed(row.state, local, haveLocal)) {",
"        if (!interact::ApplyAllowed(row.state, local, haveLocal,\n"
"                                    (objects::Shape)g_objActors[idx].shape)) {",
        "object apply direction")

E(PROXY,
"        objects::Shape sh = objects::ShapeOfClass(cn);",
"        objects::Shape sh = interact::ShapeOfClass(cn);",
        "scan classification")

E(PROXY,
"            objects::Shape sh = objects::ShapeOfClass(cls);",
"            objects::Shape sh = interact::ShapeOfClass(cls);",
        "savable dump classification")

E(PROXY,
"""bool ObjectStateOf(void* actor, objects::Shape sh, uint8_t* out, const char** used)
{
    if (out) *out = 0;
    if (used) *used = nullptr;
    bool b = false;
    const char* p = objects::StatePropFor(sh);
    if (p && ObjectBoolRead(actor, p, &b)) {
        if (out) *out = b ? 1 : 0;
        if (used) *used = p;
        return true;
    }
    const char* a = objects::StatePropAltFor(sh);
    if (a && ObjectBoolRead(actor, a, &b)) {
        bool inverted = (strcmp(a, "DoorClosed") == 0);
        if (out) *out = (inverted ? !b : b) ? 1 : 0;
        if (used) *used = a;
        return true;
    }
    return false;
}
""",
"""bool ObjectStateOf(void* actor, objects::Shape sh, uint8_t* out, const char** used)
{
    if (out) *out = 0;
    if (used) *used = nullptr;
    bool b = false;
    // The family's own state bool, then two alternates.  `interact::Prop` carries the
    // polarity, so `DoorClosed` is inverted in one place rather than at every reader.
    interact::Prop probes[3] = { interact::StateProp(sh), interact::StateAlt(sh),
                                 interact::StateAlt2(sh) };
    for (int i = 0; i < 3; ++i) {
        const char* p = probes[i].name;
        if (p && ObjectBoolRead(actor, p, &b)) {
            if (out) *out = (probes[i].inverted ? !b : b) ? 1 : 0;
            if (used) *used = p;
            return true;
        }
    }
    return false;
}
""",
        "ObjectStateOf uses interact tables")

E(PROXY,
"""// Put one object into the wanted state on THIS machine, visually and in its own state
// bool, without running the interaction verb.  `how` names what actually ran, so the live
// line says whether the class took the visual path, the restore path, or only the bool.
bool ObjectApplyToActor(void* actor, objects::Shape sh, uint8_t want,
""",
"""// One entry of `interact::Plan`: resolve the class' own function and call it with the
// WANTED direction.  `M_BOOL` passes the direction to a one-bool visual (SetToOpenState,
// which BOTH OpenDoor and CloseDoor call); a visual with no parameter is only called for
// `want != 0`, because a bare open call cannot close.  1 = called, 0 = absent,
// -1 = present but refused/failed.  A forbidden name never reaches ProcessEvent.
int ObjectCallStep(void* actor, const interact::Step& step, uint8_t want,
                   char* why, int cap)
{
    if (why && cap > 0) why[0] = 0;
    if (!actor || !step.fn || !step.fn[0]) return 0;
    if (interact::Forbidden(step.fn)) {
        ErrSet(why, cap, "refused: interaction/payout verb ", step.fn);
        return -1;
    }
    Fn f;
    bool resolved = false;
    __try { resolved = f.Resolve(actor, step.fn); }
    __except (EXCEPTION_EXECUTE_HANDLER) { ErrSet(why, cap, "fault resolving ", step.fn); return -1; }
    if (!resolved) return 0;

    const PropDesc* b = FindPropOfType(f.sig, "BoolProperty", CPF_OutParm | CPF_ReturnParm);
    Call c(f);
    if (step.mode == interact::M_BOOL) {
        if (b) c.U8(b->name, want ? 1 : 0);
        else if (f.sig.props == 0) { if (!want) return 0; }
        else { ErrSet(why, cap, "unexpected parameters on ", step.fn); return -1; }
    } else if (step.mode == interact::M_OPEN) {
        if (b) c.U8(b->name, 1);
        else if (f.sig.props != 0) { ErrSet(why, cap, "parameters on ", step.fn); return -1; }
    } else if (step.mode == interact::M_CLOSE) {
        if (b) c.U8(b->name, 0);
        else if (f.sig.props != 0) { ErrSet(why, cap, "parameters on ", step.fn); return -1; }
    } else {
        return 0;
    }
    bool ran = false;
    __try { ran = c.Run(actor); }
    __except (EXCEPTION_EXECUTE_HANDLER) { ran = false; }
    if (!ran) { ErrSet(why, cap, "ProcessEvent failed on ", step.fn); return -1; }
    return 1;
}

// The class' own animation for `want`, first step that resolves and runs.  Returns the
// name that ran (for the log) or nullptr.  Shared by the placed-object lane and by the
// savable lane's explicit door visual.
const char* ObjectAnimPlan(void* actor, objects::Shape sh, uint8_t want,
                           char* why, int cap)
{
    if (why && cap > 0) why[0] = 0;
    interact::Step steps[8];
    int n = interact::Plan(sh, want, steps, 8);
    for (int i = 0; i < n; ++i) {
        if (ObjectCallStep(actor, steps[i], want, why, cap) == 1) return steps[i].fn;
    }
    return nullptr;
}

// Put one object into the wanted state on THIS machine, visually and in its own state
// bool, without running the interaction verb.  `how` names what actually ran, so the live
// line says whether the class took the visual path, the restore path, or only the bool.
bool ObjectApplyToActor(void* actor, objects::Shape sh, uint8_t want,
""",
        "ObjectCallStep/ObjectAnimPlan + ObjectApplyToActor header")

E(PROXY,
"""    if (objects::ForbiddenFn(objects::VisualFnFor(sh))) {
        ErrSet(err, errCap, "ladder names a forbidden verb", "");
        return false;
    }

    char vwhy[160];
    bool visualRan = false;

    if (sh == objects::SH_PICKUP) {
        // A pickup's own disappear is hide + stop colliding; `Grant` is the payout and is
        // never called.  Either call succeeding is enough to say the visual ran.
        char w1[160];
        int r1 = ObjectCallFn(actor, "SetActorHiddenInGame", true, true, w1, sizeof(w1));
        int r2 = ObjectCallFn(actor, "SetActorEnableCollision", true, false, w1, sizeof(w1));
        visualRan = (r1 == 1) || (r2 == 1);
        ObjectHow(how, howCap, "visual=", visualRan ? "hide" : "none");
    } else if (sh == objects::SH_BARRICADE) {
        // The barricade is a savable: its visual is the game's own load path.  Calling it
        // here only matters for an instance the GUID lane could not reach.
        int r = ObjectCallFn(actor, "RestoreFromSave", false, false, vwhy, sizeof(vwhy));
        visualRan = (r == 1);
        ObjectHow(how, howCap, "visual=", visualRan ? "RestoreFromSave" : "none");
    } else {
        const char* first = objects::VisualFnFor(sh);
        const char* second = objects::SecondVisualFnFor(sh);
        const char* which = nullptr;
        int r = first ? ObjectCallFn(actor, first, false, false, vwhy, sizeof(vwhy)) : 0;
        if (r == 1) { visualRan = true; which = first; }
        else if (second) {
            r = ObjectCallFn(actor, second, false, false, vwhy, sizeof(vwhy));
            if (r == 1) { visualRan = true; which = second; }
        }
        ObjectHow(how, howCap, "visual=", which ? which : "none");
    }
""",
"""    char vwhy[160];
    const char* which = nullptr;

    if (sh == objects::SH_PICKUP) {
        // A pickup's own disappear is hide + stop colliding; `Grant` is the payout and is
        // never called.  Either call succeeding is enough to say the visual ran.
        char w1[160];
        int r1 = ObjectCallFn(actor, "SetActorHiddenInGame", true, true, w1, sizeof(w1));
        int r2 = ObjectCallFn(actor, "SetActorEnableCollision", true, false, w1, sizeof(w1));
        if (r1 == 1 || r2 == 1) which = "hide";
        ObjectHow(how, howCap, "visual=", which ? which : "none");
    } else {
        // The class' OWN animation, with the wanted direction: SetToOpenState(want) for a
        // door, Activate/Deactivate for a lever/shutter, SetToFinalState for a secret
        // wall, Break for a breakable.  `interact::Forbidden` is checked per step.
        which = ObjectAnimPlan(actor, sh, want, vwhy, sizeof(vwhy));
        ObjectHow(how, howCap, "visual=", which ? which : "none");
    }
    bool visualRan = (which != nullptr);
""",
        "ObjectApplyToActor visual ladder -> interact::Plan")

E(PROXY,
"""        // Already the value we hold: the 1 Hz heartbeat re-sends the whole set, and
        // writing an unchanged value through the game's own setter is a Map_Add and an
        // Array_Set per entry per second for nothing.
        int32_t have = 0;
        if (g_actorMirror.Known(g, &have) && have == v) continue;

        if (ownerCount < 0) {
            ownerCount = BuildActorStateOwners(owners, actorstate::kMaxEntries);
        }

        void* actor = nullptr;
        int index = -1;
        for (int k = 0; k < ownerCount; ++k) {
            const QdGuid& og = owners[k].guid;
            if (og.a == g[0] && og.b == g[1] && og.c == g[2] && og.d == g[3]) {
                actor = owners[k].actor;
                index = owners[k].index;
                break;
            }
        }

        QdGuid q = { g[0], g[1], g[2], g[3] };
        char err[192];
        bool changed = false;
        if (WrSetActorState(q, v, actor, index, err, sizeof(err), &changed)) {
            ++applied;
            // The host's value is now in our container.  The 4 Hz local detector will
            // see the write a moment later; remembering it here is what stops it being
            // offered back as an intent.
            g_pendingActors.RememberEcho(g, v);
            g_actorMirror.Note(g, v);
            // The container and the cached MySaveData follow immediately, but the
            // in-level actor only re-reads them on the load path.  Refresh it once, on
            // the frame that changed it, so the door/chest visibly opens here too.
            if (changed && actor && index >= 0) {
                char rerr[192];
                if (!WrRefreshActorState(actor, index, v, rerr, sizeof(rerr))) {
                    QDLOG(LOG_WARN, "actor-state refresh failed rev=", dec(rev),
                          " guid=", hex32(g[0]), "-", hex32(g[1]), "-", hex32(g[2]), "-",
                          hex32(g[3]), ": ", rerr);
                }
            }
        } else {
""",
"""        // Already the value we hold: the 1 Hz heartbeat re-sends the whole set, and
        // writing an unchanged value through the game's own setter is a Map_Add and an
        // Array_Set per entry per second for nothing.  With InteractSync ON a matching
        // CONTAINER is not proof the ACTOR is right: a door that streamed in after the
        // body was applied still reads its old `Opened` (the "streamed in later" gap).
        int32_t have = 0;
        bool mirrorSame = g_actorMirror.Known(g, &have) && have == v;
        if (mirrorSame && !Cfg().interact_sync) continue;

        if (ownerCount < 0) {
            ownerCount = BuildActorStateOwners(owners, actorstate::kMaxEntries);
        }

        void* actor = nullptr;
        int index = -1;
        for (int k = 0; k < ownerCount; ++k) {
            const QdGuid& og = owners[k].guid;
            if (og.a == g[0] && og.b == g[1] && og.c == g[2] && og.d == g[3]) {
                actor = owners[k].actor;
                index = owners[k].index;
                break;
            }
        }
        // No copy here yet: the container is already right and the next body will find
        // the actor once its level streams in.
        if (mirrorSame && (!actor || index < 0)) continue;

        // What THIS machine's copy reads, before the container write and restore.
        objects::Shape sh = objects::SH_NONE;
        uint8_t had = 0;
        bool haveHad = false;
        const char* used = nullptr;
        uint8_t wantByte = (uint8_t)(v ? 1 : 0);
        if (actor && index >= 0 && Cfg().interact_sync) {
            __try {
                char cn[96];
                if (FNameToString(ObjNameRaw(ObjClass(actor)), cn, sizeof(cn)) > 0)
                    sh = interact::ShapeOfClass(cn);
                haveHad = ObjectStateOf(actor, sh, &had, &used);
            } __except (EXCEPTION_EXECUTE_HANDLER) { haveHad = false; }
            if (mirrorSame && haveHad && had == wantByte) continue;
        }

        QdGuid q = { g[0], g[1], g[2], g[3] };
        char err[192];
        bool changed = false;
        if (WrSetActorState(q, v, actor, index, err, sizeof(err), &changed)) {
            ++applied;
            // The host's value is now in our container.  The 4 Hz local detector will
            // see the write a moment later; remembering it here is what stops it being
            // offered back as an intent.
            g_pendingActors.RememberEcho(g, v);
            g_actorMirror.Note(g, v);
            // Refresh when the container changed OR this copy's own state disagrees (a
            // door that streamed in after the container did).
            bool needRefresh = changed || (haveHad && had != wantByte);
            if (needRefresh && actor && index >= 0) {
                char rerr[192];
                if (!WrRefreshActorState(actor, index, v, rerr, sizeof(rerr))) {
                    QDLOG(LOG_WARN, "actor-state refresh failed rev=", dec(rev),
                          " guid=", hex32(g[0]), "-", hex32(g[1]), "-", hex32(g[2]), "-",
                          hex32(g[3]), ": ", rerr);
                }
                // A door's restore loads `Opened`; the timeline is driven by the same
                // visual its OpenDoor/CloseDoor call.  Play it so the partner sees the
                // opening, not a snap.
                if (Cfg().interact_sync && interact::NeedsExplicitAnim(sh, had, wantByte)) {
                    char aerr[160];
                    const char* an = ObjectAnimPlan(actor, sh, wantByte, aerr, sizeof(aerr));
                    QDLOG(an ? LOG_INFO : LOG_DEBUG, "interact: door anim ", an ? an : "none",
                          " key=", hex32(g[0]), " state=", dec((int)wantByte));
                }
            }
        } else {
""",
        "peer apply: streamed-in door + door animation")

E(PROXY,
"""        FindActorStateOwner(g, &actor, &index);
        char err[192];
        bool changed = false;
        if (WrSetActorState(g, b.value, actor, index, err, sizeof(err), &changed)) {
""",
"""        FindActorStateOwner(g, &actor, &index);
        // Read this machine's own copy BEFORE the container write/restore: the door's
        // restore sets `Opened`, so reading it afterwards would hide the change and the
        // explicit visual below would never run.
        objects::Shape sh = objects::SH_NONE;
        uint8_t had = 0;
        bool haveHad = false;
        const char* used = nullptr;
        uint8_t wantByte = (uint8_t)(b.value ? 1 : 0);
        if (actor && index >= 0 && Cfg().interact_sync) {
            __try {
                char cn[96];
                if (FNameToString(ObjNameRaw(ObjClass(actor)), cn, sizeof(cn)) > 0)
                    sh = interact::ShapeOfClass(cn);
                haveHad = ObjectStateOf(actor, sh, &had, &used);
            } __except (EXCEPTION_EXECUTE_HANDLER) { haveHad = false; }
        }
        char err[192];
        bool changed = false;
        if (WrSetActorState(g, b.value, actor, index, err, sizeof(err), &changed)) {
""",
        "host intent: read state before refresh")

E(PROXY,
"""            if (changed && actor && index >= 0) {
                char rerr[192];
                if (!WrRefreshActorState(actor, index, b.value, rerr, sizeof(rerr))) {
                    QDLOG(LOG_WARN, "actor-state refresh failed (intent) guid=",
                          hex32(g.a), "-", hex32(g.b), "-", hex32(g.c), "-", hex32(g.d),
                          ": ", rerr);
                }
            }
""",
"""            bool needRefresh = changed || (haveHad && had != wantByte);
            if (needRefresh && actor && index >= 0) {
                char rerr[192];
                if (!WrRefreshActorState(actor, index, b.value, rerr, sizeof(rerr))) {
                    QDLOG(LOG_WARN, "actor-state refresh failed (intent) guid=",
                          hex32(g.a), "-", hex32(g.b), "-", hex32(g.c), "-", hex32(g.d),
                          ": ", rerr);
                }
                if (Cfg().interact_sync && interact::NeedsExplicitAnim(sh, had, wantByte)) {
                    char aerr[160];
                    const char* an = ObjectAnimPlan(actor, sh, wantByte, aerr, sizeof(aerr));
                    QDLOG(an ? LOG_INFO : LOG_DEBUG, "interact: door anim ", an ? an : "none",
                          " (intent) key=", hex32(g.a), " state=", dec((int)wantByte));
                }
            }
""",
        "host intent: explicit door animation")

# ---------------------------------------------------------------------------
# qdconfig.h
# ---------------------------------------------------------------------------
E(CONF,
"    int  object_sync;      // placed world objects that are NOT SavableActors (K_OBJECTS)\n",
"    int  object_sync;      // placed world objects that are NOT SavableActors (K_OBJECTS)\n"
"    int  interact_sync;    // worlds' doors/breakables/secret mechanisms (qdinteract.h)\n",
        "config field")

E(CONF,
"    c.object_sync      = 0;\n",
"    c.object_sync      = 0;\n"
"    // The world-interact coverage layer (qdinteract.h): the extended door/breakable/\n"
"    // secret classification, the class' own bidirectional animation plan, and the\n"
"    // savable door's explicit SetToOpenState after its restore.  OFF until a live run\n"
"    // has shown the partner's door open with the SAME animation and a broken barrel\n"
"    // stay broken on both machines (mod/doc/features/world-interact.md).\n"
"    c.interact_sync    = 0;\n",
        "config default")

E(CONF,
'        else if (detail::StrEq(k, "ObjectSync"))    c.object_sync = detail::ParseInt(v, c.object_sync);\n',
'        else if (detail::StrEq(k, "ObjectSync"))    c.object_sync = detail::ParseInt(v, c.object_sync);\n'
'        else if (detail::StrEq(k, "InteractSync"))  c.interact_sync = detail::ParseInt(v, c.interact_sync);\n',
        "config parse")

# ---------------------------------------------------------------------------
# qdcoop.ini
# ---------------------------------------------------------------------------
E(INI,
"ObjectSync=0\n",
"ObjectSync=0\n"
"\n"
"; The world-interact coverage layer (qdinteract.h).  It rides on the two object lanes and\n"
"; adds: the complete door/breakable/secret-mechanism class survey (52/33/32 families, from\n"
"; the shipped pak); the class' own animation in BOTH directions (SetToOpenState(want),\n"
"; Activate/Deactivate, SetToFinalState, Break) so a door closes as well as opens and a\n"
"; breakable plays its break; and an explicit door visual after the savable restore, so the\n"
"; partner sees the opening rather than a snap.  A door that streamed in after the last body\n"
"; is re-applied.  Nothing here presses the interaction verb or pays out; breakable loot stays\n"
"; per-player.  OFF until proven live.\n"
"InteractSync=0\n",
        "ini switch")


def _eol(text):
    return "\r\n" if "\r\n" in text else "\n"


def _conv(s, eol):
    return s.replace("\r\n", "\n").replace("\n", eol)


def main():
    # Validate every anchor first, on the files as they are now.  The shared sources are
    # CRLF; the anchors above are written with LF, so both sides are normalised to the
    # file's own newline before the comparison and the replacement.
    pending = {}
    for path, old, new, label in EDITS:
        if path not in pending:
            with open(path, "r", encoding="utf-8", newline="") as f:
                pending[path] = f.read()
        text = pending[path]
        eol = _eol(text)
        c = text.count(_conv(old, eol))
        if c != 1:
            print("ANCHOR %-40s count=%d  (must be 1)" % (label, c))
            for p2, o2, n2, l2 in EDITS:
                if l2 == label:
                    print("   in", p2)
            return 2
    # Apply + write.
    for path in pending:
        text = pending[path]
        eol = _eol(text)
        old_text = text
        changed = []
        for p2, old, new, label in EDITS:
            if p2 != path:
                continue
            text = text.replace(_conv(old, eol), _conv(new, eol), 1)
            changed.append(label)
        with open(path + ".wi.bak", "w", encoding="utf-8", newline="") as f:
            f.write(old_text)
        with open(path, "w", encoding="utf-8", newline="") as f:
            f.write(text)
        print("patched %-24s (%d edit(s): %s)" % (os.path.basename(path), len(changed),
                                                  ", ".join(changed)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
