// qdaudit.h -- the desync auditor.
//
// WHAT THIS IS FOR
// ----------------
// Every other module in this tree makes ONE thing converge, and each of them can only
// tell you about its own lane.  Nothing in the mod can answer the question a player
// actually asks at the end of a co-op run -- "did the two games ever disagree about the
// world?" -- because a lane that converges silently and a lane that never had anything
// to do look identical from inside.  This module answers it directly: both machines
// digest the synced world state of the CURRENT map, exchange the digests, and report
// every item that differs.
//
// It is READ-ONLY and it owns no game state, so it is always on.  It never writes a
// property, never spawns, never teleports: it reads the containers the other lanes
// already maintain and compares them.  That is what makes it safe to leave enabled for
// a whole playthrough.
//
// WHY A DIGEST AND NOT THE STATE
// ------------------------------
// The state is large (up to 1024 enemy keys, 256 placed objects) and the body cap is
// 1200 bytes.  Sending it every 10 s would be the loudest lane in the session, for a
// question whose answer is almost always "nothing".  So the steady state is ONE small
// body of per-category digests (12 categories, ~140 bytes), and the per-item sets are
// sent only when a digest disagrees -- on request, split across ticks like every other
// container here.
//
// THE GRACE WINDOW, AND WHY IT IS NOT OPTIONAL
// --------------------------------------------
// The two machines are never at exactly the same instant.  An object opened 200 ms ago
// is applied on the partner a frame or two later; a level being streamed is in the
// union on one machine and not yet on the other.  Reporting those would make the
// auditor the noisiest thing in the log and would teach the reader to ignore it.  A
// mismatch is therefore only reported once it has PERSISTED for kGraceMs, and a
// category that heals before then is never mentioned at all.  A real desync does not
// heal, which is exactly what makes the 3 s window free.
//
// CATEGORIES THAT ARE LEGITIMATELY LOCAL (and are therefore NOT digested)
// ---------------------------------------------------------------------
// These are excluded on purpose, not by omission.  Each one is a thing two machines are
// SUPPOSED to disagree about, so including it would turn a correct session into a wall
// of false desyncs:
//
//   * Normal inventory (potion / ammo / tool counts).  Each player picks their own up;
//     only `Items/KeyItems/` crosses (qdprogression.h).  QUEST below is that subset.
//   * The partner's position, velocity, facing, movement mode and animation.  These are
//     30 Hz SAMPLES of two different players, not world state -- they are different by
//     construction.  (Their correctness is qdmotion.h's interpolation, measured on
//     screen, not here.)
//   * Per-player status effects, HP, stamina and the HUD.  Each machine draws its own.
//   * XP totals and achievement counters.  `KILL_XP` pays one specific award to both
//     players; a player can still gain XP another way, and the two counters are then
//     legitimately different.
//   * The puppet's equipment transform and the visual-effect lanes (projectiles, arcs,
//     cast VFX).  Cosmetics with their own lifetime; a lost sample is invisible.
//   * The shared loot set's SPAWN points and the per-death drop ordinal.  Each machine
//     materialises the same class at the same point, but the ordinal inside one death is
//     assigned by the machine that ran the death, so a raw set diff would compare
//     ordinals rather than facts.
//   * Which enemies THIS machine is running (qdenemyown.h ownership).  Ownership follows
//     the nearer player by design, so it differs on purpose; what must agree is who is
//     alive and who is dead, which is CAT_ENEMY.
//
// HOW THE WIRE BODIES ARE SHAPED
// -------------------------------
// qdwire.h owns the bytes (WIRE.md 3.27).  This header owns the LOGIC: the canonical
// per-category item encoding, the order-independent digest, the per-item diff, and the
// grace/request state machine.  proxy.cpp owns the collection (it is the only place the
// module instances are visible).  Splitting them this way is what lets
// test/test_audit.cpp exercise the parts that are easy to get wrong without a game.
#pragma once
#include <stdint.h>

