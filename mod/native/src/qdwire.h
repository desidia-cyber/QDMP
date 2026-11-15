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
    K_DAMAGE      = 0x24,
    K_CUTSCENE    = 0x25,
    K_MOVERS      = 0x26,
    K_LOOT        = 0x27,
    K_SUSPEND     = 0x28,
    K_BOSS        = 0x29,
    K_OBJECTS     = 0x2A,
    K_ENEMY_SPAWN = 0x2B,
    K_TRIGGER     = 0x2C,   // a partner's body entered/left a placed event volume (qdtrigger.h)
    // [progression] One death paid one player's XP; the other pays its own the same amount.
    // WIRE.md 3.26.
    K_KILL_XP     = 0x40,
    // [progression] "I saved; write your own slot now."  WIRE.md 3.27.
    K_SAVE        = 0x41,
    // [platforms] where a RIDER stands, in the platform's own axes (qdplatforms.h).  The
    // companion of K_MOVERS: MOVERS says where the platform is, this says where the person
    // on it stands.  Reserved for this module (0x48-0x4B).
    K_PLATFORM_RIDE = 0x48,
    K_DIALOGUE_VIEW = 0x50, // the dialogue cursor the talker is showing (qddialogue2.h)
    // [death-respawn] A player's death, respawn and rest, stated by the machine that OWNS
    // that player (qdrespawn.h).  Reliable: a death is an event, and a lost UP leaves the
    // partner's puppet where the body fell until the next position row.
    K_RESPAWN_DOWN = 0x54,
    K_RESPAWN_UP   = 0x55,
    K_RESPAWN_REST = 0x56,
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
    I_OBJECT_STATE  = 9,   // peer opened/activated a placed world object (WIRE.md 3.20)
};

// Which kinds the receiver acks and the sender retransmits (WIRE.md 2).  This is the
// one list both sides consult: qdnet.h acks exactly these and only these, and the Rust
// `Kind::is_reliable` mirrors it.
//
// It exists as a function because a hard-coded pair of case labels in the socket loop is
// how three kinds added later (ENEMY_DETAIL, TRAVEL, DAMAGE) ended up being sent
// reliably, never acked, retransmitted until the sender gave up, and logged as
// "reliable send abandoned" twice a second -- measured on the rig on 2026-11-08, with the
// travel heartbeat alone producing 5 attempts every 2 s for the life of the session.
inline bool IsReliableKind(uint8_t kind)
{
    // [progression] Tested FIRST, on its own line: the list below is edited by every lane
    // that adds a kind, so an append there is one more place this lane could silently
    // lose its ack and retransmission.
    if (kind == K_KILL_XP || kind == K_SAVE) return true;
    return kind == K_HELLO || kind == K_WELCOME || kind == K_INTENT ||
           kind == K_EFFECT || kind == K_ENEMY_DETAIL || kind == K_TRAVEL ||
           kind == K_DAMAGE || kind == K_SUSPEND || kind == K_BOSS ||
           kind == K_OBJECTS || kind == K_ENEMY_SPAWN || kind == K_TRIGGER ||
           kind == K_RESPAWN_DOWN || kind == K_RESPAWN_UP || kind == K_RESPAWN_REST;
}

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

// Optional VITALS tail on STATUS (WIRE.md 3.15, vitals tail): hp(u16) hp_max(u16) flags(u8), the
// sender's own player, whole HP points clamped to 65535.  A body that ends after `mask` is
// an older sender and carries no vitals; that is not an error.  This build has no player
// stamina (the player Blueprint declares none; only a leftover `BaseJumpStaminaCost` in
// PlayerBalance), so there is no stamina field to carry.
enum VitalsFlag : uint8_t { VF_VALID = 1, VF_DEAD = 2 };
struct Vitals { uint16_t hp; uint16_t hp_max; uint8_t flags; };
inline void WriteVitals(Writer& w, const Vitals& v) { w.U16(v.hp); w.U16(v.hp_max); w.U8(v.flags); }
inline bool ReadVitals(Reader& r, Vitals* v)
{
    if (!r.Room(5)) { v->hp = 0; v->hp_max = 0; v->flags = 0; return false; }
    v->hp = r.U16(); v->hp_max = r.U16(); v->flags = r.U8();
    return r.ok;
}

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

