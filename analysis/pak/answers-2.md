# answers-2 — C7, C10, D2, D9

Second pak pass. Four gaps, each answered against the shipped archive and, where the
question is about *saved* state, against the real save files in this workspace.

Corpus and instruments (every claim below names the one it used):

| instrument | what it reads | detection range |
|---|---|---|
| `code/pakread.py` | the pak index: **44,092 exact paths**, 1,976,294,370-byte pak, v11, unencrypted, uncompressed | exact path + payload byte range per entry |
| `code/blueprints.py` | a package's name table, import table, export table | every `FName`, every import/export record |
| `code/probes/stream_volumes.py` | 51 `.umap` export tables + their `.uexp` | every export record, every serialised actor block |
| `code/probes/inv_model.py` | `Content/Items/**` + `analysis/gvas.py` | all 4,129 item `.uasset`s; all 30-34 save properties |
| `code/probes/dialogue_model.py` | 73 dialogue `.uasset`s + `analysis/exe_strings_all.txt` | every name table, every native enum entry string |
| `code/probes/boss_state.py` | **all 1,973,612,650 payload bytes** + all 51 map export tables + 7 saves | exact ASCII substring, case sensitive; every placed actor |

Re-run any of them from `analysis/pak/code/probes/`; each prints the numbers quoted here.

Status: **C7 CLOSED · C10 CLOSED · D2 CLOSED · D9 CLOSED**. Confidence per section.

---

## C7 — inventory / equipment / key-item model

**Status: CLOSED.** The save stores object paths; the catalogue is 383 `FS_ItemData` assets;
the int in each map means something different in each of the three maps, and that is measured,
not assumed.

### C7.1 The catalogue is a bundle of assets per item, not rows

`Content/Items/` holds **4,129 `.uasset`** files (4,129 paired `.uexp`, 366 `.ubulk`). Counted
per family (`inv_model.py` §1):

| family | `.uasset` | directories | `FS_ItemData` definitions |
|---|---|---|---|
| `Items/Weapons` | 2,506 | 141 | 134 |
| `Items/KeyItems` | 503 | 42 | 87 |
| `Items/Treasure` | 400 | 43 | 71 |
| `Items/Consumables` | 310 | 27 | 30 |
| `Items/Accessories` | 281 | 38 | 48 |
| `Items/Upgrades` | 123 | 12 | 12 |
| `Misc` / `NullItem` / `AssetNotMadeYet` | 6 | 3 | 1 |
| **total** | **4,129** | 306 | **383** |

The 4,129 files are not 4,129 items: one weapon is ~18 files. The item *definition* is the asset
whose top-level export class is `FS_ItemData_C`. Measured class census over every self-export
under `Content/Items/` (`inv_model.py` §2):

```
FS_ItemData_C                    383   <- the definitions
BlueprintGeneratedClass          777   <- pickups / weapon scripts / visuals
Texture2D                        917
MaterialInstanceConstant         485
StaticMesh                       338
NiagaraSystem                    214
SoundCue / SoundWave             188 / 144
AnimMontage / AnimSequence       123 / 101
FS_WeaponStats                   106   <- per-weapon stat assets
FS_DaggerStats                    26   <- per-dagger stat assets
Material                         103
SkeletalMesh / Skeleton           32 / 31
AnimBlueprintGeneratedClass       27
PhysicsAsset                      25
```

Example bundle — `Items/Weapons/RustedSword/` (28 files, `inv_model.py` print of the folder):

```
RustySword_Item.uasset          <- definition (FS_ItemData_C)
RustySword_WeaponStats.uasset   <- stats  (FS_WeaponStats)
RustySword_WeaponScript.uasset  <- behaviour blueprint
RustySword_WeaponVisuals.uasset
RustySword_Item_Icon.uasset
SHop/RustySword_Shop_Item.uasset         <- shop variant (8 weapons ship a ShopVer)
SHop/RustySword_Shop_WeaponStats.uasset
```

### C7.2 The definition type

`Content/Blueprints/ItemSystem/FS_ItemData.uasset` (3,165 B, 77 names) exports
`FS_ItemData_C : BlueprintGeneratedClass` with **parent `FS_Item`** — i.e. the native class
`UFS_Item` seen in the exe surface. Field names present in the base (measured, `inv_model.py` §3):

```
Category  ItemCategory  EquipSlot  ItemEquipSlotType   Equippable  Consumable  Weapon
Unstackable  ShowCountInInventory  ShowInKeyMenu  HideWhenEmpty  SortOrder
KeyTag  DataImportKey  Item Description  ItemIcon  ThumbnailMask  WhiteMask
Pickup BP  Default Visual BP  "Drop BP / Fallback Visual BP"
Pickup3DConfig  Item3DConfig  Inspect3DConfig  Thumbnail3DConfig  PickupPutAwayPos
BaseLocation_…  BaseRotation_…  BaseScale_…  PickupSoundOverride  InspectBPOverride
```