// The audit's item row IS the wire row (WIRE.md 3.27), exactly as `wire::ObjectRow` is the
// placed-object row: one shape, so the digest, the diff and the encoder cannot drift.
#include "qdwire.h"

namespace qd {
namespace audit {

// ---------------------------------------------------------------------------
// Categories
// ---------------------------------------------------------------------------
// The wire order is this order and must not change without a version bump: `cat` is a
// byte on the wire and both machines must agree which byte means what.
//
// Every category is a SET of (key, value) items.  The key is the item's identity as both
// machines can compute it from the cooked content or from a lane that has already
// converged -- never a pointer, never a scan ordinal:
//
//   flags    NamedSaveData      key = the flag's own name        value = its int32
//   actors   SavableActorStates key = the GUID as 32 hex chars   value = its int32
//   objects  placed objects     key = the placed key as hex      value = state | shape<<8
//   quest    KeyItems only      key = the item's soft path       value = its count
//   enemy    alive/dead         key = the enemy key as hex       value = 0 dead, 1 alive
//   boss     phase + HP bucket  key = the boss key as hex        value = phase | bucket<<8
//   movers   moving objects     key = "<mover id hex>:<axis>"    value = bucketed 1/8 cm
//                                                                 (or 1/65536 turn)
//   levels   streamed levels    key = the level's short name     value = 1
//   map      the current world  key = the map's short name       value = 1
//   cutscene per slot           key = "s<slot>:<field>"          value = 0/1/2 or a name hash
//   dialogue per slot           key = "d<slot>:<field>"          value = 0/1 or an NPC key hash
//   deaths   death-penalty      key = "host" / "peer"            value = the stack count
enum Cat : uint8_t {
    CAT_FLAGS    = 1,
    CAT_ACTORS   = 2,
    CAT_OBJECTS  = 3,
    CAT_QUEST    = 4,
    CAT_ENEMY    = 5,
    CAT_BOSS     = 6,
    CAT_MOVERS   = 7,
    CAT_LEVELS   = 8,
    CAT_MAP      = 9,
    CAT_CUTSCENE = 10,
    CAT_DIALOGUE = 11,
    CAT_DEATHS   = 12,
};

static const int kMaxCats = 12;

inline const char* CatName(uint8_t cat)
{
    switch (cat) {
    case CAT_FLAGS:    return "flags";
    case CAT_ACTORS:   return "actors";
    case CAT_OBJECTS:  return "objects";
    case CAT_QUEST:    return "quest";
    case CAT_ENEMY:    return "enemy";
    case CAT_BOSS:     return "boss";
    case CAT_MOVERS:   return "movers";
    case CAT_LEVELS:   return "levels";
    case CAT_MAP:      return "map";
    case CAT_CUTSCENE: return "cutscene";
    case CAT_DIALOGUE: return "dialogue";
    case CAT_DEATHS:   return "deaths";
    default:           return "?";
    }
}

inline bool CatValid(uint8_t cat) { return cat >= CAT_FLAGS && cat <= CAT_DEATHS; }

// ---------------------------------------------------------------------------
// The canonical item set
// ---------------------------------------------------------------------------
// A key is at most 47 visible bytes: a 32-char GUID hex plus a short suffix, or a soft
// object path.  Anything longer is TRUNCATED and the set is marked `overflow`, which the
// caller reports once -- a silently truncated key hashes to a digest that never matches,
// which would be a desync the auditor invented.
static const int kKeyCap   = wire::kAuditKeyCap;
// 1024 is qdenemyid's bound on the enemy record table, and CAT_ENEMY is the widest set.
// Every other category is far inside it.  The table is fixed because the DLL is /MT and
// nothing on the game thread may allocate.
static const int kMaxItems = 1024;

typedef wire::AuditItem Item;

struct Set {
    uint8_t  cat = 0;
    int      n = 0;
    bool     overflow = false;

