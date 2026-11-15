// qdrespawn.h -- a player's death is a per-player fact: down, penalised, and back at
// THAT player's own last rest point, with the partner's world untouched.
//
// THE GAP THIS CLOSES
// -------------------
// In single player a death is the game's own business: `FS_PlayerChar_C:Kill` runs the
// death pose and opens the Game Over screen, and "Continue" reloads the world from the
// save -- pawn, enemies, flags, level list.  In co-op that is not a player's business at
// all.  Measured (SYNC-DESIGN §2.9, `20-residual-verification.md`): the Game Over screen
// freezes `UWorld::TimeSeconds` for its own machine, and the reload tears the world down
// under the partner as well (Session 22's peer died inside exactly such a teardown; the
// R140/R138 family is that teardown meeting a cached object).  So one player dying
// currently costs the survivor the fight, the arena, the enemy state and -- twice in the
// log -- a process.
//
// THE RULE (SYNC-DESIGN §2.9, in one line)
// ----------------------------------------
// **A respawn is a position; a rest is a world event.**  This header implements the
// first half and refuses to touch the second: nothing here calls a rest, writes a
// `SavableActorState`, clears an enemy record or asks for a level load.  The partner's
// world is not told anything about the death except where the body went down and where
// it came back -- the same two facts the position lane already carries, stated once
// instead of being inferred from a pause.
//
// WHAT CROSSES THE WIRE
// ---------------------
// Three small bodies, all per player, all stated by the machine that OWNS that player:
//
//   DOWN  the local player died (how many deaths it now carries, and where it fell)
//   UP    it respawned (the rest point, its restored HP, and the penalised maximum)
//   REST  it used a rest point (the stacks are cleared; the cap is 100% again)
//
// Nothing here is negotiated and nothing is arbitrated: a player's own machine is the
// only authority on that player's health, and the partner applies what it is told.  That
// is why there is no intent on this lane -- an intent is a REQUEST to the host, and there
// is nothing here for the host to grant.
//
// THE PENALTY, AND WHY IT IS A MULTIPLIER AND NOT A NUMBER
// -------------------------------------------------------
// Each death costs 10 % of the BASE maximum, stacking, down to a floor of 10 %:
// 100 / 90 / 80 / ... / 10 / 10 / ...  A rest restores 100 % and clears the stacks.
//
// The game recomputes its own maximum (`FS_PlayerChar_C:CalculateMaxHP`, whose out pin is
// `HP`) on a level-up, an equipment change, a load and a rest, and it stores the answer.
// A penalty applied once -- "write 360 instead of 400" -- is therefore erased by the next
// recomputation, and a penalty applied on every read is applied twice as soon as two
// callers read it.  So the rule is `max(game's computed max) x multiplier`: the engine
// half scales the VALUE THE GAME COMPUTED, each time the game computes one, and never
// keeps a second opinion of its own.  `ScaledMax` below is that rule, pure.
//
// WHAT SURVIVES WHAT
// ------------------
//   * level streaming / a world change: the STACKS survive (a death is a fact about the
//     player, not about an actor), the cached REST POINT ACTOR does not (it belongs to
//     the world that went), so the rest point is re-resolved from the key it was named
//     by -- the key is `level/actor`, the same stable key every other lane uses.
//   * the game recomputing stats: survives, because the multiplier is applied to every
//     computed maximum (above).
//   * save/quit: survives, deliberately, through a sidecar file next to the mod's own log
//     (`qdcoop-respawn-<tag>.ini`).  The game's own save cannot carry it -- writing a
//     mod field into `NamedSaveData` would make the penalty a story flag that the
//     partner's bank could overwrite, and `qdsave.py`'s monotonic guard exists precisely
//     to stop a mod from lowering the game's data.  A penalty that forgets itself on a
//     relaunch rewards quitting, so it is persisted where only this mod reads.
//
// WHY THE STATE MACHINE IS HERE AND NOT IN proxy.cpp
// --------------------------------------------------
// Everything below is a rule, and every one of them fails in a way that reads as
// something else in a game: a death counted twice looks like a lost 10 %; a respawn that
// fires before the pose has played looks like a teleport from mid-air; a penalty applied
// after a rest looks like a broken heal.  `test_respawn.cpp` pins each to a number.
#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "qdwire.h"

