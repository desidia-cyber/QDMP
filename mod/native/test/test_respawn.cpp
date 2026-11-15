// test_respawn.cpp -- the death lane's pure half (qdrespawn.h).
//
// THE PROPERTIES THAT MATTER, and why each is a number rather than a comment:
//
//   * the penalty LADDER.  Each death costs 10 % of the base maximum and stacks down to a
//     floor: 100 / 90 / 80 / ... / 10, and no further.  100 is not reachable after a
//     death, and 0 is never reachable at all (a maximum of zero would make the next hit
//     kill through any amount of healing).  A rest is the only thing that clears it.
//
//   * a death counted TWICE.  The death pose and the health read both report the same
//     death; counting both would cost two steps for one death.  And a respawn is followed
//     by a frame in which the game still says "dead" -- that frame is not another death.
//
//   * the RESPONSE IS A MULTIPLIER over the game's own computed maximum, so the game
//     recomputing its stats cannot erase it, and the engine half applying it twice cannot
//     double it (the value scaled is the game's, not ours: ScaledMax(400,2)=320 and
//     ScaledMax(320,0)=320 are the two halves of that statement).
//
//   * a rest point is per PLAYER and must be USABLE.  A remembered NaN or a never-set
//     point is refused, so the caller falls back to the save's checkpoint instead of
//     teleporting the player to (nan, nan, nan); and a save-derived point never overwrites
//     a rest this machine watched happen.
//
//   * the WIRE bodies round-trip and refuse what would become a wrong state: a stack and
//     a percentage that disagree, a NaN position, an `hp` above `hp_max`, a rest body that
//     is not "0 deaths, 100 %".
//
//   * the ORDERING gate: a duplicate is dropped (a second UP would teleport the puppet
//     back), an OLDER body is dropped, a newer one is applied, and the sequence is a
//     signed byte so 255 -> 0 is newer, not 255 steps older.
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "qdrespawn.h"
#include "qdwire.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, name)                                                          \
    do {                                                                           \
        if (cond) { ++g_pass; }                                                    \
        else { ++g_fail; printf("FAIL %s  (%s:%d)\n", name, __FILE__, __LINE__); } \
    } while (0)

using namespace qd;
using namespace qd::respawn;

// ---------------------------------------------------------------------------
static void TestPenaltyLadder()
{
    CHECK(PctFor(0) == 100, "no deaths is 100 %");
    CHECK(PctFor(1) == 90, "one death is 90 %");
    CHECK(PctFor(2) == 80, "two deaths is 80 %");
    CHECK(PctFor(3) == 70, "three deaths is 70 %");
    CHECK(PctFor(8) == 20, "eight deaths is 20 %");
    CHECK(PctFor(9) == 10, "nine deaths reaches the floor");
    CHECK(PctFor(10) == 10, "a tenth death cannot go below the floor");
    CHECK(PctFor(1000) == 10, "an absurd count cannot go below the floor");
    CHECK(PctFor(-5) == 100, "a negative count is no deaths");
    int cap = kMaxDeaths;
    CHECK(cap == 9, "the stack caps at the step that reaches the floor");

    CHECK(StacksAfterDeath(0) == 1, "first death stacks to 1");
    CHECK(StacksAfterDeath(8) == 9, "eighth death stacks to 9");
    CHECK(StacksAfterDeath(9) == 9, "the stack does not grow past the floor");
    CHECK(StacksAfterDeath(-1) == 1, "a negative stack still dies once");

    // The maximum's value at the base of 400 used by the rig's own player (measured
    // `god: health refilled 140.0 -> 400.0`, LIVE-TEST-LOG Session 26).
    CHECK(ScaledMax(400.0f, 0) == 400.0f, "400 at full health");
    CHECK(ScaledMax(400.0f, 1) == 360.0f, "400 -> 360 after one death");
    CHECK(ScaledMax(400.0f, 2) == 320.0f, "400 -> 320 after two");
    CHECK(ScaledMax(400.0f, 9) == 40.0f, "400 floors at 40 (10 %)");
    CHECK(ScaledMax(0.0f, 2) == 0.0f, "an unusable base maximum is not scaled into a heal");
    CHECK(ScaledMax(-100.0f, 0) == 0.0f, "a negative base maximum is refused, not negated");

    // THE TWO HALVES.  Scaling the game's own number and then scaling ours would be a
    // double application; scaling the game's number and reporting the result is not.
    CHECK(ScaledMax(ScaledMax(400.0f, 0), 2) == 320.0f, "scaling the game's own max is the rule");
    CHECK(ScaledMax(320.0f, 0) == 320.0f, "a rested player's already-penalised max is not penalised again");
}