    void Reset(uint8_t c = 0)
    {
        cat = c;
        n = 0;
        overflow = false;
    }

    // Adds or REPLACES.  Two machines building the same set must produce the same items
    // regardless of the order they discover them in, so a duplicate key is an update
    // rather than a second row.
    //
    // The per-row 32-bit key hash is not part of the digest (which folds the key bytes); it
    // exists because CAT_ENEMY holds up to 1024 keys and a pass is O(n^2) in rows. Comparing
    // an int first turns ~1M 33-byte string compares into ~1M int compares, which is the
    // difference between a 40 ms hitch on the game thread and a 1 ms one.
    bool Add(const char* key, int64_t value)
    {
        uint32_t h = KeyHash(key);
        int i = FindH(key, h);
        if (i >= 0) { it[i].value = value; return true; }
        if (n >= kMaxItems) { overflow = true; return false; }
        int k = 0;
        for (; key[k] && k < kKeyCap - 1; ++k) it[n].key[k] = key[k];
        if (key[k]) overflow = true;
        it[n].key[k] = 0;
        it[n].value = value;
        kh[n] = h;
        ++n;
        return true;
    }

    int Find(const char* key) const { return FindH(key, KeyHash(key)); }

    static uint32_t KeyHash(const char* s)
    {
        uint32_t h = 2166136261u;
        for (int i = 0; s && s[i]; ++i) {
            h ^= (uint8_t)s[i];
            h *= 16777619u;
        }
        return h;
    }

    Item it[kMaxItems];

    static bool Same(const char* a, const char* b)
    {
        int i = 0;
        for (; a[i] || b[i]; ++i) if (a[i] != b[i]) return false;
        return true;
    }

private:
    int FindH(const char* key, uint32_t h) const
    {
        if (!key) return -1;
        for (int i = 0; i < n; ++i) {
            if (kh[i] != h) continue;
            if (Same(it[i].key, key)) return i;
        }
        return -1;
    }

