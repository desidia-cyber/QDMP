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
//   6  WORLD: level-streaming union, dialogue lock, enemy persistence
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

#include "MinHook.h"

#include "targets.h"
#include "qdlog.h"
#include "qdconfig.h"
#include "qdsubstrate.h"
#include "qdreflect.h"
#include "qdsave.h"
#include "qdwire.h"
#include "qdnet.h"

// The five dependency-free state machines.  Header-only on purpose so the same
// logic is compiled into a test executable (native/test/run_tests.bat) with no
// Windows and no game: the parts that took the previous project longest to get
// right are the parts that must be testable off-target.
#include "qdmotion.h"
#include "qdpuppet.h"
#include "qdstream.h"
#include "qddialogue.h"
#include "qdflags.h"

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
dialogue::DialogueLock g_dialogue;
flags::FlagBank   g_flags;

// Progression mirror bookkeeping.
uint32_t          g_lastMirrorMs = 0;
int32_t           g_lastFlagCount = -1;
uint32_t          g_flagsRevision = 1;
bool              g_flagsDirty = true;
uint32_t          g_lastFlagsPushMs = 0;
uint32_t          g_lastPlayerPushMs = 0;
uint32_t          g_lastEnemyPushMs = 0;
int               g_enemyCursor = 0;

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

// Captured enemy persistence writes (host authority source).
struct EnemyKill {
    QdGuid   guid;
    float    respawn;
    float    hp;
    uint32_t seen_ms;
    bool     used;
};
EnemyKill         g_kills[32];

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
          " actors=", dec(c.actor_state_sync),
          " enemy=", dec(c.enemy_sync),
          " puppet=", dec(c.puppet_sync),
          " dialogue=", dec(c.dialogue_sync),
          " stream=", dec(c.stream_union),
          " pause=", dec(c.suppress_pause));
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

    // 2. property layout, validated against the measured GetNamedSaveData frame
    if (!s.layout.valid) {
        QDLOG(LOG_ERROR, "PROBE param-layout: FAIL (", s.layout.note, ")");
    } else {
        QDLOG(LOG_INFO, "PROBE param-layout: PASS  PropertyLink=", hex(s.layout.cand.prop_link),
              " Next=", hex(s.layout.cand.prop_next),
              " ElemSize=", hex(s.layout.cand.elem_size),
              " OffsetInternal=", hex(s.layout.cand.offset_internal),
              " Flags=", hex(s.layout.cand.flags),
              " parmsSize=", dec(s.layout.parms_size));
    }

    // 3. NamedSaveData container offset + map stride
    if (s.nsdOffset < 0) {
        QDLOG(LOG_ERROR, "PROBE NamedSaveData-offset: FAIL (property not found)");
    } else {
        QDLOG(LOG_INFO, "PROBE NamedSaveData-offset: PASS (", dec(s.nsdOffset), ")");
    }
    if (s.enumerateOk) {
        QDLOG(LOG_INFO, "PROBE NamedSaveData-map: PASS  stride=", dec(s.mapStride),
              " valueOffset=", dec(s.mapValueOff), " (cross-checked against the getter)");
    } else {
        QDLOG(LOG_WARN, "PROBE NamedSaveData-map: FAIL  stride not confirmed; "
                        "enumeration off, single-key reads unaffected");
    }

    // 4. the reader, end to end
    {
        bool set = false;
        int32_t v = 0;
        // "CheckpointUnlockCount" exists in every save the analysis read, including a
        // fresh 11-minute one, so its presence is a real check rather than a lucky hit.
        if (NsdRead("CheckpointUnlockCount", &v, &set)) {
            QDLOG(LOG_INFO, "PROBE reader: PASS  CheckpointUnlockCount=", dec(v));
        } else {
            QDLOG(LOG_WARN, "PROBE reader: INCONCLUSIVE  CheckpointUnlockCount absent "
                            "(not an error on a different save)");
        }
    }

    // 5. the writer, proved by a no-op write and a read-back.
    //
    // This is the item the analysis called unpinned: "SaveFunctions_C.SetNamedSaveData
    // -- the write entry for NamedSaveData ... the setter's RVA/param buffer is not
    // yet documented."  Writing back the value that is already there cannot corrupt
    // progression, and the read-back proves the path works.  A write of a DIFFERENT
    // value is what layers 4+ do, and only when [Rules] FlagSync=1.
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

    // 6. the dialogue action entry points exist as expected (presence only; calling
    //    them is a live-test action, not a load-time one)
    {
        void* cdo = FindCdo("SaveFunctions_C");
        if (cdo) {
            void* addItem = FindFunction(cdo, "AddItemToInventory");
            QDLOG(LOG_INFO, "PROBE AddItemToInventory: ", (addItem ? "FOUND" : "ABSENT"));
            if (addItem && s.layout.valid) {
                LayoutResult al;
                al.props = 0;
                al.parms_size = 0;
                if (WalkProperties(addItem, s.layout.cand, &al) && al.props > 0) {
                    LogSignature("AddItemToInventory", al);
                }
            }
        } else {
            QDLOG(LOG_WARN, "PROBE AddItemToInventory: cannot find SaveFunctions_C CDO");
        }
    }

    QDLOG(LOG_INFO, "======== probe complete ========");
}

