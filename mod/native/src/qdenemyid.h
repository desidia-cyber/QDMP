// qdenemyid.h -- a stable identity for every enemy, including the ones with no spawner.
//
// THE GAP THIS CLOSES (SYNC-GAPS 2, last paragraph)
// ------------------------------------------------
// Until now an enemy's key was the authored FGuid of its SPAWNER
// (`EnemySpawner_C.GetSaveGUID()`), because one spawner owns one enemy
// (`CurrentManagedEnemy`).  That key exists only for enemies a spawner created.  Two
// ordinary kinds of enemy have no such key and were therefore unsynced -- each machine
// ran its own independent copy:
//
//   * an enemy PLACED in the cooked level with no spawner at all;
//   * a SUMMONED or boss-spawned enemy, which either has no spawner or shares its
//     spawner's single GUID with the spawner's own managed enemy.
//
// The rule this header implements keeps the spawner GUID as the first choice (it is the
// key `StoreEnemySaveState` persists by, so it must win where it exists) and adds two
// deterministic fallbacks.  "Deterministic" is the whole requirement: the two machines
// must compute the SAME 128-bit key for the same enemy without negotiating, because the
// existing ownership/row model (qdenemyown.h, qdenemy.h) is keyed by it and nothing else
// changes.
//
//   SPAWNER  the spawner reported a GUID and no other live enemy holds it.
//            Key = that GUID.  Unchanged behaviour.
//
//   PLACED   the actor is part of a cooked level, so its short object name and its
//            outer chain ("<world>/<level>/<object name>") are identical on both
//            machines -- both run the same cooked package.  Key = FNV-1a 128 of that
//            path, the same construction qddialogue.h's StableId already uses for
//            placed NPCs, which is proven for exactly this job.
//
//   SUMMON   the enemy is runtime-spawned: no free spawner GUID and no place in a
//            level.  Key = FNV-1a 128 of "<summoner key hex>:<spawn x,y,z>#<n>:<class>",
//            where the spawn coordinate is where the summon first appeared, `n` is 0
//            unless a second summon of the same summoner appeared at the SAME point, and
//            the class is its asset path.  The class is a discriminator, not decoration:
//            one summoner can call two different classes at one point (measured: one key
//            resolved to a Boner_Gray_Archer_BP_C at hp 85 on one machine and a
//            FlamingSkull_BP_C at hp 30 on the other), and without it the pair names
//            both enemies.
//
//            The spawn position, not a counter, is the discriminator, and that is the
//            whole point: the incremental object scan discovers enemies in whatever
//            order each machine's GObjects happens to hold them, so an ordinal assigned
//            in scan order would differ between two machines for the same enemy.
//            Position is shared, order is not.
//
//            `n` only distinguishes summons that coincide to 1/100 cm.  Those are
//            interchangeable by construction -- identical class, identical point -- so
//            any assignment between them produces the same pair of enemies, which is
//            what makes an arbitrary numbering safe there and nowhere else.
//
// WHY NOT A POINTER OR A RUNTIME NAME
// -----------------------------------
// A pointer differs by construction and a runtime-spawned actor gets a per-machine name
// (`<Class>_<index>`, the index being that machine's object counter).  Both would look
// like sync; both are the bug this header removes.
//
// WHY THE SOURCE IS ON THE WIRE
// -----------------------------
// Two 128-bit hashes of different things can collide.  Tagging the source in two spare
// bits of the row's `flags` (bits 4-5, unused by `ENEMY` today) makes the pair
// (source, key) the identity, so a collision between a spawner GUID and a placed path
// hash cannot make two enemies share a record.  It also makes a live log readable.
//
// Two summons that coincide at one point (to 1/100 cm) are separated by a small index.
// They are identical in class and position, so they are interchangeable; that is what
// makes an arbitrary numbering safe there and nowhere else.
//
// Header-only and dependency-free, compiled into native/test like the other state
// machines.
#pragma once
#include <stdint.h>

#include "qdwire.h"

