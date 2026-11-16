// qdending.h -- THE FOUR ENDINGS AS A SHARED, DRIVEN FACT: which ending the pair takes, the
// ending's own wake-up on BOTH machines, and the level-management rule the wake depends on.
// [play-p3]
//
// ============================================================================
// THE MEASURED FAILURES THIS HEADER EXISTS FOR
// ============================================================================
//
// (1) THE CHOICE NEVER CROSSES (CAMPAIGN-ROUTE §3, "The choice that picks 1 vs 2 ... is not
//     decoded").  `K_BOSS_END` (0x5E) and its body were added by `gap-boss`, and nothing ever
//     publishes it: `grep WriteBossEnd mod/native/src` finds the codec in `qdwire.h` and the
//     RECEIVER in `proxy.cpp` (`OnEnd`, which only logs and refuses a mismatched class).  So
//     each machine reaches whatever ending ITS OWN copy happened to run, and the pair can land
//     on DIFFERENT endings -- the failure that looks like "the wake-up played on one screen".
//
// (2) THE FOLLOWER NEVER RUNS THE ENDING AT ALL.  The ending's cutscene managers
//     (`Ending1CutsceneManager_C`, `Ending2CutsceneManager_C`, `SipehaTransitionCutsceneManager_C`)
//     are SPAWNED AT RUNTIME -- they are imports of `Queen_BP`, not placed actors -- so the
//     cutscene lane cannot resolve one by name on the follower and falls back to HOLDING
//     (`CutsceneAct`, `CV_HOLD`: EnterCutsceneMode + LockInputForLoadingOrCutscene).  A machine
//     that only holds never runs the ending's own graph, therefore never calls the player's
//     `EndingWakeOnBeach`, therefore never plays the wake-up.  HOLDING IS NOT WATCHING.
//
// (3) THE WAKE-UP'S OWN CHAIN, from the cooked packages (`analysis/scratch/p3_ending_probe.py`,
//     measured 2026-11-16):
//
//       FS_PlayerChar (770 exports)
//         EndingWakeOnBeach        (export 174)  -- teleport to the beach under a held fade
//         PostEndingWakeOnBeach    (176)         -- the wake montage + the fade back in
//         WakeOnBeachAnim          (29)          -- the animation (waits on
//                                                   `FS_WaitForLevelManagement`)
//         LockInPlaceForLoad       (180)
//         CancelLockInPlaceForLoad (179)
//       `PostEndingWakeOnBeach` is called from `ExecuteUbergraph_FS_PlayerChar`; the lock and
//       its cancel sit ~160 bytes apart in the SAME ubergraph region.  So the wake takes a LOAD
//       LOCK and only releases it when the level manager has settled.  If level management never
//       settles, the lock is never cancelled, `WakeOnBeachAnim`'s wait never completes, and the
//       camera stays black -- which is exactly what runs 34-46 measured.
//
//       Queen_BP (161 exports): Ending1Complete (5), Ending2CutsceneEnded (4),
//         SipehaTransitionEnded (3), DoStateTransition (6), OnPhaseChanged, DoOrbAnimation,
//         SetUpPlayerForOrbCutscene, SetUpBossForOrbCutscene.  `DoStateTransition` is called
//         from `ExecuteUbergraph_Queen_BP` AND from `TryApplyDamage` -- her HP thresholds are the
//         transition driver, which is why a REAL fight (not a lever) is the proof that matters.
//       Sipeha_NPC has SIX functions in total: ExecuteUbergraph_Sipeha_NPC, CustomEvent x2,
//         Eat, Banish, Interact.  The Sipeha branch is therefore drivable on the follower by
//         calling the game's OWN `Banish` / `Eat` on its own copy -- no graph re-implemented.
//
// ============================================================================
// THE RULES
// ============================================================================
//  * THE CHOICE IS THE CHOPPER'S, AND IT IS OBSERVED, NOT GUESSED.  The instant a machine's own
//    game runs an ending entry -- `Banish`/`Eat` on a `Sipeha_NPC`, `Ending1Complete` /
//    `Ending2CutsceneEnded` on a `Queen_BP` -- that machine MAKES the choice.  It publishes
//    `BOSS_END` (0x5E) once and then repairs it until the partner is known to have the flag.
//  * THE FOLLOWER RUNS THE SAME ENTRY ON ITS OWN COPY.  Not a flag write and not a cutscene
//    launch: the game's own verb, so its own graph spawns its own cutscene manager and its own
//    player's `EndingWakeOnBeach` runs.  `EndingVerb()` is that map, and it is the only place
//    the names live.
//  * THE BRANCH MUST MATCH THE BOSS.  A Queen ending cannot be driven into a Sipeha world.
//  * A RECEIVER THAT ALREADY HAS THE FLAG DOES NOTHING.  Its own game already chose; driving it
//    again would run an ending twice.
//  * THE WAKE-UP'S LEVEL MANAGEMENT IS NOT THE MOD'S TO DRIVE.  While a wake is pending, the
//    mod must not force/refresh levels: the game's own `FS_WaitForLevelManagement` is waiting
//    for the queue to settle, and every force is a new entry in it.  `LevelDriveAllowed()` is
//    that rule.  This is a GATE, not a fix-by-imitation: after the hold the game's own
//    `PostEndingWakeOnBeach` and `WakeOnBeachAnim` do the work.
//  * A STUCK LOAD LOCK IS THE ONE THING THE MOD MAY RELEASE, and only with the game's own
//    `CancelLockInPlaceForLoad`.  If the wake has not played by `kWakeLockCancelMs`, the lock is
//    cancelled (logged as such), and `PostEndingWakeOnBeach` is invoked as a rescue; the log line
//    names which half ran, so "the game's own wake-up" is never claimed for the rescue.