// Bits 4-5 carry the identity SOURCE of the row's `guid` (WIRE.md 3.9, qdenemyid.h):
//   0 = an authored spawner GUID (unchanged: the persisted-key path)
//   1 = a placed actor, keyed by a hash of its cooked "<world>/<level>/<name>" path
//   2 = a summon, keyed by a hash of its summoner's key and the summoner's ordinal
// Two 128-bit hashes of different things can collide, so the pair (source, guid) is the
// identity: a placed-path hash that happens to equal a spawner GUID is still a different
// enemy, and a live log can say which kind of key it is looking at.
static const uint8_t kEnemySrcShift = 4;
static const uint8_t kEnemySrcMask  = 0x30;
static const uint8_t kEnemySrcSpawner = 0;
static const uint8_t kEnemySrcPlaced  = 1;
static const uint8_t kEnemySrcSummon  = 2;

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
//       boss(u8, the boss phase the owner observed; 0 = not a boss / no shared phase),
//       path(str, the montage asset, "" when none is playing).
//
// `boss` is the one per-boss value the shipped build exposes to a sync layer at all.
// MEASURED (SYNC-RESEARCH 6): `Phase`, `bBoss`, `OnSubStateChanged` exist only in the
// exe's name block and in zero cooked packages, so there is no Blueprint phase field to
// drive; what DOES exist is the boss spawner's own state, which the owner can read and
// the follower can act on (the health bar and the arena exit, BOSS below).  The field is
// therefore a small observed ordinal, not a fabricated phase machine: 0 means "no boss
// phase observed", and a receiver that sees 0 simply never touch a phase.
//
// `seq` is per-enemy and wraps 255 -> 1.  A receiver keys "already applied" on the pair
// (seq, path), never on seq alone: two different attacks can both be seq 3 after enough
// swings.
enum EnemyAnim {
    E_NONE   = 0,
    E_ATTACK = 1,
    E_HURT   = 2,
    E_DIE    = 3,
    E_ROAR   = 4,
    E_CAST   = 5,
    E_MOVE   = 6,
};

// True for the anim kinds a player judges the game by, where a dropped event matters:
// the swing that hit them, the telegraph they had to dodge, the death they earned.
// `enemybw` uses this to let those bypass the cosmetic rate limits.
inline bool EnemyAnimCritical(uint8_t anim)
{
    return anim == E_ATTACK || anim == E_HURT || anim == E_DIE ||
           anim == E_CAST || anim == E_ROAR;
}

static const int kEnemyPathCap = 200;

struct EnemyDetailBody {
    uint32_t guid[4];
    uint8_t  anim;
    uint8_t  seq;
    uint16_t status;
    uint8_t  boss;
    char     path[kEnemyPathCap];
};

inline void WriteEnemyDetail(Writer& w, const EnemyDetailBody& d)
{
    w.Guid(d.guid);
    w.U8(d.anim);
    w.U8(d.seq);
    w.U16(d.status);
    w.U8(d.boss);
    w.Str(d.path);
}

inline bool ReadEnemyDetail(Reader& r, EnemyDetailBody* d)
{
    r.Guid(d->guid);
    d->anim = r.U8();
    d->seq = r.U8();
    d->status = r.U16();
    d->boss = r.U8();
    if (r.Str(d->path, sizeof(d->path)) >= (int)sizeof(d->path)) return false;
    return r.ok;
}

// ---- boss (WIRE.md 3.9b) ---------------------------------------------------
// Two things about a boss are per-machine calls that only that machine can make, and
// both were measured on the shipped build (SYNC-RESEARCH 6):
//
//   * the health bar: `PlayerStatsAndUtils_C.ActivateBossHPBar(Enemy, Group, ctx)` is a
//     Blueprint function library call whose `__WorldContext` is the CALLER's world, so
//     each machine raises it on its own player for its own copy of the boss;
//   * the arena exit: `EnemySpawner_C.PlayerTeleportedOutOfBossArena()` is a zero-argument
//     spawner event, overridden by 20 boss-spawner Blueprints.  The CAUSE is computed per
//     machine, but the FACT ("a player left the arena") is one bool.
//
// Body: guid(16, the boss enemy's key; zero for "no particular boss"),
//       flags(u8, see kBoss*), phase(u8, the owner's observed phase, 0 = none),
//       hp(u16, the boss's HP as the same 0..65535 fraction an ENEMY row carries).
//
// Reliable: a boss bar that fails to appear because a datagram was lost is the kind of
// failure a player reports as "the boss fight is broken".
struct BossBody {
    uint32_t guid[4];
    uint8_t  flags;
    uint8_t  phase;
    uint16_t hp;
};

