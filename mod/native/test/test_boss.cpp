// test_boss.cpp -- the one shared boss bar, and the area-follow phase (Session 20).
//
// THE PROPERTIES THAT MATTER, and why each is a unit test rather than a live run:
//
//   * a follower's liveness is the OWNER's row.  Measured live (R143): the peer held a
//     dragon ALIVE -- its own spawner had respawned it -- while the owner held the same
//     key as a standing death.  Reading the local copy is exactly the bug, and a live run
//     can only show it, not isolate it.
//   * the host raises nothing until the peer says it HOLDS the key.  Measured live: the
//     host repaired `03FDBC21` every 3 s while the peer logged `no local copy`.  The
//     ask/ack board is the rule that stops that, and its expiry is what lets a key the
//     peer gains later become showable without a restart.
//   * a refusal must expire and an ACK must not be forgotten by a burst of refusals.
//   * TP_AREA is a distinct phase, and a travel body round-trips it.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "qdboss.h"
#include "qdwire.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, name)                                                          \
    do {                                                                           \
        if (cond) { ++g_pass; }                                                    \
        else { ++g_fail; printf("FAIL %s  (%s:%d)\n", name, __FILE__, __LINE__); } \
    } while (0)

using namespace qd;
using namespace qd::boss;

static void Key(uint32_t k[4], uint32_t a)
{
    k[0] = a; k[1] = a + 1; k[2] = a + 2; k[3] = a + 3;
}

// ---------------------------------------------------------------------------
// Liveness authority
// ---------------------------------------------------------------------------
static void TestAuthority()
{
    // The machine that runs it decides, whatever the partner's row says.
    CHECK(AliveByAuthority(true, true, false, false),
          "authority/owner alive is alive even with no partner row (and a dead row)");
    CHECK(!AliveByAuthority(true, false, true, true),
          "authority/owner dead is dead even when the partner's row says alive");

    // A follower with a row takes the OWNER's bit verbatim.
    CHECK(AliveByAuthority(false, false, true, true),
          "authority/follower alive because the owner says so, despite its own dead copy");
    CHECK(!AliveByAuthority(false, true, true, false),
          "authority/follower dead because the owner says so, despite its own live copy");

    // No row from the owner: the follower must NOT guess.  This is the measured case
    // (the peer's respawned copy reading alive while the owner held a standing death).
    CHECK(!AliveByAuthority(false, true, false, false),
          "authority/follower with no owner row is not alive (local copy is not evidence)");
}

// ---------------------------------------------------------------------------
// The ask/ack board
// ---------------------------------------------------------------------------
static void TestAckBoard()
{
    AckBoard b;
    uint32_t a[4], c[4];
    Key(a, 0x1000);
    Key(c, 0x2000);

    CHECK(!b.Acked(a), "ack/a key nobody confirmed is not acked");
    CHECK(!b.Refused(a, 1000), "ack/a key nobody refused is not refused");

    b.OnAck(a);
    CHECK(b.Acked(a), "ack/an ACK is remembered");
    CHECK(!b.Refused(a, 1000), "ack/and it does not make the key refused");
    CHECK(b.AckedCount() == 1, "ack/the count is one");

    // A refusal expires: a key the peer lacks now may appear later (a mirror, a spawner
    // activation), and a permanent refusal would keep a real fight dark for the session.
    b.OnNack(a, 5000);
    CHECK(!b.Acked(a), "ack/a NACK revokes an earlier ACK");
    CHECK(b.Refused(a, 5000), "ack/a NACK is refused at once");
    CHECK(b.Refused(a, 5000 + AckBoard::kRefuseMs), "ack/still refused at the boundary");
    CHECK(!b.Refused(a, 5000 + AckBoard::kRefuseMs + 1), "ack/and expires after the window");
    CHECK(!b.Acked(a), "ack/expiry does not silently re-ack");

    // An ACK after a refusal wins immediately (the peer constructed the copy).
    b.OnAck(a);
    CHECK(b.Acked(a), "ack/a later ACK wins");
    CHECK(!b.Refused(a, 5000 + 10), "ack/and clears the refusal");

    // Keys are independent: one refusal must never suppress another boss.
    b.OnNack(c, 6000);
    CHECK(b.Refused(c, 6000) && b.Acked(a), "ack/keys are independent");
    b.Clear(c);
    CHECK(!b.Refused(c, 6000), "ack/Clear forgets a refusal");

    // A burst of refusals cannot evict a confirmed key: the tables are separate.
    AckBoard full;
    uint32_t k[4];
    uint32_t confirmed[4];
    Key(confirmed, 0x9000);
    full.OnAck(confirmed);
    for (uint32_t i = 0; i < (uint32_t)AckBoard::kMax + 8; ++i) {
        Key(k, 0x100 + i * 8);
        full.OnNack(k, 7000 + i);
    }
    CHECK(full.Acked(confirmed), "ack/a burst of refusals does not evict an ACK");
    CHECK(full.RefusedCount(7000 + AckBoard::kMax + 8) <= AckBoard::kMax,
          "ack/the refusal table stays bounded");
}

