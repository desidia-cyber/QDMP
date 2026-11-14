// qdenemyown.h -- who runs an enemy, decided the same way on both machines.
//
// THE PROBLEM THE RULE SOLVES
// ---------------------------
// The game's enemy Blueprints aggro, chase and attack through
// `GetPlayerCharacter` / `TargetPlayer` / `GetTargetPlayer`, which on each machine mean
// player 0 of THAT machine (SYNC-GAPS G4, measured in the cooked names).  A
// host-simulated enemy therefore never targets, chases or damages the peer, and the
// peer's copy of it is frozen (`bIsAIDisabled`, actor tick off).  Without a Blueprint
// patch the only way an enemy fights the peer is for the peer's own machine to run it,
// and the only signal both machines already hold for "whose turn" is proximity: each
// machine knows both players' positions (the PLAYER lane) and its own copy's position.
//
// So ownership follows the nearer player.  The machine whose local player is nearer
// runs the AI; the other freezes its copy and follows the owner's rows.  Both machines
// compute the same preference from the same data, which is what makes it deterministic.
//
// RETARGETING WAS MEASURED AND REFUTED, so this is not a shortcut.  `TargetPlayer` is a
// Blueprint ObjectProperty variable on `EnemyBase_C` (+0x0C90, 18 bytecode sites) and
// `GetTargetPlayer` is a native UFunction the Blueprint overrides but never calls, so a
// ProcessEvent post-hook would never fire.  The variable is writable, but two cast gates
// admit only an `FS_PlayerChar_C` implementing `FB_DamageableInterface`, which the
// puppet is not.  Proximity ownership is therefore the only route that needs no
// Blueprint patch; do not add a retarget hook on top of it.
//
// THE CONSTANTS, AND WHY THEY ARE THESE
// -------------------------------------
//   kMarginCm   800   A switch needs the two players at least 8 m apart in favour of the
//                     new owner.  The machines do not see identical geometry: a frozen
//                     copy lags its owner's rows by ~25 cm at a 500 cm/s run (50 ms of
//                     rows eased with the 100 ms constant of qdenemy.h Follow), and the
//                     remote player is drawn 100 ms in the past (~50 cm).  The margin is
//                     an order of magnitude above that sum, so the two distance tests
//                     cannot disagree about who is nearer.
//   kHoldMs    1500   The preference must persist.  A hand-off costs a few datagrams
//                     (claim out, confirming rows back); 1.5 s at 20 Hz cannot be
//                     satisfied by jitter, so two players standing near each other
//                     cannot make an enemy flap.
//   kReleaseMs 2000   A frozen copy whose owner has gone silent takes its own AI back.
//                     Same figure as the pre-existing DRIVEN release: one lost row must
//                     not wake the AI and double-simulate.
//   kHandoffMs 2000   A release the other machine never confirms is abandoned and the
//                     sender resumes simulating.  That is the guard against a false
//                     claim leaving a statue on both machines.
//   kTieCm      0.5   Exact ties (both players equidistant to within half a centimetre)
//                     are broken by role so both machines agree.
//
// THE HANDSHAKE, AND WHY A CLAIM IS NOT A MEASUREMENT
// ---------------------------------------------------
//   O_LOCAL    run the AI; publish rows with the claim bit CLEAR ("I run it").
//   O_HANDOFF  our distance says the other machine should run it; publish rows with the
//              claim bit SET ("I have handed it to you") but KEEP SIMULATING until the
//              other's own rows come back with the bit clear.  That is "release only
//              after both agree": the claim alone does not stop the AI, because a lost
//              claim would otherwise leave the enemy frozen on both machines.
//   O_FROZEN   the other machine runs it; our copy is frozen and follows its rows.  A
//              claim from the other is honoured immediately -- even when our own
//              distance disagrees -- because a claim is a decision, not a measurement.
//              The copy is released to its own AI only when the owner is silent for
//              kReleaseMs.
//
// A DISPUTE CANNOT PING-PONG.  A machine that takes ownership on a claim that
// contradicts its own distance (it thinks the other should have it) records a dispute.
// While that contradiction persists a peer does not hand the enemy straight back --
// otherwise two machines holding stale positions would pass it back and forth forever,
// each convinced it is the far one.  The host is allowed to reverse a disputed hand-off,
// so a dispute can flip at most once and then settles; the latch clears as soon as the
// local distance stops saying the other machine should own it.
//
// Header-only and dependency-free, compiled into native/test like the other state
// machines.
#pragma once
#include <math.h>
#include <stdint.h>
#include "qdwire.h"
#include "qdguidindex.h"

