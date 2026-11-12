// test_objectstate.cpp -- the placed world-object lane's pure state machines.
//
// The failure this lane exists for was measured, not imagined (LIVE-TEST-LOG Session 8
// R67): `SG_Chest_BP2 [SG_Chest_BP_C] savable=no guid=- index=-1 ... opened=0`.  The chest
// has no GUID, so the GUID-keyed ACTOR_STATES lane cannot carry it, and the partner's chest
// stayed closed and lootable.  What follows tests the parts that decide whether this lane
// converges on the RIGHT object and the RIGHT state, plus the two ways it could do real
// damage in a game:
//
//   identity    two machines must mint the same key from the same cooked path, and the key
//               must not collide with the dialogue or placed-enemy keys for that path
//   survey      the class->shape table and the state-property names are the ones the
//               cooked-package decode measured; a class this lane cannot carry is refused
//   forbidden   the apply must never name the interaction verb or a payout, whichever
//               shape it is -- "the partner's chest also paid out" is not a test to run
//   chunk       a set too large for one datagram is continued, not truncated
//   revision    a revision names a body that went out in full
//   echo        a value the host wrote is not offered back as an intent
//   held        a body that crossed the peer's intent does not undo it
//   close       a `0` never undoes a local open: the game's close verb IS the interaction
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "qdobjectstate.h"

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
typedef objects::Snapshot     Snap;
typedef objects::Publisher    Pub;
typedef objects::IntentQueue  Q;
typedef objects::LocalDetector Det;

// A deterministic key: a, b, c zero and d the index, so a failure names the entry.
static void KeyOf(uint32_t k[4], uint32_t i)
{
    k[0] = 0; k[1] = 0; k[2] = 0; k[3] = 0x0B1EC700u + i;
}

struct Decoded {
    uint32_t rev;
    uint16_t n;
    int      rows;
    uint32_t key[80][4];
    uint8_t  shape[80];
    uint8_t  state[80];
};

// Read the body the way a receiver must: `n` is the sender's set size, and the number of
// rows actually in this datagram is whatever parses before the body ends.
static bool Decode(const uint8_t* body, int len, Decoded* d)
{
    wire::Reader r(body, len);
    uint32_t rev = 0;
    uint16_t n = 0;
    if (!wire::ReadObjectsHeader(r, &rev, &n)) return false;
    d->rev = rev;
    d->n = n;
    d->rows = 0;
    while (r.ok && r.pos < r.len) {
        wire::ObjectRow row;
        if (!wire::ReadObjectRow(r, &row)) break;
        if (d->rows >= 80) return false;
        for (int i = 0; i < 4; ++i) d->key[d->rows][i] = row.key[i];
        d->shape[d->rows] = row.shape;
        d->state[d->rows] = row.state;
        ++d->rows;
    }
    return r.ok;
}

// ---------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------
static void TestIdentity()
{
    const char* chest = "/Game/Maps/100_GR/PersistentLevel/SG_Chest_BP2";
    const char* door  = "/Game/Maps/100_GR/PersistentLevel/Door_Base_3";

    uint32_t a[4], b[4], c[4];
    CHECK(objects::KeyFromPath(chest, a), "identity: a placed path yields a key");
    CHECK(objects::KeyFromPath(chest, b), "identity: the same path yields a key again");
    CHECK(objects::KeyEq(a, b), "identity: the key is deterministic (both machines agree)");
    CHECK(objects::KeyFromPath(door, c), "identity: a second object yields a key");
    CHECK(!objects::KeyEq(a, c), "identity: two objects do not share a key");
    CHECK(!objects::KeyZero(a), "identity: a key is never all zero");

    // A name that differs by one character must not collide: the whole point of hashing the
    // path is that `SG_Chest_BP2` and `SG_Chest_BP20` are different chests.
    uint32_t d[4];
    CHECK(objects::KeyFromPath("/Game/Maps/100_GR/PersistentLevel/SG_Chest_BP20", d),
          "identity: a longer name yields a key");
    CHECK(!objects::KeyEq(a, d), "identity: a one-character difference changes the key");

    // No identity for a CDO/asset: an empty path is refused rather than hashed into the
    // zero-ish key that would then match everything.
    uint32_t e[4];
    CHECK(!objects::KeyFromPath("", e), "identity: an empty path has no key");
    CHECK(!objects::KeyFromPath(nullptr, e), "identity: a null path has no key");

    // The same path under the enemy/dialogue seeds must differ: three lanes, one string.
    uint32_t asEnemy[4];
    enemyid::Hash128(chest, enemyid::kPlacedSeed, asEnemy);
    CHECK(!objects::KeyEq(a, asEnemy),
          "identity: the object seed differs from the placed-enemy seed");
}

