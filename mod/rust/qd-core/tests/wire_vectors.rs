//! Golden wire vectors.
//!
//! Two jobs, and they check each other:
//!
//! 1. Assert the exact bytes of one instance of every body against the encodings
//!    hand-derived from `mod/doc/WIRE.md`. The expected hex below was produced by a
//!    separate implementation of the field layout (a short script reading the
//!    document), not by this encoder — otherwise the test would only prove the
//!    encoder is self-consistent.
//! 2. Write the same bytes to `mod/doc/vectors.txt`, which the native self-test
//!    consumes. A drift between `qd-core` and `native/src/qdwire.h` therefore
//!    fails a test instead of corrupting a session.
//!
//! The file is byte-stable: vectors are in a fixed order, and every list body is
//! built through a `BTreeMap` so entry order is sorted by key rather than by
//! insertion (`flag_lists_are_sorted_by_construction` proves it).

use std::collections::BTreeMap;
use std::path::PathBuf;

use qd_core::flags;
use qd_core::protocol::*;

struct Vector {
    name: &'static str,
    body: Body,
    /// `<kind byte> <body bytes>`, lowercase hex, space separated.
    expected: &'static str,
}

fn sample_guid() -> Guid {
    Guid::from_bytes([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15])
}

fn second_guid() -> Guid {
    Guid::from_bytes([16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31])
}

fn flag_entries(pairs: &[(&str, i32)]) -> Vec<FlagEntry> {
    let map: BTreeMap<&str, i32> = pairs.iter().copied().collect();
    map.into_iter().map(|(name, value)| FlagEntry { name: name.to_owned(), value }).collect()
}

fn item_entries(pairs: &[(&str, i32)]) -> Vec<ItemEntry> {
    let map: BTreeMap<&str, i32> = pairs.iter().copied().collect();
    map.into_iter().map(|(item_path, count)| ItemEntry { item_path: item_path.to_owned(), count })
        .collect()
}

