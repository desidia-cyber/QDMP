// qdjoin.h -- the JOIN RECONCILE: when the pair connects, the peer adopts the host's
// world/progression state exactly, including REVERTING what the peer holds and the host
// does not.
//
// THE GAP THIS CLOSES
// -------------------
// Every existing lane makes its own thing converge, and most of them are MONOTONE: the
// flag bank never lowers (`flags::FlagBank::ApplyRemote` refuses a lower value) and the
// placed-object lane never lowers either (`objects::ApplyAllowed` returns false for
// `hostState == 0`).  That is correct while the pair plays -- a rise is a shared fact and
// a crossing heartbeat must not undo a partner's action.  It is WRONG at the moment two
// saves meet.  A peer that loads a save which is AHEAD of the host (a story flag the host
// never set, a door the host's save says is shut, an enemy the host's save says is alive,
// a key item the host does not have, a teleporter the host never unlocked) keeps all of
// it forever: no lane can lower a value, so the two games stay different for the whole
// session and the desync auditor reports the same items every minute.
//
// The rule at a join is the opposite of the rule in play: the HOST is absolute, and the
// peer must match it in BOTH directions.  That rule is the one thing this module adds.  It
// is a controlled PHASE, not a lane: it runs once per session establishment, brackets the
// other lanes' echoes while it writes, backs the peer's save up first, and then lets the
// normal lanes run from a state that already agrees.
//
// WHAT IS PURE HERE
// -----------------
// No socket, no UObject, no clock, no allocation.  The diff/plan and the wire encoding are
// plain data and are exercised by `test/test_join.cpp`; proxy.cpp / qdjoin_engine.h perform
// the engine calls.  This is the same split qdaudit.h uses, and for the same reason: the
// part that goes wrong silently is which keys are compared, in which direction, and what
// is excluded -- and that part is testable without a game.
#pragma once
#include <stdint.h>
#include <string.h>

#include "qdwire.h"          // wire::Writer / Reader, K_JOIN_* (0x68-0x6B)
#include "qdworldset.h"      // the per-player exclusion list and the delta counters
#include "qdprogression.h"   // IsQuestItemPath: which inventory entries are shared

namespace qd {
namespace join {

// ---------------------------------------------------------------------------
// Categories.  THE ORDER IS THE WIRE ORDER (`cat` is a byte) and must not be
// reordered without a version bump: both machines must agree which byte means what.
// ---------------------------------------------------------------------------
enum Cat : uint8_t {
    JC_NONE     = 0,
    JC_FLAGS    = 1,   // NamedSaveData           {str name, i32 value}
    JC_ACTORS   = 2,   // SavableActorStates      {guid16, i32 value}
    JC_OBJECTS  = 3,   // placed world objects    {key16, u8 shape, u8 state}
    JC_ENEMY    = 4,   // EnemySaveStates DEAD    {guid16}
    JC_QUEST    = 5,   // key items only          {str path, i32 count}
    JC_WORLDSET = 6,   // the three save arrays   (sub = set id) {str key}
    JC_PUZZLE   = 7,   // puzzle prop poses       {key16, i16 yaw, i16 pitch, u8 state, u32 seq}
    JC_MAP      = 8,   // the current world/area  {str world, str area, i32 px,py,pz}
    JC_COUNT    = 9
};

inline const char* CatName(uint8_t cat)
{
    switch (cat) {
    case JC_FLAGS:    return "flags";
    case JC_ACTORS:   return "actors";
    case JC_OBJECTS:  return "objects";
    case JC_ENEMY:    return "enemy";
    case JC_QUEST:    return "quest";
    case JC_WORLDSET: return "worldset";
    case JC_PUZZLE:   return "puzzle";
    case JC_MAP:      return "map";
    default:          return "?";
    }
}

inline bool CatValid(uint8_t cat) { return cat >= JC_FLAGS && cat <= JC_MAP; }

// ---------------------------------------------------------------------------
// Bounds.  Fixed, because the DLL is /MT and nothing on the game thread may allocate.
// Overflow is REPORTED by the caller, never truncated in silence.
// ---------------------------------------------------------------------------
static const int kMaxFlags   = 512;    // above the 58-key enumerated universe + headroom
static const int kFlagNameCap = 64;
static const int kMaxActors  = 512;    // actorstate::kMaxEntries
static const int kMaxObjects = 256;    // objects::kMaxEntries
static const int kMaxEnemy   = 1024;   // the enemy record table's own bound
static const int kMaxQuest   = 256;    // key items actually held, not the 503-asset universe
static const int kQuestPathCap = 200;  // inventory::kMaxPath
static const int kMaxSetKeys = 128;    // worldset::kMaxEntries
static const int kSetKeyCap  = worldset::kKeyCap;
static const int kMaxPuzzle  = 512;    // puzzle::kMaxProps
static const int kMapNameCap = 64;

// ---------------------------------------------------------------------------
// Rows.  Each is exactly one wire row, so the diff, the plan and the encoder cannot drift.
// ---------------------------------------------------------------------------
struct FlagRow {
    char    name[kFlagNameCap];
    int32_t value;
};

struct ActorRow {
    uint32_t guid[4];
    int32_t  value;
};

struct ObjectRow {
    uint32_t key[4];
    uint8_t  shape;
    uint8_t  state;
};

struct KeyRow {
    uint32_t guid[4];
};

struct QuestRow {
    char    path[kQuestPathCap];
    int32_t count;
};

struct SetRow {
    uint8_t set;                 // worldset::SetId
    char    key[kSetKeyCap];
};

struct PuzzleRow {
    uint32_t key[4];
    int16_t  yaw;
    int16_t  pitch;
    uint8_t  state;
    uint32_t seq;
};

struct MapRow {
    char    world[kMapNameCap];
    char    area[kMapNameCap];
    int32_t px, py, pz;
};

// ---------------------------------------------------------------------------
// A whole side's state.  The host builds one; the peer builds one from its own game and
// also accumulates the host's into a second.  The shapes are shared so the diff can compare
// like with like and the wire encoding is one function.
// ---------------------------------------------------------------------------
struct State {
    int      nFlags;
    FlagRow  flags[kMaxFlags];
    bool     flagsOverflow;
    int      nActors;
    ActorRow actors[kMaxActors];
    bool     actorsOverflow;
    int      nObjects;
    ObjectRow objects[kMaxObjects];
    bool     objectsOverflow;
    int      nEnemy;
    KeyRow   enemy[kMaxEnemy];
    bool     enemyOverflow;
    int      nQuest;
    QuestRow quest[kMaxQuest];
    bool     questOverflow;
    int      nSet[worldset::SET_COUNT];
    char     set[worldset::SET_COUNT][kMaxSetKeys][kSetKeyCap];
    bool     setOverflow[worldset::SET_COUNT];
    int      nPuzzle;
    PuzzleRow puzzle[kMaxPuzzle];
    bool     puzzleOverflow;
    MapRow   map;
    uint8_t  sentMask;           // categories the SENDER actually carried (bit cat-1)
    bool     haveMap;

