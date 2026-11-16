// test_gapworld.cpp -- the world-side rules the campaign brief's items 1-5 rest on.
//
// The properties that matter, and the way each one fails in a game if it is wrong:
//   * the unlock union is MONOTONE and refuses a non-name, because a byte that becomes an
//     array element is a save entry nobody can remove;
//   * `LastTouchedCheckpoint` is NOT a mirrored set member -- mirroring it would move the
//     partner's respawn point, which is the one thing item 1 says must stay per player;
//   * a counter that counts distinct world events is a SUM, not a monotone MAX, or two
//     players opening different chests still see `max(a,b)` and 108 is unreachable;
//   * a shop stock key never crosses, so buying the last relic does not empty the partner's
//     shelf;
//   * a rest is one episode however many times the checkpoint fires it, and it is ordered:
//     an older episode never resets the world again;
//   * a puzzle prop is last-writer-wins per key and the machine that APPLIED a row must not
//     publish it straight back (the echo that would ping-pong a book).
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "qdworldset.h"
#include "qdrest.h"
#include "qdpuzzle.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, name)                                                          \
    do {                                                                           \
        if (cond) { ++g_pass; }                                                    \
        else { ++g_fail; printf("FAIL %s  (%s:%d)\n", name, __FILE__, __LINE__); } \
    } while (0)

using namespace qd;
using namespace qd::worldset;
using namespace qd::rest;
using namespace qd::puzzle;

// ---------------------------------------------------------------------------
// Item 1 -- the unlock sets are a union, and the respawn point is not
// ---------------------------------------------------------------------------
static void TestUnion()
{
    Union u;
    CHECK(u.Count() == 0, "union/starts empty");

    CHECK(u.Merge("GR_Camp_Save"), "union/a new name is added");
    CHECK(!u.Merge("GR_Camp_Save"), "union/the same name twice is one entry");
    CHECK(u.Count() == 1, "union/count is the distinct number");
    CHECK(u.Has("GR_Camp_Save"), "union/has() answers for a merged name");
    CHECK(!u.Has("Swamp_1_save"), "union/has() refuses an absent name");

    // A name is an asset short name.  A path, a space or a separator is refused: a
    // truncated or foreign string would resolve to a different asset.
    CHECK(!u.Merge(""), "union/empty refused");
    CHECK(!u.Merge(nullptr), "union/null refused");
    CHECK(!u.Merge("/Game/Blueprints/Areas/GR_Camp_Save"), "union/a path refused");
    CHECK(!u.Merge("GR Camp Save"), "union/a space refused");
    CHECK(!u.Merge("GR_Camp_Save\\x"), "union/a backslash refused");
    CHECK(u.Count() == 1, "union/a refused name never becomes an entry");

    // Monotone: there is no way to remove.  That is the design, not an omission.
    u.Reset();
    CHECK(u.Count() == 0, "union/reset empties it");

    // Deterministic order, so two machines' wire bodies can be compared.
    Union v;
    v.Merge("bbb");
    v.Merge("aaa");
    v.Merge("ccc");
    CHECK(strcmp(v.SortedAt(0), "aaa") == 0, "union/sorted 0");
    CHECK(strcmp(v.SortedAt(1), "bbb") == 0, "union/sorted 1");
    CHECK(strcmp(v.SortedAt(2), "ccc") == 0, "union/sorted 2");
    CHECK(v.SortedAt(3) == nullptr, "union/sorted past the end");

    // The whole universe plus headroom fits; an overflow is REPORTED.
    Union big;
    char name[32];
    for (int i = 0; i < kMaxEntries; ++i) {
        snprintf(name, sizeof(name), "Point_%03d", i);
        big.Merge(name);
    }
    CHECK(big.Count() == kMaxEntries, "union/full at the cap");
    CHECK(!big.Overflowed(), "union/at the cap is not an overflow");
    CHECK(!big.Merge("One_Too_Many"), "union/a name past the cap is refused");
    CHECK(big.Overflowed(), "union/and the overflow is reported");
}

