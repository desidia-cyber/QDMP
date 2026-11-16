// test_bossphase.cpp -- boss phase authority, the arena rule, and the ending choice.
//
// THE PROPERTIES THAT MATTER, and why each is a unit test rather than a live run:
//
//   * a STALE revision must not move a copy backwards, but the SAME revision with a
//     different state must (that is how a copy that missed a transition heals).  The
//     Queen's chain is 0 -> 1 -> 2 -> 0, so a "higher wins" rule silently drops the
//     ending; a live run shows the symptom, not the ordering rule.
//   * the ending's BRANCH must match the boss.  A body that says a Queen ending while the
//     class is Sipeha is a decode of the wrong object, and applying it would start the
//     wrong cutscene manager on one machine only.
//   * a fight whose partner is ABSENT continues.  `DeathRespawn=1` makes that the ordinary
//     case (a death returns to the last rest without a world reload), and "the fight
//     cancelled itself because my partner died" is exactly the failure the rule forbids.
//   * the authority moves to the survivor when the owner goes silent, so a respawned
//     player does not leave a frozen statue with nobody driving its phase.
//   * every body refuses a nonsense value rather than driving a Blueprint graph with it.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "qdbossphase.h"
#include "qdwire.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, name)                                                          \
    do {                                                                           \
        if (cond) { ++g_pass; }                                                    \
        else { ++g_fail; printf("FAIL %s  (%s:%d)\n", name, __FILE__, __LINE__); } \
    } while (0)

using namespace qd;
using namespace qd::bossphase;

static void Key(uint32_t k[4], uint32_t a)
{
    k[0] = a; k[1] = a + 1; k[2] = a + 2; k[3] = a + 3;
}

// ---------------------------------------------------------------------------
// The revision rule
// ---------------------------------------------------------------------------
static void TestRevision()
{
    PhaseTrack t;
    t.Reset();
    Key(t.key, 0xC88FFC0C);

    // 0 means "no revision known" and must never be handed out as a revision.
    t.rev = 0;
    CHECK(NextRev(t) == 1, "rev/the first revision is 1, not 0");
    t.rev = 0xFFFFFFFFu;
    CHECK(NextRev(t) == 1, "rev/a wrapped revision returns to 1, never to 0");
    t.rev = 7;
    CHECK(NextRev(t) == 8, "rev/a revision is strictly increasing");
}

// ---------------------------------------------------------------------------
// Judge: apply / ignore / no copy
// ---------------------------------------------------------------------------
static void TestJudge()
{
    uint32_t k[4];
    Key(k, 0xC88FFC0C);
    PhaseBody b;
    memset(&b, 0, sizeof(b));
    memcpy(b.guid, k, 16);
    b.rev = 5;
    b.state = 1;
    b.sub = 0;
    b.flags = kPhaseAuthority | kPhaseTransition;

    // No local copy: nothing to drive, and nothing invented.
    CHECK(Judge(b, false, 0, -1, -1) == kNoCopy, "judge/no local copy is reported, not applied");

    // A brand-new copy takes the owner's state (joining a fight in progress).
    CHECK(Judge(b, true, 0, -1, -1) == kApplyPhase, "judge/a new copy takes the owner's state");

    // Older revision: stale, whatever the states are.
    CHECK(Judge(b, true, 6, 0, 0) == kIgnoreStale, "judge/an older revision is stale");
    CHECK(Judge(b, true, 6, 9, 9) == kIgnoreStale, "judge/an older revision is stale even when the states differ");

    // The same revision with the same state is a repair: a no-op.
    CHECK(Judge(b, true, 5, 1, 0) == kIgnoreStale, "judge/the same revision and state is a repair no-op");

    // THE HEAL: the same revision, a DIFFERENT state.  The owner is restating its
    // authority, so the copy is wrong and is corrected.
    CHECK(Judge(b, true, 5, 0, 0) == kApplyPhase, "judge/the same revision with a different state is applied (the heal)");
    CHECK(Judge(b, true, 5, 1, 1) == kApplyPhase, "judge/the same revision with a different sub-state is applied");

    // A wrapped revision must not look infinitely old.
    PhaseBody wrap = b;
    wrap.rev = 1;
    CHECK(Judge(wrap, true, 0xFFFFFFFEu, 0, 0) == kApplyPhase,
          "judge/a wrapped revision is newer, not four billion milliseconds old");
}

