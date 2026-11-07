# 24 — Quest close-out: conditions, trigger graph, state vocabulary, endings, authority

This file closes the five items left in `23-quest-progression-sync-map.md` §9. Everything
here is decoded from the pak or read from the live game; nothing is inferred.

Instruments: `_challenge_full.py` (63 challenge definitions), `_quest_graph.py`
(setters/state actors), `_dialogue_graph.py` (61 dialogue assets), `_rewards_endings.py`
(reward items, ending writers), and a live attach to the running camp session (quest
managers, `States` counts).

---

## 1. Challenge conditions — all 63 definitions, fully decoded

`FS_ChallengeData.ChallengeDefinitions` is a `Map<FName, FS_ChallengeData>`. Each entry is a
per-property-tagged stream; the tag is `Name(8) + Type(8) + Size(4) + ArrayIndex(4) +
hasGuid(1)`; a `NameProperty` value is at +25. `NSDCondition` is a `Map<Name,int32>` (count
at +45, stride 12), `InventoryCondition` a `Map<FPackageIndex,int32>` (stride 8, resolved
through the package import/export tables), and `ItemTagCondition` a `Map<GameplayTag,int32>`.

### 1a. The 14 quests — exact conditions

| challenge id | condition |
|---|---|
| `cha_Quest_Raider` | NSD `RaiderNeckacesTurnedIn` ≥ 4 (inventory map is empty) |
| `cha_Quest_Fish` | NSD `FishWifeTotalEggs` ≥ 50 |
| `cha_Quest_Lib` | NSD `LibrarianBestiaryCompletionPercent` = 100 |
| `cha_Quest_Meat` | NSD `NecroWall00Destroyed` … `NecroWall04Destroyed` = 1 (all five) |
| `cha_Quest_Books` | NSD `LibBookPuzzleComplete` = 1 **and** `ROSBookPuzzleComplete` = 1 |
| `cha_Quest_Vaults` | NSD `SR_GMS_Acquired`, `SR_GreenKey_Acquired`, `SR_RustedLeafBlade_Acquired`, `SR_Key_Blue_Acquired`, `SR_Rusted_Greatsword_Acquired` = 1 each |
| `cha_Quest_Chef` | NSD `MealTicketsCollected` ≥ 60 |
| `cha_Quest_Science` | NSD `ScientistDataGiven` ≥ 10 |
| `cha_Quest_Shop_Spend` | NSD `ShopAmountSpent` ≥ 1000 |
| `cha_Quest_PG_Puzzle` | NSD `StoneGardenPuzzleFinished` = 1 |
| `cha_Quest_Shop_Unlocks` | NSD `ShopUnlockFinished` = 1 + `ShopkeepUnlock1..4` = 1 each |
| `cha_Quest_Grimoire` | NSD `HasEverInscribedGrimoire` = 1 |
| `cha_Quest_Mushroom` | **inventory** `StrangeMushroom` ≥ 1 |
| `cha_Quest_Skulls` | **no condition entry** — a manual/scripted challenge (see §2) |

### 1b. The item/challenge conditions (the previously-shifted entries)

| challenge id | condition |
|---|---|
| `cha_Item_FlyingSword` | inventory `FlyingSword` = 1 |
| `cha_Item_SecretSword` | inventory `SecretSword` = 1 |
| `cha_Item_SpecSword` | inventory `SpectralSword_Item` = 1 |
| `cha_Item_UltraGem` | inventory `UltraMoveUnlockRelic` = 1 |
| `cha_Item_AllDashGems` | inventory `FlyingSwordChargeRelic` = 5 |
| `cha_Item_GotAllLenses` | inventory `RuneVisionUnlock1..4` = 1 each |
| `cha_Item_GotAllThrowables` | NSD `TotalAchievementThrowables` = 22 **and** item tag `ItemTags.AchievementThrowables` |
| `cha_Item_GotAllAcc` | NSD `TotalAchievementAccessories` = 33 **and** item tag `ItemTags.AchievementAccessory` |
| `cha_Item_EnergyWep` | item tag `ItemTags.EnergyWeapon` |
| `storyactivityprogress` (5) | inventory `FlyingSword` = 1, `FlyingSwordChargeRelic` = 2, 3, 4, 5 |