Every definition also imports `/Game/StringTables/ImportedStringTable`, and each carries a unique
string-table key (`wep_SimpleAxe`, `key_acidvial_unlock`, `trs_bottle2`, `item_b33punch`,
`upgrade_health`, `acc_akumuamulet`) in `DataImportKey` — that is where the display name and
description resolve from. Family-specific extras are visible in the per-family tables
(`inv_model.py` §5): weapons add `FS_WeaponStats`/`StatData`/`LogicComponent` and
`ItemTags.AchievementWeapon`; treasure adds `Sellable`/`SellValue`; consumables add
`Cooldown`/`Duration`/`RegenPerSecond`/`DamageMultiplier`; accessories add effect multipliers
(`MagicRate`, `GunChargeRateIncrease`); upgrades add `ItemTags` (`ItemTags.Food`).

### C7.3 The two enums, decoded (not guessed)

Display names live in the enum asset's `.uexp`; pairing each `NewEnumeratorN` FName with the
following length-prefixed string gives the mapping in enum order (`inv_model.py` §4):

```
ItemCategory (9)        NewEnumerator0..8 = Consumables, Accessories, Weapons, Daggers,
                                              GemsAndBatteries, Keys, Food, Treasure, All
ItemEquipSlotType (4)   NewEnumerator0..3 = Accessory, Dagger, None, Weapon
```

Cross-check against real definitions: `AkumuAmulet` carries `ItemCategory::NewEnumerator1`
(= Accessories) and `ItemEquipSlotType::NewEnumerator0` (= Accessory); `SimpleAxe_Item` carries
`NewEnumerator2` (= Weapons) and `ItemEquipSlotType::NewEnumerator3` (= Weapon). The enum
semantics and the assets agree.

### C7.4 What the save stores, and what each int means

`World0.sav` (79,823 B, 30 properties) and `World0_Suspend_1.sav` (89,460 B, 34 properties) both
contain (read with `analysis/gvas.py`):

```
InventoryContents       Map<Object,int32>   {/Game/Items/Weapons/RustedSword/RustySword_Item.RustySword_Item: 1}
Equipment               Map<Object,int32>   {/Game/Items/Weapons/RustedSword/RustySword_Item.RustySword_Item: 2000}
EquipmentSlots          Map<int32,Object>   {2000: /Game/Items/Weapons/RustedSword/RustySword_Item.RustySword_Item}
CurrentlySelectedWeapon Object              /Game/Items/Weapons/RustedSword/RustySword_Item.RustySword_Item
```

Each stored path resolves exactly, 1:1, to a pak asset (`inv_model.py` §6):

```
/Game/Items/Weapons/RustedSword/RustySword_Item.RustySword_Item
  -> Items/Weapons/RustedSword/RustySword_Item.uasset   class=FS_ItemData_C  exports=1
```

The int means three different things and the type cannot tell them apart:

* `InventoryContents[obj]` = **stack count** (1 for the starting sword; `Unstackable` on the
  definition is the flag that makes the count meaningful).
* `Equipment[obj]` = **equip slot id**, not a count and not the enum ordinal — the sword sits at
  `2000`, while `ItemEquipSlotType::Weapon` is ordinal 3. `EquipmentSlots` is the exact inverse
  map `slot id -> item`, so the two always move together.
* `CurrentlySelectedWeapon` / `CurrentlySelectedDagger` = one object path each.

The slot id is computed by name, not stored as an enum: `FS_PlayerChar`'s name table contains
`GetSlotIDFromSlotTypeAndIndex`, `GetSlotIDFromIndexAndItem`, `TargetSlotID`, `WeaponSlotCount`,
`AccessorySlotCount`, `DaggerSlotCount`, `ValidDaggerSlots`, `SelectedWeaponSlot`. So the slot
token is `f(slot kind, index)` — *inference (MEDIUM)*: the observed value 2000 is consistent with
a `1000·kind + index` scheme with weapons on the 2000 range, but only one equipped item exists in
every save available here, so the exact arithmetic is not measured. The mod does not need it: it
copies the token opaquely.

Note the naming trap: `FS_SaveSlot_C` *declares* `Inventory`, `EquippedItems`, `EquippedItemSlots`
as well, and **none of them is serialised** in any of the 7 saves — the live property names are
`InventoryContents`, `Equipment`, `EquipmentSlots` (the save writer omits default-valued
properties, and those three never appear). Bind by the live names.

### C7.5 The model the mod must sync

| field | kind | key | value | notes |
|---|---|---|---|---|
| inventory | map | **full soft object path** of the `FS_ItemData` asset | int32 stack count | basenames are unique across all 383 defs (measured), so a short key is *safe today* but the full path is what the game stores |
| equipment | map | same item path | int32 slot token (2000 observed) | keep `EquipmentSlots` in lockstep, it is the inverse |
| selected weapon / dagger | scalar | — | item path | drives the peer's held mesh and HUD |
| weapon charges | map | item path | float | `WeaponCharges` (106 entries, 28 non-zero in the sample save) |
| dagger charges | map | item path | float | `DaggerCharges` (26 entries) |
| key items | (part of inventory) | item path under `Items/KeyItems/` | count | 87 definitions; door gating is ordinary inventory |

Definitions never cross the wire. Both machines have the same pak, so the identity key is
sufficient; the mod only ever sends `(item path, count)` / `(item path, slot)`. Because every
operation is an idempotent *set*, resend-absolute never "double-grants" — but `ItemChangedEvent`
(a serialised `MulticastInlineDelegate`, 141 B in the save) will fire locally on the receiver and
churn the HUD, so apply quietly.

