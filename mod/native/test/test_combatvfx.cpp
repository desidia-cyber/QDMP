// test_combatvfx.cpp -- the three rules qdcombatvfx.h decides, pinned to the game's
// own asset names.
//
// WHY THESE TESTS AND NOT A LIVE RUN
// ----------------------------------
// Every rule here was wrong in a way a live run cannot show as a failure: a partner
// holding a hammer swung it like a sword (a plausible animation for the wrong
// weapon), a partner's thrown dagger simply stopped in mid-air (the copy cannot
// collide, so there was nothing to look at), a gun fired nothing visible on the other
// screen, and a partner holding only a dagger held nothing at all.  Each is a string
// comparison, and the strings are the game's: they are quoted here from
// analysis/data/asset-classes.json, which lists all 1,000-odd names under
// Content/Items/Weapons.  A live run can only prove the ones you happened to equip;
// this proves the ones you did not.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "qdcombatvfx.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, name)                                                          \
    do {                                                                           \
        if (cond) { ++g_pass; }                                                    \
        else { ++g_fail; printf("FAIL %s  (%s:%d)\n", name, __FILE__, __LINE__); } \
    } while (0)

using namespace qd::cxf;

// A helper rather than a macro: /W4 warns C4127 on a constant conditional, and a
// suppressed warning is where a wrong constant hides.
static void CheckEq(int got, int want, const char* name)
{
    if (got == want) { ++g_pass; return; }
    ++g_fail;
    printf("FAIL %s  (got %d, want %d)\n", name, got, want);
}

// ---------------------------------------------------------------------------
// 0. The kind numbers are the wire's.  `PLAYER.action` carries the kind in 5 bits
//    (WIRE.md 3.5) and the values below are `avatar::ActKind`, which is the type the
//    sender and the body share.  Duplicating them here is the point: a kind inserted
//    in the middle of either enum breaks this test instead of silently re-labelling
//    every move on the partner's screen.
// ---------------------------------------------------------------------------
static void TestKindValues()
{
    CheckEq(K_NONE, 0, "K_NONE is 0");
    CheckEq(K_ATTACK, 1, "K_ATTACK is 1");
    CheckEq(K_HURT, 2, "K_HURT is 2");
    CheckEq(K_DYING, 3, "K_DYING is 3");
    CheckEq(K_DEAD, 4, "K_DEAD is 4");
    CheckEq(K_SLASH_R, 5, "K_SLASH_R is 5");
    CheckEq(K_SLASH_L, 6, "K_SLASH_L is 6");
    CheckEq(K_SLASH_DOWN, 7, "K_SLASH_DOWN is 7");
    CheckEq(K_THRUST, 8, "K_THRUST is 8");
    CheckEq(K_CHARGE, 9, "K_CHARGE is 9");
    CheckEq(K_THROW, 10, "K_THROW is 10");
    CheckEq(K_USE, 11, "K_USE is 11");
    CheckEq(K_SWAP, 12, "K_SWAP is 12");
    CheckEq(K_CAST, 13, "K_CAST is 13");
    CheckEq(K_CHARGE_THRUST, 14, "K_CHARGE_THRUST is 14");
    CheckEq(K_SHOOT, 15, "K_SHOOT is 15");
    CheckEq(K_SPIN, 16, "K_SPIN is 16");
    CheckEq(K_FLURRY, 17, "K_FLURRY is 17");
    CheckEq(kNotAMove, -1, "kNotAMove is -1");
    // The action byte keeps the kind in 5 bits: every kind must fit.
    CheckEq(K_FLURRY <= 0x1F ? 1 : 0, 1, "every kind fits the 5-bit action field");
}