static void TestPerPlayerExclusions()
{
    // The four save members the brief names.
    CHECK(IsPerPlayerProperty("LastTouchedCheckpoint"),
          "per-player/LastTouchedCheckpoint is never mirrored");
    CHECK(IsPerPlayerProperty("SavePointID"), "per-player/SavePointID is never mirrored");
    CHECK(IsPerPlayerProperty("SaveLocation"), "per-player/SaveLocation is never mirrored");
    CHECK(!IsPerPlayerProperty("UnlockedTeleporters"),
          "per-player/a mirrored SET is not excluded");
    CHECK(!IsPerPlayerProperty("SavePointsVisited"),
          "per-player/the visited set is not the respawn point");
    CHECK(!IsPerPlayerProperty("ShopTeleporterDestinations"),
          "per-player/the shop pads are a set");
    // A prefix test would be a bug: a real field that starts with an excluded name.
    CHECK(!IsPerPlayerProperty("LastTouchedCheckpointUnlockCount"),
          "per-player/exact names only, no prefix match");
    // And the set table.
    CHECK(strcmp(SetProperty(SET_TELEPORTERS), "UnlockedTeleporters") == 0, "sets/teleporters property");
    CHECK(strcmp(SetProperty(SET_SAVEPOINTS), "SavePointsVisited") == 0, "sets/savepoints property");
    CHECK(strcmp(SetProperty(SET_SHOP_TP), "ShopTeleporterDestinations") == 0, "sets/shop-tp property");
    CHECK(SetValid(SET_TELEPORTERS) && SetValid(SET_SAVEPOINTS) && SetValid(SET_SHOP_TP), "sets/ids valid");
    CHECK(!SetValid(SET_COUNT), "sets/SET_COUNT is not a set");
    CHECK(!SetValid(SET_NONE), "sets/SET_NONE is not a set");
}

static void TestSetWire()
{
    Union u;
    u.Merge("Teleporter_100_GR");
    u.Merge("Teleporter_200_AQ");
    u.Merge("Teleporter_300_LIB");

    uint8_t buf[512];
    wire::Writer w(buf, sizeof(buf));
    CHECK(WriteSetHeader(w, SET_TELEPORTERS, 7, (uint16_t)u.Count()), "set-wire/header");
    int next = WriteSetChunk(w, u, 0);
    CHECK(next == u.Count(), "set-wire/all rows fit one body");
    CHECK(w.ok, "set-wire/writer stayed valid");

    wire::Reader r(buf, w.len);
    SetHeader h;
    CHECK(ReadSetHeader(r, &h), "set-wire/header reads back");
    CHECK(h.set == SET_TELEPORTERS, "set-wire/set id survives");
    CHECK(h.rev == 7, "set-wire/revision survives");
    CHECK(h.n == u.Count(), "set-wire/the container size survives");

    SetRow rows[8];
    int n = ReadSetChunk(r, rows, 8);
    CHECK(n == u.Count(), "set-wire/all rows read back");
    CHECK(strcmp(rows[0].key, "Teleporter_100_GR") == 0, "set-wire/sorted order on the wire");
    CHECK(strcmp(rows[2].key, "Teleporter_300_LIB") == 0, "set-wire/last row");

    // A bad set id is refused rather than becoming a receiver-side switch default.
    wire::Writer bad(buf, sizeof(buf));
    CHECK(!WriteSetHeader(bad, SET_COUNT, 1, 1), "set-wire/an invalid set id is refused");

    // A continuation: the cursor is the API, never a fragment (WIRE.md 3.6).
    CHECK(WriteSetChunk(w, u, 2) == 3, "set-wire/a cursor past the rows does nothing");
    CHECK(WriteSetChunk(w, u, 9) == u.Count(), "set-wire/a cursor past the end clamps");
}

