// qdjoin_engine.h -- the engine half of [join-sync].
//
// Included at the END of proxy.cpp, after the desync-auditor block, because it calls the
// audit helpers, the reflection layer, the save layer, the enemy table, the inventory
// writer and the travel entry points.  The RULES it applies are in qdjoin.h, which is pure
// and unit-tested (`test/test_join.cpp`); this file is only the part that touches a game.
//
// WHAT IT DOES, IN ONE PARAGRAPH
// ------------------------------
// On every session establishment the PEER asks the HOST for its world/progression state.
// The host enumerates its flag bank, its `SavableActorStates`, its placed-object set, its
// dead-enemy set, its key items, the three save unlock arrays, the puzzle poses and its
// current world/area, and streams them as K_JOIN_STATE chunks.  The peer accumulates them,
// diffs them against its OWN state (qdjoin::BuildPlan), and applies the differences in one
// controlled phase -- including the reverts no existing lane can perform.  It backs its
// save files up first, brackets the other lanes' echoes while it writes, persists the save
// so a later solo load agrees, then follows the host's map through the existing travel lane
// and forces the desync auditor.
//
// The host's state is NEVER written.  Nothing here reads or writes a host object: the host
// only serialises what it already holds.
#pragma once

#include <stdio.h>          // snprintf for the backup directory name