fn vectors() -> Vec<Vector> {
    vec![
        Vector {
            name: "hello",
            body: Body::Hello(Hello {
                proto: 1,
                build_hash: 0x1122_3344,
                role_wanted: roles::PEER,
                save_level: "/Game/Maps/Coop".to_owned(),
                display_name: "Player2".to_owned(),
            }),
            expected: "01 01 44 33 22 11 02 0f 00 2f 47 61 6d 65 2f 4d 61 70 73 2f 43 6f 6f 70 07 00 50 6c 61 79 65 72 32",
        },
        Vector {
            name: "welcome",
            body: Body::Welcome(Welcome {
                role_assigned: roles::HOST,
                session: 7,
                host_tick: 123_456,
                save_level: "/Game/Maps/Coop".to_owned(),
                flags_rev: 42,
            }),
            expected: "02 01 07 00 00 00 40 e2 01 00 0f 00 2f 47 61 6d 65 2f 4d 61 70 73 2f 43 6f 6f 70 2a 00 00 00",
        },
        Vector { name: "keepalive", body: Body::Keepalive, expected: "03" },
        Vector {
            name: "ack",
            body: Body::Ack(Ack { seq: 0x0000_0102 }),
            expected: "04 02 01 00 00",
        },
        Vector {
            name: "player_row",
            body: Body::PlayerRow(PlayerRow {
                slot: slots::PEER,
                sample_ms: 1500,
                x: 1000,
                y: -2000,
                z: 300,
                yaw: 8192,
                pitch: -4096,
                vx: 100,
                vy: -100,
                vz: 0,
                move_mode: 3,
                action: player_action::ATTACK,
                flags: player_flags::GROUNDED | player_flags::SPRINTING,
            }),
            expected: "10 01 dc 05 00 00 e8 03 00 00 30 f8 ff ff 2c 01 00 00 00 20 00 f0 64 00 9c ff 00 00 03 01 05",
        },
        Vector {
            name: "world_flags",
            body: Body::WorldFlags(WorldFlags {
                rev: 5,
                entries: flag_entries(&[("CheckpointReached", 7), ("DoorA", 1)]),
            }),
            expected: "11 05 00 00 00 02 00 11 00 43 68 65 63 6b 70 6f 69 6e 74 52 65 61 63 68 65 64 07 00 00 00 05 00 44 6f 6f 72 41 01 00 00 00",
        },
        Vector {
            name: "actor_states",
            body: Body::ActorStates(ActorStates {
                rev: 9,
                entries: vec![ActorEntry { actor: sample_guid(), value: -1 }],
            }),
            expected: "12 09 00 00 00 01 00 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f ff ff ff ff",
        },
        Vector {
            name: "inventory",
            body: Body::Inventory(Inventory {
                rev: 3,
                entries: item_entries(&[
                    ("/Game/Items/KeyItems/AquaDoorKey_2.AquaDoorKey_2", 1),
                    ("/Game/Items/Tools/Lamp.Lamp", 4),
                ]),
            }),
            expected: "13 03 00 00 00 02 00 30 00 2f 47 61 6d 65 2f 49 74 65 6d 73 2f 4b 65 79 49 74 65 6d 73 2f 41 71 75 61 44 6f 6f 72 4b 65 79 5f 32 2e 41 71 75 61 44 6f 6f 72 4b 65 79 5f 32 01 00 00 00 1b 00 2f 47 61 6d 65 2f 49 74 65 6d 73 2f 54 6f 6f 6c 73 2f 4c 61 6d 70 2e 4c 61 6d 70 04 00 00 00",
        },
        Vector {
            name: "enemy_rows",
            body: Body::EnemyRows(EnemyRows {
                rows: vec![
                    EnemyRow {
                        id: sample_guid(),
                        x: 100,
                        y: -200,
                        z: 50,
                        yaw: 16384,
                        hp: 80,
                        flags: enemy_flags::ALIVE | enemy_flags::DRIVEN | enemy_flags::HAS_BODY,
                    },
                    EnemyRow {
                        id: second_guid(),
                        x: -300,
                        y: 400,
                        z: -25,
                        yaw: -16384,
                        hp: 0,
                        flags: enemy_flags::DRIVEN,
                    },
                ],
            }),
            expected: "14 02 00 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f 64 00 00 00 38 ff ff ff 32 00 00 00 00 40 50 00 07 10 11 12 13 14 15 16 17 18 19 1a 1b 1c 1d 1e 1f d4 fe ff ff 90 01 00 00 e7 ff ff ff 00 c0 00 00 02",
        },
        Vector {
            name: "enemy_detail",
            body: Body::EnemyDetail(EnemyDetail {
                id: sample_guid(),
                anim: enemy_anim::ATTACK,
                seq: 7,
                status: 0x0005,
                boss: 0,
                montage_path: "/Game/Anim/Enemy/Slash_Montage".to_owned(),
            }),
            expected: "19 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f 01 07 05 00 00 1e 00 2f 47 61 6d 65 2f 41 6e 69 6d 2f 45 6e 65 6d 79 2f 53 6c 61 73 68 5f 4d 6f 6e 74 61 67 65",
        },
        Vector {
            name: "boss",
            body: Body::Boss(Boss {
                id: sample_guid(),
                flags: boss_flags::BAR_ON,
                phase: 2,
                hp: 40_000,
            }),
            expected: "29 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f 01 02 40 9c",
        },
        Vector {
            name: "objects",
            body: Body::Objects(Objects {
                rev: 7,
                entries: vec![ObjectRow {
                    key: sample_guid(),
                    shape: object_shape::CHEST,
                    state: 1,
                }],
            }),
            expected: "2a 07 00 00 00 01 00 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f 01 01",
        },
        Vector {
            // guid, flags, pos(3 x i32), yaw, hp fraction, class path.
            name: "enemy_spawn",
            body: Body::EnemySpawn(EnemySpawn {
                id: sample_guid(),
                flags: enemy_spawn_flags::MIRROR,
                px: 100,
                py: -200,
                pz: 50,
                yaw: 16384,
                hp: 40000,
                class_path: "/Game/Enemies/Dragon/Dragon_Green_BP.Dragon_Green_BP_C".to_owned(),
            }),
            expected: "2b 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f 01 64 00 00 00 38 ff ff ff 32 00 00 00 00 40 40 9c 36 00 2f 47 61 6d 65 2f 45 6e 65 6d 69 65 73 2f 44 72 61 67 6f 6e 2f 44 72 61 67 6f 6e 5f 47 72 65 65 6e 5f 42 50 2e 44 72 61 67 6f 6e 5f 47 72 65 65 6e 5f 42 50 5f 43",
        },
        Vector {
            name: "intent_set_flag",
            body: Body::Intent(Intent {
                based_on_rev: 5,
                payload: IntentPayload::SetFlag {
                    name: "DoorA".to_owned(),
                    value: 1,
                    mode: flags::MODE_ABSOLUTE,
                },
            }),
            expected: "20 01 05 00 00 00 05 00 44 6f 6f 72 41 01 00 00 00 01",
        },
        Vector {
            name: "intent_grant_item",
            body: Body::Intent(Intent {
                based_on_rev: 6,
                payload: IntentPayload::GrantItem {
                    item_path: "/Game/Items/Tools/Lamp.Lamp".to_owned(),
                    count: 2,
                },
            }),
            expected: "20 02 06 00 00 00 1b 00 2f 47 61 6d 65 2f 49 74 65 6d 73 2f 54 6f 6f 6c 73 2f 4c 61 6d 70 2e 4c 61 6d 70 02 00 00 00",
        },
        Vector {
            name: "intent_consume_item",
            body: Body::Intent(Intent {
                based_on_rev: 7,
                payload: IntentPayload::ConsumeItem {
                    item_path: "/Game/Items/KeyItems/AquaDoorKey_2.AquaDoorKey_2".to_owned(),
                    count: 1,
                },
            }),
            expected: "20 03 07 00 00 00 30 00 2f 47 61 6d 65 2f 49 74 65 6d 73 2f 4b 65 79 49 74 65 6d 73 2f 41 71 75 61 44 6f 6f 72 4b 65 79 5f 32 2e 41 71 75 61 44 6f 6f 72 4b 65 79 5f 32 01 00 00 00",
        },
        Vector {
            name: "intent_dialogue_answer",
            body: Body::Intent(Intent {
                based_on_rev: 8,
                payload: IntentPayload::DialogueAnswer {
                    npc: sample_guid(),
                    line_index: 3,
                    option_index: 1,
                },
            }),
            expected: "20 04 08 00 00 00 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f 03 00 01 00",
        },
        Vector {
            name: "intent_checkpoint",
            body: Body::Intent(Intent {
                based_on_rev: 9,
                payload: IntentPayload::Checkpoint {
                    save_point_path: "/Game/Maps/Coop:PlayerStart_1".to_owned(),
                },
            }),
            expected: "20 05 09 00 00 00 1d 00 2f 47 61 6d 65 2f 4d 61 70 73 2f 43 6f 6f 70 3a 50 6c 61 79 65 72 53 74 61 72 74 5f 31",
        },
        Vector {
            name: "intent_actor_state",
            body: Body::Intent(Intent {
                based_on_rev: 10,
                payload: IntentPayload::ActorStateSet { actor: sample_guid(), value: 42 },
            }),
            expected: "20 06 0a 00 00 00 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f 2a 00 00 00",
        },
        Vector {
            name: "intent_enemy_hit",
            body: Body::Intent(Intent {
                based_on_rev: 11,
                payload: IntentPayload::EnemyHit { spawner: sample_guid(), damage_frac: 16384 },
            }),
            expected: "20 07 0b 00 00 00 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f 00 40 00 00",
        },
        Vector {
            name: "intent_enemy_record",
            body: Body::Intent(Intent {
                based_on_rev: 12,
                payload: IntentPayload::EnemyRecord {
                    spawner: sample_guid(),
                    respawn_centi: 30000,
                    hp_centi: 0,
                },
            }),
            expected: "20 08 0c 00 00 00 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f 30 75 00 00 00 00 00 00",
        },
        Vector {
            name: "intent_object_state",
            body: Body::Intent(Intent {
                based_on_rev: 13,
                payload: IntentPayload::ObjectState {
                    key: sample_guid(),
                    shape: object_shape::CHEST,
                    state: 1,
                },
            }),
            expected: "20 09 0d 00 00 00 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f 01 01",
        },
        Vector {
            name: "dialogue_state",
            body: Body::DialogueState(DialogueState {
                seq: 4,
                npc: sample_guid(),
                slot: slots::PEER,
                line: 12,
                entry: 3,
            }),
            expected: "21 04 00 00 00 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f 01 0c 00 03 00",
        },
        Vector {
            name: "dialogue_view",
            body: Body::DialogueView(DialogueView {
                seq: 4,
                npc: sample_guid(),
                slot: slots::PEER,
                flags: dialogue_view_flags::OPTIONS | dialogue_view_flags::CHOSEN,
                sequence: 2,
                entry: 1,
                options: 3,
                chosen: 1,
                asset: "Knight_Arjaot_Dialogue_00".to_owned(),
                speaker: "Spirit of Erma, Matron of the Deep".to_owned(),
                line: "For all my scrying and divination, I did not foresee this betrayal yes."
                    .to_owned(),
            }),
            expected: "50 04 00 00 00 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f 01 03 02 00 01 00 03 00 01 00 19 00 4b 6e 69 67 68 74 5f 41 72 6a 61 6f 74 5f 44 69 61 6c 6f 67 75 65 5f 30 30 22 00 53 70 69 72 69 74 20 6f 66 20 45 72 6d 61 2c 20 4d 61 74 72 6f 6e 20 6f 66 20 74 68 65 20 44 65 65 70 47 00 46 6f 72 20 61 6c 6c 20 6d 79 20 73 63 72 79 69 6e 67 20 61 6e 64 20 64 69 76 69 6e 61 74 69 6f 6e 2c 20 49 20 64 69 64 20 6e 6f 74 20 66 6f 72 65 73 65 65 20 74 68 69 73 20 62 65 74 72 61 79 61 6c 20 79 65 73 2e",
        },
        Vector {
            name: "level_union",
            body: Body::LevelUnion(LevelUnion {
                bits: vec![0b0000_0101, 0b0000_0010],
                names: Some(vec![
                    "L_Main".to_owned(),
                    "L_Dungeon".to_owned(),
                    "L_Boss".to_owned(),
                ]),
            }),
            expected: "22 02 00 05 02 01 03 00 06 00 4c 5f 4d 61 69 6e 09 00 4c 5f 44 75 6e 67 65 6f 6e 06 00 4c 5f 42 6f 73 73",
        },
        Vector {
            name: "level_union_steady",
            body: Body::LevelUnion(LevelUnion {
                bits: vec![0b0000_0101, 0b0000_0010],
                names: None,
            }),
            expected: "22 02 00 05 02 00",
        },
        Vector {
            name: "travel",
            body: Body::Travel(Travel {
                seq: 0x123,
                role: roles::HOST,
                phase: travel_phase::NONE,
                map: "100_GR".to_owned(),
                dest: "Teleporter_500_AUT_02/100_GR".to_owned(),
            }),
            expected: "23 23 01 00 00 01 00 06 00 31 30 30 5f 47 52 1c 00 54 65 6c 65 70 6f 72 74 65 72 5f 35 30 30 5f 41 55 54 5f 30 32 2f 31 30 30 5f 47 52",
        },
        Vector {
            name: "travel_hard",
            body: Body::Travel(Travel {
                seq: 0x124,
                role: roles::HOST,
                phase: travel_phase::HARD_BEGIN,
                map: "900_SipehaWorld".to_owned(),
                dest: "/Game/RuinsLevel/Maps/900_SipehaWorld.900_SipehaWorld".to_owned(),
            }),
            expected: "23 24 01 00 00 01 01 0f 00 39 30 30 5f 53 69 70 65 68 61 57 6f 72 6c 64 35 00 2f 47 61 6d 65 2f 52 75 69 6e 73 4c 65 76 65 6c 2f 4d 61 70 73 2f 39 30 30 5f 53 69 70 65 68 61 57 6f 72 6c 64 2e 39 30 30 5f 53 69 70 65 68 61 57 6f 72 6c 64",
        },
        Vector {
            name: "damage",
            body: Body::Damage(Damage {
                attacker: sample_guid(),
                amount_centi: 3500,
                flags: damage_flags::MELEE,
            }),
            expected: "24 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f ac 0d 00 00 01",
        },
        Vector {
            name: "cutscene",
            body: Body::Cutscene(Cutscene {
                seq: 2,
                slot: slots::HOST,
                state: cutscene_state::PLAYING,
                name: "SipehaTransitionCutsceneManager".to_owned(),
            }),
            expected: "25 02 00 00 00 00 01 1f 00 53 69 70 65 68 61 54 72 61 6e 73 69 74 69 6f 6e 43 75 74 73 63 65 6e 65 4d 61 6e 61 67 65 72",
        },
        Vector {
            name: "movers",
            body: Body::Movers(Movers {
                rev: 5,
                rows: vec![MoverRow {
                    id: sample_guid(),
                    x: 100,
                    y: -200,
                    z: 50,
                    yaw: 16384,
                    pitch: -4096,
                    flags: mover_flags::MOVING | mover_flags::OWNER,
                }],
            }),
            expected: "26 05 00 00 00 01 00 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f 64 00 00 00 38 ff ff ff 32 00 00 00 00 40 00 f0 03",
        },
        Vector {
            name: "loot",
            body: Body::Loot(Loot {
                rev: 9,
                entries: vec![LootEntry {
                    source_hash: 0xDEAD_BEEF,
                    drop_index: 2,
                    taken: true,
                    flags: 0,
                    yaw: 0,
                    px: 0,
                    py: 0,
                    pz: 0,
                    item_path: String::new(),
                }],
            }),
            // kind, rev, n, hash, index, taken, flags, yaw, px, py, pz, empty path.
            expected: "27 09 00 00 00 01 00 ef be ad de 02 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00",
        },
        Vector {
            name: "suspend",
            body: Body::Suspend(Suspend {
                seq: 3,
                state: suspend_state::HOLD,
                slot: "World0_Suspend_1".to_owned(),
            }),
            expected: "28 03 00 00 00 01 10 00 57 6f 72 6c 64 30 5f 53 75 73 70 65 6e 64 5f 31",
        },
        Vector {
            name: "log",
            body: Body::Log(Log { bytes: b"hello from host".to_vec() }),
            expected: "30 68 65 6c 6c 6f 20 66 72 6f 6d 20 68 6f 73 74",
        },
        Vector {
            name: "kill_xp",
            body: Body::KillXp(KillXp { id: sample_guid(), seq: 2, xp: 250 }),
            // kind, guid, seq, xp.
            expected: "40 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f 02 00 fa 00 00 00",
        },
        Vector {
            name: "save",
            body: Body::Save(Save { seq: 7, role: save_role::PEER, route: save_route::SUSPEND }),
            // kind, seq, role, route.
            expected: "41 07 00 00 00 02 02",
        },
        Vector {
            name: "platform_ride",
            body: Body::PlatformRide(PlatformRide {
                ms: 1234,
                rows: vec![PlatformRideRow {
                    id: sample_guid(),
                    ox: 100,
                    oy: -200,
                    oz: 50,
                    oyaw: 16384,
                    flags: platform_ride_flags::ON | platform_ride_flags::MOVING,
                }],
            }),
            // kind, ms, n, guid, ox, oy, oz, oyaw, flags.
            expected: "48 d2 04 00 00 01 00 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f 64 00 00 00 38 ff ff ff 32 00 00 00 00 40 03",
        },
        Vector {
            name: "respawn_down",
            body: Body::RespawnDown(RespawnDown {
                deaths: 1,
                pct: respawn_penalty::pct_for(1),
                seq: 2,
                px: 1000,
                py: -2000,
                pz: 300,
            }),
            // kind, deaths, pct, seq, x, y, z.
            expected: "54 01 5a 02 e8 03 00 00 30 f8 ff ff 2c 01 00 00",
        },
        Vector {
            name: "respawn_up",
            body: Body::RespawnUp(RespawnUp {
                deaths: 1,
                pct: respawn_penalty::pct_for(1),
                seq: 3,
                px: 1000,
                py: -2000,
                pz: 300,
                yaw: 8192,
                hp: 90,
                hp_max: 100,
                name: "Checkpoint7".to_owned(),
            }),
            // kind, deaths, pct, seq, x, y, z, yaw, hp, hp_max, name.
            expected: "55 01 5a 03 e8 03 00 00 30 f8 ff ff 2c 01 00 00 00 20 5a 00 64 00 0b 00 43 68 65 63 6b 70 6f 69 6e 74 37",
        },
        Vector {
            name: "respawn_rest",
            body: Body::RespawnRest(RespawnRest {
                deaths: 0,
                pct: respawn_penalty::pct_for(0),
                seq: 4,
                px: 1000,
                py: -2000,
                pz: 300,
                name: "Checkpoint7".to_owned(),
            }),
            // kind, deaths, pct, seq, x, y, z, name.
            expected: "56 00 64 04 e8 03 00 00 30 f8 ff ff 2c 01 00 00 0b 00 43 68 65 63 6b 70 6f 69 6e 74 37",
        },
        // [desync-audit] The steady state of the auditor: kind, seq, map_hash, n, then
        // {cat, digest u64, count u16} per category. Two categories here (flags, deaths).
        Vector {
            name: "audit",
            body: Body::Audit(Audit {
                seq: 0x0102_0304,
                map_hash: 0xAABB_CCDD,
                cats: vec![
                    AuditCat { cat: 1, digest: 0x1122_3344_5566_7788, count: 11 },
                    AuditCat { cat: 12, digest: 0xDEAD_BEEF_0BAD_F00D, count: 2 },
                ],
            }),
            expected: "58 04 03 02 01 dd cc bb aa 02 01 88 77 66 55 44 33 22 11 0b 00 0c 0d f0 ad 0b ef be ad de 02 00",
        },
        // kind, seq, cat.
        Vector {
            name: "audit_req",
            body: Body::AuditReq(AuditReq { seq: 7, cat: 3 }),
            expected: "59 07 00 00 00 03",
        },
        // kind, seq, cat, total, first, then {str key, u64 value} rows read to the end.
        Vector {
            name: "audit_items",
            body: Body::AuditItems(AuditItems {
                seq: 9,
                cat: 4,
                total: 3,
                first: 0,
                items: vec![
                    AuditItemRow {
                        key: "/Game/Items/KeyItems/AquaDoorKey_2.AquaDoorKey_2".to_owned(),
                        value: 1,
                    },
                    AuditItemRow { key: "BrokeBarricade".to_owned(), value: 0 },
                ],
            }),
            expected: "5a 09 00 00 00 04 03 00 00 00 30 00 2f 47 61 6d 65 2f 49 74 65 6d 73 2f 4b 65 79 49 74 65 6d 73 2f 41 71 75 61 44 6f 6f 72 4b 65 79 5f 32 2e 41 71 75 61 44 6f 6f 72 4b 65 79 5f 32 01 00 00 00 00 00 00 00 0e 00 42 72 6f 6b 65 42 61 72 72 69 63 61 64 65 00 00 00 00 00 00 00 00",
        },
        // [gap-boss] The boss phase lane (WIRE.md §3.28). The Queen's key, revision 12, the
        // state she transitions to (1 = the phase-1 -> phase-2 step she got stuck in), sub 0,
        // and AUTHORITY|TRANSITION. kind, guid16, rev u32, state i32, sub i32, flags u8.
        Vector {
            name: "boss_phase",
            body: Body::BossPhase(BossPhase {
                id: Guid([
                    0xC8, 0x8F, 0xFC, 0x0C, 0x4A, 0x90, 0xA8, 0x7A,
                    0xF3, 0x0B, 0xA1, 0x98, 0x5A, 0x3A, 0x28, 0x63,
                ]),
                rev: 12,
                state: 2,
                sub: 1,
                flags: boss_phase_flags::AUTHORITY | boss_phase_flags::TRANSITION,
            }),
            expected: "5c c8 8f fc 0c 4a 90 a8 7a f3 0b a1 98 5a 3a 28 63 0c 00 00 00 02 00 00 00 01 00 00 00 03",
        },
        // The arena rule (WIRE.md §3.29): the fight is CLOSED and only the HOST's player is
        // inside. kind, guid16, rev u32, arena u8, present u8.
        Vector {
            name: "boss_arena",
            body: Body::BossArena(BossArena {
                id: Guid([
                    0xC8, 0x8F, 0xFC, 0x0C, 0x4A, 0x90, 0xA8, 0x7A,
                    0xF3, 0x0B, 0xA1, 0x98, 0x5A, 0x3A, 0x28, 0x63,
                ]),
                rev: 3,
                arena: boss_arena_phase::CLOSED,
                present: boss_present::HOST,
            }),
            expected: "5d c8 8f fc 0c 4a 90 a8 7a f3 0b a1 98 5a 3a 28 63 03 00 00 00 02 01",
        },
        // The ending choice (WIRE.md §3.30): the PEER's player chose Queen ending 1.
        // kind, guid16, rev u32, ending u8, who u8.
        Vector {
            name: "boss_end",
            body: Body::BossEnd(BossEnd {
                id: Guid([
                    0xC8, 0x8F, 0xFC, 0x0C, 0x4A, 0x90, 0xA8, 0x7A,
                    0xF3, 0x0B, 0xA1, 0x98, 0x5A, 0x3A, 0x28, 0x63,
                ]),
                rev: 1,
                ending: boss_ending::QUEEN1,
                who: boss_present::PEER,
            }),
            expected: "5e c8 8f fc 0c 4a 90 a8 7a f3 0b a1 98 5a 3a 28 63 01 00 00 00 01 02",
        },
    ]
}