// ---------------------------------------------------------------------------
// Item 5 -- distinct world events count as a sum
// ---------------------------------------------------------------------------
static void TestUnionCounters()
{
    CHECK(IsUnionCounter("TotalChestsOpened"), "counters/TotalChestsOpened is a sum");
    CHECK(IsUnionCounter("TotalSecretDoorsOpened"), "counters/TotalSecretDoorsOpened is a sum");
    CHECK(IsUnionCounter("ShopAmountSpent"), "counters/ShopAmountSpent is a sum");
    // The counters derived from the mirrored SETS must not be summed: two players
    // unlocking the same destination is one destination.
    CHECK(!IsUnionCounter("TeleporterUnlockCount"), "counters/teleporter count is derived from the set");
    CHECK(!IsUnionCounter("CheckpointUnlockCount"), "counters/checkpoint count is derived from the set");
    // Per-player collections are not a union of counters either.
    CHECK(!IsUnionCounter("TotalAchievementWeapons"), "counters/weapon total is per player");
    CHECK(!IsUnionCounter("Arjaot_State"), "counters/a story flag is not a counter");
    CHECK(!IsUnionCounter(nullptr), "counters/null is not a counter");

    // The delta rule: only a RISE contributes.
    CHECK(CounterDelta(5, 3) == 2, "counters/a rise is the delta");
    CHECK(CounterDelta(3, 5) == 0, "counters/a fall contributes nothing");
    CHECK(CounterDelta(4, 4) == 0, "counters/no change contributes nothing");

    // The two machines open DIFFERENT chests; the true total is the sum, and the old
    // monotone-max rule would have said 5.
    int32_t host = 5, peer = 5;
    host = CounterNewValue(host, CounterDelta(peer, 0));   // 5 + 5
    CHECK(host == 10, "counters/two players' chests sum, not max");
    CHECK(CounterNewValue(10, 0) == 10, "counters/a zero delta changes nothing");
    CHECK(CounterNewValue(100000000, 5) == 100000000, "counters/the sum is bounded");
}

// ---------------------------------------------------------------------------
// Item 4 -- a purchase is local; stock does not cross
// ---------------------------------------------------------------------------
static void TestShopStock()
{
    // The stock keys, in the composition the bank actually uses.
    CHECK(IsLocalShopStock("ShopPurchasedKey"), "shop/a bare stock key stays local");
    CHECK(IsLocalShopStock("Shop1_ShopPurchasedKey"), "shop/a composed stock key stays local");
    CHECK(IsLocalShopStock("ShopPurchases"), "shop/the purchase log stays local");
    CHECK(IsLocalShopStock("ShopSales"), "shop/the sell log stays local");
    CHECK(IsLocalShopStock("Shop_NewItemsInShop"), "shop/the badge stays local");

    // The pair's shared shop progression still crosses.
    CHECK(!IsLocalShopStock("ShopAmountSpent"), "shop/spend still crosses (the shared quest)");
    CHECK(!IsLocalShopStock("ShopUnlockFinished"), "shop/unlock still crosses");
    CHECK(!IsLocalShopStock("ShopkeepUnlock1"), "shop/keep unlock still crosses");
    CHECK(!IsLocalShopStock("ShopProgressionKey/GoldDuckShopUnlock"), "shop/duck progression still crosses");
    CHECK(!IsLocalShopStock("TotalChestsOpened"), "shop/an unrelated key is not stock");
    CHECK(!IsLocalShopStock(nullptr), "shop/null is not stock");

    // One predicate for all three sides of the lane.
    CHECK(FlagStaysLocal("ShopPurchasedKey"), "shop/FlagStaysLocal agrees with the stock rule");
    CHECK(!FlagStaysLocal("ShopAmountSpent"), "shop/FlagStaysLocal lets the shareable key through");
}

