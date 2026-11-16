// qdworld_engine.h -- the engine half of [gap-world]: the save-side unlock sets, a rest as
// a world event, the puzzle props whose state is a transform, and the two flag rules.
//
// Included at the END of proxy.cpp (beside qdrespawn_engine.h) because it uses the
// reflection, save, enemy-table and socket helpers that live there. The RULES it applies
// are in qdworldset.h / qdrest.h / qdpuzzle.h, which are pure and unit-tested; this file is
// only the part that touches the game.
//
// WHAT IS MEASURED HERE AND WHAT IS INFERRED
// ------------------------------------------
// measured:  `FS_SaveSlot_C` carries `UnlockedTeleporters`, `SavePointsVisited`,
//            `ShopTeleporterDestinations` as `Array<Object>` and `LastTouchedCheckpoint` as
//            `Object` (analysis/04-sync-surface.md §2.4 decodes each from a real save;
//            analysis/pak/blueprint-surface.md FS_SaveSlot lists the same names);
//            a TArray is `{void* Data; int32 Num; int32 Max}` (the same header
//            `ReadActorArrayNum` already reads on a SavableActor);
//            `Checkpoint_C` imports `ClearAllEnemySaveStates` (0x00E27460), whose body is
//            `TMap::Reset` on `save+0x270` -- a rest clears EVERY enemy record
//            (analysis/14-combat-save-answers.md D4-p7);
//            `Teleporter_C` imports `Array_AddUnique`/`Array_Contains` and owns a
//            `RegisterSphere` overlap event -- that is the game's own unlock call, the same
//            "replay the placed actor's own bound event" mechanism the trigger lane uses
//            and proved live;
//            `Lib_Book_*_BP_C` decodes to a `StaticMeshActor` subclass with NO property and
//            NO function (`analysis/pak/code/blueprints.py`), so a book's progress is its
//            transform.
// inferred:  that the object stored in those arrays resolves by its short name on the other
//            machine. It does for a cooked asset (both machines run the same pak), which is
//            the same assumption the travel lane already makes for a placed actor's name.
#pragma once