// ---------------------------------------------------------------------------
// 1. Weapon shapes, read out of the asset name the game chose.
// ---------------------------------------------------------------------------
static void TestShapeOfName()
{
    CHECK(ShapeOfName("/Game/Items/Weapons/CyberHammer/CyberHammer_WeaponVisuals") == SH_HAMMER,
          "CyberHammer is a hammer");
    CHECK(ShapeOfName("CrushingLance_WeaponVisuals") == SH_LANCE, "CrushingLance is a lance");
    CHECK(ShapeOfName("BasicSpear_WeaponVisuals") == SH_SPEAR, "BasicSpear is a spear");
    CHECK(ShapeOfName("GreatSpear_WeaponVisuals") == SH_SPEAR, "GreatSpear is a spear");
    CHECK(ShapeOfName("BloodyClaw_WeaponVisuals") == SH_CLAW, "BloodyClaw is a claw");
    CHECK(ShapeOfName("FinalClaw_WeaponVisuals") == SH_CLAW, "FinalClaw is a claw");
    CHECK(ShapeOfName("RayneScythe_WeaponVisuals") == SH_SCYTHE, "RayneScythe is a scythe");
    CHECK(ShapeOfName("BirdFlail_WeaponVisuals") == SH_FLAIL, "BirdFlail is a flail");
    CHECK(ShapeOfName("LuudeCrossbow_WeaponVisuals") == SH_BOW, "LuudeCrossbow is a bow arm");
    CHECK(ShapeOfName("Shotgun_WeaponVisuals") == SH_GUN, "Shotgun is a gun");
    CHECK(ShapeOfName("SniperRifle_WeaponVisuals") == SH_GUN, "SniperRifle is a gun");
    CHECK(ShapeOfName("RocketLauncher_WeaponVisuals") == SH_GUN, "RocketLauncher is a gun");
    CHECK(ShapeOfName("Torch_WeaponVisuals") == SH_TORCH, "Torch is a light");
    CHECK(ShapeOfName("HarkLamp_WeaponVisuals") == SH_TORCH, "HarkLamp is a light");
    CHECK(ShapeOfName("ParryShield_WeaponVisuals") == SH_SHIELD, "ParryShield is a shield");
    CHECK(ShapeOfName("HagDagger_WeaponVisuals") == SH_DAGGER, "HagDagger is a dagger");
    CHECK(ShapeOfName("VampireKnife_WeaponVisuals") == SH_DAGGER, "VampireKnife is a dagger");
    CHECK(ShapeOfName("Katar_WeaponVisuals") == SH_KATAR, "Katar is a katar");
    CHECK(ShapeOfName("Cestus_WeaponVisuals") == SH_CESTUS, "Cestus is a cestus");
    CHECK(ShapeOfName("Rapier_WeaponVisuals") == SH_RAPIER, "Rapier is a rapier");
    CHECK(ShapeOfName("RustySword_WeaponVisuals") == SH_SWORD, "RustySword is a sword");
    CHECK(ShapeOfName("") == SH_UNKNOWN, "an empty name has no shape");
    CHECK(ShapeOfName("Player_FPArms_Std_Charge_Attack_Montage") == SH_UNKNOWN,
          "a generic montage names no weapon");

    // MOST SPECIFIC FIRST: "greatsword" must not be read as "sword", "longhammer"
    // must not be read as a one-handed hammer, "crossbow" must not be a "bow".
    CHECK(ShapeOfName("WodasoGreatSword_WeaponVisuals") == SH_GREATSWORD,
          "a greatsword is not a sword");
    CHECK(ShapeOfName("Rusted_GreatAxe_Item") == SH_AXE, "a great axe is an axe");
    CHECK(ShapeOfName("Sipeha_LongHammer_WeaponVisuals") == SH_HAMMER,
          "a long hammer is still a hammer");
}

static void TestShapeTwoHanded()
{
    CHECK(ShapeTwoHanded(SH_GREATSWORD, "WodasoGreatSword"), "a greatsword takes both hands");
    CHECK(ShapeTwoHanded(SH_LANCE, "CrushingLance"), "a lance takes both hands");
    CHECK(ShapeTwoHanded(SH_SPEAR, "BasicSpear"), "a spear takes both hands");
    CHECK(ShapeTwoHanded(SH_SCYTHE, "RayneScythe"), "a scythe takes both hands");
    CHECK(ShapeTwoHanded(SH_FLAIL, "BirdFlail"), "a flail takes both hands");
    CHECK(ShapeTwoHanded(SH_HAMMER, "Sipeha_LongHammer"), "a LONG hammer takes both hands");
    CHECK(!ShapeTwoHanded(SH_HAMMER, "CyberHammer"), "a plain hammer does not");
    CHECK(!ShapeTwoHanded(SH_SWORD, "RustySword"), "a sword does not");
    CHECK(!ShapeTwoHanded(SH_DAGGER, "HagDagger"), "a dagger does not");
    CHECK(!ShapeTwoHanded(SH_GUN, "Shotgun"), "a gun does not");
}