**Confidence: HIGH** for the catalogue, the class split, the enums, the identity key and the save
semantics (all measured). **MEDIUM** only for the slot-id arithmetic.

---

## C10 — level streaming: every volume, and what it loads

**Status: CLOSED.** 6 `FS_LevelManagerVolume` actors and 49 `LevelManagerTrigger_C` actors, each
mapped to the level list it configures; 42 streamed sublevels; and the union arithmetic the mod
needs.

### C10.1 The class census (all 51 maps, export tables)

`stream_volumes.py` §1 — placed actors, not asset names:

| class | count | where |
|---|---|---|
| `FS_LevelManagerVolume` (native `AFS_LevelManagerVolume`) | **6** | 5 in `Ruins_Level_00`, 1 in `BirdsTestZone` (dev map) |
| `LevelManagerTrigger_C` | **49** | 48 in `Ruins_Level_00`, 1 in `740_SZ_Prison` |
| `LevelStreamingDynamic` | **43** | 42 in `Ruins_Level_00`, 1 in `BirdsTestZone` |
| `LevelStreamingAlwaysLoaded` | **5** | `title_screen` 1, `Intro_Level_00` 1, `Ruins_Level_00` 1, `VideoConfigMap` 2 |
| `LevelStreamingPersistent` | 0 | — |

So the persistent world is `Ruins_Level_00`; 42 of its 43 streaming objects are dynamic.
Confirmed independently: its name table contains exactly **42 distinct `/Game/RuinsLevel/Maps/<Level>`
package paths** (excluding `LevelLists/` and itself), which matches the 42 `LevelStreamingDynamic`
exports one-for-one. The sublevels are:

```
000_Shared_Base_Lighting 050_Ocean 060_Ocean_Shadow 070_UpperShell 100_GR 110_GR_dungeon
120_GR_goldenroom 125_GR_CubeHall 130_GR_outerisles 140_GR_ShopIsleOnly 200_AQ
210_AQ_underground 300_LIB 310_LIB_exterior 320_LIB_basement 400_MAUS_entrance 420_MAUS_mid
450_MAUS_darkmaze 500_AUT 510_AUT_droptunnel 600_Castle 610_Castle_Ext 700_SZ_GR 710_SZ_AQ
720_SZ_LIB 730_HOD 740_SZ_Prison 750_SZ_MAUS2 760_Dekanima 760_SZ_AUT 770_SZ_Swamp
775_JigekiTemple 780_SZ_Depths 790_SG 800_FinalZone 900_SipehaWorld 990_B33sment
997_FinalZoneCutscene 998_QueenCutscenes 999_SipehaCutscenes Intro_Level_00 Intro_Level_01
```

### C10.2 How the mapping was read (and why it is trustworthy)

A cooked level actor's export block is a tagged property stream. Rather than trust a whole-block
parse (map actor blocks do not parse end-to-end — see the limitation note), each reference is
**structurally validated**: an `ObjectProperty` value at offset *p* is accepted only if the FName
at *p−25* names the property and the FName at *p−17* is `ObjectProperty`, and only if the resolved
import is of class `FS_LevelList`. That signature was recovered from the bytes and then confirmed
against the two property names the blueprint actually declares, which makes it self-checking.

Recovered layout (measured, and the reason the offsets are fixed):
volume `LevelList` value at block offset **25** (first property); trigger `A Side` at **286** and
`B Side` at **315** — identical across all 48 triggers, because they are blueprint defaults.

### C10.3 Volume → level list (all 6, no gaps)

```
ACTOR                       PROPERTY   LEVEL LIST                       MAP
FS_LevelManagerVolume       LevelList  zzz_birdTest_SL_ON               BirdsTestZone
AquaRuins_LevelVolume       LevelList  AquaRuins                        Ruins_Level_00
FS_LevelManagerVolume       LevelList  GreenRuins_OuterIsles            Ruins_Level_00
FS_LevelManagerVolume2      LevelList  GreenRuins_OuterIsles_ExtraPri   Ruins_Level_00
FS_LevelManagerVolume3      LevelList  GreenRuins_NearLibrary           Ruins_Level_00
GreenRuinsLevelVolume       LevelList  GreenRuins                       Ruins_Level_00
```

### C10.4 Trigger → A/B side lists (49 triggers, 72 references)

78 references total (6 volume + 72 trigger). Tallies: **23 triggers configure both sides, 21 A-side
only, 5 B-side only** (23+21+5 = 49). Sample (full table printed by the probe):

```
LevelManagerTrigger       A AquaRuins        B SZ_Depths
LevelManagerTrigger11     A Mausoleum        B Mausoleum2_Deep
LevelManagerTrigger13     A Library_SZ       B Library
LevelManagerTrigger14     A AutumnIslandCastle B AutumnIsland
LevelManagerTrigger17     A ROS              B SZ_Depths
LevelManagerTrigger24     A Prison_SZ        B SZ_Depths
LevelManagerTrigger45     A StoneGarden      B SZ_Depths
LevelManagerTrigger49     A Prison_SZ        B SZ_Depths     (in 740_SZ_Prison)
```

One-sided triggers are real and meaningful (e.g. `LevelManagerTrigger10` sets only `SZ_Depths`;
`LevelManagerTrigger2` only `Mausoleum`) — they are the edge of the streamed region where only one
neighbour is loaded.