// ---------------------------------------------------------------------------
// The class survey
// ---------------------------------------------------------------------------
static void TestSurvey()
{
    // Classes named by actual cooked packages (analysis/pak/code/blueprints.py).
    CHECK(objects::ShapeOfClass("SG_Chest_BP_C") == objects::SH_CHEST,
          "survey: SG_Chest_BP_C is a chest");
    CHECK(objects::ShapeOfClass("Chest_C") == objects::SH_CHEST, "survey: Chest_C is a chest");
    CHECK(objects::ShapeOfClass("OW_Lever_00_lift_BP_C") == objects::SH_NONE,
          "a lift lever is the mover lane's, not object state (Session 26 crash)");
    CHECK(objects::ShapeOfClass("OW_Lift_Lever_00_BP_C") == objects::SH_NONE, "lift lever, other spelling");
    CHECK(objects::ShapeOfClass("LabElevatorSwitch_C") == objects::SH_NONE, "an elevator switch is not object state");
    CHECK(objects::ShapeOfClass("OW_Lever_00_BP_C") == objects::SH_ACTIVATABLE, "a plain lever still is");
    CHECK(objects::ShapeOfClass("MimicChest_C") == objects::SH_CHEST,
          "survey: MimicChest_C is a chest");
    CHECK(objects::ShapeOfClass("Door_Base_C") == objects::SH_DOOR,
          "survey: Door_Base_C is a door");
    CHECK(objects::ShapeOfClass("SecretZoneDoor_BP_C") == objects::SH_DOOR,
          "survey: SecretZoneDoor_BP_C is a door");
    CHECK(objects::ShapeOfClass("SZ_Door_02_BP_C") == objects::SH_DOOR,
          "survey: SZ_Door_02_BP_C is a door");
    CHECK(objects::ShapeOfClass("ItemPickup_Base_C") == objects::SH_PICKUP,
          "survey: ItemPickup_Base_C is a pickup");
    CHECK(objects::ShapeOfClass("BreakableBarricade_C") == objects::SH_BARRICADE,
          "survey: BreakableBarricade_C is a barricade");
    CHECK(objects::ShapeOfClass("WoodenBarricade_UnBroken_C") == objects::SH_BARRICADE,
          "survey: WoodenBarricade_UnBroken_C is a barricade");
    CHECK(objects::ShapeOfClass("Activatable_C") == objects::SH_ACTIVATABLE,
          "survey: Activatable_C is an activatable");
    // Session 26: lift controls left the object lane (they drive a mover; writing their
    // `Activated` preceded two Blueprint access violations in 500_AUT).
    CHECK(objects::ShapeOfClass("LiftSwitch_BP_C") == objects::SH_NONE,
          "LiftSwitch_BP_C belongs to the mover lane, not object state");

    // Bookkeeping classes carry the same substring and must never be treated as the object
    // they are named after.  Session 6 sent the peer 423 m and 33 km to a
    // `SkullDoorQuestStateManager`; that lesson is structural here.
    CHECK(objects::ShapeOfClass("SkullDoorQuestStateManager_C") == objects::SH_NONE,
          "survey: a *Manager class is not a door");
    CHECK(objects::ShapeOfClass("DoorStateSubsystem_C") == objects::SH_NONE,
          "survey: a *Subsystem class is not a door");
    CHECK(objects::ShapeOfClass("ChestData_C") == objects::SH_NONE,
          "survey: a *Data class is not a chest");
    CHECK(objects::ShapeOfClass("ChestComponent_C") == objects::SH_NONE,
          "survey: a *Component class is not a chest");
    CHECK(objects::ShapeOfClass("FS_PlayerChar_C") == objects::SH_NONE,
          "survey: an unrelated class is not an interactable");
    CHECK(objects::ShapeOfClass("") == objects::SH_NONE, "survey: an empty class is nothing");
    CHECK(objects::ShapeOfClass(nullptr) == objects::SH_NONE, "survey: a null class is nothing");

    // The measured state property and visual entry per shape.
    CHECK(strcmp(objects::StatePropFor(objects::SH_CHEST), "Opened") == 0,
          "survey: the chest's state is Opened (R67)");
    CHECK(strcmp(objects::StatePropFor(objects::SH_DOOR), "Opened") == 0,
          "survey: the door's state is Opened");
    CHECK(strcmp(objects::StatePropFor(objects::SH_PICKUP), "PickedUp") == 0,
          "survey: the pickup's state is PickedUp");
    CHECK(strcmp(objects::StatePropAltFor(objects::SH_PICKUP), "Acquired") == 0,
          "survey: the pickup's alternate probe is Acquired");
    CHECK(objects::StatePropFor(objects::SH_BARRICADE) == nullptr,
          "survey: a barricade has no property this lane reads (it is a savable)");
    CHECK(strcmp(objects::VisualFnFor(objects::SH_CHEST), "SetToFinalState") == 0,
          "survey: the chest's visual entry is SetToFinalState");
    CHECK(strcmp(objects::VisualFnFor(objects::SH_DOOR), "SetToOpenState") == 0,
          "survey: the door's visual entry is SetToOpenState");
    CHECK(objects::VisualFnFor(objects::SH_PICKUP) == nullptr,
          "survey: a pickup hides itself rather than calling one function");

    // The refusal list is the safety property: no shape's ladder may name a verb that pays
    // out, and the ladder members themselves must pass the same test.
    CHECK(objects::ForbiddenFn("Interact"), "forbidden: Interact");
    CHECK(objects::ForbiddenFn("Grant"), "forbidden: Grant");
    CHECK(objects::ForbiddenFn("GrantItemsAndActivate"), "forbidden: GrantItemsAndActivate");
    CHECK(objects::ForbiddenFn("InsertKey"), "forbidden: InsertKey");
    CHECK(objects::ForbiddenFn("CheckRequiredItems"), "forbidden: CheckRequiredItems");
    CHECK(objects::ForbiddenFn("OpenDoor"), "forbidden: OpenDoor");
    CHECK(objects::ForbiddenFn("CloseDoor"), "forbidden: CloseDoor");
    CHECK(objects::ForbiddenFn("Open"), "forbidden: Open");
    CHECK(objects::ForbiddenFn("TryUseKey"), "forbidden: TryUseKey");
    CHECK(!objects::ForbiddenFn("SetToFinalState"), "forbidden: SetToFinalState is allowed");
    CHECK(!objects::ForbiddenFn("SetToOpenState"), "forbidden: SetToOpenState is allowed");
    CHECK(!objects::ForbiddenFn("RestoreFromSave"), "forbidden: RestoreFromSave is allowed");
    CHECK(!objects::ForbiddenFn("SetActorHiddenInGame"), "forbidden: hiding a pickup is allowed");
    CHECK(!objects::ForbiddenFn(""), "forbidden: an empty name is not a verb");

    for (uint8_t s = 1; s <= (uint8_t)objects::SH_ACTIVATABLE; ++s) {
        const char* v1 = objects::VisualFnFor((objects::Shape)s);
        const char* v2 = objects::SecondVisualFnFor((objects::Shape)s);
        CHECK(!objects::ForbiddenFn(v1), "forbidden: no ladder head is a payout verb");
        CHECK(!objects::ForbiddenFn(v2), "forbidden: no ladder second is a payout verb");
    }
}

