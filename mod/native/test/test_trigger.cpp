// test_trigger.cpp -- the trigger lane's pure half (qdtrigger.h).
//
// THE PROPERTIES THAT MATTER
//   * a body round-trips exactly, and a malformed one is refused rather than replayed:
//     a replay dispatches a Blueprint event on the receiver, so a garbage key or a NaN
//     position must never reach it.
//   * the echo window: a key this machine just replayed is not sent back (the replay and
//     the pull-in's physical overlap are both "the local player entered"), a different
//     event or a different key is, and the window EXPIRES so walking in again fires again.
//   * a negative age (a note taken later in the same tick than `now`) is "just now" --
//     the unsigned version of that comparison is the Session 25 fall-guard bug.
//   * the pull-in moves only a far partner, only for a boss trigger, only on BEGIN.
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "qdtrigger.h"
#include "qdwire.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, name)                                                          \
    do {                                                                           \
        if (cond) { ++g_pass; }                                                    \
        else { ++g_fail; printf("FAIL %s  (%s:%d)\n", name, __FILE__, __LINE__); } \
    } while (0)

using namespace qd;
using namespace qd::trigger;

static Body Sample()
{
    Body b;
    memset(&b, 0, sizeof(b));
    b.key[0] = 0x11111111u; b.key[1] = 0x22222222u; b.key[2] = 0x33333333u; b.key[3] = 0x44444444u;
    b.event = kEvBegin;
    b.volume = kVolBox;
    b.flags = kFlagBoss;
    b.px = -27846.5f; b.py = -15403.25f; b.pz = -15339.0f;
    strcpy(b.name, "ActivationTrigger11");
    return b;
}

static void TestRoundTrip()
{
    uint8_t buf[256];
    wire::Writer w(buf, (int)sizeof(buf));
    Body a = Sample();
    CHECK(Encode(w, a), "encode a valid body");
    wire::Reader r(buf, w.len);
    Body b;
    CHECK(Decode(r, &b), "decode it back");
    CHECK(memcmp(a.key, b.key, 16) == 0, "key survives");
    CHECK(b.event == kEvBegin && b.volume == kVolBox && b.flags == kFlagBoss, "bytes survive");
    CHECK(b.px == a.px && b.py == a.py && b.pz == a.pz, "position survives bit-exact");
    CHECK(strcmp(b.name, "ActivationTrigger11") == 0, "name survives");
    CHECK(r.pos == w.len, "decode consumed exactly the body");
}

static void TestRefusals()
{
    uint8_t buf[256];
    {
        Body a = Sample();
        a.event = 3;
        wire::Writer w(buf, (int)sizeof(buf));
        CHECK(!Encode(w, a), "an unknown event is not encoded");
    }
    {
        Body a = Sample();
        a.px = NAN;
        wire::Writer w(buf, (int)sizeof(buf));
        CHECK(!Encode(w, a), "a NaN position is not encoded");
    }
    {
        // Hand-build a body with a zero key: a key nothing can resolve is refused at decode.
        Body a = Sample();
        memset(a.key, 0, 16);
        wire::Writer w(buf, (int)sizeof(buf));
        CHECK(Encode(w, a), "a zero key encodes (the writer does not judge keys)");
        wire::Reader r(buf, w.len);
        Body b;
        CHECK(!Decode(r, &b), "a zero key is refused at decode");
    }
    {
        Body a = Sample();
        wire::Writer w(buf, (int)sizeof(buf));
        Encode(w, a);
        buf[17] = 9;   // the event byte, corrupted in flight
        wire::Reader r(buf, w.len);
        Body b;
        CHECK(!Decode(r, &b), "a corrupted event byte is refused");
    }
    {
        Body a = Sample();
        wire::Writer w(buf, (int)sizeof(buf));
        Encode(w, a);
        wire::Reader r(buf, w.len - 3);
        Body b;
        CHECK(!Decode(r, &b), "a truncated body is refused");
    }
    {
        // A position past the sane bound written straight into the bytes.
        Body a = Sample();
        wire::Writer w(buf, (int)sizeof(buf));
        Encode(w, a);
        float huge = 3e9f;
        memcpy(buf + 19, &huge, 4);   // px
        wire::Reader r(buf, w.len);
        Body b;
        CHECK(!Decode(r, &b), "an out-of-range position is refused");
    }
}

