# 07 — Verification of the two pre-code checks

Two questions gated the design. Both were answerable **offline**, and both came back
favourably. This file records the test, the result, and what it changes.

---

## Check 1 — Are the save GUIDs stable (authored) or runtime-generated?

### Why it mattered

`SavableActorStates` and `EnemySaveStates` are keyed by 16-byte GUIDs. If those were
generated at spawn time with `FGuid::NewGuid()`, two machines loading the same level would
invent *different* IDs for the same enemy — and entity sync would need a whole parallel
identity scheme. If they are authored into the content, identity is free.

### The test

The pak's payloads are **plaintext and uncompressed** (no encryption key GUID, no
compression methods, and — see Check 2 — a readable `DefaultEngine.ini` inside it). So a
GUID that is cooked into a level package can simply be *found* in the pak by byte search.

I extracted all 421 persisted IDs from the live save — 7 from `SavableActorStates` and all
**414** from `EnemySaveStates` — and searched the entire 1,976,294,370-byte pak for each.

### The result

```
SavableActorStates keys: 7
EnemySaveStates keys   : 414
searching 421 ids across the pak ...
scan took 149.1s
FOUND 421 / 421 ids in the pak
missing: (none)
```

**421 of 421.** A 128-bit value has a 2⁻¹²⁸ chance of an accidental match, so every hit is
real content.

### Where they live

Each GUID sits inside a **level package**, in property data next to the actors it
describes. Contextual evidence:

| ID | Enclosing package imports | Reading |
|---|---|---|
| `8d7e…7452`, `cc38…02d0` | `ItemPickup_Base`, `ItemPickupA…`, `ChallengeCompleteVolume`, `DamagingCollision`, `ENVControl/ENVControlData`, `FS_Light` | a gameplay level with placed interactables |
| `d31a…57da` | `Artifacts/Egg_Fossil`, `Artifacts/StrangeMushroom`, forest/ocean ambience | the Green Ruins level |
| `3b5d…28cf`, `43fc…1f62` | Library interior ambience | the Library level |

Each GUID sits behind a 16-byte size field in *unversioned* property data, i.e. it is a
plain serialized `FGuid` property on a placed actor — not an editor-only
`AActor::ActorGuid`, which is stripped from cooked builds.

### Corroborating names in the binary

```
PersistentGuid   SpawnerGUID   ObjectGuid   UniqueId
UFS_SpawnerInterface   DoSpawnerUpdate   CheckSpawnerMaxDist   CheckSpawnerVis
SpawnerDistanceDespawnCheck   FaceFollowComp ...
```

`SpawnerGUID` exists because enemies are spawned by `Enemies/EnemySpawner` actors; the
persisted key is authored either on the spawner or on the placed enemy, and either way it
is cooked.

### Verdict

> **Entity identity is authored and stable.** Both machines resolve the same GUID for the
> same enemy or interactable after loading the same level, with no extra scheme.

**What this changes:** the highest-severity risk in the analysis is gone. Entity sync keys
directly on the GUID, exactly as the design already described. No spawn-lineage inference,
no label matching, no hashing of actor paths.

**What it does not prove:** that a GUID survives a *respawn* (a dead enemy re-created by
its spawner) with the same ID — that is a design question about the spawner, resolvable
later, and the fallback (spawner GUID + index) is available.

### One more thing the 414 records told us

Every one of the 414 records carries **exactly the same three fields**:

```
bValid        bool
RespawnTime   float   = -1 in all 414
RemainingHP   float   = -1 in all 414
```

Fixed stride: 16-byte key + 141-byte value = 157 bytes per record, verified against the
map's declared size (`8 + 414 × 157 = 65,006` exactly).

In a save 658 seconds into the game, *nothing* has been killed or damaged yet — the map is
a **registry of enemies that have streamed in**, with per-enemy deltas applied only once
something happens to them. That is a very small, very cheap sync surface: three scalars
per enemy, and only over the enemies that are relevant.

---

## Check 2 — Does Steam actually load?

### Why it mattered

If Steamworks loads, Steam P2P is a zero-config transport (DollsMP already has a working
implementation). If it does not, the mod must ship its own UDP relay or users must forward
ports.

### The test

Parsed the PE import directory and, separately, the **delay-import** directory of
`QueensDomain-Win64-Shipping.exe`, then read the game's own `DefaultEngine.ini` out of the
pak.

### The result

`steam_api64.dll` is **not** a static import. It is in the **delay-load** table:

```
DELAY-LOAD IMPORTS:
  steam_api64.dll   attrs=0x1 (RVA-based)
  d3d12.dll, SHLWAPI.dll, GFSDK_Aftermath_Lib.x64.dll,
  PhysX3_x64.dll, PhysX3Cooking_x64.dll, PhysX3Common_x64.dll,
  PxFoundation_x64.dll, PxPvdSDK_x64.dll, ApexFramework_x64.dll,
  NvCloth_x64.dll, libvorbisfile_64.dll, XAudio2_9Redist.dll,
  dbghelp.dll, MF.dll, MFPlat.DLL, MFReadWrite.dll
```

and the DLL file itself exists **only** at
`Engine\Binaries\ThirdParty\Steamworks\Steamv163\Win64\steam_api64.dll` — **not** next to
the shipping exe, and not on any default DLL search path.

**Delay-load is why the game runs at all without it.** A missing static import aborts
startup; a missing delay-load import only raises at first call, where UE4's online layer
fails and the game continues. That is consistent with the observed evidence: the game has
been played on this machine (658 s, a real save chain) with the DLL unreachable.

### But the game *is* configured for Steam

`DefaultEngine.ini` inside the pak — **plaintext** — says:

```ini
[/Script/EngineSettings.GameMapsSettings]
GameInstanceClass=/Game/Blueprints/FS_GameInstance.FS_GameInstance_C
GameDefaultMap=/Game/LD45/Maps/title_screen.title_screen
bUseSplitscreen=True
TwoPlayerSplitscreenLayout=Horizontal

[/Script/Engine.GameEngine]
+NetDriverDefinitions=(DefName="GameNetDriver",
    DriverClassName="OnlineSubsystemSteam.SteamNetDriver",
    DriverClassNameFallback="OnlineSubsystemUtils.IpNetDriver")

[OnlineSubsystem]
DefaultPlatformService=Steam

[OnlineSubsystemSteam]
bEnabled=true
SteamDevAppId=1799840
Achievement_0_Id=cha_All_Complete
Achievement_1_Id=cha_Area_Aqr
... 35 achievements ...
```

So the game is *built* for Steam (`DefaultPlatformService=Steam`,
`SteamNetDriver` as the primary net driver with `IpNetDriver` as fallback), and it also
ships `OnlineSubsystemNull` for Steam-free LAN sessions.

### Verdict

> **In this install, Steamworks almost certainly does not load** — the DLL is not
> resolvable. The game runs, but as a plain offline single-player build.

**What this changes:**

1. **Do not make Steam P2P the only transport.** Ship DollsMP's UDP relay as the primary
   path. That is the proven, testable route and it does not depend on Steam.
2. **There is a real fallback worth keeping in the design:** `OnlineSubsystemNull` +
   `IpNetDriver` are compiled in and configured as the *fallback* driver. A LAN listen-server
   experiment is not obviously impossible — but it is an experiment, not a plan, because
   no game class implements sessions or RPCs (`01-binary-forensics.md` §4).
3. **If the user later installs the game properly under Steam**, Steam transport may light
   up for free; keep the relay as the default and treat Steam as an optional upgrade.

### A note for the packaging step

DollsMP's `tools/steam_mock/` is still the right thing to test with — but the reason is now
different: not "prove the tunnel against a fake Steam", but "make sure the game still runs
when Steam is absent", which is this user's actual configuration.

---

## Bonus finding — the pak payloads are plaintext

Discovering `DefaultEngine.ini` readable inside the pak answers the one open item from
`02-pak-inventory.md` §8 and `05-primary-evidence.md` §7:

- **No decryption of pak payloads is needed.** `.ini` files were the case that needed a key
  in Dolls Nest; here there is no key at all, and the file reads straight out.
- The only remaining obstacle to reading `.uasset`/`.umap` payloads is the
  **bit-packed `EncodedPakEntries` table** that maps a name to an offset/size. No crypto.

**Practical consequence:** the config is already mineable for balance numbers, achievement
IDs, keybinds and map defaults. Asset extraction is now a pure *format* problem (a decoder),
not a *key* problem.

Also recovered from the pak config, and useful for the acceptance test:

- `SteamDevAppId=1799840`
- **35 achievement ids**, which are the game's own definition of completion:
  `cha_All_Complete`, `cha_Area_{Aqr,Lib,Maus,Aut,Castle,Prison,Forest,Swamp,ROS,SG}`,
  `cha_Quest_{Raider,Fish,Lib,Meat,Books,Vaults,Chef,Science,Shop_Spend,PG_Puzzle,Shop_Unlocks,Mushroom,Skulls,Grimoire}`,
  `cha_Item_{FlyingSword,SecretSword,SpecSword,UltraGem,AllDashGems,UpgradeAllWeps,GotAllWeps,GotAllThrowables,GotAllAcc,GotAllTreasures}`

Note the achievement ids use a slightly different spelling from the asset names
(`cha_Area_Aqr` vs asset `area_AQ`, `cha_Item_GotAllWeps`), so an acceptance test must key
on the **save's** strings, not the asset filenames.