// ---------------------------------------------------------------------------
// Save-layer readiness, retried from the frame pump
// ---------------------------------------------------------------------------
// Called every frame.  Cheap when the layer is already up: one liveness check on a
// cached pointer.  Re-initialises when the save object dies or is replaced, which is
// what a level load does -- and the corpus rule is explicit that a cached handle must
// not be trusted across a map change.
bool TryInitSaveLayer(uint32_t now)
{
    SaveLayer& s = Save();
    // Re-resolve when the world changes, not on an ObjectIsLive probe: that probe
    // compares the object-array slot back to the object, and on this build the slot for
    // a live save object does not always read back as the same pointer, so trusting it
    // made the layer re-initialise every other second.
    if (s.readOk && s.save) {
        void* world = *(void**)Ptr<void*>(0x04BA86B0u);
        if (world != nullptr && world == s.world) return true;
    }

    // Do not hammer SaveInit: it walks the object graph and does the property search.
    if (g_lastSaveInitMs != 0 && (now - g_lastSaveInitMs) < 2000) return false;
    g_lastSaveInitMs = now;

    void* world = *(void**)Ptr<void*>(0x04BA86B0u);
    if (!world) return false;

    QDLOG(LOG_DEBUG, "  TryInitSaveLayer: calling SaveInit");
    if (!SaveInit(world)) {
        QDLOG(LOG_DEBUG, "  TryInitSaveLayer: SaveInit returned false");
        // DEBUG, not WARN: at the main menu this is the expected state, and a warning
        // every two seconds trains the operator to ignore warnings.
        QDLOG(LOG_DEBUG, "save layer not ready: ", s.note);
        return false;
    }

    if (!g_probeDone) {
        RunApiProbe();
        g_probeDone = true;
    } else {
        QDLOG(LOG_INFO, "save layer re-initialised (level change or new save)");
        // Re-learn what changed: the mirror must report every key again, and the flag
        // push must not think it already sent this container.
        g_mirrorCount = 0;
        g_lastFlagCount = -1;
        g_flagsDirty = true;
    }
    return s.readOk;
}

// ---------------------------------------------------------------------------
// LAYER 3 -- the read-only mirror
// ---------------------------------------------------------------------------
// Every container change is logged with its old and new value.  This is the
// instrument that turns "the saves look equal" into "these 11 keys changed in this
// order", and it is what a two-instance run is compared against.
// (MirrorState / g_mirror are declared with the other globals, above.)