// The boss's health bar should be shown on the receiver (raised on its own player).
static const uint8_t kBossBarOn    = 0x01;
// The boss's health bar should be hidden again (the fight ended, or the copy died).
static const uint8_t kBossBarOff   = 0x02;
// A player left the boss arena: the receiver raises its own spawner's exit event.
static const uint8_t kBossArenaExit = 0x04;
// R143/Session 20: the host ASKS whether the peer can show this key before raising the
// one shared bar.  Measured failure: the host selected `03FDBC21`, which existed only on
// the host (outside the 25 m mirror radius), and repaired a bar for it every 3 s while
// the peer logged `no local copy` and showed nothing.  A bar the partner cannot show is
// not a shared bar, so the selection is now negotiated:
//   kBossAsk  host -> peer: "do you hold a live copy of this key?"  (no state change)
//   kBossAck  peer -> host: yes -- the host may raise and publish this key.
//   kBossNack peer -> host: no  -- the host must pick another candidate (or none).
// The bits are mutually exclusive in a body; a body carries exactly one of them except a
// bar-on, which is the steady state after an ACK.
static const uint8_t kBossAsk     = 0x08;
static const uint8_t kBossAck     = 0x10;
static const uint8_t kBossNack    = 0x20;
// Session 29: a boss whose fight starts from a CONVERSATION (the Queen's greeting) began on
// the sender -- its player finished the greeting.  The receiver runs the same end-of-greeting
// event on its own dormant copy, so both machines fight an active boss.  Either direction.
static const uint8_t kBossBegin   = 0x40;
// Session 29: the boss's grab reached the RECEIVER's player (host -> peer).  The Queen's orb homes
// on her target, and on the host the partner is an inert body the orb cannot grab; the receiver
// runs the boss's own grab (`OrbProjectileHitPlayer`) on its copy against its own player.
static const uint8_t kBossGrab    = 0x80;

inline void WriteBoss(Writer& w, const BossBody& b)
{
    w.Guid(b.guid);
    w.U8(b.flags);
    w.U8(b.phase);
    w.U16(b.hp);
}

inline bool ReadBoss(Reader& r, BossBody* b)
{
    r.Guid(b->guid);
    b->flags = r.U8();
    b->phase = r.U8();
    b->hp = r.U16();
    return r.ok;
}

// ---- enemy spawn: the CLASS for a key the receiver holds no actor for (WIRE.md 3.9c) --
// THE GAP THIS CLOSES (Session 16)
// --------------------------------
// An enemy's identity is its spawner's authored GUID, and a spawner only spawns while ITS
// LOCAL player is inside its activation range.  So an enemy whose spawner activated on one
// machine exists there and NOT AT ALL on the other: measured in 780_SZ_Depths, the Green
// Dragon (`Dragon_Green_BP_C`, key 17181166) is a live actor on the owner and the peer
// "holds no copy of this GUID".  Every lane that follows an enemy -- rows, detail, boss,
// damage -- is addressed to a COPY, so on the machine with no actor there is nothing to
// follow, nothing to hit and nothing to see.
//
// The rows already say a key is alive and where it is.  What is missing is the one thing a
// row cannot carry: WHICH CLASS to construct.  It cannot be derived -- a summon's key is a
// hash of its summoner and a spawn point, and a placed key is a hash of a path, so neither
// names a package -- so it rides the wire, as an asset path like ENEMY_DETAIL's montage and
// for the same reason: both machines load the same cooked assets and an index would select
// a different class on each side.
//
// Body: guid(16, the enemy's key), flags(u8, see kSpawn*), px/py/pz(i32, 1/8 cm, the same
// quantization an ENEMY row uses), yaw(i16), hp(u16, 0..65535 like a row), path(str, the
// actor's class asset path).
//
// Sent RELIABLE and on change: a missing copy that never appears because a datagram was lost
// is a partner fighting an invisible boss.  It is only ever sent by the machine that RUNS the
// enemy, so a receiver can never be told to spawn a copy of something the sender does not
// hold; and only when the key is ALIVE, because a dead key's residue is the DeadLedger's job.
static const int kSpawnPathCap = 200;

// The copy exists because the owner's row named a class this machine had none for.  Cleared
// for ordinary owners: a flag byte with one meaning would be a spare field pretending to be
// a state machine.
static const uint8_t kSpawnMirror = 0x01;

struct EnemySpawnBody {
    uint32_t guid[4];
    uint8_t  flags;
    int32_t  px, py, pz;
    int16_t  yaw;
    uint16_t hp;
    char     path[kSpawnPathCap];
};

inline void WriteEnemySpawn(Writer& w, const EnemySpawnBody& s)
{
    w.Guid(s.guid);
    w.U8(s.flags);
    w.I32(s.px);
    w.I32(s.py);
    w.I32(s.pz);
    w.I16(s.yaw);
    w.U16(s.hp);
    w.Str(s.path);
}

inline bool ReadEnemySpawn(Reader& r, EnemySpawnBody* s)
{
    r.Guid(s->guid);
    s->flags = r.U8();
    s->px = r.I32();
    s->py = r.I32();
    s->pz = r.I32();
    s->yaw = r.I16();
    s->hp = r.U16();
    if (r.Str(s->path, sizeof(s->path)) >= (int)sizeof(s->path)) return false;
    // An empty path is not a class: refuse it here rather than at the spawn attempt, so a
    // malformed body costs a datagram and not an engine call.
    return r.ok && s->path[0] != 0;
}

