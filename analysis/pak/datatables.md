# DataTables — none carry game data, and here is the proof

## Verdict

**The pak contains no game-data DataTable.** A top-level export-class scan over all **19,162**
`.uasset` files finds **9 `UDataTable` assets, and every one is UI text styling**:
`UI/Text_Images/FS_RichTextImages` plus eight under `UI/UI_Style/FontStyles/`
(`DialogueRichText{,_Light}`, `BigDialogueRichText{,_Light}`, `DescriptionRichText{,_Light}`,
`BindingSpecialText{,_Light}`). Their row structs are `RichTextStyleRow` / `TextBlockStyle`.
None is item, weapon, quest, shop, teleport or dialogue data.

Quest state, challenge definitions, item definitions, weapon stats, shop stock,
teleport/save-point lists and dialogue are all carried by **DataAsset-style
blueprint/struct assets** (`*_Data`, `*_WeaponStats`, `FS_ChallengeData`, …) and by
**placed map actors**, never by a `UDataTable`.

This is the single most consequential finding for the mod, because it means there is no
"read the quest table" shortcut and no row struct to enumerate.

## The searches, and what they returned

All path searches run against `entries.tsv` — the 44,092 exact paths decoded out of the pak's
FullDirectoryIndex by `code/pakread.py`. All content searches run over the **entire 1.97 GB pak**
with `code/scan_strings.py`, which reads every payload byte and maps each hit back to its owning
entry.

| search | scope | hits | what the hits are |
|---|---|---|---|
| path contains `/DataTable` or `/DataTables/` | 44,092 paths | **0** | — |
| path contains `DT_` | 44,092 paths | **0** | — |
| path contains `table` (case-insensitive) | 44,092 paths | 242 | all furniture/architecture props (`Castle_Table_00_*`, `Lib_EndTable_*`, `M_Table_00_*`), plus `*Activatable*` and `SmallTablet_*`. Not one is a data table. |
| any asset with extension `.uasset` whose file name ends `_Data` | 44,092 paths | 284 | data *assets* / structs, see below |
| top-level export class == `DataTable` | 19,162 `.uasset` files | **9** | UI text styling only — `FS_RichTextImages` + 8 `FontStyles/*`; row structs `RichTextStyleRow`/`TextBlockStyle`. Found by a class scan, not by path (this is what the `DT_`/`/DataTables/` filters missed). |
| byte search `DataTable` | whole pak | 83 | **all** in `Engine/Content/Localization/Engine/*/Engine.locres` (18 in `en`) and in UMG rich-text widgets (`FS_RichTextImages`, `*RichText` font styles, `DialogueScreen`, `JournalMenu`, `OptionsMenu_BindingEntry`, `ReadMe`-style text widgets). These are the *UMG* `UDataTable` property that rich-text decorators use, not game data. |
| byte search `GetDataTableRow` | whole pak | 4 | `Engine.locres` only — engine localisation for the K2 node. |
| byte search `RowHandle` | whole pak | **0** | — |
| byte search `StructRowHandle` | whole pak | **0** | — |
| byte search `CurveTable` | whole pak | 43 | `Engine.locres` only (5 locales). |

`RowHandle` returning zero is the clincher: any blueprint that reads a DataTable must hold a
`FDataTableRowHandle`, and that type name would appear in some package's name table. It appears
nowhere.

## What the game uses instead

### 1. `*_Data` blueprint/struct data assets — 284 of them

284 assets whose name ends `_Data`, plus the type assets they instance:

```
Blueprints/ItemSystem/FS_ItemData          item definition type
Blueprints/ReadableText_DataAsset          journal / readable-text entries
Blueprints/Teleporter/TeleporterData       fast-travel destination data
Blueprints/ENVControl/ENVControlData       environment / fog profiles
Blueprints/MusicControl/MusicTrackData     music tracks
Blueprints/SecretSwordAltar/Puwema_NPC_Data
Enemies/**/*_Data                          one per enemy variant (~200)
NPCs/**/*_Data                             one per NPC
```

`Enemies/Bat_00/Bat_00_Data`, `Enemies/Boner_00/Skeletons24/Black/Boner_Black_Berserker_Data`,
`NPCs/Knight/Knight_Arjaot/Knight_Arjaot_Data` … each is a small asset holding that actor's tuning.
This is the functional replacement for a "row", but there is **no shared row struct and no
table-level enumeration** — the set of rows is the set of assets.

### 2. Weapon stats are per-weapon assets — 102 of them

```
Items/Weapons/<Weapon>/<Weapon>_WeaponStats   102
Items/Weapons/<Weapon>/<Weapon>_WeaponScript  107
Items/Weapons/<Weapon>/<Weapon>_Item          155   (also Items/KeyItems, Consumables, …)
Items/Weapons/<Weapon>/ShopVer/<Weapon>_Shop_Item   8  shop stock entries
```

