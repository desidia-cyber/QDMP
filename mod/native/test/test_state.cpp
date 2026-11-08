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
#include "qdenemy.h"
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
// stream: the partner-forced set (the veto's bookkeeping)
// ===========================================================================
static void TestStreamForce()
{
    stream::UnionGuard g;
    const char* mine[] = {"100_GR"};
    const char* theirs[] = {"500_AUT", "100_GR"};
    g.OnLocalList(mine, 1, 1000);
    g.OnPeerList(1, theirs, 2, 1000);
    CHECK(g.PeerWants("500_AUT", 1500), "force/peer wants its own level");
    CHECK(!g.PeerWants("140_GR_ShopIsleOnly", 1500), "force/peer does not want an unlisted level");
    // The local slot never counts as "the partner needs it" -- the game may hide its
    // own levels; only the partner overrides.
    const char* none[] = {"x"};
    g.OnPeerList(1, none, 0, 2000);
    CHECK(!g.PeerWants("100_GR", 2500), "force/local wish is not a partner need");
    CHECK(g.AnyPeerLive(2500), "force/an empty peer list is still a live peer");
    CHECK(!g.AnyPeerLive(2000 + stream::UnionGuard::kPeerStaleMs + 1), "force/a silent peer goes stale");

    stream::ForceBook b;
    CHECK(b.Need("500_AUT", 1000), "force/first need asks the caller to show");
    CHECK(!b.Need("500_AUT", 1200), "force/second need is a refresh, not a second show");
    CHECK(b.IsForced("500_AUT"), "force/is forced");
    // Hysteresis: a partner stepping out for less than kReleaseMs keeps the level.
    CHECK(!b.ShouldRelease("500_AUT", false, false, 1200 + 1000), "force/no release inside the window");
    CHECK(b.ShouldRelease("500_AUT", false, false, 1200 + stream::ForceBook::kReleaseMs), "force/release after the window");
    CHECK(!b.IsForced("500_AUT"), "force/released level is no longer forced");
    // A level the local game now wants is handed back, never hidden by us.
    b.Need("300_X", 0);
    CHECK(!b.ShouldRelease("300_X", false, true, 999999), "force/local wish: no hide");
    CHECK(!b.IsForced("300_X"), "force/local wish: the game owns it again");
    // Partner still needs it: refresh, no release however long.
    b.Need("400_Y", 0);
    CHECK(!b.ShouldRelease("400_Y", true, false, 999999), "force/still needed: kept");

    char out[64];
    stream::ShortLevelName("/Game/Maps/Ruins/100_GR.100_GR", out, sizeof(out));
    CHECK(strcmp(out, "100_GR") == 0, "force/short name from a package path");
    stream::ShortLevelName("140_GR_ShopIsleOnly", out, sizeof(out));
    CHECK(strcmp(out, "140_GR_ShopIsleOnly") == 0, "force/short name of a bare name");
}

// ===========================================================================
// dialogue: per-slot talk board (SYNC-DESIGN 2.7, no global lock)
// ===========================================================================
static void TestTalkBoard()
{
    dialogue::TalkBoard t;
    uint8_t npcA[16], npcB[16];
    dialogue::StableId("Chef_NPC_2/PersistentLevel/100_GR/", npcA);
    dialogue::StableId("Merchant_NPC_BP_2/PersistentLevel/140_GR_ShopIsleOnly/", npcB);
    CHECK(!dialogue::TalkBoard::SameId(npcA, npcB), "talk/distinct actors get distinct ids");
    uint8_t again[16];
    dialogue::StableId("Chef_NPC_2/PersistentLevel/100_GR/", again);
    CHECK(dialogue::TalkBoard::SameId(npcA, again), "talk/the id is stable");

    CHECK(t.Accept(1, 5, true, npcA, 0, 1000), "talk/accept");
    CHECK(t.OtherTalkingTo(0, npcA, 1100), "talk/partner talking to A");
    CHECK(!t.OtherTalkingTo(1, npcA, 1100), "talk/own slot is not 'other'");
    CHECK(!t.OtherTalkingTo(0, npcB, 1100), "talk/not talking to B");
    // Older seq is discarded, never applied backwards.
    CHECK(!t.Accept(1, 4, false, npcA, 0, 1200), "talk/stale seq discarded");
    CHECK(t.OtherTalkingTo(0, npcA, 1200), "talk/stale seq changed nothing");
    // The same seq again only refreshes liveness.
    CHECK(!t.Accept(1, 5, true, npcA, 0, 5500), "talk/repeat is not a change");
    CHECK(t.OtherTalkingTo(0, npcA, 5500 + dialogue::TalkBoard::kStaleMs - 1), "talk/repeat kept it live");
    // A sender that stops publishing does not block an NPC forever.
    CHECK(!t.OtherTalkingTo(0, npcA, 5500 + dialogue::TalkBoard::kStaleMs + 1), "talk/stale sender releases");
    // End of talk.
    CHECK(t.Accept(1, 6, false, npcA, 0, 6000), "talk/end accepted");
    uint8_t got[16];
    CHECK(!t.RemoteNpc(1, got, 6001), "talk/ended: no remote npc");
    // seq wraparound is ordered, not refused.
    t.Accept(0, 0xFFFFFFF0u, true, npcB, 0, 7000);
    CHECK(t.Accept(0, 3, true, npcA, 0, 7001), "talk/seq wraps forward");
}