// ---------------------------------------------------------------------------
static void TestDeathState()
{
    DeathState s;
    s.Reset();
    CHECK(s.Deaths() == 0 && s.Pct() == 100, "a fresh state is unpenalised");
    CHECK(!s.Down(), "a fresh state is not down");
    CHECK(!s.Due(1000), "a fresh state does not respawn");
    CHECK(!UsablePoint(s.Rest()), "a fresh state has no rest point");

    CHECK(s.NoteDeath(1000), "the first death is a death");
    CHECK(s.Down(), "the player is down");
    CHECK(s.Deaths() == 1 && s.Pct() == 90, "one death costs 10 %");
    CHECK(!s.NoteDeath(1100), "the same death reported again is not a second death");
    CHECK(s.Deaths() == 1, "and it did not cost a second 10 %");

    CHECK(!s.Due(1000 + kPoseMs - 1), "the pose is not over before it is over");
    CHECK(s.Due(1000 + kPoseMs), "the pose is over at kPoseMs");

    s.NoteRespawn(1000 + kPoseMs);
    CHECK(!s.Down(), "a respawn clears the down flag");
    CHECK(s.Deaths() == 1, "a respawn does NOT clear the penalty");

    // The frame after a respawn still reads zero health on some builds.  Inside the settle
    // window that is not a new death; outside it, it is.
    CHECK(!s.NoteDeath(1000 + kPoseMs + 100), "a zero health right after the respawn is not a death");
    CHECK(s.NoteDeath(1000 + kPoseMs + kSettleMs), "a death after the settle window is");
    CHECK(s.Deaths() == 2 && s.Pct() == 80, "the second real death is 80 %");

    // A rest is the only thing that clears the stack, and it is also a rest point.
    Point p;
    memset(&p, 0, sizeof(p));
    p.have = true;
    p.px = 4010.0f; p.py = -1735.0f; p.pz = 51.0f; p.yaw = -95.0f;
    strcpy(p.name, "Checkpoint17/100_GR");
    int cleared = s.NoteRest(p, 90000);
    CHECK(cleared == 2, "the rest reports the stacks it cleared");
    CHECK(s.Deaths() == 0 && s.Pct() == 100, "a rest restores 100 %");
    CHECK(!s.Down(), "a rest also ends a down state");
    CHECK(UsablePoint(s.Rest()), "the rest point is usable");
    CHECK(s.Rest().px == 4010.0f && strcmp(s.Rest().name, "Checkpoint17/100_GR") == 0,
          "the rest point is the place the player rested");

    // The NEXT death comes back to that point, penalised once from 100 %.
    CHECK(s.NoteDeath(91000), "a death after the rest is a death");
    CHECK(s.Deaths() == 1 && s.Pct() == 90, "the ladder restarts after a rest");
    CHECK(UsablePoint(s.Rest()), "the rest point survives the death");
}

// ---------------------------------------------------------------------------
static void TestRestoredStacks()
{
    // A stack count restored from the sidecar is history, not a live death: the player must
    // not be DOWN and no respawn may be armed, or a relaunch would teleport a player who
    // simply loaded a save.
    DeathState s;
    s.Reset();
    s.RestoreStacks(2);
    CHECK(s.Deaths() == 2 && s.Pct() == 80, "restored stacks carry the penalty");
    CHECK(!s.Down(), "a restored stack is not a live death");
    CHECK(!s.Due(100000), "a restored stack arms no respawn");
    CHECK(s.NoteDeath(100000), "the next real death is counted");
    CHECK(s.Deaths() == 3 && s.Pct() == 70, "and it stacks on top of the restored ones");

    s.RestoreStacks(999);
    CHECK(s.Deaths() == kMaxDeaths && s.Pct() == 10, "a corrupted count is clamped to the floor");
    s.RestoreStacks(-4);
    CHECK(s.Deaths() == 0 && s.Pct() == 100, "a negative count is no penalty");
}