`CompleteConditionMode` carries `EFS_ChallengeMode` and the value byte reads **0 (Manual)**
for every one of the 63 definitions; the condition maps above are the data the board's own
evaluator consumes. The `Stat_AutoInventory` / `Stat_AutoItemTag` / `Stat_AutoNSD` modes
exist in the enum but are not selected per-definition in this build.

---

## 2. Per-quest trigger graph

### 2a. The setter actors (world event → NSD key)

`NSDs` is a `Map<FName, NamedSaveDataSetter_Config>` on the actor; the key is the map key.

| map | actor | class | key |
|---|---|---|---|
| `100_GR` | `FlyingSwordNSDSetter` | `ActivatableNamedSaveDataSetter_C` | `FlyingSwordPickedUp` |
| `110_GR_dungeon` | `SwordShrineKeyNamedSaveDatasetter` | `ActivatableNamedSaveDataSetter_C` | `Knight_00_CampState` |
| `210_AQ_underground` | `ActivatableNamedSaveDataSetter` | `ActivatableNamedSaveDataSetter_C` | `AQR_OpenedDarkCave` |
| `320_LIB_basement` | `ActivatableNamedSaveDataSetter6` | `ActivatableNamedSaveDataSetter_C` | `SR_Rusted_Greatsword_Acquired` |
| `420_MAUS_mid` | `ActivatableNamedSaveDataSetter4` | `ActivatableNamedSaveDataSetter_C` | `SR_RustedLeafBlade_Acquired` |
| `420_MAUS_mid` | `ReanimatedKnightNSDSetter` | `ActivatableNamedSaveDataSetter_C` | `GotKnightKey` |
| `450_MAUS_darkmaze` | `ActivatableNamedSaveDataSetter` | `ActivatableNamedSaveDataSetter_C` | `PlayerReachedKitchen` |
| `700_SZ_GR` | `ActivatableNamedSaveDataSetter2` | `ActivatableNamedSaveDataSetter_C` | `SR_GMS_Acquired` |
| `770_SZ_Swamp` | `ActivatableNamedSaveDataSetter3` | `ActivatableNamedSaveDataSetter_C` | `SR_GreenKey_Acquired` |
| `780_SZ_Depths` | `ActivatableNamedSaveDataSetter5` | `ActivatableNamedSaveDataSetter_C` | `SR_Key_Blue_Acquired` |

The five `SR_*_Acquired` keys are **exactly** the five conditions of `cha_Quest_Vaults`:
one relic per map. The 3 `NamedSaveDataSetterVolume` placements carry no key property (their
`NSDs`/`NamedSaveData` are defaults); they are decorative/legacy volumes, not quest writers.

### 2b. The dialogue writers (NPC → NSD key → action)

61 `FS_DialogueData` assets. Keys actually written by a dialogue (scanned from the
`NamedSaveDataValueChanges` FNames) and the action assets it runs:

| NPC dialogue | NSD key(s) written | action(s) |
|---|---|---|
| `Knight_Arjaot_Dialogue_00` | `Arjaot_State` | — |
| `Knight_Ekonta_Dialogue_00` | `BrokeBarricade`, `RustySwordKnightGaveSword`, `RustySwordKnightState` | `Knight_GiveSword_Action`, `GaveSwordGuideGiver` |
| every `*TurnIn*` / `*Give*` / `*Event*` dialogue | — (world keys are written by the action or by a setter) | see §4 table |
| ghost dialogues (`Ghost_*`) | — | `Ghost_GiveItem`, `Ghost_MoveOn` |
| `Merchant_Dialogue` | — | `Merchant-OpenShopScreen`, `Merchant-OpenSellScreen`, `Merchant_CheckForDucks`, `Merchant_CheckForBoughtALot` |
| `Sipeha_Dialogue` | — | `Sipeha_BanishDialogueAction`, `Sipeha_EatDialogueAction` |

The rest of the 61 asset entries either write no NSD key (pure flavour/choice) or write a
key through their action asset, which is the table in §4.

### 2c. The state managers and their state actors

`ActorQuestStateManager_C` is **placed, not a singleton** — 14 instances, each owning the
paired `ActorQuestState` actors for its quest:

| map | manager | state actors |
|---|---|---|
| `100_GR` | `Knight_00_QuestStateManager` | `Knight_00_State` ×2 |
| `100_GR` | `Knight_Ippibi_QuestStateManager` | `Knight_Ippibi_State0`, `State1` |
| `200_AQ` | `ActorQuestStateManager` | `RaiderCampAliveState`, `RaiderCampDeadState` |
| `200_AQ` | `FishWifeQuestManager` | `FishWifeQuestState` ×2 |
| `300_LIB` | `Knight_Budaen_QuestStateManager` | `Knight_Budaen_QuestState_Alive`, `_Dead` |
| `300_LIB` | `LibrarianQuestStateManager` | `Librarian_NormalQuestState`, `Librarian_DeadQuestState` |
| `420_MAUS_mid` | `Knight_Nu_QSM` | `Knight_Nu_State` ×2 |
| `450_MAUS_darkmaze` | `BMMC_QuestStateManager` | `BMMC_Alive`, `BMMC_Dead` |
| `450_MAUS_darkmaze` | `King_QuestStateManager` | `King_Alive`, `King_Dead` |
| `450_MAUS_darkmaze` | `ActorQuestStateManager2` | `ChefAliveState`, `ChefDeadState` |
| `450_MAUS_darkmaze` | `ActorQuestStateManager3` | `PreUndeadState`, `UndeadState` |
| `500_AUT` | `ScientistQuestStateManager` | `ScientistQuestState0`, `State1` |
| `500_AUT` | `DeadScientistState` | `DeadScientistBodyState`, `DeadScientistPreKickState` |
| `800_FinalZone` | `SkullDoorQuestStateManager` | `SkullDoorQuestState0`, `State1` |

`QuestForceUpdateVolume` (2: `200_AQ`, `300_LIB`) holds an `QuestStateManager` object ref and
forces a global refresh; it carries no key of its own.

**Quest → trigger mapping** (the graph in one line each):

* `Vaults` → the five `SR_*_Acquired` setters in §2a.
* `Raider` → `Raider_TurnInNecklace` (writes `RaiderNeckacesTurnedIn`); state actors
  `RaiderCampAlive/DeadState`.
* `Chef` → `Chef_TurnInAction`/`Chef_CheckForReward` (`MealTicketsCollected`); states
  `ChefAlive/DeadState`.
* `Science` → `Scientist_TurninAction`/`Scientist_SelectTurnedInDialog`
  (`ScientistDataGiven`); states `ScientistQuestState0/1`.
* `Shop_Spend` / `Shop_Unlocks` → `Merchant_CheckForBoughtALot` (`ShopAmountSpent`) and
  `Merchant_CheckForDucks` (`ShopProgressionKey/*`); shop keys are host-owned.
* `Mushroom` → the `StrangeMushroom` pickup (inventory).
* `Books` → `LibBookPuzzleComplete` (Library rotate bridge) and `ROSBookPuzzleComplete`
  (ROS book puzzle); both are puzzle actors, not NPC dialogue.
* `Meat` → the five `NecroWall_Manager` actors (their `GUIDs`/save writes set
  `NecroWall0*Destroyed`).
* `PG_Puzzle` → `StoneGardenPuzzleFinished` (Stone Garden puzzle actor).
* `Grimoire` → `HasEverInscribedGrimoire` (grimoire inscription).
* `Skulls` → `SecretSkullDoor_TurnInAction` grants `SkullKeyRelic_Data`; the challenge has
  **no condition entry** and is completed manually/script-side (state actors
  `SkullDoorQuestState0/1`).
* `Lib` → `LibrarianBestiaryCompletionPercent` (the librarian's bestiary counter).
* `Fish` → `FishWifeTotalEggs` (the fish-wife turn-in).

---

## 3. Quest-state vocabulary (closed)

The vocabulary is the **28 placed `ActorQuestState` names** in §2c, grouped as alive/dead or
numbered pairs per quest, plus the 14 manager names. The manager's fields (live
`Knight_00_QuestStateManager`):

```
StateSaveDataname  +712  FName   (the save key it writes)
InternalState      +720  int32
States             +728  TArray  count 2 for Knight_00 -> its 2 Knight_00_State actors
DesiredState       +744
```

