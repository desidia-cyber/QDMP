// qdtrigger.h -- the trigger lane: a partner walking into an event volume fires it here too.
//
// THE GAP THIS CLOSES (story/content round, after Session 25)
// ----------------------------------------------------------
// Every boss fight with a start trigger, and a good share of the world's one-time events,
// begin the same way: the player walks into a placed `ActivationTrigger_C` box, and the box
// calls `Activate` on its `ActivationTargets`.  Decoded from the shipped maps:
//
//   100_GR             ShrinebossActivationTrigger -> ShrinebossSpawner
//   450_MAUS_darkmaze  ActivationTrigger4          -> DemonSpider_Spawner
//   740_SZ_Prison      ActivationTrigger4          -> JosamiBloodLord_Sword_Spawner
//   760_SZ_AUT         ActivationTrigger11         -> ROSDragon_Spawner
//   780_SZ_Depths      ActivationTrigger9          -> GreenDragon_Spawner
//   900_SipehaWorld    SipehaFightStartTrigger     -> Sipeha_Spawner
//
// and the boss spawners say `SpawnOnActivate=1` in their class defaults.  The trigger
// itself answers only its OWN machine's player (`PlayerOnlyTrigger`, `GetPlayerCharacter`
// in its name table), so before this lane a boss fight started on ONE machine: the arena
// door, the music, the fog and the spawner's own activation never happened on the other.
// The partner then saw at most a constructed mirror of the boss (Session 16) in an arena
// that, on its screen, had not started -- and a boss whose spawner is never activated never
// spawns at all for a partner who walked in alone (Session 21, R130: "four minutes inside
// its spawner's radius produced no Dragon_Ros_BP_C").
//
// THE RULE
// --------
// What crosses the wire is the EVENT the game itself dispatched: "the local player's body
// began (or ended) overlapping trigger <placed key>", observed on `ProcessEvent` where the
// component's overlap delegate calls the trigger's bound event.  The receiver dispatches
// the SAME bound event on its own copy of the trigger with ITS OWN local player as the
// overlapping actor -- so the trigger's own gates (`OnlyOnce`, `HasAlreadybeenTriggered`,
// `RequireActivationBeforeUse`, `WaitForPlayerGroundedBeforeTrigger`) decide, exactly as if
// that player had walked in.  Nothing here calls `Activate` on a target directly, and
// nothing decides on the trigger's behalf.
//
// A BOSS TRIGGER PULLS THE PARTNER IN
// -----------------------------------
// A boss arena closes behind the player (`TrapActivateTrigger`, `DoorToManage`).  Fired on
// the partner's machine with the partner standing outside, it would lock the partner OUT
// of the fight on its own screen.  So a trigger whose targets include a boss spawner first
// moves the receiving player beside the sender's reported position (the point where the
// sender's body was when it entered), and only then dispatches the event.  "Far" is
// `kPullInCm`: a partner already in the arena is not moved.
//
// ECHO
// ----
// The replay is a local-player overlap on the receiver, and a pulled-in player may ALSO
// physically overlap the box a moment later.  Either would be observed as "the local player
// entered" and sent straight back.  `EchoGuard` remembers each key this machine replayed
// (or sent) for `kEchoMs`, and a local observation of that key inside the window is not
// sent.  The same window drops a duplicate arrival.
//
// Header-only and dependency-free apart from the wire codec, compiled into native/test.
#pragma once
#include <stdint.h>
#include <string.h>

#include "qdwire.h"

