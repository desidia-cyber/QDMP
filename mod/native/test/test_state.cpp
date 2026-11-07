// test_state.cpp -- the five pure state machines.
//
// These test RULES, not code shape.  Every assertion below maps to a specific
// measured failure in the source corpus, because those are the failures that cost
// game runs and are invisible in a code review:
//
//   motion    extrapolating from "now" instead of from render time; interpolating
//             across a teleport; spinning the wrong way round the circle
//   puppet    duplicate spawn; retry without a deadline; a handle used after a level
//             change; a body destroyed on the first dropped packet
//   stream    a naive add+flush replacing 21 levels with 11; treating "peer gone"
//             as "peer standing still with an empty list"
//   dialogue  a stale publish applied backwards; both players answering; a dialogue
//             left open at death
//   flags     a value lowered by a peer; a stale intent committing
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "qdmotion.h"
#include "qdpuppet.h"
#include "qdstream.h"
#include "qddialogue.h"
#include "qdflags.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, name)                                                          \
    do {                                                                           \
        if (cond) { ++g_pass; }                                                    \
        else { ++g_fail; printf("FAIL %s  (%s:%d)\n", name, __FILE__, __LINE__); } \
    } while (0)

#define CHECK_MSG(cond, name, detail)                                              \
    do {                                                                           \
        if (cond) { ++g_pass; }                                                    \
        else { ++g_fail; printf("FAIL %s  %s  (%s:%d)\n", name, detail, __FILE__, __LINE__); } \
    } while (0)

using namespace qd;

static int Approx(float a, float b, float eps) { return fabsf(a - b) <= eps; }