namespace joinsync {

// ---------------------------------------------------------------------------
// The state machines
// ---------------------------------------------------------------------------
// The peer's episode: ask, accumulate, apply, report.  An episode is per SESSION
// ESTABLISHMENT (a first connect or a reconnect after a drop), never per frame.
enum Phase : uint8_t {
    PH_IDLE      = 0,
    PH_WAIT      = 1,   // the request is out; the host's chunks are arriving
    PH_APPLY     = 2,   // complete state received; the peer is writing
    PH_DONE      = 3,
};

static join::State  g_hostState;      // the host's view (host builds it, peer accumulates it)
static join::State  g_localState;     // this peer's own view
static join::Plan   g_plan;           // the differences to apply (peer only)

static uint8_t      g_phase = PH_IDLE;
static uint32_t     g_seq = 0;        // the episode's sequence number
static uint32_t     g_deadline = 0;   // the WAIT deadline
static uint32_t     g_nextReq = 0;
static int          g_requests = 0;
static uint8_t      g_recvMask = 0;   // categories whose LAST chunk has arrived
static uint8_t      g_recvHas = 0;    // categories with at least one chunk
static bool         g_silent = false; // the link went quiet while connected
static bool         g_wasConnected = false;
static uint32_t     g_lastSession = 0;
static uint32_t     g_busyUntil = 0;  // the controlled phase's echo window
static char         g_backupPath[192] = {0};
static uint32_t     g_auditAt = 0;    // force the auditor once the phase is over
static char         g_summaryTravel[64] = {0};

// The host's serving cursor: one chunk per tick, one category at a time, a `first` cursor
// across ticks (never a fragment).
static bool         g_serveActive = false;
static uint32_t     g_serveSeq = 0;
static uint8_t      g_serveCat = 0;
static uint8_t      g_serveSub = 0;
static int          g_serveFirst = 0;
static uint32_t     g_serveRepeatAt = 0;
static int          g_serveRepeats = 0;
static uint32_t     g_serveAckedAt = 0;

// The `g_joinApplying` flag lives near the top of proxy.cpp because the
// `HK_StoreEnemySaveState` hook (defined before this include) tests it.  While it is set,
// that hook lets the game's write happen but must not ledger or broadcast it: the host
// already holds the death, and echoing a restore write would be the ping-pong the lane's
// own rules prevent.

static const uint32_t kWaitMs        = 12000;   // how long a peer waits for a full state
static const uint32_t kRetryReqMs    = 1500;    // re-ask while waiting
static const uint32_t kMaxRequests   = 6;
static uint32_t       g_lastChunkAt  = 0;   // [coordinator] when a state chunk last arrived
static const uint32_t kBusyTailMs    = 2500;    // the echo window after the apply
static const int      kServePerTick  = 8;       // chunks a host sends in one frame
static const int      kServeRepeats  = 75;   // [coordinator] was 20 (~16 s): a peer still loading stalls longer than that      // how many full passes before giving up
static const uint32_t kServeRepeatMs = 800;

// ---------------------------------------------------------------------------
// The global gate the other lanes consult (declared in proxy.cpp's forward block)
// ---------------------------------------------------------------------------
bool JoinBusyImpl()
{
    if (g_phase == PH_APPLY) return true;
    return (int32_t)(g_busyUntil - Net::Now()) > 0;
}

// ---------------------------------------------------------------------------
// Small text helpers
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Enumerating this machine's OWN state
// ---------------------------------------------------------------------------
struct JoinNsdCtx { join::State* s; };
static void JoinNsdVisit(const char* key, int32_t value, void* user)
{
    JoinNsdCtx* c = (JoinNsdCtx*)user;
    if (!c || !c->s || !key || !key[0]) return;
    if (join::FlagStaysLocal(key)) return;
    c->s->AddFlag(key, value);
}

// `EnemySaveStates` is `TMap<FGuid,{u8 bValid, float RespawnTime, float RemainingHP}>` at the
// reflected property offset (measured `save+0x270`).  NO STRIDE IS DOCUMENTED, so it is
// probed: a TSet element is the 28-byte TPair plus two hash words, and only a stride whose
// `bValid` byte is 0/1 and whose two floats are finite on every probed key can be right.
// The probe accepts the first stride that passes, and reports it.  When it cannot be
// established the caller falls back to the mod's own death ledger (which is what the
// auditor digests), so the category degrades rather than inventing keys.
static const int kEnemyStatesOffsetMeasured = 0x270;

static bool JoinEnemyMapStorage(const uint8_t** arr, int32_t* num, int* offsetOut)
{
    *arr = nullptr; *num = 0;
    join::State dummy;
    (void)dummy;
    if (!Save().save) return false;
    int off = FindPropertyOffsetIn(ObjClass(Save().save), "EnemySaveStates", nullptr);
    if (off < 0) off = kEnemyStatesOffsetMeasured;
    if (offsetOut) *offsetOut = off;
    bool ok = false;
    __try {
        const uint8_t* base = (const uint8_t*)Save().save + off;
        const uint8_t* data = *(const uint8_t* const*)base;
        int32_t n = *(const int32_t*)(base + 8);
        if (n >= 0 && n <= 200000 && (n == 0 || data)) {
            *arr = data;
            *num = n;
            ok = true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    return ok;
}

// Is a float plausible?  A wrong stride lands on pointer halves and padding, which are
// either enormous or a denormal; a real RespawnTime is within +/-1e7 and a real HP within
// +/-1e7 (the game stores -1 for "no respawn" and HP fractions elsewhere).
static bool JoinFiniteEnough(float v)
{
    if (!(v == v)) return false;                 // NaN
    if (v > 1.0e7f || v < -1.0e7f) return false;
    return true;
}

struct JoinEnemyRec { uint32_t guid[4]; uint8_t bValid; float respawn, hp; };

// Reads all records at `stride`, filling `out` (up to cap) and returning the number written.
static int JoinEnemyRead(const uint8_t* data, int32_t num, int stride, JoinEnemyRec* out, int cap)
{
    int n = 0;
    __try {
        for (int32_t i = 0; i < num && n < cap; ++i) {
            const uint8_t* e = data + (size_t)i * stride;
            uint32_t g[4];
            for (int k = 0; k < 4; ++k) g[k] = *(const uint32_t*)(e + k * 4);
            if (join::State::GuidZero(g)) continue;
            uint8_t bv = *(const uint8_t*)(e + 16);
            if (bv > 1) return -1;
            float rs = 0.0f, hp = 0.0f;
            memcpy(&rs, e + 20, 4);
            memcpy(&hp, e + 24, 4);
            if (!JoinFiniteEnough(rs) || !JoinFiniteEnough(hp)) return -1;
            for (int k = 0; k < 4; ++k) out[n].guid[k] = g[k];
            out[n].bValid = bv;
            out[n].respawn = rs;
            out[n].hp = hp;
            ++n;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
    return n;
}

// Fills the ENEMY set with the DEAD keys this machine's save holds.  Returns the number of
// records read, or -1 when the map could not be decoded.
static int JoinCollectDeadEnemies(join::State* s)
{
    const uint8_t* data = nullptr;
    int32_t num = 0;
    int off = -1;
    if (!s || !JoinEnemyMapStorage(&data, &num, &off)) return -1;
    if (num <= 0) return 0;

    static JoinEnemyRec probe[64];
    static const int strides[] = { 36, 40, 32, 44, 48, 52 };
    int chosen = 0;
    for (unsigned si = 0; si < sizeof(strides) / sizeof(strides[0]); ++si) {
        int got = JoinEnemyRead(data, num, strides[si], probe, 64);
        if (got > 0) { chosen = strides[si]; break; }
    }
    if (!chosen) {
        QDLOG(LOG_WARN, "join: EnemySaveStates at offset ", dec(off),
              " could not be decoded (no stride passed the bValid/finite probe); ",
              "the death set falls back to this machine's ledger");
        return -1;
    }

    static JoinEnemyRec recs[1024];
    int n = JoinEnemyRead(data, num, chosen, recs, 1024);
    if (n < 0) {
        QDLOG(LOG_WARN, "join: EnemySaveStates read failed at the chosen stride ", dec(chosen));
        return -1;
    }
    int dead = 0;
    for (int i = 0; i < n; ++i) {
        if (!recs[i].bValid) continue;
        if (!(recs[i].hp <= 0.0f)) continue;
        if (!s->HasEnemy(recs[i].guid)) {
            s->AddEnemy(recs[i].guid);
            ++dead;
        }
    }
    QDLOG(LOG_INFO, "join: EnemySaveStates offset=", dec(off), " num=", dec((int)num),
          " stride=", dec(chosen), " decoded=", dec(n), " dead=", dec(dead));
    return n;
}

// The mod's own death memory: deaths this machine ran, deaths the partner ran, and records
// the sweep has already marked.  Unioned with the save's set so a death recorded in this
// session is not lost when the save map could not be decoded.
static int JoinCollectLedgerDeaths(join::State* s, uint32_t now)
{
    int added = 0;
    for (int i = 0; i < g_killLog.Count(); ++i) {
        const uint32_t* g = g_killLog.At(i);
        if (!g || s->HasEnemy(g)) continue;
        s->AddEnemy(g);
        ++added;
    }
    for (int i = 0;; ++i) {
        const uint32_t* g = g_remoteDead.At(i, now);
        if (!g) break;
        if (s->HasEnemy(g)) continue;
        s->AddEnemy(g);
        ++added;
    }
    for (int i = 0; i < g_enN; ++i) {
        const EnemyRec& r = g_en[i];
        if (!r.haveGuid) continue;
        if (!r.deathRecorded && !(r.lastHp <= 0.0f)) continue;
        if (s->HasEnemy(r.guid)) continue;
        s->AddEnemy(r.guid);
        ++added;
    }
    return added;
}

struct JoinInvCtx { join::State* s; };
static void JoinInvVisit(void* item, int32_t count, void* user)
{
    JoinInvCtx* c = (JoinInvCtx*)user;
    if (!c || !c->s || !item || count <= 0) return;
    // The identity is the item's soft object path, exactly what `InventoryContents` stores
    // and what `WrGrantItem` resolves back to an object (SYNC-DESIGN section 2.4).
    char path[join::kQuestPathCap];
    int n = 0;
    __try {
        n = avatar::AssetPath(item, path, sizeof(path));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        n = 0;
    }
    if (n <= 0) return;
    if (!InvEntryIsQuest(item, path)) return;
    c->s->AddQuest(path, count);
}

static void JoinCollectPuzzle(join::State* s)
{
    if (!Cfg().puzzle_sync) return;
    void* actorBase = FindClassByName("Actor");
    void* world = GWorldPtr();
    if (!actorBase || !world) return;
    int total = ObjectCount();
    int guard = 0;
    for (int i = 0; i < total && guard < 600000; ++i, ++guard) {
        void* o = ObjectAt(i);
        if (!o || !LooksLikeUObject(o)) continue;
        char cls[96] = "";
        bool member = false;
        __try {
            member = IsSubclassOf(ObjClass(o), actorBase) && ActorInThisWorld(o, world) &&
                     FNameToString(ObjNameRaw(ObjClass(o)), cls, sizeof(cls)) > 0 &&
                     worldsync::IsPuzzlePropClass(cls);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            member = false;
        }
        if (!member) continue;
        ActorXform x = {};
        if (!ReadActorXform(o, &x)) continue;
        char path[192];
        if (ActorLevelPath(o, world, path, sizeof(path)) <= 0) continue;
        uint32_t key[4];
        enemyid::Hash128(path, worldsync::kPuzzleSeed, key);
        if (join::State::GuidZero(key)) continue;
        join::PuzzleRow row;
        memset(&row, 0, sizeof(row));
        for (int k = 0; k < 4; ++k) row.key[k] = key[k];
        row.yaw = wire::QuantizeAngle(x.yaw);
        row.pitch = wire::QuantizeAngle(x.pitch);
        row.state = 0;
        row.seq = 0;
        s->AddPuzzle(row);
    }
}

// One builder for both sides.  `s` is cleared first.  `sentMask` says which categories this
// machine actually carried, so a disabled lane is "enable on one side only", never a lie
// about an empty container.
static void JoinBuildState(join::State* s, uint32_t now)
{
    if (!s) return;
    s->Reset();

    // flags: straight out of the game's own NamedSaveData
    if (Save().enumerateOk) {
        JoinNsdCtx ctx;
        ctx.s = s;
        if (NsdEnumerate(&JoinNsdVisit, &ctx) >= 0) {
            s->sentMask |= (uint8_t)(1u << (join::JC_FLAGS - 1));
        }
    }

    // savable actor states
    {
        actorstate::Snapshot snap;
        int live = 0, dropped = 0;
        if (ReadLocalActorStates(&snap, &live, &dropped)) {
            for (int i = 0; i < snap.Count(); ++i) {
                const actorstate::Row* r = snap.At(i);
                if (!r) continue;
                s->AddActor(r->guid, r->value);
            }
            s->sentMask |= (uint8_t)(1u << (join::JC_ACTORS - 1));
        }
    }

    // placed world objects.  The scan is incremental and world-scoped; the host pushes its
    // own table and the peer's is built by the same call.
    if (Cfg().object_sync) {
        int guard = 0;
        // One full pass so the set is complete, not a prefix of the incremental scan.  The
        // call's own budget keeps each step small; the cap bounds a pathological world.
        do {
            ObjectsScanStep();
            ++guard;
        } while (g_objScanCursor != 0 && guard < 600);
        objects::Snapshot snap;
        int dropped = 0;
        ObserveLocalObjects(&snap, &dropped);
        for (int i = 0; i < snap.Count(); ++i) {
            const objects::Row* r = snap.At(i);
            if (!r) continue;
            s->AddObject(r->key, r->shape, r->state);
        }
        s->sentMask |= (uint8_t)(1u << (join::JC_OBJECTS - 1));
    }

    // enemy deaths: the save's map first, the ledger unioned in
    {
        JoinCollectDeadEnemies(s);
        JoinCollectLedgerDeaths(s, now);
        s->sentMask |= (uint8_t)(1u << (join::JC_ENEMY - 1));
    }

    // quest / key items
    {
        JoinInvCtx ctx;
        ctx.s = s;
        if (InventoryEnumerate(&JoinInvVisit, &ctx) >= 0) {
            s->sentMask |= (uint8_t)(1u << (join::JC_QUEST - 1));
        }
    }

    // the three save unlock arrays
    {
        for (uint8_t set = worldset::SET_TELEPORTERS; set < worldset::SET_COUNT; ++set) {
            worldsync::ArrView v;
            if (!worldsync::SaveArrayView(set, &v)) continue;
            for (int32_t i = 0; i < v.num; ++i) {
                uint8_t* el = nullptr;
                __try { el = v.data[i]; } __except (EXCEPTION_EXECUTE_HANDLER) { el = nullptr; }
                char nm[join::kSetKeyCap];
                if (!worldsync::ElementName(el, nm, sizeof(nm))) continue;
                if (!worldset::PlausibleKey(nm)) continue;
                s->AddSet(set, nm);
            }
            s->sentMask |= (uint8_t)(1u << (join::JC_WORLDSET - 1));
        }
    }

    // puzzle poses
    if (Cfg().puzzle_sync) {
        JoinCollectPuzzle(s);
        s->sentMask |= (uint8_t)(1u << (join::JC_PUZZLE - 1));
    }

    // the current world and area.  `area` is the sublevel this machine pinned, which is the
    // same fact the travel lane's TP_AREA carries.
    {
        char world[join::kMapNameCap];
        LocalMapName(world, sizeof(world));
        char area[join::kMapNameCap];
        join::State::CopyText(area, sizeof(area), g_rigLevelPin);
        s->SetMap(world, area, 0, 0, 0);
        s->sentMask |= (uint8_t)(1u << (join::JC_MAP - 1));
    }
}

// ---------------------------------------------------------------------------
// The save-file backup.  The DLL cannot pull in bcrypt for a SHA-256, so this is the
// copy half of `mod/tools/backup.ps1` (which the brief allows): every `.sav` in the save
// directory is copied into a timestamped directory beside it, and the copy is verified by
// byte count.  The game itself writes suspend saves on its own schedule, so a copy that
// changes size while it is being taken is retried once.
// ---------------------------------------------------------------------------
static int JoinCopyFileVerified(const char* src, const char* dst)
{
    if (!CopyFileA(src, dst, FALSE)) return 0;
    WIN32_FILE_ATTRIBUTE_DATA a, b;
    if (!GetFileAttributesExA(src, GetFileExInfoStandard, &a)) return 0;
    if (!GetFileAttributesExA(dst, GetFileExInfoStandard, &b)) return 0;
    if (a.nFileSizeHigh != b.nFileSizeHigh || a.nFileSizeLow != b.nFileSizeLow) return 0;
    return 1;
}

static bool JoinStamp(char* out, int cap)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    int n = snprintf(out, (size_t)cap, "%04u%02u%02u-%02u%02u%02u",
                     (unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
                     (unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond);
    return n > 0 && n < cap;
}

// The save directory the GAME writes to.  [coordinator] A shipping UE4 game on Windows saves
// under %LOCALAPPDATA%\<Project>\Saved\SaveGames -- NOT next to the executable -- and
// `-saveddirsuffix=<x>` renames `Saved` to `Saved_<x>`.  The first version guessed
// `<exe>\..\..\Saved\SaveGames`, so on a real player's machine the backup failed and the
// reconcile refused itself (measured on the rig as `save directory does not exist ...
// inst2\Saved\SaveGames` while the game wrote `inst2\Saved_qd2\SaveGames`).
static bool JoinDirExists(const char* d)
{
    DWORD a = GetFileAttributesA(d);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static bool JoinSaveDir(char* out, int cap)
{
    if (!out || cap < 32) return false;
    out[0] = 0;
    char sfx[64] = "", savedName[80] = "Saved";
    if (CmdArg("-saveddirsuffix", sfx, sizeof(sfx)) && sfx[0])
        snprintf(savedName, sizeof(savedName), "Saved_%s", sfx);
    char base[MAX_PATH] = "";
    if (CmdArg("-UserDir", base, sizeof(base)) && base[0]) {
        int n = snprintf(out, (size_t)cap, "%s\\%s\\SaveGames", base, savedName);
        return n > 0 && n < cap;
    }
    char la[MAX_PATH];
    DWORD got = GetEnvironmentVariableA("LOCALAPPDATA", la, MAX_PATH);
    if (got > 0 && got < MAX_PATH) {
        int n = snprintf(out, (size_t)cap, "%s\\QueensDomain\\%s\\SaveGames", la, savedName);
        if (n > 0 && n < cap && JoinDirExists(out)) return true;
    }
    // Last resort (a portable/odd install): next to the project directory.
    char exe[MAX_PATH];
    got = GetModuleFileNameA(nullptr, exe, MAX_PATH);
    if (got == 0 || got >= MAX_PATH) return false;
    int cut = (int)got;
    while (cut > 0 && exe[cut - 1] != '\\' && exe[cut - 1] != '/') --cut;
    exe[cut] = 0;
    int n = snprintf(out, (size_t)cap, "%s..\\..\\%s\\SaveGames", exe, savedName);
    return n > 0 && n < cap;
}

static bool JoinBackupSave(char* why, int cap)
{
    g_backupPath[0] = 0;
    char dir[MAX_PATH];
    if (!JoinSaveDir(dir, sizeof(dir))) {
        ErrSet(why, cap, "cannot locate the save directory; refusing to reconcile", "");
        return false;
    }
    DWORD attr = GetFileAttributesA(dir);
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        ErrSet(why, cap, "save directory does not exist: ", dir);
        return false;
    }

    char stamp[32];
    if (!JoinStamp(stamp, sizeof(stamp))) join::State::CopyText(stamp, sizeof(stamp), "now");
    char backup[MAX_PATH];
    int n = snprintf(backup, sizeof(backup), "%s\\join-backup-%s", dir, stamp);
    if (n <= 0 || n >= (int)sizeof(backup)) {
        ErrSet(why, cap, "backup path too long", "");
        return false;
    }
    CreateDirectoryA(backup, nullptr);          // already-exists is fine
    join::State::CopyText(g_backupPath, sizeof(g_backupPath), backup);

    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*.sav", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    int copied = 0, failed = 0;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            char src[MAX_PATH], dst[MAX_PATH];
            if (snprintf(src, sizeof(src), "%s\\%s", dir, fd.cFileName) <= 0) continue;
            if (snprintf(dst, sizeof(dst), "%s\\%s", backup, fd.cFileName) <= 0) continue;
            if (JoinCopyFileVerified(src, dst)) {
                ++copied;
            } else if (JoinCopyFileVerified(src, dst)) {
                ++copied;                        // one retry against a writer mid-save
            } else {
                ++failed;
                QDLOG(LOG_WARN, "join: backup copy failed for ", fd.cFileName);
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    if (copied == 0) {
        ErrSet(why, cap, "no .sav file could be backed up; refusing to reconcile", "");
        return false;
    }
    QDLOG(LOG_INFO, "join: backed up ", dec(copied), " save file(s) to ", backup,
          (failed ? " (some files failed)" : ""));
    return failed == 0;
}

// ---------------------------------------------------------------------------
// Applying the plan -- peer only
// ---------------------------------------------------------------------------
static void JoinResetEchoes()
{
    // This machine is about to WRITE state that every detector would report as a local
    // change.  Reset their baselines instead: the next observation pass is a baseline, not
    // a burst of intents (the same rule the lanes use at join).
    g_pendingFlags.Reset();
    g_mirrorBaseline = false;
    g_pendingActors.Reset();
    g_actorMirror.Reset();
    g_pendingObjs.Reset();
    g_objMirror.Reset();
    g_invIntents.Reset();
    g_invForceObserve = true;
}

// The mirror holds what this machine last observed; the reconcile's writes are not a
// player's act, so they update the memory without queueing an intent.
static void JoinMirrorSet(const char* key, int32_t value)
{
    int i = MirrorFind(key);
    if (i >= 0) { g_mirror[i].last_value = value; return; }
    MirrorAdd(key, value);
}

static void JoinApplyFlags(uint32_t now, int* setOut, int* revertedOut, int* donatedOut)
{
    int set = 0, reverted = 0, donated = 0, failed = 0;
    for (int i = 0; i < g_plan.nFlag; ++i) {
        const join::FlagOp& o = g_plan.flag[i];
        if (!o.name[0]) continue;
        if (o.act == join::A_DONATE) {
            // The pair's SUM: hand the host the excess as an ADDITIVE intent, and leave
            // this machine's own value alone.  The host reaches the peer's value and the
            // two agree without either losing its own chests.
            uint8_t body[256];
            wire::Writer w(body, sizeof(body));
            wire::WriteIntentHeader(w, wire::I_SET_FLAG, g_flags.Revision());
            w.Str(o.name);
            w.I32(o.to);
            w.U8(0);                              // mode 0 = additive
            if (w.ok && g_net.SendReliable(wire::K_INTENT, body, w.len)) {
                ++donated;
                QDLOG(LOG_INFO, "join: donated ", dec(o.to), " to the host's ", o.name,
                      " (this machine holds ", dec(o.from), ")");
            } else {
                QDLOG(LOG_WARN, "join: the counter donation for ", o.name, " was not sent");
            }
            continue;
        }
        if (o.act != join::A_ADOPT && o.act != join::A_REVERT) continue;
        char err[160];
        if (NsdWrite(o.name, o.to, err, sizeof(err))) {
            g_flags.SetLocal(o.name, o.to, true);   // absolute: a revert may LOWER it
            JoinMirrorSet(o.name, o.to);
            FlagRefreshWorld(o.name, o.to);
            if (o.act == join::A_REVERT) ++reverted; else ++set;
        } else {
            ++failed;
            if (failed <= 8) QDLOG(LOG_WARN, "join: flag ", o.name, " = ", dec(o.to),
                                   " was refused: ", err);
        }
    }
    (void)now;
    if (setOut) *setOut = set;
    if (revertedOut) *revertedOut = reverted;
    if (donatedOut) *donatedOut = donated;
}

static int JoinApplyActors()
{
    int n = 0, failed = 0;
    for (int i = 0; i < g_plan.nActor; ++i) {
        const join::ActorOp& o = g_plan.actor[i];
        QdGuid q = { o.guid[0], o.guid[1], o.guid[2], o.guid[3] };
        void* actor = nullptr;
        int index = -1;
        FindActorStateOwner(q, &actor, &index);

        char err[192];
        bool changed = false;
        if (!WrSetActorState(q, o.to, actor, index, err, sizeof(err), &changed)) {
            ++failed;
            if (failed <= 8) {
                QDLOG(LOG_WARN, "join: actor state guid=", hex32(o.guid[0]), " value=",
                      dec(o.to), " was refused: ", err);
            }
            continue;
        }
        ++n;
        if (actor && index >= 0) {
            char rerr[192];
            bool ok = WrRefreshActorState(actor, index, o.to, rerr, sizeof(rerr));
            if (!ok) {
                QDLOG(LOG_WARN, "join: actor state refresh guid=", hex32(o.guid[0]),
                      " value=", dec(o.to), " INCOMPLETE: ", rerr);
            }
            // A door / lever also needs its own timeline driven: the game's close verb is an
            // interaction, but `SetToOpenState(false)` / `Deactivate` is the visual it calls.
            if (Cfg().interact_sync && actor) {
                objects::Shape sh = objects::SH_NONE;
                __try {
                    char cn[96];
                    if (FNameToString(ObjNameRaw(ObjClass(actor)), cn, sizeof(cn)) > 0)
                        sh = interact::ShapeOfClass(cn);
                } __except (EXCEPTION_EXECUTE_HANDLER) { sh = objects::SH_NONE; }
                if (interact::Bidirectional(sh)) {
                    char how[192], oerr[192];
                    if (!ObjectApplyToActor(actor, sh, (uint8_t)(o.to ? 1 : 0),
                                            how, sizeof(how), oerr, sizeof(oerr))) {
                        QDLOG(LOG_DEBUG, "join: actor visual for guid=", hex32(o.guid[0]),
                              " did not run: ", oerr);
                    }
                }
            }
        }
    }
    if (failed) QDLOG(LOG_WARN, "join: ", dec(failed), " actor state(s) refused");
    return n;
}

static int JoinApplyObjects()
{
    int n = 0, unknown = 0, failed = 0;
    for (int i = 0; i < g_plan.nObject; ++i) {
        const join::ObjectOp& o = g_plan.object[i];
        int idx = ObjectFindByKey(o.key);
        if (idx < 0) { ++unknown; continue; }
        char how[192], err[192];
        if (ObjectApplyToActor(g_objActors[idx].actor, (objects::Shape)o.shape,
                               o.to, how, sizeof(how), err, sizeof(err))) {
            ++n;
            g_objMirror.Note(o.key, o.shape, o.to);
            QDLOG(LOG_INFO, "join: object ", objects::ShapeName((objects::Shape)o.shape),
                  " key=", hex32(o.key[0]), " ", dec((int)o.from), "->", dec((int)o.to),
                  " ", how);
        } else {
            ++failed;
            if (failed <= 8) {
                QDLOG(LOG_WARN, "join: object key=", hex32(o.key[0]), " state=",
                      dec((int)o.to), " was refused: ", err);
            }
        }
    }
    if (unknown) {
        QDLOG(LOG_INFO, "join: ", dec(unknown), " object(s) are not loaded on this machine; ",
              "their state follows when their level streams in");
    }
    if (failed) QDLOG(LOG_WARN, "join: ", dec(failed), " object(s) refused");
    return n;
}

static int JoinApplyEnemyReset(uint32_t now)
{
    if (g_plan.nEnemy == 0) return 0;
    void* world = GWorldPtr();
    if (!world || !Save().save) return 0;

    // The game's own clear, then the host's dead set written back.  Exactly the shape the
    // rest lane uses (`ApplyRestReset`), because it is the one mechanism measured to work
    // on this build's `EnemySaveStates`.
    g_joinApplying = true;
    bool cleared = false;
    __try {
        worldsync::FnClearAllEnemySaveStates fn =
            (worldsync::FnClearAllEnemySaveStates)Ptr<void*>(0x00E27460u);
        if (fn) { fn(world); cleared = true; }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        cleared = false;
    }
    if (!cleared) {
        g_joinApplying = false;
        QDLOG(LOG_WARN, "join: ClearAllEnemySaveStates was refused; the enemy set is not "
                        "reconciled");
        return 0;
    }
    int restored = 0;
    for (int i = 0; i < g_hostState.nEnemy; ++i) {
        const join::KeyRow& k = g_hostState.enemy[i];
        QdGuid g = { k.guid[0], k.guid[1], k.guid[2], k.guid[3] };
        __try {
            StoreEnemySaveState(world, g, -1.0f, 0.0f);    // never comes back at a join
            ++restored;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    g_joinApplying = false;

    // This machine's own death memory now names enemies the host runs alive: drop it, so
    // the next sweep can see them and the death broadcast cannot re-kill them.
    g_killLog.Clear();
    g_remoteDead.Reset();
    for (int i = 0; i < g_plan.nEnemy; ++i) {
        const join::EnemyOp& o = g_plan.enemy[i];
        if (o.act != join::A_REVIVE) continue;
        g_remoteDead.Remove(o.guid);
        EnemyRec* r = EnemyByGuid(o.guid);
        if (!r) continue;
        r->deathRecorded = false;
        r->killedByUs = false;
        r->ledgerKillAt = 0;
        r->hpZeroSince = 0;
        r->seenAlive = false;
    }
    (void)now;
    QDLOG(LOG_INFO, "join: enemy world reset -- ClearAllEnemySaveStates ran, ",
          dec(restored), " host death(s) restored, ", dec(g_plan.EnemyRevived()),
          " revived, ", dec(g_plan.EnemyKilled()), " killed");
    return 1;
}

static int JoinApplyQuest(int* added, int* removed)
{
    int n = 0, failed = 0;
    for (int i = 0; i < g_plan.nQuest; ++i) {
        const join::QuestOp& o = g_plan.quest[i];
        char err[192];
        if (o.act == join::A_ADOPT) {
            int32_t delta = o.to - o.from;
            if (delta <= 0) continue;
            if (WrGrantItem(o.path, delta, err, sizeof(err))) {
                ++*added; ++n;
            } else {
                ++failed;
                if (failed <= 8) QDLOG(LOG_WARN, "join: key item grant ", o.path, " x",
                                       dec(delta), " refused: ", err);
            }
        } else if (o.act == join::A_REVERT) {
            int32_t want = o.from - o.to;
            if (want <= 0) continue;
            int32_t got = 0;
            if (WrConsumeItem(o.path, want, &got, err, sizeof(err))) {
                ++*removed; ++n;
            } else {
                ++failed;
                if (failed <= 8) QDLOG(LOG_WARN, "join: key item removal ", o.path, " x",
                                       dec(want), " refused: ", err);
            }
        }
    }
    if (failed) QDLOG(LOG_WARN, "join: ", dec(failed), " key item change(s) refused");
    return n;
}

// In-place removal from a `TArray`: shift the tail down and lower Num.  No allocation, so
// it is safe on a foreign (engine-owned) array; a realloc is not.
static bool JoinArrayRemove(const worldsync::ArrView& v, int off, int index)
{
    if (!Save().save || off < 0 || index < 0 || index >= v.num || !v.data) return false;
    bool ok = false;
    __try {
        for (int32_t i = index; i + 1 < v.num; ++i) v.data[i] = v.data[i + 1];
        v.data[v.num - 1] = nullptr;
        *(int32_t*)((uint8_t*)Save().save + off + 8) = v.num - 1;
        ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    return ok;
}

static int JoinApplySets(int* added, int* removed)
{
    int n = 0, failed = 0;
    for (int i = 0; i < g_plan.nSet; ++i) {
        const join::SetOp& o = g_plan.set[i];
        if (!worldset::SetValid(o.set) || !o.key[0]) continue;
        worldsync::ArrView v;
        if (!worldsync::SaveArrayView(o.set, &v)) continue;
        int off = -1;
        if (Save().save) {
            off = FindPropertyOffsetIn(ObjClass(Save().save), worldset::SetProperty(o.set), nullptr);
        }
        if (o.act == join::A_ADOPT) {
            if (worldsync::ArrayHolds(v, o.key)) continue;
            void* obj = worldsync::ResolveElement(o.key);
            if (!obj) {
                ++failed;
                QDLOG(LOG_WARN, "join: '", o.key, "' does not resolve to an object here");
                continue;
            }
            bool placed = false;
            if (v.num < v.max && off >= 0) {
                __try {
                    v.data[v.num] = (uint8_t*)obj;
                    *(int32_t*)((uint8_t*)Save().save + off + 8) = v.num + 1;
                    placed = true;
                    ++v.num;
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    placed = false;
                }
            }
            if (placed) {
                ++*added; ++n;
            } else if (o.set == worldset::SET_TELEPORTERS) {
                void* tp = worldsync::FindTeleporterFor(o.key);
                char how[96] = "";
                if (tp && worldsync::RegisterTeleporter(tp, how, sizeof(how))) {
                    ++*added; ++n;
                    QDLOG(LOG_INFO, "join: teleporter ", o.key,
                          " unlocked through the game's own RegisterSphere");
                } else {
                    ++failed;
                    QDLOG(LOG_WARN, "join: teleporter ", o.key, " not placeable here");
                }
            } else {
                ++failed;
                QDLOG(LOG_WARN, "join: ", worldset::SetName(o.set), " is FULL and '", o.key,
                      "' cannot be appended without the engine allocator");
            }
        } else if (o.act == join::A_REVERT) {
            int index = -1;
            for (int32_t k = 0; k < v.num; ++k) {
                uint8_t* el = nullptr;
                __try { el = v.data[k]; } __except (EXCEPTION_EXECUTE_HANDLER) { el = nullptr; }
                char nm[join::kSetKeyCap];
                if (!worldsync::ElementName(el, nm, sizeof(nm))) continue;
                if (join::State::TextEq(nm, o.key)) { index = (int)k; break; }
            }
            if (index < 0) continue;
            if (JoinArrayRemove(v, off, index)) {
                ++*removed; ++n;
                QDLOG(LOG_INFO, "join: removed ", o.key, " from ", worldset::SetName(o.set),
                      " (the host does not hold it)");
            } else {
                ++failed;
                QDLOG(LOG_WARN, "join: could not remove ", o.key, " from ",
                      worldset::SetName(o.set));
            }
        }
    }

    // The union lane is monotone and would put a removed key straight back on its next
    // pass.  The host's set IS the truth at a join, so the memory is rebuilt from the
    // now-applied arrays rather than edited key by key.
    if (*removed > 0) {
        for (uint8_t set = worldset::SET_TELEPORTERS; set < worldset::SET_COUNT; ++set) {
            worldsync::ArrView v;
            if (!worldsync::SaveArrayView(set, &v)) continue;
            worldsync::g_union[set].Reset();
            for (int32_t i = 0; i < v.num; ++i) {
                uint8_t* el = nullptr;
                __try { el = v.data[i]; } __except (EXCEPTION_EXECUTE_HANDLER) { el = nullptr; }
                char nm[join::kSetKeyCap];
                if (!worldsync::ElementName(el, nm, sizeof(nm))) continue;
                worldsync::g_union[set].Merge(nm);
            }
            const char* dk = worldsync::DerivedCounter(set);
            if (dk) worldsync::RaiseCounter(dk, v.num, "the join reconcile's set");
        }
    }
    if (failed) QDLOG(LOG_WARN, "join: ", dec(failed), " world-set change(s) refused");
    return n;
}

struct JoinPuzActor { uint32_t key[4]; void* actor; };
static JoinPuzActor g_joinPuz[join::kMaxPuzzle];
static int          g_joinPuzN = 0;

static void JoinScanPuzzleActors()
{
    g_joinPuzN = 0;
    void* actorBase = FindClassByName("Actor");
    void* world = GWorldPtr();
    if (!actorBase || !world) return;
    int total = ObjectCount();
    for (int i = 0; i < total && g_joinPuzN < join::kMaxPuzzle; ++i) {
        void* o = ObjectAt(i);
        if (!o || !LooksLikeUObject(o)) continue;
        char cls[96] = "";
        bool member = false;
        __try {
            member = IsSubclassOf(ObjClass(o), actorBase) && ActorInThisWorld(o, world) &&
                     FNameToString(ObjNameRaw(ObjClass(o)), cls, sizeof(cls)) > 0 &&
                     worldsync::IsPuzzlePropClass(cls);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            member = false;
        }
        if (!member) continue;
        char path[192];
        if (ActorLevelPath(o, world, path, sizeof(path)) <= 0) continue;
        uint32_t key[4];
        enemyid::Hash128(path, worldsync::kPuzzleSeed, key);
        if (join::State::GuidZero(key)) continue;
        for (int k = 0; k < 4; ++k) g_joinPuz[g_joinPuzN].key[k] = key[k];
        g_joinPuz[g_joinPuzN].actor = o;
        ++g_joinPuzN;
    }
}

static void* JoinFindPuzzleActor(const uint32_t key[4])
{
    for (int i = 0; i < g_joinPuzN; ++i)
        if (join::State::GuidEq(g_joinPuz[i].key, key) && ObjectIsLive(g_joinPuz[i].actor))
            return g_joinPuz[i].actor;
    return nullptr;
}

static int JoinApplyPuzzle()
{
    if (g_plan.nPuzzle == 0) return 0;
    JoinScanPuzzleActors();
    int n = 0;
    for (int i = 0; i < g_plan.nPuzzle; ++i) {
        const join::PuzzleRow& r = g_plan.puzzle[i].row;
        void* actor = JoinFindPuzzleActor(r.key);
        if (!actor) continue;
        ActorXform x = {};
        if (!ReadActorXform(actor, &x)) continue;
        if (SetActorXform(actor, x.px, x.py, x.pz, wire::DequantizeAngle(r.yaw))) {
            ++n;
            // Keep the puzzle lane's own per-key memory in step, so it does not re-publish
            // the pose as a local turn (its Row and ours are different structs).
            puzzle::Row pr;
            memset(&pr, 0, sizeof(pr));
            for (int k = 0; k < 4; ++k) pr.key[k] = r.key[k];
            pr.yaw = r.yaw;
            pr.pitch = r.pitch;
            pr.state = r.state;
            pr.seq = 1;
            worldsync::g_puzzle.OnPeer(pr);
        }
    }
    if (n) QDLOG(LOG_INFO, "join: applied ", dec(n), " puzzle pose(s)");
    return n;
}

static bool JoinTravel()
{
    if (!g_plan.travel) {
        join::State::CopyText(g_summaryTravel, sizeof(g_summaryTravel), "none");
        return true;
    }
    char err[192] = "";
    bool ok = false;
    if (g_plan.travel == join::TV_LEAVE) {
        ClearAreaPin("the join reconcile: the host is not in an area");
        join::State::CopyText(g_summaryTravel, sizeof(g_summaryTravel), "none");
        return true;
    }
    bool streamed = false;
    ok = FollowHardTravel(g_plan.travelTo, err, sizeof(err), &streamed, true);
    join::State::CopyText(g_summaryTravel, sizeof(g_summaryTravel), g_plan.travelTo);
    QDLOG(ok ? LOG_INFO : LOG_WARN, "join: travel to the host's ",
          (g_plan.travel == join::TV_AREA ? "area " : "map "), g_plan.travelTo,
          ok ? (streamed ? " (streamed as a sublevel)" : " (world load begun)")
             : " REFUSED: ", ok ? "" : err);
    return ok;
}

static uint8_t JoinApplyAll(uint32_t now)
{
    if (!Cfg().join_reconcile) return join::AK_IN_SYNC;
    if (!g_net.IsPeer()) return join::AK_IN_SYNC;
    // No active save (the main menu, or a save being replaced): nothing to diff and nothing
    // to write.  Deferred, so the pump re-asks when a save is up.
    if (!Save().save || ActiveSaveNoCreate(GWorldPtr()) != Save().save) {
        QDLOG(LOG_INFO, "join: no active save yet; the reconcile waits for the world");
        return join::AK_DEFERRED;
    }

    g_phase = PH_APPLY;

    // 0. the safety net FIRST: nothing is written until the peer's own save files exist in
    //    a timestamped directory beside them.
    char why[192] = "";
    if (!JoinBackupSave(why, sizeof(why))) {
        QDLOG(LOG_ERROR, "join: ", why, " -- the host's state was NOT applied");
        g_phase = PH_DONE;
        return join::AK_BACKUP_FAILED;
    }

    // 1. the diff is decided from what arrived, not from what we are about to write
    JoinBuildState(&g_localState, now);
    join::BuildPlan(g_hostState, g_localState, &g_plan);

    if (!g_plan.Any()) {
        QDLOG(LOG_INFO, "join: already in sync with host");
        g_phase = PH_DONE;
        return join::AK_IN_SYNC;
    }

    // 2. bracket the other lanes' echoes: their detectors would read these writes as the
    //    player's own acts.
    g_busyUntil = now + kBusyTailMs;
    JoinResetEchoes();

    int flagsSet = 0, flagsReverted = 0, donated = 0;
    int actorStates = 0, objects = 0, enemies = 0;
    int itemsAdded = 0, itemsRemoved = 0, setsAdded = 0, setsRemoved = 0, puzzles = 0;

    // 3. world sets first: the derived unlock counters are flags and are adopted after.
    JoinApplySets(&setsAdded, &setsRemoved);

    // 4. flags (a revert is the thing no other lane can do)
    JoinApplyFlags(now, &flagsSet, &flagsReverted, &donated);

    // 5. one-shot world objects (doors/chests/levers/secret walls/breakables)
    actorStates = JoinApplyActors();
    objects = JoinApplyObjects();

    // 6. enemy deaths: one reset, then the host's dead set written back
    enemies = JoinApplyEnemyReset(now);

    // 7. key items: add what the peer lacks, remove what the host lacks
    JoinApplyQuest(&itemsAdded, &itemsRemoved);

    // 8. puzzle poses (same world only; the props are not persisted)
    puzzles = JoinApplyPuzzle();

    // 9. persist this machine's save so a later SOLO load agrees with what happened.  The
    //    bracket stops the mirrored-save lane from announcing our own write as a request.
    {
        char serr[160];
        g_saveBoard.BeginOwnSave(now);
        int r = ProgressionSaveToDisk(progression::ROUTE_SLOT, serr, sizeof(serr));
        g_saveBoard.EndOwnSave(Net::Now());
        if (r > 0) {
            QDLOG(LOG_INFO, "join: this machine's save was written to disk");
        } else {
            QDLOG(LOG_WARN, "join: the save writer did not happen (", serr,
                  "); the in-memory state is still correct");
        }
    }

    // 10. the map LAST: a hard load tears the world down, so every save-scoped write has
    //     already landed before it runs.
    JoinTravel();

    // 11. the summary, exactly the shape the coordinator asked for
    QDLOG(LOG_INFO, "join: reconciled to host -- ", dec(flagsSet), " flags set, ",
          dec(flagsReverted), " flags reverted, ", dec(actorStates), " actor states, ",
          dec(enemies ? (g_plan.EnemyRevived() + g_plan.EnemyKilled()) : 0),
          " enemies revived/killed, ", dec(itemsAdded + itemsRemoved),
          " key items added/removed, travel=", g_summaryTravel,
          ", backup=", g_backupPath);
    QDLOG(LOG_INFO, "join: detail -- enemies ", dec(g_plan.EnemyRevived()), " revived / ",
          dec(g_plan.EnemyKilled()), " killed; key items ", dec(itemsAdded), " added / ",
          dec(itemsRemoved), " removed; objects ", dec(objects), "; sets ", dec(setsAdded),
          " added / ", dec(setsRemoved), " removed; puzzle ", dec(puzzles),
          "; counters donated ", dec(donated));

    g_phase = PH_DONE;
    return join::AK_RECONCILED;
}

// ---------------------------------------------------------------------------
// The host's serving side
// ---------------------------------------------------------------------------
// Advance the serving cursor to the next (category, sub) the host carries.  Returns false
// when every one is finished.
static bool JoinServeAdvance()
{
    for (;;) {
        if (!join::CatValid(g_serveCat)) return false;
        if (g_serveCat == join::JC_WORLDSET) {
            int nextSub = (int)g_serveSub + 1;
            if (nextSub < worldset::SET_COUNT) {
                g_serveSub = (uint8_t)nextSub;
                g_serveFirst = 0;
                return true;
            }
        }
        int nextCat = (int)g_serveCat + 1;
        if (nextCat > join::JC_MAP) return false;
        g_serveCat = (uint8_t)nextCat;
        g_serveSub = (g_serveCat == join::JC_WORLDSET) ? worldset::SET_TELEPORTERS : 0;
        g_serveFirst = 0;
        if (g_hostState.sentMask & (1u << (g_serveCat - 1))) return true;
        // not carried on this host: skip it
    }
}

static bool JoinCarried(uint8_t cat)
{
    return (g_hostState.sentMask & (1u << (cat - 1))) != 0;
}

// Put the cursor back at the first carried category (a repeat pass).
static void JoinServeRewind()
{
    g_serveFirst = 0;
    g_serveSub = 0;
    g_serveCat = join::JC_COUNT;               // "nothing left" unless a category is carried
    for (uint8_t c = join::JC_FLAGS; c <= join::JC_MAP; ++c) {
        if (JoinCarried(c)) { g_serveCat = c; break; }
    }
    if (g_serveCat == join::JC_WORLDSET) g_serveSub = worldset::SET_TELEPORTERS;
}

static void JoinServeStart(uint32_t seq)
{
    g_serveActive = true;
    g_serveSeq = seq;
    g_serveRepeatAt = 0;
    g_serveRepeats = 0;
    g_serveAckedAt = 0;
    JoinServeRewind();
}

// Sends ONE chunk (or one header-only body for an empty category) and advances.  Called
// from a per-tick loop, so a large category continues next tick with its cursor.
static void JoinServeStep(uint32_t now)
{
    if (!g_serveActive || !g_net.Connected()) return;
    if (!g_net.IsHost()) { g_serveActive = false; return; }

    for (int sent = 0; sent < kServePerTick; ++sent) {
        if (!join::CatValid(g_serveCat)) {
            // every category done: tell the peer, then repeat on a timer until it acks
            // (an unreliable chunk lane; the repeat IS the repair).
            uint8_t body[16];
            wire::Writer w(body, sizeof(body));
            join::WriteJoinEnd(w, g_serveSeq, g_hostState.sentMask);
            if (w.ok) g_net.SendUnreliable(wire::K_JOIN_END, body, w.len);
            JoinServeRewind();
            g_serveRepeatAt = now + kServeRepeatMs;
            ++g_serveRepeats;
            if (g_serveRepeats > kServeRepeats) {
                QDLOG(LOG_WARN, "join: the peer never acked the reconcile state after ",
                      dec(g_serveRepeats), " passes");
                g_serveActive = false;
            }
            return;
        }

        int total = join::TotalRows(g_hostState, g_serveCat, g_serveSub);
        uint8_t buf[wire::kMaxBody];
        wire::Writer w(buf, sizeof(buf));
        join::StateHeader h;
        h.seq = g_serveSeq;
        h.cat = g_serveCat;
        h.sub = g_serveSub;
        h.total = (uint16_t)total;
        h.first = (uint16_t)g_serveFirst;
        join::WriteStateHeader(w, h);
        int next = join::WriteRows(w, g_hostState, g_serveCat, g_serveSub, g_serveFirst);
        if (!w.ok) {
            QDLOG(LOG_WARN, "join: a chunk of ", join::CatName(g_serveCat),
                  " did not fit one datagram; the lane is stalled");
            return;
        }
        // An EMPTY category is announced too (a header with total 0): the peer's
        // completeness test is per category, and silence would read as "not sent".
        if (!g_net.SendUnreliable(wire::K_JOIN_STATE, buf, w.len)) return;
        if (next > g_serveFirst) g_serveFirst = next;
        if (g_serveFirst >= total) {
            // this (category, sub) is fully sent; move on
            g_serveFirst = 0;
            if (!JoinServeAdvance()) g_serveCat = join::JC_COUNT;
        }
    }
}

// ---------------------------------------------------------------------------
// The peer's receiving side
// ---------------------------------------------------------------------------
// A repeat pass (the host re-sends the whole state until the ack) restarts every category
// at row 0.  Without this clear, the second pass's first chunk would collide with the rows
// already accumulated and `ReadRows` would refuse the chunk as a duplicate.
static void JoinClearCategory(join::State* s, uint8_t cat, uint8_t sub)
{
    if (!s) return;
    switch (cat) {
    case join::JC_FLAGS:    s->nFlags = 0; break;
    case join::JC_ACTORS:   s->nActors = 0; break;
    case join::JC_OBJECTS:  s->nObjects = 0; break;
    case join::JC_ENEMY:    s->nEnemy = 0; break;
    case join::JC_QUEST:    s->nQuest = 0; break;
    case join::JC_WORLDSET: if (worldset::SetValid(sub)) s->nSet[sub] = 0; break;
    case join::JC_PUZZLE:   s->nPuzzle = 0; break;
    case join::JC_MAP:      s->haveMap = false; break;
    default: break;
    }
}

void JoinOnState(wire::Reader& r, uint32_t now)
{
    (void)now;
    if (!Cfg().join_reconcile || !g_net.IsPeer()) return;
    join::StateHeader h;
    if (!join::ReadStateHeader(r, &h)) {
        QDLOG(LOG_WARN, "join: a malformed state header was refused");
        return;
    }
    if (g_phase != PH_WAIT && g_phase != PH_APPLY) return;
    if (h.seq != g_seq) return;                     // another episode's chunk

    // A chunk that starts at row 0 begins a (new or repeated) pass of this category.
    if (h.first == 0) JoinClearCategory(&g_hostState, h.cat, h.sub);

    int n = join::ReadRows(r, h.cat, h.sub, &g_hostState);
    if (n < 0) {
        QDLOG(LOG_WARN, "join: a malformed ", join::CatName(h.cat), " chunk was refused");
        return;
    }
    g_recvHas |= (uint8_t)(1u << (h.cat - 1));
    // [coordinator] DATA IS ARRIVING: the wait is measured from the LAST chunk, not from the
    // request.  MEASURED (join run, relaunched peer): every row arrived but the game thread
    // stalled 19 s while loading, the 12 s deadline fired first and the reconcile was dropped.
    g_lastChunkAt = now;
    g_deadline = now + kWaitMs;
    int total = join::TotalRows(g_hostState, h.cat, h.sub);
    QDLOG(LOG_DEBUG, "join: state ", join::CatName(h.cat), " first=", dec((int)h.first),
          " rows=", dec(n), " have=", dec(total), " of ", dec((int)h.total));
    if (total >= (int)h.total && (int)h.first + n >= (int)h.total) {
        if (h.cat != join::JC_WORLDSET) {
            g_recvMask |= (uint8_t)(1u << (h.cat - 1));
        } else if (h.sub + 1 >= worldset::SET_COUNT) {
            g_recvMask |= (uint8_t)(1u << (h.cat - 1));
        }
    }
}

void JoinOnEnd(wire::Reader& r, uint32_t now)
{
    (void)now;
    if (!Cfg().join_reconcile || !g_net.IsPeer()) return;
    uint32_t seq = 0;
    uint8_t mask = 0;
    if (!join::ReadJoinEnd(r, &seq, &mask)) return;
    if (seq != g_seq) return;
    if (g_phase != PH_WAIT) return;

    // Every category the host announced must have arrived COMPLETE: a partial state is
    // never written (a truncated absolute state looks like a delete).
    uint8_t missing = (uint8_t)(mask & ~g_recvMask);
    if (missing) {
        QDLOG(LOG_WARN, "join: the host finished with ", hex32(missing),
              " category mask(s) still incomplete; the next pass will carry them");
        return;
    }
    QDLOG(LOG_INFO, "join: the host's state is complete (", hex32(mask),
          "); reconciling");
    g_phase = PH_APPLY;                     // the pump does the work, not this dispatch
}

void JoinOnReq(wire::Reader& r, uint32_t now)
{
    resp::RespawnAnnounceAgain();   // [coordinator] a (re)joining partner needs this player's stacks
    if (!Cfg().join_reconcile || !g_net.IsHost()) return;
    uint32_t seq = 0;
    uint8_t cat = 0;
    if (!join::ReadJoinReq(r, &seq, &cat)) return;
    (void)cat;
    // Build the host's view once per request and stream it.  The host NEVER writes: this is
    // a serialisation of what it already holds.
    JoinBuildState(&g_hostState, now);
    JoinServeStart(seq);
    QDLOG(LOG_INFO, "join: the peer asked for the world state (seq=", dec(seq),
          "); sending ", dec((int)g_hostState.nFlags), " flags, ",
          dec(g_hostState.nActors), " actor states, ", dec(g_hostState.nObjects),
          " objects, ", dec(g_hostState.nEnemy), " dead keys, ", dec(g_hostState.nQuest),
          " key items, mask=", hex32(g_hostState.sentMask));
    JoinServeStep(now);
}

void JoinOnAck(wire::Reader& r, uint32_t now)
{
    if (!Cfg().join_reconcile || !g_net.IsHost()) return;
    uint32_t seq = 0;
    uint8_t res = 0;
    if (!join::ReadJoinAck(r, &seq, &res)) return;
    g_serveActive = false;
    g_serveAckedAt = now;
    QDLOG(LOG_INFO, "join: the peer answered seq=", dec(seq), " result=", dec((int)res));
    // The peer's state now claims to match ours.  Force the auditor on this side too, so
    // the next digest pair is a fresh comparison rather than a ten-second-old memory.
    g_auditAt = now;
}

// ---------------------------------------------------------------------------
// The pump
// ---------------------------------------------------------------------------
void JoinPump(uint32_t now)
{
    if (!Cfg().join_reconcile) return;

    // The forced auditor, either side, once the phase is over and the world is not mid-load.
    if (g_auditAt && (int32_t)(now - g_auditAt) >= 0 && !g_worldTransition) {
        g_auditAt = 0;
        AuditForceNow("the join reconcile finished");
    }

    bool connected = g_net.Connected();
    if (!connected) {
        g_phase = PH_IDLE;
        g_requests = 0;
        g_deadline = 0;
        g_nextReq = 0;
        g_serveActive = false;
        g_wasConnected = false;
        g_silent = false;
        return;                              // nothing to do without a partner
    }

    // ---- host: serve the chunks, then the repeats until an ack ----
    if (g_net.IsHost()) {
        if (!g_serveActive) return;
        if (g_serveRepeatAt && (int32_t)(now - g_serveRepeatAt) >= 0) {
            uint8_t body[16];
            wire::Writer w(body, sizeof(body));
            join::WriteJoinEnd(w, g_serveSeq, g_hostState.sentMask);
            if (w.ok) g_net.SendUnreliable(wire::K_JOIN_END, body, w.len);
            g_serveRepeatAt = now + kServeRepeatMs;
            ++g_serveRepeats;
            if (g_serveRepeats > kServeRepeats) {
                QDLOG(LOG_WARN, "join: the peer never acked; stopping the repeats");
                g_serveActive = false;
            }
        } else {
            JoinServeStep(now);
        }
        return;
    }
    if (!g_net.IsPeer()) return;

    // ---- peer: establish detection ----
    uint32_t session = g_net.Session();
    uint32_t lastRx = g_net.LastRxMs();
    // [coordinator] SIGNED: the socket thread stamps last_rx_ms_ after this tick captured `now`,
    // so `now - lastRx` is often slightly NEGATIVE; unsigned it wrapped to ~4e9 ms of "silence"
    // and the next tick declared "the link came back" -- MEASURED: a re-request of the whole
    // world state every 100-400 ms for the life of the session.
    int32_t gap = (lastRx != 0) ? (int32_t)(now - lastRx) : 0;
    uint32_t silence = gap > 0 ? (uint32_t)gap : 0u;
    if (lastRx != 0 && silence > 3000) g_silent = true;
    bool reconnected = g_silent && silence < 700;
    bool newSession = (!g_wasConnected || session != g_lastSession);
    g_wasConnected = true;
    g_lastSession = session;

    if ((newSession || reconnected) && g_phase != PH_APPLY) {
        if (reconnected) g_silent = false;
        g_hostState.Reset();
        g_recvMask = 0;
        g_recvHas = 0;
        g_requests = 0;
        g_seq++;
        g_nextReq = now;
        g_deadline = now + kWaitMs;
        g_phase = PH_WAIT;
        QDLOG(LOG_INFO, "join: ", (newSession ? "session established" : "the link came back"),
              " (session=", dec(session), "); asking the host for its world state");
    } else if (g_phase == PH_IDLE && g_requests == 0) {
        // A session that was already up when this pump first ran (or an establish that
        // raced the WELCOME): ask once.
        g_hostState.Reset();
        g_recvMask = 0;
        g_recvHas = 0;
        g_seq++;
        g_nextReq = now;
        g_deadline = now + kWaitMs;
        g_phase = PH_WAIT;
    }

    // ---- peer: the apply ----
    if (g_phase == PH_APPLY) {
        uint8_t res = JoinApplyAll(now);
        if (res == join::AK_DEFERRED) {
            g_phase = PH_WAIT;
            g_deadline = now + kWaitMs;
            g_nextReq = now + kRetryReqMs;
            g_requests = 0;
        } else {
            // The ack stops the host's repeats and asks it to force its auditor too.
            uint8_t body[16];
            wire::Writer w(body, sizeof(body));
            join::WriteJoinAck(w, g_seq, res);
            if (w.ok) g_net.SendReliable(wire::K_JOIN_ACK, body, w.len);
            g_auditAt = now + 1500;
        }
        return;
    }

    if (g_phase != PH_WAIT) return;

    // ---- peer: the request and its bounded retries ----
    if (g_nextReq && (int32_t)(now - g_nextReq) >= 0) {
        if (g_requests >= (int)kMaxRequests) {
            QDLOG(LOG_WARN, "join: the host did not answer after ", dec(g_requests),
                  " requests; giving up on this establishment");
            g_phase = PH_DONE;
            return;
        }
        if (g_requests > 0 && g_lastChunkAt && (uint32_t)(now - g_lastChunkAt) < kRetryReqMs) {
            // [coordinator] the host is still streaming; re-asking would restart it from row 0
            g_nextReq = now + kRetryReqMs;
            return;
        }
        if (g_requests > 0) {
            // A retry re-uses the SAME sequence and clears the partial state: the host
            // serves from row 0 again, and a duplicate row would be refused by ReadRows.
            g_hostState.Reset();
            g_recvMask = 0;
            g_recvHas = 0;
        }
        ++g_requests;
        g_nextReq = now + kRetryReqMs;
        uint8_t body[16];
        wire::Writer w(body, sizeof(body));
        join::WriteJoinReq(w, g_seq, 0);
        if (w.ok) g_net.SendReliable(wire::K_JOIN_REQ, body, w.len);
        QDLOG(LOG_DEBUG, "join: asked for the host's state (try ", dec(g_requests),
              ", seq=", dec(g_seq), ")");
    }
    if (g_deadline && (int32_t)(now - g_deadline) >= 0) {
        QDLOG(LOG_WARN, "join: no complete state from the host within ",
              dec((int)(kWaitMs / 1000)), " s; the normal lanes carry this session");
        g_phase = PH_DONE;
    }
}

} // namespace joinsync

// ---------------------------------------------------------------------------
// The mechanical hooks proxy.cpp calls (declared in its forward block, defined here)
// ---------------------------------------------------------------------------
bool JoinBusy() { return joinsync::JoinBusyImpl(); }

void JoinReconcilePump(uint32_t now) { joinsync::JoinPump(now); }
