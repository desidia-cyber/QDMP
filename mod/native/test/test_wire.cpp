// test_wire.cpp -- the native side of the wire format has to agree with the Rust
// side byte for byte, and with doc/WIRE.md.  These are the checks that make that
// true rather than hoped for.
//
// WHY THIS IS NOT PART OF THE SUBSYSTEM TESTS
// -------------------------------------------
// qdmotion/qdpuppet/qdstream/qddialogue/qdflags are pure state machines with no
// encoding.  qdwire.h is a format contract with a SECOND IMPLEMENTATION in Rust, so
// two different tests matter and they are separate files:
//
//   test_wire.cpp     (this file) round-trip, boundary and refusal behaviour.  A
//                     round-trip test alone passes for a symmetric bug, so it is not
//                     the cross-implementation check.
//   test_vectors.cpp  the actual cross-implementation lock: it rebuilds the samples
//                     that `cargo test -p qd-core` used and compares against
//                     doc/vectors.txt byte for byte.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "qdwire.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, name)                                                      \
    do {                                                                       \
        if (cond) { ++g_pass; printf("PASS %s\n", name); }                     \
        else { ++g_fail; printf("FAIL %s (%s:%d)\n", name, __FILE__, __LINE__); } \
    } while (0)

#define CHECK_BYTES(a, b, n, name)                                             \
    do {                                                                       \
        bool eq = true;                                                        \
        for (int _i = 0; _i < (n); ++_i) if (((const uint8_t*)(a))[_i] != ((const uint8_t*)(b))[_i]) { eq = false; break; } \
        if (eq) { ++g_pass; printf("PASS %s\n", name); }                       \
        else {                                                                 \
            ++g_fail;                                                          \
            printf("FAIL %s\n  got      :", name);                             \
            for (int _i = 0; _i < (n); ++_i) printf(" %02X", ((const uint8_t*)(a))[_i]); \
            printf("\n  expected :");                                          \
            for (int _i = 0; _i < (n); ++_i) printf(" %02X", ((const uint8_t*)(b))[_i]); \
            printf("\n");                                                      \
        }                                                                      \
    } while (0)

using namespace qd;

// Declared before first use; defined below.  The test deliberately does not include
// qdsubstrate.h (which needs <windows.h>) so it stays buildable as a plain console
// program.
static bool NameEqualsC(const char* a, const char* b);

// ---------------------------------------------------------------------------
static void TestEnvelope()
{
    uint8_t buf[64];
    wire::Writer w(buf, sizeof(buf));
    wire::WriteHeader(w, wire::K_PLAYER, 0x11223344u, 7u, 0x0012u);

    const uint8_t expect[] = {
        0x51, 0x44, 0x43, 0x31,   // 'Q','D','C','1'
        0x01,                     // ver
        0x10,                     // kind K_PLAYER
        0x12, 0x00,               // body_len 18
        0x44, 0x33, 0x22, 0x11,   // session
        0x07, 0x00, 0x00, 0x00,   // seq
    };
    CHECK(w.ok, "envelope/encodes");
    CHECK(w.len == 16, "envelope/is 16 bytes");
    CHECK_BYTES(buf, expect, 16, "envelope/exact bytes");

    // The declared body must actually be present, or ReadHeader is right to refuse --
    // see envelope/rejects truncated body, which asserts exactly that.
    for (int i = 0; i < 18; ++i) w.U8((uint8_t)i);

    wire::Reader r(buf, w.len);
    wire::Envelope e;
    CHECK(wire::ReadHeader(r, &e), "envelope/decodes");
    CHECK(e.magic == wire::kMagic, "envelope/magic");
    CHECK(e.ver == 1, "envelope/version");
    CHECK(e.kind == wire::K_PLAYER, "envelope/kind");
    CHECK(e.body_len == 18, "envelope/body_len");
    CHECK(e.session == 0x11223344u, "envelope/session");
    CHECK(e.seq == 7u, "envelope/seq");
    CHECK(r.pos == 16, "envelope/body starts at offset 16");
}