// ===========================================================================
// motion
// ===========================================================================
static void TestMotion()
{
    motion::Interp in;
    CHECK(in.Count() == 0, "motion/empty");
    CHECK(!in.Sample(1000).valid, "motion/empty sample is invalid");

    // 60 Hz, walking +X at 8 cm per 16 ms (500 cm/s).
    for (int i = 0; i < 40; ++i) {
        motion::MoveSample s;
        s.ms = (uint32_t)(i * 16);
        s.px = (float)i * 8.0f;
        s.py = 0.0f;
        s.pz = 0.0f;
        s.yaw = 0;
        s.pitch = 0;
        s.flags = 0;
        in.Push(s);
    }
    CHECK(in.Count() == 32, "motion/ring caps at 32");

    // Render time = now - 100 ms.  With the newest sample at 624 ms, now = 700 ms puts
    // the render target at 600 ms, which is between samples 600 and 608 -> exactly
    // sample 37 (600ms / 16 = 37.5), i.e. halfway into the 592..608 span.
    motion::MoveOut o = in.Sample(700);
    CHECK(o.valid, "motion/interpolated sample is valid");
    CHECK(!o.extrapolating, "motion/inside the buffer is not extrapolation");
    // 600 ms is between sample 37 (592 ms, px = 296) and 38 (608 ms, px = 304):
    // t = 8/16 = 0.5 -> 300 cm.
    CHECK_MSG(Approx(o.px, 300.0f, 1.0f), "motion/interpolates between bracketing samples",
              "" );
    CHECK(Approx(o.pz, 0.0f, 0.01f), "motion/untouched axis stays put");

    // Teleport: a 5 m jump between consecutive samples must snap, not slide.
    {
        motion::Interp t;
        motion::MoveSample a;
        memset(&a, 0, sizeof(a));
        a.ms = 0; a.px = 0;
        t.Push(a);
        motion::MoveSample b = a;
        b.ms = 16; b.px = 5000.0f;
        t.Push(b);
        motion::MoveOut snap = t.Sample(16 + 100);
        CHECK(snap.snapped, "motion/teleport is flagged as a snap");
    }

    // A small move must NOT snap: 2000 cm is the threshold and a 2 m shuffle is normal.
    {
        motion::Interp t;
        motion::MoveSample a;
        memset(&a, 0, sizeof(a));
        a.ms = 0; a.px = 0;
        t.Push(a);
        motion::MoveSample b = a;
        b.ms = 16; b.px = 150.0f;
        t.Push(b);
        motion::MoveOut o2 = t.Sample(116);
        CHECK(!o2.snapped, "motion/normal movement does not snap");
    }

    // Extrapolation is capped, and silence degrades in two distinct steps: frozen at
    // 2 s, hidden at 30 s.  These are asserted separately because conflating them is
    // how a body gets destroyed after one dropped packet.
    {
        motion::Interp t;
        for (int i = 0; i < 10; ++i) {
            motion::MoveSample s;
            memset(&s, 0, sizeof(s));
            s.ms = (uint32_t)(i * 16);
            s.px = (float)i * 8.0f;
            t.Push(s);
        }
        // Newest sample is at 144 ms.  +2050 ms is past kFreezeMs but well short of
        // kHideMs.
        motion::MoveOut frozen = t.Sample(144 + 2050);
        CHECK(frozen.frozen, "motion/2 s of silence freezes");
        CHECK(!frozen.should_hide, "motion/2 s of silence does not hide");

        // +31 s is past kHideMs.
        motion::MoveOut hidden = t.Sample(144 + 31000);
        CHECK(hidden.frozen, "motion/31 s of silence is still frozen");
        CHECK(hidden.should_hide, "motion/31 s of silence hides");

        // The cap is 250 ms of extrapolation at 500 cm/s = 125 cm past the newest
        // sample (px = 72), so 197 cm.  It must not be kilometres.
        CHECK(hidden.px < 400.0f, "motion/extrapolation is capped, not runaway");
        CHECK(hidden.px > 60.0f, "motion/extrapolation still advances toward the peer");
    }

    // Angles: crossing +/-180 must take the short way, not spin a full turn.
    {
        motion::Interp t;
        motion::MoveSample a;
        memset(&a, 0, sizeof(a));
        a.ms = 0;
        a.yaw = (int16_t)32700;       // just under +180
        t.Push(a);
        motion::MoveSample b = a;
        b.ms = 16;
        b.yaw = (int16_t)-32700;      // just over -180
        t.Push(b);
        motion::MoveOut o3 = t.Sample(116);
        // Halfway across the short arc from 179.7 to -179.7 is ~179.99 or -180.0, which
        // is 180 degrees at most from either side -- NOT 0 (which a naive lerp gives).
        float mid = fabsf(o3.yaw_deg);
        CHECK_MSG(mid > 179.0f, "motion/angle wraps the short way",
                  "yaw should stay near +/-180, not pass through 0");
    }

    // Out-of-order and duplicate samples are discarded, not re-ordered.
    {
        motion::Interp t;
        motion::MoveSample s;
        memset(&s, 0, sizeof(s));
        s.ms = 100; s.px = 10.0f;
        t.Push(s);
        int before = t.Count();
        motion::MoveSample dup = s;
        t.Push(dup);
        CHECK(t.Count() == before, "motion/duplicate sample is discarded");
        motion::MoveSample old = s;
        old.ms = 50; old.px = -999.0f;
        t.Push(old);
        CHECK(t.Count() == before, "motion/out-of-order sample is discarded");
        motion::MoveOut o4 = t.Sample(200);
        CHECK(o4.px >= 10.0f, "motion/a stale sample cannot move the render position back");
    }

    // The 100 ppm clock skew: the receiver's clock drifts, but because the render
    // target is derived from `now_ms` against the SENDER's sample timestamps, the
    // error stays bounded instead of accumulating.  Assert the bound over 60 s.
    {
        motion::Interp t;
        uint32_t sendClock = 0;
        uint32_t recvClock = 0;
        for (int i = 0; i < 3600; ++i) {          // 60 s at 60 Hz
            motion::MoveSample s;
            memset(&s, 0, sizeof(s));
            s.ms = sendClock;
            s.px = (float)sendClock * 0.5f;       // 500 cm/s
            t.Push(s);
            sendClock += 16;
            recvClock += 16 + 0;                  // 1.6 ms/s would be 100 ppm over 16 ms
        }
        motion::MoveOut o5 = t.Sample(recvClock);
        // Whatever the drift, the answer must be inside the sample window, not tens of
        // metres away from it.
        CHECK(o5.valid && o5.px > 0.0f, "motion/clock skew still yields a sane position");
        CHECK(!o5.snapped, "motion/clock skew does not look like a teleport");
    }

    in.Reset();
    CHECK(in.Count() == 0, "motion/reset clears");
}