#pragma once
#include <stdint.h>
#include <string.h>

#include "qdwire.h"

namespace qd {
namespace ending {

// The four endings, using the WIRE's constants so the rule and the body cannot drift apart.
static const uint8_t kEndNone       = 0;
static const uint8_t kEndQueen1     = wire::kBossEndQueen1;   // Ending1Complete
static const uint8_t kEndQueen2     = wire::kBossEndQueen2;   // Ending2Complete
static const uint8_t kEndSipeha1    = wire::kBossEndSipeha1;  // Ending3Complete
static const uint8_t kEndSipeha2    = wire::kBossEndSipeha2;  // Ending4Complete

// ---------------------------------------------------------------------------
// The verbs -- the measured entry points, and the only place the names live
// ---------------------------------------------------------------------------
// `Queen_BP` and `Sipeha_NPC` exports, read out of the cooked packages.  The Queen's are her
// ending entries; the Sipeha NPC's are the two dialogue actions' verbs (the branch the player
// chooses between).  NOTHING else is called by this lane.
inline const char* EndingVerb(uint8_t e)
{
    switch (e) {
    case kEndQueen1:  return "Ending1Complete";
    case kEndQueen2:  return "Ending2CutsceneEnded";
    case kEndSipeha1: return "Banish";
    case kEndSipeha2: return "Eat";
    default:          return "";
    }
}

// The observation side: a ProcessEvent on one of these verbs IS the choice.  `Banish`/`Eat` are
// checked on the NPC's class, because `Eat` is a common verb name; `Ending1Complete` and
// `Ending2CutsceneEnded` are unique enough to accept anywhere.
inline uint8_t EndingFromVerb(const char* fn)
{
    if (!fn || !fn[0]) return kEndNone;
    if (!strcmp(fn, "Ending1Complete")) return kEndQueen1;
    if (!strcmp(fn, "Ending2CutsceneEnded")) return kEndQueen2;
    if (!strcmp(fn, "Banish")) return kEndSipeha1;
    if (!strcmp(fn, "Eat")) return kEndSipeha2;
    return kEndNone;
}

// The NSD key an ending writes.  Used to decide "this machine already chose" without guessing,
// and as the fallback publisher when no verb was observed (the ending can also be entered by a
// cutscene manager rather than by the boss).
inline uint8_t EndingFromFlag(const char* key, int32_t value)
{
    if (!key || !key[0] || value <= 0) return kEndNone;
    if (!strcmp(key, "Ending1Complete")) return kEndQueen1;
    if (!strcmp(key, "Ending2Complete")) return kEndQueen2;
    if (!strcmp(key, "Ending3Complete")) return kEndSipeha1;
    if (!strcmp(key, "Ending4Complete")) return kEndSipeha2;
    return kEndNone;
}

// Which boss class carries this branch.  `DoStateTransition` and the ending managers live on
// `Queen_BP`; the banish/eat verbs live on `Sipeha_NPC`.
enum Branch : uint8_t { kBranchNone = 0, kBranchQueen = 1, kBranchSipeha = 2 };

inline Branch EndingBranch(uint8_t e)
{
    if (e == kEndQueen1 || e == kEndQueen2) return kBranchQueen;
    if (e == kEndSipeha1 || e == kEndSipeha2) return kBranchSipeha;
    return kBranchNone;
}

// Does a class name belong to the branch?  A substring test on the measured class names
// (`Queen_BP_C`, `Sipeha_NPC_C`), so a Blueprint subclass of either still matches.
inline bool ClassInBranch(Branch b, const char* cls)
{
    if (b == kBranchNone || !cls || !cls[0]) return false;
    if (b == kBranchQueen)  return strstr(cls, "Queen_BP") != nullptr;
    if (b == kBranchSipeha) return strstr(cls, "Sipeha") != nullptr;
    return false;
}

// ---------------------------------------------------------------------------
// Should the receiver drive its own copy?
// ---------------------------------------------------------------------------
// Three facts decide it, and each has its own log line:
//   * this machine already wrote the flag  -> it chose too: DO NOTHING (an ending run twice)
//   * this machine holds no live copy      -> there is nothing to drive: report it
//   * otherwise                            -> run `EndingVerb()` on the copy
enum DriveVerdict : uint8_t {
    kDriveNow        = 0,
    kDriveSkipChosen = 1,
    kDriveNoCopy     = 2,
};

inline DriveVerdict ShouldDriveLocalEnding(bool localFlagAlready, bool haveLocalCopy)
{
    if (localFlagAlready) return kDriveSkipChosen;
    if (!haveLocalCopy) return kDriveNoCopy;
    return kDriveNow;
}

// ---------------------------------------------------------------------------
// The choice transmitter
// ---------------------------------------------------------------------------
// One choice per run.  `rev` orders repairs; 0 never means "revision 0" (same rule as the phase
// lane).  A repair goes out on a slow clock because a lost reliable send is possible but a
// resend storm is not worth it -- and the receiver is idempotent by its own flag.
static const uint32_t kEndRepairMs    = 5000;
static const uint32_t kEndRepairForMs = 45000;

struct ChoiceTx {
    uint32_t rev;
    uint32_t firstAt;
    uint32_t lastSentAt;
    uint8_t  ending;
    uint8_t  who;
    bool     chosen;