// ---------------------------------------------------------------------------
// 2. The classifier, against the montage names the game actually ships.  The
//    "before" column is what the old English-keyword rule did -- every entry
//    marked WRONG was a weapon the partner saw swing like a sword or not at all.
// ---------------------------------------------------------------------------
struct MontageCase { const char* name; int want; };
static const MontageCase kMontages[] = {
    // --- the generic set (must not regress) --------------------------------
    {"Player_FPArms_Std_Charge_Attack_Montage", K_CHARGE},
    {"Player_Slash_Right_Montage",      kNotAMove},   // follow-through, not a move
    {"Player_FPArms_LeftHand_MeleeResponse_Montage", kNotAMove},
    {"Player_FPArms_Base_Jump_Action_Left_Montage",  kNotAMove},
    {"Player_FPArms_Death_Montage",     K_DYING},
    {"Player_SwapRightWeapon",          K_SWAP},
    {"Player_FPArms_Base_Gun_Reload_Montage", K_SWAP},
    // --- HAMMERS / MACES / CLUBS: the alt is an overhead slam ---------------
    {"Player_FPArms_Hammer_AltAttack_Montage",      K_SLASH_DOWN},   // was K_SLASH_R
    {"Player_FPArms_CyberHammer_AltAttack_Montage", K_SLASH_DOWN},   // was K_SLASH_R
    {"Player_FPArms_KingsHammer_AltAttack_Montage", K_SLASH_DOWN},   // was K_SLASH_R
    {"Player_FPArms_Hammer_Slam",                   K_SLASH_DOWN},
    {"Player_FPArms_Hammer_Attack_Montage",         K_SLASH_R},      // its normal swing
    {"Player_FPArms_SpikedMace_Alt_Montage",        K_SLASH_DOWN},   // was K_SLASH_R
    {"Player_FPArms_WoodClub_AltAttack_Montage",    K_SLASH_DOWN},   // was K_SLASH_R
    {"DustyClub_AltAttack",                         K_SLASH_DOWN},
    {"SimpleAxe_AltAttack",                         K_SLASH_DOWN},
    {"VirtualBat_AltAttack",                        K_SLASH_DOWN},
    {"DiamondBlade_DownSmackMontage",               K_SLASH_DOWN},
    {"Player_FPArms_DuckHammer_AltAttack",          K_SLASH_DOWN},
    {"Player_FPArms_VampireHammer_AltAttack_Montage", K_SLASH_DOWN},
    // --- CLAWS / KATARS / CESTUS: multi-hit sets --------------------------
    {"Player_FPArms_Claw_AltAttack_Montage",        K_SLASH_R},      // was a sword swing
    {"Player_ClawSlash_LeftDown_Montage",           K_SLASH_DOWN},
    {"Player_ClawSlash_RightDown_Montage",          K_SLASH_DOWN},
    {"Player_Cestus_Attack1_Montage",               K_FLURRY},       // was K_SLASH_R
    {"Player_Katar_Attack1_Montage",                K_FLURRY},       // was K_SLASH_R
    {"Player_FPArms_Katar_Attack_Special",          K_THRUST},       // a punch-dagger lunge
    {"Player_FPArms_Katar_Charge_Attack_Montage",   K_CHARGE_THRUST},// a held stab
    // --- LANCES / SPEARS: everything is a thrust --------------------------
    {"Player_LanceAttack_1_Montage",                K_THRUST},       // was K_SLASH_R
    {"Player_LanceAttack_2_Montage",                K_THRUST},       // was K_SLASH_R
    {"Player_FPArms_CrushingLance_Alt_Montage",     K_THRUST},       // was NOTHING
    {"Player_Lance_Charge_Montage",                 K_CHARGE_THRUST},
    {"Player_FPArms_Spear_AltAttack_Montage",       K_THRUST},
    {"GreatSpear_AltAttack_Montage",                K_THRUST},
    {"KingSpear_AltAttack_Montage",                 K_THRUST},
    {"BasicSpearEvolved_AltAttack",                 K_THRUST},
    {"Player_OWS_PolearmStab_2_Montage",            K_THRUST},
    {"Player_FPArms_EnergySpear_Stab",              K_THRUST},
    {"Player_FPArms_GhostSpear_Special",            K_THRUST},
    {"Player_FPArms_PoisonPiercer_SpecialAttack",   K_THRUST},
    // --- SWORD-FAMILY ALTS: lunges ----------------------------------------
    {"Player_Greatsword-Alt_Montage",               K_SLASH_DOWN},   // was NOTHING
    {"Player_FPArms_RustedGreatsword_Alt",          K_SLASH_DOWN},   // was NOTHING
    {"Player_FPArms_Zweihander_Alt",                K_SLASH_DOWN},   // was NOTHING
    {"Player_FPArms_WodasoGS_Alt",                  K_SLASH_DOWN},   // was NOTHING
    {"Player_FPArms_CyberSword_Alt",                K_THRUST},       // was NOTHING
    {"Player_FPArms_EvolvedSword_Alt",              K_THRUST},       // was NOTHING
    {"Player_FPArms_SaoSword_Alt",                  K_THRUST},       // was NOTHING
    {"Player_FPArms_LeafBlade_AltAttack",           K_THRUST},
    {"Player_FPArms_SpectralSword_AltAttack",       K_THRUST},
    {"Player_FPArms_EvolvedSword_DownSlash",        K_SLASH_DOWN},
    {"B0Blade_Dash_Montage",                        K_THRUST},
    {"RustySwordDash",                              K_THRUST},
    // A "magic"-named SWORD was read as a spell cast by the old rule.
    {"Player_FPArms_MagicStoneSword_Alt",           K_THRUST},
    // --- RAPIER: the alt is the four-stab flurry ---------------------------
    {"Player_FPArms_Rapier_AltAttack_Charge_Montage", K_CHARGE_THRUST},
    {"Player_FPArms_Rapier_AltAttack_Startup_Montage", K_THRUST},
    {"RapierAlt_Stab_1_Montage",                    K_THRUST},
    {"RapierAlt_Stab_4_Montage",                    K_THRUST},
    // --- SCYTHES / FLAILS: a sweep ---------------------------------------
    {"Player_FPArmsRayneScythe_DashLeft_Montage",   K_THRUST},
    {"TiniScythe_ChargedScythe",                    K_SPIN},
    {"TiniScythe_ChargedHammerAttack",              K_SLASH_DOWN},
    {"TiniScythe_HammerToScythe_Montage",           K_SWAP},
    {"Player_FPArms_ScytheCatch",                   kNotAMove},
    // --- THROWS -----------------------------------------------------------
    {"Player_FPArms_Attack_Scythe_Throw",           K_THROW},
    {"Warhammer_Throw_Attack-Montage",              K_THROW},
    {"Player_FPArms_Warhammer_Throw",               K_THROW},
    {"Player_FPArms_ScytheThrow",                   K_THROW},
    // --- RANGED: the moves that had no animation at all -------------------
    {"Shotgun_Fire_Montage",                        K_SHOOT},        // was NOTHING
    {"Shotgun_AltFire_Montage",                     K_SHOOT},        // was NOTHING
    {"HeavyRifle_Fire",                             K_SHOOT},        // was NOTHING
    {"Player_FPArms_HeavyRifle_Fire",               K_SHOOT},        // was NOTHING
    {"PlasmaRifle_Fire_Montage",                    K_SHOOT},        // was NOTHING
    {"PlasmaRifle_AltFire_Montage",                 K_SHOOT},        // was NOTHING
    {"HempieSVT38_Fire_Montage",                    K_SHOOT},        // was NOTHING
    {"LuudeCrossbow_Fire_Montage",                  K_SHOOT},        // was NOTHING
    {"LuudeCrossbow_Charge_Montage",                K_CHARGE},
    {"SniperGun_LaserFire_Montage",                 K_SHOOT},        // was NOTHING
    {"Player_FPArms_SniperGun-AltFire_Montage",     K_SHOOT},        // was NOTHING
    {"WindSlashGun_PrimaryFire",                    K_SHOOT},        // was NOTHING
    {"SipehaLongHammer_Shoot_Montage",              K_SHOOT},        // was NOTHING
    {"EyePeaSyringe_FireNeedle_Montage",            K_SHOOT},        // was NOTHING
    {"Player_FPArms_RocketLauncher_Melee_Montage",  K_SLASH_R},
    // --- items and reactions ---------------------------------------------
    {"Player_FPArms_ApplyAcid",                     K_USE},
    {"Player_FPArms_DrinkPotion",                   K_USE},
    {"Player_FPArms_Hurt_Montage",                  K_HURT},
    // THE "eat" TRAP.  As a substring, "eat" matches Gr-eat-spear, Lanc-eAt-tack and
    // Gr-eat-sword: all three used to play the drink-a-potion pose.  They are weapon
    // attacks and must classify as weapon attacks.
    {"GreatSpear_AltAttack_Montage",                K_THRUST},
    {"Player_LanceAttack_1_Montage",                K_THRUST},
    {"Player_Greatsword-Alt_Montage",               K_SLASH_DOWN},
};

