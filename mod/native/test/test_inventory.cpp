// test_inventory.cpp -- the host -> peer inventory mirror's pure state machines.
//
// These test the things that go wrong SILENTLY in a live session, where there is no
// debugger and one run costs a match:
//
//   split       a set too large for one datagram is truncated instead of continued, and
//               the missing tail looks like items the host does not have
//   count       the header carries "entries in this datagram" instead of the whole
//               container size, so a receiver cannot tell a chunk from a complete set
//   revision    a revision names a body that was never fully sent, so a peer that
//               missed a chunk believes it is current
//   retry       a refused grant is dropped, and the key is lost: the exact failure
//               SYNC-DESIGN section 2.4's acceptance test names
//   overgrant   a stale difference is applied, and the peer ends up with more than the
//               host -- which the next host body cannot remove
//   lower       an item is removed or a count lowered because the host's body said so
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "qdinventory.h"

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
typedef inventory::Snapshot     Snap;
typedef inventory::HostPublisher Pub;
typedef inventory::GrantQueue   Grants;

// ===========================================================================
// helpers
// ===========================================================================
static void Path(char* out, int cap, const char* stem, int i)
{
    // A realistic soft path from SYNC-DESIGN section 2.4, with a variable name so the
    // order of the set is observable in the wire bytes.
    if (i < 0) snprintf(out, cap, "/Game/Items/KeyItems/%s.%s", stem, stem);
    else       snprintf(out, cap, "/Game/Items/KeyItems/%s_%03d.%s_%03d", stem, i, stem, i);
}

struct Decoded {
    uint32_t rev;
    uint16_t n;
    int      entries;
    char     path[64][inventory::kMaxPath];
    int32_t  count[64];
};

static bool Decode(const uint8_t* body, int len, Decoded* d)
{
    wire::Reader r(body, len);
    d->rev = r.U32();
    d->n   = r.U16();
    d->entries = 0;
    while (r.ok && r.pos < r.len) {
        char p[inventory::kMaxPath];
        int l = r.Str(p, sizeof(p));
        int32_t c = r.I32();
        if (l < 0 || !r.ok) return false;
        if (d->entries < 64) {
            memcpy(d->path[d->entries], p, sizeof(p));
            d->count[d->entries] = c;
        }
        ++d->entries;
    }
    return r.ok;
}

static int FindDecoded(const Decoded& d, const char* path)
{
    for (int i = 0; i < d.entries && i < 64; ++i) {
        if (strcmp(d.path[i], path) == 0) return i;
    }
    return -1;
}

static bool AllPresent(const Snap& s, const Decoded& d)
{
    if (s.Count() != d.entries) return false;
    for (int i = 0; i < s.Count(); ++i) {
        const inventory::Entry* e = s.At(i);
        int j = FindDecoded(d, e->path);
        if (j < 0 || d.count[j] != e->count) return false;
    }
    return true;
}