    uint32_t kh[kMaxItems];
};

// ---------------------------------------------------------------------------
// Hashing
// ---------------------------------------------------------------------------
// FNV-1a 64.  Chosen for the same reason the rest of this tree uses FNV: it is four lines,
// has no table, no allocation and no platform dependence, so the digest of a set computed
// by this file and by a future implementation of the same canonical encoding agree.
inline uint64_t Fnv64(const void* p, int n, uint64_t h = 1469598103934665603ull)
{
    const uint8_t* b = (const uint8_t*)p;
    for (int i = 0; i < n; ++i) {
        h ^= (uint64_t)b[i];
        h *= 1099511628211ull;
    }
    return h;
}

inline uint64_t HashText(const char* s)
{
    int n = 0;
    while (s && s[n]) ++n;
    return Fnv64(s, n);
}

// One item's contribution.  The key and the value are both folded, so a value change and
// a key change are both detected; the 8-byte little-endian value is folded explicitly
// rather than by memcpy of the struct, because the struct has padding.
inline uint64_t ItemHash(const char* key, int64_t value)
{
    int n = 0;
    while (key && key[n]) ++n;
    uint64_t h = Fnv64(key, n);
    uint8_t v[8];
    uint64_t u = (uint64_t)value;
    for (int i = 0; i < 8; ++i) v[i] = (uint8_t)((u >> (i * 8)) & 0xFF);
    return Fnv64(v, 8, h);
}

// The set digest: an ORDER-INDEPENDENT sum.  Insertion order is a scan artefact on both
// machines (the entity table's order is its own), so an order-dependent digest would
// disagree on two machines holding identical state.  Keys are unique inside a set, so a
// sum has no multiplicity to lose; two independent 64-bit hash changes cancelling is
// 2^-64 and is not a risk worth a sort on the game thread.
struct Digest {
    uint64_t sum;
    uint16_t count;
    bool     overflow;
};

inline Digest DigestOf(const Set& s)
{
    Digest d;
    d.sum = 0;
    d.count = (uint16_t)(s.n < 0 ? 0 : (s.n > 0xFFFF ? 0xFFFF : s.n));
    d.overflow = s.overflow;
    for (int i = 0; i < s.n; ++i) d.sum += ItemHash(s.it[i].key, s.it[i].value);
    return d;
}

// ---------------------------------------------------------------------------
// The per-item diff
// ---------------------------------------------------------------------------
// Emitted only for a category whose digest has disagreed past the grace window, so its
// cost is paid once per real desync rather than per audit pass.
struct DiffRow {
    char    key[kKeyCap];
    bool    have_local;
    bool    have_peer;
    int64_t local;
    int64_t peer;
};

static const int kMaxDiffRows = 64;

// Writes at most `cap` rows; returns the number of rows that DIFFER (which may exceed
// `cap`, so the caller can say how many were not printed).  A row where both sides hold
// the key with the same value is never written.
// [coordinator] A SCOPED category's membership follows what each machine has streamed in
// (movers it tracks, bosses it holds a copy of), so a key present on one side only is a
// streaming fact, not a desync; only keys BOTH hold are compared.  MEASURED (Queen run):
// 701 `movers` and 43 `boss` lines, every one `[absent on the host/peer]`.
inline bool ScopedPresence(uint8_t cat) { return cat == CAT_MOVERS || cat == CAT_BOSS; }

inline int DiffSets(const Set& local, const Set& peer, DiffRow* out, int cap, int* total_out)
{
    int wrote = 0;
    int differed = 0;
    const bool scoped = ScopedPresence(local.cat ? local.cat : peer.cat);
    for (int i = 0; i < local.n; ++i) {
        int j = peer.Find(local.it[i].key);
        if (scoped && j < 0) continue;
        bool same = (j >= 0) && peer.it[j].value == local.it[i].value;
        if (same) continue;
        ++differed;
        if (wrote >= cap) continue;
        DiffRow& r = out[wrote++];
        int k = 0;
        for (; local.it[i].key[k] && k < kKeyCap - 1; ++k) r.key[k] = local.it[i].key[k];
        r.key[k] = 0;
        r.have_local = true;
        r.local = local.it[i].value;
        r.have_peer = j >= 0;
        r.peer = j >= 0 ? peer.it[j].value : 0;
    }
    for (int j = 0; j < peer.n; ++j) {
        if (local.Find(peer.it[j].key) >= 0) continue;
        if (scoped) continue;
        ++differed;
        if (wrote >= cap) continue;
        DiffRow& r = out[wrote++];
        int k = 0;
        for (; peer.it[j].key[k] && k < kKeyCap - 1; ++k) r.key[k] = peer.it[j].key[k];
        r.key[k] = 0;
        r.have_local = false;
        r.local = 0;
        r.have_peer = true;
        r.peer = peer.it[j].value;
    }
    if (total_out) *total_out = differed;
    return wrote;
}

// ---------------------------------------------------------------------------
// Key helpers -- the canonical text form of every identity
// ---------------------------------------------------------------------------
// GUIDs are 32 lowercase hex characters, the game's own field order preserved: both
// machines read the same 16 bytes out of the same cooked data, so this is a key rather
// than a negotiation.
inline void KeyHex(char* out, int cap, const uint32_t g[4])
{
    static const char* H = "0123456789abcdef";
    int w = 0;
    for (int i = 0; i < 4; ++i) {
        for (int s = 28; s >= 0; s -= 4) {
            if (w < cap - 1) out[w++] = H[(g[i] >> s) & 0xF];
        }
    }
    if (cap > 0) out[w < cap ? w : cap - 1] = 0;
}

inline void KeyHex32(char* out, int cap, uint32_t v)
{
    static const char* H = "0123456789abcdef";
    int w = 0;
    for (int s = 28; s >= 0; s -= 4) {
        if (w < cap - 1) out[w++] = H[(v >> s) & 0xF];
    }
    if (cap > 0) out[w < cap ? w : cap - 1] = 0;
}

// Appends ":<suffix>" so one entity can contribute several items (a mover's four axes)
// without a second key convention.
inline void KeySuffix(char* out, int cap, const char* base, const char* suffix)
{
    int w = 0;
    for (; base && base[w] && w < cap - 1; ++w) out[w] = base[w];
    if (w < cap - 1) out[w++] = ':';
    for (int i = 0; suffix && suffix[i] && w < cap - 1; ++i) out[w++] = suffix[i];
    if (cap > 0) out[w < cap ? w : cap - 1] = 0;
}

// Round to the nearest multiple of `step`, half away from zero.  A bucket exists so a
// value that legitimately lags by one frame (a mover applied a tick late) does not read
// as a disagreement; it must be large enough to absorb the lag and small enough that a
// real divergence still changes the bucket.
inline int32_t Bucket(int32_t v, int32_t step)
{
    if (step <= 1) return v;
    int32_t half = step / 2;
    int32_t q = (v >= 0) ? (v + half) / step : -((-(int64_t)v + half) / step);
    return (int32_t)((int64_t)q * step);
}

// The buckets, named once so the doc and the code cannot drift.
//   positions 8 cm  (64 wire units: the wire is 1/8 cm, WIRE.md 3.5)
//   angles    1/64 turn (1024 wire units: the wire is 1/65536 turn)
//   boss HP   10 % steps, so a bar that is a frame behind is the same bucket
static const int32_t kPosStep  = 64;
static const int32_t kAngStep  = 1024;
static const int     kBossHpBuckets = 10;
static const int     kHpUnknown = 0xFF;

inline int HpBucket(float hp, float hp_max)
{
    if (!(hp_max > 0.0f) || !(hp >= 0.0f)) return kHpUnknown;
    float f = hp / hp_max;
    if (f > 1.0f) f = 1.0f;
    int b = (int)(f * (float)kBossHpBuckets);
    if (b < 0) b = 0;
    if (b >= kBossHpBuckets) b = kBossHpBuckets - 1;
    return b;
}

// ---------------------------------------------------------------------------
// The comparison state machine
// ---------------------------------------------------------------------------
// Pure.  It is fed this machine's digests and the partner's, and it says what to LOG and
// what to ASK FOR.  It never logs, never sends, and never touches a UObject, which is
// what lets test_audit.cpp drive every path (including the grace window) in microseconds.
static const uint32_t kGraceMs     = 3000;   // a mismatch must persist this long to be reported
static const uint32_t kRepeatMs    = 30000;  // a still-open desync is re-reported this often
static const uint32_t kPeerStaleMs = 30000;  // a silent partner's digests stop being compared
static const uint32_t kReqRetryMs  = 2000;   // an unanswered item request is re-sent this often
static const uint32_t kItemsFreshMs = 2000;  // an item dump older than this is re-requested

struct CatIn {
    uint8_t  cat;
    uint64_t digest;
    uint16_t count;
};

enum EvKind : uint8_t {
    EV_DESYNC      = 1,   // a category disagreed past the grace window
    EV_HEALED      = 2,   // a category that was open now agrees
    EV_ONE_SIDED   = 3,   // a category present on one machine and not the other
    EV_MAP_SPLIT   = 4,   // the two machines are in different worlds; data categories skipped
};

struct Event {
    uint8_t  kind;
    uint8_t  cat;
    uint32_t age_ms;
    uint16_t local_count;
    uint16_t peer_count;
};

class Board {
public:
    Board() { Reset(); }