So "weapon stats" is `*_WeaponStats.uasset` (one asset per weapon), and "shop stock" is
`*_Shop_Item.uasset` (8 assets) plus the `ShopEntry`/`ShopTreasureActor` blueprints.

### 3. Challenge / achievement definitions — `Blueprints/Challenges/FS_ChallengeData`

This is the closest thing to a table in the whole game, and it is a **UserDefinedStruct used by
blueprint variables**, not a DataTable. Its 121 property names include the entire achievement key
set:

```
ChallengeDefinitions   EFS_ChallengeMode   CompleteConditionMode   CompletedChallenges
cha_All_Complete       cha_Area_Aqr  cha_Area_Aut  cha_Area_Castle  cha_Area_Forest
cha_Area_Lib  cha_Area_Maus  cha_Area_Prison  cha_Area_ROS  cha_Area_SG  cha_Area_Swamp
cha_Bosses_BridgeLord  cha_Bosses_Demons  cha_Bosses_DragonLord  cha_Bosses_GiantFish
cha_Bosses_Necro  cha_Bosses_RedLords  cha_Bosses_RoSDragon  cha_Bosses_Whale
cha_Challenge_AllChests  cha_Challenge_AllSecretDoors  cha_Challenge_AllTeleporters
cha_Endings  cha_Item_AllDashGems  cha_Item_EnergyWep  cha_Item_FlyingSword  cha_Item_GotAllAcc
cha_Item_GotAllCells  cha_Item_GotAllLenses  cha_Item_GotAllThrowables  cha_Item_GotAllTreasures
cha_Item_GotAllWeps  cha_Item_SecretSword  cha_Item_SpecSword  cha_Item_UltraGem
cha_Item_UpgradeAllWeps
cha_Quest_Books  cha_Quest_Chef  cha_Quest_Fish  cha_Quest_Grimoire  cha_Quest_Lib
cha_Quest_Meat  cha_Quest_Mushroom  cha_Quest_PG_Puzzle  cha_Quest_Raider  cha_Quest_Science
cha_Quest_Shop_Spend  cha_Quest_Shop_Unlocks  cha_Quest_Skulls  cha_Quest_Vaults
EFS_ChallengeMode::Manual  EFS_ChallengeMode::Stat_AutoInventory
EFS_ChallengeMode::Stat_AutoItemTag  EFS_ChallengeMode::Stat_AutoNSD
```

Note those enum values — they are the *whole* design of the achievement system in three names:
a challenge is either **Manual**, or auto-earned from **an inventory count**, from **an item tag**,
or from **a NamedSaveData key**. `Stat_AutoNSD` is what makes achievements and NamedSaveData the
same mechanism.

### 4. Quest definitions — 15 `Blueprints/Challenges/quest_*.uasset`

```
quest_book  quest_chef  quest_ducks  quest_egg  quest_grimoire  quest_lib  quest_mushroom
quest_necro  quest_raider  quest_sci  quest_skullls  quest_spend  quest_trident
```

Each is a blueprint asset, and the `cha_Quest_*` achievement keys above are their completion
markers. See `answers.md` Q1 for where the *runtime* state lives.

### 5. Dialogue — 38 `*_Dialogue.uasset` blueprint assets

```
NPCs/**/Ghost_*_Dialogue, NPCs/Knight/Knight_*_Dialogue_00, NPCs/Chef, NPCs/Scientist,
Enemies/**/{Queen,Sipeha,WhaleKing,Dragon,BookBoss,...}_Dialogue,
Blueprints/ChefCrowEvent/{Chef,Crow}_Event_Dialogue,
Blueprints/SecretSwordAltar/SecretSwordAltar_*Dialogue
```

Dialogue is a blueprint that drives a dialogue UI and stores `NamedSaveDataValueChanges` — not a
text table. The *text* itself comes from `Game.locres` (see `localisation.md`).

### 6. Teleport / save-point lists — map actors, not assets

`Blueprints/Teleporter/TeleporterData` is one type asset; the actual destinations are **placed
actors** in the 51 `.umap` files. `answers.md` Q3 enumerates all 43 of them with object paths,
recovered from the maps' export tables.

## If you still want a table-shaped view

The pak gives you everything needed to *build* one: `entries.tsv` lists every asset, and every
`*_Data` / `*_WeaponStats` / `*_Item` asset's own name table (via `code/blueprints.py`) gives its
field names. What is missing is the **values** — those are in each asset's `.uexp`, and decoding
them needs the property-tag stream parsed with the asset's struct layout. That is the smallest
remaining step if row values are wanted:

> parse `.uexp` tagged properties for a `*_Data` asset, using its `UserDefinedStruct`
> (e.g. `FS_ItemData`) field list as the layout, then dump the tagged values.
> `code/blueprints.py` already locates the summary, name table and import/export tables the
> parser would need.