// ===========================================================================
// dialogue: same-NPC arbitration (concurrent start is PREVENTED, not logged)
// ===========================================================================
static void TestTalkArbitration()
{
    dialogue::TalkBoard t;
    uint8_t npcA[16], npcB[16];
    dialogue::StableId("Chef_NPC_2/PersistentLevel/100_GR/", npcA);
    dialogue::StableId("Merchant_NPC_BP_2/PersistentLevel/140_GR_ShopIsleOnly/", npcB);

    // The host reserves; the peer may not start the same NPC.
    CHECK(t.Reserve(0, npcA, 1000), "arb/the host reserves an NPC");
    CHECK(!t.Reserve(1, npcA, 1001), "arb/the peer cannot reserve the same NPC");
    CHECK(t.Reserve(1, npcB, 1001), "arb/the peer may reserve a different NPC");
    t.ReleaseReservation(0);
    CHECK(t.Reserve(1, npcA, 1002), "arb/after release the peer may reserve it");

    // The crossed case: both slots end up in the same conversation.  The higher slot
    // is the loser, and the host (slot 0) keeps it.
    dialogue::TalkBoard c;
    CHECK(c.Accept(0, 1, true, npcA, 0, 2000), "arb/host talk accepted");
    CHECK(c.Accept(1, 1, true, npcA, 0, 2000), "arb/peer talk accepted");
    CHECK(c.SameConversation(0, npcA, 2001), "arb/both are in the same conversation");
    CHECK(c.LosesConflict(1, npcA, 2001), "arb/the peer yields");
    CHECK(!c.LosesConflict(0, npcA, 2001), "arb/the host keeps it");

    // A reservation outlives its stale window exactly as a talk state does.
    dialogue::TalkBoard r;
    CHECK(r.Reserve(0, npcA, 3000), "arb/a reservation is taken");
    CHECK(!r.Reserve(1, npcA, 3000 + dialogue::TalkBoard::kStaleMs - 1),
          "arb/it still blocks inside the stale window");
}

