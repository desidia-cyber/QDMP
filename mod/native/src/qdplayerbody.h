// qdplayerbody.h -- the rules that come with spawning the remote body as a real player
// class (B7), and the only two places where that body changes what the game decides.
//
// WHY A SEPARATE HEADER
// ---------------------
// Spawning an `FS_PlayerChar_C` gives the mod something it never had: an object the
// enemy Blueprints will both TARGET and DAMAGE (SYNC-RESEARCH 1b -- two cast gates, to
// `FS_PlayerChar_C` and to `FB_DamageableInterface`, which a `SkeletalMeshActor` fails).
// Three decisions become possible the moment that body exists, and every one of them is
// silently wrong when its rule is wrong:
//
//   1. Which player an enemy's `TargetPlayer` should point at.  `TargetPlayer` is a
//      Blueprint variable the AI reads directly (SYNC-RESEARCH 1a), so writing it is the
//      only mechanism that reaches the AI -- and writing it when the AI picked something
//      that is not a player destroys its aggro.  The rule must therefore only ever
//      redirect a target that is ALREADY one of the two players, with hysteresis so two
//      nearby players cannot make one enemy flap between them.
//
//   2. Whether the described-damage lane (qdenemydmg.h) still has to describe a swing.
//      Once the game itself resolves a hit on the proxy body, that hit is forwarded with
//      the game's own damage amount; describing the same swing from the montage as well
//      is double damage on the partner.  This is a rule, not a special case: "if the real
//      hit path handled it, the estimate must stay quiet".
//
//   3. What a re-created enemy starts at.  `PlayerIsInActivationRange` and
//      `PlayerDistanceDespawnCheck` are Blueprint functions that read
//      `GetPlayerCharacter` -- player 0 of THIS machine -- so a host's spawner cannot see
//      the partner standing next to the enemy and re-creates an enemy the partner is
//      already fighting (G5).  The mod cannot change that answer; what it can refuse to
//      do is let the re-created actor be a FRESH fight.  The partner's own row carries
//      the HP it has already taken off, and the rule is "converge down to the lower of
//      the two, never up".
//
// WHY THESE ARE PURE
// ------------------
// Each is a rule, not an engine call, and each fails in a way that reads as something
// else in a game: a flapping target looks like an enemy with broken aggro, a doubled
// description like "enemies hit too hard", an adopted HP that goes UP like a healing bug.
// test_playerbody.cpp pins every one of them to a number, which is the only evidence
// available without a second machine.
#pragma once
#include <math.h>
#include <stdint.h>

#include "qdenemy.h"

namespace qd {
namespace playerbody {

// What the enemy's own perception has selected right now.  Only the four states the mod
// can tell apart; everything that is not one of the two players is CUR_OTHER and is left
// alone.
enum Cur : uint8_t {
    CUR_NONE   = 0,   // no target yet (the AI has not picked a player)
    CUR_LOCAL  = 1,   // the local player's pawn
    CUR_REMOTE = 2,   // the proxy body standing in for the partner
    CUR_OTHER  = 3,   // an NPC, a projectile, a corpse: not ours to steer
};

// What the mod should do with the enemy's target this tick.
enum Tgt : uint8_t {
    TGT_LEAVE     = 0,   // leave the AI's decision alone
    TGT_TO_REMOTE = 1,   // point it at the proxy body
    TGT_TO_LOCAL  = 2,   // point it back at the local player
};

// THE HYSTERESIS, AND WHY IT IS SMALLER THAN THE OWNERSHIP MARGIN
// --------------------------------------------------------------
// Both distances are measured on THIS machine, from the same enemy, so the only error is
// that the partner's body is drawn ~100 ms in the past (WIRE.md 5): ~50 cm at a 500 cm/s
// run.  The 800 cm margin in qdenemyown.h has to absorb a lagging frozen copy as well;
// this one does not, and a margin that large would let an enemy keep chasing a player who
// has walked away.  200 cm is four times the render-delay error and still small enough
// that "the nearer player" is the answer whenever the two are genuinely apart.
static constexpr float kSwitchCm = 200.0f;

inline float Dist3(const float a[3], const float b[3])
{
    float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

// The next target for one enemy.
//   can_retarget   the machine running the enemy, with a session and a body to point at
//   remote_usable  the proxy body exists, is alive and is not hidden
//   d_local        enemy -> local player, cm; negative means "not measured"
//   d_remote       enemy -> proxy body, cm; negative means "not measured"
inline Tgt ChooseTarget(bool can_retarget, bool remote_usable, int cur,
                        float d_local, float d_remote)
{
    if (!can_retarget) return TGT_LEAVE;
    // A target the mod set, on a body that no longer exists, is left pointing at a
    // destroyed actor.  Restoring it is not a preference, it is a repair.
    if (cur == CUR_REMOTE && !remote_usable) return TGT_TO_LOCAL;
    // Only a target that is already a player is ever redirected: an enemy chasing an NPC,
    // a projectile or nothing keeps the AI's decision.
    if (cur != CUR_LOCAL && cur != CUR_REMOTE) return TGT_LEAVE;
    if (!remote_usable) return TGT_LEAVE;
    if (d_local < 0.0f || d_remote < 0.0f) return TGT_LEAVE;    // unmeasured: no switch
    if (cur == CUR_LOCAL && d_remote + kSwitchCm < d_local) return TGT_TO_REMOTE;
    if (cur == CUR_REMOTE && d_local + kSwitchCm < d_remote) return TGT_TO_LOCAL;
    return TGT_LEAVE;                       // inside the margin: keep what the AI has
}

// True when this swing must NOT be described by the montage lane, because the game's own
// hit path already carried it.  A swing the enemy aimed at the LOCAL player is still
// described the old way: that hit lands for real on this machine's own pawn, and the
// partner is not its victim.
//
// `real_hit_proven` is the third fact and it is not decoration.  The proxy body exists
// with collision OFF (R57), and this build's melee resolves its victims itself; so "the
// enemy is aimed at the proxy" does not mean the game will apply damage to it.  Measured
// (R74): the enemy was steered at the proxy, swung, and produced neither
// `player body hit ... -> sent` (the game never applied a hit) nor `damage sent` (this
// rule had suppressed the description) -- the partner was hit by nothing at all.  So the
// real path may suppress the description only after it has landed one hit on this body.
inline bool SwingAlreadyReal(bool proxy_usable, bool enemy_targets_proxy, bool real_hit_proven)
{
    return proxy_usable && enemy_targets_proxy && real_hit_proven;
}

// The HP fraction (0..65535, WIRE.md 3.9) a freshly created enemy must be brought down
// to.  `peer_fresh` is "the other machine's row for this GUID is recent enough to
// describe an enemy in the same world".  Never raises: the answer is the lower of the
// two, so a row that is older than the local state cannot heal the copy.
inline uint16_t AdoptedHpFrac(uint16_t local_frac, bool have_local,
                             uint16_t peer_frac, bool have_peer, bool peer_fresh)
{
    if (!have_peer || !peer_fresh) return have_local ? local_frac : 0;
    if (!have_local) return peer_frac;
    return peer_frac < local_frac ? peer_frac : local_frac;
}

} // namespace playerbody
} // namespace qd