// ---------------------------------------------------------------------------
// The set
// ---------------------------------------------------------------------------
static void TestSnapshot()
{
    Snap s;
    uint32_t k[4];
    CHECK(s.Count() == 0, "set: starts empty");

    KeyOf(k, 1);
    CHECK(s.Set(k, objects::SH_CHEST, 0), "set: an insert succeeds");
    CHECK(s.Count() == 1, "set: an insert counts once");
    CHECK(s.Find(k) == 0, "set: the key is findable");

    // A second write of the same key UPDATES rather than duplicating: the chest's state
    // changes, the object does not become two objects.
    CHECK(s.Set(k, objects::SH_CHEST, 1), "set: an update succeeds");
    CHECK(s.Count() == 1, "set: an update does not add an entry");
    CHECK(s.At(0)->state == 1, "set: the update is visible");

    uint32_t k2[4];
    KeyOf(k2, 2);
    CHECK(s.Find(k2) < 0, "set: an unknown key is not found");
    s.Set(k2, objects::SH_DOOR, 0);

    // Order-independent equality: the two machines discover in different orders.
    Snap a, b;
    a.Set(k, objects::SH_CHEST, 1);
    a.Set(k2, objects::SH_DOOR, 0);
    b.Set(k2, objects::SH_DOOR, 0);
    b.Set(k, objects::SH_CHEST, 1);
    CHECK(a.SameAs(b), "set: equality does not depend on insertion order");
    b.Set(k, objects::SH_CHEST, 0);
    CHECK(!a.SameAs(b), "set: a changed state makes the sets differ");
    CHECK(!a.SameAs(Snap()), "set: a non-empty set differs from an empty one");

    // The table is bounded, and overflow is reported rather than silently truncating.
    Snap full;
    int i = 0;
    for (; i < objects::kMaxEntries; ++i) {
        uint32_t kk[4];
        KeyOf(kk, 0x4000 + (uint32_t)i);
        if (!full.Set(kk, objects::SH_CHEST, 0)) break;
    }
    CHECK(i == objects::kMaxEntries, "set: fills to the documented bound");
    uint32_t extra[4];
    KeyOf(extra, 0x9999);
    CHECK(!full.Set(extra, objects::SH_CHEST, 0), "set: a full table refuses the next key");
    CHECK(full.Overflowed(), "set: the refusal is reported, not silent");
    CHECK(full.Full(), "set: Full() agrees");
}