// ---- placed world objects (WIRE.md 3.20) ----------------------------------
// The lane for interactables that are NOT SavableActors: the chest measured in
// LIVE-TEST-LOG Session 8 R67 has no GUID and no `SavableActorStates` entry, so the
// GUID-keyed ACTOR_STATES lane cannot carry it at all.
//
// Body: rev(u32), n(u16, the whole set), then n x {key(16), shape(u8), state(u8)}.
// Row size 18 bytes.  `key` is FNV-1a 128 of the placed actor's
// "<world>/<level>/<name>" path with the object seed (qdenemyid.h's PLACED rule with its
// own seed), so both machines compute it from the cooked package rather than negotiating.
// `shape` is a byte on the wire so the two logs are comparable and the receiver does not
// have to re-classify to decide a property name.
static const int kObjectsMax = 256;

struct ObjectRow {
    uint32_t key[4];
    uint8_t  shape;    // 0 none, 1 chest, 2 door, 3 pickup, 4 barricade, 5 activatable
    uint8_t  state;    // 0 closed / not activated, 1 open / activated
};

inline void WriteObjectsHeader(Writer& w, uint32_t rev, uint16_t n)
{
    w.U32(rev);
    w.U16(n);
}

// Same no-produced-count rule as the flag bank and the actor-state set: the header's `n`
// is the container size, and a datagram carries whole rows or it does not go out.
inline int WriteObjectsChunk(Writer& w, const ObjectRow* e, int n, int first)
{
    int i = first;
    while (i < n && w.len + 18 <= w.cap) {
        w.Guid(e[i].key);
        w.U8(e[i].shape);
        w.U8(e[i].state);
        ++i;
        if (!w.ok) break;
    }
    return i;
}

inline bool ReadObjectsHeader(Reader& r, uint32_t* rev, uint16_t* n)
{
    *rev = r.U32();
    *n   = r.U16();
    return r.ok;
}