// ===========================================================================
// Snapshot -- the absolute set
// ===========================================================================
static void TestSnapshot()
{
    Snap s;
    CHECK(s.Count() == 0, "snapshot/starts empty");

    CHECK(s.Set("/Game/Items/A.A", 1), "snapshot/set inserts");
    CHECK(s.Set("/Game/Items/B.B", 2), "snapshot/set inserts a second entry");
    CHECK(s.Count() == 2, "snapshot/count");
    CHECK(s.Find("/Game/Items/A.A") == 0, "snapshot/find by path");
    CHECK(s.Find("/Game/Items/Missing.M") < 0, "snapshot/missing path is not found");

    // An update is not an insertion: the host re-reads the same map every time.
    CHECK(s.Set("/Game/Items/A.A", 7), "snapshot/set updates");
    CHECK(s.Count() == 2, "snapshot/update does not grow the table");
    CHECK(s.At(0)->count == 7, "snapshot/update replaces the count");
    CHECK(s.At(0) != nullptr && s.At(7) == nullptr, "snapshot/at is bounded");

    // Equality is order-independent: a TSet walk may hand the same entries over in any
    // order, and a re-observation must not bump the revision.
    Snap a, b;
    a.Set("/Game/X.X", 1); a.Set("/Game/Y.Y", 2);
    b.Set("/Game/Y.Y", 2); b.Set("/Game/X.X", 1);
    CHECK(a.SameAs(b), "snapshot/equality ignores order");
    b.Set("/Game/X.X", 3);
    CHECK(!a.SameAs(b), "snapshot/equality notices a count change");
    b.Set("/Game/X.X", 1); b.Set("/Game/Z.Z", 1);
    CHECK(!a.SameAs(b), "snapshot/equality notices an inserted entry");

    // A path that does not fit is refused, not truncated into a different asset.
    {
        char longp[inventory::kMaxPath + 8];
        memset(longp, 'a', sizeof(longp));
        longp[inventory::kMaxPath - 1] = 0;         // exactly the largest accepted
        Snap p;
        CHECK(p.Set(longp, 1), "snapshot/accepts the longest representable path");
        memset(longp, 'b', sizeof(longp));
        longp[inventory::kMaxPath] = 0;             // one byte too long
        Snap q;
        CHECK(!q.Set(longp, 1), "snapshot/refuses an over-long path");
        CHECK(q.Overflowed(), "snapshot/an over-long path is reported");
        CHECK(q.Count() == 0, "snapshot/an over-long path is not stored");
    }

    // Empty is not a path, and the full table is reported rather than silently dropping.
    {
        Snap p;
        CHECK(!p.Set("", 1), "snapshot/refuses an empty path");
        for (int i = 0; i < inventory::kMaxEntries; ++i) {
            char path[inventory::kMaxPath];
            Path(path, sizeof(path), "Full", i);
            CHECK(p.Set(path, i + 1), "snapshot/fills the table");
        }
        CHECK(p.Full(), "snapshot/full is visible");
        CHECK(!p.Set("/Game/Items/OneTooMany", 1), "snapshot/refuses past the cap");
        CHECK(p.Overflowed(), "snapshot/reports the overflow");
        CHECK(p.Count() == inventory::kMaxEntries, "snapshot/count stays at the cap");
    }
}

// ===========================================================================
// HostPublisher -- absolute body, chunked across ticks, own revision
// ===========================================================================
static void TestEmptyBody()
{
    Pub pub;
    CHECK(pub.Due(0), "publish/first body is due");

    uint8_t body[wire::kMaxBody];
    Pub::Chunk c = pub.Peek(body, sizeof(body));
    CHECK(!c.blocked, "publish/an empty set is a valid body");
    CHECK(c.entries == 0, "publish/an empty body carries no entries");

    Decoded d;
    CHECK(Decode(body, c.bytes, &d), "publish/an empty body decodes");
    CHECK(d.rev == 1, "publish/first revision is 1");
    CHECK(d.n == 0, "publish/an empty body says n=0");

    uint32_t rev = pub.Revision();
    CHECK(pub.Commit(c.entries, 1000), "publish/committing an empty pass completes it");
    CHECK(pub.Revision() == rev + 1, "publish/a completed pass bumps the revision");
    CHECK(!pub.Due(1000), "publish/not immediately due again");
    CHECK(!pub.Due(1999), "publish/not due before the heartbeat");
    CHECK(pub.Due(2000), "publish/due at the 1 Hz heartbeat");
}

