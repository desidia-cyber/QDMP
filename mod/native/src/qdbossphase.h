// qdbossphase.h -- boss PHASE authority, the final fight's transition chain, and the arena
// rule for a fight whose partner is not in the arena.  [gap-boss]
//
// ============================================================================
// THE MEASURED FAILURES THIS HEADER EXISTS FOR
// ============================================================================
//
// (1) BOSS PHASES ARE NOT A SYNCED VALUE (COVERAGE-MATRIX C12 / GAP rank 1).
//     Session 29 run 15, the Queen in `600_Castle`:
//
//       phase 1 reaches HP 0 -> she enters `DoStateTransition` and WAITS
//       log: `boss death watch C88FFC0C: state=0 phaseAnim=0 deathSuspended=0 bDead=1`
//       log: `dev props: Queen_BP_C CurrentStateIndex=i0/f0 DoingPhaseAnim=0 ... bDead=0`
//       ... for 45 s, at MINUS 200 HP, and the fight never continues.
//
//     That is not a co-op regression: it is the phase machine. The measurement that made
//     this look unexpressible was "`Phase`, `bBoss`, `OnSubStateChanged` exist ONLY in the
//     exe name block and in zero cooked packages".  That measurement was about the WRONG
//     NAMES. The shipped build has a real, generic, state-index machine, and it is in the
//     cooked content:
//
//       FS_EnemyCharacterBase (native, `/Script/QueensDomain`)
//         CurrentStateIndex   (int32, the state machine's cursor)
//         CurrentSubState     (int32)
//         SetCurrentStateIndex(int32)   -- a native UFunction, callable via ProcessEvent
//         SetCurrentSubState(int32)
//         OnStateIndexChanged(NewState) -- the Blueprint event a state change dispatches
//         OnSubStateChanged(NewState)
//         K2Node_Event_NewPhase / NewPhase / Phase / OnPhaseChanged
//
//       Queen_BP (cooked, 161 exports) additionally declares
//         DoStateTransition         (Function -- the transition the rig saw her stuck in)
//         OnPhaseChanged            (Function)
//         DoOrbAnimation, OrbAttack, SetUpPlayerForOrbCutscene, SetUpBossForOrbCutscene,
//         OrbProjectileHitPlayer, KillOrbs, UnlockFromOrbStuff,
//         SipehaTransitionEnded, Ending1Complete, Ending2CutsceneEnded
//       and its imports name `Ending1CutsceneManager_C`, `Ending2CutsceneManager_C`,
//       `SipehaTransitionCutsceneManager_C`, `Queen_Nude_Data`, `Queen_P1_P2_Transition_Montage`,
//       `Queen_SpecialShieldBreakGreetDialogue` and both `Music/3-Boss` cues.
//
//     So a phase IS readable (`CurrentStateIndex`/`CurrentSubState`) and a transition IS
//     drivable (`SetCurrentStateIndex` + `OnStateIndexChanged`) -- the same reflective
//     surface the damage, HP and montage lanes already use. See §"WHAT IS DRIVEN" below.
//
// (2) THE FINAL FIGHT (COVERAGE-MATRIX D10 / GAP rank 2, CAMPAIGN-ROUTE R33/E1-E4).
//     Nobody has ever SEEN phase 2 in co-op. The transition is driven here, and the four
//     endings' consistency is a rule, not a hope: whichever player's machine RUNS the boss
//     makes the transition, and the other machine's copy is driven through the SAME native
//     setter -- so `Ending1Complete`/`Ending2Complete`/`Ending3Complete`/`Ending4Complete`
//     are written by each machine's OWN game from the same state, never from a guess.
//
// (3) ARENAS (COVERAGE-MATRIX D7/D8/D12, GAP ranks 9 and 11, and the `DeathRespawn=1`
//     interaction). Two concrete holes, and the rule this header defines for them:
//
//     a. THE BARRIER AND THE BAR.  `qdboss.h` already owns the ONE shared HP bar and the
//        arena-exit event. What was never stated is what happens to a fight when the
//        partner is NOT in the arena -- which is now the ordinary case, because
//        `DeathRespawn=1` puts a dead player back at their last rest WITHOUT a world
//        reload, and the world (and the fight) keeps running for the survivor.
//        THE RULE (implemented as `FightPolicy`): the fight CONTINUES with the players who
//        are present. It is never cancelled, never restarted, and the absent player is
//        never dragged in by force -- the arena gate is a `Door_Base`/`SavableActor` whose
//        state the door lane keeps, so the respawned player walks back in through the same
//        gate the survivor is still behind. `kBossArenaExit` is NOT sent for an absent
//        partner: it would teleport the survivor out of a fight that is still running.
//
//     b. A FIGHT WHOSE OWNER LEFT.  The boss is sticky-owned (`qdenemyown.h`). If the owner
//        dies (and respawns far away) the follower must not be left with a frozen statue
//        AND no authority: after `kOwnerSilentMs` the surviving machine becomes the
//        authority for the phase as well as the body.
//
// ============================================================================
// THE RULE (authority)
// ============================================================================
//   * ONE AUTHORITY PER BOSS, and it is the machine that RUNS the boss
//     (`enemyown::OwnerSet::State(key) != O_FROZEN` and the record is not a constructed
//     mirror). That is the same authority the bar already uses; a second, different
//     authority would be a second bug.
//   * AN ORDINAL IS NOT A SCRIPT. What crosses the wire is the pair
//     (CurrentStateIndex, CurrentSubState) plus a monotonically increasing `rev`. The
//     follower does not run any phase logic of its own; it calls the game's own
//     `SetCurrentStateIndex` + `OnStateIndexChanged` on its own copy, so the copy's OWN
//     Blueprint graph performs the transition (montage, model swap, cutscene manager,
//     ending write). That is what keeps the two machines identical without the mod ever
//     re-implementing a phase.
//   * A LOWER `rev` IS STALE, A LOWER STATE WITH A NEWER `rev` IS A REAL CHANGE. The Queen
//     goes 0 -> 1 -> 2 and back to 0 for the ending; comparing state numbers alone would
//     drop the ending. The revision is what orders them.
//   * MONOTONE-ON-ARRIVAL, NEVER FABRICATED. A follower that has no live copy says so and
//     does nothing; a follower whose copy is already at or past the announced revision does
//     nothing. Nothing is invented: no state is set that the owner did not report.
//
// ============================================================================
// WIRE (reserved kinds 0x5C-0x5F)
// ============================================================================
//   0x5C K_BOSS_PHASE  owner -> follower, RELIABLE, on change + 2 s repair.
//        body: key(16) | rev(u32) | state(i32) | sub(i32) | flags(u8)
//        flags: bit0 = the sender is AUTHORITY (it runs the boss),
//               bit1 = this is a TRANSITION the boss itself started, not a steady sample,
//               bit2 = the sender saw `DoingPhaseAnim` set (cosmetic/diagnostic only).
//        Reliable because a lost transition is a permanently stuck boss -- the exact
//        failure run 15 measured.
//
//   0x5D K_BOSS_ARENA  either -> either, RELIABLE, on change.
//        body: key(16) | phase of the fight(u8: kArenaIdle/Open/Closed) | present(u8 bitmask)
//        | rev(u32)
//        "the fight is CLOSED and player 1 is inside, player 2 is not". Both machines apply
//        the same rule from this one fact, so the barrier agrees without either machine
//        driving the other's door.
//
//   0x5E K_BOSS_END    any -> any, RELIABLE, one-shot per choice.
//        body: key(16) | choice(u8: kEndQueen1..kEndSipeha2) | who(u8: 1 host, 2 peer) | rev(u32)
//        The ending CHOICE is made by whichever player makes it; the other follows. The
//        receiver does not invent a choice -- it runs the same ending entry the sender's
//        own game ran, so each machine's own save writes its own `EndingNComplete`.
//
//   0x5F is reserved and unused; kept free so this lane can grow without renumbering.
//
// Header-only and dependency-free (apart from the wire codec), compiled into native/test
// like the other state machines.
#pragma once
#include <stdint.h>
#include <string.h>