namespace qd {
namespace respawn {

// Each death costs this much of the BASE maximum, and the stack cannot take the maximum
// below the floor.  Both are percentages of the base so the penalty never depends on the
// order the game picked its own numbers in.
static const int kStepPct  = 10;
static const int kFloorPct = 10;

// The stack is capped at the number of steps that reaches the floor: a tenth death can
// lower nothing, and an unbounded count would eventually overflow its wire byte.
static const int kMaxDeaths = (100 - kFloorPct) / kStepPct;   // 9 -> 10 %

// THE DEATH POSE.  "A short death pose/fade, a few seconds": the game's own down montage
// is ~2 s (the measured `A_DYING` -> `A_DEAD` pair in qdavatar.h) and the partner needs
// to see the body go down before it reappears somewhere else.  Shorter and the respawn
// reads as a teleport out of a standing pose; longer and a death costs the pair a fight.
static const uint32_t kPoseMs = 3500;

// A respawn is a teleport, and the frame after it still has the game's own `IsDead` and a
// zero `Health` on the old values (a read that lands before the write).  Inside this
// window a zero health is not a second death.
static const uint32_t kSettleMs = 1500;

// A rest point's name is the same stable key every lane uses: `<actor>/<level>`.
static const int kNameCap = 64;
// ... and the LEVEL half is kept as its own field.  Measured (first live run): the actor path
// of a checkpoint in a streamed sublevel is longer than 63 characters
// (`//Game/RuinsLevel/Maps/700_SZ_GR/700_SZ_GR/PersistentLevel/Checkpoint7`), so slicing the
// level back out of the truncated name gave `Chec` -- a name that matches no map, which made
// a perfectly good rest point look like it was in another world.
static const int kMapCap = 40;

// ---------------------------------------------------------------------------
// The penalty
// ---------------------------------------------------------------------------
// The maximum-health percentage a player with `deaths` stacks carries.  Negative input is
// "no deaths"; anything past the floor returns the floor, so this cannot produce 0 (a
// maximum of 0 would make the next hit kill through any amount of healing).
inline int PctFor(int deaths)
{
    int d = deaths < 0 ? 0 : (deaths > kMaxDeaths ? kMaxDeaths : deaths);
    int pct = 100 - kStepPct * d;
    return pct < kFloorPct ? kFloorPct : pct;
}

// The stack a player has after one more death.
inline int StacksAfterDeath(int deaths)
{
    int d = deaths < 0 ? 0 : deaths;
    return d >= kMaxDeaths ? kMaxDeaths : d + 1;
}

// THE RULE: the game's computed maximum, scaled.  A non-positive base is not a maximum
// (`CalculateMaxHP` was refused), and scaling it would turn a read failure into a heal.
inline float ScaledMax(float baseMax, int deaths)
{
    if (!(baseMax > 0.0f)) return 0.0f;
    return baseMax * (float)PctFor(deaths) / 100.0f;
}

// ---------------------------------------------------------------------------
// The rest point
// ---------------------------------------------------------------------------
struct Point {
    bool     have;
    float    px, py, pz;
    float    yaw;
    uint32_t at;             // ms, this machine's clock
    char     name[kNameCap];  // "<actor>/<level>", for the log and for re-resolving
    char     map[kMapCap];    // the level the point is in, as the machine named it
};

inline void ClearPoint(Point* p) { memset(p, 0, sizeof(*p)); }

// Is this a rest point a respawn may use?  A point that was never set, or one whose
// position is not a place, is refused rather than used: the fallback is the save's
// checkpoint, and a remembered NaN is worse than no memory.
//
// A POINT IN A MENU MAP IS NOT A PLACE (measured, second live run).  This game keeps its
// menu maps in the SAME persistent world (`VideoConfigMap` is a sublevel of
// `Ruins_Level_00`), so a `Checkpoint` placed there resolves, is "near the player", and has
// coordinates -- but a real player cannot walk into it, and putting a respawned player there
// is the "teleport into empty space" failure this lane exists to avoid.
inline bool NameLooksLikeMenuMap(const char* name)
{
    if (!name || !name[0]) return false;
    static const char* const bad[] = {"/UI/", "/Menus/", "VideoConfigMap", "/Menu/"};
    for (int k = 0; k < (int)(sizeof(bad) / sizeof(bad[0])); ++k) {
        const char* needle = bad[k];
        for (int i = 0; name[i]; ++i) {
            int j = 0;
            for (; needle[j] && name[i + j]; ++j) {
                // BOTH SIDES are folded: the needles are written the way the game spells them
                // ("/UI/", "VideoConfigMap") and folding only the path compared 'U' against
                // 'u' and matched nothing -- caught by test_respawn.cpp, not by the live run.
                char a = name[i + j];
                char b = needle[j];
                if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
                if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
                if (a != b) break;
            }
            if (!needle[j]) return true;
        }
    }
    return false;
}

inline bool UsablePoint(const Point& p)
{
    if (!p.have) return false;
    if (!(p.px == p.px) || !(p.py == p.py) || !(p.pz == p.pz)) return false;
    if (p.px > 1.0e8f || p.px < -1.0e8f) return false;
    if (p.py > 1.0e8f || p.py < -1.0e8f) return false;
    if (p.pz > 1.0e8f || p.pz < -1.0e8f) return false;
    if (NameLooksLikeMenuMap(p.name)) return false;
    return true;
}

// ---------------------------------------------------------------------------
// The per-player state machine
// ---------------------------------------------------------------------------
// ONE instance per machine: the local player's.  There is deliberately no second copy for
// the partner -- the partner's stacks arrive on the wire, are applied to the partner's
// bar, and are never simulated here (a follower that recomputed them would need the
// partner's rest history and would disagree the moment one body was lost).
class DeathState {
public:
    void Reset()
    {
        deaths_ = 0;
        down_ = false;
        deathAt_ = 0;
        respawnAt_ = 0;
        haveRespawn_ = false;
        ClearPoint(&rest_);
    }