fn to_hex(bytes: &[u8]) -> String {
    let mut out = String::with_capacity(bytes.len() * 3);
    for (i, b) in bytes.iter().enumerate() {
        if i > 0 {
            out.push(' ');
        }
        out.push_str(&format!("{b:02x}"));
    }
    out
}

fn parse_hex(s: &str) -> Vec<u8> {
    s.split_whitespace()
        .map(|t| u8::from_str_radix(t, 16).unwrap_or_else(|_| panic!("bad hex token {t:?}")))
        .collect()
}

/// The wire bytes of one vector: the kind byte followed by the body.
fn vector_bytes(v: &Vector) -> Vec<u8> {
    let body = encode_body(&v.body).expect("body must encode");
    let mut out = Vec::with_capacity(1 + body.len());
    out.push(v.body.kind().as_u8());
    out.extend_from_slice(&body);
    out
}

#[test]
fn body_encodings_match_the_specification() {
    for v in vectors() {
        let actual = to_hex(&vector_bytes(&v));
        assert_eq!(actual, v.expected, "wire bytes changed for {}", v.name);
    }
}

#[test]
fn the_expected_bytes_decode_back_to_the_same_body() {
    for v in vectors() {
        let bytes = parse_hex(v.expected);
        assert_eq!(bytes[0], v.body.kind().as_u8(), "kind byte for {}", v.name);
        let decoded = decode_body(v.body.kind(), &bytes[1..])
            .unwrap_or_else(|e| panic!("{} must decode: {e}", v.name));
        assert_eq!(decoded, v.body, "roundtrip failed for {}", v.name);
    }
}