    void Reset()
    {
        for (int i = 0; i < kMaxCats; ++i) {
            CatState& s = s_[i];
            s.used = false;
            s.local_set = false;
            s.local_digest = 0;
            s.local_count = 0;
            s.peer_set = false;
            s.peer_digest = 0;
            s.peer_count = 0;
            s.peer_at = 0;
            s.open = false;
            s.open_at = 0;
            s.last_report = 0;
            s.one_sided = 0;
            s.req_at = 0;
            s.diff_at = 0;
            s.items_at = 0;
            s.items_total = 0;
            s.items_got = 0;
            s.items_complete = false;
        }
        peer_seq_ = 0;
        peer_map_ = 0;
        have_peer_ = false;
        map_split_ = false;
        compared_ = 0;
        items_compared_ = 0;
        for (int i = 0; i < kMaxCats; ++i) s_[i].local_set = false;
    }

    // ---- local side --------------------------------------------------------
    // Called once per audit pass, before SetLocal.  A category the local build does not
    // collect at all is simply never set, which is how a DISABLED lane is expressed: two
    // machines with different switches produce a one-sided category, not a desync.
    void BeginPass()
    {
        for (int i = 0; i < kMaxCats; ++i) s_[i].local_set = false;
    }

    void SetLocal(uint8_t cat, uint64_t digest, uint16_t count)
    {
        CatState* s = Slot(cat);
        if (!s) return;
        s->local_set = true;
        s->local_digest = digest;
        s->local_count = count;
    }