// ---------------------------------------------------------------------------
// Item 2 -- a rest is ONE episode, ordered, and bosses never revive
// ---------------------------------------------------------------------------
static void TestRestEpisode()
{
    Episode e;
    uint32_t s1 = e.NoteLocal(1000, "GR_Camp_Save");
    CHECK(s1 != 0, "rest/the first local rest has a sequence");
    // A checkpoint fires `PlayerRested` more than once for one rest (its own refresh).
    uint32_t s1again = e.NoteLocal(1500, "GR_Camp_Save");
    CHECK(s1again == s1, "rest/a repeat inside the window is the same episode");
    uint32_t s2 = e.NoteLocal(1000 + kEpisodeMs + 1, "Swamp_1_save");
    CHECK(s2 != s1, "rest/a later rest is a new episode");

    // Peer -> host is a REQUEST; the host does not act on its own echo.
    Episode p;
    CHECK(p.OnPeer(4, "GR_Camp_Save", false, 100, false) == RV_REQUEST,
          "rest/a peer's rest is a request to the host");
    CHECK(p.OnPeer(4, "GR_Camp_Save", false, 120, true) == RV_DUPLICATE,
          "rest/the host's order for the same episode is a duplicate");
    CHECK(p.OnPeer(5, "Swamp_1_save", false, 200, true) == RV_APPLY,
          "rest/a newer order applies");
    CHECK(p.OnPeer(3, "old", false, 300, true) == RV_STALE,
          "rest/an older episode never resets the world again");
    CHECK(p.OnPeer(0, "", false, 400, true) == RV_REFUSED, "rest/sequence 0 is refused");
    CHECK(p.Applied() == 2, "rest/exactly two episodes applied");
    CHECK(strcmp(p.Point(), "Swamp_1_save") == 0, "rest/the last point is kept for the log");

    // The host applies its own rest and broadcasts it as an order.
    Episode h;
    CHECK(h.OnPeer(6, "Lib_Archives_Save", true, 500, false) == RV_APPLY,
          "rest/the host applies a rest it heard as a request");
    CHECK(h.OnPeer(6, "Lib_Archives_Save", true, 560, true) == RV_DUPLICATE,
          "rest/the broadcast of what it just applied is not a second reset");

    // Bosses and one-time enemies never reset.
    CHECK(ResetExempt(true, false, false), "rest/a boss is exempt");
    CHECK(ResetExempt(false, true, false), "rest/a one-time spawner is exempt");
    CHECK(ResetExempt(false, false, true), "rest/a flag-gated spawner is exempt");
    CHECK(!ResetExempt(false, false, false), "rest/an ordinary enemy is reset");

    Plan pl = MakePlan(true, 3, 40, 9);
    CHECK(pl.clear, "rest/an accepted episode clears the table");
    CHECK(pl.keep == 3, "rest/three exempt keys are re-stated as dead");
    CHECK(pl.release == 40, "rest/forty ordinary enemies come back");
    Plan refused = MakePlan(false, 3, 40, 0);
    CHECK(!refused.clear && refused.keep == 0, "rest/a refused episode touches nothing");
}

static void TestRestWire()
{
    Body b;
    memset(&b, 0, sizeof(b));
    b.seq = 12;
    b.flags = kRestOrder | kRestLocal;
    b.px = 4106.5f; b.py = -1744.25f; b.pz = 146.0f;
    strcpy(b.name, "GR_Camp_Save");

    uint8_t buf[256];
    wire::Writer w(buf, sizeof(buf));
    CHECK(Encode(w, b), "rest-wire/encode");
    wire::Reader r(buf, w.len);
    Body got;
    CHECK(Decode(r, &got), "rest-wire/decode");
    CHECK(got.seq == 12, "rest-wire/seq survives");
    CHECK(got.flags == (kRestOrder | kRestLocal), "rest-wire/flags survive");
    CHECK(strcmp(got.name, "GR_Camp_Save") == 0, "rest-wire/the point name survives");
    CHECK(got.px > 4106.0f && got.px < 4107.0f, "rest-wire/the position is quantised, not lost");

    Body bad2 = b;
    bad2.seq = 0;
    wire::Writer w2(buf, sizeof(buf));
    CHECK(!Encode(w2, bad2), "rest-wire/sequence 0 is not encodable");
    Body bad3 = b;
    bad3.flags = 0xFF;
    wire::Writer w3(buf, sizeof(buf));
    CHECK(!Encode(w3, bad3), "rest-wire/an unknown flag bit is refused");
    Body bad4 = b;
    volatile float zero = 0.0f;
    bad4.px = zero / zero;                    // a runtime NaN, not a constant fold
    wire::Writer w4(buf, sizeof(buf));
    CHECK(!Encode(w4, bad4), "rest-wire/NaN is refused");
}

// ---------------------------------------------------------------------------
// Item 3 -- the intermediate state of a placed puzzle prop
// ---------------------------------------------------------------------------
static void MakeKey(uint32_t k[4], uint32_t a)
{
    for (int i = 0; i < 4; ++i) k[i] = a + (uint32_t)i * 7919u;
}