static void TestClassifyMontage()
{
    const int n = (int)(sizeof(kMontages) / sizeof(kMontages[0]));
    for (int i = 0; i < n; ++i) {
        int got = ClassifyMontage(kMontages[i].name);
        if (got != kMontages[i].want) {
            printf("FAIL montage %s -> %d, want %d\n", kMontages[i].name, got, kMontages[i].want);
            ++g_fail;
        } else {
            ++g_pass;
        }
    }
}

// The held item is the fallback when the montage name says nothing about a weapon.
static void TestHeldFallback()
{
    // A name with no weapon in it at all, but a hammer in hand: the alt is a slam.
    CHECK(ClassifyMontage("Player_FPArms_Alt_Attack", "CyberHammer_WeaponVisuals") == K_SLASH_DOWN,
          "an unnamed alt with a hammer in hand is a slam");
    CHECK(ClassifyMontage("Player_FPArms_Alt_Attack", "CrushingLance_WeaponVisuals") == K_THRUST,
          "an unnamed alt with a lance in hand is a thrust");
    // A montage that NAMES its weapon outranks a stale held item.
    CHECK(ClassifyMontage("Player_LanceAttack_1_Montage", "CyberHammer_WeaponVisuals") == K_THRUST,
          "the montage's own weapon wins over the held item");
    // Nothing to go on: the honest default is a swing, never "no animation".
    CHECK(ClassifyMontage("Something_Alt_Attack", "") == K_SLASH_R,
          "an unknown alt is still a swing");
    CHECK(ClassifyMontage("", "") == K_NONE, "an empty name is no move");
}