// ---------------------------------------------------------------------------
// The Queen's chain, end to end
// ---------------------------------------------------------------------------
static void TestQueenChain()
{
    uint32_t k[4];
    Key(k, 0xC88FFC0C);
    // The measured chain: 0 (phase 1) -> 1 (the transition she got stuck in) ->
    // 2 (her second phase / the nude form) -> 0 for the ending.  A "keep the highest"
    // rule would refuse the last step; the revision is what orders them.
    const int32_t chain[] = {0, 1, 2, 0};
    uint32_t rev = 0;
    int32_t localState = -1;
    uint32_t appliedRev = 0;
    int applied = 0;
    for (int i = 0; i < 4; ++i) {
        PhaseBody b;
        memset(&b, 0, sizeof(b));
        memcpy(b.guid, k, 16);
        b.rev = ++rev;
        b.state = chain[i];
        b.sub = 0;
        b.flags = kPhaseAuthority | kPhaseTransition;
        if (Judge(b, true, appliedRev, localState, 0) == kApplyPhase) {
            appliedRev = b.rev;
            localState = b.state;
            ++applied;
        }
    }
    CHECK(applied == 4, "queen/all four steps of the chain are applied, including the return to 0");
    CHECK(localState == 0, "queen/the copy ends at the ending's state, not at the highest one seen");
}

// ---------------------------------------------------------------------------
// Arena: does the fight continue when a player is absent?
// ---------------------------------------------------------------------------
static void TestArenaPolicy()
{
    // Both present, boss alive: an ordinary fight.
    CHECK(Decide(true, kPlayerHost | kPlayerPeer, kArenaClosed) == kFightContinueBoth,
          "arena/both players present is an ordinary fight");

    // THE RULE: a death and a respawn do not cancel a fight.
    CHECK(Decide(true, kPlayerHost, kArenaClosed) == kFightContinueAbsent,
          "arena/one player absent does NOT cancel a running fight");
    CHECK(Decide(true, kPlayerPeer, kArenaClosed) == kFightContinueAbsent,
          "arena/the other player absent does NOT cancel it either");

    // Nobody has arrived yet: the fight has not started, so there is nothing to continue.
    CHECK(Decide(true, 0, kArenaIdle) == kFightHoldForArrival,
          "arena/an empty arena with a live boss holds for arrival");

    // The boss is dead: the fight is over whatever the barrier says.
    CHECK(Decide(false, kPlayerHost, kArenaClosed) == kFightFinished,
          "arena/a dead boss ends the fight even with the barrier still closed");

    // The barrier actions are idempotent statements about the door lane.
    CHECK(BarrierFor(kArenaClosed, true) == kBarrierNone, "arena/an already-closed fight asks for nothing");
    CHECK(BarrierFor(kArenaOpen, true) == kBarrierClose, "arena/a started fight asks the door lane to close");
    CHECK(BarrierFor(kArenaDone, false) == kBarrierOpen, "arena/a finished fight asks the door lane to open");
    CHECK(BarrierFor(kArenaClosed, false) == kBarrierOpen, "arena/a dead boss opens the barrier");
    CHECK(BarrierFor(kArenaIdle, true) == kBarrierNone, "arena/an idle arena asks for nothing");
}

// ---------------------------------------------------------------------------
// Authority
// ---------------------------------------------------------------------------
static void TestAuthority()
{
    const uint32_t now = 100000;
    // The machine that runs the body is the authority.
    CHECK(IsAuthority(true, false, 0, now), "auth/the machine that runs the boss is the authority");
    // A constructed copy never is, even if something says it runs.
    CHECK(!IsAuthority(true, true, 0, now), "auth/a mirror is never the authority");
    // A follower with a live owner is not the authority.
    CHECK(!IsAuthority(false, false, now - 1000, now), "auth/a follower with a live owner follows");
    // A follower whose owner has gone silent takes over (the respawned-partner case).
    CHECK(IsAuthority(false, false, now - kOwnerSilentMs - 1, now),
          "auth/the survivor takes the phase authority when the owner goes silent");
    // Silence exactly at the threshold is not yet silence.
    CHECK(!IsAuthority(false, false, now - kOwnerSilentMs, now),
          "auth/silence is not assumed at the exact threshold");
    // A follower that has never heard from the owner does not invent authority.
    CHECK(!IsAuthority(false, false, 0, now), "auth/no word from the owner is not authority");
}

// ---------------------------------------------------------------------------
// Publishing cadence
// ---------------------------------------------------------------------------
static void TestPublish()
{
    PhaseTrack t;
    t.Reset();
    Key(t.key, 0xC88FFC0C);
    t.state = 0;
    t.sub = 0;

    // A transition always publishes at once -- a lost one is a stuck boss.
    CHECK(ShouldPublish(t, 1, 0, true, 5000), "pub/a transition publishes immediately");
    // A changed steady state publishes.
    CHECK(ShouldPublish(t, 1, 0, false, 5000), "pub/a changed state publishes");
    CHECK(ShouldPublish(t, 0, 1, false, 5000), "pub/a changed sub-state publishes");

    t.lastSendAt = 5000;
    // Unchanged inside the repair window: nothing.
    CHECK(!ShouldPublish(t, 0, 0, false, 6000), "pub/no change inside the repair window is silent");
    // Unchanged after it: the repair.
    CHECK(ShouldPublish(t, 0, 0, false, 5000 + kPhaseRepairMs),
          "pub/an unchanged state repairs on the clock");
}