static void TestEnvelopeRejects()
{
    // A wrong magic must be a refusal, never a best-effort parse.
    uint8_t bad[16];
    memset(bad, 0, sizeof(bad));
    bad[0] = 'X';
    wire::Reader r(bad, sizeof(bad));
    wire::Envelope e;
    CHECK(!wire::ReadHeader(r, &e), "envelope/rejects bad magic");

    // A body_len beyond the cap must be a refusal: it is how an attacker (or a bug)
    // turns a 16-byte header into a 60 KB read.
    uint8_t buf[16];
    wire::Writer w(buf, sizeof(buf));
    wire::WriteHeader(w, wire::K_PLAYER, 1, 1, (uint16_t)(wire::kMaxBody + 1));
    wire::Reader r2(buf, sizeof(buf));
    CHECK(!wire::ReadHeader(r2, &e), "envelope/rejects over-cap body_len");

    // A body_len longer than the datagram must be a refusal too.
    wire::Writer w2(buf, sizeof(buf));
    wire::WriteHeader(w2, wire::K_PLAYER, 1, 1, 40);
    wire::Reader r3(buf, 20);
    CHECK(!wire::ReadHeader(r3, &e), "envelope/rejects truncated body");
}

// ---------------------------------------------------------------------------
static void TestPlayer()
{
    wire::PlayerRow in;
    memset(&in, 0, sizeof(in));
    in.slot = 1;
    in.sample_ms = 0x000003E8u;
    in.px = 8; in.py = -16; in.pz = 100;
    in.yaw = 16384; in.pitch = -8192;
    in.vx = 8; in.vy = 0; in.vz = -8;
    in.move_mode = 3;
    in.action = 1;
    in.flags = 0x05;

    uint8_t buf[128];
    wire::Writer w(buf, sizeof(buf));
    wire::WritePlayer(w, in);
    CHECK(w.ok, "player/encodes");

    // 1+4+12+4+6+1+1+1 = 30
    CHECK(w.len == 30, "player/is 30 bytes");

    wire::Reader r(buf, w.len);
    wire::PlayerRow out;
    CHECK(wire::ReadPlayer(r, &out), "player/decodes");
    CHECK(r.pos == w.len, "player/consumes exactly its bytes");
    CHECK(out.slot == in.slot, "player/slot round-trip");
    CHECK(out.sample_ms == in.sample_ms, "player/sample_ms round-trip");
    CHECK(out.px == in.px && out.py == in.py && out.pz == in.pz, "player/position round-trip");
    CHECK(out.yaw == in.yaw && out.pitch == in.pitch, "player/angles round-trip");
    CHECK(out.vx == in.vx && out.vy == in.vy && out.vz == in.vz, "player/velocity round-trip");
    CHECK(out.move_mode == in.move_mode && out.action == in.action && out.flags == in.flags,
          "player/mode+action+flags round-trip");
}

// ---------------------------------------------------------------------------
static void TestQuantize()
{
    // Position: 1/8 cm.  The bound is exact for values on the grid and within half a
    // unit elsewhere, which at this scale is 0.0625 cm -- a twentieth of a millimetre
    // is invisible and the game's own collision tolerance is centimetres.
    struct { float cm; int32_t q; } cases[] = {
        {   0.0f,      0 },
        {   1.0f,      8 },
        {  -1.0f,     -8 },
        {  12.5f,    100 },
        { 100.0f,    800 },
        {-100.0f,   -800 },
    };
    bool allOk = true;
    for (int i = 0; i < 6; ++i) {
        if (wire::QuantizePos(cases[i].cm) != cases[i].q) allOk = false;
    }
    CHECK(allOk, "quantize/position exact on the grid");

    float maxErr = 0.0f;
    for (int i = -10000; i <= 10000; i += 7) {
        float cm = (float)i * 0.5f;
        int32_t q = wire::QuantizePos(cm);
        float back = wire::DequantizePos(q);
        float d = back - cm;
        if (d < 0) d = -d;
        if (d > maxErr) maxErr = d;
    }
    CHECK(maxErr <= 0.0625f + 1e-4f, "quantize/position error <= 1/16 cm");

    // Saturation: a value far outside the range must clamp, never wrap.  A wrap here
    // teleports the peer across the map.
    CHECK(wire::QuantizePos(1.0e12f) == 2147483000, "quantize/position clamps high");
    CHECK(wire::QuantizePos(-1.0e12f) == -2147483000, "quantize/position clamps low");

    // Angles: full turn = 65536 units, wrapping is short-way by construction.
    CHECK(wire::QuantizeAngle(0.0f) == 0, "quantize/angle 0");
    CHECK(wire::QuantizeAngle(90.0f) == 16384, "quantize/angle 90");
    CHECK(wire::QuantizeAngle(180.0f) == -32768, "quantize/angle 180 wraps to -32768");
    CHECK(wire::QuantizeAngle(270.0f) == -16384, "quantize/angle 270");
    CHECK(wire::QuantizeAngle(360.0f) == 0, "quantize/angle 360 == 0");

    float aerr = 0.0f;
    for (int d = 0; d < 360; d += 3) {
        float back = wire::DequantizeAngle(wire::QuantizeAngle((float)d));
        float e = back - (float)d;
        while (e > 180.0f) e -= 360.0f;
        while (e < -180.0f) e += 360.0f;
        if (e < 0) e = -e;
        if (e > aerr) aerr = e;
    }
    // 360/65536 = 0.0055 degrees; allow the rounding half-step.
    CHECK(aerr <= 0.0056f, "quantize/angle error <= 1 unit");

    // Velocity saturates at i16 rather than wrapping.
    CHECK(wire::QuantizeVel(1.0e9f) == 32767, "quantize/velocity clamps high");
    CHECK(wire::QuantizeVel(-1.0e9f) == -32768, "quantize/velocity clamps low");
}