// ---------------------------------------------------------------------------
// 3. Which spawned thing is one of the player's projectiles.
// ---------------------------------------------------------------------------
static void TestLooksLikeProjectile()
{
    // Every player projectile class in the corpus, by the name the game gave it.
    static const char* const kPlayerProjectiles[] = {
        "Dagger_Projectile_Base_C", "DefaultDagger_Projectile_BP_C",
        "DiseaseDagger_Projectile_BP_C", "EnergyDagger_Projectile_BP_C",
        "Dagger_Explode_Projectile_C", "Fireball_Projectile_BP_C",
        "FreezeDagger_Projectile_BP_C", "Dagger_Homing_Projectile_C",
        "KingsDagger_Projectile_BP_C", "MagicFireDagger_Projectile_BP_C",
        "Needle_Projectile_BP_C", "Dagger_Paralyze_Projectile_C",
        "Dagger_Poison_Projectile_C", "SpikeBall_Projectile_BP_C",
        "StrangeStoneBlock_Projectile_BP_C", "SuperCube_Projectile_BP_C",
        "Dagger_Tech_Projectile_C", "ThrowingAxe_Projectile_BP_C",
        "ThrowingDuckDagger_Projectile_BP_C", "Dagger_Vampire_Projectile_C",
        "Vineshroom_Projectile_BP_C", "WeakenDagger_Projectile_BP_C",
        "ByrdSword_Projectile_C", "BigNut_Projectile_BP_C",
        "Chakram_Projectile_BP_C", "ElecGun_Projectile_C",
        "ElecGun_Alt_Projectile_C", "HeavyRifle_Projectile_C",
        "HeavyRifle_AltFire_Projectile_C", "HempieSVT38_Projectile_C",
        "LuudeCrossbow_Projectile_C", "PlasmaRifle_AltFireProjectile_C",
        "RocketLauncher_Projectile_C", "ShotGun_Projectile_BP_C",
        "Shotgun_AltFireProjectile_BP_C", "SniperGun_Projectile_C",
        "SlashGun_Projectile_BP_C", "SlashGun_AltFireProjectile_C",
        "SipehaLongHammer_Projectile_C", "SkullRod_Projectile_C",
        "Torch_Projectile_C", "GreatSpear_Projectile_C", "Scythe_Projectile_C",
        "Spear_Projectile_C", "Warhammer_Projectile_C",
        "GolemCrusher_Projectile_C", "PebbleHammer_Projectile_C",
        "ArcProjectile_Base_C", "SaoSword_ArcProjectile_C",
        "HiddenSword_ArcProjectile_C", "ForestFlameAxe_Projectile_C",
        "GroundTravellingProjectile_Base_C", "PoisonPiercer_TravellingWave_C",
        "CyberHammer_TravellingWave_C", "GhostSpear_TravelWave_C",
        "OtherWorldSpear_TravellingWave_C", "Wodaso_GS_Projectile_C",
        "Default_Gun_Projectile_C", "EyePeaSyringProjectile_C",
    };
    int n = (int)(sizeof(kPlayerProjectiles) / sizeof(kPlayerProjectiles[0]));
    for (int i = 0; i < n; ++i)
        CHECK(LooksLikeProjectile(false, kPlayerProjectiles[i]), kPlayerProjectiles[i]);

    // A projectile that carries the component is accepted whatever it is called.
    CHECK(LooksLikeProjectile(true, "Actor"), "a ProjectileMovement component is enough");

    // Things that must NOT be copied: a world prop, the player, a status effect.
    CHECK(!LooksLikeProjectile(false, "Actor"), "a bare Actor is not a projectile");
    CHECK(!LooksLikeProjectile(false, "StaticMeshActor"), "a prop is not a projectile");
    CHECK(!LooksLikeProjectile(false, "FS_PlayerChar_C"), "the player is not a projectile");
    CHECK(!LooksLikeProjectile(false, "Poison_CharFX"), "a character effect is not a projectile");
    CHECK(!LooksLikeProjectile(false, ""), "no name, no projectile");
}