// ---------------------------------------------------------------------------
// The publisher
// ---------------------------------------------------------------------------
static void TestPublisher()
{
    Pub p;
    CHECK(p.Due(1000), "pub: a fresh publisher has a body due");
    CHECK(p.Dirty(), "pub: a fresh publisher is dirty");

    // An empty set is still a valid body: it is how the lane proves it is alive.
    {
        Snap empty;
        uint8_t body[wire::kMaxBody];
        Pub::Chunk c = p.Peek(body, sizeof(body));
        Decoded d;
        CHECK(!c.blocked, "pub: an empty set is not blocked");
        CHECK(Decode(body, c.bytes, &d), "pub: an empty body decodes");
        CHECK(d.n == 0 && d.rows == 0, "pub: an empty body carries no rows");
    }

    // A small set goes out in one datagram, and the revision only advances when a whole
    // pass has been sent.
    Snap small;
    uint32_t k[4];
    KeyOf(k, 1);
    small.Set(k, objects::SH_CHEST, 1);
    CHECK(p.Observe(small), "pub: a change is observed");
    uint32_t rev0 = p.Revision();
    {
        uint8_t body[wire::kMaxBody];
        Pub::Chunk c = p.Peek(body, sizeof(body));
        CHECK(c.entries == 1, "pub: one row is written");
        CHECK(c.bytes == 6 + 18, "pub: the body is header plus one 18-byte row");
        CHECK(p.Commit(c.entries, 1000), "pub: the pass completes");
        CHECK(p.Revision() == rev0 + 1, "pub: the revision advances once per full body");
        CHECK(!p.Dirty(), "pub: the publisher is clean after a full pass");
    }

    // An unchanged set does not re-send immediately; the repair interval does.
    CHECK(!p.Observe(small), "pub: an unchanged set is not a change");
    CHECK(!p.Due(1000 + objects::kRepairMs - 1), "pub: no body before the repair interval");
    CHECK(p.Due(1000 + objects::kRepairMs), "pub: the repair pass is due at the interval");

    // ForceDirty is the join / suspend-load / world-change path.
    p.ForceDirty();
    CHECK(p.Due(1001), "pub: ForceDirty makes a body due at once");

    // A set larger than one datagram is CONTINUED across ticks, never truncated.  This is
    // the difference between "the peer has fewer objects" and "the body was a lie".
    Snap big;
    for (int i = 0; i < objects::kMaxEntries; ++i) {
        uint32_t kk[4];
        KeyOf(kk, 0x2000 + (uint32_t)i);
        big.Set(kk, objects::SH_CHEST, (uint8_t)(i & 1));
    }
    Pub q;
    CHECK(q.Observe(big), "pub: a large set is observed");
    int sentRows = 0, datagrams = 0;
    uint32_t now = 2000;
    while (sentRows < big.Count() && datagrams < 100) {
        uint8_t body[wire::kMaxBody];
        Pub::Chunk c = q.Peek(body, sizeof(body));
        CHECK(!c.blocked, "pub: a chunk is never blocked for an 18-byte row");
        if (c.blocked) break;
        Decoded d;
        CHECK(Decode(body, c.bytes, &d), "pub: a chunk decodes");
        CHECK(d.n == (uint16_t)big.Count(), "pub: the header names the WHOLE set");
        sentRows += c.entries;
        ++datagrams;
        q.Commit(c.entries, now);
        now += 10;
    }
    CHECK(sentRows == big.Count(), "pub: every row is eventually sent");
    CHECK(datagrams > 1, "pub: a large set needs more than one datagram");
    CHECK(!q.Due(now), "pub: nothing is due after the pass completed");
}