    // ---- peer side ---------------------------------------------------------
    // Feed one K_AUDIT body.  `map_hash` is the hash of the partner's own map name: if it
    // differs from ours the two are in different worlds and comparing the data categories
    // would produce a dozen meaningless rows, so the board suppresses them and reports the
    // split once.
    int OnPeer(uint32_t seq, uint32_t map_hash, uint32_t local_map_hash,
               const CatIn* cats, int n, uint32_t now, Event* ev, int evcap)
    {
        int wrote = 0;
        peer_seq_ = seq;
        peer_map_ = map_hash;
        have_peer_ = true;
        map_split_ = (map_hash != 0 && local_map_hash != 0 && map_hash != local_map_hash);

        for (int i = 0; i < kMaxCats; ++i) s_[i].peer_set = false;

        for (int i = 0; i < n; ++i) {
            CatState* s = Slot(cats[i].cat);
            if (!s) continue;
            s->peer_set = true;
            s->peer_digest = cats[i].digest;
            s->peer_count = cats[i].count;
            s->peer_at = now;
        }

        if (map_split_ && wrote < evcap) {
            Event& e = ev[wrote++];
            e.kind = EV_MAP_SPLIT;
            e.cat = CAT_MAP;
            e.age_ms = 0;
            e.local_count = 0;
            e.peer_count = 0;
        }
        return wrote;
    }