    void Reset()
    {
        nFlags = nActors = nObjects = nEnemy = nQuest = nPuzzle = 0;
        flagsOverflow = actorsOverflow = objectsOverflow = enemyOverflow = false;
        questOverflow = puzzleOverflow = false;
        for (int i = 0; i < worldset::SET_COUNT; ++i) {
            nSet[i] = 0;
            setOverflow[i] = false;
            for (int k = 0; k < kMaxSetKeys; ++k) set[i][k][0] = 0;
        }
        for (int i = 0; i < kMaxFlags; ++i) flags[i].name[0] = 0;
        for (int i = 0; i < kMaxQuest; ++i) quest[i].path[0] = 0;
        memset(&map, 0, sizeof(map));
        sentMask = 0;
        haveMap = false;
    }

    // ---- adders.  Return false when the table is full; the caller reports the overflow. ----
    bool AddFlag(const char* name, int32_t value)
    {
        if (!name || !name[0]) return false;
        if (nFlags >= kMaxFlags) { flagsOverflow = true; return false; }
        CopyText(flags[nFlags].name, kFlagNameCap, name);
        flags[nFlags].value = value;
        ++nFlags;
        return true;
    }
    bool AddActor(const uint32_t g[4], int32_t value)
    {
        if (nActors >= kMaxActors) { actorsOverflow = true; return false; }
        for (int i = 0; i < 4; ++i) actors[nActors].guid[i] = g[i];
        actors[nActors].value = value;
        ++nActors;
        return true;
    }
    bool AddObject(const uint32_t k[4], uint8_t shape, uint8_t state)
    {
        if (nObjects >= kMaxObjects) { objectsOverflow = true; return false; }
        for (int i = 0; i < 4; ++i) objects[nObjects].key[i] = k[i];
        objects[nObjects].shape = shape;
        objects[nObjects].state = state;
        ++nObjects;
        return true;
    }
    bool AddEnemy(const uint32_t g[4])
    {
        if (nEnemy >= kMaxEnemy) { enemyOverflow = true; return false; }
        for (int i = 0; i < 4; ++i) enemy[nEnemy].guid[i] = g[i];
        ++nEnemy;
        return true;
    }
    bool AddQuest(const char* path, int32_t count)
    {
        if (!path || !path[0]) return false;
        if (nQuest >= kMaxQuest) { questOverflow = true; return false; }
        CopyText(quest[nQuest].path, kQuestPathCap, path);
        quest[nQuest].count = count;
        ++nQuest;
        return true;
    }
    bool AddSet(uint8_t setId, const char* key)
    {
        if (!worldset::SetValid(setId) || !key || !key[0]) return false;
        if (nSet[setId] >= kMaxSetKeys) { setOverflow[setId] = true; return false; }
        CopyText(set[setId][nSet[setId]], kSetKeyCap, key);
        ++nSet[setId];
        return true;
    }
    bool AddPuzzle(const PuzzleRow& r)
    {
        if (nPuzzle >= kMaxPuzzle) { puzzleOverflow = true; return false; }
        puzzle[nPuzzle] = r;
        ++nPuzzle;
        return true;
    }

