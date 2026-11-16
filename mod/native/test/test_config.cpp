// test_config.cpp -- does the SHIPPED qdcoop.ini actually arrive?
//
// WHY THIS TEST EXISTS
// --------------------
// MEASURED (final soak, 16 Nov): the shipped `mod/qdcoop.ini` had grown to 21265 bytes and
// `ConfigLoad` read a FIXED 8192 bytes in one `ReadFile`.  Every switch declared after byte
// 8191 therefore kept its COMPILED DEFAULT (0) no matter what the file said:
//
//     PlayerBody, DialogueView, StreamUnion, TravelSync, CutsceneSync, MoverSync,
//     PlatformRide, LootSync, SuspendGuard, SuppressMenuPause, CombatVfx, DeathRespawn,
//     QuestItemSync, KillXPSync, PartnerSaveSync
//
// Thirteen switches that the file sets to 1 ran at 0.  That is the worst shape a bug can
// take here: the product LOOKS configured, the boot line even printed the truth
// (`playerBody=0 dialogueView=0 ...`), and every per-lane live run passed because each of
// those runs wrote its own small ini via the harness rather than shipping one.
//
// WHY THE TEST IS SHAPED THIS WAY
// -------------------------------
// A parser fed a truncated string is perfectly correct -- it parses what it was given.  The
// only assertion that catches this is "did the LAST switch in the REAL FILE arrive", so this
// test reads the repository's own `qdcoop.ini`, asserts its size is past the old buffer, and
// then asserts the values that live at the END of it.  A future buffer regression fails here
// rather than in a two-machine session.
//
// It also pins the properties that must keep working: a missing file means defaults, a
// comment line is skipped, an unknown key is ignored (a newer config on an older DLL must
// still load), and the two clamps at the end of the parse still apply.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "qdconfig.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, name)                                                          \
    do {                                                                           \
        if (cond) { ++g_pass; }                                                    \
        else { ++g_fail; printf("FAIL %s  (%s:%d)\n", name, __FILE__, __LINE__); } \
    } while (0)

// Read a whole file into `out`.  Returns the byte count, or -1.
static int ReadWhole(const char* path, char* out, int cap)
{
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    int n = (int)fread(out, 1, (size_t)(cap - 1), f);
    fclose(f);
    if (n < 0 || n >= cap - 1) return -1;
    out[n] = 0;
    return n;
}

// The test may run from test\, test\build\ or mod\ -- the same bounded walk the vector test
// uses, so a run from a second worktree cannot silently validate the OTHER checkout.
static const char* FindShippedIni(char* out, int cap)
{
    static const char* candidates[] = {
        "../qdcoop.ini",
        "../../qdcoop.ini",
        "../../../qdcoop.ini",
        "../../../../qdcoop.ini",
        "qdcoop.ini",
    };
    for (int i = 0; i < (int)(sizeof(candidates) / sizeof(candidates[0])); ++i) {
        int n = ReadWhole(candidates[i], out, cap);
        if (n > 0) return candidates[i];
    }
    return nullptr;
}