### C10.5 The level lists themselves

**22 `FS_LevelList` assets** exist (20 under `RuinsLevel/Maps/LevelLists/`, plus
`LD45/.../zzz_birdTest_SL_ON`, plus `Areas/Other/EmptyLevelList`). 19 are referenced by a volume.
Level names are plain strings in the list asset's `.uexp`, so the list → levels mapping is exact:

| list | levels | | list | levels |
|---|---|---|---|---|
| GreenRuins_OuterIsles | 16 | | AquaRuins | 15 |
| GreenRuins_OuterIsles_ExtraPri | 16 | | GreenRuins | 15 |
| Library | 16 | | GreenRuins_NearLibrary | 15 |
| Mausoleum | 16 | | GreenRuins_SZ | 13 |
| Mausoleum2_Deep | 12 | | SZ_Depths | 11 |
| Library_SZ | 10 | | Mausoleum_SZ | 9 |
| Prison_SZ | 9 | | StoneGarden | 8 |
| DropTunnel | 6 | | ROS | 6 |
| AutumnIsland | 5 | | AutumnIslandCastle | 5 |
| IntroCutscene | 2 | | SipehaArena | 2 |
| EmptyLevelList | 0 | | zzz_birdTest_SL_ON | 2 |

Every list ends with `000_Shared_Base_Lighting`; most also carry `VideoConfigMap`, which is the
engine's shared lighting/UI-preview sublevel, not a gameplay level.

### C10.6 Can two players' loaded-level needs be unioned?

**Yes — structurally, by construction.**

Measured: the game does not "load a level list"; it maintains a **stack of level lists** and the
residency set is their union. The evidence is in the names, and it is consistent across three
independent assets:

* `LevelManagerTrigger_C` imports `AddLevelListToStack` and `RemoveLevelListFromStack`, and
  `Checkpoint_C` (`LD45/Blueprints/Checkpoint`) imports the same two plus `ClearTriggerLevelLists`
  and `SetFreezeLevelListUpdates`.
* The save declares three separate stacks: `LevelListStack`, `TriggerLevelListStack`,
  `SafeLocLevelListStack` (`FS_SaveSlot_C` property list; the suspend saves additionally carry
  `LevelLoadingSuspendState`).
* The manager has `FS_LevelListStackEntry`, `FS_LevelManagerAction`, `FlushLevelManagerActions`,
  `EFS_WaitForLevelManagementMode`, `LevelManagerUpdate`, `LevelManagerDoneLoading`.

Add/remove by value on a stack is set semantics, so a second player in a second volume is the same
operation as the first: `stack = {listA, listB}`. The design is also cheap by intent — the lists
overlap heavily, so a union is barely larger than one list (measured):

* largest single list: **16** levels
* `GreenRuins` (15) ∪ `Library` (16) = **18** distinct levels (13 common)
* `SZ_Depths` (11) ∪ `Mausoleum2_Deep` (12) = **18** distinct levels (5 common)
* union of *everything* reachable in the persistent world: **37** levels

So a split party costs at most ~18 resident sublevels, against a 42-sublevel universe. **This is an
inference from structure, not a live observation** — the union claim rests on the stack API and the
list algebra, both measured, but nobody has run two pawns through it.

### C10.7 What the mod must carry

| datum | why | source of truth |
|---|---|---|
| volume identity | to say "player 2 is in `Library`'s volume" | placed actor name is unique per map, but the durable key is the actor **Guid** — the same Guid space `SavableActorStates` already uses (C3/C4) |
| the list it configures | the actual load instruction | `LevelList` / `A Side` / `B Side` object reference; 78 measured refs |
| trigger state | which side is active, so a peer entering from the other side does not fight the local player | `ASideTriggered` / `BSideTriggered` / `IsBSide` / `OldTrigger` on `LevelManagerTrigger_C`, `SolidMode`, `Should Run` |
| the stack | the union itself, and its order (removal is by value, so order is not load-order-sensitive) | `LevelListStack` / `TriggerLevelListStack` / `SafeLocLevelListStack` |
| load completion | never un-gate a peer early | `LevelManagerDoneLoading`, `LevelManagerUpdate`, `EFS_WaitForLevelManagementMode` |

Rule for the mod: **union, never replace.** If the peer's stack is authoritative for its own
volume and the host's for its own, both lists must be resident on both machines or one player falls
through unloaded floor. Send `(generation, volumeId, listName, side)` and let each machine rebuild
its own stack as the union of the party's entries.

**Confidence: HIGH** for the census, the 6/6 volume mapping, the 49/49 trigger mapping and the
list contents. **MEDIUM** for "union is sufficient" (structural inference). **Not measured:** the
runtime behaviour of two simultaneous triggers, and the memory cost of an 18-sublevel union.

### C10 limitation, stated

The per-actor property stream is not parsed end-to-end in this pass: the tag model that consumes a
`.uasset` export exactly (`Name/Type/Size/ArrayIndex/[type FNames]/+1/value`, terminated by `None`)
does **not** consume map actor blocks — they desynchronise after the first few properties. The
`LevelList` / `A Side` / `B Side` values were therefore recovered with the *validated signature*
(name at p−25, type at p−17) rather than a full parse, and all 78 references pass that check with
no ambiguity. Anything beyond those three properties in a map actor is **not** decoded here.