// ---------------------------------------------------------------------------
// The intent queue
// ---------------------------------------------------------------------------
static void TestIntentQueue()
{
    Q q;
    uint32_t k[4];
    KeyOf(k, 7);

    CHECK(q.Observe(k, objects::SH_CHEST, 1) == Q::kQueued, "q: a local change is queued");
    CHECK(q.Count() == 1, "q: one pending intent");
    CHECK(q.Observe(k, objects::SH_CHEST, 1) == Q::kDuplicate, "q: the same value is a duplicate");
    CHECK(q.Count() == 1, "q: a duplicate adds nothing");

    Q::Out out[Q::kMaxPerTick];
    int n = q.Due(0, out, Q::kMaxPerTick);
    CHECK(n == 1, "q: a fresh intent is due");
    CHECK(out[0].attempt == 1, "q: the first send is attempt 1");
    CHECK(out[0].state == 1, "q: the intent carries the state");
    CHECK(out[0].shape == objects::SH_CHEST, "q: the intent carries the shape");

    // No retry before the retry interval.
    n = q.Due(0 + Q::kRetryMs - 1, out, Q::kMaxPerTick);
    CHECK(n == 0, "q: no retry before the interval");
    n = q.Due(0 + Q::kRetryMs, out, Q::kMaxPerTick);
    CHECK(n == 1, "q: a retry is due at the interval");
    CHECK(out[0].attempt == 2, "q: the retry counts the attempt");

    // The host's body answers it.
    bool apply = true;
    CHECK(q.OnHostEntry(k, 1, &apply) == Q::kConfirmed, "q: a matching body confirms");
    CHECK(apply, "q: a confirmed value is applied");
    CHECK(q.Empty(), "q: a confirmed intent is settled");

    // A body that carries the asked value while it is pending applies and settles.
    q.Observe(k, objects::SH_CHEST, 1);
    apply = false;
    CHECK(q.OnHostEntry(k, 1, &apply) == Q::kConfirmed, "q: a pending intent matched is confirmed");
    CHECK(apply, "q: the matching value is applied even while pending");

    // A body that disagrees while an intent is in flight is HELD, not applied: it is
    // indistinguishable from one that left the host before the intent arrived.
    q.Observe(k, objects::SH_CHEST, 1);
    apply = true;
    CHECK(q.OnHostEntry(k, 0, &apply) == Q::kHeld, "q: a disagreeing body is held");
    CHECK(!apply, "q: a held body is not applied");

    // Coalescing: a second value before confirmation replaces the first, on a fresh budget.
    q.Observe(k, objects::SH_CHEST, 0);
    CHECK(q.Count() == 1, "q: coalescing does not add an entry");
    n = q.Due(5000, out, Q::kMaxPerTick);
    CHECK(n == 1 && out[0].state == 0, "q: the latest value is the one sent");

    // Echo: the host's value seen locally is not a local change, and it cancels a pending
    // intent for that key (otherwise a peer that toggled twice trades the values forever).
    Q e;
    e.RememberEcho(k, 1);
    CHECK(e.EchoMatches(k, 1), "q: a remembered echo matches");
    CHECK(!e.EchoMatches(k, 0), "q: a different value is not an echo");
    e.Observe(k, objects::SH_CHEST, 1);
    CHECK(e.Empty(), "q: an echoed value is never queued");
    e.Observe(k, objects::SH_CHEST, 0);          // a real local change
    CHECK(e.Count() == 1, "q: a non-echo change is queued");
    CHECK(e.Observe(k, objects::SH_CHEST, 1) == Q::kEcho, "q: going back to the host's value is an echo");
    CHECK(e.Empty(), "q: the echo cancels the pending intent");

    // Bounded retries: after kMaxAttempts the caller reaps it and the host's body wins.
    Q g;
    g.Observe(k, objects::SH_CHEST, 1);
    uint32_t t = 0;
    int attempts = 0;
    for (int i = 0; i < 20; ++i) {
        n = g.Due(t, out, Q::kMaxPerTick);
        if (n == 0) break;
        ++attempts;
        t += Q::kRetryMs;
    }
    CHECK(attempts == Q::kMaxAttempts, "q: the attempt budget is exactly kMaxAttempts");
    CHECK(g.GiveUp(k), "q: the give-up names a pending intent");
    CHECK(g.Empty(), "q: the give-up settles it");

    // The rate limit: a burst cannot emit more than kMaxPerTick in one frame.
    Q b;
    for (int i = 0; i < 8; ++i) {
        uint32_t kk[4];
        KeyOf(kk, 100 + (uint32_t)i);
        b.Observe(kk, objects::SH_CHEST, 1);
    }
    n = b.Due(10000, out, 8);
    CHECK(n == Q::kMaxPerTick, "q: at most kMaxPerTick datagrams per frame");
    int sent = n;
    // The bucket holds kBucket (4), so a second frame of two is still inside it...
    n = b.Due(10000, out, 8);
    CHECK(n == Q::kMaxPerTick, "q: a second frame is still inside the bucket");
    sent += n;
    // ...and the third is not.
    n = b.Due(10000, out, 8);
    CHECK(n == 0, "q: the bucket bounds the burst");
    // One refill window returns one token.
    n = b.Due(10000 + Q::kRefillMs, out, 8);
    CHECK(n == 1, "q: the bucket refills one token per window");
    sent += n;
    CHECK(sent <= Q::kBucket + 1, "q: the rolling budget holds");
}

