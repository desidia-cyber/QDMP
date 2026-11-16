// test_audit.cpp -- the desync auditor's digest, diff and grace logic.
//
// WHY THIS IS A UNIT TEST AND NOT A LIVE RUN
// ------------------------------------------
// Three parts of qdaudit.h are easy to get wrong and impossible to see in a log:
//
//   * the digest must be ORDER-INDEPENDENT.  The two machines discover the same objects
//     in different orders (the entity table's order is a scan artefact).  An
//     order-dependent digest would disagree on two machines that hold identical state,
//     and every audit pass would report a desync that is not there.
//   * the diff must report EXACTLY the items that differ -- not the whole set -- because
//     the whole point of the line it prints is that a reader can act on it.
//   * the grace window must swallow a transient and must NOT swallow a permanent
//     mismatch.  A window that is too eager turns the auditor into noise; one that never
//     opens never reports anything.
//
// None of those produce a visible symptom in a two-instance run until the auditor is
// already wrong.  Here they are a two-second test.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "qdaudit.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, name)                                                          \
    do {                                                                           \
        if (cond) { ++g_pass; }                                                    \
        else { ++g_fail; printf("FAIL %s  (%s:%d)\n", name, __FILE__, __LINE__); } \
    } while (0)

using namespace qd::audit;

// ---- the digest ------------------------------------------------------------
static void TestDigest()
{
    Set a;
    a.Reset(CAT_OBJECTS);
    a.Add("k1", 1);
    a.Add("k2", 2);
    a.Add("k3", 3);

    // Same items, different insertion order: THE property the whole design rests on.
    Set b;
    b.Reset(CAT_OBJECTS);
    b.Add("k3", 3);
    b.Add("k1", 1);
    b.Add("k2", 2);

    Digest da = DigestOf(a);
    Digest db = DigestOf(b);
    CHECK(da.sum == db.sum, "digest is order-independent");
    CHECK(da.count == 3 && db.count == 3, "digest carries the item count");

    // A value change must move the digest.
    Set c = a;
    c.it[0].value = 9;
    CHECK(DigestOf(c).sum != da.sum, "digest sees a value change");

    // A key change must move the digest, and must not cancel against the old one.
    Set d = a;
    d.it[0].key[0] = 'x';
    CHECK(DigestOf(d).sum != da.sum, "digest sees a key change");

    // An empty set has a stable digest, and it is not the same as a set with one item.
    Set e;
    e.Reset(CAT_FLAGS);
    CHECK(DigestOf(e).sum == 0 && DigestOf(e).count == 0, "an empty set digests to zero");

    // Duplicate keys REPLACE rather than accumulate: a set that grew a second row for one
    // key would have a count the other machine cannot reproduce.
    Set f;
    f.Reset(CAT_FLAGS);
    f.Add("dup", 1);
    f.Add("dup", 2);
    CHECK(f.n == 1, "a duplicate key replaces rather than appends");
    CHECK(f.it[0].value == 2, "the replacement wins");
}

// ---- key canonicalisation --------------------------------------------------
static void TestKeys()
{
    char k[64];
    const uint32_t g[4] = { 0x03020100u, 0x07060504u, 0x0b0a0908u, 0x0f0e0d0cu };
    KeyHex(k, sizeof(k), g);
    CHECK(strcmp(k, "03020100070605040b0a09080f0e0d0c") == 0, "a GUID is 32 lowercase hex");

    KeyHex32(k, sizeof(k), 0x0000BEEFu);
    CHECK(strcmp(k, "0000beef") == 0, "a 32-bit key is 8 lowercase hex");

    KeySuffix(k, sizeof(k), "abcd", "x");
    CHECK(strcmp(k, "abcd:x") == 0, "a suffix joins with a colon");

    // Truncation is REPORTED, never silent: a silently truncated key hashes to a digest
    // that can never match, which would be a desync the auditor invented.
    char small[8];
    KeyHex(small, sizeof(small), g);
    CHECK(strlen(small) == 7, "a short buffer truncates to fit");
}

