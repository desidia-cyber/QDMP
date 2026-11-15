// qdcombatvfx.h -- what a combat animation MEANS, and which spawned thing is a
// player's combat visual worth copying to the partner's screen.
//
// WHY THIS IS A SEPARATE HEADER
// -----------------------------
// The remote body (qdavatar.h) and the effect lane (proxy.cpp) both have to answer
// questions that are pure data questions, and that were wrong in ways no log line
// could show:
//
//   1. "Which move is this montage?"  The old rule matched English words in order
//      (slash / attack / stab ...).  It reads the player's *generic* sword swing
//      correctly, but the game gives EVERY weapon its own montage set -- 120
//      montages under Content/Items/Weapons -- and the ones that do not spell their
//      move in English fell through to the generic horizontal swing or to nothing
//      at all.  `Player_FPArms_Hammer_AltAttack_Montage` (an overhead slam),
//      `Player_LanceAttack_1_Montage` (a thrust) and
//      `Player_FPArms_Claw_AltAttack_Montage` (a claw rake) all played a flat
//      right-to-left sword slash on the partner's body, and
//      `Player_Greatsword-Alt_Montage`, `Shotgun_Fire_Montage`,
//      `Player_FPArms_CrushingLance_Alt_Montage` played *nothing*.  The fix is not
//      more English keywords: it is to read the WEAPON out of the montage name (the
//      game names every weapon montage after its weapon) and pick the move that
//      weapon's alt attack actually is.  `Hammer_AltSlam_VFX`,
//      `CyberHammer_GroundBlast`, `HarpoClub_AltSlam_VFX` in the same folder are the
//      corpus telling us what a hammer alt is.
//
//   2. "Is this actor a projectile I should copy?"  The lane accepted exactly one
//      class, `Dagger_Projectile_Base_C`.  The player's other 20-odd projectiles --
//      every gun, the crossbow bolt, the rocket, the spear/hammer throws, the
//      travelling ground waves, the arc projectiles -- derive from `Actor` directly
//      and were invisible to the partner.  Class identity is the wrong test: the
//      right test is what the engine uses to move it (`ProjectileMovement`) plus the
//      name the game gave it, which is stable across all of them.
//
//   3. "Which hand-held visual is on the player?"  Weapon visuals are two unrelated
//      Blueprint roots (`WeaponVisuals_C` for weapons, `Dagger_Visuals_Base_C` for
//      every dagger/thrown item), both deriving straight from `Actor`.  A scan
//      filtered on `WeaponVisuals_C` shows a partner's sword but never their
//      dagger.  The fix is to accept both roots and, for the rest, the one thing
//      every hand item shares: its class name says "Visuals".
//
//   4. "Which of the systems on a projectile is the IMPACT one?"  The game names its
//      impact systems `*_Impact`, `*_ImpactFX`, `*_ProjectileImpact`, `*_Explosion`,
//      `*_GroundBlast`: 29 of them under Items/Weapons alone.  A copy that cannot
//      collide never plays one, so the partner sees a thrown dagger stop in mid-air.
//
// Everything here is a pure function of strings and bits so test_combatvfx.cpp can
// pin it without a game.  The kinds are numerically identical to `avatar::ActKind`
// (qdavatar.h), which is the type that actually reaches the wire; test_combatvfx.cpp
// asserts the two agree, so a kind added on one side cannot silently shift on the
// other.  `PLAYER.action` carries the kind in 5 bits, so the new kinds need no wire
// change.
#pragma once
#include <stdint.h>