inline bool ReadObjectRow(Reader& r, ObjectRow* e)
{
    r.Guid(e->key);
    e->shape = r.U8();
    e->state = r.U8();
    if (e->shape > 5) return false;     // a shape this build cannot interpret
    if (e->state > 1) return false;
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

// ---- dialogue spectator view (K_DIALOGUE_VIEW 0x50, qddialogue2.h) --------
// The cursor the talking machine's DialogueManager is showing, so the partner can display
// the same conversation on its OWN game's dialogue widget.  The lines are NOT in the body:
// they live in the cooked `FS_DialogueData` asset, identified by its OBJECT NAME, which
// both machines load from the same package.  What travels is the cursor plus the number of
// options the talker is offered and the one it picked -- never the dialogue's outcomes,
// which ride the flag lane as they always did.  Unreliable and latest-wins: a lost sample
// costs a beat, the next one repairs it.
struct DialogueViewBody {
    uint32_t seq;
    uint32_t npc[4];      // qddialogue.h StableId of the NPC actor
    uint8_t  slot;        // 0xFF = not talking
    uint8_t  flags;       // bit0 options on screen, bit1 a choice was made, bit2 hide the name
    uint16_t sequence;    // CurrentDialogueSequenceIndex
    uint16_t entry;       // entry index inside that sequence (0 when unreadable)
    uint16_t options;     // every option the talker can pick from (0 = none on screen)
    uint16_t chosen;      // 0xFFFF = none yet
    char     asset[96];   // the FS_DialogueData object's own name
};

static const uint16_t kDialogueViewNoOption = 0xFFFF;

inline void WriteDialogueView(Writer& w, const DialogueViewBody& d)
{
    w.U32(d.seq);
    w.Guid(d.npc);
    w.U8(d.slot);
    w.U8(d.flags);
    w.U16(d.sequence);
    w.U16(d.entry);
    w.U16(d.options);
    w.U16(d.chosen);
    w.Str(d.asset);
}

inline bool ReadDialogueView(Reader& r, DialogueViewBody* d)
{
    d->seq = r.U32();
    r.Guid(d->npc);
    d->slot  = r.U8();
    d->flags = r.U8();
    d->sequence = r.U16();
    d->entry    = r.U16();
    d->options  = r.U16();
    d->chosen   = r.U16();
    if (r.Str(d->asset, sizeof(d->asset)) < 0) return false;
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
// `dest` is what makes the announcement actionable.  It names the destination the
// sender just took -- the placed actor that carries the game's own `LocationData`
// (a teleporter, a checkpoint, a cutscene trigger) -- by its short object name plus
// world, e.g. "Teleporter_500_AUT_02/100_GR".  Both machines run the same cooked
// world, so the same names exist on both: the receiver resolves the actor itself and
// drives its own travel entry point.  Empty when the sender is not travelling.
//
// Body: seq(u32, bumps per change), role(u8 1 = host, 2 = peer), map(str, short name),
//       dest(str, destination identity, "" = not travelling).
struct TravelBody {
    uint32_t seq;
    uint8_t  role;
    uint8_t  phase;      // see TravelPhase
    char     map[64];
    char     dest[96];
};

// TRAVEL.phase (WIRE.md 3.15, vitals tail).  An in-world travel is one announcement; a hard
// transition is a handshake because both machines must be in the new UWorld before
// either resumes applying absolute state.
enum TravelPhase {
    TP_NONE       = 0,
    TP_HARD_BEGIN = 1,
    TP_ARRIVED    = 2,
    TP_RESUME     = 3,
    // Session 20, R147: the AREA one machine has entered by streaming a sublevel of the
    // persistent world.  This is not a UWorld switch, so TP_HARD_BEGIN is the wrong
    // instruction: a hard load of an area is the pawn-less path R102 measured.  The
    // sender names the area in `dest` (and the UWorld in `map`, as in every phase, because
    // the divergence board consumes `map` before the phase is read); the receiver streams
    // it on its own machine (the
    // game's own level-list mechanism) or refuses with a named error.  Measured failure
    // it closes: one 600 s run ended with the host showing 780_SZ_Depths and the peer
    // still in the hub area, because the area was only ever entered by a LOCAL lever on
    // each machine and nothing carried it across.
    TP_AREA       = 4,
};

inline void WriteTravel(Writer& w, const TravelBody& t)
{
    w.U32(t.seq);
    w.U8(t.role);
    w.U8(t.phase);
    w.Str(t.map);
    w.Str(t.dest);
}

inline bool ReadTravel(Reader& r, TravelBody* t)
{
    t->seq = r.U32();
    t->role = r.U8();
    t->phase = r.U8();
    if (r.Str(t->map, sizeof(t->map)) >= (int)sizeof(t->map)) return false;
    if (r.Str(t->dest, sizeof(t->dest)) >= (int)sizeof(t->dest)) return false;
    return r.ok;
}

// ---- damage to a player ---------------------------------------------------
// Ownership follows the nearer player, so an enemy next to the partner is run BY the
// partner and hurts it through the game's own attack path.  What that does not cover
// is the opposite case: an enemy the HOST runs (the partner is too far for its spawner,
// or ownership is mid-handoff) attacking across the distance.  The host sees the attack
// -- it owns the enemy and the montage is already an event here -- and the partner is
// the only machine that can apply damage to its own pawn, because damage goes through
// `TryApplyDamage` on a `FB_Damageable` that the host has no object for.
//
// Body: guid(16, the attacking enemy; all-zero for an environmental hit),
//       i32 amount_centi (1/100 of the game's damage unit), u8 flags.
// flags: bit0 melee, bit1 ranged, bit2 the sender could not resolve the damage source
//        and the amount is an estimate (logged as such, never silent).
struct DamageBody {
    uint32_t guid[4];
    int32_t  amount_centi;
    uint8_t  flags;
};

static const uint8_t kDamageMelee   = 0x01;
static const uint8_t kDamageRanged  = 0x02;
static const uint8_t kDamageGuessed = 0x04;

inline void WriteDamage(Writer& w, const DamageBody& d)
{
    w.Guid(d.guid);
    w.I32(d.amount_centi);
    w.U8(d.flags);
}

inline bool ReadDamage(Reader& r, DamageBody* d)
{
    r.Guid(d->guid);
    d->amount_centi = r.I32();
    d->flags = r.U8();
    return r.ok;
}

// ---- cutscene (WIRE.md 3.16) ----------------------------------------------
// Body: seq(u32), slot(u8 0 host / 1 peer / 0xFF free), state(u8), name(str).
// A cutscene is per-machine Blueprint state, so the sender publishes which manager it
// is running and the receiver either resolves the same manager in its own cooked
// world or is held consistently with the game's own cutscene-mode switch.
enum CutsceneState {
    CS_NONE    = 0,
    CS_PLAYING = 1,
    CS_ENDED   = 2,
};

struct CutsceneBody {
    uint32_t seq;
    uint8_t  slot;
    uint8_t  state;
    char     name[96];
};

inline void WriteCutscene(Writer& w, const CutsceneBody& c)
{
    w.U32(c.seq);
    w.U8(c.slot);
    w.U8(c.state);
    w.Str(c.name);
}

inline bool ReadCutscene(Reader& r, CutsceneBody* c)
{
    c->seq = r.U32();
    c->slot = r.U8();
    c->state = r.U8();
    if (c->state > CS_ENDED) return false;
    if (r.Str(c->name, sizeof(c->name)) >= (int)sizeof(c->name)) return false;
    return r.ok;
}

// ---- movers (WIRE.md 3.17) -------------------------------------------------
// Owner-authoritative absolute state for moving world objects (elevators, platforms,
// `Activatable` movers).  Body: rev(u32), n(u16), n x {guid(16), i32 x,y,z,
// i16 yaw, i16 pitch, u8 flags}.  Row size 16+12+2+2+1 = 33 bytes.
static const uint8_t kMoverMoving = 0x01;
static const uint8_t kMoverOwner  = 0x02;

struct MoverRow {
    uint32_t id[4];
    int32_t  px, py, pz;
    int16_t  yaw, pitch;
    uint8_t  flags;
};

struct MoversBody {
    uint32_t rev;
    uint16_t n_sent;
    uint16_t n;              // rows stored (bounded by kMoversMax)
    MoverRow row[24];
};

static const int kMoversMax = 24;

inline void WriteMoversHeader(Writer& w, uint32_t rev, uint16_t n)
{
    w.U32(rev);
    w.U16(n);
}

inline int WriteMoversChunk(Writer& w, const MoverRow* e, int n, int first)
{
    int i = first;
    int rows = 0;
    while (i < n && rows < 20 && w.len + 33 <= w.cap) {
        w.Guid(e[i].id);
        w.I32(e[i].px); w.I32(e[i].py); w.I32(e[i].pz);
        w.I16(e[i].yaw); w.I16(e[i].pitch);
        w.U8(e[i].flags);
        ++i;
        ++rows;
        if (!w.ok) break;
    }
    return i;
}

inline bool ReadMovers(Reader& r, MoversBody* b)
{
    b->rev = r.U32();
    b->n_sent = r.U16();
    b->n = 0;
    for (int i = 0; i < b->n_sent && r.ok; ++i) {
        MoverRow tmp;
        r.Guid(tmp.id);
        tmp.px = r.I32(); tmp.py = r.I32(); tmp.pz = r.I32();
        tmp.yaw = r.I16(); tmp.pitch = r.I16();
        tmp.flags = r.U8();
        if (b->n < kMoversMax) b->row[b->n++] = tmp;
    }
    return r.ok;
}

// ---- platform rides (WIRE.md 3.23) -----------------------------------------
// [platforms] Where a RIDER stands, in the platform's own axes, keyed by the platform's
// stable id -- the second half of the moving-platform problem.  `MOVERS` says where the
// platform is; this says where the person standing on it is.  The receiver rebuilds the
// place from ITS OWN copy of the platform, so the doll is on the platform to the
// centimetre even when the two copies run a few frames apart.
//
// WHY THE ABSOLUTE POSITION IS NOT ENOUGH (dollsMP `docs/sync/36` §9, measured): a rider's
// "position minus the platform's position" jumps 50-900 cm from one sample to the next,
// because the pawn and the platform are sampled at different moments of the sender's frame.
// A standing passenger's place IN THE PLATFORM'S AXES, by contrast, does not change at all
// while the platform moves -- which is what this row carries.
//
// Body: ms(u32 -- the sender's clock, for ordering only), n(u16, never split across a
// datagram), n x {guid id, i32 ox, i32 oy, i32 oz, i16 oyaw, u8 flags}.  Row 31 bytes;
// 4 rows bound the body at 130 bytes.
static const uint8_t kRideOn     = 0x01;   // the sender's player is on this platform
static const uint8_t kRideMoving = 0x02;   // and the platform is moving

struct PlatformRideRow {
    uint32_t id[4];
    int32_t  ox, oy, oz;    // the rider's place in the platform's axes, 1/8 cm
    int16_t  oyaw;          // the rider's heading RELATIVE to the platform, 1/65536 turn
    uint8_t  flags;
};

struct PlatformRideBody {
    uint32_t ms;
    uint16_t n_sent;
    uint16_t n;             // rows stored (bounded by kPlatformRidesMax)
    PlatformRideRow row[4];
};

static const int kPlatformRidesMax = 4;

inline void WritePlatformRide(Writer& w, const PlatformRideBody& b)
{
    w.U32(b.ms);
    w.U16((uint16_t)b.n);
    for (int i = 0; i < b.n; ++i) {
        w.Guid(b.row[i].id);
        w.I32(b.row[i].ox); w.I32(b.row[i].oy); w.I32(b.row[i].oz);
        w.I16(b.row[i].oyaw);
        w.U8(b.row[i].flags);
    }
}

inline bool ReadPlatformRide(Reader& r, PlatformRideBody* b)
{
    b->ms = r.U32();
    b->n_sent = r.U16();
    b->n = 0;
    // Same prefix rule as the other absolute containers: `n` is the sender's count and a
    // datagram carries a prefix, so stop when the body is exhausted rather than refusing.
    for (int i = 0; i < (int)b->n_sent && r.ok && r.Room(1); ++i) {
        if (b->n >= kPlatformRidesMax) return false;
        PlatformRideRow& t = b->row[b->n];
        r.Guid(t.id);
        t.ox = r.I32(); t.oy = r.I32(); t.oz = r.I32();
        t.oyaw = r.I16();
        t.flags = r.U8();
        if (!r.ok) return false;
        ++b->n;
    }
    return r.ok;
}

// ---- loot (WIRE.md 3.18) ---------------------------------------------------
// A shared drop set plus tombstones.  Body: rev(u32), n(u16), n x {u32 source_hash,
// i32 drop_index, u8 taken, u8 flags, i16 yaw, i32 px, i32 py, i32 pz, str item_path}.
// The host is authoritative; a peer's own rolls are merged as proposals.  `taken` wins:
// a drop one player picked up must disappear for the other.
//
// THE SPAWN HALF (Session 22, R129).  The game rolls its own drop on EACH machine, and
// the two rolls are not the same item and not at the same centimetre, so a key derived
// from the roll's class and position could never agree (measured: two machines, four
// drops, not one key in common).  So a drop's identity is the DEATH that caused it and
// the machine that ran that death is its ORIGINATOR: it owns the drop and describes it
// here.  The partner materialises the same class at the same point under the same key
// instead of trusting its own roll.  `flags` says whether this entry carries a spawn
// point; `yaw` is `QuantizeAngle`, the three positions are `QuantizePos`.
static const int kLootMax = 40;

enum LootFlags {
    LF_SPAWN = 1        // the entry carries a point: a drop to materialise if absent
};

struct LootEntry {
    uint32_t source_hash;
    int32_t  drop_index;
    uint8_t  taken;
    uint8_t  flags;
    int16_t  yaw;
    int32_t  px, py, pz;
    char     item_path[200];
};

struct LootBody {
    uint32_t  rev;
    uint16_t  n_sent;
    uint16_t  n;
    LootEntry entry[kLootMax];
};

inline void WriteLootHeader(Writer& w, uint32_t rev, uint16_t n)
{
    w.U32(rev);
    w.U16(n);
}

inline int WriteLootChunk(Writer& w, const LootEntry* e, int n, int first)
{
    int i = first;
    while (i < n) {
        int pl = 0;
        while (e[i].item_path[pl]) ++pl;
        int need = 4 + 4 + 1 + 1 + 2 + 12 + 2 + pl;
        if (w.len + need > w.cap) break;
        w.U32(e[i].source_hash);
        w.I32(e[i].drop_index);
        w.U8(e[i].taken);
        w.U8(e[i].flags);
        w.I16(e[i].yaw);
        w.I32(e[i].px);
        w.I32(e[i].py);
        w.I32(e[i].pz);
        w.Str(e[i].item_path);
        ++i;
        if (!w.ok) break;
    }
    return i;
}

inline bool ReadLoot(Reader& r, LootBody* b)
{
    b->rev = r.U32();
    b->n_sent = r.U16();
    b->n = 0;
    // `n_sent` is the size of the sender's LEDGER, not the number of entries in this
    // datagram: a ledger of 68 drops is several datagrams (a path is 40-90 bytes), and the
    // sender writes a prefix with a cursor, exactly as the flag and actor-state
    // containers do (WIRE.md 3.6).  Reading `n_sent` entries is what made every big loot
    // body undecodable -- the relay refused it as malformed AND the native receiver here
    // refused it, so the lane's merge and tombstone never crossed at all.
    while (r.ok && r.pos < r.len && b->n_sent > 0) {
        LootEntry tmp;
        tmp.source_hash = r.U32();
        tmp.drop_index = r.I32();
        tmp.taken = r.U8();
        tmp.flags = r.U8();
        tmp.yaw = r.I16();
        tmp.px = r.I32();
        tmp.py = r.I32();
        tmp.pz = r.I32();
        if (r.Str(tmp.item_path, sizeof(tmp.item_path)) >= (int)sizeof(tmp.item_path)) return false;
        if (b->n < kLootMax) b->entry[b->n++] = tmp;
        if (!r.ok) return false;
        // A body cut inside an entry is a framing error, not a short read.
    }
    return r.ok;
}

// ---- suspend saves (WIRE.md 3.19) -----------------------------------------
// Body: seq(u32), state(u8), slot(str).  The peer holds the game's own
// `BlockSuspendSaves` while a session is live, and a suspend load it does take is
// announced so the host can re-apply its absolute state.
enum SuspendState {
    SS_CLEAR     = 0,
    SS_HOLD      = 1,
    SS_LOADED    = 2,
    SS_REAPPLIED = 3,
};

struct SuspendBody {
    uint32_t seq;
    uint8_t  state;
    char     slot[64];
};

inline void WriteSuspend(Writer& w, const SuspendBody& s)
{
    w.U32(s.seq);
    w.U8(s.state);
    w.Str(s.slot);
}

inline bool ReadSuspend(Reader& r, SuspendBody* s)
{
    s->seq = r.U32();
    s->state = r.U8();
    if (s->state > SS_REAPPLIED) return false;
    if (r.Str(s->slot, sizeof(s->slot)) >= (int)sizeof(s->slot)) return false;
    return r.ok;
}


// ---- kill XP (WIRE.md 3.26) -------------------------------------------------
// One death paid one machine's player `xp`; the other machine pays its own player the same
// amount IF its own game did not.  `xp` is what the sender MEASURED -- never a value derived
// from the enemy's data, because this build exposes no enemy XP field and the award is
// Blueprint state (qdprogression.h states the whole reason).  A machine that measured
// nothing sends nothing, so a kill the game did not pay for is never invented.
//
// Body: guid(16, the enemy's key -- the identity every other enemy lane uses),
//       seq(u16, the death's ordinal for that key, so a retransmit is not a second award),
//       xp(u32, the measured amount).
//
// Reliable: an award lost to a dropped datagram is a player who fought for nothing.
struct KillXpBody {
    uint32_t guid[4];
    uint16_t seq;
    uint32_t xp;
};

inline void WriteKillXp(Writer& w, const uint32_t guid[4], uint16_t seq, int32_t xp)
{
    w.Guid(guid);
    w.U16(seq);
    w.U32((uint32_t)xp);
}

inline bool ReadKillXp(Reader& r, KillXpBody* b)
{
    r.Guid(b->guid);
    b->seq = r.U16();
    b->xp  = r.U32();
    if (!r.ok) return false;
    // An award of nothing is not an award, and an implausible one is a framing error.
    if (b->xp == 0 || b->xp > 1000000u) return false;
    return true;
}

// ---- save (WIRE.md 3.27) ----------------------------------------------------
// "I saved; write your own slot now."  The detector is the game's own save DISPATCH
// (`SaveFunctions_C.SaveGameToDisk` and the instance's `GI_SaveGameToDisk`), so a rest
// point, the game's autosave, a menu save and a scripted save all arrive on this one body.
// A suspend save is deliberately NOT mirrored: it is another machine's snapshot, and
// qdsuspend.h already owns it.
//
// Body: seq(u32, the sender's own save counter -- monotone, so a retransmit is one
//       request), role(u8, 1 host / 2 peer, diagnostics only),
//       route(u8, 1 main slot / 2 the game's own suspend writer -- WIRE.md 3.27).
//
// A mirrored save is never announced again: the receiver's own dispatch is the echo of the
// save it ran for the partner, and qdprogression.h's SaveBoard brackets exactly that.
//
// WHY `route` IS ON THE WIRE.  MEASURED (round 3, progression.md): on this build a
// checkpoint REST -- the save a player actually makes -- writes `World0_Suspend_N.sav` and
// does NOT touch `World0.sav`, and `SaveHere` dispatches nothing at all.  A partner that
// always answered with a main-slot write would have saved a DIFFERENT file than the one its
// own game resumes from, so the request names the route and the receiver runs its OWN
// matching writer on its OWN slot.  No save file ever crosses; only the instruction.
struct SaveBody {
    uint32_t seq;
    uint8_t  role;
    uint8_t  route;
};

inline void WriteSave(Writer& w, uint32_t seq, uint8_t role, uint8_t route)
{
    w.U32(seq);
    w.U8(role);
    w.U8(route);
}

inline bool ReadSave(Reader& r, SaveBody* b)
{
    b->seq   = r.U32();
    b->role  = r.U8();
    if (!r.ok) return false;
    if (b->role > 2) return false;
    // A body that predates the route byte (5 bytes, seq+role only) means the main slot:
    // refusing it would lose a partner's save for a field that is a diagnostic plus a
    // choice, not a fact about the request.
    if (r.pos >= r.len) { b->route = 1; return true; }
    b->route = r.U8();
    if (!r.ok) return false;
    if (b->route == 0) b->route = 1;
    if (b->route > 2) return false;
    return true;
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

// kind 9: the placed-object key, the shape byte the ROW carries, and the state asked
// for.  `shape` is carried so the host logs the same classification the peer acted on.
inline void WriteIntentObjectState(Writer& w, uint32_t basedOnRev, const uint32_t key[4],
                                   uint8_t shape, uint8_t state)
{
    WriteIntentHeader(w, I_OBJECT_STATE, basedOnRev);
    w.Guid(key);
    w.U8(shape);
    w.U8(state);
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
    case I_OBJECT_STATE:
        // guid is the placed-object key here, `value` the state and `mode` the shape --
        // the same three fields the row form uses, so there is no second shape to keep in
        // step and the cross-check in doc/vectors.txt names one encoding.
        r.Guid(b->guid);
        b->mode = r.U8();
        b->value = (int32_t)r.U8();
        if (b->mode > 5 || b->value > 1) return false;
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