// ---------------------------------------------------------------------------
// The endpoint of the "the encoder refuses rather than truncates" rule: a body that
// does not fit must set `ok` false, and the caller must send nothing.
static void TestWriterRefusesOverflow()
{
    // Exactly 12 bytes of room: three u32 fit and the fourth must be refused.
    uint8_t small[12];
    wire::Writer w(small, sizeof(small));
    w.U32(1);
    w.U32(2);
    w.U32(3);
    CHECK(w.ok, "writer/fills exactly");
    w.U32(4);
    CHECK(!w.ok, "writer/refuses to overflow");
}

static void TestReaderRefusesOverrun()
{
    uint8_t buf[8];
    memset(buf, 0xAB, sizeof(buf));
    wire::Reader r(buf, 4);
    r.U32();
    CHECK(r.ok, "reader/reads 4");
    r.U32();
    CHECK(!r.ok, "reader/refuses to read past the end");
    // After the failure every subsequent read must stay failed rather than return
    // plausible zeroes.
    uint8_t x = r.U8();
    CHECK(!r.ok && x == 0, "reader/stays failed after overrun");
}

// ---------------------------------------------------------------------------
static void TestFlagsChunking()
{
    // A container too big for one datagram must SPLIT ACROSS TICKS with the cursor
    // returned, never fragment.  The corpus measured 92.4 % effective loss on a
    // fragmented unreliable stream, so this property is load-bearing.
    const int N = 60;
    wire::FlagEntry e[N];
    static char names[N][40];
    for (int i = 0; i < N; ++i) {
        snprintf(names[i], sizeof(names[i]), "SomeProgressionFlag_%02d", i);
        e[i].name = names[i];
        e[i].value = i * 3;
    }

    uint8_t body[wire::kMaxBody];
    wire::Writer w(body, sizeof(body));
    wire::WriteFlagsHeader(w, 1, (uint16_t)N);
    int next = wire::WriteFlagsChunk(w, e, N, 0, 1);

    CHECK(w.ok, "flags/large container encodes without overflowing");
    CHECK(next > 0, "flags/at least one entry fits");
    CHECK(next < N, "flags/does not fit in one datagram (so splitting is required)");
    CHECK(w.len <= wire::kMaxBody, "flags/stays inside the body cap");

    // Round-trip the produced chunk.  The flag header is rev(u32) + n(u16) = 6 bytes;
    // entries begin immediately after it.
    wire::Reader r(body + 6, w.len - 6);
    int read = 0;
    while (r.pos < r.len) {
        char nm[96];
        int32_t v = 0;
        if (!wire::ReadFlagEntry(r, nm, sizeof(nm), &v)) break;
        if (read == 0) {
            CHECK(NameEqualsC(nm, "SomeProgressionFlag_00"), "flags/first name round-trips");
            CHECK(v == 0, "flags/first value round-trips");
        }
        ++read;
    }
    CHECK(read == next, "flags/chunk count matches the returned cursor");
}

