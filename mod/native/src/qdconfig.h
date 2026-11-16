// qdconfig.h -- qdcoop.ini.
//
// WHY ONE SWITCH PER SUBSYSTEM
// ----------------------------
// The corpus records this as a testing-discipline lesson, not a nicety: "One switch
// per subsystem, so one broken part does not block testing the rest."  Every
// subsystem below can be turned off independently, and the default for anything that
// writes to the game's state is OFF until it has been proven on a live session.
// That ordering matters: a default-on write path turns a first-run crash into an
// unattributable one.
//
// The file is read once at load and never re-read. A config change requires a
// restart, which is the honest contract for something that installs hooks at
// DllMain time.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdint.h>

namespace qd {

struct Config {
    // master
    int  enabled;          // 0 = the DLL loads, logs the substrate, does nothing else
    int  log_level;

    // transports
    char relay_host[64];
    uint16_t relay_port;

    // subsystem switches.  0 = observe and log only.
    int  flag_sync;        // NamedSaveData apply/broadcast
    int  inventory_sync;   // host -> peer InventoryContents mirror + grants
    int  actor_state_sync; // SavableActorStates
    int  object_sync;      // placed world objects that are NOT SavableActors (K_OBJECTS)
    int  interact_sync;    // worlds' doors/breakables/secret mechanisms (qdinteract.h)
    int  enemy_sync;       // enemy rows + StoreEnemySaveState
    int  mirror_spawn;     // construct a copy of an enemy this machine never spawned
    int  puppet_sync;      // remote body spawn/drive
    int  player_body;      // ... as the game's own player class (B7 / G4 / G5)
    int  dialogue_sync;    // lock + first-answer-decides
    int  dialogue_view;    // drive the partner's own dialogue widget in spectator mode (0x50)
    int  view_camera;      // [view-fix] the spectator camera at the TALKER's eye (qdview.h)
    int  stream_union;     // level-streaming union guard
    int  travel_sync;      // G6 map/world announcement, follow and hard handshake
    int  cutscene_sync;    // play/hold a partner's cutscene, resume together
    int  mover_sync;       // owner-authoritative moving world objects
    int  platform_ride;    // a rider's place ON the platform, keyed by the platform (K_PLATFORM_RIDE)
    int  loot_sync;        // one shared drop set
    int  suspend_guard;    // hold the peer's timed suspend save, repair a load
    int  suppress_pause;   // stop a menu freeze from freezing the session
    int  object_scan;      // walk GObjects to find UFunctions (OFF: it exits the game)
    int  trigger_sync;     // a partner entering a placed event volume fires it here (K_TRIGGER)
    int  boss_pull_in;     // ... and a boss arena's trigger moves a far partner in first
    // [progression] mod/doc/features/progression.md.  Three separate switches because
    // they are three separate risks: a narrowed lane, an invented award, and a write to
    // this machine's own save slot.
    int  quest_item_sync;  // only Items/KeyItems entries cross; loot stays per-player
    int  kill_xp_sync;     // a kill pays both players, measured, exactly once each
    int  partner_save_sync; // the partner's save writes this machine's own slot
    int  combat_vfx;       // every player projectile copied, plus its impact and cast VFX (EFFECT kind 5)
    int  death_respawn;    // a death is per player: back at the owner's rest point, penalised (qdrespawn.h)
    // [gap-world] qdworldset.h / qdrest.h / qdpuzzle.h.  Two switches because they are two
    // separate write risks: one edits the game's own save arrays and two achievement
    // counters, the other rotates placed actors.  Both default OFF until a live run shows
    // the `worldset:` / `rest:` / `puzzle:` lines.
    int  world_set_sync;   // the UNION of UnlockedTeleporters/SavePointsVisited/ShopTeleporter-
                           // Destinations + a rest as a world event (K_WORLD_SET 0x60, K_REST_WORLD 0x61)
    int  puzzle_sync;      // the rotation of a placed puzzle prop (K_PUZZLE 0x62)
    // [gap-boss] qdbossphase.h. One switch for the phase authority, because the arena rule
    // and the ending choice are the same fact seen at three moments: "the machine that RUNS
    // this boss decides what phase it is in, the other machine drives its own copy through
    // the game's own setters".  OFF until a live run has shown the Queen's transition play
    // on both screens (`boss phase: ... driven here`).
    int  boss_phase_sync;  // owner-authoritative boss phase + arena rule + ending choice
    // [play-p3] qdending.h.  The FOUR endings as a shared fact: the machine whose own game runs
    // an ending entry publishes it, and the partner runs the same entry on its own copy (so its
    // own cutscene plays and its own player wakes up).  It also gates the mod's level forcing
    // while a wake-up is pending -- the game's own wake-up waits on level management.
    int  ending_sync;      // the choice crosses, and the wake-up is not starved by the mod
    // [desync-audit] qdaudit.h.  READ-ONLY and cheap: it digests the containers the other
    // lanes already maintain and compares them, so it costs no game state and writes
    // nothing.  ON by default once proven -- a playthrough must be able to PROVE the two
    // machines agree, and a diagnostic that is off by default proves nothing.
    int  desync_audit;     // periodically digests + compares the synced world state (K_AUDIT)
    int  audit_ms;         // how often the digest pass runs (default 10000 = 10 s)
    // [join-sync] qdjoin.h.  On every session establishment the PEER adopts the HOST's
    // world/progression state exactly, INCLUDING reverting what the peer holds and the host
    // does not (the flag bank and the object lane are monotone and can never lower a
    // value).  OFF until the live proof shows the `join: reconciled to host -- ...` line,
    // `audit: OK`, and the backup directory; it WRITES the peer's save containers and
    // persists its slot, so it is a write path like every other one.
    int  join_reconcile;   // peer adopts the host's world/progression at every establish
    // [gap-combat] qdhazard.h.  Two switches because they are two risks: the first makes
    // the local player take damage from a MIRRORED hazard, the second lets a peer-run
    // enemy hurt the host (which was a silent no-op before: K_DAMAGE was host -> peer).
    int  hazard_sync;      // mirror spawned hazards; each machine hurts its own player
    int  damage_both_ways; // K_DAMAGE describes a hit on the OTHER machine's player

