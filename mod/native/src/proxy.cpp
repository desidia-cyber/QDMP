// proxy.cpp -- the QD co-op native module.
//
// WHAT THIS IS
// ------------
// A proxy DLL named after a system DLL the game imports (winmm.dll -- see build.bat
// for why not version.dll or dxgi.dll), which the Windows loader maps before the
// game's entry point runs.  So: no injector, no remote thread, no UE4SS.  Every one
// of winmm's 180 exports is forwarded to the real System32 DLL by a generated .def,
// so owning the name breaks nothing else in the process.
//
// WHAT IT DOES, IN LAYERS
// -----------------------
//   0  refuse to run on an unrecognised build (byte-verified target table)
//   1  prove the substrate (GObjects / FName / GWorld / GEngine / ProcessEvent)
//   2  PROBE: resolve and log the four write-side APIs that the analysis could not
//      pin, and close the one that is safe to close offline (a no-op write of the
//      value already there, verified by read-back)
//   3  MIRROR: read the progression containers and log every change
//   4  SYNC: host broadcasts absolute flag state; a peer sends intents, the host
//      applies and echoes
//   5  BODY: spawn and drive a full-body actor for the remote player
//   6  WORLD: level-streaming union (veto at SetShouldBeVisible), enemies (host
//      simulates, peer copies driven or locally owned, HP converges through the
//      game's damage path), dialogue (per-slot talk state, partner's NPC blocked)
//
// Layers 0-3 are read-only against the game.  Layers 4-6 write, and each has its own
// switch in qdcoop.ini, defaulting OFF until proven live.  That ordering is
// deliberate: the corpus records that a default-on write path turns a first-run
// crash into an unattributable one.
//
// DESIGN RULES THIS FILE OBEYS, ALL OF THEM PAID FOR
// --------------------------------------------------
//   * Only UObject access on the game thread; decode and socket work elsewhere.
//   * A value is never lowered (monotonic progression); a peer's action is an intent.
//   * "called=yes is not evidence the call ran" -- every write is read back.
//   * Validate every handle; reflection can hand back a valid-looking object of the
//     wrong type and driving it is an access violation __try cannot catch.
//   * Never trust a cached handle across a map change.
//   * Gate on game events, not timers; never destroy a puppet on silence.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdint.h>
#include <math.h>
#include <stdlib.h>

#include "MinHook.h"

#include "targets.h"
#include "qdlog.h"
#include "qdconfig.h"
#include "qdsubstrate.h"
#include "qdreflect.h"
#include "qdsave.h"
#include "qdwrite.h"
#include "qdavatar.h"
#include "qdwire.h"
#include "qdnet.h"

// The five dependency-free state machines.  Header-only on purpose so the same
// logic is compiled into a test executable (native/test/run_tests.bat) with no
// Windows and no game: the parts that took the previous project longest to get
// right are the parts that must be testable off-target.
#include "qdmotion.h"
#include "qdpuppet.h"
#include "qdstream.h"
#include "qdtravel.h"
#include "qddialogue.h"
#include "qdsequence.h"
#include "qdmover.h"
#include "qdloot.h"
#include "qdsuspend.h"
#include "qdenemy.h"
#include "qdenemyown.h"
#include "qdenemydmg.h"
#include "qdplayerbody.h"
#include "qdflags.h"
#include "qdpending.h"
#include "qdinventory.h"
#include "qdinvintent.h"
#include "qdactorstate.h"

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
namespace {

using namespace qd;

struct Hook {
    const char* name;
    uint32_t    rva;
    void*       detour;
    void**      trampoline;
    bool        installed;
};

// Detour prototypes.  Declared with the real signatures: a target with a float
// parameter would need its real prototype (the previous project's proxy used the
// five-pointer form everywhere and noted the float caveat explicitly), and
// SetGamePaused carries a bool, so the five-pointer form would be fine here -- but
// the typed form documents the contract.
// FEngineLoop::Tick returns bool -- "keep running".  The first version of this detour
// returned void, so the value the engine loop tested in rax was whatever happened to be
// left there.  The game exited a few seconds after the hook went live, with no crash
// dump and no further log line: an engine loop that reads a garbage result quits
// quietly, which looks nothing like a crash and is exactly why the signature matters.
typedef bool      (*FnTick)(void* engineLoop);
typedef bool      (*FnSetGamePaused)(void* worldContext, bool paused);
typedef void      (*FnStoreEnemy)(void* self, const QdGuid* guid, float respawn, float hp);
typedef void      (*FnProcessEvent)(void* self, void* func, void* parms);

FnTick            g_oTick = nullptr;
FnSetGamePaused   g_oSetGamePaused = nullptr;
FnStoreEnemy      g_oStoreEnemy = nullptr;
FnProcessEvent    g_oProcessEvent = nullptr;

Net               g_net;
bool              g_engineReady = false;
bool              g_sessionLive = false;
uint32_t          g_lastTickMs = 0;
uint32_t          g_frame = 0;

// The local player and the remote body.
void*             g_localPawn = nullptr;
void*             g_puppetActor = nullptr;
void*             g_puppetClass = nullptr;

// Sub-system state.
motion::Interp    g_peerInterp;
puppet::PuppetFsm g_puppet;
stream::UnionGuard g_stream;
stream::MapBoard   g_map;
// The follow half of G6 (qdtravel.h): what the partner's `dest` means here, and
// whether this machine has already acted on it.  Separate from g_map because the
// divergence debounce and the follow dedup reset for different reasons.
travel::Board      g_travel;
// The hard-transition handshake (qdtravel.h HardBoard).  Separate from `g_travel`: an
// in-world follow and a world switch have different memory (one remembers a
// destination actor, the other a map load and an arrival).
travel::HardBoard  g_hard;
uint32_t           g_travelSeq = 0;
dialogue::DialogueLock g_dialogue;

// Cutscenes, moving world objects, loot and suspend saves (qdsequence / qdmover /
// qdloot / qdsuspend).  Each is behind its own switch.
sequence::CutsceneBoard g_cutscene;
mover::Board            g_movers;
loot::Ledger            g_loot;
suspend::Board          g_suspend;
uint32_t                g_cutsceneSeq = 0;
uint32_t                g_lootRev = 0;
uint32_t                g_moverRev = 0;
uint32_t                g_suspendSeq = 0;
flags::FlagBank   g_flags;

// The peer's outbound half of flag sync: rises the mirror observed, waiting for the
// host to echo them.  On the host it stays empty.
pending::IntentQueue g_pendingFlags;

// The inventory mirror (section 6d).  Declared here rather than beside its banner
// because TryInitSaveLayer must reset both halves when the active save is replaced: a
// snapshot or a pending grant that names an item in the old container is not a fact
// about the new one.
inventory::HostPublisher g_invPub;
inventory::GrantQueue   g_invGrants;
inventory::Snapshot     g_invNow;
uint32_t                g_lastInvObserveMs = 0;
uint32_t                g_peerInvRev = 0;
void*                   g_invSnapshotSave = nullptr;
bool                    g_invHaveSnapshot = false;

// The other direction of the same lane (qdinvintent.h): the peer's own pickups and
// consumptions, as two independent cumulative counters per path, and the host's
// monotone record of how much of each it has already applied.  One table, two axes, so
// a retransmit or a reorder is exactly-once for a grant and for a removal alike.
// Reset with the inventory state above because an in-flight contribution names a path
// in the container being replaced.
invintent::PickupQueue  g_invIntents;
invintent::GrantLedger  g_invGrantLedger;

// A host grant landed in the save; the next host publisher pass must read the new
// container now rather than at the 4 Hz observation tick, so the peer's confirmation
// (and the line a live run is compared against) follows the apply within one frame.
bool                    g_invForceObserve = false;

// The one-shot world-object lane (G2).  Declared beside the inventory state for the
// same reason: TryInitSaveLayer must reset all of it when the active save is replaced,
// because a GUID, a revision or a pending intent from the old container is not a fact
// about the new one.
actorstate::HostPublisher g_actorPub;
actorstate::IntentQueue   g_pendingActors;
actorstate::Snapshot      g_actorNow;
actorstate::LocalDetector g_actorMirror;
uint32_t                  g_lastActorObserveMs = 0;
uint32_t                  g_lastActorLocalObserveMs = 0;
uint32_t                  g_peerActorRev = 0;     // last host revision this peer applied
uint32_t                  g_actorHostRev = 0;     // the revision an intent is based on
void*                     g_actorSnapshotSave = nullptr;
bool                      g_actorHaveSnapshot = false;

// Progression mirror bookkeeping.
uint32_t          g_lastMirrorMs = 0;
int32_t           g_lastFlagCount = -1;
uint32_t          g_flagsRevision = 1;
bool              g_flagsDirty = true;
uint32_t          g_lastFlagsPushMs = 0;
uint32_t          g_lastPlayerPushMs = 0;
// False until the mirror has read the save once.  Until then every key looks new, and a
// peer that treated that as news would push its whole save at the host (see MirrorPump).
bool              g_mirrorBaseline = false;

// The probe runs once per process.  It is guarded separately from the save layer's own
// readiness because the two become ready at different times: the engine exists long
// before a save is loaded, and a fresh launch sits at the main menu with NO save object
// at all.  The first version of this mod ran the probe inline at bootstrap and printed
// "will retry on the next load" -- and nothing ever retried.  That is exactly the
// silent-failure shape the corpus warns about, so the retry now lives in the frame pump.
bool              g_probeDone = false;
uint32_t          g_lastSaveInitMs = 0;
bool              g_substrateValidated = false;
bool              g_substrateRefused = false;
bool              g_substrateReady = false;

// The progression mirror lives here rather than beside its pump because the save-layer
// retry must be able to reset it when the container is replaced.
struct MirrorState {
    char    last_name[80];
    int32_t last_value;
};
MirrorState       g_mirror[64];
int               g_mirrorCount = 0;


// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
void TickNow(uint32_t* out) { *out = Net::Now(); }

void LogConfig()
{
    const Config& c = Cfg();
    QDLOG(LOG_INFO, "config: enabled=", dec(c.enabled), " log=", dec(c.log_level),
          " relay=", c.relay_host, ":", dec(c.relay_port));
    QDLOG(LOG_INFO, "switches: flags=", dec(c.flag_sync),
          " inventory=", dec(c.inventory_sync),
          " actors=", dec(c.actor_state_sync),
          " enemy=", dec(c.enemy_sync),
          " puppet=", dec(c.puppet_sync),
          " playerBody=", dec(c.player_body),
          " dialogue=", dec(c.dialogue_sync),
          " stream=", dec(c.stream_union),
          " travel=", dec(c.travel_sync),
          " cutscene=", dec(c.cutscene_sync),
          " movers=", dec(c.mover_sync),
          " loot=", dec(c.loot_sync),
          " suspend=", dec(c.suspend_guard),
          " pause=", dec(c.suppress_pause));
    // The player body is the one switch whose prerequisites are another switch: without
    // the puppet there is no body to drive, and without the enemy lane there is nowhere
    // for a hit to go.  Say so at load rather than leaving it to be discovered.
    if (c.player_body && !(c.puppet_sync && c.enemy_sync))
        QDLOG(LOG_WARN, "PlayerBody=1 needs PuppetSync=1 and EnemySync=1; ",
              (c.puppet_sync ? "" : "there will be no body to drive and "),
              (c.enemy_sync ? "" : "a hit on it cannot be forwarded"));
}

// ---------------------------------------------------------------------------
// LAYER 2 -- the API probe
// ---------------------------------------------------------------------------
// The analysis listed four write-side APIs it could not pin, and said the right way
// to close them is inside the first build rather than by more static reading.  This
// function is that closure.  It is diagnostic, it changes no game state, and it
// logs a pass/fail per item -- including a deliberate no-op write, which is the only
// way to prove the writer works without risking progression.
void RunApiProbe()
{
    QDLOG(LOG_INFO, "======== API probe ========");
    SaveLayer& s = Save();

    // 1. save object
    if (!s.save) { QDLOG(LOG_ERROR, "PROBE save-object: FAIL (", s.note, ")"); return; }
    {
        char d[160];
        ObjDescribe(s.save, d, sizeof(d));
        QDLOG(LOG_INFO, "PROBE save-object: PASS (", d, ")");
    }

    // 2. property layout, validated against the engine's own UFunction bookkeeping
    if (!Lay().valid) QDLOG(LOG_ERROR, "PROBE param-layout: FAIL (", Lay().note, ")");
    else QDLOG(LOG_INFO, "PROBE param-layout: PASS (", Lay().note, ")");

    // 3. NamedSaveData container offset + map stride
    if (s.nsdOffset < 0) QDLOG(LOG_ERROR, "PROBE NamedSaveData-offset: FAIL (property not found)");
    else QDLOG(LOG_INFO, "PROBE NamedSaveData-offset: PASS (", dec(s.nsdOffset), ")");
    if (s.enumerateOk) {
        QDLOG(LOG_INFO, "PROBE NamedSaveData-map: PASS  stride=", dec(s.mapStride),
              " valueOffset=", dec(s.mapValueOff), " (cross-checked against the getter)");
    } else {
        QDLOG(LOG_WARN, "PROBE NamedSaveData-map: FAIL  stride not confirmed; "
                        "enumeration off, single-key reads unaffected");
    }

    // 4. the reader, end to end -- and, independently, the reflective getter.  Two
    //    unrelated routes to the same value agreeing is the evidence that both the
    //    native signature and the reflected one are right.
    {
        const char* key = "CheckpointUnlockCount";
        bool set = false;
        int32_t v = 0;
        bool have = NsdRead(key, &v, &set);
        if (have) QDLOG(LOG_INFO, "PROBE reader: PASS  ", key, "=", dec(v));
        else QDLOG(LOG_WARN, "PROBE reader: INCONCLUSIVE  ", key, " absent on this save");

        if (s.getSig.valid) {
            QdName n;
            const PropDesc* pKey = FindPropOfType(s.getSig, "NameProperty", CPF_OutParm | CPF_ReturnParm);
            const PropDesc* pCtx = FindPropOfType(s.getSig, "ObjectProperty", CPF_OutParm | CPF_ReturnParm);
            if (pKey && FNameFromAnsi(key, &n)) {
                ArgList a;
                a.AddFName(pKey->name, n);
                if (pCtx) a.AddObject(pCtx->name, GWorldPtr());
                ReadBack rb;
                char err[160];
                if (Invoke(s.saveFunctions, s.fnGetNamedSaveData, s.getSig, a, &rb, err, sizeof(err))) {
                    bool agree = (rb.value == v) && (!have || rb.was_set);
                    QDLOG(agree ? LOG_INFO : LOG_ERROR, "PROBE reflective-getter: ",
                          (agree ? "PASS" : "FAIL"), "  value=", dec(rb.value),
                          " wasSet=", dec(rb.was_set ? 1 : 0), " native=", dec(v));
                } else {
                    QDLOG(LOG_ERROR, "PROBE reflective-getter: FAIL  ", err);
                }
            }
        }
    }

    // 5. the writer, proved by a no-op write and a read-back.
    if (!s.writeOk) {
        QDLOG(LOG_ERROR, "PROBE writer: FAIL (write path unavailable)");
    } else {
        bool set = false;
        int32_t before = 0;
        if (!NsdRead("CheckpointUnlockCount", &before, &set)) {
            QDLOG(LOG_WARN, "PROBE writer: SKIP (no key to no-op against)");
        } else {
            char err[160];
            if (NsdWrite("CheckpointUnlockCount", before, err, sizeof(err))) {
                QDLOG(LOG_INFO, "PROBE writer: PASS  no-op write of ", dec(before),
                      " accepted and read back equal");
            } else {
                QDLOG(LOG_ERROR, "PROBE writer: FAIL  ", err);
            }
        }
    }

    QDLOG(LOG_INFO, "======== probe complete ========");
}

// ---------------------------------------------------------------------------
// Save-layer readiness, checked every frame
// ---------------------------------------------------------------------------
// Two pointer reads per frame (GWorld -> GameInstance -> active save).  The full init
// -- reflection, offsets, map probe -- runs only when the active save object changes,
// which is exactly a load, a new game or a return to the menu.  Nothing here can create
// a save (see ActiveSaveNoCreate in qdsave.h for why that matters).
bool TryInitSaveLayer(uint32_t now)
{
    (void)now;
    SaveLayer& s = Save();
    void* world = GWorldPtr();
    void* save = ActiveSaveNoCreate(world);

    if (save == s.save && world == s.world) return s.readOk;

    if (!save) {
        if (s.readOk) QDLOG(LOG_INFO, "active save gone (menu or level transition)");
        s.Reset();
        s.world = world;
        // There is no container to read or grant into.  Dropping the queued grants here
        // avoids a retry storm while the player is in a menu; the next save's first body
        // rebuilds the list from the host's truth.
        g_invGrants.Reset();
        g_invHaveSnapshot = false;
        g_invSnapshotSave = nullptr;
        // Both directions name the replaced container.  The peer's baseline is rebuilt
        // from the next host body and the host's ledger from the next peer intent.
        g_invIntents.Reset();
        g_invGrantLedger.Reset();
        g_invForceObserve = false;
        // The actor-state lane names GUIDs in the same vanished container.  Keeping a
        // pending intent would make the peer retry a write into a save it is not in.
        g_actorPub.Reset();
        g_pendingActors.Reset();
        g_actorMirror.Reset();
        g_actorNow.Clear();
        g_actorHaveSnapshot = false;
        g_actorSnapshotSave = nullptr;
        g_peerActorRev = 0;
        g_actorHostRev = 0;
        return false;
    }
    if (save == s.save) {           // same save, new world: a level change
        s.world = world;
        return s.readOk;
    }

    if (!SaveInit(world, save)) {
        QDLOG(LOG_DEBUG, "save layer not ready: ", s.note);
        return false;
    }
    if (!g_probeDone) {
        RunApiProbe();
        g_probeDone = true;
        // §5 step 1 wants the write-path oracle at LOAD, not at the first write.
        // Measured in Session 5: the session produced no `NAMED-SAVE ORACLE` line at all,
        // because the oracle only ran when the first flag write asked for it and nothing
        // wrote.  Resolving it here makes "this build's write path is proven" a load-time
        // fact instead of something that depends on whether the player earned anything.
        if (Cfg().flag_sync || Cfg().inventory_sync || Cfg().actor_state_sync) (void)WrReady();
    } else {
        QDLOG(LOG_INFO, "save layer re-initialised (new active save)");
    }
    g_mirrorCount = 0;
    g_lastFlagCount = -1;
    g_flagsDirty = true;
    // A new save has to be re-baselined: its whole container is "new" to the mirror, and
    // that is a load, not the player earning anything.
    g_mirrorBaseline = false;
    // The queue refers to values in the save that was just replaced (and to echoes the
    // old container produced), so carrying it over would send progress for a save the
    // player is no longer in.
    g_pendingFlags.Reset();
    // Same rule for inventory: the old snapshot and any un-granted item name the old
    // container.  The next 4 Hz observation reads the new one and republishes.
    g_invPub.Reset();
    g_invGrants.Reset();
    g_invHaveSnapshot = false;
    g_invSnapshotSave = nullptr;
    g_peerInvRev = 0;
    g_invIntents.Reset();
    g_invGrantLedger.Reset();
    g_invForceObserve = false;
    // Same rule for the actor-state lane: a GUID is authored per level, and a pending
    // intent that names a GUID from the previous save must not be replayed into this one.
    g_actorPub.Reset();
    g_pendingActors.Reset();
    g_actorMirror.Reset();
    g_actorNow.Clear();
    g_actorHaveSnapshot = false;
    g_actorSnapshotSave = nullptr;
    g_peerActorRev = 0;
    g_actorHostRev = 0;
    return s.readOk;
}

// ---------------------------------------------------------------------------
// LAYER 3 -- the read-only mirror
// ---------------------------------------------------------------------------
// Every container change is logged with its old and new value.  This is the
// instrument that turns "the saves look equal" into "these 11 keys changed in this
// order", and it is what a two-instance run is compared against.
// (MirrorState / g_mirror are declared with the other globals, above.)

// Peer only: a rise the local game produced is a progression event the host does not
// have.  It is queued rather than sent here because the same frame can produce several
// of them and because an intent may have to be retried; the pump in LAYER 4 sends.
// "Rise" includes a key appearing: NamedSaveData has no entry for an unearned key, so
// its first appearance is the 0 -> value step.  An appearance at 0 is not a rise -- 0
// is what the bank already reports for an absent key -- and would carry no information.
void QueueFlagIntent(const char* key, int32_t value)
{
    if (!Cfg().flag_sync || !g_net.IsPeer() || !g_net.Connected()) return;
    // The baseline pass is not a change the player made.  Without this gate the peer
    // sent an intent for every non-zero key in its save the moment it joined.
    if (!g_mirrorBaseline) return;

    switch (g_pendingFlags.Observe(key, value)) {
    case pending::IntentQueue::kQueued:
        QDLOG(LOG_INFO, "flag intent queued ", key, "=", dec(value));
        break;
    case pending::IntentQueue::kCoalesced:
        QDLOG(LOG_INFO, "flag intent coalesced ", key, "=", dec(value));
        break;
    case pending::IntentQueue::kEcho:
        // The host put this value here; sending it back is the ping-pong.
        QDLOG(LOG_DEBUG, "flag ", key, "=", dec(value), " came from the host, not an intent");
        break;
    case pending::IntentQueue::kDuplicate:
        break;
    case pending::IntentQueue::kOverflow:
        QDLOG(LOG_WARN, "flag intent dropped ", key, "=", dec(value),
              ": pending list full (", dec(pending::IntentQueue::kMaxPending),
              "), host has not answered");
        break;
    }
}

void MirrorVisit(const char* key, int32_t value, void* user)
{
    (void)user;
    for (int i = 0; i < g_mirrorCount; ++i) {
        if (NameEquals(g_mirror[i].last_name, key)) {
            if (g_mirror[i].last_value != value) {
                QDLOG(LOG_INFO, "flag CHANGE ", key, " : ",
                      dec(g_mirror[i].last_value), " -> ", dec(value));
                // A rise only.  The peer's own game never lowers a progression flag, and
                // an intent for a lower value would be refused by the host's monotone
                // rule anyway; acting on one would only add a datagram to the refusal.
                if (value > g_mirror[i].last_value) QueueFlagIntent(key, value);
                g_mirror[i].last_value = value;
                g_flagsDirty = true;
            }
            return;
        }
    }
    if (g_mirrorCount < (int)(sizeof(g_mirror) / sizeof(g_mirror[0]))) {
        MirrorState& m = g_mirror[g_mirrorCount++];
        int i = 0;
        for (; key[i] && i < (int)sizeof(m.last_name) - 1; ++i) m.last_name[i] = key[i];
        m.last_name[i] = 0;
        m.last_value = value;
        QDLOG(LOG_INFO, "flag NEW ", key, " = ", dec(value));
        if (value > 0) QueueFlagIntent(key, value);
        g_flagsDirty = true;
    }
}

// Keys the analysis measured in a real save. Without map enumeration these are what the
// mirror can observe: a fixed probe list, read through the native getter, diffed every
// second. It is a downgrade from enumerating the container, and it is enough to prove
// that reads work and to watch a flag change live.
static const char* kSeedKeys[] = {
    "CheckpointUnlockCount", "TotalAchievementWeapons", "TotalAchievementTreasures",
    "CompletedChallenges", "IntroMusicState", "FullEnergyCellsCount",
    "Arjaot_State", "RustySwordKnightState", "RustySwordKnightGaveSword",
    "BrokeBarricade", "TiniScytheHammerAP", "DragonLordDead", "EndingSeen",
};
static const int kSeedCount = (int)(sizeof(kSeedKeys) / sizeof(kSeedKeys[0]));

void MirrorObserve(const char* key, int32_t value)
{
    for (int i = 0; i < g_mirrorCount; ++i) {
        if (NameEquals(g_mirror[i].last_name, key)) {
            if (g_mirror[i].last_value != value) {
                QDLOG(LOG_INFO, "flag CHANGE ", key, " : ",
                      dec(g_mirror[i].last_value), " -> ", dec(value));
                // A rise only.  The peer's own game never lowers a progression flag, and
                // an intent for a lower value would be refused by the host's monotone
                // rule anyway; acting on one would only add a datagram to the refusal.
                if (value > g_mirror[i].last_value) QueueFlagIntent(key, value);
                g_mirror[i].last_value = value;
                g_flagsDirty = true;
            }
            return;
        }
    }
    if (g_mirrorCount < (int)(sizeof(g_mirror) / sizeof(g_mirror[0]))) {
        MirrorState& m = g_mirror[g_mirrorCount++];
        int i = 0;
        for (; key[i] && i < (int)sizeof(m.last_name) - 1; ++i) m.last_name[i] = key[i];
        m.last_name[i] = 0;
        m.last_value = value;
        QDLOG(LOG_INFO, "flag ", key, " = ", dec(value));
        if (value > 0) QueueFlagIntent(key, value);
        g_flagsDirty = true;
    }
}

void MirrorPump(uint32_t now)
{
    if (now - g_lastMirrorMs < 1000) return;
    g_lastMirrorMs = now;

    int n = NsdEnumerate(&MirrorVisit, nullptr);
    if (n >= 0) {
        if (n != g_lastFlagCount) {
            QDLOG(LOG_INFO, "flag container size ", dec(n));
            g_lastFlagCount = n;
        }
        // The first pass is the BASELINE, not news.  Every key is "new" to an empty
        // mirror, and without this the peer uploaded its whole save as intents the
        // moment it joined -- measured on the rig as a run of
        // "intent refused (regressive or stale)" for keys the host already held, and,
        // when the peer's save was AHEAD on any key, a silent overwrite of the host's
        // world by the joining player.  From here on, a rise is the peer's own doing.
        g_mirrorBaseline = true;
        return;
    }

    // Enumeration unavailable: probe the known keys instead.
    if (!Save().readOk || !Save().world) return;
    int found = 0;
    for (int i = 0; i < kSeedCount; ++i) {
        int32_t v = 0;
        bool set = false;
        if (!NsdRead(kSeedKeys[i], &v, &set)) continue;
        ++found;
        MirrorObserve(kSeedKeys[i], v);
    }
    static int lastFound = -1;
    if (found != lastFound) {
        QDLOG(LOG_INFO, "flag probe: ", dec(found), "/", dec(kSeedCount), " known keys present");
        lastFound = found;
    }
    // The same rule on the probe path: the first sweep establishes what the save already
    // holds, and only a later change is the player's doing.
    g_mirrorBaseline = true;
}

// ---------------------------------------------------------------------------
// LAYER 4 -- flag sync
// ---------------------------------------------------------------------------
// Host side: push the absolute set, never a delta.  The container is 11-58 entries of
// a few dozen bytes, so "send the full truth" costs about a kilobyte and is the only
// thing that repairs a peer which missed a baseline (the corpus rule: a repair path
// that only carries deltas cannot repair a peer that missed the baseline).
struct PushEntry { const char* name; int32_t value; };
PushEntry g_push[128];
int       g_pushCount = 0;

void CollectVisit(const char* key, int32_t value, void* user)
{
    (void)user;
    if (g_pushCount >= (int)(sizeof(g_push) / sizeof(g_push[0]))) return;
    // The name pointer must outlive the send.  MirrorState's storage is stable for
    // the life of the process, and MirrorVisit already copied every key into it, so
    // point at that copy rather than at a stack frame.
    for (int i = 0; i < g_mirrorCount; ++i) {
        if (NameEquals(g_mirror[i].last_name, key)) {
            g_push[g_pushCount].name = g_mirror[i].last_name;
            g_push[g_pushCount].value = value;
            ++g_pushCount;
            return;
        }
    }
}

void HostPushFlags(uint32_t now)
{
    if (!Cfg().flag_sync) return;
    bool due = g_flagsDirty || (now - g_lastFlagsPushMs > 1000);
    if (!due) return;
    if (!Save().enumerateOk) return;      // nothing trustworthy to send

    g_pushCount = 0;
    int n = NsdEnumerate(&CollectVisit, nullptr);
    if (n <= 0) return;

    wire::FlagEntry entries[128];
    for (int i = 0; i < g_pushCount; ++i) {
        entries[i].name = g_push[i].name;
        entries[i].value = g_push[i].value;
    }

    // Split across ticks rather than fragment: fill one datagram, remember the
    // cursor, continue next tick.
    static int cursor = 0;
    if (cursor >= g_pushCount) cursor = 0;

    uint8_t body[wire::kMaxBody];
    wire::Writer w(body, sizeof(body));
    // n is the container size, not the number of entries in this datagram: the
    // receiver learns that from what parses before the end of the body.  One count,
    // always true, rather than a second one that has to be patched after the fact.
    wire::WriteFlagsHeader(w, g_flagsRevision, (uint16_t)g_pushCount);
    int next = wire::WriteFlagsChunk(w, entries, g_pushCount, cursor, g_flagsRevision);
    if (next == cursor) {                 // nothing fit; do not spin
        cursor = 0;
        return;
    }

    if (g_net.SendUnreliable(wire::K_WORLD_FLAGS, body, w.len)) {
        g_lastFlagsPushMs = now;
        cursor = next;
        if (cursor >= g_pushCount) {
            cursor = 0;
            g_flagsDirty = false;
            ++g_flagsRevision;
        }
    }
}

// Peer side: apply what the host sent.  Never lower, never invent.
void PeerApplyFlags(wire::Reader& r)
{
    wire::FlagsHeader h;
    if (!wire::ReadFlagsHeader(r, &h)) return;
    int applied = 0;
    int refused = 0;
    // Read until the body ends.  `h.n` is the container size on the sender, which may
    // be larger than this datagram's share because a large container is split across
    // ticks.
    for (int i = 0; i < h.n && r.ok && r.pos < r.len; ++i) {
        char name[96];
        int32_t value = 0;
        if (!wire::ReadFlagEntry(r, name, sizeof(name), &value)) break;
        if (!Cfg().flag_sync) continue;

        // The host's body is also its answer to an intent: equal settles it, higher means
        // the authority is already ahead and the value we asked for is implied.  This is
        // tested before ApplyRemote because it is about what the HOST holds, not about
        // whether this peer needed to change its own bank.
        int32_t asked = 0;
        pending::IntentQueue::Resolve res = g_pendingFlags.Applied(name, value, &asked);

        bool ok = false;
        if (h.rev != 0) {
            // Host authority: ApplyRemote refuses to lower a value.
            ok = g_flags.ApplyRemote(name, value, h.rev);
        }
        if (ok) {
            ++applied;
            if (Save().writeOk) {
                char err[128];
                if (NsdWrite(name, value, err, sizeof(err))) {
                    // This value is now in the local container, so the mirror will see it
                    // within the second as a local change.  Remember the echo; otherwise
                    // that observation becomes an intent and the machines ping-pong.
                    g_pendingFlags.RememberEcho(name, value);
                } else {
                    QDLOG(LOG_WARN, "apply failed for ", name, ": ", err);
                }
            }
        } else {
            ++refused;
        }

        if (res == pending::IntentQueue::kConfirmed) {
            QDLOG(LOG_INFO, "flag intent confirmed ", name, "=", dec(value),
                  " rev=", dec(h.rev));
        } else if (res == pending::IntentQueue::kSuperseded) {
            QDLOG(LOG_INFO, "flag intent superseded ", name, "=", dec(asked),
                  " by the host's ", dec(value), " rev=", dec(h.rev));
        }
    }
    if (applied || refused) {
        QDLOG(applied ? LOG_INFO : LOG_DEBUG, "flags applied=", dec(applied), " refused=", dec(refused),
              " rev=", dec(h.rev));
    }
}

// Peer side: put the rises the mirror observed on the wire as absolute flag intents.
// The queue decides how many may go now; the revision is read here, at send time, so a
// retry always carries the freshest host state this peer is based on.
void PeerFlagIntentPump(uint32_t now)
{
    if (!Cfg().flag_sync || !g_net.IsPeer() || !g_net.Connected()) return;

    pending::IntentQueue::Out out[pending::IntentQueue::kMaxPerTick];
    int n = g_pendingFlags.Due(now, out, pending::IntentQueue::kMaxPerTick);
    for (int i = 0; i < n; ++i) {
        uint32_t rev = g_flags.Revision();

        // kind 1, then str(name) i32(value) u8(mode), which is exactly ReadIntent's
        // I_SET_FLAG layout.  mode 1 is absolute: the mirror observes totals, never
        // deltas, and an additive intent would apply the host's arithmetic twice.
        uint8_t body[256];
        wire::Writer w(body, sizeof(body));
        wire::WriteIntentHeader(w, wire::I_SET_FLAG, rev);
        w.Str(out[i].name);
        w.I32(out[i].value);
        w.U8(1);

        bool sent = w.ok && g_net.SendReliable(wire::K_INTENT, body, w.len);
        if (sent) {
            QDLOG(LOG_INFO, "flag intent sent ", out[i].name, "=", dec(out[i].value),
                  " rev=", dec(rev), " try ", dec(out[i].attempt), "/",
                  dec(pending::IntentQueue::kMaxAttempts));
        } else {
            QDLOG(LOG_WARN, "flag intent send failed ", out[i].name, "=",
                  dec(out[i].value), " rev=", dec(rev));
        }

        // The attempt is spent whether or not the socket took it: the give-up has to be
        // bounded, and a lane that keeps refusing the datagram is exactly what the cap is
        // for.  The host's echo is the only thing that settles an intent other than this.
        if (out[i].attempt >= pending::IntentQueue::kMaxAttempts) {
            g_pendingFlags.GiveUp(out[i].name);
            QDLOG(LOG_WARN, "flag intent given up ", out[i].name, "=", dec(out[i].value),
                  " after ", dec(out[i].attempt), " attempts, host never echoed it");
        }
    }
}

void ApplyEnemyHitIntent(const wire::IntentBody& b);      // LAYER 6
void ApplyEnemyRecordIntent(const wire::IntentBody& b);   // LAYER 6 (host only)

// Peer side: an intent is a request, the host decides.
void HostApplyIntent(wire::Reader& r)
{
    wire::IntentBody b;
    if (!wire::ReadIntent(r, &b)) return;

    if (b.kind == wire::I_SET_FLAG) {
        if (!Cfg().flag_sync) return;
        // g_flagsRevision is bumped when a body has been FULLY sent, so once anything has
        // gone out it already names the NEXT revision rather than the one on the wire.  A
        // peer can only ever have applied the body's revision, so comparing against the
        // counter refuses every intent by construction; the gate means "the revision I
        // last published", which is the value below.
        uint32_t lastSent = g_flagsRevision - 1;
        bool ok = g_flags.ApplyIntent(b.name, b.value, b.mode != 0,
                                      b.based_on_rev, lastSent);
        if (!ok) {
            QDLOG(LOG_INFO, "intent refused (regressive or stale): ", b.name,
                  "=", dec(b.value), " based_on=", dec(b.based_on_rev),
                  " last_sent=", dec(lastSent));
            return;
        }
        char err[128];
        if (NsdWrite(b.name, b.value, err, sizeof(err))) {
            QDLOG(LOG_INFO, "intent applied: ", b.name, "=", dec(b.value));
            g_flagsDirty = true;          // echo to everybody, including the asker
        } else {
            QDLOG(LOG_WARN, "intent write failed: ", b.name, " ", err);
        }
    } else if (b.kind == wire::I_ACTOR_STATE) {
        if (!Cfg().actor_state_sync) return;
        // The host is the authority for the container, so a peer's change is applied to
        // the host's own SavableActorStates and then echoed by the next publish.  The
        // gate is the flag lane's: `based_on_rev` must not predate the revision the host
        // last published, or the peer would be acting on a view it has already been
        // told is old.  A refusal is not a loss -- the peer's bounded queue retries with
        // the freshest revision it has.
        uint32_t lastSent = g_actorPub.Revision() - 1;
        if (b.based_on_rev < lastSent) {
            QDLOG(LOG_INFO, "actor-state intent refused (stale): based_on=",
                  dec(b.based_on_rev), " last_sent=", dec(lastSent));
            return;
        }
        QdGuid g = { b.guid[0], b.guid[1], b.guid[2], b.guid[3] };
        void* actor = nullptr;
        int index = -1;
        FindActorStateOwner(g, &actor, &index);
        char err[192];
        bool changed = false;
        if (WrSetActorState(g, b.value, actor, index, err, sizeof(err), &changed)) {
            QDLOG(LOG_INFO, "actor-state intent applied: guid=", hex32(g.a), "-",
                  hex32(g.b), "-", hex32(g.c), "-", hex32(g.d), " value=", dec(b.value));
            // The peer opened a door/chest here too.  The container is already written;
            // run the actor's own load-path refresh once so the authority's world shows
            // what the partner did.
            if (changed && actor && index >= 0) {
                char rerr[192];
                if (!WrRefreshActorState(actor, index, b.value, rerr, sizeof(rerr))) {
                    QDLOG(LOG_WARN, "actor-state refresh failed (intent) guid=",
                          hex32(g.a), "-", hex32(g.b), "-", hex32(g.c), "-", hex32(g.d),
                          ": ", rerr);
                }
            }
            // ObserveHostActorStates runs at 4 Hz and its Observe() sees this write, which
            // marks the publisher dirty and sends the echo.  Doing it here as well would
            // race the enumerator for no benefit.
        } else {
            QDLOG(LOG_WARN, "actor-state intent failed: ", err);
        }
    } else if (b.kind == wire::I_GRANT_ITEM) {
        if (!Cfg().inventory_sync) return;
        // Inventory has its OWN switch (section 6d): it creates items rather than
        // raising a flag, so riding FlagSync would let a client that never enabled the
        // mirror still mint items on the authority.
        //
        // The payload is the peer's CUMULATIVE own count for the path, not an increment
        // (qdinvintent.h explains why).  WrGrantItem only adds, so applying `b.value`
        // directly on every copy would turn one pickup into two whenever the reliable
        // lane retried or the peer retried after a lost result body.  The ledger grants
        // only the increase over what this path has already had added, which is what
        // makes the additive field exactly-once.
        //
        // `based_on_rev` is carried for diagnostics but is NOT the gate here.  For an
        // ABSOLUTE flag a stale view is dangerous and the host must refuse it; for a
        // CUMULATIVE grant a replay is simply the same peer count and the ledger makes
        // it a no-op.  Gating on the host's last published revision would refuse the
        // very retry that recovers a lost result body -- the class of bug SYNC-GAPS G1
        // records, where comparing against the next revision refused every intent
        // forever.  The ledger's monotone `applied` value is the structural guard.
        int32_t need = 0;
        invintent::GrantLedger::Result lr = g_invGrantLedger.Need(b.name, b.value, &need);
        if (lr == invintent::GrantLedger::kAlreadyApplied) {
            QDLOG(LOG_INFO, "inventory grant duplicate: ", b.name, " peer total ",
                  dec(b.value), " already applied");
        } else if (lr == invintent::GrantLedger::kBelowApplied) {
            // The peer's own count fell after it sent (a consumption), so what it now
            // asks for is behind what it already had granted.  Never lower: the host's
            // stock is authoritative and there is no removal path.
            QDLOG(LOG_INFO, "inventory grant superseded: ", b.name, " peer total ",
                  dec(b.value), " is below applied ", dec(g_invGrantLedger.Applied(b.name)));
        } else if (lr == invintent::GrantLedger::kLedgerFull) {
            QDLOG(LOG_WARN, "inventory grant refused: ", b.name, " x", dec(b.value),
                  " (ledger full; untracked grants are not applied)");
        } else {
            char err[160];
            if (WrGrantItem(b.name, need, err, sizeof(err))) {
                g_invGrantLedger.Commit(b.name, b.value);
                // Publish the result in this same tick: the peer's confirmation is the
                // next body, and the line below is the one a live run is matched against
                // ("inventory grant applied:" on the host, "item intent confirmed" on
                // the peer).
                g_invForceObserve = true;
                QDLOG(LOG_INFO, "inventory grant applied: ", b.name, " x", dec(need),
                      " (peer total ", dec(b.value), " rev=", dec(b.based_on_rev), ")");
            } else {
                // Not committed: a refused write must be retried, and committing now
                // would make the retry look like a duplicate and lose the item.
                QDLOG(LOG_WARN, "inventory grant failed: ", b.name, " x", dec(need),
                      ": ", err);
            }
        }
    } else if (b.kind == wire::I_CONSUME_ITEM) {
        if (!Cfg().inventory_sync) return;
        // The peer used an item.  The payload is its CUMULATIVE use for the path, and
        // the ledger removes only the increase, so a retransmit or a reorder is
        // exactly-once.  This is the half that used to be missing: without it the next
        // host body granted the consumed item back and counts drifted upward.
        int32_t need = 0;
        invintent::GrantLedger::ConsumeResult lr =
            g_invGrantLedger.NeedConsume(b.name, b.value, &need);
        if (lr == invintent::GrantLedger::kConsumeAlreadyApplied) {
            QDLOG(LOG_INFO, "inventory consume duplicate: ", b.name, " peer total ",
                  dec(b.value), " already applied");
        } else if (lr == invintent::GrantLedger::kConsumeBelowApplied) {
            // A stale or declined use: the peer consumed less than it already told us
            // about.  Never remove twice.
            QDLOG(LOG_INFO, "inventory consume superseded: ", b.name, " peer total ",
                  dec(b.value), " is below applied ",
                  dec(g_invGrantLedger.AppliedConsume(b.name)));
        } else if (lr == invintent::GrantLedger::kConsumeLedgerFull) {
            QDLOG(LOG_WARN, "inventory consume refused: ", b.name, " x", dec(b.value),
                  " (ledger full; untracked removals are not applied)");
        } else {
            int32_t removed = 0;
            char err[160];
            if (WrConsumeItem(b.name, need, &removed, err, sizeof(err))) {
                // Commit the whole cumulative value even when the host held less than
                // the peer asked for: there is nothing left to remove, and keeping the
                // intent alive would only retry a settled use.
                g_invGrantLedger.CommitConsume(b.name, b.value);
                g_invForceObserve = true;
                QDLOG(LOG_INFO, "inventory consume applied: ", b.name, " -", dec(removed),
                      " (peer total ", dec(b.value), " rev=", dec(b.based_on_rev), ")");
                if (removed < need) {
                    QDLOG(LOG_WARN, "inventory consume partial: ", b.name, " asked -",
                          dec(need), " but the host held ", dec(removed),
                          "; the rest is settled, never invented");
                }
            } else {
                QDLOG(LOG_WARN, "inventory consume failed: ", b.name, " x", dec(need),
                      ": ", err);
            }
        }
    } else if (b.kind == wire::I_ENEMY_HIT) {
        // The owner applies a hit the non-owner's player landed on its frozen copy.
        // Either machine can own an enemy, so this path serves both roles.
        ApplyEnemyHitIntent(b);
    } else if (b.kind == wire::I_ENEMY_RECORD) {
        // Persistence is host-authoritative whichever machine ran the enemy.
        ApplyEnemyRecordIntent(b);
    } else if (b.kind == wire::I_DIALOGUE_ANS) {
        // The wire carries {npc, line, option}; `entry` is the cursor within the
        // sequence and is known locally on both machines, so it is not sent.  The
        // answer is recorded against the line only, and the first answer wins.
        if (g_dialogue.RecordAnswer(b.line, 0, b.option)) {
            QDLOG(LOG_INFO, "dialogue answer recorded line=", dec(b.line),
                  " option=", dec(b.option));
        }
    }
}

// ---------------------------------------------------------------------------
// Actor reflection -- transforms read and written through the engine's own surface
// ---------------------------------------------------------------------------
// Offsets come from the reflected property list (RootComponent on Actor,
// RelativeLocation/RelativeRotation/ComponentVelocity on SceneComponent), never from a
// typed constant.  Movement goes through AActor::K2_SetActorLocationAndRotation, so the
// engine updates the render transform, overlaps and the movement component itself.
struct ActorApi {
    bool  tried;
    bool  ok;
    int   rootComponent;     // AActor::RootComponent
    int   relLocation;       // USceneComponent::RelativeLocation
    int   relRotation;       // USceneComponent::RelativeRotation
    int   velocity;          // USceneComponent::ComponentVelocity
    void* fnSetLocRot;       // AActor::K2_SetActorLocationAndRotation
    void* fnSetHidden;       // AActor::SetActorHiddenInGame
    void* fnDestroy;         // AActor::K2_DestroyActor
    LayoutResult sigSetLocRot;
    LayoutResult sigSetHidden;
    LayoutResult sigDestroy;
};
ActorApi g_actor = {};

void* FindNativeFn(void* cls, const char* name)
{
    return cls ? FindFunctionInClass(cls, name) : nullptr;
}

// Resolved once per process: Actor/SceneComponent are native classes that never unload.
bool EnsureActorApi()
{
    if (g_actor.tried) return g_actor.ok;
    g_actor.tried = true;

    void* actorCls = FindObjectByNameAndClass("Actor", "Class");
    void* sceneCls = FindObjectByNameAndClass("SceneComponent", "Class");
    if (!actorCls || !sceneCls) {
        QDLOG(LOG_ERROR, "actor api: Actor/SceneComponent class not found");
        return false;
    }
    g_actor.fnSetLocRot = FindNativeFn(actorCls, "K2_SetActorLocationAndRotation");
    g_actor.fnSetHidden = FindNativeFn(actorCls, "SetActorHiddenInGame");
    g_actor.fnDestroy   = FindNativeFn(actorCls, "K2_DestroyActor");
    void* fnGetLoc      = FindNativeFn(actorCls, "K2_GetActorLocation");

    // The FProperty layout may not be validated yet if no save has loaded; these native
    // functions are as good an oracle as SaveFunctions_C's.
    if (!Lay().valid) {
        void* fns[4] = {g_actor.fnSetLocRot, g_actor.fnSetHidden, fnGetLoc,
                        FindNativeFn(actorCls, "K2_SetActorLocation")};
        if (!DiscoverLayout(fns, 4)) {
            QDLOG(LOG_ERROR, "actor api: property layout not validated (", Lay().note, ")");
            return false;
        }
        QDLOG(LOG_INFO, "property layout: PASS on ", dec(Lay().validated_on), " Actor functions");
    }

    g_actor.rootComponent = FindPropertyOffsetIn(actorCls, "RootComponent");
    g_actor.relLocation   = FindPropertyOffsetIn(sceneCls, "RelativeLocation");
    g_actor.relRotation   = FindPropertyOffsetIn(sceneCls, "RelativeRotation");
    g_actor.velocity      = FindPropertyOffsetIn(sceneCls, "ComponentVelocity");
    g_actor.sigSetLocRot  = Signature(g_actor.fnSetLocRot);
    g_actor.sigSetHidden  = Signature(g_actor.fnSetHidden);
    g_actor.sigDestroy    = Signature(g_actor.fnDestroy);

    QDLOG(LOG_INFO, "actor api: RootComponent=", dec(g_actor.rootComponent),
          " RelativeLocation=", dec(g_actor.relLocation),
          " RelativeRotation=", dec(g_actor.relRotation),
          " ComponentVelocity=", dec(g_actor.velocity),
          " SetLocRot=", dec(g_actor.sigSetLocRot.valid ? 1 : 0),
          " SetHidden=", dec(g_actor.sigSetHidden.valid ? 1 : 0),
          " Destroy=", dec(g_actor.sigDestroy.valid ? 1 : 0));
    if (g_actor.sigSetLocRot.valid) LogSignature("K2_SetActorLocationAndRotation", g_actor.sigSetLocRot);

    g_actor.ok = g_actor.rootComponent >= 0 && g_actor.relLocation >= 0 &&
                 g_actor.relRotation >= 0;
    return g_actor.ok;
}

struct ActorXform {
    float px, py, pz;
    float pitch, yaw, roll;
    float vx, vy, vz;
};

bool ReadActorXformGuarded(void* actor, ActorXform* x)
{
    __try {
        void* root = *(void**)((uint8_t*)actor + g_actor.rootComponent);
        if (!LooksLikeUObject(root)) return false;
        const float* l = (const float*)((uint8_t*)root + g_actor.relLocation);
        const float* r = (const float*)((uint8_t*)root + g_actor.relRotation);
        x->px = l[0]; x->py = l[1]; x->pz = l[2];
        x->pitch = r[0]; x->yaw = r[1]; x->roll = r[2];
        x->vx = x->vy = x->vz = 0.0f;
        if (g_actor.velocity >= 0) {
            const float* v = (const float*)((uint8_t*)root + g_actor.velocity);
            x->vx = v[0]; x->vy = v[1]; x->vz = v[2];
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ReadActorXform(void* actor, ActorXform* x)
{
    if (!actor || !EnsureActorApi() || !ObjectIsLive(actor)) return false;
    return ReadActorXformGuarded(actor, x);
}

bool SetActorXform(void* actor, float px, float py, float pz, float yaw)
{
    if (!actor || !g_actor.sigSetLocRot.valid || !ObjectIsLive(actor)) return false;
    ArgList a;
    a.AddVector("NewLocation", px, py, pz);
    a.AddRotator("NewRotation", 0.0f, yaw, 0.0f);
    a.AddBool("bSweep", false);
    a.AddBool("bTeleport", true);
    char err[128];
    if (!Invoke(actor, g_actor.fnSetLocRot, g_actor.sigSetLocRot, a, nullptr, err, sizeof(err))) {
        static uint32_t lastLog = 0;
        uint32_t now = Net::Now();
        if (now - lastLog > 5000) { lastLog = now; QDLOG(LOG_WARN, "puppet move failed: ", err); }
        return false;
    }
    return true;
}

void SetActorHidden(void* actor, bool hidden)
{
    if (!actor || !g_actor.sigSetHidden.valid || !ObjectIsLive(actor)) return;
    const PropDesc* p = g_actor.sigSetHidden.props > 0 ? &g_actor.sigSetHidden.p[0] : nullptr;
    if (!p) return;
    ArgList a;
    a.AddBool(p->name, hidden);
    char err[128];
    Invoke(actor, g_actor.fnSetHidden, g_actor.sigSetHidden, a, nullptr, err, sizeof(err));
}

void DestroyActor(void* actor)
{
    if (!actor || !g_actor.sigDestroy.valid || !ObjectIsLive(actor)) return;
    ArgList a;
    char err[128];
    Invoke(actor, g_actor.fnDestroy, g_actor.sigDestroy, a, nullptr, err, sizeof(err));
}

bool EndsWith(const char* s, const char* tail)
{
    int a = 0, b = 0;
    while (s[a]) ++a;
    while (tail[b]) ++b;
    if (b > a) return false;
    for (int i = 0; i < b; ++i) if (s[a - b + i] != tail[i]) return false;
    return true;
}

// ---------------------------------------------------------------------------
// LAYER 5 -- the remote body
// ---------------------------------------------------------------------------
// The first puppet was a spawned copy of FS_Full_PlayerChar_C.  That is the
// FIRST-PERSON character: it draws only arms, so the remote player showed as "two
// small blue fragments".  The body is now qdavatar.h's: an engine SkeletalMeshActor
// carrying the game's own full-body player model (Player_FullChar_2026, used by the
// cutscenes), posed procedurally, holding the peer's weapon mesh.
//
// `PlayerBody=1` swaps the ACTOR under that body for the game's own player class, which
// is the only thing the enemy Blueprints accept as a target (the block below).
//
// Defined with the other reflection helpers further down; the body needs both.
void* ObjPropC(void* obj, const char* name);
bool CallBool1(void* obj, Fn& f, const char* fn, bool v);
bool CallNoArgs(void* obj, Fn& f, const char* fn);
bool WriteBytePropC(void* obj, const char* name, uint8_t v);

bool IsGameplayPawn(void* pawn)
{
    if (!pawn || !ObjectIsLive(pawn)) return false;
    void* c = ObjClass(pawn);
    return ObjNameIs(c, "FS_Full_PlayerChar_C") || ObjNameIs(c, "FS_PlayerChar_C");
}

bool g_peerInGameplay = false;   // from the peer's row flags (bit5 = menu)
// The partner's last reported position, in world cm.  Ownership of a moving world
// object follows the nearer player, and the partner has no pawn here, so its own row is
// the only position available.
float g_peerPos[3] = {0, 0, 0};

// The engine spawn pair (B7: the game has no character-construction path of its own).
typedef void* (*FnBeginDeferred)(void* worldContext, void* actorClass, const void* transform,
                                 uint8_t collisionHandling, void* owner);
typedef void* (*FnFinishSpawning)(void* actor, const void* transform);
typedef void* (*FnGetPawn)(void*, int32_t);

void* LocalPawn()
{
    FnGetPawn gp = (FnGetPawn)Ptr<void*>(0x028E0640u);
    void* world = GWorldPtr();
    return (gp && world) ? gp(world, 0) : nullptr;
}

void* SpawnGuarded(FnBeginDeferred begin, FnFinishSpawning finish, void* world, void* cls,
                   const uint8_t* xform, int* faulted)
{
    *faulted = 0;
    __try {
        // 1 = ESpawnActorCollisionHandlingMethod::AlwaysSpawn.  The first version passed
        // 0 (Undefined -> the class default), which refuses a spawn that overlaps
        // geometry -- and the puppet spawns exactly where another player stands.
        void* actor = begin(world, cls, xform, 1, nullptr);
        if (!actor) return nullptr;
        return finish(actor, xform);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        *faulted = 1;
        return nullptr;
    }
}

void* SpawnActorAt(void* cls, float x, float y, float z, float yawDeg)
{
    void* world = GWorldPtr();
    if (!world || !cls) return nullptr;
    FnBeginDeferred begin = (FnBeginDeferred)Ptr<void*>(0x028D3C90u);
    FnFinishSpawning finish = (FnFinishSpawning)Ptr<void*>(0x028DCD50u);
    avatar::Xf xf = avatar::XfIdentity();
    float h = yawDeg * 3.14159265f / 360.0f;
    xf.qz = sinf(h); xf.qw = cosf(h);
    xf.tx = x; xf.ty = y; xf.tz = z;
    alignas(16) uint8_t buf[48];
    memcpy(buf, &xf, 48);
    int faulted = 0;
    void* a = SpawnGuarded(begin, finish, world, cls, buf, &faulted);
    if (!a) QDLOG(LOG_WARN, "spawn failed (", (faulted ? "fault" : "null"), ")");
    return a;
}

// Half the player capsule: the peer reports its capsule CENTRE, the body stands on
// its feet.  Read from the local pawn (same class on both sides); 90 if unreadable.
float CapsuleHalfHeight()
{
    static float hh = 0.0f;
    if (hh > 0.0f) return hh;
    void* pawn = g_localPawn;
    if (!pawn || !Lay().valid) return 90.0f;
    int co = FindPropertyOffsetIn(ObjClass(pawn), "CapsuleComponent");
    void* cap = co >= 0 ? *(void**)((uint8_t*)pawn + co) : nullptr;
    int ho = cap ? FindPropertyOffsetIn(ObjClass(cap), "CapsuleHalfHeight") : -1;
    float v = ho >= 0 ? *(float*)((uint8_t*)cap + ho) : 0.0f;
    hh = (v > 20.0f && v < 300.0f) ? v : 90.0f;
    QDLOG(LOG_INFO, "capsule half height ", flt(hh, 1));
    return hh;
}

// ---------------------------------------------------------------------------
// The player-class proxy body (B7's spawn, G4's target, G5's state)
// ---------------------------------------------------------------------------
// MEASURED (SYNC-RESEARCH 1b): an enemy's Blueprint admits a hit only through two cast
// gates -- `FS_PlayerChar_C`, then `FB_DamageableInterface` -- so however near the
// SkeletalMeshActor puppet stands, pointing `TargetPlayer` at it changes nothing: the
// AI's own cast fails and the swing is discarded.  A body OF THE PLAYER CLASS is the
// only value that satisfies both gates, so this spawns one and drives it with the SAME
// poseable/animation/equipment code, which keeps what is on screen identical.
//
// The actor is UNPOSSESSED, AI-less, tickless and non-colliding: it is a target, not a
// player.  Nothing applies damage to it either -- `TryApplyDamage` on it is intercepted
// in HK_ProcessEvent and forwarded to the machine that owns that player (K_DAMAGE ->
// OnPlayerDamage), so each player's HP still has exactly one authority.
//
// The unproven part is the engine constructing a full gameplay Blueprint: its
// ReceiveBeginPlay runs, and this game's player Blueprint is not a passive object.  That
// is why it sits behind `PlayerBody=1`, default OFF (`API-GAPS` section 5, B7).
void* g_proxyCls = nullptr;      // FS_PlayerChar_C (or FS_Full_PlayerChar_C)
bool  g_proxyClsTried = false;
void* g_proxySeen = nullptr;     // the last proxy actor: recognises a target we set

// A player Blueprint carries its own skeletal meshes -- the first-person `Arms`, and on
// some builds a `CharacterMesh0` or `Mesh`.  They are made invisible rather than emptied:
// the asset belongs to the class default object, and clearing it would be a write to
// content rather than to this actor.
void HidePawnMeshes(void* actor)
{
    static const char* const kNames[3] = {"Arms", "CharacterMesh0", "Mesh"};
    static int off[3] = {-2, -2, -2};
    for (int i = 0; i < 3; ++i) {
        if (off[i] == -2) off[i] = FindPropertyOffsetIn(ObjClass(actor), kNames[i]);
        if (off[i] < 0) continue;
        void* comp = *(void**)((uint8_t*)actor + off[i]);
        if (comp && LooksLikeUObject(comp)) avatar::SetVisibility(comp, false);
    }
}

// The player-class body's own spawn, with a hook between BeginDeferred and
// FinishSpawning.  That window is what a deferred spawn is FOR, and it is the only place
// where the two properties that decide possession can be cleared before the engine's
// PostActorConstruction acts on them:
//   * `AutoPossessPlayer` would hand this body the machine's own player slot -- the host's
//     player would be unpossessed and the proxy would be the player the mod reads as
//     "local", which is the one failure this whole block must not have;
//   * `AutoPossessAI` would give it a controller and an AI of its own.
// Clearing them is belt and braces on top of the CDO's own values, and it is cheap.
void* SpawnPlayerBodyActor(void* cls, float x, float y, float z, float yawDeg)
{
    void* world = GWorldPtr();
    if (!world || !cls) return nullptr;
    FnBeginDeferred begin = (FnBeginDeferred)Ptr<void*>(0x028D3C90u);
    FnFinishSpawning finish = (FnFinishSpawning)Ptr<void*>(0x028DCD50u);
    avatar::Xf xf = avatar::XfIdentity();
    float h = yawDeg * 3.14159265f / 360.0f;
    xf.qz = sinf(h); xf.qw = cosf(h);
    xf.tx = x; xf.ty = y; xf.tz = z;
    alignas(16) uint8_t buf[48];
    memcpy(buf, &xf, 48);
    __try {
        void* actor = begin(world, cls, buf, 1, nullptr);   // 1 = AlwaysSpawn
        if (!actor) return nullptr;
        bool noPlayer = WriteBytePropC(actor, "AutoPossessPlayer", 0);
        bool noAi = WriteBytePropC(actor, "AutoPossessAI", 0);
        void* out = finish(actor, buf);
        if (out) {
            QDLOG(LOG_INFO, "player body: possession cleared (AutoPossessPlayer ",
                  (noPlayer ? "0" : "property absent"), ", AutoPossessAI ",
                  (noAi ? "0" : "property absent"), ")");
        }
        return out;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        QDLOG(LOG_ERROR, "player body: the engine's own spawn pair faulted on the player "
                         "class (B7 fails on this build)");
        return nullptr;
    }
}

// Spawn and prepare the body on the player class.  Null means "this could not be done
// here", and the caller falls back to the proven mesh body rather than leaving the
// partner invisible.
void* SpawnPlayerBody(avatar::Body* b, float x, float y, float z, float yaw)
{
    if (!EnsureActorApi() || !avatar::EnsureApi()) return nullptr;

    if (!g_proxyClsTried) {
        g_proxyClsTried = true;
        // The name the enemy's cast gate uses first; the class the live game possesses
        // second (it is the same gate if the full character derives from the base).
        g_proxyCls = FindClassByName("FS_PlayerChar_C");
        if (g_proxyCls) {
            QDLOG(LOG_INFO, "player body: class FS_PlayerChar_C resolved");
        } else {
            g_proxyCls = FindClassByName("FS_Full_PlayerChar_C");
            if (g_proxyCls)
                QDLOG(LOG_INFO, "player body: class FS_Full_PlayerChar_C resolved "
                                "(FS_PlayerChar_C is not in this build)");
            else
                QDLOG(LOG_ERROR, "player body: neither FS_PlayerChar_C nor "
                                 "FS_Full_PlayerChar_C resolved; PlayerBody cannot work "
                                 "on this build");
        }
    }
    if (!g_proxyCls) return nullptr;

    // The actor's own Z is the CAPSULE CENTRE here, not the body's feet: that is what
    // ACharacter's root is, and it is also what the peer reports in its PLAYER row.
    void* actor = SpawnPlayerBodyActor(g_proxyCls, x, y, z, yaw);
    if (!actor) {
        QDLOG(LOG_WARN, "player body: spawn of the player class failed; "
                        "falling back to the mesh body");
        return nullptr;
    }

    float hh = CapsuleHalfHeight();
    void* root = g_actor.rootComponent >= 0
                     ? *(void**)((uint8_t*)actor + g_actor.rootComponent) : nullptr;
    if (!root || !LooksLikeUObject(root)) root = nullptr;
    if (!avatar::BuildBody(b, actor, root, -hh)) {
        QDLOG(LOG_WARN, "player body: could not attach the visible body; falling back");
        DestroyActor(actor);
        memset(b, 0, sizeof(*b));
        return nullptr;
    }
    b->zoff = 0.0f;

    // Inert.  A player Blueprint ticks input, HUD, weapon and attribute systems that need
    // a controller; with no controller they can only misbehave, so its tick is off, the
    // movement component's is off too (gravity would otherwise walk the body away from
    // the position the peer sends), and the capsule neither blocks nor is swept.
    static Fn fTick = {}, fCompTick = {}, fMode = {};
    bool tick = CallBool1(actor, fTick, "SetActorTickEnabled", false);
    void* cmc = ObjPropC(actor, "CharacterMovement");
    bool cmcTick = false, cmcMode = false;
    if (cmc) {
        cmcTick = CallBool1(cmc, fCompTick, "SetComponentTickEnabled", false);
        if (fMode.Resolve(cmc, "SetMovementMode")) {
            Call c(fMode);
            if (fMode.sig.props > 0) c.U8(fMode.sig.p[0].name, 0);
            if (fMode.sig.props > 1) c.U8(fMode.sig.p[1].name, 0);
            cmcMode = c.Run(cmc);
        }
    }
    HidePawnMeshes(actor);

    // A controller -- if the Blueprint's own defaults gave it one -- must not run: it
    // would drive the body away from the positions the peer sends.
    void* ctl = ObjPropC(actor, "Controller");
    static Fn fCtlTick = {}, fStop = {};
    if (ctl) {
        CallBool1(ctl, fCtlTick, "SetActorTickEnabled", false);
        CallNoArgs(ctl, fStop, "StopMovement");
    }
    // The last line of defence.  If the body somehow became player 0 of this machine, the
    // session is broken in a way that must be reported and undone, never driven: the mod
    // would read the proxy as "the local player" for every lane it has.
    if (LocalPawn() == actor) {
        QDLOG(LOG_ERROR, "player body: the spawn took over THIS machine's player slot "
                         "(AutoPossessPlayer was not honoured); destroying it");
        DestroyActor(actor);
        memset(b, 0, sizeof(*b));
        return nullptr;
    }

    g_proxySeen = actor;
    QDLOG(LOG_INFO, "player body: spawned unpossessed at ", flt(x, 0), ",", flt(y, 0), ",",
          flt(z, 0), " (tick ", (tick ? "off" : "MISSING"), ", movement ",
          (cmc ? (cmcTick ? "off" : "tick MISSING") : "component absent"),
          (cmcMode ? "/mode set" : ""), ", controller ", (ctl ? "present, stopped" : "none"),
          ", collision off, own meshes hidden); the poseable body is the visible one");
    return actor;
}

void* LocalController()
{
    typedef void* (*FnGetPC)(void*, int32_t);
    FnGetPC gpc = (FnGetPC)Ptr<void*>(0x028E0340u);
    void* world = GWorldPtr();
    return (gpc && world) ? gpc(world, 0) : nullptr;
}

// Where the player LOOKS: AController::ControlRotation.  In this game the pawn's root
// does not turn with the view (measured: a body placed along the root's yaw appeared
// off to the side of the camera), so the heading sent to the peer must be this.
bool LocalViewRotation(float* pitch, float* yaw)
{
    static int off = -2, pcmOff = -2;
    static Fn camRot = {};
    void* pc = LocalController();
    if (!pc || !Lay().valid) return false;
    // The camera manager is the authority on what is on screen.
    if (pcmOff == -2) pcmOff = FindPropertyOffsetIn(ObjClass(pc), "PlayerCameraManager");
    void* pcm = pcmOff >= 0 ? *(void**)((uint8_t*)pc + pcmOff) : nullptr;
    if (pcm && ObjectIsLive(pcm) && camRot.Resolve(pcm, "GetCameraRotation")) {
        Call c(camRot);
        if (c.Run(pcm) && c.Ret()) {
            const float* r = (const float*)c.Ret();
            *pitch = r[0];
            *yaw = r[1];
            return true;
        }
    }
    if (off == -2) off = FindPropertyOffsetIn(ObjClass(pc), "ControlRotation");
    if (off < 0) return false;
    const float* r = (const float*)((uint8_t*)pc + off);
    *pitch = r[0];
    *yaw = r[1];
    return true;
}

// ---------------------------------------------------------------------------
// What the local player is DOING: the first-person arms' active montage (every attack,
// wind-up, throw and swap plays one -- measured, see avatar::ClassifyMontage) and the
// movement mode (falling = jumping).  Sent in PLAYER.action / PLAYER.flags bit0.
// ---------------------------------------------------------------------------
struct LocalAction {
    void*   last;        // montage seen last frame
    int     kind;        // avatar::ActKind
    int     seq;         // bumps on every new move, so a repeat restarts on the peer
    uint8_t byte;        // PLAYER.action
    bool    airborne;
    uint8_t moveMode;    // EMovementMode
    int     pendMove;    // held charge: the move its release plays (0 = not charging)
    uint32_t moveAt;     // when the last strike was sent (follow-through filter)
};
LocalAction g_localAct = {};
volatile bool g_localUsed = false;   // set by UseObserve (ProcessEvent hook)

void* LocalArmsMontage()
{
    static int armsOff = -2;
    static Fn fAnim = {}, fMont = {};
    if (!IsGameplayPawn(g_localPawn)) return nullptr;
    if (armsOff == -2) {
        armsOff = FindPropertyOffsetIn(ObjClass(g_localPawn), "Arms");
        if (armsOff < 0) QDLOG(LOG_WARN, "action sync: the pawn has no Arms mesh property");
    }
    if (armsOff < 0) return nullptr;
    void* arms = *(void**)((uint8_t*)g_localPawn + armsOff);
    if (!arms || !fAnim.Resolve(arms, "GetAnimInstance")) return nullptr;
    Call c(fAnim);
    if (!c.Run(arms) || !c.Ret()) return nullptr;
    void* ai = *(void**)c.Ret();
    if (!ai || !fMont.Resolve(ai, "GetCurrentActiveMontage")) return nullptr;
    Call m(fMont);
    if (!m.Run(ai) || !m.Ret()) return nullptr;
    return *(void**)m.Ret();
}

// ACharacter::CharacterMovement -> UCharacterMovementComponent::MovementMode.
uint8_t LocalMovementMode()
{
    static int cmcOff = -2, modeOff = -2;
    if (!IsGameplayPawn(g_localPawn)) return 0;
    if (cmcOff == -2) cmcOff = FindPropertyOffsetIn(ObjClass(g_localPawn), "CharacterMovement");
    if (cmcOff < 0) return 0;
    void* cmc = *(void**)((uint8_t*)g_localPawn + cmcOff);
    if (!cmc) return 0;
    if (modeOff == -2) {
        modeOff = FindPropertyOffsetIn(ObjClass(cmc), "MovementMode");
        QDLOG(LOG_INFO, "action sync: CharacterMovement@", dec(cmcOff), " MovementMode@", dec(modeOff));
    }
    return modeOff >= 0 ? *((uint8_t*)cmc + modeOff) : 0;
}

void LocalActionPump(uint32_t now)
{
    (void)now;
    LocalAction& a = g_localAct;
    a.moveMode = LocalMovementMode();
    a.airborne = (a.moveMode == 3);                        // MOVE_Falling
    void* mon = LocalArmsMontage();
    if (mon != a.last) {
        a.last = mon;
        char nm[128] = "";
        if (mon) FNameToString(ObjNameRaw(mon), nm, sizeof(nm));
        int k = mon ? avatar::ClassifyMontage(nm) : 0;
        int raw = k;
        if (a.pendMove && !avatar::IsChargeKind(k)) {
            // Released: whatever comes next (the left hand's MeleeResponse, the alt
            // attack's own move, or nothing) means the strike is happening now.
            k = (k == avatar::A_THRUST || k == avatar::A_ATTACK) ? k : a.pendMove;
            a.pendMove = 0;
            a.moveAt = now;
        } else if (avatar::IsChargeKind(k)) {
            a.pendMove = avatar::ChargeOpenerMove(nm);
            if (!a.pendMove) a.pendMove = avatar::A_ATTACK;
        } else if (k > 0 && k != avatar::A_SWAP && k != avatar::A_HURT && k != avatar::A_DYING &&
                   k <= avatar::A_THRUST && now - a.moveAt < 700) {
            // Stab_N / Slash_* after a strike are its follow-through, not a new move.
            k = -1;
        } else if (k == avatar::A_THRUST || k == avatar::A_ATTACK) {
            a.moveAt = now;
        }
        if (k > 0) ++a.seq;
        if (k >= 0) a.kind = k;
        QDLOG(LOG_INFO, "local montage ", (mon ? nm : "none"), " -> kind ", dec(raw), " sent ", dec(k));
    }
    if (g_localUsed) {
        g_localUsed = false;
        a.kind = avatar::A_USE;
        ++a.seq;
        QDLOG(LOG_INFO, "local action: used an item -> kind ", dec(avatar::A_USE));
    }
    a.byte = avatar::ActionByte(a.kind, a.seq);
}

avatar::Body g_body = {};
uint8_t  g_peerAction = 0;
int      g_peerAirborne = -1;
uint32_t g_peerActionMs = 0;
wire::EquipBody g_peerEquip = {};
bool g_peerEquipDirty = false;

// Spawn and build one body at (x,y,z = capsule centre).  nullptr on failure.
void* SpawnBody(avatar::Body* b, float x, float y, float z, float yaw)
{
    if (!EnsureActorApi() || !avatar::EnsureApi()) return nullptr;
    // The player class when asked for, and the proven mesh actor otherwise -- including
    // when the player class fails, because a switched-on body must never leave the
    // partner invisible.
    if (Cfg().player_body) {
        if (void* a = SpawnPlayerBody(b, x, y, z, yaw)) return a;
    }
    void* cls = FindClassByName("SkeletalMeshActor");
    void* actor = SpawnActorAt(cls, x, y, z - CapsuleHalfHeight(), yaw);
    if (!actor) return nullptr;
    if (!avatar::BuildBody(b, actor)) {
        DestroyActor(actor);
        memset(b, 0, sizeof(*b));
        return nullptr;
    }
    b->zoff = -CapsuleHalfHeight();   // this actor's origin IS the body's feet
    return actor;
}

void PlaceBody(avatar::Body* b, float x, float y, float z, float yaw, uint32_t now)
{
    // The actor's own Z differs between the two body kinds (a Character's root is its
    // capsule centre, a SkeletalMeshActor's is the feet); the body itself is placed at
    // the same height either way, which is what Animate's ground reference needs.
    SetActorXform(b->actor, x, y, z + b->zoff, yaw);
    avatar::Animate(b, x, y, z - CapsuleHalfHeight(), yaw, now);
}

avatar::Xf XfFromWire(const int32_t* v)
{
    avatar::Xf x = avatar::XfIdentity();
    x.tx = v[0] / 100.0f; x.ty = v[1] / 100.0f; x.tz = v[2] / 100.0f;
    x.qx = v[3] / 1e6f; x.qy = v[4] / 1e6f; x.qz = v[5] / 1e6f; x.qw = v[6] / 1e6f;
    x.sx = v[7] / 1000.0f; x.sy = v[8] / 1000.0f; x.sz = v[9] / 1000.0f;
    return x;
}

void ApplyEquip(avatar::Body* b, const wire::EquipBody& e)
{
    bool done[2] = {false, false};
    for (int i = 0; i < e.n; ++i) {
        const wire::EquipItem& it = e.items[i];
        int h = it.hand ? 1 : 0;
        if (done[h] || (it.mesh_kind != 1 && it.mesh_kind != 2)) continue;
        avatar::SetHeld(b, h, it.path, XfFromWire(it.xform), it.mesh_kind);
        done[h] = true;
    }
    for (int h = 0; h < 2; ++h) if (!done[h]) avatar::SetHeld(b, h, "", avatar::XfIdentity());
}

// ---- the local player's held items ---------------------------------------
// Weapons are WeaponVisuals_C actors attached to the first-person arms; the mesh is a
// StaticMeshComponent ("Mesh") inside.  Found by walking component attachment back to
// the pawn, filtered to visible components of WeaponVisuals-derived actors.
struct EquipScan {
    bool    init;
    void*   wvClass;         // WeaponVisuals_C
    int32_t smcName;         // FName comparison index of "StaticMeshComponent"
    int32_t skcName;         // ... and of "SkeletalMeshComponent"
    int     relLoc, relRot, relScale, staticMesh, skelMesh;
    Fn      isVisible;
};
EquipScan g_eqs = {};

bool DerivesFrom(void* cls, void* base)
{
    for (int i = 0; cls && i < 16; ++i, cls = SuperOf(cls)) if (cls == base) return true;
    return false;
}

avatar::Xf RelXf(void* comp)
{
    avatar::Xf x = avatar::XfIdentity();
    const float* l = (const float*)((uint8_t*)comp + g_eqs.relLoc);
    const float* r = (const float*)((uint8_t*)comp + g_eqs.relRot);
    const float* s = (const float*)((uint8_t*)comp + g_eqs.relScale);
    avatar::Q4 q = avatar::QFromRotator(r[0], r[1], r[2]);
    x.qx = q.x; x.qy = q.y; x.qz = q.z; x.qw = q.w;
    x.tx = l[0]; x.ty = l[1]; x.tz = l[2];
    x.sx = s[0]; x.sy = s[1]; x.sz = s[2];
    return x;
}

bool IsLeftSocket(const char* sock)
{
    int n = 0;
    while (sock[n]) ++n;
    if (n >= 2 && (sock[n - 2] == '_' || sock[n - 2] == '.') && (sock[n - 1] | 0x20) == 'l') return true;
    for (int k = 0; k + 3 < n; ++k) {
        if ((sock[k] | 0x20) == 'l' && (sock[k + 1] | 0x20) == 'e' && (sock[k + 2] | 0x20) == 'f' &&
            (sock[k + 3] | 0x20) == 't')
            return true;
    }
    return false;
}

int ScanLocalEquip(void* pawn, wire::EquipBody* out, bool verbose)
{
    out->n = 0;
    avatar::Api& a = avatar::A();
    if (!pawn || !avatar::EnsureApi() || a.attachParent < 0) return 0;
    if (!g_eqs.init) {
        g_eqs.init = true;
        QdName n;
        g_eqs.smcName = FNameFromAnsi("StaticMeshComponent", &n) ? (int32_t)n.comparison_index : -1;
        g_eqs.relLoc = FindPropertyOffsetIn(a.clsScene, "RelativeLocation");
        g_eqs.relRot = FindPropertyOffsetIn(a.clsScene, "RelativeRotation");
        g_eqs.relScale = FindPropertyOffsetIn(a.clsScene, "RelativeScale3D");
        g_eqs.staticMesh = FindPropertyOffsetIn(a.clsStatic, "StaticMesh");
        g_eqs.skcName = FNameFromAnsi("SkeletalMeshComponent", &n) ? (int32_t)n.comparison_index : -1;
        g_eqs.skelMesh = FindPropertyOffsetIn(a.clsSkinned, "SkeletalMesh");
        g_eqs.isVisible.Resolve(nullptr, "IsVisible", a.clsScene);
        QDLOG(LOG_INFO, "equip scan: RelativeScale3D@", dec(g_eqs.relScale),
              " StaticMesh@", dec(g_eqs.staticMesh), " IsVisible=", dec(g_eqs.isVisible.fn ? 1 : 0));
    }
    // WeaponVisuals_C loads with the first weapon; keep looking until it exists.
    if (!g_eqs.wvClass || !ObjectIsLive(g_eqs.wvClass)) {
        static uint32_t lastLook = 0;
        uint32_t now = Net::Now();
        if (now - lastLook < 5000) return 0;
        lastLook = now;
        g_eqs.wvClass = FindClassByName("WeaponVisuals_C");
        if (!g_eqs.wvClass) return 0;
    }
    if (g_eqs.relLoc < 0 || g_eqs.relRot < 0 || g_eqs.relScale < 0 || g_eqs.staticMesh < 0 ||
        g_eqs.smcName < 0 || !g_eqs.isVisible.fn)
        return 0;
    int n = ObjectCount();
    for (int i = 0; i < n && out->n < wire::kMaxEquipItems; ++i) {
        void* o = ObjectAt(i);
        if (!o) continue;
        void* cls = ObjClass(o);
        int32_t ci = cls ? (int32_t)ObjNameRaw(cls).comparison_index : -1;
        bool isSkel = ci == g_eqs.skcName && g_eqs.skcName >= 0 && g_eqs.skelMesh >= 0;
        if (ci != g_eqs.smcName && !isSkel) continue;
        void* owner = ObjOuter(o);
        if (!owner || owner == pawn || !DerivesFrom(ObjClass(owner), g_eqs.wvClass)) continue;
        // Walk up the attachment chain to the pawn, composing relative transforms.
        avatar::Xf rel = RelXf(o);
        void* c = o;
        void* link = nullptr;
        for (int d = 0; d < 8; ++d) {
            void* p = *(void**)((uint8_t*)c + a.attachParent);
            if (!p) break;
            if (ObjOuter(p) == pawn) { link = c; break; }
            rel = avatar::XfCompose(RelXf(p), rel);
            c = p;
        }
        if (!link) continue;
        Call vc(g_eqs.isVisible);
        if (!vc.Run(o) || !vc.Ret() || !*vc.Ret()) continue;
        void* mesh = *(void**)((uint8_t*)o + (isSkel ? g_eqs.skelMesh : g_eqs.staticMesh));
        wire::EquipItem& it = out->items[out->n];
        if (avatar::AssetPath(mesh, it.path, sizeof(it.path)) <= 0) continue;
        char sock[64] = "";
        if (a.attachSocket >= 0) FNameToString(*(QdName*)((uint8_t*)link + a.attachSocket), sock, sizeof(sock));
        bool left = IsLeftSocket(sock);
        it.hand = left ? 1 : 0;
        it.mesh_kind = isSkel ? 2 : 1;
        it.xform[0] = (int32_t)(rel.tx * 100.0f); it.xform[1] = (int32_t)(rel.ty * 100.0f); it.xform[2] = (int32_t)(rel.tz * 100.0f);
        it.xform[3] = (int32_t)(rel.qx * 1e6f); it.xform[4] = (int32_t)(rel.qy * 1e6f);
        it.xform[5] = (int32_t)(rel.qz * 1e6f); it.xform[6] = (int32_t)(rel.qw * 1e6f);
        it.xform[7] = (int32_t)(rel.sx * 1000.0f); it.xform[8] = (int32_t)(rel.sy * 1000.0f); it.xform[9] = (int32_t)(rel.sz * 1000.0f);
        if (verbose) {
            char od[128];
            ObjDescribe(owner, od, sizeof(od));
            QDLOG(LOG_INFO, "held: ", od, " mesh=", it.path, " socket=", sock, " hand=", (left ? "L" : "R"),
                  " rel t=", flt(rel.tx, 1), ",", flt(rel.ty, 1), ",", flt(rel.tz, 1),
                  " q=", flt(rel.qx, 3), ",", flt(rel.qy, 3), ",", flt(rel.qz, 3), ",", flt(rel.qw, 3),
                  " s=", flt(rel.sx, 2));
        }
        ++out->n;
    }
    return out->n;
}

bool EquipEqual(const wire::EquipBody& a, const wire::EquipBody& b)
{
    if (a.n != b.n) return false;
    for (int i = 0; i < a.n; ++i) {
        if (a.items[i].hand != b.items[i].hand) return false;
        if (strcmp(a.items[i].path, b.items[i].path) != 0) return false;
    }
    return true;
}

wire::EquipBody g_localEquip = {};

// Rescan twice a second; send on change and once a second regardless (the body is
// latest-wins, so a lost datagram is repaired by the next).
void LocalEquipPump(uint32_t now)
{
    static uint32_t lastScan = 0, lastSend = 0;
    static bool first = true;
    if (!IsGameplayPawn(g_localPawn)) return;
    if (now - lastScan < 500) return;
    lastScan = now;
    wire::EquipBody e = {};
    ScanLocalEquip(g_localPawn, &e, false);
    bool changed = !EquipEqual(e, g_localEquip);
    if (changed || first) {
        first = false;
        e.rev = g_localEquip.rev + 1;
        g_localEquip = e;
        wire::EquipBody v = {};
        ScanLocalEquip(g_localPawn, &v, true);           // log what changed
        QDLOG(LOG_INFO, "local equipment rev ", dec((int)e.rev), ": ", dec(e.n), " item(s)");
    }
    if (g_net.Connected() && (changed || now - lastSend > 1000)) {
        lastSend = now;
        static uint8_t body[wire::kMaxBody];
        wire::Writer w(body, sizeof(body));
        wire::WriteEquip(w, g_localEquip);
        if (w.ok) g_net.SendUnreliable(wire::K_EQUIPMENT, body, w.len);
    }
}

void SpawnPuppetOnce(uint32_t now)
{
    motion::MoveOut m = g_peerInterp.Sample(now);
    if (!m.valid) { g_puppet.SpawnFailed("no peer sample yet", now); return; }
    g_puppet.SpawnStarted(now);
    void* actor = SpawnBody(&g_body, m.px, m.py, m.pz, m.yaw_deg);
    if (!actor) {
        g_puppet.SpawnFailed("body spawn failed", now);
        QDLOG(LOG_WARN, "puppet body spawn failed");
        return;
    }
    g_puppetActor = actor;
    g_peerEquipDirty = true;
    g_puppet.Spawned(now);
    char cn[96] = "?";
    FNameToString(ObjNameRaw(ObjClass(actor)), cn, sizeof(cn));
    QDLOG(LOG_INFO, "puppet spawned as ", cn, " at ", flt(m.px, 0), ",", flt(m.py, 0), ",",
          flt(m.pz, 0));
}

void ApplyStatus(avatar::Body* b, uint32_t mask);
extern uint32_t g_peerStatus;
void PuppetPump(uint32_t now)
{
    if (!Cfg().puppet_sync) return;

    // The body dies with its world on a level load.  Notice, and let the FSM respawn.
    if (g_puppetActor && !ObjectIsLive(g_puppetActor)) {
        QDLOG(LOG_INFO, "puppet actor gone (level change)");
        g_puppetActor = nullptr;
        memset(&g_body, 0, sizeof(g_body));
        g_puppet.LocalWorldChanged(now);
    }

    // Presence is the SESSION, not the last packet: a level load stalls the peer for
    // seconds, and treating 2 s of silence as "left" destroyed and respawned the body on
    // every load.  Silence is handled by the FSM's freeze/hide timers instead.
    // A body only makes sense when BOTH players are in the game world: the title
    // screen and file select are worlds of their own.
    // Signed: the network thread can stamp a receive AFTER this tick read `now`, and
    // the unsigned difference then wraps to ~4e9 -- which read as "peer silent" and
    // destroyed and respawned the body every few seconds.
    bool c1 = g_net.Connected(), c2 = (int32_t)(now - g_net.LastRxMs()) < 60000;
    bool c3 = g_peerInGameplay, c4 = IsGameplayPawn(g_localPawn);
    bool present = c1 && c2 && c3 && c4;
    static bool lastPresent = false;
    if (present != lastPresent) {
        lastPresent = present;
        QDLOG(LOG_INFO, "peer presence ", dec(present ? 1 : 0), " (connected=", dec(c1), " rx=", dec(c2),
              " peerInGame=", dec(c3), " localInGame=", dec(c4), ")");
    }
    g_puppet.PeerPresent(present, now);
    g_puppet.Tick(now);

    puppet::PuppetFsm::Step st = g_puppet.Get();
    if (st.want_spawn && g_puppet.ReadyToSpawn(now)) SpawnPuppetOnce(now);

    if (st.want_destroy) {
        if (g_puppetActor) {
            QDLOG(LOG_INFO, "puppet destroyed (peer left the session)");
            DestroyActor(g_puppetActor);
            g_puppetActor = nullptr;
            memset(&g_body, 0, sizeof(g_body));
        }
        g_puppet.AckDestroy();
    }

    if (!g_puppetActor) return;
    static bool hidden = false;
    if (st.want_hide != hidden) {
        SetActorHidden(g_puppetActor, st.want_hide);
        hidden = st.want_hide;
    }
    if (g_peerEquipDirty) {
        g_peerEquipDirty = false;
        ApplyEquip(&g_body, g_peerEquip);
    }
    if (g_body.actByte != g_peerAction) {
        QDLOG(LOG_INFO, "peer action kind ", dec(g_peerAction & 0x1F), " #", dec(g_peerAction >> 5));
    }
    avatar::SetAction(&g_body, g_peerAction);
    g_body.airIn = g_peerAirborne;
    ApplyStatus(&g_body, g_peerStatus);
    motion::MoveOut m = g_peerInterp.Sample(now);
    if (m.valid && !m.frozen) PlaceBody(&g_body, m.px, m.py, m.pz, m.yaw_deg, now);

    // Read-back: where the body ACTUALLY is versus where the peer says it should be.
    // "The call returned" is not evidence the actor moved.
    static uint32_t lastReport = 0;
    if (m.valid && now - lastReport > 10000) {
        lastReport = now;
        ActorXform x;
        if (ReadActorXform(g_puppetActor, &x)) {
            float fz = m.pz + g_body.zoff;   // the ACTOR's own Z for either body kind
            float dx = x.px - m.px, dy = x.py - m.py, dz = x.pz - fz;
            QDLOG(LOG_INFO, "puppet at ", flt(x.px, 0), ",", flt(x.py, 0), ",", flt(x.pz, 0),
                  " yaw=", flt(x.yaw, 0), "  peer ", flt(m.px, 0), ",", flt(m.py, 0), ",",
                  flt(fz, 0), "  err=", flt(sqrtf(dx * dx + dy * dy + dz * dz), 1), "cm",
                  " speed=", flt(g_body.speed, 0), " holding=",
                  (g_body.held[0].path[0] ? g_body.held[0].path : "-"));
        }
    }
}

// ---------------------------------------------------------------------------
// Effects: thrown weapons and spells (WIRE.md 3.14)
// ---------------------------------------------------------------------------
// The game's "magic" -- Fireball, MagicFireDagger, ice/poison/energy daggers -- is a
// thrown projectile: an actor deriving from Dagger_Projectile_Base_C that carries its
// own Niagara trail, light and sound, flies by ProjectileMovement, and hits by its own
// traces.  The local throw is seen where it begins (ReceiveBeginPlay reaches every
// Blueprint actor through ProcessEvent; the hook compares one FName index per call)
// and is sent as one reliable event: class, transform, velocity.
//
// The peer spawns the SAME class, so it looks and sounds the same, but as a visual
// only: collision is turned off before the actor finishes spawning and its actor tick
// (where the hit trace lives) is turned off after, so it can neither hit nor damage
// anything in the peer's world.  Its components still tick: it flies, glows and trails.
// A lifespan removes it, since without collision it never hits a wall.
int32_t g_beginPlayIdx = -1;
extern void* g_enemyBaseCls;         // defined with the enemy section; 0 until it is found
void* volatile g_fxRing[64];
volatile long g_fxHead = 0;
long g_fxTail = 0;
void* g_fxOurs[32];                  // copies spawned here: never sent back
int g_fxOursN = 0;
void* g_fxLastLocal = nullptr;     // dev read-back: the last local projectile ...
void* g_fxLastCopy = nullptr;      // ... and the last copy spawned

void FxObserve(void* self, void* func)
{
    if (g_beginPlayIdx < 0 || !func) return;
    if (ObjNameRaw(func).comparison_index != g_beginPlayIdx) return;
    long h = InterlockedIncrement(&g_fxHead) - 1;
    g_fxRing[h & 63] = self;
}

bool IsProjectileClass(void* cls)
{
    for (int d = 0; cls && d < 16; ++d, cls = SuperOf(cls)) {
        char n[96];
        if (FNameToString(ObjNameRaw(cls), n, sizeof(n)) <= 0) return false;
        if (NameEquals(n, "Dagger_Projectile_Base_C")) return true;
        if (NameEquals(n, "Actor")) return false;
    }
    return false;
}

void* ActorObjProp(void* actor, const char* name)
{
    int off = FindPropertyOffsetIn(ObjClass(actor), name);
    return off >= 0 ? *(void**)((uint8_t*)actor + off) : nullptr;
}

bool ProjectileVelocity(void* actor, float v[3])
{
    void* pm = ActorObjProp(actor, "ProjectileMovement");
    if (!pm) return false;
    int vo = FindPropertyOffsetIn(ObjClass(pm), "Velocity");
    if (vo < 0) return false;
    memcpy(v, (uint8_t*)pm + vo, 12);
    return true;
}

bool CallActorFloat(void* actor, const char* fn, const char* param, float v)
{
    Fn f = {};
    if (!f.Resolve(actor, fn)) return false;
    Call c(f);
    c.F32(param, v);
    return c.Run(actor);
}

bool CallActorBool(void* actor, const char* fn, const char* param, bool v)
{
    Fn f = {};
    if (!f.Resolve(actor, fn)) return false;
    Call c(f);
    c.U8(param, v ? 1 : 0);
    return c.Run(actor);
}

// Spawn with a full rotation; collision is off BEFORE components register.
void* SpawnVisualCopy(void* cls, const wire::EffectBody& e, float offX, float offY)
{
    void* world = GWorldPtr();
    if (!world || !cls) return nullptr;
    FnBeginDeferred begin = (FnBeginDeferred)Ptr<void*>(0x028D3C90u);
    FnFinishSpawning finish = (FnFinishSpawning)Ptr<void*>(0x028DCD50u);
    avatar::Xf xf = avatar::XfIdentity();
    avatar::Q4 q = avatar::QFromRotator(wire::DequantizeAngle(e.pitch), wire::DequantizeAngle(e.yaw), 0);
    xf.qx = q.x; xf.qy = q.y; xf.qz = q.z; xf.qw = q.w;
    xf.tx = wire::DequantizePos(e.px) + offX;
    xf.ty = wire::DequantizePos(e.py) + offY;
    xf.tz = wire::DequantizePos(e.pz);
    alignas(16) uint8_t buf[48];
    memcpy(buf, &xf, 48);
    void* actor = nullptr;
    __try {
        actor = begin(world, cls, buf, 1, nullptr);
        if (actor) {
            avatar::SetActorCollision(actor, false);
            actor = finish(actor, buf);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        QDLOG(LOG_WARN, "effect: spawn faulted for ", e.cls);
        return nullptr;
    }
    if (!actor) return nullptr;
    if (g_fxOursN < 32) g_fxOurs[g_fxOursN++] = actor;
    else { memmove(g_fxOurs, g_fxOurs + 1, sizeof(void*) * 31); g_fxOurs[31] = actor; }
    avatar::SetActorCollision(actor, false);
    CallActorBool(actor, "SetActorTickEnabled", "bEnabled", false);
    CallActorFloat(actor, "SetLifeSpan", "InLifespan", (e.life_ms ? e.life_ms : 4000) / 1000.0f);
    void* pm = ActorObjProp(actor, "ProjectileMovement");
    int vo = pm ? FindPropertyOffsetIn(ObjClass(pm), "Velocity") : -1;
    if (vo >= 0) {
        float v[3] = {wire::DequantizePos(e.vx), wire::DequantizePos(e.vy), wire::DequantizePos(e.vz)};
        memcpy((uint8_t*)pm + vo, v, 12);
    }
    return actor;
}

struct FxCopy { uint16_t id; void* actor; };
FxCopy g_fxCopies[16];
int g_fxCopyNext = 0;

// The sender's projectile stopped (hit something) or vanished: the copy, which cannot
// collide, is put where it stopped and removed -- otherwise it flies through the floor.
void EndEffect(const wire::EffectBody& e, float offX, float offY)
{
    for (int i = 0; i < 16; ++i) {
        FxCopy& c = g_fxCopies[i];
        if (!c.actor || c.id != e.id) continue;
        void* a = c.actor;
        c.actor = nullptr;
        if (!ObjectIsLive(a)) return;
        void* pm = ActorObjProp(a, "ProjectileMovement");
        if (pm) { Fn f = {}; if (f.Resolve(pm, "StopMovementImmediately")) { Call k(f); k.Run(pm); } }
        ActorXform x;
        float yaw = ReadActorXform(a, &x) ? x.yaw : 0.0f;
        SetActorXform(a, wire::DequantizePos(e.px) + offX, wire::DequantizePos(e.py) + offY,
                      wire::DequantizePos(e.pz), yaw);
        CallActorFloat(a, "SetLifeSpan", "InLifespan", 0.35f);
        return;
    }
}

// Where the sender's projectile is now: it bounces and rolls on the sender's ground,
// which a copy that cannot collide could never reproduce on its own.  Position is
// snapped, velocity carries it until the next update.
void MoveEffect(const wire::EffectBody& e, float offX, float offY)
{
    for (int i = 0; i < 16; ++i) {
        FxCopy& c = g_fxCopies[i];
        if (!c.actor || c.id != e.id) continue;
        if (!ObjectIsLive(c.actor)) { c.actor = nullptr; return; }
        ActorXform x;
        float yaw = ReadActorXform(c.actor, &x) ? x.yaw : 0.0f;
        SetActorXform(c.actor, wire::DequantizePos(e.px) + offX, wire::DequantizePos(e.py) + offY,
                      wire::DequantizePos(e.pz), yaw);
        void* pm = ActorObjProp(c.actor, "ProjectileMovement");
        int vo = pm ? FindPropertyOffsetIn(ObjClass(pm), "Velocity") : -1;
        if (vo >= 0) {
            float v[3] = {wire::DequantizePos(e.vx), wire::DequantizePos(e.vy), wire::DequantizePos(e.vz)};
            memcpy((uint8_t*)pm + vo, v, 12);
        }
        return;
    }
}

void SpawnMeleeArc(const wire::EffectBody& e, avatar::Body* b);
extern avatar::Body* g_devMirrorBody;
void SpawnEffect(const wire::EffectBody& e, float offX, float offY)
{
    if (e.kind == 2) { EndEffect(e, offX, offY); return; }
    if (e.kind == 3) { MoveEffect(e, offX, offY); return; }
    if (e.kind == 4) { SpawnMeleeArc(e, (offX != 0.0f || offY != 0.0f) ? g_devMirrorBody : &g_body); return; }
    if (e.kind != 1) return;
    void* cls = avatar::LoadAsset(e.cls);
    if (!cls) return;
    void* a = SpawnVisualCopy(cls, e, offX, offY);
    g_fxLastCopy = a;
    if (a) {
        g_fxCopies[g_fxCopyNext].id = e.id;
        g_fxCopies[g_fxCopyNext].actor = a;
        g_fxCopyNext = (g_fxCopyNext + 1) & 15;
    }
    QDLOG(LOG_INFO, "effect: ", e.cls, a ? " shown" : " NOT shown", " at ",
          flt(wire::DequantizePos(e.px) + offX, 0), ",", flt(wire::DequantizePos(e.py) + offY, 0), ",",
          flt(wire::DequantizePos(e.pz), 0));
}

void DevMirrorEffect(const wire::EffectBody& e);

// Game thread: look at what began play since last frame; send the local player's
// projectiles.  Read one frame late on purpose -- a thrower may set the velocity
// after SpawnActor returns.
struct FxTrack { void* actor; uint16_t id; float x, y, z; };
FxTrack g_fxTrack[8];
uint16_t g_fxNextId = 1;

void FxSend(const wire::EffectBody& e)
{
    DevMirrorEffect(e);
    if (!g_net.Connected()) return;
    uint8_t body[320];
    wire::Writer w(body, sizeof(body));
    wire::WriteEffect(w, e);
    if (!w.ok) return;
    // Spawn and end are events (reliable); position updates are superseded by the
    // next one (unreliable).
    if (e.kind == 3) g_net.SendUnreliable(wire::K_EFFECT_MOVE, body, w.len);
    else g_net.SendReliable(wire::K_EFFECT, body, w.len);
}

// Follow each sent projectile until it stops or is destroyed, then send where.
void FxTrackPump(uint32_t now)
{
    static uint32_t lastMove = 0;
    bool moveTick = now - lastMove >= 66;
    if (moveTick) lastMove = now;
    for (int i = 0; i < 8; ++i) {
        FxTrack& t = g_fxTrack[i];
        if (!t.actor) continue;
        bool live = ObjectIsLive(t.actor);
        float v[3] = {1, 1, 1};
        ActorXform x;
        if (live && ReadActorXform(t.actor, &x)) { t.x = x.px; t.y = x.py; t.z = x.pz; }
        if (live) ProjectileVelocity(t.actor, v);
        bool stopped = fabsf(v[0]) + fabsf(v[1]) + fabsf(v[2]) < 1.0f;
        if (live && !stopped) {
            if (moveTick) {
                wire::EffectBody m;
                memset(&m, 0, sizeof(m));
                m.kind = 3;
                m.id = t.id;
                m.px = wire::QuantizePos(t.x); m.py = wire::QuantizePos(t.y); m.pz = wire::QuantizePos(t.z);
                m.vx = wire::QuantizePos(v[0]); m.vy = wire::QuantizePos(v[1]); m.vz = wire::QuantizePos(v[2]);
                FxSend(m);
            }
            continue;
        }
        wire::EffectBody e;
        memset(&e, 0, sizeof(e));
        e.kind = 2;
        e.id = t.id;
        e.px = wire::QuantizePos(t.x); e.py = wire::QuantizePos(t.y); e.pz = wire::QuantizePos(t.z);
        QDLOG(LOG_INFO, "effect: local #", dec(t.id), (live ? " stopped" : " gone"), " at ",
              flt(t.x, 0), ",", flt(t.y, 0), ",", flt(t.z, 0));
        t.actor = nullptr;
        FxSend(e);
    }
}

void FxPump(uint32_t now)
{
    FxTrackPump(now);
    long head = g_fxHead;
    if (head - g_fxTail > 64) g_fxTail = head - 64;
    for (; g_fxTail < head; ++g_fxTail) {
        void* a = g_fxRing[g_fxTail & 63];
        if (!a || !ObjectIsLive(a) || !IsGameplayPawn(g_localPawn)) continue;
        bool ours = false;
        for (int i = 0; i < g_fxOursN; ++i) if (g_fxOurs[i] == a) { ours = true; break; }
        if (ours) continue;
        void* owner = ActorObjProp(a, "Owner");
        void* inst = ActorObjProp(a, "Instigator");
        // An enemy's shot is not a dagger and is not "mine", so the class test below
        // would never accept it: fireballs and spit existed on one machine only
        // (SYNC-GAPS).  The enemy base class is already cached by the enemy scan, so
        // this is a pointer comparison; while EnemySync is off the pointer is null and
        // nothing changes.  Requiring a ProjectileMovement component is what keeps a
        // flood of enemy-owned VFX actors out of the effect lane.
        bool enemyShot = Cfg().enemy_sync && g_enemyBaseCls &&
                         ((owner && DerivesFrom(ObjClass(owner), g_enemyBaseCls)) ||
                          (inst && DerivesFrom(ObjClass(inst), g_enemyBaseCls))) &&
                         ActorObjProp(a, "ProjectileMovement") != nullptr;
        if (!enemyShot && !IsProjectileClass(ObjClass(a))) continue;
        ActorXform x;
        if (!ReadActorXform(a, &x)) continue;
        ActorXform px;
        bool closeBy = ReadActorXform(g_localPawn, &px) &&
                    fabsf(px.px - x.px) + fabsf(px.py - x.py) + fabsf(px.pz - x.pz) < 600.0f;
        bool mine = owner == g_localPawn || inst == g_localPawn || (!owner && !inst && closeBy);
        if (!mine && !enemyShot) continue;
        wire::EffectBody e;
        memset(&e, 0, sizeof(e));
        e.kind = 1;
        if (avatar::AssetPath(ObjClass(a), e.cls, sizeof(e.cls)) <= 0) continue;
        float v[3] = {0, 0, 0};
        ProjectileVelocity(a, v);
        e.px = wire::QuantizePos(x.px); e.py = wire::QuantizePos(x.py); e.pz = wire::QuantizePos(x.pz);
        e.pitch = wire::QuantizeAngle(x.pitch); e.yaw = wire::QuantizeAngle(x.yaw);
        e.vx = wire::QuantizePos(v[0]); e.vy = wire::QuantizePos(v[1]); e.vz = wire::QuantizePos(v[2]);
        e.life_ms = 6000;
        e.id = g_fxNextId++;
        if (!g_fxNextId) g_fxNextId = 1;
        QDLOG(LOG_INFO, "effect: local #", dec(e.id), " ", e.cls, " v=", flt(v[0], 0), ",", flt(v[1], 0), ",", flt(v[2], 0));
        g_fxLastLocal = a;
        int slot = 0;
        for (int i = 0; i < 8; ++i) if (!g_fxTrack[i].actor) { slot = i; break; }
        g_fxTrack[slot].actor = a; g_fxTrack[slot].id = e.id;
        g_fxTrack[slot].x = x.px; g_fxTrack[slot].y = x.py; g_fxTrack[slot].z = x.pz;
        FxSend(e);
    }
}

// ---------------------------------------------------------------------------
// Niagara on the body: melee swing arcs and status effects
// ---------------------------------------------------------------------------
// UNiagaraFunctionLibrary::SpawnSystemAttached, called through reflection on the
// library's CDO.  Location/rotation are relative to `parent` (KeepRelativeOffset).
void* NiagaraSpawnAttached(void* system, void* parent, const float loc[3], const float rot[3], bool autoDestroy)
{
    static void* lib = nullptr;
    static Fn f = {};
    if (!system || !parent) return nullptr;
    if (!lib || !ObjectIsLive(lib)) lib = FindObjectByNameAndClass("Default__NiagaraFunctionLibrary", "NiagaraFunctionLibrary");
    if (!lib || !f.Resolve(lib, "SpawnSystemAttached")) { QDLOG(LOG_WARN, "niagara: SpawnSystemAttached unavailable"); return nullptr; }
    Call c(f);
    c.Obj("SystemTemplate", system);
    c.Obj("AttachToComponent", parent);
    QdName none = {0, 0};
    c.Name("AttachPointName", none);
    c.Set("Location", loc, 12);
    c.Set("Rotation", rot, 12);              // FRotator: pitch, yaw, roll
    c.U8("LocationType", 0);                 // EAttachLocation::KeepRelativeOffset
    c.U8("bAutoDestroy", autoDestroy ? 1 : 0);
    c.U8("bAutoActivate", 1);
    c.U8("PoolingMethod", 0);                // ENCPoolMethod::None
    c.U8("bPreCullCheck", 0);
    if (!c.Run(lib) || !c.Ret()) return nullptr;
    return *(void**)c.Ret();
}

bool NiagaraSetColor(void* comp, const char* name, const float rgba[4])
{
    static Fn f = {};
    if (!comp || !f.Resolve(comp, "SetVariableLinearColor")) return false;
    QdName n;
    if (!FNameFromAnsi(name, &n)) return false;
    Call c(f);
    c.Name("InVariableName", n);
    c.Set("InValue", rgba, 16);
    return c.Run(comp);
}

bool NiagaraSetFloat(void* comp, const char* name, float v)
{
    static Fn f = {};
    if (!comp || !f.Resolve(comp, "SetVariableFloat")) return false;
    QdName n;
    if (!FNameFromAnsi(name, &n)) return false;
    Call c(f);
    c.Name("InVariableName", n);
    c.F32("InValue", v);
    return c.Run(comp);
}

// Stop emitting; with bAutoDestroy the component removes itself once its particles die.
void NiagaraStop(void* comp)
{
    static Fn f = {};
    if (comp && ObjectIsLive(comp) && f.Resolve(comp, "Deactivate")) { Call c(f); c.Run(comp); }
}

// The user parameters a Blueprint set on a component (UNiagaraComponent::
// OverrideParameters, an FNiagaraParameterStore): name, type, value.  Reflection gives
// every offset; only the element stride is derived (Offset is the last field).
struct NiagaraParam { char name[64]; char type[48]; float v[4]; int n; };

int NiagaraUserParams(void* comp, NiagaraParam* out, int cap)
{
    static int ovr = -2, sorted = -1, data = -1, nameOff = -1, typeOff = -1, offOff = -1, stride = 0;
    if (ovr == -2) {
        ovr = FindPropertyOffsetIn(ObjClass(comp), "OverrideParameters");
        void* store = FindObjectByNameAndClass("NiagaraParameterStore", "ScriptStruct");
        void* elem = FindObjectByNameAndClass("NiagaraVariableWithOffset", "ScriptStruct");
        void* base = FindObjectByNameAndClass("NiagaraVariableBase", "ScriptStruct");
        void* td = FindObjectByNameAndClass("NiagaraTypeDefinition", "ScriptStruct");
        if (store) { sorted = FindPropertyOffsetIn(store, "SortedParameterOffsets"); data = FindPropertyOffsetIn(store, "ParameterData"); }
        if (elem) offOff = FindPropertyOffsetIn(elem, "Offset");
        if (base) { nameOff = FindPropertyOffsetIn(base, "Name"); typeOff = FindPropertyOffsetIn(base, "TypeDef"); }
        int cso = td ? FindPropertyOffsetIn(td, "ClassStructOrEnum") : -1;
        if (typeOff >= 0 && cso >= 0) typeOff += cso;
        stride = offOff >= 0 ? ((offOff + 4 + 7) & ~7) : 0;
        QDLOG(LOG_INFO, "niagara params: Override@", dec(ovr), " Sorted@", dec(sorted), " Data@", dec(data),
              " name@", dec(nameOff), " type@", dec(typeOff), " offset@", dec(offOff), " stride ", dec(stride));
    }
    // 4.27 keeps a type HANDLE (an index into the type registry) instead of the type
    // object; without it the type is inferred from the user's own parameter name.
    if (ovr < 0 || sorted < 0 || data < 0 || nameOff < 0 || offOff < 0 || !stride) return 0;
    uint8_t* st = (uint8_t*)comp + ovr;
    uint8_t* arr = *(uint8_t**)(st + sorted);
    int num = *(int*)(st + sorted + 8);
    uint8_t* bytes = *(uint8_t**)(st + data);
    int nbytes = *(int*)(st + data + 8);
    int got = 0;
    for (int i = 0; arr && bytes && i < num && i < 64 && got < cap; ++i) {
        uint8_t* e = arr + i * stride;
        NiagaraParam& p = out[got];
        memset(&p, 0, sizeof(p));
        if (FNameToString(*(QdName*)(e + nameOff), p.name, sizeof(p.name)) <= 0) continue;
        if (typeOff >= 0) {
            void* t = *(void**)(e + typeOff);
            if (t) FNameToString(ObjNameRaw(t), p.type, sizeof(p.type));
            p.n = avatar::HasCI(p.type, "linearcolor") ? 4 : (avatar::HasCI(p.type, "float") ? 1 : 0);
        } else {
            lstrcpynA(p.type, "?", sizeof(p.type));
            p.n = (avatar::HasCI(p.name, "color") || avatar::HasCI(p.name, "colour")) ? 4 : 1;
        }
        int off = *(int*)(e + offOff);
        if (p.n && off >= 0 && off + p.n * 4 <= nbytes) memcpy(p.v, bytes + off, p.n * 4);
        else p.n = 0;
        ++got;
    }
    return got;
}

// ---- melee swing arc (EFFECT kind 4, WIRE.md 3.14) --------------------------
// The game spawns the weapon's arc (MeleeArc_Default, or the weapon's override) as a
// Niagara component attached to the first-person ARMS ~0.2 s into a swing, colours it
// with LinearColor user parameters and orients it by its relative rotation (the roll
// is the swing direction).  The sender reports each new arc; the peer spawns the same
// system on the body at eye height with the same relative rotation and colours.
// Field use: cls = "<system>|<param>=<v>[,<v>,<v>,<v>]|..." (values x1000, integers;
// four values = a LinearColor, one = a float); id = pitch, pitch = roll, yaw = yaw
// (quantized degrees, relative to the arms); positions and velocities unused.

void* g_arcSeen[16];
int g_arcSeenN = 0;

void ArcObservePump(uint32_t now)
{
    static uint32_t atkAt = 0;
    static uint8_t lastAct = 0;
    static int armsOff = -2, kids = -2;
    static int32_t ncName = -1;
    if (!IsGameplayPawn(g_localPawn)) return;
    // Watch while a move plays and 600 ms after (a slow frame rate stretches a swing
    // in wall time: measured 2.6 s for one swing on a throttled test instance).
    if (g_localAct.byte != lastAct) {
        lastAct = g_localAct.byte;
        if ((lastAct & 0x1F) != 0) atkAt = now;
    }
    if (g_localAct.last && atkAt) atkAt = now;
    if (!atkAt) return;
    if (now - atkAt > 600) { atkAt = 0; return; }
    avatar::Api& a = avatar::A();
    if (armsOff == -2) {
        armsOff = FindPropertyOffsetIn(ObjClass(g_localPawn), "Arms");
        kids = a.clsScene ? FindPropertyOffsetIn(a.clsScene, "AttachChildren") : -1;
        QdName n;
        ncName = FNameFromAnsi("NiagaraComponent", &n) ? (int32_t)n.comparison_index : -1;
    }
    if (armsOff < 0 || kids < 0 || ncName < 0 || g_eqs.relRot < 0) return;
    void* arms = *(void**)((uint8_t*)g_localPawn + armsOff);
    if (!arms) return;
    void** ch = *(void***)((uint8_t*)arms + kids);
    int nch = *(int*)((uint8_t*)arms + kids + 8);
    for (int i = 0; ch && i < nch && i < 64; ++i) {
        void* c = ch[i];
        if (!c || (int32_t)ObjNameRaw(ObjClass(c)).comparison_index != ncName) continue;
        bool seen = false;
        for (int k = 0; k < 16; ++k) if (g_arcSeen[k] == c) { seen = true; break; }
        if (seen) continue;
        g_arcSeen[g_arcSeenN] = c;
        g_arcSeenN = (g_arcSeenN + 1) & 15;
        static int assetOff = -2;
        if (assetOff == -2) assetOff = FindPropertyOffsetIn(ObjClass(c), "Asset");
        wire::EffectBody e;
        memset(&e, 0, sizeof(e));
        e.kind = 4;
        if (assetOff < 0 || avatar::AssetPath(*(void**)((uint8_t*)c + assetOff), e.cls, 110) <= 0) continue;
        NiagaraParam ps[16];
        int np = NiagaraUserParams(c, ps, 16);
        // Every user parameter the Blueprint set, as text: "|name=v[,v,v,v]".  Colours
        // carry four values; anything else is read as a float (4.27 stores no type).
        for (int k = 0; k < np; ++k) {
            int nv = ps[k].n == 4 ? 4 : 1;
            if (ps[k].n == 0) {
                // Not a colour by name: a float (Lifetime, Power, Charge on the default arc).
                ps[k].n = 1;
            }
            char item[96];
            int w = wsprintfA(item, "|%s=", ps[k].name);
            for (int j = 0; j < nv; ++j) {
                int milli = (int)(ps[k].v[j] * 1000.0f);
                w += wsprintfA(item + w, j ? ",%d" : "%d", milli);
            }
            if (lstrlenA(e.cls) + w >= (int)sizeof(e.cls) - 1) break;
            lstrcatA(e.cls, item);
        }
        const float* r = (const float*)((uint8_t*)c + g_eqs.relRot);
        e.id = (uint16_t)wire::QuantizeAngle(r[0]);
        e.pitch = wire::QuantizeAngle(r[2]);
        e.yaw = wire::QuantizeAngle(r[1]);
        QDLOG(LOG_INFO, "arc: local ", e.cls, " rot=", flt(r[0], 1), ",", flt(r[1], 1), ",", flt(r[2], 1),
              " params=", dec(np), " +", dec((int)(now - atkAt)), "ms");
        FxSend(e);
    }
}

avatar::Body* g_devMirrorBody = nullptr;

void SpawnMeleeArc(const wire::EffectBody& e, avatar::Body* b)
{
    if (!b || !b->pose) return;
    char parts[8][200];
    memset(parts, 0, sizeof(parts));
    int pi = 0, w = 0;
    for (const char* p = e.cls; *p && pi < 8; ++p) {
        if (*p == '|') { ++pi; w = 0; continue; }
        if (w < 199) parts[pi][w++] = *p;
    }
    // A flurry sends one thrust arc per stab; the body stabs with each.
    if (avatar::HasCI(parts[0], "thrust")) avatar::Jab(b);
    void* sys = avatar::LoadAsset(parts[0]);
    if (!sys) return;
    // The arms' frame is the body mesh's frame (Y forward, X left, Z up), so the arc
    // keeps its relative rotation; it sits at eye height instead of at the camera.
    float loc[3] = {0.0f, 12.0f, 150.0f};
    float rot[3] = {wire::DequantizeAngle((int16_t)e.id), wire::DequantizeAngle(e.yaw), wire::DequantizeAngle(e.pitch)};
    void* c = NiagaraSpawnAttached(sys, b->pose, loc, rot, true);
    if (!c) { QDLOG(LOG_WARN, "arc: spawn failed for ", parts[0]); return; }
    int set = 0;
    for (int k = 1; k < 8 && parts[k][0]; ++k) {
        char* eq = strchr(parts[k], '=');
        if (!eq) continue;
        *eq = 0;
        float v[4] = {0, 0, 0, 1};
        int nv = 0;
        for (char* t = eq + 1; *t && nv < 4;) {
            v[nv++] = atoi(t) / 1000.0f;
            while (*t && *t != ',') ++t;
            if (*t == ',') ++t;
        }
        bool ok = nv == 4 ? NiagaraSetColor(c, parts[k], v) : NiagaraSetFloat(c, parts[k], v[0]);
        set += ok ? 1 : 0;
    }
    QDLOG(LOG_INFO, "arc: shown ", parts[0], " roll=", flt(rot[2], 0), " params set ", dec(set));
}

// ---- status effects (STATUS, WIRE.md 3.15) ------------------------------------
// Read from the player's own components: StatusEffectComponent_C.Applied for the
// timed effects, MagicFireEffectLogic_C.CurrentMagicFireDamagePool > 0 for burning,
// MagicPoisonDebuffLogic_C.Active for magic poison.  Shown on the body with the
// systems the game puts on ENEMIES for the same effects (EnemyBase_C references them;
// they sample the attach parent's skinned mesh, so they wrap the body).
struct StatusSrc { const char* comp; uint32_t bit; };
static const StatusSrc kStatusSrc[] = {
    {"PoisonStatusEffect", wire::ST_POISON},       {"DiseaseStatusEffect", wire::ST_DISEASE},
    {"WeakenStatusEffect", wire::ST_WEAKEN},       {"HeavyStatusEffect", wire::ST_HEAVY},
    {"BindingStatusEffect", wire::ST_BINDING},     {"BerserkFruitStatusEffect", wire::ST_BERSERK},
    {"BerserkPotionStatusEffect", wire::ST_BERSERK}, {"IronskinFruitStatusEffect", wire::ST_IRONSKIN},
    {"IronskinPotionStatusEffect", wire::ST_IRONSKIN}, {"RadiantFruitStatusEffect", wire::ST_RADIANT},
};
static const int kStatusSrcN = (int)(sizeof(kStatusSrc) / sizeof(kStatusSrc[0]));

// bit index -> what the body shows.  `ground`: the system the game puts on an ENEMY
// with this effect; it samples a skeletal mesh, which the poseable body is not, so it
// only draws at the feet (measured) -- kept for the ground glow.  `body`: a system that
// needs no mesh, attached at four bones and scaled, so the effect covers the body.
// Chosen on screenshots (_test/v-gallery*.png): magic fire is the blue brazier flame,
// poison the poison fishman's projectile wisps, magic poison its magenta cloud.
struct StatusLook { const char* ground; const char* body; float scale; };
static const StatusLook kStatusFx[10] = {
    {"/Game/Items/Weapons/Torch/Fire_CharFX.Fire_CharFX",
     "/Game/Enemies/Gargler_00/Gargoyle_SmallBrazierFire.Gargoyle_SmallBrazierFire", 0.35f},      // magic fire
    {"/Game/Enemies/Shared/Poison_CharFX.Poison_CharFX",
     "/Game/Enemies/PoisonFishMan/MagicPoisonFishman_ProjectileFX.MagicPoisonFishman_ProjectileFX", 0.6f}, // magic poison
    {"/Game/Enemies/Shared/Poison_CharFX.Poison_CharFX",
     "/Game/Enemies/PoisonFishMan/PoisonFishman_ProjectilFX.PoisonFishman_ProjectilFX", 0.7f},    // poison
    // No stationary disease / weaken aura exists in the game's content (the dagger
    // trails emit only while moving; v-gallery2.png): the enemy effect alone.
    {"/Game/Enemies/Shared/DiseaseCharFX.DiseaseCharFX", "", 1.0f},                                // disease
    {"/Game/Enemies/Shared/WeakenCharFX.WeakenCharFX", "", 1.0f},                                  // weaken
    {"/Game/Enemies/Shared/Heavy_CharFX.Heavy_CharFX", "", 1.0f},                                  // heavy
    {"/Game/Items/Weapons/Daggers/Paralyze/Paralyze_CharFX.Paralyze_CharFX", "", 1.0f},            // binding
    {"", "", 1.0f}, {"", "", 1.0f}, {"", "", 1.0f},       // berserk, ironskin, radiant: screen-only in the game
};

uint32_t LocalStatusMask()
{
    static void* pawn = nullptr;
    static void* comps[16];
    static void* fireLogic = nullptr;
    static void* poisonLogic = nullptr;
    static int applied = -1, pool = -1, active = -1;
    if (!IsGameplayPawn(g_localPawn)) return 0;
    if (pawn != g_localPawn) {
        pawn = g_localPawn;
        memset(comps, 0, sizeof(comps));
        fireLogic = poisonLogic = nullptr;
        int n = ObjectCount();
        for (int i = 0; i < n; ++i) {
            void* o = ObjectAt(i);
            if (!o || ObjOuter(o) != pawn) continue;
            for (int k = 0; k < kStatusSrcN; ++k) if (ObjNameIs(o, kStatusSrc[k].comp)) comps[k] = o;
            if (ObjNameIs(ObjClass(o), "MagicFireEffectLogic_C")) fireLogic = o;
            if (ObjNameIs(ObjClass(o), "MagicPoisonDebuffLogic_C")) poisonLogic = o;
        }
        for (int k = 0; k < kStatusSrcN && applied < 0; ++k)
            if (comps[k]) applied = FindPropertyOffsetIn(ObjClass(comps[k]), "Applied");
        if (fireLogic) pool = FindPropertyOffsetIn(ObjClass(fireLogic), "CurrentMagicFireDamagePool");
        if (poisonLogic) active = FindPropertyOffsetIn(ObjClass(poisonLogic), "Active");
        int found = 0;
        for (int k = 0; k < kStatusSrcN; ++k) found += comps[k] ? 1 : 0;
        QDLOG(LOG_INFO, "status: ", dec(found), "/", dec(kStatusSrcN), " effect components, Applied@", dec(applied),
              " fire pool@", dec(pool), " poison active@", dec(active));
    }
    uint32_t m = 0;
    for (int k = 0; k < kStatusSrcN; ++k)
        if (comps[k] && applied >= 0 && ObjectIsLive(comps[k]) && *((uint8_t*)comps[k] + applied)) m |= kStatusSrc[k].bit;
    if (fireLogic && pool >= 0 && ObjectIsLive(fireLogic) && *(float*)((uint8_t*)fireLogic + pool) > 0.0f) m |= wire::ST_MAGIC_FIRE;
    if (poisonLogic && active >= 0 && ObjectIsLive(poisonLogic) && *((uint8_t*)poisonLogic + active)) m |= wire::ST_MAGIC_POISON;
    return m;
}

void ApplyStatus(avatar::Body* b, uint32_t mask)
{
    if (!b || !b->pose || mask == b->statusMask) return;
    uint32_t was = b->statusMask;
    b->statusMask = mask;
    static const char* const kAnchors[4] = {"root_x", "spine_02_x", "forearm_stretch_l", "forearm_stretch_r"};
    for (int bit = 0; bit < 10; ++bit) {
        bool on = (mask & (1u << bit)) != 0;
        // Poison and magic poison share one look: show it once.
        if (bit == 2 && (mask & wire::ST_MAGIC_POISON)) on = false;
        bool had = false;
        for (int k = 0; k < 5; ++k) had = had || b->statusFx[bit][k] != nullptr;
        if (on == had) continue;
        if (!on) {
            for (int k = 0; k < 5; ++k) { NiagaraStop(b->statusFx[bit][k]); b->statusFx[bit][k] = nullptr; }
            continue;
        }
        const StatusLook& lk = kStatusFx[bit];
        float z[3] = {0, 0, 0};
        if (lk.ground[0]) b->statusFx[bit][4] = NiagaraSpawnAttached(avatar::LoadAsset(lk.ground), b->pose, z, z, true);
        void* sys = lk.body[0] ? avatar::LoadAsset(lk.body) : nullptr;
        for (int k = 0; k < 4 && sys; ++k) {
            void* c = NiagaraSpawnAttached(sys, b->pose, z, z, true);
            if (!c) continue;
            QdName bone;
            if (FNameFromAnsi(kAnchors[k], &bone)) avatar::AttachTo(c, b->pose, bone);
            avatar::Xf x = avatar::XfIdentity();
            x.sx = x.sy = x.sz = lk.scale;
            avatar::SetRelativeTransform(c, x);
            b->statusFx[bit][k] = c;
        }
    }
    QDLOG(LOG_INFO, "status: body now shows mask ", hex32(mask), " (was ", hex32(was), ")");
}

uint32_t g_localStatus = 0, g_localStatusRev = 0;
uint32_t g_peerStatus = 0, g_peerStatusRev = 0;

void StatusPump(uint32_t now)
{
    static uint32_t lastScan = 0, lastSend = 0;
    if (now - lastScan < 200) return;
    lastScan = now;
    uint32_t m = LocalStatusMask();
    bool changed = m != g_localStatus;
    if (changed) {
        g_localStatus = m;
        ++g_localStatusRev;
        QDLOG(LOG_INFO, "status: local mask ", hex32(m), " rev ", dec((int)g_localStatusRev));
    }
    if (g_devMirrorBody) ApplyStatus(g_devMirrorBody, g_localStatus);
    if (g_net.Connected() && (changed || now - lastSend > 1000)) {
        lastSend = now;
        uint8_t body[16];
        wire::Writer w(body, sizeof(body));
        wire::WriteStatus(w, g_localStatusRev, g_localStatus);
        if (w.ok) g_net.SendUnreliable(wire::K_STATUS, body, w.len);
    }
}

// ---------------------------------------------------------------------------
// LAYER 6 -- world: level-streaming union, enemies, dialogue
// ---------------------------------------------------------------------------
// Each of the three is behind its own switch (StreamUnion / EnemySync / DialogueSync)
// and each obeys the same two rules as the rest of this file: every pointer read from
// the game is validated before it is followed, and every write is read back and the
// result logged, because "called" is not evidence that anything happened.

// ---- small reflection helpers shared by the three --------------------------
// Property offsets per (class, name), cached: a reflected lookup walks the class's
// whole field chain, and the enemy pump asks the same questions every frame.
struct PropCacheEntry { void* cls; const char* name; int off; };
PropCacheEntry g_propCache[192];
int g_propCacheN = 0;

int PropOff(void* cls, const char* name)
{
    if (!cls) return -1;
    for (int i = 0; i < g_propCacheN; ++i)
        if (g_propCache[i].cls == cls && g_propCache[i].name == name) return g_propCache[i].off;
    int off = FindPropertyOffsetIn(cls, name);
    if (g_propCacheN < (int)(sizeof(g_propCache) / sizeof(g_propCache[0])))
        g_propCache[g_propCacheN++] = {cls, name, off};
    return off;
}

void* ObjPropC(void* obj, const char* name)
{
    if (!obj) return nullptr;
    int off = PropOff(ObjClass(obj), name);
    if (off < 0) return nullptr;
    void* v = *(void**)((uint8_t*)obj + off);
    return LooksLikeUObject(v) ? v : nullptr;
}

// Blueprint bools occupy a whole byte (no bitfield), so nonzero = true.
bool BytePropC(void* obj, const char* name, uint8_t* out)
{
    if (!obj) return false;
    int off = PropOff(ObjClass(obj), name);
    if (off < 0) return false;
    *out = *((uint8_t*)obj + off);
    return true;
}

bool WriteBytePropC(void* obj, const char* name, uint8_t v)
{
    if (!obj) return false;
    int off = PropOff(ObjClass(obj), name);
    if (off < 0) return false;
    *((uint8_t*)obj + off) = v;
    return *((uint8_t*)obj + off) == v;
}

bool IntPropC(void* obj, const char* name, int32_t* out)
{
    if (!obj) return false;
    int off = PropOff(ObjClass(obj), name);
    if (off < 0) return false;
    *out = *(int32_t*)((uint8_t*)obj + off);
    return true;
}

// Float properties are read the same way.  The player has no `GetHP` (its class tag
// list carries `Health`), and the enemy's per-swing multiplier is
// `ActiveHitboxDamageMult`; both are values, not function calls, so they are read
// directly rather than through a getter that may not exist.
bool FloatPropC(void* obj, const char* name, float* out)
{
    if (!obj) return false;
    int off = PropOff(ObjClass(obj), name);
    if (off < 0) return false;
    memcpy(out, (uint8_t*)obj + off, 4);
    return true;
}

// The write side of the same property, read back before reporting success.  Used only by
// the test rig's health floor (QDCOOP_TEST_GOD); no sync lane writes a gameplay scalar.
bool SetFloatPropC(void* obj, const char* name, float v)
{
    if (!obj) return false;
    int off = PropOff(ObjClass(obj), name);
    if (off < 0) return false;
    memcpy((uint8_t*)obj + off, &v, 4);
    float back = 0.0f;
    memcpy(&back, (uint8_t*)obj + off, 4);
    return back == v;
}

// The value a getter produced: its ReturnValue, or -- for a Blueprint function, which
// returns through output parameters -- its first output parameter of the right size.
uint8_t* OutParm(Call& c, int size)
{
    const LayoutResult& s = c.f.sig;
    int ro = c.f.RetOff();
    if (ro >= 0) {
        for (int i = 0; i < s.props; ++i)
            if (s.p[i].offset == ro && s.p[i].size == size) return c.buf + ro;
    }
    for (int i = 0; i < s.props; ++i) {
        if ((s.p[i].flags & 0x100ull) && s.p[i].size == size) return c.buf + s.p[i].offset;  // CPF_OutParm
    }
    return nullptr;
}

bool CallGetFloat(void* obj, Fn& f, const char* fn, float* out)
{
    if (!f.Resolve(obj, fn)) return false;
    Call c(f);
    if (!c.Run(obj)) return false;
    uint8_t* p = OutParm(c, 4);
    if (!p) return false;
    memcpy(out, p, 4);
    return true;
}

bool CallNoArgs(void* obj, Fn& f, const char* fn)
{
    if (!obj || !f.Resolve(obj, fn)) return false;
    Call c(f);          // zeroed parameters: any bool argument is false
    return c.Run(obj);
}

bool CallBool1(void* obj, Fn& f, const char* fn, bool v)
{
    if (!obj || !f.Resolve(obj, fn)) return false;
    Call c(f);
    if (f.sig.props > 0) c.U8(f.sig.p[0].name, v ? 1 : 0);
    return c.Run(obj);
}

int RemoteSlot() { return g_net.IsHost() ? 1 : 0; }
int LocalSlot()  { return g_net.IsHost() ? 0 : 1; }

// ===========================================================================
// 6a. Level-streaming union
// ===========================================================================
// MEASURED (this pass, disassembly of the shipped image):
//
//   * The level manager applies ONE level per frame from its resolved queue
//     (+0x320 -> current entry +0x330, wanted-hidden flag +0x338) and WAITS until the
//     level's real visibility matches (0x029A4310: LoadedLevel +0x128 -> bIsVisible
//     +0x1F4 bit 0x20) before taking the next entry (0x00E3CF9E..0x00E3D0F3).
//   * Its only lever on the engine is ULevelStreaming::SetShouldBeVisible 0x029ACB20
//     (bShouldBeVisible = +0xBA bit 3), called from the per-frame path (0x00E3CFE1 /
//     0x00E3CFFA) and the immediate path 0x00E21E40.
//
// So the union is enforced at that one setter: a hide of a level the partner needs is
// vetoed.  A bare veto would STALL the queue forever (the manager waits for a hide
// that never happens, and the local player's next level never loads), so the veto
// also flips the manager's pending entry to "show": next frame the manager re-issues
// it as a show, finds the level visible, and moves on.  One frame, no deadlock.
//
// THE UNLOAD PATH IS GUARDED TOO (measured this pass, disassembly of the shipped image):
//
//   * `ULevelStreaming::SetShouldBeLoaded` is virtual.  Its UFunction thunk
//     (0x02DC8CF0, pair table 0x03FFE340) parses the bool and calls [vtable+0x270].
//   * The level-streaming vtable is 0x03E72448: ProcessEvent sits at its +0x220
//     (0x011A7AA0), the family's +0x0A0/+0x160 slots are 0x029AC610/0x029A2D90, and
//     +0x270 is 0x029ACAC0 -- a byte +0xBA bit 4 setter with exactly the shape of
//     SetShouldBeVisible (bit 3, 0x029ACB20), including the same tail call to
//     UWorld::UpdateStreamingLevelShouldBeConsidered 0x02D4A0E0.
//   * The neighbouring slot +0x278 (0x029ACF70) is ShouldBeLoaded and reads that
//     same +0xBA bit 4, which is what pins the bit.
//   * The three sibling family vtables (0x03E71328, 0x03E718C8, 0x03E71B58) override
//     +0x270 with a bare `ret` (0x007746E0): those classes deliberately ignore
//     SetShouldBeLoaded, so the one honouring body is the one hooked here.
//
// What is PUBLISHED is what the local game wants (recorded from the same setter), never
// what this mod forced -- otherwise the two machines would hold each other's levels up
// forever.  Levels the partner needs that are not shown here are shown by this mod
// (SetShouldBeLoaded + SetShouldBeVisible) and released 5 s after the partner leaves
// them (stream::ForceBook).
typedef void (*FnSetShouldBeVisible)(void* levelStreaming, bool visible);
typedef void (*FnSetShouldBeLoaded)(void* levelStreaming, bool loaded);
FnSetShouldBeVisible g_oSetVis = nullptr;
FnSetShouldBeLoaded  g_oSetLoaded = nullptr;
stream::ForceBook    g_force;

static const int kLevelVisByte = 0xBA;     // ULevelStreaming flags byte (setter bytes verify it)
static const int kLevelVisBit  = 0x08;     // bShouldBeVisible
static const int kLevelLoadBit = 0x10;     // bShouldBeLoaded (ShouldBeLoaded +0x278 reads it)
static const int kMgrSlot      = 0x3B0;    // GameInstance -> AFS_LevelManager (0x00E2EEAC)
static const int kMgrCurEntry  = 0x330;    // {X*, u8 hide}; X + 0x30 = ULevelStreaming*
static const int kMgrCurHide   = 0x338;

struct LevelRec {
    void*  ls;
    char   name[stream::kMaxNameLen];
    int8_t gameWants;      // -1 unknown, else the game's last request
    bool   skipNextShow;   // the manager is about to re-issue our flipped entry
};
LevelRec g_levels[96];
int      g_levelN = 0;
void*    g_levelWorld = nullptr;
bool     g_ownVisCall = false;
uint32_t g_vetoCount = 0;

bool LevelVisBitGuarded(void* ls, bool* out)
{
    __try { *out = (*((uint8_t*)ls + kLevelVisByte) & kLevelVisBit) != 0; return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool LevelVisible(void* ls)
{
    bool v = false;
    return ls && LevelVisBitGuarded(ls, &v) && v;
}

bool LevelLoadBitGuarded(void* ls, bool* out)
{
    __try { *out = (*((uint8_t*)ls + kLevelVisByte) & kLevelLoadBit) != 0; return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool LevelLoaded(void* ls)
{
    bool v = false;
    return ls && LevelLoadBitGuarded(ls, &v) && v;
}

LevelRec* LevelFind(void* ls)
{
    for (int i = 0; i < g_levelN; ++i) if (g_levels[i].ls == ls) return &g_levels[i];
    return nullptr;
}

// Once per key, not once per frame.  LevelsRefresh creates each entry once, but a
// world change rebuilds the table and would otherwise repeat every line on every
// transition.  The cache is deliberately small: past it the log is allowed to repeat
// rather than allocate.
bool SameNameText(const char* a, const char* b)
{
    int i = 0;
    for (; a[i] || b[i]; ++i) if (a[i] != b[i]) return false;
    return true;
}

bool RouteOnce(char keys[][160], int& n, const char* key)
{
    for (int i = 0; i < n; ++i) {
        int k = 0;
        for (; keys[i][k] && keys[i][k] == key[k]; ++k) {}
        if (!keys[i][k] && !key[k]) return false;
    }
    if (n < 64) {
        int k = 0;
        for (; key[k] && k < 159; ++k) keys[n][k] = key[k];
        keys[n][k] = 0;
        ++n;
    }
    return true;
}

void LogLevelNameOnce(const char* name, const char* source)
{
    static char seen[64][160];
    static int  n = 0;
    if (!RouteOnce(seen, n, name)) return;
    QDLOG(LOG_INFO, "level name: ", name, " from ", source);
}

void LogLevelNameRefusedOnce(const char* objectName)
{
    static char seen[64][160];
    static int  n = 0;
    char key[160];
    int k = 0;
    for (; objectName[k] && k < 159; ++k) key[k] = objectName[k];
    key[k] = 0;
    if (!RouteOnce(seen, n, key)) return;
    QDLOG(LOG_WARN, "level name REFUSED for streaming object ",
          (key[0] ? key : "?"), ": no plausible package or object name; it is not "
          "published to the union (the partner would protect a level that does not exist)");
}

void LogLevelNameDisagreeOnce(const char* wa, const char* pk)
{
    static char seen[64][160];
    static int  n = 0;
    char key[160];
    int k = 0;
    for (; wa[k] && k < 158; ++k) key[k] = wa[k];
    key[k++] = '|';
    for (int j = 0; pk[j] && k < 159; ++j) key[k++] = pk[j];
    key[k] = 0;
    if (!RouteOnce(seen, n, key)) return;
    QDLOG(LOG_WARN, "level name: WorldAsset says '", wa, "' but PackageNameToLoad says '",
          pk, "'; using PackageNameToLoad (the soft path at +0x10 is unverified)");
}

// "/Game/.../100_GR.100_GR" from WorldAsset (TSoftObjectPtr<UWorld>: the
// FSoftObjectPath's AssetPathName is at +0x10, after the weak pointer and the tag),
// else PackageNameToLoad, else the object's own name.  The first candidate that
// yields a PLAUSIBLE short name wins; a wrong name is worse than an empty one because
// it makes the partner protect a level that does not exist.  Which source won is
// logged once per name -- the soft path is unverified, so a live run must be able to
// see whether `WorldAsset` or the fallback is doing the work.
void LevelNameOf(void* ls, char* out, int cap)
{
    if (!out || cap <= 0) return;
    out[0] = 0;
    char path[256];
    char wa[stream::kMaxNameLen] = {0};
    char pk[stream::kMaxNameLen] = {0};
    char obj[stream::kMaxNameLen] = {0};
    void* cls = ObjClass(ls);

    int wo = PropOff(cls, "WorldAsset");
    if (wo >= 0 && FNameToString(*(QdName*)((uint8_t*)ls + wo + 0x10), path, sizeof(path)) > 0 &&
        path[0] == '/') {
        stream::ShortLevelName(path, wa, sizeof(wa));
        if (!stream::PlausibleLevelName(wa)) wa[0] = 0;
    }
    int po = PropOff(cls, "PackageNameToLoad");
    if (po >= 0 && FNameToString(*(QdName*)((uint8_t*)ls + po), path, sizeof(path)) > 0) {
        stream::ShortLevelName(path, pk, sizeof(pk));
        if (!stream::PlausibleLevelName(pk)) pk[0] = 0;
    }

    const char* chosen = nullptr;
    const char* source = nullptr;
    if (wa[0] && pk[0] && !SameNameText(wa, pk)) {
        LogLevelNameDisagreeOnce(wa, pk);
        chosen = pk;
        source = "PackageNameToLoad (WorldAsset disagreed)";
    } else if (wa[0]) {
        chosen = wa;
        source = "WorldAsset";
    } else if (pk[0]) {
        chosen = pk;
        source = "PackageNameToLoad";
    } else if (FNameToString(ObjNameRaw(ls), obj, sizeof(obj)) > 0 &&
               stream::PlausibleLevelName(obj)) {
        chosen = obj;
        source = "object name";
    }

    if (!chosen) {
        if (!obj[0]) FNameToString(ObjNameRaw(ls), obj, sizeof(obj));
        LogLevelNameRefusedOnce(obj);
        return;
    }
    int i = 0;
    for (; chosen[i] && i < cap - 1; ++i) out[i] = chosen[i];
    out[i] = 0;
    LogLevelNameOnce(out, source);
}

void* LevelManager()
{
    // Guarded, because this runs from inside the game's own level-streaming call, and a
    // world teardown (death -> reload, a travel, a level change) can leave the world's
    // GameInstance half-built: measured as an EXCEPTION_ACCESS_VIOLATION reading
    // 0x0000000100000000 with WINMM+0x27647 -- this function -- called from the veto, and
    // it took the game down on Continue from the game-over screen.
    __try {
        void* gi = GameInstanceOf(GWorldPtr());
        if (!gi) return nullptr;
        void* m = *(void**)((uint8_t*)gi + kMgrSlot);
        return LooksLikeUObject(m) ? m : nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// The veto's other half (see above).  True if the manager's pending entry was this
// level's hide and is now a show.
bool FlipPendingHideGuarded(void* mgr, void* ls)
{
    __try {
        uint8_t* cur = *(uint8_t**)((uint8_t*)mgr + kMgrCurEntry);
        if (!cur || *(void**)(cur + 0x30) != ls) return false;
        uint8_t* hide = (uint8_t*)mgr + kMgrCurHide;
        if (*hide == 0) return false;
        *hide = 0;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool StreamVetoes(const LevelRec* r)
{
    if (!r || !r->name[0] || !Cfg().stream_union || !g_net.Connected()) return false;
    uint32_t now = Net::Now();
    // While the machines are in different worlds the partner's `PeerWants` answer is
    // about another world's name.  Refusing to veto there is the same rule as
    // refusing to apply its union: the local game owns the decision again.
    return g_map.PeerStateApplies(now) && g_stream.PeerWants(r->name, now);
}

void HK_SetShouldBeVisible(void* ls, bool visible)
{
    if (!g_ownVisCall && ls && g_engineReady) {
        LevelRec* r = LevelFind(ls);
        if (r) {
            if (visible && r->skipNextShow) r->skipNextShow = false;
            else r->gameWants = visible ? 1 : 0;
            if (!visible && StreamVetoes(r) && LevelVisible(ls)) {
                void* mgr = LevelManager();
                if (mgr && FlipPendingHideGuarded(mgr, ls)) r->skipNextShow = true;
                ++g_vetoCount;
                static uint32_t lastLog = 0;
                uint32_t now = Net::Now();
                if (now - lastLog > 1000) {
                    lastLog = now;
                    QDLOG(LOG_INFO, "BLOCK hide of ", r->name, " (the partner is in it); vetoes=",
                          dec((int)g_vetoCount), (r->skipNextShow ? " queue entry flipped" : ""));
                }
                return;
            }
        }
    }
    if (g_oSetVis) g_oSetVis(ls, visible);
}

// The unload half of the same guard.  This is the failure the detector would have
// converted into a log: if the manager (or a Blueprint UnloadStreamLevel) calls
// SetShouldBeLoaded(false) on a level the partner is standing in, the union is
// defeated and the partner falls through the world.  The veto keeps the level loaded
// and, when the manager's pending entry is this level, flips that entry to "show" so
// the queue does not wait for an unload that will never happen.
void HK_SetShouldBeLoaded(void* ls, bool loaded)
{
    if (ls && g_engineReady && !loaded) {
        LevelRec* r = LevelFind(ls);
        if (r && StreamVetoes(r) && LevelLoaded(ls)) {
            void* mgr = LevelManager();
            bool flipped = mgr && FlipPendingHideGuarded(mgr, ls);
            if (flipped) r->skipNextShow = true;
            ++g_vetoCount;
            static uint32_t lastLog = 0;
            uint32_t now = Net::Now();
            if (now - lastLog > 1000) {
                lastLog = now;
                QDLOG(LOG_INFO, "BLOCK unload of ", r->name,
                      " (the partner is in it); vetoes=", dec((int)g_vetoCount),
                      (flipped ? " queue entry flipped" : ""));
            }
            return;
        }
    }
    if (g_oSetLoaded) g_oSetLoaded(ls, loaded);
}

void LevelsReset()
{
    g_levelN = 0;
    g_force.Reset();
    g_stream.Reset();
}

// world->StreamingLevels (reflected UWorld property), merged into g_levels so the
// recorded wishes survive a refresh.
void LevelsRefresh()
{
    void* world = GWorldPtr();
    if (world != g_levelWorld) {
        if (g_levelWorld) QDLOG(LOG_INFO, "level union: world changed, level table reset");
        g_levelWorld = world;
        LevelsReset();
    }
    if (!world || !EnsureActorApi()) return;
    int off = PropOff(ObjClass(world), "StreamingLevels");
    if (off < 0) {
        static bool said = false;
        if (!said) { said = true; QDLOG(LOG_WARN, "level union: UWorld.StreamingLevels not found"); }
        return;
    }
    void** data = *(void***)((uint8_t*)world + off);
    int32_t num = *(int32_t*)((uint8_t*)world + off + 8);
    if (!data || num < 0 || num > 512) return;
    for (int i = 0; i < num; ++i) {
        void* ls = data[i];
        if (!LooksLikeUObject(ls) || LevelFind(ls)) continue;
        if (g_levelN >= (int)(sizeof(g_levels) / sizeof(g_levels[0]))) break;
        LevelRec& r = g_levels[g_levelN++];
        r.ls = ls;
        LevelNameOf(ls, r.name, sizeof(r.name));
        r.gameWants = LevelVisible(ls) ? 1 : 0;    // until the game says otherwise
        r.skipNextShow = false;
    }
    // Drop levels the world no longer lists.
    for (int i = 0; i < g_levelN;) {
        bool listed = false;
        for (int k = 0; k < num; ++k) if (data[k] == g_levels[i].ls) { listed = true; break; }
        if (listed) { ++i; continue; }
        g_levels[i] = g_levels[--g_levelN];
    }
}

// A level name that is never a place the partner can be standing.  `VideoConfigMap` is
// the game's video/config map and `*_ShaderCompile` is a warm-up level; neither is
// gameplay, so forcing either visible buys the union nothing -- and one of them kills the
// game.  MEASURED (Session 6): the peer process died 0.5 s after
// `level union: SHOW VideoConfigMap -> bShouldBeVisible=1` during a world load, with the
// process gone and no further log line; the same force had been harmless in an earlier
// run, which is what makes this a race rather than a rule.
bool LevelNotForcible(const char* n)
{
    if (!n || !n[0]) return false;
    return enemyown::HasCI(n, "VideoConfig") || enemyown::HasCI(n, "ShaderCompile");
}

void ForceLevel(LevelRec& r, bool show)
{
    static Fn fLoad = {};
    if (show) CallBool1(r.ls, fLoad, "SetShouldBeLoaded", true);
    // Release is VISIBILITY ONLY: this path never calls SetShouldBeLoaded(false), so a
    // forced level stays loaded after the partner leaves it.  That is the deliberate
    // trade-off -- the level's package and actors stay resident (memory) in exchange
    // for no re-load and no re-spawn when the partner walks back across the boundary,
    // which is the churn the union exists to prevent.  It is not changed here because
    // the measured manager path only ever calls SetShouldBeVisible (0x00E21E40 and
    // 0x00E3CF9E), so there is no engine unload to piggy-back on; forcing one would be
    // a new write path racing the manager.  If a live run shows the game re-hiding
    // these on its own schedule, the release can then be dropped entirely.
    g_ownVisCall = true;
    if (g_oSetVis) g_oSetVis(r.ls, show);
    g_ownVisCall = false;
    QDLOG(LOG_INFO, "level union: ", (show ? "SHOW " : "release "), r.name,
          " -> bShouldBeVisible=", dec(LevelVisible(r.ls) ? 1 : 0),
          (show && !fLoad.fn ? " (SetShouldBeLoaded not found)" : ""));
}

void SendLevelUnion(const char* const* names, int n)
{
    uint8_t bits[wire::kLevelUnionMaxNames / 8] = {0};
    for (int i = 0; i < n; ++i) bits[i / 8] |= (uint8_t)(1u << (i % 8));
    uint8_t body[wire::kMaxBody];
    for (;;) {
        wire::Writer w(body, sizeof(body));
        wire::WriteLevelUnion(w, bits, (uint16_t)((n + 7) / 8), names, (uint16_t)n);
        if (w.ok) { g_net.SendUnreliable(wire::K_LEVEL_UNION, body, w.len); return; }
        if (n == 0) return;
        // Over the body cap: drop the tail and say so -- a partner then lacks a level,
        // which the log must make visible rather than silent.
        --n;
        static bool said = false;
        if (!said) { said = true; QDLOG(LOG_WARN, "level union: list truncated to fit one datagram"); }
    }
}

void StreamPump(uint32_t now)
{
    if (!Cfg().stream_union) return;
    static uint32_t lastRefresh = 0, lastApply = 0, lastSend = 0, lastHash = 0;
    if (now - lastRefresh >= 1000) { lastRefresh = now; LevelsRefresh(); }
    if (now - lastApply < 500) return;
    lastApply = now;

    // A hard transition on either machine makes the partner's list refer to another
    // world.  Drop the slot rather than merely ignoring new packets: an old list would
    // otherwise keep holding levels visible here for its 30 s staleness window, and a
    // same-named sublevel in this world is exactly the thing that must not be held up.
    const bool worldsAgree = g_map.PeerStateApplies(now);
    if (!worldsAgree) g_stream.OnPeerGone((uint8_t)RemoteSlot());

    // The local game's own wish, published.  A name we could not resolve is skipped:
    // publishing "" (or a guessed name) makes the partner protect a level that does not
    // exist, and that failure looks exactly like "the level did not stream".
    const char* names[wire::kLevelUnionMaxNames];
    int n = 0;
    uint32_t h = 2166136261u;
    for (int i = 0; i < g_levelN && n < wire::kLevelUnionMaxNames; ++i) {
        if (g_levels[i].gameWants != 1 || !g_levels[i].name[0]) continue;
        names[n++] = g_levels[i].name;
        for (const char* c = g_levels[i].name; *c; ++c) h = (h ^ (uint8_t)*c) * 16777619u;
        h = (h ^ 0x2Fu) * 16777619u;
    }
    g_stream.OnLocalList(names, n, now);
    // Do not put cross-world names on the wire while the worlds differ.  When they
    // agree again the 3 s gap has already passed the 2 s heartbeat, so a fresh union
    // goes out on the first pump.
    if (worldsAgree && g_net.Connected() && (h != lastHash || now - lastSend >= 2000)) {
        if (h != lastHash) QDLOG(LOG_INFO, "level union: local set ", dec(n), " level(s)");
        lastHash = h;
        lastSend = now;
        SendLevelUnion(names, n);
    }

    // The partner's need, applied.  Two levels per pump at most: a show is a load, and
    // ten at once is a hitch the player feels.
    int budget = 2;
    for (int i = 0; i < g_levelN; ++i) {
        LevelRec& r = g_levels[i];
        if (!r.name[0] || !ObjectIsLive(r.ls)) continue;
        bool peer = worldsAgree && g_net.Connected() && g_stream.PeerWants(r.name, now);
        if (peer) {
            g_force.Need(r.name, now);
            if (r.gameWants != 1 && !LevelVisible(r.ls) && budget > 0) {
                // The check sits here, not inside ForceLevel, so a refused level does not
                // consume the two-levels-per-pump budget and starve the ones that matter.
                if (LevelNotForcible(r.name)) {
                    static char said[8][64];
                    static int saidN = 0;
                    bool told = false;
                    for (int k = 0; k < saidN; ++k) if (strcmp(said[k], r.name) == 0) told = true;
                    if (!told) {
                        if (saidN < 8) lstrcpynA(said[saidN++], r.name, sizeof(said[0]));
                        QDLOG(LOG_WARN, "level union: refusing to force '", r.name,
                              "' (a video/config level, never a place a player stands)");
                    }
                } else {
                    --budget;
                    ForceLevel(r, true);
                }
            }
        } else if (g_force.IsForced(r.name)) {
            if (g_force.ShouldRelease(r.name, false, r.gameWants == 1, now) && r.gameWants != 1)
                ForceLevel(r, false);
        }
    }

    stream::LevelSet want = g_stream.Wanted(now);
    static int lastN = -1;
    if ((int)want.n != lastN) {
        QDLOG(LOG_INFO, "level union size ", dec((int)want.n), " (local ", dec(n), ", forced for partner ",
              dec(g_force.Count()), ")", (g_stream.PeerListStale(now) ? " (peer list stale)" : ""));
        lastN = (int)want.n;
    }
    int lost = g_stream.Regressions(now);
    if (lost > 0 && g_stream.AnyPeerLive(now))
        QDLOG(LOG_DEBUG, "level union: ", dec(lost), " level(s) left the union since the last pump");
}

void OnLevelUnion(wire::Reader& r, uint32_t now)
{
    static wire::LevelUnionBody b;     // 4 KB: not on the game thread's stack
    if (!wire::ReadLevelUnion(r, &b)) return;
    if (!b.has_names) return;          // this sender always lists names (WIRE.md 3.12)
    if (!g_map.PeerStateApplies(now)) {
        // These names describe the other world.  Dropping them is the refusal, and
        // clearing the slot also releases anything the previous list still holds up.
        g_stream.OnPeerGone((uint8_t)RemoteSlot());
        return;
    }
    const char* names[wire::kLevelUnionMaxNames];
    int n = 0;
    for (int i = 0; i < b.n; ++i) if (wire::LevelUnionBit(b, i)) names[n++] = b.name[i];
    static int lastN = -1;
    if (n != lastN) { lastN = n; QDLOG(LOG_INFO, "level union: partner set ", dec(n), " level(s)"); }
    g_stream.OnPeerList((uint8_t)RemoteSlot(), names, n, now);
}

// ---- travel announcement (SYNC-GAPS G6) -----------------------------------
// The session is not torn down on a transition.  The first half of G6 is announcement
// plus refusal: each machine says which world it is in, a disagreement is logged once,
// and the partner's absolute bodies are not applied while the names differ.  That half
// is still here unchanged.
//
// The second half is the follow, further down: `dest` names the placed actor carrying
// the game's own LocationData, and the receiver drives ITS OWN pawn through the game's
// own entry point.  Nothing here calls OpenLevel, and nothing ever writes the partner's
// pawn -- a hard map switch still ends in the explicit refusal below, because the
// destination actor is not in this world.
void LocalMapName(char* out, int cap)
{
    out[0] = 0;
    void* world = GWorldPtr();
    if (!world) return;
    char raw[128];
    // `ShortLevelName` is the same normaliser the union uses, so both machines spell
    // the map the same way without negotiating.  The outer package is the canonical
    // map name; the UWorld object name is the fallback for a build that names them
    // differently.
    void* outer = ObjOuter(world);
    if (outer && FNameToString(ObjNameRaw(outer), raw, sizeof(raw)) > 0)
        stream::ShortLevelName(raw, out, cap);
    if (!out[0] && FNameToString(ObjNameRaw(world), raw, sizeof(raw)) > 0)
        stream::ShortLevelName(raw, out, cap);
}

// ---- travel follow: the game's own state -----------------------------------
// qdtravel.h owns the decision; this owns the measurement.  Every fact below is the
// game's, read from the pawn, not inferred from input: `FS_PlayerChar_C` carries
// `Teleporting`, `QuickTeleporting`, `DoingTeleportCrystal` and the after-move latch
// `TeleportedRecently` as Blueprint bools, and the destination is one of four object
// properties that carry the game's own `LocationData`.
bool LocalTeleporting(void* pawn)
{
    if (!IsGameplayPawn(pawn)) return false;
    uint8_t b = 0;
    if (BytePropC(pawn, "Teleporting", &b) && b) return true;
    if (BytePropC(pawn, "QuickTeleporting", &b) && b) return true;
    if (BytePropC(pawn, "DoingTeleportCrystal", &b) && b) return true;
    // The after-move latch is what makes a fast teleport still announce itself: the
    // move can finish inside one tick, and the announcement has to survive that tick.
    if (BytePropC(pawn, "TeleportedRecently", &b) && b) return true;
    return false;
}

// "Do not fight the player".  Each condition is a state the game itself would use to
// refuse a teleport.  A follow that interrupts a local transition is worse than a
// missed one, so every one of these skips.
bool LocalTravelBusy(void* pawn)
{
    if (!IsGameplayPawn(pawn)) return true;   // menu world / no pawn: never drive it
    uint8_t b = 0;
    if (BytePropC(pawn, "Teleporting", &b) && b) return true;
    if (BytePropC(pawn, "QuickTeleporting", &b) && b) return true;
    if (BytePropC(pawn, "DoingTeleportCrystal", &b) && b) return true;
    if (BytePropC(pawn, "TeleportingDisabled", &b) && b) return true;
    // A menu is an object property that is null when the menu is closed.
    if (ObjPropC(pawn, "CurrentGameMenu")) return true;
    if (ObjPropC(pawn, "CurrentSaveMenu")) return true;
    if (ObjPropC(pawn, "KeyMenu")) return true;
    if (ObjPropC(pawn, "CurrentShopMenu")) return true;
    // A cutscene is a manager object; the intro flag is the same test the game uses.
    if (ObjPropC(pawn, "CurrentCutsceneManager")) return true;
    if (BytePropC(pawn, "InIntroCutscene", &b) && b) return true;
    // The dialogue manager is a persistent component, so its own running bool is the
    // fact -- the same test DialoguePump uses for the local conversation.
    void* dm = ObjPropC(pawn, "DialogueManager");
    if (dm && BytePropC(dm, "RunningDialogue", &b) && b) return true;
    return false;
}

// The placed actor carrying the LocationData this machine is travelling through.  We do
// not guess which of the pawn's travel properties the game filled; we take the first
// live one that actually HAS a `LocationData` property, which is the same test the
// receiver applies to the actor it resolves.  Naming an actor that does not carry the
// data is how a follow lands on the wrong place.
void LocalTravelDest(void* pawn, const char* world, char* out, int cap)
{
    if (!out || cap <= 0) return;
    out[0] = 0;
    if (!IsGameplayPawn(pawn) || !world || !world[0]) return;
    static const char* kFields[] = {
        "CurrentTeleporterTarget", "TeleportLocationTarget",
        "TempTeleporter", "TeleporterStartActor"
    };
    for (unsigned i = 0; i < sizeof(kFields) / sizeof(kFields[0]); ++i) {
        void* a = ObjPropC(pawn, kFields[i]);
        if (!a || !ObjectIsLive(a)) continue;
        if (PropOff(ObjClass(a), "LocationData") < 0) continue;   // not a carrier
        char an[64];
        if (FNameToString(ObjNameRaw(a), an, sizeof(an)) <= 0) continue;
        if (travel::MakeDest(an, world, out, cap)) return;
    }
}

bool ActorOuterReachesWorld(void* actor, void* world)
{
    void* p = ObjOuter(actor);
    for (int depth = 0; p && depth < 8; p = ObjOuter(p), ++depth) {
        if (p == world) return true;
    }
    return false;
}

// The receiver's half of the identity: the same short actor name, alive in OUR world.
// Both machines run the same cooked world, so a placed actor has the same object name
// on both; the outer-chain test is what separates it from a CDO, an asset, or an actor
// in a level this machine has not streamed in.
void* FindTravelActor(const char* name, void* world)
{
    if (!name || !name[0] || !world) return nullptr;
    static void* actorBase = nullptr;
    if (!actorBase || !ObjectIsLive(actorBase)) actorBase = FindClassByName("Actor");
    if (!actorBase) return nullptr;

    int n = ObjectCount();
    for (int i = 0; i < n; ++i) {
        void* o = ObjectAt(i);
        if (!o) continue;
        // The whole read is fault-contained: a probe that walks GObjects must never
        // die for one slot, and the name decode is a read of game memory like any other.
        __try {
            if (!ObjectIsLive(o) || !ObjNameIs(o, name)) continue;
            void* c = ObjClass(o);
            if (!c || !IsSubclassOf(c, actorBase)) continue;
            if (ActorOuterReachesWorld(o, world)) return o;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            // An unreadable slot is skipped; a follow must never die for one.
        }
    }
    return nullptr;
}

// The LocationData the actor carries.  The property is measured (ObjectProperty on both
// `Teleporter_C` and `Checkpoint_C`); the game's own `GetLocationData` is the fallback
// for a carrier that exposes it only through the interface.
void* ActorLocationData(void* actor)
{
    void* data = ObjPropC(actor, "LocationData");
    if (data) return data;
    Fn fGet;
    if (!fGet.Resolve(actor, "GetLocationData")) return nullptr;
    Call c(fGet);
    if (!c.Run(actor)) return nullptr;
    uint8_t* p = OutParm(c, 8);
    return p ? *(void**)p : nullptr;
}

// Drive our own pawn through the game's own travel entry point.  The signature is
// measured: `TeleportToLocation(Location Data: ObjectProperty, GreenFX: BoolProperty)`.
// The parameter's name really contains a space (a Blueprint pin name), so it is passed
// by that exact name and a type-based fallback follows it.  `GreenFX` is the full-screen
// effect; a follow drives it with 0 so the partner's transition is not doubled on this
// screen.
bool FollowTravelDest(const char* dest, void* pawn, char* err, int errCap)
{
    if (err && errCap > 0) err[0] = 0;
    travel::DestParts p;
    if (!travel::SplitDest(dest, &p)) {
        ErrSet(err, errCap, "malformed destination: ", dest);
        return false;
    }
    if (!IsGameplayPawn(pawn)) {
        ErrSet(err, errCap, "no local gameplay pawn", "");
        return false;
    }
    void* world = GWorldPtr();
    if (!world) { ErrSet(err, errCap, "no world", ""); return false; }
    if (!ReflectionReady()) {
        ErrSet(err, errCap, "property layout not validated: ", Lay().note);
        return false;
    }

    char local[stream::kMaxNameLen];
    LocalMapName(local, sizeof(local));
    if (!travel::SameText(p.world, local)) {
        ErrSet(err, errCap, "destination world is ", p.world);
        return false;
    }

    void* actor = FindTravelActor(p.actor, world);
    if (!actor) { ErrSet(err, errCap, "no live actor named ", p.actor); return false; }
    void* data = ActorLocationData(actor);
    if (!data) { ErrSet(err, errCap, p.actor, " carries no LocationData"); return false; }

    Fn fTp;
    if (!fTp.Resolve(pawn, "TeleportToLocation")) {
        ErrSet(err, errCap, "FS_PlayerChar.TeleportToLocation not found", "");
        return false;
    }
    const PropDesc* pObj = FindProp(fTp.sig, "Location Data");
    if (!pObj) pObj = FindPropOfType(fTp.sig, "ObjectProperty", CPF_OutParm | CPF_ReturnParm);
    const PropDesc* pFx = FindProp(fTp.sig, "GreenFX");
    if (!pFx) pFx = FindPropOfType(fTp.sig, "BoolProperty", CPF_OutParm | CPF_ReturnParm);
    if (!pObj) {
        ErrSet(err, errCap, "TeleportToLocation has no LocationData parameter", "");
        return false;
    }

    Call c(fTp);
    c.Obj(pObj->name, data);
    if (pFx) c.U8(pFx->name, 0);
    if (!c.Run(pawn)) {
        ErrSet(err, errCap, "TeleportToLocation faulted", "");
        return false;
    }
    return true;
}

// ---- hard map transition: the game's own world-load entry (G6) -------------
// Measured (`analysis/re/travel_*_probe.*`): `LoadMapWithLoadingScreen` is a native
// UFunction on the game instance -- registration table 0x03950090, exec thunk
// 0x00E58F70, body 0x00E34F30.  The body sets `bLoadingMap = 1`, records
// `CurrentlyLoadingMapPackage`, and starts an asynchronous world load with the game's
// loading screen.  `OpenLevel` is imported only by menus, death and cheats; this is the
// game's own hard-travel path.
//
// The call goes through ProcessEvent on the LIVE GameInstance, so no RVA is baked into
// the call site and the parameter list is discovered from the UFunction.  The map asset
// is resolved by the same `LoadAsset_Blocking` path the body uses (qdavatar.h).
static const char* kMapPackageRoot = "/Game/RuinsLevel/Maps/";

struct FStringArg { wchar_t* Data; int32_t Num; int32_t Max; };
static wchar_t g_fstrBuf[2][512];
static int     g_fstrNext = 0;

bool MakeFString(const char* ascii, uint8_t* out)
{
    wchar_t* b = g_fstrBuf[g_fstrNext++ & 1];
    int n = MultiByteToWideChar(CP_ACP, 0, ascii, -1, b, 511);
    if (n <= 0) return false;
    FStringArg a;
    a.Data = b; a.Num = n; a.Max = n;
    memcpy(out, &a, sizeof(a));
    return true;
}

void MapPackagePath(const char* map, char* out, int cap)
{
    out[0] = 0;
    if (!map || !map[0]) return;
    int w = 0;
    for (const char* s = kMapPackageRoot; *s && w < cap - 1; ++s) out[w++] = *s;
    for (int i = 0; map[i] && w < cap - 1; ++i) out[w++] = map[i];
    if (w < cap - 1) out[w++] = '.';
    for (int i = 0; map[i] && w < cap - 1; ++i) out[w++] = map[i];
    out[w] = 0;
}

bool FollowHardTravel(const char* map, char* err, int errCap)
{
    if (err && errCap > 0) err[0] = 0;
    if (!map || !map[0] || !stream::PlausibleLevelName(map)) {
        ErrSet(err, errCap, "not a plausible map name: ", map ? map : "?");
        return false;
    }
    if (!ReflectionReady()) {
        ErrSet(err, errCap, "property layout not validated: ", Lay().note);
        return false;
    }
    void* world = GWorldPtr();
    void* gi = world ? GameInstanceOf(world) : nullptr;
    if (!gi) { ErrSet(err, errCap, "no game instance", ""); return false; }

    static Fn fLoad = {};
    static bool logged = false;
    if (!fLoad.Resolve(gi, "LoadMapWithLoadingScreen")) {
        ErrSet(err, errCap, "GameInstance.LoadMapWithLoadingScreen not found", "");
        return false;
    }
    if (!logged) {
        logged = true;
        LogSignature("LoadMapWithLoadingScreen", fLoad.sig);
    }

    char pkg[192];
    MapPackagePath(map, pkg, sizeof(pkg));
    void* mapAsset = avatar::LoadAsset(pkg);
    if (!mapAsset) {
        // Loading the map asset is how the parameter is filled when it is an object.
        // If the only parameters are names/strings the call can still succeed, so this
        // is a warning, not a refusal; the unsupported-parameter check below decides.
        QDLOG(LOG_WARN, "hard travel: could not load ", pkg, " (will try name/string parameters)");
    }

    Call c(fLoad);
    int unsupported = 0;
    bool needsMapObject = false;
    for (int i = 0; i < fLoad.sig.props; ++i) {
        const PropDesc& d = fLoad.sig.p[i];
        if (d.flags & (CPF_OutParm | CPF_ReturnParm)) continue;
        if (NameEquals(d.type, "ObjectProperty") || NameEquals(d.type, "ClassProperty")) {
            bool ctx = strstr(d.name, "Context") != nullptr || strstr(d.name, "Outer") != nullptr;
            if (!ctx && !mapAsset) needsMapObject = true;
            c.Obj(d.name, ctx ? gi : mapAsset);
        } else if (NameEquals(d.type, "StrProperty")) {
            uint8_t tmp[16];
            if (MakeFString(pkg, tmp)) c.Set(d.name, tmp, 16);
            else ++unsupported;
        } else if (NameEquals(d.type, "NameProperty")) {
            QdName n;
            if (FNameFromAnsi(map, &n)) c.Name(d.name, n);
            else ++unsupported;
        } else if (NameEquals(d.type, "BoolProperty")) {
            c.U8(d.name, 1);
        } else if (NameEquals(d.type, "SoftObjectProperty") && d.size >= 40) {
            QdName n;
            uint8_t* p = c.At(d.name, 40);
            if (p && FNameFromAnsi(pkg, &n)) memcpy(p + 16, &n, 8);
            else ++unsupported;
        } else {
            ++unsupported;
        }
    }
    if (unsupported > 0) {
        ErrSet(err, errCap, "unhandled parameter type in LoadMapWithLoadingScreen", "");
        return false;
    }
    if (needsMapObject) {
        ErrSet(err, errCap, "LoadMapWithLoadingScreen wants a map object that did not load: ", pkg);
        return false;
    }
    if (!c.Run(gi)) { ErrSet(err, errCap, "LoadMapWithLoadingScreen faulted", ""); return false; }
    return true;
}

void TravelPump(uint32_t now)
{
    // Travel is deliberately NOT gated on StreamUnion: a hard map switch changes the
    // UWorld, not a streamed sublevel, and the follow must work with the union off.
    if (!Cfg().travel_sync && !Cfg().stream_union) return;
    static uint32_t lastSend = 0;
    static bool     sent = false;
    static bool     wasDiverged = false;

    if (!g_net.Connected()) {
        if (g_map.Local()[0] || g_map.Peer()[0]) g_map.Reset();
        // The follow memory names actors in a session that has ended; a new partner
        // must be judged on its own transitions, not on what this machine followed
        // before.  Without this a rejoin would suppress the first travel it should take.
        g_travel.Reset();
        g_hard.Reset();
        g_travelSeq = 0;
        sent = false;
        wasDiverged = false;
        return;
    }

    char local[stream::kMaxNameLen];
    LocalMapName(local, sizeof(local));
    bool changed = g_map.OnLocal(local, now);
    if (Cfg().travel_sync && g_hard.OnLocalMap(local, now)) {
        ++g_travelSeq;
        QDLOG(LOG_INFO, "travel: HARD transition to ", local,
              " -- announcing it so the partner loads the same world");
    }

    // The destination this machine is taking, if any.  "" is "not travelling", which is
    // the wire's own encoding (WIRE.md 3.15a): the partner must never have to infer a
    // travel from a map change.  NoteLocal also suppresses the echo of a travel this
    // machine just followed for the partner -- announcing it would invite a teleport
    // back, and the dedup on the other side is not enough to stop the first hop.
    char dest[travel::kDestCap];
    dest[0] = 0;
    bool travelling = LocalTeleporting(g_localPawn);
    if (travelling) LocalTravelDest(g_localPawn, local, dest, sizeof(dest));
    bool destChanged = g_travel.NoteLocal(dest, travelling, now);

    if (changed || destChanged) ++g_travelSeq;
    // One line per change, so a live run can see the announcement the partner is about
    // to act on.  "(none)" is the end of a travel, not a missing measurement.
    if (destChanged)
        QDLOG(LOG_INFO, "travel: local destination ",
              (g_travel.Announce()[0] ? g_travel.Announce() : "(none)"),
              " seq=", dec((int)g_travelSeq));

    // ---- the hard handshake ------------------------------------------------
    uint8_t hardPhase = 0;
    const char* hardMap = "";
    if (Cfg().travel_sync) {
        if (g_hard.Tick(now))
            QDLOG(LOG_WARN, "travel: hard transition to ", g_hard.TargetMap(),
                  " missed its " , dec((int)(travel::kHardDeadlineMs / 1000)),
                  " s deadline; abandoned and not retried");
        if (g_hard.ReadyToResume() && g_net.IsHost()) {
            g_hard.NoteAnnounce(3);          // RESUME
            QDLOG(LOG_INFO, "travel: both machines arrived in ", g_hard.TargetMap(),
                  "; host releasing the pair to resume");
        }
        hardPhase = g_hard.TakeAnnounce();
        if (hardPhase) hardMap = g_hard.TargetMap();
    }

    // On change at once, then every 2 s.  A change announcement can be lost with the
    // session; the heartbeat is what makes a rejoin converge without a new event.
    const bool hardSend = hardPhase != 0;
    if ((changed || destChanged || hardSend || !sent || now - lastSend >= 2000) && local[0]) {
        wire::TravelBody t;
        memset(&t, 0, sizeof(t));
        t.seq = g_travelSeq;
        t.role = g_net.IsHost() ? 1 : 2;   // 1 = host, 2 = peer (WIRE.md travel)
        t.phase = hardPhase;
        const char* mapOut = (hardSend && hardMap[0]) ? hardMap : local;
        int i = 0;
        for (; mapOut[i] && i < (int)sizeof(t.map) - 1; ++i) t.map[i] = mapOut[i];
        t.map[i] = 0;
        char pkg[192];
        const char* d = g_travel.Announce();
        if (hardPhase == wire::TP_HARD_BEGIN) {
            MapPackagePath(g_hard.TargetMap(), pkg, sizeof(pkg));
            d = pkg;
        }
        i = 0;
        for (; d[i] && i < (int)sizeof(t.dest) - 1; ++i) t.dest[i] = d[i];
        t.dest[i] = 0;
        uint8_t body[wire::kMaxBody];
        wire::Writer w(body, sizeof(body));
        wire::WriteTravel(w, t);
        if (w.ok && g_net.SendReliable(wire::K_TRAVEL, body, w.len)) {
            lastSend = now;
            sent = true;
            if (hardSend) {
                QDLOG(LOG_INFO, "travel: hard phase ", dec((int)hardPhase),
                      " map=", t.map, (t.dest[0] ? " dest=" : ""), t.dest);
                if (hardPhase == wire::TP_RESUME) g_hard.MarkResumed();
            }
        } else if (hardSend) {
            // The handshake phase must not be lost because the reliable lane was full;
            // queue it for the next tick.
            g_hard.NoteAnnounce(hardPhase);
        }
    }

    bool div = g_map.Diverged(now);
    if (div != wasDiverged) {
        wasDiverged = div;
        if (div) {
            // One line per transition, not per pump: this is the operator's only
            // evidence that a working session is looking at two different worlds.
            QDLOG(LOG_WARN, "WORLD DIVERGENCE: local map ", g_map.Local(), ", partner map ",
                  (g_map.Peer()[0] ? g_map.Peer() : "?"),
                  " -- cross-world state refused until they agree again (G6)");
        } else {
            // An empty local name is "no world" (a menu or a teardown), not a match;
            // logging agreement there would suggest the session converged when it
            // merely left the world.
            if (g_map.BothKnown()) QDLOG(LOG_INFO, "worlds agree again: ", g_map.Local());
        }
    }
}

void OnTravel(wire::Reader& r, uint32_t now)
{
    wire::TravelBody t;
    if (!wire::ReadTravel(r, &t)) return;
    if (g_map.OnPeer(t.map, now))
        QDLOG(LOG_INFO, "travel: partner role ", dec((int)t.role), " is in ",
              (t.map[0] ? t.map : "(no map)"), " seq=", dec((int)t.seq));

    // ---- hard transition handshake -----------------------------------------
    if (Cfg().travel_sync && t.phase != wire::TP_NONE) {
        // Read the current world NOW rather than from g_map: DispatchInbound runs
        // before the pump, so g_map is one frame old during a load.
        char cur[stream::kMaxNameLen];
        LocalMapName(cur, sizeof(cur));
        bool busy = LocalTravelBusy(g_localPawn);
        switch (t.phase) {
        case wire::TP_HARD_BEGIN: {
            travel::HardVerdict v = g_hard.OnPeerBegin(t.seq, t.map, cur, busy);
            if (v == travel::HV_LOAD) {
                g_hard.Arm(now);
                char err[160];
                bool ok = FollowHardTravel(t.map, err, sizeof(err));
                g_hard.OnLoadResult(ok, now);
                if (ok)
                    QDLOG(LOG_INFO, "travel: hard transition: loading ", t.map,
                          " through GameInstance.LoadMapWithLoadingScreen");
                else
                    QDLOG(LOG_WARN, "travel: cannot follow hard transition to ", t.map,
                          ": ", err, " (recorded; this map is not retried)");
            } else if (v == travel::HV_ANNOUNCE_ARRIVE) {
                g_hard.NoteAnnounce(2);       // ARRIVED
                QDLOG(LOG_INFO, "travel: already in ", t.map, "; reporting arrival");
            } else if (v == travel::HV_REFUSED) {
                QDLOG(LOG_WARN, "travel: refused hard transition to ", t.map,
                      " (implausible map or one that already failed); recorded, not retried");
            }
            break;
        }
        case wire::TP_ARRIVED: {
            g_hard.OnPeerArrived(t.seq, t.map, cur);
            QDLOG(LOG_INFO, "travel: the partner arrived in ", t.map,
                  (travel::SameText(cur, t.map) ? " (both there)" : ""));
            if (g_hard.ReadyToResume() && g_net.IsHost())
                g_hard.NoteAnnounce(3);       // RESUME
            break;
        }
        case wire::TP_RESUME: {
            if (g_hard.OnPeerResume(t.seq, t.map) == travel::HV_RESUME)
                QDLOG(LOG_INFO, "travel: host released the pair; resuming in ", t.map);
            break;
        }
        default:
            break;
        }
        return;
    }

    // An empty dest is the heartbeat's "not travelling", not news.
    if (!t.dest[0]) return;

    // The in-world half.  The decision is pure (qdtravel.h).  This half only answers
    // the questions the decision needs -- is the local player free, which world are we
    // in -- and performs the drive.  Nothing here writes the partner's pawn: each
    // machine moves its own.
    char cur[stream::kMaxNameLen];
    LocalMapName(cur, sizeof(cur));
    bool busy = LocalTravelBusy(g_localPawn);
    travel::Verdict v = g_travel.OnPeer(t.seq, t.dest, cur, busy);
    switch (v) {
    case travel::V_FOLLOW: {
        char err[160];
        if (FollowTravelDest(t.dest, g_localPawn, err, sizeof(err))) {
            g_travel.OnFollowResult(true, now);
            QDLOG(LOG_INFO, "travel: followed ", t.dest, " (seq ", dec((int)t.seq),
                  ") -- local pawn driven through TeleportToLocation");
        } else {
            // Explicit failure, then silence for this destination: the acceptance
            // criteria forbid retrying a name that cannot resolve.
            g_travel.OnFollowResult(false, now);
            QDLOG(LOG_WARN, "travel: cannot follow ", t.dest, ": ", err,
                  " (recorded; this destination is not retried)");
        }
        break;
    }
    case travel::V_BUSY:
        QDLOG(LOG_INFO, "travel: partner is travelling to ", t.dest,
              " but the local player is busy; skipped (not remembered)");
        break;
    case travel::V_WRONG_WORLD: {
        // The in-world follow cannot cross a UWorld.  Now that the game's own hard-load
        // entry exists, take the world switch instead of ending in a refusal: the
        // destination names the world the partner is in.
        travel::DestParts dp;
        if (Cfg().travel_sync && travel::SplitDest(t.dest, &dp)) {
            char err[160];
            if (FollowHardTravel(dp.world, err, sizeof(err))) {
                g_hard.OnPeerBegin(t.seq, dp.world, cur, false);
                g_hard.Arm(now);
                QDLOG(LOG_INFO, "travel: following the partner's world switch to ",
                      dp.world, " (an in-world destination cannot cross a map)");
            } else {
                QDLOG(LOG_WARN, "travel: cannot follow ", t.dest, ": ", err);
            }
        } else {
            QDLOG(LOG_WARN, "travel: cannot follow ", t.dest, ": its world is not this "
                  "machine's (", (cur[0] ? cur : "?"), ") and TravelSync is off");
        }
        break;
    }
    case travel::V_BAD_DEST:
        QDLOG(LOG_WARN, "travel: malformed destination '", t.dest,
              "' from the partner; ignored");
        break;
    case travel::V_DUPLICATE:
    case travel::V_GAVE_UP:
    case travel::V_NONE:
    default:
        break;      // already acted on (or already refused): a heartbeat is not news
    }
}

// ===========================================================================
// 6b. Enemies (qdenemy.h holds the model and the reasons)
// ===========================================================================
struct EnemyRec {
    void*    actor;
    void*    spawner;
    uint32_t guid[4];
    bool     haveGuid;
    bool     driven;         // this copy is frozen and following the owner's rows
    bool     killedByUs;     // a KILL from the owner was applied (one shot)
    bool     fresh;          // a NEW actor for this GUID: the adoption rule runs once
    uint8_t  savedMoveMode;
    uint8_t  savedPaused;    // EnemyBase_C.Paused before this mod froze the copy
    float    lastHp, maxHp;
    float    pos[3], yaw;
    uint32_t seenPass;
    // The detail lane (WIRE.md 3.9a).  `pub*` is what this machine last published as
    // the owner; `got*` is what it last applied as the follower.  The montage is
    // identified by asset path, never by index: the two machines discover cooked
    // assets in their own order.
    void*    pubMon;
    uint8_t  pubSeq;
    uint16_t pubStatus;
    bool     pubInit;        // the first observation is the baseline, not a change
    uint8_t  gotSeq;
    uint16_t gotStatus;
    char     gotPath[wire::kEnemyPathCap];
};
EnemyRec        g_en[256];
int             g_enN = 0;
void*           g_enemyBaseCls = nullptr;
uint32_t        g_enPass = 1;
int             g_enCursor = 0;
enemy::HostView g_hostView;        // the other machine's rows (either direction)
enemy::KillLog  g_killLog;         // deaths this machine ran, re-broadcast as dead rows
enemy::RowCursor g_rowCursor;
enemyown::OwnerSet g_own;          // who runs each enemy (qdenemyown.h)
enemydmg::SwingGate g_dmgGate;     // one K_DAMAGE per swing, bounded per second
int             g_deadCursor = 0;
bool            g_inOwnRecord = false;
bool            g_inRemoteDeath = false;   // a death being applied FROM the other machine
void*           g_clsYes[32];  int g_clsYesN = 0;    // class -> derives from the enemy base
void*           g_clsNo[256];  int g_clsNoN = 0;     // (reset with the world: classes unload)

EnemyRec* EnemyByActor(void* a) { for (int i = 0; i < g_enN; ++i) if (g_en[i].actor == a) return &g_en[i]; return nullptr; }
EnemyRec* EnemyByGuid(const uint32_t g[4])
{
    for (int i = 0; i < g_enN; ++i) if (g_en[i].haveGuid && enemy::SameGuid(g_en[i].guid, g)) return &g_en[i];
    return nullptr;
}

bool SpawnerGuid(void* spawner, uint32_t out[4])
{
    static Fn f = {};
    if (!spawner || !f.Resolve(spawner, "GetSaveGUID")) return false;
    Call c(f);
    if (!c.Run(spawner)) return false;
    uint8_t* p = OutParm(c, 16);
    if (!p) return false;
    memcpy(out, p, 16);
    return !enemy::ZeroGuid(out);
}

bool EnemyHp(void* actor, float* hp, float* maxHp)
{
    static Fn fHp = {}, fMax = {};
    return CallGetFloat(actor, fHp, "GetHP", hp) && CallGetFloat(actor, fMax, "GetMaxHP", maxHp);
}

bool EnemyDead(void* actor)
{
    uint8_t d = 0;
    return BytePropC(actor, "bDead", &d) && d != 0;
}

// Defined with the damage path below (it applies through it); the scan calls it the
// moment a GUID is known.
void AdoptOnRespawn(EnemyRec& r);

// Incremental discovery: 16 K object slots per frame, so a full pass costs no single
// frame more than a fraction of a millisecond.  At the end of a pass, records whose
// actor was not seen are dropped.
void EnemyScanStep()
{
    if (!g_enemyBaseCls) {
        static uint32_t lastTry = 0;
        uint32_t now = Net::Now();
        if (now - lastTry < 5000) return;
        lastTry = now;
        g_enemyBaseCls = FindClassByName("FS_EnemyCharacterBase");
        if (!g_enemyBaseCls) { QDLOG(LOG_DEBUG, "enemy sync: FS_EnemyCharacterBase not loaded yet"); return; }
        QDLOG(LOG_INFO, "enemy sync: FS_EnemyCharacterBase found");
    }
    void** clsYes = g_clsYes; int& nYes = g_clsYesN;
    void** clsNo = g_clsNo;   int& nNo = g_clsNoN;
    int total = ObjectCount();
    int end = g_enCursor + 16384;
    if (end > total) end = total;
    for (int i = g_enCursor; i < end; ++i) {
        void* o = ObjectAt(i);
        if (!o || !LooksLikeUObject(o)) continue;
        void* cls = ObjClass(o);
        bool yes = false, known = false;
        for (int k = 0; k < nYes; ++k) if (clsYes[k] == cls) { yes = known = true; break; }
        if (!known) for (int k = 0; k < nNo; ++k) if (clsNo[k] == cls) { known = true; break; }
        if (!known) {
            yes = DerivesFrom(cls, g_enemyBaseCls);
            if (yes) { if (nYes < 32) clsYes[nYes++] = cls; }
            else if (nNo < 256) clsNo[nNo++] = cls;
            else nNo = 0;                       // a bounded cache that restarts, never grows
        }
        if (!yes) continue;
        char nm[12];
        if (FNameToString(ObjNameRaw(o), nm, sizeof(nm)) > 0 && strncmp(nm, "Default__", 9) == 0) continue;
        EnemyRec* r = EnemyByActor(o);
        if (!r) {
            if (g_enN >= (int)(sizeof(g_en) / sizeof(g_en[0]))) continue;
            r = &g_en[g_enN++];
            memset(r, 0, sizeof(*r));
            r->actor = o;
            r->lastHp = -1.0f;
            r->fresh = true;
        }
        r->seenPass = g_enPass;
        void* sp = ObjPropC(o, "OwningSpawner");
        if (sp != r->spawner) { r->spawner = sp; r->haveGuid = false; }
        if (sp && !r->haveGuid) {
            r->haveGuid = SpawnerGuid(sp, r->guid);
            if (r->haveGuid) {
                char d[96];
                ObjDescribe(o, d, sizeof(d));
                QDLOG(LOG_DEBUG, "enemy ", d, " guid=", hex32(r->guid[0]), "-", hex32(r->guid[1]));
            }
        }
        // The GUID is only known now, so this is the first moment the partner's row for
        // the same enemy can be consulted.  Once per actor.
        if (r->fresh && r->haveGuid) AdoptOnRespawn(*r);
    }
    g_enCursor = end;
    if (g_enCursor >= total) {
        g_enCursor = 0;
        for (int i = 0; i < g_enN;) {
            if (g_en[i].seenPass == g_enPass && ObjectIsLive(g_en[i].actor)) { ++i; continue; }
            g_en[i] = g_en[--g_enN];
        }
        static int lastN = -1;
        if (g_enN != lastN) { lastN = g_enN; QDLOG(LOG_INFO, "enemy sync: ", dec(g_enN), " live enemies"); }
        ++g_enPass;
    }
}

// ---------------------------------------------------------------------------
// Enemy damage: through the message the enemy actually answers
// ---------------------------------------------------------------------------
// MEASURED, and the reason the previous round's damage path was inert:
// `UGameplayStatics::ApplyDamage` (0x028D2190) IS the engine entry -- its body guards
// and calls `AActor::TakeDamage` through vtable+0x598 -- but `EnemyBase_C` never
// imports it and a live pass recorded it never firing during a real enemy kill.  HP
// movement built on it (convergence on a driven copy, a peer's I_ENEMY_HIT, a dead
// I_ENEMY_RECORD) therefore changed nothing, while the read-back said "no effect" and
// nothing acted on it.  This function is named for what it does, not for what the
// engine calls its damage entry.
//
// The entry the enemy DOES answer is `TryApplyDamage`, the `FB_Damageable` interface
// message declared by EnemyBase_C (and by the player class).  Its payload is a
// `DamageToApply` UScriptStruct.  The struct's fields are WALKED from reflection and
// filled by name: a hardcoded layout is one content change away from writing the
// amount into the wrong field.  The HP read-back is the only proof it worked, and the
// "had NO effect" line below now names the struct fill as the suspect.
static int DamageAmountScore(const char* n)
{
    if (NameEquals(n, "DamageAmount")) return 4;
    if (NameEquals(n, "Damage") || NameEquals(n, "Amount") || NameEquals(n, "BaseDamage")) return 3;
    if (NameEquals(n, "DamageValue") || NameEquals(n, "Value")) return 2;
    if (enemyown::HasCI(n, "damage") &&
        (enemyown::HasCI(n, "amount") || enemyown::HasCI(n, "value"))) return 1;
    return 0;
}

// The struct names the party, not the amount.  Filled only when a field with the name
// exists; a struct without them simply carries none.
static bool CauserField(const char* n)
{
    return NameEquals(n, "Instigator") || NameEquals(n, "DamageInstigator") ||
           NameEquals(n, "DamageCauser") || NameEquals(n, "Causer") ||
           NameEquals(n, "SourceActor") || NameEquals(n, "DamageSource") ||
           NameEquals(n, "Attacker") || NameEquals(n, "Applier");
}

static bool TargetField(const char* n)
{
    return NameEquals(n, "DamagedActor") || NameEquals(n, "TargetActor") ||
           NameEquals(n, "Target") || NameEquals(n, "Victim");
}

// --- the payload struct, resolved once and shared by both directions -------------
// `TryApplyDamage` is the `FB_Damageable` interface message, and every implementing
// class (NPC_Base_C, EnemyBase_C, FS_PlayerChar_C) declares the same signature with the
// same `FB_DamageAttributeSet` payload.  So the struct's layout is a property of the
// build, not of the class, and it is resolved once for BOTH uses: an enemy copy on the
// host and the peer's own pawn on the receive lane.
//
// The old lookup asked for a ScriptStruct *named* "DamageToApply".  That is the
// parameter's name, not the type's: the cooked tag lists the parameter as
// `StructProperty DamageToApply` whose type is `FB_DamageAttributeSet` (measured in
// EnemyBase_C's `ApplyDamageToHitboxTarget`).  Asking for the parameter name finds
// nothing on a build that names the type correctly, which would have disabled every
// damage path in silence.  The real name is tried first; the old one remains as a
// fallback for a build that exports it differently.
struct DamageStruct {
    bool         tried;
    bool         ok;
    LayoutResult fields;
    int          amountOff;
    int          amountSize;
    char         amountName[64];
    bool         haveAmount;
};
DamageStruct g_dmgStruct = {};

bool DamageStructInit()
{
    DamageStruct& s = g_dmgStruct;
    if (s.tried) return s.ok;
    s.tried = true;

    // The walk reads the class's FField chain with the discovered Layout.  Without a
    // validated Layout every candidate offset is a guess, and a guessed struct fill is
    // exactly the silent failure this path exists to make visible.
    if (!Lay().valid) {
        ReflectionReady();
        if (!Lay().valid) {
            QDLOG(LOG_ERROR, "damage: property layout not validated (", Lay().note,
                  "); damage refused");
            return false;
        }
    }

    void* ss = FindObjectByNameAndClass("FB_DamageAttributeSet", "ScriptStruct");
    if (!ss) ss = FindObjectByNameAndClass("DamageToApply", "ScriptStruct");
    if (!ss) ss = FindObjectByNameAndClass("FB_DamageAttributeSet", "UserDefinedStruct");
    if (!ss) {
        QDLOG(LOG_ERROR, "damage: the FB_DamageAttributeSet UScriptStruct is not loaded; "
                         "damage cannot be built");
        return false;
    }
    if (!WalkFieldsRaw(ss, Lay(), false, &s.fields)) {
        QDLOG(LOG_ERROR, "damage: FB_DamageAttributeSet properties could not be walked");
        return false;
    }
    int best = 0;
    for (int i = 0; i < s.fields.props; ++i) {
        const PropDesc& d = s.fields.p[i];
        bool isFloat = NameEquals(d.type, "FloatProperty") && d.size == 4;
        bool isDouble = NameEquals(d.type, "DoubleProperty") && d.size == 8;
        if (!isFloat && !isDouble) continue;
        int sc = DamageAmountScore(d.name);
        if (sc > best) {
            best = sc;
            s.amountOff = d.offset;
            s.amountSize = d.size;
            memcpy(s.amountName, d.name, sizeof(s.amountName));
            s.amountName[sizeof(s.amountName) - 1] = 0;
            s.haveAmount = true;
        }
    }
    // Dump the struct once at INFO: the live run then has the real field set, which is
    // the only way a wrong fill is diagnosable rather than mysterious.
    for (int i = 0; i < s.fields.props; ++i)
        QDLOG(LOG_INFO, "    FB_DamageAttributeSet.", s.fields.p[i].name, " : ",
              s.fields.p[i].type, " @", dec(s.fields.p[i].offset),
              " size=", dec(s.fields.p[i].size));
    if (!s.haveAmount) {
        QDLOG(LOG_ERROR, "damage: no plausible damage-amount field in FB_DamageAttributeSet");
        return false;
    }
    s.ok = true;
    QDLOG(LOG_INFO, "damage: FB_DamageAttributeSet fields=", dec(s.fields.props),
          ", amount -> ", s.amountName, " @", dec(s.amountOff),
          " size=", dec(s.amountSize));
    return true;
}

// One resolved TryApplyDamage PER CLASS.  The UFunction pointer is class-specific even
// though the signature is not: calling an enemy's implementation with the player pawn
// as `this` would run Blueprint bytecode against the wrong object layout, which fails
// as an access violation the __try cannot catch.  So the enemy's lane and the peer's
// own-pawn lane keep separate resolved functions, and only the payload struct is shared.
struct DamageApi {
    Fn              fn;
    bool            tried;
    bool            ok;
    const PropDesc* param;     // the DamageToApply parameter of THIS class's function
};
DamageApi g_enDmg = {};        // enemies (host-side convergence and peer hit intents)
DamageApi g_pawnDmg = {};      // the peer's own pawn (the G4 receive lane)

bool DamageApiInit(DamageApi& a, void* actor, const char* label)
{
    if (a.tried) return a.ok;
    a.tried = true;
    if (!a.fn.Resolve(actor, "TryApplyDamage")) {
        // A Blueprint interface message can live on the interface rather than on the
        // implementing class; the object scan is the fallback for that case.
        a.fn.fn = FindObjectByNameAndClass("TryApplyDamage", "Function");
        if (a.fn.fn) a.fn.sig = Signature(a.fn.fn);
    }
    if (!a.fn.fn || !a.fn.sig.valid) {
        QDLOG(LOG_ERROR, "damage: TryApplyDamage (FB_Damageable) not found on ", label,
              "; its HP cannot be driven from this module");
        return false;
    }
    a.param = nullptr;
    for (int i = 0; i < a.fn.sig.props; ++i) {
        const PropDesc& d = a.fn.sig.p[i];
        if (!NameEquals(d.type, "StructProperty")) continue;
        if (d.flags & CPF_OutParm) continue;
        if (!a.param || enemyown::HasCI(d.name, "DamageToApply")) a.param = &d;
    }
    if (!a.param) {
        QDLOG(LOG_ERROR, "damage: TryApplyDamage on ", label,
              " has no DamageToApply struct parameter");
        return false;
    }
    if (!DamageStructInit()) return false;
    // Never write outside the parameter: a struct whose PropertiesSize disagrees with
    // the function's parm block would otherwise scribble over the next one.
    if (g_dmgStruct.amountOff + g_dmgStruct.amountSize > a.param->size) {
        QDLOG(LOG_ERROR, "damage: ", g_dmgStruct.amountName, " lies outside ",
              a.param->name, " size=", dec(a.param->size), " on ", label);
        return false;
    }
    QDLOG(LOG_INFO, "damage: TryApplyDamage param ", a.param->name, " size=",
          dec(a.param->size), " on ", label, ", amount -> ", g_dmgStruct.amountName,
          " @", dec(g_dmgStruct.amountOff));
    a.ok = true;
    return true;
}

// The HP oracle for each kind of target.  Enemies answer GetHP/GetMaxHP (the same
// read-back the enemy convergence path already trusts).  The player does not have
// `GetHP` -- its current HP is the `Health` FloatProperty in its own tag list -- so it
// is read as a value; asking for a function that is not there would report "no effect"
// for a hit that actually landed.
bool ActorHp(void* actor, bool player, float* hp, float* maxHp)
{
    if (!actor || !hp) return false;
    if (player) {
        if (!FloatPropC(actor, "Health", hp)) return false;
        if (maxHp) {
            static Fn fMax = {};
            if (!CallGetFloat(actor, fMax, "GetMaxHP", maxHp)) *maxHp = 0.0f;
        }
        return true;
    }
    return EnemyHp(actor, hp, maxHp);
}

// Apply an amount to ANY implementer.  This is the part `ApplyEnemyDamage` already did
// correctly: the payload is walked from reflection and filled by field name, the HP is
// read before and after, and the HP movement is the only proof it worked.  Factoring it
// out is what lets the peer apply the same message to its own pawn instead of the two
// directions drifting apart.
//
// `before`/`after`/`haveHp` are optional out-parameters so the caller can log the
// outcome with the attacker's identity in one line.
bool ApplyDamageToActor(void* actor, float amount, void* causer, bool player,
                        float* beforeOut, float* afterOut, bool* haveHpOut)
{
    if (beforeOut) *beforeOut = 0.0f;
    if (afterOut) *afterOut = 0.0f;
    if (haveHpOut) *haveHpOut = false;
    if (!actor || !ObjectIsLive(actor) || !(amount > 0.0f)) return false;

    DamageApi& a = player ? g_pawnDmg : g_enDmg;
    const char* label = player ? "the local player" : "an enemy";
    if (!DamageApiInit(a, actor, label)) return false;

    float before = 0, after = 0, mx = 0;
    bool haveBefore = ActorHp(actor, player, &before, &mx);

    Call c(a.fn);
    uint8_t* base = c.At(a.param->name, a.param->size);
    if (!base) return false;
    if (g_dmgStruct.amountSize == 8) {
        double v = (double)amount;
        memcpy(base + g_dmgStruct.amountOff, &v, 8);
    } else {
        memcpy(base + g_dmgStruct.amountOff, &amount, 4);
    }
    for (int i = 0; i < g_dmgStruct.fields.props; ++i) {
        const PropDesc& d = g_dmgStruct.fields.p[i];
        if (!NameEquals(d.type, "ObjectProperty") || d.size < 8) continue;
        if (d.offset + 8 > a.param->size) continue;
        void* v = nullptr;
        if (CauserField(d.name)) v = causer;
        else if (TargetField(d.name)) v = actor;
        else continue;
        memcpy(base + d.offset, &v, 8);
    }
    bool ran = c.Run(actor);
    float ret = 0.0f;
    if (c.Ret()) memcpy(&ret, c.Ret(), 4);

    bool haveAfter = ActorHp(actor, player, &after, &mx);
    bool took = haveBefore && haveAfter && after < before;
    if (beforeOut) *beforeOut = before;
    if (afterOut) *afterOut = after;
    if (haveHpOut) *haveHpOut = haveBefore && haveAfter;

    static int failures = 0;
    if (!took && haveBefore && haveAfter && failures++ < 5) {
        // The enemy variant is SYNC-GAPS section 5's acceptance criterion; keep the
        // exact "enemy damage had NO effect" wording so a live run can grep for it.
        if (player)
            QDLOG(LOG_WARN, "player damage had NO effect: TryApplyDamage ",
                  (ran ? "ran" : "did not run"), " asked ", flt(amount, 1),
                  " returned ", flt(ret, 2), " hp ", flt(before, 1), " -> ",
                  flt(after, 1), " (if this repeats, the FB_DamageAttributeSet fill is wrong)");
        else
            QDLOG(LOG_WARN, "enemy damage had NO effect: TryApplyDamage ",
                  (ran ? "ran" : "did not run"), " asked ", flt(amount, 1),
                  " returned ", flt(ret, 2), " hp ", flt(before, 1), " -> ",
                  flt(after, 1), " (if this repeats, the FB_DamageAttributeSet fill is wrong)");
    }
    return took;
}

bool ApplyEnemyDamage(void* actor, float amount, void* causer)
{
    return ApplyDamageToActor(actor, amount, causer, false, nullptr, nullptr, nullptr);
}

// ---------------------------------------------------------------------------
// G5: a spawner that only sees ITS OWN player re-creates an enemy the partner is
// already fighting
// ---------------------------------------------------------------------------
// `PlayerIsInActivationRange` and `PlayerDistanceDespawnCheck` are Blueprint functions on
// the spawner that read `GetPlayerCharacter` -- player 0 of THIS machine -- so the host's
// spawner cannot see that the partner is standing next to the enemy.  When the host walks
// up to an enemy the partner has been fighting, the host's spawner produces a FRESH actor
// at full HP, ownership moves to it, and the partner's copy is then driven from rows that
// describe a fight that never happened.
//
// The mod cannot change that answer (those functions are Blueprint, and only reachable
// through a patched asset), and it does not try to: it refuses to let the re-created
// actor be a fresh fight.  The partner's own row for the same GUID is the HP it has
// already taken off, and applying the difference is the game's own damage path, so the
// fight continues exactly where it was.  Nothing is ever raised -- see
// playerbody::AdoptedHpFrac.
//
// The distance pair in the line is the point: it is the only place a live run can see
// that the game's range check and the partner's position disagree, which is what the
// spawner's decision was based on.
void AdoptOnRespawn(EnemyRec& r)
{
    r.fresh = false;
    if (!Cfg().enemy_sync || !g_net.Connected()) return;
    if (!r.haveGuid || !ObjectIsLive(r.actor)) return;
    // Only the machine that PUBLISHES this enemy can make the correction stick: a
    // follower converges from the row anyway, and a machine with no actor has none.
    if (!g_own.Running(r.guid)) return;
    const enemy::HostRow* row = g_hostView.Get(r.guid);
    if (!row) return;

    uint32_t now = Net::Now();
    uint32_t age = (now > row->rx_ms) ? now - row->rx_ms : 0;
    bool freshRow = age <= enemy::HostView::kFreshMs;

    float hp = 0.0f, mx = 0.0f;
    bool haveLocal = EnemyHp(r.actor, &hp, &mx);
    uint16_t localFrac = haveLocal ? enemy::HpFrac(hp, mx) : 0;
    uint16_t want = playerbody::AdoptedHpFrac(localFrac, haveLocal, row->hp, true, freshRow);
    if (!haveLocal || want >= localFrac) return;
    float d = enemy::ConvergeDamage(hp, mx, want, true);
    if (d <= 0.0f) return;

    ApplyEnemyDamage(r.actor, d, g_localPawn);
    float after = hp;
    EnemyHp(r.actor, &after, &mx);

    float dl = -1.0f, dp = -1.0f;
    ActorXform ex = {}, lx = {}, px = {};
    if (ReadActorXform(r.actor, &ex)) {
        const float e[3] = {ex.px, ex.py, ex.pz};
        if (ReadActorXform(g_localPawn, &lx)) {
            const float a[3] = {lx.px, lx.py, lx.pz};
            dl = playerbody::Dist3(e, a);
        }
        if (g_puppetActor && ObjectIsLive(g_puppetActor) && ReadActorXform(g_puppetActor, &px)) {
            const float b[3] = {px.px, px.py, px.pz};
            dp = playerbody::Dist3(e, b);
        }
    }
    QDLOG(LOG_INFO, "enemy spawn adopted guid=", hex32(r.guid[0]), " hp ", flt(hp, 1), " -> ",
          flt(after, 1), ": the partner was already fighting it (local ", flt(dl, 0),
          "cm, partner ", flt(dp, 0), "cm; the spawner's range check reads the local "
          "player only)");
}

// ---------------------------------------------------------------------------
// The two numbers the K_DAMAGE lane must not invent (SYNC-RESEARCH 1c, section A)
// ---------------------------------------------------------------------------
// DAMAGE.  The value the game applies is the `DamageAmount` field of the
// `FB_DamageAttributeSet` that `GetAttackDamageForSelectedAttack` returns.
// `ApplyDamageToHitboxTarget` calls exactly that getter (its bytecode references
// `CallFunc_GetAttackDamageForSelectedAttack_FB_DamageAttributeSet` and reads
// `ActiveHitboxDamageMult`, which it passes as `ExtraMult`) and hands the result to
// `TryApplyDamage`.  Reproducing that call is therefore the game's own number.
//
// The chain it is built on, measured from the cooked packages:
//     EnemyBase_C.Data          ObjectProperty -> the EnemyData_C asset
//     EnemyData_C.BaseDamage    FloatProperty  (Enemies/Shared/EnemyData.uasset)
//     EnemyBase_C.Get Enemy Base Damage  7 BaseDamage FName sites; returns that value
//     EnemyBase_C.Get Attack Damage(ExtraMult, AttackIndex) -> FB_DamageAttributeSet
//     EnemyBase_C.GetAttackDamageForSelectedAttack(ExtraMult) -> that struct
// So if the getter cannot be called, `Data.BaseDamage * ActiveHitboxDamageMult` is
// still a measured property read (every factor named), and only if that also fails is
// the constant an estimate.
bool EnemySwingDamage(void* actor, float* out, const char** source)
{
    if (out) *out = 0.0f;
    if (source) *source = "none";
    if (!out || !actor || !DamageStructInit()) return false;

    // ActiveHitboxDamageMult is the per-swing multiplier `Activate Hitbox Advanced`
    // writes and `ApplyDamageToHitboxTarget` passes as ExtraMult.
    float mult = 1.0f;
    FloatPropC(actor, "ActiveHitboxDamageMult", &mult);

    static Fn fSel = {};
    if (fSel.Resolve(actor, "GetAttackDamageForSelectedAttack")) {
        Call c(fSel);
        bool put = false;
        if (fSel.Off("ExtraMult") >= 0) {
            c.F32("ExtraMult", mult);
            put = true;
        } else if (fSel.sig.props > 0) {
            c.F32(fSel.sig.p[0].name, mult);
            put = true;
        }
        uint8_t* ret = (put && c.Run(actor)) ? c.Ret() : nullptr;
        if (ret) {
            const PropDesc* rp = nullptr;
            for (int i = 0; i < fSel.sig.props; ++i)
                if (fSel.sig.p[i].flags & CPF_ReturnParm) rp = &fSel.sig.p[i];
            // The return is the same struct type the payload walk measured; comparing
            // the offset against the return's own size is what keeps a different
            // struct from being read past its end.
            if (rp && g_dmgStruct.amountOff + g_dmgStruct.amountSize <= rp->size) {
                float v = 0.0f;
                if (g_dmgStruct.amountSize == 8) {
                    double dv = 0.0;
                    memcpy(&dv, ret + g_dmgStruct.amountOff, 8);
                    v = (float)dv;
                } else {
                    memcpy(&v, ret + g_dmgStruct.amountOff, 4);
                }
                if (v > 0.0f && v < 1.0e6f) {
                    *out = v;
                    if (source) *source = "GetAttackDamageForSelectedAttack";
                    return true;
                }
            }
        }
    }

    // The property the getter is built on.  This is a measured value, not an estimate;
    // it just omits the selected attack's own extra multiplier.
    void* data = ObjPropC(actor, "Data");
    float base = 0.0f;
    if (data && FloatPropC(data, "BaseDamage", &base) && base > 0.0f && base < 1.0e6f) {
        *out = base * (mult > 0.0f ? mult : 1.0f);
        if (source) *source = "Data.BaseDamage";
        return *out > 0.0f;
    }
    static Fn fBase = {};
    if (CallGetFloat(actor, fBase, "Get Enemy Base Damage", &base) &&
        base > 0.0f && base < 1.0e6f) {
        *out = base * (mult > 0.0f ? mult : 1.0f);
        if (source) *source = "Get Enemy Base Damage";
        return *out > 0.0f;
    }
    return false;
}

// RANGE.  `GetAttackRange()` is the AI's reach for the attack it currently has
// selected: its payload references `SelectedAttackConfig` and
// `GetAttackRange_Base.{MinRange,MaxRange}`, and returns the `Range` FloatProperty.
// `LargestAttackRange` is the enemy's own cached maximum (read by
// `CacheAndInitializeData`), so it is a conservative second choice.  Only if neither
// resolves does the caller send with the stated approximation constant.
bool EnemyAttackRange(void* actor, float* out, const char** source)
{
    if (out) *out = 0.0f;
    if (source) *source = "none";
    if (!out || !actor) return false;

    static Fn fRange = {};
    float v = 0.0f;
    if (CallGetFloat(actor, fRange, "GetAttackRange", &v) && v > 0.0f && v < 1.0e6f) {
        *out = v;
        if (source) *source = "GetAttackRange";
        return true;
    }
    if (FloatPropC(actor, "LargestAttackRange", &v) && v > 0.0f && v < 1.0e6f) {
        *out = v;
        if (source) *source = "LargestAttackRange";
        return true;
    }
    *out = enemydmg::kFallbackRangeCm;
    if (source) *source = "APPROXIMATION";
    return false;
}

// Freeze a copy (the other machine runs it) or hand it back to its own AI.
// Idempotent on purpose: freeze/unfreeze is driven by an ownership state that can be
// re-evaluated every frame, and a second freeze would read the movement mode it last
// wrote (MOVE_None) into savedMoveMode, so the copy would come back walking in place.
void EnemyDrive(EnemyRec& r, bool on)
{
    if (r.driven == on) return;
    static Fn fTick = {}, fTickCtl = {}, fMode = {}, fStop = {};
    void* a = r.actor;
    void* ctl = ObjPropC(a, "Controller");
    void* cmc = ObjPropC(a, "CharacterMovement");
    // The game's own stop is EnemySpawner_C.SetEnemiesPaused(bool), which writes
    // EnemyBase_C.Paused (BoolProperty, +0x1F4E).  That is the first lever; the native
    // bIsAIDisabled is the second.  Both are logged, because a copy that was frozen by
    // neither is a statue that still attacks.  Paused is saved on freeze and restored on
    // release: the game may have set it for its own reasons.
    if (on) BytePropC(a, "Paused", &r.savedPaused);
    uint8_t pausedWant = on ? 1 : (r.savedPaused ? 1 : 0);
    bool paused = WriteBytePropC(a, "Paused", pausedWant);
    bool aiFlag = WriteBytePropC(a, "bIsAIDisabled", on ? 1 : 0);
    bool tick = CallBool1(a, fTick, "SetActorTickEnabled", !on);
    if (ctl) {
        CallBool1(ctl, fTickCtl, "SetActorTickEnabled", !on);
        if (on) CallNoArgs(ctl, fStop, "StopMovement");
    }
    if (cmc) {
        if (on) BytePropC(cmc, "MovementMode", &r.savedMoveMode);
        // MOVE_None while driven: otherwise the movement component re-derives velocity
        // and gravity every tick and fights the position written from the host's row
        // (Dolls F-12.16).  Both arguments, always.
        if (fMode.Resolve(cmc, "SetMovementMode")) {
            Call c(fMode);
            uint8_t mode = on ? 0 : (r.savedMoveMode ? r.savedMoveMode : 1);
            if (fMode.sig.props > 0) c.U8(fMode.sig.p[0].name, mode);
            if (fMode.sig.props > 1) c.U8(fMode.sig.p[1].name, 0);
            c.Run(cmc);
        }
    }
    r.driven = on;
    QDLOG(LOG_INFO, "enemy ", hex32(r.guid[0]), (on ? " DRIVEN by owner" : " released to local AI"),
          " (Paused ", (paused ? "ok" : "MISSING"), "=", dec((int)pausedWant),
          ", bIsAIDisabled ", (aiFlag ? "ok" : "MISSING"),
          ", tick ", (tick ? "ok" : "FAILED"),
          ", controller ", (ctl ? "yes" : "none"), ")");
}

// ---------------------------------------------------------------------------
// Animation: what an enemy is playing, read from its mesh and replayed on the copy
// ---------------------------------------------------------------------------
// The same chain the player's arms use (LocalArmsMontage): SkeletalMeshComponent ->
// GetAnimInstance -> GetCurrentActiveMontage.  An enemy is an ACharacter, so its
// montage runs on the inherited `Mesh`, not on the first person `Arms`.
void* EnemyMesh(void* actor)
{
    void* m = ObjPropC(actor, "Mesh");
    if (!m) m = ObjPropC(actor, "CharacterMesh0");   // a build that renamed it
    return m;
}

void* MeshAnimInstance(void* mesh)
{
    static Fn f = {};
    if (!mesh || !f.Resolve(mesh, "GetAnimInstance")) return nullptr;
    Call c(f);
    if (!c.Run(mesh) || !c.Ret()) return nullptr;
    void* ai = *(void**)c.Ret();
    return LooksLikeUObject(ai) ? ai : nullptr;
}

void* MeshActiveMontage(void* mesh)
{
    void* ai = MeshAnimInstance(mesh);
    static Fn f = {};
    if (!ai || !f.Resolve(ai, "GetCurrentActiveMontage")) return nullptr;
    Call c(f);
    if (!c.Run(ai) || !c.Ret()) return nullptr;
    return *(void**)c.Ret();
}

// Play one montage on a copy.  "called=yes is not evidence the call ran", so the active
// montage is read back afterwards and logged: a live run can then tell a working
// reflective Montage_Play from one that silently did nothing.
bool PlayEnemyMontage(void* mesh, void* montage)
{
    void* ai = MeshAnimInstance(mesh);
    static Fn f = {};
    if (!ai || !montage || !f.Resolve(ai, "Montage_Play")) return false;
    Call c(f);
    c.Obj("MontageToPlay", montage);
    if (f.Off("InPlayRate") >= 0) c.F32("InPlayRate", 1.0f);
    // bStopAllMontages defaults to true; the argument block is zeroed, so it has to be
    // set explicitly or an attack montage would layer over whatever is playing.
    if (f.Off("bStopAllMontages") >= 0) c.U8("bStopAllMontages", 1);
    bool ran = c.Run(ai);
    void* back = MeshActiveMontage(mesh);
    QDLOG(LOG_INFO, "enemy detail: Montage_Play ", (ran ? "called" : "FAILED"),
          " read-back ", (back == montage ? "ok" : (back ? "a different montage" : "none")));
    return ran && back == montage;
}

// The status byte properties this build actually carries.  `bEnemyPoisoned` is the
// shipped name (analysis/exe_names.txt); `Poisoned` is a fallback.  A missing property
// is reported, never invented.
uint16_t EnemyStatusMask(void* actor)
{
    uint8_t v = 0;
    // One name, not a fallback chain: if `bEnemyPoisoned` exists its zero means "not
    // poisoned", and reading a second name for the same fact could contradict it.
    bool havePoison = BytePropC(actor, "bEnemyPoisoned", &v) || BytePropC(actor, "Poisoned", &v);
    bool poisoned = havePoison && v != 0;
    uint8_t s = 0;
    bool staggered = BytePropC(actor, "bIsStaggered", &s) && s != 0;
    return enemyown::StatusMask(poisoned, staggered);
}

void MirrorEnemyStatus(void* actor, uint16_t mask)
{
    uint8_t cur = 0;
    const char* poison = BytePropC(actor, "bEnemyPoisoned", &cur) ? "bEnemyPoisoned"
                       : (BytePropC(actor, "Poisoned", &cur) ? "Poisoned" : nullptr);
    const char* stag = BytePropC(actor, "bIsStaggered", &cur) ? "bIsStaggered" : nullptr;
    bool wrotePoison = poison && WriteBytePropC(actor, poison, (mask & (uint16_t)wire::ST_POISON) ? 1 : 0);
    bool wroteStag = stag && WriteBytePropC(actor, stag, (mask & (uint16_t)wire::ST_HEAVY) ? 1 : 0);
    QDLOG(LOG_INFO, "enemy status applied mask=", hex32(mask),
          " poison ", (poison ? (wrotePoison ? "ok" : "WRITE FAILED") : "property absent"),
          " stagger ", (stag ? (wroteStag ? "ok" : "WRITE FAILED") : "property absent"));
}

// ---------------------------------------------------------------------------
// TargetPlayer: the lever the player-class body unlocks (SYNC-RESEARCH 1a)
// ---------------------------------------------------------------------------
// The AI reads `TargetPlayer` DIRECTLY -- it is a Blueprint ObjectProperty on
// EnemyBase_C (+0x0C90, 18 bytecode sites) and `GetTargetPlayer` is never called -- so
// writing the variable is the only mechanism that reaches the Blueprint.  Before the
// body existed there was no value worth writing it to (both cast gates reject a
// SkeletalMeshActor); now there is, and the rule for WHEN is qdplayerbody.h's.
//
// Read and write are guarded, and a write is read back: a property whose offset was
// found on a class that is not the one the actor has (a Blueprint class is its own
// layout) would otherwise scribble eight bytes of an unrelated field.
struct TgtApi { void* cls; int off; };
TgtApi g_tgtApi[32];
int    g_tgtApiN = 0;

int EnemyTargetOff(void* cls)
{
    if (!cls) return -1;
    for (int i = 0; i < g_tgtApiN; ++i) if (g_tgtApi[i].cls == cls) return g_tgtApi[i].off;
    int size = 0;
    int off = FindPropertyOffsetIn(cls, "TargetPlayer", &size);
    if (off >= 0 && size != 8) off = -1;      // an object reference is eight bytes
    if (g_tgtApiN < (int)(sizeof(g_tgtApi) / sizeof(g_tgtApi[0]))) {
        g_tgtApi[g_tgtApiN].cls = cls;
        g_tgtApi[g_tgtApiN].off = off;
        ++g_tgtApiN;
    }
    return off;
}

void* ReadEnemyTarget(void* actor, int off)
{
    __try { return *(void**)((uint8_t*)actor + off); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

bool WriteEnemyTarget(void* actor, int off, void* v)
{
    __try {
        *(void**)((uint8_t*)actor + off) = v;
        return *(void**)((uint8_t*)actor + off) == v;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// True when this enemy is aimed at the proxy body.  `g_proxySeen` and not `g_puppetActor`
// on purpose: after the body is destroyed the property still holds the old pointer, and
// that is precisely the state that has to be recognised and repaired.
bool EnemyTargetsProxy(void* actor)
{
    if (!actor || !g_proxySeen) return false;
    if (!Cfg().player_body) return false;
    int off = EnemyTargetOff(ObjClass(actor));
    if (off < 0) return false;
    return ReadEnemyTarget(actor, off) == g_proxySeen;
}

// ---------------------------------------------------------------------------
// The host describes a swing the partner could not have felt (G4)
// ---------------------------------------------------------------------------
// The trigger is the same montage change the detail lane already detects.  One
// K_DAMAGE per new montage, keyed by the detail sequence, so a re-classification of the
// same swing (a status byte changed, the pump ran again) cannot send it twice.
//
// The condition is deliberately conservative.  The host only describes a swing when it
// is the machine actually running the enemy, the partner is in the game world, the two
// machines agree on the map, the partner's interpolated body exists, and that body is
// inside the attack's own range.  Every one of those is a reason NOT to invent a hit
// that the game never decided.
void TrySendEnemyDamage(EnemyRec& r, uint32_t seq, uint8_t anim, uint32_t now)
{
    if (!Cfg().enemy_sync) return;
    if (!g_net.IsHost() || !g_net.Connected()) return;
    // The detail caller already filters O_LOCAL, but the gate is repeated because this
    // is the function that decides who may describe a hit; a future caller must not be
    // able to make a frozen follower send one.
    if (!r.haveGuid || g_own.State(r.guid) != enemyown::O_LOCAL) return;

    // A swing the enemy aimed at the proxy body is resolved by the GAME: the hit lands on
    // a real player-class actor, `PlayerBodyObserve` forwards its own damage payload, and
    // describing the same swing here from the montage would apply it twice.
    if (playerbody::SwingAlreadyReal(g_puppetActor != nullptr && ObjectIsLive(g_puppetActor),
                                     EnemyTargetsProxy(r.actor))) {
        static uint32_t lastReal = 0;
        if (now - lastReal > 5000) {
            lastReal = now;
            QDLOG(LOG_DEBUG, "damage: guid=", hex32(r.guid[0]),
                  " swing handled by the game's own hit path; the described lane stays quiet");
        }
        return;
    }

    // E_ATTACK is the melee vocabulary (attack/slash/swing/claw/bite/sting/punch/swipe)
    // and E_CAST is the ranged one (cast/spell/magic/summon/spit) in ClassifyAnim.  The
    // range test below is what keeps a self-centred buff from being read as a hit.
    bool melee = (anim == wire::E_ATTACK);
    bool ranged = (anim == wire::E_CAST);
    if (!melee && !ranged) return;

    enemydmg::Context c;
    c.host_owns     = true;
    c.peer_in_world = g_peerInGameplay;
    c.worlds_agree  = g_map.PeerStateApplies(now);

    // The peer's drawn body, exactly as the player lane maintains it.  A frozen body is
    // no position at all (qdmotion.h), and a hit cannot be decided against it.
    motion::MoveOut m = g_peerInterp.Sample(now);
    c.have_peer_pos = m.valid && !m.frozen && !m.should_hide;

    ActorXform x = {};
    bool haveEnemy = ReadActorXform(r.actor, &x);
    c.have_enemy_pos = haveEnemy;

    float range = 0.0f;
    const char* rangeSrc = "none";
    EnemyAttackRange(r.actor, &range, &rangeSrc);
    c.range_cm = range;

    float ex[3] = {x.px, x.py, x.pz};
    float peer[3] = {m.px, m.py, m.pz};
    if (!enemydmg::ShouldSend(c, ex, peer)) {
        // Debug level and rate-limited: an out-of-range swing is the normal case and
        // must not fill the log of a fight that is working.
        static uint32_t lastRefusal = 0;
        if (now - lastRefusal > 5000) {
            lastRefusal = now;
            QDLOG(LOG_DEBUG, "damage: swing not sent guid=", hex32(r.guid[0]),
                  " owner=", dec(c.host_owns ? 1 : 0),
                  " peerInWorld=", dec(c.peer_in_world ? 1 : 0),
                  " worldsAgree=", dec(c.worlds_agree ? 1 : 0),
                  " havePos=", dec((c.have_enemy_pos && c.have_peer_pos) ? 1 : 0),
                  " range=", flt(range, 0), " (", rangeSrc, ")");
        }
        return;
    }

    // The gate consumes the swing only after the range test passed, so a partner who
    // was out of reach does not use up the per-second budget.
    if (!g_dmgGate.Admit(r.guid, (uint8_t)seq, now)) {
        static uint32_t lastGate = 0;
        if (now - lastGate > 5000) {
            lastGate = now;
            QDLOG(LOG_DEBUG, "damage: gate dropped guid=", hex32(r.guid[0]),
                  " seq=", dec((int)seq), " repeats=", dec(g_dmgGate.RepeatDrops()),
                  " rate=", dec(g_dmgGate.RateDrops()));
        }
        return;
    }

    float amount = 0.0f;
    const char* dmgSrc = "none";
    bool measured = EnemySwingDamage(r.actor, &amount, &dmgSrc);
    if (!measured) {
        // The one case the wire has a bit for: no reliable property resolved, so say
        // so.  An estimate that is silently presented as measured turns a balance bug
        // into a sync bug.
        amount = enemydmg::kFallbackDamage;
        dmgSrc = "ESTIMATE";
    }
    int32_t centi = enemydmg::Centi(amount);
    if (centi <= 0) {
        QDLOG(LOG_WARN, "damage: refusing to send a non-positive amount for guid=",
              hex32(r.guid[0]));
        return;
    }

    wire::DamageBody d;
    memset(&d, 0, sizeof(d));
    memcpy(d.guid, r.guid, sizeof(d.guid));
    d.amount_centi = centi;
    d.flags = melee ? wire::kDamageMelee : wire::kDamageRanged;
    if (!measured) d.flags |= wire::kDamageGuessed;

    uint8_t body[wire::kMaxBody];
    wire::Writer w(body, sizeof(body));
    wire::WriteDamage(w, d);
    if (w.ok && g_net.SendReliable(wire::K_DAMAGE, body, w.len)) {
        QDLOG(LOG_INFO, "damage sent guid=", hex32(r.guid[0]), " seq=", dec((int)seq),
              " ", (melee ? "melee" : "ranged"), " amount=", flt(amount, 1),
              " centi=", dec(centi), " (", dmgSrc, ")", " range=", flt(range, 0),
              " (", rangeSrc, ")", (!measured ? " ESTIMATED" : ""));
    } else {
        QDLOG(LOG_WARN, "damage: K_DAMAGE send failed guid=", hex32(r.guid[0]),
              " seq=", dec((int)seq));
    }
}

// At most this many enemy details per second across all enemies.  The reliable table has
// 16 slots and a loaded level holds ~78 enemies: measured on the rig, publishing the
// first observation of every enemy filled the table in one frame and the socket then
// dropped everything else with "no free reliable slot" -- travel and intents included.
// Detail is cosmetic (a dropped animation is re-sent on the next change), so it is the
// lane that yields.
static const uint32_t kEnemyDetailPerSec = 10;
uint32_t g_detailTokens = kEnemyDetailPerSec;
uint32_t g_detailRefillMs = 0;
uint32_t g_detailDropped = 0;

// The owner publishes what the enemy is doing.  Sent on change only: a montage is an
// event, not a sample, and a status mask only changes when a debuff lands or expires.
void SendEnemyDetail(EnemyRec& r, uint32_t now)
{
    void* mesh = EnemyMesh(r.actor);
    void* mon = mesh ? MeshActiveMontage(mesh) : nullptr;
    uint16_t mask = EnemyStatusMask(r.actor);

    if (now - g_detailRefillMs >= 1000) { g_detailRefillMs = now; g_detailTokens = kEnemyDetailPerSec; }

    // The first sight of an enemy is its BASELINE, not news.  At level load every enemy
    // in range is already playing something, so treating the first observation as a
    // change sent one packet per enemy at once.
    if (!r.pubInit) {
        r.pubInit = true;
        r.pubMon = mon;
        r.pubStatus = mask;
        r.pubSeq = mon ? 1 : 0;
        return;
    }

    bool monChanged = (mon != r.pubMon);
    bool statusChanged = (mask != r.pubStatus);
    if (!monChanged && !statusChanged) return;

    wire::EnemyDetailBody d;
    memset(&d, 0, sizeof(d));
    memcpy(d.guid, r.guid, 16);
    d.status = mask;
    if (mon) {
        char nm[128] = "";
        FNameToString(ObjNameRaw(mon), nm, sizeof(nm));
        d.anim = enemyown::ClassifyAnim(nm);
        if (avatar::AssetPath(mon, d.path, sizeof(d.path)) <= 0)
            QDLOG(LOG_WARN, "enemy detail: montage ", nm, " has no asset path");
    }
    // A montage that classifies to nothing (an idle, trap or locomotion animation) has
    // no meaning for the copy: it is recorded without spending a packet, unless the
    // status mask changed in the same frame.
    if (!(mon && d.anim != wire::E_NONE) && !statusChanged) { r.pubMon = mon; return; }

    if (g_detailTokens == 0) {
        // Left unrecorded on purpose: the change is retried on the next pump, when a
        // token is free, instead of being lost.
        ++g_detailDropped;
        return;
    }
    --g_detailTokens;

    // The counter bumps per NEW montage.  A status-only change keeps it, so the copy
    // does not restart a one-shot animation because poison ticked.
    if (monChanged && mon) { if (++r.pubSeq == 0) r.pubSeq = 1; }
    d.seq = r.pubSeq;

    uint8_t body[wire::kMaxBody];
    wire::Writer w(body, sizeof(body));
    wire::WriteEnemyDetail(w, d);
    bool sent = w.ok && g_net.SendReliable(wire::K_ENEMY_DETAIL, body, w.len);

    // A new attack montage is also the moment to describe the hit to the partner: the
    // owner is the only machine that saw the swing, and the peer is the only machine
    // that can apply it to its own pawn.  Keyed by the same seq, so it is one per
    // occurrence rather than one per pump -- including when the detail itself had to be
    // retried.
    if (monChanged && mon && (d.anim == wire::E_ATTACK || d.anim == wire::E_CAST))
        TrySendEnemyDamage(r, d.seq, d.anim, now);

    if (!sent) {
        QDLOG(LOG_WARN, "enemy detail dropped (no reliable slot) guid=", hex32(r.guid[0]),
              " anim=", dec((int)d.anim), "; retried next pump, drops=", dec((int)g_detailDropped));
        return;
    }
    r.pubMon = mon;
    r.pubStatus = mask;
    QDLOG(LOG_INFO, "enemy detail sent guid=", hex32(r.guid[0]), " anim=", dec((int)d.anim),
          " seq=", dec((int)d.seq), " status=", hex32(mask),
          (mon ? " " : " (no montage)"), (mon ? d.path : ""));
}

// The follower applies the owner's animation and status to its frozen copy.
void OnEnemyDetail(wire::Reader& r, uint32_t now)
{
    wire::EnemyDetailBody d;
    if (!wire::ReadEnemyDetail(r, &d)) return;
    EnemyRec* er = EnemyByGuid(d.guid);
    if (!er || !ObjectIsLive(er->actor)) return;
    // Only a frozen follower applies it; the owner is the source of truth.
    if (!er->driven || g_own.State(d.guid) != enemyown::O_FROZEN) return;
    // A detail is the continuation of a row stream, so it is only valid while that
    // stream is live.  This is the detail lane's world-divergence guard: when the peer
    // is in another map its K_ENEMY rows stop applying (the travel gate refuses them)
    // and this GUID's row goes stale; an animation from the other world must not then
    // be played on a same-GUID enemy here.
    const enemy::HostRow* row = g_hostView.Get(d.guid);
    uint32_t age = (row && now > row->rx_ms) ? now - row->rx_ms : 0xFFFFFFFFu;
    if (!row || age > enemy::HostView::kFreshMs) {
        static int stale = 0;
        if (stale++ < 5)
            QDLOG(LOG_WARN, "enemy detail dropped: no fresh row for guid=", hex32(d.guid[0]));
        return;
    }

    if (d.status != er->gotStatus) {
        MirrorEnemyStatus(er->actor, d.status);
        er->gotStatus = d.status;
    }
    if (d.anim == wire::E_NONE || !d.path[0]) return;
    if (d.seq == er->gotSeq && strcmp(d.path, er->gotPath) == 0) return;

    void* montage = avatar::LoadAsset(d.path);
    void* mesh = EnemyMesh(er->actor);
    bool played = (montage && mesh) ? PlayEnemyMontage(mesh, montage) : false;
    if (!montage) QDLOG(LOG_WARN, "enemy detail: could not load ", d.path);
    er->gotSeq = d.seq;
    memcpy(er->gotPath, d.path, sizeof(er->gotPath));
    QDLOG(LOG_INFO, "enemy detail applied guid=", hex32(d.guid[0]), " anim=", dec((int)d.anim),
          " seq=", dec((int)d.seq), (played ? " played" : " NOT played"));
}

void SendEnemyIntentHit(const uint32_t guid[4], int32_t frac)
{
    uint8_t body[64];
    wire::Writer w(body, sizeof(body));
    wire::WriteIntentEnemyHit(w, 0, guid, frac);
    if (w.ok) g_net.SendReliable(wire::K_INTENT, body, w.len);
}

// ---------------------------------------------------------------------------
// Ownership by proximity (qdenemyown.h), applied to the local records
// ---------------------------------------------------------------------------
// Whichever machine runs an enemy publishes its rows; both machines apply incoming rows
// to their own copies.  Today K_ENEMY is no longer host -> peer only.
void EnemyOwnPump(uint32_t now)
{
    static uint32_t lastMs = 0, lastHpMs = 0;
    float dt = lastMs ? (float)(now - lastMs) / 1000.0f : 0.0f;
    lastMs = now;
    bool hpTick = now - lastHpMs >= 100;
    if (hpTick) lastHpMs = now;

    float mine[3] = {0, 0, 0};
    bool haveMine = false;
    { ActorXform x; if (IsGameplayPawn(g_localPawn) && ReadActorXform(g_localPawn, &x)) {
          mine[0] = x.px; mine[1] = x.py; mine[2] = x.pz; haveMine = true; } }
    float theirs[3] = {0, 0, 0};
    bool haveTheirs = false;
    // The partner's drawn position is slightly in the past, which the 800 cm margin in
    // qdenemyown.h absorbs; a frozen (silent) partner is no position at all.
    { motion::MoveOut m = g_peerInterp.Sample(now);
      if (m.valid && !m.frozen) {
          theirs[0] = m.px; theirs[1] = m.py; theirs[2] = m.pz; haveTheirs = true; } }

    for (int i = 0; i < g_enN; ++i) {
        EnemyRec& r = g_en[i];
        if (!r.haveGuid || !ObjectIsLive(r.actor)) continue;

        float ex[3] = {0, 0, 0};
        bool haveEx = false;
        float exYaw = 0.0f;
        { ActorXform x; if (ReadActorXform(r.actor, &x)) {
              ex[0] = x.px; ex[1] = x.py; ex[2] = x.pz; exYaw = x.yaw; haveEx = true; } }

        enemyown::OwnerState st = g_own.Tick(r.guid, true, ex, haveEx,
                                            mine, haveMine, theirs, haveTheirs, now);
        const enemy::HostRow* row = g_hostView.Get(r.guid);
        if (st == enemyown::O_FROZEN && row) {
            // A death the other machine ran reaches the machine that did not kill it.
            // Only a FOLLOWER takes a death from a row: an owner never gives up an enemy
            // it is simulating because the other machine's stale kill log still lists
            // it, and the owner's own game path already handles every death it causes.
            uint32_t age = (now > row->rx_ms) ? now - row->rx_ms : 0;
            if (!(row->flags & enemy::kAlive) && age <= enemy::HostView::kDeadStaleMs) {
                if (!r.killedByUs && !EnemyDead(r.actor)) {
                    float hp = 0, mx = 0;
                    if (EnemyHp(r.actor, &hp, &mx) && hp > 0.0f) {
                        QDLOG(LOG_INFO, "enemy ", hex32(r.guid[0]), ": the owner says dead, killing the copy");
                        // This death must not be re-broadcast as our own (no echo of a
                        // kill the other machine already recorded).
                        g_inRemoteDeath = true;
                        ApplyEnemyDamage(r.actor, enemy::ConvergeDamage(hp, mx, 0, false), g_localPawn);
                        g_inRemoteDeath = false;
                    }
                    r.killedByUs = true;
                }
                continue;
            }
            r.killedByUs = false;
            if (!r.driven) {
                EnemyDrive(r, true);
                if (haveEx) { r.pos[0] = ex[0]; r.pos[1] = ex[1]; r.pos[2] = ex[2]; r.yaw = exYaw; }
                r.lastHp = -1.0f;
            }
            // A frozen copy is not a statue: it follows the owner's position and yaw
            // exactly the way the old host-driven path did.
            float tgt[3] = {wire::DequantizePos(row->px), wire::DequantizePos(row->py),
                            wire::DequantizePos(row->pz)};
            enemy::Follow(r.pos, tgt, dt);
            r.yaw = enemy::FollowYaw(r.yaw, wire::DequantizeAngle(row->yaw), dt);
            SetActorXform(r.actor, r.pos[0], r.pos[1], r.pos[2], r.yaw);
            if (hpTick) {
                float hp = 0, mx = 0;
                if (EnemyHp(r.actor, &hp, &mx)) {
                    // A drop this mod did not cause is the local player hitting the
                    // copy: the OWNER decides what it did, so send it the delta.
                    int32_t hit = (r.lastHp >= 0.0f) ? enemy::HitFrac(r.lastHp, hp, mx) : 0;
                    if (hit > 0) SendEnemyIntentHit(r.guid, hit);
                    // The owner's HP is the truth and is never converged downward from
                    // the other machine's row.  Only a follower converges.
                    float d = enemy::ConvergeDamage(hp, mx, row->hp, true);
                    if (d > 0.0f) { ApplyEnemyDamage(r.actor, d, g_localPawn); EnemyHp(r.actor, &hp, &mx); }
                    r.lastHp = hp;
                    r.maxHp = mx;
                }
            }
        } else {
            // LOCAL or an in-flight HAND-OFF: the copy runs its own AI.  A hand-off
            // keeps simulating until the other machine's rows confirm it (qdenemyown.h).
            r.killedByUs = false;
            if (r.driven) EnemyDrive(r, false);
        }
    }
}

// Point the enemies this machine runs at whichever player is actually nearer.  This is
// the second half of G4: the described-damage lane made a distant partner hurtable, but
// the enemy still CHOSE the host every time, because `GetPlayerCharacter` on the host is
// the host.  Only a target the AI itself picked from the two players is ever redirected
// (qdplayerbody.h), and only by the machine that simulates the enemy -- a frozen copy
// keeps its own decision, because it is not making one.
void EnemyTargetPump(uint32_t now)
{
    if (!Cfg().enemy_sync || !Cfg().player_body) return;
    // K_DAMAGE, the lane a redirected hit travels on, is host -> peer: the peer never
    // sends a player's damage to the host, so on the peer this would only mislead the AI
    // it has released to its own controller.  G4's case is the host's enemies.
    if (!g_net.IsHost() || !g_net.Connected()) return;
    if (!IsGameplayPawn(g_localPawn)) return;
    (void)now;

    bool proxyOk = (g_puppetActor != nullptr && ObjectIsLive(g_puppetActor));
    // The same "the partner is reachable" test the damage lane uses: a frozen sample means
    // the body has not moved for seconds and a hidden one means the peer is gone, and
    // neither is a target worth walking to.
    { motion::MoveOut m = g_peerInterp.Sample(now);
      if (!m.valid || m.frozen || m.should_hide) proxyOk = false; }
    ActorXform lx = {};
    if (!ReadActorXform(g_localPawn, &lx)) return;
    ActorXform px = {};
    if (proxyOk) proxyOk = ReadActorXform(g_puppetActor, &px);
    if (proxyOk) g_proxySeen = g_puppetActor;

    for (int i = 0; i < g_enN; ++i) {
        EnemyRec& r = g_en[i];
        if (!r.haveGuid || !ObjectIsLive(r.actor)) continue;
        // A copy that follows the other machine's rows is not simulating, so it is not
        // choosing anything; changing its target would corrupt what the owner sends.
        if (!g_own.Running(r.guid)) continue;
        int off = EnemyTargetOff(ObjClass(r.actor));
        if (off < 0) continue;

        void* cur = ReadEnemyTarget(r.actor, off);
        int c = (cur != nullptr && cur == g_localPawn)      ? playerbody::CUR_LOCAL
              : (cur != nullptr && cur == g_proxySeen)      ? playerbody::CUR_REMOTE
              : (cur == nullptr)                            ? playerbody::CUR_NONE
                                                            : playerbody::CUR_OTHER;
        ActorXform ex = {};
        if (!ReadActorXform(r.actor, &ex)) continue;
        const float e[3] = {ex.px, ex.py, ex.pz};
        const float me[3] = {lx.px, lx.py, lx.pz};
        const float pr[3] = {px.px, px.py, px.pz};
        float dLocal = playerbody::Dist3(e, me);
        float dRemote = proxyOk ? playerbody::Dist3(e, pr) : -1.0f;

        playerbody::Tgt act = playerbody::ChooseTarget(true, proxyOk, c, dLocal, dRemote);
        if (act == playerbody::TGT_LEAVE) continue;
        void* want = (act == playerbody::TGT_TO_REMOTE) ? g_proxySeen : g_localPawn;
        bool ok = WriteEnemyTarget(r.actor, off, want);
        QDLOG(ok ? LOG_INFO : LOG_WARN, "enemy ", hex32(r.guid[0]), ": TargetPlayer ",
              (act == playerbody::TGT_TO_REMOTE ? "local player -> the remote body"
                                                : "remote body -> the local player"),
              (ok ? " (read back)" : " WRITE FAILED"),
              " dLocal=", flt(dLocal, 0), " dRemote=", flt(dRemote, 0));
    }
}

// Publish rows for the enemies this machine runs, plus a few recent deaths.  The
// kEnemyPeerOwns bit says "I have handed it to you"; a clear bit says "I run it".
void EnemySendPump(uint32_t now)
{
    static uint32_t last = 0;
    if (now - last < 50) return;      // 20 Hz (WIRE.md 3.9)
    last = now;
    static wire::EnemyRow rows[256];
    int n = 0;
    for (int i = 0; i < g_enN; ++i) {
        EnemyRec& r = g_en[i];
        if (!r.haveGuid || !ObjectIsLive(r.actor) || EnemyDead(r.actor)) continue;
        enemyown::Publish pub = g_own.Publishing(r.guid);
        if (pub == enemyown::PUB_NONE) continue;
        ActorXform x;
        if (!ReadActorXform(r.actor, &x)) continue;
        float hp = 0, mx = 0;
        if (!EnemyHp(r.actor, &hp, &mx) || hp <= 0.0f) continue;
        if (pub == enemyown::PUB_OWN && g_killLog.Count()) g_killLog.Remove(r.guid);  // respawned
        wire::EnemyRow& w = rows[n++];
        memcpy(w.guid, r.guid, 16);
        w.px = wire::QuantizePos(x.px); w.py = wire::QuantizePos(x.py); w.pz = wire::QuantizePos(x.pz);
        w.yaw = wire::QuantizeAngle(x.yaw);
        w.hp = enemy::HpFrac(hp, mx);
        w.flags = enemy::kAlive | enemy::kBody;
        if (pub == enemyown::PUB_HANDOFF) w.flags |= wire::kEnemyPeerOwns;
    }
    // Detail for the enemies this machine runs.  Reliable and on change only.
    for (int i = 0; i < g_enN; ++i) {
        EnemyRec& r = g_en[i];
        if (!r.haveGuid || !ObjectIsLive(r.actor)) continue;
        if (g_own.State(r.guid) != enemyown::O_LOCAL) continue;
        SendEnemyDetail(r, now);
    }

    int count = 0;
    int first = g_rowCursor.Take(n, 16, &count);     // 16 live + up to 4 dead = 20 rows
    wire::EnemyRow out[20];
    int m = 0;
    for (int i = 0; i < count; ++i) out[m++] = rows[first + i];
    for (int k = 0; k < 4 && g_killLog.Count() > 0; ++k) {
        if (g_deadCursor >= g_killLog.Count()) g_deadCursor = 0;
        const uint32_t* g = g_killLog.At(g_deadCursor++);
        wire::EnemyRow& w = out[m++];
        memset(&w, 0, sizeof(w));
        memcpy(w.guid, g, 16);
    }
    if (m == 0) return;
    uint8_t body[wire::kMaxBody];
    wire::Writer w(body, sizeof(body));
    wire::WriteEnemyHeader(w, (uint16_t)m);
    wire::WriteEnemyChunk(w, out, m, 0);
    if (w.ok) g_net.SendUnreliable(wire::K_ENEMY, body, w.len);
}

void OnEnemyRows(wire::Reader& r, uint32_t now)
{
    uint16_t n = 0;
    if (!wire::ReadEnemyHeader(r, &n)) return;
    for (int i = 0; i < n && i < 32; ++i) {
        wire::EnemyRow e;
        if (!wire::ReadEnemyRow(r, &e)) break;
        g_hostView.OnRow(e.guid, e.px, e.py, e.pz, e.yaw, e.hp, e.flags, now);
        // The owner says it is alive: a death this machine still held from when IT ran
        // the enemy is stale and must stop being broadcast.
        if ((e.flags & enemy::kAlive) && g_killLog.Count() &&
            g_own.State(e.guid) == enemyown::O_FROZEN)
            g_killLog.Remove(e.guid);
        // The claim bit is the sender's own statement of who runs it.  Feed it even for
        // a dead row: a frozen copy must not sit waiting on a stale claim.
        g_own.OnRemoteRow(e.guid, (e.flags & wire::kEnemyPeerOwns) != 0, now);
    }
    if (g_hostView.Overflowed()) {
        static bool said = false;
        if (!said) { said = true; QDLOG(LOG_WARN, "enemy sync: row view full; some enemies run locally"); }
    }
    if (g_own.Overflowed()) {
        static bool said = false;
        if (!said) { said = true; QDLOG(LOG_WARN, "enemy sync: ownership table full; some enemies run locally"); }
    }
}

// The owner applies a hit the other player landed on its frozen copy.
void ApplyEnemyHitIntent(const wire::IntentBody& b)
{
    if (!Cfg().enemy_sync) return;
    EnemyRec* r = EnemyByGuid(b.guid);
    if (!r || !ObjectIsLive(r->actor) || EnemyDead(r->actor)) return;
    float hp = 0, mx = 0;
    if (!EnemyHp(r->actor, &hp, &mx) || mx <= 0.0f) return;
    float dmg = mx * (float)b.value / 65535.0f;
    bool ok = ApplyEnemyDamage(r->actor, dmg, g_puppetActor ? g_puppetActor : g_localPawn);
    QDLOG(LOG_INFO, "hit on enemy ", hex32(b.guid[0]), " from the other player, ", flt(dmg, 1),
          (ok ? "" : " (no effect)"));
}

// A persistence record for an enemy the other machine ran.  Host-authoritative: the
// host owns the save, so it applies the record to its own copy of the world.
void ApplyEnemyRecordIntent(const wire::IntentBody& b)
{
    if (!Cfg().enemy_sync) return;
    EnemyRec* r = EnemyByGuid(b.guid);
    QdGuid g = {b.guid[0], b.guid[1], b.guid[2], b.guid[3]};
    float respawn = (float)b.value / 100.0f, hp = (float)b.value2 / 100.0f;
    void* ctx = (r && ObjectIsLive(r->spawner)) ? r->spawner : GWorldPtr();
    g_inOwnRecord = true;
    StoreEnemySaveState(ctx, g, respawn, hp);       // through the hook: lands in the kill log
    g_inOwnRecord = false;
    QDLOG(LOG_INFO, "enemy record applied guid=", hex32(b.guid[0]), " hp=", flt(hp, 1),
          " respawn=", flt(respawn, 1));
    // The host could have spawned it meanwhile: the record is the truth.
    if (hp <= 0.0f && r && ObjectIsLive(r->actor) && !EnemyDead(r->actor)) {
        float cur = 0, mx = 0;
        if (EnemyHp(r->actor, &cur, &mx)) ApplyEnemyDamage(r->actor, enemy::ConvergeDamage(cur, mx, 0, false), g_localPawn);
    }
}

// A non-host receives only the enemy hit intent; the rest belong to the host's world.
void PeerApplyEnemyIntent(wire::Reader& r)
{
    wire::IntentBody b;
    if (!wire::ReadIntent(r, &b)) return;
    if (b.kind == wire::I_ENEMY_HIT) ApplyEnemyHitIntent(b);
}

// ---------------------------------------------------------------------------
// The peer performs the hit the host could not (G4, WIRE.md 3.15b)
// ---------------------------------------------------------------------------
// This is the other half of TrySendEnemyDamage: the host described an enemy's swing,
// and only this machine can apply it because only this machine owns the pawn.  The
// application goes through the SAME TryApplyDamage message the game would have sent --
// so hit reactions, resist, status and the death path all run -- and the HP read-back
// is the proof, exactly as on the enemy path.
void OnPlayerDamage(wire::Reader& r, uint32_t now)
{
    if (!Cfg().enemy_sync) return;
    // K_DAMAGE is host -> peer.  A host that receives one is looking at a stale or
    // misdirected packet and must not damage its own player with it.
    if (!g_net.IsPeer()) return;

    wire::DamageBody d;
    if (!wire::ReadDamage(r, &d)) return;
    float amount = (float)d.amount_centi / 100.0f;
    if (!(amount > 0.0f)) return;

    // A world the two machines disagree about has its own enemies and its own FGuids.
    // The pre-switch divergence filter does not name K_DAMAGE (that filter belongs to
    // the streaming stream), so the same gate is asked here.
    if (!g_map.PeerStateApplies(now)) {
        QDLOG(LOG_WARN, "damage dropped: worlds differ (local ", g_map.Local(),
              ", partner ", (g_map.Peer()[0] ? g_map.Peer() : "?"), ")");
        return;
    }

    // The attacker must still be an enemy this machine can place.  It does NOT need a
    // local actor: G4 is exactly the case where the host runs an enemy the peer's
    // spawner never spawned.  A GUID the host has published a row for is enough to
    // attribute the hit; anything else is a packet about a spawn this machine has no
    // record of, and applying it would be damage from nothing.
    EnemyRec* er = EnemyByGuid(d.guid);
    bool haveActor = (er && ObjectIsLive(er->actor));
    if (!haveActor && !g_hostView.Get(d.guid)) {
        static int dropped = 0;
        if (dropped++ < 5)
            QDLOG(LOG_WARN, "damage dropped: attacker guid=", hex32(d.guid[0]),
                  " does not resolve on this machine");
        return;
    }

    if (!IsGameplayPawn(g_localPawn)) {
        QDLOG(LOG_INFO, "damage dropped: the local pawn is not in the game world");
        return;
    }
    uint8_t dead = 0;
    bool isDead = (BytePropC(g_localPawn, "IsDead", &dead) && dead != 0) ||
                  (BytePropC(g_localPawn, "bDead", &dead) && dead != 0);
    if (isDead) {
        QDLOG(LOG_INFO, "damage dropped: the local player is dead");
        return;
    }

    // The causer is the enemy actor when this machine has it; a null causer is what the
    // game itself passes for an environmental hit, and the payload simply carries none.
    void* causer = haveActor ? er->actor : nullptr;
    const char* kind = (d.flags & wire::kDamageMelee) ? "melee"
                     : (d.flags & wire::kDamageRanged) ? "ranged" : "untyped";

    float before = 0.0f, after = 0.0f;
    bool haveHp = false;
    bool applied = ApplyDamageToActor(g_localPawn, amount, causer, true,
                                      &before, &after, &haveHp);
    if (haveHp)
        QDLOG(applied ? LOG_INFO : LOG_WARN, "damage from enemy ", hex32(d.guid[0]), " ",
              kind, (d.flags & wire::kDamageGuessed) ? " (ESTIMATED amount)" : "",
              ": ", flt(amount, 1), (applied ? " applied" : " NO effect"),
              " hp ", flt(before, 1), " -> ", flt(after, 1),
              (causer ? "" : " (no local actor for the attacker)"));
    else
        QDLOG(applied ? LOG_INFO : LOG_WARN, "damage from enemy ", hex32(d.guid[0]), " ",
              kind, (d.flags & wire::kDamageGuessed) ? " (ESTIMATED amount)" : "",
              ": ", flt(amount, 1), (applied ? " applied" : " NO effect"),
              " (the pawn's Health property was unreadable)",
              (causer ? "" : " (no local actor for the attacker)"));
}

// ---------------------------------------------------------------------------
// The hit the game itself resolved on the proxy body (G4, the exact half)
// ---------------------------------------------------------------------------
// Once the partner's body is a real player class, an enemy that targets it hits it with
// the GAME's own code: `ApplyDamageToHitboxTarget` casts, `TickIndividualHitbox` runs the
// hitbox test, and `TryApplyDamage` arrives here with a fully built
// `FB_DamageAttributeSet`.  That is a better number than the montage-and-range estimate
// TrySendEnemyDamage has to use -- it is what the game decided -- so the amount is read
// out of the payload and sent to the machine that owns the player, and the local
// application is suppressed.  Nothing changes about the player's HP here: each machine
// owns exactly one player's HP, which is the rule the K_DAMAGE receiver already obeys.
//
// Returns false when the call must NOT reach the original.
bool WriteFalseReturn(void* parms, const LayoutResult& sig)
{
    if (sig.ret_offset == 0xFFFF) return false;
    __try {
        for (int i = 0; i < sig.props; ++i) {
            const PropDesc& d = sig.p[i];
            if (d.offset != sig.ret_offset) continue;
            int n = (d.size > 0 && d.size <= 8) ? d.size : 1;
            memset((uint8_t*)parms + d.offset, 0, (size_t)n);
            return true;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return false;
}

// A projectile is its own actor; the enemy that fired it is its owner or instigator.
EnemyRec* EnemyForCauser(void* causer)
{
    if (!causer) return nullptr;
    if (EnemyRec* r = EnemyByActor(causer)) return r;
    if (void* o = ObjPropC(causer, "Owner")) { if (EnemyRec* r = EnemyByActor(o)) return r; }
    if (void* o = ObjPropC(causer, "Instigator")) { if (EnemyRec* r = EnemyByActor(o)) return r; }
    return nullptr;
}

bool PlayerBodyObserve(void* self, void* func, void* parms)
{
    if (!Cfg().player_body || !Cfg().enemy_sync) return true;
    // The one compare that runs on every Blueprint call in the process.
    if (!g_puppetActor || self != g_puppetActor || !func || !parms) return true;
    char fn[64];
    if (FNameToString(ObjNameRaw(func), fn, sizeof(fn)) <= 0) return true;
    if (strcmp(fn, "TryApplyDamage") != 0) return true;

    struct HitApi {
        bool         tried;
        bool         ok;
        void*        fn;          // the UFunction this signature belongs to
        LayoutResult sig;
        const PropDesc* dmg;      // DamageToApply: the incoming payload
        const PropDesc* applier;  // who the game says hit us
    };
    static HitApi h = {};
    // A UFunction is class-owned, and the class reloads with the world: a body respawned
    // in a new map arrives with a DIFFERENT function object, and reusing the old
    // signature would read the caller's frame at stale offsets.
    if (!h.tried || h.fn != func) {
        h = HitApi();
        h.tried = true;
        h.fn = func;
        h.sig = Signature(func);
        if (!h.sig.valid) {
            QDLOG(LOG_ERROR, "player body: TryApplyDamage signature unavailable (",
                  h.sig.note, "); hits on the proxy will be applied locally");
            return true;
        }
        for (int i = 0; i < h.sig.props; ++i) {
            const PropDesc& d = h.sig.p[i];
            if (d.flags & CPF_OutParm) continue;
            if (NameEquals(d.type, "StructProperty") && enemyown::HasCI(d.name, "DamageToApply"))
                h.dmg = &d;
            else if (NameEquals(d.type, "ObjectProperty") && d.size >= 8 &&
                     (NameEquals(d.name, "Applier") || NameEquals(d.name, "DamageCauser") ||
                      NameEquals(d.name, "Instigator")))
                h.applier = h.applier ? h.applier : &d;
        }
        bool structOk = DamageStructInit();
        bool fits = structOk && h.dmg &&
                    g_dmgStruct.amountOff + g_dmgStruct.amountSize <= h.dmg->size;
        if (!h.dmg || !fits) {
            QDLOG(LOG_ERROR, "player body: the hit payload is not buildable (",
                  (h.dmg ? "the damage struct is unreadable" : "no DamageToApply parameter"),
                  "); hits on the proxy will be applied locally");
            return true;
        }
        h.ok = true;
        QDLOG(LOG_INFO, "player body: damage hook armed on the proxy (", h.dmg->name,
              " size=", dec(h.dmg->size), ", amount -> ", g_dmgStruct.amountName,
              " @", dec(g_dmgStruct.amountOff), ", applier ",
              (h.applier ? h.applier->name : "absent"), ")");
    }
    if (!h.ok) return true;

    float amount = 0.0f;
    const uint8_t* base = (const uint8_t*)parms + h.dmg->offset;
    if (g_dmgStruct.amountSize == 8) amount = (float)*(const double*)(base + g_dmgStruct.amountOff);
    else memcpy(&amount, base + g_dmgStruct.amountOff, 4);

    void* applier = nullptr;
    if (h.applier) memcpy(&applier, (const uint8_t*)parms + h.applier->offset, 8);

    EnemyRec* er = EnemyForCauser(applier);
    if (!g_net.IsHost() || !g_net.Connected() || !er || !er->haveGuid || !(amount > 0.0f)) {
        // Not routable.  Let the game apply it to the proxy: the proxy is not the player,
        // so nothing of value is lost, and the reason is logged rather than hidden.
        static uint32_t lastLog = 0;
        uint32_t nowMs = Net::Now();
        if (nowMs - lastLog > 5000) {
            lastLog = nowMs;
            const char* why = !g_net.IsHost() ? "this machine does not own a player's HP"
                            : !g_net.Connected() ? "no session"
                            : (!er || !er->haveGuid) ? "no enemy to attribute the hit to"
                            : "the amount is not positive";
            QDLOG(LOG_DEBUG, "player body: hit not forwarded (", flt(amount, 1), " damage, ",
                  why, ", applier ", (applier ? "present" : "absent"),
                  "); the game applies it to the proxy");
        }
        return true;
    }

    int32_t centi = enemydmg::Centi(amount);
    if (centi <= 0) return true;

    wire::DamageBody d;
    memset(&d, 0, sizeof(d));
    memcpy(d.guid, er->guid, sizeof(d.guid));
    d.amount_centi = centi;
    // The applier being the enemy itself is a melee hit; anything else (its projectile,
    // its spawned effect) is the ranged one.  The receiver only reports the kind.
    d.flags = (er->actor == applier) ? wire::kDamageMelee : wire::kDamageRanged;

    uint8_t body[wire::kMaxBody];
    wire::Writer w(body, sizeof(body));
    wire::WriteDamage(w, d);
    bool sent = w.ok && g_net.SendReliable(wire::K_DAMAGE, body, w.len);
    if (!sent) {
        QDLOG(LOG_WARN, "player body: K_DAMAGE send failed guid=", hex32(er->guid[0]),
              "; the hit is applied locally on the proxy instead");
        return true;
    }
    // Answer the Blueprint's own question honestly -- the damage was NOT applied here.
    // A frame whose return slot cannot be written is still swallowed: the hit has already
    // been described to the player's own machine, and running it here as well would apply
    // it twice.
    bool answered = WriteFalseReturn(parms, h.sig);
    QDLOG(LOG_INFO, "player body hit guid=", hex32(er->guid[0]), " amount=", flt(amount, 1),
          " centi=", dec(centi), " (", ((d.flags & wire::kDamageMelee) ? "melee" : "ranged"),
          ") -> sent to the machine that owns the player; local damage suppressed",
          (answered ? "" : " (the return value could not be written)"));
    return false;
}

void EnemyPump(uint32_t now)
{
    if (!Cfg().enemy_sync) return;
    if (!g_substrateReady || !EnsureActorApi()) return;
    static void* lastWorld = nullptr;
    void* world = GWorldPtr();
    if (world != lastWorld) {
        lastWorld = world;
        g_enN = 0; g_enCursor = 0;
        g_propCacheN = 0;           // classes reload with the world
        g_enemyBaseCls = nullptr;
        g_clsYesN = g_clsNoN = 0;
        // Ownership, remote rows and the kill log are all per-world.
        g_own.Reset();
        g_hostView.Reset();
        g_killLog.Clear();
        g_deadCursor = 0;
        // A swing sequence is per-world too; carrying it across a map change would
        // treat the first attack in the new world as already described.
        g_dmgGate.Reset();
        // Classes unload with the world, so every per-class offset and every UFunction
        // pointer found in the old one is a stale number.
        g_tgtApiN = 0;
        g_proxySeen = nullptr;
    }
    EnemyScanStep();
    if (!g_net.Connected()) {
        // Never leave a copy frozen after the session ends, and forget who owned what:
        // a stale claim from the last session must not freeze a copy on reconnect.
        g_own.Reset();
        g_hostView.Reset();
        g_dmgGate.Reset();
        for (int i = 0; i < g_enN; ++i) if (g_en[i].driven && ObjectIsLive(g_en[i].actor)) EnemyDrive(g_en[i], false);
        return;
    }
    g_own.SetHost(g_net.IsHost());
    EnemyOwnPump(now);
    // After ownership, so only the machine that actually simulates an enemy steers it.
    EnemyTargetPump(now);
    EnemySendPump(now);
}

// ===========================================================================
// 6c. Dialogue
// ===========================================================================
// Read from the game's own state, never inferred from input: the player's
// DialogueManager component carries RunningDialogue, CurrentDialogueNPC and
// CurrentDialogueSequenceIndex (cooked names, DialogueManager.uasset).  The partner's
// NPC is made non-interactable here with NPC_Base's own BlockInteract /
// ReenableInteract.  See qddialogue.h TalkBoard for why it is per slot and not a lock.
dialogue::TalkBoard g_talk;
struct DlgLocal {
    bool     talking;
    uint8_t  npc[16];
    uint16_t line;
    uint32_t seq;
    uint32_t lastSend;
} g_dlg = {};
bool    g_dlgConflictSaid = false;
void*   g_blockedNpc = nullptr;
uint8_t g_blockedId[16];

void NpcIdOf(void* npc, uint8_t out[16])
{
    char path[256];
    int k = 0;
    // object / level / world, innermost first: placed actors are unique per level.
    void* o = npc;
    for (int depth = 0; o && depth < 3; ++depth, o = ObjOuter(o)) {
        char nm[80];
        if (FNameToString(ObjNameRaw(o), nm, sizeof(nm)) <= 0) break;
        for (int i = 0; nm[i] && k < (int)sizeof(path) - 2; ++i) path[k++] = nm[i];
        path[k++] = '/';
    }
    path[k] = 0;
    dialogue::StableId(path, out);
}

void SendDialogue()
{
    wire::DialogueBody d;
    memset(&d, 0, sizeof(d));
    d.seq = g_dlg.seq;
    memcpy(d.npc, g_dlg.npc, 16);
    d.slot = g_dlg.talking ? (uint8_t)LocalSlot() : dialogue::kSlotFree;
    d.line = g_dlg.line;
    uint8_t body[64];
    wire::Writer w(body, sizeof(body));
    wire::WriteDialogue(w, d);
    if (w.ok) g_net.SendUnreliable(wire::K_DIALOGUE, body, w.len);
}

void* FindNpcById(const uint8_t id[16])
{
    static void* npcBase = nullptr;
    if (!npcBase || !ObjectIsLive(npcBase)) npcBase = FindClassByName("NPC_Base_C");
    if (!npcBase) return nullptr;
    int total = ObjectCount();
    for (int i = 0; i < total; ++i) {
        void* o = ObjectAt(i);
        if (!o || !LooksLikeUObject(o) || !DerivesFrom(ObjClass(o), npcBase)) continue;
        char nm[12];
        if (FNameToString(ObjNameRaw(o), nm, sizeof(nm)) > 0 && strncmp(nm, "Default__", 9) == 0) continue;
        uint8_t got[16];
        NpcIdOf(o, got);
        if (dialogue::TalkBoard::SameId(got, id)) return o;
    }
    return nullptr;
}

void NpcSetBlocked(void* npc, bool block)
{
    // `BlockInteract` is a BOOLEAN PROPERTY on NPC_Base_C (+0x0613), not a function.  The
    // cooked packages export no UFunction of that name, none of them imports one, and the
    // string is absent from the exe's name pool -- so the previous version, which called
    // it through ProcessEvent, resolved nothing and left the partner's NPC interactable.
    // Setting the bool is the same lever the Blueprint's own gate reads.
    static Fn fReenable = {};
    bool wrote = WriteBytePropC(npc, "BlockInteract", block ? 1 : 0);
    // Reenabling also runs the game's own 0-argument ReenableInteract (a real UFunction,
    // 58-byte payload, no parameters): it is what restores the interaction state the
    // Blueprint may have set beyond the flag.  Blocking has no such counterpart.
    bool re = block ? false : CallNoArgs(npc, fReenable, "ReenableInteract");
    char d[96];
    ObjDescribe(npc, d, sizeof(d));
    QDLOG(LOG_INFO, "dialogue: ", (block ? "BLOCK " : "unblock "), d,
          (wrote ? " BlockInteract=" : " BlockInteract NOT FOUND (property absent)"),
          (block ? "" : (re ? " reenabled" : "")));
}

void DialoguePump(uint32_t now)
{
    if (!Cfg().dialogue_sync) return;
    static uint32_t last = 0;
    if (now - last < 100) return;
    last = now;
    const uint8_t slot = (uint8_t)LocalSlot();

    // ---- local state ---------------------------------------------------------
    bool talking = false;
    uint8_t npc[16] = {0};
    uint16_t line = 0;
    void* npcActor = nullptr;
    if (IsGameplayPawn(g_localPawn)) {
        void* dm = ObjPropC(g_localPawn, "DialogueManager");
        uint8_t running = 0;
        if (dm && BytePropC(dm, "RunningDialogue", &running) && running) {
            npcActor = ObjPropC(dm, "CurrentDialogueNPC");
            int32_t idx = 0;
            IntPropC(dm, "CurrentDialogueSequenceIndex", &idx);
            line = (uint16_t)idx;
            if (npcActor) { talking = true; NpcIdOf(npcActor, npc); }
        }
    }

    // ---- a death ends the conversation -------------------------------------
    // The measured failure: a dialogue left open at death leaves an invisible
    // unanswered choice, and the partner's NPC stays blocked.  Release the lock and
    // end the local talk explicitly rather than waiting for the 2-minute expiry.
    {
        uint8_t dead = 0;
        bool localDead = IsGameplayPawn(g_localPawn) &&
                         ((BytePropC(g_localPawn, "IsDead", &dead) && dead) ||
                          (BytePropC(g_localPawn, "bDead", &dead) && dead));
        if (localDead && g_dlg.talking) {
            g_dialogue.OnPlayerDeath(slot);
            static Fn fEnd = {}, fEnded = {};
            void* dm = ObjPropC(g_localPawn, "DialogueManager");
            bool a = dm && CallNoArgs(dm, fEnd, "End");
            bool b = CallNoArgs(g_localPawn, fEnded, "DialogueEnded");
            QDLOG(LOG_INFO, "dialogue: local death ended the conversation "
                  "(end=", dec(a ? 1 : 0), " ended=", dec(b ? 1 : 0), "); lock released");
            talking = false;
            npcActor = nullptr;
        }
    }

    // ---- concurrent start: prevent it, do not merely log it ------------------
    // The reservation is taken before the conversation is published.  If the partner
    // already reserved or is in this NPC, the local start is ended with the game's own
    // `DialogueManager.End`.  If the reservations crossed, the lower slot (the host) is
    // the winner and the higher slot yields -- logged as PREVENTED, not as a warning.
    if (talking) {
        bool ok = g_talk.Reserve(slot, npc, now);
        if (!ok) {
            // The crossed case.  The host (slot 0) keeps the conversation; the peer
            // yields by ending its own.  `LosesConflict` cannot decide this on the
            // local machine because the local slot's state is not published to itself,
            // so the rule is the slot number -- which is also the documented rule.
            if (slot != 0) {
                static uint32_t lastPrevent = 0;
                if (now - lastPrevent > 2000) {
                    lastPrevent = now;
                    static Fn fEnd = {}, fEnded = {};
                    void* dm = ObjPropC(g_localPawn, "DialogueManager");
                    bool a = dm && CallNoArgs(dm, fEnd, "End");
                    bool b = CallNoArgs(g_localPawn, fEnded, "DialogueEnded");
                    QDLOG(LOG_WARN, "dialogue: PREVENTED a concurrent start on the same NPC "
                          "(the host holds it); ending the local conversation "
                          "(end=", dec(a ? 1 : 0), " ended=", dec(b ? 1 : 0), ")");
                }
                talking = false;
                npcActor = nullptr;
            } else {
                if (!g_dlgConflictSaid) {
                    g_dlgConflictSaid = true;
                    QDLOG(LOG_INFO, "dialogue: the partner also started this NPC within one "
                          "round trip; the host keeps it and the peer yields");
                }
            }
        }
    }
    if (!talking) {
        g_talk.ReleaseReservation(slot);
        g_dlgConflictSaid = false;
    }

    bool changed = talking != g_dlg.talking || (talking && !dialogue::TalkBoard::SameId(npc, g_dlg.npc)) ||
                   line != g_dlg.line;
    if (changed) {
        if (talking != g_dlg.talking || (talking && !dialogue::TalkBoard::SameId(npc, g_dlg.npc))) {
            char d[96] = "-";
            if (npcActor) ObjDescribe(npcActor, d, sizeof(d));
            QDLOG(LOG_INFO, "dialogue: local ", (talking ? "started with " : "ended"), (talking ? d : ""));
        }
        g_dlg.talking = talking;
        memcpy(g_dlg.npc, npc, 16);
        g_dlg.line = line;
        ++g_dlg.seq;
    }
    if (g_net.Connected() && (changed || now - g_dlg.lastSend >= 1000)) {
        g_dlg.lastSend = now;
        SendDialogue();
    }

    // ---- the partner's NPC is not interactable here --------------------------
    uint8_t want[16];
    bool have = g_net.Connected() && g_talk.RemoteNpc((uint8_t)RemoteSlot(), want, now);
    if (have && g_blockedNpc && dialogue::TalkBoard::SameId(want, g_blockedId) && ObjectIsLive(g_blockedNpc)) return;
    if (g_blockedNpc) {
        if (ObjectIsLive(g_blockedNpc)) NpcSetBlocked(g_blockedNpc, false);
        g_blockedNpc = nullptr;
    }
    if (!have) return;
    // Never block the NPC the local player is talking to: that would strand the local
    // conversation.  The overlap is logged above and host progression settles it.
    if (g_dlg.talking && dialogue::TalkBoard::SameId(want, g_dlg.npc)) return;
    static uint32_t lastSearch = 0;
    if (now - lastSearch < 1000) return;       // the NPC may be in a level not shown here
    lastSearch = now;
    void* n = FindNpcById(want);
    if (!n) return;
    NpcSetBlocked(n, true);
    g_blockedNpc = n;
    memcpy(g_blockedId, want, 16);
}

void OnDialogue(wire::Reader& r, uint32_t now)
{
    wire::DialogueBody d;
    if (!wire::ReadDialogue(r, &d)) return;
    uint8_t npc[16];
    memcpy(npc, d.npc, 16);
    bool talking = d.slot != dialogue::kSlotFree;
    if (g_talk.Accept((uint8_t)RemoteSlot(), d.seq, talking, npc, d.line, now))
        QDLOG(LOG_DEBUG, "dialogue: partner ", (talking ? "talking" : "free"), " seq=", dec((int)d.seq));
}

// ===========================================================================
// 6c-bis. Cutscenes, moving world objects, loot, suspend saves
// ===========================================================================
// Four independent lanes, each behind its own switch.  They share the rules the rest
// of the file obeys: validate every handle, write only your own machine's objects, and
// make every refusal a named log line.

void CopyText2(char* dst, int cap, const char* src)
{
    if (!dst || cap <= 0) return;
    int i = 0;
    if (src) for (; src[i] && i < cap - 1; ++i) dst[i] = src[i];
    dst[i] = 0;
}

void NumText(int v, char* out, int cap)
{
    if (!out || cap <= 0) return;
    char tmp[16];
    int n = 0;
    bool neg = v < 0;
    unsigned u = neg ? (unsigned)(-(int64_t)v) : (unsigned)v;
    do { tmp[n++] = (char)('0' + (u % 10)); u /= 10; } while (u && n < (int)sizeof(tmp));
    int k = 0;
    if (neg && k < cap - 1) out[k++] = '-';
    while (n > 0 && k < cap - 1) out[k++] = tmp[--n];
    out[k] = 0;
}

void StablePathOf(void* obj, char* out, int cap)
{
    if (!out || cap <= 0) return;
    int k = 0;
    void* o = obj;
    for (int depth = 0; o && depth < 4; ++depth, o = ObjOuter(o)) {
        char nm[80];
        if (FNameToString(ObjNameRaw(o), nm, sizeof(nm)) <= 0) break;
        for (int i = 0; nm[i] && k < cap - 2; ++i) out[k++] = nm[i];
        out[k++] = '/';
    }
    out[k] = 0;
}

bool ContainsCI(const char* hay, const char* needle)
{
    if (!hay || !needle || !needle[0]) return false;
    for (int i = 0; hay[i]; ++i) {
        int k = 0;
        for (; needle[k]; ++k) {
            char a = hay[i + k];
            char b = needle[k];
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b) break;
        }
        if (!needle[k]) return true;
    }
    return false;
}

void* GameInstanceObj()
{
    void* w = GWorldPtr();
    return w ? GameInstanceOf(w) : nullptr;
}

// A one-object UFunction call.  The parameter is located by type, never by a guessed
// pin name: a Blueprint pin may be spelled with a space or a suffix.
bool CallObject1(void* obj, Fn& f, const char* name, void* arg)
{
    if (!obj || !f.Resolve(obj, name)) return false;
    const PropDesc* p = FindPropOfType(f.sig, "ObjectProperty", CPF_OutParm | CPF_ReturnParm);
    if (!p && f.sig.props > 0) p = &f.sig.p[0];
    Call c(f);
    if (p) c.Obj(p->name, arg);
    return c.Run(obj);
}

// A one-bool UFunction call, but only when the first parameter really is a bool: a
// one-byte write into an object/struct parameter would hand the engine a pointer-shaped
// value (the guarded ProcessEvent would catch a fault, but it would still be the wrong
// call).  Functions with no parameters run unchanged.
bool CallFlag1(void* obj, Fn& f, const char* name, bool v)
{
    if (!obj || !f.Resolve(obj, name)) return false;
    Call c(f);
    if (f.sig.props > 0 && NameEquals(f.sig.p[0].type, "BoolProperty"))
        c.U8(f.sig.p[0].name, v ? 1 : 0);
    return c.Run(obj);
}

// ---------------------------------------------------------------------------
// Cutscenes
// ---------------------------------------------------------------------------
bool LocalCutscene(char* name, int cap)
{
    if (name && cap > 0) name[0] = 0;
    if (!IsGameplayPawn(g_localPawn)) return false;
    void* mgr = ObjPropC(g_localPawn, "CurrentCutsceneManager");
    uint8_t b = 0;
    if (mgr && ObjectIsLive(mgr)) {
        if (FNameToString(ObjNameRaw(mgr), name, cap) <= 0) name[0] = 0;
        return true;
    }
    if (BytePropC(g_localPawn, "InIntroCutscene", &b) && b) {
        sequence::CutsceneBoard::Copy(name, cap, "IntroCutscene");
        return true;
    }
    return false;
}

void CutsceneAct(sequence::Verdict v)
{
    void* gi = GameInstanceObj();
    static Fn fEnter = {}, fExit = {}, fBegin = {}, fLock = {}, fUnlock = {};
    if (v == sequence::CV_PLAY) {
        void* mgr = FindTravelActor(g_cutscene.HeldName(), GWorldPtr());
        bool played = mgr && IsGameplayPawn(g_localPawn) &&
                      CallObject1(g_localPawn, fBegin, "BeginCutsceneWithManager", mgr);
        if (played) {
            QDLOG(LOG_INFO, "cutscene: playing the partner's sequence ",
                  g_cutscene.HeldName(), " (resolved its manager locally)");
            return;
        }
        v = sequence::CV_HOLD;      // no manager here: hold consistently instead
    }
    if (v == sequence::CV_HOLD) {
        bool e = gi && CallFlag1(gi, fEnter, "EnterCutsceneMode", true);
        bool l = IsGameplayPawn(g_localPawn) &&
                 CallFlag1(g_localPawn, fLock, "LockInputForLoadingOrCutscene", true);
        QDLOG(LOG_INFO, "cutscene: holding the local player for the partner's cutscene "
              "(enter=", dec(e ? 1 : 0), " lock=", dec(l ? 1 : 0), ")");
        if (!e && !l) g_cutscene.OnPlayFailed();
    } else if (v == sequence::CV_RELEASE) {
        bool e = gi && CallFlag1(gi, fExit, "ExitCutsceneMode", false);
        bool u = IsGameplayPawn(g_localPawn) &&
                 CallFlag1(g_localPawn, fUnlock, "UnlockInputForLoadingOrCutscene", false);
        QDLOG(LOG_INFO, "cutscene: partner's cutscene ended; resuming together "
              "(exit=", dec(e ? 1 : 0), " unlock=", dec(u ? 1 : 0), ")");
    }
}

void CutscenePump(uint32_t now)
{
    if (!Cfg().cutscene_sync) return;
    static uint32_t lastSend = 0;
    static bool wasPlaying = false;
    static char lastName[sequence::kMaxNameLen] = {0};

    char name[sequence::kMaxNameLen];
    bool playing = LocalCutscene(name, sizeof(name));
    bool changed = playing != wasPlaying ||
                   (playing && !sequence::CutsceneBoard::Same(name, lastName));
    bool ended = wasPlaying && !playing;
    if (playing) sequence::CutsceneBoard::Copy(lastName, sizeof(lastName), name);
    if (changed || ended) {
        ++g_cutsceneSeq;
        QDLOG(LOG_INFO, "cutscene: local ", (playing ? "started " : "ended "),
              ((playing ? name : lastName)[0] ? (playing ? name : lastName) : "(unnamed)"));
        wasPlaying = playing;
    }

    if (g_net.Connected() && (changed || ended || now - lastSend >= 1000)) {
        lastSend = now;
        wire::CutsceneBody c;
        memset(&c, 0, sizeof(c));
        c.seq = g_cutsceneSeq;
        c.slot = (uint8_t)LocalSlot();
        c.state = playing ? wire::CS_PLAYING : (ended ? wire::CS_ENDED : wire::CS_NONE);
        sequence::CutsceneBoard::Copy(c.name, sizeof(c.name), playing ? name : lastName);
        uint8_t body[160];
        wire::Writer w(body, sizeof(body));
        wire::WriteCutscene(w, c);
        if (w.ok) g_net.SendUnreliable(wire::K_CUTSCENE, body, w.len);
    }

    sequence::Verdict v = g_cutscene.Decide((uint8_t)LocalSlot(), playing, name, now);
    if (v == sequence::CV_PLAY || v == sequence::CV_HOLD || v == sequence::CV_RELEASE)
        CutsceneAct(v);
}

void OnCutscene(wire::Reader& r, uint32_t now)
{
    wire::CutsceneBody c;
    if (!wire::ReadCutscene(r, &c)) return;
    bool playing = c.state == wire::CS_PLAYING;
    bool ended = c.state == wire::CS_ENDED;
    if (g_cutscene.Accept((uint8_t)RemoteSlot(), c.seq, playing, ended, c.name, now))
        QDLOG(LOG_INFO, "cutscene: partner ", (playing ? "started " : (ended ? "ended " : "idle ")),
              (c.name[0] ? c.name : "(unnamed)"));
}

// ---------------------------------------------------------------------------
// Moving world objects (owner-authoritative)
// ---------------------------------------------------------------------------
struct MoverActorRec {
    void*    actor;
    uint8_t  id[mover::kIdLen];
};
MoverActorRec g_moverActors[64];
int           g_moverActorN = 0;
int           g_moverScanCursor = 0;
uint32_t      g_lastMoverSend = 0;
int           g_moverDropped = 0;      // seen but not tracked (capacity), reported once

bool IsMoverClass(void* cls)
{
    char cn[96];
    if (FNameToString(ObjNameRaw(cls), cn, sizeof(cn)) <= 0) return false;
    return ContainsCI(cn, "Elevator") || ContainsCI(cn, "Mover") ||
           ContainsCI(cn, "Platform") || ContainsCI(cn, "Activatable");
}

void MoverScanStep()
{
    static void* actorBase = nullptr;
    if (!actorBase || !ObjectIsLive(actorBase)) actorBase = FindClassByName("Actor");
    if (!actorBase) return;
    void* world = GWorldPtr();
    if (!world) return;
    int total = ObjectCount();
    int budget = 4096;
    while (budget-- > 0 && g_moverScanCursor < total) {
        void* o = ObjectAt(g_moverScanCursor++);
        if (!o || !LooksLikeUObject(o)) continue;
        void* c = ObjClass(o);
        if (!c || !IsMoverClass(c)) continue;
        __try {
            if (!IsSubclassOf(c, actorBase) || !ActorOuterReachesWorld(o, world)) continue;
        } __except (EXCEPTION_EXECUTE_HANDLER) { continue; }
        bool have = false;
        for (int i = 0; i < g_moverActorN; ++i) if (g_moverActors[i].actor == o) { have = true; break; }
        if (have) continue;
        if (g_moverActorN >= (int)(sizeof(g_moverActors) / sizeof(g_moverActors[0]))) {
            ++g_moverDropped;
            continue;
        }
        char path[256];
        StablePathOf(o, path, sizeof(path));
        MoverActorRec& r = g_moverActors[g_moverActorN++];
        r.actor = o;
        mover::Board::IdFromPath(path, r.id);
        uint32_t id0 = 0;
        memcpy(&id0, r.id, 4);
        QDLOG(LOG_INFO, "movers: tracking ", path, " id=", hex32(id0));
    }
    if (g_moverScanCursor >= total) g_moverScanCursor = 0;
}

void ToWireMover(const mover::Sample& s, wire::MoverRow* o)
{
    memcpy(o->id, s.id, mover::kIdLen);
    o->px = s.x; o->py = s.y; o->pz = s.z;
    o->yaw = s.yaw;
    o->flags = s.flags;
}

void FromWireMover(const wire::MoverRow& r, mover::Sample* s)
{
    memset(s, 0, sizeof(*s));
    memcpy(s->id, r.id, mover::kIdLen);
    s->x = r.px; s->y = r.py; s->z = r.pz;
    s->yaw = r.yaw;
    s->flags = r.flags;
}

void MoverApply(const mover::Sample& s)
{
    for (int i = 0; i < g_moverActorN; ++i) {
        if (!mover::Board::SameId(g_moverActors[i].id, s.id)) continue;
        if (!ObjectIsLive(g_moverActors[i].actor)) return;
        SetActorXform(g_moverActors[i].actor, wire::DequantizePos(s.x), wire::DequantizePos(s.y),
                      wire::DequantizePos(s.z), wire::DequantizeAngle(s.yaw));
        return;
    }
}

void MoverPump(uint32_t now)
{
    if (!Cfg().mover_sync) return;
    static void* lastWorld = nullptr;
    void* world = GWorldPtr();
    if (world != lastWorld) {
        lastWorld = world;
        g_movers.Reset();
        g_moverActorN = 0;
        g_moverScanCursor = 0;
    }
    // Incremental discovery, like the enemy scan: a full GObjects pass is ~500k slots,
    // so a budget per frame keeps the cost invisible and still completes a pass in a
    // few seconds.
    MoverScanStep();
    if (!EnsureActorApi()) return;

    ActorXform me;
    bool haveMe = IsGameplayPawn(g_localPawn) && ReadActorXform(g_localPawn, &me);
    for (int i = 0; i < g_moverActorN;) {
        void* a = g_moverActors[i].actor;
        if (!a || !ObjectIsLive(a)) {
            g_moverActors[i] = g_moverActors[--g_moverActorN];
            continue;
        }
        ActorXform x;
        if (ReadActorXform(a, &x)) {
            mover::Sample s;
            memset(&s, 0, sizeof(s));
            memcpy(s.id, g_moverActors[i].id, mover::kIdLen);
            s.x = wire::QuantizePos(x.px);
            s.y = wire::QuantizePos(x.py);
            s.z = wire::QuantizePos(x.pz);
            s.yaw = wire::QuantizeAngle(x.yaw);
            s.pitch = wire::QuantizeAngle(x.pitch);
            float localD = 1.0e17f, peerD = 1.0e17f;   // < INT64_MAX, so the cast is defined
            if (haveMe) {
                float dx = me.px - x.px, dy = me.py - x.py, dz = me.pz - x.pz;
                localD = dx * dx + dy * dy + dz * dz;
            }
            if (g_net.Connected()) {
                float dx = g_peerPos[0] - x.px, dy = g_peerPos[1] - x.py, dz = g_peerPos[2] - x.pz;
                peerD = dx * dx + dy * dy + dz * dz;
                g_movers.ResolveOwner(s.id, (int64_t)localD, (int64_t)peerD, g_net.IsHost());
            }
            g_movers.ObserveLocal(s.id, s, false, 8);
        }
        ++i;
    }

    if (g_net.Connected() && now - g_lastMoverSend >= 100) {
        g_lastMoverSend = now;
        mover::Sample rows[20];
        int n = g_movers.SnapshotDirty(rows, 20);
        static uint32_t lastFull = 0;
        if (n == 0 && now - lastFull >= 2000) { lastFull = now; n = g_movers.Snapshot(rows, 20, true); }
        if (n > 0) {
            wire::MoverRow out[20];
            for (int k = 0; k < n; ++k) ToWireMover(rows[k], &out[k]);
            uint8_t body[wire::kMaxBody];
            wire::Writer w(body, sizeof(body));
            wire::WriteMoversHeader(w, ++g_moverRev, (uint16_t)n);
            wire::WriteMoversChunk(w, out, n, 0);
            if (w.ok) g_net.SendUnreliable(wire::K_MOVERS, body, w.len);
        }
    }
    static int lastDropped = -1;
    if (g_moverDropped != lastDropped) {
        lastDropped = g_moverDropped;
        QDLOG(LOG_WARN, "movers: ", dec(g_moverDropped),
              " candidate(s) not tracked (bounded table); they are not on the wire");
    }
}

void OnMovers(wire::Reader& r, uint32_t now)
{
    static wire::MoversBody b;
    if (!wire::ReadMovers(r, &b)) return;
    int applied = 0;
    for (int i = 0; i < b.n; ++i) {
        mover::Sample s;
        FromWireMover(b.row[i], &s);
        if (g_movers.Accept(s, b.rev, now) && !g_movers.IsOwner(s.id)) {
            MoverApply(s);
            ++applied;
        }
    }
    if (applied) QDLOG(LOG_DEBUG, "movers: applied ", dec(applied), " owner row(s) rev=", dec((int)b.rev));
}

// ---------------------------------------------------------------------------
// Loot drops (one shared set)
// ---------------------------------------------------------------------------
struct LootActorRec {
    void*    actor;
    uint32_t source_hash;
    int32_t  drop_index;
    char     path[loot::kMaxPathLen];
    bool     gone;
};
LootActorRec g_lootActors[64];
int          g_lootActorN = 0;
int          g_lootScanCursor = 0;
uint32_t     g_lastLootSend = 0;
bool         g_lootSaidUnrepresented = false;

bool IsDropClass(void* cls)
{
    char cn[96];
    if (FNameToString(ObjNameRaw(cls), cn, sizeof(cn)) <= 0) return false;
    return ContainsCI(cn, "ItemDrop") || ContainsCI(cn, "SpawnItemDrop") ||
           ContainsCI(cn, "DroppedItem");
}

// The game's own drop key, recovered from the drop actor where the class exposes it.
// A class that names none of these is skipped and reported once: inventing an identity
// would make two different drops address one slot.
bool LootKeyOf(void* actor, uint32_t* hash, int32_t* index, char* path, int cap)
{
    static const char* kSources[] = {"DropSource", "SourceActor", "SourceObject", "Source",
                                     "Owner", "Instigator"};
    static const char* kItems[]   = {"ItemData", "DroppedItemData", "ItemObject", "Item",
                                     "DroppedItem"};
    static const char* kIndexes[] = {"DropIndex", "SourceDropIndex", "Index"};

    void* src = nullptr;
    for (unsigned i = 0; i < sizeof(kSources) / sizeof(kSources[0]) && !src; ++i)
        src = ObjPropC(actor, kSources[i]);
    if (!src) return false;
    char sn[80];
    if (FNameToString(ObjNameRaw(src), sn, sizeof(sn)) <= 0) return false;
    *hash = loot::HashSource(sn);

    *index = 0;
    for (unsigned i = 0; i < sizeof(kIndexes) / sizeof(kIndexes[0]); ++i)
        if (IntPropC(actor, kIndexes[i], index)) break;

    void* item = nullptr;
    for (unsigned i = 0; i < sizeof(kItems) / sizeof(kItems[0]) && !item; ++i)
        item = ObjPropC(actor, kItems[i]);
    if (!item) return false;
    if (avatar::AssetPath(item, path, cap) <= 0) return false;
    return true;
}

void LootScanStep()
{
    static void* actorBase = nullptr;
    if (!actorBase || !ObjectIsLive(actorBase)) actorBase = FindClassByName("Actor");
    if (!actorBase) return;
    void* world = GWorldPtr();
    if (!world) return;
    int total = ObjectCount();
    int budget = 4096;
    while (budget-- > 0 && g_lootScanCursor < total) {
        void* o = ObjectAt(g_lootScanCursor++);
        if (!o || !LooksLikeUObject(o)) continue;
        void* c = ObjClass(o);
        if (!c || !IsDropClass(c)) continue;
        __try {
            if (!IsSubclassOf(c, actorBase) || !ActorOuterReachesWorld(o, world)) continue;
        } __except (EXCEPTION_EXECUTE_HANDLER) { continue; }
        bool have = false;
        for (int i = 0; i < g_lootActorN; ++i) if (g_lootActors[i].actor == o) { have = true; break; }
        if (have) continue;
        if (g_lootActorN >= (int)(sizeof(g_lootActors) / sizeof(g_lootActors[0]))) continue;
        uint32_t hash = 0;
        int32_t index = 0;
        char path[loot::kMaxPathLen];
        if (!LootKeyOf(o, &hash, &index, path, sizeof(path))) {
            if (!g_lootSaidUnrepresented) {
                g_lootSaidUnrepresented = true;
                QDLOG(LOG_WARN, "loot: a drop actor exposes no source/item property; "
                                "it is not represented on the wire (first occurrence)");
            }
            continue;
        }
        LootActorRec& r = g_lootActors[g_lootActorN++];
        r.actor = o;
        r.source_hash = hash;
        r.drop_index = index;
        CopyText2(r.path, sizeof(r.path), path);
        r.gone = false;
        if (g_loot.Offer(hash, index, path)) {
            QDLOG(LOG_INFO, "loot: drop ", hex32(hash), "#", dec((int)index), " = ", path);
            g_lootRev = g_loot.Revision();
        }
    }
    if (g_lootScanCursor >= total) g_lootScanCursor = 0;
}

// Hide the local copy of a drop the other player took.  Hiding rather than destroying:
// destroying a Blueprint actor from native code is the AV the corpus warns about, and
// the drop must be reversible if a message crossed.
void LootHide(uint32_t hash, int32_t index)
{
    for (int i = 0; i < g_lootActorN; ++i) {
        if (g_lootActors[i].source_hash != hash || g_lootActors[i].drop_index != index) continue;
        if (g_lootActors[i].gone) continue;
        g_lootActors[i].gone = true;
        if (ObjectIsLive(g_lootActors[i].actor)) {
            SetActorHidden(g_lootActors[i].actor, true);
            QDLOG(LOG_INFO, "loot: hiding local drop ", hex32(hash), "#", dec((int)index),
                  " -- the partner took it");
        }
    }
}

void LootPump(uint32_t now)
{
    if (!Cfg().loot_sync) return;
    static void* lastWorld = nullptr;
    void* world = GWorldPtr();
    if (world != lastWorld) {
        lastWorld = world;
        g_loot.Reset();          // drops are per-world; a stale tombstone is not a fact
        g_lootActorN = 0;
        g_lootScanCursor = 0;
    }
    LootScanStep();

    // A recorded drop that has gone from the world was picked up here.
    for (int i = 0; i < g_lootActorN; ++i) {
        if (g_lootActors[i].gone) continue;
        if (ObjectIsLive(g_lootActors[i].actor)) continue;
        g_lootActors[i].gone = true;
        if (g_loot.Take(g_lootActors[i].source_hash, g_lootActors[i].drop_index)) {
            QDLOG(LOG_INFO, "loot: local pickup ", hex32(g_lootActors[i].source_hash), "#",
                  dec((int)g_lootActors[i].drop_index), " -- tombstoned for the partner");
            g_lootRev = g_loot.Revision();
        }
    }

    if (!g_net.Connected()) return;
    static uint32_t lastState = 0;
    bool due = g_loot.Revision() != lastState || now - g_lastLootSend >= 1000;
    if (!due) return;
    g_lastLootSend = now;
    lastState = g_loot.Revision();

    loot::Entry e[loot::kMaxDrops];
    int n = g_loot.Snapshot(e, loot::kMaxDrops);
    wire::LootEntry out[wire::kLootMax];
    int m = 0;
    for (int i = 0; i < n && m < wire::kLootMax; ++i) {
        out[m].source_hash = e[i].source_hash;
        out[m].drop_index = e[i].drop_index;
        out[m].taken = e[i].taken ? 1 : 0;
        CopyText2(out[m].item_path, sizeof(out[m].item_path), e[i].item_path);
        ++m;
    }
    uint8_t body[wire::kMaxBody];
    wire::Writer w(body, sizeof(body));
    wire::WriteLootHeader(w, g_loot.Revision(), (uint16_t)m);
    wire::WriteLootChunk(w, out, m, 0);
    if (w.ok) g_net.SendUnreliable(wire::K_LOOT, body, w.len);
}

void OnLoot(wire::Reader& r)
{
    static wire::LootBody b;
    if (!wire::ReadLoot(r, &b)) return;
    loot::Entry in[wire::kLootMax];
    int n = 0;
    for (int i = 0; i < b.n && n < loot::kMaxDrops; ++i) {
        in[n].used = true;
        in[n].source_hash = b.entry[i].source_hash;
        in[n].drop_index = b.entry[i].drop_index;
        in[n].taken = b.entry[i].taken != 0;
        CopyText2(in[n].item_path, sizeof(in[n].item_path), b.entry[i].item_path);
        ++n;
    }
    int changed = g_loot.Merge(in, n, !g_net.IsHost());   // the peer's inbound body is the host's
    if (changed) {
        g_lootRev = g_loot.Revision();
        for (int i = 0; i < b.n; ++i)
            if (b.entry[i].taken) LootHide(b.entry[i].source_hash, b.entry[i].drop_index);
        QDLOG(LOG_INFO, "loot: merged ", dec(changed), " entr",
              (changed == 1 ? "y" : "ies"), " rev=", dec(g_loot.Revision()));
    }
}

// ---------------------------------------------------------------------------
// Suspend saves
// ---------------------------------------------------------------------------
int32_t g_lastSuspendSlot = -1;

void SuspendPump(uint32_t now)
{
    if (!Cfg().suspend_guard) return;
    const bool live = g_net.Connected();
    const bool peer = g_net.IsPeer();

    if (g_suspend.WantHold(live, peer)) {
        if (peer) {
            void* gi = GameInstanceObj();
            static Fn fBlock = {};
            bool ok = gi && CallNoArgs(gi, fBlock, "BlockSuspendSaves");
            // A build that exposes it as a Blueprint bool rather than a function: the
            // game's own volume/enemy callers are the same lever either way.
            if (!ok && gi) ok = WriteBytePropC(gi, "BlockSuspendSaves", 1);
            QDLOG(LOG_INFO, "suspend: peer holds BlockSuspendSaves=", dec(ok ? 1 : 0),
                  " -- the game's timed suspend save must not advance past the host");
        }
        ++g_suspendSeq;
    }

    if (peer && Save().save) {
        int32_t slot = -1;
        if (IntPropC(Save().save, "ActiveSuspendSlot", &slot) && slot != g_lastSuspendSlot) {
            bool first = g_lastSuspendSlot == -1;
            g_lastSuspendSlot = slot;
            if (!first && slot >= 0) {
                char nm[32];
                NumText(slot, nm, sizeof(nm));
                if (g_suspend.OnLocalSuspendLoaded(nm, now)) {
                    ++g_suspendSeq;
                    QDLOG(LOG_WARN, "suspend: peer loaded a suspend save (slot ", nm,
                          "); announcing it so the host re-applies its state");
                }
            }
        }
    }

    if (g_net.IsHost() && g_suspend.TakeReapply()) {
        g_flagsDirty = true;
        g_invPub.ForceDirty();
        g_actorPub.ForceDirty();
        g_invForceObserve = true;
        ++g_suspendSeq;
        QDLOG(LOG_INFO, "suspend: host re-applying WORLD_FLAGS, ACTOR_STATES and "
                        "INVENTORY after the peer's suspend load");
    }

    if (g_suspend.Tick(now))
        QDLOG(LOG_WARN, "suspend: the peer's suspend load was not repaired within ",
              dec((int)(suspend::kReapplyDeadlineMs / 1000)), " s; reported, not retried");

    static uint32_t lastSend = 0;
    static uint8_t  lastState = 0xFF;
    uint8_t st = g_suspend.StateToSend();
    if (g_net.Connected() && (st != lastState || now - lastSend >= 1000)) {
        lastSend = now;
        lastState = st;
        wire::SuspendBody s;
        memset(&s, 0, sizeof(s));
        s.seq = g_suspend.Seq();
        s.state = st;
        CopyText2(s.slot, sizeof(s.slot), g_suspend.Slot());
        uint8_t body[96];
        wire::Writer w(body, sizeof(body));
        wire::WriteSuspend(w, s);
        if (w.ok) g_net.SendReliable(wire::K_SUSPEND, body, w.len);
    }
}

void OnSuspend(wire::Reader& r, uint32_t now)
{
    wire::SuspendBody s;
    if (!wire::ReadSuspend(r, &s)) return;
    if (s.state == wire::SS_LOADED) {
        if (g_suspend.OnPeerLoaded(s.seq, s.slot, now))
            QDLOG(LOG_WARN, "suspend: the partner loaded a suspend save (slot ",
                  (s.slot[0] ? s.slot : "?"), "); re-applying the host's state");
    } else if (s.state == wire::SS_REAPPLIED) {
        if (g_suspend.OnPeerReapplied(s.seq))
            QDLOG(LOG_INFO, "suspend: partner re-applied the host's state after its suspend load");
    }
}


// WHAT THIS MIRRORS
// -----------------
// `InventoryContents` is a TMap<UObject*, int32> at save+0x28 (measured; SYNC-DESIGN
// section 2.4).  The host publishes the WHOLE set as an absolute body and the peer
// grants only the difference through `SaveFunctions_C.AddItemToInventory`
// (WrGrantItem), which reads the game's own counter back and requires it to rise by
// exactly the requested amount.  The design's reason is the key: a key granted twice is
// harmless, a key the peer lacks locks that player out of the region alone.
//
// WHAT THIS DOES NOT DO
// ---------------------
// It does not remove anything, in either direction.  The host is the authority and
// `WrGrantItem` only adds; a count that falls is a consumption and is never sent,
// because re-granting a consumed item is the failure this lane must not have.  A
// peer's consumption is replenished by the next host body (the mirror is additive),
// and a host's consumption is not propagated to the peer at all (SYNC-DESIGN
// section 2.4 accepts that: the peer keeps its copy until it uses it).
//
// THE OTHER DIRECTION (G3)
// ------------------------
// A pickup the peer makes becomes `INTENT kind 2` (I_GRANT_ITEM).  `qdinvintent.h`
// holds the model: the peer sends its cumulative own contribution per path, the host
// applies only the increase, and the host's published body is the resolution.  The
// state objects (g_invPub, g_invGrants, g_invNow, the snapshot identity) are declared
// beside g_pendingFlags so the save-reinit path can reset them together.

// Resolving an item path means a GObjects walk (FindClassByName /
// FindObjectByNameAndClass), and the peer would otherwise pay that for every entry of
// every body -- a hitch on a large inventory for no benefit.  The cache is bounded and
// re-resolves when its object has gone away (a level change can unload an asset).
static const int kInvCache = 256;
struct InvCached {
    char  path[inventory::kMaxPath];
    void* obj;
};
InvCached g_invCache[kInvCache];
int       g_invCacheNext = 0;

void* ResolveInvItem(const char* path)
{
    for (int i = 0; i < kInvCache; ++i) {
        if (g_invCache[i].path[0] && inventory::PathEq(g_invCache[i].path, path)) {
            if (ObjectIsLive(g_invCache[i].obj)) return g_invCache[i].obj;
            g_invCache[i].obj = ResolveItemObject(path);
            return g_invCache[i].obj;
        }
    }
    void* obj = ResolveItemObject(path);
    InvCached& c = g_invCache[g_invCacheNext++ % kInvCache];
    inventory::PathCopy(c.path, sizeof(c.path), path);
    c.obj = obj;
    return obj;
}

// Host: one map element -> one soft object path.  The path form is the same helper the
// equipment and effect lanes use, because it is the identity SYNC-DESIGN section 2.4
// names (`/Game/.../AquaDoorKey_2.AquaDoorKey_2`) and the same one WrGrantItem resolves
// back to an object.  Sending pointers would be meaningless across processes.
struct InvCollect {
    inventory::Snapshot* snap;
    int                  dropped;
};

void CollectInventoryItem(void* item, int32_t count, void* user)
{
    InvCollect* c = (InvCollect*)user;
    // A zero is indistinguishable from absence on this lane and can never grant
    // anything; keeping it out of the body keeps the body to facts.
    if (count <= 0) return;

    // Two bytes of headroom over kMaxPath so a path exactly at the limit is accepted and
    // a longer one is rejected by Snapshot rather than silently truncated into an alias
    // of a different asset.
    char path[inventory::kMaxPath + 2];
    int n = 0;
    __try {
        n = avatar::AssetPath(item, path, sizeof(path));
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        n = 0;
    }
    if (n <= 0) { ++c->dropped; return; }
    if (!c->snap->Set(path, count)) ++c->dropped;
}

void ObserveHostInventory(uint32_t now)
{
    g_invNow.Clear();
    InvCollect c = { &g_invNow, 0 };
    int live = InventoryEnumerate(&CollectInventoryItem, &c);
    if (live < 0) {                       // the save was replaced under us; next tick
        g_invHaveSnapshot = false;
        return;
    }
    g_invSnapshotSave = Save().save;
    g_invHaveSnapshot = true;

    if (g_invPub.Observe(g_invNow)) {
        QDLOG(LOG_INFO, "inventory host set ", dec(g_invNow.Count()), " entr",
              (g_invNow.Count() == 1 ? "y" : "ies"),
              " (live ", dec(live), ") rev=", dec(g_invPub.Revision()));
    }
    if (c.dropped || g_invNow.Overflowed()) {
        static uint32_t lastLog = 0;
        if (now - lastLog > 5000) {
            lastLog = now;
            QDLOG(LOG_WARN, "inventory host: ", dec(c.dropped), " entr",
                  (c.dropped == 1 ? "y" : "ies"),
                  " could not be represented (path too long or table full); "
                  "they are NOT on the wire");
        }
    }
}

void HostPushInventory(uint32_t now)
{
    if (!Cfg().inventory_sync) return;
    if (!g_net.IsHost() || !g_net.Connected()) return;
    if (!Save().save) return;

    // Observe at 4 Hz, not at the send rate: a pickup is an event, and waiting for the
    // 1 Hz heartbeat would make the partner's key arrive up to a second late.  The walk
    // reads a few dozen map slots and neither creates nor writes anything.  A grant the
    // host just applied forces the walk now, so the peer's confirmation follows within
    // one frame instead of up to 250 ms later.
    if (g_invForceObserve || now - g_lastInvObserveMs >= 250) {
        g_invForceObserve = false;
        g_lastInvObserveMs = now;
        ObserveHostInventory(now);
    }

    // A snapshot from a save object that is no longer the active one is another
    // world's inventory.  Wait for the 4 Hz observation to read the new container
    // rather than grant items from a save the peer is not in.
    if (!g_invHaveSnapshot || g_invSnapshotSave != Save().save) return;
    if (ActiveSaveNoCreate(GWorldPtr()) != g_invSnapshotSave) return;

    if (!g_invPub.Due(now)) return;

    uint8_t body[wire::kMaxBody];
    inventory::HostPublisher::Chunk c = g_invPub.Peek(body, sizeof(body));
    if (c.blocked) {
        // Cannot happen while kMaxPath is far inside kMaxBody, but if it ever does, do
        // not spin on a datagram that can never hold the next entry.
        QDLOG(LOG_WARN, "inventory: no entry fits one datagram; the lane is stalled");
        return;
    }

    // Capture the revision stamped into THIS body: Commit bumps the counter when the
    // pass completes, and a log that read it afterwards would name the next one.  A
    // change-driven pass is INFO (it is the acceptance evidence); the 1 Hz heartbeat
    // with nothing changed is DEBUG so a quiet session does not fill the log.
    uint32_t rev = g_invPub.Revision();
    bool changed = g_invPub.Dirty();
    if (!g_net.SendUnreliable(wire::K_INVENTORY, body, c.bytes)) return;
    if (g_invPub.Commit(c.entries, now)) {
        QDLOG(changed ? LOG_INFO : LOG_DEBUG,
              "inventory sent rev=", dec(rev), " entries=", dec(g_invPub.Size()));
    }
}

// Peer: the peer's own count for a path, through the game's native counter.  A false
// return means "cannot answer yet" (asset not loaded, no save, no world), not "zero".
bool PeerInventoryCount(const char* path, int32_t* out)
{
    void* item = ResolveInvItem(path);
    if (!item) return false;
    void* world = GWorldPtr();
    if (!world) return false;
    return InventoryCountNative(world, item, out);
}

void PeerApplyInventory(wire::Reader& r, uint32_t now)
{
    if (!Cfg().inventory_sync) return;

    uint32_t rev = r.U32();
    uint16_t n   = r.U16();
    if (!r.ok) return;

    // The host's publisher restarts at revision 1 when its save is replaced.  A large
    // backwards step is that, not an out-of-order datagram: the peer's whole host view
    // (floors, confirmations, in-flight intents) names a container that no longer
    // exists.  A one-step regression is just reordering and is absorbed by the
    // high-water floors.
    if (g_invIntents.HaveHostBaseline() &&
        (int32_t)(rev - g_invIntents.HostRevision()) < -1) {
        QDLOG(LOG_INFO, "inventory host view reset: rev ", dec(rev), " after ",
              dec(g_invIntents.HostRevision()));
        g_invIntents.ResetHostView();
    }
    g_invIntents.BeginHostBody(rev, n);

    int seen = 0, queued = 0, satisfied = 0, unread = 0, overflow = 0, bad = 0;
    // `n` is the whole container size, not this datagram's share: a large set is split
    // across ticks, and the receiver learns how many arrived from where the body ends.
    // There is deliberately no produced count (the K_WORLD_FLAGS rule).
    while (r.ok && r.pos < r.len) {
        char path[inventory::kMaxPath];
        int len = r.Str(path, sizeof(path));
        int32_t count = r.I32();
        if (len < 0 || !r.ok) break;
        if (len >= inventory::kMaxPath) { ++bad; continue; }   // truncated by the reader
        ++seen;

        // Feed the peer -> host model the host's absolute count.  This is the set the
        // peer compares its own container against on the next local pass; the floor is
        // a high-water mark (qdinvintent.h) so a host consumption is never mistaken for
        // a peer surplus and granted back.
        invintent::PickupQueue::HostResult hr = g_invIntents.HostItem(path, count);
        if (hr == invintent::PickupQueue::kHostConfirmed) {
            QDLOG(LOG_INFO, "item intent confirmed: ", path, " host=", dec(count));
        } else if (hr == invintent::PickupQueue::kHostSuperseded) {
            QDLOG(LOG_INFO, "item intent superseded: ", path, " host=", dec(count),
                  " is already at or above the peer's ask");
        } else if (hr == invintent::PickupQueue::kHostConsumeConfirmed) {
            QDLOG(LOG_INFO, "item consume confirmed: ", path, " host=", dec(count));
        }

        int32_t local = 0;
        bool haveLocal = PeerInventoryCount(path, &local);
        // The host body may predate a use the peer made.  Grant up to the count the
        // host will hold once it applies that use, or the consumed item is handed
        // straight back and the count drifts upward (the G3 caveat).  The pump applies
        // the same adjustment at grant time, so a grant queued before the use cannot
        // undo it either.
        int32_t target = g_invIntents.GrantTarget(path, count);
        if (haveLocal && target <= local) {
            // The host is not ahead once the outstanding use is counted.  This settles
            // (cancels) any pending grant; it does NOT lower the peer's count, which
            // may be higher from a local pickup.
            g_invGrants.Done(path);
            ++satisfied;
            continue;
        }
        if (!haveLocal) ++unread;

        // Queue the ABSOLUTE host count, not the difference: the peer's count and the
        // outstanding use can both move between this body and the grant, and recomputing
        // the target at grant time makes a stale difference a no-op rather than an
        // over-grant.  An unresolvable item is queued too, so its failure is reported
        // and remembered.
        inventory::GrantQueue::Observed o = g_invGrants.Observe(path, count, now);
        if (o == inventory::GrantQueue::kQueued ||
            o == inventory::GrantQueue::kRaised) {
            ++queued;
        } else if (o == inventory::GrantQueue::kOverflow) {
            ++overflow;
        }
    }

    if (g_invIntents.EndHostBody(seen)) {
        QDLOG(LOG_INFO, "item intent baseline: host set complete at rev=", dec(rev),
              " n=", dec((int)n));
    }
    if (rev != g_peerInvRev) {
        g_peerInvRev = rev;
        QDLOG(LOG_INFO, "inventory body rev=", dec(rev), " n=", dec((int)n),
              " entries=", dec(seen), " applied=", dec(satisfied),
              " queued=", dec(queued));
    }
    if (bad) {
        QDLOG(LOG_WARN, "inventory: ", dec(bad), " entr", (bad == 1 ? "y" : "ies"),
              " had a truncated path and were refused (nothing granted)");
    }
    if (overflow) {
        QDLOG(LOG_WARN, "inventory: pending-grant table full; ", dec(overflow),
              " entr", (overflow == 1 ? "y" : "ies"), " were NOT remembered");
    }
    if (unread) {
        QDLOG(LOG_DEBUG, "inventory: ", dec(unread), " entr",
              (unread == 1 ? "y" : "ies"),
              " not resolvable yet; queued for retry");
    }
}

// Peer: spend at most kMaxPerTick grants per frame.  Each one is a ProcessEvent plus a
// native read-back, so doing a whole body in one frame is the hitch this bound exists
// to prevent.
void PeerGrantPump(uint32_t now)
{
    if (!Cfg().inventory_sync) return;
    if (!g_net.IsPeer() || !g_net.Connected()) return;

    inventory::GrantQueue::Out out[inventory::GrantQueue::kMaxPerTick];
    int n = g_invGrants.Due(now, out, inventory::GrantQueue::kMaxPerTick);
    for (int i = 0; i < n; ++i) {
        int32_t local = 0;
        if (!PeerInventoryCount(out[i].path, &local)) {
            // The difference is unknown, so do not guess one: WrGrantItem would then be
            // asked to add the wrong number.  Keep the item and try after the next body.
            g_invGrants.Failed(out[i].path, now);
            QDLOG(LOG_DEBUG, "inventory grant waiting: ", out[i].path,
                  " (peer count unreadable; kept)");
            continue;
        }
        int32_t target = g_invIntents.GrantTarget(out[i].path, out[i].target);
        int32_t need = target - local;
        // Re-evaluate against the use still outstanding NOW, not when the body queued
        // this grant.  A grant queued before the player used the item must not restore
        // it when it is applied a tick later.
        if (need <= 0) {
            // A local pickup (or the outstanding use) covered it between the body and
            // now.  Nothing to add and (by the design's rule) nothing to take away.
            g_invGrants.Done(out[i].path);
            continue;
        }

        char err[160];
        if (WrGrantItem(out[i].path, need, err, sizeof(err))) {
            g_invGrants.Done(out[i].path);
            // This much of the peer's local count was put there by the HOST, so it is
            // not the peer's own contribution and must not travel back (the echo rule in
            // qdinvintent.h).  Record it before the next local enumeration.
            g_invIntents.HostGranted(out[i].path, need);
            QDLOG(LOG_INFO, "inventory grant applied: ", out[i].path, " +", dec(need),
                  " (host ", dec(target), ", peer ", dec(local), ")");
        } else {
            // KEPT, not dropped.  The reason is logged and the item is retried after
            // kRetryMs; a transient "asset not loaded yet" must not lose a key.
            g_invGrants.Failed(out[i].path, now);
            QDLOG(LOG_WARN, "inventory grant refused: ", out[i].path, " +", dec(need),
                  ": ", err, " (kept for retry)");
        }
    }
}

// ---------------------------------------------------------------------------
// Peer: a pickup the peer made becomes an INTENT kind 2 (G3's other half)
// ---------------------------------------------------------------------------
// qdinvintent.h owns the model; this only supplies the container walk, the clock and
// the socket.  The walk is the SAME `InventoryEnumerate` the host publisher uses, so
// the two directions cannot disagree about what "the local inventory" is.
struct InvIntentCollect {
    invintent::PickupQueue* q;
    int unreadable;
};

void CollectInvIntentItem(void* item, int32_t count, void* user)
{
    InvIntentCollect* c = (InvIntentCollect*)user;
    // A zero is indistinguishable from absence on this lane and can never be a pickup.
    if (count <= 0) return;

    // Two bytes of headroom over kMaxPath so an exactly-limit path is accepted and a
    // longer one is refused by the queue rather than truncated into a different asset.
    char path[inventory::kMaxPath + 2];
    int n = 0;
    __try {
        n = avatar::AssetPath(item, path, sizeof(path));
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        n = 0;
    }
    if (n <= 0) { ++c->unreadable; return; }

    switch (c->q->LocalItem(path, count)) {
    case invintent::PickupQueue::kQueued:
        QDLOG(LOG_INFO, "item intent queued ", path, " x", dec(count));
        break;
    case invintent::PickupQueue::kCoalesced:
        QDLOG(LOG_INFO, "item intent superseded ", path, " by local ", dec(count));
        break;
    case invintent::PickupQueue::kConsumeQueued:
        QDLOG(LOG_INFO, "item consume queued ", path, " x", dec(count));
        break;
    case invintent::PickupQueue::kConsumeCoalesced:
        QDLOG(LOG_INFO, "item consume superseded ", path, " by local ", dec(count));
        break;
    case invintent::PickupQueue::kDropped:
        QDLOG(LOG_INFO, "item intent dropped (no longer held) ", path);
        break;
    default:
        break;
    }
}

void PeerInventoryIntentPump(uint32_t now)
{
    if (!Cfg().inventory_sync) return;
    if (!g_net.IsPeer() || !g_net.Connected()) return;
    if (!Save().save) return;
    // No complete host set yet: every local item is either pre-existing or one whose
    // host entry is still in a later chunk, and neither is a pickup.  Testing this
    // before the walk avoids paying the GObjects cost for a view that cannot be used.
    if (!g_invIntents.HaveHostBaseline()) return;

    InvIntentCollect c = { &g_invIntents, 0 };
    g_invIntents.BeginLocalPass();
    int live = InventoryEnumerate(&CollectInvIntentItem, &c);
    if (live < 0) {
        // The active save was replaced under us.  TryInitSaveLayer will reset the model;
        // do not compute a delta against a container that is no longer there, and do not
        // end the pass (an empty first pass would absorb nothing and upload later).
        g_invIntents.AbortLocalPass();
        return;
    }
    int vanished = g_invIntents.EndLocalPass();
    if (vanished) {
        QDLOG(LOG_INFO, "item intent dropped: ", dec(vanished),
              " contribution", (vanished == 1 ? "" : "s"),
              " the peer no longer holds");
    }

    // Both of these are table-full conditions, and both are the kind that would
    // otherwise be silent.  Rate-limited so a corrupt container cannot fill the log.
    if (c.unreadable || g_invIntents.PendingOverflowed() || g_invIntents.ItemOverflowed() ||
        g_invIntents.PathOverflowed()) {
        static uint32_t lastLog = 0;
        if (now - lastLog > 5000) {
            lastLog = now;
            QDLOG(LOG_WARN, "item intent: ", dec(c.unreadable),
                  " unreadable path(s); pending overflow=",
                  dec(g_invIntents.PendingOverflowed() ? 1 : 0),
                  " item overflow=", dec(g_invIntents.ItemOverflowed() ? 1 : 0),
                  " path overflow=", dec(g_invIntents.PathOverflowed() ? 1 : 0),
                  "; those pickups are NOT on the wire");
        }
    }

    invintent::PickupQueue::Out out[invintent::PickupQueue::kMaxPerTick];
    int n = g_invIntents.Due(now, out, invintent::PickupQueue::kMaxPerTick);
    for (int i = 0; i < n; ++i) {
        // kind 2 (pickup) or kind 3 (consumption), then str(item_path) i32(cumulative).
        // Both counters are cumulative, never deltas: the host's two ledgers grant or
        // remove only the increase, which is what makes retransmission exactly-once.
        uint8_t body[inventory::kMaxPath + 32];
        wire::Writer w(body, sizeof(body));
        wire::WriteIntentHeader(w, out[i].intentKind, out[i].basedOnRev);
        w.Str(out[i].path);
        w.I32(out[i].count);

        bool sent = w.ok && g_net.SendReliable(wire::K_INTENT, body, w.len);
        const bool consuming = (out[i].intentKind == wire::I_CONSUME_ITEM);
        if (sent) {
            QDLOG(LOG_INFO, consuming ? "item consume sent " : "item intent sent ",
                  out[i].path, " x", dec(out[i].count),
                  " rev=", dec(out[i].basedOnRev), " try ", dec(out[i].attempt), "/",
                  dec(invintent::PickupQueue::kMaxAttempts));
        } else {
            QDLOG(LOG_WARN, consuming ? "item consume send failed " : "item intent send failed ",
                  out[i].path, " x", dec(out[i].count));
        }

        // The attempt is spent whether or not the socket took it: the give-up must be
        // bounded, and the host's echo is the only thing that settles an intent.
        if (out[i].attempt >= invintent::PickupQueue::kMaxAttempts) {
            g_invIntents.GiveUp(out[i].path, out[i].intentKind);
            QDLOG(LOG_WARN, consuming ? "item consume given up " : "item intent given up ",
                  out[i].path, " x", dec(out[i].count),
                  " after ", dec(out[i].attempt), " attempts, the host never showed it");
        }
    }
}

// ---------------------------------------------------------------------------
// LAYER 4b -- one-shot world objects (SavableActorStates)
// ---------------------------------------------------------------------------
// Measured in SYNC-RESEARCH section A.8: the container is a reflected
// `TMap<FGuid,int32>` on the save object, and the game's own writer is
// `SavableActor_C.Save Data Index to World Save(Index, Data)`.  This is the host ->
// peer absolute set plus the peer -> host intent lane; the peer's local changes become
// `INTENT kind 6`, and the host applies them to its own container.
struct ActorCollect {
    actorstate::Snapshot* snap;
    int                   dropped;
};

void CollectActorStateRow(const QdGuid& guid, int32_t value, void* user)
{
    ActorCollect* c = (ActorCollect*)user;
    uint32_t g[4] = { guid.a, guid.b, guid.c, guid.d };
    if (!c->snap->Set(g, value)) ++c->dropped;
}

// Read the whole container through the measured hash-bucket walk.  A false/negative
// result means the save was replaced under us or the geometry refused; the caller does
// not publish a partial set as the truth.
bool ReadLocalActorStates(actorstate::Snapshot* out, int* live, int* dropped)
{
    out->Clear();
    ActorCollect c = { out, 0 };
    int n = ActorStateEnumerate(&CollectActorStateRow, &c);
    if (live) *live = n;
    if (dropped) *dropped = c.dropped;
    return n >= 0;
}

void ObserveHostActorStates()
{
    int live = 0, dropped = 0;
    if (!ReadLocalActorStates(&g_actorNow, &live, &dropped)) {
        g_actorHaveSnapshot = false;
        return;
    }
    g_actorSnapshotSave = Save().save;
    g_actorHaveSnapshot = true;

    if (g_actorPub.Observe(g_actorNow)) {
        QDLOG(LOG_INFO, "actor-state host set ", dec(g_actorNow.Count()), " entr",
              (g_actorNow.Count() == 1 ? "y" : "ies"), " (live ", dec(live),
              ") rev=", dec(g_actorPub.Revision()));
    }
    if (dropped || g_actorNow.Overflowed()) {
        static uint32_t lastLog = 0;
        uint32_t now = Net::Now();
        if (now - lastLog > 5000) {
            lastLog = now;
            QDLOG(LOG_WARN, "actor-state host: ", dec(dropped), " entr",
                  (dropped == 1 ? "y" : "ies"),
                  " could not be represented (table full); they are NOT on the wire");
        }
    }
}

// Host: the whole container, chunked across ticks exactly as HostPushFlags does.  `n` is
// the container size, never a produced count, and the cursor is a continuation rather
// than a fragment.
void HostPushActorStates(uint32_t now)
{
    if (!Cfg().actor_state_sync) return;
    if (!g_net.IsHost() || !g_net.Connected()) return;
    if (!Save().save) return;

    // 4 Hz observation so an interaction reaches the partner promptly; the send is still
    // on change plus 1 Hz.
    if (now - g_lastActorObserveMs >= actorstate::kObserveMs) {
        g_lastActorObserveMs = now;
        ObserveHostActorStates();
    }

    if (!g_actorHaveSnapshot || g_actorSnapshotSave != Save().save) return;
    if (ActiveSaveNoCreate(GWorldPtr()) != g_actorSnapshotSave) return;
    if (!g_actorPub.Due(now)) return;

    uint8_t body[wire::kMaxBody];
    actorstate::HostPublisher::Chunk c = g_actorPub.Peek(body, sizeof(body));
    if (c.blocked) {
        QDLOG(LOG_WARN, "actor-state: no entry fits one datagram; the lane is stalled");
        return;
    }

    uint32_t rev = g_actorPub.Revision();
    bool changed = g_actorPub.Dirty();
    if (!g_net.SendUnreliable(wire::K_ACTOR_STATES, body, c.bytes)) return;
    if (g_actorPub.Commit(c.entries, now)) {
        QDLOG(changed ? LOG_INFO : LOG_DEBUG, "actor states sent rev=", dec(rev),
              " entries=", dec(g_actorPub.Size()));
    }
}

// Peer: apply the host's absolute set.  The rule is AbsoluteAccept (a chest stays open,
// a door can close); the one exception is a GUID whose local intent is still in flight,
// which OnHostEntry holds rather than letting a 1 Hz heartbeat undo it.
void PeerApplyActorStates(wire::Reader& r, uint32_t now)
{
    (void)now;
    if (!Cfg().actor_state_sync) return;

    uint32_t rev = r.U32();
    uint16_t n   = r.U16();
    if (!r.ok) return;

    // Absolute state over an unreliable lane: a body older than the last applied one is
    // not stale data about a newer world, it is an older world.  Ignore it whole.
    if (rev < g_peerActorRev) return;

    static ActorStateOwner owners[actorstate::kMaxEntries];
    // -1 = not built.  A body where every entry is held needs no GObjects pass at all,
    // and an empty heartbeat body is common once per second; building the owner table for
    // those would be a needless walk of every live object.
    int ownerCount = -1;

    int seen = 0, applied = 0, held = 0, confirmed = 0, failed = 0;
    while (r.ok && r.pos < r.len) {
        uint32_t g[4];
        r.Guid(g);
        int32_t v = r.I32();
        if (!r.ok) break;
        ++seen;

        bool apply = true;
        actorstate::IntentQueue::Resolve res = g_pendingActors.OnHostEntry(g, v, &apply);
        if (res == actorstate::IntentQueue::kHeld) { ++held; continue; }
        if (res == actorstate::IntentQueue::kConfirmed) ++confirmed;
        if (!apply) { ++held; continue; }

        // Already the value we hold: the 1 Hz heartbeat re-sends the whole set, and
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
            ++failed;
            if (failed <= 4) {
                QDLOG(LOG_WARN, "actor-state apply failed rev=", dec(rev), ": ", err);
            }
        }
    }

    if (rev > g_peerActorRev) g_peerActorRev = rev;
    g_actorHostRev = g_peerActorRev;
    if (seen) {
        // INFO only when the body changed something.  The 1 Hz heartbeat carries the same
        // seven values every second, and logging that at INFO buries the one line a live
        // run is looking for: the moment a door or chest actually converged.
        int acted = applied + held + confirmed + failed;
        QDLOG(failed ? LOG_WARN : (acted ? LOG_INFO : LOG_DEBUG),
              "actor states applied=", dec(applied), " held=", dec(held),
              " confirmed=", dec(confirmed), " failed=", dec(failed),
              " rev=", dec(rev), " n=", dec((int)n));
    }
}

// Peer: turn local container changes into INTENT kind 6 and spend at most kMaxPerTick
// sends per frame.  The revision stamped into each intent is read at send time, so a
// retry always names the freshest host state this peer has seen.
void PeerActorIntentPump(uint32_t now)
{
    if (!Cfg().actor_state_sync) return;
    if (!g_net.IsPeer() || !g_net.Connected()) return;
    if (!Save().save) return;

    if (now - g_lastActorLocalObserveMs >= actorstate::kObserveMs) {
        g_lastActorLocalObserveMs = now;
        int live = 0, dropped = 0;
        if (ReadLocalActorStates(&g_actorNow, &live, &dropped)) {
            actorstate::LocalDetector::Stats st;
            g_actorMirror.Observe(g_actorNow, g_pendingActors, &st);
            if (st.queued || st.coalesced || st.overflow) {
                QDLOG(LOG_INFO, "actor-state local change: queued=", dec(st.queued),
                      " coalesced=", dec(st.coalesced), " echoes=", dec(st.echoes),
                      " overflow=", dec(st.overflow));
            }
        }
    }

    actorstate::IntentQueue::Out out[actorstate::IntentQueue::kMaxPerTick];
    int n = g_pendingActors.Due(now, out, actorstate::IntentQueue::kMaxPerTick);
    for (int i = 0; i < n; ++i) {
        // kind 6, then guid(16) i32(value): exactly ReadIntent's I_ACTOR_STATE layout.
        uint8_t body[64];
        wire::Writer w(body, sizeof(body));
        wire::WriteIntentHeader(w, wire::I_ACTOR_STATE, g_actorHostRev);
        w.Guid(out[i].guid);
        w.I32(out[i].value);

        bool sent = w.ok && g_net.SendReliable(wire::K_INTENT, body, w.len);
        if (sent) {
            QDLOG(LOG_INFO, "actor-state intent sent guid=", hex32(out[i].guid[0]), "-",
                  hex32(out[i].guid[1]), "-", hex32(out[i].guid[2]), "-",
                  hex32(out[i].guid[3]), " value=", dec(out[i].value),
                  " rev=", dec(g_actorHostRev), " try ", dec(out[i].attempt), "/",
                  dec(actorstate::IntentQueue::kMaxAttempts));
        } else {
            QDLOG(LOG_WARN, "actor-state intent send failed value=", dec(out[i].value));
        }

        // The attempt is spent whether or not the socket took it: the give-up has to be
        // bounded, and the host's echo is the only thing that settles an intent other
        // than this.  After the give-up the next host body wins.
        if (out[i].attempt >= actorstate::IntentQueue::kMaxAttempts) {
            g_pendingActors.GiveUp(out[i].guid);
            QDLOG(LOG_WARN, "actor-state intent given up guid=", hex32(out[i].guid[0]),
                  "-", hex32(out[i].guid[1]), "-", hex32(out[i].guid[2]), "-",
                  hex32(out[i].guid[3]), " value=", dec(out[i].value), " after ",
                  dec(out[i].attempt), " attempts, host never echoed it");
        }
    }
}

// The single tick hook the pump block calls.  Host and peer work are different directions
// of the same lane, but they share the save-snapshot identity, so one entry point keeps
// the reset rule in one place.
void ActorStatePump(uint32_t now)
{
    if (!Cfg().actor_state_sync) return;
    if (g_net.IsHost()) HostPushActorStates(now);
    else if (g_net.IsPeer()) PeerActorIntentPump(now);
}

// ---------------------------------------------------------------------------
// Inbound dispatch -- game thread only
// ---------------------------------------------------------------------------
void DispatchInbound(const InboundPacket& p, uint32_t now)
{
    wire::Reader r(p.body, p.body_len);
    // Hard transitions put the machines in different worlds, where an absolute body is
    // not stale but refers to something else.  The union refuses itself inside
    // OnLevelUnion; enemy rows, enemy detail and the dialogue cursor are refused here,
    // before they reach the enemy/dialogue regions.  K_TRAVEL is never filtered: it is how
    // the split is detected and how it clears again, and the hard handshake lives on it.
    // The gate follows travel too, so the follow works with StreamUnion=0.
    if ((Cfg().stream_union || Cfg().travel_sync) && !g_map.PeerStateApplies(now) &&
        (p.kind == wire::K_ENEMY || p.kind == wire::K_DIALOGUE ||
         p.kind == wire::K_ENEMY_DETAIL)) {
        return;
    }
    switch (p.kind) {
    case wire::K_PLAYER: {
        wire::PlayerRow row;
        if (!wire::ReadPlayer(r, &row)) return;
        motion::MoveSample s;
        s.ms = row.sample_ms;
        s.px = wire::DequantizePos(row.px);
        s.py = wire::DequantizePos(row.py);
        s.pz = wire::DequantizePos(row.pz);
        s.yaw = row.yaw;
        s.pitch = row.pitch;
        s.flags = row.flags;
        g_peerPos[0] = s.px;
        g_peerPos[1] = s.py;
        g_peerPos[2] = s.pz;
        bool inGame = (row.flags & 0x20) == 0;
        if (inGame != g_peerInGameplay) {
            g_peerInGameplay = inGame;
            QDLOG(LOG_INFO, "peer is ", (inGame ? "in the game world" : "in a menu world"));
            g_peerInterp.Reset();
        }
        if (inGame) g_peerInterp.Push(s);
        // Unreliable rows can arrive out of order; an older row must not roll the
        // action counter back (that would replay a swing).
        if ((int32_t)(row.sample_ms - g_peerActionMs) >= 0) {
            g_peerActionMs = row.sample_ms;
            g_peerAction = row.action;
            g_peerAirborne = (row.flags & 0x01) ? 0 : 1;
        }
        g_puppet.DataArrived(now);
        break;
    }
    case wire::K_EFFECT:
    case wire::K_EFFECT_MOVE: {
        wire::EffectBody e;
        if (!wire::ReadEffect(r, &e)) return;
        if (Cfg().puppet_sync && IsGameplayPawn(g_localPawn)) SpawnEffect(e, 0, 0);
        break;
    }
    case wire::K_WORLD_FLAGS:
        if (g_net.IsPeer()) PeerApplyFlags(r);
        break;
    case wire::K_ACTOR_STATES:
        // Host -> peer only.  The body is the absolute set; the peer applies it through
        // the game's own SavableActor writer and reads every value back.
        if (g_net.IsPeer()) PeerApplyActorStates(r, now);
        break;
    case wire::K_INVENTORY:
        // Host -> peer only.  The body is absolute and the apply is additive, so an
        // out-of-order or duplicated datagram cannot lower anything.
        if (g_net.IsPeer()) PeerApplyInventory(r, now);
        break;
    case wire::K_INTENT:
        // Progression intents are requests to the host.  An enemy hit is a message to
        // whichever machine OWNS that enemy, which may be either one.
        if (g_net.IsHost()) HostApplyIntent(r);
        else if (Cfg().enemy_sync) PeerApplyEnemyIntent(r);
        break;
    case wire::K_DIALOGUE:
        // Per-slot talk state, both directions (qddialogue.h TalkBoard).
        if (Cfg().dialogue_sync) OnDialogue(r, now);
        break;
    case wire::K_LEVEL_UNION:
        if (Cfg().stream_union) OnLevelUnion(r, now);
        break;
    case wire::K_TRAVEL:
        // Both machines announce; the comparison is what detects a split.  Reliable,
        // so a transition that crosses a dropped packet is still seen.  Travel is not
        // gated on the streaming union: a hard switch is a world change.
        if (Cfg().travel_sync || Cfg().stream_union) OnTravel(r, now);
        break;
    case wire::K_CUTSCENE:
        if (Cfg().cutscene_sync) OnCutscene(r, now);
        break;
    case wire::K_MOVERS:
        if (Cfg().mover_sync) OnMovers(r, now);
        break;
    case wire::K_LOOT:
        if (Cfg().loot_sync) OnLoot(r);
        break;
    case wire::K_SUSPEND:
        if (Cfg().suspend_guard) OnSuspend(r, now);
        break;
    case wire::K_STATUS: {
        uint32_t rev = 0, mask = 0;
        if (!wire::ReadStatus(r, &rev, &mask)) return;
        if (mask != g_peerStatus) QDLOG(LOG_INFO, "peer status mask ", hex32(mask), " rev ", dec((int)rev));
        g_peerStatusRev = rev;
        g_peerStatus = mask;
        break;
    }
    case wire::K_EQUIPMENT: {
        wire::EquipBody e;
        if (!wire::ReadEquip(r, &e)) return;
        if (e.rev == g_peerEquip.rev && EquipEqual(e, g_peerEquip)) break;
        g_peerEquip = e;
        g_peerEquipDirty = true;
        QDLOG(LOG_INFO, "peer equipment rev ", dec((int)e.rev), ": ", dec(e.n), " item(s)",
              (e.n > 0 ? " first=" : ""), (e.n > 0 ? e.items[0].path : ""));
        break;
    }
    case wire::K_ENEMY:
        // Rows travel both ways: whoever owns an enemy publishes it.
        if (Cfg().enemy_sync) OnEnemyRows(r, now);
        break;
    case wire::K_ENEMY_DETAIL:
        // The owner's animation and status, replayed on the follower's frozen copy.
        if (Cfg().enemy_sync) OnEnemyDetail(r, now);
        break;
    case wire::K_DAMAGE:
        // Host -> peer: an enemy the HOST runs attacked the partner across the
        // distance, and only the machine that owns the pawn can apply it.  This case
        // is last on purpose -- it is the newest kind and touches no other lane.
        if (Cfg().enemy_sync) OnPlayerDamage(r, now);
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Sending the local player
// ---------------------------------------------------------------------------
void SendLocalPlayer(uint32_t now)
{
    // The local pawn is tracked even without a session: the puppet class comes from it.
    void* pawn = LocalPawn();
    if (pawn == g_puppetActor) pawn = nullptr;     // never report the puppet as ourselves
    if (pawn != g_localPawn) {
        g_localPawn = pawn;
        if (pawn) {
            char d[160];
            ObjDescribe(pawn, d, sizeof(d));
            QDLOG(LOG_INFO, "local pawn: ", d);
        }
    }

    if (!g_net.Connected()) return;
    int hz = Cfg().player_hz;
    if (hz <= 0) hz = 30;
    uint32_t interval = (uint32_t)(1000 / hz);
    if (now - g_lastPlayerPushMs < interval) return;
    g_lastPlayerPushMs = now;
    if (!pawn) return;

    // The real transform, through reflected offsets.  The first version sent a zeroed
    // row because the offsets were unknown -- the puppet could never move.
    ActorXform x;
    if (!ReadActorXform(pawn, &x)) return;
    // Heading = where the player looks; the pawn root does not turn with the view.
    { float vp, vy; if (LocalViewRotation(&vp, &vy)) { x.yaw = vy; x.pitch = vp; } }

    wire::PlayerRow row;
    memset(&row, 0, sizeof(row));
    row.slot = g_net.IsHost() ? 0 : 1;
    row.sample_ms = now;
    row.px = wire::QuantizePos(x.px);
    row.py = wire::QuantizePos(x.py);
    row.pz = wire::QuantizePos(x.pz);
    row.yaw = wire::QuantizeAngle(x.yaw);
    row.pitch = wire::QuantizeAngle(x.pitch);
    row.vx = wire::QuantizeVel(x.vx);
    row.vy = wire::QuantizeVel(x.vy);
    row.vz = wire::QuantizeVel(x.vz);
    // WIRE.md 3.5: action = kind | counter << 5; move_mode = EMovementMode;
    // flags bit0 = grounded, bit5 = menu (the sender is not in the game world).
    row.action = g_localAct.byte;
    row.move_mode = g_localAct.moveMode;
    row.flags = IsGameplayPawn(pawn) ? 0 : 0x20;
    if (!g_localAct.airborne) row.flags |= 0x01;
    if (g_dlg.talking) row.flags |= 0x10;          // WIRE.md 3.5 bit4: in a dialogue

    uint8_t body[64];
    wire::Writer w(body, sizeof(body));
    wire::WritePlayer(w, row);
    if (w.ok) g_net.SendUnreliable(wire::K_PLAYER, body, w.len);

    static uint32_t lastPosLog = 0;
    if (now - lastPosLog > 10000) {
        lastPosLog = now;
        QDLOG(LOG_INFO, "local pos ", flt(x.px, 0), ",", flt(x.py, 0), ",", flt(x.pz, 0),
              " yaw=", flt(x.yaw, 0));
    }
}

// ---------------------------------------------------------------------------
// Hooks
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Substrate validation -- GAME THREAD ONLY
// ---------------------------------------------------------------------------
// Reads GEngine/GWorld and the FName pool. Those are plain reads, but they belong on
// the thread that owns the world: the first version ran them (and then the whole save
// layer) on the bootstrap thread and blocked inside engine code.
//
// Gated on the same readiness the bootstrap used to poll for -- GWorld set and the
// object graph populated -- but observed from where it is safe to act.
bool ValidateSubstrateOnGameThread()
{
    if (g_substrateRefused) return false;

    void* world = *(void**)Ptr<void*>(0x04BA86B0u);
    int objects = ObjectCount();
    if (!world || objects <= 1000) return false;      // not up yet; try next frame

    g_substrateValidated = true;
    SubstrateStatus st = RuntimeValidate();
    QDLOG(LOG_INFO, "substrate: globals=", dec(st.globals_ok ? 1 : 0),
          " fname=", dec(st.fname_ok ? 1 : 0), "(", st.class_name, ")",
          " gobjects=", dec(st.gobjects_ok ? 1 : 0), " objects=", dec(objects));
    if (!st.fname_ok) {
        g_substrateRefused = true;
        QDLOG(LOG_ERROR, "REFUSING TO RUN: the FName decoder did not reproduce "
                         "'GameEngine'; every name-based lookup would be garbage");
        return false;
    }
    if (!st.gobjects_ok) {
        g_substrateRefused = true;
        QDLOG(LOG_ERROR, "REFUSING TO RUN: GObjects liveness check failed");
        return false;
    }
    g_substrateReady = true;
    { QdName bp; if (FNameFromAnsi("ReceiveBeginPlay", &bp)) g_beginPlayIdx = bp.comparison_index; }
    return true;
}

// ---------------------------------------------------------------------------
// DEV ONLY -- load a save slot without driving the menu by keyboard
// ---------------------------------------------------------------------------
// Two-instance testing needs both games in the world, and keyboard navigation of File
// Select proved unreliable (presses dropped, cursor jumping) -- one attempt started a
// NEW GAME on an empty slot.  With QDCOOP_AUTOLOAD=<slot> in the environment, the
// title screen's own widget functions are called instead.  Off unless the variable is
// set; never in a player's build of the config.
int DevEnvInt(const char* name, int dflt)
{
    char buf[32];
    DWORD n = GetEnvironmentVariableA(name, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf)) return dflt;
    return atoi(buf);
}

// A live, non-CDO instance of a class, by class name.
void* FindInstanceOf(const char* className)
{
    int n = ObjectCount();
    for (int i = 0; i < n; ++i) {
        void* o = ObjectAt(i);
        if (!o || !LooksLikeUObject(o)) continue;
        if (!ObjNameIs(ObjClass(o), className)) continue;
        char nm[16];
        if (FNameToString(ObjNameRaw(o), nm, sizeof(nm)) > 0 && nm[0] == 'D' && nm[1] == 'e' &&
            nm[2] == 'f' && nm[3] == 'a' && nm[4] == 'u' && nm[5] == 'l' && nm[6] == 't')
            continue;
        return o;
    }
    return nullptr;
}

// Value of a -key=value command-line switch (case-insensitive key, quotes stripped).
bool CmdArg(const char* key, char* out, int cap)
{
    out[0] = 0;
    const char* cl = GetCommandLineA();
    for (const char* p = cl; p && *p; ++p) {
        const char* a = p;
        const char* k = key;
        while (*k && (*a | 0x20) == (*k | 0x20)) { ++a; ++k; }
        if (*k != 0 || *a != '=') continue;
        ++a;
        bool q = (*a == '"');
        if (q) ++a;
        int t = 0;
        while (*a && t < cap - 1 && (q ? *a != '"' : *a != ' ')) out[t++] = *a++;
        out[t] = 0;
        return t > 0;
    }
    return false;
}

// The rig's save for slot N: <UserDir>\Saved_<suffix>\SaveGames\World<N>.sav.  Without
// -UserDir (a normal player launch) this refuses, so autoload can never touch a real
// save, and it never loads a slot that has no save -- that is what starts a new game.
bool DevSlotHasSave(int slot)
{
    char ud[260], sfx[40];
    if (!CmdArg("-UserDir", ud, sizeof(ud)) || !CmdArg("-saveddirsuffix", sfx, sizeof(sfx))) return false;
    char path[400];
    wsprintfA(path, "%s\\Saved_%s\\SaveGames\\World%d.sav", ud, sfx, slot);
    DWORD a = GetFileAttributesA(path);
    QDLOG(LOG_INFO, "dev autoload: ", path, (a == INVALID_FILE_ATTRIBUTES ? " MISSING" : " present"));
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool DevCallNoArgs(void* obj, const char* fn)
{
    void* f = FindFunction(obj, fn);
    LayoutResult s = Signature(f);
    ArgList a;
    char err[128];
    bool ok = s.valid && Invoke(obj, f, s, a, nullptr, err, sizeof(err));
    QDLOG(LOG_INFO, "dev autoload: ", fn, (ok ? " called" : " FAILED"));
    return ok;
}

// QDCOOP_AUTOLOAD=<slot index>; index 0 is the first File Select row (World0.sav).
void DevAutoLoad(uint32_t now)
{
    static int slot = DevEnvInt("QDCOOP_AUTOLOAD", -1);
    static int stage = 0;           // 0 wait UI, 1 settle, 2 loaded, 3 suspend confirmed
    static uint32_t t0 = 0, last = 0;
    if (slot < 0 || stage < 0) return;
    if (IsGameplayPawn(g_localPawn)) { stage = -1; QDLOG(LOG_INFO, "dev autoload: in game"); return; }
    if (now - last < 1000) return;
    last = now;
    if (!Lay().valid && !EnsureActorApi()) return;

    static void* ui = nullptr;
    if (!ui || !ObjectIsLive(ui)) { ui = FindInstanceOf("TitleScreenUI_C"); if (!ui) return; }
    if (stage == 0) {
        if (!DevSlotHasSave(slot)) { stage = -1; QDLOG(LOG_WARN, "dev autoload: refused"); return; }
        stage = 1; t0 = now;
        return;
    }
    // Let the intro splashes and shader warm-up finish; the menu calls from there.
    if (stage == 1 && now - t0 > 10000) {
        void* f = FindFunction(ui, "Load Slot");
        LayoutResult s = Signature(f);
        ArgList a;
        a.AddInt("Slot", slot);
        a.AddBool("Backup", false);
        a.AddInt("BackupSlot", 0);
        char err[128];
        bool ok = s.valid && Invoke(ui, f, s, a, nullptr, err, sizeof(err));
        QDLOG(LOG_INFO, "dev autoload: Load Slot(", dec(slot), ") ", (ok ? "called" : err));
        stage = ok ? 2 : -1; t0 = now;
        return;
    }
    // A suspend save on the slot raises a "resume?" modal; accept it once.
    if (stage == 2 && now - t0 > 3000) {
        int mo = FindPropertyOffsetIn(ObjClass(ui), "ModalOpen");
        bool modal = mo >= 0 && *((uint8_t*)ui + mo) != 0;
        QDLOG(LOG_INFO, "dev autoload: modal open=", dec(modal ? 1 : 0));
        if (modal) DevCallNoArgs(ui, "ConfirmLoadSuspend");
        stage = 3;
    }
}

// ---------------------------------------------------------------------------
// DEV ONLY -- body test: QDCOOP_AVATAR_TEST=1 spawns candidate bodies in front of the
// local player so they can be judged on a screenshot with ONE instance.
// ---------------------------------------------------------------------------
void DevLogAttached(void* pawn)
{
    avatar::Api& a = avatar::A();
    if (a.attachParent < 0) return;
    int n = ObjectCount(), shown = 0;
    for (int i = 0; i < n && shown < 40; ++i) {
        void* o = ObjectAt(i);
        if (!o || !LooksLikeUObject(o)) continue;
        char cn[96];
        if (FNameToString(ObjNameRaw(ObjClass(o)), cn, sizeof(cn)) <= 0) continue;
        if (!EndsWith(cn, "MeshComponent")) continue;
        if (ObjOuter(o) == pawn) continue;
        void* p = o;
        bool ours = false;
        for (int d = 0; d < 6 && p; ++d) {
            p = *(void**)((uint8_t*)p + a.attachParent);
            if (p && ObjOuter(p) == pawn) { ours = true; break; }
        }
        if (!ours) continue;
        ++shown;
        char d1[160], d2[160], asset[200] = "-", sock[64] = "-";
        ObjDescribe(o, d1, sizeof(d1));
        ObjDescribe(ObjOuter(o), d2, sizeof(d2));
        int mo = FindPropertyOffsetIn(ObjClass(o), "StaticMesh");
        if (mo < 0) mo = FindPropertyOffsetIn(ObjClass(o), "SkeletalMesh");
        if (mo >= 0) avatar::AssetPath(*(void**)((uint8_t*)o + mo), asset, sizeof(asset));
        void* ap = *(void**)((uint8_t*)o + a.attachParent);
        if (a.attachSocket >= 0) FNameToString(*(QdName*)((uint8_t*)o + a.attachSocket), sock, sizeof(sock));
        char d3[160];
        ObjDescribe(ap, d3, sizeof(d3));
        QDLOG(LOG_INFO, "  attached ", d1, " of ", d2, " mesh=", asset, " parent=", d3, " socket=", sock,
              " b14d=", hex32(*((uint8_t*)o + 0x14D)));
    }
    QDLOG(LOG_INFO, "attached mesh components: ", dec(shown));
}

void DevLogBones(void* comp)
{
    int nb = avatar::NumBones(comp);
    QDLOG(LOG_INFO, "body bones: ", dec(nb));
    for (int i = 0; i < nb && i < 200; ++i) {
        QdName bn = avatar::BoneName(comp, i);
        char b[64], pn[64] = "-";
        if (FNameToString(bn, b, sizeof(b)) <= 0) continue;
        if (b[0] == 'H' && b[1] == 'a' && b[2] == 'i') continue;           // hair chain
        FNameToString(avatar::ParentBone(comp, bn), pn, sizeof(pn));
        avatar::Xf x;
        if (!avatar::SocketTransform(comp, bn, 2, &x)) continue;
        QDLOG(LOG_INFO, "  bone ", dec(i), " ", b, " <- ", pn, "  t=", flt(x.tx, 1), ",", flt(x.ty, 1), ",",
              flt(x.tz, 1), " q=", flt(x.qx, 3), ",", flt(x.qy, 3), ",", flt(x.qz, 3), ",", flt(x.qw, 3));
    }
}

// QDCOOP_AVATAR_TEST=1: a "mirror" body that copies the LOCAL player -- position offset
// ahead along the VIEW direction it had when spawned, same heading, same walk speed,
// same weapon -- so the remote-body code path can be judged with one instance.
//   QDCOOP_TEST_DIST   cm ahead (default 300)
//   QDCOOP_TEST_FACE=1 face the camera instead of copying the heading
//   QDCOOP_TEST_SPEED  walk in place at this speed (cm/s) and do not follow
bool g_devMirrorOn = false;
float g_devMirrorOffX = 0, g_devMirrorOffY = 0;

// Mirror mode: show the local throw as the peer would see it, from the mirror body.
void DevMirrorEffect(const wire::EffectBody& e)
{
    if (g_devMirrorOn) SpawnEffect(e, g_devMirrorOffX, g_devMirrorOffY);
}

// QDCOOP_TEST_FIRE=<projectile class path>: every 7 s, throw it through the player's own
// FireDagger(Class) -- for saves where throwing is not unlocked yet.
void DevTestFire(uint32_t now)
{
    static char path[256];
    static int on = GetEnvironmentVariableA("QDCOOP_TEST_FIRE", path, sizeof(path)) ? 1 : 0;
    static uint32_t last = 0, t0 = 0;
    static Fn fFire = {};
    if (!on) return;
    if (!IsGameplayPawn(g_localPawn)) { t0 = now; return; }
    static bool checked = true;
    if (!checked && now - last > 1200) {
        checked = true;
        ActorXform l, c2;
        bool hl = g_fxLastLocal && ObjectIsLive(g_fxLastLocal) && ReadActorXform(g_fxLastLocal, &l);
        bool hc = g_fxLastCopy && ObjectIsLive(g_fxLastCopy) && ReadActorXform(g_fxLastCopy, &c2);
        QDLOG(LOG_INFO, "test fire +1.2s: local ", (hl ? "" : "GONE "), flt(l.px, 0), ",", flt(l.py, 0), ",", flt(l.pz, 0),
              "  copy ", (hc ? "" : "GONE "), flt(c2.px, 0), ",", flt(c2.py, 0), ",", flt(c2.pz, 0),
              "  offset ", flt(g_devMirrorOffX, 0), ",", flt(g_devMirrorOffY, 0));
    }
    if (now - t0 < 25000 || now - last < 7000) return;
    checked = false;
    last = now;
    void* cls = avatar::LoadAsset(path);
    if (!cls || !fFire.Resolve(g_localPawn, "FireDagger")) { QDLOG(LOG_WARN, "test fire: no class or no FireDagger"); on = 0; return; }
    Call c(fFire);
    c.Obj("Class", cls);
    bool ran = c.Run(g_localPawn);
    uint8_t* ok = c.At("Success", 1);
    QDLOG(LOG_INFO, "test fire: FireDagger(", path, ") ran=", dec(ran ? 1 : 0), " success=", dec(ok ? *ok : -1));
}

// QDCOOP_TEST_TP="x,y,z,yaw": TEST INSTANCES ONLY -- 15 s into gameplay, put the local
// pawn there (and turn the view to yaw), so two instances can stand in each other's view.
void DevTeleport(uint32_t now)
{
    static char spec[96];
    static int stage = GetEnvironmentVariableA("QDCOOP_TEST_TP", spec, sizeof(spec)) ? 0 : -1;
    static uint32_t t0 = 0;
    if (stage < 0) return;
    if (!IsGameplayPawn(g_localPawn)) { t0 = now; return; }
    if (now - t0 < 15000) return;
    stage = -1;
    float v[4] = {0, 0, 0, 0};
    char* save = nullptr;
    char work[96];
    lstrcpynA(work, spec, sizeof(work));
    int n = 0;
    for (char* t = strtok_s(work, ",", &save); t && n < 4; t = strtok_s(nullptr, ",", &save)) v[n++] = (float)atof(t);
    bool ok = SetActorXform(g_localPawn, v[0], v[1], v[2], v[3]);
    void* pc = LocalController();
    int ro = pc ? FindPropertyOffsetIn(ObjClass(pc), "ControlRotation") : -1;
    if (ro >= 0) { float* r = (float*)((uint8_t*)pc + ro); r[0] = 0.0f; r[1] = v[3]; }
    QDLOG(LOG_INFO, "dev teleport to ", flt(v[0], 0), ",", flt(v[1], 0), ",", flt(v[2], 0), " yaw ", flt(v[3], 0),
          ok ? "" : " FAILED");
}

// The rig's control files.  The environment is not the place for them: measured twice, an
// instance launched through the harness did not see QDCOOP_TEST_TPFILE even though its
// parent did inherit it, and the scenario silently drove nothing.  A test instance always
// has -UserDir, so the control file lives there and needs no configuration; the
// environment variable stays as an override.
void DevRigPath(const char* envName, const char* leaf, char* out, int cap)
{
    out[0] = 0;
    if (GetEnvironmentVariableA(envName, out, cap) && out[0]) return;
    char ud[260];
    if (!CmdArg("-UserDir", ud, sizeof(ud)) || !ud[0]) { out[0] = 0; return; }
    lstrcpynA(out, ud, cap);
    int n = lstrlenA(out);
    if (n > 0 && n < cap - 1 && out[n - 1] != '\\') { out[n++] = '\\'; out[n] = 0; }
    lstrcpynA(out + n, leaf, cap - n);
}

// An integer rig lever, read from the environment OR from a control file in the
// UserDir.  The control file is what the rig uses, for the same measured reason the
// teleport file exists: `QDCOOP_TEST_TPFILE` was set in the parent and did not arrive in
// an instance launched through the harness.  Control files need no inheritance.
//
//  returns 0 = off, `onValue` = "1" (plain "on"), or the value read.
// `dflt` is what a present-but-empty file means.
int DevLever(const char* envVar, const char* leaf, int dflt, int onValue)
{
    int v = 0;
    char buf[64];
    if (GetEnvironmentVariableA(envVar, buf, sizeof(buf)) && buf[0]) {
        v = atoi(buf);
        return v <= 1 ? onValue : v;
    }
    char p[MAX_PATH];
    if (!CmdArg("-UserDir", p, sizeof(p)) || !p[0]) return 0;
    int n = lstrlenA(p);
    if (n > 0 && n < (int)sizeof(p) - 1 && p[n - 1] != '\\') { p[n++] = '\\'; p[n] = 0; }
    lstrcpynA(p + n, leaf, sizeof(p) - n);
    HANDLE h = CreateFileA(p, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return 0;
    DWORD got = 0;
    (void)ReadFile(h, buf, sizeof(buf) - 1, &got, nullptr);
    CloseHandle(h);
    buf[got] = 0;
    if (!buf[0]) return dflt;
    v = atoi(buf);
    return v <= 1 ? onValue : v;
}

// QDCOOP_TEST_TPFILE=<path>: TEST INSTANCES ONLY -- teleport the local pawn to whatever
// "x,y,z,yaw" the named file holds, whenever its contents change.
//
// The launch-time QDCOOP_TEST_TP can place a player once, at a spot fixed before the
// process starts. A live test of the co-op lanes needs the player moved to a door, then
// to a pickup, then to an enemy, inside one session; a file the driver rewrites is the
// smallest interface that allows it, and it needs no window focus.
//
// WHY "near:<kind>" AND NOT JUST COORDINATES
// ------------------------------------------
// Coordinates copied out of the scan killed the peer twice: the first put the pawn
// inside a brazier, the second over a drop.  Both are the same mistake -- treating a
// number in a log as a place a player can stand.  So the file also accepts
// `near:<substring>`, which resolves a live actor and puts the pawn BESIDE it: offset
// horizontally along the approach direction, at that actor's own height, which is the
// height the level placed it at.  The actor is the authority on where the ground is;
// the driver is not.
// A candidate's own name or its class name.  `near:chest` has to find a
// `Chest_00_C` whose instance name is `Chest_00`, and `near:door` a doorway whose
// instance name is a number, so both halves are tested.
bool RigNameMatches(void* actor, const char* sub)
{
    if (!actor || !sub || !sub[0]) return false;
    char nm[64] = "", cl[48] = "";
    FNameToString(ObjNameRaw(actor), nm, sizeof(nm));
    FNameToString(ObjNameRaw(ObjClass(actor)), cl, sizeof(cl));
    // A class named `...Door...StateManager` is bookkeeping, not a door: Session 6 sent
    // the peer 423 m and 33 km down to one, which is the same blind-teleport mistake in a
    // new costume.  The whole CLASS is excluded, not just the class-name match: the
    // instance is called `SkullDoorQuestStateManager`, so it also matched on its own name.
    static const char* const kJunk[] = {"Manager", "Subsystem", "Component", "State", "Data"};
    for (int k = 0; k < (int)(sizeof(kJunk) / sizeof(kJunk[0])); ++k)
        if (enemyown::HasCI(cl, kJunk[k])) return false;
    if (nm[0] && enemyown::HasCI(nm, sub)) return true;
    if (cl[0] && enemyown::HasCI(cl, sub)) return true;
    return false;
}

// The nearest PLACED actor of a class, by object-graph walk.  Needed for the kinds that
// are not in `SavableActorStates` at all: an NPC to talk to, a checkpoint (whose unlock
// IS a NamedSaveData flag), a sub-level door.  A CDO or a component has no transform and
// is skipped, so this never reports a class default as a place to stand.
void* RigNearestOfClass(const char* clsName, const char* sub, const ActorXform& me, float* distOut,
                        ActorXform* xfOut, char* nameOut, int nameCap)
{
    if (distOut) *distOut = -1.0f;
    void* base = FindClassByName(clsName);
    if (!base) return nullptr;
    void* best = nullptr;
    float bestD = -1.0f;
    ActorXform bestXf = {};
    int count = ObjectCount();
    for (int i = 0; i < count; ++i) {
        void* o = ObjectAt(i);
        if (!o || !LooksLikeUObject(o)) continue;
        bool member = false;
        __try { member = IsSubclassOf(ObjClass(o), base); }
        __except (EXCEPTION_EXECUTE_HANDLER) { continue; }
        if (!member) continue;
        if (sub && sub[0] && !RigNameMatches(o, sub)) continue;
        ActorXform x;
        if (!ReadActorXform(o, &x)) continue;
        float dx = x.px - me.px, dy = x.py - me.py, dz = x.pz - me.pz;
        float d = sqrtf(dx * dx + dy * dy + dz * dz);
        if (bestD >= 0.0f && d >= bestD) continue;
        bestD = d; best = o; bestXf = x;
    }
    if (!best) return nullptr;
    if (distOut) *distOut = bestD;
    if (xfOut) *xfOut = bestXf;
    if (nameOut && nameCap > 0) {
        nameOut[0] = 0;
        FNameToString(ObjNameRaw(best), nameOut, nameCap);
    }
    return best;
}

// The nearest live actor of any kind whose name matches, out of the two sets a scenario
// can actually use: the save's savable actors (doors, chests, barricades, keys, pickups)
// and the enemies this machine knows about.
void* RigFindNear(const char* sub, float* distOut, ActorXform* xfOut, char* nameOut, int nameCap)
{
    if (distOut) *distOut = -1.0f;
    if (!sub || !sub[0]) return nullptr;
    ActorXform me;
    if (!ReadActorXform(g_localPawn, &me)) return nullptr;

    void* best = nullptr;
    float bestD = -1.0f;
    ActorXform bestXf = {};
    const bool wantEnemy = (enemyown::HasCI(sub, "enemy") || enemyown::HasCI("enemy", sub));

    static ActorStateOwner owners[256];
    int n = BuildActorStateOwners(owners, 256);
    for (int i = 0; i < n; ++i) {
        if (!owners[i].actor || !ObjectIsLive(owners[i].actor)) continue;
        if (!RigNameMatches(owners[i].actor, sub)) continue;
        ActorXform x;
        if (!ReadActorXform(owners[i].actor, &x)) continue;
        float dx = x.px - me.px, dy = x.py - me.py, dz = x.pz - me.pz;
        float d = sqrtf(dx * dx + dy * dy + dz * dz);
        if (bestD >= 0.0f && d >= bestD) continue;
        bestD = d; best = owners[i].actor; bestXf = x;
    }
    for (int i = 0; i < g_enN; ++i) {
        EnemyRec& r = g_en[i];
        if (!r.haveGuid || !ObjectIsLive(r.actor) || EnemyDead(r.actor)) continue;
        // `near:enemy` means "the nearest enemy", not "an actor whose NAME contains
        // 'enemy'" -- no enemy is called that.  Measured: the literal match made every
        // G4/enemy step a no-op with "no live actor matches 'enemy'".
        if (!wantEnemy && !RigNameMatches(r.actor, sub)) continue;
        ActorXform x;
        if (!ReadActorXform(r.actor, &x)) continue;
        float dx = x.px - me.px, dy = x.py - me.py, dz = x.pz - me.pz;
        float d = sqrtf(dx * dx + dy * dy + dz * dz);
        if (bestD >= 0.0f && d >= bestD) continue;
        bestD = d; best = r.actor; bestXf = x;
    }

    // Nothing in the two synced sets matched.  The remaining useful kinds are in neither
    // of them -- an NPC, a checkpoint and a sub-level door are plain placed actors -- so
    // the last resort is a class walk keyed on the word the scenario asked for.
    if (!best) {
        static const struct { const char* key; const char* cls; } kKeys[] = {
            {"npc", "NPC_Base_C"},
            {"checkpoint", "Checkpoint_C"},
            {"sublevel", "SubLevelDoor_C"},
        };
        for (int k = 0; k < (int)(sizeof(kKeys) / sizeof(kKeys[0])) && !best; ++k) {
            if (!enemyown::HasCI(sub, kKeys[k].key) && !enemyown::HasCI(kKeys[k].key, sub)) continue;
            float d2 = -1.0f;
            ActorXform x2 = {};
            char n2[64] = "";
            void* a2 = RigNearestOfClass(kKeys[k].cls, nullptr, me, &d2, &x2, n2, sizeof(n2));
            if (!a2) continue;
            bestD = d2; best = a2; bestXf = x2;
            if (nameOut && nameCap > 0) { nameOut[0] = 0; FNameToString(ObjNameRaw(a2), nameOut, nameCap); }
        }
    }
    if (!best) return nullptr;
    if (distOut) *distOut = bestD;
    if (xfOut) *xfOut = bestXf;
    if (nameOut && nameCap > 0) {
        nameOut[0] = 0;
        FNameToString(ObjNameRaw(best), nameOut, nameCap);
    }
    return best;
}

// QDCOOP_TEST_NEAR: cm to stand from a `near:` target.  Far enough not to spawn inside
// it, close enough that the interaction trace reaches it.
void DevTeleportToTarget(const char* sub, float yawOverride, bool haveYaw)
{
    float d = 0.0f;
    ActorXform t = {};
    char nm[64] = "-";
    void* actor = RigFindNear(sub, &d, &t, nm, sizeof(nm));
    if (!actor) {
        QDLOG(LOG_WARN, "dev teleport near: no live actor matches '", sub, "'");
        return;
    }
    float off = (float)DevEnvInt("QDCOOP_TEST_NEAR", 140);
    if (off < 40.0f) off = 40.0f;
    if (off > 400.0f) off = 400.0f;

    ActorXform me = {};
    if (!ReadActorXform(g_localPawn, &me)) return;
    float dx = me.px - t.px, dy = me.py - t.py;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1.0f) { dx = 1.0f; dy = 0.0f; len = 1.0f; }
    dx /= len; dy /= len;

    float px = t.px + dx * off;
    float py = t.py + dy * off;
    // The actor's own height is the level's ground for that actor; a `z` copied from a
    // distant log line is not.
    float pz = t.pz;
    float yaw = haveYaw ? yawOverride : atan2f(-dx, -dy) * 57.29578f;  // face the target
    bool ok = SetActorXform(g_localPawn, px, py, pz, yaw);
    void* pc = LocalController();
    int ro = pc ? FindPropertyOffsetIn(ObjClass(pc), "ControlRotation") : -1;
    if (ro >= 0) { float* r = (float*)((uint8_t*)pc + ro); r[0] = 0.0f; r[1] = yaw; }
    QDLOG(LOG_INFO, "dev teleport near '", sub, "' -> ", nm, " at ", flt(t.px, 0), ",", flt(t.py, 0),
          ",", flt(t.pz, 0), " dist=", flt(d, 0), "; standing at ", flt(px, 0), ",", flt(py, 0), ",",
          flt(pz, 0), " offset=", flt(off, 0), (ok ? "" : " FAILED"));
}

void DevTeleportFile(uint32_t now)
{
    static char path[MAX_PATH];
    static int on = (DevRigPath("QDCOOP_TEST_TPFILE", "qd.tp", path, sizeof(path)), path[0] != 0) ? 1 : 0;
    static uint32_t lastPoll = 0;
    static char applied[128] = "";
    static bool said = false;
    if (!said) {
        said = true;
        QDLOG(LOG_INFO, "rig: tp control '", path, "' ", (on ? "enabled" : "DISABLED"),
              " exists=", (path[0] && GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES ? "yes" : "no"));
    }
    if (!on || !IsGameplayPawn(g_localPawn)) return;
    if (now - lastPoll < 200) return;      // a file read per tick would be a syscall storm
    lastPoll = now;

    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    char buf[128];
    DWORD got = 0;
    (void)ReadFile(h, buf, sizeof(buf) - 1, &got, nullptr);
    CloseHandle(h);
    buf[got] = 0;
    for (int i = 0; buf[i]; ++i) {
        if (buf[i] == '\r' || buf[i] == '\n' || buf[i] == ' ') { buf[i] = 0; break; }
    }
    // The same instruction is not a new one: without this the pawn would be pinned to the
    // file's coordinates every 200 ms, and a scenario that walks into a door could never.
    if (buf[0] == 0 || strcmp(buf, applied) == 0) return;
    lstrcpynA(applied, buf, sizeof(applied));

    if (!strncmp(buf, "near:", 5)) {
        // `near:<substring>[,yaw]` -- stand beside a live actor by name.
        char* spec = buf + 5;
        float yaw = 0.0f;
        bool haveYaw = false;
        char* comma = nullptr;
        for (char* p = spec; *p; ++p) if (*p == ',') { comma = p; break; }
        if (comma) { *comma = 0; yaw = (float)atof(comma + 1); haveYaw = true; }
        DevTeleportToTarget(spec, yaw, haveYaw);
        return;
    }

    float v[4] = {0, 0, 0, 0};
    char* save = nullptr;
    int n = 0;
    for (char* t = strtok_s(buf, ",", &save); t && n < 4; t = strtok_s(nullptr, ",", &save))
        v[n++] = (float)atof(t);
    if (n < 3) { QDLOG(LOG_WARN, "dev teleport file: need x,y,z[,yaw]"); return; }
    bool ok = SetActorXform(g_localPawn, v[0], v[1], v[2], v[3]);
    void* pc = LocalController();
    int ro = pc ? FindPropertyOffsetIn(ObjClass(pc), "ControlRotation") : -1;
    if (ro >= 0) { float* r = (float*)((uint8_t*)pc + ro); r[0] = 0.0f; r[1] = v[3]; }
    QDLOG(LOG_INFO, "dev teleport file -> ", flt(v[0], 0), ",", flt(v[1], 0), ",", flt(v[2], 0),
          " yaw ", flt(v[3], 0), (ok ? "" : " FAILED"));
}

// QDCOOP_TEST_SCAN=1: TEST INSTANCES ONLY -- 20 s into gameplay and then every 10 s, log
// where the local player is and what is nearest to it: an enemy and a one-shot world
// object (chest, door, barricade).
//
// A scenario driver has to be told coordinates to walk to. Reading them off the level
// layout would be a guess dressed up as a test, and the interesting actors are not where
// the save starts the player.
void DevScanNearestBody();

// The scan also names the nearest actor of a few classes that are NOT savables: an NPC to
// talk to, and a checkpoint (whose unlock is a `NamedSaveData` flag, so it is the one
// interaction that can demonstrate the G1 lane without a barricade).
void RigScanActorsOfClass(const ActorXform& me)
{
    static const char* const names[] = {"NPC_Base_C", "Checkpoint_C", "SubLevelDoor_C"};
    for (int k = 0; k < (int)(sizeof(names) / sizeof(names[0])); ++k) {
        float d = 0.0f;
        ActorXform x = {};
        char nm[64] = "";
        void* a = RigNearestOfClass(names[k], nullptr, me, &d, &x, nm, sizeof(nm));
        if (!a) { QDLOG(LOG_INFO, "scan: class ", names[k], " none in reach of the object graph"); continue; }
        QDLOG(LOG_INFO, "scan: class ", names[k], " ", nm, " at ", flt(x.px, 0), ",", flt(x.py, 0),
              ",", flt(x.pz, 0), " dist=", flt(d, 0));
    }
}

// File scope, not function-local: a dynamically-initialised static (`DevEnvInt(...)`)
// needs a thread-safe init guard, which makes the frame require object unwinding, and
// MSVC refuses __try in such a frame (C2712).
int      g_devScanOn = -1;          // -1 = not read yet
uint32_t g_devScanT0 = 0, g_devScanLast = 0;

void DevScanNearest(uint32_t now)
{
    if (g_devScanOn < 0) g_devScanOn = DevLever("QDCOOP_TEST_SCAN", "qd.scan", 1, 1);
    if (!g_devScanOn) return;
    if (!IsGameplayPawn(g_localPawn)) { g_devScanT0 = now; return; }
    if (now - g_devScanT0 < 20000) return;
    if (g_devScanLast && now - g_devScanLast < 10000) return;
    g_devScanLast = now;

    // Everything the body does dereferences actors the game may be destroying right now:
    // it runs during a world reload too (game over -> Continue destroys and recreates the
    // pawn), and reading a name or a component of an object that died between
    // `ObjectIsLive` and the read is an access violation.  Measured: the peer crashed on
    // Continue with WINMM+0x18c36 in the stack, which is inside this probe.  A probe must
    // never be able to take the session down.
    __try { DevScanNearestBody(); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        QDLOG(LOG_WARN, "scan: fault while reading actors (world reload?); scan skipped");
    }
}

// Separate function because MSVC refuses __try in a frame that requires object unwinding
// (C2712), and the wrapper's static initialisers do.
void DevScanNearestBody()
{
    ActorXform me;
        if (!ReadActorXform(g_localPawn, &me)) return;
    QDLOG(LOG_INFO, "scan: local at ", flt(me.px, 0), ",", flt(me.py, 0), ",", flt(me.pz, 0),
          " yaw ", flt(me.yaw, 0));

    float bestD = -1.0f;
    ActorXform best = {};
    uint32_t bestGuid[4] = {0, 0, 0, 0};
    for (int i = 0; i < g_enN; ++i) {
        EnemyRec& r = g_en[i];
        if (!r.haveGuid || !ObjectIsLive(r.actor) || EnemyDead(r.actor)) continue;
        ActorXform x;
        if (!ReadActorXform(r.actor, &x)) continue;
        float dx = x.px - me.px, dy = x.py - me.py, dz = x.pz - me.pz;
        float d = sqrtf(dx * dx + dy * dy + dz * dz);
        if (bestD >= 0.0f && d >= bestD) continue;
        bestD = d; best = x;
        memcpy(bestGuid, r.guid, 16);
    }
    if (bestD >= 0.0f) {
        QDLOG(LOG_INFO, "scan: nearest enemy guid=", hex32(bestGuid[0]), " at ", flt(best.px, 0),
              ",", flt(best.py, 0), ",", flt(best.pz, 0), " dist=", flt(bestD, 0));
    }

    static ActorStateOwner owners[64];
    int n = BuildActorStateOwners(owners, 64);
    bestD = -1.0f;
    const ActorStateOwner* bestOwner = nullptr;
    for (int i = 0; i < n; ++i) {
        if (!owners[i].actor || !ObjectIsLive(owners[i].actor)) continue;
        ActorXform x;
        if (!ReadActorXform(owners[i].actor, &x)) continue;
        float dx = x.px - me.px, dy = x.py - me.py, dz = x.pz - me.pz;
        float d = sqrtf(dx * dx + dy * dy + dz * dz);
        if (bestD >= 0.0f && d >= bestD) continue;
        bestD = d; bestOwner = &owners[i]; best = x;
    }
    if (bestOwner) {
        char nm[64] = "";
        FNameToString(ObjNameRaw(bestOwner->actor), nm, sizeof(nm));
        char gs[80] = "";
        EncodeGuidForLog(bestOwner->guid, gs, sizeof(gs));
        QDLOG(LOG_INFO, "scan: nearest savable ", nm, " guid=", gs, " index=", dec(bestOwner->index),
              " at ", flt(best.px, 0), ",", flt(best.py, 0), ",", flt(best.pz, 0),
              " dist=", flt(bestD, 0));
    } else {
        QDLOG(LOG_INFO, "scan: no live savable actor in the object graph");
    }

    // The five nearest one-shot objects, by name: the nearest one is usually a coin in a
    // corner, and a scenario needs a chest or a door, which it can only choose between if
    // it can see the names.  A pick is marked taken so the next pass finds a different
    // one -- marked, not cleared: clearing the entry hid it from the KIND list below,
    // which then reported the nearest coin as 195 m away instead of 46 m (measured,
    // Session 6).
    bool taken[64] = {false};
    for (int k = 0; k < 5; ++k) {
        float bd = -1.0f;
        int bi = -1;
        for (int i = 0; i < n && i < 64; ++i) {
            if (taken[i] || !owners[i].actor || !ObjectIsLive(owners[i].actor)) continue;
            ActorXform x;
            if (!ReadActorXform(owners[i].actor, &x)) continue;
            float dx = x.px - me.px, dy = x.py - me.py, dz = x.pz - me.pz;
            float d = sqrtf(dx * dx + dy * dy + dz * dz);
            if (bd >= 0.0f && d >= bd) continue;
            bd = d; bi = i;
        }
        if (bi < 0) break;
        char nm[64] = "";
        FNameToString(ObjNameRaw(owners[bi].actor), nm, sizeof(nm));
        ActorXform x;
        taken[bi] = true;
        if (!ReadActorXform(owners[bi].actor, &x)) continue;
        QDLOG(LOG_INFO, "scan: savable ", dec(k), " ", nm, " at ", flt(x.px, 0), ",", flt(x.py, 0),
              ",", flt(x.pz, 0), " dist=", flt(bd, 0));
    }

    // THE SAFE TARGET LIST.  Measured (LIVE-TEST-LOG Session 5): the five nearest
    // one-shot objects were five coins, and the two blind teleports that followed a
    // coordinate list like that killed the peer -- one into a brazier, one into a fall.
    // "Nearest savable" therefore cannot choose a door, and a coordinate copied out of
    // the log is not a place a player can stand.  So the probe names the nearest
    // representative of each KIND together with its own position, and the rig teleports
    // to that actor (`near:<kind>` in qd.tp), not to a number.
    {
        static const char* const kinds[] = {"door",    "chest",  "barricade", "key", "coin",
                                            "pickup",  "altar",  "lever",     "npc", "teleport"};
        for (int k = 0; k < (int)(sizeof(kinds) / sizeof(kinds[0])); ++k) {
            float bd = -1.0f;
            int bi = -1;
            for (int i = 0; i < n; ++i) {
                if (!owners[i].actor || !ObjectIsLive(owners[i].actor)) continue;
                if (!RigNameMatches(owners[i].actor, kinds[k])) continue;
                ActorXform x;
                if (!ReadActorXform(owners[i].actor, &x)) continue;
                float dx = x.px - me.px, dy = x.py - me.py, dz = x.pz - me.pz;
                float d = sqrtf(dx * dx + dy * dy + dz * dz);
                if (bd >= 0.0f && d >= bd) continue;
                bd = d; bi = i;
            }
            if (bi < 0) continue;
            char nm[64] = "";
            FNameToString(ObjNameRaw(owners[bi].actor), nm, sizeof(nm));
            ActorXform x;
            if (!ReadActorXform(owners[bi].actor, &x)) continue;
            char gs[80] = "";
            EncodeGuidForLog(owners[bi].guid, gs, sizeof(gs));
            QDLOG(LOG_INFO, "scan: kind ", kinds[k], " ", nm, " guid=", gs, " at ", flt(x.px, 0), ",",
                  flt(x.py, 0), ",", flt(x.pz, 0), " dist=", flt(bd, 0));
        }
        // The enemy is the other half of the list: a scenario has to stand next to one to
        // be attacked by it (G4) or to kill it (the ownership lane).
        {
            float bd = -1.0f;
            int bi = -1;
            for (int i = 0; i < g_enN; ++i) {
                EnemyRec& r = g_en[i];
                if (!r.haveGuid || !ObjectIsLive(r.actor) || EnemyDead(r.actor)) continue;
                ActorXform x;
                if (!ReadActorXform(r.actor, &x)) continue;
                float dx = x.px - me.px, dy = x.py - me.py, dz = x.pz - me.pz;
                float d = sqrtf(dx * dx + dy * dy + dz * dz);
                if (bd >= 0.0f && d >= bd) continue;
                bd = d; bi = i;
            }
            if (bi >= 0) {
                char nm[64] = "";
                FNameToString(ObjNameRaw(g_en[bi].actor), nm, sizeof(nm));
                ActorXform x;
                if (ReadActorXform(g_en[bi].actor, &x))
                    QDLOG(LOG_INFO, "scan: kind enemy ", nm, " guid=", hex32(g_en[bi].guid[0]), " at ",
                          flt(x.px, 0), ",", flt(x.py, 0), ",", flt(x.pz, 0), " dist=", flt(bd, 0),
                          " local-run=", dec(g_own.Running(g_en[bi].guid) ? 1 : 0));
            }
        }
    }

    RigScanActorsOfClass(me);

    // What the game itself thinks the player can interact with right now.  A scenario
    // that has to guess which of forty actors is a chest is not a test.
    //
    // `Current Interact Actor` is a property of the INTERACTION COMPONENT, not of the
    // pawn (the cooked name table lists it beside `PlayerInteractionComponent`), which is
    // why the probe reported "-" on every pass of Session 5 even with an actor in reach.
    void* comp = ObjPropC(g_localPawn, "PlayerInteractionComponent");
    void* tgt = comp ? ObjPropC(comp, "Current Interact Actor") : nullptr;
    if (!tgt) tgt = ObjPropC(g_localPawn, "Current Interact Actor");
    char td[96] = "-";
    if (tgt) ObjDescribe(tgt, td, sizeof(td));
    QDLOG(LOG_INFO, "scan: interact target ", td, " (component=", (comp ? "found" : "MISSING"), ")");

    float hp = 0.0f;
    if (FloatPropC(g_localPawn, "Health", &hp)) QDLOG(LOG_INFO, "scan: health ", flt(hp, 1));
    else QDLOG(LOG_INFO, "scan: health unreadable");
}

// QDCOOP_TEST_ACTFILE=<path>: TEST INSTANCES ONLY -- perform the player's own interact
// verb once, each time the named file's contents change.
//
// Measured on the rig: a synthetic key posted to the game window reaches the UI (the title
// and file-select screens respond) but NOT gameplay -- pressing E or W in the world moves
// and interacts with nothing, because the engine reads device state rather than window
// messages for in-world input.  So the rig drives the verb the key is bound to, through
// the game's own function: `Interact` on the pawn or on its interaction component, which
// is the same code path the key press ends in.
// File-scope, not function-local: a `static Fn` needs a thread-safe init guard, which
// makes the enclosing frame require object unwinding, and MSVC refuses __try there.

// A verb resolved once per (object, name).  Fixed-size and linear because there are a
// handful; a map would be machinery for its own sake.
struct RigVerb {
    Fn          f;
    void*       obj;
    const char* name;
};
RigVerb g_rigVerbs[24];
int     g_rigVerbN = 0;

// Call `name` on `obj`, resolving it once.  When the verb takes exactly one object
// parameter that parameter is the interactive actor: that is not a guess, it is the only
// object parameter such a verb can have, and leaving it null is what makes a Blueprint
// interaction branch out immediately.
bool RigCallVerb(void* obj, const char* name, void* arg, const char** ranName)
{
    if (!obj) return false;
    RigVerb* slot = nullptr;
    for (int i = 0; i < g_rigVerbN; ++i)
        if (g_rigVerbs[i].obj == obj && strcmp(g_rigVerbs[i].name, name) == 0) { slot = &g_rigVerbs[i]; break; }
    if (!slot) {
        if (g_rigVerbN >= (int)(sizeof(g_rigVerbs) / sizeof(g_rigVerbs[0]))) return false;
        slot = &g_rigVerbs[g_rigVerbN++];
        slot->f = Fn();
        slot->obj = obj;
        slot->name = name;
    }
    if (!slot->f.Resolve(obj, name)) return false;
    Call c(slot->f);
    if (arg) {
        for (int i = 0; i < slot->f.sig.props; ++i) {
            const PropDesc& d = slot->f.sig.p[i];
            if (!NameEquals(d.type, "ObjectProperty") || (d.flags & CPF_OutParm) || d.size < 8) continue;
            c.Obj(d.name, arg);
            break;
        }
    }
    bool ok = c.Run(obj);
    if (ok && ranName) *ranName = name;
    return ok;
}

void DevInteractBody(void* explicitTarget)
{
    // WHERE THE VERB ACTUALLY IS.  Measured (Session 6): `PlayerInteractionComponent_C`
    // has exactly five UFUNCTIONs -- `Update`, `ReceiveBeginPlay`, `ReceiveTick`,
    // `ExecuteUbergraph_...` and a delegate signature.  It TRACKS the target
    // (`Current Interact Actor`); it does not interact.  The verb is on the target
    // itself: `Checkpoint_C.Interact` was the one call that ran in Session 5.  So a
    // scenario names the actor (`qd.act` = `near:checkpoint`) and this calls the
    // target's own verb -- an interaction the component's trace would have dispatched.
    void* comp = ObjPropC(g_localPawn, "PlayerInteractionComponent");
    void* tgt = explicitTarget;
    if (!tgt && comp) tgt = ObjPropC(comp, "Current Interact Actor");
    if (!tgt) tgt = ObjPropC(g_localPawn, "Current Interact Actor");
    char d[96] = "-";
    if (tgt) ObjDescribe(tgt, d, sizeof(d));

    // PRESS THEN HOLD.  A key press is not one message: the game's own binding calls
    // `Interact` and then `InteractHeld` for as long as the key is down, and an
    // interactable that fills a bar over time only completes on the second.
    const char* ran1 = nullptr;
    const char* ran2 = nullptr;
    static const char* const onTgt[]  = {"Interact", "InteractHeld", "OnInteract", "Use", "Activate",
                                         "StartDialogue", "Talk", "BeginDialogue", "StartConversation"};
    static const char* const onComp[] = {"Interact", "DoInteract"};
    static const char* const onPawn[] = {"Interact", "DoInteract"};
    for (int i = 0; i < (int)(sizeof(onTgt) / sizeof(onTgt[0])) && !ran1; ++i)
        RigCallVerb(tgt, onTgt[i], g_localPawn, &ran1);
    for (int i = 0; i < (int)(sizeof(onComp) / sizeof(onComp[0])) && !ran1; ++i)
        RigCallVerb(comp, onComp[i], tgt, &ran1);
    for (int i = 0; i < (int)(sizeof(onPawn) / sizeof(onPawn[0])) && !ran1; ++i)
        RigCallVerb(g_localPawn, onPawn[i], tgt, &ran1);
    if (ran1) {
        RigCallVerb(tgt, "InteractHeld", g_localPawn, &ran2);
        if (!ran2) RigCallVerb(comp, "InteractHeld", tgt, &ran2);
    }

    QDLOG(LOG_INFO, "dev interact: target=", d, " component=", (comp ? "found" : "MISSING"),
          (ran1 ? " verb=" : " no verb ran"), (ran1 ? ran1 : ""), (ran2 ? "+" : ""), (ran2 ? ran2 : ""));
    if (!ran1 && tgt) {
        // A refused verb is only diagnosable if the real surface is on the record.
        static void* dumpedFor = nullptr;
        if (dumpedFor != tgt) {
            dumpedFor = tgt;
            LogFunctionsOf(tgt, d, 40);
        }
    }
}

char     g_devActPath[MAX_PATH];
int      g_devActOn = -1;
uint32_t g_devActPoll = 0;
char     g_devActApplied[64] = "";

void DevInteract(uint32_t now)
{
    if (g_devActOn < 0)
        g_devActOn = (DevRigPath("QDCOOP_TEST_ACTFILE", "qd.act", g_devActPath, sizeof(g_devActPath)), g_devActPath[0] != 0) ? 1 : 0;
    // Same diagnostic as the teleport control, and for the same reason: measured twice,
    // a lever whose path lookup failed was silent, and every later "it does not fire"
    // conclusion was a guess about which half was broken.
    {
        static bool said = false;
        if (!said) {
            said = true;
            QDLOG(LOG_INFO, "rig: act control '", g_devActPath, "' ", (g_devActOn ? "enabled" : "DISABLED"),
                  " exists=", (g_devActPath[0] && GetFileAttributesA(g_devActPath) != INVALID_FILE_ATTRIBUTES ? "yes" : "no"));
        }
    }
    if (!g_devActOn || !IsGameplayPawn(g_localPawn)) return;
    if (now - g_devActPoll < 200) return;
    g_devActPoll = now;

    HANDLE h = CreateFileA(g_devActPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    char buf[64];
    DWORD got = 0;
    (void)ReadFile(h, buf, sizeof(buf) - 1, &got, nullptr);
    CloseHandle(h);
    buf[got] = 0;
    for (int i = 0; buf[i]; ++i) {
        if (buf[i] == '\r' || buf[i] == '\n' || buf[i] == ' ') { buf[i] = 0; break; }
    }
    if (buf[0] == 0 || strcmp(buf, g_devActApplied) == 0) return;
    lstrcpynA(g_devActApplied, buf, sizeof(g_devActApplied));

    // `near:<name>`: interact with THAT actor.  The name is the instruction, so a
    // scenario can say which door, chest or NPC it means instead of hoping the engine's
    // own trace happened to be pointing at it.
    void* explicitTarget = nullptr;
    if (!strncmp(buf, "near:", 5)) {
        float d = 0.0f;
        ActorXform xf = {};
        char nm[64] = "";
        explicitTarget = RigFindNear(buf + 5, &d, &xf, nm, sizeof(nm));
        if (!explicitTarget) {
            QDLOG(LOG_WARN, "dev interact: no live actor matches '", buf + 5, "'");
            return;
        }
        QDLOG(LOG_INFO, "dev interact: resolved '", buf + 5, "' -> ", nm, " at ", flt(xf.px, 0), ",",
              flt(xf.py, 0), ",", flt(xf.pz, 0), " dist=", flt(d, 0));
    }

    // Same rule as the scan: this dereferences the pawn, its component and whatever the
    // game currently considers interactable, and it runs while a reload may be tearing
    // those down.
    __try { DevInteractBody(explicitTarget); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        QDLOG(LOG_WARN, "dev interact: fault while resolving the verb; skipped");
    }
}

// QDCOOP_TEST_GOD=1: TEST INSTANCES ONLY -- keep the local player alive while a scenario
// teleports it around looking for a door, a chest or an enemy.
//
// Measured: two blind teleports ended a session, one into a brazier and one into a fall,
// and each death reloaded the world.
//
// THE CHEAT OBJECT IS NOT AVAILABLE ON THIS BUILD.  `FS_Cheats_C` is found, but neither
// `GodModeOn` nor `HealToFull` resolves (Session 6 measured `GodModeOn=no HealToFull=no`),
// and `fn <name>: not found` is logged for both.  So the mechanism that actually works is
// a FLOOR ON THE HEALTH PROPERTY: the pawn's own `Health` is a FloatProperty (the same
// one the damage lane reads back, so a rig that pins it is still hurtable in the sense
// that matters -- the hit still lands and is still logged -- it just cannot reach 0).
//
// The class's real UFunction surface is dumped once, because "the name we guessed is not
// there" is only useful next to the list of what is.
Fn g_devGodFn = {}, g_devHealFn = {};
int      g_devGodOn = -1;

struct RigGodState {
    int    armed;          // 0 no, 1 yes
    int    floorSet;
    float  floor;
    float  peak;
    int    refills;
    uint32_t lastLog;
};
RigGodState g_rigGod = {0, 0, 0, 0, 0, 0};

// The cheat object is a convenience, not the mechanism.  Try the two names this module
// knew plus the plausible spellings, and let the log say which (if any) exists.
void DevGodCheats()
{
    void* ch = FindInstanceOf("FS_Cheats_C");
    if (!ch) { QDLOG(LOG_WARN, "god: FS_Cheats_C not found"); return; }
    static bool dumped = false;
    if (!dumped) {
        dumped = true;
        LogFunctionsOf(ch, "FS_Cheats_C", 120);
    }
    // MEASURED: this build has no `GodModeOn` and no `HealToFull`.  The god switch is
    // called `DeadGod` (72 UFUNCTIONs on the class, listed above).  Both old names are
    // still tried so the log records the miss rather than a guess.
    bool god = CallNoArgs(ch, g_devGodFn, "GodModeOn");
    bool heal = CallNoArgs(ch, g_devHealFn, "HealToFull");
    static Fn fDeadGod = {};
    bool dead = CallNoArgs(ch, fDeadGod, "DeadGod");
    QDLOG(LOG_INFO, "god: GodModeOn=", (god ? "ok" : "no"), " HealToFull=", (heal ? "ok" : "no"),
          " DeadGod=", (dead ? "ok" : "no"),
          " (health floor=", flt(g_rigGod.floor, 0), ", hp=", flt(g_rigGod.peak, 1), ")");
}

// Called from the frame pump.  Cheap enough to run every tick: one property read and,
// only when it has dropped, one write.
void DevGod(uint32_t now)
{
    if (g_devGodOn < 0) {
        // TWO gates, on purpose.  A lever asks for it (the environment, or the `qd.god`
        // control file in the UserDir); the `-UserDir` argument is what makes this a rig
        // instance at all (the launcher always passes it, and a normal player never
        // does).  A switch that can only be reached from a test instance cannot be left
        // on by accident in normal play.
        char ud[260];
        bool rig = CmdArg("-UserDir", ud, sizeof(ud)) && ud[0] != 0;
        int want = DevLever("QDCOOP_TEST_GOD", "qd.god", 400, 400);
        g_devGodOn = (want && rig) ? 1 : 0;
        g_rigGod.floor = want > 0 ? (float)want : 400.0f;
        if (want && !rig) QDLOG(LOG_WARN, "god: the rig asks for god mode but this is not a rig instance (no -UserDir); refusing");
        if (g_devGodOn)
            QDLOG(LOG_INFO, "rig: god control enabled (rig-only, floor=", flt(g_rigGod.floor, 0), ")");
    }
    if (!g_devGodOn || !IsGameplayPawn(g_localPawn)) return;

    if (!g_rigGod.armed) {
        g_rigGod.armed = 1;
        __try { DevGodCheats(); }
        __except (EXCEPTION_EXECUTE_HANDLER) { QDLOG(LOG_WARN, "god: fault while calling the cheat object"); }
    }

    float hp = 0.0f;
    if (!FloatPropC(g_localPawn, "Health", &hp)) {
        static uint32_t lastWarn = 0;
        if (now - lastWarn > 5000) { lastWarn = now; QDLOG(LOG_WARN, "god: the pawn has no readable Health property"); }
        return;
    }
    if (hp > g_rigGod.peak) g_rigGod.peak = hp;
    float floorHp = g_rigGod.floor;
    if (floorHp < g_rigGod.peak) floorHp = g_rigGod.peak;
    if (hp >= floorHp) return;
    if (!SetFloatPropC(g_localPawn, "Health", floorHp)) return;
    ++g_rigGod.refills;
    if (now - g_rigGod.lastLog > 1000) {
        g_rigGod.lastLog = now;
        QDLOG(LOG_INFO, "god: health refilled ", flt(hp, 1), " -> ", flt(floorHp, 1),
              " (refills=", dec(g_rigGod.refills), ")");
    }
}

// QDCOOP_TEST_HIT=<amount> (<UserDir>\qd.hit): TEST INSTANCES ONLY -- hit the nearest enemy
// this machine RUNS through the module's own `TryApplyDamage` payload, every
// QDCOOP_TEST_HITMS (default 3000).  The kill lane needs a kill that both machines
// attribute correctly, and the peer's swing cannot be synthesised (measured: window
// messages do not reach gameplay input), so the damage is applied here instead.
void DevKillNearest(uint32_t now)
{
    // Polled, not latched: a scenario has to switch the kill on for the one step that
    // wants it (and off again), and a lever read once at launch could only be armed for
    // the whole session -- which would damage enemies during every earlier step.
    static int amount = 0;
    static uint32_t lastPoll = 0, last = 0;
    if (now - lastPoll >= 500) { lastPoll = now; amount = DevLever("QDCOOP_TEST_HIT", "qd.hit", 200, 200); }
    if (!amount || !IsGameplayPawn(g_localPawn)) return;
    int gap = DevEnvInt("QDCOOP_TEST_HITMS", 3000);
    if (last && now - last < (uint32_t)gap) return;
    last = now;

    ActorXform me;
    if (!ReadActorXform(g_localPawn, &me)) return;
    // The three nearest locally-run enemies, tried in order.  Not every enemy answers
    // `TryApplyDamage` -- measured: `Looker_Base_BP_C` has neither that message nor
    // `GetHP` (the hit logged `ran=0 hp 0.0 -> 0.0`), while a fighter does. Giving up at
    // the nearest one tested the class that cannot be hit and called it a failure.
    EnemyRec* cand[3] = {nullptr, nullptr, nullptr};
    float cd[3] = {-1.0f, -1.0f, -1.0f};
    bool cf[3] = {false, false, false};
    for (int i = 0; i < g_enN; ++i) {
        EnemyRec& r = g_en[i];
        if (!r.haveGuid || !ObjectIsLive(r.actor) || EnemyDead(r.actor)) continue;
        ActorXform x;
        if (!ReadActorXform(r.actor, &x)) continue;
        float dx = x.px - me.px, dy = x.py - me.py, dz = x.pz - me.pz;
        float d = sqrtf(dx * dx + dy * dy + dz * dz);
        if (d > 1500.0f) continue;
        bool running = g_own.Running(r.guid);
        for (int k = 0; k < 3; ++k) {
            if (cd[k] >= 0.0f && d >= cd[k]) continue;
            for (int j = 2; j > k; --j) { cand[j] = cand[j - 1]; cd[j] = cd[j - 1]; cf[j] = cf[j - 1]; }
            cand[k] = &r; cd[k] = d; cf[k] = !running;
            break;
        }
    }
    if (!cand[0]) {
        QDLOG(LOG_WARN, "rig: hit -- no live enemy within 15 m of ", flt(me.px, 0), ",", flt(me.py, 0),
              ",", flt(me.pz, 0));
        return;
    }
    for (int k = 0; k < 3 && cand[k]; ++k) {
        float before = 0, after = 0;
        bool haveHp = false;
        bool ok = ApplyDamageToActor(cand[k]->actor, (float)amount, g_localPawn, false, &before, &after, &haveHp);
        QDLOG(LOG_INFO, "rig: hit enemy guid=", hex32(cand[k]->guid[0]), " amount=", dec(amount),
              " ran=", dec(ok ? 1 : 0), " hp ", flt(before, 1), " -> ", flt(after, 1),
              (cf[k] ? " (FROZEN copy: its HP belongs to the host)" : " (local-run)"));
        if (ok) return;
    }
}

// QDCOOP_TEST_CHEAT=<function> (<UserDir>\qd.cheat, argument in qd.cheatarg): TEST
// INSTANCES ONLY -- call one of the game's own `FS_Cheats_C` UFUNCTIONs on this machine.
//
// WHY THE CHEAT OBJECT IS THE RIGHT ORIGIN FOR A LANE TEST.  The lanes are about what
// happens AFTER a machine's world changes; how the change was caused is not part of the
// claim.  The two levers that can cause a change without a keyboard are the interaction
// component (whose `Current Interact Actor` only fills when the engine's own trace finds
// a real E-interactable -- coins and fruit are overlap pickups, so the act lever does
// nothing near them) and this object, which is the game's own code path for exactly these
// events: `UnlockAllCheckpoints` raises a NamedSaveData flag, `GiveAllKeys` adds items,
// `KillAllEnemies` records deaths, `ForceOpenDoor` changes a savable's state.
//
// The function list is dumped once by DevGodCheats, so the attempt is always visible next
// to the surface that exists.
void DevCheatCall(const char* fn, float amount, bool hasAmount)
{
    void* ch = FindInstanceOf("FS_Cheats_C");
    if (!ch) { QDLOG(LOG_WARN, "rig: cheat ", fn, " -- FS_Cheats_C not found"); return; }
    static Fn f = {};
    static char lastName[64] = "";
    if (strcmp(lastName, fn) != 0) { f = Fn(); lstrcpynA(lastName, fn, sizeof(lastName)); }
    if (!f.Resolve(ch, fn)) {
        QDLOG(LOG_WARN, "rig: cheat ", fn, " -- no such UFunction on FS_Cheats_C");
        LogFunctionsOf(ch, "FS_Cheats_C", 120);
        return;
    }
    Call c(f);
    if (hasAmount) {
        // The cheat signature is not known ahead of time; a float parameter named like an
        // amount is filled, and if there is none the call is still made (most cheats take
        // nothing).
        for (int i = 0; i < f.sig.props; ++i) {
            const PropDesc& p = f.sig.p[i];
            if (p.flags & CPF_OutParm) continue;
            if (NameEquals(p.type, "FloatProperty") || NameEquals(p.type, "DoubleProperty")) {
                c.F32(p.name, amount);
                break;
            }
        }
    }
    bool ok = c.Run(ch);
    QDLOG(LOG_INFO, "rig: cheat ", fn, (hasAmount ? " (amount given)" : ""), " ran=", dec(ok ? 1 : 0));
}

char     g_devCheatPath[MAX_PATH];
int      g_devCheatOn = -1;
uint32_t g_devCheatPoll = 0;
char     g_devCheatApplied[64] = "";

void DevCheat(uint32_t now)
{
    if (g_devCheatOn < 0) {
        char ud[260];
        bool rig = CmdArg("-UserDir", ud, sizeof(ud)) && ud[0] != 0;
        g_devCheatOn = (DevRigPath("QDCOOP_TEST_CHEAT", "qd.cheat", g_devCheatPath, sizeof(g_devCheatPath)),
                        (g_devCheatPath[0] != 0 && rig)) ? 1 : 0;
        if (g_devCheatOn)
            QDLOG(LOG_INFO, "rig: cheat control '", g_devCheatPath, "' enabled exists=",
                  (GetFileAttributesA(g_devCheatPath) != INVALID_FILE_ATTRIBUTES ? "yes" : "no"));
    }
    if (!g_devCheatOn || !IsGameplayPawn(g_localPawn)) return;
    if (now - g_devCheatPoll < 500) return;
    g_devCheatPoll = now;

    HANDLE h = CreateFileA(g_devCheatPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    char buf[96];
    DWORD got = 0;
    (void)ReadFile(h, buf, sizeof(buf) - 1, &got, nullptr);
    CloseHandle(h);
    buf[got] = 0;
    for (int i = 0; buf[i]; ++i) {
        if (buf[i] == '\r' || buf[i] == '\n' || buf[i] == ' ') { buf[i] = 0; break; }
    }
    if (buf[0] == 0 || strcmp(buf, g_devCheatApplied) == 0) return;
    lstrcpynA(g_devCheatApplied, buf, sizeof(g_devCheatApplied));

    float amount = 0.0f;
    bool hasAmount = false;
    {
        char ap[MAX_PATH];
        if (CmdArg("-UserDir", ap, sizeof(ap)) && ap[0]) {
            int nn = lstrlenA(ap);
            if (nn > 0 && nn < (int)sizeof(ap) - 1 && ap[nn - 1] != '\\') { ap[nn++] = '\\'; ap[nn] = 0; }
            lstrcpynA(ap + nn, "qd.cheatarg", sizeof(ap) - nn);
            HANDLE ah = CreateFileA(ap, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (ah != INVALID_HANDLE_VALUE) {
                char ab[48];
                DWORD ag = 0;
                (void)ReadFile(ah, ab, sizeof(ab) - 1, &ag, nullptr);
                CloseHandle(ah);
                ab[ag] = 0;
                if (ab[0]) { amount = (float)atof(ab); hasAmount = true; }
            }
        }
    }
    __try { DevCheatCall(buf, amount, hasAmount); }
    __except (EXCEPTION_EXECUTE_HANDLER) { QDLOG(LOG_WARN, "rig: cheat ", buf, " -- faulted"); }
}

void DevAvatarTest(uint32_t now)
{
    static int on = DevEnvInt("QDCOOP_AVATAR_TEST", 0);
    static int stage = 0;
    static uint32_t t0 = 0;
    static avatar::Body body = {};
    static float offX = 0, offY = 0, baseX = 0, baseY = 0, baseZ = 0, baseYaw = 0;
    static uint32_t lastEquipRev = 0;
    static int face = DevEnvInt("QDCOOP_TEST_FACE", 0);
    static int force = DevEnvInt("QDCOOP_TEST_SPEED", 0);
    if (!on || stage < 0) return;
    if (!IsGameplayPawn(g_localPawn)) { t0 = now; return; }
    ActorXform x;
    if (!ReadActorXform(g_localPawn, &x)) return;
    float viewPitch = 0, viewYaw = x.yaw;
    LocalViewRotation(&viewPitch, &viewYaw);
    if (stage == 0) {
        static bool tp = GetEnvironmentVariableA("QDCOOP_TEST_TP", nullptr, 0) > 0;
        if (now - t0 < (tp ? 18000u : 12000u)) return;   // the view settles after a load (and a test teleport)
        float yr = viewYaw * 3.14159265f / 180.0f;
        float d = (float)DevEnvInt("QDCOOP_TEST_DIST", 300);
        offX = cosf(yr) * d;
        offY = sinf(yr) * d;
        baseX = x.px + offX; baseY = x.py + offY; baseZ = x.pz;
        baseYaw = face ? viewYaw + 180.0f : viewYaw;
        if (!SpawnBody(&body, baseX, baseY, baseZ, baseYaw)) { stage = -1; return; }
        body.forceSpeed = (float)force;
        body.actSlow = (float)DevEnvInt("QDCOOP_TEST_ACTSLOW", 1);
        g_devMirrorOn = true; g_devMirrorOffX = offX; g_devMirrorOffY = offY;
        g_devMirrorBody = &body;
        QDLOG(LOG_INFO, "dev mirror: body spawned ", flt(d, 0), " cm ahead; pawn yaw=", flt(x.yaw, 0),
              " view yaw=", flt(viewYaw, 0), " pitch=", flt(viewPitch, 0),
              " pawn at ", flt(x.px, 0), ",", flt(x.py, 0), ",", flt(x.pz, 0));
        ActorXform bx;
        if (ReadActorXform(body.actor, &bx))
            QDLOG(LOG_INFO, "dev mirror: body actor at ", flt(bx.px, 0), ",", flt(bx.py, 0), ",",
                  flt(bx.pz, 0), " yaw=", flt(bx.yaw, 0));
        stage = 1;
        return;
    }
    if (!ObjectIsLive(body.actor)) { QDLOG(LOG_INFO, "dev mirror: body gone"); stage = -1; return; }
    if (g_localEquip.rev != lastEquipRev) {
        lastEquipRev = g_localEquip.rev;
        static char hold[220];
        static int hasHold = GetEnvironmentVariableA("QDCOOP_TEST_HOLD", hold, sizeof(hold)) ? 1 : 0;
        if (hasHold && g_localEquip.n > 0) {
            // The real item's grip transform, another mesh: exercises the receiving
            // side of a skeletal weapon on a save that cannot equip one.
            wire::EquipBody e = g_localEquip;
            char* k = strchr(hold, '#');
            int kind = k ? atoi(k + 1) : 1;
            if (k) *k = 0;
            lstrcpynA(e.items[0].path, hold, sizeof(e.items[0].path));
            e.items[0].mesh_kind = (uint8_t)kind;
            if (k) *k = '#';
            ApplyEquip(&body, e);
        } else {
            ApplyEquip(&body, g_localEquip);
        }
    }
    avatar::SetAction(&body, g_localAct.byte);
    body.airIn = g_localAct.airborne ? 1 : 0;
    // QDCOOP_TEST_FXGALLERY="path;path;...": each system attached to the body in a row
    // (80 cm apart, chest height), once, to judge candidates on one screenshot.
    static int gal = 0;
    if (gal == 0) {
        static char list[1024];
        gal = GetEnvironmentVariableA("QDCOOP_TEST_FXGALLERY", list, sizeof(list)) ? 1 : -1;
        if (gal == 1) {
            char* save = nullptr;
            int i = 0;
            for (char* t = strtok_s(list, ";", &save); t; t = strtok_s(nullptr, ";", &save), ++i) {
                float loc[3] = {-270.0f + 135.0f * i + (i >= 2 ? 135.0f : 0.0f), 0.0f, 110.0f}, rot[3] = {0, 0, 0};
                void* c = NiagaraSpawnAttached(avatar::LoadAsset(t), body.pose, loc, rot, false);
                QDLOG(LOG_INFO, "fx gallery #", dec(i), " ", t, c ? " shown" : " FAILED");
            }
        }
    }
    if (force > 0) PlaceBody(&body, baseX, baseY, baseZ, baseYaw, now);
    else PlaceBody(&body, x.px + offX, x.py + offY, x.pz, face ? viewYaw + 180.0f : viewYaw, now);
    static uint32_t lastLog = 0;
    if (now - lastLog > 3000) {
        lastLog = now;
        avatar::Xf h;
        bool got = avatar::GetBoneXf(body.pose, body.bone[avatar::B_HAND_L], &h);
        QDLOG(LOG_INFO, "dev mirror: speed=", flt(body.speed, 0), " phase=", flt(body.phase, 2),
              " rigged=", dec(body.rigged ? 1 : 0), " air=", flt(body.air, 2),
              " handL target=", flt(body.dbgHandL.x, 1), ",", flt(body.dbgHandL.y, 1), ",", flt(body.dbgHandL.z, 1),
              " actual=", (got ? "" : "?"), flt(h.tx, 1), ",", flt(h.ty, 1), ",", flt(h.tz, 1),
              " view yaw=", flt(viewYaw, 0));
    }
}

// QDCOOP_PROBE=1: one-shot discovery dump, 15 s into gameplay -- every UFunction whose
// name mentions items, equipment, attacks, jumping or magic; the pawn's properties and
// components.  QDCOOP_PE_TRACE=1: hook ProcessEvent; record every (object class,
// function) pair silently for a baseline window, then log each NEW pair once, so the
// functions an attack / jump / spell runs show up next to the key press that caused it.
// (`ContainsCI` is defined once, above, and used by both the world lanes and these
// probes.)
volatile int g_peTraceState = 0;     // 0 off, 1 baseline, 2 logging new pairs
bool g_peWide = false;               // QDCOOP_PE_TRACE=2
void* g_peSeen[16384];
volatile long g_peSeenCount = 0;

bool PeSeenInsert(void* key)
{
    uintptr_t h = ((uintptr_t)key >> 4) * 0x9E3779B97F4A7C15ull;
    for (int n = 0; n < 16384; ++n) {
        int i = (int)((h + n) & 16383);
        void* cur = g_peSeen[i];
        if (cur == key) return false;
        if (!cur) {
            if (InterlockedCompareExchangePointer(&g_peSeen[i], key, nullptr) == nullptr) {
                InterlockedIncrement(&g_peSeenCount);
                return true;
            }
            if (g_peSeen[i] == key) return false;
        }
    }
    return false;
}

void PeTraceObserve(void* self, void* func)
{
    if (g_peTraceState == 0 || !self || !func) return;
    if (g_peSeenCount > 15000) return;
    // key: function XOR class, so the same function on two classes is two entries
    // QDCOOP_PE_TRACE=2: also every call on the local pawn (and its components) in the
    // 1.5 s after one of its input events, each name once per window.
    if (g_peTraceState == 2 && g_peWide && (self == g_localPawn || ObjOuter(self) == g_localPawn)) {
        static void* winSeen[256]; static int winN = 0; static DWORD winUntil = 0;
        char fn[96];
        if (FNameToString(ObjNameRaw(func), fn, sizeof(fn)) > 0) {
            DWORD t = GetTickCount();
            if (fn[0] == 'I' && fn[1] == 'n' && fn[2] == 'p' && fn[3] == 'A' && fn[4] == 'c') { winUntil = t + 1500; winN = 0; }
            if ((int32_t)(winUntil - t) > 0 && !ContainsCI(fn, "tick")) {
                bool seen = false;
                for (int i = 0; i < winN && !seen; ++i) seen = winSeen[i] == func;
                if (!seen && winN < 256) {
                    winSeen[winN++] = func;
                    char ob[160];
                    ObjDescribe(self, ob, sizeof(ob));
                    QDLOG(LOG_INFO, "PE win: ", fn, "  on ", ob);
                }
            }
        }
    }
    void* key = (void*)((uintptr_t)func ^ ((uintptr_t)ObjClass(self) << 1));
    if (!PeSeenInsert(key)) return;
    if (g_peTraceState != 2) return;
    char fn[96], ob[160];
    if (FNameToString(ObjNameRaw(func), fn, sizeof(fn)) <= 0) return;
    ObjDescribe(self, ob, sizeof(ob));
    QDLOG(LOG_INFO, "PE new: ", fn, "  on ", ob);
}

void DevProbe(uint32_t now)
{
    static int on = DevEnvInt("QDCOOP_PROBE", 0);
    static int pe = DevEnvInt("QDCOOP_PE_TRACE", 0);
    static int stage = 0;
    static uint32_t t0 = 0;
    if ((!on && !pe) || stage < 0) return;
    if (!IsGameplayPawn(g_localPawn)) { t0 = now; return; }
    if (stage == 0 && pe && now - t0 > 5000) { g_peTraceState = 1; stage = 1; }
    if (stage <= 1 && now - t0 > 15000) {
        stage = 2;
        if (pe) {
            g_peTraceState = 2;
            g_peWide = pe >= 2;
            QDLOG(LOG_INFO, "PE trace: baseline ", dec((int)g_peSeenCount), " pairs; logging new ones");
        }
        if (!on) return;
        static const char* const kw[] = {"item", "inventor", "equip", "attack", "jump", "spell",
                                         "magic", "cast", "swing", "grant", "give", "unlock",
                                         "weapon", "montage", "charge", "dagger", "throw"};
        int n = ObjectCount(), shown = 0;
        for (int i = 0; i < n && shown < 1500; ++i) {
            void* o = ObjectAt(i);
            if (!o || !LooksLikeUObject(o) || !IsFunctionObject(o)) continue;
            char nm[96];
            if (FNameToString(ObjNameRaw(o), nm, sizeof(nm)) <= 0) continue;
            bool hit = false;
            for (int k = 0; k < (int)(sizeof(kw) / sizeof(kw[0])) && !hit; ++k) hit = ContainsCI(nm, kw[k]);
            if (!hit) continue;
            char outer[96];
            if (FNameToString(ObjNameRaw(ObjOuter(o)), outer, sizeof(outer)) <= 0) outer[0] = 0;
            QDLOG(LOG_INFO, "fnscan ", outer, ".", nm);
            ++shown;
        }
        QDLOG(LOG_INFO, "fnscan: ", dec(shown), " functions");
        LogPropertiesOf(ObjClass(g_localPawn), "pawn", 700);
        for (int i = 0; i < n; ++i) {
            void* o = ObjectAt(i);
            if (!o || ObjOuter(o) != g_localPawn) continue;
            char d[160];
            ObjDescribe(o, d, sizeof(d));
            QDLOG(LOG_INFO, "pawn child: ", d);
        }
        static const char* const sigs[] = {"AddItemToInventory", "GiveItem", "AutoEquipItem", "EquipItem",
                                           "EquipItemWeapon", "GiveAllWeapons", "GiveAllThrowingWeapons",
                                           "GetCurrentActiveMontage", "FireDagger", "SpawnDaggerVisuals"};
        for (int i = 0; i < n; ++i) {
            void* o = ObjectAt(i);
            if (!o || !LooksLikeUObject(o) || !IsFunctionObject(o)) continue;
            for (int k = 0; k < (int)(sizeof(sigs) / sizeof(sigs[0])); ++k) {
                if (!ObjNameIs(o, sigs[k])) continue;
                char lab[200], outer[96];
                if (FNameToString(ObjNameRaw(ObjOuter(o)), outer, sizeof(outer)) <= 0) outer[0] = 0;
                wsprintfA(lab, "%s.%s", outer, sigs[k]);
                LogSignature(lab, Signature(o));
            }
        }
        { char d[160]; void* ch = FindInstanceOf("FS_Cheats_C"); ObjDescribe(ch, d, sizeof(d)); QDLOG(LOG_INFO, "cheats instance: ", d); }
        void* ws = FindInstanceOf("WeaponSystem_C");
        if (ws) { LogPropertiesOf(ObjClass(ws), "WeaponSystem", 120); LogFunctionsOf(ws, "WeaponSystem", 200); }
        stage = -1;
    }
}

// QDCOOP_GIVE="<item path>;<item path>..." -- TEST INSTANCES ONLY: 20 s into gameplay,
// grant each item through the game's own cheat object (FS_Cheats_C.GiveItem) and then
// FS_PlayerChar_C.AutoEquipItem, so a weapon swap / thrown weapon can be exercised on a
// save that owns only the starting sword.  The game persists it into that instance's
// save like any pickup.
void DevGive(uint32_t now)
{
    static char list[1024];
    static int stage = GetEnvironmentVariableA("QDCOOP_GIVE", list, sizeof(list)) ? 0 : -1;
    static uint32_t t0 = 0;
    static Fn fGive = {}, fEquip = {};
    if (stage < 0) return;
    if (!IsGameplayPawn(g_localPawn)) { t0 = now; return; }
    if (now - t0 < (stage == 0 ? 20000u : 23000u)) return;
    void* cheats = FindInstanceOf("FS_Cheats_C");
    char* save = nullptr;
    char work[1024];
    lstrcpynA(work, list, sizeof(work));
    for (char* tok = strtok_s(work, ";", &save); tok; tok = strtok_s(nullptr, ";", &save)) {
        // "path@slotId": equip into that slot (EquipItem) instead of the first free one.
        int slotId = -1;
        bool select = false;
        if (char* at = strchr(tok, '@')) { *at = 0; select = (at[1] == 's'); slotId = select ? -1 : atoi(at + 1); }
        // "+path": SaveFunctions_C.AddItemToInventory -- the pickup path, for key items
        // the cheat's GiveItem does not add (measured: the Dagger Belt).
        if (tok[0] == '+') {
            if (stage != 0) continue;
            static Fn fa = {};
            void* cdo = FindCdo("SaveFunctions_C");
            void* item = avatar::LoadAsset(tok + 1);
            if (!cdo || !item || !fa.Resolve(cdo, "AddItemToInventory")) { QDLOG(LOG_WARN, "dev give: cannot add ", tok + 1); continue; }
            static bool logged = false;
            if (!logged) { logged = true; LogSignature("AddItemToInventory", fa.sig); }
            Call c(fa);
            bool haveItem = false;
            for (int i = 0; i < fa.sig.props; ++i) {
                const PropDesc& d = fa.sig.p[i];
                if (d.flags & (CPF_OutParm | CPF_ReturnParm)) continue;
                uint8_t* at = c.buf + d.offset;
                if (strcmp(d.name, "__WorldContext") == 0) *(void**)at = g_localPawn;
                else if (strcmp(d.type, "ObjectProperty") == 0 && !haveItem) { *(void**)at = item; haveItem = true; }
                else if (strcmp(d.type, "SoftObjectProperty") == 0 && !haveItem && d.size >= 40) {
                    // FSoftObjectPtr: weak ptr (left null), tag, FSoftObjectPath{FName @16,
                    // FString @24}: the path alone resolves the already loaded asset.
                    QdName nm = {};
                    haveItem = FNameFromAnsi(tok + 1, &nm);
                    if (haveItem) memcpy(at + 16, &nm, sizeof(nm));
                }
                else if (strcmp(d.type, "IntProperty") == 0) *(int32_t*)at = 1;
            }
            bool ok = c.Run(cdo);
            QDLOG(LOG_INFO, "dev give: AddItemToInventory(", tok + 1, ") ", ok ? "ran" : "FAILED");
            continue;
        }
        // "!CheatName": one of FS_Cheats_C's no-argument grants (GiveAllConsumables...).
        if (tok[0] == '!') {
            if (stage != 0) continue;
            Fn fc = {};
            if (!cheats || !fc.Resolve(cheats, tok + 1)) { QDLOG(LOG_WARN, "dev give: no cheat ", tok + 1); continue; }
            Call c(fc);
            bool ok = c.Run(cheats);
            QDLOG(LOG_INFO, "dev give: cheat ", tok + 1, " ", ok ? "ran" : "FAILED");
            continue;
        }
        if (stage == 0) {
            if (!cheats || !fGive.Resolve(cheats, "GiveItem")) { QDLOG(LOG_ERROR, "dev give: no cheat object"); stage = -1; return; }
            static wchar_t wbuf[256];
            int nch = MultiByteToWideChar(CP_UTF8, 0, tok, -1, wbuf, 256);
            Call c(fGive);
            uint8_t* fs = c.At("Path", 16);
            if (!fs || nch <= 0) { QDLOG(LOG_ERROR, "dev give: bad GiveItem signature"); stage = -1; return; }
            *(wchar_t**)fs = wbuf;
            *(int32_t*)(fs + 8) = nch;
            *(int32_t*)(fs + 12) = nch;
            bool ok = c.Run(cheats);
            QDLOG(LOG_INFO, "dev give: GiveItem(", tok, ") ", ok ? "ran" : "FAILED");
        } else {
            void* item = avatar::LoadAsset(tok);
            static Fn fEquipAt = {}, fSel = {};
            if (!item) continue;
            if (select) {
                void* ws = FindInstanceOf("WeaponSystem_C");
                if (!ws || !fSel.Resolve(ws, "SetSelectedWeaponByItem")) continue;
                Call c(fSel);
                c.Obj("Item", item);
                bool ok = c.Run(ws);
                QDLOG(LOG_INFO, "dev give: SetSelectedWeaponByItem(", tok, ") ", ok ? "ran" : "FAILED");
                continue;
            }
            if (slotId >= 0) {
                if (!fEquipAt.Resolve(g_localPawn, "EquipItem")) continue;
                Call c(fEquipAt);
                c.Obj("Item", item);
                c.I32("SlotIndex", slotId);
                bool ok = c.Run(g_localPawn);
                QDLOG(LOG_INFO, "dev give: EquipItem(", tok, ", ", dec(slotId), ") ", ok ? "ran" : "FAILED");
                continue;
            }
            if (!fEquip.Resolve(g_localPawn, "AutoEquipItem")) continue;
            Call c(fEquip);
            c.Obj("Item", item);
            bool ok = c.Run(g_localPawn);
            QDLOG(LOG_INFO, "dev give: AutoEquipItem(", tok, ") ", ok ? "ran" : "FAILED");
        }
    }
    stage = stage == 0 ? 1 : -1;
}

// QDCOOP_PROBE2=1: discovery for status effects and the melee arc.
//   +20 s  property layouts of the status components, the weapon scripts, NiagaraComponent
//   QDCOOP_TEST_STATUS=1: +30 s FS_Cheats_C.MagicFireMe, +70 s MagicPoisonMe
//   while on: status property changes; after each local attack, the Niagara components
//   that hang off the pawn (asset, parent, socket, relative transform).
void ProbeLogNiagara(const char* tag)
{
    avatar::Api& a = avatar::A();
    if (a.attachParent < 0) return;
    int n = ObjectCount(), shown = 0;
    static int relLoc = -2, relRot = -2, assetOff = -2;
    for (int i = 0; i < n && shown < 30; ++i) {
        void* o = ObjectAt(i);
        if (!o || !LooksLikeUObject(o) || !ObjNameIs(ObjClass(o), "NiagaraComponent")) continue;
        bool ours = ObjOuter(o) == g_localPawn || (ObjOuter(o) && ObjOuter(ObjOuter(o)) == g_localPawn);
        void* p = o;
        for (int d = 0; d < 8 && p && !ours; ++d) {
            p = *(void**)((uint8_t*)p + a.attachParent);
            if (p && (p == g_localPawn || ObjOuter(p) == g_localPawn)) ours = true;
        }
        if (!ours) continue;
        if (relLoc == -2) {
            relLoc = FindPropertyOffsetIn(a.clsScene, "RelativeLocation");
            relRot = FindPropertyOffsetIn(a.clsScene, "RelativeRotation");
            assetOff = FindPropertyOffsetIn(ObjClass(o), "Asset");
        }
        char d1[160], d2[160], d3[160], asset[200] = "-", sock[64] = "-";
        ObjDescribe(o, d1, sizeof(d1));
        ObjDescribe(ObjOuter(o), d2, sizeof(d2));
        void* ap = *(void**)((uint8_t*)o + a.attachParent);
        ObjDescribe(ap, d3, sizeof(d3));
        if (assetOff >= 0) avatar::AssetPath(*(void**)((uint8_t*)o + assetOff), asset, sizeof(asset));
        if (a.attachSocket >= 0) FNameToString(*(QdName*)((uint8_t*)o + a.attachSocket), sock, sizeof(sock));
        const float* l = (const float*)((uint8_t*)o + relLoc);
        const float* r = (const float*)((uint8_t*)o + relRot);
        uint8_t active = *((uint8_t*)o + 138);
        QDLOG(LOG_INFO, "probe2 ", tag, " niagara ", d1, " outer=", d2, " asset=", asset, " parent=", d3,
              " sock=", sock, " active=", hex32(active), " rel=", flt(l[0], 1), ",", flt(l[1], 1), ",", flt(l[2], 1),
              " rot=", flt(r[0], 1), ",", flt(r[1], 1), ",", flt(r[2], 1));
        ++shown;
    }
    QDLOG(LOG_INFO, "probe2 ", tag, " niagara components on the pawn: ", dec(shown));
}

void DevCheatAmount(const char* fn, float amount)
{
    static Fn f[2] = {};
    void* ch = FindInstanceOf("FS_Cheats_C");
    Fn& ff = f[fn[5] == 'F' ? 0 : 1];
    if (!ch || !ff.Resolve(ch, fn)) { QDLOG(LOG_WARN, "dev cheat: ", fn, " unavailable"); return; }
    Call c(ff);
    c.F32("Amount", amount);
    bool ok = c.Run(ch);
    QDLOG(LOG_INFO, "dev cheat: ", fn, "(", flt(amount, 0), ") ", ok ? "ran" : "FAILED");
}

// Every NiagaraComponent in the world whose asset looks like a melee arc / swing FX.
void ProbeLogArcs(const char* tag)
{
    avatar::Api& a = avatar::A();
    static int assetOff = -2, relLoc = -2, relRot = -2;
    int n = ObjectCount(), shown = 0;
    for (int i = 0; i < n && shown < 12; ++i) {
        void* o = ObjectAt(i);
        if (!o || !LooksLikeUObject(o) || !ObjNameIs(ObjClass(o), "NiagaraComponent")) continue;
        if (assetOff == -2) {
            assetOff = FindPropertyOffsetIn(ObjClass(o), "Asset");
            relLoc = FindPropertyOffsetIn(a.clsScene, "RelativeLocation");
            relRot = FindPropertyOffsetIn(a.clsScene, "RelativeRotation");
        }
        char asset[200] = "-";
        if (assetOff >= 0) avatar::AssetPath(*(void**)((uint8_t*)o + assetOff), asset, sizeof(asset));
        { char oc[64] = ""; FNameToString(ObjNameRaw(ObjClass(ObjOuter(o))), oc, sizeof(oc));
          if (ContainsCI(oc, "class")) continue; }        // a template on a class, not a live FX
        static Fn fLoc = {};
        float wl[3] = {0, 0, 0};
        if (fLoc.Resolve(o, "K2_GetComponentLocation")) {
            Call lc(fLoc);
            if (lc.Run(o) && lc.Ret()) memcpy(wl, lc.Ret(), 12);
        }
        ActorXform pp = {};
        ReadActorXform(g_localPawn, &pp);
        if (fabsf(wl[0] - pp.px) + fabsf(wl[1] - pp.py) + fabsf(wl[2] - pp.pz) > 600.0f) continue;
        QDLOG(LOG_INFO, "probe2 ", tag, " fx world ", flt(wl[0], 0), ",", flt(wl[1], 0), ",", flt(wl[2], 0));
        char d1[160], d2[160], d3[160] = "-", d4[160] = "-", sock[64] = "-";
        ObjDescribe(o, d1, sizeof(d1));
        ObjDescribe(ObjOuter(o), d2, sizeof(d2));
        void* ap = a.attachParent >= 0 ? *(void**)((uint8_t*)o + a.attachParent) : nullptr;
        if (ap) { ObjDescribe(ap, d3, sizeof(d3)); ObjDescribe(ObjOuter(ap), d4, sizeof(d4)); }
        if (a.attachSocket >= 0) FNameToString(*(QdName*)((uint8_t*)o + a.attachSocket), sock, sizeof(sock));
        const float* l = (const float*)((uint8_t*)o + relLoc);
        const float* r = (const float*)((uint8_t*)o + relRot);
        ActorXform px = {};
        ReadActorXform(g_localPawn, &px);
        QDLOG(LOG_INFO, "probe2 ", tag, " fx ", d1, " outer=", d2, " asset=", asset, " parent=", d3, " of ", d4,
              " sock=", sock, " active=", hex32(*((uint8_t*)o + 138)), " rel=", flt(l[0], 1), ",", flt(l[1], 1), ",",
              flt(l[2], 1), " rot=", flt(r[0], 1), ",", flt(r[1], 1), ",", flt(r[2], 1),
              " pawn=", flt(px.px, 0), ",", flt(px.py, 0), ",", flt(px.pz, 0), " yaw=", flt(px.yaw, 0));
        ++shown;
    }
    QDLOG(LOG_INFO, "probe2 ", tag, " fx shown: ", dec(shown));
}

void* g_devPokeComp[4]; int g_devPokeOff[4]; uint32_t g_devPokeUntil[4]; int g_devPokeN = 0;
void DevProbe2(uint32_t now)
{
    for (int i = 0; i < 4; ++i)
        if (g_devPokeComp[i] && (int32_t)(now - g_devPokeUntil[i]) > 0) {
            if (ObjectIsLive(g_devPokeComp[i])) *((uint8_t*)g_devPokeComp[i] + g_devPokeOff[i]) = 0;
            g_devPokeComp[i] = nullptr;
        }
    static int on = DevEnvInt("QDCOOP_PROBE2", 0);
    static int status = DevEnvInt("QDCOOP_TEST_STATUS", 0);
    static int stage = 0;
    static uint32_t t0 = 0, lastWatch = 0, atkAt = 0;
    static uint8_t lastAct = 0;
    if (!on) return;
    if (!IsGameplayPawn(g_localPawn)) { t0 = now; return; }
    uint32_t t = now - t0;
    if (stage == 0 && t > 20000) {
        stage = 1;
        static const char* const cls[] = {"StatusEffectComponent_C", "MagicFireEffectLogic_C",
                                          "MagicPoisonDebuffLogic_C", "StatusEffectVisBase_C",
                                          "MagicFireStatusEffectVis_C", "WeaponScriptBase_C"};
        for (int k = 0; k < 6; ++k) {
            void* c = FindClassByName(cls[k]);
            if (!c) { QDLOG(LOG_INFO, "probe2 class ", cls[k], " not loaded"); continue; }
            LayoutResult r;
            if (WalkFieldsRaw(c, Lay(), false, &r))
                for (int i = 0; i < r.props; ++i)
                    QDLOG(LOG_INFO, "probe2 prop ", cls[k], ".", r.p[i].name, " : ", r.p[i].type,
                          " @", dec(r.p[i].offset), " size=", dec(r.p[i].size));
        }
        void* nc = FindClassByName("NiagaraComponent");
        if (nc) LogPropertiesOf(nc, "NiagaraComponent", 60);
        void* fs = FindClassByName("FS_MeleeFXOverride");
        if (fs) LogPropertiesOf(fs, "FS_MeleeFXOverride", 20);
        void* ch = FindInstanceOf("FS_Cheats_C");
        if (ch) {
            LogSignature("MagicFireMe", Signature(FindFunction(ch, "MagicFireMe")));
            LogSignature("MagicPoisonMe", Signature(FindFunction(ch, "MagicPoisonMe")));
        }
        ProbeLogNiagara("idle");
        for (const void* s = ObjClass(g_localPawn); s; s = SuperOf(s)) {
            LayoutResult r;
            if (!WalkFieldsRaw(s, Lay(), false, &r)) break;
            for (int i = 0; i < r.props; ++i) {
                const char* nm = r.p[i].name;
                if (!ContainsCI(nm, "slot") && !ContainsCI(nm, "dagger") && !ContainsCI(nm, "consumable") &&
                    !ContainsCI(nm, "equip"))
                    continue;
                uint32_t v = 0;
                memcpy(&v, (uint8_t*)g_localPawn + r.p[i].offset, 4);
                QDLOG(LOG_INFO, "probe2 pawn ", nm, " : ", r.p[i].type, " @", dec(r.p[i].offset),
                      " size=", dec(r.p[i].size), " first4=", hex32(v));
            }
        }
        static const char* const sg[] = {"EquipItem", "EquipSpecifiedWeapon", "SetSelectedWeaponByItem",
                                         "AddItemToQuickSlots", "UseItem", "Equip To Slot"};
        for (int k = 0; k < 6; ++k) {
            void* f = FindFunction(g_localPawn, sg[k]);
            void* ws = FindInstanceOf("WeaponSystem_C");
            if (!f && ws) f = FindFunction(ws, sg[k]);
            if (f) LogSignature(sg[k], Signature(f));
        }
    }
    static char wantWeapon[64];
    static int ww = GetEnvironmentVariableA("QDCOOP_TEST_WEAPON", wantWeapon, sizeof(wantWeapon)) ? 1 : 0;
    if (ww == 1 && t > 26000) {
        ww = 2;
        void* ws = FindInstanceOf("WeaponSystem_C");
        int ao = ws ? FindPropertyOffsetIn(ObjClass(ws), "AllWeapons") : -1;
        QDLOG(LOG_INFO, "dev weapon: want ", wantWeapon, " ws=", hex((uintptr_t)ws), " AllWeapons@", dec(ao));
        if (ao >= 0) {
            void** data = *(void***)((uint8_t*)ws + ao);
            int num = *(int*)((uint8_t*)ws + ao + 8);
            for (int i = 0; i < num && data; ++i) {
                char cn[96] = "";
                FNameToString(ObjNameRaw(ObjClass(data[i])), cn, sizeof(cn));
                QDLOG(LOG_INFO, "probe2 AllWeapons[", dec(i), "] ", cn);
                if (!ContainsCI(cn, wantWeapon)) continue;
                static Fn fe = {};
                if (!fe.Resolve(ws, "EquipSpecifiedWeapon")) break;
                Call c(fe);
                c.I32("Index", i);
                QDLOG(LOG_INFO, "dev weapon: EquipSpecifiedWeapon(", dec(i), ") ", c.Run(ws) ? "ran" : "FAILED");
                break;
            }
        }
    }
    if (status && stage == 1 && t > 30000) {
        stage = 2;
        DevCheatAmount("MagicFireMe", (float)DevEnvInt("QDCOOP_TEST_FIREAMT", 60));
    }
    if (status && stage == 2 && t > 50000) {
        stage = 3;
        static char list[256];
        if (GetEnvironmentVariableA("QDCOOP_TEST_APPLY", list, sizeof(list))) {
            char* save = nullptr;
            for (char* tok = strtok_s(list, ";", &save); tok; tok = strtok_s(nullptr, ";", &save)) {
                char* colon = strchr(tok, ':');
                float dur = colon ? (float)atof(colon + 1) : 8.0f;
                if (colon) *colon = 0;
                void* comp = nullptr;
                int n = ObjectCount();
                for (int i = 0; i < n && !comp; ++i) {
                    void* o = ObjectAt(i);
                    if (o && ObjOuter(o) == g_localPawn && ObjNameIs(o, tok)) comp = o;
                }
                void* f = comp ? FindFunction(comp, "Apply") : nullptr;
                LayoutResult sg = Signature(f);
                LogSignature(tok, sg);
                Fn fa = {};
                bool ran = false;
                if (comp && fa.Resolve(comp, "Apply")) {
                    LogSignature("StatusEffectComponent.Apply", fa.sig);
                    Call c(fa);
                    if (fa.Off("Duration") >= 0) c.F32("Duration", dur);
                    ran = c.Run(comp);
                    QDLOG(LOG_INFO, "dev apply: ", tok, ".Apply ", ran ? "ran" : "FAILED");
                }
                if (!ran) {
                    // No callable Apply: flip the flag the status reader looks at, for
                    // `dur` seconds (exercises detection -> STATUS -> the body's FX).
                    if (comp) LogFunctionsOf(comp, tok, 40);
                    int ao = comp ? FindPropertyOffsetIn(ObjClass(comp), "Applied") : -1;
                    if (ao >= 0) {
                        *((uint8_t*)comp + ao) = 1;
                        g_devPokeComp[g_devPokeN & 3] = comp; g_devPokeOff[g_devPokeN & 3] = ao;
                        g_devPokeUntil[g_devPokeN & 3] = now + (uint32_t)(dur * 1000.0f); ++g_devPokeN;
                        QDLOG(LOG_INFO, "dev apply: ", tok, ".Applied = 1 for ", flt(dur, 1), " s (test flag)");
                    }
                    continue;
                }
            }
            return;
        }
        DevCheatAmount("MagicPoisonMe", (float)DevEnvInt("QDCOOP_TEST_POISONAMT", 25));
    }
    // Watch: status-ish properties on the pawn's components, logged on change.
    if (stage >= 1 && now - lastWatch > 500) {
        lastWatch = now;
        static const char* const props[] = {"Applied", "Duration", "Permanent", "Active",
                                            "CurrentMagicFireDamagePool", "CurrentBuildup", "IsActive"};
        static uint32_t seen[64][7];
        int n = ObjectCount(), idx = 0;
        for (int i = 0; i < n && idx < 64; ++i) {
            void* o = ObjectAt(i);
            if (!o || ObjOuter(o) != g_localPawn) continue;
            char cn[96];
            if (FNameToString(ObjNameRaw(ObjClass(o)), cn, sizeof(cn)) <= 0) continue;
            if (!ContainsCI(cn, "status") && !ContainsCI(cn, "magicfire") && !ContainsCI(cn, "magicpoison")) continue;
            for (int k = 0; k < 7; ++k) {
                int sz = 0;
                int off = FindPropertyOffsetIn(ObjClass(o), props[k], &sz);
                if (off < 0 || off < 200) continue;
                uint32_t v = 0;
                memcpy(&v, (uint8_t*)o + off, sz >= 4 ? 4 : (sz > 0 ? sz : 1));
                if (sz == 1) v &= 0xFF;
                if (v != seen[idx][k]) {
                    seen[idx][k] = v;
                    char d[160];
                    ObjDescribe(o, d, sizeof(d));
                    float fv;
                    memcpy(&fv, &v, 4);
                    QDLOG(LOG_INFO, "probe2 status ", d, ".", props[k], " = ", hex32(v),
                          " size=", dec(sz), " f=", flt(fv, 2));
                }
            }
            ++idx;
        }
    }
    if (g_localAct.byte != lastAct) {
        lastAct = g_localAct.byte;
        if ((lastAct & 0x1F) != 0) atkAt = now;
    }
    if (atkAt && now - atkAt > 250) { ProbeLogArcs("attack+250"); atkAt = 0; }
    static uint32_t lastFx = 0;
    if (status && stage >= 2 && now - lastFx > 5000) { lastFx = now; ProbeLogArcs("status"); }
}

bool HK_Tick(void* engineLoop)
{
    // Forward first and keep the answer: the pump below must never change the engine's
    // control flow, so the return value is the original's, unconditionally.
    bool keepRunning = g_oTick ? g_oTick(engineLoop) : true;
    if (!g_engineReady) return keepRunning;

    uint32_t now = Net::Now();
    ++g_frame;

    // Substrate first: nothing below is safe until the engine globals are proven.
    if (!g_substrateReady && !ValidateSubstrateOnGameThread()) return keepRunning;

    // Inbound first: applying before sending means a round trip completes in one
    // frame rather than two.
    const bool tracing = (g_frame <= 6);
    if (tracing) QDLOG(LOG_DEBUG, "  frame ", dec(g_frame), " begin");

    InboundPacket p;
    int drained = 0;
    while (drained < 16 && g_net.Pop(&p)) { DispatchInbound(p, now); ++drained; }

    g_net.PumpReliable();
    if (tracing) QDLOG(LOG_DEBUG, "  frame ", dec(g_frame), " net done");
    // Before the mirror and the push, so the first frame after a save loads already
    // has a container to read.
    TryInitSaveLayer(now);
    if (tracing) QDLOG(LOG_DEBUG, "  frame ", dec(g_frame), " save layer done");
    MirrorPump(now);
    if (tracing) QDLOG(LOG_DEBUG, "  frame ", dec(g_frame), " mirror done");
    LocalActionPump(now);
    SendLocalPlayer(now);
    if (tracing) QDLOG(LOG_DEBUG, "  frame ", dec(g_frame), " player sent");

    if (g_net.IsHost()) {
        HostPushFlags(now);
        // 6d: the host's inventory is the shared truth.  Off unless InventorySync=1.
        HostPushInventory(now);
    } else {
        PeerFlagIntentPump(now);
        // 6d: apply at most two grants per frame from the last body.
        PeerGrantPump(now);
        // 6d: and send the peer's own pickups.  AFTER PeerGrantPump on purpose: the
        // local walk must see the grants that landed this frame, and HostGranted has
        // already raised their host-derived share, so they cannot be read as pickups.
        PeerInventoryIntentPump(now);
    }
    DevAutoLoad(now);
    LocalEquipPump(now);
    DevAvatarTest(now);
    DevProbe(now);
    DevProbe2(now);
    DevGive(now);
    PuppetPump(now);
    FxPump(now);
    ArcObservePump(now);
    StatusPump(now);
    DevTestFire(now);
    DevTeleport(now);
    DevTeleportFile(now);
    DevScanNearest(now);
    DevInteract(now);
    DevGod(now);
    DevKillNearest(now);
    DevCheat(now);
    TravelPump(now);
    StreamPump(now);
    EnemyPump(now);
    ActorStatePump(now);
    DialoguePump(now);
    CutscenePump(now);
    MoverPump(now);
    LootPump(now);
    SuspendPump(now);

    if (tracing) QDLOG(LOG_DEBUG, "  frame ", dec(g_frame), " end");
    g_lastTickMs = now;
    return keepRunning;
}

bool HK_SetGamePaused(void* worldContext, bool paused)
{
    // D1 is closed: the menus DO pause (Status/Inventory, Pause, Photo, Checkpoint all
    // freeze UWorld::TimeSeconds), while dialogue does not.  A paused host stops
    // ticking, which to the peer looks exactly like a hang.  So in a live session a
    // pause is declined -- and logged as BLOCK so a suppressed pause is never silent.
    if (Cfg().suppress_pause && paused && g_net.Connected()) {
        static uint32_t lastLog = 0;
        uint32_t now = Net::Now();
        if (now - lastLog > 1000) {
            lastLog = now;
            QDLOG(LOG_WARN, "BLOCK SetGamePaused(1) during a live session "
                            "(the peer would see a hang)");
        }
        return false;
    }
    return g_oSetGamePaused ? g_oSetGamePaused(worldContext, paused) : false;
}

void HK_StoreEnemySaveState(void* self, const QdGuid* guid, float respawn, float hp)
{
    if (g_oStoreEnemy) g_oStoreEnemy(self, guid, respawn, hp);

    // The record is {bValid, RespawnTime, RemainingHP} keyed by the spawner's authored
    // GUID (D5 closed live: a kill is hp 0).
    if (!Cfg().enemy_sync || !guid || !g_net.Connected()) return;
    uint32_t g[4] = {guid->a, guid->b, guid->c, guid->d};
    // A NEGATIVE hp is the game saying "this spawner has no record" -- measured at level
    // load as -1 on every spawner with no live enemy, which is most of them.  Treating
    // that as a death recorded 123 kills in one frame and forwarded them as records, so
    // the host would have been told that the whole level's enemies were dead.
    if (hp < 0.0f) return;
    // Whichever machine RAN the enemy re-broadcasts the death, as dead rows
    // (qdenemy.h KillLog), so the machine that did not kill it learns it.  A death
    // being applied FROM the other machine's row is not re-broadcast: that would echo
    // the kill straight back.
    if (hp <= 0.0f && !g_inRemoteDeath && g_own.Running(g)) {
        g_killLog.Add(g);
        QDLOG(LOG_INFO, "enemy death recorded guid=", hex32(g[0]), " respawn=", flt(respawn, 1),
              (g_inOwnRecord ? " (record from the other player)" : ""));
    }
    if (g_net.IsHost()) return;
    // Peer: forward a persistence record only for an enemy THIS machine ran.  A frozen
    // copy or an owner-ordered kill writes the record the host already has.
    if (!g_own.Running(g)) return;
    uint8_t body[64];
    wire::Writer w(body, sizeof(body));
    wire::WriteIntentEnemyRecord(w, 0, g, (int32_t)(respawn * 100.0f), (int32_t)(hp * 100.0f));
    if (w.ok) g_net.SendReliable(wire::K_INTENT, body, w.len);
    QDLOG(LOG_INFO, "enemy record sent to host guid=", hex32(g[0]), " hp=", flt(hp, 1));
}

// A rest clears every enemy record in this machine's save (ClearAllEnemySaveStates,
// called from Checkpoint_C), so the kill log must forget them too, or this machine
// would keep telling the other that an enemy the rest brought back is still dead.  Seen
// as the local pawn's PlayerRested event.  Each machine's log only holds enemies it
// ran, so a local rest forgets exactly the deaths this machine is broadcasting.
void RestObserve(void* self, void* func)
{
    static void* fnRest = nullptr;
    static void* other[256];
    static int nOther = 0;
    if (!self || self != g_localPawn || !func || !Cfg().enemy_sync) return;
    if (func != fnRest) {
        if (fnRest) return;
        for (int i = 0; i < nOther; ++i) if (other[i] == func) return;
        char fn[64];
        if (FNameToString(ObjNameRaw(func), fn, sizeof(fn)) <= 0) return;
        if (strcmp(fn, "PlayerRested") != 0) { if (nOther < 256) other[nOther++] = func; return; }
        fnRest = func;
    }
    if (g_killLog.Count()) {
        QDLOG(LOG_INFO, "enemy sync: rest -- ", dec(g_killLog.Count()), " death(s) forgotten");
        g_killLog.Clear();
    }
}

// Using an item plays no first-person montage (measured: Berserker's Brew, herbs).
// What the pawn runs is InpActEvt_UseItem and then, when something was consumed,
// InventoryChanged; a teleport fossil or the grimoire opens a menu and changes
// nothing.  So: the input, then an inventory change within 400 ms = "used an item".
void UseObserve(void* self, void* func)
{
    static void* fnUse = nullptr;
    static void* fnInv = nullptr;
    static void* other[256];
    static int nOther = 0;
    static DWORD armedAt = 0;
    if (!self || self != g_localPawn || !func) return;
    if (func != fnUse && func != fnInv) {
        for (int i = 0; i < nOther; ++i) if (other[i] == func) return;
        char fn[96];
        if (FNameToString(ObjNameRaw(func), fn, sizeof(fn)) <= 0) return;
        if (strncmp(fn, "InpActEvt_UseItem", 17) == 0) fnUse = func;
        else if (strcmp(fn, "InventoryChanged") == 0) fnInv = func;
        else { if (nOther < 256) other[nOther++] = func; return; }
    }
    DWORD t = GetTickCount();
    if (func == fnUse) { armedAt = t; return; }
    if (armedAt && t - armedAt < 400) { g_localUsed = true; armedAt = 0; }
}

void HK_ProcessEvent(void* self, void* func, void* parms)
{
    FxObserve(self, func);
    UseObserve(self, func);
    RestObserve(self, func);
    PeTraceObserve(self, func);
    // The proxy body reports rather than absorbs: false means the game's own hit has been
    // described to the machine that owns the player, so the local TryApplyDamage must not
    // also run (that would apply the same damage twice, on the wrong machine).
    if (!PlayerBodyObserve(self, func, parms)) return;
    if (g_oProcessEvent) g_oProcessEvent(self, func, parms);
}

// ---------------------------------------------------------------------------
// Installation
// ---------------------------------------------------------------------------
bool InstallHook(const QdTarget& t, void* detour, void** trampoline)
{
    MH_STATUS s = MH_CreateHook(Ptr<void*>(t.rva), detour, trampoline);
    if (s != MH_OK) {
        QDLOG(LOG_ERROR, "MH_CreateHook(", t.name, ") failed: ", dec((int)s));
        return false;
    }
    s = MH_EnableHook(Ptr<void*>(t.rva));
    if (s != MH_OK) {
        QDLOG(LOG_ERROR, "MH_EnableHook(", t.name, ") failed: ", dec((int)s));
        return false;
    }
    QDLOG(LOG_INFO, "hooked ", t.name, " @ ", hex32(t.rva));
    return true;
}

// ---------------------------------------------------------------------------
// Bootstrap -- runs on our own thread, after the loader, before the engine exists
// ---------------------------------------------------------------------------
DWORD WINAPI BootstrapThread(LPVOID)
{
    ConfigLoad();

    char dir[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, dir, MAX_PATH);
    int cut = (int)n;
    while (cut > 0 && dir[cut - 1] != '\\' && dir[cut - 1] != '/') --cut;
    dir[cut > 0 ? cut - 1 : 0] = 0;
    // Per-instance log name from -saveddirsuffix=<x>, which the test rig gives every
    // instance; a normal launch has none and logs to qdcoop.log as before.
    char tag[32] = {0};
    {
        const char* cl = GetCommandLineA();
        const char* key = "saveddirsuffix=";
        for (const char* p = cl; p && *p; ++p) {
            const char* a = p;
            const char* k = key;
            while (*k && (*a | 0x20) == (*k | 0x20)) { ++a; ++k; }
            if (*k != 0) continue;
            int t = 0;
            while (*a && *a != ' ' && *a != '"' && t < (int)sizeof(tag) - 1) {
                char c = *a++;
                char lc = (char)(c | 0x20);
                if ((c >= '0' && c <= '9') || (lc >= 'a' && lc <= 'z') || c == '_' || c == '-')
                    tag[t++] = c;
            }
            tag[t] = 0;
            break;
        }
    }
    LogOpen(dir, tag);
    LogSetLevel(Cfg().log_level);

    QDLOG(LOG_INFO, "qdcMp bootstrap: pid=", dec(GetCurrentProcessId()),
          " base=", hex(ImageBase()), " image=", QD_IMAGE_SHA256);
    LogConfig();

    // ---- layer 0: is this the build we verified? --------------------------
    // Pure memory reads of the mapped image. Safe from this thread.
    TargetReport tr = VerifyTargets();
    QDLOG(LOG_INFO, "targets: ", dec(tr.ok), " byte-verified, ",
          dec(tr.relocated), " relocated-pointer, ",
          dec(tr.runtime_only), " runtime-validated, ", dec(tr.mismatches), " mismatched");
    if (tr.mismatches > 0) {
        // Refuse rather than half-work: a wrong RVA is an access violation at best and
        // a silent corruption at worst.
        QDLOG(LOG_ERROR, "REFUSING TO RUN: build mismatch in ", tr.first_mismatch_name,
              " @ ", hex32(tr.first_mismatch_rva), " byte ", dec(tr.first_mismatch_byte),
              " (expected image ", QD_IMAGE_SHA256, ")");
        QDLOG(LOG_ERROR, "the game has probably been updated; regenerate targets.h with "
                         "tools/gen_targets.py and re-verify");
        return 0;
    }

    // ---- hooks -----------------------------------------------------------
    // MinHook patches code and touches no UObject, so this is safe here.
    if (MH_Initialize() != MH_OK) {
        QDLOG(LOG_ERROR, "MH_Initialize failed");
        return 0;
    }
    {
        // Find the exact entries by name so a reordering of targets.h cannot silently
        // hook the wrong address.
        const QdTarget* tTick = nullptr;
        const QdTarget* tPause = nullptr;
        const QdTarget* tStore = nullptr;
        const QdTarget* tPE = nullptr;
        const QdTarget* tVis = nullptr;
        const QdTarget* tLoad = nullptr;
        for (unsigned k = 0; k < kQdTargetCount; ++k) {
            if (NameEquals(kQdTargets[k].name, "ULevelStreaming::SetShouldBeVisible")) tVis = &kQdTargets[k];
            if (NameEquals(kQdTargets[k].name, "ULevelStreaming::SetShouldBeLoaded")) tLoad = &kQdTargets[k];
            if (NameEquals(kQdTargets[k].name, "FEngineLoop::Tick")) tTick = &kQdTargets[k];
            if (NameEquals(kQdTargets[k].name, "UGameplayStatics::SetGamePaused")) tPause = &kQdTargets[k];
            if (NameEquals(kQdTargets[k].name, "StoreEnemySaveState")) tStore = &kQdTargets[k];
            if (NameEquals(kQdTargets[k].name, "UObject::ProcessEvent")) tPE = &kQdTargets[k];
        }
        // A missing target must be LOUD. The first version looked these up by
        // exact name and silently installed nothing when the names were padded, so
        // "no hooks" was indistinguishable from "hooks installed and idle".
        int installed = 0;
        if (tTick) { if (InstallHook(*tTick, (void*)&HK_Tick, (void**)&g_oTick)) ++installed; }
        else QDLOG(LOG_ERROR, "target not found in the table: FEngineLoop::Tick");
        if (tPause) { if (InstallHook(*tPause, (void*)&HK_SetGamePaused, (void**)&g_oSetGamePaused)) ++installed; }
        else QDLOG(LOG_WARN, "target not found in the table: SetGamePaused");
        if (tStore) { if (InstallHook(*tStore, (void*)&HK_StoreEnemySaveState, (void**)&g_oStoreEnemy)) ++installed; }
        else QDLOG(LOG_WARN, "target not found in the table: StoreEnemySaveState");
        QDLOG(LOG_INFO, "hooks installed: ", dec(installed), " of 3 (ProcessEvent is opt-in)");
        // The streaming veto is an engine-wide setter; it is hooked only when the union
        // is switched on, so a session without it runs the engine untouched.
        if (Cfg().stream_union) {
            if (tVis) InstallHook(*tVis, (void*)&HK_SetShouldBeVisible, (void**)&g_oSetVis);
            else QDLOG(LOG_ERROR, "target not found in the table: ULevelStreaming::SetShouldBeVisible");
            if (tLoad) InstallHook(*tLoad, (void*)&HK_SetShouldBeLoaded, (void**)&g_oSetLoaded);
            else QDLOG(LOG_ERROR, "target not found in the table: ULevelStreaming::SetShouldBeLoaded");
        }
        // ProcessEvent is the widest hook in the process and costs the most; it stays
        // off unless someone is tracing a specific Blueprint surface.
        // Effect sync needs it to see thrown weapons begin play (one compare per call).
        // Enemy sync needs it to see the host rest (RestObserve).
        if (tPE && (Cfg().puppet_sync || Cfg().enemy_sync || Cfg().player_body ||
                    Cfg().log_level >= LOG_TRACE || DevEnvInt("QDCOOP_PE_TRACE", 0))) {
            InstallHook(*tPE, (void*)&HK_ProcessEvent, (void**)&g_oProcessEvent);
        }
    }

    // ---- net -------------------------------------------------------------
    if (Cfg().enabled) {
        if (g_net.Start(Cfg().relay_host, Cfg().relay_port, "")) {
            QDLOG(LOG_INFO, "relay transport up; waiting for a session");
        } else {
            QDLOG(LOG_WARN, "transport failed to start; running local-only (mirror mode)");
        }
    }

    // ---- hand over to the game thread ------------------------------------
    //
    // EVERYTHING THAT TOUCHES A UOBJECT HAPPENS IN HK_Tick, NOT HERE.
    //
    // This thread used to run the substrate check, the save-layer init and the API
    // probe, and it BLOCKED inside SaveInit: the run that reached that point stopped
    // logging while the game kept running. That is the rule this project wrote down and
    // then broke -- "Only UObject access goes on the game thread". Engine code takes
    // locks and expects to run where the world is being ticked.
    g_engineReady = true;
    QDLOG(LOG_INFO, "frame pump armed; substrate check and API probe run on the game thread");
    return 0;
}

} // namespace

// ---------------------------------------------------------------------------
// DllMain
// ---------------------------------------------------------------------------
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        // Do as little as possible here: the loader lock is held, and the CRT of the
        // game has not initialised.  Everything real happens on our own thread.
        DisableThreadLibraryCalls(module);
        {
            // 8 MB, not the 1 MB default: this thread calls into engine code that was
            // written for the game's own threads. A shallow call would survive a small
            // stack, but a fault here presents as "the mod silently stopped" and costs a
            // run to diagnose.
            HANDLE h = CreateThread(nullptr, 8u << 20, &BootstrapThread, nullptr, 0, nullptr);
            if (h) CloseHandle(h);
        }
        break;
    case DLL_PROCESS_DETACH:
        // The process is going away; the OS reclaims the socket and the hooks.  We do
        // not unhook on detach because the ordering against a torn-down engine is
        // undefined, and a trampoline call into a half-freed image is worse than a
        // leak that the process exit collects.
        break;
    default:
        break;
    }
    return TRUE;
}