    // ---- the comparison ----------------------------------------------------
    // Called after SetLocal and OnPeer.  Emits the transitions; a steady state emits
    // nothing, which is what makes the log readable over an hour.
    int Update(uint32_t now, Event* ev, int evcap)
    {
        int wrote = 0;
        compared_ = 0;
        items_compared_ = 0;

        for (int i = 0; i < kMaxCats; ++i) {
            CatState& s = s_[i];
            if (!s.used) continue;
            // Before the partner has ever spoken there is nothing to compare against, and a
            // category we do collect would otherwise read as "enabled on one side only" on
            // every category at boot -- twelve false lines on a correct session.
            if (!have_peer_) continue;
            bool peer_live = s.peer_set && Fresh(s.peer_at, now);
            // A category present on one machine and absent on the other is a SWITCH
            // difference, not a desync: report it once and never compare it.
            if (s.local_set && !s.peer_set) {
                if (s.one_sided != 2) {
                    s.one_sided = 2;
                    if (wrote < evcap) Emit(ev[wrote++], EV_ONE_SIDED, (uint8_t)(i + 1), now, s);
                }
                continue;
            }
            if (!s.local_set && peer_live) {
                if (s.one_sided != 1) {
                    s.one_sided = 1;
                    if (wrote < evcap) Emit(ev[wrote++], EV_ONE_SIDED, (uint8_t)(i + 1), now, s);
                }
                continue;
            }
            if (!s.local_set || !peer_live) continue;
            s.one_sided = 0;

            // The map itself is always compared: it is the one category whose disagreement
            // EXPLAINS every other one.
            uint8_t cat = (uint8_t)(i + 1);
            if (map_split_ && cat != CAT_MAP) continue;

            ++compared_;
            items_compared_ += (int)s.local_count;

            bool same = (s.local_digest == s.peer_digest) && (s.local_count == s.peer_count);
            // [coordinator] digests that differ but whose items were diffed and all matched
            // (a scoped category, or a count-only race) stay agreed until either side changes.
            if (!same && s.agreed && s.local_digest == s.agreed_l && s.peer_digest == s.agreed_p)
                same = true;
            if (same) {
                if (s.open) {
                    s.open = false;
                    s.open_at = 0;
                    s.last_report = 0;
                    s.diff_at = 0;
                    s.items_complete = false;
                    if (wrote < evcap) Emit(ev[wrote++], EV_HEALED, cat, now, s);
                }
                continue;
            }
            if (!s.open) {
                s.open = true;
                s.open_at = now;
                s.last_report = 0;
                s.diff_at = 0;
                s.items_complete = false;
            }
            uint32_t age = Age(now - s.open_at, now);
            if (age < kGraceMs) continue;         // in flight: say nothing
            if (s.last_report != 0 && (uint32_t)(now - s.last_report) < kRepeatMs) continue;
            s.last_report = now;
            if (wrote < evcap) Emit(ev[wrote++], EV_DESYNC, cat, now, s);
        }
        return wrote;
    }

    // ---- what to ask for ---------------------------------------------------
    // One category at a time: the per-item body is chunked across ticks and the receiver
    // accumulates one dump at a time, so a second request would interleave two sets.
    //
    // AND ONCE PER EPISODE, NOT ONCE PER RETRY.  The first live run asked every 2 s and
    // re-printed the same diff each time (1843 `audit: DESYNC` lines in four minutes), which
    // is exactly the noise this module exists not to be.  A category that has already been
    // diffed is re-asked only on the same 30 s clock the repeat report uses.
    uint8_t NextRequest(uint32_t now)
    {
        for (int i = 0; i < kMaxCats; ++i) {
            CatState& s = s_[i];
            if (!s.used || !s.open) continue;
            if (s.local_set == false || s.peer_set == false) continue;
            if (Age(now - s.open_at, now) < kGraceMs) continue;
            if (s.diff_at != 0 && (uint32_t)(now - s.diff_at) < kRepeatMs) continue;
            if (s.req_at != 0 && (uint32_t)(now - s.req_at) < kReqRetryMs) continue;
            s.req_at = now;
            s.items_complete = false;
            s.items_got = 0;
            s.items_total = 0;
            return (uint8_t)(i + 1);
        }
        return 0xFF;
    }

    // The per-item diff for `cat` has been printed, so the next request waits for the
    // episode's repeat clock.
    void MarkDiffed(uint8_t cat, uint32_t now)
    {
        CatState* s = Slot(cat);
        if (s) s->diff_at = now;
    }

    // [coordinator] The item diff found no real difference: remember this digest pair as
    // agreed and close the episode, so the summary does not report a desync nobody can see.
    void MarkAgreed(uint8_t cat)
    {
        CatState* s = Slot(cat);
        if (!s) return;
        s->agreed = true;
        s->agreed_l = s->local_digest;
        s->agreed_p = s->peer_digest;
        s->open = false;
        s->open_at = 0;
        s->last_report = 0;
        s->diff_at = 0;
    }

    // The partner's chunked item dump for `cat`.  A dump is complete when every row the
    // header claimed has arrived, which is what `items_complete` gates the diff on.
    void OnItems(uint8_t cat, uint16_t total, uint16_t got, uint32_t now)
    {
        CatState* s = Slot(cat);
        if (!s) return;
        s->items_total = total;
        s->items_got = got;
        if (got >= total) {
            s->items_complete = true;
            s->items_at = now;
        }
    }