    // behaviour
    int  host_authoritative;   // 1 = only the host writes world facts
    int  allow_plaintext_internet; // refuses a public relay unless set
    int  spawn_attempts_max;
    int  enemy_rows_per_tick;  // capped at 20 by WIRE.md; lower on a weak link
    int  player_hz;            // 30 or 60
};

inline Config& Cfg() { static Config c; return c; }

inline void ConfigDefaults()
{
    Config& c = Cfg();
    c.enabled = 1;
    c.log_level = 2;                 // LOG_INFO

    const char* h = "127.0.0.1";
    int i = 0;
    for (; h[i] && i < (int)sizeof(c.relay_host) - 1; ++i) c.relay_host[i] = h[i];
    c.relay_host[i] = 0;
    c.relay_port = 47800;

    c.flag_sync        = 1;
    // OFF, like every other write path.  This one writes an item container through a
    // reflective Blueprint call; Enable it only after a live session has shown the
    // `inventory grant applied ... read back` line (qdwrite.h's WrGrantItem oracle).
    c.inventory_sync   = 0;
    c.actor_state_sync = 0;
    // OFF, like every other write path.  This one calls a Blueprint's visual/restore entry
    // and writes the object's own state bool, so enable it only on a run that is looking
    // for the `object apply:` line (`SYNC-GAPS` section 5.5).
    c.object_sync      = 0;
    // The world-interact coverage layer (qdinteract.h): the extended door/breakable/
    // secret classification, the class' own bidirectional animation plan, and the
    // savable door's explicit SetToOpenState after its restore.  OFF until a live run
    // has shown the partner's door open with the SAME animation and a broken barrel
    // stay broken on both machines (mod/doc/features/world-interact.md).
    c.interact_sync    = 0;
    // OFF like every write path: the receiver DISPATCHES a Blueprint event on its own
    // trigger.  Enable it on a run that looks for `trigger: replayed the partner's`.
    c.trigger_sync     = 0;
    // ON when the lane is on: without it a boss arena closes on a partner standing outside.
    c.boss_pull_in     = 1;
    // OFF until a live run has shown `effect: impact ...` and `effect: cast ...` on
    // the watching instance.  This is the widest visual lane: it copies EVERY player
    // projectile class (not just daggers) and spawns the impact and cast systems the
    // receiving side could otherwise never play.
    c.combat_vfx       = 0;
    c.enemy_sync       = 0;
    // ON: proven live in 780_SZ_Depths (LIVE-TEST-LOG Session 16).  This is the one write
    // path that CONSTRUCTS a full gameplay enemy Blueprint -- `Dragon_Green_BP_C` and friends
    // -- whose own BeginPlay runs, so the rule above ("off until a live session has shown it")
    // is what it was measured against: the partner now holds a copy of an enemy its own
    // spawner never created (item1 15/15, boss 25/25 in that run), it never publishes, it is
    // refused by the save hook, and it is removed when the owner says dead or the world
    // changes.  It stays a switch so one broken mirror can be turned off without losing the
    // rest of the enemy lane.
    c.mirror_spawn     = 1;
    c.puppet_sync      = 0;
    // OFF, like every other write path.  This one asks the engine to CONSTRUCT a full
    // gameplay Blueprint (`FS_PlayerChar_C`), whose ReceiveBeginPlay runs; enable it only
    // on a run that is already looking for B7's answer (`API-GAPS` section 5).
    c.player_body      = 0;
    c.dialogue_sync    = 0;
    // PROVEN LIVE (mod/doc/features/dialogue.md, run 3: the partner's dialogue box came up
    // with its speaker and line, the camera framed the speaker, input was locked, and A's
    // death released all three).  The built-in default stays OFF so a MISSING ini cannot
    // hijack a player's camera; the shipped `qdcoop.ini` turns it on.  The lane calls
    // DISPLAY functions only -- the dialogue's outcomes still ride the flag lane.
    c.dialogue_view    = 0;
    // [view-fix] OFF by default like every other engine write; the shipped `qdcoop.ini`
    // turns it on.  It spawns a temporary CameraActor and forces the talker's level
    // visible on this machine, so a missing ini falls back to the old behaviour.
    c.view_camera      = 0;
    c.stream_union     = 0;
    // Travel is independent of the streaming union on purpose: a hard map switch is a
    // world change, not a sublevel load, and it must still be followed with
    // StreamUnion=0.  Still default OFF, like every write path.
    c.travel_sync      = 0;
    c.cutscene_sync    = 0;
    c.mover_sync       = 0;
    c.platform_ride    = 0;
    c.loot_sync        = 0;
    c.suspend_guard    = 0;
    c.suppress_pause   = 0;
    c.object_scan      = 0;
    // OFF like every write path.  This one REFUSES a dispatch (the Game Over screen, which
    // pauses this world and reloads both players' world), teleports the local player and
    // writes its health.  Enable it after a run has shown the `respawn:` lines
    // (mod/doc/features/death-respawn.md).
    c.death_respawn    = 0;
    // [gap-world] OFF like every other write path.  `world_set_sync` appends to the game's own
    // `Array<Object>` members and raises `TeleporterUnlockCount`/`CheckpointUnlockCount`, and
    // it turns a rest into a world event (which CLEARS the local enemy table through the
    // game's own `ClearAllEnemySaveStates`).  `puzzle_sync` rotates placed actors.  Enable
    // each after a run has shown its `worldset:` / `rest:` / `puzzle:` lines
    // (mod/doc/features/gap-world.md).
    c.world_set_sync   = 0;
    c.puzzle_sync      = 0;
    // OFF until proven live, for the same reason: it CALLS the game's own state setters on
    // another machine's boss copy, which runs that copy's transition body.
    c.boss_phase_sync  = 0;
    // [play-p3] OFF until a live run shows the chooser's `boss end: sent` and the follower's
    // `boss end: driven here` -- it is a write path on the partner's own boss copy.
    c.ending_sync      = 0;

    // OFF until proven live, like every other write path.  `quest_item_sync` also
    // NARROWS two proven lanes (the inventory mirror and the shared drop set), so it is
    // the one to flip first: with it off, behaviour is exactly what it was before this
    // module existed.
    c.quest_item_sync = 0;
    c.kill_xp_sync = 0;
    c.partner_save_sync = 0;

    // [desync-audit] ON, and it is the one switch here that reads nothing but state the
    // other lanes already hold.  Proven live: a clean session logs `audit: OK ...` every
    // minute, and a deliberately de-synced item is reported by name and value.
    c.desync_audit = 1;
    c.audit_ms = 10000;
    // [join-sync] OFF, like every other write path.  It writes the peer's flag bank, its
    // `SavableActorStates`, its `EnemySaveStates`, its key items and its save arrays, then
    // persists the slot -- enable it after a live run has shown the
    // `join: reconciled to host -- ...` line and `audit: OK` (mod/doc/features/join-sync.md).
    c.join_reconcile = 0;

    // [gap-combat] OFF until a live run has shown the `hazard:` lines (a copy drawn on the
    // machine that did not spawn it, and damage applied by that machine to ITS OWN player).
    // `damage_both_ways` is the smaller of the two and the one to flip second: it only
    // widens an existing, proven receiver to the other direction.
    c.hazard_sync = 0;
    c.damage_both_ways = 0;

    c.host_authoritative = 1;
    c.allow_plaintext_internet = 0;
    c.spawn_attempts_max = 5;
    c.enemy_rows_per_tick = 20;
    c.player_hz = 30;
}

// ---- a very small INI reader ----------------------------------------------
// Hand-rolled because the game thread must not pull in <fstream>/<string>, and
// because the whole format is "section, key=value, # comment".
namespace detail {

inline char* Trim(char* s)
{
    while (*s == ' ' || *s == '\t') ++s;
    int n = 0;
    while (s[n]) ++n;
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) {
        s[--n] = 0;
    }
    return s;
}

inline bool StrEq(const char* a, const char* b)
{
    for (int i = 0; a[i] || b[i]; ++i) if (a[i] != b[i]) return false;
    return true;
}

inline int ParseInt(const char* s, int fallback)
{
    int v = 0;
    bool neg = false;
    if (*s == '-') { neg = true; ++s; }
    if (*s == 0) return fallback;
    for (; *s; ++s) {
        if (*s < '0' || *s > '9') return fallback;
        v = v * 10 + (*s - '0');
        if (v > 100000000) return fallback;
    }
    return neg ? -v : v;
}

inline void CopyStr(char* dst, int cap, const char* src)
{
    int i = 0;
    for (; src[i] && i < cap - 1; ++i) dst[i] = src[i];
    dst[i] = 0;
}

} // namespace detail