int main()
{
    printf("=== qdcoop.ini load tests ===\n");

    // ---- 1. the shipped file is read WHOLE -------------------------------------
    // The buffer is deliberately the size of the whole file, because the assertion is about
    // the file's own tail.
    static char section[64 * 1024];
    const char* used = FindShippedIni(section, (int)sizeof(section));
    if (!used) {
        printf("SKIP  shipped qdcoop.ini not reachable from the working directory\n");
        printf("=== 0 passed, 0 failed (skipped) ===\n");
        return 0;
    }
    int fileLen = (int)strlen(section);
    printf("      file: %s (%d bytes)\n", used, fileLen);

    // THE REGRESSION ITSELF.  The old loader's limit was 8192 bytes; the file must be bigger
    // than that or this test is not exercising the bug it exists for.
    CHECK(fileLen > 8192, "the shipped ini is larger than the old fixed 8192-byte read limit");

    // ---- 2. every switch the file turns ON arrives as 1 ------------------------
    // Parsed from the REAL file text, exactly as ConfigLoad does after the read.
    qd::ConfigDefaults();
    char* copy = (char*)malloc((size_t)fileLen + 1);
    memcpy(copy, section, (size_t)fileLen + 1);
    int lines = qd::ConfigParseText(copy);
    free(copy);
    CHECK(lines > 100, "the parse saw the whole file (more than 100 lines)");

    qd::Config& c = qd::Cfg();
    // The thirteen that the truncation silently dropped.  Named individually rather than
    // looped, so a failure says WHICH product switch did not survive.
    CHECK(c.player_body == 1,     "PlayerBody=1 arrived (declared past byte 8191)");
    CHECK(c.dialogue_view == 1,   "DialogueView=1 arrived (declared past byte 8191)");
    CHECK(c.stream_union == 1,    "StreamUnion=1 arrived");
    CHECK(c.mover_sync == 1,      "MoverSync=1 arrived");
    CHECK(c.platform_ride == 1,   "PlatformRide=1 arrived");
    CHECK(c.loot_sync == 1,       "LootSync=1 arrived");
    CHECK(c.suspend_guard == 1,   "SuspendGuard=1 arrived");
    CHECK(c.suppress_pause == 1,  "SuppressMenuPause=1 arrived");
    CHECK(c.combat_vfx == 1,      "CombatVfx=1 arrived");
    CHECK(c.death_respawn == 1,   "DeathRespawn=1 arrived");
    CHECK(c.quest_item_sync == 1, "QuestItemSync=1 arrived");
    CHECK(c.kill_xp_sync == 1,    "KillXPSync=1 arrived");
    CHECK(c.partner_save_sync == 1, "PartnerSaveSync=1 arrived (the LAST switch in the file)");
    // ... and the early ones still work, so the fix did not trade one half for the other.
    CHECK(c.enabled == 1,          "Enabled=1 arrived");
    CHECK(c.flag_sync == 1,        "FlagSync=1 arrived");
    CHECK(c.inventory_sync == 1,   "InventorySync=1 arrived");
    CHECK(c.actor_state_sync == 1, "ActorStateSync=1 arrived");
    CHECK(c.object_sync == 1,      "ObjectSync=1 arrived");
    CHECK(c.interact_sync == 1,    "InteractSync=1 arrived");
    CHECK(c.enemy_sync == 1,       "EnemySync=1 arrived");
    CHECK(c.dialogue_sync == 1,    "DialogueSync=1 arrived");
    CHECK(c.travel_sync == 1,      "TravelSync=1 arrived");
    CHECK(c.cutscene_sync == 1,    "CutsceneSync=1 arrived");
    CHECK(c.host_authoritative == 1, "HostAuthoritative=1 arrived");
    // The shipped file pins LogLevel=2 and a loopback relay; the harness rewrites both.
    CHECK(c.relay_port == 47800,   "RelayPort arrived");
    CHECK(strcmp(c.relay_host, "127.0.0.1") == 0, "RelayHost arrived");

    // ---- 3. defaults: a missing file means "off", never a half-set ------------
    qd::ConfigDefaults();
    qd::Config& d = qd::Cfg();
    CHECK(d.player_body == 0 && d.dialogue_view == 0 && d.combat_vfx == 0 &&
          d.death_respawn == 0 && d.kill_xp_sync == 0 && d.partner_save_sync == 0,
          "every write path defaults OFF when no ini is present");
    CHECK(d.enabled == 0 || d.enabled == 1, "enabled has a defined default");
    CHECK(d.host_authoritative == 1, "the authority default stays ON");
    CHECK(d.boss_pull_in == 1, "BossPullIn defaults ON (it accompanies the trigger lane)");

    // ---- 4. parser rules that must not regress -------------------------------
    // A comment line, a section header, an unknown key, and a malformed line.
    static char sample[] =
        "; a comment\n"
        "# another comment style\n"
        "[Rules]\n"
        "UnknownKeyFromANewerBuild=1\n"
        "NotAnAssignment\n"
        "LootSync=1\n"
        "PlayerHz=45\n"        // clamped to 30 by the parser
        "EnemyRowsPerTick=99\n" // capped at 20
        "RelayHost=10.0.0.5\n";
    qd::ConfigDefaults();
    int n2 = qd::ConfigParseText(sample);
    qd::Config& s = qd::Cfg();
    CHECK(n2 > 0, "the parser reports a line count");
    CHECK(s.loot_sync == 1, "a switch after an unknown key still applies");
    CHECK(s.player_hz == 30, "PlayerHz=45 is clamped to 30 (not 60, not 45)");
    CHECK(s.enemy_rows_per_tick == 20, "EnemyRowsPerTick is capped at 20");
    CHECK(strcmp(s.relay_host, "10.0.0.5") == 0, "RelayHost is copied verbatim");
    // The unknown key must NOT have become a switch by accident.  `enabled` defaults to 1,
    // so the probe is a field the file does NOT touch and whose default is 0.
    CHECK(s.enabled == 1, "the master switch keeps its default (an unknown key changed nothing)");
    CHECK(s.object_scan == 0, "an unknown key did not turn on a real switch");

    // ---- 5. an empty file is not a crash -------------------------------------
    static char empty[] = "";
    qd::ConfigDefaults();
    (void)qd::ConfigParseText(empty);
    CHECK(qd::Cfg().player_body == 0, "an empty file leaves defaults intact");

    // ---- 6. a truncated file is exactly what the old code produced -----------
    // This is the shape of the old bug, kept as a guard on the fix's REASON: if the shipped
    // file ever shrinks below 8192 the regression test above stops exercising anything, and
    // this says so instead of passing quietly.
    if (fileLen > 8192) {
        static char cut[8193];
        memcpy(cut, section, 8192);
        cut[8192] = 0;
        qd::ConfigDefaults();
        (void)qd::ConfigParseText(cut);
        qd::Config& t = qd::Cfg();
        // The truncated text cannot see the tail switch, which is why the fix reads it all.
        CHECK(t.partner_save_sync == 0,
              "the first 8192 bytes alone do NOT contain PartnerSaveSync (proof the limit was the bug)");
        CHECK(t.dialogue_view == 0,
              "the first 8192 bytes alone do NOT contain DialogueView either");
    }

    printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