// ---------------------------------------------------------------------------
// 4. Which system on a projectile is the IMPACT.
// ---------------------------------------------------------------------------
static void TestLooksLikeImpactFx()
{
    // The impact systems that exist under Content/Items/Weapons.
    static const char* const kImpacts[] = {
        "BigNut_Impact", "EnergyDagger_ImpactFX", "Explode_Dagger_Explosion",
        "Fireball_Impact", "Default_Dagger_ImpactVFX", "StrangeStoneBlock_Impact",
        "ElecGun_ProjectileImpact", "ElecGun_Alt_ProjectileImpact",
        "HeavyRifle_Projectile_Impact", "HempieSVT38_Projectile_Impact",
        "PlasmaRifle_Projectile_Impact", "PlasmaRifle_AltFire_Impact",
        "Shotgun_ProjectileImpact", "Shotgun_AltFire_Explosion",
        "SlashGun_ImpactFX", "SlashGun_AltFireExplosion",
        "SniperRifle_ProjectileImpact", "SniperRifle_BeamImpact",
        "Torch_ImpactFX", "Torch_Fireball_Impact", "Evolved_Spear_AltImpact",
        "B0Blade_AltImpact", "OtherWorldSpear_Impact_FX", "CyberHammer_GroundBlast",
        "EnergyBallHammer_Explosion", "GeometrizerBatExplosion", "SuperCube_AOE",
    };
    int n = (int)(sizeof(kImpacts) / sizeof(kImpacts[0]));
    for (int i = 0; i < n; ++i) CHECK(LooksLikeImpactFx(kImpacts[i]), kImpacts[i]);

    // The systems in the same folders that are NOT impacts: picking one of these
    // would spawn a fireball on the thrower's hand or a trail at the hit point.
    static const char* const kNotImpacts[] = {
        "Dagger_Projectile_Trail", "HeavyRifle_Projectile_MuzzleFLash",
        "CyberSword_AltCharge", "Fireball_InHandFX", "Claw_Charge_VFX",
        "Jigeki_SpellSigil_Charge", "PlasmaRifle_Idle",
    };
    n = (int)(sizeof(kNotImpacts) / sizeof(kNotImpacts[0]));
    for (int i = 0; i < n; ++i) CHECK(!LooksLikeImpactFx(kNotImpacts[i]), kNotImpacts[i]);

    CHECK(!LooksLikeImpactFx(""), "no name, no impact");
}