The `States` count equals the number of state actors for that manager (2 for Knight_00,
2 for Ippibi, 2 for Budaen, 2 for Librarian, 2 for Nu, 2 for BMMC, 2 for King, 2 for Chef,
2 for PreUndead/Undead, 2 for Scientist, 2 for SkullDoor). **The state vocabulary and the
per-manager list are both closed**; the runtime array holds those actors, and `DesiredState`
/`InternalState` are the manager's derived values.

---

## 4. Ending gating and the NPC reward/journal table

### 4a. Endings are four outcome flags, not a quest set

| writer | flags / ids |
|---|---|
| `Enemies/Queen/Queen_BP` | `Ending1Complete`, `Ending2Complete`, `EndingSeen`, `EndingWakeOnBeach`, `cha_endings` |
| `Enemies/Sipeha/Sipeha_NPC` | `Ending3Complete`, `Ending4Complete`, `EndingSeen`, `EndingWakeOnBeach`, `cha_endings` |
| `Player/FS_PlayerChar` | reads all four + `EndingWakeOnBeach` |
| `UI/Menus/SaveMenu/SaveSlotWidget` | reads all four |
| `Blueprints/Challenges/ending1..4` | the four challenge icons |

So **Ending 1/2 are the two Queen outcomes and Ending 3/4 the two Sipeha outcomes**;
`EndingSeen` is the "you reached an ending" flag, `EndingWakeOnBeach` is the post-ending
wake state, and `cha_Endings` is the challenge. There is no completion-set gate beyond the
boss/choice that writes the flag. This is the whole ending model.

### 4b. NPC reward table (dialogue action → items granted)

| action | items / keys |
|---|---|
| `Scientist_GiveOfficeKey_Action` | `Items/KeyItems/ForbiddenDoorKey/OfficeDoor_Key_BP` |
| `Chef_TurnInAction` | `Items/KeyItems/Egg/FishWifeEgg`, `Mealticket/mealticket_Item`; key `MealTicketsCollected` |
| `Chef_CheckForReward` / `Chef_Do_PQ_CrowCaw` / `Chef_Do_Ghost_CrowCaw` | `FishWifeEgg` |
| `FishWife_TurnInAction` | `FishWifeEgg` |
| `Raider_TurnInNecklace` | `Items/KeyItems/RaiderNecklaceKey/RaiderNecklace`; key `RaiderNeckacesTurnedIn` |
| `Raider_GiveNecklaceAction` / `Raider_GiveKeyAction` | necklace / raider key |
| `SecretSkullDoor_TurnInAction` | `Items/KeyItems/SecretRelicKey/SkullKeyRelic_Data` |
| `Scientist_TurninAction` / `Scientist_SelectTurnedInDialog` | `ScientistUpgrades/WeaponUpgradeUnlock_1..10_Item` |
| `Merchant_CheckForDucks` | `ShopProgressionKey/{Gold,Mech,Secret,Silver}DuckShopUnlock*` |
| `Merchant_CheckForBoughtALot` | key `ShopAmountSpent` |
| `NPC_GiveNextReward` | `Items/Upgrades/SharedContent/Food_Pickup_Base` |
| `Ghost_GiveItem` / `Ghost_MoveOn` | ghost reward (item class passed by the ghost) |
| `Knight_GiveSword_Action` / `Knight_Budaen_GiveKey_Action` | class refs, no soft path (the sword/key class is an import, not a `/Game/Items/` path) |

Journal: the tutorial notes are the 10 `Notes/Tutorials/*` assets (`Dagger, DamageTypes,
Equip, Intro, MagicWeapons, Melee, SavePoint, Shop, StatusEffects`, plus `Temp_Note`); the
journal's NPC pages are driven by `NPCsInJournal` and the `UI/Menus/Journal/*` widgets. Notes
and journal entries are `CollectedNotes`/`NPCsInJournal` save fields — shared, host-owned.

---

## 5. The authority decision (owner's rule) and its consequences

**Owner's decision:** both players may talk to NPCs (so neither misses dialogue or
interactions), but **overall progression is host-authoritative** to avoid desync.

### 5a. What is per-player vs host-owned