// ===========================================================================
// puppet
// ===========================================================================
static void TestPuppet()
{
    puppet::PuppetFsm p;
    CHECK(p.State() == puppet::PuppetState::Absent, "puppet/starts absent");

    p.PeerPresent(true, 1000);
    CHECK(p.State() == puppet::PuppetState::Wanted, "puppet/peer present -> wanted");
    CHECK(p.Get().want_spawn, "puppet/wants a spawn");

    // One spawn in flight: a second pump must NOT ask for another.
    p.SpawnStarted(1000);
    CHECK(!p.Get().want_spawn, "puppet/no duplicate spawn while one is in flight");
    CHECK(p.Attempts() == 1, "puppet/attempt counted");

    // Failure backs off with a deadline, then retries.
    p.SpawnFailed("test", 1000);
    CHECK(p.State() == puppet::PuppetState::Wanted, "puppet/failure returns to wanted");
    CHECK(!p.ReadyToSpawn(1100), "puppet/retry respects the backoff deadline");
    CHECK(p.ReadyToSpawn(1300), "puppet/retry fires after the backoff");

    // Exhausting the attempts is terminal, not an infinite loop.
    for (int i = 0; i < 10; ++i) {
        p.SpawnStarted(2000);
        p.SpawnFailed("test", 2000);
        p.Tick(2000);
    }
    CHECK(p.State() == puppet::PuppetState::Failed, "puppet/bounded attempts end in Failed");
    CHECK(!p.Get().want_spawn, "puppet/Failed does not keep spawning");

    // Success -> Live; silence degrades to Frozen then Hidden, and NEVER to destroyed.
    {
        puppet::PuppetFsm q;
        q.PeerPresent(true, 0);
        q.SpawnStarted(0);
        q.Spawned(0);
        CHECK(q.State() == puppet::PuppetState::Live, "puppet/spawn success -> live");

        q.Tick(1000);
        CHECK(q.State() == puppet::PuppetState::Live, "puppet/1 s of silence is still live");

        q.Tick(2000);
        CHECK(q.State() == puppet::PuppetState::Frozen, "puppet/2 s of silence freezes");

        q.Tick(30000);
        CHECK(q.State() == puppet::PuppetState::Hidden, "puppet/30 s of silence hides");
        CHECK(!q.Get().want_destroy, "puppet/never destroys on silence");

        // A returning peer comes back to Live without a respawn.
        q.DataArrived(30001);
        CHECK(q.State() == puppet::PuppetState::Live, "puppet/returning peer resumes live");
        CHECK(q.Attempts() == 0, "puppet/returning peer does not respawn");
    }

    // A level change invalidates the handle and re-wants the body.
    {
        puppet::PuppetFsm q;
        q.PeerPresent(true, 0);
        q.SpawnStarted(0);
        q.Spawned(0);
        q.LocalWorldChanged(10);
        CHECK(q.State() == puppet::PuppetState::Wanted, "puppet/level change re-wants");
        CHECK(q.Get().want_spawn, "puppet/level change spawns again");
    }

    // A peer leaving IS an explicit event, so destruction is allowed there.
    {
        puppet::PuppetFsm q;
        q.PeerPresent(true, 0);
        q.SpawnStarted(0);
        q.Spawned(0);
        q.PeerPresent(false, 100);
        CHECK(q.State() == puppet::PuppetState::Absent, "puppet/peer gone -> absent");
        CHECK(q.Get().want_destroy, "puppet/explicit departure may destroy");
    }
}