static void TestPuzzleBoard()
{
    Board b;
    uint32_t key[4];
    MakeKey(key, 111);
    uint32_t seq = 0;
    Row out;
    memset(&out, 0, sizeof(out));

    // The first observation is news (a book turned for the first time here).
    CHECK(b.ObserveLocal(key, 8192, 0, 0, &seq, &out), "puzzle/a turn is published");
    CHECK(seq == 1, "puzzle/the first sequence is 1");
    CHECK(out.yaw == 8192, "puzzle/the row carries the pose");

    // The same pose again is not news: a book's rotation is read every fixture tick.
    CHECK(!b.ObserveLocal(key, 8192, 0, 0, &seq, &out), "puzzle/an unchanged pose is not published");
    // One quantiser unit of noise is not a turn.
    CHECK(!b.ObserveLocal(key, 8193, 0, 0, &seq, &out), "puzzle/a one-unit wobble is not a turn");
    // A real turn is.
    CHECK(b.ObserveLocal(key, 16384, 0, 0, &seq, &out), "puzzle/a real turn is published");
    CHECK(seq == 2, "puzzle/the sequence rises with each real turn");

    // The partner's row applies, and then must NOT be echoed back.
    Board p;
    Row peer;
    memset(&peer, 0, sizeof(peer));
    MakeKey(peer.key, 111);
    peer.yaw = 24576;
    peer.seq = 7;
    CHECK(p.OnPeer(peer) == P_OK, "puzzle/the partner's row applies");
    CHECK(p.OnPeer(peer) == P_DUPLICATE, "puzzle/a retransmission is a duplicate");
    uint32_t seq2 = 0;
    Row echo;
    memset(&echo, 0, sizeof(echo));
    CHECK(!p.ObserveLocal(peer.key, 24576, 0, 0, &seq2, &echo),
          "puzzle/the pose just applied is not republished (the echo that ping-pongs)");

    // Last-writer-wins per key.
    peer.seq = 9;
    peer.yaw = 8192;
    CHECK(p.OnPeer(peer) == P_OK, "puzzle/a newer row wins");
    peer.seq = 8;
    peer.yaw = 1;
    CHECK(p.OnPeer(peer) == P_STALE, "puzzle/an older row is refused");
    Row held;
    CHECK(p.Get(peer.key, &held), "puzzle/the key is held");
    CHECK(held.yaw == 8192, "puzzle/the newer pose stands after a stale row");

    // Malformed rows never enter the board.
    Row zero;
    memset(&zero, 0, sizeof(zero));
    CHECK(p.OnPeer(zero) == P_REFUSED, "puzzle/a zero key is refused");
    Row noseq = peer;
    noseq.seq = 0;
    CHECK(p.OnPeer(noseq) == P_REFUSED, "puzzle/a zero sequence is refused");

    // Two keys are independent.
    uint32_t other[4];
    MakeKey(other, 222);
    CHECK(!p.Get(other, &held), "puzzle/an unseen key is absent");
    Row r2;
    memset(&r2, 0, sizeof(r2));
    MakeKey(r2.key, 222);
    r2.yaw = 1234;
    r2.seq = 1;
    CHECK(p.OnPeer(r2) == P_OK, "puzzle/a second key applies on its own sequence");
    CHECK(p.Count() == 2, "puzzle/two props are tracked");
}

static void TestPuzzleWire()
{
    Row rows[3];
    memset(rows, 0, sizeof(rows));
    for (int i = 0; i < 3; ++i) {
        MakeKey(rows[i].key, (uint32_t)(300 + i));
        rows[i].yaw = (int16_t)(100 * i);
        rows[i].pitch = (int16_t)(-7 * i);
        rows[i].state = (uint8_t)i;
        rows[i].seq = (uint32_t)(i + 1);
    }

    uint8_t buf[512];
    wire::Writer w(buf, sizeof(buf));
    CHECK(WriteHeader(w, 42, 3), "puzzle-wire/header");
    CHECK(WriteRows(w, rows, 3, 0) == 3, "puzzle-wire/all rows fit");
    CHECK(w.ok, "puzzle-wire/writer valid");

    wire::Reader r(buf, w.len);
    uint32_t rev = 0;
    uint8_t n = 0;
    CHECK(ReadHeader(r, &rev, &n), "puzzle-wire/header reads back");
    CHECK(rev == 42 && n == 3, "puzzle-wire/header fields survive");

    Row back[4];
    int got = ReadRows(r, back, 4);
    CHECK(got == 3, "puzzle-wire/all rows read back");
    CHECK(back[1].yaw == 100 && back[2].pitch == -14, "puzzle-wire/poses survive");
    CHECK(back[2].state == 2 && back[2].seq == 3, "puzzle-wire/state and sequence survive");
    CHECK(SameKey(back[0].key, rows[0].key), "puzzle-wire/keys survive");
}

int main()
{
    TestUnion();
    TestPerPlayerExclusions();
    TestSetWire();
    TestUnionCounters();
    TestShopStock();
    TestRestEpisode();
    TestRestWire();
    TestPuzzleBoard();
    TestPuzzleWire();

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