#include "qdwire.h"

namespace qd {
namespace bossphase {

// ---------------------------------------------------------------------------
// Arena / fight policy
// ---------------------------------------------------------------------------
// The fight's own state, as both machines must agree on it. It is deliberately three
// values and not a boolean: "not started" and "over" behave differently (an idle arena
// has no barrier; a finished one has an open gate).
enum ArenaPhase : uint8_t {
    kArenaIdle   = 0,   // no fight here
    kArenaOpen   = 1,   // the fight has begun; the barrier has not closed yet
    kArenaClosed = 2,   // the barrier is closed: the fight is running
    kArenaDone   = 3,   // the boss is dead and the gate is open again
};

// Which players are inside. Bit 0 = host's player, bit 1 = peer's player.
static const uint8_t kPlayerHost = 0x01;
static const uint8_t kPlayerPeer = 0x02;

// How long the surviving machine waits for the owner's phase before it takes over the
// phase authority as well as the body. Longer than the ownership lane's `kReleaseMs`
// (2000 ms) so a normal ownership release wins first; short enough that a partner who
// died and respawned does not leave the survivor in front of a statue.
static const uint32_t kOwnerSilentMs = 6000;

// A phase body is re-sent at this interval while the fight is running, so a single lost
// datagram cannot leave a boss mid-transition. Matches the bar's own repair cadence.
static const uint32_t kPhaseRepairMs = 2000;

// The transition is an EVENT: within this window the receiver treats the announcement as
// "run the transition now" rather than "sample this state".
static const uint32_t kTransitionWindowMs = 4000;

// DOES THE FIGHT CONTINUE WHEN A PLAYER IS ABSENT?
// Yes, and this returns the reason so the live log can state the rule rather than imply it.
enum FightDecision : uint8_t {
    kFightContinueBoth     = 0,   // both players present: ordinary fight
    kFightContinueAbsent   = 1,   // one player absent: the fight goes on with the survivor
    kFightHoldForArrival   = 2,   // the fight has not started and a player is missing
    kFightFinished         = 3,   // the boss is dead: nothing to continue
};

// `present` is the same bitmask as the wire. `bossAlive` is the authority's own answer.
//
// THE RULE, STATED: a running fight is never cancelled because a player is absent. The
// only thing a missing player changes is that the fight does not START until somebody is
// inside -- and once it has started, the barrier stays closed and the fight runs for
// whoever is there. A player who respawns can walk back in through the gate (the gate is
// the door lane's, not this lane's, and it is already synced), which is why nothing here
// pulls an absent player in.
inline FightDecision Decide(bool bossAlive, uint8_t present, uint8_t arenaPhase)
{
    if (!bossAlive) return kFightFinished;
    bool anyIn = (present & (kPlayerHost | kPlayerPeer)) != 0;
    bool allIn = (present & (kPlayerHost | kPlayerPeer)) == (kPlayerHost | kPlayerPeer);
    if (arenaPhase == kArenaIdle) return anyIn ? kFightContinueBoth : kFightHoldForArrival;
    if (allIn) return kFightContinueBoth;
    return kFightContinueAbsent;
}

// THE ARENA BARRIER IS NOT DRIVEN FROM HERE. A barrier is a `Door_Base_C`/`SavableActor_C`
// and the door lane already keeps its open/closed state on both machines
// (COVERAGE-MATRIX B4/D7). What this lane adds is that the REASON it closed is agreed: an
// arena that reports `kArenaClosed` implies the door lane will keep it shut, and
// `kArenaDone` implies the same lane opens it. `BarrierAction` is the only statement this
// lane makes about a barrier, and it is idempotent on purpose.
enum BarrierAction : uint8_t {
    kBarrierNone  = 0,   // leave the door lane alone
    kBarrierClose = 1,   // the fight is running: the door lane should hold it shut
    kBarrierOpen  = 2,   // the fight is over: the door lane should open it
};

inline BarrierAction BarrierFor(uint8_t arenaPhase, bool bossAlive)
{
    if (arenaPhase == kArenaClosed && bossAlive) return kBarrierNone;   // already shut
    if (arenaPhase == kArenaOpen && bossAlive) return kBarrierClose;
    if (arenaPhase == kArenaDone || !bossAlive) return kBarrierOpen;
    return kBarrierNone;
}

// ---------------------------------------------------------------------------
// The phase body (0x5C)
// ---------------------------------------------------------------------------
// The BODY TYPES AND THEIR FLAG BITS LIVE IN qdwire.h (`BossPhaseBody`, `kBossPhase*`), and
// this header deliberately does NOT redeclare them: two spellings of one layout is how a
// codec and its rule drift apart. What lives here is only the RULE.
using PhaseBody = wire::BossPhaseBody;
using ArenaBody = wire::BossArenaBody;
using EndBody   = wire::BossEndBody;

static const uint8_t kPhaseAuthority  = wire::kBossPhaseAuthority;
static const uint8_t kPhaseTransition = wire::kBossPhaseTransition;
static const uint8_t kPhaseAnimating  = wire::kBossPhaseAnimating;

static const int kPhaseBodyBytes = wire::kBossPhaseBytes;
static const int kArenaBodyBytes = wire::kBossArenaBytes;
static const int kEndBodyBytes   = wire::kBossEndBytes;

// The TYPE (`BossPhaseBody`) and its codec live in qdwire.h; this is its rule.
inline bool ValidPhase(const PhaseBody& b)
{
    if ((b.guid[0] | b.guid[1] | b.guid[2] | b.guid[3]) == 0) return false;
    // A Blueprint state cursor is a small ordinal. Anything else is a decode of the wrong
    // object, and applying it would drive the boss's graph with a nonsense index.
    if (b.state < -1 || b.state > 255) return false;
    if (b.sub < -1 || b.sub > 255) return false;
    return true;
}

// ---------------------------------------------------------------------------
// Should an arriving phase be applied HERE?
// ---------------------------------------------------------------------------
// `localState`/`localSub` are this machine's own read of its copy (-1 = unreadable).
// `localRev` is the newest revision this machine has applied for the key (0 = none).
// `haveLocalCopy` is "this machine holds a live actor for the key".
//
// The three answers are distinct on purpose, because each one is a different log line and
// a different behaviour:
//   kApplyPhase   -- set it: the owner moved and this copy has not
//   kIgnoreStale  -- already at or past this revision: do nothing (idempotent re-send)
//   kNoCopy       -- there is nothing here to drive; report it, never invent one
enum PhaseVerdict : uint8_t {
    kApplyPhase  = 0,
    kIgnoreStale = 1,
    kNoCopy      = 2,
};

inline PhaseVerdict Judge(const PhaseBody& b, bool haveLocalCopy, uint32_t localRev,
                          int32_t localState, int32_t localSub)
{
    if (!haveLocalCopy) return kNoCopy;
    // Signed compare: `rev` wraps, and a wrapped rev must not look infinitely old.
    if (localRev == 0) return kApplyPhase;
    int32_t age = (int32_t)(b.rev - localRev);
    // STRICTLY OLDER IS STALE, whatever the values say. A newer state number is not a newer
    // fact: the Queen's chain returns to 0 for the ending, so ordering is the revision's job
    // and a body the owner has already superseded must never move a copy.
    if (age < 0) return kIgnoreStale;
    // THE SAME REVISION: the owner is restating its authority. If the values already agree
    // this is the 2 s repair and is a no-op; if they do not, this machine's copy missed a
    // transition and is wrong -- apply it. That is how a copy heals without a second event.
    if (age == 0) return (localState == b.state && localSub == b.sub) ? kIgnoreStale : kApplyPhase;
    return kApplyPhase;    // a newer revision
}

// ---------------------------------------------------------------------------
// The arena body (0x5D) -- the TYPE lives in qdwire.h; this is its rule
// ---------------------------------------------------------------------------
inline bool ValidArena(const ArenaBody& b)
{
    if ((b.guid[0] | b.guid[1] | b.guid[2] | b.guid[3]) == 0) return false;
    if (b.arena > kArenaDone) return false;
    if (b.present & ~(uint8_t)(kPlayerHost | kPlayerPeer)) return false;
    return true;
}

// ---------------------------------------------------------------------------
// The ending body (0x5E)
// ---------------------------------------------------------------------------
// The four endings, as the content writes them: `Queen_BP` writes `Ending1Complete` /
// `Ending2Complete` and `Sipeha_NPC` writes `Ending3Complete` / `Ending4Complete`
// (CAMPAIGN-ROUTE §3). The choice is made by whichever player makes it.
enum Ending : uint8_t {
    kEndNone   = 0,
    kEndQueen1 = 1,   // Ending1Complete
    kEndQueen2 = 2,   // Ending2Complete
    kEndSipeha1 = 3,  // Ending3Complete
    kEndSipeha2 = 4,  // Ending4Complete
};

inline const char* EndingFlag(uint8_t e)
{
    switch (e) {
    case kEndQueen1:  return "Ending1Complete";
    case kEndQueen2:  return "Ending2Complete";
    case kEndSipeha1: return "Ending3Complete";
    case kEndSipeha2: return "Ending4Complete";
    default:          return "";
    }
}

// THE BRANCH AND THE BOSS MUST AGREE. A `Queen` ending cannot be chosen in Sipeha's world
// and vice versa; a body that says so is a decode of the wrong thing and is refused rather
// than driving the wrong cutscene manager on one machine.
inline bool EndingMatchesBoss(uint8_t ending, const char* bossClass)
{
    if (!bossClass) return false;
    bool queen = strstr(bossClass, "Queen") != nullptr;
    bool sipeha = strstr(bossClass, "Sipeha") != nullptr;
    if (ending == kEndQueen1 || ending == kEndQueen2) return queen;
    if (ending == kEndSipeha1 || ending == kEndSipeha2) return sipeha;
    return false;
}

// The TYPE lives in qdwire.h (`BossEndBody`); this is its rule.
inline bool ValidEnd(const EndBody& b)
{
    if ((b.guid[0] | b.guid[1] | b.guid[2] | b.guid[3]) == 0) return false;
    if (b.ending < kEndQueen1 || b.ending > kEndSipeha2) return false;
    if (b.who != kPlayerHost && b.who != kPlayerPeer) return false;
    return true;
}

// ---------------------------------------------------------------------------
// Per-boss bookkeeping
// ---------------------------------------------------------------------------
// `PhaseTrack` is what one boss needs on one machine. It is a value type with no friends,
// so the test can drive it directly.
//
// THE TRANSITION IS REMEMBERED, NOT RE-DERIVED. `transitionAt` is when the owner last
// announced a transition; `appliedRev` is the newest revision this machine has driven.
// A machine that arrives at a fight already in progress gets the steady state (rev, state)
// and applies it, which is correct: the boss's own graph reads its state index, so setting
// the index IS joining the fight where it is.
struct PhaseTrack {
    uint32_t key[4];
    uint32_t rev;            // owner's newest revision seen
    uint32_t appliedRev;     // newest revision this machine has driven on its copy
    int32_t  state;
    int32_t  sub;
    uint32_t transitionAt;   // when the last transition was announced (0 = none)
    uint32_t lastSendAt;     // owner: the repair clock
    uint32_t lastLocalAt;    // follower: when the local copy last changed on its own
    uint8_t  flags;
    bool     used;