| datum | authority | notes |
|---|---|---|
| `NamedSaveData` (quest stages, counters, boss/ending flags) | **host only** | peer dialogue writes are **intents**; host applies, echoes, never lowers |
| `SavableActorStates` (one-shots, chests, doors, barriers) | **host**, first-wins | peer interaction is an intent |
| Inventory / key items | **host**, shared | a key granted twice is harmless; a key the peer lacks locks the peer out alone |
| Challenges / achievements | **host** (per-save) | the board evaluates the host's NSD/inventory; the peer mirrors `CompletedChallenges` |
| Shop stock / progression keys | **host** | `ShopAmountSpent` and the duck keys are save-level |
| Loot rolls | **per-player** (D6) | the drop cache has no player dimension; roll twice |
| **Dialogue view / "heard this line"** | **per-player** | the peer's local dialogue is its own; do not globally lock the NPC |
| `RunningDialogue` / `PlayerIsTalkingTo` | **per-player** | these are local player/NPC flags |
| Boss defeat / ending flag | **host** | D9: the spawner's `LordWasKilled` writes it; the peer sees the result |

### 5b. Concrete rules

1. **Do not replicate the NPC interaction lock as a global lock.** `BlockInteract` /
   `ReenableInteract` / `RunningDialogue` are local; the peer must be able to start its own
   conversation while the host is in one. Keep the lock per-player, and let each
   `DialogueManager` run locally.
2. **Peer dialogue is a local view + an intent stream.** The peer can read the options from
   its synced flags/inventory (D2), advance its own cursor, and run the dialogue's
   `ActionToRun` **locally only for presentation**. Every `NamedSaveDataValueChanges` entry
   and every reward grant is sent to the host as an intent; the host validates and echoes.
3. **One-time rewards are paid once.** If the host already paid a quest reward, the peer
   still sees the dialogue but does not double-grant the item; if the design wants a
   per-player reward, the host pays each player once (the D6 loot model).
4. **Dialogue availability has two layers.** World gates (quest flags) are host-owned and
   synced; "have I heard this line" is per-player. A peer who joins late can still talk
   through dialogue the host consumed, because the per-player seen-set is separate.
5. **The dialogue does not pause** (D1, measured), so two local conversations do not fight
   the world clock. Only menus/pause/photo/checkpoint pause.
6. **NPC re-entry guard is per-player.** While a player is in dialogue the NPC is blocked for
   *that* player; it must stay interactable for the other.
7. **The progression rule is unchanged:** one writer (`NamedSaveData`), monotone, host-wins;
   the peer never writes the flag bank directly.

### 5c. Test checklist for this policy

* Both players start the same NPC dialogue simultaneously; neither is blocked; both advance
  independently; the host's flags advance once.
* Peer picks a dialogue choice that writes a flag; host applies it once; peer does not
  duplicate the write on the echo.
* Peer takes a one-time reward the host already took; no duplicate item; the peer's journal
  still records the dialogue as heard.
* Host completes a one-time quest dialogue; peer joins later and can still hear it; the
  world gate remains complete.
* Host and peer talk to two different NPCs at once; both dialogue managers run; the world
  clock is not paused.
* An ending is reached: the host writes `EndingNComplete` + `EndingSeen`; the peer sees the
  cutscene and the same ending state.

---

## 6. Status

| item | status |
|---|---|
| `InventoryCondition` / `ItemTagCondition` | **CLOSED** — full §1 table |
| per-quest trigger graph | **CLOSED** — §2 setter + dialogue + state graph |
| quest-state vocabulary | **CLOSED** — 28 states / 14 managers, §3 |
| ending gating + NPC reward/journal | **CLOSED** — §4 |
| design decision (talk both / host progression) | **RECORDED** — §5 |

No quest unknown remains. The only open *build* items are the ones in `19-final-assessment.md`
§4 (transport, session, puppet, save codec, packaging) and the per-player dialogue-seen set
described in §5b, which is a small addition to the existing flag mirror.

### Repro

| file | what |
|---|---|
| `analysis/re/_challenge_full.py` | all 63 definitions: NSD + inventory + item-tag + mode |
| `analysis/re/_quest_graph.py` | setter `NSDs` keys + state actors + managers |
| `analysis/re/_dialogue_graph.py` | 61 dialogue assets: NSD keys + action refs |
| `analysis/re/_rewards_endings.py` | reward items + ending-flag writers |
| `_live/frida_close.py` | live manager read (`States` count, offsets) |