// The test must not depend on qdsubstrate.h (which needs Windows), so it carries its
// own tiny comparison.
static bool NameEqualsC(const char* a, const char* b)
{
    int i = 0;
    for (; a[i] && b[i]; ++i) if (a[i] != b[i]) return false;
    return a[i] == 0 && b[i] == 0;
}

// ---------------------------------------------------------------------------
static void TestIntentDecode()
{
    uint8_t body[256];
    wire::Writer w(body, sizeof(body));
    wire::WriteIntentHeader(w, wire::I_SET_FLAG, 42);
    w.Str("DragonLordDead");
    w.I32(1);
    w.U8(1);                       // absolute

    wire::Reader r(body, w.len);
    wire::IntentBody b;
    CHECK(wire::ReadIntent(r, &b), "intent/decodes");
    CHECK(b.kind == wire::I_SET_FLAG, "intent/kind");
    CHECK(b.based_on_rev == 42u, "intent/based_on_rev");
    CHECK(NameEqualsC(b.name, "DragonLordDead"), "intent/name");
    CHECK(b.value == 1, "intent/value");
    CHECK(b.mode == 1, "intent/mode");

    // An unknown intent kind must be refused, not applied as a no-op: a peer running
    // a newer build must not be able to make an older host silently drop an action.
    uint8_t bad[8];
    wire::Writer bw(bad, sizeof(bad));
    bw.U8(0xEE);
    bw.U32(1);
    wire::Reader br(bad, bw.len);
    CHECK(!wire::ReadIntent(br, &b), "intent/rejects unknown kind");
}