    void Reset()
    {
        memset(this, 0, sizeof(*this));
        state = sub = -1;
    }
};

static const int kTrackMax = 16;

class TrackSet {
public:
    TrackSet() { Reset(); }
    void Reset() { for (int i = 0; i < kTrackMax; ++i) t_[i].used = false; }

    PhaseTrack* Find(const uint32_t key[4])
    {
        for (int i = 0; i < kTrackMax; ++i)
            if (t_[i].used && SameKey(t_[i].key, key)) return &t_[i];
        return nullptr;
    }

    PhaseTrack* Touch(const uint32_t key[4])
    {
        if (PhaseTrack* t = Find(key)) return t;
        for (int i = 0; i < kTrackMax; ++i) {
            if (t_[i].used) continue;
            t_[i].Reset();
            memcpy(t_[i].key, key, 16);
            t_[i].used = true;
            return &t_[i];
        }
        // Full: reuse the least recently touched. A fight has a handful of bosses; the tie
        // is only reachable after a very long world, where an old track is worthless.
        int victim = 0;
        uint32_t oldest = 0xFFFFFFFFu;
        for (int i = 0; i < kTrackMax; ++i) {
            uint32_t age = t_[i].lastSendAt > t_[i].lastLocalAt ? t_[i].lastSendAt : t_[i].lastLocalAt;
            if (age <= oldest) { oldest = age; victim = i; }
        }
        t_[victim].Reset();
        memcpy(t_[victim].key, key, 16);
        t_[victim].used = true;
        return &t_[victim];
    }