static void TestOneDatagram()
{
    Snap s;
    char p[inventory::kMaxPath];
    Path(p, sizeof(p), "AquaDoorKey", 2); s.Set(p, 1);
    Path(p, sizeof(p), "Lamp", -1);       s.Set(p, 4);
    Path(p, sizeof(p), "RustySword", -1); s.Set(p, 1);

    Pub pub;
    CHECK(pub.Observe(s), "publish/a new set is a change");
    CHECK(pub.Size() == 3, "publish/size tracks the set");
    uint32_t rev = pub.Revision();

    uint8_t body[wire::kMaxBody];
    Pub::Chunk c = pub.Peek(body, sizeof(body));
    CHECK(c.entries == 3, "publish/all three entries fit one datagram");
    CHECK(!c.blocked, "publish/a complete body is not blocked");

    Decoded d;
    CHECK(Decode(body, c.bytes, &d), "publish/body decodes");
    CHECK(d.rev == rev, "publish/body carries the current revision");
    // The rule the flag encoder was rewritten for: n is the CONTAINER size, never the
    // number of entries in this datagram.  Here they happen to be equal.
    CHECK(d.n == 3, "publish/n is the whole container size");
    CHECK(AllPresent(s, d), "publish/every entry and count arrives exactly");

    CHECK(pub.Commit(c.entries, 500), "publish/the pass completes");
    CHECK(pub.Revision() == rev + 1, "publish/complete pass bumps the revision");
    CHECK(!pub.Observe(s), "publish/re-observing the same set is not a change");
    CHECK(!pub.Dirty(), "publish/an unchanged set is not dirty");
}

static void TestChunking()
{
    Snap s;
    char p[inventory::kMaxPath];
    // Long enough that a dozen or so fill a 1200-byte body, so the cursor must be used.
    for (int i = 0; i < 40; ++i) {
        Path(p, sizeof(p), "A", i);
        s.Set(p, i + 1);
    }

    Pub pub;
    CHECK(pub.Observe(s), "chunk/a large set is a change");
    uint32_t rev0 = pub.Revision();

    bool seen[40] = {false};
    int  datagrams = 0;
    bool complete = false;
    uint32_t at = 1000;
    uint8_t body[wire::kMaxBody];

    while (!complete && datagrams < 100) {
        Pub::Chunk c = pub.Peek(body, sizeof(body));
        ++datagrams;
        CHECK(!c.blocked, "chunk/every entry fits an empty datagram");
        CHECK(c.entries > 0, "chunk/a non-empty set always makes progress");
        CHECK(pub.Cursor() < (int)pub.Size(), "chunk/the cursor stays inside the set");

        Decoded d;
        CHECK(Decode(body, c.bytes, &d), "chunk/body decodes");
        // Every datagram of the pass names the WHOLE container, so a receiver knows a
        // chunk is a chunk without a second count.
        CHECK(d.n == (uint16_t)pub.Size(), "chunk/n is the whole container on every chunk");
        CHECK(d.rev == rev0, "chunk/all chunks of a pass share one revision");
        CHECK(d.entries > 0 && d.entries <= 64, "chunk/body carries whole entries");

        for (int i = 0; i < d.entries && i < 64; ++i) {
            int idx = -1;
            for (int k = 0; k < s.Count(); ++k) {
                if (strcmp(s.At(k)->path, d.path[i]) == 0) { idx = k; break; }
            }
            CHECK(idx >= 0, "chunk/every entry maps back to the set");
            if (idx >= 0) {
                CHECK(!seen[idx], "chunk/no entry is sent twice in one pass");
                CHECK(d.count[i] == s.At(idx)->count, "chunk/the count is exact");
                seen[idx] = true;
            }
        }

        complete = pub.Commit(c.entries, at++);
        if (!complete) CHECK(pub.Dirty(), "chunk/a partial pass stays dirty");
    }
    CHECK(complete, "chunk/the pass completes");
    CHECK(datagrams > 1, "chunk/the set actually needed more than one datagram");
    for (int i = 0; i < 40; ++i) CHECK(seen[i], "chunk/every entry was carried");
    CHECK(pub.Revision() == rev0 + 1, "chunk/one complete pass is one revision bump");
    CHECK(!pub.Dirty(), "chunk/a complete pass clears dirty");
}

