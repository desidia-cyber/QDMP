// test_dialogue2.cpp -- the dialogue spectator lane (K_DIALOGUE_VIEW, 0x50).
//
// What these tests exist for, in the order the live failures would appear:
//   * a spectator is ENGAGED once, not once per frame (re-driving the widget on every
//     pump would re-show the line and re-blend the camera 30 times a second);
//   * the cursor advancing is an UPDATE, and an unchanged heartbeat is NOT;
//   * a player being attacked is not hijacked, and the refusal survives the 1 Hz
//     republish of the same state (otherwise it is re-decided every second);
//   * a genuine next line (a NEW seq) re-opens the question once combat ends;
//   * an ended / forgotten / silent sender HIDES exactly once, so the camera and the
//     input are given back;
//   * an older seq is discarded: a reordered datagram cannot un-end a conversation.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "qddialogue2.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, name)                                                          \
    do {                                                                           \
        if (cond) { ++g_pass; }                                                    \
        else { ++g_fail; printf("FAIL %s  (%s:%d)\n", name, __FILE__, __LINE__); } \
    } while (0)

using namespace qd;
using namespace qd::dlgview;

static void Npc(uint8_t out[16], uint8_t seed)
{
    for (int i = 0; i < 16; ++i) out[i] = (uint8_t)(seed + i);
}

static bool Push(Board& b, uint8_t slot, uint32_t seq, bool talking, uint8_t npcSeed,
                 const char* asset, uint16_t sequence, uint16_t entry, uint16_t options,
                 uint16_t chosen, uint8_t flags, uint32_t now)
{
    uint8_t id[16];
    Npc(id, npcSeed);
    return b.Accept(slot, seq, talking, id, asset, sequence, entry, options, chosen, flags, now);
}

static void TestEngageOnce()
{
    Board b;
    CHECK(b.Decide(0, false, false, 1000) == DV_NONE, "idle/no partner is nothing");
    CHECK(Push(b, 1, 1, true, 7, "Knight_Arjaot_Dialogue_00", 0, 0, 0, kNoOption, 0, 1000),
          "engage/a talking state is accepted");
    CHECK(b.Decide(0, false, false, 1100) == DV_SHOW, "engage/the line is shown once");
    CHECK(b.Showing(), "engage/the board remembers it is showing");
    CHECK(b.Decide(0, false, false, 1200) == DV_NONE,
          "engage/a second frame does not re-show the same line");
    CHECK(strcmp(b.Shown().asset, "Knight_Arjaot_Dialogue_00") == 0, "engage/the asset is kept");
}

static void TestCursorAdvances()
{
    Board b;
    Push(b, 1, 1, true, 7, "Merchant_Dialogue", 0, 0, 0, kNoOption, 0, 1000);
    CHECK(b.Decide(0, false, false, 1100) == DV_SHOW, "cursor/engaged");
    // The same seq republished (the 1 Hz heartbeat) is not an update, but it DOES refresh
    // liveness -- that is what the heartbeat is for.
    CHECK(!Push(b, 1, 1, true, 7, "Merchant_Dialogue", 0, 0, 0, kNoOption, 0, 1500),
          "cursor/a repeat is not new state");
    CHECK(b.Live(1, 1500 + kStaleMs - 1), "cursor/a repeat refreshes liveness");
    CHECK(b.Decide(0, false, false, 1600) == DV_NONE, "cursor/a heartbeat is not an update");
    // The next line: a new seq AND a new cursor.
    CHECK(Push(b, 1, 2, true, 7, "Merchant_Dialogue", 1, 0, 0, kNoOption, 0, 1700),
          "cursor/the next line is accepted");
    CHECK(b.Decide(0, false, false, 1800) == DV_UPDATE, "cursor/the next line updates the widget");
    CHECK(b.Shown().sequence == 1, "cursor/the cursor moved");
    // Options appearing, then a choice being made, are both updates.
    CHECK(Push(b, 1, 3, true, 7, "Merchant_Dialogue", 1, 0, 3, kNoOption, F_OPTIONS, 1900),
          "cursor/options are accepted");
    CHECK(b.Decide(0, false, false, 2000) == DV_UPDATE, "cursor/the option list is an update");
    CHECK(Push(b, 1, 4, true, 7, "Merchant_Dialogue", 1, 1, 3, 1, (uint8_t)(F_OPTIONS | F_CHOSEN), 2100),
          "cursor/the choice is accepted");
    CHECK(b.Decide(0, false, false, 2200) == DV_UPDATE, "cursor/the choice updates the widget");
    CHECK(b.Shown().chosen == 1, "cursor/the chosen option is visible");
}

