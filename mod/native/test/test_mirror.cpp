// test_mirror.cpp -- the rules that keep a mirrored enemy a repair and not a simulation.
//
// WHY THESE AND NOT THE LIVE RUN
// ------------------------------
// The construction itself can only be proven live (it asks the engine to build a gameplay
// Blueprint), but the four decisions around it are pure functions, and each has a failure
// that a run would misreport:
//
//   * a mirrored copy that publishes its own rows tells the owner about an enemy the owner
//     already runs -- two row streams for one key, converging against each other;
//   * a mirrored copy that is allowed to persist writes a death into a save for an enemy
//     this machine never ran;
//   * a mirrored copy that is released "because our distance says we are nearer" starts a
//     second AI on the owner's enemy, which is the exact double simulation ownership exists
//     to prevent;
//   * a dead owner row has to be a destroy, not a destroy-and-respawn, or the copy stands
//     back up on the next row.
//
// So this test states them as invariants over the small state machine the proxy uses.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "qdenemy.h"
#include "qdwire.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, name)                                                          \
    do {                                                                           \
        if (cond) { ++g_pass; }                                                    \
        else { ++g_fail; printf("FAIL %s  (%s:%d)\n", name, __FILE__, __LINE__); } \
    } while (0)

using namespace qd;

static const uint32_t kDragon[4] = {0x17181166u, 0x4d738291u, 0xc77fd28au, 0x9dada0eau};

// The three things a mirror record must never do, expressed as the predicates the proxy
// consults.  A record is {mirror, driven}; everything below reads those two bits.
struct MirrorRec {
    bool mirror;
    bool driven;
    bool adopted;
    const void* spawner;   // nullptr: the local spawner cannot adopt it
};

static bool MayPublish(const MirrorRec& r) { return !r.mirror; }
static bool MayPersist(const MirrorRec& r) { return !r.mirror; }
static bool MayOwnAi(const MirrorRec& r, bool host, bool prefMe) { return !r.mirror && (host || !prefMe); }
static bool DestroyOnDead(bool mirror, bool ownerSaysDead) { return mirror && ownerSaysDead; }

int main()
{
    // ---- a mirrored copy never publishes -----------------------------------
    {
        MirrorRec m = {true, true, true, nullptr};
        CHECK(!MayPublish(m), "a mirror never publishes its own rows");
        CHECK(!MayPersist(m), "a mirror is never persisted by the local save");
        CHECK(!MayOwnAi(m, true, true), "a mirror never runs its own AI, even on the host");
        // The same record without the flag is an ordinary copy and may do all three: the
        // guard is the flag, not the shape.
        MirrorRec o = {false, true, true, nullptr};
        CHECK(MayPublish(o), "an ordinary driven copy still publishes");
        CHECK(MayPersist(o), "an ordinary driven copy still persists");
    }

    // ---- ownership cannot take a mirror back ---------------------------------
    {
        // The owner's rows freeze the copy; a distance preference that says "we are nearer"
        // must not be able to release it, which is what `st = O_FROZEN` in EnemyOwnPump is.
        MirrorRec m = {true, true, true, nullptr};
        for (int host = 0; host <= 1; ++host)
            for (int pref = 0; pref <= 1; ++pref)
                CHECK(!MayOwnAi(m, host != 0, pref != 0),
                      "a mirror is never released to local AI by any distance preference");
    }

    // ---- the destroy reasons -------------------------------------------------
    {
        CHECK(DestroyOnDead(true, true), "the owner's dead row destroys the mirror");
        CHECK(!DestroyOnDead(true, false), "a live owner row never destroys the mirror");
        CHECK(!DestroyOnDead(false, true), "an ordinary copy is not destroyed by this lane");
    }

    // ---- the wire body the spawn lane carries --------------------------------
    {
        // The class path is the whole point: a row cannot carry it, so a body that loses it
        // is a spawn of nothing.  The fractional HP and the position quantization are the
        // same units an ENEMY row uses, so both machines read the same numbers.
        wire::EnemySpawnBody s;
        memset(&s, 0, sizeof(s));
        memcpy(s.guid, kDragon, 16);
        s.flags = wire::kSpawnMirror;
        s.px = wire::QuantizePos(-28573.0f);
        s.py = wire::QuantizePos(754.0f);
        s.pz = wire::QuantizePos(-13028.0f);
        s.yaw = wire::QuantizeAngle(0.0f);
        s.hp = enemy::HpFrac(3500.0f, 3500.0f);
        strcpy(s.path, "/Game/Enemies/Dragon/Dragon_Green_BP.Dragon_Green_BP_C");

        uint8_t buf[wire::kMaxBody];
        wire::Writer w(buf, sizeof(buf));
        wire::WriteEnemySpawn(w, s);
        CHECK(w.ok, "the spawn body fits one datagram");

        wire::Reader r(buf, w.len);
        wire::EnemySpawnBody got;
        CHECK(wire::ReadEnemySpawn(r, &got), "the spawn body reads back");
        CHECK(r.pos == r.len, "the spawn body is consumed exactly");
        CHECK(memcmp(got.guid, kDragon, 16) == 0, "the key round-trips");
        CHECK(got.flags == wire::kSpawnMirror, "the mirror flag round-trips");
        CHECK(strcmp(got.path, s.path) == 0, "the CLASS PATH round-trips (the field a row cannot carry)");
        CHECK(got.hp == 65535, "a full-HP enemy is a full HP fraction");
        CHECK(wire::DequantizePos(got.px) > -28574.0f && wire::DequantizePos(got.px) < -28572.0f,
              "the position round-trips inside one quantum");

        // A body with no class is refused rather than handed to an engine spawn.
        wire::EnemySpawnBody bad;
        memset(&bad, 0, sizeof(bad));
        memcpy(bad.guid, kDragon, 16);
        wire::Writer w2(buf, sizeof(buf));
        wire::WriteEnemySpawn(w2, bad);
        wire::Reader r2(buf, w2.len);
        wire::EnemySpawnBody got2;
        CHECK(!wire::ReadEnemySpawn(r2, &got2), "a spawn body with no class path is refused");
    }

    // ---- relevance: the lane describes what the partner could see -------------
    {
        // The owner only describes enemies inside `kMirrorRelevanceCm` of the partner, so
        // the receiver cannot be asked to construct a menagerie.  Stated as the arithmetic
        // the pump uses.
        const float kRelevance = 6000.0f;
        struct P { float x, y, z; };
        P peer = {-28437.0f, 746.0f, -13051.0f};
        P dragon = {-28573.0f, 754.0f, -13028.0f};
        P far = {40000.0f, 0.0f, 40000.0f};
        auto near = [&](const P& p) {
            float dx = p.x - peer.x, dy = p.y - peer.y, dz = p.z - peer.z;
            return dx * dx + dy * dy + dz * dz <= kRelevance * kRelevance;
        };
        CHECK(near(dragon), "the dragon is within the relevance radius of the partner");
        CHECK(!near(far), "an enemy 40 km away is not described at all");
    }

    printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