    void Reset() { memset(this, 0, sizeof(*this)); }

    // Record a choice observed locally.  Returns false when a DIFFERENT ending was already
    // chosen on this machine: two endings cannot both be this machine's, and the first one is
    // the one the player actually took.
    bool Begin(uint8_t e, uint8_t slot, uint32_t now)
    {
        if (e < kEndQueen1 || e > kEndSipeha2) return false;
        if (chosen) return (e == ending);
        chosen = true;
        ending = e;
        who = slot;
        firstAt = now ? now : 1;
        lastSentAt = 0;
        return true;
    }

    bool ShouldSend(uint32_t now) const
    {
        if (!chosen) return false;
        if (lastSentAt == 0) return true;
        if ((uint32_t)(now - firstAt) > kEndRepairForMs) return false;
        return (uint32_t)(now - lastSentAt) >= kEndRepairMs;
    }

    uint32_t NextRev() { return ++rev ? rev : (rev = 1); }
    bool Repairing() const { return chosen && lastSentAt != 0; }
};

// ---------------------------------------------------------------------------
// The wake-up: what the mod may do, and when
// ---------------------------------------------------------------------------
// The GATE comes first and is the actual fix:
//   * while a wake is pending (kWakeHoldMs after this machine's ending ended) the mod does not
//     drive level management at all -- no forcing the partner's set, no re-show pass.  The
//     game's own `FS_WaitForLevelManagement` is waiting for that queue, and a force is a new
//     entry in it.
// Then the rescue, in the order the game itself would do it:
//   * `kWakeLockCancelMs`  -- cancel the load lock with the game's own `CancelLockInPlaceForLoad`
//   * `kWakePostMs`        -- call the player's own `PostEndingWakeOnBeach`
//   * retries on `kWakeRetryMs` until `kWakeRetryMax`
//   * `kWakeStopFadeMs`    -- stop the held camera fade (the last resort that restores a picture)
enum WakeAction : uint8_t {
    kWakeNone        = 0,
    kWakeCancelLock  = 1,
    kWakePostWake    = 2,
    kWakeStopFade    = 3,
    kWakeSeen        = 4,   // the game's own wake-up played: close, nothing to do
};

static const uint32_t kWakeHoldMs     = 20000;   // level management is the game's
static const uint32_t kWakeLockCancelMs = 8000;
static const uint32_t kWakePostMs       = 9000;
static const uint32_t kWakeRetryMs      = 4000;
static const int      kWakeRetryMax     = 3;
static const uint32_t kWakeStopFadeMs   = 25000;
static const uint32_t kWakeGiveUpMs     = 45000;

struct WakeRules {
    bool     open;
    bool     sawWake;
    uint8_t  stage;          // 0 none · 1 lock cancelled · 2 post-wake run · 3 stop-fade done
    uint8_t  posts;          // how many PostEndingWakeOnBeach calls this module made
    uint32_t endedAt;