// ---------------------------------------------------------------------------
// The wire
// ---------------------------------------------------------------------------
// The wire
// ---------------------------------------------------------------------------
// The codecs live in qdwire.h (one spelling of the layout); these checks are about the
// VALUES each body must refuse, and the exact size both sides depend on.
static void TestWire()
{
    uint32_t k[4];
    Key(k, 0xC88FFC0C);

    PhaseBody p;
    memset(&p, 0, sizeof(p));
    memcpy(p.guid, k, 16);
    p.rev = 12;
    p.state = 2;
    p.sub = 1;
    p.flags = kPhaseAuthority | kPhaseTransition | kPhaseAnimating;
    uint8_t buf[256];
    wire::Writer w(buf, sizeof(buf));
    wire::WriteBossPhase(w, p);
    CHECK(w.ok, "wire/the phase body writes");
    CHECK(w.len == kPhaseBodyBytes, "wire/the phase body is the documented size");
    wire::Reader r(buf, w.len);
    PhaseBody out;
    CHECK(wire::ReadBossPhase(r, &out), "wire/and reads back");
    CHECK(memcmp(out.guid, k, 16) == 0, "wire/the key round-trips");
    CHECK(out.rev == 12 && out.state == 2 && out.sub == 1, "wire/rev, state and sub round-trip");
    CHECK(out.flags == p.flags, "wire/the flags round-trip");
    CHECK(ValidPhase(out), "wire/a well-formed body passes its rule");

    // -1 is the legitimate "unreadable on the sender" value: a follower must not read it as
    // state 0, which would re-run phase 1.
    PhaseBody unknown = p;
    unknown.state = -1;
    unknown.sub = -1;
    CHECK(ValidPhase(unknown), "wire/-1 is a legal 'unreadable' ordinal");

    // A nonsense ordinal is refused by the rule, not driven into a Blueprint graph.
    PhaseBody bad = p;
    bad.state = 9999;
    CHECK(!ValidPhase(bad), "wire/an out-of-range state is refused");

    // A zero key names no boss.
    PhaseBody nokey = p;
    memset(nokey.guid, 0, 16);
    CHECK(!ValidPhase(nokey), "wire/a zero key is refused");

    // A truncated body is a desynchronised stream, not a smaller one.
    wire::Reader r1(buf, w.len - 1);
    PhaseBody got;
    memset(&got, 0, sizeof(got));
    CHECK(!wire::ReadBossPhase(r1, &got), "wire/a truncated body does not decode");

    // ---- arena ----
    ArenaBody a;
    memset(&a, 0, sizeof(a));
    memcpy(a.guid, k, 16);
    a.rev = 3;
    a.arena = kArenaClosed;
    a.present = kPlayerHost;
    uint8_t ab[256];
    wire::Writer aw(ab, sizeof(ab));
    wire::WriteBossArena(aw, a);
    CHECK(aw.ok && aw.len == kArenaBodyBytes, "wire/the arena body is the documented size");
    wire::Reader ar(ab, aw.len);
    ArenaBody ao;
    CHECK(wire::ReadBossArena(ar, &ao), "wire/and reads back");
    CHECK(ao.arena == kArenaClosed && ao.present == kPlayerHost && ao.rev == 3,
          "wire/the arena phase, presence and rev round-trip");
    CHECK(ValidArena(ao), "wire/a well-formed arena body passes its rule");

    // One player absent is a LEGAL state: that is the whole point of the body.
    ArenaBody absent = a;
    absent.present = kPlayerPeer;
    CHECK(ValidArena(absent), "wire/one player absent is a legal arena state");
    ArenaBody empty = a;
    empty.present = 0;
    empty.arena = kArenaIdle;
    CHECK(ValidArena(empty), "wire/nobody present is legal too (an idle arena holds)");

    ArenaBody badArena = a;
    badArena.arena = 9;
    CHECK(!ValidArena(badArena), "wire/an unknown arena phase is refused");
    ArenaBody badPresent = a;
    badPresent.present = 0x80;
    CHECK(!ValidArena(badPresent), "wire/an unknown player bit is refused");

    // ---- ending ----
    EndBody e;
    memset(&e, 0, sizeof(e));
    memcpy(e.guid, k, 16);
    e.rev = 1;
    e.ending = kEndQueen1;
    e.who = kPlayerPeer;
    uint8_t eb[256];
    wire::Writer ew(eb, sizeof(eb));
    wire::WriteBossEnd(ew, e);
    CHECK(ew.ok && ew.len == kEndBodyBytes, "wire/the ending body is the documented size");
    wire::Reader er(eb, ew.len);
    EndBody eo;
    CHECK(wire::ReadBossEnd(er, &eo), "wire/and reads back");
    CHECK(eo.ending == kEndQueen1 && eo.who == kPlayerPeer,
          "wire/the ending choice and its maker round-trip");
    CHECK(ValidEnd(eo), "wire/a well-formed ending body passes its rule");

    CHECK(strcmp(EndingFlag(kEndQueen1), "Ending1Complete") == 0, "wire/queen 1 names its own flag");
    CHECK(strcmp(EndingFlag(kEndQueen2), "Ending2Complete") == 0, "wire/queen 2 names its own flag");
    CHECK(strcmp(EndingFlag(kEndSipeha1), "Ending3Complete") == 0, "wire/sipeha 1 names its own flag");
    CHECK(strcmp(EndingFlag(kEndSipeha2), "Ending4Complete") == 0, "wire/sipeha 2 names its own flag");
    CHECK(strcmp(EndingFlag(kEndNone), "") == 0, "wire/no ending names no flag");

    // All four are expressible: the four flags are four independent writes in the content.
    for (uint8_t i = kEndQueen1; i <= kEndSipeha2; ++i) {
        EndBody b = e;
        b.ending = i;
        CHECK(ValidEnd(b), "wire/every one of the four endings is a legal choice");
    }

    // THE BRANCH MUST MATCH THE BOSS.
    CHECK(EndingMatchesBoss(kEndQueen1, "Queen_BP_C"), "ending/a Queen ending matches the Queen");
    CHECK(EndingMatchesBoss(kEndQueen2, "Queen_BP_C"), "ending/the other Queen ending matches too");
    CHECK(!EndingMatchesBoss(kEndQueen1, "Sipeha_BP_C"), "ending/a Queen ending does not match Sipeha");
    CHECK(EndingMatchesBoss(kEndSipeha1, "Sipeha_NPC_C"), "ending/a Sipeha ending matches Sipeha");
    CHECK(!EndingMatchesBoss(kEndSipeha2, "Queen_BP_C"), "ending/a Sipeha ending does not match the Queen");

    // A body with no ending, or a maker who is neither player, is refused.
    EndBody noEnd = e;
    noEnd.ending = kEndNone;
    CHECK(!ValidEnd(noEnd), "wire/no ending is refused");
    EndBody badWho = e;
    badWho.who = 7;
    CHECK(!ValidEnd(badWho), "wire/an unknown maker is refused");
}