    bool ItemsReady(uint8_t cat, uint32_t now) const
    {
        const CatState* s = SlotC(cat);
        if (!s || !s->items_complete) return false;
        return (uint32_t)(now - s->items_at) <= kItemsFreshMs;
    }

    uint32_t Age(uint8_t cat, uint32_t now) const
    {
        const CatState* s = SlotC(cat);
        if (!s || !s->open) return 0;
        return Age((uint32_t)(now - s->open_at), now);
    }

    // ---- queries -----------------------------------------------------------
    int  CatsCompared() const { return compared_; }
    int  ItemsCompared() const { return items_compared_; }
    bool MapSplit() const { return map_split_; }
    bool HavePeer() const { return have_peer_; }
    uint32_t PeerMapHash() const { return peer_map_; }
    uint32_t PeerSeq() const { return peer_seq_; }

    bool OpenCount(int* out_cats, uint32_t now) const
    {
        int n = 0;
        for (int i = 0; i < kMaxCats; ++i) {
            const CatState& s = s_[i];
            if (!s.used || !s.open) continue;
            if (Age((uint32_t)(now - s.open_at), now) < kGraceMs) continue;
            ++n;
        }
        if (out_cats) *out_cats = n;
        return n > 0;
    }

    // Which categories are currently open past the grace window, for the summary line.
    int OpenList(uint8_t* out, int cap, uint32_t now) const
    {
        int n = 0;
        for (int i = 0; i < kMaxCats && n < cap; ++i) {
            const CatState& s = s_[i];
            if (!s.used || !s.open) continue;
            if (Age((uint32_t)(now - s.open_at), now) < kGraceMs) continue;
            out[n++] = (uint8_t)(i + 1);
        }
        return n;
    }

    // Test seam: is this category tracking a mismatch at all (inside or past the window)?
    bool IsOpen(uint8_t cat) const { const CatState* s = SlotC(cat); return s && s->open; }

private:
    struct CatState {
        bool     used;
        bool     local_set;
        uint64_t local_digest;
        uint16_t local_count;
        bool     peer_set;
        uint64_t peer_digest;
        uint16_t peer_count;
        uint32_t peer_at;
        bool     open;
        uint32_t open_at;
        uint32_t last_report;
        uint8_t  one_sided;      // 0 none, 1 peer-only, 2 local-only
        uint32_t req_at;
        uint32_t diff_at;        // when this episode's per-item diff was printed (0 = never)
        uint32_t items_at;
        uint16_t items_total;
        uint16_t items_got;
        bool     items_complete;
        bool     agreed = false;     // [coordinator] the last item diff found nothing
        uint64_t agreed_l = 0, agreed_p = 0;
    };

    CatState* Slot(uint8_t cat)
    {
        if (!CatValid(cat)) return nullptr;
        CatState& s = s_[cat - 1];
        s.used = true;
        return &s;
    }

    const CatState* SlotC(uint8_t cat) const
    {
        if (!CatValid(cat)) return nullptr;
        return &s_[cat - 1];
    }

    static uint32_t Age(uint32_t d, uint32_t) { return d; }

    static bool Fresh(uint32_t at, uint32_t now)
    {
        return now >= at && (uint32_t)(now - at) <= kPeerStaleMs;
    }

    static void Emit(Event& e, uint8_t kind, uint8_t cat, uint32_t now, const CatState& s)
    {
        e.kind = kind;
        e.cat = cat;
        e.age_ms = s.open ? (uint32_t)(now - s.open_at) : 0;
        e.local_count = s.local_count;
        e.peer_count = s.peer_count;
    }

    CatState s_[kMaxCats];
    uint32_t peer_seq_;
    uint32_t peer_map_;
    bool     have_peer_;
    bool     map_split_;
    int      compared_;
    int      items_compared_;
};

} // namespace audit
} // namespace qd