/// Sorted-by-key lists mean the file cannot change because a `HashMap` iterated in
/// a different order on another machine.
#[test]
fn flag_lists_are_sorted_by_construction() {
    let a = flag_entries(&[("Zeta", 1), ("Alpha", 2), ("Mid", 3)]);
    let b = flag_entries(&[("Mid", 3), ("Alpha", 2), ("Zeta", 1)]);
    assert_eq!(a, b);
    let names: Vec<&str> = a.iter().map(|e| e.name.as_str()).collect();
    assert_eq!(names, vec!["Alpha", "Mid", "Zeta"]);

    let i = item_entries(&[
        ("/Game/Items/Tools/Lamp.Lamp", 4),
        ("/Game/Items/KeyItems/AquaDoorKey_2.AquaDoorKey_2", 1),
    ]);
    assert_eq!(i[0].item_path, "/Game/Items/KeyItems/AquaDoorKey_2.AquaDoorKey_2");
    assert_eq!(i[1].item_path, "/Game/Items/Tools/Lamp.Lamp");
}

#[test]
fn envelope_header_layout() {
    // WIRE.md §1, byte for byte: magic, ver, kind, body_len, session, seq.
    let row = PlayerRow {
        slot: 1,
        sample_ms: 1500,
        x: 1000,
        y: -2000,
        z: 300,
        yaw: 8192,
        pitch: -4096,
        vx: 100,
        vy: -100,
        vz: 0,
        move_mode: 3,
        action: 1,
        flags: 5,
    };
    let env = Envelope::new(0x0A0B_0C0D, 0x0102_0304, Body::PlayerRow(row));
    let datagram = env.encode().expect("encode");
    assert_eq!(
        to_hex(&datagram[..HEADER_LEN]),
        "51 44 43 31 01 10 1e 00 0d 0c 0b 0a 04 03 02 01",
        "envelope header must match WIRE.md §1 exactly"
    );
    // And the body after it is the player-row vector without its kind byte.
    let expected = parse_hex(
        "10 01 dc 05 00 00 e8 03 00 00 30 f8 ff ff 2c 01 00 00 00 20 00 f0 64 00 9c ff 00 00 03 01 05",
    );
    assert_eq!(datagram[HEADER_LEN..], expected[1..]);
    assert_eq!(Envelope::decode(&datagram).unwrap(), env);
}