// ---- bucketing -------------------------------------------------------------
static void TestBucket()
{
    CHECK(Bucket(0, 64) == 0, "bucket 0");
    CHECK(Bucket(31, 64) == 0, "31 rounds down to 0");
    CHECK(Bucket(32, 64) == 64, "32 rounds up to 64");
    CHECK(Bucket(95, 64) == 64, "95 rounds down to 64");
    // The half-away-from-zero rule: a naive v/step*step truncates toward zero and would
    // put -31 and +31 in different buckets, which reads as a desync on a symmetric
    // oscillation.
    CHECK(Bucket(-31, 64) == 0, "-31 rounds to 0");
    CHECK(Bucket(-32, 64) == -64, "-32 rounds away from zero");
    CHECK(Bucket(-95, 64) == -64, "-95 rounds to -64");
    CHECK(Bucket(5, 1) == 5, "step 1 is the identity");

    // Two samples a frame apart must land in the same bucket; a real jump must not.
    CHECK(Bucket(1000, kPosStep) == Bucket(1032, kPosStep), "a one-frame lag is inside the bucket");
    CHECK(Bucket(1000, kPosStep) != Bucket(1200, kPosStep), "a 25 cm move leaves the bucket");

    // HP buckets: unreadable is its own value, and 10 % steps.
    CHECK(HpBucket(0.0f, 0.0f) == kHpUnknown, "no max HP reads as unknown");
    CHECK(HpBucket(-1.0f, 100.0f) == kHpUnknown, "a negative HP reads as unknown");
    CHECK(HpBucket(100.0f, 100.0f) == 9, "full HP is the top bucket");
    CHECK(HpBucket(0.0f, 100.0f) == 0, "zero HP is the bottom bucket");
    CHECK(HpBucket(55.0f, 100.0f) == 5, "55 % is bucket 5");
    CHECK(HpBucket(200.0f, 100.0f) == 9, "an over-full bar clamps to the top bucket");
}

// ---- the diff --------------------------------------------------------------
static void TestDiff()
{
    Set local;
    local.Reset(CAT_OBJECTS);
    local.Add("chest_a", 1);
    local.Add("chest_b", 0);
    local.Add("door_c", 1);

    DiffRow rows[kMaxDiffRows];
    int total = 0;

    // Identical sets say nothing at all.
    CHECK(DiffSets(local, local, rows, kMaxDiffRows, &total) == 0, "identical sets differ in nothing");
    CHECK(total == 0, "identical sets report zero differences");

    // One value differs: EXACTLY one row, naming that key and both values.
    Set peer = local;
    peer.it[1].value = 1;                       // chest_b
    int n = DiffSets(local, peer, rows, kMaxDiffRows, &total);
    CHECK(n == 1 && total == 1, "one changed value is one row");
    CHECK(strcmp(rows[0].key, "chest_b") == 0, "the row names the changed key");
    CHECK(rows[0].have_local && rows[0].have_peer, "the row has both sides");
    CHECK(rows[0].local == 0 && rows[0].peer == 1, "the row carries both values");

    // A key only the local machine holds, and a key only the peer holds: two rows, each
    // marked as one-sided, so the log can say which machine is missing what.
    Set asymmetric = local;
    asymmetric.n = 0;
    asymmetric.Add("chest_a", 1);
    asymmetric.Add("door_c", 1);
    asymmetric.Add("lever_d", 1);               // peer has it, local does not
    n = DiffSets(local, asymmetric, rows, kMaxDiffRows, &total);
    CHECK(n == 2 && total == 2, "a missing key on either side is one row");
    bool saw_b = false, saw_d = false;
    for (int i = 0; i < n; ++i) {
        if (strcmp(rows[i].key, "chest_b") == 0) {
            saw_b = true;
            CHECK(rows[i].have_local && !rows[i].have_peer, "a local-only key is marked local-only");
        }
        if (strcmp(rows[i].key, "lever_d") == 0) {
            saw_d = true;
            CHECK(!rows[i].have_local && rows[i].have_peer, "a peer-only key is marked peer-only");
        }
    }
    CHECK(saw_b && saw_d, "both one-sided keys are reported");

    // The cap bounds the log, and `total` still says how many there really were.
    Set many;
    many.Reset(CAT_FLAGS);
    for (int i = 0; i < 40; ++i) {
        char k[16];
        k[0] = 'k'; k[1] = (char)('0' + i / 10); k[2] = (char)('0' + i % 10); k[3] = 0;
        many.Add(k, 0);
    }
    Set none;
    none.Reset(CAT_FLAGS);
    DiffRow small[8];
    n = DiffSets(many, none, small, 8, &total);
    CHECK(n == 8, "the row cap bounds what is written");
    CHECK(total == 40, "the total still counts every difference");
}