namespace qd {
namespace trigger {

// The event, as the receiver must replay it.
static const uint8_t kEvBegin = 1;   // a ComponentBeginOverlap bound event
static const uint8_t kEvEnd   = 2;   // a ComponentEndOverlap bound event

// Which volume dispatched it.  An `ActivationTrigger_C` has two (`BoxMode` picks one per
// instance).  An `EnemySpawner_C` has its own TRAP pair -- `TrapActivateTrigger` (a begin
// event) and `TrapDeactivateTrigger` (an end event) -- which is how a proximity boss wakes
// up (`DisableDamageTillTrapTriggered=1` on WhaleKing, Necronomicon, JosamiLord, the Blood
// Lords and the Queen) and how its arena door closes.
static const uint8_t kVolBox      = 0;
static const uint8_t kVolSphere   = 1;
static const uint8_t kVolTrapOn   = 2;   // EnemySpawner_C.TrapActivateTrigger, begin only
static const uint8_t kVolTrapOff  = 3;   // EnemySpawner_C.TrapDeactivateTrigger, end only

// Flag bits.
static const uint8_t kFlagBoss = 0x01;   // a target is a spawner whose EnemyData says Boss

// A partner nearer than this to the sender's point is already in the arena.
static const float kPullInCm = 1500.0f;

// A key replayed or sent within this window is not re-sent, and a duplicate arrival inside
// it is dropped.  Long enough to cover the physical overlap a pull-in causes and a
// reliable retransmit; short enough that a player walking out and back in fires again.
static const uint32_t kEchoMs = 4000;

// 'QDTR'.  The trigger's identity is its placed path, hashed with a seed of its own so the
// same path can never collide with an object, dialogue or enemy key.
static const uint32_t kTriggerSeed = 0x51445452u;

static const int kNameCap = 64;

struct Body {
    uint32_t key[4];
    uint8_t  event;            // kEvBegin / kEvEnd
    uint8_t  volume;           // kVolBox / kVolSphere
    uint8_t  flags;            // kFlag*
    float    px, py, pz;       // the sender's body when it entered (cm)
    char     name[kNameCap];   // the trigger's instance name, for the two logs
};

inline bool ValidEvent(uint8_t e) { return e == kEvBegin || e == kEvEnd; }
inline bool ValidVolume(uint8_t v) { return v <= kVolTrapOff; }

// A trap volume carries exactly one event; a body that pairs it with the other one names an
// event the spawner does not have.
inline bool ValidPair(uint8_t ev, uint8_t vol)
{
    if (!ValidEvent(ev) || !ValidVolume(vol)) return false;
    if (vol == kVolTrapOn) return ev == kEvBegin;
    if (vol == kVolTrapOff) return ev == kEvEnd;
    return true;
}

// The six bound events the lane knows, as one index: [0..3] the trigger's box/sphere
// begin/end, [4] the spawner's trap begin, [5] the spawner's trap end.  -1 = no such event.
static const int kSlots = 6;
inline int SlotOf(uint8_t ev, uint8_t vol)
{
    if (!ValidPair(ev, vol)) return -1;
    if (vol == kVolTrapOn) return 4;
    if (vol == kVolTrapOff) return 5;
    return (vol == kVolBox ? 0 : 2) + (ev == kEvBegin ? 0 : 1);
}
inline uint8_t EventOfSlot(int slot)
{
    return (slot == 0 || slot == 2 || slot == 4) ? kEvBegin : kEvEnd;
}
inline uint8_t VolumeOfSlot(int slot)
{
    return slot < 2 ? kVolBox : slot < 4 ? kVolSphere : slot == 4 ? kVolTrapOn : kVolTrapOff;
}

inline bool Finite(float f) { return f == f && f < 1e9f && f > -1e9f; }

inline bool Encode(wire::Writer& w, const Body& b)
{
    if (!ValidPair(b.event, b.volume)) return false;
    if (!Finite(b.px) || !Finite(b.py) || !Finite(b.pz)) return false;
    w.Guid(b.key);
    w.U8(b.event);
    w.U8(b.volume);
    w.U8(b.flags);
    uint32_t u;
    memcpy(&u, &b.px, 4); w.U32(u);
    memcpy(&u, &b.py, 4); w.U32(u);
    memcpy(&u, &b.pz, 4); w.U32(u);
    w.Str(b.name);
    return w.ok;
}

inline bool Decode(wire::Reader& r, Body* b)
{
    memset(b, 0, sizeof(*b));
    r.Guid(b->key);
    b->event = r.U8();
    b->volume = r.U8();
    b->flags = r.U8();
    uint32_t u;
    u = r.U32(); memcpy(&b->px, &u, 4);
    u = r.U32(); memcpy(&b->py, &u, 4);
    u = r.U32(); memcpy(&b->pz, &u, 4);
    r.Str(b->name, kNameCap);
    if (!r.ok) return false;
    if (!ValidPair(b->event, b->volume)) return false;
    if (!Finite(b->px) || !Finite(b->py) || !Finite(b->pz)) return false;
    if ((b->key[0] | b->key[1] | b->key[2] | b->key[3]) == 0) return false;
    return true;
}

// Should the receiving player be moved to the sender's point before the replay?
// Only for a boss trigger's (or a boss spawner trap's) BEGIN, and only when the receiver is
// far from it -- a partner
// already inside the arena keeps its own position.
inline bool WantPullIn(const Body& b, float myX, float myY, float myZ)
{
    if (!(b.flags & kFlagBoss) || b.event != kEvBegin) return false;
    float dx = b.px - myX, dy = b.py - myY, dz = b.pz - myZ;
    return dx * dx + dy * dy + dz * dz > kPullInCm * kPullInCm;
}

// ---------------------------------------------------------------------------
// EchoGuard: one window per (key, event)
// ---------------------------------------------------------------------------
// Fixed and tiny: a burst of triggers is a handful, and the oldest slot is reused.
class EchoGuard {
public:
    static const int kSlots = 32;

    void Reset() { memset(this, 0, sizeof(*this)); }

    // Remember that this machine sent or replayed (key, event) at `now`.
    void Note(const uint32_t key[4], uint8_t ev, uint32_t now)
    {
        int slot = -1;
        uint32_t oldestAge = 0;
        for (int i = 0; i < kSlots; ++i) {
            if (!s_[i].used) { slot = i; break; }
            if (Same(s_[i], key, ev)) { slot = i; break; }
            uint32_t age = now - s_[i].at;
            if (slot < 0 || age > oldestAge) { slot = i; oldestAge = age; }
        }
        Slot& s = s_[slot];
        memcpy(s.key, key, 16);
        s.ev = ev;
        s.at = now;
        s.used = true;
    }

    // True while (key, event) is inside its window.  Signed age: `now` can be taken earlier
    // in the same tick than the note (the Session 25 fall-guard bug), and a negative age is
    // "just now", not "four billion ms ago".
    bool Recent(const uint32_t key[4], uint8_t ev, uint32_t now) const
    {
        for (int i = 0; i < kSlots; ++i) {
            if (!s_[i].used || !Same(s_[i], key, ev)) continue;
            int32_t age = (int32_t)(now - s_[i].at);
            if (age < (int32_t)kEchoMs) return true;
        }
        return false;
    }

private:
    struct Slot {
        uint32_t key[4];
        uint32_t at;
        uint8_t  ev;
        bool     used;
    };
    static bool Same(const Slot& s, const uint32_t key[4], uint8_t ev)
    {
        return s.ev == ev && s.key[0] == key[0] && s.key[1] == key[1] &&
               s.key[2] == key[2] && s.key[3] == key[3];
    }
    Slot s_[kSlots];
};

} // namespace trigger
} // namespace qd
