// qdrespawn_engine.h -- the engine half of the death lane: the calls, the wire and the
// ProcessEvent observations.  The RULES live in qdrespawn.h and are unit-tested; this file
// is the part that has to talk to a live UObject graph.
//
// WHY IT IS A LATE INCLUDE AND NOT A BLOCK IN proxy.cpp
// -----------------------------------------------------
// Every other lane's engine half sits in proxy.cpp because proxy.cpp has nothing to
// include: its helpers (`g_localPawn`, `PlaceBody`, `Call`, `Fn`, `OutParm`, the wire
// sender, the puppet's interpolation buffer) are in proxy.cpp's anonymous namespace, and a
// header included at the TOP cannot see them.  This header is included immediately before
// `HK_ProcessEvent` -- the hook that observes the two functions this lane needs -- where
// all of them are in scope.  proxy.cpp therefore gains an include, six call sites and a
// declaration block, and the lane's ~400 lines stay in files this feature owns.  It has no
// include guard on purpose: it is compiled inside proxy.cpp's anonymous namespace, is not
// a standalone unit, and the only thing a guard could do is hide a double include.
//
// WHAT IT OBSERVES (ProcessEvent, which every Blueprint dispatch crosses)
// ---------------------------------------------------------------------
//   FS_PlayerChar_C:Kill               the game's own death entry ran on the local player
//   FS_PlayerChar_C:ShowGameOverScreen  the death UI + the world reload behind it: REFUSED
//   FS_PlayerChar_C:CalculateMaxHP      the game computing its own maximum: scaled
//   Checkpoint_C:Interact / InteractHeld  the local player used a rest point
//   Checkpoint_C:HealToFull             the game's own completion of that rest
//
// WHAT IT DOES
// ------------
// POST / 0x54 `RESPAWN_DOWN`   the body went down, and how many deaths it now carries
// POST / 0x55 `RESPAWN_UP`     it came back, where, and with what health
// POST / 0x56 `RESPAWN_REST`   a rest cleared the stacks
// and, on the machine that OWNS the dead player: refuse the Game Over dispatch, wait
// `respawn::kPoseMs`, restore the penalised maximum, clear the death state and teleport the
// pawn to its own last rest point.  No rest is called, no actor state is written, no enemy
// record is cleared and no level load is asked for -- see the header above for why that is
// the whole point of the lane.