    int  Deaths() const { return deaths_; }
    int  Pct() const { return PctFor(deaths_); }
    float Multiplier() const { return (float)PctFor(deaths_) / 100.0f; }
    bool Down() const { return down_; }
    // When the death being served was noticed, so the log can say how far into the pose a
    // refused reload arrived.  Only meaningful while `Down()`.
    uint32_t DownAt() const { return down_ ? deathAt_ : 0; }
    const Point& Rest() const { return rest_; }

    // A death this machine observed (the game killed the local player, or the health read
    // reached zero).  Idempotent while down: the pose and the fade both report a death,
    // and a second report inside one death must not cost another 10 %.
    bool NoteDeath(uint32_t now)
    {
        if (down_) return false;
        if (haveRespawn_ && (int32_t)(now - respawnAt_) < (int32_t)kSettleMs) return false;
        down_ = true;
        deathAt_ = now;
        deaths_ = StacksAfterDeath(deaths_);
        return true;
    }

    // The pose has played and the player may come back.
    bool Due(uint32_t now) const
    {
        return down_ && (int32_t)(now - deathAt_) >= (int32_t)kPoseMs;
    }

    // The player is back.  Called once, by the caller that actually respawned it.
    void NoteRespawn(uint32_t now)
    {
        down_ = false;
        respawnAt_ = now;
        haveRespawn_ = true;
    }

    // A rest: 100 % again, and the rest point is where a later death comes back to.
    // Returns the number of stacks that were cleared, so the log can say what a rest cost.
    int NoteRest(const Point& p, uint32_t now)
    {
        int cleared = deaths_;
        deaths_ = 0;
        down_ = false;              // a rest is also the player's own "I am not down"
        // NO settle window here.  The window exists to ignore a zero-health read that
        // lands one frame after we WROTE a dead player's health back; a rest happens to a
        // living player, so a death a second later is a death, not a stale read.
        rest_ = p;
        rest_.have = true;
        rest_.at = now;
        return cleared;
    }