// ---- the grace window and the wire state machine ---------------------------
static void TestBoard()
{
    Board b;
    b.Reset();

    const uint32_t kMap = 0xABCD1234u;
    CatIn cats[3];
    cats[0].cat = CAT_FLAGS;  cats[0].digest = 0x100; cats[0].count = 10;
    cats[1].cat = CAT_OBJECTS; cats[1].digest = 0x200; cats[1].count = 4;
    cats[2].cat = CAT_MAP;    cats[2].digest = 0x300; cats[2].count = 1;

    Event ev[16];
    uint32_t now = 1000;

    // A matching pass says nothing.  This is the steady state of a healthy session and
    // it must be SILENT, or the log teaches the reader to ignore it.
    b.BeginPass();
    b.SetLocal(CAT_FLAGS, 0x100, 10);
    b.SetLocal(CAT_OBJECTS, 0x200, 4);
    b.SetLocal(CAT_MAP, 0x300, 1);
    CHECK(b.OnPeer(1, kMap, kMap, cats, 3, now, ev, 16) == 0, "same map emits no event");
    CHECK(b.Update(now, ev, 16) == 0, "a matching pass emits nothing");
    CHECK(b.CatsCompared() == 3, "all three categories are compared");
    CHECK(b.ItemsCompared() == 15, "the item counts are summed");

    // A mismatch inside the grace window is NOT reported...
    now += 100;
    b.BeginPass();
    b.SetLocal(CAT_FLAGS, 0x100, 10);
    b.SetLocal(CAT_OBJECTS, 0x999, 5);         // objects now disagree
    b.SetLocal(CAT_MAP, 0x300, 1);
    CHECK(b.OnPeer(2, kMap, kMap, cats, 3, now, ev, 16) == 0, "the peer packet itself emits nothing");
    CHECK(b.Update(now, ev, 16) == 0, "a mismatch inside the grace window is not reported");
    CHECK(b.IsOpen(CAT_OBJECTS), "but it is TRACKED");

    // ...and asking for items before the window closes is refused, so an in-flight change
    // cannot cost a 1024-item dump.
    CHECK(b.NextRequest(now) == 0xFF, "no item request inside the grace window");

    // Past the window it is reported, exactly once, with its age.
    now += kGraceMs;
    int n = b.Update(now, ev, 16);
    CHECK(n == 1, "past the grace window the mismatch is reported once");
    CHECK(n == 1 && ev[0].kind == EV_DESYNC && ev[0].cat == CAT_OBJECTS, "the report names the category");
    CHECK(n == 1 && ev[0].age_ms >= kGraceMs, "the report carries the mismatch age");
    CHECK(n == 1 && ev[0].local_count == 5 && ev[0].peer_count == 4, "the report carries both counts");

    // ...and it is NOT repeated on the next pass.
    now += 5000;
    CHECK(b.Update(now, ev, 16) == 0, "an open desync is not re-reported every pass");

    // The item request is issued once the window has passed, and retried on its own clock.
    uint8_t want = b.NextRequest(now);
    CHECK(want == CAT_OBJECTS, "the open category is the one requested");
    CHECK(b.NextRequest(now) == 0xFF, "a second request waits for the retry interval");
    now += kReqRetryMs;
    CHECK(b.NextRequest(now) == CAT_OBJECTS, "the request is retried after its interval");

    // Once the diff has been PRINTED, the retry clock no longer re-asks: a still-open
    // desync is re-dumped on the 30 s repeat clock instead, so a four-minute episode costs
    // one diff line per item and not one per two seconds (the first live run printed 1843).
    b.MarkDiffed(CAT_OBJECTS, now);
    CHECK(b.NextRequest(now + kReqRetryMs + 10) == 0xFF,
          "a diffed episode is not re-asked on the retry clock");
    CHECK(b.NextRequest(now + kRepeatMs + 10) == CAT_OBJECTS,
          "a diffed episode is re-asked on the repeat clock");

    // A partial dump is not ready; a complete one is.
    b.OnItems(CAT_OBJECTS, 5, 2, now);
    CHECK(!b.ItemsReady(CAT_OBJECTS, now), "a partial item dump is not ready");
    b.OnItems(CAT_OBJECTS, 5, 5, now);
    CHECK(b.ItemsReady(CAT_OBJECTS, now), "a complete item dump is ready");
    // An old dump is stale, so a still-open desync asks again rather than diffing history.
    CHECK(!b.ItemsReady(CAT_OBJECTS, now + kItemsFreshMs + 1), "an old item dump goes stale");

    // When it heals, the healing is reported once and the category closes.
    now += 1000;
    b.BeginPass();
    b.SetLocal(CAT_FLAGS, 0x100, 10);
    b.SetLocal(CAT_OBJECTS, 0x200, 4);
    b.SetLocal(CAT_MAP, 0x300, 1);
    b.OnPeer(3, kMap, kMap, cats, 3, now, ev, 16);
    n = b.Update(now, ev, 16);
    CHECK(n == 1 && ev[0].kind == EV_HEALED && ev[0].cat == CAT_OBJECTS, "healing is reported once");
    CHECK(!b.IsOpen(CAT_OBJECTS), "the healed category is closed");
    CHECK(b.Update(now + 100, ev, 16) == 0, "healing is not repeated");

    // A category the partner does not carry is a SWITCH difference, reported once and
    // never compared -- two machines with different ini files are not desynced.
    b.Reset();
    now += 100;
    b.BeginPass();
    b.SetLocal(CAT_FLAGS, 0x100, 10);
    b.SetLocal(CAT_QUEST, 0x555, 3);
    b.SetLocal(CAT_MAP, 0x300, 1);
    CatIn peerOnly[2];
    peerOnly[0].cat = CAT_FLAGS; peerOnly[0].digest = 0x100; peerOnly[0].count = 10;
    peerOnly[1].cat = CAT_MAP;   peerOnly[1].digest = 0x300; peerOnly[1].count = 1;
    b.OnPeer(4, kMap, kMap, peerOnly, 2, now, ev, 16);
    n = b.Update(now, ev, 16);
    CHECK(n == 1 && ev[0].kind == EV_ONE_SIDED && ev[0].cat == CAT_QUEST,
          "a category only one machine collects is a switch difference, not a desync");
    CHECK(!b.IsOpen(CAT_QUEST), "a one-sided category is never tracked as a desync");
    CHECK(b.Update(now + 100, ev, 16) == 0, "the switch difference is said once");

    // A different world suppresses the data categories: the map disagreement EXPLAINS all
    // of them, and reporting twelve rows because of one load would bury the load.
    b.Reset();
    now += 100;
    b.BeginPass();
    b.SetLocal(CAT_FLAGS, 0x100, 10);
    b.SetLocal(CAT_OBJECTS, 0x200, 4);
    b.SetLocal(CAT_MAP, 0x300, 1);
    n = b.OnPeer(5, 0x9999, kMap, cats, 3, now, ev, 16);
    CHECK(n == 1 && ev[0].kind == EV_MAP_SPLIT, "a world difference is its own event");
    CHECK(b.MapSplit(), "the board remembers the split");
    // Only the map category is compared, so the flags/objects disagreement inside the
    // different world is not what gets reported.
    now += kGraceMs;
    n = b.Update(now, ev, 16);
    CHECK(n <= 1, "a split suppresses the data categories");
    for (int i = 0; i < n; ++i) {
        CHECK(ev[i].cat == CAT_MAP, "only the map is reported while the worlds differ");
    }

    // A silent partner stops being compared rather than being reported as desynced: a
    // disconnect is a different problem with its own line.
    b.Reset();
    now += 100;
    b.BeginPass();
    b.SetLocal(CAT_FLAGS, 0x100, 10);
    b.SetLocal(CAT_MAP, 0x300, 1);
    b.OnPeer(6, kMap, kMap, cats, 3, now, ev, 16);
    b.Update(now, ev, 16);
    now += kPeerStaleMs + 1000;
    b.BeginPass();
    b.SetLocal(CAT_FLAGS, 0x777, 10);
    b.SetLocal(CAT_MAP, 0x300, 1);
    CHECK(b.Update(now, ev, 16) == 0, "a stale partner is not compared");
}