// ---------------------------------------------------------------------------
static void TestRestPointRules()
{
    DeathState s;
    s.Reset();
    Point bad;
    memset(&bad, 0, sizeof(bad));
    bad.have = true;
    bad.px = NAN;
    bad.py = 0.0f;
    bad.pz = 0.0f;
    CHECK(!UsablePoint(bad), "a NaN rest point is refused");
    bad.px = 0.0f;
    bad.py = 1.0e9f;
    CHECK(!UsablePoint(bad), "a point outside any world is refused");

    // MEASURED (second live run): this game keeps its menu maps inside the same persistent
    // world, so a `Checkpoint` placed in `VideoConfigMap` resolves, is near the player, and
    // has coordinates -- but a respawn must never put a player there.
    CHECK(NameLooksLikeMenuMap("//Game/UI/Menus/OptionsMenu/VideoConfigMap/VideoConfigMap/PersistentLevel/Checkpoint"),
          "the menu map's own checkpoint path is recognised");
    CHECK(!NameLooksLikeMenuMap("//Game/RuinsLevel/Maps/700_SZ_GR/700_SZ_GR/PersistentLevel/Checkpoint17"),
          "a gameplay checkpoint path is not");
    Point menu;
    memset(&menu, 0, sizeof(menu));
    menu.have = true;
    menu.px = 6677.0f; menu.py = 36453.0f; menu.pz = 885.0f;
    strcpy(menu.name, "//Game/UI/Menus/OptionsMenu/VideoConfigMap/VideoConfigMap/PersistentLevel/Checkpoint");
    CHECK(!UsablePoint(menu), "a rest point in a menu map is refused");

    // A save-derived point is only a FALLBACK: it must not overwrite a watched rest.
    Point watched;
    memset(&watched, 0, sizeof(watched));
    watched.have = true;
    watched.px = 1.0f; watched.py = 2.0f; watched.pz = 3.0f;
    strcpy(watched.name, "Checkpoint19/100_GR");
    s.NoteRest(watched, 500);

    Point fromSave;
    memset(&fromSave, 0, sizeof(fromSave));
    fromSave.have = true;
    fromSave.px = 900.0f; fromSave.py = 900.0f; fromSave.pz = 900.0f;
    strcpy(fromSave.name, "Checkpoint17/100_GR");
    s.NoteSavePoint(fromSave);
    CHECK(s.Rest().px == 1.0f, "the save's checkpoint does not overwrite a watched rest");

    // ... and it IS used when there is nothing better.
    DeathState t;
    t.Reset();
    t.NoteSavePoint(fromSave);
    CHECK(UsablePoint(t.Rest()), "the save's checkpoint is the fallback");
    CHECK(t.Rest().px == 900.0f, "and it is the point the save named");
}

// ---------------------------------------------------------------------------
static DownBody SampleDown()
{
    DownBody b;
    memset(&b, 0, sizeof(b));
    b.deaths = 2;
    b.pct = 80;
    b.seq = 7;
    b.px = -27846.5f;
    b.py = -15403.25f;
    b.pz = -15339.0f;
    return b;
}

static UpBody SampleUp()
{
    UpBody b;
    memset(&b, 0, sizeof(b));
    b.deaths = 2;
    b.pct = 80;
    b.seq = 8;
    b.px = 4010.0f;
    b.py = -1735.0f;
    b.pz = 51.0f;
    b.yaw = -95.0f;
    b.hp = 320;
    b.hp_max = 320;
    strcpy(b.name, "Checkpoint17/100_GR");
    return b;
}

static RestBody SampleRest()
{
    RestBody b;
    memset(&b, 0, sizeof(b));
    b.deaths = 0;
    b.pct = 100;
    b.seq = 9;
    b.px = 4010.0f;
    b.py = -1735.0f;
    b.pz = 51.0f;
    strcpy(b.name, "Checkpoint17/100_GR");
    return b;
}

static void TestWire()
{
    uint8_t buf[512];

    {
        DownBody a = SampleDown();
        wire::Writer w(buf, (int)sizeof(buf));
        CHECK(EncodeDown(w, a), "encode a valid DOWN");
        wire::Reader r(buf, w.len);
        DownBody b;
        CHECK(DecodeDown(r, &b), "decode it back");
        CHECK(b.deaths == 2 && b.pct == 80 && b.seq == 7, "bytes survive");
        CHECK(fabsf(b.px - a.px) <= 0.2f && fabsf(b.py - a.py) <= 0.2f && fabsf(b.pz - a.pz) <= 0.2f,
              "position survives within a quantisation step");
        CHECK(r.pos == w.len, "decode consumed exactly the body");
    }
    {
        UpBody a = SampleUp();
        wire::Writer w(buf, (int)sizeof(buf));
        CHECK(EncodeUp(w, a), "encode a valid UP");
        wire::Reader r(buf, w.len);
        UpBody b;
        CHECK(DecodeUp(r, &b), "decode it back");
        CHECK(b.deaths == 2 && b.pct == 80, "bytes survive");
        CHECK(b.hp == 320 && b.hp_max == 320, "the restored HP survives");
        CHECK(fabsf(b.yaw - a.yaw) <= 0.2f, "yaw survives within a quantisation step");
        CHECK(strcmp(b.name, "Checkpoint17/100_GR") == 0, "the rest point's name survives");
        CHECK(r.pos == w.len, "decode consumed exactly the body");
    }
    {
        RestBody a = SampleRest();
        wire::Writer w(buf, (int)sizeof(buf));
        CHECK(EncodeRest(w, a), "encode a valid REST");
        wire::Reader r(buf, w.len);
        RestBody b;
        CHECK(DecodeRest(r, &b), "decode it back");
        CHECK(b.deaths == 0 && b.pct == 100 && strcmp(b.name, "Checkpoint17/100_GR") == 0,
              "a REST is 0 deaths and 100 %");
        CHECK(r.pos == w.len, "decode consumed exactly the body");
    }
}