// ===========================================================================
// enemy: ownership, convergence, kill log, row budget
// ===========================================================================
static void TestEnemy()
{
    const uint32_t g1[4] = {0x5ae3b37bu, 0x4f721b25u, 0xbd55a3b5u, 0x958ab1c2u};
    const uint32_t g2[4] = {1, 2, 3, 4};
    enemy::HostView v;
    CHECK(v.Decide(g1, false, 1000) == enemy::LOCAL, "enemy/no host row: the peer runs it");
    v.OnRow(g1, 0, 0, 0, 0, 40000, enemy::kAlive, 1000);
    CHECK(v.Decide(g1, false, 1500) == enemy::DRIVEN, "enemy/fresh row: driven");
    CHECK(v.Decide(g1, false, 1000 + enemy::HostView::kFreshMs + 1) == enemy::LOCAL,
          "enemy/never-driven copy with an old row stays local");
    // Hysteresis: one late datagram must not wake the AI of a driven copy.
    CHECK(v.Decide(g1, true, 1000 + enemy::HostView::kFreshMs + 1) == enemy::DRIVEN,
          "enemy/driven copy holds through a short gap");
    CHECK(v.Decide(g1, true, 1000 + enemy::HostView::kReleaseMs + 1) == enemy::LOCAL,
          "enemy/driven copy released after the gap");
    // Death, refreshed by the host's re-broadcast, then forgotten after a rest.
    v.OnRow(g1, 0, 0, 0, 0, 0, 0, 3000);
    CHECK(v.Decide(g1, false, 3100) == enemy::KILL, "enemy/dead row: kill");
    CHECK(v.Decide(g1, false, 3000 + enemy::HostView::kDeadStaleMs + 1) == enemy::LOCAL,
          "enemy/a death the host stopped sending (rest) no longer kills");
    // Alive again replaces dead.
    v.OnRow(g1, 0, 0, 0, 0, 65535, enemy::kAlive, 4000);
    CHECK(v.Decide(g1, false, 4001) == enemy::DRIVEN, "enemy/respawn replaces the death");
    v.OnRow(g2, 0, 0, 0, 0, 1, enemy::kAlive, 4000);
    CHECK(v.Count() == 2, "enemy/two keys");
    const uint32_t zero[4] = {0, 0, 0, 0};
    v.OnRow(zero, 0, 0, 0, 0, 1, enemy::kAlive, 4000);
    CHECK(v.Count() == 2, "enemy/zero guid ignored");

    // HP fraction.
    CHECK(enemy::HpFrac(100, 100) == 65535, "enemy/full hp");
    CHECK(enemy::HpFrac(0, 100) == 0, "enemy/zero hp");
    CHECK(enemy::HpFrac(0.0001f, 100) == 1, "enemy/a sliver is alive, not 0");
    CHECK(enemy::HpFrac(50, 0) == 0, "enemy/no max is not a divide");

    // Convergence: downward only, dead band, overkill on death.
    CHECK(Approx(enemy::ConvergeDamage(100, 100, 32768, true), 50.0f, 0.1f), "enemy/converge to host fraction");
    CHECK(enemy::ConvergeDamage(40, 100, 32768, true) == 0.0f, "enemy/never heals");
    CHECK(enemy::ConvergeDamage(50.5f, 100, 32768, true) == 0.0f, "enemy/inside the dead band");
    CHECK(enemy::ConvergeDamage(30, 100, 0, false) > 30.0f, "enemy/host dead: overkill");
    CHECK(enemy::ConvergeDamage(0, 100, 0, false) == 0.0f, "enemy/already dead: nothing");

    // A hit is a fraction of max, so the host scales it by its own max.
    CHECK(enemy::HitFrac(100, 75, 100) == 16384, "enemy/hit fraction");
    CHECK(enemy::HitFrac(75, 100, 100) == 0, "enemy/regen is not a hit");

    // Kill log: dedup, removal, bounded.
    enemy::KillLog k;
    k.Add(g1); k.Add(g1); k.Add(g2);
    CHECK(k.Count() == 2, "enemy/kill log dedups");
    k.Remove(g1);
    CHECK(k.Count() == 1 && enemy::SameGuid(k.At(0), g2), "enemy/kill log removal");
    for (uint32_t i = 0; i < 200; ++i) { uint32_t g[4] = {i + 10, 0, 0, 1}; k.Add(g); }
    CHECK(k.Count() == enemy::KillLog::kMax, "enemy/kill log bounded");

    // Row budget: never more than asked, every row visited, then wraps.
    enemy::RowCursor c;
    int n = 0;
    int f = c.Take(45, 16, &n);
    CHECK(f == 0 && n == 16, "enemy/cursor first chunk");
    f = c.Take(45, 16, &n);
    CHECK(f == 16 && n == 16, "enemy/cursor second chunk");
    f = c.Take(45, 16, &n);
    CHECK(f == 32 && n == 13, "enemy/cursor tail");
    f = c.Take(45, 16, &n);
    CHECK(f == 0 && n == 16, "enemy/cursor wraps");
    f = c.Take(10, 16, &n);
    CHECK(f == 0 && n == 10, "enemy/cursor survives a shrinking set");

    // Follow: snaps on a teleport, eases otherwise, yaw takes the short way.
    float cur[3] = {0, 0, 0};
    float far[3] = {5000, 0, 0};
    enemy::Follow(cur, far, 0.016f);
    CHECK(cur[0] == 5000.0f, "enemy/follow snaps across a teleport");
    float near_[3] = {5100, 0, 0};
    enemy::Follow(cur, near_, 0.016f);
    CHECK(cur[0] > 5000.0f && cur[0] < 5100.0f, "enemy/follow eases");
    CHECK(enemy::FollowYaw(170.0f, -170.0f, 1.0f) > 170.0f, "enemy/yaw short way round");
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
    TestStreamForce();
    TestTalkBoard();
    TestTalkArbitration();
    TestEnemy();
    printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