// ---- the grace window must not swallow a real desync -----------------------
// The specific failure a playthrough must be able to prove ABSENT: one item set locally,
// with sync off for that write, that never heals.  Here it is, at the unit level.
static void TestPermanentDesync()
{
    Board b;
    const uint32_t kMap = 1;
    Event ev[8];
    uint32_t now = 0;

    Set localSet;
    localSet.Reset(CAT_FLAGS);
    localSet.Add("BrokeBarricade", 0);
    localSet.Add("CheckpointUnlockCount", 1);
    Set peerSet = localSet;
    peerSet.it[0].value = 1;                       // the deliberate local-only write

    Digest dl = DigestOf(localSet);
    Digest dp = DigestOf(peerSet);
    CHECK(dl.sum != dp.sum, "the injected write changes the digest");

    CatIn mine = { CAT_FLAGS, dl.sum, dl.count };
    CatIn theirs = { CAT_FLAGS, dp.sum, dp.count };

    // Ten passes over a minute: silent for the first grace window, then reported, and the
    // category STAYS open (it is a desync, not a transient).
    int reports = 0;
    for (int pass = 0; pass < 10; ++pass) {
        b.BeginPass();
        b.SetLocal(CAT_FLAGS, dl.sum, dl.count);
        b.OnPeer((uint32_t)pass + 1, kMap, kMap, &theirs, 1, now, ev, 8);
        int n = b.Update(now, ev, 8);
        for (int i = 0; i < n; ++i) if (ev[i].kind == EV_DESYNC) ++reports;
        now += 10000;
    }
    CHECK(reports >= 1, "a permanent desync is reported");
    CHECK(b.IsOpen(CAT_FLAGS), "and it stays open");
    CHECK(b.NextRequest(now) == CAT_FLAGS, "and the items are requested");

    // The diff names EXACTLY the injected item.
    DiffRow rows[kMaxDiffRows];
    int total = 0;
    int n = DiffSets(localSet, peerSet, rows, kMaxDiffRows, &total);
    CHECK(n == 1 && total == 1, "exactly one item differs");
    CHECK(strcmp(rows[0].key, "BrokeBarricade") == 0, "and it is the injected one");
    CHECK(rows[0].local == 0 && rows[0].peer == 1, "with both values");
}

int main()
{
    printf("=== desync auditor ===\n");
    TestDigest();
    TestKeys();
    TestBucket();
    TestDiff();
    TestBoard();
    TestPermanentDesync();
    printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
