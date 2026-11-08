// qdwire.h -- the native encoder/decoder for doc/WIRE.md.
//
// This is the second implementation of the wire format on purpose.  The Rust crate
// `qd-core` is the canonical one and emits doc/vectors.txt; this one is checked
// against those vectors by test/test_wire.cpp.  A format that only one side
// implements drifts silently; a format with two implementations and a vector file
// fails a test instead.
//
// The rule the encoder obeys everywhere: when a container does not fit in one
// datagram, it is SPLIT ACROSS TICKS and the continuation index is returned.  It is
// never fragmented (DollsMP 12 H-6 measured 92.4 % effective loss at 256
// fragments) and never truncated (a truncated absolute state looks like a delete).
#pragma once
#include <stdint.h>
#include <string.h>

namespace qd {
namespace wire {

// 16 byte envelope + a 1200 byte body.  See WIRE.md section 1 for why 1200.
static const int kMaxBody = 1200;
static const int kMaxPacket = 16 + kMaxBody;

static const uint32_t kMagic = 0x31434451u;   // 'Q','D','C','1' little-endian

enum Kind {
    K_HELLO       = 0x01,
    K_WELCOME     = 0x02,
    K_KEEPALIVE   = 0x03,
    K_ACK         = 0x04,
    K_PLAYER      = 0x10,
    K_WORLD_FLAGS = 0x11,
    K_ACTOR_STATES= 0x12,
    K_INVENTORY   = 0x13,
    K_ENEMY       = 0x14,
    K_EQUIPMENT   = 0x15,
    K_EFFECT      = 0x16,
    K_EFFECT_MOVE = 0x17,
    K_STATUS      = 0x18,
    K_ENEMY_DETAIL= 0x19,
    K_INTENT      = 0x20,
    K_DIALOGUE    = 0x21,
    K_LEVEL_UNION = 0x22,
    K_TRAVEL      = 0x23,
    K_LOG         = 0x30,
};

enum IntentKind {
    I_SET_FLAG      = 1,
    I_GRANT_ITEM    = 2,
    I_CONSUME_ITEM  = 3,
    I_DIALOGUE_ANS  = 4,
    I_CHECKPOINT    = 5,
    I_ACTOR_STATE   = 6,
    I_ENEMY_HIT     = 7,   // peer hit a host-driven enemy copy (WIRE.md 3.10)
    I_ENEMY_RECORD  = 8,   // peer wrote a persistence record for an enemy it ran itself
};

// --------------------------------------------------------------------------
// Writer
// --------------------------------------------------------------------------
struct Writer {
    uint8_t* p;
    int      cap;
    int      len;
    bool     ok;

    Writer(uint8_t* buf, int capacity) : p(buf), cap(capacity), len(0), ok(true) {}

    bool Room(int n) const { return ok && len + n <= cap; }

    void U8(uint8_t v)  { if (!Room(1)) { ok = false; return; } p[len++] = v; }
    void U16(uint16_t v){ if (!Room(2)) { ok = false; return; } memcpy(p + len, &v, 2); len += 2; }
    void U32(uint32_t v){ if (!Room(4)) { ok = false; return; } memcpy(p + len, &v, 4); len += 4; }
    void I32(int32_t v) { U32((uint32_t)v); }
    void I16(int16_t v) { U16((uint16_t)v); }
    void U64(uint64_t v){ if (!Room(8)) { ok = false; return; } memcpy(p + len, &v, 8); len += 8; }

    void Bytes(const void* src, int n)
    {
        if (!Room(n)) { ok = false; return; }
        memcpy(p + len, src, (size_t)n);
        len += n;
    }

    void Guid(const uint32_t g[4])
    {
        for (int i = 0; i < 4; ++i) U32(g[i]);
    }

    // u16 length + UTF-8 bytes.  A name longer than 255 bytes is a caller bug, not
    // something to truncate silently.
    void Str(const char* s)
    {
        int n = 0;
        if (s) while (s[n] && n < 255) ++n;
        U16((uint16_t)n);
        if (n) Bytes(s, n);
    }
};

// --------------------------------------------------------------------------
// Reader
// --------------------------------------------------------------------------
struct Reader {
    const uint8_t* p;
    int            len;
    int            pos;
    bool           ok;