// ---------------------------------------------------------------------------
// The three world bodies (enemy, intents 7/8, level union) round-trip, and a level
// union that lies about its lengths is refused rather than read past.
static void TestWorldBodies()
{
    using namespace qd::wire;
    uint8_t buf[kMaxBody];

    EnemyRow rows[2];
    memset(rows, 0, sizeof(rows));
    rows[0].guid[0] = 0x5ae3b37bu; rows[0].px = -800; rows[0].hp = 65535; rows[0].flags = kEnemyAlive | kEnemyBody;
    rows[1].guid[3] = 7; rows[1].pz = 123456; rows[1].yaw = -16384; rows[1].hp = 0; rows[1].flags = 0;
    {
        Writer w(buf, sizeof(buf));
        WriteEnemyHeader(w, 2);
        WriteEnemyChunk(w, rows, 2, 0);
        CHECK(w.ok && w.len == 2 + 2 * 33, "enemy body size");
        Reader r(buf, w.len);
        uint16_t n = 0;
        EnemyRow a, b;
        CHECK(ReadEnemyHeader(r, &n) && n == 2, "enemy header");
        CHECK(ReadEnemyRow(r, &a) && ReadEnemyRow(r, &b), "enemy rows read");
        CHECK(a.guid[0] == 0x5ae3b37bu && a.px == -800 && a.hp == 65535 && a.flags == 5, "enemy row 0 round-trips");
        CHECK(b.guid[3] == 7 && b.pz == 123456 && b.yaw == -16384 && b.flags == 0, "enemy row 1 round-trips");
        CHECK(r.pos == r.len, "enemy body fully consumed");
    }
    {
        const uint32_t g[4] = {1, 2, 3, 4};
        Writer w(buf, sizeof(buf));
        WriteIntentEnemyHit(w, 9, g, 70000);
        Reader r(buf, w.len);
        IntentBody b;
        CHECK(ReadIntent(r, &b) && b.kind == I_ENEMY_HIT && b.based_on_rev == 9 && b.guid[3] == 4 &&
              b.value == 70000, "intent enemy_hit round-trips");
        Writer w2(buf, sizeof(buf));
        WriteIntentEnemyRecord(w2, 0, g, 12000, 0);
        Reader r2(buf, w2.len);
        CHECK(ReadIntent(r2, &b) && b.kind == I_ENEMY_RECORD && b.value == 12000 && b.value2 == 0,
              "intent enemy_record round-trips");
    }
    {
        const char* names[3] = {"L_Main", "L_Dungeon", "L_Boss"};
        uint8_t bits[2] = {0x05, 0x02};
        Writer w(buf, sizeof(buf));
        WriteLevelUnion(w, bits, 2, names, 3);
        Reader r(buf, w.len);
        static LevelUnionBody b;
        CHECK(ReadLevelUnion(r, &b) && b.has_names && b.n == 3, "level union reads");
        CHECK(strcmp(b.name[1], "L_Dungeon") == 0, "level union name");
        CHECK(LevelUnionBit(b, 0) && !LevelUnionBit(b, 1) && LevelUnionBit(b, 2) && LevelUnionBit(b, 9),
              "level union bits LSB-first");
        CHECK(!LevelUnionBit(b, 200), "level union bit past the end is clear");
        // bits_len larger than the body: refused.
        uint8_t bad[4] = {0x40, 0x00, 0x01, 0x02};
        Reader rb(bad, 4);
        CHECK(!ReadLevelUnion(rb, &b), "level union with a lying bits_len is refused");
    }

    // ENEMY_DETAIL: the event lane for an enemy the rows only sample.
    {
        EnemyDetailBody d, got;
        memset(&d, 0, sizeof(d));
        d.guid[0] = 0x5ae3b37bu; d.guid[3] = 9;
        d.anim = E_ATTACK; d.seq = 3; d.status = ST_POISON | ST_HEAVY;
        strcpy(d.path, "/Game/Anim/Enemy/Slash_Montage");
        d.path[sizeof(d.path) - 1] = 0;
        Writer w(buf, sizeof(buf));
        WriteEnemyDetail(w, d);
        Reader r(buf, w.len);
        CHECK(ReadEnemyDetail(r, &got), "enemy_detail reads");
        CHECK(got.guid[0] == 0x5ae3b37bu && got.guid[3] == 9, "enemy_detail guid round-trips");
        CHECK(got.anim == E_ATTACK && got.seq == 3, "enemy_detail anim/seq round-trip");
        CHECK(got.status == (uint16_t)(ST_POISON | ST_HEAVY), "enemy_detail status round-trips");
        CHECK(strcmp(got.path, d.path) == 0, "enemy_detail montage path round-trips");
        CHECK(r.pos == r.len, "enemy_detail body fully consumed");
    }

    // TRAVEL: the map the session is in, and where the sender went.
    {
        TravelBody t, got;
        memset(&t, 0, sizeof(t));
        t.seq = 0x1234;
        t.role = 1;
        strcpy(t.map, "100_GR");
        strcpy(t.dest, "Teleporter_500_AUT_02/100_GR");
        Writer w(buf, sizeof(buf));
        WriteTravel(w, t);
        Reader r(buf, w.len);
        CHECK(ReadTravel(r, &got), "travel reads");
        CHECK(got.seq == 0x1234 && got.role == 1, "travel seq/role round-trip");
        CHECK(strcmp(got.map, "100_GR") == 0, "travel map round-trips");
        CHECK(strcmp(got.dest, t.dest) == 0, "travel destination round-trips");
        CHECK(r.pos == r.len, "travel body fully consumed");
    }

    // DAMAGE: a host-run enemy's hit on the partner.
    {
        DamageBody d, got;
        memset(&d, 0, sizeof(d));
        d.guid[0] = 0x5ae3b37bu; d.guid[3] = 11;
        d.amount_centi = 3500;
        d.flags = kDamageMelee | kDamageGuessed;
        Writer w(buf, sizeof(buf));
        WriteDamage(w, d);
        Reader r(buf, w.len);
        CHECK(ReadDamage(r, &got), "damage reads");
        CHECK(got.guid[0] == 0x5ae3b37bu && got.guid[3] == 11, "damage attacker round-trips");
        CHECK(got.amount_centi == 3500, "damage amount round-trips");
        CHECK(got.flags == (kDamageMelee | kDamageGuessed), "damage flags round-trip");
        CHECK(r.pos == r.len, "damage body fully consumed");
    }
}

int main()
{
    printf("=== qdwire contract tests ===\n");
    TestEnvelope();
    TestEnvelopeRejects();
    TestPlayer();
    TestQuantize();
    TestWriterRefusesOverflow();
    TestReaderRefusesOverrun();
    TestFlagsChunking();
    TestIntentDecode();
    TestWorldBodies();
    printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