---

## D2 — dialogue data model (conditional branches)

**Status: CLOSED.** 61 dialogue data assets of one class, one 18-field schema, four enums with the
values read out of the exe, and a manager whose state machine is visible by name.

### D2.1 The asset population

`dialogue_model.py` §1 — 73 `.uasset` paths contain `Dialogue`:

| kind | count | examples |
|---|---|---|
| `FS_DialogueData` data assets | **61** | `NPCs/Knight/Knight_Arjaot/Knight_Arjaot_Dialogue_00`, `NPCs/Merchant/Merchant_Dialogue`, `Enemies/Queen/Queen_GreetDialogue` |
| `UFS_DialogueAction` blueprints | 2 | `Enemies/Sipeha/Sipeha_BanishDialogueAction`, `…_EatDialogueAction` |
| UI blueprints | 4 | `UI/DialogueSystem/{DialogueManager, DialogueScreen, DialogueOptionEntry, DialogueExitButton}` |
| texture / material | 2 | `UI/UI_Style/DialogueAdvanceDiamond{,_Mat}` |
| font-style `DataTable`s | 4 | `UI/UI_Style/FontStyles/DialogueRichText{,_Light}`, `BigDialogueRichText{,_Light}` |

The two `*_DialogueAction` assets are blueprints deriving from `UFS_DialogueAction` and expose
`RunActionWithinContext` and `FinishAction` (`Sipeha_BanishDialogueAction.uasset`); that pair is
the action contract — *begin, then report finished* — which is exactly the shape a sync layer needs
to fence a remote action.

### D2.2 The schema: fields shared by all 61 assets

Set intersection of every asset's classified name table (`dialogue_model.py` §2) — this is the
`FS_DialogueData` type's own vocabulary, measured:

```
Greetings                  DialogueSequence         OptionalDialogueToRunAfter
Conditions                 NamedSaveDataValueChanges
ActionToRun                ActionToRunAtEnd
Entry                      OptionText               AutoClose  bHideName  bSkipGreeting  DevName
FS_DialogueSequence        S_DialogueSequenceEntry  S_DialogueOption  S_SaveDataCondition
```

Reading that list as a specification:

```
FS_DialogueData (one asset per NPC / per boss / per altar)
├─ Greetings                  : [FS_DialogueSequence]        the lines this NPC can say
│     each sequence has an authored label and entries (S_DialogueSequenceEntry)
├─ DialogueOptions            : [S_DialogueOption]           the player's choices
│     OptionText + its own Conditions + ActionToRun
├─ Conditions                 : [S_SaveDataCondition]        gate: Type, DataToCheck,
│                                                             Comparison, ValueToCompare, Behaviour
├─ NamedSaveDataValueChanges  : [FS_NamedSaveDataChange]     writes: ChangeType, DataToCheck, Value
├─ ActionToRun / ActionToRunAtEnd : UFS_DialogueAction        side effects
└─ OptionalDialogueToRunAfter : FS_DialogueData               chaining into the next asset
```

`Entry` is the sequence entry point; `bSkipGreeting` / `bHideName` / `AutoClose` / `DevName` are
presentation flags. Text is never inline: each asset carries string-table keys (`d_arjaot_00…03`,
`d_shrineboss_talk_1/2`) resolved through `DialogueFilenameFormat` in `Game.locres`.

### D2.3 The condition enums — the values, not just the names

These are native `UENUM`s, so their entries live in the exe's string pool; `exe_strings_all.txt`
yields them exactly (`dialogue_model.py` §3, source `analysis/exe_strings_all.txt`):

```
EFS_DialogueConditionType      2 values   EDCT_Inventory, EDCT_NamedSaveData
EFS_DialogueConditionBehaviour 4 values   EDCB_HideAND, EDCB_HideOR, EDCB_ShowAND, EDCB_ShowOR
EFS_Comparator                 6 values   EC_Equal, EC_GreaterThan, EC_GreaterThanOrEqual,
                                          EC_LessThan, EC_LessThanOrEqual, EC_NotEqual
EFS_NamedSaveDataChangeMode    2 values   ENSDCM_Add, ENSDCM_Set
```

That is the entire branch language: **two** condition sources (inventory count, or a NamedSaveData
value), compared with one of six comparators, combined under a 4-valued show/hide × AND/OR mode.
A dialogue tree is therefore not a bytecode graph — it is a flat list of conditionally-shown
options, which is why it can be enumerated statically at all.

`ENSDCM_Add` vs `ENSDCM_Set` is the same trap as in C6, now confirmed at the type level: an `Add`
write is a counter increment and must be replicated as a post-write **absolute** value.

### D2.4 Per-asset condition keys (measured)

Per-asset literals that survive the shared-vocabulary subtraction split cleanly into two groups,
which is itself the structural finding:

* **sequence labels** (contain a space) — breadcrumbs for the designer:
  `Greet First Time`, `Turn in exactly 4 necklaces`, `Sequence if talked to post first time while
  you have belt`, `repeat - quest finished`, `not ready`, …