static void TestPeekDoesNotCommit()
{
    Snap s;
    char p[inventory::kMaxPath];
    for (int i = 0; i < 40; ++i) { Path(p, sizeof(p), "B", i); s.Set(p, 1); }

    Pub pub;
    pub.Observe(s);
    uint8_t body1[wire::kMaxBody];
    uint8_t body2[wire::kMaxBody];
    Pub::Chunk c1 = pub.Peek(body1, sizeof(body1));
    CHECK(pub.Cursor() == 0, "commit/peek does not advance the cursor");
    Pub::Chunk c2 = pub.Peek(body2, sizeof(body2));
    CHECK(c1.entries == c2.entries && c1.bytes == c2.bytes, "commit/a refused send is retried identically");
    CHECK(memcmp(body1, body2, c1.bytes) == 0, "commit/the retry bytes are identical");
}

static void TestChangeRestartsPass()
{
    Snap s1, s2;
    char p[inventory::kMaxPath];
    for (int i = 0; i < 40; ++i) { Path(p, sizeof(p), "C", i); s1.Set(p, 1); }
    s2 = s1;
    Path(p, sizeof(p), "C", 0);
    s2.Set(p, 9);      // the first entry changed

    Pub pub;
    pub.Observe(s1);
    uint8_t body[wire::kMaxBody];
    Pub::Chunk c = pub.Peek(body, sizeof(body));
    CHECK(pub.Commit(c.entries, 100) == false, "restart/the first chunk is not the whole pass");
    CHECK(pub.Cursor() > 0, "restart/the cursor advanced");

    CHECK(pub.Observe(s2), "restart/a change mid-pass is noticed");
    CHECK(pub.Cursor() == 0, "restart/a change restarts at entry 0");
    CHECK(pub.Dirty(), "restart/the restarted pass is dirty");

    Pub::Chunk r = pub.Peek(body, sizeof(body));
    Decoded d;
    CHECK(Decode(body, r.bytes, &d), "restart/the new body decodes");
    CHECK(d.entries > 0 && strcmp(d.path[0], s2.At(0)->path) == 0,
          "restart/the first entry of the new set is sent first");
}

// A heartbeat pass must not crawl at one chunk per second: a peer that joins a long
// session is missing exactly the keys it needs, and every extra second is a locked door.
static void TestHeartbeatContinues()
{
    Snap s;
    char p[inventory::kMaxPath];
    for (int i = 0; i < 40; ++i) { Path(p, sizeof(p), "D", i); s.Set(p, 1); }

    Pub pub;
    pub.Observe(s);
    uint8_t body[wire::kMaxBody];
    uint32_t t = 0;
    bool complete = false;
    while (!complete && t < 100) {
        Pub::Chunk c = pub.Peek(body, sizeof(body));
        complete = pub.Commit(c.entries, t++);
    }
    CHECK(complete, "heartbeat/the initial pass completes");
    uint32_t last = t - 1;
    CHECK(!pub.Dirty(), "heartbeat/the initial pass cleared dirty");

    CHECK(!pub.Due(last + inventory::kHeartbeatMs - 1), "heartbeat/not due before 1 Hz");
    CHECK(pub.Due(last + inventory::kHeartbeatMs), "heartbeat/due at the 1 Hz mark");

    Pub::Chunk first = pub.Peek(body, sizeof(body));
    CHECK(first.entries > 0 && !first.blocked, "heartbeat/the first chunk goes out");
    CHECK(!pub.Commit(first.entries, last + inventory::kHeartbeatMs),
          "heartbeat/the heartbeat pass is not complete after one chunk");
    CHECK(pub.Due(last + inventory::kHeartbeatMs + 1),
          "heartbeat/a pass in progress continues at tick rate");
}