// ---------------------------------------------------------------------------
// 5. Which hand-held VISUAL class belongs to a hand item.
// ---------------------------------------------------------------------------
static void TestVisualRoots()
{
    // The two unrelated roots the scan looks up.
    CHECK(strcmp(kVisualRootNames[0], "WeaponVisuals_C") == 0, "root 0 is the weapon root");
    CHECK(strcmp(kVisualRootNames[1], "Dagger_Visuals_Base_C") == 0, "root 1 is the dagger root");
    CHECK(kVisualRootNames[kMaxVisualRoots - 1][0] == 0, "the last root slot is the name fallback");

    // The name fallback: every hand item's visual is "<Item>_Visuals" or
    // "<Item>_InHandVisuals".
    static const char* const kHand[] = {
        "BigNut_Visuals_C", "Chakram_InHandVisuals_C", "Default_Dagger_Visuals_C",
        "Dagger_Explode_Visuals_InHand_C", "Needle_Visuals_InHand_C",
        "ThrowingDuck_Dagger_InHandVisuals_C", "CyberHammer_WeaponVisuals_C",
        "LuudeCrossbow_WeaponVisuals_C",
    };
    int n = (int)(sizeof(kHand) / sizeof(kHand[0]));
    for (int i = 0; i < n; ++i) CHECK(VisualNameLooksRight(kHand[i]), kHand[i]);

    // Not hand items: a world pickup, an inspect-only prop, the player itself.
    CHECK(!VisualNameLooksRight("BigNut_Pickup_C"), "a pickup is not in a hand");
    CHECK(!VisualNameLooksRight("Energy_Dagger_Inspect_Visuals_C"),
          "an inspect-only visual is not in a hand");
    CHECK(!VisualNameLooksRight("FS_PlayerChar_C"), "the player is not a hand item");
    CHECK(!VisualNameLooksRight(""), "no name, not a hand item");
}

// ---------------------------------------------------------------------------
// 6. Wind-up versus release: the body HOLDS a wind-up and plays the release.
// ---------------------------------------------------------------------------
static void TestReleaseMove()
{
    CHECK(IsWindUp("Player_FPArms_Std_Charge_Attack_Montage"), "std charge attack is a wind-up");
    CHECK(!IsWindUp("TiniScythe_ChargedHammerAttack"), "Charged... is the strike, not a wind-up");
    CHECK(ReleaseMove("Player_FPArms_Std_Charge_Attack_Montage", SH_SWORD) == K_ATTACK,
          "the generic wind-up releases a swing");
    CHECK(ReleaseMove("Player_Lance_Charge_Montage", SH_LANCE) == K_THRUST,
          "a lance charge releases a thrust");
    CHECK(ReleaseMove("Player_FPArms_Hammer_AltAttack_Charge", SH_HAMMER) == K_SLASH_DOWN,
          "a hammer charge releases a slam");
    CHECK(ReleaseMove("Player_Slash_Right_Montage", SH_SWORD) == K_NONE,
          "a plain swing names no release");
}

int main()
{
    TestKindValues();
    TestShapeOfName();
    TestShapeTwoHanded();
    TestClassifyMontage();
    TestHeldFallback();
    TestLooksLikeProjectile();
    TestLooksLikeImpactFx();
    TestVisualRoots();
    TestReleaseMove();

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