* **NamedSaveData keys** (no space, underscore-joined) — the actual branch variable:
  `Arjaot_State` (Arjaot), `ShopDoorOpened` and `MerchantDidShopDoorComment` (Merchant),
  `DragonIsDead` (Green Dragon), and the per-quest keys carried by the quest-giver assets.

So an NPC's whole conversation state is: *one int in `NamedSaveData`* (which greeting/option set is
live) plus *the inventory query* (has-this-item gates like the knife belt). That is what a sync
design has to mirror — not the text.

### D2.5 The runtime model

`UI/DialogueSystem/DialogueManager.uasset` (21,492 B, 374 names, 21 exports) is the driver. Its
state and transitions, by name:

```
state       CurrentDialogue  CurrentDialogueNPC  CurrentDialogueSequence  CurrentDialogueSequenceIndex
            RunningDialogue  AvailableOptions  FilteredOptions  CurrentAvailableOptions
            ChangedNamedSaveDatas  DisplayingError  EntryPoint
evaluate    EvaluateConditionSet  FilterAvailableOptions  GetNamedSaveData
progress    DialogueSequenceContinue  DialogueSequenceContinuePressed  DialogueSequenceEnded
            ShowDialogueSequenceEntry  ShowDialogueOptions  ReturnToOptions
branch      JumpToDialogue  DialogueToJumpTo  DialogueOptionChosen  DialogueOptionSelected
effects     RunAction  SaveNamedData  SequenceFinalActionEnded  ActionFinished
exports     StartDialogue  End  EndDialogue  ShowDialogueWidget  HideDialogueWidget
            JumpToDialogue  FilterAvailableOptions  ReturnToOptions  SequenceFinalActionEnded
            Bind Continue Event  Reset Event Bindings  ChangedNamedSaveDatas
            Display Options Error  Report Greeting Error  CustomEvent
delegates   DialogueActionEvent, DialogueJumpEvent, DialogueOptionChosen,
            DialogueSequenceContinue, DialogueSequenceEnded
note        the manager imports FS_DialogueAction / FS_DialogueData / FS_DialogueSequence /
            FS_NamedSaveDataChange / S_DialogueOption / S_DialogueSequenceEntry and calls the
            game's own SaveNamedData + GetNamedSaveData - it does not own a private store
```

The model a sync design must mirror, therefore:

1. **A dialogue is a cursor, not a stream.** `(dialogueAsset, sequenceIndex, entryIndex)` is the
   complete position. Two machines agree by agreeing on three values.
2. **Conditions are evaluated locally from shared state.** Because the condition inputs are only
   `NamedSaveData` and inventory (both already synced for C6/C7), the *option list* is derivable —
   it never needs to be sent. Send the cursor; each side recomputes `AvailableOptions`.
3. **A choice is an event with an owner.** `DialogueOptionChosen` must carry which player chose;
   two players' choices on one global sequence cannot share a stream.
4. **Actions are begin/end fenced.** `RunActionWithinContext` / `FinishAction` gives the action a
   lifetime; `ActionToRunAtEnd` makes it order-sensitive. A remote-triggered action must not be
   re-run on receipt of a cursor jump — apply the cursor *quietly* and let the action arrive on its
   own channel (same rule as C6 flag application).
5. **`OptionalDialogueToRunAfter` chains assets** — so "end of dialogue" is not the end of the
   conversation, and a naive "dialogue closed" broadcast will cut off the chain.

**Confidence: HIGH** for the asset population, the 18-field schema, the enum values (exe strings),
the per-asset keys and the manager vocabulary. **MEDIUM** for the runtime-state reading (it is
name-derived; no dialogue was executed). **Not measured:** whether a conversation pauses the world
(that is D1, still BLOCKING/OPEN and out of scope here).

---

## D9 — boss arena state

**Status: CLOSED.** There is no boss-state container. Boss progress is three separate, already-known
mechanisms: `NamedSaveData` int flags, `SavableActorStates` one-shot bits, and the challenge map.
One encounter controller exists as an actor, not as persistence.

### D9.1 The negative, with its instrument

`boss_state.py` §1 runs a real byte scan over **all 1,973,612,650 payload bytes** of the pak
(exact ASCII substring, case sensitive, every payload byte plus every name table):

```
BossStates     0 hits
bossKilled     0 hits
BossDefeated   0 hits
BossState      0 hits
BossManager    0 hits
```

So the DollsMP analogue does not exist here, under any of the four obvious spellings. This is a
whole-corpus negative, not a path guess.

### D9.2 The positive: boss defeat is a `NamedSaveData` int

Same scan, positive needles — each key appears in `FS_ChallengeData` **and** in the blueprint that
owns the boss:

| key | hits | written by (owner of the hit) |
|---|---|---|
| `DemonBossesKilled` | 5 | `Enemies/DemonBosses/DemonSpider/DemonSpider_BP`, `DemonSpider_Spawner`, and the two maps that place the controller |
| `BridgeLordDead` | 3 | `Enemies/JosamiLord/JosamiLord_BP`, `JosamiLord_Spawner` |
| `DragonLordDead` | 3 | `Enemies/Dragon/GreenDragon_Spawner`, `780_SZ_Depths.umap` |
| `RedLordsDead` | 3 | `Enemies/JosamiLord/BloodSisters/JosamiBloodLord_Sword_Spawner`, `740_SZ_Prison.umap` |
| `ROSDragonDead` | 4 | `Dragon_Ros_BP`, `Dragon_Ros_Bestiary`, `ROSDragon_Spawner` |
| `GiantSwampFish_IsDead` | 3 | `GiantFish_Enemy_BP`, `GiantSwampFish_EmergeSpot` |
| `NecronimiconDefeated` | 3 | `Necronomicon_BP`, `Necronomicon_Boss_Spawner` |
| `EndingSeen` | 3 | `Queen_BP`, `Sipeha_NPC` |
| `NecroWall00Destroyed` … `04` | 2 each | `FS_ChallengeData` + the owning LIB map |