static void TestCombatRefusesAndIsNotRetried()
{
    Board b;
    Push(b, 1, 1, true, 7, "Knight_Budaen_Dialogue_00", 0, 0, 0, kNoOption, 0, 1000);
    CHECK(b.Decide(0, false, true, 1100) == DV_NONE,
          "combat/a player in combat is not hijacked");
    CHECK(!b.Showing(), "combat/nothing was shown");
    // The heartbeat republishes the SAME seq: the refusal must hold silently.
    Push(b, 1, 1, true, 7, "Knight_Budaen_Dialogue_00", 0, 0, 0, kNoOption, 0, 1600);
    CHECK(b.Decide(0, false, false, 1700) == DV_NONE,
          "combat/the refusal survives the republish after combat ends");
    // A genuine next line re-opens the question -- and now engages.
    Push(b, 1, 2, true, 7, "Knight_Budaen_Dialogue_00", 1, 0, 0, kNoOption, 0, 1800);
    CHECK(b.Decide(0, false, false, 1900) == DV_SHOW,
          "combat/a new line after combat engages");
}

static void TestCombatReleasesAMidDialogueHijack()
{
    Board b;
    Push(b, 1, 1, true, 7, "Merchant_Dialogue", 0, 0, 0, kNoOption, 0, 1000);
    CHECK(b.Decide(0, false, false, 1100) == DV_SHOW, "release/engaged");
    // An enemy arrives: give the camera and the legs back immediately.
    CHECK(b.Decide(0, false, true, 1200) == DV_HIDE, "release/combat hides the view");
    CHECK(!b.Showing(), "release/nothing is showing");
    Push(b, 1, 1, true, 7, "Merchant_Dialogue", 0, 0, 0, kNoOption, 0, 1300);
    CHECK(b.Decide(0, false, false, 1400) == DV_NONE,
          "release/the same publication does not re-engage");
}

static void TestBusyLocalPlayer()
{
    Board b;
    Push(b, 1, 1, true, 7, "Merchant_Dialogue", 0, 0, 0, kNoOption, 0, 1000);
    CHECK(b.Decide(0, true, false, 1100) == DV_NONE,
          "busy/a local conversation or cutscene is not taken over");
    Push(b, 1, 2, true, 7, "Merchant_Dialogue", 1, 0, 0, kNoOption, 0, 1200);
    CHECK(b.Decide(0, true, false, 1300) == DV_NONE, "busy/a new line is still not taken over");
}

static void TestEndHidesOnce()
{
    Board b;
    Push(b, 1, 1, true, 7, "Merchant_Dialogue", 0, 0, 0, kNoOption, 0, 1000);
    CHECK(b.Decide(0, false, false, 1100) == DV_SHOW, "end/engaged");
    // The talker ends: slot free, new seq, empty asset.
    CHECK(Push(b, 1, 2, false, 0, "", 0, 0, 0, kNoOption, 0, 1200), "end/ended is accepted");
    CHECK(b.Decide(0, false, false, 1300) == DV_HIDE, "end/hides exactly once");
    CHECK(b.Decide(0, false, false, 1400) == DV_NONE, "end/a second frame is nothing");
}