namespace qd {
namespace enemyid {

// The identity source, in the spare bits of an ENEMY row's `flags` (WIRE.md 3.9).
static const uint8_t kSrcShift = 4;
static const uint8_t kSrcMask  = 0x30;

enum Source : uint8_t {
    SRC_SPAWNER = 0,   // an authored spawner GUID: unchanged, the persisted-key path
    SRC_PLACED  = 1,   // a placed actor: hash of "<world>/<level>/<object name>"
    SRC_SUMMON  = 2,   // runtime-spawned: hash of summoner key + spawn point
};

inline uint8_t TagFlags(uint8_t flags, Source s)
{
    return (uint8_t)((flags & (uint8_t)~kSrcMask) | (uint8_t)((s << kSrcShift) & kSrcMask));
}

inline Source SourceOf(uint8_t flags)
{
    uint8_t v = (uint8_t)((flags & kSrcMask) >> kSrcShift);
    return (v <= (uint8_t)SRC_SUMMON) ? (Source)v : SRC_SUMMON;
}

// `flags` with the source bits cleared, for code that only cares about the state bits.
inline uint8_t StateBits(uint8_t flags) { return (uint8_t)(flags & (uint8_t)~kSrcMask); }

inline bool ZeroKey(const uint32_t g[4]) { return (g[0] | g[1] | g[2] | g[3]) == 0; }
inline bool SameKey(const uint32_t a[4], const uint32_t b[4])
{
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

// ---------------------------------------------------------------------------
// The 128-bit hash
// ---------------------------------------------------------------------------
// FNV-1a in two lanes with a final avalanche on the second.  The same construction as
// qddialogue.h's StableId, on purpose: a placed object already has a proven stable id in
// this codebase, and two different hash functions for the same job would be two things
// to get wrong.  A seed distinguishes the two uses so a dialogue id and an enemy id of
// the same path cannot collide.
inline void Hash128(const char* s, uint32_t seed, uint32_t out[4])
{
    uint64_t h1 = 1469598103934665603ULL ^ (uint64_t)seed;
    uint64_t h2 = 0x84222325CBF29CE4ULL ^ ((uint64_t)seed * 0x9E3779B97F4A7C15ULL);
    for (int i = 0; s && s[i]; ++i) {
        h1 = (h1 ^ (uint8_t)s[i]) * 1099511628211ULL;
        h2 = (h2 ^ (uint8_t)s[i]) * 1099511628211ULL;
        h2 ^= h2 >> 29;
    }
    // Avalanche so a one-character difference in the tail changes every lane.
    h1 ^= h1 >> 33; h1 *= 0xFF51AFD7ED558CCDULL; h1 ^= h1 >> 33;
    h2 ^= h2 >> 33; h2 *= 0xC4CEB9FE1A85EC53ULL; h2 ^= h2 >> 33;
    out[0] = (uint32_t)h1;
    out[1] = (uint32_t)(h1 >> 32);
    out[2] = (uint32_t)h2;
    out[3] = (uint32_t)(h2 >> 32);
}

// Seed for a placed enemy.  A constant, not a machine value: it must be identical on
// both machines and must not equal the dialogue seed.
static const uint32_t kPlacedSeed = 0x5144454Eu;   // 'QDEN'
static const uint32_t kSummonSeed = 0x5144534Du;   // 'QDSM'

// A placed actor's key from "<world>/<level>/<object name>".  The caller builds that
// string from the actor's own outer chain, so it is the cooked package's identity, not
// either machine's object index.
inline bool PlacedKey(const char* worldLevelPath, uint32_t out[4])
{
    if (!worldLevelPath || !worldLevelPath[0]) return false;
    Hash128(worldLevelPath, kPlacedSeed, out);
    return !ZeroKey(out);
}

// A summon's key from its summoner's key, where it appeared, a coincidence index, and
// its CLASS.  `summoner` may be any identity (a spawner GUID, a placed path, or another
// summon), which is what lets a chain of summons stay deterministic.  `spawn` is the
// position the summon first appeared at, in 1/100 cm, and `dup` is 0 unless another
// summon of the same summoner appeared at the same point.
//
// THE CLASS IS NOT OPTIONAL (Session 19, R137).  A summoner can call two DIFFERENT
// classes at the same point -- the soak measured one key resolving to a
// Boner_Gray_Archer_BP_C at hp 85 on one machine and a FlamingSkull_BP_C at hp 30 on
// the other.  Without a class discriminator the pair (summoner, position) names both,
// so the health bar, the row and the kill apply to whichever was seen last.  Both
// machines run the same cooked class, so its asset path is shared by construction and
// keeps the key deterministic.
inline void SummonKey(const uint32_t summoner[4], const int32_t spawn[3], uint16_t dup,
                      const char* clsPath, uint32_t out[4])
{
    // Spell the summoner key in hex, not as raw bytes: a byte string can contain a 0 and
    // would then silently hash a truncated prefix (the C-string hash above stops at 0).
    static const char* H = "0123456789abcdef";
    char s[256];
    int k = 0;
    for (int i = 0; i < 4; ++i) {
        for (int b = 28; b >= 0; b -= 4) s[k++] = H[(summoner[i] >> b) & 0xF];
    }
    s[k++] = ':';
    // The spawn point, each coordinate with an explicit sign and no padding ambiguity
    // ('-' vs nothing is enough because the separator is ':').  Written by hand: this
    // header is CRT-free, and the separators are what make the three numbers
    // un-conflatable ("1,23" and "12,3" must not be the same string).
    int32_t v[3] = {spawn[0], spawn[1], spawn[2]};
    for (int i = 0; i < 3; ++i) {
        if (i) s[k++] = ',';
        int32_t q = v[i];
        if (q < 0) { s[k++] = '-'; q = -q; }
        char d[12];
        int n = 0;
        if (q == 0) d[n++] = '0';
        while (q) { d[n++] = (char)('0' + (q % 10)); q /= 10; }
        while (n) s[k++] = d[--n];
    }
    s[k++] = '#';
    // `dup` in decimal, and it is 0 for the overwhelmingly common case, so the steady
    // spelling is "<hex>:x,y,z#0:<class>".
    char d[12];
    int n = 0;
    uint32_t u = dup;
    if (u == 0) d[n++] = '0';
    while (u) { d[n++] = (char)('0' + (u % 10)); u /= 10; }
    while (n) s[k++] = d[--n];
    // The class separates two different summons that coincide exactly, which the
    // position and the coincidence index cannot.  A ':' separator keeps an empty class
    // from colliding with a class whose path starts with a digit.
    s[k++] = ':';
    for (int i = 0; clsPath && clsPath[i] && k < (int)sizeof(s) - 1; ++i) s[k++] = clsPath[i];
    s[k] = 0;
    Hash128(s, kSummonSeed, out);
}

// ---------------------------------------------------------------------------
// Which source applies
// ---------------------------------------------------------------------------
// `guidTakenByOther` is true when the spawner names a DIFFERENT enemy as its
// `CurrentManagedEnemy`.  That is the multi-spawn case the design measured: one spawner
// owns one managed enemy, so a second enemy naming the same spawner is a spawned one and
// must not overwrite the managed one's identity.  The caller asks the game, not the scan
// order, because "who the scan saw first" differs between the two machines.
inline Source Decide(bool haveSpawnerGuid, bool guidTakenByOther, bool havePlacedPath)
{
    if (haveSpawnerGuid && !guidTakenByOther) return SRC_SPAWNER;
    if (havePlacedPath) return SRC_PLACED;
    return SRC_SUMMON;
}

// A hash-derived key has a positional component, so the two machines can legitimately mint
// it differently (a summon seen late, or an actor the conservative placed-vs-spawned test
// classified another way).  A spawner GUID cannot: it is exact.  This is the rule that lets
// the receiver adopt an unknown key -- it may only absorb a hash-derived one, never a
// spawner GUID, because a mismatched GUID is a genuinely different enemy.
inline bool HashDerived(Source s) { return s == SRC_PLACED || s == SRC_SUMMON; }

// ---------------------------------------------------------------------------
// Coincident spawns -- the only thing a position key cannot separate
// ---------------------------------------------------------------------------
// Two summons of one summoner that appear at the SAME point (to 1/100 cm) hash to the
// same key.  That is a real case for a summoner that spawns at its own feet, so the
// second and later ones get a small index.  Because they are identical in class and
// position they are interchangeable, which is what makes an arbitrary numbering safe
// here -- and only here.
//
// Bounded and recycled, like every other record table in this codebase.  A record is
// touched whenever its key is asked for, so recycling only reclaims a summoner that has
// not been seen for kRecycleMs.
class CoincidentSpawns {
public:
    static const int      kMax = 128;
    static const uint32_t kRecycleMs = 60000;

    CoincidentSpawns() { Reset(); }

    void Reset()
    {
        for (int i = 0; i < kMax; ++i) r_[i].used = false;
        overflow_ = 0;
    }

    // How many summons were already seen at (summoner, spawn).  0 for the first.
    uint16_t Claim(const uint32_t summoner[4], const int32_t spawn[3], uint32_t now_ms)
    {
        Rec* r = Touch(summoner, spawn, now_ms);
        if (!r) { ++overflow_; return 0; }
        uint16_t v = r->n;
        if (r->n != 0xFFFF) ++r->n;
        return v;
    }

    int      Count() const { int n = 0; for (int i = 0; i < kMax; ++i) n += r_[i].used ? 1 : 0; return n; }
    uint32_t Overflows() const { return overflow_; }

private:
    struct Rec {
        bool     used;
        uint32_t summoner[4];
        int32_t  spawn[3];
        uint16_t n;
        uint32_t touchMs;
    };

    Rec* Touch(const uint32_t g[4], const int32_t s[3], uint32_t now_ms)
    {
        if (ZeroKey(g)) return nullptr;
        for (int i = 0; i < kMax; ++i) {
            if (!r_[i].used) continue;
            if (!SameKey(r_[i].summoner, g)) continue;
            if (r_[i].spawn[0] != s[0] || r_[i].spawn[1] != s[1] ||
                r_[i].spawn[2] != s[2]) continue;
            r_[i].touchMs = now_ms;
            return &r_[i];
        }
        int victim = -1;
        uint32_t oldest = 0;
        for (int i = 0; i < kMax; ++i) {
            if (!r_[i].used) { victim = i; break; }
            uint32_t age = (uint32_t)(now_ms - r_[i].touchMs);
            if (age > kRecycleMs && age > oldest) { oldest = age; victim = i; }
        }
        if (victim < 0) return nullptr;
        Rec& r = r_[victim];
        r.used = true;
        for (int k = 0; k < 4; ++k) r.summoner[k] = g[k];
        for (int k = 0; k < 3; ++k) r.spawn[k] = s[k];
        r.n = 0;
        r.touchMs = now_ms;
        return &r;
    }

    Rec      r_[kMax];
    uint32_t overflow_;
};

// ---------------------------------------------------------------------------
// Adoption: when the two machines minted different summon keys
// ---------------------------------------------------------------------------
// Position keys are deterministic by construction, but a machine can still see a summon
// at a position it first observed LATE (a stream-in from a distance), one frame after the
// summon moved.  Rather than let that become two enemies, a row for a key this machine
// does not know is matched against the local summon records: if exactly one local summon
// sits within kAdoptCm of that row, it is the same enemy and the LOCAL RECORD TAKES THE
// SENDER'S KEY.  The sender ran the spawn, so its key is the truth.
//
// kAdoptCm is 300: an unidentified summon has barely moved, so two different summons of
// one summoner are not within 3 m of each other at spawn.  If two are, the caller refuses
// rather than guesses.
static const float kAdoptCm = 300.0f;

inline bool CouldAdopt(float dist_cm) { return dist_cm >= 0.0f && dist_cm <= kAdoptCm; }
} // namespace enemyid
} // namespace qd