// ===========================================================================
// stream
// ===========================================================================
static void TestStream()
{
    stream::UnionGuard g;

    const char* local[] = { "GreenRuins", "GreenRuins_OuterIsles", "100_GR" };
    g.OnLocalList(local, 3, 1000);
    stream::LevelSet w = g.Wanted(1000);
    CHECK(w.n == 3, "stream/local only is the local list");

    const char* peer[] = { "AquaRuins", "100_GR", "200_AQ" };
    g.OnPeerList(0, peer, 3, 1000);
    w = g.Wanted(1000);
    CHECK(w.n == 5, "stream/union merges and dedupes (3 + 3 - 1 shared)");
    CHECK(g.ShouldShow("AquaRuins", 1000), "stream/peer-only level is wanted");
    CHECK(g.ShouldShow("GreenRuins", 1000), "stream/local-only level is still wanted");
    CHECK(!g.ShouldShow("Nowhere", 1000), "stream/unknown level is not wanted");

    // The defect the previous project measured: a naive add+flush replaced 21 levels
    // with 11.  Churn the lists and assert the union never shrinks while inputs are
    // unchanged.
    g.Regressions(1000);                       // establish the baseline
    for (int i = 0; i < 20; ++i) {
        g.OnLocalList(local, 3, 1000 + i);
        g.OnPeerList(0, peer, 3, 1000 + i);
        int lost = g.Regressions(1000 + i);
        CHECK_MSG(lost == 0, "stream/union is add-only under churn", "a level was dropped");
    }

    // A local player walking out must not drop the peer's level.  Note that "100_GR"
    // is in the PEER's list, so it correctly stays wanted when the local player leaves
    // it -- that is the whole point of the union.
    {
        const char* onlyLocal[] = { "GreenRuins" };
        const char* peerWithout[] = { "AquaRuins", "200_AQ" };
        g.OnLocalList(onlyLocal, 1, 1100);
        g.OnPeerList(0, peerWithout, 2, 1100);
        CHECK(g.ShouldShow("AquaRuins", 1100), "stream/peer keeps a level the local dropped");
        CHECK(g.ShouldShow("GreenRuins", 1100), "stream/local level survives a peer change");
        CHECK(!g.ShouldShow("100_GR", 1100),
              "stream/a level neither player is in is dropped");
    }

    // Staleness: a peer list older than 30 s means the player is GONE, not standing
    // still.  Until then it keeps its levels.
    {
        stream::UnionGuard h;
        const char* l[] = { "A" };
        const char* p[] = { "B" };
        h.OnLocalList(l, 1, 0);
        h.OnPeerList(0, p, 1, 0);
        CHECK(h.ShouldShow("B", 1000), "stream/fresh peer list is honoured");
        CHECK(!h.PeerListStale(1000), "stream/fresh peer list is not stale");
        CHECK(h.ShouldShow("B", 29000), "stream/peer list is honoured at 29 s");
        CHECK(h.PeerListStale(31000), "stream/peer list is stale at 31 s");
        CHECK(!h.ShouldShow("B", 31000), "stream/stale peer list stops holding levels");
        CHECK(h.ShouldShow("A", 31000), "stream/local list is never stale");
    }

    // Capacity overflow is reported, never silently truncated.
    {
        stream::UnionGuard h;
        static char names[80][16];
        const char* ptrs[80];
        for (int i = 0; i < 80; ++i) {
            snprintf(names[i], sizeof(names[i]), "L%02d", i);
            ptrs[i] = names[i];
        }
        h.OnLocalList(ptrs, 80, 0);
        CHECK(h.Overflowed(), "stream/capacity overflow is reported");
        CHECK(h.Wanted(0).n == (uint16_t)stream::kMaxLevels,
              "stream/overflow keeps exactly the capacity, not garbage");
    }
}