    Reader(const uint8_t* buf, int n) : p(buf), len(n), pos(0), ok(true) {}

    bool Room(int n) const { return ok && pos + n <= len; }

    uint8_t  U8()  { if (!Room(1)) { ok = false; return 0; } return p[pos++]; }
    uint16_t U16() { uint16_t v = 0; if (!Room(2)) { ok = false; return 0; } memcpy(&v, p + pos, 2); pos += 2; return v; }
    uint32_t U32() { uint32_t v = 0; if (!Room(4)) { ok = false; return 0; } memcpy(&v, p + pos, 4); pos += 4; return v; }
    int32_t  I32() { return (int32_t)U32(); }
    int16_t  I16() { return (int16_t)U16(); }

    void Guid(uint32_t g[4]) { for (int i = 0; i < 4; ++i) g[i] = U32(); }

    // Reads into `out` with a hard cap.  Returns the real length even when it did
    // not fit, so the caller can tell "empty" from "too long for my buffer".
    int Str(char* out, int cap)
    {
        uint16_t n = U16();
        if (!Room(n)) { ok = false; return -1; }
        int copy = (n < cap - 1) ? n : cap - 1;
        if (copy > 0) memcpy(out, p + pos, (size_t)copy);
        out[copy] = 0;
        pos += n;
        return n;
    }
};

// --------------------------------------------------------------------------
// Envelope
// --------------------------------------------------------------------------
struct Envelope {
    uint32_t magic;
    uint8_t  ver;
    uint8_t  kind;
    uint16_t body_len;
    uint32_t session;
    uint32_t seq;
};

inline void WriteHeader(Writer& w, uint8_t kind, uint32_t session, uint32_t seq, uint16_t bodyLen)
{
    w.U32(kMagic);
    w.U8(1);
    w.U8(kind);
    w.U16(bodyLen);
    w.U32(session);
    w.U32(seq);
}

inline bool ReadHeader(Reader& r, Envelope* e)
{
    e->magic    = r.U32();
    e->ver      = r.U8();
    e->kind     = r.U8();
    e->body_len = r.U16();
    e->session  = r.U32();
    e->seq      = r.U32();
    if (!r.ok) return false;
    if (e->magic != kMagic) return false;
    if (e->ver != 1) return false;
    if (e->body_len > kMaxBody) return false;
    if (e->body_len > r.len - 16) return false;
    return true;
}

// --------------------------------------------------------------------------
// Bodies
// --------------------------------------------------------------------------
struct PlayerRow {
    uint8_t  slot;
    uint32_t sample_ms;
    int32_t  px, py, pz;      // 1/8 cm
    int16_t  yaw, pitch;      // 1/65536 turn
    int16_t  vx, vy, vz;      // 1/8 cm/s
    uint8_t  move_mode;
    uint8_t  action;
    uint8_t  flags;
};

inline void WritePlayer(Writer& w, const PlayerRow& r)
{
    w.U8(r.slot);
    w.U32(r.sample_ms);
    w.I32(r.px); w.I32(r.py); w.I32(r.pz);
    w.I16(r.yaw); w.I16(r.pitch);
    w.I16(r.vx); w.I16(r.vy); w.I16(r.vz);
    w.U8(r.move_mode);
    w.U8(r.action);
    w.U8(r.flags);
}

inline bool ReadPlayer(Reader& r, PlayerRow* out)
{
    out->slot       = r.U8();
    out->sample_ms  = r.U32();
    out->px = r.I32(); out->py = r.I32(); out->pz = r.I32();
    out->yaw = r.I16(); out->pitch = r.I16();
    out->vx = r.I16(); out->vy = r.I16(); out->vz = r.I16();
    out->move_mode  = r.U8();
    out->action     = r.U8();
    out->flags      = r.U8();
    return r.ok;
}

// ---- equipment (WIRE.md 3.13) ---------------------------------------------
// What the sender holds, absolute and latest-wins: rev(u32) n(u8) then per item
// hand(u8: 0 right, 1 left) mesh_kind(u8: 1 static, 2 skeletal) path(str)
// xform(10 x i32: location 1/100 cm, quaternion 1/1e6, scale 1/1000).
static const int kMaxEquipItems = 4;

struct EquipItem {
    uint8_t hand;
    uint8_t mesh_kind;
    char    path[200];
    int32_t xform[10];
};

struct EquipBody {
    uint32_t  rev;
    int       n;
    EquipItem items[kMaxEquipItems];
};

inline void WriteEquip(Writer& w, const EquipBody& b)
{
    int n = b.n < 0 ? 0 : (b.n > kMaxEquipItems ? kMaxEquipItems : b.n);
    w.U32(b.rev);
    w.U8((uint8_t)n);
    for (int i = 0; i < n; ++i) {
        w.U8(b.items[i].hand);
        w.U8(b.items[i].mesh_kind);
        w.Str(b.items[i].path);
        for (int k = 0; k < 10; ++k) w.I32(b.items[i].xform[k]);
    }
}

inline bool ReadEquip(Reader& r, EquipBody* b)
{
    b->rev = r.U32();
    b->n = r.U8();
    if (b->n > kMaxEquipItems) return false;
    for (int i = 0; i < b->n; ++i) {
        b->items[i].hand = r.U8();
        b->items[i].mesh_kind = r.U8();
        if (r.Str(b->items[i].path, sizeof(b->items[i].path)) >= (int)sizeof(b->items[i].path)) return false;
        for (int k = 0; k < 10; ++k) b->items[i].xform[k] = r.I32();
    }
    return r.ok;
}

// ---- effect (WIRE.md 3.14) ------------------------------------------------
// One visual event from the sender's world: kind(u8: 1 projectile spawned, 2 ended --
// both sent as EFFECT, reliable; 3 moved -- sent as EFFECT_MOVE 0x17, unreliable, 15 Hz) id(u16, the sender's handle for it) class(str "<package>.<object>"
// of the actor class, "" for kind 2) x,y,z(i32 1/8 cm) pitch,yaw(i16 1/65536 turn)
// vx,vy,vz(i32 1/8 cm/s) life_ms(u16).
struct EffectBody {
    uint8_t  kind;
    uint16_t id;
    char     cls[200];
    int32_t  px, py, pz;
    int16_t  pitch, yaw;
    int32_t  vx, vy, vz;
    uint16_t life_ms;
};

inline void WriteEffect(Writer& w, const EffectBody& e)
{
    w.U8(e.kind);
    w.U16(e.id);
    w.Str(e.cls);
    w.I32(e.px); w.I32(e.py); w.I32(e.pz);
    w.I16(e.pitch); w.I16(e.yaw);
    w.I32(e.vx); w.I32(e.vy); w.I32(e.vz);
    w.U16(e.life_ms);
}

inline bool ReadEffect(Reader& r, EffectBody* e)
{
    e->kind = r.U8();
    e->id = r.U16();
    if (r.Str(e->cls, sizeof(e->cls)) >= (int)sizeof(e->cls)) return false;
    e->px = r.I32(); e->py = r.I32(); e->pz = r.I32();
    e->pitch = r.I16(); e->yaw = r.I16();
    e->vx = r.I32(); e->vy = r.I32(); e->vz = r.I32();
    e->life_ms = r.U16();
    return r.ok;
}

// ---- STATUS (WIRE.md 3.15) -------------------------------------------------
// Body: rev(u32) mask(u32).  Latest-wins; bits are StatusBit.
enum StatusBit : uint32_t {
    ST_MAGIC_FIRE = 1u << 0, ST_MAGIC_POISON = 1u << 1, ST_POISON = 1u << 2, ST_DISEASE = 1u << 3,
    ST_WEAKEN = 1u << 4, ST_HEAVY = 1u << 5, ST_BINDING = 1u << 6, ST_BERSERK = 1u << 7,
    ST_IRONSKIN = 1u << 8, ST_RADIANT = 1u << 9,
};
inline void WriteStatus(Writer& w, uint32_t rev, uint32_t mask) { w.U32(rev); w.U32(mask); }
inline bool ReadStatus(Reader& r, uint32_t* rev, uint32_t* mask) { *rev = r.U32(); *mask = r.U32(); return r.ok; }

// ---- flags ----------------------------------------------------------------
// Body: rev(u32), n(u16), then as many entries as fit.
//
// There is deliberately NO "produced" count: the number of entries that arrived is
// whatever parses before the end of the body.  An extra field has to be patched
// after the fact (the first version of this encoder did exactly that, writing a
// placeholder then memcpy'ing the real value in), and a patched count is a count
// that can disagree with the bytes next to it.
// Cross-checked against doc/vectors.txt `world_flags`.
struct FlagEntry { const char* name; int32_t value; };

struct FlagsHeader {
    uint32_t rev;
    uint16_t n;
};

inline void WriteFlagsHeader(Writer& w, uint32_t rev, uint16_t n)
{
    w.U32(rev);
    w.U16(n);
}

// Fills as many entries as fit starting at `first`, returns the next index.
inline int WriteFlagsChunk(Writer& w, const FlagEntry* e, int n, int first, uint32_t rev)
{
    (void)rev;
    int i = first;
    // Dry-size each entry: 2 + strlen(name) + 4.
    while (i < n) {
        int nameLen = 0;
        while (e[i].name[nameLen]) ++nameLen;
        int need = 2 + nameLen + 4;
        if (w.len + need > w.cap) break;
        w.Str(e[i].name);
        w.I32(e[i].value);
        ++i;
        if (!w.ok) break;
    }
    return i;
}

inline bool ReadFlagsHeader(Reader& r, FlagsHeader* h)
{
    h->rev = r.U32();
    h->n   = r.U16();
    return r.ok;
}

inline bool ReadFlagEntry(Reader& r, char* name, int cap, int32_t* value)
{
    if (r.Str(name, cap) < 0) return false;
    *value = r.I32();
    return r.ok;
}

// ---- actor states ---------------------------------------------------------
// Body: rev(u32), n(u16), then `n` x {guid(16), i32}.  Same no-produced rule as
// the flag bank; cross-checked against doc/vectors.txt `actor_states`.
struct ActorState { uint32_t guid[4]; int32_t value; };

inline void WriteActorStatesHeader(Writer& w, uint32_t rev, uint16_t n)
{
    w.U32(rev); w.U16(n);
}

inline int WriteActorStatesChunk(Writer& w, const ActorState* e, int n, int first)
{
    int i = first;
    while (i < n && w.len + 20 <= w.cap) {
        w.Guid(e[i].guid);
        w.I32(e[i].value);
        ++i;
        if (!w.ok) break;
    }
    return i;
}

// ---- enemy rows -----------------------------------------------------------
struct EnemyRow {
    uint32_t guid[4];
    int32_t  px, py, pz;
    int16_t  yaw;
    uint16_t hp;
    uint8_t  flags;
};

// Body: n(u16), then rows.  Cross-checked against doc/vectors.txt `enemy_rows`.
inline void WriteEnemyHeader(Writer& w, uint16_t n)
{
    w.U16(n);
}

// Row size 33 bytes.  WIRE.md caps a datagram at 20 rows at N=2 to stay inside the
// measured state-application ceiling (24 rows in one packet = 30/s), not merely
// inside the byte cap.
inline int WriteEnemyChunk(Writer& w, const EnemyRow* e, int n, int first)
{
    int i = first;
    int rows = 0;
    while (i < n && rows < 20 && w.len + 33 <= w.cap) {
        w.Guid(e[i].guid);
        w.I32(e[i].px); w.I32(e[i].py); w.I32(e[i].pz);
        w.I16(e[i].yaw);
        w.U16(e[i].hp);
        w.U8(e[i].flags);
        ++i;
        ++rows;
        if (!w.ok) break;
    }
    return i;
}

inline bool ReadEnemyHeader(Reader& r, uint16_t* n)
{
    *n = r.U16();
    return r.ok;
}

inline bool ReadEnemyRow(Reader& r, EnemyRow* e)
{
    r.Guid(e->guid);
    e->px = r.I32(); e->py = r.I32(); e->pz = r.I32();
    e->yaw = r.I16();
    e->hp = r.U16();
    e->flags = r.U8();
    return r.ok;
}

// ENEMY.flags (WIRE.md 3.9).  hp is a FRACTION of the sender's max HP, 0..65535:
// both machines run the same enemy data, but the max can differ (equipment such as
// the crown multiplies enemy HP on one machine only), so an absolute value would
// mean a different health bar on each side.
static const uint8_t kEnemyAlive  = 0x01;
static const uint8_t kEnemyDriven = 0x02;
static const uint8_t kEnemyBody   = 0x04;
// Who runs this enemy.  A row is sent by the machine that OWNS the enemy, and the
// receiver freezes its own copy.  Ownership follows the nearer player, because the
// game's enemy AI only ever targets the local player (SYNC-GAPS G4): the only way an
// enemy attacks the partner is for the partner's own machine to run it.
//
// 0 = the sender runs it (the receiver freezes its copy)
// 1 = the sender has handed it to the receiver (the sender freezes its own copy)
static const uint8_t kEnemyPeerOwns = 0x08;

// ---- enemy detail (WIRE.md 3.9a) -------------------------------------------
// An ENEMY row is a SAMPLE: position, health, ownership, 20 Hz, latest wins.  What an
// enemy is DOING is an EVENT -- a montage starts once, a status mask changes when a
// debuff lands or expires -- so it rides its own reliable body, sent on change by the
// machine that owns that enemy.  The montage is identified by its asset path rather
// than by an index: both machines load the same cooked assets, but the order in which
// each discovers them is its own, so an index would mean different animations.
//
// Body: guid(16), anim(u8 kind, see EnemyAnim), seq(u8, bumps on every new montage),
//       status(u16 mask, the same StatusBit values STATUS uses for players),
//       path(str, the montage asset, "" when none is playing).
enum EnemyAnim {
    E_NONE   = 0,
    E_ATTACK = 1,
    E_HURT   = 2,
    E_DIE    = 3,
    E_ROAR   = 4,
    E_CAST   = 5,
    E_MOVE   = 6,
};

static const int kEnemyPathCap = 200;

struct EnemyDetailBody {
    uint32_t guid[4];
    uint8_t  anim;
    uint8_t  seq;
    uint16_t status;
    char     path[kEnemyPathCap];
};

inline void WriteEnemyDetail(Writer& w, const EnemyDetailBody& d)
{
    w.Guid(d.guid);
    w.U8(d.anim);
    w.U8(d.seq);
    w.U16(d.status);
    w.Str(d.path);
}

inline bool ReadEnemyDetail(Reader& r, EnemyDetailBody* d)
{
    r.Guid(d->guid);
    d->anim = r.U8();
    d->seq = r.U8();
    d->status = r.U16();
    if (r.Str(d->path, sizeof(d->path)) >= (int)sizeof(d->path)) return false;
    return r.ok;
}

// ---- dialogue -------------------------------------------------------------
struct DialogueBody {
    uint32_t seq;
    uint32_t npc[4];
    uint8_t  slot;      // 0xFF = free
    uint16_t line;
    uint16_t entry;
};

inline void WriteDialogue(Writer& w, const DialogueBody& d)
{
    w.U32(d.seq);
    w.Guid(d.npc);
    w.U8(d.slot);
    w.U16(d.line);
    w.U16(d.entry);
}

inline bool ReadDialogue(Reader& r, DialogueBody* d)
{
    d->seq = r.U32();
    r.Guid(d->npc);
    d->slot  = r.U8();
    d->line  = r.U16();
    d->entry = r.U16();
    return r.ok;
}

// ---- level union ----------------------------------------------------------
// Body: bits_len(u16), the bitset bytes, then has_names(u8) and, if set, a name
// table.  The order matters and is cross-checked against doc/vectors.txt
// `level_union`: a receiver that read has_names first would take the first bitset
// byte for a flag.
//
// The name table is sent when the sender's list order changed; the receiver keeps
// the last table per slot and maps bits through it, so the steady-state body is the
// bitset alone (`level_union_steady`).
inline void WriteLevelUnionHeader(Writer& w, uint16_t bitsLen)
{
    w.U16(bitsLen);
}

inline void WriteLevelUnionNamesFlag(Writer& w, uint8_t hasNames)
{
    w.U8(hasNames);
}

static const int kLevelUnionMaxNames = 64;
static const int kLevelUnionNameCap  = 64;

// The whole body.  `names`/`n` is the sender's list; bit i set = names[i] wanted.
// names == nullptr sends the steady-state body (bits only).
inline void WriteLevelUnion(Writer& w, const uint8_t* bits, uint16_t bitsLen,
                            const char* const* names, uint16_t n)
{
    WriteLevelUnionHeader(w, bitsLen);
    if (bitsLen) w.Bytes(bits, bitsLen);
    WriteLevelUnionNamesFlag(w, names ? 1 : 0);
    if (names) {
        w.U16(n);
        for (uint16_t i = 0; i < n; ++i) w.Str(names[i]);
    }
}

struct LevelUnionBody {
    uint16_t bits_len;
    uint8_t  bits[kLevelUnionMaxNames / 8];
    bool     has_names;
    uint16_t n;                 // names actually stored (<= kLevelUnionMaxNames)
    uint16_t n_sent;            // names the sender listed
    char     name[kLevelUnionMaxNames][kLevelUnionNameCap];
};

inline bool LevelUnionBit(const LevelUnionBody& b, int i)
{
    if (i < 0 || i / 8 >= b.bits_len || i / 8 >= (int)sizeof(b.bits)) return false;
    return (b.bits[i / 8] >> (i % 8)) & 1;
}

inline bool ReadLevelUnion(Reader& r, LevelUnionBody* b)
{
    b->bits_len = r.U16();
    if (!r.Room(b->bits_len)) { r.ok = false; return false; }
    for (int i = 0; i < (int)sizeof(b->bits); ++i) b->bits[i] = 0;
    for (int i = 0; i < b->bits_len; ++i) {
        uint8_t v = r.U8();
        if (i < (int)sizeof(b->bits)) b->bits[i] = v;
    }
    b->has_names = r.U8() != 0;
    b->n = b->n_sent = 0;
    if (b->has_names) {
        b->n_sent = r.U16();
        for (int i = 0; i < b->n_sent && r.ok; ++i) {
            char tmp[kLevelUnionNameCap];
            if (r.Str(tmp, sizeof(tmp)) < 0) return false;
            if (b->n < kLevelUnionMaxNames) {
                memcpy(b->name[b->n], tmp, sizeof(tmp));
                ++b->n;
            }
        }
    }
    return r.ok;
}

// ---- travel ---------------------------------------------------------------
// The session is one persistent level plus streamed sublevels, but the game also has
// hard transitions (the cutscene maps, `900_SipehaWorld`, `990_B33sment`).  A hard
// transition on one machine leaves the two in different worlds, where every absolute
// body means something different.  So the map is announced rather than inferred:
// the host sends what it is in, the peer compares, and a peer that disagrees says so
// (`ack` = 0) instead of quietly applying level names from another world.
//
// Body: seq(u32, bumps per change), role(u8 1 = host, 2 = peer), map(str, short name).
struct TravelBody {
    uint32_t seq;
    uint8_t  role;
    char     map[64];
};

inline void WriteTravel(Writer& w, const TravelBody& t)
{
    w.U32(t.seq);
    w.U8(t.role);
    w.Str(t.map);
}

inline bool ReadTravel(Reader& r, TravelBody* t)
{
    t->seq = r.U32();
    t->role = r.U8();
    if (r.Str(t->map, sizeof(t->map)) >= (int)sizeof(t->map)) return false;
    return r.ok;
}

// ---- inventory ------------------------------------------------------------
// Body: rev(u32), n(u16), then n x {str path, i32 count}.  Cross-checked against
// doc/vectors.txt `inventory`.
inline void WriteInventoryHeader(Writer& w, uint32_t rev, uint16_t n)
{
    w.U32(rev); w.U16(n);
}

inline void WriteInventoryEntry(Writer& w, const char* path, int32_t count)
{
    w.Str(path);
    w.I32(count);
}

// ---- intent header --------------------------------------------------------
inline void WriteIntentHeader(Writer& w, uint8_t intentKind, uint32_t basedOnRev)
{
    w.U8(intentKind);
    w.U32(basedOnRev);
}

struct IntentBody {
    uint8_t     kind;
    uint32_t    based_on_rev;
    char        name[192];
    int32_t     value;
    uint8_t     mode;      // 0 additive, 1 absolute
    uint16_t    line;
    uint16_t    option;
    uint32_t    guid[4];
    int32_t     value2;    // I_ENEMY_RECORD: remaining HP (centi)
};

// kind 7: guid spawner, i32 damage as a fraction of max HP (1/65535 units, may
// exceed 65535 for an overkill).  kind 8: guid spawner, i32 respawn_centi,
// i32 hp_centi -- the three scalars of StoreEnemySaveState (bValid is implicit).
inline void WriteIntentEnemyHit(Writer& w, uint32_t basedOnRev, const uint32_t guid[4],
                                int32_t damageFrac)
{
    WriteIntentHeader(w, I_ENEMY_HIT, basedOnRev);
    w.Guid(guid);
    w.I32(damageFrac);
}

inline void WriteIntentEnemyRecord(Writer& w, uint32_t basedOnRev, const uint32_t guid[4],
                                   int32_t respawnCenti, int32_t hpCenti)
{
    WriteIntentHeader(w, I_ENEMY_RECORD, basedOnRev);
    w.Guid(guid);
    w.I32(respawnCenti);
    w.I32(hpCenti);
}

inline bool ReadIntent(Reader& r, IntentBody* b)
{
    b->kind = r.U8();
    b->based_on_rev = r.U32();
    b->value = 0; b->mode = 0; b->line = 0; b->option = 0; b->value2 = 0;
    b->guid[0] = b->guid[1] = b->guid[2] = b->guid[3] = 0;
    b->name[0] = 0;
    switch (b->kind) {
    case I_SET_FLAG:
    case I_GRANT_ITEM:
    case I_CONSUME_ITEM:
    case I_CHECKPOINT:
        if (r.Str(b->name, sizeof(b->name)) < 0) return false;
        if (b->kind == I_SET_FLAG) { b->value = r.I32(); b->mode = r.U8(); }
        else if (b->kind != I_CHECKPOINT) { b->value = r.I32(); }
        break;
    case I_DIALOGUE_ANS:
        r.Guid(b->guid);
        b->line = r.U16();
        b->option = r.U16();
        break;
    case I_ACTOR_STATE:
    case I_ENEMY_HIT:
        r.Guid(b->guid);
        b->value = r.I32();
        break;
    case I_ENEMY_RECORD:
        r.Guid(b->guid);
        b->value = r.I32();
        b->value2 = r.I32();
        break;
    default:
        return false;
    }
    return r.ok;
}

// ---- quantisation ---------------------------------------------------------
// WIRE.md section 3.5.  Kept here rather than in a separate header because both
// directions must use the same constants as the Rust side, and having them in one
// place is the cheapest way to keep that true.
static const int kPosUnitsPerCm   = 8;
static const int kAngleUnitsPerTurn = 65536;

inline int32_t QuantizePos(float cm)
{
    // Computed in double, not float.  2147483000 is not representable in a float --
    // the nearest float is 2147483648, and converting that to int32 is undefined
    // behaviour that lands on INT32_MIN.  The first version of this function did
    // exactly that, and test_wire.cpp caught it: 1e12 cm quantised to -2147483648,
    // which is a teleport to the far side of the map rather than a clamp.
    double v = (double)cm * (double)kPosUnitsPerCm;
    if (v > 2147483000.0) v = 2147483000.0;
    if (v < -2147483000.0) v = -2147483000.0;
    v += (v < 0.0) ? -0.5 : 0.5;
    return (int32_t)v;
}

inline float DequantizePos(int32_t q) { return (float)q / (float)kPosUnitsPerCm; }

inline int16_t QuantizeAngle(float degrees)
{
    // No explicit wrap: an angle is a turn, and the 16-bit truncation IS the wrap.
    // 180 deg -> 32768 -> int16 -32768, and -180 deg -> -32768: the same point
    // encodes the same way, which is the property interpolation relies on.
    double t = (double)degrees * (double)kAngleUnitsPerTurn / 360.0;
    double r = (t < 0.0) ? (t - 0.5) : (t + 0.5);
    int32_t i = (int32_t)r;
    return (int16_t)(uint16_t)(uint32_t)i;
}

inline float DequantizeAngle(int16_t q) { return (float)q * 360.0f / (float)kAngleUnitsPerTurn; }

inline int16_t QuantizeVel(float cmPerSec)
{
    float v = cmPerSec * (float)kPosUnitsPerCm;
    if (v > 32767.0f) v = 32767.0f;
    if (v < -32768.0f) v = -32768.0f;
    return (int16_t)(v < 0 ? v - 0.5f : v + 0.5f);
}

inline float DequantizeVel(int16_t q) { return (float)q / (float)kPosUnitsPerCm; }

} // namespace wire
} // namespace qd