// ---------------------------------------------------------------------------
// The wire
// ---------------------------------------------------------------------------
static void TestWire()
{
    // The new bits must be distinct from each other and from the existing ones: a mask
    // collision would make an ACK read as a bar-on, which is the divergence itself.
    // These are compile-time invariants, so a static_assert is the right instrument (and
    // C4127 correctly refuses to let a constant condition look like a runtime test).
    static_assert((wire::kBossAsk & wire::kBossAck) == 0, "ask and ack are distinct");
    static_assert((wire::kBossAsk & wire::kBossNack) == 0, "ask and nack are distinct");
    static_assert((wire::kBossAck & wire::kBossNack) == 0, "ack and nack are distinct");
    static_assert((wire::kBossAsk & (wire::kBossBarOn | wire::kBossBarOff | wire::kBossArenaExit)) == 0,
                  "ask collides with no existing boss flag");
    static_assert((wire::kBossAck & (wire::kBossBarOn | wire::kBossBarOff | wire::kBossArenaExit)) == 0,
                  "ack collides with no existing boss flag");
    static_assert((wire::kBossNack & (wire::kBossBarOn | wire::kBossBarOff | wire::kBossArenaExit)) == 0,
                  "nack collides with no existing boss flag");
    CHECK((wire::kBossAsk | wire::kBossAck | wire::kBossNack) != 0,
          "wire/the negotiation bits exist");

    uint32_t key[4];
    Key(key, 0xABCD);
    wire::BossBody in;
    memset(&in, 0, sizeof(in));
    memcpy(in.guid, key, 16);
    in.flags = wire::kBossAck;
    in.phase = 3;
    in.hp = 0x4567;

    uint8_t buf[64];
    wire::Writer w(buf, sizeof(buf));
    wire::WriteBoss(w, in);
    CHECK(w.ok, "wire/the boss body writes");

    wire::Reader r(buf, w.len);
    wire::BossBody out;
    memset(&out, 0, sizeof(out));
    CHECK(wire::ReadBoss(r, &out), "wire/and reads back");
    CHECK(out.flags == wire::kBossAck && out.phase == 3 && out.hp == 0x4567,
          "wire/flags, phase and hp round-trip");
    CHECK(memcmp(out.guid, key, 16) == 0, "wire/the key round-trips");

    // TP_AREA must not be mistaken for the hard handshake: the instruction is different
    // (stream a sublevel, do not load it as a world).
    static_assert(wire::TP_AREA != wire::TP_NONE && wire::TP_AREA != wire::TP_HARD_BEGIN &&
                  wire::TP_AREA != wire::TP_ARRIVED && wire::TP_AREA != wire::TP_RESUME,
                  "TP_AREA is its own phase");
    CHECK(wire::TP_AREA == 4, "wire/TP_AREA keeps its documented value");

    wire::TravelBody t;
    memset(&t, 0, sizeof(t));
    t.seq = 42;
    t.role = 1;
    t.phase = wire::TP_AREA;
    strcpy(t.map, "780_SZ_Depths");
    strcpy(t.dest, "");
    uint8_t tb[wire::kMaxBody];
    wire::Writer tw(tb, sizeof(tb));
    wire::WriteTravel(tw, t);
    CHECK(tw.ok, "wire/the area travel body writes");
    wire::Reader tr(tb, tw.len);
    wire::TravelBody to;
    memset(&to, 0, sizeof(to));
    CHECK(wire::ReadTravel(tr, &to), "wire/and reads back");
    CHECK(to.phase == wire::TP_AREA && to.seq == 42, "wire/phase and seq round-trip");
    CHECK(strcmp(to.map, "780_SZ_Depths") == 0, "wire/the area name round-trips");
}

int main()
{
    TestAuthority();
    TestAckBoard();
    TestWire();
    printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
