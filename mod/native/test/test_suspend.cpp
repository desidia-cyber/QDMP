// test_suspend.cpp -- the peer's timed suspend save (SYNC-GAPS §4).
//
// The game writes a suspend save every ~120 s on its own schedule.  In co-op the peer
// holds the game's own BlockSuspendSaves so its save cannot advance past the host, and
// if a suspend load happens anyway the host re-applies its absolute state.  This test
// drives the phases and, just as importantly, the deadlines: a repair that never lands
// must be reported rather than silently forgotten.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "qdsuspend.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, name)                                                          \
    do {                                                                           \
        if (cond) { ++g_pass; }                                                    \
        else { ++g_fail; printf("FAIL %s  (%s:%d)\n", name, __FILE__, __LINE__); } \
    } while (0)

using namespace qd;
using namespace qd::suspend;

static void TestHold()
{
    Board b;
    CHECK(b.WantHold(true, true), "hold/a live session on the peer wants the hold");
    CHECK(b.Holding(), "hold/it is holding");
    CHECK(b.StateToSend() == 1, "hold/the wire state is HOLD");
    CHECK(!b.WantHold(true, true), "hold/repeating the request is not a change");

    // A host does not hold: it is the authority whose state the peer converges on.
    Board h;
    CHECK(!h.WantHold(true, false), "hold/the host does not hold (it is the authority)");
    CHECK(!h.Holding(), "hold/the host is not holding");
    CHECK(h.StateToSend() == 0, "host/the wire state is CLEAR");
}

static void TestLocalLoad()
{
    Board b;
    b.WantHold(true, true);
    CHECK(b.OnLocalSuspendLoaded("World0_Suspend_1", 1000), "load/the peer reports its load");
    CHECK(b.Current() == PH_LOADED, "load/the phase is LOADED");
    CHECK(b.StateToSend() == 2, "load/the wire state is LOADED");
    CHECK(!b.OnLocalSuspendLoaded("World0_Suspend_0", 1100), "load/a second report is suppressed");
}

static void TestHostReapply()
{
    Board b;                       // the host
    CHECK(b.OnPeerLoaded(1, "World0_Suspend_1", 1000), "reapply/the host arms a reapply");
    CHECK(b.ReapplyDue(), "reapply/it is due");
    CHECK(b.TakeReapply(), "reapply/taking it consumes the flag");
    CHECK(!b.ReapplyDue(), "reapply/and it cannot be taken twice");
    CHECK(b.StateToSend() == 3, "reapply/the host answers REAPPLIED");
    CHECK(!b.TakeReapply(), "reapply/a second take is nothing");
}

static void TestPeerConfirms()
{
    Board b;                       // the peer
    b.WantHold(true, true);
    b.OnLocalSuspendLoaded("World0_Suspend_1", 1000);
    CHECK(b.OnPeerReapplied(1), "confirm/the host's REAPPLIED is accepted from LOADED");
    CHECK(b.Current() == PH_REAPPLIED, "confirm/the phase is REAPPLIED");
    CHECK(!b.OnPeerReapplied(2), "confirm/a second confirmation is nothing");
}

static void TestDeadline()
{
    Board b;                       // the peer, waiting for the host's repair
    b.WantHold(true, true);
    b.OnLocalSuspendLoaded("World0_Suspend_1", 1000);
    CHECK(!b.Tick(1000 + kReapplyDeadlineMs - 1), "deadline/not yet");
    CHECK(b.Tick(1000 + kReapplyDeadlineMs), "deadline/reported exactly once at the bound");
    CHECK(!b.Tick(1000 + kReapplyDeadlineMs + 1000), "deadline/and not again");
    CHECK(b.Current() == PH_GAVE_UP, "deadline/the phase is GAVE_UP");
}

static void TestRelease()
{
    Board b;
    b.WantHold(true, true);
    CHECK(b.WantHold(false, true), "release/a dead session clears the hold");
    CHECK(!b.Holding(), "release/it is not holding");
    CHECK(b.StateToSend() == 0, "release/the wire state is CLEAR");
}

int main()
{
    printf("=== qd suspend tests ===\n");
    TestHold();
    TestLocalLoad();
    TestHostReapply();
    TestPeerConfirms();
    TestDeadline();
    TestRelease();
    printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