void MirrorVisit(const char* key, int32_t value, void* user)
{
    (void)user;
    for (int i = 0; i < g_mirrorCount; ++i) {
        if (NameEquals(g_mirror[i].last_name, key)) {
            if (g_mirror[i].last_value != value) {
                QDLOG(LOG_INFO, "flag CHANGE ", key, " : ",
                      dec(g_mirror[i].last_value), " -> ", dec(value));
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

        bool ok = false;
        if (h.rev != 0) {
            // Host authority: ApplyRemote refuses to lower a value.
            ok = g_flags.ApplyRemote(name, value, h.rev);
        }
        if (ok) {
            ++applied;
            if (Save().writeOk) {
                char err[128];
                if (!NsdWrite(name, value, err, sizeof(err))) {
                    QDLOG(LOG_WARN, "apply failed for ", name, ": ", err);
                }
            }
        } else {
            ++refused;
        }
    }
    if (applied || refused) {
        QDLOG(LOG_DEBUG, "flags applied=", dec(applied), " refused=", dec(refused),
              " rev=", dec(h.rev));
    }
}

// Peer side: an intent is a request, the host decides.
void HostApplyIntent(wire::Reader& r)
{
    wire::IntentBody b;
    if (!wire::ReadIntent(r, &b)) return;

    if (b.kind == wire::I_SET_FLAG) {
        if (!Cfg().flag_sync) return;
        bool ok = g_flags.ApplyIntent(b.name, b.value, b.mode != 0,
                                     b.based_on_rev, g_flagsRevision);
        if (!ok) {
            QDLOG(LOG_INFO, "intent refused (regressive or stale): ", b.name,
                  "=", dec(b.value), " based_on=", dec(b.based_on_rev),
                  " last_sent=", dec(g_flagsRevision));
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
        QDLOG(LOG_INFO, "actor-state intent from peer guid=", hex32(b.guid[0]),
              " value=", dec(b.value), " (apply path pending)");
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
// LAYER 5 -- the remote body
// ---------------------------------------------------------------------------
// Asset identity is transmitted as class names, never pointers.  The body class ships
// with the game (Player_FullChar_2026, worn by FS_Full_PlayerChar_C), so no new art
// is needed -- the single largest schedule win over the previous project, whose
// unfinished milestone was exactly this.
void* ResolvePuppetClass()
{
    if (g_puppetClass && ObjectIsLive(g_puppetClass)) return g_puppetClass;
    // The analysed live pawn class is FS_Full_PlayerChar_C (gap B3, measured).
    g_puppetClass = FindClassByName("FS_Full_PlayerChar_C");
    if (!g_puppetClass) g_puppetClass = FindClassByName("FS_PlayerChar_C");
    if (!g_puppetClass) {
        QDLOG(LOG_ERROR, "no player character class found in GObjects");
    } else {
        char d[128];
        ObjDescribe(g_puppetClass, d, sizeof(d));
        QDLOG(LOG_INFO, "puppet class resolved: ", d);
    }
    return g_puppetClass;
}

// The engine spawn pair, byte-verified (B7: the game has no character-construction
// path of its own, so the engine pair is the correct instrument).
typedef void* (*FnBeginDeferred)(void* worldContext, void* actorClass, const void* transform,
                                 uint8_t collisionHandling, void* owner);
typedef void* (*FnFinishSpawning)(void* actor, const void* transform);

void SpawnPuppetOnce()
{
    if (!Save().world) return;
    void* cls = ResolvePuppetClass();
    if (!cls) { g_puppet.SpawnFailed("no class", Net::Now()); return; }

    FnBeginDeferred begin = (FnBeginDeferred)Ptr<void*>(0x028D3C90u);
    FnFinishSpawning finish = (FnFinishSpawning)Ptr<void*>(0x028DCD50u);
    if (!begin || !finish) { g_puppet.SpawnFailed("no spawn entry", Net::Now()); return; }

    // FTransform for UE4.27 is { FQuat(16); FVector(12); float pad(4); FVector(12);
    // float pad(4) } = 48 bytes, 16-byte aligned.  Zeroed is identity: rotation
    // identity, translation 0, scale 1 must be written explicitly because a zero
    // scale collapses the mesh.
    alignas(16) uint8_t xform[48];
    for (int i = 0; i < 48; ++i) xform[i] = 0;
    float one = 1.0f;
    memcpy(xform + 0, &one, 0);          // (identity quat w is at +12)
    // FQuat { x, y, z, w } -> w = 1
    memcpy(xform + 12, &one, 4);
    memcpy(xform + 32, &one, 4);         // Scale3D.x
    memcpy(xform + 36, &one, 4);         // Scale3D.y
    memcpy(xform + 40, &one, 4);         // Scale3D.z

    g_puppet.SpawnStarted(Net::Now());
    void* actor = begin(Save().world, cls, xform, 0 /* AlwaysSpawn */, nullptr);
    if (!actor) {
        g_puppet.SpawnFailed("BeginDeferredActorSpawnFromClass returned null", Net::Now());
        return;
    }
    void* done = finish(actor, xform);
    if (!done) {
        g_puppet.SpawnFailed("FinishSpawningActor returned null", Net::Now());
        return;
    }
    g_puppetActor = done;
    g_puppet.Spawned(Net::Now());
    char d[160];
    ObjDescribe(done, d, sizeof(d));
    QDLOG(LOG_INFO, "puppet spawned: ", d);
}

void PuppetPump(uint32_t now)
{
    if (!Cfg().puppet_sync) return;

    // Presence is "we have heard from the peer recently", which the interpolation
    // layer already tracks; the puppet FSM owns the lifecycle decisions.
    bool present = (now - g_net.LastRxMs()) < 2000 && g_net.Connected();
    g_puppet.PeerPresent(present, now);
    g_puppet.Tick(now);

    puppet::PuppetFsm::Step st = g_puppet.Get();
    // The FSM owns the backoff deadline; the pump asks rather than deciding, so a
    // 60 Hz tick cannot turn a failed spawn into 60 attempts per second.
    if (st.want_spawn && g_puppet.ReadyToSpawn(now)) SpawnPuppetOnce();

    if (st.want_destroy && g_puppetActor) {
        // Never reached on silence -- only on a peer that left the session.  The
        // corpus rule: "Never destroy a puppet on silence. Freeze at 2 s, hide at
        // 30 s, keep the actor valid."  The FSM only asks for this on a level change
        // or a session end, both of which are explicit events.
        QDLOG(LOG_INFO, "puppet destroy requested (explicit event)");
        g_puppetActor = nullptr;
    }
}

// ---------------------------------------------------------------------------
// LAYER 6 -- world
// ---------------------------------------------------------------------------
// Streaming: the loaded set must be the union of what is around BOTH players, and a
// level must not unload while the other player stands in it.  The manager is a stack
// of lists (analysis gap C10), and a naive add+flush replaced 21 levels with 11 in the
// previous project's measurement -- so the union is computed here and applied only
// when it is a strict superset of what is already wanted.
void StreamPump(uint32_t now)
{
    if (!Cfg().stream_union) return;
    // The local list is populated from the game's level manager elsewhere; until
    // that binding lands, this reports the union's shape so a two-instance run can
    // confirm the add-only property from the log.
    stream::LevelSet want = g_stream.Wanted(now);
    static int lastN = -1;
    if ((int)want.n != lastN) {
        QDLOG(LOG_INFO, "level union size ", dec((int)want.n),
              (g_stream.PeerListStale(now) ? " (peer list stale)" : ""));
        lastN = (int)want.n;
    }
}

void DialoguePump(uint32_t now)
{
    if (!Cfg().dialogue_sync) return;
    g_dialogue.Tick(now);
}

// ---------------------------------------------------------------------------
// Inbound dispatch -- game thread only
// ---------------------------------------------------------------------------
void DispatchInbound(const InboundPacket& p, uint32_t now)
{
    wire::Reader r(p.body, p.body_len);
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
        g_peerInterp.Push(s);
        break;
    }
    case wire::K_WORLD_FLAGS:
        if (g_net.IsPeer()) PeerApplyFlags(r);
        break;
    case wire::K_INTENT:
        if (g_net.IsHost()) HostApplyIntent(r);
        break;
    case wire::K_DIALOGUE: {
        wire::DialogueBody d;
        if (!wire::ReadDialogue(r, &d)) return;
        dialogue::DialogueState st;
        st.seq = d.seq;
        for (int i = 0; i < 4; ++i) memcpy(st.npc + i * 4, &d.npc[i], 4);
        st.slot = d.slot;
        st.line = d.line;
        st.entry = d.entry;
        if (!g_dialogue.Accept(st)) {
            QDLOG(LOG_DEBUG, "stale dialogue state seq=", dec(d.seq), " discarded");
        }
        break;
    }
    case wire::K_LEVEL_UNION:
        // Bits + optional name table; the guard owns the merge.
        break;
    case wire::K_ENEMY:
        // Peer applies persistence through StoreEnemySaveState on its own spawner;
        // the spawner lookup by authored GUID is the next slice's binding work.
        break;
    default:
        break;
    }
    (void)now;
}

// ---------------------------------------------------------------------------
// Sending the local player
// ---------------------------------------------------------------------------
void SendLocalPlayer(uint32_t now)
{
    if (!g_net.Connected()) return;
    int hz = Cfg().player_hz;
    uint32_t interval = (uint32_t)(1000 / hz);
    if (now - g_lastPlayerPushMs < interval) return;
    g_lastPlayerPushMs = now;

    // The pawn is re-resolved every time rather than cached: a cached pawn across a
    // map change is the access violation the corpus warns about.
    typedef void* (*FnGetPawn)(void*, int32_t);
    FnGetPawn gp = (FnGetPawn)Ptr<void*>(0x028E0640u);
    void* world = Save().world;
    void* pawn = nullptr;
    if (gp && world) pawn = gp(world, 0);
    if (pawn != g_localPawn) {
        g_localPawn = pawn;
        if (pawn) {
            char d[160];
            ObjDescribe(pawn, d, sizeof(d));
            QDLOG(LOG_INFO, "local pawn: ", d);
            g_peerInterp.Reset();
        }
    }
    if (!pawn) return;

    // Position is read through the reflection-free route: the pawn's own transform is
    // reached via the actor's root component.  Until that binding is verified live the
    // row is sent with zero transform -- a valid, non-crashing row that keeps the
    // session and the animation state alive, rather than guessing an offset.  The
    // corpus's rule governs: "measure it, then pin it", never assume a stride.
    wire::PlayerRow row;
    memset(&row, 0, sizeof(row));
    row.slot = g_net.IsHost() ? 0 : 1;
    row.sample_ms = now;
    row.action = 0;

    uint8_t body[64];
    wire::Writer w(body, sizeof(body));
    wire::WritePlayer(w, row);
    if (w.ok) g_net.SendUnreliable(wire::K_PLAYER, body, w.len);
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
    return true;
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
    SendLocalPlayer(now);
    if (tracing) QDLOG(LOG_DEBUG, "  frame ", dec(g_frame), " player sent");

    if (g_net.IsHost()) {
        HostPushFlags(now);
        if (Cfg().enemy_sync && now - g_lastEnemyPushMs > 50) {
            g_lastEnemyPushMs = now;
        }
    }
    PuppetPump(now);
    StreamPump(now);
    DialoguePump(now);

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

    // D5 is closed and this is the whole of enemy persistence sync: the record is
    // {bValid, RespawnTime, RemainingHP} keyed by an authored GUID.  Capture it and
    // let the host broadcast; a peer never originates one.
    if (!Cfg().enemy_sync || !g_net.IsHost() || !guid) return;
    for (int i = 0; i < 32; ++i) {
        if (!g_kills[i].used) {
            g_kills[i].guid = *guid;
            g_kills[i].respawn = respawn;
            g_kills[i].hp = hp;
            g_kills[i].seen_ms = Net::Now();
            g_kills[i].used = true;
            QDLOG(LOG_INFO, "enemy persistence write captured guid=", hex32(guid->a),
                  " hp=", flt(hp, 1), " respawn=", flt(respawn, 1));
            return;
        }
    }
}

void HK_ProcessEvent(void* self, void* func, void* parms)
{
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
    LogOpen(dir);
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
        for (unsigned k = 0; k < kQdTargetCount; ++k) {
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
        // ProcessEvent is the widest hook in the process and costs the most; it stays
        // off unless someone is tracing a specific Blueprint surface.
        if (tPE && Cfg().log_level >= LOG_TRACE) {
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