    static bool SameKey(const uint32_t a[4], const uint32_t b[4])
    {
        return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
    }

    int Used() const
    {
        int n = 0;
        for (int i = 0; i < kTrackMax; ++i) n += t_[i].used ? 1 : 0;
        return n;
    }

private:
    PhaseTrack t_[kTrackMax];
};

// ---------------------------------------------------------------------------
// Owner side: should a phase be published now?
// ---------------------------------------------------------------------------
// A transition always publishes at once (a lost one is a stuck boss). A steady state
// publishes once, then repairs on the `kPhaseRepairMs` clock.
inline bool ShouldPublish(const PhaseTrack& t, int32_t state, int32_t sub, bool transition,
                          uint32_t now)
{
    if (transition) return true;
    bool changed = (state != t.state) || (sub != t.sub);
    if (changed) return true;
    if (t.lastSendAt == 0) return true;
    return (uint32_t)(now - t.lastSendAt) >= kPhaseRepairMs;
}

// The revision rule, in one place: every published body is newer than the last one.
inline uint32_t NextRev(const PhaseTrack& t)
{
    uint32_t r = t.rev + 1;
    return r ? r : 1;      // 0 means "no revision known", so it is never a revision
}

// ---------------------------------------------------------------------------
// The authority question
// ---------------------------------------------------------------------------
// A boss has ONE phase authority, and it is the machine that runs the body. `runsHere`
// comes from the ownership lane; `mirror` is the constructed-copy marker (a mirror never
// runs anything); `ownerSeenAt` is when the other machine last published anything for the
// key (0 = never). A machine whose partner has gone silent for `kOwnerSilentMs` takes the
// phase authority even if the ownership lane has not released it yet -- otherwise a
// player who died and respawned leaves the survivor in front of a statue.
inline bool IsAuthority(bool runsHere, bool mirror, uint32_t ownerSeenAt, uint32_t now)
{
    if (mirror) return false;
    if (runsHere) return true;
    if (ownerSeenAt == 0) return false;
    return (uint32_t)(now - ownerSeenAt) > kOwnerSilentMs;
}

// ---------------------------------------------------------------------------
// What the follower drives (the exact reflective surface)
// ---------------------------------------------------------------------------
// NAMED HERE SO THE ENGINE HALF AND THE RULES CANNOT DRIFT APART, and so a reader can see
// what the lane depends on without reading proxy.cpp. Every name below was measured in
// the cooked packages (`Queen_BP` / `FS_EnemyCharacterBase`), not guessed.
//
//   FS_EnemyCharacterBase.CurrentStateIndex      int32   read AND write (direct)
//   FS_EnemyCharacterBase.CurrentSubState        int32   read AND write (direct)
//   FS_EnemyCharacterBase.SetCurrentStateIndex   native  int32 param `NewState`
//   FS_EnemyCharacterBase.SetCurrentSubState     native  int32 param `NewState`
//   FS_EnemyCharacterBase.OnStateIndexChanged    event   int32 param `NewState` (the graph)
//   FS_EnemyCharacterBase.OnSubStateChanged      event   int32 param `NewState`
//
// THE ORDER MATTERS and is asserted by the test: the setter first (so the graph's own
// reads see the new index), then the event (so the graph's transition body runs). Calling
// the event without the setter would run a transition for the OLD state.
inline bool ValidStateName(const char* n)
{
    return n && n[0] != 0;
}

static const char* const kPropState = "CurrentStateIndex";
static const char* const kPropSub   = "CurrentSubState";

} // namespace bossphase
} // namespace qd