// The ini lives beside the game executable, not beside our DLL's module directory:
// a proxy DLL's own path is the game's Binaries directory either way, but being
// explicit stops a future relocation from silently reading a stale file.
inline bool ConfigPath(char* out, int cap)
{
    char exe[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, exe, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;
    // Strip the file name.
    int cut = (int)n;
    while (cut > 0 && exe[cut - 1] != '\\' && exe[cut - 1] != '/') --cut;
    int w = 0;
    for (int i = 0; i < cut && w < cap - 1; ++i) out[w++] = exe[i];
    const char* f = "qdcoop.ini";
    for (int i = 0; f[i] && w < cap - 1; ++i) out[w++] = f[i];
    out[w] = 0;
    return true;
}

// Parse an ini's TEXT into `Cfg()`.  Split out of ConfigLoad so the file-reading half and
// the parsing half can be tested separately -- which is the specific test that would have
// caught the 8192-byte truncation (a parser fed a truncated string looks perfectly correct;
// only "did the file's LAST switch arrive" catches it).  Returns the line count.
inline int ConfigParseText(char* buf)
{
    Config& c = Cfg();
    char section[32] = "";
    char* p = buf;
    int line = 0;
    while (*p) {
        // Split one line.
        char* start = p;
        while (*p && *p != '\n') ++p;
        if (*p == '\n') { *p = 0; ++p; }
        ++line;

        char* s = detail::Trim(start);
        if (*s == 0 || *s == '#' || *s == ';') continue;
        if (*s == '[') {
            char* e = s;
            while (*e && *e != ']') ++e;
            int n = (int)(e - s - 1);
            if (n > 0 && n < (int)sizeof(section)) {
                for (int i = 0; i < n; ++i) section[i] = s[1 + i];
                section[n] = 0;
            }
            continue;
        }
        char* eq = s;
        while (*eq && *eq != '=') ++eq;
        if (*eq != '=') continue;
        *eq = 0;
        char* k = detail::Trim(s);
        char* v = detail::Trim(eq + 1);

        if      (detail::StrEq(k, "Enabled"))       c.enabled = detail::ParseInt(v, c.enabled);
        else if (detail::StrEq(k, "LogLevel"))      c.log_level = detail::ParseInt(v, c.log_level);
        else if (detail::StrEq(k, "RelayHost"))     detail::CopyStr(c.relay_host, sizeof(c.relay_host), v);
        else if (detail::StrEq(k, "RelayPort"))     c.relay_port = (uint16_t)detail::ParseInt(v, c.relay_port);
        else if (detail::StrEq(k, "FlagSync"))      c.flag_sync = detail::ParseInt(v, c.flag_sync);
        else if (detail::StrEq(k, "InventorySync")) c.inventory_sync = detail::ParseInt(v, c.inventory_sync);
        else if (detail::StrEq(k, "ActorStateSync")) c.actor_state_sync = detail::ParseInt(v, c.actor_state_sync);
        else if (detail::StrEq(k, "ObjectSync"))    c.object_sync = detail::ParseInt(v, c.object_sync);
        else if (detail::StrEq(k, "InteractSync"))  c.interact_sync = detail::ParseInt(v, c.interact_sync);
        else if (detail::StrEq(k, "TriggerSync"))   c.trigger_sync = detail::ParseInt(v, c.trigger_sync);
        else if (detail::StrEq(k, "BossPullIn"))    c.boss_pull_in = detail::ParseInt(v, c.boss_pull_in);
        else if (detail::StrEq(k, "QuestItemSync")) c.quest_item_sync = detail::ParseInt(v, c.quest_item_sync);
        else if (detail::StrEq(k, "KillXPSync"))    c.kill_xp_sync = detail::ParseInt(v, c.kill_xp_sync);
        else if (detail::StrEq(k, "PartnerSaveSync")) c.partner_save_sync = detail::ParseInt(v, c.partner_save_sync);
    else if (detail::StrEq(k, "CombatVfx"))     c.combat_vfx = detail::ParseInt(v, c.combat_vfx);
        else if (detail::StrEq(k, "DesyncAudit"))  c.desync_audit = detail::ParseInt(v, c.desync_audit);
        else if (detail::StrEq(k, "AuditMs"))      c.audit_ms = detail::ParseInt(v, c.audit_ms);
        else if (detail::StrEq(k, "JoinReconcile")) c.join_reconcile = detail::ParseInt(v, c.join_reconcile);
        else if (detail::StrEq(k, "EnemySync"))     c.enemy_sync = detail::ParseInt(v, c.enemy_sync);
        else if (detail::StrEq(k, "MirrorSpawn"))   c.mirror_spawn = detail::ParseInt(v, c.mirror_spawn);
        else if (detail::StrEq(k, "PuppetSync"))    c.puppet_sync = detail::ParseInt(v, c.puppet_sync);
        else if (detail::StrEq(k, "PlayerBody"))    c.player_body = detail::ParseInt(v, c.player_body);
        else if (detail::StrEq(k, "DialogueSync"))  c.dialogue_sync = detail::ParseInt(v, c.dialogue_sync);
        else if (detail::StrEq(k, "DialogueView"))  c.dialogue_view = detail::ParseInt(v, c.dialogue_view);
        else if (detail::StrEq(k, "ViewCamera"))    c.view_camera = detail::ParseInt(v, c.view_camera);
        else if (detail::StrEq(k, "StreamUnion"))   c.stream_union = detail::ParseInt(v, c.stream_union);
        else if (detail::StrEq(k, "TravelSync"))    c.travel_sync = detail::ParseInt(v, c.travel_sync);
        else if (detail::StrEq(k, "CutsceneSync"))  c.cutscene_sync = detail::ParseInt(v, c.cutscene_sync);
        else if (detail::StrEq(k, "MoverSync"))     c.mover_sync = detail::ParseInt(v, c.mover_sync);
        else if (detail::StrEq(k, "PlatformRide"))  c.platform_ride = detail::ParseInt(v, c.platform_ride);
        else if (detail::StrEq(k, "LootSync"))      c.loot_sync = detail::ParseInt(v, c.loot_sync);
        else if (detail::StrEq(k, "SuspendGuard"))  c.suspend_guard = detail::ParseInt(v, c.suspend_guard);
        else if (detail::StrEq(k, "SuppressMenuPause")) c.suppress_pause = detail::ParseInt(v, c.suppress_pause);
        else if (detail::StrEq(k, "ObjectScan"))   c.object_scan = detail::ParseInt(v, c.object_scan);
        else if (detail::StrEq(k, "DeathRespawn"))  c.death_respawn = detail::ParseInt(v, c.death_respawn);
        else if (detail::StrEq(k, "BossPhaseSync")) c.boss_phase_sync = detail::ParseInt(v, c.boss_phase_sync);
        else if (detail::StrEq(k, "EndingSync"))    c.ending_sync = detail::ParseInt(v, c.ending_sync);
        else if (detail::StrEq(k, "WorldSetSync")) c.world_set_sync = detail::ParseInt(v, c.world_set_sync);
        else if (detail::StrEq(k, "PuzzleSync"))    c.puzzle_sync = detail::ParseInt(v, c.puzzle_sync);
        // [gap-combat] qdhazard.h.  HazardSync mirrors an enemy/boss-spawned hazard so both
        // machines see it and each hurts only its own player; DamageBothWays lets a
        // peer-run enemy describe a hit on the HOST's player, which K_DAMAGE could not
        // express before (host -> peer only).
        else if (detail::StrEq(k, "HazardSync"))    c.hazard_sync = detail::ParseInt(v, c.hazard_sync);
        else if (detail::StrEq(k, "DamageBothWays")) c.damage_both_ways = detail::ParseInt(v, c.damage_both_ways);
        else if (detail::StrEq(k, "HostAuthoritative")) c.host_authoritative = detail::ParseInt(v, c.host_authoritative);
        else if (detail::StrEq(k, "AllowPlaintextInternet")) c.allow_plaintext_internet = detail::ParseInt(v, c.allow_plaintext_internet);
        else if (detail::StrEq(k, "SpawnAttemptsMax")) c.spawn_attempts_max = detail::ParseInt(v, c.spawn_attempts_max);
        else if (detail::StrEq(k, "EnemyRowsPerTick")) c.enemy_rows_per_tick = detail::ParseInt(v, c.enemy_rows_per_tick);
        else if (detail::StrEq(k, "PlayerHz"))      c.player_hz = detail::ParseInt(v, c.player_hz);
        // Unknown keys are ignored on purpose: a config from a newer build must
        // still load on an older DLL rather than refuse to start.
    }

    if (c.enemy_rows_per_tick > 20) c.enemy_rows_per_tick = 20;
    if (c.player_hz != 30 && c.player_hz != 60) c.player_hz = 30;
    // A zero or negative interval would run the digest every frame; a sub-second one would
    // ask a slow lane to keep up with a fast pass.  Clamp rather than accept.
    if (c.audit_ms < 1000) c.audit_ms = 1000;
    if (c.audit_ms > 120000) c.audit_ms = 120000;
    return line;
}

// Load the ini.  A missing file is normal and means "defaults": the DLL must be
// installable without one.  A malformed line is logged and skipped, never fatal.
inline int ConfigLoad()
{
    ConfigDefaults();

    char path[MAX_PATH];
    if (!ConfigPath(path, MAX_PATH)) return 0;

    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return 0;

    // THE WHOLE FILE, NOT A PREFIX.  This used to read a fixed 8192 bytes in ONE
    // ReadFile, which silently truncated the shipped `qdcoop.ini` the moment it grew
    // past that: MEASURED (final soak) the shipped file is 21265 bytes, so every
    // switch declared after byte 8191 -- PlayerBody, DialogueView, StreamUnion,
    // MoverSync, PlatformRide, LootSync, SuspendGuard, SuppressMenuPause, CombatVfx,
    // DeathRespawn, QuestItemSync, KillXPSync, PartnerSaveSync -- took its COMPILED
    // DEFAULT and stayed 0.  The boot line said so and nothing else did:
    //   switches: ... playerBody=0 dialogueView=0 stream=0 travel=0 cutscene=0
    //             movers=0 loot=0 suspend=0 pause=0 ...
    // A shipped-on switch that is off is worse than one that is off on purpose: the
    // file is the product's promise, and the player has no way to see the difference.
    //
    // Read to EOF in a loop, and refuse a file that is implausibly large rather than
    // grow without bound -- an ini is a configuration, not a dataset.
    static const DWORD kIniMax = 256 * 1024;
    static char s_buf[kIniMax + 1];
    DWORD got = 0;
    for (;;) {
        if (got >= kIniMax) break;
        DWORD chunk = 0;
        if (!ReadFile(h, s_buf + got, kIniMax - got, &chunk, nullptr)) break;
        if (chunk == 0) break;
        got += chunk;
    }
    CloseHandle(h);
    s_buf[got] = 0;
    return ConfigParseText(s_buf);
}

} // namespace qd