    void SetMap(const char* world, const char* area, int32_t px, int32_t py, int32_t pz)
    {
        CopyText(map.world, kMapNameCap, world ? world : "");
        CopyText(map.area, kMapNameCap, area ? area : "");
        map.px = px; map.py = py; map.pz = pz;
        haveMap = map.world[0] != 0;
    }

    // ---- lookups ----
    const FlagRow* FindFlag(const char* name) const
    {
        for (int i = 0; i < nFlags; ++i)
            if (TextEq(flags[i].name, name)) return &flags[i];
        return nullptr;
    }
    const ActorRow* FindActor(const uint32_t g[4]) const
    {
        for (int i = 0; i < nActors; ++i)
            if (GuidEq(actors[i].guid, g)) return &actors[i];
        return nullptr;
    }
    const ObjectRow* FindObject(const uint32_t k[4]) const
    {
        for (int i = 0; i < nObjects; ++i)
            if (GuidEq(objects[i].key, k)) return &objects[i];
        return nullptr;
    }
    bool HasEnemy(const uint32_t g[4]) const
    {
        for (int i = 0; i < nEnemy; ++i)
            if (GuidEq(enemy[i].guid, g)) return true;
        return false;
    }
    const QuestRow* FindQuest(const char* path) const
    {
        for (int i = 0; i < nQuest; ++i)
            if (TextEq(quest[i].path, path)) return &quest[i];
        return nullptr;
    }
    bool HasSet(uint8_t setId, const char* key) const
    {
        if (!worldset::SetValid(setId)) return false;
        for (int i = 0; i < nSet[setId]; ++i)
            if (TextEq(set[setId][i], key)) return true;
        return false;
    }
    const PuzzleRow* FindPuzzle(const uint32_t k[4]) const
    {
        for (int i = 0; i < nPuzzle; ++i)
            if (GuidEq(puzzle[i].key, k)) return &puzzle[i];
        return nullptr;
    }