All of these are also **fields of `FS_ChallengeData`** (the 121-name struct analysed in
`analysis/pak/answers.md` Q2), and `EFS_ChallengeMode::Stat_AutoNSD` is the mode that auto-earns a
challenge from an NSD key. So the boss flag and the achievement are the same write: **one int in
`NamedSaveData` per boss.**

*Inference (MEDIUM-HIGH):* that these keys land in `NamedSaveData` rather than in some
challenge-only store. Not observed in a save — every available save is early-game (11 NSD keys, no
boss key; see §D9.5) — but the key literals sit in boss blueprints that call
`SaveFunctions_C`/`GetNamedSaveData`, and `FS_ChallengeData` declares them, which is the same
composition rule C6 established for `Arjaot_State`.

### D9.3 The arena: 31 placed boss actors, and how their state persists

`boss_state.py` §3 — placed actors whose class contains `Boss`, across all 51 maps:

```
Boss_Deco_00_BP_C                10   (3 maps)
Boss_Env_PPC_C                    3   (210_AQ_underground, 790_SG, BirdsTestZone)
Lib_BossDoor_BP_C                 3   (300_LIB)
DemonBoss_FightController_C       2   (450_MAUS_darkmaze, BirdsTestZone)
WebBossWall_BP_C                  2   (450_MAUS_darkmaze, BirdsTestZone)
GraveyardBoss_Gate_C              2   (100_GR)
Boner_ShrineBoss_Spawner_C        1   (100_GR)
AquaBossDoor_BP_C / AquaBossExitDoor_C   1 / 1  (210_AQ_underground)
RibberBoss_Tiara_Pickup_C, ChickenBossNecklace_Pickup_C,
Fishman_Boss_Crown_Pickup_C      1 each
Necro_Boss_Door_C, Necronomicon_Boss_Spawner_C   1 / 1  (300_LIB)
DesBoss_Gate_C                   1   (780_SZ_Depths)
                                 ---
                                 31
```

Where each gate's own state lives is determined by its **parent class** (§3b):

```
AquaBossDoor_BP      parent = SavableActor_C     -> one-shot bit in SavableActorStates (Guid -> int)
WebBossWall_BP       parent = SavableActor_C     -> same
Lib_BossDoor_BP      parent = Door_Base_C        -> door base's own persistence
DesBoss_Gate         parent = Door_Base_C
GraveyardBoss_Gate   parent = Door_Base_C
Necro_Boss_Door      parent = Necro_Deco_Base_C
Lib_TreasureGate_BP  parent = Door_Base_C
```

So arena *gating* needs no new channel: it is the `SavableActorStates` map (`Map<Guid,int>`, 7
entries in the sample save) that C4/C5 already own, plus the `Door_Base` family. **Boss defeat** is
the NSD flag. Those are two different lanes and conflating them is the bug to avoid — a gate is
idempotent set-to-1, a defeat flag is a monotone assertion that must also fire the challenge.

### D9.4 The encounter controller

`DemonBoss_FightController` (`Enemies/DemonBosses/DemonBoss_FightController.uasset`,
class `DemonBoss_FightController_C`, parent `Actor`) is the only "boss manager" in the game, and it
is an **actor, not a store**. Measured fields:

```
SourceGUID  Finished  Finished__DelegateSignature
LavaPathSpline  Spline  SplineCurves  ProjectWorldLocationOntoTrack
ProjectPlayerPositionOnTrack  ProjectActorPositionOnTrack  GetTrackTargetLocation
SpawnMagmaPuddle  UpdateMagmaPuddles  ManagedPuddles  Puddle  Magma Puddle Despawning
Check Puddles Colliding With Player  LavaDamage  DamageAmount  DamageTags  TryApplyDamage
```

It is the dual-boss (Demon Spider + Hell Bat) encounter: it tracks the player along a spline,
spawns despawnable magma puddles, applies lava damage, and raises `Finished`. Note `SourceGUID` —
the same GUID identity concept as `SavableActorStates`, i.e. the controller can be tied to a placed
instance rather than to a class.

The rest of the boss surface is presentational and reproduced for completeness: `HUD_BossHPBar`
(+mask), `BossHealthBar` in `UI/HUD/Widgets/EnemyHealthBar/`, arena post-process
(`Queen_BossArena_PPC`, `ROS_BossArenaPPC`, `Boss_Env_PPC`), 65 assets under `Music/3-Boss/`, and
`ActivateBossHPbar` / `DeactivateBossHPbar` on `FS_PlayerChar`.

### D9.5 The challenge layer, and a naming correction

`boss_state.py` §5:

* `FS_ChallengeData` contains **8** boss challenge ids:
  `cha_Bosses_BridgeLord`, `cha_Bosses_Demons`, `cha_Bosses_DragonLord`, `cha_Bosses_GiantFish`,
  `cha_Bosses_Necro`, `cha_Bosses_RedLords`, `cha_Bosses_RoSDragon`, `cha_Bosses_Whale`.
* The eight `boss_*` assets (`boss_bigfish`, `boss_dragon`, `boss_hellduo`, `boss_jl`, `boss_necro`,
  `boss_redlords`, `boss_ros`, `boss_whaleking`) are **`Texture2D` achievement icons**, not keys —
  each was checked: top-export class `Texture2D`, `PF_DXT1`, `TEXTUREGROUP_UI`. So "challenge key
  `boss_*`" is really *challenge id* `cha_Bosses_*` + *icon* `boss_*`.

The id/icon pairing is also visible in the raw bytes of `FS_ChallengeData` (offset `0x0bb3f04a`
context reads `boss_ros … boss_whaleking … BridgeLordDead … cha_All_Complete`), i.e. the challenge
record carries `id, icon, NSDCondition` adjacently — the structure the mod should treat as
`cha_Bosses_X ⇔ <Boss>Dead`.

### D9.6 No save carries boss state yet

`boss_state.py` §6, all 7 saves in the workspace (`save-backup-before-live/`, `_live/…`):

```
World0.sav / World0_backup.sav / World0_Suspend_0/1.sav   11 NamedSaveData keys, 0 boss-ish
System0.sav                                                0 NamedSaveData keys
```

Every save is an early-game character (the 11 keys are the Arjaot/RustySword/Checkpoint set). So the
boss keys are **not observed in a save here** — the claim that they persist through `NamedSaveData`
rests on the write sites plus `Stat_AutoNSD`, and should be confirmed with one save taken after a
boss kill.

### D9.7 What the mod must sync

| what | lane | rule |
|---|---|---|
| boss defeated | `NamedSaveData[<Boss>Dead] = 1` | host-only write; absolute value; idempotent; must also let the challenge evaluate (`cha_Bosses_*`) |
| arena gate / boss wall | `SavableActorStates[actorGuid] = 1` | host-only, first-wins, keyed by the placed actor's Guid |
| encounter in progress | `DemonBoss_FightController` state (`SourceGUID`, spline position, `ManagedPuddles`) | **do not replicate**; the controller runs on the host only, peer sees puppets. Only the `Finished` transition crosses the wire |
| boss HP bar | `ActivateBossHPbar`/`DeactivateBossHPbar` on the player | drive from the encounter state, not from the HUD |
| arena post-process / music | `Boss_Env_PPC`, `Music/3-Boss/*` | cosmetic, but mismatched boss music reads as broken (same note as the sync matrix #24) |

**Confidence: HIGH** for the negative (whole-pak, named instrument, named range), the 31 placed
actors, the gate parents, the 8 ids and the icon correction. **MEDIUM-HIGH** for "boss defeat is a
`NamedSaveData` key" (write-site + challenge-struct evidence, not observed in a save).
**Not measured:** the `Finished` → flag write ordering, and whether the controller survives a
reload.

---

## Appendix — two corrections to earlier documents

Both are outside the four items, but they were found by the probes above and are load-bearing for
D2, so they are recorded here rather than silently left wrong.

1. **`analysis/pak/datatables.md` says "zero DataTable assets".** That is true for *game data*
   tables but not literally true for the archive: scanning the top-level export class of all
   **19,162 `.uasset` files** finds **9 `UDataTable` assets**, all UI text-style tables —
   `UI/Text_Images/FS_RichTextImages` and eight under `UI/UI_Style/FontStyles/`
   (`DialogueRichText{,_Light}`, `BigDialogueRichText{,_Light}`, `DescriptionRichText{,_Light}`,
   `BindingSpecialText{,_Light}`). They were missed because they carry no `DT_` prefix and live
   outside any `DataTables/` folder. The substantive half of the claim survives: `RowHandle` is
   still 0 hits across the pak, and none of the 9 is a game-data table (their row structs are
   `RichTextStyleRow` / `TextBlockStyle`).

2. **`boss_*` is not a challenge-key namespace.** The eight `boss_*` assets are achievement
   **icons** (`Texture2D`); the challenge ids are `cha_Bosses_*`. Any manifest built on the
   assumption "`boss_*` keys exist" would find nothing at runtime.

## Appendix — probe inventory

| file | sections printed |
|---|---|
| `code/probes/inv_model.py` | family census, class census, `FS_ItemData` schema, two enums decoded, per-family representative tables, save-path resolution, identity/stack model |
| `code/probes/stream_volumes.py` | class census over 51 maps, 6 volume→list + 49 trigger→A/B mappings, 22 level lists with contents, 42 sublevels, union/overlap arithmetic |
| `code/probes/dialogue_model.py` | 73-asset population, shared vs per-asset vocabulary, four enums from the exe, one asset decoded, manager vocabulary |
| `code/probes/boss_state.py` | whole-pak negative scan, boss-key positive scan, 31 placed actors, gate parents, encounter controller, challenge namespace, save check |

Machine-readable summary: `analysis/pak/answers-2.json`.