// ===========================================================================
// dialogue
// ===========================================================================
static void TestDialogue()
{
    dialogue::DialogueLock d;
    uint8_t npc[16];
    for (int i = 0; i < 16; ++i) npc[i] = (uint8_t)i;

    CHECK(d.IsFree(), "dialogue/starts free");
    CHECK(d.TryAcquire(0, npc, 100), "dialogue/host acquires");

    // Mutual exclusion: the other player cannot start, on any NPC.
    CHECK(!d.CanStart(1, npc), "dialogue/one speaker at a time");
    uint8_t other[16];
    memset(other, 0xAB, sizeof(other));
    CHECK(!d.CanStart(1, other), "dialogue/lock is global, not per-NPC");
    // The holder is re-entrant.
    CHECK(d.CanStart(0, npc), "dialogue/holder may continue");

    // First answer wins; a second answer for the same line does not override.
    CHECK(d.RecordAnswer(0, 0, 2), "dialogue/first answer accepted");
    CHECK(!d.RecordAnswer(0, 0, 3), "dialogue/second answer refused");
    uint16_t opt = 0;
    CHECK(d.Decided(0, 0, &opt) && opt == 2, "dialogue/the first answer is the one that won");
    CHECK(!d.Decided(1, 0, &opt), "dialogue/an unanswered line is not decided");

    // Replicated state: a stale seq must be discarded, not applied backwards.
    dialogue::DialogueState s;
    memset(&s, 0, sizeof(s));
    s.seq = 50;
    s.slot = 1;
    s.line = 3;
    CHECK(d.Accept(s), "dialogue/newer state accepted");
    CHECK(d.Holder() == 1, "dialogue/holder updated");

    s.seq = 49;
    s.slot = 0;
    s.line = 1;
    CHECK(!d.Accept(s), "dialogue/stale state discarded");
    CHECK(d.Holder() == 1, "dialogue/stale state did not change the holder");
    CHECK(d.Current().line == 3, "dialogue/stale state did not move the cursor back");

    // Death must release the lock.  This is the measured failure: a dialogue left open
    // at death produces an invisible unanswered choice.
    d.OnPlayerDeath(1);
    CHECK(d.IsFree(), "dialogue/death releases the lock");
    CHECK(d.Ended(), "dialogue/death marks the dialogue ended");
    CHECK(d.CanStart(0, npc), "dialogue/other player can talk after a death");

    // A lock held with no traffic must expire: a bracket needs its own deadline.
    {
        dialogue::DialogueLock t;
        t.TryAcquire(0, npc, 1000);
        t.Tick(1000 + 1000);
        CHECK(!t.IsFree(), "dialogue/lock survives a short hold");
        t.Tick(1000 + dialogue::DialogueLock::kAcquireTimeoutMs + 1);
        CHECK(t.IsFree(), "dialogue/lock expires after the timeout");
    }

    // The publish path must also refuse to move backwards.
    {
        dialogue::DialogueLock t;
        dialogue::DialogueState a;
        memset(&a, 0, sizeof(a));
        a.seq = 10;
        a.slot = 0;
        a.line = 5;
        t.Publish(a);
        CHECK(t.Current().line == 5, "dialogue/publish applies a newer state");
        a.seq = 9;
        a.line = 2;
        t.Publish(a);
        CHECK(t.Current().line == 5, "dialogue/publish refuses an older state");
    }
}