    // ---- text / id helpers (no CRT dependency on the game thread) ----
    static bool TextEq(const char* a, const char* b)
    {
        if (!a || !b) return a == b;
        for (int i = 0; a[i] || b[i]; ++i) if (a[i] != b[i]) return false;
        return true;
    }
    static int CopyText(char* dst, int cap, const char* src)
    {
        if (!dst || cap <= 0) return 0;
        int i = 0;
        if (src) for (; src[i] && i < cap - 1; ++i) dst[i] = src[i];
        dst[i] = 0;
        return i;
    }
    static bool GuidEq(const uint32_t a[4], const uint32_t b[4])
    {
        return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
    }
    static bool GuidZero(const uint32_t g[4])
    {
        return (g[0] | g[1] | g[2] | g[3]) == 0;
    }
};

// ---------------------------------------------------------------------------
// WHICH FLAGS ARE NOT SHARED, AND WHY.  This is the exclusion list the task asks for; it
// is one predicate per direction so the push side, the apply side and this diff cannot
// disagree about the same key.
// ---------------------------------------------------------------------------

// A shop purchase is a per-player act: `ShopPurchasedKey/*` consumes the buyer's stock,
// `ShopPurchases`/`ShopSales` are the buyer's own logs, `NewItemsInShop` is the buyer's
// badge.  Buying the last unique relic must not empty the partner's shelf.  This is the
// list `features/gap-world.md` §4 measured; qdworldset.h owns it.
inline bool FlagStaysLocal(const char* name) { return worldset::FlagStaysLocal(name); }

// A counter whose truth is the SUM of two machines' distinct acts (a chest each player
// opened is applied here WITHOUT its payout, by design).  Adopting the host's value would
// discard the peer's own act, so the reconcile DONATES the excess to the host first and
// then adopts the host's resulting value.  qdworldset.h owns the list.
inline bool FlagIsDelta(const char* name) { return worldset::IsUnionCounter(name); }

inline bool HasPrefix(const char* s, const char* prefix)
{
    if (!s || !prefix) return false;
    int i = 0;
    for (; prefix[i]; ++i) if (s[i] != prefix[i]) return false;
    return true;
}

// A per-player collection counter: each player earns their own, and the achievement is
// judged on that player's own collection.  The reconcile may RAISE a peer to a host that
// is ahead (a shared goal already met is harmless) but never LOWERS one, because a
// lowered counter is a trophy the player loses.  Documented in join-sync.md.
inline bool FlagMonotoneOnly(const char* name)
{
    if (!name || !name[0]) return false;
    if (HasPrefix(name, "TotalAchievement")) return true;
    static const char* const kExact[] = {
        "CompletedChallenges",
        "FullEnergyCellsCount",
        "TiniScytheHammerAP",
        "IntroMusicState",
    };
    for (unsigned i = 0; i < sizeof(kExact) / sizeof(kExact[0]); ++i)
        if (State::TextEq(name, kExact[i])) return true;
    return false;
}

// ---------------------------------------------------------------------------
// The plan: the rows the PEER must change to match the host, with the direction.
// ---------------------------------------------------------------------------
enum Act : uint8_t {
    A_NONE   = 0,
    A_ADOPT  = 1,   // write the host's value / add the host's key
    A_REVERT = 2,   // undo the peer's value / remove the peer's key
    A_DONATE = 3,   // a delta counter the peer holds above the host: give the excess away
    A_KILL   = 4,   // an enemy dead on the host and alive on the peer
    A_REVIVE = 5,   // an enemy dead on the peer and alive on the host
};

struct FlagOp {
    char    name[kFlagNameCap];
    int32_t from;    // the peer's value (0 when absent)
    int32_t to;      // the host's value
    uint8_t act;
};

struct ActorOp {
    uint32_t guid[4];
    int32_t  from;
    int32_t  to;
    uint8_t  act;
};

struct ObjectOp {
    uint32_t key[4];
    uint8_t  shape;
    uint8_t  from;
    uint8_t  to;
    uint8_t  act;
};

struct EnemyOp {
    uint32_t guid[4];
    uint8_t  act;
};

struct QuestOp {
    char    path[kQuestPathCap];
    int32_t from;
    int32_t to;
    uint8_t act;
};

struct SetOp {
    uint8_t set;
    char    key[kSetKeyCap];
    uint8_t act;
};

struct PuzzleOp {
    PuzzleRow row;
};

// The travel instruction, decided from the two map rows.  `kind` says which existing travel
// entry point the engine must drive; this header never calls it.
enum TravelKind : uint8_t {
    TV_NONE   = 0,
    TV_WORLD  = 1,   // a hard UWorld switch: FollowHardTravel(world)
    TV_AREA   = 2,   // a streamed sublevel: FollowHardTravel(area) (streams it)
    TV_LEAVE  = 3,   // the host has no area and the peer is pinned in one: ClearAreaPin
};

struct Plan {
    int      nFlag;      FlagOp   flag[kMaxFlags];
    int      nActor;     ActorOp  actor[kMaxActors];
    int      nObject;    ObjectOp object[kMaxObjects];
    int      nEnemy;     EnemyOp  enemy[kMaxEnemy];
    int      nQuest;     QuestOp  quest[kMaxQuest];
    int      nSet;       SetOp    set[worldset::SET_COUNT * kMaxSetKeys];
    int      nPuzzle;    PuzzleOp puzzle[kMaxPuzzle];
    uint8_t  travel;
    char     travelTo[kMapNameCap];
    bool     overflow;

    void Reset()
    {
        nFlag = nActor = nObject = nEnemy = nQuest = nSet = nPuzzle = 0;
        travel = TV_NONE;
        travelTo[0] = 0;
        overflow = false;
    }

    bool Any() const
    {
        return nFlag || nActor || nObject || nEnemy || nQuest || nSet || nPuzzle ||
               travel != TV_NONE;
    }