// ---------------------------------------------------------------------------
// The local detector
// ---------------------------------------------------------------------------
static void TestDetector()
{
    Det d;
    Q q;
    Snap s;
    uint32_t k[4];
    KeyOf(k, 3);
    s.Set(k, objects::SH_CHEST, 0);

    // The first sample is the BASELINE: a peer joining a world full of open chests must not
    // offer every one of them as its own change.
    Det::Stats st;
    d.Observe(s, q, &st);
    CHECK(st.queued == 0, "det: the first sample queues nothing");
    CHECK(q.Empty(), "det: the first sample leaves the queue empty");

    // No change, no intent.
    d.Observe(s, q, &st);
    CHECK(st.changed == 0, "det: an unchanged set reports nothing");

    // A real change is queued.
    Snap s2;
    s2.Set(k, objects::SH_CHEST, 1);
    d.Observe(s2, q, &st);
    CHECK(st.changed == 1, "det: a change is seen");
    CHECK(st.queued == 1, "det: a change is queued");
    CHECK(q.Count() == 1, "det: the queue holds it");

    // A value the host applied is not a local change: Note() records it first.
    Q q2;
    Det d2;
    Snap base;
    uint32_t k2[4];
    KeyOf(k2, 4);
    base.Set(k2, objects::SH_DOOR, 0);
    d2.Observe(base, q2, &st);
    Snap applied;
    applied.Set(k2, objects::SH_DOOR, 1);
    d2.Note(k2, objects::SH_DOOR, 1);
    d2.Observe(applied, q2, &st);
    CHECK(st.changed == 0, "det: a value the host applied is not a local change");
    CHECK(q2.Empty(), "det: and queues nothing");

    // Known() answers with the last state, which is what the apply path asks before it
    // writes: rewriting an unchanged value through the property is pointless work.
    uint8_t known = 0xFF;
    CHECK(d.Known(k, &known), "det: a seen key is known");
    CHECK(known == 1, "det: the known value is the last observed one");
    uint32_t unknown[4];
    KeyOf(unknown, 99);
    CHECK(!d.Known(unknown, &known), "det: an unseen key is not known");
}

// ---------------------------------------------------------------------------
// The one-directional apply rule
// ---------------------------------------------------------------------------
static void TestApplyRule()
{
    // Open on the partner: allowed when it is not already open.
    CHECK(objects::ApplyAllowed(1, 0, true), "apply: closed -> open is allowed");
    CHECK(objects::ApplyAllowed(1, 0, false), "apply: an unreadable local state is still allowed");
    CHECK(!objects::ApplyAllowed(1, 1, true), "apply: already open is a no-op");
    // And never the other way: the game's close/deactivate verbs ARE the interaction path,
    // so a `0` cannot be applied without running the verb the partner did not run.
    CHECK(!objects::ApplyAllowed(0, 1, true), "apply: a 0 never undoes a local open");
    CHECK(!objects::ApplyAllowed(0, 0, true), "apply: a 0 on a closed object is a no-op");
    CHECK(!objects::ApplyAllowed(0, 0, false), "apply: a 0 with no local state is still refused");
}