// ===========================================================================
// GrantQueue -- absolute targets, bounded work, nothing ever forgotten
// ===========================================================================
static void TestGrantBasics()
{
    Grants q;
    CHECK(q.Empty(), "grant/starts empty");

    CHECK(q.Observe("/Game/Items/Key.A", 3, 0) == Grants::kQueued,
          "grant/a host count above the peer is queued");
    CHECK(q.Count() == 1, "grant/count after a queue");
    CHECK(q.Observe("/Game/Items/Key.A", 3, 0) == Grants::kSame,
          "grant/the same target is not new work");
    CHECK(q.Observe("/Game/Items/Key.A", 5, 0) == Grants::kRaised,
          "grant/a higher host count raises the target");
    CHECK(q.Observe("/Game/Items/Key.A", 2, 0) == Grants::kLowered,
          "grant/a lower host count lowers the target");

    Grants::Out out[Grants::kMaxPerTick];
    int n = q.Due(0, out, Grants::kMaxPerTick);
    CHECK(n == 1, "grant/one grant is due");
    CHECK(strcmp(out[0].path, "/Game/Items/Key.A") == 0, "grant/the path is carried");
    CHECK(out[0].target == 2, "grant/the latest absolute target is carried");
    CHECK(out[0].attempt == 1, "grant/the first attempt is 1");

    CHECK(q.Due(Grants::kRetryMs - 1, out, Grants::kMaxPerTick) == 0,
          "grant/no retry before the retry window");
    n = q.Due(Grants::kRetryMs, out, Grants::kMaxPerTick);
    CHECK(n == 1 && out[0].attempt == 2, "grant/a retry is due after the window");

    CHECK(q.Done("/Game/Items/Key.A"), "grant/done removes the item");
    CHECK(q.Empty(), "grant/done leaves the queue empty");
    CHECK(!q.Done("/Game/Items/Key.A"), "grant/done on an absent item is a no-op");
}

static void TestGrantFailureIsKept()
{
    Grants q;
    q.Observe("/Game/Items/Key.A", 4, 0);
    Grants::Out out[Grants::kMaxPerTick];
    CHECK(q.Due(0, out, Grants::kMaxPerTick) == 1, "fail/the grant is attempted");

    // WrGrantItem refused it.  The item must NOT disappear: this is the key the peer
    // needs to open the door, and the design names "lost" as the acceptance failure.
    q.Failed("/Game/Items/Key.A", 0);
    CHECK(q.Count() == 1, "fail/a refusal keeps the item");
    CHECK(q.Due(1, out, Grants::kMaxPerTick) == 0, "fail/a refusal waits for the retry window");
    CHECK(q.Due(Grants::kRetryMs, out, Grants::kMaxPerTick) == 1, "fail/a later body retries it");
    CHECK(out[0].target == 4, "fail/the target is unchanged across the retry");
}

static void TestGrantBounds()
{
    Grants q;
    char p[inventory::kMaxPath];
    for (int i = 0; i < 5; ++i) {
        Path(p, sizeof(p), "Bound", i);
        CHECK(q.Observe(p, 1, 0) == Grants::kQueued, "bounds/queue");
    }
    Grants::Out out[8];
    CHECK(q.Due(0, out, 8) == Grants::kMaxPerTick,
          "bounds/at most kMaxPerTick grants per tick");
    CHECK(q.Count() == 5, "bounds/the rest stay queued");

    // A full table reports the loss instead of dropping it silently.
    Grants full;
    for (int i = 0; i < Grants::kMaxPending; ++i) {
        char path[inventory::kMaxPath];
        Path(path, sizeof(path), "FullGrant", i);
        CHECK(full.Observe(path, 1, 0) == Grants::kQueued, "bounds/fill");
    }
    CHECK(full.Observe("/Game/Items/Overflow", 1, 0) == Grants::kOverflow,
          "bounds/overflow is reported");
    CHECK(full.Overflowed(), "bounds/overflow flag");
}