fn vectors_path() -> PathBuf {
    // <repo>/mod/rust/qd-core  ->  <repo>/mod/doc/vectors.txt
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("..").join("..").join("doc").join("vectors.txt")
}

fn render_vectors_file() -> String {
    let mut out = String::new();
    out.push_str("# QDCOOP wire vectors, version 1.\n");
    out.push_str("# Generated by mod/rust/qd-core/tests/wire_vectors.rs; do not edit by hand.\n");
    out.push_str("# Format: <name> | <hex bytes>. The first byte is the envelope `kind`;\n");
    out.push_str("# the 16-byte envelope header (WIRE.md §1) is not included.\n");
    out.push_str("# Entry lists are sorted by key and the line order is fixed, so this file is byte-stable.\n");
    for v in vectors() {
        out.push_str(&format!("{} | {}\n", v.name, to_hex(&vector_bytes(&v))));
    }
    out
}

#[test]
fn vectors_file_is_written_and_matches_the_encoders() {
    let path = vectors_path();
    let rendered = render_vectors_file();
    std::fs::write(&path, &rendered)
        .unwrap_or_else(|e| panic!("cannot write {}: {e}", path.display()));

    // Read it back: the native self-test reads this exact file, so it must be
    // valid UTF-8, newline-terminated and parseable.
    let on_disk = std::fs::read_to_string(&path)
        .unwrap_or_else(|e| panic!("cannot read back {}: {e}", path.display()));
    assert_eq!(on_disk, rendered);
    assert!(on_disk.ends_with('\n'));

    let all = vectors();
    let mut lines = 0usize;
    for line in on_disk.lines().filter(|l| !l.starts_with('#') && !l.is_empty()) {
        let (name, hex) = line
            .split_once(" | ")
            .unwrap_or_else(|| panic!("malformed vector line: {line:?}"));
        let v = all
            .iter()
            .find(|v| v.name == name)
            .unwrap_or_else(|| panic!("vector file names {name:?}, which is not in the vector list"));
        let bytes = parse_hex(hex);
        assert_eq!(bytes.len(), 1 + encode_body(&v.body).unwrap().len());
        // The file's bytes are the encoder's bytes, which
        // `body_encodings_match_the_specification` has already pinned to the
        // document.
        assert_eq!(to_hex(&bytes), v.expected, "{name} on disk disagrees with the spec");
        assert_eq!(to_hex(&vector_bytes(v)), to_hex(&bytes));
        lines += 1;
    }
    assert_eq!(lines, all.len(), "the file must contain every vector exactly once");

    // No two vectors may share a name, or the line count above would be satisfied
    // by a duplicate standing in for a missing body.
    for (i, a) in all.iter().enumerate() {
        for b in all.iter().skip(i + 1) {
            assert_ne!(a.name, b.name, "duplicate vector name");
        }
    }
}