// ---------------------------------------------------------------------------
// The row on the wire
// ---------------------------------------------------------------------------
static void TestRows()
{
    // The numeric contract with WIRE.md 3.20, checked at compile time so a change to the
    // row or the kind numbers cannot pass as a green test run.
    static_assert(objects::kRowBytes == 18, "the row is 18 bytes (WIRE.md 3.20)");
    static_assert(wire::K_OBJECTS == 0x2A, "the next free kind is 0x2A");
    static_assert(wire::I_OBJECT_STATE == 9, "the intent kind is 9");
    CHECK(true, "row: the numeric contract holds");

    wire::ObjectRow r;
    memset(&r, 0, sizeof(r));
    r.key[0] = 0x03020100u; r.key[1] = 0x07060504u;
    r.key[2] = 0x0b0a0908u; r.key[3] = 0x0f0e0d0cu;
    r.shape = objects::SH_CHEST;
    r.state = 1;

    uint8_t buf[64];
    wire::Writer w(buf, sizeof(buf));
    wire::WriteObjectsHeader(w, 7, 1);
    CHECK(w.len == 6, "row: the header is 6 bytes");
    wire::WriteObjectsChunk(w, &r, 1, 0);
    CHECK(w.len == 6 + 18, "row: header plus one 18-byte row");

    wire::Reader rd(buf, w.len);
    uint32_t rev = 0;
    uint16_t n = 0;
    CHECK(wire::ReadObjectsHeader(rd, &rev, &n), "row: the header decodes");
    CHECK(rev == 7 && n == 1, "row: the header carries rev and set size");
    wire::ObjectRow back;
    CHECK(wire::ReadObjectRow(rd, &back), "row: the row decodes");
    CHECK(objects::KeyEq(back.key, r.key), "row: the key round-trips");
    CHECK(back.shape == r.shape && back.state == r.state, "row: shape and state round-trip");
    CHECK(rd.pos == rd.len, "row: the body is exactly consumed");

    // A receiver refuses a row it cannot interpret rather than applying a guessed property.
    uint8_t badShape[32];
    wire::Writer bw(badShape, sizeof(badShape));
    bw.Guid(r.key);
    bw.U8(9);              // not a shape this build knows
    bw.U8(1);
    wire::Reader br(badShape, bw.len);
    wire::ObjectRow junk;
    CHECK(!wire::ReadObjectRow(br, &junk), "row: an unknown shape is refused");

    uint8_t badState[32];
    wire::Writer sw(badState, sizeof(badState));
    sw.Guid(r.key);
    sw.U8(objects::SH_CHEST);
    sw.U8(2);              // not a state
    wire::Reader sr(badState, sw.len);
    CHECK(!wire::ReadObjectRow(sr, &junk), "row: an unknown state is refused");

    // The intent form carries the same three fields.
    uint8_t ib[64];
    wire::Writer iw(ib, sizeof(ib));
    wire::WriteIntentObjectState(iw, 13, r.key, objects::SH_CHEST, 1);
    wire::Reader ir(ib, iw.len);
    wire::IntentBody body;
    CHECK(wire::ReadIntent(ir, &body), "row: the intent decodes");
    CHECK(body.kind == wire::I_OBJECT_STATE, "row: the intent kind is 9");
    CHECK(body.based_on_rev == 13, "row: the intent carries its baseline revision");
    CHECK(objects::KeyEq(body.guid, r.key), "row: the intent carries the key");
    CHECK(body.mode == objects::SH_CHEST, "row: the intent carries the shape");
    CHECK(body.value == 1, "row: the intent carries the state");

    // And the kind is on the reliable list, on both sides of the wire.
    CHECK(wire::IsReliableKind(wire::K_OBJECTS), "row: K_OBJECTS is a reliable kind");
}

int main()
{
    printf("=== placed world-object lane (K_OBJECTS 0x2A) ===\n");
    TestIdentity();
    TestSurvey();
    TestSnapshot();
    TestPublisher();
    TestIntentQueue();
    TestDetector();
    TestApplyRule();
    TestRows();
    printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