    // The rest point learned from the SAVE (the fallback): only remembered when this
    // player has not rested in this session yet, so a freshly loaded save cannot overwrite
    // a newer rest this machine watched happen.
    void NoteSavePoint(const Point& p)
    {
        if (UsablePoint(rest_)) return;
        rest_ = p;
        rest_.have = UsablePoint(p);
    }

    // Stacks restored from the sidecar: a COUNT, not a death.  The player is not down and
    // no pose is armed -- a restored stack is history, and running it through NoteDeath
    // would both arm a respawn and (through the settle window) refuse the next real death.
    void RestoreStacks(int n)
    {
        deaths_ = n < 0 ? 0 : (n > kMaxDeaths ? kMaxDeaths : n);
    }

private:
    int      deaths_ = 0;
    bool     down_ = false;
    uint32_t deathAt_ = 0;
    uint32_t respawnAt_ = 0;
    bool     haveRespawn_ = false;
    Point    rest_ = {};
};

// ---------------------------------------------------------------------------
// The wire bodies
// ---------------------------------------------------------------------------
// The KINDS carry the meaning (0x54 down / 0x55 up / 0x56 rest); a body does not repeat the
// envelope's kind byte, because `doc/vectors.txt` and the Rust `qd-core` are the second
// implementation and neither repeats it.  `seq` is per-lane and only answers "is this newer
// than the last one I applied" (see SeqGate): the two machines' clocks are unrelated.
struct DownBody {
    uint8_t deaths;      // stacks after this death (0..kMaxDeaths)
    uint8_t pct;         // PctFor(deaths), carried so a receiver can check its own maths
    uint8_t seq;
    float   px, py, pz;  // where the body went down
};

struct UpBody {
    uint8_t  deaths;     // stacks now (0 after a rest)
    uint8_t  pct;
    uint8_t  seq;
    float    px, py, pz, yaw;   // the rest point the body came back to
    uint16_t hp, hp_max;        // the restored values, whole points
    char     name[kNameCap];     // the rest point's key
};

struct RestBody {
    uint8_t  deaths;     // 0
    uint8_t  pct;        // 100
    uint8_t  seq;
    float    px, py, pz;
    char     name[kNameCap];
};

inline bool Finite3(float x, float y, float z)
{
    if (!(x == x) || !(y == y) || !(z == z)) return false;
    if (x > 1.0e8f || x < -1.0e8f) return false;
    if (y > 1.0e8f || y < -1.0e8f) return false;
    if (z > 1.0e8f || z < -1.0e8f) return false;
    return true;
}

inline bool ValidDeaths(uint8_t d) { return d <= kMaxDeaths; }

// The percentage and the stack must agree: a body that says "2 deaths, 100 %" is either a
// rest or a bug, and applying it would either heal a penalised player or penalise a rested
// one.  Refusing it keeps a corrupted body from becoming a state.
inline bool ValidStack(uint8_t deaths, uint8_t pct) { return ValidDeaths(deaths) && pct == (uint8_t)PctFor(deaths); }

// Positions travel quantised, exactly like the position lane (WIRE.md §3.5): 1/8 cm on a
// respawn point is below anything a capsule can feel, and it keeps one encoding for
// "where a player is" in the whole protocol.
inline bool EncodeDown(wire::Writer& w, const DownBody& b)
{
    if (!ValidStack(b.deaths, b.pct) || !Finite3(b.px, b.py, b.pz)) return false;
    w.U8(b.deaths);
    w.U8(b.pct);
    w.U8(b.seq);
    w.I32(wire::QuantizePos(b.px));
    w.I32(wire::QuantizePos(b.py));
    w.I32(wire::QuantizePos(b.pz));
    return w.ok;
}

inline bool DecodeDown(wire::Reader& r, DownBody* b)
{
    memset(b, 0, sizeof(*b));
    b->deaths = r.U8();
    b->pct = r.U8();
    b->seq = r.U8();
    b->px = wire::DequantizePos(r.I32());
    b->py = wire::DequantizePos(r.I32());
    b->pz = wire::DequantizePos(r.I32());
    if (!r.ok) return false;
    return ValidStack(b->deaths, b->pct) && Finite3(b->px, b->py, b->pz);
}

inline bool EncodeUp(wire::Writer& w, const UpBody& b)
{
    if (!ValidStack(b.deaths, b.pct) || !Finite3(b.px, b.py, b.pz)) return false;
    if (!(b.yaw == b.yaw)) return false;
    w.U8(b.deaths);
    w.U8(b.pct);
    w.U8(b.seq);
    w.I32(wire::QuantizePos(b.px));
    w.I32(wire::QuantizePos(b.py));
    w.I32(wire::QuantizePos(b.pz));
    w.I16(wire::QuantizeAngle(b.yaw));
    w.U16(b.hp);
    w.U16(b.hp_max);
    w.Str(b.name);
    return w.ok;
}

inline bool DecodeUp(wire::Reader& r, UpBody* b)
{
    memset(b, 0, sizeof(*b));
    b->deaths = r.U8();
    b->pct = r.U8();
    b->seq = r.U8();
    b->px = wire::DequantizePos(r.I32());
    b->py = wire::DequantizePos(r.I32());
    b->pz = wire::DequantizePos(r.I32());
    b->yaw = wire::DequantizeAngle(r.I16());
    b->hp = r.U16();
    b->hp_max = r.U16();
    if (r.Str(b->name, kNameCap) < 0) return false;
    if (!r.ok) return false;
    return ValidStack(b->deaths, b->pct) && Finite3(b->px, b->py, b->pz) &&
           b->hp <= b->hp_max;
}

inline bool EncodeRest(wire::Writer& w, const RestBody& b)
{
    if (!ValidStack(b.deaths, b.pct) || !Finite3(b.px, b.py, b.pz)) return false;
    w.U8(b.deaths);
    w.U8(b.pct);
    w.U8(b.seq);
    w.I32(wire::QuantizePos(b.px));
    w.I32(wire::QuantizePos(b.py));
    w.I32(wire::QuantizePos(b.pz));
    w.Str(b.name);
    return w.ok;
}

inline bool DecodeRest(wire::Reader& r, RestBody* b)
{
    memset(b, 0, sizeof(*b));
    b->deaths = r.U8();
    b->pct = r.U8();
    b->seq = r.U8();
    b->px = wire::DequantizePos(r.I32());
    b->py = wire::DequantizePos(r.I32());
    b->pz = wire::DequantizePos(r.I32());
    if (r.Str(b->name, kNameCap) < 0) return false;
    if (!r.ok) return false;
    return b->deaths == 0 && b->pct == (uint8_t)PctFor(0) && Finite3(b->px, b->py, b->pz);
}

// ---------------------------------------------------------------------------
// The receiver's ordering rule
// ---------------------------------------------------------------------------
// The lane is reliable, so a body is normally applied once.  A retransmission that crossed
// a newer body is the case worth stating: the newer sequence wins, and a duplicate of the
// newest is dropped rather than applied twice (a second UP would teleport the puppet back
// to a rest point the player has since left).  Signed age, because a sender may stamp a
// body in the same tick that the receiver's clock read `now` -- the negative-age trap from
// the trigger lane.
class SeqGate {
public:
    void Reset() { have_ = false; last_ = 0; }
    // True when this body is the newest seen and must be applied.
    bool Accept(uint8_t seq)
    {
        if (have_ && seq == last_) return false;
        int8_t age = (int8_t)(seq - last_);
        if (have_ && age < 0) return false;
        have_ = true;
        last_ = seq;
        return true;
    }
    bool Have() const { return have_; }
    uint8_t Last() const { return last_; }

private:
    bool    have_ = false;
    uint8_t last_ = 0;
};

} // namespace respawn
} // namespace qd