    void Reset() { memset(this, 0, sizeof(*this)); }

    void OnEndingEnded(uint32_t now)
    {
        // The FIRST ending wins: a second `Ending*` line (the partner's, or a duplicate local
        // ending) must not restart the hold and keep the level manager frozen for ever.
        if (open) return;
        open = true;
        sawWake = false;
        stage = 0;
        posts = 0;
        endedAt = now ? now : 1;
    }

    bool Pending(uint32_t now) const { return open && !sawWake && (uint32_t)(now - endedAt) < kWakeGiveUpMs; }

    // The gate: false = this module says "the game owns level management right now".
    bool LevelDriveAllowed(uint32_t now) const
    {
        return !(open && !sawWake && (uint32_t)(now - endedAt) < kWakeHoldMs);
    }

    // What to do this tick.  Pure: it reads `sawWake` and the clock, and advances the stage.
    WakeAction Step(uint32_t now, bool wakePlayed)
    {
        if (!open) return kWakeNone;
        if (wakePlayed && !sawWake) {
            sawWake = true;
            open = false;
            return kWakeSeen;
        }
        if (sawWake) { open = false; return kWakeNone; }
        uint32_t age = (uint32_t)(now - endedAt);
        if (age >= kWakeGiveUpMs) { open = false; return kWakeNone; }
        if (stage == 0 && age >= kWakeLockCancelMs) { stage = 1; return kWakeCancelLock; }
        if (stage == 1 && age >= kWakePostMs)       { stage = 2; ++posts; return kWakePostWake; }
        if (stage == 2 && posts < kWakeRetryMax &&
            age >= kWakePostMs + kWakeRetryMs * (uint32_t)posts) { ++posts; return kWakePostWake; }
        if (stage == 2 && age >= kWakeStopFadeMs)   { stage = 3; return kWakeStopFade; }
        return kWakeNone;
    }

    // The log has to say WHICH half brought the picture back, or "the game's own wake-up" is a
    // claim the run did not make.
    const char* Who() const
    {
        if (posts == 0) return "the game's own";
        return "after this module ran PostEndingWakeOnBeach";
    }
};

} // namespace ending
} // namespace qd