// A retry backlog must not consume every slot for the session.  Two items that keep
// failing, and two that arrived later, is the exact shape: the later key is the one the
// player is standing at the door with, so it goes first.
static void TestFreshBeforeRetry()
{
    Grants q;
    Grants::Out out[Grants::kMaxPerTick];

    q.Observe("/Game/Items/Old1.Old1", 1, 0);
    q.Observe("/Game/Items/Old2.Old2", 1, 0);
    CHECK(q.Due(0, out, Grants::kMaxPerTick) == 2, "retry/both old grants attempted");
    q.Failed("/Game/Items/Old1.Old1", 0);
    q.Failed("/Game/Items/Old2.Old2", 0);

    q.Observe("/Game/Items/New1.New1", 1, 500);
    q.Observe("/Game/Items/New2.New2", 1, 500);

    // At the retry deadline the two old items ARE due, but the two new items must win.
    int n = q.Due(Grants::kRetryMs, out, Grants::kMaxPerTick);
    CHECK(n == 2, "retry/two grants are due");
    bool new1 = strcmp(out[0].path, "/Game/Items/New1.New1") == 0 ||
                strcmp(out[1].path, "/Game/Items/New1.New1") == 0;
    bool new2 = strcmp(out[0].path, "/Game/Items/New2.New2") == 0 ||
                strcmp(out[1].path, "/Game/Items/New2.New2") == 0;
    CHECK(new1 && new2, "retry/fresh work is not starved by a retry backlog");
}

static void TestGrantNeverRemoves()
{
    Grants q;
    CHECK(q.Observe("/Game/Items/Key.A", 0, 0) == Grants::kDropped,
          "lower/a zero target queues nothing");
    CHECK(q.Empty(), "lower/a zero target leaves the queue empty");

    q.Observe("/Game/Items/Key.A", 3, 0);
    CHECK(q.Observe("/Game/Items/Key.A", 0, 0) == Grants::kDropped,
          "lower/a zero target cancels a pending grant");
    CHECK(q.Empty(), "lower/cancelling a grant touches no peer item");

    // The only removal API throws away a GRANT, never a peer's item: there is no
    // "remove item" operation anywhere in the header, deliberately.
    char p[inventory::kMaxPath];
    Path(p, sizeof(p), "Keep", -1);
    q.Observe(p, 1, 0);
    q.Failed(p, 1000);
    CHECK(q.Count() == 1, "lower/a failure never removes the item");
}

static void TestGrantPaths()
{
    Grants q;
    CHECK(q.Observe("/Game/A.A", 1, 0) == Grants::kQueued, "paths/exact A");
    CHECK(q.Observe("/Game/B.B", 1, 0) == Grants::kQueued, "paths/exact B");
    CHECK(q.Count() == 2, "paths/different paths are different grants");
    CHECK(q.Done("/Game/A.A"), "paths/done A");
    CHECK(q.Count() == 1, "paths/only A was settled");
    CHECK(!q.Done("/Game/A"), "paths/a prefix is not an alias");
}