static void TestEcho()
{
    EchoGuard g;
    g.Reset();
    uint32_t k1[4] = {1, 2, 3, 4};
    uint32_t k2[4] = {5, 6, 7, 8};
    CHECK(!g.Recent(k1, kEvBegin, 1000), "an empty guard knows nothing");
    g.Note(k1, kEvBegin, 1000);
    CHECK(g.Recent(k1, kEvBegin, 1000), "the same tick is recent");
    CHECK(g.Recent(k1, kEvBegin, 1000 + kEchoMs - 1), "inside the window");
    CHECK(!g.Recent(k1, kEvBegin, 1000 + kEchoMs), "the window expires");
    CHECK(!g.Recent(k1, kEvEnd, 1001), "a different event is not suppressed");
    CHECK(!g.Recent(k2, kEvBegin, 1001), "a different key is not suppressed");
    // Negative age: `now` sampled before the note in the same tick.
    g.Note(k2, kEvEnd, 5000);
    CHECK(g.Recent(k2, kEvEnd, 4990), "a negative age is 'just now', not ancient");
    // A re-note refreshes the window rather than adding a slot.
    g.Note(k1, kEvBegin, 20000);
    CHECK(g.Recent(k1, kEvBegin, 20000 + kEchoMs - 1), "a re-note restarts the window");
    // Overflow reuses the oldest slot and never loses the newest note.
    for (uint32_t i = 0; i < 100; ++i) {
        uint32_t k[4] = {100 + i, 0, 0, 1};
        g.Note(k, kEvBegin, 30000 + i);
    }
    uint32_t last[4] = {199, 0, 0, 1};
    CHECK(g.Recent(last, kEvBegin, 30100), "the newest note survives an overflow");
    // Wraparound of the 32-bit clock.
    EchoGuard w;
    w.Reset();
    w.Note(k1, kEvBegin, 0xFFFFFF00u);
    CHECK(w.Recent(k1, kEvBegin, 0x00000100u), "the window survives a clock wrap");
}

static void TestSlots()
{
    CHECK(SlotOf(kEvBegin, kVolBox) == 0 && SlotOf(kEvEnd, kVolBox) == 1, "box slots");
    CHECK(SlotOf(kEvBegin, kVolSphere) == 2 && SlotOf(kEvEnd, kVolSphere) == 3, "sphere slots");
    CHECK(SlotOf(kEvBegin, kVolTrapOn) == 4, "the trap's begin");
    CHECK(SlotOf(kEvEnd, kVolTrapOff) == 5, "the trap's end");
    CHECK(SlotOf(kEvEnd, kVolTrapOn) == -1, "the activate trap has no end event");
    CHECK(SlotOf(kEvBegin, kVolTrapOff) == -1, "the deactivate trap has no begin event");
    CHECK(SlotOf(kEvBegin, 4) == -1, "an unknown volume");
    for (int s = 0; s < kSlots; ++s) {
        CHECK(SlotOf(EventOfSlot(s), VolumeOfSlot(s)) == s, "slot -> (event, volume) -> slot");
    }
    uint8_t buf[256];
    Body a = Sample();
    a.volume = kVolTrapOff;      // a begin on the deactivate trap
    wire::Writer w(buf, (int)sizeof(buf));
    CHECK(!Encode(w, a), "an impossible (event, volume) pair is not encoded");
    a.event = kEvEnd;
    wire::Writer w2(buf, (int)sizeof(buf));
    CHECK(Encode(w2, a), "the trap's end encodes");
    wire::Reader r(buf, w2.len);
    Body b;
    CHECK(Decode(r, &b) && b.volume == kVolTrapOff && b.event == kEvEnd, "and decodes");
}

static void TestPullIn()
{
    Body b = Sample();
    CHECK(WantPullIn(b, b.px + 5000.0f, b.py, b.pz), "a far partner is pulled into a boss arena");
    CHECK(!WantPullIn(b, b.px + 500.0f, b.py, b.pz), "a partner already in the arena stays");
    Body e = b;
    e.event = kEvEnd;
    CHECK(!WantPullIn(e, e.px + 5000.0f, e.py, e.pz), "leaving a trigger never moves anyone");
    Body t = b;
    t.volume = kVolTrapOn;
    CHECK(WantPullIn(t, t.px + 5000.0f, t.py, t.pz), "a boss spawner's trap pulls a far partner in");
    Body n = b;
    n.flags = 0;
    CHECK(!WantPullIn(n, n.px + 5000.0f, n.py, n.pz), "an ordinary trigger never moves anyone");
}

int main()
{
    TestRoundTrip();
    TestRefusals();
    TestEcho();
    TestSlots();
    TestPullIn();
    printf("test_trigger: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