namespace qd {
namespace cxf {

// ---------------------------------------------------------------------------
// Move kinds.  MUST equal avatar::ActKind (static_assert in the test).
// ---------------------------------------------------------------------------
enum Kind : int {
    K_NONE           = 0,
    K_ATTACK         = 1,
    K_HURT           = 2,
    K_DYING          = 3,
    K_DEAD           = 4,
    K_SLASH_R        = 5,
    K_SLASH_L        = 6,
    K_SLASH_DOWN     = 7,
    K_THRUST         = 8,
    K_CHARGE         = 9,
    K_THROW          = 10,
    K_USE            = 11,
    K_SWAP           = 12,
    K_CAST           = 13,
    K_CHARGE_THRUST  = 14,
    // Added here: the three moves the weapon-specific montage sets needed and the
    // old word list had no name for.
    K_SHOOT          = 15,   // a gun / crossbow / launcher fires
    K_SPIN           = 16,   // scythe, flail, morning star: a sweep around the body
    K_FLURRY         = 17,   // claw, katar, cestus: several fast strikes in a row
};

// -1 is the "not a move of its own" answer (a wind-up's other hand, a jump, a
// follow-through).  Keep it distinct from K_NONE (0), which means "no move".
static const int kNotAMove = -1;

// ---------------------------------------------------------------------------
// Weapon shapes.  Derived from the montage name first (every weapon names its own
// montages) and from the held mesh path second, so a classification survives a
// stale equipment sample.
// ---------------------------------------------------------------------------
enum Shape : int {
    SH_UNKNOWN = 0,
    SH_GREATSWORD, SH_SWORD, SH_DAGGER, SH_RAPIER, SH_KATAR, SH_CLAW, SH_CESTUS,
    SH_SPEAR, SH_LANCE, SH_HAMMER, SH_MACE, SH_AXE, SH_CLUB, SH_SCYTHE, SH_FLAIL,
    SH_GUN, SH_BOW, SH_STAFF, SH_TORCH, SH_SHIELD, SH_BAT,
};

namespace detail {

inline bool HasCI(const char* s, const char* k)
{
    for (int i = 0; s[i]; ++i) {
        int j = 0;
        for (; k[j]; ++j) {
            char a = s[i + j], b = k[j];
            if (!a) return false;
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (a != b) break;
        }
        if (!k[j]) return true;
    }
    return false;
}

} // namespace detail

inline bool HasCI(const char* s, const char* k) { return detail::HasCI(s, k); }

// Most specific first: "greatsword" before "sword", "longhammer" before "hammer".
// Each entry is a substring of the asset name the game itself chose.
struct ShapeWord { const char* word; Shape shape; };
static const ShapeWord kShapeWords[] = {
    // Two-handed blades, then plain blades.
    {"greatsword", SH_GREATSWORD}, {"greatswrd", SH_GREATSWORD},
    {"zweihander", SH_GREATSWORD}, {"wodasogs", SH_GREATSWORD},
    {"wodasogreatsword", SH_GREATSWORD}, {"byrdsword", SH_SWORD},
    // Guns and launchers fired the "fire"/"shoot" montages.
    {"crossbow", SH_BOW}, {"shotgun", SH_GUN}, {"sniper", SH_GUN},
    {"rifle", SH_GUN}, {"plasma", SH_GUN}, {"rocketlauncher", SH_GUN},
    {"launcher", SH_GUN}, {"elecgun", SH_GUN}, {"slashgun", SH_GUN},
    {"svt38", SH_GUN}, {"gun", SH_GUN}, {"bow", SH_BOW},
    // Polearms.
    {"lance", SH_LANCE}, {"spear", SH_SPEAR}, {"halberd", SH_SPEAR},
    {"trident", SH_SPEAR}, {"polearm", SH_SPEAR},
    // Blunt.
    {"hammer", SH_HAMMER}, {"mace", SH_MACE}, {"club", SH_CLUB},
    {"morningstar", SH_FLAIL}, {"flail", SH_FLAIL}, {"bat", SH_BAT},
    {"cestus", SH_CESTUS}, {"cook", SH_CLUB},
    // Edged.
    {"greataxe", SH_AXE}, {"axe", SH_AXE}, {"scythe", SH_SCYTHE},
    {"katar", SH_KATAR}, {"claw", SH_CLAW}, {"rapier", SH_RAPIER},
    {"dagger", SH_DAGGER}, {"knife", SH_DAGGER}, {"needle", SH_DAGGER},
    {"sword", SH_SWORD}, {"blade", SH_SWORD}, {"katana", SH_SWORD},
    {"saber", SH_SWORD}, {"sabre", SH_SWORD},
    // Non-weapon hand items that still need the right hand and a pose.
    {"torch", SH_TORCH}, {"lamp", SH_TORCH}, {"lantern", SH_TORCH},
    {"shield", SH_SHIELD}, {"staff", SH_STAFF}, {"rod", SH_STAFF},
    {"wand", SH_STAFF},
};

// The MOST SPECIFIC word wins, and the table is ordered that way, so the first hit
// is the answer.
inline Shape ShapeOfName(const char* s)
{
    if (!s || !s[0]) return SH_UNKNOWN;
    for (int i = 0; i < (int)(sizeof(kShapeWords) / sizeof(kShapeWords[0])); ++i)
        if (HasCI(s, kShapeWords[i].word)) return kShapeWords[i].shape;
    return SH_UNKNOWN;
}

inline const char* ShapeName(Shape s)
{
    switch (s) {
    case SH_GREATSWORD: return "greatsword"; case SH_SWORD: return "sword";
    case SH_DAGGER: return "dagger"; case SH_RAPIER: return "rapier";
    case SH_KATAR: return "katar"; case SH_CLAW: return "claw";
    case SH_CESTUS: return "cestus"; case SH_SPEAR: return "spear";
    case SH_LANCE: return "lance"; case SH_HAMMER: return "hammer";
    case SH_MACE: return "mace"; case SH_AXE: return "axe";
    case SH_CLUB: return "club"; case SH_SCYTHE: return "scythe";
    case SH_FLAIL: return "flail"; case SH_GUN: return "gun";
    case SH_BOW: return "bow"; case SH_STAFF: return "staff";
    case SH_TORCH: return "torch"; case SH_SHIELD: return "shield";
    case SH_BAT: return "bat"; default: return "unknown";
    }
}

// A two-handed weapon's left hand holds the shaft, not another item.  Used to
// decide whether the free left hand should support the weapon.
inline bool ShapeTwoHanded(Shape s, const char* name)
{
    switch (s) {
    case SH_GREATSWORD: case SH_LANCE: case SH_SPEAR: case SH_SCYTHE:
    case SH_FLAIL: case SH_BOW: case SH_STAFF:
        return true;
    case SH_AXE: case SH_HAMMER: case SH_MACE: case SH_CLUB:
        // Long/great variants of the one-handed blunt weapons are two-handed; the
        // plain ones are not.
        return name && (HasCI(name, "great") || HasCI(name, "long") ||
                        HasCI(name, "war") || HasCI(name, "giant"));
    default:
        return false;
    }
}

// Heavy shapes whose ALT attack is an overhead slam in this game's own content
// (`Hammer_AltSlam_VFX`, `HarpoClub_AltSlam_VFX`, `CyberHammer_GroundBlast`,
// `DuckHammer`/`DustyClub`/`WoodClub`/`SpikedMace`/`EnergyBallHammer` alt montages,
// `DiamondBlade_DownSmackMontage`).
inline bool ShapeAltIsSlam(Shape s)
{
    return s == SH_HAMMER || s == SH_MACE || s == SH_CLUB || s == SH_AXE ||
           s == SH_BAT || s == SH_GREATSWORD;
}

// ---------------------------------------------------------------------------
// Wind-up ("charge") recognition, split off because it decides whether the body
// HOLDS a pose or plays a strike.
//
// Measured: every click plays `Player_FPArms_Std_Charge_Attack_Montage` and the
// strike lands on RELEASE, 0.33-0.58 s in.  The rapier's alt is
// `..._AltAttack_Charge` then `..._AltAttack_Startup`.  So a name containing
// "charge" is the wind-up -- except `Charged...` / "..._ChargedX", which is the
// charged STRIKE that follows it (`TiniScythe_ChargedHammerAttack`).
// ---------------------------------------------------------------------------
inline bool IsWindUp(const char* n)
{
    if (!n || !n[0]) return false;
    if (HasCI(n, "charged")) return false;         // a charged strike, not a wind-up
    return HasCI(n, "charge") || HasCI(n, "windup") || HasCI(n, "wind_up");
}

// The move a wind-up releases, or K_NONE when the name is not a wind-up.
// `shape` refines "a charge attack" into a swing or a stab.
inline int ReleaseMove(const char* n, Shape shape)
{
    if (!n || !n[0]) return K_NONE;
    if (HasCI(n, "std_charge_attack") || HasCI(n, "rotated_charge_attack"))
        return K_ATTACK;
    if (HasCI(n, "stab_charge")) return K_THRUST;
    if (!IsWindUp(n)) return K_NONE;
    // A weapon with a point releases into a thrust; a heavy one into a slam.
    if (shape == SH_LANCE || shape == SH_SPEAR || shape == SH_RAPIER ||
        shape == SH_DAGGER || shape == SH_KATAR || shape == SH_SWORD)
        return K_THRUST;
    if (ShapeAltIsSlam(shape)) return K_SLASH_DOWN;
    return K_ATTACK;
}

// ---------------------------------------------------------------------------
// The classifier.  `name` is the montage/sequence asset name playing on the
// first-person arms; `held` is the item's mesh path ("" when unknown).  Returns a
// Kind >= 0, or kNotAMove when the montage is not a move of its own.
// ---------------------------------------------------------------------------
inline int ClassifyMontage(const char* name, const char* held = "")
{
    if (!name || !name[0]) return K_NONE;

    // 1. Death first: `Player_FPArms_Death_Montage` used to match "eat" -> a use.
    if (HasCI(name, "death") || HasCI(name, "_die") || HasCI(name, "dying")) return K_DYING;

    // 2. Not moves of their own: the other hand's reaction during a strike, jumps,
    //    landings, and a thrown weapon coming back.
    if (HasCI(name, "meleeresponse") || HasCI(name, "jump") || HasCI(name, "landing") ||
        HasCI(name, "catch"))
        return kNotAMove;

    // 3. Equipping / swapping / reloading: the hands are busy with the weapon.
    if (HasCI(name, "swap") || HasCI(name, "equip") || HasCI(name, "reload") ||
        HasCI(name, "toscythe") || HasCI(name, "tohammer") || HasCI(name, "toform"))
        return K_SWAP;

    // The weapon the move belongs to.  The montage name carries it; the held mesh
    // is the fallback (a stale equipment sample still names the same weapon).
    Shape shape = ShapeOfName(name);
    if (shape == SH_UNKNOWN) shape = ShapeOfName(held);

    // 4. A wind-up is held; its release is whatever the weapon does.
    if (IsWindUp(name)) {
        int rel = ReleaseMove(name, shape);
        return rel == K_THRUST ? K_CHARGE_THRUST : K_CHARGE;
    }

    // 5. Left-hander follow-through montages are not new moves.
    if (HasCI(name, "player_slash_")) return kNotAMove;

    // 5b. `Charged...` is the strike the wind-up released, so it takes the shape's
    //     own move (`TiniScythe_ChargedHammerAttack` is an overhead, not a slash).
    if (HasCI(name, "charged"))
        return shape == SH_SCYTHE ? K_SPIN : (ShapeAltIsSlam(shape) ? K_SLASH_DOWN : K_SLASH_R);

    // 6. Ranged: a gun, a crossbow, a launcher, a needle syringe.
    if (HasCI(name, "fire") || HasCI(name, "shoot") || HasCI(name, "muzzle"))
        return K_SHOOT;

    // 7. Throw (checked before "attack": `Player_FPArms_Attack_Scythe_Throw` is a
    //    throw, and `Warhammer_Throw_Attack-Montage` is too).
    if (HasCI(name, "throw") || HasCI(name, "toss") || HasCI(name, "hurl"))
        return K_THROW;

    // 8. Player items whose use is not a swing.  "eat" is deliberately NOT a keyword:
    //    as a substring it matches `Gr-eat-spear_AltAttack_Montage`,
    //    `Lanc-eAt-tack_1_Montage` and `Gr-eat-sword-Alt_Montage`, which is exactly how
    //    three weapon alts came to play the drink-a-potion pose.
    if (HasCI(name, "applyacid")) return K_USE;
    if (HasCI(name, "drink") || HasCI(name, "consum") || HasCI(name, "potion") ||
        HasCI(name, "useitem") || HasCI(name, "use_item") || HasCI(name, "eating"))
        return K_USE;

    // 9. Directional words.  "down"/"slam" before "slash" so
    //    `Player_ClawSlash_LeftDown_Montage` is a downward strike.
    if (HasCI(name, "dash") || HasCI(name, "startup") || HasCI(name, "lunge")) return K_THRUST;
    if (HasCI(name, "down") || HasCI(name, "overhead") || HasCI(name, "slam") ||
        HasCI(name, "smash") || HasCI(name, "smack") || HasCI(name, "vertical") ||
        HasCI(name, "ground"))
        return K_SLASH_DOWN;
    if (HasCI(name, "spin") || HasCI(name, "whirl") || HasCI(name, "sweep"))
        return K_SPIN;

    // 10. Point-and-drive weapons.
    if (HasCI(name, "stab") || HasCI(name, "thrust") || HasCI(name, "poke") ||
        HasCI(name, "pierce") || HasCI(name, "needle") || shape == SH_LANCE ||
        shape == SH_SPEAR || shape == SH_RAPIER)
        return K_THRUST;

    // 11. Multi-hit sets: claws, katars, cestus.
    if (HasCI(name, "flurry") || HasCI(name, "combo") ||
        HasCI(name, "attack1") || HasCI(name, "attack2") || HasCI(name, "attack3") ||
        HasCI(name, "attack_1") || HasCI(name, "attack_2") || HasCI(name, "attack_3"))
        return K_FLURRY;

    // 12. The ALT attack, resolved by what that weapon's alt actually is.  This is
    //     the fix: the old rule sent every one of these to a generic sword swing.
    if (HasCI(name, "alt") || HasCI(name, "special")) {
        if (shape == SH_SCYTHE || shape == SH_FLAIL) return K_SPIN;
        if (ShapeAltIsSlam(shape)) return K_SLASH_DOWN;
        if (shape == SH_LANCE || shape == SH_SPEAR || shape == SH_RAPIER ||
            shape == SH_DAGGER || shape == SH_KATAR || shape == SH_SWORD)
            return K_THRUST;
        if (shape == SH_GUN || shape == SH_BOW) return K_SHOOT;
        if (shape == SH_CLAW || shape == SH_CESTUS) return K_SLASH_R;
        // An unknown weapon: a swing is the honest default.
        return K_SLASH_R;
    }

    // 13. Directional slashes.
    if (HasCI(name, "slash_left") || HasCI(name, "slashleft") || HasCI(name, "_left"))
        return K_SLASH_L;
    if (HasCI(name, "slash") || HasCI(name, "swing") || HasCI(name, "attack") ||
        HasCI(name, "hit") || HasCI(name, "strike") || HasCI(name, "melee"))
        return K_SLASH_R;

    // 14. A weapon with a spin head sweeps even when the name only says "attack".
    if (shape == SH_SCYTHE || shape == SH_FLAIL) return K_SPIN;

    // 15. Status reactions.
    if (HasCI(name, "hurt") || HasCI(name, "damage") || HasCI(name, "flinch")) return K_HURT;

    // 16. A cast is a grimoire/inscription, never a magic-looking weapon name
    //     (`Player_FPArms_MagicStoneSword_Alt` is a sword alt, handled above).
    if (HasCI(name, "grimoire") || HasCI(name, "inscrib") || HasCI(name, "castspell") ||
        HasCI(name, "spell"))
        return K_CAST;
    return kNotAMove;
}

// ---------------------------------------------------------------------------
// Is this spawned actor one of the player's own projectiles?
//
// CLASS IDENTITY IS THE WRONG TEST.  The corpus shows the player's projectiles
// split across four unrelated Blueprint roots that all derive from `Actor`
// directly (`Dagger_Projectile_Base_C`, `ArcProjectile_Base_C`,
// `GroundTravellingProjectile_Base_C`, `Spear_Projectile_C`) plus ~12 that derive
// from nothing shared at all (`Default_Gun_Projectile_C`, `ShotGun_Projectile_BP_C`,
// `SniperGun_Projectile_C`, `LuudeCrossbow_Projectile_C`, `RocketLauncher_Projectile_C`,
// ...).  What they DO share is how the engine moves them and what the game calls
// them.
// ---------------------------------------------------------------------------
inline bool LooksLikeProjectile(bool hasProjectileMovement, const char* className)
{
    if (hasProjectileMovement) return true;
    if (!className || !className[0]) return false;
    return HasCI(className, "projectile") || HasCI(className, "travellingwave") ||
           HasCI(className, "travelwave") || HasCI(className, "arcprojectile") ||
           HasCI(className, "bolt") || HasCI(className, "arrow");
}

// ---------------------------------------------------------------------------
// Is this Niagara system the IMPACT of something?  Read from the asset name the
// game gave it.  Measured across Content/Items/Weapons: `*_Impact`,
// `*_ImpactFX`, `*_AltImpact`, `*_ProjectileImpact`, `*_BeamImpact`, `*_Explosion`,
// `*_GroundBlast`, `*_AOE`.  The trail/muzzle/charge systems in the same folder
// must NOT match, or a copy would spawn a fireball on the thrower's own hand.
// ---------------------------------------------------------------------------
inline bool LooksLikeImpactFx(const char* systemName)
{
    if (!systemName || !systemName[0]) return false;
    if (HasCI(systemName, "trail") || HasCI(systemName, "muzzle") ||
        HasCI(systemName, "charge") || HasCI(systemName, "cast") ||
        HasCI(systemName, "sigil") || HasCI(systemName, "beam_loop"))
        return false;
    return HasCI(systemName, "impact") || HasCI(systemName, "explosion") ||
           HasCI(systemName, "blast") || HasCI(systemName, "aoe") ||
           HasCI(systemName, "detonat");
}

// ---------------------------------------------------------------------------
// Which hand-held visual class roots exist.  The equipment scan accepts a
// component whose owner actor derives from one of these; `kMaxVisualRoots` is the
// fixed table size, and the last entry is the generic fallback used when neither
// root exists in a build ("<something>Visuals_C").
// ---------------------------------------------------------------------------
static const int kMaxVisualRoots = 4;
static const char* const kVisualRootNames[kMaxVisualRoots] = {
    "WeaponVisuals_C",        // every weapon: swords, hammers, lances, guns ...
    "Dagger_Visuals_Base_C",  // every dagger / thrown / off-hand visual
    "InHandVisuals_C",        // the few "*_InHandVisuals_C" one-offs
    "",                       // "" = the name-substring fallback, see VisualNameLooksRight
};

// The fallback rule: the game names every hand item's visual class "<Item>_Visuals"
// or "<Item>_InHandVisuals".  Accepting the WORD is what makes the scan generic
// instead of a growing list of roots.
inline bool VisualNameLooksRight(const char* className)
{
    if (!className || !className[0]) return false;
    if (!HasCI(className, "visuals") && !HasCI(className, "visual")) return false;
    // Not a hand item: pickups, inspect-only props and the player's own body.
    if (HasCI(className, "pickup") || HasCI(className, "inspect")) return false;
    return true;
}

} // namespace cxf
} // namespace qd