namespace resp {

using namespace qd;
using namespace qd::respawn;

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static DeathState g_self;              // the LOCAL player: stacks + its own rest point
static SeqGate    g_peerGate;          // the partner's bodies, in arrival order
static uint8_t    g_seq = 0;
static bool       g_peerDown = false;
static int        g_peerDeaths = 0;
static uint32_t   g_peerDownAt = 0;

// The base maximum, learned three ways, best first: what the game itself computed (the
// value it accepted, divided by the multiplier in force at the time), what our own call to
// `CalculateMaxHP` returns now, and -- when neither exists -- the highest health this
// machine has ever read for the player, which is a LOWER BOUND on the maximum (healing caps
// at the max).  A lower bound errs toward "a smaller penalty", never toward healing a
// penalised player above its cap.
static float      g_seenBase = 0.0f;
static float      g_peakHealth = 0.0f;

static bool       g_ourMaxCall = false;      // inside our own CalculateMaxHP call
static bool       g_armedDead = true;        // health was readable and above zero since the last death
static uint32_t   g_restPendingAt = 0;       // a rest to be completed on the next pump
static uint32_t   g_dismissAt = 0;           // dismiss any death screen from the next tick
static uint32_t   g_lastScaleLog = 0;
static uint32_t   g_lastCapLog = 0;
static uint32_t   g_lastBlockLog = 0;
static uint32_t   g_lastReloadLog = 0;       // the reload veto's own rate limit
static uint32_t   g_lastPoll = 0;
static uint32_t   g_lastSidecar = 0;
static bool       g_sidecarLoaded = false;
static uint32_t   g_kills = 0;               // deaths observed this session (for the log)

// WHERE THE INPUT AND MOVEMENT RESTORE ACTUALLY LIVES (measured from the cooked packages).
//
// `EnableAllPlayerInput` / `DisableAllPlayerInput` are UFUNCTIONs of `PlayerStatsAndUtils_C`,
// a **BlueprintFunctionLibrary** -- NOT of the player class.  The first version of this lane
// resolved them with `FindFunctionInClass(ObjClass(pawn), ...)`, which walks only the pawn's
// own class chain and never a library, so both missed and every respawn line read `input=0`.
// A library call is dispatched on the library's own CDO, and that is exactly what the boss-bar
// lane already does on this build (`BossBarLocal` -> `Default__PlayerStatsAndUtils_C`).
//
// `SetIgnoreMoveInput` / `SetIgnoreLookInput` are `APlayerController` entries (every shipped
// caller passes the controller), and `SetMovementMode` is `UCharacterMovementComponent`'s --
// reached through the pawn's `CharacterMovement` component, never through the pawn.  Resolving
// each where it is DECLARED is what turns "the call resolved" into "the player can walk again".
struct Fns {
    void*    pawnCls = nullptr;
    void*    fKill = nullptr;
    void*    fGameOver = nullptr;
    void*    fMaxHp = nullptr;        // the maximum-health computation, wherever it lives
    void*    maxOwner = nullptr;      // the object whose class declares it (pawn or its stats comp)
    void*    libCdo = nullptr;        // PlayerStatsAndUtils_C's CDO: the input gate's owner
    void*    fEnableInput = nullptr;
    void*    fDisableInput = nullptr;
    void*    fSetMove = nullptr;
    void*    fIgnoreMove = nullptr;
    void*    fIgnoreLook = nullptr;
    void*    cpCls = nullptr;
    void*    fCpInteract = nullptr;
    void*    fCpInteractHeld = nullptr;
    void*    fCpHeal = nullptr;
    // THE RELOAD THAT SITS BEHIND "Continue".  Dumped from the cooked packages: `DieUI_C`'s
    // `ContinueButton` handler is the widget's ONE bound event that imports `OpenLevel`
    // (`BndEvt__DieUI_ContinueButton_...` -> `OpenLevel`), and proxy.cpp's own hard-travel
    // note records that "OpenLevel is imported only by menus, death and cheats".  Refusing
    // these while this machine's player is down is what makes the 3.5 s pose safe whether or
    // not the screen was dismissed in time.
    void*    reloadFns[8] = {};
    int      reloadCount = 0;
    // THE WIDGET'S OWN CREATION.  `UWidgetBlueprintLibrary::Create` is the single call any
    // Blueprint uses to make a widget, and `DieUI_C` is made by `FS_PlayerChar` through it
    // (dumped: the player imports `Create`, `/Game/UI/Menus/DieUI`, `WidgetBlueprintLibrary`
    // and the `DieUI_C` class).  Refusing THAT dispatch when the requested class is the death
    // screen stops the screen from existing at all -- which is stronger than removing it
    // afterwards, because a widget that was never created has no Continue button and no input
    // to swallow in the first place.
    void*    fWidgetCreate = nullptr;
    uint32_t at = 0;
    int      found = -1;
};
static Fns g_fn;

// ---------------------------------------------------------------------------
// The sidecar: the stacks survive save/quit (see qdrespawn.h for the argument)
// ---------------------------------------------------------------------------
// Beside the mod's own log, which is per instance (`qdcoop-<tag>.log`), so two players on
// one machine never share a penalty.  Integers only: the position is stored in the wire's
// own 1/8 cm units, so there is no float formatting and no locale to get wrong.
static void SidecarPath(char* out, int cap)
{
    if (!out || cap <= 0) return;
    out[0] = 0;
    const char* log = LogPath();
    if (!log || !log[0]) return;
    int n = lstrlenA(log);
    int cut = n;
    for (int i = n - 1; i >= 0; --i) if (log[i] == '.') { cut = i; break; }
    int w = 0;
    for (int i = 0; i < cut && w < cap - 1; ++i) out[w++] = log[i];
    const char* suffix = "-respawn.ini";
    for (int i = 0; suffix[i] && w < cap - 1; ++i) out[w++] = suffix[i];
    out[w] = 0;
}

// Split one line into `key=value`.  No CRT float parsing, and no assumption that the file
// was written by this build: an unreadable line is skipped, never applied.
static const char* SidecarValue(const char* line, const char* key)
{
    int n = lstrlenA(key);
    if (strncmp(line, key, (size_t)n) != 0) return nullptr;
    if (line[n] != '=') return nullptr;
    return line + n + 1;
}

static void SidecarLoad()
{
    if (g_sidecarLoaded) return;
    g_sidecarLoaded = true;
    char path[MAX_PATH];
    SidecarPath(path, sizeof(path));
    if (!path[0]) return;
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    char buf[1024];
    DWORD got = 0;
    (void)ReadFile(h, buf, sizeof(buf) - 1, &got, nullptr);
    CloseHandle(h);
    buf[got] = 0;

    int stacks = 0, qx = 0, qy = 0, qz = 0, yaw = 0;
    char name[kNameCap] = "";
    char map[kMapCap] = "";
    char* p = buf;
    while (*p) {
        char* start = p;
        while (*p && *p != '\n') ++p;
        if (*p == '\n') { *p = 0; ++p; }
        if (!start[0] || start[0] == '#' || start[0] == ';') continue;
        const char* v;
        if ((v = SidecarValue(start, "stacks"))) stacks = atoi(v);
        else if ((v = SidecarValue(start, "qx"))) qx = atoi(v);
        else if ((v = SidecarValue(start, "qy"))) qy = atoi(v);
        else if ((v = SidecarValue(start, "qz"))) qz = atoi(v);
        else if ((v = SidecarValue(start, "yaw"))) yaw = atoi(v);
        else if ((v = SidecarValue(start, "map"))) {
            for (int i = 0; v[i] && i < kMapCap - 1; ++i) map[i] = v[i];
        } else if ((v = SidecarValue(start, "point"))) {
            for (int i = 0; v[i] && i < kNameCap - 1; ++i) name[i] = v[i];
        }
    }
    if (stacks < 0) stacks = 0;
    if (stacks > kMaxDeaths) stacks = kMaxDeaths;
    Point pt;
    ClearPoint(&pt);
    // THE POINT IS NOT PERSISTED (only the stacks are).  A position belongs to a world and a
    // save: the second live run proved the hazard -- a point remembered from a previous
    // session (a `Checkpoint` in a menu map) was restored and used as "this player's last
    // rest" before the player had rested at all.  A relaunch falls back to the save's
    // checkpoint, which is what a load restores anyway.
    (void)pt;
    g_self.RestoreStacks(stacks);
    if (stacks > 0) QDLOG(LOG_INFO, "respawn: restored ", dec(stacks), " death stack(s) from ",
                          path, " (max ", dec(PctFor(g_self.Deaths())), "%)");
}

static void SidecarSave(uint32_t now)
{
    if (now - g_lastSidecar < 1000) return;
    g_lastSidecar = now;
    char path[MAX_PATH];
    SidecarPath(path, sizeof(path));
    if (!path[0]) return;
    const Point& p = g_self.Rest();
    char body[512];
    char* d = body;
    const char* e = body + sizeof(body) - 1;
    int stacks = g_self.Deaths();
    d = detail::Cat(d, e, "# qdcoop death stacks (qdrespawn.h) -- the STACKS are what is read back;\n"
                          "# the point below is a record of the last rest, not a respawn target\n");
    d = detail::Cat(d, e, "stacks=");
    d = detail::CatDec(d, e, stacks);
    d = detail::Cat(d, e, "\npoint=");
    if (UsablePoint(p)) d = detail::Cat(d, e, p.name);
    d = detail::Cat(d, e, "\nmap=");
    if (UsablePoint(p)) d = detail::Cat(d, e, p.map);
    d = detail::Cat(d, e, "\nqx=");
    d = detail::CatDec(d, e, UsablePoint(p) ? wire::QuantizePos(p.px) : 0);
    d = detail::Cat(d, e, "\nqy=");
    d = detail::CatDec(d, e, UsablePoint(p) ? wire::QuantizePos(p.py) : 0);
    d = detail::Cat(d, e, "\nqz=");
    d = detail::CatDec(d, e, UsablePoint(p) ? wire::QuantizePos(p.pz) : 0);
    d = detail::Cat(d, e, "\nyaw=");
    d = detail::CatDec(d, e, UsablePoint(p) ? (int64_t)wire::QuantizeAngle(p.yaw) : 0);
    d = detail::Cat(d, e, "\n");
    HANDLE h = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD wrote = 0;
    (void)WriteFile(h, body, (DWORD)(d - body), &wrote, nullptr);
    CloseHandle(h);
}

// ---------------------------------------------------------------------------
// Resolution
// ---------------------------------------------------------------------------
// WHERE THE MAXIMUM ACTUALLY LIVES.  Measured (first live run of this lane): the player
// class answers `Kill` and `ShowGameOverScreen` but NOT `CalculateMaxHP` -- the name table of
// `FS_PlayerChar` carries it, yet the UFunction is not in the class chain, so the game's own
// computation is on the PLAYER'S STATS COMPONENT (`GetPlayerStatsComp` / `PlayerStatsComp`
// are both on the class, and the Blueprint pins `CallFunc_CalculateMaxHP_HP` name it).  So
// the function is looked for on the pawn, then on that component, and the recursion the
// game does itself is what this lane scales.
static void* FindMaxFnIn(void* obj, const char** which)
{
    if (!obj || !ObjectIsLive(obj)) return nullptr;
    void* f = FindFunctionInClass(ObjClass(obj), "CalculateMaxHP");
    if (f) { *which = "CalculateMaxHP"; return f; }
    f = FindFunctionInClass(ObjClass(obj), "GetMaxHP");
    if (f) { *which = "GetMaxHP"; return f; }
    return nullptr;
}

static void* FindMaxFn(void* pawn, void** owner, const char** which)
{
    *owner = pawn;
    if (void* f = FindMaxFnIn(pawn, which)) return f;
    // The stats component: the property first (no call), then the getter.
    void* comp = pawn ? ObjPropC(pawn, "PlayerStatsComp") : nullptr;
    if (!comp && pawn) {
        static Fn fGet = {};
        if (fGet.Resolve(pawn, "GetPlayerStatsComp")) {
            Call c(fGet);
            if (c.Run(pawn)) {
                uint8_t* p = OutParm(c, 8);
                if (p) {
                    void* v = nullptr;
                    memcpy(&v, p, 8);
                    if (v && LooksLikeUObject(v) && ObjectIsLive(v)) comp = v;
                }
            }
        }
    }
    if (comp) {
        if (void* f = FindMaxFnIn(comp, which)) { *owner = comp; return f; }
    }
    return nullptr;
}

static bool ResolveFns(uint32_t now)
{
    if (g_fn.at && now - g_fn.at < 2000) return g_fn.found > 0;
    g_fn.at = now;
    void* pc = g_localPawn ? ObjClass(g_localPawn) : nullptr;
    int found = 0;
    if (pc && ObjectIsLive(pc)) {
        g_fn.pawnCls = pc;
        g_fn.fKill        = FindFunctionInClass(pc, "Kill");
        g_fn.fGameOver    = FindFunctionInClass(pc, "ShowGameOverScreen");
        const char* which = "none";
        g_fn.fMaxHp       = FindMaxFn(g_localPawn, &g_fn.maxOwner, &which);
        if (g_fn.fKill) ++found;
        if (g_fn.fGameOver) ++found;
        if (g_fn.fMaxHp) ++found;
        if (g_fn.fMaxHp && g_fn.maxOwner && g_fn.maxOwner != g_localPawn) {
            QDLOG(LOG_INFO, "respawn: the maximum-health function ", which,
                  " is on the player's STATS COMPONENT, not the pawn -- the multiplier is ",
                  "applied to the game's own computation there");
        }
    }
    // THE INPUT GATE IS A LIBRARY, NOT THE PAWN (measured).  Resolved on
    // `Default__PlayerStatsAndUtils_C` -- the CDO every caller of the library dispatches
    // through -- and dispatched there in `DoRespawn`.  Falls back to the pawn for a build
    // that declares its own wrapper, so the restore is never silently skipped.
    if (!g_fn.libCdo || !ObjectIsLive(g_fn.libCdo))
        g_fn.libCdo = FindObjectByNameAndClass("Default__PlayerStatsAndUtils_C",
                                               "PlayerStatsAndUtils_C");
    void* gateOwner = g_fn.libCdo ? ObjClass(g_fn.libCdo) : pc;
    g_fn.fEnableInput  = gateOwner ? FindFunctionInClass(gateOwner, "EnableAllPlayerInput") : nullptr;
    g_fn.fDisableInput = gateOwner ? FindFunctionInClass(gateOwner, "DisableAllPlayerInput") : nullptr;
    if (!g_fn.fEnableInput && pc) g_fn.fEnableInput = FindFunctionInClass(pc, "EnableAllPlayerInput");
    // `SetMovementMode` belongs to the movement component.
    void* cmc = g_localPawn ? ObjPropC(g_localPawn, "CharacterMovement") : nullptr;
    g_fn.fSetMove = (cmc && ObjectIsLive(cmc)) ? FindFunctionInClass(ObjClass(cmc), "SetMovementMode")
                                               : nullptr;
    if (!g_fn.fSetMove && pc) g_fn.fSetMove = FindFunctionInClass(pc, "SetMovementMode");
    // The ignore-input pair belongs to the controller.
    void* ctl = LocalController();
    void* ctlCls = (ctl && ObjectIsLive(ctl)) ? ObjClass(ctl) : nullptr;
    g_fn.fIgnoreMove = ctlCls ? FindFunctionInClass(ctlCls, "SetIgnoreMoveInput") : nullptr;
    g_fn.fIgnoreLook = ctlCls ? FindFunctionInClass(ctlCls, "SetIgnoreLookInput") : nullptr;
    if (!g_fn.fIgnoreMove && pc) g_fn.fIgnoreMove = FindFunctionInClass(pc, "SetIgnoreMoveInput");
    if (!g_fn.fIgnoreLook && pc) g_fn.fIgnoreLook = FindFunctionInClass(pc, "SetIgnoreLookInput");
    if (g_fn.fEnableInput) ++found;
    if (g_fn.fSetMove) ++found;
    if (g_fn.fIgnoreMove) ++found;

    // THE WORLD-RELOAD ENTRY BEHIND "Continue".  Matched by NAME anywhere they are declared,
    // because the point is to refuse a reload this machine's own death screen asked for --
    // the receiver of the dispatch is not part of the test.
    if (g_fn.reloadCount == 0) {
        static void* pcCls = nullptr;
        if (!pcCls || !ObjectIsLive(pcCls)) pcCls = FindClassByName("PlayerController");
        static void* giCls = nullptr;
        if (!giCls || !ObjectIsLive(giCls)) giCls = FindClassByName("GameplayStatics");
        static const char* const kReload[] = {"OpenLevel", "RestoreFromSave", "DeathLoad",
                                              "LoadCheckpoint", "ReturnToSavePoint",
                                              "RestoreToSavePoint"};
        for (int i = 0; i < (int)(sizeof(kReload) / sizeof(kReload[0])); ++i) {
            void* f = nullptr;
            // `OpenLevel` is a `UGameplayStatics`/`KismetSystemLibrary` entry; the others are
            // the player's own.  Trying both owners costs one class walk each, once.
            if (giCls) f = FindFunctionInClass(giCls, kReload[i]);
            if (!f && pc) f = FindFunctionInClass(pc, kReload[i]);
            if (f && g_fn.reloadCount < (int)(sizeof(g_fn.reloadFns) / sizeof(g_fn.reloadFns[0])))
                g_fn.reloadFns[g_fn.reloadCount++] = f;
        }
    }

    // The widget factory, for the death screen itself (see Fns).
    if (!g_fn.fWidgetCreate || !ObjectIsLive(g_fn.fWidgetCreate)) {
        void* libCdo2 = FindCdo("WidgetBlueprintLibrary");
        if (libCdo2) g_fn.fWidgetCreate = FindFunctionInClass(ObjClass(libCdo2), "Create");
    }

    if (!g_fn.cpCls) {
        void* c = FindClassByName("Checkpoint_C");
        if (c && ObjectIsLive(c)) g_fn.cpCls = c;
    }
    if (g_fn.cpCls) {
        g_fn.fCpInteract     = FindFunctionInClass(g_fn.cpCls, "Interact");
        g_fn.fCpInteractHeld = FindFunctionInClass(g_fn.cpCls, "InteractHeld");
        g_fn.fCpHeal         = FindFunctionInClass(g_fn.cpCls, "HealToFull");
        if (g_fn.fCpInteract || g_fn.fCpInteractHeld) ++found;
    }
    if (found != g_fn.found) {
        g_fn.found = found;
        QDLOG(found >= 4 ? LOG_INFO : LOG_WARN, "respawn: player class ",
              (g_fn.pawnCls ? "FS_PlayerChar_C" : "(not loaded)"), ": Kill=",
              (g_fn.fKill ? "yes" : "NO"), " ShowGameOverScreen=", (g_fn.fGameOver ? "yes" : "NO"),
              " CalculateMaxHP=", (g_fn.fMaxHp ? "yes" : "NO"), " EnableAllPlayerInput=",
              (g_fn.fEnableInput ? "yes" : "NO"), " (on ", (g_fn.libCdo ? "PlayerStatsAndUtils_C" : "the pawn"),
              ") SetMovementMode=", (g_fn.fSetMove ? "yes" : "NO"), " SetIgnoreMoveInput=",
              (g_fn.fIgnoreMove ? "yes" : "NO"), "; Checkpoint_C=",
              (g_fn.cpCls ? "loaded" : "not loaded"), " Interact=",
              (g_fn.fCpInteract ? "yes" : "NO"), " HealToFull=", (g_fn.fCpHeal ? "yes" : "NO"),
              "; reload entries refused while down=", dec(g_fn.reloadCount));
    }
    return found >= 2;     // a death and a max are the minimum this lane is useless without
}

// ---------------------------------------------------------------------------
// Small reflective helpers
// ---------------------------------------------------------------------------
static void CallBoolFn(void* obj, void* fnPointer, const char* name, bool v)
{
    if (!obj || !fnPointer) return;
    static Fn f = {};
    if (f.fn != fnPointer) { f.tried = false; f.fn = nullptr; f.sig.valid = false; }
    if (!f.Accept(fnPointer, name)) return;
    Call c(f);
    for (int i = 0; i < f.sig.props; ++i) {
        const PropDesc& d = f.sig.p[i];
        if (d.flags & (CPF_OutParm | CPF_ReturnParm)) continue;
        if (NameEquals(d.type, "BoolProperty")) { c.U8(d.name, v ? 1 : 0); break; }
    }
    c.Run(obj);
}

// `SetMovementMode(EMovementMode NewMovementMode, uint8 NewCustomMode)`: both are bytes, and
// only the first is an enum.  Setting them by name is the difference between "called" and
// "moved" -- a Blueprint enum is a byte parameter, and zero is Walking.
//
// `this` is the MOVEMENT COMPONENT, not the pawn: the UFunction is declared by
// `UCharacterMovementComponent` and a dispatch on the pawn would resolve its own class chain
// (where the function does not exist) or, worse, run a component body against an actor.
static bool CallMovementMode(int mode)
{
    void* cmc = g_localPawn ? ObjPropC(g_localPawn, "CharacterMovement") : nullptr;
    if (!cmc || !ObjectIsLive(cmc) || !g_fn.fSetMove) return false;
    static Fn f = {};
    if (f.fn != g_fn.fSetMove) { f.tried = false; f.fn = nullptr; f.sig.valid = false; }
    if (!f.Accept(g_fn.fSetMove, "SetMovementMode")) return false;
    Call c(f);
    for (int i = 0; i < f.sig.props; ++i) {
        const PropDesc& d = f.sig.p[i];
        if (d.flags & (CPF_OutParm | CPF_ReturnParm)) continue;
        if (!NameEquals(d.type, "ByteProperty")) continue;
        if (ContainsCI(d.name, "Custom")) c.U8(d.name, 0);
        else c.U8(d.name, (uint8_t)mode);
    }
    return c.Run(cmc);
}

// The game's own statement that this player is dead: the `IsDead`/`bDead` byte if the class
// carries one, else the `IsDead` predicate.
static bool PawnDead()
{
    void* pawn = g_localPawn;
    if (!pawn) return false;
    uint8_t v = 0;
    if (BytePropC(pawn, "bDead", &v)) return v != 0;
    if (BytePropC(pawn, "IsDead", &v)) return v != 0;
    static Fn f = {};
    if (f.Resolve(pawn, "IsDead")) {
        Call c(f);
        if (c.Run(pawn)) {
            uint8_t* p = OutParm(c, 1);
            if (p) return *p != 0;
        }
    }
    return false;
}

static bool PawnHealth(float* out)
{
    if (!out) return false;
    *out = 0.0f;
    return g_localPawn && FloatPropC(g_localPawn, "Health", out);
}

// A death can move the CAMERA off the player (the death cam / the game-over view).  Putting
// the view target back is the same call the game itself makes when it resumes control, and
// it is the difference between "the player is alive" and "the player can see where it is".
static void RestoreView()
{
    void* pc = LocalController();
    if (!pc || !g_localPawn) return;
    static Fn f = {};
    if (!f.Resolve(pc, "SetViewTargetWithBlend")) return;
    Call c(f);
    if (f.sig.props > 0) c.Obj(f.sig.p[0].name, g_localPawn);
    if (c.Run(pc)) QDLOG(LOG_INFO, "respawn: the view target is the player again");
}

// ---------------------------------------------------------------------------
// The base maximum
// ---------------------------------------------------------------------------
// Called ONLY when the lane needs the base (a respawn or the completion of a rest), never
// per frame.  `g_ourMaxCall` keeps the ProcessEvent hook from scaling this very call -- the
// hook is for the GAME's computation, and scaling ours would apply the penalty twice.
// The fallback chain: what the game last computed, and the highest health this machine has
// ever read for the player (a LOWER bound on the maximum, because healing caps at it).  The
// LARGER of the two wins: a stale computation from a menu map is smaller than the health the
// real player has been seen to hold, and taking it would shrink a 400 hp player to 90.
static float BaseMaxFallback()
{
    if (g_seenBase > 0.0f && g_peakHealth > g_seenBase) return g_peakHealth;
    if (g_seenBase > 0.0f) return g_seenBase;
    if (g_peakHealth > 0.0f) return g_peakHealth;
    return 0.0f;
}

// One place decides what a computed maximum means, for both halves of the lane: this
// machine's own call (BaseMax) and the game's own computation (RespawnAfter).  A value below
// what the player has been seen to hold is not this player's maximum.
static bool RecordComputedMax(float v, const char* from)
{
    if (!(v > 1.0f) || v > 1.0e7f) return false;
    if (g_peakHealth > 0.0f && v < g_peakHealth * 0.75f) {
        QDLOG(LOG_WARN, "respawn: ", from, " says the maximum is ", flt(v, 1),
              " but this player has been seen at ", flt(g_peakHealth, 1),
              " -- that computation is not this player's; using ", flt(BaseMaxFallback(), 1));
        return false;
    }
    g_seenBase = v;
    return true;
}

static float BaseMax()
{
    float v = 0.0f;
    void* owner = g_fn.maxOwner ? g_fn.maxOwner : g_localPawn;
    if (owner && g_fn.fMaxHp && ObjectIsLive(owner)) {
        static Fn f = {};
        if (f.fn != g_fn.fMaxHp) { f.tried = false; f.fn = nullptr; f.sig.valid = false; }
        if (f.Accept(g_fn.fMaxHp, "max-health")) {
            Call c(f);
            g_ourMaxCall = true;
            bool ran = c.Run(owner);
            g_ourMaxCall = false;
            uint8_t* out = ran ? OutParm(c, 4) : nullptr;
            bool ok = false;
            if (out) { memcpy(&v, out, 4); ok = true; }
            if (ok && RecordComputedMax(v, "CalculateMaxHP")) return v;
            if (!ok) QDLOG(LOG_WARN, "respawn: the maximum-health function produced no value");
        }
    }
    return BaseMaxFallback();
}

// ---------------------------------------------------------------------------
// The rest point
// ---------------------------------------------------------------------------
// The point is recorded as THE PLACE THE PLAYER STOOD when the rest ran, not as the
// checkpoint actor's own origin.  That is deliberate: the player was standing there, so it
// is a place the capsule already occupied, and the authored origin may be inside the
// statue the checkpoint is built into.  The actor's NAME is kept as the key, so the log
// says which rest point it was and a later level check can refuse a point from another map.
static bool PointInThisWorld(const Point& p)
{
    if (!p.map[0]) return true;        // a point recorded before the map field existed
    char map[stream::kMaxNameLen];
    LocalMapName(map, sizeof(map));
    if (!map[0]) return true;
    return !strcmp(p.map, map);
}

static void SendRest(uint32_t now);

static void RecordRest(void* checkpoint, const char* why)
{
    Point p;
    ClearPoint(&p);
    ActorXform me = {};
    if (g_localPawn && ReadActorXform(g_localPawn, &me)) {
        p.have = true;
        p.px = me.px; p.py = me.py; p.pz = me.pz; p.yaw = me.yaw;
    } else if (checkpoint && ReadActorXform(checkpoint, &me)) {
        p.have = true;
        p.px = me.px; p.py = me.py; p.pz = me.pz + 50.0f; p.yaw = me.yaw;
    }
    if (!UsablePoint(p)) {
        QDLOG(LOG_WARN, "respawn: a rest ran (", why, ") but neither the player nor the ",
              "checkpoint had a readable position; the previous rest point is kept");
        return;
    }
    if (checkpoint) {
        void* world = GWorldPtr();
        char path[192];
        if (world && ActorLevelPath(checkpoint, world, path, sizeof(path)) > 0) {
            // A REST POINT IN A MENU MAP IS NOT A PLACE.  Measured (second live run): the rig
            // stood the player beside the nearest actor called "checkpoint", which was a
            // `Checkpoint` placed in `VideoConfigMap` -- a UI sublevel of the same persistent
            // world.  A real player cannot walk there, and respawning into a menu map is the
            // "teleport into empty space" failure.  So a point whose level is a UI/menu map is
            // refused, and the lane falls back to the save's checkpoint.
            if (ContainsCI(path, "/UI/") || ContainsCI(path, "/Menus/") ||
                ContainsCI(path, "VideoConfigMap")) {
                QDLOG(LOG_WARN, "respawn: a rest ran on a checkpoint in a MENU map (", path,
                      "); that is not a place a respawn may put a player, so no rest point is ",
                      "recorded and the stacks stay at ", dec(g_self.Deaths()));
                return;
            }
            for (int i = 0; path[i] && i < kNameCap - 1; ++i) p.name[i] = path[i];
        }
    }
    LocalMapName(p.map, sizeof(p.map));
    uint32_t now = Net::Now();
    int cleared = g_self.NoteRest(p, now);
    g_restPendingAt = now;
    QDLOG(LOG_INFO, "respawn: rest at ", (p.name[0] ? p.name : "(unnamed checkpoint)"),
          " (", why, "): ", dec(cleared), " stack(s) cleared; max back to ",
          dec(PctFor(g_self.Deaths())), "% at ", flt(p.px, 0), ",", flt(p.py, 0), ",",
          flt(p.pz, 0));
    SidecarSave(now);
    SendRest(now);
}

// The fallback the task names: the save's checkpoint, as the GAME knows it -- the player
// carries `LastTouchedCheckpoint` (and `LastCheckpoint`), which is what a load restores.
static bool SaveCheckpointPoint(Point* out)
{
    ClearPoint(out);
    void* pawn = g_localPawn;
    if (!pawn) return false;
    void* cp = ObjPropC(pawn, "LastTouchedCheckpoint");
    if (!cp || !ObjectIsLive(cp)) cp = ObjPropC(pawn, "LastCheckpoint");
    if (!cp || !ObjectIsLive(cp)) return false;
    ActorXform x = {};
    if (!ReadActorXform(cp, &x)) return false;
    out->have = true;
    out->px = x.px; out->py = x.py; out->pz = x.pz + 50.0f; out->yaw = x.yaw;
    void* world = GWorldPtr();
    char path[192];
    if (world && ActorLevelPath(cp, world, path, sizeof(path)) > 0) {
        for (int i = 0; path[i] && i < kNameCap - 1; ++i) out->name[i] = path[i];
    }
    LocalMapName(out->map, sizeof(out->map));
    return UsablePoint(*out);
}

// ---------------------------------------------------------------------------
// The wire
// ---------------------------------------------------------------------------
static bool SendBody(uint8_t kind, const uint8_t* body, int len)
{
    if (!g_net.Connected() || !body || len <= 0) return false;
    return g_net.SendReliable(kind, body, len);
}

static void SendDown(uint32_t now)
{
    DownBody b;
    memset(&b, 0, sizeof(b));
    b.deaths = (uint8_t)g_self.Deaths();
    b.pct = (uint8_t)g_self.Pct();
    b.seq = ++g_seq;
    if (g_localPawn) {
        ActorXform x = {};
        if (ReadActorXform(g_localPawn, &x)) { b.px = x.px; b.py = x.py; b.pz = x.pz; }
        else { b.px = b.py = b.pz = 0.0f; }
    }
    if (!Finite3(b.px, b.py, b.pz)) { b.px = b.py = b.pz = 0.0f; }
    static uint8_t buf[wire::kMaxBody];
    wire::Writer w(buf, sizeof(buf));
    if (!EncodeDown(w, b) || !SendBody(wire::K_RESPAWN_DOWN, buf, w.len)) {
        QDLOG(LOG_WARN, "respawn: the DOWN body was not sent (encode=", dec(w.ok ? 1 : 0),
              ", connected=", dec(g_net.Connected() ? 1 : 0), ")");
        return;
    }
    QDLOG(LOG_INFO, "respawn: told the partner this body went DOWN (#", dec(b.deaths),
          ", max ", dec(b.pct), "%) at ", flt(b.px, 0), ",", flt(b.py, 0), ",", flt(b.pz, 0));
}

static void SendUp(uint32_t now, const Point& p, float hp, float maxHp)
{
    UpBody b;
    memset(&b, 0, sizeof(b));
    b.deaths = (uint8_t)g_self.Deaths();
    b.pct = (uint8_t)g_self.Pct();
    b.seq = ++g_seq;
    b.px = p.px; b.py = p.py; b.pz = p.pz; b.yaw = p.yaw;
    b.hp = (uint16_t)(hp > 65535.0f ? 65535 : (hp < 0.0f ? 0 : (int)(hp + 0.5f)));
    b.hp_max = (uint16_t)(maxHp > 65535.0f ? 65535 : (maxHp < 0.0f ? 0 : (int)(maxHp + 0.5f)));
    if (b.hp_max < b.hp) b.hp_max = b.hp;
    int i = 0;
    for (; p.name[i] && i < kNameCap - 1; ++i) b.name[i] = p.name[i];
    b.name[i] = 0;
    static uint8_t buf[wire::kMaxBody];
    wire::Writer w(buf, sizeof(buf));
    if (!EncodeUp(w, b) || !SendBody(wire::K_RESPAWN_UP, buf, w.len)) {
        QDLOG(LOG_WARN, "respawn: the UP body was not sent (encode=", dec(w.ok ? 1 : 0), ")");
        return;
    }
    QDLOG(LOG_INFO, "respawn: told the partner this body is UP at ",
          (b.name[0] ? b.name : "(unnamed)"), " hp ", dec((int)b.hp), "/", dec((int)b.hp_max),
          " (#", dec(b.deaths), ", max ", dec(b.pct), "%) -- the puppet teleports, no lerp");
}

static void SendRest(uint32_t now)
{
    const Point& p = g_self.Rest();
    if (!UsablePoint(p)) return;
    RestBody b;
    memset(&b, 0, sizeof(b));
    b.deaths = 0;
    b.pct = (uint8_t)PctFor(0);
    b.seq = ++g_seq;
    b.px = p.px; b.py = p.py; b.pz = p.pz;
    int i = 0;
    for (; p.name[i] && i < kNameCap - 1; ++i) b.name[i] = p.name[i];
    b.name[i] = 0;
    static uint8_t buf[wire::kMaxBody];
    wire::Writer w(buf, sizeof(buf));
    if (!EncodeRest(w, b) || !SendBody(wire::K_RESPAWN_REST, buf, w.len)) return;
    QDLOG(LOG_INFO, "respawn: told the partner this player rested (", b.name, ")");
}

void OnRespawnDown(wire::Reader& r, uint32_t now)
{
    if (!Cfg().death_respawn) return;
    DownBody b;
    if (!DecodeDown(r, &b)) {
        QDLOG(LOG_WARN, "respawn: malformed DOWN body refused");
        return;
    }
    if (!g_peerGate.Accept(b.seq)) {
        QDLOG(LOG_INFO, "respawn: partner DOWN #", dec(b.seq), " is not newer than what is ",
              "known here; dropped");
        return;
    }
    g_peerDown = true;
    g_peerDownAt = now;
    g_peerDeaths = b.deaths;
    QDLOG(LOG_INFO, "respawn: partner went DOWN (#", dec(b.deaths), ", max ", dec(b.pct),
          "%) at ", flt(b.px, 0), ",", flt(b.py, 0), ",", flt(b.pz, 0),
          " -- this machine's world is not touched");
}

void OnRespawnUp(wire::Reader& r, uint32_t now)
{
    if (!Cfg().death_respawn) return;
    UpBody b;
    if (!DecodeUp(r, &b)) {
        QDLOG(LOG_WARN, "respawn: malformed UP body refused");
        return;
    }
    if (!g_peerGate.Accept(b.seq)) {
        QDLOG(LOG_INFO, "respawn: partner UP #", dec(b.seq), " is not newer than what is ",
              "known here; dropped");
        return;
    }
    g_peerDown = false;
    g_peerDeaths = b.deaths;
    g_peerVitals.hp = b.hp;
    g_peerVitals.hp_max = b.hp_max;
    g_peerVitals.flags = wire::VF_VALID;
    g_peerVitalsAt = now;
    bool moved = false;
    const bool havePos = !(b.px == 0.0f && b.py == 0.0f && b.pz == 0.0f);  // [coordinator] #5
    if (havePos && Cfg().puppet_sync && g_puppetActor && ObjectIsLive(g_puppetActor)) {
        // A RESPAWN IS A TELEPORT, NOT A WALK.  Pushing the sample into the interpolation
        // buffer without clearing it would make the body slide across the map from where it
        // died -- the one thing the partner must not see.  Reset, push one sample, and place
        // the body at it in the same frame.
        motion::MoveSample s;
        s.ms = now;
        s.px = b.px; s.py = b.py; s.pz = b.pz;
        s.yaw = wire::QuantizeAngle(b.yaw);    // MoveSample carries a 1/65536 turn
        s.pitch = 0;
        s.flags = 0;
        g_peerInterp.Reset();
        g_peerInterp.Push(s);
        avatar::SetAction(&g_body, 0);          // clear the down pose
        PlaceBody(&g_body, b.px, b.py, b.pz, b.yaw, now);
        g_puppet.DataArrived(now);
        moved = true;
    }
    QDLOG(LOG_INFO, "respawn: partner respawned at ", (b.name[0] ? b.name : "(unnamed)"),
          " (", flt(b.px, 0), ",", flt(b.py, 0), ",", flt(b.pz, 0), ") hp ", dec((int)b.hp),
          "/", dec((int)b.hp_max), " (#", dec(b.deaths), ", max ", dec(b.pct), "%) -- ",
          (moved ? "puppet teleported to the point, no lerp" : "no puppet here to move"),
          "; this machine's world and enemies are untouched");
}

void OnRespawnRest(wire::Reader& r, uint32_t now)
{
    if (!Cfg().death_respawn) return;
    RestBody b;
    if (!DecodeRest(r, &b)) {
        QDLOG(LOG_WARN, "respawn: malformed REST body refused");
        return;
    }
    if (!g_peerGate.Accept(b.seq)) return;
    g_peerDeaths = 0;
    g_peerDown = false;   // [coordinator] review #5: a rest means the partner is up
    QDLOG(LOG_INFO, "respawn: partner rested at ", (b.name[0] ? b.name : "(unnamed)"),
          " -- its stacks are cleared, max back to 100 %");
}

// ---------------------------------------------------------------------------
// The local player's death, pose and respawn
// ---------------------------------------------------------------------------
static void NoteLocalDeath(uint32_t now, const char* why)
{
    if (!Cfg().death_respawn) return;
    if (!g_self.NoteDeath(now)) return;
    g_armedDead = false;      // nothing counts again until a positive health is read
    g_dismissAt = now + 250;  // the death screen (if any) goes from the next pump
    ++g_kills;
    float hp = 0.0f;
    bool haveHp = PawnHealth(&hp);
    // NO ENGINE CALL HERE.  This can be reached from inside the ProcessEvent hook (the
    // game's own `Kill` dispatch), and calling `CalculateMaxHP` from there would re-enter the
    // dispatcher while the original dispatch's frame is live.  The base is whatever is
    // already known; the respawn asks for a fresh one from the pump, where that is safe.
    float base = BaseMaxFallback();
    float target = ScaledMax(base, g_self.Deaths());
    QDLOG(LOG_INFO, "respawn: DEATH #", dec(g_self.Deaths()), " (", why, ") -- max ",
          dec(g_self.Pct()), "%", (base > 0.0f ? "" : " (the base maximum is not readable yet)"),
          "; Health ", (haveHp ? "" : "unreadable"), flt(haveHp ? hp : -1.0f, 1),
          ", the respawn target is ", flt(target, 1), "; respawning in ",
          dec((int)(kPoseMs / 1000)), " s at this player's own last rest point");
    SidecarSave(now);
    SendDown(now);
}

static void FinishRest(uint32_t now)
{
    g_restPendingAt = 0;
    if (!g_localPawn || g_self.Down()) return;
    float base = BaseMax();
    if (!(base > 0.0f)) {
        QDLOG(LOG_WARN, "respawn: a rest cleared the stacks but the base maximum is not ",
              "readable, so Health is left to the game's own HealToFull");
        return;
    }
    float full = ScaledMax(base, 0);          // 100 %
    bool ok = SetFloatPropC(g_localPawn, "Health", full);
    float back = 0.0f;
    PawnHealth(&back);
    QDLOG(ok && fabsf(back - full) < 1.0f ? LOG_INFO : LOG_WARN,
          "respawn: max HP back to 100 % -- Health ", flt(back, 1), " of ", flt(full, 1),
          (ok ? " (written and read back)" : " (the write did not take)"));
}

// The death screen is a WIDGET left on the viewport.  Measured (first live run): the screen
// was up on the dying machine while its player was already alive and walking -- the world
// was not reloaded, but a dead-looking screen over a live player is not "cleared death
// state".  So after a respawn any death widget still on the viewport is removed with its own
// `RemoveFromParent`.  A name test, not a class: the widget blueprint is content.
//
// TWO FALSE POSITIVES WERE MEASURED AND ARE NOW EXCLUDED (Round 2, run 1): the name test
// matched `Default__DieUI_C`, which is the CLASS DEFAULT OBJECT -- it is never on a viewport
// and "removing" it is meaningless -- and it matched the class object itself.  A widget is
// dismissed only when it is a real instance (`Default__` prefix refused) AND the engine says
// it is on the screen (`IsInViewport`), which is the same test the `widgets` dump uses.  The
// log keeps reporting the CDO as SEEN but not acted on, so the difference is on the record.
static void DismissDeathScreens(const char* why)
{
    void* uw = FindClassByName("UserWidget");
    if (!uw || !ObjectIsLive(uw)) return;
    static Fn fIn = {};
    static void* missIn = nullptr;
    static Fn fRemove = {};
    int n = ObjectCount(), removed = 0, cdo = 0;
    for (int i = 0; i < n && removed < 8; ++i) {
        void* o = ObjectAt(i);
        if (!o || !LooksLikeUObject(o) || !ObjectIsLive(o)) continue;
        char cls[96] = "";
        if (FNameToString(ObjNameRaw(ObjClass(o)), cls, sizeof(cls)) <= 0) continue;
        if (!ContainsCI(cls, "GameOver") && !ContainsCI(cls, "Game_Over") &&
            !ContainsCI(cls, "DieUI") && !ContainsCI(cls, "DeathScreen")) continue;
        bool sub = false;
        __try { sub = IsSubclassOf(ObjClass(o), uw); }
        __except (EXCEPTION_EXECUTE_HANDLER) { sub = false; }
        if (!sub) continue;
        char nm[96] = "?";
        FNameToString(ObjNameRaw(o), nm, sizeof(nm));
        if (!strncmp(nm, "Default__", 9)) { ++cdo; continue; }   // the CDO is not a screen
        bool inVp = false;
        bool known = false;
        __try {
            if (ResolvePerClass(fIn, &missIn, o, "IsInViewport")) {
                Call c(fIn);
                if (c.Run(o) && c.Ret()) { inVp = *(uint8_t*)c.Ret() != 0; known = true; }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) { known = false; }
        // An unreadable `IsInViewport` is NOT treated as "up": dismissing an object we cannot
        // see on screen is the guess that produced the CDO false positive.
        if (known && !inVp) continue;
        if (fRemove.Resolve(o, "RemoveFromParent")) {
            Call c(fRemove);
            bool ran = c.Run(o);
            QDLOG(LOG_INFO, "respawn: a death screen was still up (", nm, " [", cls, "]) ",
                  (ran ? "-- removed from the viewport" : "-- RemoveFromParent DID NOT RUN"),
                  " (", why, ")");
            if (ran) ++removed;
        } else {
            QDLOG(LOG_WARN, "respawn: a death screen is up (", nm, " [", cls,
                  "]) and has no RemoveFromParent");
        }
    }
    if (cdo) QDLOG(LOG_DEBUG, "respawn: ", dec(cdo), " death-screen CLASS DEFAULT OBJECT(s) seen ",
                   "and not touched (a CDO is never on a viewport) (", why, ")");
    if (!removed) QDLOG(LOG_INFO, "respawn: no death screen left on the viewport (", why, ")");
}

static void DoRespawn(uint32_t now)
{
    void* pawn = g_localPawn;
    if (!pawn || !IsGameplayPawn(pawn)) {
        QDLOG(LOG_WARN, "respawn: a respawn was due and there is no player pawn on this ",
              "machine -- the game's own death path owns it (its pawn was destroyed, which ",
              "this lane cannot rebuild); the stacks stay at ", dec(g_self.Deaths()));
        g_self.NoteRespawn(now);
        // [coordinator] review #5: still tell the partner, or its puppet keeps the death
        // pose forever.  No pawn = no position: the receiver keeps the puppet where it is.
        {
            Point none;
            memset(&none, 0, sizeof(none));
            SendUp(now, none, 0.0f, 0.0f);
        }
        return;
    }
    Point p = g_self.Rest();
    const char* from = "this player's last rest";
    float restDist = -1.0f;
    bool pointOk = UsablePoint(p) && PointInThisWorld(p);
    if (!pointOk) {
        Point sp;
        const char* why = !UsablePoint(p) ? "no rest has been used on this machine"
                                          : "the rest point is in another map";
        if (SaveCheckpointPoint(&sp) && PointInThisWorld(sp)) {
            p = sp;
            from = "the save's checkpoint";
            QDLOG(LOG_INFO, "respawn: ", why, "; using the save's checkpoint instead");
        } else {
            // A point in another map is not a place this machine can put a player without the
            // level load this lane exists to avoid; staying where the body fell is a place the
            // capsule already occupied, which is the honest answer.
            ClearPoint(&p);
            from = "where the body fell (no usable rest point in this map)";
            QDLOG(LOG_WARN, "respawn: ", why, ", and the save's checkpoint is not in this map ",
                  "either; the player stays where the body fell");
        }
    }
    float base = BaseMax();
    float maxHp = ScaledMax(base, g_self.Deaths());
    float hp = maxHp;
    if (!(hp > 0.0f)) {
        hp = g_peakHealth > 0.0f ? g_peakHealth : 1.0f;
        QDLOG(LOG_WARN, "respawn: the base maximum is unreadable (CalculateMaxHP refused and ",
              "no value was observed); Health is set to ", flt(hp, 1),
              " and the maximum itself is left to the game");
    }

    // 1. WHERE.  A refused position keeps the player where the body fell, which is a place
    //    the capsule already occupied -- never a move to a coordinate we could not read.
    ActorXform here = {};
    bool haveHere = ReadActorXform(pawn, &here);
    bool moved = false;
    if (UsablePoint(p)) {
        moved = SetActorXform(pawn, p.px, p.py, p.pz, p.yaw);
        if (moved && haveHere) {
            float dx = here.px - p.px, dy = here.py - p.py, dz = here.pz - p.pz;
            restDist = sqrtf(dx * dx + dy * dy + dz * dz);
        }
    }

    // 2. THE DEATH STATE.  `bDead`/`IsDead` off, no knockdown, walking, input back.  Each is
    //    reported, because "the player is alive" is a claim about the game's own flags.
    uint8_t was = 0;
    bool hadDead = BytePropC(pawn, "bDead", &was);
    if (hadDead) WriteBytePropC(pawn, "bDead", 0);
    bool hadKnock = BytePropC(pawn, "KnockedDown", &was);
    if (hadKnock) WriteBytePropC(pawn, "KnockedDown", 0);
    // EACH LEVER IS DISPATCHED WHERE IT IS DECLARED (see Fns for the measurement):
    // walking on the movement component, the ignore pair on the controller, the input gate on
    // the PlayerStatsAndUtils library.  The old version ran all four on the pawn, resolved
    // none of them (`input=0`), and reported a restore it had not performed.
    bool walkOn = CallMovementMode(0);               // EMovementMode::MOVE_Walking
    void* ctl = LocalController();
    void* ctlObj = (ctl && ObjectIsLive(ctl)) ? ctl : pawn;
    CallBoolFn(ctlObj, g_fn.fIgnoreMove, "SetIgnoreMoveInput", false);
    CallBoolFn(ctlObj, g_fn.fIgnoreLook, "SetIgnoreLookInput", false);
    RestoreView();
    // THE LIBRARY CALL.  `this` is the library's CDO: that is the object a Blueprint's
    // `EnableAllPlayerInput` node dispatches on, and the node's single argument is the player.
    bool inputOn = false;
    if (g_fn.fEnableInput) {
        static Fn f = {};
        if (f.fn != g_fn.fEnableInput) { f.tried = false; f.fn = nullptr; f.sig.valid = false; }
        if (f.Accept(g_fn.fEnableInput, "EnableAllPlayerInput")) {
            Call c(f);
            for (int i = 0; i < f.sig.props; ++i) {
                const PropDesc& d = f.sig.p[i];
                if (d.flags & (CPF_OutParm | CPF_ReturnParm)) continue;
                if (NameEquals(d.type, "ObjectProperty")) c.Obj(d.name, pawn);
            }
            void* receiver = (g_fn.libCdo && ObjectIsLive(g_fn.libCdo)) ? g_fn.libCdo : pawn;
            inputOn = c.Run(receiver);
        }
    }

    // 3. HEALTH, as the penalised maximum -- written AFTER the flags so the game's own
    //    death branch (which reads `bDead`) cannot re-zero it.
    bool hpOk = SetFloatPropC(pawn, "Health", hp);
    float back = 0.0f;
    bool haveBack = PawnHealth(&back);

    g_self.NoteRespawn(now);
    SidecarSave(now);
    // [coordinator] review #3: a cleared point would put the partner's puppet at the world
    // origin -- send where this body actually is instead.
    Point up = p;
    if (!UsablePoint(up)) {
        ActorXform hx;
        if (ReadActorXform(pawn, &hx)) {
            up.px = hx.px; up.py = hx.py; up.pz = hx.pz; up.yaw = hx.yaw;
        }
    }
    SendUp(now, up, haveBack ? back : hp, maxHp > 0.0f ? maxHp : hp);
    QDLOG(LOG_INFO, "respawn: respawned at ",
          (p.name[0] ? p.name : (UsablePoint(p) ? "(unnamed point)" : "(where the body fell)")),
          " -- source=", from, "; Health -> ", flt(back, 1), " (base ", flt(base, 1), " x ",
          flt(g_self.Multiplier(), 2), ", ", dec(g_self.Pct()), "%); moved=", dec(moved ? 1 : 0),
          " input=", dec(inputOn ? 1 : 0), " walking=", dec(walkOn ? 1 : 0),
          " ignoreInputCleared=", dec((g_fn.fIgnoreMove || g_fn.fIgnoreLook) ? 1 : 0),
          " hpWrite=", dec(hpOk ? 1 : 0),
          (hadDead ? " bDead->0" : ""), (hadKnock ? " KnockedDown->0" : ""));
    if (restDist >= 0.0f && restDist > 1.0f) {
        QDLOG(LOG_INFO, "respawn: the body fell ", flt(restDist, 0), " cm from the rest point");
    }
    DismissDeathScreens("after the respawn");
}

// ---------------------------------------------------------------------------
// The tick
// ---------------------------------------------------------------------------
void RespawnPump(uint32_t now)
{
    if (!Cfg().death_respawn) return;
    if (!IsGameplayPawn(g_localPawn)) return;
    SidecarLoad();
    if (now - g_lastPoll < 200) return;
    g_lastPoll = now;
    ResolveFns(now);

    // The base maximum is learned from the highest health this machine has seen: healing
    // caps at the maximum, so this is a lower bound and is only a last resort.
    float hp = 0.0f;
    bool haveHp = PawnHealth(&hp);
    if (haveHp && hp > g_peakHealth) g_peakHealth = hp;

    // THE PENALTY IS A CAP, NOT ONLY A SPAWN VALUE.  The game's own maximum is not on a class
    // this lane can write on this build (measured: `CalculateMaxHP` is not in the player's
    // function chain, and its regeneration fills Health back up to the UNPENALISED maximum --
    // the first live run showed 360 -> 400 within 40 s).  So a player carrying stacks is held
    // at or below `ScaledMax(base, stacks)`: whatever the game computes and heals to, the
    // penalised maximum is what the player can reach.  A rest makes the multiplier 1.0 and
    // this stops entirely.
    if (!g_self.Down() && g_self.Deaths() > 0 && haveHp && g_armedDead) {
        float base = BaseMaxFallback();
        float cap = ScaledMax(base, g_self.Deaths());
        if (cap > 0.0f && hp > cap + 0.5f) {
            bool wrote = SetFloatPropC(g_localPawn, "Health", cap);
            if (g_lastCapLog == 0 || now - g_lastCapLog > 2000) {
                g_lastCapLog = now;
                QDLOG(wrote ? LOG_INFO : LOG_WARN, "respawn: Health ", flt(hp, 1), " is above the ",
                      "penalised maximum ", flt(cap, 1), " (base ", flt(base, 1), " x ",
                      flt(g_self.Multiplier(), 2), ") -- held at the cap",
                      (wrote ? "" : "; the write did not take"));
            }
        }
    }

    if (g_restPendingAt) FinishRest(now);

    // THE DEATH SCREEN goes as soon as it is seen, not only when the player is back: the
    // screen is the door to the game's world reload, and every second it stays is a second in
    // which a press of "Continue" would reload BOTH players' world.  The dismissal is done
    // here rather than inside the ProcessEvent hook because it walks the object list.
    if (g_dismissAt && (int32_t)(now - g_dismissAt) >= 0) {
        g_dismissAt = 0;
        DismissDeathScreens("the player is down");
    }

    // ONE DEATH, ONE STACK.  `Health` is the signal: the game's `Kill` drives it to zero,
    // and a death counted here is not counted again until this machine has READ a positive
    // health in between.  The `bDead` flag alone is NOT a death: the game leaves it set for
    // a while after the pose, and treating it as a death is how one death becomes two.
    if (haveHp && hp > 0.0f) g_armedDead = true;
    if (!g_self.Down() && haveHp && hp <= 0.0f && g_armedDead) {
        NoteLocalDeath(now, PawnDead() ? "Health reached 0 and the game's dead flag is set"
                                       : "Health reached 0");
    }
    if (g_self.Down() && g_self.Due(now)) DoRespawn(now);
}

// ---------------------------------------------------------------------------
// The ProcessEvent observations
// ---------------------------------------------------------------------------
// `RespawnVeto` is asked BEFORE the dispatch is forwarded.  It refuses TWO things, and the
// second one is what makes the pose safe:
//
//   1. the Game Over screen itself (`ShowGameOverScreen`), and
//   2. THE RELOAD BEHIND ITS "Continue" -- `OpenLevel` and the save-checkpoint restores.
//
// WHY (2) IS THE LOAD-BEARING ONE.  Measured live: the death screen is NOT created through
// `ShowGameOverScreen` on this build, so (1) does not fire and the screen exists anyway; the
// old mitigation was to remove the widget within ~250 ms.  That leaves a window: the world
// reload starts when the screen's Continue button is pressed, and a player can press it
// during the 3.5 s pose.  Removing a widget is a race; refusing the FUNCTION the button calls
// is not.  So while this machine's player is DOWN, a dispatch of a reload entry is refused
// outright, whether or not any screen is up, and whether the press came from the button, a
// key bound to the same handler, or the game's own follow-up.  When the player is not down
// and carries no pending respawn, nothing is refused -- a legitimate `OpenLevel` (a cheat, a
// menu's own load) is the game's business.
static bool ReloadFn(void* func)
{
    if (!func) return false;
    for (int i = 0; i < g_fn.reloadCount; ++i)
        if (g_fn.reloadFns[i] && g_fn.reloadFns[i] == func) return true;
    return false;
}

bool RespawnVeto(void* self, void* func, void* parms)
{
    (void)parms;
    if (!Cfg().death_respawn || !func || !self) return false;

    // ---- 0. THE DEATH SCREEN IS NEVER CREATED --------------------------------------
    // `UWidgetBlueprintLibrary::Create(WorldContextObject, WidgetType, OwningPlayer)` is the
    // one factory every Blueprint uses, and `DieUI_C` is made through it by `FS_PlayerChar`
    // (dumped from the cooked packages).  Refusing the dispatch when `WidgetType` is the death
    // screen means the screen does not exist: no Continue button, no input to swallow, no
    // window to race.
    //
    // MEASURED (Round 2 run 1): this did NOT fire.  The line below is a DIAGNOSTIC rather than
    // an assumption -- every `Create` dispatch seen while down is logged with the class it was
    // asked for, so the next run says whether the screen on this build comes through this
    // factory at all (the dismissal log shows the class name is `DieUI_C`, and the widget is
    // visible, so SOMETHING made it).  The refusal below is kept because it costs one pointer
    // compare and is the correct defence on a build that does use the factory.
    if (g_fn.fWidgetCreate && func == g_fn.fWidgetCreate && (g_self.Down() || g_dismissAt)) {
        void* want = nullptr;
        static Fn fc = {};
        if (fc.fn != func) { fc.tried = false; fc.fn = nullptr; fc.sig.valid = false; }
        if (fc.Accept(func, "Create")) {
            int off = fc.Off("WidgetType");
            if (off >= 0) {
                __try { memcpy(&want, (uint8_t*)parms + off, 8); }
                __except (EXCEPTION_EXECUTE_HANDLER) { want = nullptr; }
            }
        }
        char cn[96] = "(none)";
        if (want && LooksLikeUObject(want)) FNameToString(ObjNameRaw(want), cn, sizeof(cn));
        uint32_t now = Net::Now();
        if (g_lastReloadLog == 0 || now - g_lastReloadLog > 1000) {
            g_lastReloadLog = now;
            uint32_t at = g_self.DownAt();
            QDLOG(LOG_INFO, "respawn: a widget was created while the player is down: ", cn,
                  " (", (at ? dec((int)(now - at)) : dec(-1)), " ms into the death)");
        }
        if (want && LooksLikeUObject(want) &&
            (ContainsCI(cn, "DieUI") || ContainsCI(cn, "GameOver") || ContainsCI(cn, "DeathScreen"))) {
            QDLOG(LOG_INFO, "respawn: BLOCK the death screen's CREATION -- ", cn,
                  " refused at WidgetBlueprintLibrary::Create, so there is no Continue button to ",
                  "press during the pose");
            return true;
        }
    }

    // ---- 1. THE RELOAD BEHIND "Continue" -------------------------------------------
    // `g_self.Down()` is "this machine has a death in progress": from the moment the death is
    // noticed until the respawn has run.  A reload dispatched in that window is refused and
    // named in the log, so a refused press is never silent.
    if (g_self.Down() && ReloadFn(func)) {
        uint32_t now = Net::Now();
        if (g_lastReloadLog == 0 || now - g_lastReloadLog > 1000) {
            g_lastReloadLog = now;
            char fn[96] = "?";
            FNameToString(ObjNameRaw(func), fn, sizeof(fn));
            char cn[64] = "?";
            FNameToString(ObjNameRaw(ObjClass(self)), cn, sizeof(cn));
            uint32_t at = g_self.DownAt();
            QDLOG(LOG_INFO, "respawn: BLOCK the world reload -- ", fn, " refused (receiver ", cn,
                  ") while this machine's player is down (",
                  (at ? dec((int)(now - at)) : dec(-1)),
                  " ms into the death): respawning is a position, and a load here would restart ",
                  "BOTH players' world");
        }
        return true;
    }

    // ---- 2. the Game Over screen dispatch -------------------------------------------
    // The FUNCTION is what is matched, not `self`: measured (first live run) the Game Over
    // screen still came up, so whatever opened it did not dispatch it with the pawn as the
    // receiver.  `ShowGameOverScreen` is this player class's own entry, so refusing every
    // dispatch of it can only ever refuse this player's death screen.
    if (!g_fn.fGameOver || func != g_fn.fGameOver) return false;
    uint32_t now = Net::Now();
    if (g_lastBlockLog == 0 || now - g_lastBlockLog > 3000) {
        g_lastBlockLog = now;
        char cn[64] = "?";
        FNameToString(ObjNameRaw(ObjClass(self)), cn, sizeof(cn));
        QDLOG(LOG_INFO, "respawn: BLOCK the game's death flow -- FS_PlayerChar_C.ShowGameOverScreen ",
              "refused (receiver ", cn, "): in co-op a death is per player, and this screen ",
              "pauses this world and leads to a reload of BOTH players' world");
    }
    return true;
}

// `RespawnAfter` runs after the dispatch was forwarded: the value the GAME computed is the
// one that gets scaled, which is what makes the penalty survive a stat recomputation.
void RespawnAfter(void* self, void* func, void* parms)
{
    if (!Cfg().death_respawn || !func || !parms) return;
    if (!g_localPawn) return;
    // The maximum may be computed on the pawn OR on the player's stats component (measured:
    // on this build it is the component), so the owner is what is compared, not the pawn.
    if (self != g_localPawn && (!g_fn.maxOwner || self != g_fn.maxOwner)) return;
    if (!g_fn.fMaxHp || func != g_fn.fMaxHp) return;
    if (g_ourMaxCall) return;
    if (g_self.Deaths() <= 0) return;
    static Fn f = {};
    if (f.fn != func) { f.tried = false; f.fn = nullptr; f.sig.valid = false; }
    if (!f.Accept(func, "CalculateMaxHP")) return;
    int off = f.Off("HP");
    if (off < 0) off = f.RetOff();
    if (off < 0) return;
    float before = 0.0f;
    __try { memcpy(&before, (uint8_t*)parms + off, 4); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return; }
    if (!(before > 0.0f) || before > 1.0e7f) return;
    float after = before * g_self.Multiplier();
    __try { memcpy((uint8_t*)parms + off, &after, 4); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return; }
    // The game's own number, un-scaled, is the best base maximum this machine will ever see.
    RecordComputedMax(before, "the game's own computation");
    uint32_t now = Net::Now();
    if (g_lastScaleLog == 0 || now - g_lastScaleLog > 2000) {
        g_lastScaleLog = now;
        QDLOG(LOG_INFO, "respawn: the game computed max HP ", flt(before, 1), " -> ",
              flt(after, 1), " (", dec(g_self.Pct()), "%, ", dec(g_self.Deaths()),
              " death(s)) -- the multiplier is applied to the game's own number");
    }
}

// The rest check: the local player's press lands on a `Checkpoint_C`, and the completion is
// `HealToFull` on that same actor.  Both are observed; the PRESS records the point (the
// requirement is "the last rest point that player interacted with"), and the HEAL says the
// rest actually completed.
void RespawnObserve(void* self, void* func, void* parms)
{
    (void)parms;
    if (!Cfg().death_respawn || !func || !self) return;
    // THE COMMON CASE IS FOUR POINTER COMPARES.  This runs on every Blueprint dispatch in the
    // process, so the function test comes before anything that reads an object.
    bool isKill  = g_fn.fKill && func == g_fn.fKill;
    bool isCp    = g_fn.fCpInteract && func == g_fn.fCpInteract;
    bool isHeld  = g_fn.fCpInteractHeld && func == g_fn.fCpInteractHeld;
    bool isHeal  = g_fn.fCpHeal && func == g_fn.fCpHeal;
    if (!isKill && !isCp && !isHeld && !isHeal) return;
    if (!g_localPawn || !IsGameplayPawn(g_localPawn)) return;
    // THE GAME'S OWN DEATH ENTRY.  `Kill` is the statement "this player died"; it is
    // observed here, before it runs, so the stack is counted once even if the game never
    // drives `Health` to zero on this build.
    if (isKill) {
        if (self == g_localPawn) {
            NoteLocalDeath(Net::Now(), "the game ran its own death entry (FS_PlayerChar_C.Kill)");
        }
        return;
    }
    if (!g_fn.cpCls || !ObjectIsLive(self)) return;
    bool selfIsCheckpoint = false;
    __try { selfIsCheckpoint = IsSubclassOf(ObjClass(self), g_fn.cpCls); }
    __except (EXCEPTION_EXECUTE_HANDLER) { selfIsCheckpoint = false; }
    if (!selfIsCheckpoint) return;
    if (isCp) {
        RecordRest(self, "Interact");
    } else if (isHeld && !isCp) {
        // `InteractHeld` follows `Interact` in the same press; it is the fallback for a
        // build where the one-shot verb is the one that exists.
        static uint32_t lastHeld = 0;
        uint32_t now = Net::Now();
        if (now - lastHeld > 1500) {
            lastHeld = now;
            RecordRest(self, "InteractHeld");
        }
    } else if (isHeal) {
        static uint32_t lastHeal = 0;
        uint32_t now = Net::Now();
        if (now - lastHeal > 2000) {
            lastHeal = now;
            QDLOG(LOG_INFO, "respawn: the rest completed on this machine (Checkpoint_C.HealToFull)");
        }
    }
}

// A world change drops the pointers, never the player's stacks.  The rest point is a
// position and a name; it is kept, and `PointInThisWorld` refuses it if the player has since
// travelled -- respawning into another map would need the load this lane exists to avoid.
void RespawnWorldReset(uint32_t now, const char* why)
{
    (void)now;
    if (!Cfg().death_respawn) return;
    g_peerDown = false;
    g_peerGate.Reset();
    g_fn.cpCls = nullptr;
    g_fn.fCpInteract = nullptr;
    g_fn.fCpInteractHeld = nullptr;
    g_fn.fCpHeal = nullptr;
    g_restPendingAt = 0;
    QDLOG(LOG_INFO, "respawn: world changed (", why, "); stacks keep (",
          dec(g_self.Deaths()), "), the cached rest-point ACTOR is dropped");
}

} // namespace resp

// The one call the puppet pump makes: while the partner is down, its body plays the death
// pose instead of whatever action arrives on the wire.
bool RespawnPeerDown() { return resp::g_peerDown; }
