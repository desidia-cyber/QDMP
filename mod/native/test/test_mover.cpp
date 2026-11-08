// test_mover.cpp -- owner-authoritative moving world objects (SYNC-GAPS §4).
//
// The properties that matter:
//   * exactly one machine owns a mover (nearer player; the host breaks an exact tie),
//     so the two never drive one object;
//   * a follower applies the owner's absolute transform and refuses a non-owner row --
//     applying a follower's idle copy would freeze the platform for the rider;
//   * an older revision is discarded, and the local transform is only published when it
//     changed by more than the quantiser's epsilon.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "qdmover.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, name)                                                          \
    do {                                                                           \
        if (cond) { ++g_pass; }                                                    \
        else { ++g_fail; printf("FAIL %s  (%s:%d)\n", name, __FILE__, __LINE__); } \
    } while (0)

using namespace qd;
using namespace qd::mover;

static void MakeId(uint8_t id[kIdLen], uint8_t seed)
{
    for (int i = 0; i < kIdLen; ++i) id[i] = (uint8_t)(seed + i);
}

static Sample MakeSample(const uint8_t id[kIdLen], int32_t x, int32_t y, int32_t z,
                         int16_t yaw, uint8_t flags)
{
    Sample s;
    memset(&s, 0, sizeof(s));
    memcpy(s.id, id, kIdLen);
    s.x = x; s.y = y; s.z = z;
    s.yaw = yaw;
    s.flags = flags;
    return s;
}

static void TestOwnership()
{
    Board b;
    uint8_t id[kIdLen];
    MakeId(id, 1);
    Sample s = MakeSample(id, 100, 0, 0, 0, 0);
    b.ObserveLocal(id, s, false, 8);

    CHECK(b.ResolveOwner(id, 100, 400, true), "owner/the nearer local player owns it");
    CHECK(b.IsOwner(id), "owner/and it is recorded as the owner");
    CHECK(!b.ResolveOwner(id, 90, 400, true), "owner/an unchanged decision is not a change");

    CHECK(b.ResolveOwner(id, 400, 100, true), "owner/the nearer peer takes ownership");
    CHECK(!b.IsOwner(id), "owner/the local machine stops driving it");

    // An exact tie is broken by role: the host owns it, whichever side asks.
    CHECK(b.ResolveOwner(id, 1, 1, true), "tie/the host takes an exact tie");
    CHECK(b.IsOwner(id), "tie/the host drives a co-located mover");
    CHECK(b.ResolveOwner(id, 1, 1, false), "tie/the peer yields an exact tie");
    CHECK(!b.IsOwner(id), "tie/the peer does not drive a co-located mover");
}

static void TestPublishOnlyWhenOwnerAndChanged()
{
    Board b;
    uint8_t id[kIdLen];
    MakeId(id, 2);
    Sample s = MakeSample(id, 0, 0, 0, 0, 0);

    // Not the owner: local observations are recorded but never published.
    b.ResolveOwner(id, 400, 100, false);
    CHECK(!b.ObserveLocal(id, s, false, 8), "publish/a follower does not publish");
    Sample rows[4];
    CHECK(b.SnapshotDirty(rows, 4) == 0, "publish/a follower has no dirty rows");

    // Own it, then move beyond the epsilon.
    b.ResolveOwner(id, 100, 400, true);
    s.x = 100;
    CHECK(b.ObserveLocal(id, s, false, 8), "publish/the owner publishes a real move");
    CHECK(b.SnapshotDirty(rows, 4) == 1, "publish/the dirty row is emitted");
    CHECK((rows[0].flags & kOwner) != 0, "publish/the row marks the sender as owner");

    // A sub-epsilon jitter is not a publish.
    s.x = 104;
    CHECK(!b.ObserveLocal(id, s, false, 8), "publish/a sub-epsilon move is not published");
    CHECK(b.SnapshotDirty(rows, 4) == 0, "publish/and leaves no dirty row");
}

static void TestAcceptRules()
{
    Board b;
    uint8_t id[kIdLen];
    MakeId(id, 3);
    Sample owner = MakeSample(id, 50, 0, 0, 16384, kOwner);
    Sample follower = MakeSample(id, 999, 0, 0, 0, 0);

    CHECK(b.Accept(follower, 1, 1000) == false, "accept/a non-owner row is refused");
    CHECK(b.Accept(owner, 1, 1000), "accept/the owner row is applied");
    CHECK(b.Accept(owner, 1, 1100) == false, "accept/a repeated revision is refused");
    CHECK(b.Accept(owner, 2, 1200), "accept/a newer revision is applied");
    CHECK(b.Accept(owner, 1, 1300) == false, "accept/an older revision is refused");

    Sample got;
    CHECK(b.Remote(id, &got), "accept/the remote transform is available");
    CHECK(got.x == 50 && got.yaw == 16384, "accept/it is the owner's transform");
    CHECK(b.Has(id), "accept/the mover is tracked");
}

static void TestIdentity()
{
    uint8_t a[kIdLen], b[kIdLen], c[kIdLen];
    Board::IdFromPath("World/Level/Elevator_1", a);
    Board::IdFromPath("World/Level/Elevator_1", b);
    Board::IdFromPath("World/Level/Elevator_2", c);
    CHECK(Board::SameId(a, b), "id/the same path is the same id");
    CHECK(!Board::SameId(a, c), "id/a different actor is a different id");
}

static void TestOverflowIsReported()
{
    Board b;
    uint8_t id[kIdLen];
    for (int i = 0; i < kMaxMovers + 4; ++i) {
        MakeId(id, (uint8_t)i);
        Sample s = MakeSample(id, i, 0, 0, 0, 0);
        b.ObserveLocal(id, s, false, 8);
    }
    CHECK(b.Overflowed(), "overflow/more movers than the table is reported, not silent");
}

int main()
{
    printf("=== qd mover tests ===\n");
    TestOwnership();
    TestPublishOnlyWhenOwnerAndChanged();
    TestAcceptRules();
    TestIdentity();
    TestOverflowIsReported();
    printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