namespace worldsync {

// 'QDWS' -- this lane's placed-prop seed, distinct from the enemy/dialogue/object seeds so
// one path hashed by four lanes cannot produce one key that means two things.
static const uint32_t kPuzzleSeed = 0x51445753u;

// ---------------------------------------------------------------------------
// Save-side object arrays
// ---------------------------------------------------------------------------
// `Array<Object>` on `FS_SaveSlot_C`: `{UObject** Data; int32 Num; int32 Max}`, elements
// 8 bytes.  Every read and write is fault-contained: this walks a live save object whose
// layout is only known through reflection.
struct ArrView {
    uint8_t** data;
    int32_t   num;
    int32_t   max;
};

static bool SaveArrayView(uint8_t set, ArrView* v)
{
    if (!v) return false;
    v->data = nullptr;
    v->num = v->max = 0;
    SaveLayer& s = Save();
    if (!s.save || !ObjectIsLive(s.save)) return false;
    const char* prop = worldset::SetProperty(set);
    if (!prop || !prop[0]) return false;
    int sz = 0;
    int off = FindPropertyOffsetIn(ObjClass(s.save), prop, &sz);
    if (off < 0 || sz < 16) return false;
    bool ok = false;
    __try {
        uint8_t** data = *(uint8_t***)((uint8_t*)s.save + off);
        int32_t n = *(int32_t*)((uint8_t*)s.save + off + 8);
        int32_t m = *(int32_t*)((uint8_t*)s.save + off + 12);
        if (n >= 0 && n <= 4096 && m >= n && m <= 4096 && (n == 0 || data)) {
            v->data = data; v->num = n; v->max = m;
            ok = true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    return ok;
}

// The short name of an element, or false when the slot is null / not an object.  The name
// is what both machines can agree on (the same cooked asset), which is why it is the key.
static bool ElementName(uint8_t* el, char* out, int cap)
{
    if (out && cap > 0) out[0] = 0;
    if (!el) return false;
    __try {
        if (!LooksLikeUObject(el)) return false;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return FNameToString(ObjNameRaw(el), out, cap) > 0;
}

// Resolve one name back to the object this machine would store.  A CLASS is a legitimate
// answer here (a `_Save` asset's top-level object can be one), so the class test is only
// used to PREFER a non-class object when both exist -- never to refuse a real one.
static void* ResolveElement(const char* name)
{
    if (!name || !name[0]) return nullptr;
    void* o = FindObjectByNameAndClass(name, nullptr);
    if (o && ObjectIsLive(o)) return o;
    // A composed name is not a plain FName ("GR_Camp_Save_C" for a class).  Try the
    // suffix-free form once before giving up.
    int n = 0;
    for (; name[n]; ++n) {}
    if (n > 2 && name[n - 2] == '_' && name[n - 1] == 'C') {
        char base[96];
        int k = 0;
        for (; k < n - 2 && k < (int)sizeof(base) - 1; ++k) base[k] = name[k];
        base[k] = 0;
        o = FindObjectByNameAndClass(base, nullptr);
        if (o && ObjectIsLive(o)) return o;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Item 1 -- the mirrored unlock sets
// ---------------------------------------------------------------------------
static worldset::Union g_union[worldset::SET_COUNT];    // local + partner, merged
static worldset::Union g_localSet[worldset::SET_COUNT]; // what THIS save holds
static uint32_t g_setRev[worldset::SET_COUNT]     = {0, 0, 0, 0};
static uint32_t g_setSentRev[worldset::SET_COUNT] = {0, 0, 0, 0};
static int      g_setCursor[worldset::SET_COUNT]  = {0, 0, 0, 0};
static uint32_t g_setReadMs = 0;
static uint32_t g_setSendMs = 0;
static uint32_t g_setApplyAt[worldset::SET_COUNT] = {0, 0, 0, 0};
static int      g_setApplied = 0, g_setRefused = 0, g_setSentRows = 0;

// The counter a set DERIVES.  A teleporter/checkpoint unlock count is the size of its set:
// two players unlocking the SAME destination is one destination, which is why these are not
// additive (see worldset::IsUnionCounter).  Raising them here is what makes
// `cha_Challenge_AllTeleporters` (10 teleporters AND 32 checkpoints) reachable for the pair.
static const char* DerivedCounter(uint8_t set)
{
    switch (set) {
    case worldset::SET_TELEPORTERS: return "TeleporterUnlockCount";
    case worldset::SET_SAVEPOINTS:  return "CheckpointUnlockCount";
    default:                        return nullptr;
    }
}

static void RaiseCounter(const char* key, int32_t want, const char* why)
{
    if (!key || want <= 0) return;
    SaveLayer& s = Save();
    if (!s.writeOk || !s.save) return;
    int32_t have = 0;
    bool set = false;
    bool reads = NsdRead(key, &have, &set);
    if (reads && have >= want) return;          // never lower: the bank is monotone anyway
    char err[128];
    if (NsdWrite(key, want, err, sizeof(err))) {
        QDLOG(LOG_INFO, "worldset: ", key, " raised to ", dec(want), " from ",
              dec(reads ? have : -1), " (", (reads ? "was set" : "was absent, reads -1"), "; ",
              why, ")");
    } else {
        QDLOG(LOG_WARN, "worldset: ", key, " could not be raised to ", dec(want), ": ", err);
    }
}

// Read the local save's arrays into `g_localSet` and merge them into `g_union`.
static void ReadLocalSets(uint32_t now)
{
    (void)now;
    bool changed = false;
    for (uint8_t set = worldset::SET_TELEPORTERS; set < worldset::SET_COUNT; ++set) {
        ArrView v;
        if (!SaveArrayView(set, &v)) continue;
        worldset::Union fresh;
        for (int32_t i = 0; i < v.num; ++i) {
            char nm[worldset::kKeyCap];
            uint8_t* el = nullptr;
            __try { el = v.data[i]; } __except (EXCEPTION_EXECUTE_HANDLER) { el = nullptr; }
            if (!ElementName(el, nm, sizeof(nm))) continue;
            fresh.Merge(nm);
        }
        if (fresh.Count() != g_localSet[set].Count()) {
            changed = true;
            QDLOG(LOG_INFO, "worldset: ", worldset::SetName(set), " holds ", dec(fresh.Count()),
                  " entr", (fresh.Count() == 1 ? "y" : "ies"), " in this save");
        }
        g_localSet[set] = fresh;
        bool added = false;
        for (int i = 0; i < fresh.Count(); ++i) {
            const char* k = fresh.SortedAt(i);
            if (k && g_union[set].Merge(k)) added = true;
        }
        if (added) {
            ++g_setRev[set];
            changed = true;
        }
        // The derived counters follow the set, whichever side added the destination.
        const char* dk = DerivedCounter(set);
        if (dk) RaiseCounter(dk, fresh.Count(), "the local save's own set");
    }
    if (changed) g_setSendMs = 0;      // push at once, not on the next heartbeat
}

// Send one set's rows, a chunk per datagram (a cursor across ticks, never a fragment).
static void PublishSet(uint8_t set, uint32_t now)
{
    if (g_setSentRev[set] == g_setRev[set]) return;
    if (g_union[set].Count() == 0) { g_setSentRev[set] = g_setRev[set]; return; }

    uint8_t body[wire::kMaxBody];
    wire::Writer w(body, sizeof(body));
    if (!worldset::WriteSetHeader(w, set, g_setRev[set], (uint16_t)g_union[set].Count())) return;
    int first = g_setCursor[set];
    if (first >= g_union[set].Count()) first = 0;
    int next = worldset::WriteSetChunk(w, g_union[set], first);
    if (next == first) { g_setCursor[set] = 0; return; }
    if (!g_net.SendReliable(wire::K_WORLD_SET, body, w.len)) return;

    g_setSentRows += next - first;
    QDLOG(LOG_INFO, "worldset: sent ", worldset::SetName(set), " rows ", dec(first), "..",
          dec(next - 1), " of ", dec(g_union[set].Count()), " rev=", dec(g_setRev[set]));
    g_setCursor[set] = next;
    if (next >= g_union[set].Count()) {
        g_setCursor[set] = 0;
        g_setSentRev[set] = g_setRev[set];
    }
    (void)now;
}

static void PublishSets(uint32_t now)
{
    if (!Cfg().world_set_sync || !g_net.Connected()) return;
    if (now - g_setSendMs < 250) return;
    g_setSendMs = now;
    for (uint8_t set = worldset::SET_TELEPORTERS; set < worldset::SET_COUNT; ++set)
        PublishSet(set, now);
}

// ---------------------------------------------------------------------------
// Item 1 -- applying the partner's unlocks
// ---------------------------------------------------------------------------
// Two routes, in this order, and each is the game's own:
//
//  (1) IN PLACE.  A TArray with spare capacity (`Num < Max`) can take the element without an
//      allocation, which is the only growth this module performs.  The game's own
//      `Array_AddUnique` leaves slack behind on any earlier local unlock, so after the first
//      destination either player reaches, every later one lands this way.
//
//  (2) THE GAME'S OWN UNLOCK CALL.  When the array is exactly full (`Num == Max`, which is
//      what a freshly LOADED save gives: the loader sizes the array to its contents), this
//      module cannot append safely -- reallocating a TArray needs the engine's allocator,
//      and handing it a foreign pointer corrupts it.  For a teleporter the game has its own
//      registration entry: `Teleporter_C`'s `RegisterSphere` begin-overlap event adds the
//      destination with `Array_AddUnique`.  So the receiver dispatches that event on its own
//      copy of the same placed teleporter, with its own player as `OtherActor` -- the
//      mechanism the trigger lane already uses.  The game appends, and its own counter moves.
//
// A key that neither route can place is LOGGED, never dropped in silence.
static bool ArrayHolds(const ArrView& v, const char* name)
{
    for (int32_t i = 0; i < v.num; ++i) {
        char nm[worldset::kKeyCap];
        uint8_t* el = nullptr;
        __try { el = v.data[i]; } __except (EXCEPTION_EXECUTE_HANDLER) { el = nullptr; }
        if (!ElementName(el, nm, sizeof(nm))) continue;
        if (worldset::KeyEq(nm, name)) return true;
    }
    return false;
}

// The teleporter that OWNS the destination name: the placed `Teleporter_C` whose
// `LocationData` names it.  A class walk, bounded, and only on the slow path.
static void* FindTeleporterFor(const char* name)
{
    if (!name || !name[0]) return nullptr;
    void* cls = FindClassByName("Teleporter_C");
    if (!cls) return nullptr;
    void* world = GWorldPtr();
    if (!world) return nullptr;
    int n = ObjectCount();
    for (int i = 0; i < n; ++i) {
        void* o = ObjectAt(i);
        if (!o || !LooksLikeUObject(o)) continue;
        bool member = false;
        __try { member = IsSubclassOf(ObjClass(o), cls) && ActorInThisWorld(o, world); }
        __except (EXCEPTION_EXECUTE_HANDLER) { member = false; }
        if (!member) continue;
        char an[worldset::kKeyCap];
        if (FNameToString(ObjNameRaw(o), an, sizeof(an)) && worldset::KeyEq(an, name)) return o;
        void* data = ObjPropC(o, "LocationData");
        if (!data || !LooksLikeUObject(data)) continue;
        char dn[worldset::kKeyCap];
        if (!FNameToString(ObjNameRaw(data), dn, sizeof(dn))) continue;
        if (worldset::KeyEq(dn, name)) return o;
    }
    return nullptr;
}

// Dispatch `Teleporter_C`'s own RegisterSphere begin-overlap with this machine's player.
// Every parameter is set only when the signature has it, exactly like the trigger lane.
static bool RegisterTeleporter(void* tp, char* how, int howCap)
{
    if (!tp || !g_localPawn) {
        if (how && howCap > 0) lstrcpynA(how, "no local player", howCap);
        return false;
    }
    static Fn f;
    if (!f.Resolve(tp, "BndEvt__Teleporter_RegisterSphere_K2Node_ComponentBoundEvent_0_"
                       "ComponentBeginOverlapSignature__DelegateSignature")) {
        if (how && howCap > 0) lstrcpynA(how, "RegisterSphere event not resolved", howCap);
        return false;
    }
    if (f.Off("OtherActor") < 0) {
        if (how && howCap > 0) lstrcpynA(how, "event has no OtherActor parameter", howCap);
        return false;
    }
    Call c(f);
    void* comp = ObjPropC(tp, "RegisterSphere");
    void* root = ObjPropC(g_localPawn, "CapsuleComponent");
    if (!root) root = ObjPropC(g_localPawn, "RootComponent");
    if (f.Off("OverlappedComponent") >= 0) c.Obj("OverlappedComponent", comp);
    c.Obj("OtherActor", g_localPawn);
    if (f.Off("OtherComp") >= 0) c.Obj("OtherComp", root);
    if (f.Off("OtherBodyIndex") >= 0) c.I32("OtherBodyIndex", 0);
    if (f.Off("bFromSweep") >= 0) c.U8("bFromSweep", 0);
    bool ran = c.Run(tp);
    if (how && howCap > 0) lstrcpynA(how, ran ? "dispatched" : "ProcessEvent refused", howCap);
    return ran;
}

// Make this machine's save hold every key the union learned.  `why` is for the log.
static void ApplySet(uint8_t set, const char* why)
{
    if (!Cfg().world_set_sync) return;
    if (!worldset::SetValid(set)) return;
    ArrView v;
    if (!SaveArrayView(set, &v)) return;

    int added = 0, viaGame = 0, refused = 0;
    int off = -1;
    if (Save().save) off = FindPropertyOffsetIn(ObjClass(Save().save), worldset::SetProperty(set), nullptr);
    if (off < 0) {
        QDLOG(LOG_WARN, "worldset: ", worldset::SetName(set),
              " could not be reached on the save object; nothing applied");
        return;
    }
    for (int i = 0; i < g_union[set].Count(); ++i) {
        const char* key = g_union[set].SortedAt(i);
        if (!key) break;
        if (ArrayHolds(v, key)) continue;

        void* obj = ResolveElement(key);
        if (!obj) {
            ++refused;
            QDLOG(LOG_WARN, "worldset: '", key, "' does not resolve to an object on this machine");
            continue;
        }

        bool placed = false;
        if (v.num < v.max && off >= 0) {
            __try {
                v.data[v.num] = (uint8_t*)obj;
                *(int32_t*)((uint8_t*)Save().save + off + 8) = v.num + 1;
                placed = true;
                ++v.num;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                placed = false;
            }
        }
        if (placed) { ++added; continue; }

        // Exactly full.  A teleporter has the game's own registration entry; anything else
        // is reported, because a foreign-pointer TArray realloc is not safe here.
        if (set == worldset::SET_TELEPORTERS) {
            void* tp = FindTeleporterFor(key);
            char how[96] = "";
            if (tp && RegisterTeleporter(tp, how, sizeof(how))) {
                ++viaGame;
                QDLOG(LOG_INFO, "worldset: teleporter ", key,
                      " unlocked through the game's own RegisterSphere (", how, ")");
                continue;
            }
            QDLOG(LOG_WARN, "worldset: teleporter ", key, " is not placeable here (",
                  (tp ? how : "no placed teleporter carries that destination"), ")");
        }
        QDLOG(LOG_WARN, "worldset: ", worldset::SetName(set), " is FULL (", dec(v.num), "/",
              dec(v.max), ") and '", key, "' cannot be appended without the engine allocator; ",
              "it lands as soon as the game itself grows the array");
        ++refused;
        break;                       // one report per pass, not one per key
    }
    if (added || viaGame || refused) {
        QDLOG((refused ? LOG_WARN : LOG_INFO), "worldset: applied ", worldset::SetName(set),
              " from ", why, " -- appended=", dec(added), " via the game=", dec(viaGame),
              " left=", dec(refused), " now ", dec(v.num), " entries");
        g_setApplied += added + viaGame;
        g_setRefused += refused;
        const char* dk = DerivedCounter(set);
        if (dk) RaiseCounter(dk, v.num, "the mirrored set");
        ++g_setRev[set];
    }
}

void OnSet(wire::Reader& r, uint32_t now)
{
    if (!Cfg().world_set_sync) return;
    worldset::SetHeader h;
    if (!worldset::ReadSetHeader(r, &h)) {
        QDLOG(LOG_WARN, "worldset: malformed set header refused");
        return;
    }
    worldset::SetRow rows[24];
    int n = worldset::ReadSetChunk(r, rows, 24);
    int added = 0;
    for (int i = 0; i < n; ++i) {
        if (g_union[h.set].Merge(rows[i].key)) ++added;
    }
    QDLOG(LOG_INFO, "worldset: the partner's ", worldset::SetName(h.set), " rev=",
          dec(h.rev), " carries ", dec(h.n), " entr", (h.n == 1 ? "y" : "ies"), " here ",
          dec(n), " row(s), ", dec(added), " new");
    if (added) {
        ++g_setRev[h.set];
        g_setApplyAt[h.set] = now;
    }
}

// The apply pass runs from the pump (the save object may not be readable from inside a
// dispatch, and a set can arrive in several chunks).
static void ApplyPendingSets(uint32_t now)
{
    for (uint8_t set = worldset::SET_TELEPORTERS; set < worldset::SET_COUNT; ++set) {
        if (!g_setApplyAt[set]) continue;
        if (now - g_setApplyAt[set] < 400) continue;   // let the rest of the chunks land
        g_setApplyAt[set] = 0;
        ApplySet(set, "the partner's unlock");
    }
}

// ---------------------------------------------------------------------------
// Item 2 -- a rest is a world event
// ---------------------------------------------------------------------------
static rest::Episode g_restEpisode;
static int           g_restAppliedCount = 0;

// Is this enemy key one a rest must NOT revive?  See rest::ResetExempt.  A `_Dead`-style
// flag gate is read off the SPAWNER, which is where the game keeps it
// (`RequireNamedSaveDataToSpawn` + `NamedSaveDataToCheck`, the same pair the spawn gate and
// the boss lane read).
static bool RestExemptKey(const EnemyRec& rec)
{
    if (enemy::ZeroGuid(rec.guid)) return true;       // no identity: nothing to keep
    if (BossKeyIs(rec.guid)) return true;             // the shared bar's key is a boss
    void* sp = rec.spawner.Get();
    if (!sp || !ObjectIsLive(sp)) return false;
    static const char* const bossProps[] = {"Boss", "bBoss", "IsBoss", "IsABoss"};
    for (unsigned i = 0; i < sizeof(bossProps) / sizeof(bossProps[0]); ++i) {
        uint8_t b = 0;
        if (BytePropC(sp, bossProps[i], &b) && b) return true;
    }
    uint8_t gated = 0;
    if (BytePropC(sp, "RequireNamedSaveDataToSpawn", &gated) && gated) return true;
    return false;
}

// The reset itself.  Order matters: snapshot what must stay dead, clear through the game's
// own call, then write the deaths back.
//
// `ClearAllEnemySaveStates` is called through the same entry point the game uses
// (`0x00E27460`, `(UObject* WorldContext)`), not by resetting the TMap by hand: the engine
// keeps bookkeeping beside the element array and a hand reset is the kind of "close enough"
// write this codebase's own docs warn about.
typedef void (*FnClearAllEnemySaveStates)(void* worldContext);

static void ApplyRestReset(uint32_t seq, const char* why)
{
    // 1. what must never come back
    static uint32_t keep[rest::kMaxKeep][4];
    int keepN = 0;
    int ordinary = 0;
    for (int i = 0; i < g_enN; ++i) {
        EnemyRec& rec = g_en[i];
        if (!rec.haveGuid) continue;
        if (RestExemptKey(rec)) {
            if (keepN < rest::kMaxKeep) {
                for (int k = 0; k < 4; ++k) keep[keepN][k] = rec.guid[k];
                ++keepN;
            }
        } else {
            ++ordinary;
        }
    }
    rest::Plan plan = rest::MakePlan(true, keepN, ordinary, seq);

    // 2. the game's own clear
    void* world = GWorldPtr();
    bool cleared = false;
    if (world && Save().save) {
        __try {
            FnClearAllEnemySaveStates fn = (FnClearAllEnemySaveStates)Ptr<void*>(0x00E27460u);
            if (fn) { fn(world); cleared = true; }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            cleared = false;
        }
    }
    QDLOG(cleared ? LOG_INFO : LOG_WARN, "rest: world reset (", why, ") seq=", dec((int)seq),
          " -- ClearAllEnemySaveStates ", (cleared ? "ran" : "REFUSED"),
          "; ", dec(plan.release), " ordinary enem", (plan.release == 1 ? "y" : "ies"),
          " respawn here, ", dec(plan.keep), " boss/one-time death(s) kept");

    // 3. put the exempt deaths back, so a rest cannot revive a boss
    int restored = 0;
    for (int i = 0; i < plan.keep; ++i) {
        void* sp = nullptr;
        for (int k = 0; k < g_enN && !sp; ++k) {
            if (g_en[k].haveGuid && enemy::SameGuid(g_en[k].guid, keep[i])) sp = g_en[k].spawner.Get();
        }
        if (!sp || !ObjectIsLive(sp)) continue;
        QdGuid g;
        g.a = keep[i][0]; g.b = keep[i][1]; g.c = keep[i][2]; g.d = keep[i][3];
        __try {
            StoreEnemySaveState(sp, g, -1.0f, 0.0f);   // RespawnTime<0: never comes back
            ++restored;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    if (plan.keep > 0) {
        QDLOG(restored == plan.keep ? LOG_INFO : LOG_WARN, "rest: ", dec(restored), " of ",
              dec(plan.keep), " exempt death(s) written back after the clear");
    }

    // 4. this machine's own kill log is now wrong: every ordinary death is void
    if (g_killLog.Count()) {
        QDLOG(LOG_INFO, "rest: ", dec(g_killLog.Count()), " death(s) forgotten here");
        g_killLog.Clear();
    }
    ++g_restAppliedCount;
}

static void SendRest(uint32_t seq, const char* point, bool order)
{
    if (!Cfg().world_set_sync || !g_net.Connected()) return;
    rest::Body b;
    memset(&b, 0, sizeof(b));
    b.seq = seq;
    b.flags = (uint8_t)((order ? rest::kRestOrder : 0) | rest::kRestLocal);
    ActorXform me = {};
    if (g_localPawn && ReadActorXform(g_localPawn, &me)) { b.px = me.px; b.py = me.py; b.pz = me.pz; }
    int i = 0;
    if (point) for (; point[i] && i < rest::kNameCap - 1; ++i) b.name[i] = point[i];
    b.name[i] = 0;

    uint8_t body[256];
    wire::Writer w(body, sizeof(body));
    if (!rest::Encode(w, b) || !g_net.SendReliable(wire::K_REST_WORLD, body, w.len)) {
        QDLOG(LOG_WARN, "rest: the rest body was not sent");
        return;
    }
    QDLOG(LOG_INFO, "rest: told the partner about rest #", dec((int)seq), " at ",
          (b.name[0] ? b.name : "(unnamed)"),
          (order ? " -- the world reset is ordered from here" : " -- asking the host to order it"));
}

// A rest this machine observed on its own player (`PlayerRested`).  `point` may be null: the
// respawn lane already records where the rest happened (`resp::g_self.Rest().name`), and this
// lane only carries the NAME for the log -- it never moves a player.
//
// ONE APPLY PER EPISODE.  The local path is reached once per `PlayerRested` dispatch, and a
// checkpoint refreshes that event, so the sequence -- not the event count -- decides: an
// episode this machine has already reset is not reset again.
static uint32_t g_localResetSeq = 0;

void NoteLocalRest(uint32_t now, const char* point)
{
    if (!Cfg().world_set_sync) return;
    const char* nm = point;
    if (!nm || !nm[0]) nm = resp::g_self.Rest().name;
    uint32_t seq = g_restEpisode.NoteLocal(now, nm ? nm : "");
    if (g_net.IsHost()) {
        // The host is the authority: apply it once, then order it.
        if (seq != g_localResetSeq) {
            g_localResetSeq = seq;
            ApplyRestReset(seq, "the host's own rest");
        }
        g_restEpisode.NoteAppliedOrder(seq, now);
        SendRest(seq, nm, true);
    } else {
        SendRest(seq, nm, false);       // a request; the host decides (and applies)
    }
}

void OnRest(wire::Reader& r, uint32_t now)
{
    if (!Cfg().world_set_sync) return;
    rest::Body b;
    if (!rest::Decode(r, &b)) {
        QDLOG(LOG_WARN, "rest: malformed rest body refused");
        return;
    }
    bool order = (b.flags & rest::kRestOrder) != 0;
    rest::Verdict v = g_restEpisode.OnPeer(b.seq, b.name, g_net.IsHost(), now, order);
    switch (v) {
    case rest::RV_REQUEST:
        // Host: the partner rested.  Make it a world event.
        QDLOG(LOG_INFO, "rest: the partner rested at ", (b.name[0] ? b.name : "(unnamed)"),
              " -- applying the same reset here and ordering it");
        ApplyRestReset(b.seq, "the partner's rest");
        g_restEpisode.NoteAppliedOrder(b.seq, now);
        SendRest(b.seq, b.name, true);
        break;
    case rest::RV_APPLY:
        QDLOG(LOG_INFO, "rest: the host ordered rest #", dec((int)b.seq), " (",
              (b.name[0] ? b.name : "(unnamed)"), ") -- applying the same reset here");
        ApplyRestReset(b.seq, "the host's order");
        break;
    case rest::RV_DUPLICATE:
        QDLOG(LOG_DEBUG, "rest: rest #", dec((int)b.seq), " already applied");
        break;
    case rest::RV_STALE:
        QDLOG(LOG_INFO, "rest: rest #", dec((int)b.seq), " is older than #",
              dec((int)g_restEpisode.Last()), "; refused");
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Item 3 -- puzzle props whose state is a transform
// ---------------------------------------------------------------------------
static puzzle::Board g_puzzle;
static uint32_t     g_puzzleObserveMs = 0;
static uint32_t     g_puzzleSendMs = 0;
static uint32_t     g_puzzleRev = 0;
static int          g_puzzleSent = 0, g_puzzleApplied = 0;

// Rows this machine turned and has not sent yet.
static puzzle::Row g_puzzlePending[puzzle::kMaxRows];
static int         g_puzzlePendingN = 0;

// The placed prop's key -> live actor, rebuilt on the observation pass.  A puzzle is a
// handful of props near the player, never a GObjects walk per received row.
struct PuzzleActor { uint32_t key[4]; void* actor; };
static PuzzleActor g_puzzleActors[256];
static int         g_puzzleActorN = 0;
static void*       g_puzzleActorWorld = nullptr;

// The classes whose intermediate state is a TRANSFORM and nothing else.  Decoded parents are
// in qdpuzzle.h's header comment: the 287 `Lib_Book_*` placements, whose blueprint declares no
// property at all.  The rest of the puzzle actor set is carried by the existing lanes and is
// listed there so this list stays honest.
static bool IsPuzzlePropClass(const char* cls)
{
    if (!cls || !cls[0]) return false;
    if (enemyown::HasCI(cls, "Lib_Book_")) return true;
    return !strcmp(cls, "SuperCubePuzzleDoor");
}

static void PuzzleObserve(uint32_t now)
{
    if (!Cfg().puzzle_sync || !g_localPawn) return;
    if (now - g_puzzleObserveMs < 500) return;
    g_puzzleObserveMs = now;

    void* actorBase = FindClassByName("Actor");
    void* world = GWorldPtr();
    if (!actorBase || !world) return;
    ActorXform me = {};
    if (!ReadActorXform(g_localPawn, &me)) return;

    if (world != g_puzzleActorWorld) {
        g_puzzleActorWorld = world;
        g_puzzleActorN = 0;
        g_puzzle.Reset();
        g_puzzlePendingN = 0;
    }
    // The actor table is REBUILT on every pass: the props are placed actors in a world that
    // streams in and out, and appending to it each pass would fill the table with dead
    // duplicates within a minute.
    g_puzzleActorN = 0;

    int n = ObjectCount();
    for (int i = 0; i < n; ++i) {
        void* o = ObjectAt(i);
        if (!o || !LooksLikeUObject(o)) continue;
        char cls[96] = "";
        bool member = false;
        __try {
            member = IsSubclassOf(ObjClass(o), actorBase) && ActorInThisWorld(o, world) &&
                     FNameToString(ObjNameRaw(ObjClass(o)), cls, sizeof(cls)) > 0 &&
                     IsPuzzlePropClass(cls);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            member = false;
        }
        if (!member) continue;
        ActorXform x = {};
        if (!ReadActorXform(o, &x)) continue;
        // Only nearby props: a puzzle is solved standing in front of it, and a per-frame walk
        // over 287 books in one world is not a cost worth paying.
        float dx = x.px - me.px, dy = x.py - me.py;
        if (dx * dx + dy * dy > 4000.0f * 4000.0f) continue;

        char path[192];
        if (ActorLevelPath(o, world, path, sizeof(path)) <= 0) continue;
        uint32_t key[4];
        enemyid::Hash128(path, kPuzzleSeed, key);

        if (g_puzzleActorN < (int)(sizeof(g_puzzleActors) / sizeof(g_puzzleActors[0]))) {
            for (int k = 0; k < 4; ++k) g_puzzleActors[g_puzzleActorN].key[k] = key[k];
            g_puzzleActors[g_puzzleActorN].actor = o;
            ++g_puzzleActorN;
        }

        uint32_t seq = 0;
        puzzle::Row row;
        memset(&row, 0, sizeof(row));
        if (g_puzzle.ObserveLocal(key, wire::QuantizeAngle(x.yaw),
                                  wire::QuantizeAngle(x.pitch), 0, &seq, &row)) {
            if (g_puzzlePendingN < puzzle::kMaxRows) g_puzzlePending[g_puzzlePendingN++] = row;
        }
    }
}

static void* FindPuzzleActor(const uint32_t key[4])
{
    for (int i = 0; i < g_puzzleActorN; ++i) {
        if (puzzle::SameKey(g_puzzleActors[i].key, key) && ObjectIsLive(g_puzzleActors[i].actor))
            return g_puzzleActors[i].actor;
    }
    return nullptr;
}

static void PuzzlePublish(uint32_t now)
{
    if (!Cfg().puzzle_sync || !g_net.Connected()) return;
    if (now - g_puzzleSendMs < 250) return;
    g_puzzleSendMs = now;
    if (g_puzzlePendingN <= 0) return;

    int sent = 0;
    int first = 0;
    while (first < g_puzzlePendingN) {
        uint8_t body[wire::kMaxBody];
        wire::Writer w(body, sizeof(body));
        int chunk = g_puzzlePendingN - first;
        if (chunk > puzzle::kMaxRows) chunk = puzzle::kMaxRows;
        if (!puzzle::WriteHeader(w, ++g_puzzleRev, (uint8_t)chunk)) break;
        int next = puzzle::WriteRows(w, g_puzzlePending, first + chunk, first);
        if (next == first) break;
        if (!g_net.SendReliable(wire::K_PUZZLE, body, w.len)) break;
        sent += next - first;
        first = next;
    }
    if (sent) {
        g_puzzleSent += sent;
        QDLOG(LOG_INFO, "puzzle: sent ", dec(sent), " prop row(s) rev=", dec(g_puzzleRev));
    }
    // Keep what did not go out: a lost row is a book that stays turned on one screen only,
    // and the next pass would never re-observe it (the pose has not changed again).
    if (sent >= g_puzzlePendingN) {
        g_puzzlePendingN = 0;
    } else if (sent > 0) {
        int keep = g_puzzlePendingN - sent;
        for (int i = 0; i < keep; ++i) g_puzzlePending[i] = g_puzzlePending[sent + i];
        g_puzzlePendingN = keep;
    }
}

void OnPuzzle(wire::Reader& r, uint32_t now)
{
    if (!Cfg().puzzle_sync) return;
    uint32_t rev = 0;
    uint8_t n = 0;
    if (!puzzle::ReadHeader(r, &rev, &n)) {
        QDLOG(LOG_WARN, "puzzle: malformed header refused");
        return;
    }
    puzzle::Row rows[puzzle::kMaxRows];
    int got = puzzle::ReadRows(r, rows, (int)n < puzzle::kMaxRows ? (int)n : puzzle::kMaxRows);
    int applied = 0;
    for (int i = 0; i < got; ++i) {
        puzzle::Apply a = g_puzzle.OnPeer(rows[i]);
        if (a != puzzle::P_OK) continue;
        void* actor = FindPuzzleActor(rows[i].key);
        if (!actor) continue;
        ActorXform x = {};
        if (!ReadActorXform(actor, &x)) continue;
        // The engine's own move: same position, the row's yaw.  `K2_SetActorLocationAnd-
        // Rotation` updates the render transform, overlaps and navigation.
        if (SetActorXform(actor, x.px, x.py, x.pz, wire::DequantizeAngle(rows[i].yaw))) {
            ++applied;
            ++g_puzzleApplied;
        }
    }
    if (applied) {
        QDLOG(LOG_INFO, "puzzle: applied ", dec(applied), " of ", dec(got),
              " prop row(s) from the partner rev=", dec(rev));
    }
    (void)now;
}

// ---------------------------------------------------------------------------
// Item 5 -- a counter whose truth is the SUM travels as a delta
// ---------------------------------------------------------------------------
// The flag lane is monotone-absolute: the host pushes what it holds and the peer takes the
// higher value.  That is exactly right for a story flag and exactly wrong for
// `TotalChestsOpened`: both players open DIFFERENT chests, each machine counts only its
// own (the partner's chest is applied here through the object lane WITHOUT its payout, by
// design), so `max(a, b)` loses the other player's acts and `cha_Challenge_AllChests` = 108
// becomes unreachable.
//
// So for the keys `worldset::IsUnionCounter` names, the peer reports the LOCAL RISE as a
// delta and the host ADDS it (`I_SET_FLAG` with mode 0 = additive, which
// `flags::FlagBank::ApplyIntent` already implements).  `lastReported` is what this machine
// has already handed to the lane; a delta is only the part above it.
struct CounterMark { char name[80]; int32_t last; };
static CounterMark g_counterMark[8];
static int         g_counterMarkN = 0;

static CounterMark* CounterMarkFor(const char* name, bool create)
{
    if (!name || !name[0]) return nullptr;
    for (int i = 0; i < g_counterMarkN; ++i)
        if (worldset::KeyEq(g_counterMark[i].name, name)) return &g_counterMark[i];
    if (!create || g_counterMarkN >= (int)(sizeof(g_counterMark) / sizeof(g_counterMark[0])))
        return nullptr;
    CounterMark& m = g_counterMark[g_counterMarkN];
    worldset::CopyKey(m.name, sizeof(m.name), name);
    m.last = 0;
    ++g_counterMarkN;
    return &m;
}

// The delta to put on the wire for `name`'s new local value (0 = nothing to send).  The
// mark is advanced here, at queue time: a give-up is rolled back by
// `CounterRollback` so the next rise re-sends the accumulated delta rather than losing it.
int32_t CounterDeltaFor(const char* name, int32_t value)
{
    CounterMark* m = CounterMarkFor(name, true);
    if (!m) return 0;
    int32_t d = worldset::CounterDelta(value, m->last);
    if (d > 0) m->last = value;
    return d;
}

void CounterRollback(const char* name, int32_t delta)
{
    if (delta <= 0) return;
    CounterMark* m = CounterMarkFor(name, false);
    if (!m) return;
    m->last -= delta;
    if (m->last < 0) m->last = 0;
}

// ---------------------------------------------------------------------------
// The rig lever (`qd.world` / QDCOOP_TEST_WORLD)
// ---------------------------------------------------------------------------
// The proof for this lane is a VALUE on both machines, not a picture: which destinations each
// save holds, what the derived counters read, and what the rest episode is.  One text file,
// polled at 1 Hz, so a live session can ask at any moment and a solo run can ask twice.
//   "dump"  log the sets, the counters, the rest episode and the puzzle props
//   "rest"  run the same reset a rest runs, through this lane (proves item 2 solo)
static void DumpToLog(uint32_t now)
{
    (void)now;
    for (uint8_t set = worldset::SET_TELEPORTERS; set < worldset::SET_COUNT; ++set) {
        ArrView v;
        bool have = SaveArrayView(set, &v);
        QDLOG(LOG_INFO, "worldset[", worldset::SetName(set), "]: save=", dec(have ? v.num : -1),
              "/", dec(have ? v.max : -1), " union=", dec(g_union[set].Count()),
              " rev=", dec(g_setRev[set]), " applied=", dec(g_setApplied),
              " refused=", dec(g_setRefused));
        for (int i = 0; i < g_union[set].Count() && i < 40; ++i) {
            const char* k = g_union[set].SortedAt(i);
            if (k) QDLOG(LOG_INFO, "  ", k, (have && ArrayHolds(v, k) ? "  (in the save)" : ""));
        }
    }
    for (uint8_t set = worldset::SET_TELEPORTERS; set < worldset::SET_COUNT; ++set) {
        const char* dk = DerivedCounter(set);
        if (!dk) continue;
        int32_t have = 0;
        bool set2 = false;
        bool reads = NsdRead(dk, &have, &set2);
        QDLOG(LOG_INFO, "worldset counter ", dk, " = ", dec(reads ? have : -1),
              " (set size ", dec(g_localSet[set].Count()), ")");
    }
    QDLOG(LOG_INFO, "rest: episode have=", dec(g_restEpisode.Have() ? 1 : 0), " last=",
          dec((int)g_restEpisode.Last()), " point=",
          (g_restEpisode.Point()[0] ? g_restEpisode.Point() : "(none)"),
          " applied=", dec(g_restAppliedCount));
    QDLOG(LOG_INFO, "puzzle: ", dec(g_puzzle.Count()), " prop(s) held, sent=",
          dec(g_puzzleSent), " applied=", dec(g_puzzleApplied),
          " actors=", dec(g_puzzleActorN));
    puzzle::Row pr;
    for (int i = 0; g_puzzle.At(i, &pr) && i < 20; ++i) {
        QDLOG(LOG_INFO, "  prop key=", hex32(pr.key[0]), " yaw=", dec((int)pr.yaw),
              " seq=", dec((int)pr.seq));
    }
    for (int i = 0; i < g_counterMarkN; ++i) {
        int32_t have = 0;
        bool set3 = false;
        bool reads = NsdRead(g_counterMark[i].name, &have, &set3);
        QDLOG(LOG_INFO, "counter ", g_counterMark[i].name, " = ",
              dec(reads ? have : -1), " reported=", dec(g_counterMark[i].last),
              " (", (worldset::IsUnionCounter(g_counterMark[i].name) ? "SUM" : "monotone"),
              ")");
    }
}

void DevPoll(uint32_t now)
{
    static uint32_t lastPoll = 0;
    if (now - lastPoll < 1000) return;
    lastPoll = now;
    char cmd[32];
    if (!DevRigText("QDCOOP_TEST_WORLD", "qd.world", cmd, sizeof(cmd))) return;
    if (!cmd[0]) return;
    if (enemyown::HasCI(cmd, "dump")) {
        DumpToLog(now);
        return;
    }
    // The file is a COMMAND, not a state, and the mod cannot unlink it.  So `rest` acts once
    // per distinct text: the lever is polled every second, and a re-apply each second would
    // be four resets for one instruction.  Writing a different word (`rest2`) is the next one.
    static char lastRest[32] = "";
    if (enemyown::HasCI(cmd, "rest") && strcmp(cmd, lastRest) != 0) {
        worldset::CopyKey(lastRest, sizeof(lastRest), cmd);
        // Through the REAL lane, so the partner is told exactly as a checkpoint rest tells it.
        // This is what proves item 2: one machine rests, BOTH machines reset.
        NoteLocalRest(now, "rig:qd.world=rest");
    }
}

// ---------------------------------------------------------------------------
// The pump
// ---------------------------------------------------------------------------
void Tick(uint32_t now)
{
    if (!Cfg().world_set_sync) return;
    if (now - g_setReadMs < 1000) {
        ApplyPendingSets(now);
        PuzzleObserve(now);
        PuzzlePublish(now);
        PublishSets(now);
        return;
    }
    g_setReadMs = now;
    ReadLocalSets(now);
    ApplyPendingSets(now);
    PublishSets(now);
    PuzzleObserve(now);
    PuzzlePublish(now);
}

} // namespace worldsync