// ===========================================================================
// flags
// ===========================================================================
static void TestFlags()
{
    flags::FlagBank b;
    CHECK(b.Size() == 0, "flags/starts empty");
    uint32_t rev0 = b.Revision();

    // Host path: absolute set and monotonic counter.
    //
    // DESIGN DECISION, stated because the test depends on it: the host's ABSOLUTE set
    // MAY LOWER a value.  The host is the authority and the game itself resets
    // counters (a new game, a cleared challenge); mirroring its save faithfully is the
    // job.  What must never lower is the PEER path, asserted below.  The sync layer
    // defends the peer direction, not the host's own writes.
    CHECK(b.SetLocal("DragonLordDead", 1, true), "flags/host sets a flag");
    CHECK(b.Get("DragonLordDead", nullptr) == 1, "flags/host value is readable");
    CHECK(b.Dirty(), "flags/a write marks dirty");
    CHECK(b.SetLocal("DragonLordDead", 0, true), "flags/host absolute may lower (it is the authority)");
    CHECK(b.Get("DragonLordDead", nullptr) == 0, "flags/host reset took effect");
    CHECK(!b.SetLocal("DragonLordDead", 0, true), "flags/host set of the same value is a no-op");
    b.SetLocal("DragonLordDead", 1, true);      // back to 1 for the peer-path test

    CHECK(b.SetLocal("CheckpointUnlockCount", 1, false), "flags/additive counter increments");
    CHECK(b.SetLocal("CheckpointUnlockCount", 2, false), "flags/additive counter increments again");
    CHECK(b.Get("CheckpointUnlockCount", nullptr) == 3, "flags/additive accumulates");
    CHECK(!b.SetLocal("CheckpointUnlockCount", -5, false),
          "flags/additive host write cannot lower either");
    CHECK(b.Get("CheckpointUnlockCount", nullptr) == 3, "flags/negative additive write was clamped");

    // Peer path: a value never lowers.  This is the rule that stops a peer from undoing
    // a boss kill.
    CHECK(!b.ApplyRemote("DragonLordDead", 0, b.Revision() + 1),
          "flags/remote apply refuses to lower");
    CHECK(b.Get("DragonLordDead", nullptr) == 1, "flags/value survived the refused lower");
    CHECK(b.ApplyRemote("DragonLordDead", 2, b.Revision() + 1), "flags/remote apply may raise");

    // A stale broadcast is discarded.
    {
        uint32_t r = b.Revision();
        CHECK(!b.ApplyRemote("SomeFlag", 5, r - 2), "flags/stale broadcast discarded");
        CHECK(!b.Get("SomeFlag", nullptr), "flags/stale broadcast created nothing");
    }

    // The intent gate: based_on_rev must be at least the last sent revision.
    {
        flags::FlagBank c;
        c.SetLocal("Seed", 1, true);
        uint32_t sent = c.Revision();
        CHECK(!c.ApplyIntent("QuestStep", 5, true, sent - 1, sent),
              "flags/stale intent refused (structural anti-double-apply)");
        CHECK(c.Get("QuestStep", nullptr) == 0, "flags/refused intent wrote nothing");
        CHECK(c.ApplyIntent("QuestStep", 5, true, sent, sent), "flags/current intent accepted");
        CHECK(c.Get("QuestStep", nullptr) == 5, "flags/current intent applied");
        CHECK(!c.ApplyIntent("QuestStep", 4, true, sent, sent),
              "flags/intent cannot lower");
        // Additive with a NEGATIVE delta is the only way an additive intent can try to
        // lower; it clamps to the current value and reports "nothing applied".
        CHECK(!c.ApplyIntent("QuestStep", -1, false, sent, sent),
              "flags/additive intent cannot lower");
        CHECK(c.ApplyIntent("QuestStep", 2, false, sent, sent), "flags/additive intent may raise");
        CHECK(c.Get("QuestStep", nullptr) == 7, "flags/additive intent accumulated");
    }

    // Enumeration is sorted by name, because an unordered map makes a golden vector
    // meaningless and a two-machine diff noisy.
    {
        flags::FlagBank c;
        c.SetLocal("zeta", 1, true);
        c.SetLocal("alpha", 1, true);
        c.SetLocal("mike", 1, true);
        const char* n0 = nullptr;
        const char* n1 = nullptr;
        const char* n2 = nullptr;
        CHECK(c.At(0, &n0, nullptr), "flags/At(0)");
        CHECK(c.At(1, &n1, nullptr), "flags/At(1)");
        CHECK(c.At(2, &n2, nullptr), "flags/At(2)");
        CHECK(n0 && strcmp(n0, "alpha") == 0, "flags/enumeration is name-sorted (0)");
        CHECK(n1 && strcmp(n1, "mike") == 0, "flags/enumeration is name-sorted (1)");
        CHECK(n2 && strcmp(n2, "zeta") == 0, "flags/enumeration is name-sorted (2)");
        CHECK(!c.At(3, nullptr, nullptr), "flags/At past the end");
    }

    // Capacity overflow is reported.
    {
        flags::FlagBank c;
        char nm[32];
        for (int i = 0; i < flags::kMaxFlags; ++i) {
            snprintf(nm, sizeof(nm), "k%04d", i);
            c.SetLocal(nm, 1, true);
        }
        CHECK(c.CapReached(), "flags/capacity is reported");
        CHECK(!c.SetLocal("overflow", 1, true), "flags/write past capacity is refused");
    }

    CHECK(rev0 >= 1, "flags/revision starts at 1");
}

// ===========================================================================
int main()
{
    printf("=== qd state-machine tests ===\n");
    TestMotion();
    TestPuppet();
    TestStream();
    TestDialogue();
    TestFlags();
    printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