// ---------------------------------------------------------------------------
// The track set
// ---------------------------------------------------------------------------
static void TestTrackSet()
{
    TrackSet s;
    uint32_t a[4], b[4], c[4];
    Key(a, 0x1000);
    Key(b, 0x2000);
    Key(c, 0x3000);

    CHECK(s.Used() == 0, "track/a fresh set is empty");
    PhaseTrack* ta = s.Touch(a);
    CHECK(ta && s.Used() == 1, "track/touching a key adds it");
    CHECK(s.Touch(a) == ta, "track/touching the same key returns the same track");
    s.Touch(b);
    s.Touch(c);
    CHECK(s.Used() == 3, "track/three keys make three tracks");
    CHECK(s.Find(b) != nullptr && s.Find(a) != s.Find(b), "track/keys do not collide");
    PhaseTrack missing;
    Key(missing.key, 0x9999);
    CHECK(s.Find(missing.key) == nullptr, "track/an unknown key has no track");

    // A track reset leaves no state behind.
    ta->Reset();
    CHECK(ta->state == -1 && ta->sub == -1 && ta->rev == 0 && ta->appliedRev == 0,
          "track/a reset track has no state, no revision and an unreadable ordinal");

    // Overfill: a fight has a handful of bosses, but a long world must not corrupt the set.
    for (uint32_t i = 0; i < 64; ++i) {
        uint32_t k[4];
        Key(k, 0x4000 + i * 4);
        s.Touch(k);
    }
    CHECK(s.Used() == kTrackMax, "track/an overfilled set stays at its bound");
}

int main()
{
    TestRevision();
    TestJudge();
    TestQueenChain();
    TestArenaPolicy();
    TestAuthority();
    TestPublish();
    TestWire();
    TestTrackSet();
    printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