    // The counts the summary line uses ("E enemies revived/killed", etc.).
    int EnemyRevived() const
    {
        int n = 0;
        for (int i = 0; i < nEnemy; ++i) if (enemy[i].act == A_REVIVE) ++n;
        return n;
    }
    int EnemyKilled() const
    {
        int n = 0;
        for (int i = 0; i < nEnemy; ++i) if (enemy[i].act == A_KILL) ++n;
        return n;
    }
    int ItemsAdded() const
    {
        int n = 0;
        for (int i = 0; i < nQuest; ++i) if (quest[i].act == A_ADOPT) ++n;
        return n;
    }
    int ItemsRemoved() const
    {
        int n = 0;
        for (int i = 0; i < nQuest; ++i) if (quest[i].act == A_REVERT) ++n;
        return n;
    }
};

// ---------------------------------------------------------------------------
// BuildPlan -- the whole reconciliation decision, with no engine involved.
// ---------------------------------------------------------------------------
// `host` is what the host sent, `local` is what this peer's own game holds.  The result is
// the exact list of changes, so the caller can log the counts before it writes anything
// and so a test can assert the direction of every category.
//
// Rules, one per category:
//   flags    shared: adopt the host exactly (set OR revert).  Delta counters: donate the
//            peer's excess and adopt the host.  Per-player counters: adopt only upward.
//            Shop stock: never touched.
//   actors   the host's container is save-wide, so absence means 0: adopt a differing value
//            AND revert a peer entry the host does not hold.
//   objects  the host's set is world-scoped (the host's scan sees only ITS loaded levels),
//            so a peer-only object is NOT a divergence: only a key present on both is
//            compared, and it is adopted in either direction (a door can close).
//   enemy    the host's DEAD keys vs the peer's DEAD keys: a key dead only here is revived,
//            a key dead only there is killed.  Applied as one reset (see the engine).
//   quest    key items only: add what the peer lacks, remove what the host lacks.
//   worldsets the host's set is exact: add what the peer lacks, remove what the host lacks.
//   puzzle   a pose the peer holds differently is adopted; a prop the peer does not hold is
//            not invented.
//   map      a different world/area is followed through the existing travel entry point.
inline void BuildPlan(const State& host, const State& local, Plan* out)
{
    if (!out) return;
    out->Reset();

    // ---- flags ----
    for (int i = 0; i < host.nFlags && out->nFlag < kMaxFlags; ++i) {
        const FlagRow& h = host.flags[i];
        if (!h.name[0]) continue;
        if (FlagStaysLocal(h.name)) continue;
        const FlagRow* l = local.FindFlag(h.name);
        int32_t lv = l ? l->value : 0;
        const bool present = (l != nullptr);
        if (present && lv == h.value) continue;

        uint8_t act;
        int32_t to = h.value;
        if (!present) {
            // A key the peer lacks.  For a delta counter, absence means "contributed
            // nothing", so adopting the host's total is right.  For a per-player counter,
            // absent is 0 and the host's value is an adoption (raise).
            act = A_ADOPT;
        } else if (FlagIsDelta(h.name)) {
            // The pair's truth is a SUM.  A peer above the host keeps its value and hands
            // the host the difference; a peer below adopts the host.
            if (lv > h.value) { act = A_DONATE; to = lv - h.value; }
            else { act = A_ADOPT; to = h.value; }
        } else if (FlagMonotoneOnly(h.name)) {
            if (h.value > lv) act = A_ADOPT;
            else continue;                    // never lower a per-player counter
        } else {
            act = (h.value < lv) ? A_REVERT : A_ADOPT;
        }
        FlagOp& o = out->flag[out->nFlag++];
        State::CopyText(o.name, kFlagNameCap, h.name);
        o.from = lv;
        o.to = to;
        o.act = act;
    }
    // A shared flag the host does NOT hold is a fact: the authority has no such key, so the
    // peer's key is reverted to absent (written as 0, which is what the game's getter
    // reports).  Delta and per-player keys are left alone: absence on the host is the other
    // machine's own act not yet applied, not a fact about this one.
    for (int i = 0; i < local.nFlags && out->nFlag < kMaxFlags; ++i) {
        const FlagRow& l = local.flags[i];
        if (!l.name[0]) continue;
        if (FlagStaysLocal(l.name) || FlagIsDelta(l.name) || FlagMonotoneOnly(l.name)) continue;
        if (host.FindFlag(l.name)) continue;
        if (l.value == 0) continue;           // absent already reads as 0
        FlagOp& o = out->flag[out->nFlag++];
        State::CopyText(o.name, kFlagNameCap, l.name);
        o.from = l.value;
        o.to = 0;
        o.act = A_REVERT;
    }

    // ---- savable actor states ----
    for (int i = 0; i < host.nActors && out->nActor < kMaxActors; ++i) {
        const ActorRow& h = host.actors[i];
        const ActorRow* l = local.FindActor(h.guid);
        int32_t lv = l ? l->value : 0;
        if (l && lv == h.value) continue;
        if (!l && h.value == 0) continue;     // absent == 0: nothing to change
        ActorOp& o = out->actor[out->nActor++];
        for (int k = 0; k < 4; ++k) o.guid[k] = h.guid[k];
        o.from = lv;
        o.to = h.value;
        o.act = (h.value < lv) ? A_REVERT : A_ADOPT;
    }
    for (int i = 0; i < local.nActors && out->nActor < kMaxActors; ++i) {
        const ActorRow& l = local.actors[i];
        if (host.FindActor(l.guid)) continue;
        if (l.value == 0) continue;
        ActorOp& o = out->actor[out->nActor++];
        for (int k = 0; k < 4; ++k) o.guid[k] = l.guid[k];
        o.from = l.value;
        o.to = 0;
        o.act = A_REVERT;
    }

    // ---- placed world objects ----
    for (int i = 0; i < host.nObjects && out->nObject < kMaxObjects; ++i) {
        const ObjectRow& h = host.objects[i];
        const ObjectRow* l = local.FindObject(h.key);
        if (!l) continue;                     // not loaded here: not a divergence
        if (l->state == h.state) continue;
        ObjectOp& o = out->object[out->nObject++];
        for (int k = 0; k < 4; ++k) o.key[k] = h.key[k];
        o.shape = h.shape;
        o.from = l->state;
        o.to = h.state;
        o.act = (h.state < l->state) ? A_REVERT : A_ADOPT;
    }

    // ---- enemy deaths ----
    for (int i = 0; i < host.nEnemy && out->nEnemy < kMaxEnemy; ++i) {
        const KeyRow& h = host.enemy[i];
        if (local.HasEnemy(h.guid)) continue;
        EnemyOp& o = out->enemy[out->nEnemy++];
        for (int k = 0; k < 4; ++k) o.guid[k] = h.guid[k];
        o.act = A_KILL;
    }
    for (int i = 0; i < local.nEnemy && out->nEnemy < kMaxEnemy; ++i) {
        const KeyRow& l = local.enemy[i];
        if (host.HasEnemy(l.guid)) continue;
        EnemyOp& o = out->enemy[out->nEnemy++];
        for (int k = 0; k < 4; ++k) o.guid[k] = l.guid[k];
        o.act = A_REVIVE;
    }

    // ---- quest / key items ----
    for (int i = 0; i < host.nQuest && out->nQuest < kMaxQuest; ++i) {
        const QuestRow& h = host.quest[i];
        if (!progression::IsQuestItemPath(h.path)) continue;
        const QuestRow* l = local.FindQuest(h.path);
        int32_t lv = l ? l->count : 0;
        if (lv == h.count) continue;
        QuestOp& o = out->quest[out->nQuest++];
        State::CopyText(o.path, kQuestPathCap, h.path);
        o.from = lv;
        o.to = h.count;
        o.act = (h.count < lv) ? A_REVERT : A_ADOPT;
    }
    for (int i = 0; i < local.nQuest && out->nQuest < kMaxQuest; ++i) {
        const QuestRow& l = local.quest[i];
        if (!progression::IsQuestItemPath(l.path)) continue;
        if (host.FindQuest(l.path)) continue;
        if (l.count <= 0) continue;
        QuestOp& o = out->quest[out->nQuest++];
        State::CopyText(o.path, kQuestPathCap, l.path);
        o.from = l.count;
        o.to = 0;
        o.act = A_REVERT;
    }

    // ---- world sets ----
    for (uint8_t s = worldset::SET_TELEPORTERS; s < worldset::SET_COUNT; ++s) {
        for (int i = 0; i < host.nSet[s]; ++i) {
            const char* k = host.set[s][i];
            if (!k[0]) continue;
            if (local.HasSet(s, k)) continue;
            if (out->nSet >= worldset::SET_COUNT * kMaxSetKeys) { out->overflow = true; break; }
            SetOp& o = out->set[out->nSet++];
            o.set = s;
            State::CopyText(o.key, kSetKeyCap, k);
            o.act = A_ADOPT;
        }
        for (int i = 0; i < local.nSet[s]; ++i) {
            const char* k = local.set[s][i];
            if (!k[0]) continue;
            if (host.HasSet(s, k)) continue;
            if (out->nSet >= worldset::SET_COUNT * kMaxSetKeys) { out->overflow = true; break; }
            SetOp& o = out->set[out->nSet++];
            o.set = s;
            State::CopyText(o.key, kSetKeyCap, k);
            o.act = A_REVERT;
        }
    }

    // ---- puzzle poses ----
    for (int i = 0; i < host.nPuzzle && out->nPuzzle < kMaxPuzzle; ++i) {
        const PuzzleRow& h = host.puzzle[i];
        const PuzzleRow* l = local.FindPuzzle(h.key);
        if (!l) continue;                     // not loaded/observed here
        if (l->yaw == h.yaw && l->pitch == h.pitch && l->state == h.state) continue;
        out->puzzle[out->nPuzzle++].row = h;
    }

    // ---- the map / area ----
    if (host.haveMap && host.map.world[0]) {
        if (!local.haveMap || !State::TextEq(local.map.world, host.map.world)) {
            out->travel = TV_WORLD;
            State::CopyText(out->travelTo, kMapNameCap, host.map.world);
        } else if (!State::TextEq(local.map.area, host.map.area)) {
            if (host.map.area[0]) {
                out->travel = TV_AREA;
                State::CopyText(out->travelTo, kMapNameCap, host.map.area);
            } else {
                out->travel = TV_LEAVE;
                out->travelTo[0] = 0;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Wire (WIRE.md 3.35).  Kinds 0x68-0x6B.
// ---------------------------------------------------------------------------
//
//   JOIN_REQ    peer -> host   seq u32 | cat u8              (0 = every category)
//   JOIN_STATE  host -> peer   seq u32 | cat u8 | sub u8 | total u16 | first u16 | rows...
//   JOIN_END    host -> peer   seq u32 | mask u8             (bit cat-1 = sent)
//   JOIN_ACK    peer -> host   seq u32 | result u8           (see AckResult)
//
// The STATE body continues across ticks with a `first` CURSOR, never a fragment (the rule
// every container in this tree follows).  It is sent UNRELIABLE and repeated until the peer
// acks: a lost chunk costs one retry pass, and a 150 KB state never occupies a reliable
// slot.  END is the host saying "the cursor finished"; the peer only applies when every
// category the mask announces completed, so a partial state is never written.
static const int  kStateHeaderBytes = 4 + 1 + 1 + 2 + 2;   // 10

struct StateHeader {
    uint32_t seq;
    uint8_t  cat;
    uint8_t  sub;
    uint16_t total;
    uint16_t first;
};

inline void WriteJoinReq(wire::Writer& w, uint32_t seq, uint8_t cat)
{
    w.U32(seq);
    w.U8(cat);
}

inline bool ReadJoinReq(wire::Reader& r, uint32_t* seq, uint8_t* cat)
{
    *seq = r.U32();
    *cat = r.U8();
    if (!r.ok) return false;
    return *cat == 0 || CatValid(*cat);
}

inline void WriteStateHeader(wire::Writer& w, const StateHeader& h)
{
    w.U32(h.seq);
    w.U8(h.cat);
    w.U8(h.sub);
    w.U16(h.total);
    w.U16(h.first);
}

inline bool ReadStateHeader(wire::Reader& r, StateHeader* h)
{
    h->seq   = r.U32();
    h->cat   = r.U8();
    h->sub   = r.U8();
    h->total = r.U16();
    h->first = r.U16();
    if (!r.ok) return false;
    if (!CatValid(h->cat)) return false;
    if (h->cat == JC_WORLDSET && !worldset::SetValid(h->sub)) return false;
    return true;
}

inline void WriteJoinEnd(wire::Writer& w, uint32_t seq, uint8_t mask)
{
    w.U32(seq);
    w.U8(mask);
}

inline bool ReadJoinEnd(wire::Reader& r, uint32_t* seq, uint8_t* mask)
{
    *seq = r.U32();
    *mask = r.U8();
    return r.ok;
}

// What the peer says it did.  `in sync` is the "nothing to do" answer the log quotes; a
// `deferred` means the state arrived but the game could not be written (no active save, a
// load in flight) and the peer will ask again.
enum AckResult : uint8_t {
    AK_IN_SYNC   = 0,
    AK_RECONCILED = 1,
    AK_DEFERRED  = 2,
    AK_BACKUP_FAILED = 3,
};

inline void WriteJoinAck(wire::Writer& w, uint32_t seq, uint8_t result)
{
    w.U32(seq);
    w.U8(result);
}

inline bool ReadJoinAck(wire::Reader& r, uint32_t* seq, uint8_t* result)
{
    *seq = r.U32();
    *result = r.U8();
    return r.ok;
}

// ---------------------------------------------------------------------------
// Rows: how many, and the chunk writer/reader
// ---------------------------------------------------------------------------
// 10,000-foot rule for every row: a row is written WHOLE or not at all, and a receiver reads
// until the body ends.  A body cut inside a row is a framing error the reader refuses; the
// caller's cursor then re-sends that row.
inline int TotalRows(const State& s, uint8_t cat, uint8_t sub)
{
    switch (cat) {
    case JC_FLAGS:    return s.nFlags;
    case JC_ACTORS:   return s.nActors;
    case JC_OBJECTS:  return s.nObjects;
    case JC_ENEMY:    return s.nEnemy;
    case JC_QUEST:    return s.nQuest;
    case JC_WORLDSET: return worldset::SetValid(sub) ? s.nSet[sub] : 0;
    case JC_PUZZLE:   return s.nPuzzle;
    case JC_MAP:      return s.haveMap ? 1 : 0;
    default:          return 0;
    }
}

inline int TextLen(const char* s)
{
    int n = 0;
    if (s) while (s[n]) ++n;
    return n;
}

inline int RowBytes(uint8_t cat, const State& s, uint8_t sub, int i)
{
    switch (cat) {
    case JC_FLAGS:    return 2 + TextLen(s.flags[i].name) + 4;
    case JC_ACTORS:   return 20;
    case JC_OBJECTS:  return 18;
    case JC_ENEMY:    return 16;
    case JC_QUEST:    return 2 + TextLen(s.quest[i].path) + 4;
    case JC_WORLDSET: return 2 + TextLen(s.set[sub][i]);
    case JC_PUZZLE:   return 16 + 2 + 2 + 1 + 4;
    case JC_MAP:      return 2 + TextLen(s.map.world) + 2 + TextLen(s.map.area) + 12;
    default:          return 0;
    }
}

inline void WriteOneRow(wire::Writer& w, uint8_t cat, const State& s, uint8_t sub, int i)
{
    switch (cat) {
    case JC_FLAGS: {
        w.Str(s.flags[i].name);
        w.I32(s.flags[i].value);
        break;
    }
    case JC_ACTORS: {
        w.Guid(s.actors[i].guid);
        w.I32(s.actors[i].value);
        break;
    }
    case JC_OBJECTS: {
        w.Guid(s.objects[i].key);
        w.U8(s.objects[i].shape);
        w.U8(s.objects[i].state);
        break;
    }
    case JC_ENEMY: {
        w.Guid(s.enemy[i].guid);
        break;
    }
    case JC_QUEST: {
        w.Str(s.quest[i].path);
        w.I32(s.quest[i].count);
        break;
    }
    case JC_WORLDSET: {
        w.Str(s.set[sub][i]);
        break;
    }
    case JC_PUZZLE: {
        const PuzzleRow& r = s.puzzle[i];
        w.Guid(r.key);
        w.I16(r.yaw);
        w.I16(r.pitch);
        w.U8(r.state);
        w.U32(r.seq);
        break;
    }
    case JC_MAP: {
        w.Str(s.map.world);
        w.Str(s.map.area);
        w.I32(s.map.px);
        w.I32(s.map.py);
        w.I32(s.map.pz);
        break;
    }
    default:
        break;
    }
}

// Writes as many rows as fit starting at `first`; returns the first index NOT written.
inline int WriteRows(wire::Writer& w, const State& s, uint8_t cat, uint8_t sub, int first)
{
    int total = TotalRows(s, cat, sub);
    int i = first < 0 ? 0 : first;
    if (i > total) i = total;
    for (; i < total; ++i) {
        int need = RowBytes(cat, s, sub, i);
        if (need <= 0) break;
        if (w.len + need > w.cap) break;
        WriteOneRow(w, cat, s, sub, i);
        if (!w.ok) break;
    }
    return i;
}

// Reads rows until the body ends, appending to `s`.  Returns the number read, or -1 on a
// malformed body (the caller then refuses the whole state rather than applying a prefix).
inline bool objects_shape_valid(uint8_t s) { return s <= 5; }

inline int ReadRows(wire::Reader& r, uint8_t cat, uint8_t sub, State* s)
{
    int n = 0;
    while (r.ok && r.pos < r.len) {
        switch (cat) {
        case JC_FLAGS: {
            char name[kFlagNameCap];
            if (r.Str(name, sizeof(name)) < 0) return -1;
            int32_t v = r.I32();
            if (!r.ok) return -1;
            if (!name[0]) return -1;
            if (s->FindFlag(name)) return -1;          // duplicate: a framing error
            s->AddFlag(name, v);
            break;
        }
        case JC_ACTORS: {
            uint32_t g[4];
            r.Guid(g);
            int32_t v = r.I32();
            if (!r.ok) return -1;
            if (s->FindActor(g)) return -1;
            s->AddActor(g, v);
            break;
        }
        case JC_OBJECTS: {
            uint32_t k[4];
            r.Guid(k);
            uint8_t shape = r.U8();
            uint8_t state = r.U8();
            if (!r.ok) return -1;
            if (!objects_shape_valid(shape)) return -1;
            if (state > 1) return -1;
            if (s->FindObject(k)) return -1;
            s->AddObject(k, shape, state);
            break;
        }
        case JC_ENEMY: {
            uint32_t g[4];
            r.Guid(g);
            if (!r.ok) return -1;
            if (State::GuidZero(g)) return -1;
            if (s->HasEnemy(g)) continue;              // a repeat inside one chunk
            s->AddEnemy(g);
            break;
        }
        case JC_QUEST: {
            char path[kQuestPathCap];
            if (r.Str(path, sizeof(path)) < 0) return -1;
            int32_t v = r.I32();
            if (!r.ok) return -1;
            if (!path[0] || v < 0) return -1;
            if (s->FindQuest(path)) return -1;
            s->AddQuest(path, v);
            break;
        }
        case JC_WORLDSET: {
            char key[kSetKeyCap];
            if (r.Str(key, sizeof(key)) < 0) return -1;
            if (!r.ok) return -1;
            if (!worldset::PlausibleKey(key)) return -1;
            if (s->HasSet(sub, key)) continue;
            s->AddSet(sub, key);
            break;
        }
        case JC_PUZZLE: {
            PuzzleRow row;
            memset(&row, 0, sizeof(row));
            r.Guid(row.key);
            row.yaw = r.I16();
            row.pitch = r.I16();
            row.state = r.U8();
            row.seq = r.U32();
            if (!r.ok) return -1;
            if (State::GuidZero(row.key)) return -1;
            if (s->FindPuzzle(row.key)) return -1;
            s->AddPuzzle(row);
            break;
        }
        case JC_MAP: {
            char world[kMapNameCap], area[kMapNameCap];
            if (r.Str(world, sizeof(world)) < 0) return -1;
            if (r.Str(area, sizeof(area)) < 0) return -1;
            int32_t px = r.I32(), py = r.I32(), pz = r.I32();
            if (!r.ok) return -1;
            if (!world[0]) return -1;
            s->SetMap(world, area, px, py, pz);
            break;
        }
        default:
            return -1;
        }
        ++n;
    }
    return r.ok ? n : -1;
}

// The shape byte is `objects::Shape`; this header must not pull qdobjectstate.h in (that
// would add the whole hash/scan vocabulary for one bounds test), so the bound is stated
// once above and a test asserts it matches.

} // namespace join
} // namespace qd