namespace qd {
namespace enemyown {

// What a machine does with one enemy this tick.
enum OwnerState : uint8_t {
    O_LOCAL   = 0,   // run the AI; publish rows with the claim bit clear
    O_HANDOFF = 1,   // claim sent; still simulating until the other confirms
    O_FROZEN  = 2,   // the other runs it; follow its rows
};

// How this machine's own distance test sees the two players.
enum Pref : uint8_t { P_HOLD = 0, P_ME = 1, P_THEM = 2 };

// What (if anything) this machine publishes for one enemy.
enum Publish : uint8_t { PUB_NONE = 0, PUB_OWN = 1, PUB_HANDOFF = 2 };

inline float Dist3(const float a[3], const float b[3])
{
    float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

class OwnerSet {
public:
    static const int      kMax = 1024;   // the castle runs ~600 (run 36: 256 overflowed)
    static constexpr float kMarginCm  = 800.0f;
    static constexpr float kTieCm     = 0.5f;
    static const uint32_t  kHoldMs    = 1500;
    static const uint32_t  kReleaseMs = 2000;
    static const uint32_t  kHandoffMs = 2000;
    static const uint32_t  kRecycleMs = 60000;
    static const uint32_t  kYieldMs     = 500;    // double run, the peer not clearly nearer
    static const uint32_t  kYieldNearMs = 4000;   // double run outliving the host's hand-off window

    explicit OwnerSet(bool host = true) : host_(host), overflow_(false), yields_(0) { Reset(); }

    void Reset()
    {
        for (int i = 0; i < kMax; ++i) r_[i].used = false;
        idx_.Clear();
        used_ = 0;
        overflow_ = false;
    }

    void SetHost(bool host) { host_ = host; }

    // Advance one enemy's ownership.  `haveLocal` is false when this machine has no
    // live actor for the GUID (its spawner never activated here): it must not own it.
    // Any of the positions may be missing; the preference then holds (no switch).
    //
    // `sticky` (a boss): ownership never moves by distance once settled -- an owner never
    // hands off and a follower never takes over except on silence.  A boss fight is the
    // one place both players stand together for minutes, and every hand-off mid-fight is a
    // visible jump of the whole model on the follower's screen.
    OwnerState Tick(const uint32_t guid[4], bool haveLocal,
                    const float enemy[3], bool haveEnemy,
                    const float mine[3], bool haveMine,
                    const float theirs[3], bool haveTheirs, uint32_t now,
                    bool sticky = false, float marginCm = kMarginCm)
    {
        if (!(guid[0] | guid[1] | guid[2] | guid[3])) return O_LOCAL;   // not an enemy
        Rec* r = Touch(guid, now);
        if (!r) { overflow_ = true; return O_LOCAL; }   // full table: run locally

        Pref p = P_HOLD;
        if (haveEnemy && haveMine && haveTheirs) p = Preference(mine, theirs, enemy, marginCm);
        // Inside the margin neither distance resolves.  P_HOLD used to mean "both keep
        // running", which is fine when the players are far apart and WRONG when they
        // stand together -- which is the ordinary way to play co-op.  Measured on the rig
        // with the two players 460 cm apart: zero ownership hand-offs with 66-160 enemies
        // alive, i.e. every enemy simulated twice, with two independent AI outcomes and
        // two health pools to converge.  So the tie is broken by role: the host owns, the
        // peer follows.  With the partner's position unknown there is nothing to compare
        // and nothing to follow, so P_HOLD still holds there.
        // A NARROW MARGIN KEEPS ITS OWNER (Session 29, run 38): a grab boss's 3 m margin moved
        // the Queen to the nearer peer, then the role tie-break handed her straight back 1.6 s
        // later when the two players drew within 3 m (peer 207 cm, host 416 cm).  The tie-break
        // exists to end double runs, and a double run is now resolved by the yield in O_LOCAL,
        // so with the narrow margin the current owner simply keeps her inside it.
        if (p == P_HOLD && haveMine && haveTheirs && marginCm >= kMarginCm) p = host_ ? P_ME : P_THEM;
        if (p != (Pref)r->pref) { r->pref = (uint8_t)p; r->prefSince = now; }
        // The dispute latch only suppresses a reversal while the contradiction is still
        // present; a genuine change of distances clears it.
        if (p != P_THEM && r->disputed) r->disputed = 0;

        if (!haveLocal) {
            // No actor here: nothing to simulate.  Following the other's rows is the
            // only thing this machine can do with the GUID.
            if (r->state != O_FROZEN) { r->state = O_FROZEN; r->stateSince = now; }
            return O_FROZEN;
        }

        uint32_t held = now - r->prefSince;
        switch (r->state) {
        case O_LOCAL:
            // TWO MACHINES BOTH SAY "I RUN IT" (Session 29, measured run 33): the peer's
            // constructed Queen copy was frozen, then its OWN spawner's Queen replaced it and
            // started LOCAL; the host ran her too, and every row each sent said "I run it".
            // Nothing in the handshake answered that -- a clear row reaching an O_LOCAL record
            // was "we already agree" -- so both simulated her for the whole fight: two Queens
            // firing at once.  A double run is now resolved by role: the PEER yields and
            // follows; the host's distance test then hands the enemy over the normal way if
            // the peer's player is the nearer one.  It yields at once (0.5 s, past any
            // reordered row from a hand-off in flight) unless its own distance says it is
            // clearly the nearer machine -- then the host's own test is about to hand it over,
            // and yielding first would only flip the model twice; a conflict that outlives
            // that hand-off window is resolved by role all the same.
            if (!host_ && r->conflictSince && now - r->remoteRunMs < kReleaseMs + 500) {
                uint32_t dur = now - r->conflictSince;
                if ((p != P_ME && dur >= kYieldMs) || dur >= kYieldNearMs) {
                    r->state = O_FROZEN; r->stateSince = now;
                    r->conflictSince = 0; r->disputed = 0;
                    ++yields_;
                    break;
                }
            } else if (r->conflictSince && now - r->remoteRunMs >= kReleaseMs + 500) {
                r->conflictSince = 0;
            }
            // Hand off only to a machine that has shown it holds this enemy (it has
            // sent us at least one row for the GUID).  Handing to a machine without
            // the actor would leave the enemy simulated by nobody.
            if (!sticky && p == P_THEM && held >= kHoldMs && r->sawRemote &&
                (host_ || !r->disputed)) {
                r->state = O_HANDOFF;
                r->handoffSince = now;
                r->stateSince = now;
            }
            break;
        case O_HANDOFF:
            // Confirmation is applied in OnRemoteRow.  Here only the abort paths: the
            // decision reversed, or the other machine never answered.
            if (p == P_ME && held >= kHoldMs) {
                r->state = O_LOCAL; r->stateSince = now; r->disputed = 0;
            } else if (now - r->handoffSince >= kHandoffMs) {
                r->state = O_LOCAL; r->stateSince = now;
            }
            break;
        case O_FROZEN:
            // Release ONLY when the owner goes quiet.  A frozen copy used to take the enemy
            // back on its own preference as well -- and the owner, which had not handed
            // anything over, kept simulating: MEASURED in Session 26 on the Green Dragon,
            // both machines ran it for ten seconds, a hit on one never reached the other
            // (host 3300 / peer 3100) and the follower's model jumped between the two
            // simulations ("flickering, visible on one instance").  A follower that should
            // own it gets it the safe way: the owner's own distance test hands it over
            // (O_HANDOFF), and OnRemoteRow honours that claim.  No deadlock: a peer that is
            // handing over sets the claim bit, and the frozen host takes it the moment it
            // reads it.
            if (!r->sawRemote || now - r->lastRemoteMs >= kReleaseMs) {
                r->state = O_LOCAL; r->stateSince = now; r->disputed = 0;
            }
            break;
        default:
            r->state = O_LOCAL; r->stateSince = now;
            break;
        }
        return (OwnerState)r->state;
    }

    // A row arrived from the other machine.  `peerOwns` is its ENEMY row's
    // wire::kEnemyPeerOwns bit.
    void OnRemoteRow(const uint32_t guid[4], bool peerOwns, uint32_t now)
    {
        if (!(guid[0] | guid[1] | guid[2] | guid[3])) return;   // not an enemy
        Rec* r = Touch(guid, now);
        if (!r) { overflow_ = true; return; }
        r->lastRemoteMs = now;
        r->sawRemote = true;
        if (peerOwns) {
            // The sender has handed it over.  Honour the claim even if our own distance
            // disagrees: two distance tests can differ while positions are in flight.
            Pref was = (Pref)r->pref;
            if (r->state == O_FROZEN) {
                r->state = O_LOCAL;
                r->stateSince = now;
                r->conflictSince = 0;
                r->pref = P_HOLD;
                r->prefSince = now;
                r->disputed = (was == P_THEM) ? 1 : 0;
            } else if (r->state == O_HANDOFF) {
                // Both machines handed the enemy to each other at once.  Exactly one
                // may run it; the role decides which, so both agree (host wins).
                r->state = host_ ? O_LOCAL : O_FROZEN;
                r->stateSince = now;
                r->pref = P_HOLD;
                r->prefSince = now;
                r->disputed = 0;
            }
            // O_LOCAL: both sides already agree that we own it.
        } else if (r->state == O_LOCAL) {
            // The other machine says "I run it" while this one runs it too: a double run.
            // Tick resolves it (the peer yields); here it is only recorded.
            if (!r->conflictSince) r->conflictSince = now ? now : 1;
            r->remoteRunMs = now;
        } else if (r->state == O_HANDOFF) {
            r->state = O_FROZEN;       // the other has taken it: the release completes
            r->stateSince = now;
        }
    }

    // The other machine has said, on another lane, that it holds a live copy of this enemy
    // (Session 29, run 37: the boss lane's ACK).  A follower publishes no rows, so a peer that
    // yielded a double run at once never sent the host a row for the key, `sawRemote` stayed
    // false, and the host could never hand the Queen to the nearer peer.  This is the same
    // fact a row proves -- "the other machine has this enemy" -- and nothing else: no
    // ownership state changes here.
    void NoteRemoteHolds(const uint32_t guid[4], uint32_t now)
    {
        Rec* r = Touch(guid, now);
        if (!r) { overflow_ = true; return; }
        if (!r->sawRemote) { r->sawRemote = true; r->lastRemoteMs = now; }
    }

    OwnerState State(const uint32_t guid[4]) const
    {
        const Rec* r = Find(guid);
        return r ? (OwnerState)r->state : O_LOCAL;
    }

    // Unknown GUIDs are LOCAL: a machine that has never heard from the other must run
    // and publish, not freeze into silence.
    Publish Publishing(const uint32_t guid[4]) const
    {
        const Rec* r = Find(guid);
        if (!r) return PUB_OWN;
        if (r->state == O_FROZEN) return PUB_NONE;
        return r->state == O_HANDOFF ? PUB_HANDOFF : PUB_OWN;
    }

    // True when this machine is the one simulating (or still simulating through a
    // hand-off).  Used to decide whether a local kill record belongs on the wire.
    bool Running(const uint32_t guid[4]) const { return Publishing(guid) != PUB_NONE; }

    bool Host() const { return host_; }
    // Diagnostic: the record's state, preference, and the ages of its conflict and of the
    // other machine's latest row (0xFFFFFFFF = never).
    bool Debug(const uint32_t guid[4], uint32_t now, int* state, int* pref,
               uint32_t* conflictAge, uint32_t* rowAge, bool* sawRemote) const
    {
        const Rec* r = Find(guid);
        if (!r) return false;
        *state = r->state; *pref = r->pref; *sawRemote = r->sawRemote;
        *conflictAge = r->conflictSince ? now - r->conflictSince : 0xFFFFFFFFu;
        *rowAge = r->sawRemote ? now - r->lastRemoteMs : 0xFFFFFFFFu;
        return true;
    }
    // How many double runs this machine resolved by yielding (diagnostic).
    uint32_t Yields() const { return yields_; }
    bool Overflowed() const { return overflow_; }
    int  Count() const { int n = 0; for (int i = 0; i < kMax; ++i) n += r_[i].used ? 1 : 0; return n; }

private:
    struct Rec {
        bool     used;
        uint32_t guid[4];
        uint8_t  state;       // OwnerState
        uint8_t  pref;        // Pref
        uint8_t  disputed;    // took ownership on a claim that contradicts our distance
        bool     sawRemote;   // the other machine has sent a row for this GUID
        uint32_t prefSince;
        uint32_t handoffSince;
        uint32_t lastRemoteMs;
        uint32_t stateSince;
        uint32_t touchMs;
        uint32_t conflictSince;   // first "I run it" row from the other while we run it too
        uint32_t remoteRunMs;     // the latest such row
    };

    Pref Preference(const float me[3], const float them[3], const float e[3], float margin) const
    {
        float dm = Dist3(me, e);
        float dt = Dist3(them, e);
        if (dm + margin < dt) return P_ME;
        if (dt + margin < dm) return P_THEM;
        float diff = (dm > dt) ? dm - dt : dt - dm;
        // Exactly equidistant: the host runs it.  Both machines compute P_ME on the
        // host and P_THEM on the peer, so they agree on one owner.
        if (diff <= kTieCm) return host_ ? P_ME : P_THEM;
        return P_HOLD;                     // inside the margin: keep the current owner
    }

    const Rec* Find(const uint32_t g[4]) const
    {
        int i = idx_.Find(g, [this](int k) { return (const uint32_t*)r_[k].guid; });
        return i >= 0 ? &r_[i] : nullptr;
    }
    Rec* Find(const uint32_t g[4])
    {
        return const_cast<Rec*>(static_cast<const OwnerSet*>(this)->Find(g));
    }

    Rec* Touch(const uint32_t g[4], uint32_t now)
    {
        if (!(g[0] | g[1] | g[2] | g[3])) return nullptr;   // a zero GUID is no enemy
        Rec* r = Find(g);
        if (!r) {
            int victim = -1;
            uint32_t oldest = 0;
            if (used_ < kMax) victim = used_;          // records fill in order; never freed singly
            for (int i = 0; victim < 0 && i < kMax; ++i) {
                // Full: recycle the record nothing has touched for a minute.  A record
                // that old is past every window above and decides nothing.
                if (now - r_[i].touchMs > kRecycleMs && now - r_[i].touchMs > oldest) {
                    oldest = now - r_[i].touchMs;
                    victim = i;
                }
            }
            if (victim < 0) return nullptr;
            r = &r_[victim];
            bool recycled = r->used;
            r->used = true;
            for (int k = 0; k < 4; ++k) r->guid[k] = g[k];
            if (victim == used_) ++used_;
            if (recycled) {                            // a GUID changed: rebuild the index
                idx_.Clear();
                for (int k = 0; k < used_; ++k) if (r_[k].used) idx_.Insert(r_[k].guid, k);
            } else {
                idx_.Insert(g, victim);
            }
            r->state = O_LOCAL;            // run and publish until the other speaks
            r->pref = P_HOLD;
            r->disputed = 0;
            r->sawRemote = false;
            r->prefSince = now;
            r->handoffSince = 0;
            r->lastRemoteMs = 0;
            r->stateSince = now;
            r->conflictSince = 0;
            r->remoteRunMs = 0;
        }
        r->touchMs = now;
        return r;
    }

    Rec  r_[kMax];
    GuidIndex<2 * kMax> idx_;
    int  used_ = 0;
    bool host_;
    bool overflow_;
    uint32_t yields_;
};

// ---------------------------------------------------------------------------
// An enemy montage name -> wire::EnemyAnim
// ---------------------------------------------------------------------------
// Case-insensitive "does `s` contain `k`".  BOTH sides are folded.  The first version
// lowered only the haystack, so a needle containing an uppercase letter could never
// match -- measured (Session 6): a `"VideoConfig"` guard deciding whether to force a
// level visible silently answered "no" and the level was forced anyway, which is the
// change that killed the game.  Every older call site passes an already-lowercase
// needle, so folding the needle changes none of their answers.
inline bool HasCI(const char* s, const char* k)
{
    for (int i = 0; s[i]; ++i) {
        int j = 0;
        for (; k[j]; ++j) {
            char a = s[i + j], b = k[j];
            if (!a) return false;
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b) break;
        }
        if (!k[j]) return true;
    }
    return false;
}

inline bool EndsCI(const char* s, const char* tail)
{
    int a = 0, b = 0;
    while (s[a]) ++a;
    while (tail[b]) ++b;
    if (b > a) return false;
    for (int i = 0; i < b; ++i) {
        char x = s[a - b + i], y = tail[i];
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return false;
    }
    return true;
}

// The enemy's own montages are not the player's first-person arms, so
// avatar::ClassifyMontage (which names moves like a slash or a charge) is the wrong
// vocabulary here.  A driven copy only needs to know which of the WIRE.md 3.9a kinds a
// montage is, so the peer can play the same asset.  Order matters: "death" before
// "hit", "spit" (a ranged cast) before "attack".  "die" is matched only as a word end
// so that a name like "SoldierWalk" is not read as a death.
inline uint8_t ClassifyAnim(const char* n)
{
    if (!n || !n[0]) return wire::E_NONE;
    if (HasCI(n, "death") || HasCI(n, "dying") || HasCI(n, "_die") || EndsCI(n, "die") ||
        EndsCI(n, "dead") || HasCI(n, "_dead")) return wire::E_DIE;
    if (HasCI(n, "hurt") || HasCI(n, "flinch") || HasCI(n, "hitreact") ||
        HasCI(n, "_hit") || HasCI(n, "hit_") || HasCI(n, "recoil") ||
        HasCI(n, "damage")) return wire::E_HURT;
    if (HasCI(n, "roar") || HasCI(n, "scream") || HasCI(n, "shout")) return wire::E_ROAR;
    if (HasCI(n, "cast") || HasCI(n, "spell") || HasCI(n, "magic") || HasCI(n, "summon") ||
        HasCI(n, "spit")) return wire::E_CAST;
    if (HasCI(n, "attack") || HasCI(n, "slash") || HasCI(n, "swing") || HasCI(n, "claw") ||
        HasCI(n, "bite") || HasCI(n, "sting") || HasCI(n, "punch") ||
        HasCI(n, "swipe")) return wire::E_ATTACK;
    // "run" only as its own word: "Grunt" contains it, and a Grunt's idle montage is
    // not a locomotion kind.
    if (HasCI(n, "walk") || HasCI(n, "_run") || HasCI(n, "run_") || EndsCI(n, "run") ||
        HasCI(n, "locomotion") || HasCI(n, "_move") || EndsCI(n, "move") ||
        HasCI(n, "fly")) return wire::E_MOVE;
    return wire::E_NONE;
}

// ---------------------------------------------------------------------------
// Status bits, from the two byte properties the shipped build exposes
// ---------------------------------------------------------------------------
// `bEnemyPoisoned` is the name in the shipped image (analysis/exe_names.txt); the
// unprefixed `Poisoned` is kept as a fallback for a build that renamed it.  There is no
// STAGGER bit in STATUS; stagger is the game's "heavy" condition (ST_HEAVY), the same
// bit the player's HeavyStatusEffect maps to in proxy.cpp.
inline uint16_t StatusMask(bool poisoned, bool staggered)
{
    uint16_t m = 0;
    if (poisoned)  m |= (uint16_t)wire::ST_POISON;
    if (staggered) m |= (uint16_t)wire::ST_HEAVY;
    return m;
}

} // namespace enemyown
} // namespace qd