static void TestWireRefusals()
{
    uint8_t buf[512];
    {
        DownBody a = SampleDown();
        a.pct = 100;                       // disagrees with 2 deaths
        wire::Writer w(buf, (int)sizeof(buf));
        CHECK(!EncodeDown(w, a), "a stack that disagrees with its percentage is not encoded");
    }
    {
        DownBody a = SampleDown();
        a.deaths = kMaxDeaths + 1;
        a.pct = 10;
        wire::Writer w(buf, (int)sizeof(buf));
        CHECK(!EncodeDown(w, a), "a stack past the floor is not encoded");
    }
    {
        DownBody a = SampleDown();
        a.px = NAN;
        wire::Writer w(buf, (int)sizeof(buf));
        CHECK(!EncodeDown(w, a), "a NaN position is not encoded");
    }
    {
        UpBody a = SampleUp();
        a.hp = 400;
        a.hp_max = 320;
        wire::Writer w(buf, (int)sizeof(buf));
        CHECK(EncodeUp(w, a), "encode an hp above hp_max to test the DECODER");
        wire::Reader r(buf, w.len);
        UpBody b;
        CHECK(!DecodeUp(r, &b), "a decoder refuses hp above hp_max");
    }
    {
        // A REST body that carries a stack would clear a penalty on the receiver that the
        // sender still has -- the exact divergence this lane must not create.
        RestBody a = SampleRest();
        a.deaths = 3;
        a.pct = 70;
        wire::Writer w(buf, (int)sizeof(buf));
        CHECK(EncodeRest(w, a), "encode a contradictory REST to test the DECODER");
        wire::Reader r(buf, w.len);
        RestBody b;
        CHECK(!DecodeRest(r, &b), "a decoder refuses a REST that is not 0 deaths / 100 %");
    }
    {
        // A DOWN body's bytes are not an UP body: the layouts differ (7 bytes against a
        // 3 + 12 + 2 + 4 + a string), so the decoder refuses the short body rather than
        // reading a position out of an unrelated field.
        DownBody a = SampleDown();
        wire::Writer w(buf, (int)sizeof(buf));
        CHECK(EncodeDown(w, a), "encode a DOWN to test a layout mismatch");
        wire::Reader r(buf, w.len);
        UpBody b;
        CHECK(!DecodeUp(r, &b), "a DOWN body is not decoded as an UP");
    }
    {
        uint8_t junk[4] = {1, 90, 1, 0};
        wire::Reader r(junk, (int)sizeof(junk));
        DownBody b;
        CHECK(!DecodeDown(r, &b), "a truncated body is refused, not half-applied");
    }
}

// ---------------------------------------------------------------------------
static void TestSeqGate()
{
    SeqGate g;
    g.Reset();
    CHECK(g.Accept(1), "the first body is applied");
    CHECK(!g.Accept(1), "a duplicate is dropped");
    CHECK(g.Accept(2), "a newer body is applied");
    CHECK(!g.Accept(1), "an older body is dropped");
    CHECK(g.Accept(2) == false, "the duplicate of the newest is dropped");

    // Signed: 255 -> 0 is NEWER (the counter wrapped), not 255 steps older.
    SeqGate w;
    w.Reset();
    CHECK(w.Accept(255), "255 first");
    CHECK(w.Accept(0), "0 after 255 is newer, not older");
    CHECK(w.Accept(254) == false, "254 after 0 is older");
}

// ---------------------------------------------------------------------------
int main()
{
    TestPenaltyLadder();
    TestDeathState();
    TestRestoredStacks();
    TestRestPointRules();
    TestWire();
    TestWireRefusals();
    TestSeqGate();

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