// ===========================================================================
// The zero-count tombstone ledger (quest-item mode)
// ===========================================================================
// MEASURED live (progression.md round 2/3): the host used its last key, the key simply
// disappeared from the published set, and the peer's copy stayed forever -- "the host holds
// none" had no wire form.  The decision: in quest-item mode a path that HAS been published
// and now reads 0 IS published, with count 0, and the receiver applies it by consuming its
// own copies.  These are the ledger's rules, and they are what keeps the set BOUNDED and
// the tombstone reversible.
static void TestTombstoneLedger()
{
    Pub p;
    const char* keyA = "/Game/Items/KeyItems/A.A";
    const char* keyB = "/Game/Items/KeyItems/B.B";

    CHECK(!p.Has(keyA), "tomb/a path never published is not tombstonable");

    // A published entry is remembered, so a later zero is a CHANGE the partner owes.
    p.DocNote(keyA);
    CHECK(p.Has(keyA), "tomb/a published path is remembered");
    CHECK(!p.Has(keyB), "tomb/and an unrelated path is not");

    // Re-noting is idempotent: the ledger is a set, not a counter.
    p.DocNote(keyA);
    CHECK(p.Has(keyA), "tomb/re-noting is idempotent");

    // The count rose again after a tombstone: the ledger forgets it, so the path is no
    // longer eligible to be tombstoned until it is published once more.
    p.DocClear(keyA);
    CHECK(!p.Has(keyA), "tomb/a cleared path is forgotten");

    // A tombstone is only a zero in the BODY; the ledger keeps the path so a second 0 is
    // still publishable and a rise-then-fall does not need a re-note first.
    p.DocNote(keyA);
    CHECK(p.Has(keyA), "tomb/republished");

    // =========================================================================
    // THE TOMBSTONE SECOND PASS -- the half that actually makes the peer's copy vanish.
    // `InventoryContents` does not keep a zero element: using a key up removes it outright,
    // so the container walk can never visit the path again.  A set built only from the walk
    // is therefore missing the key, which is indistinguishable from "never had it".  This
    // pass makes the difference observable.
    // =========================================================================
    Pub pub;
    Snap live;

    // The host holds two keys.  The walk produces them, and each is remembered.
    CHECK(live.Set(keyA, 1), "tomb/live A");
    CHECK(live.Set(keyB, 1), "tomb/live B");
    pub.DocNote(keyA);
    pub.DocNote(keyB);

    // Nothing is missing yet: no tombstone, and the set is unchanged.
    CHECK(pub.TombstoneMissing(&live, 2) == 0, "tomb/nothing missing yields no tombstone");
    CHECK(live.Find(keyA) >= 0 && live.Find(keyB) >= 0, "tomb/both still live");

    // THE HOST USES key A UP.  The container walk now returns only key B; key A is GONE
    // from the map entirely, which is the measured shape.
    Snap after;
    CHECK(after.Set(keyB, 1), "tomb/only B is in the container now");
    // `DocClear`/`DocNote` are not called for a path the walk never visits, which is exactly
    // why the ledger (and not the container) has to remember it.
    CHECK(pub.Has(keyA), "tomb/A is still remembered after it left the container");
    CHECK(pub.TombstoneMissing(&after, 1) == 1, "tomb/A becomes exactly one tombstone");
    const inventory::Entry* ea = after.At(after.Find(keyA));
    CHECK(ea != nullptr, "tomb/A is present in the published set");
    CHECK(ea && ea->count == 0, "tomb/A is published with count 0");
    const inventory::Entry* eb = after.At(after.Find(keyB));
    CHECK(eb && eb->count == 1, "tomb/B is untouched at count 1");

    // Idempotent: a second pass over the same state does not multiply the row.
    CHECK(pub.TombstoneMissing(&after, 1) == 0, "tomb/a second pass adds nothing");
    CHECK(after.Count() == 2, "tomb/the set is still two entries");

    // THE HOST PICKS THE KEY UP AGAIN: the count rises, so the collector clears and the
    // tombstone is carried by a LIVE count instead of a zero.
    pub.DocClear(keyA);
    pub.DocNote(keyA);
    Snap risen;
    CHECK(risen.Set(keyA, 2), "tomb/A is back with 2");
    (void)risen.Set(keyB, 1);
    CHECK(pub.TombstoneMissing(&risen, 2) == 0, "tomb/a key that rose is not tombstoned");
    const inventory::Entry* ra = risen.At(risen.Find(keyA));
    CHECK(ra && ra->count == 2, "tomb/A keeps its live count");

    // A BROKEN WALK MUST NOT READ AS A MASS CONSUMPTION.  `hadLive == 0` is "the read
    // produced nothing", which is a failed enumeration, not "the host lost everything".
    Snap empty;
    CHECK(pub.TombstoneMissing(&empty, 0) == 0, "tomb/a failed walk tombstones nothing");
    CHECK(empty.Count() == 0, "tomb/and publishes nothing");
}

// ===========================================================================
int main()
{
    printf("=== qd inventory tests ===\n");
    TestSnapshot();
    TestEmptyBody();
    TestOneDatagram();
    TestChunking();
    TestPeekDoesNotCommit();
    TestChangeRestartsPass();
    TestHeartbeatContinues();
    TestGrantBasics();
    TestGrantFailureIsKept();
    TestGrantBounds();
    TestFreshBeforeRetry();
    TestGrantNeverRemoves();
    TestGrantPaths();
    TestTombstoneLedger();
    printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