static void TestForgetAndStale()
{
    Board b;
    Push(b, 1, 1, true, 7, "Merchant_Dialogue", 0, 0, 0, kNoOption, 0, 1000);
    CHECK(b.Decide(0, false, false, 1100) == DV_SHOW, "forget/engaged");
    b.Forget(1);
    CHECK(b.Decide(0, false, false, 1200) == DV_HIDE, "forget/a disconnected partner hides the view");

    Board c;
    Push(c, 1, 1, true, 7, "Merchant_Dialogue", 0, 0, 0, kNoOption, 0, 1000);
    CHECK(c.Decide(0, false, false, 1100) == DV_SHOW, "stale/engaged");
    CHECK(c.Decide(0, false, false, 1000 + kStaleMs + 1) == DV_HIDE,
          "stale/a silent sender releases the spectator");
}

static void TestOrdering()
{
    Board b;
    CHECK(Push(b, 1, 5, true, 7, "Merchant_Dialogue", 0, 0, 0, kNoOption, 0, 1000),
          "order/new seq accepted");
    CHECK(!Push(b, 1, 4, true, 7, "Merchant_Dialogue", 9, 0, 0, kNoOption, 0, 1100),
          "order/an older seq is discarded");
    CHECK(!Push(b, 1, 5, true, 7, "Merchant_Dialogue", 9, 0, 0, kNoOption, 0, 1100),
          "order/a repeated seq does not re-apply");
    CHECK(b.Decide(0, false, false, 1200) == DV_SHOW, "order/shows");
    CHECK(b.Shown().sequence == 0, "order/a reordered datagram cannot move the cursor");
    CHECK(Push(b, 1, 6, true, 7, "Merchant_Dialogue", 2, 0, 0, kNoOption, 0, 1300),
          "order/a later seq is accepted");
    CHECK(b.Decide(0, false, false, 1400) == DV_UPDATE, "order/the later seq advances");
}

static void TestEmptyAssetAndReset()
{
    Board b;
    Push(b, 1, 1, true, 7, "", 0, 0, 0, kNoOption, 0, 1000);
    CHECK(b.Decide(0, false, false, 1100) == DV_NONE,
          "empty/nothing to show without an asset name is not a hijack");
    Push(b, 1, 2, true, 7, "Merchant_Dialogue", 0, 0, 0, kNoOption, 0, 1200);
    CHECK(b.Decide(0, false, false, 1300) == DV_SHOW, "reset/engaged");
    b.Reset();
    CHECK(!b.Showing(), "reset/cleared");
    CHECK(b.Decide(0, false, false, 1400) == DV_NONE, "reset/nothing after reset");
    // A reset also clears the declined record, so a NEW session's low seqs are usable.
    Push(b, 1, 1, true, 7, "Merchant_Dialogue", 0, 0, 0, kNoOption, 0, 1500);
    CHECK(b.Decide(0, false, false, 1600) == DV_SHOW, "reset/a new session can engage again");
}

static void TestSlotsAreIndependent()
{
    Board b;
    Push(b, 0, 1, true, 3, "Chef_Dialogue_00", 0, 0, 0, kNoOption, 0, 1000);
    Push(b, 1, 1, true, 7, "Merchant_Dialogue", 0, 0, 0, kNoOption, 0, 1000);
    // Machine 0 sees slot 1's view, machine 1 sees slot 0's.
    CHECK(b.Decide(0, false, false, 1100) == DV_SHOW, "slots/engaged on the partner's view");
    CHECK(strcmp(b.Shown().asset, "Merchant_Dialogue") == 0, "slots/the partner's asset, not own");
    b.NoteClosed();
    CHECK(b.Decide(1, false, false, 1200) == DV_SHOW, "slots/the other machine shows the other view");
    CHECK(strcmp(b.Shown().asset, "Chef_Dialogue_00") == 0, "slots/the other asset");
}

int main()
{
    printf("=== dialogue spectator board (K_DIALOGUE_VIEW 0x50) ===\n");
    TestEngageOnce();
    TestCursorAdvances();
    TestCombatRefusesAndIsNotRetried();
    TestCombatReleasesAMidDialogueHijack();
    TestBusyLocalPlayer();
    TestEndHidesOnce();
    TestForgetAndStale();
    TestOrdering();
    TestEmptyAssetAndReset();
    TestSlotsAreIndependent();
    printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
