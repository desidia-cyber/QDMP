// qdobjectstate.h -- the world-object state lane: interactables that are NOT savables.
//
// THE GAP THIS CLOSES (LIVE-TEST-LOG Session 8, R67; SYNC-GAPS section 2)
// ----------------------------------------------------------------------
// `ACTOR_STATES` (0x12) is keyed by an FGuid into the save's `SavableActorStates`
// container.  That is the right key for a door or a barricade, because the game itself
// persists them that way.  It is the WRONG key for the objects that are only *placed*:
// measured live, `SG_Chest_BP2 [SG_Chest_BP_C]` has `savable=no guid=- index=-1`, no
// `GUIDs` array and no container entry, and opening it changes exactly two things -- its
// own `Opened` bool and the global `TotalChestsOpened` flag.  So the chest opens on one
// machine, the flag crosses on `WORLD_FLAGS`, and the partner's chest stays closed and
// lootable.  No amount of work on the GUID lane can carry it: there is no GUID.
//
// WHAT THE COOKED BLUEPRINTS SAY (survey, analysis/pak/code/blueprints.py)
// ----------------------------------------------------------------------
// Decoded from the shipped pak (the packages named below are the ones the level
// instances of these classes descend from):
//
//   Chest_C              (Props/Chest_00/Chest.uasset; SG_Chest_BP_C is a mesh/sound
//                         subclass of it and carries NO state or functions of its own,
//                         which is why the live chest is a `Chest_C`)
//     state    Opened  (also AlwaysOpened, PreviewOpen, Animating, LidOpenAngle)
//     visual   SetToFinalState, OpenLid timeline
//     pays out Interact, Open -> TotalChestsOpened / ActorsToActivateOnOpen /
//              SaveNamedData_Advanced, and InsertKey / TryUseKey for a locked one
//
//   Door_Base_C          (Blueprints/Door_Base.uasset)
//     state    Opened / DoorClosed / CurrentActivationCount
//     visual   SetToOpenState   (OpenDoor / CloseDoor are the activation verbs)
//     pays out Interact -> InsertKey (key consumption) -> CheckRequiredItems
//
//   BreakableBarricade_C (Blueprints/BreakableBarricade/BreakableBarricade.uasset)
//     state    Has a `SavableActorState` and a `Save Data Index to World Save`, i.e. it
//              is the OLD lane's business whenever its instance has a GUID.  It also has
//              no bool state property at all, which is why this lane cannot carry it.
//     visual   Restore State From Save Data / SetActorEnableCollision
//
//   ItemPickup_Base_C    (Blueprints/ItemPickup_Base.uasset)
//     state    PickedUp / Acquired / GrantedItems / Activated
//     visual   SetActorHiddenInGame + SetActorEnableCollision (the pickup's own
//              disappear), DeleteDropData
//     pays out Interact -> Grant -> GrantItemsAndActivate -> AddItemToInventory
//
//   Activatable_C        (LD45/Blueprints/Objects/Shared/Activatable.uasset)
//     an INTERFACE (`Activate` / `Deactivate` + `ActivateByLoad` / `DeactivateByLoad`),
//     so it has no properties of its own.  A lever, switch or gate implements it and
//     holds its own bool; the interface is the seam, so the lane probes the concrete
//     class it finds.
//
// Reproduce the table with `python analysis/pak/code/probes/object_state_classes.py`.
//
// The rule the ladder in proxy.cpp enforces follows from that table: the lane never
// calls a function whose name is the game's interaction verb or its payout
// (`Interact`, `Grant`, `GrantItemsAndActivate`, `InsertKey`, `CheckRequiredItems`,
// `OpenDoor`, `CloseDoor`, `Open`).  `Open` is on that list because the chest package's
// own name table carries it beside `TotalChestsOpened` and `ActorsToActivateOnOpen`:
// it is the event that opens AND counts, so a `0 -> 1` through it would pay out twice on
// the machine that did not earn it.  The ladder calls the visual/restore path and then
// writes the state bool through the property the survey names.  The global counters
// (`TotalChestsOpened`) are NOT touched here -- they already ride `WORLD_FLAGS`, and
// incrementing them a second time on the receiver is exactly the double-count this
// comment exists to prevent.
//
// IDENTITY
// --------
// A placed actor's identity is its place in the cooked world: object name plus outer
// chain ("<world>/<level>/<name>").  Both machines run the same cooked package, so the
// string is identical on both -- and a runtime spawn's name is not (it carries that
// machine's object counter), which is what the `ActorIsPlaced` test in proxy.cpp filters
// on.  This is `qdenemyid.h`'s PLACED rule, reused rather than reinvented: the same
// 128-bit hash with a different seed, so an object key and an enemy key of the same path
// can never collide.
//
// WHY THE SHAPE IS ON THE WIRE
// ----------------------------
// The receiver has its own copy of the class, so it could classify locally; carrying the
// sender's `shape` makes the two logs comparable and makes an apply decision readable
// without a second classification that could disagree ("the sender says chest, I say
// nothing").  It also lets the receiver refuse a row whose shape it cannot interpret
// instead of guessing a property name.
//
// RELIABILITY
// -----------
// `K_OBJECTS` is a RELIABLE kind (WIRE.md section 2), unlike the 1 Hz actor-state
// heartbeat: an interaction is a one-shot event and a lost datagram is a chest that stays
// closed on the wrong machine with no way to notice.  The absolute body is re-sent on
// change, on join, after a suspend load, and as a bounded repair pass.
//
// WHAT IS PURE HERE
// -----------------
// No socket, no UObject, no allocation: the caller supplies the clock, the class C
// strings and the send.  Shape classification, identity, the chunk cursor, the revision,
// the echo and the retry are all exercisable with no game (test/test_objectstate.cpp).
#pragma once
#include <stdint.h>
#include <string.h>

#include "qdenemyid.h"   // Hash128: the placed-actor identity rule, reused
#include "qdwire.h"

namespace qd {
namespace objects {

// ---------------------------------------------------------------------------
// What an object is, as far as state sync is concerned
// ---------------------------------------------------------------------------
// The order is the wire order (a byte on the row), so it must not be reordered without a
// version bump.
enum Shape : uint8_t {
    SH_NONE       = 0,   // not a state-bearing interactable: never published
    SH_CHEST      = 1,
    SH_DOOR       = 2,
    SH_PICKUP     = 3,
    SH_BARRICADE  = 4,
    SH_ACTIVATABLE = 5,  // a lever / switch / gate: an `Activatable` implementation
};

// One row on the wire: 16-byte key, 1-byte shape, 1-byte state.  18 bytes.
// `wire::ObjectRow` IS the row (WIRE.md §3.20), exactly as `wire::ActorState` is the
// actor-state row, so there is no second shape to keep in step.
typedef wire::ObjectRow Row;

static const int kRowBytes = 18;

// 256 objects.  An area's interactables are a few dozen (the chest probe found eight
// within 40 m); a whole loaded level is comfortably inside this.  The table is fixed
// because the DLL is /MT and must not allocate on the game thread; overflow is REPORTED,
// never truncated in silence.
static const int kMaxEntries = 256;

// The reliable lane carries changes immediately.  This is the REPAIR pass: if nothing has
// gone out for this long, the whole set is sent again, so a machine that missed a body
// (a join mid-session, a suspend load, an ack the relay dropped) converges without a
// special case per cause.
static const uint32_t kRepairMs = 5000;

// Local observation.  An interaction is an event, and waiting for a repair pass would
// deliver the partner's state seconds late; the walk is a scan of the cached actor table.
static const uint32_t kObserveMs = 250;

// ---------------------------------------------------------------------------
// Identity -- qdenemyid's PLACED rule with its own seed
// ---------------------------------------------------------------------------
// 'QDOB'.  Distinct from the dialogue seed and from kPlacedSeed/kSummonSeed, so the same
// path hashed by three lanes cannot produce one key that means two things.
static const uint32_t kObjectSeed = 0x51444F42u;

inline bool KeyEq(const uint32_t a[4], const uint32_t b[4])
{
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

inline bool KeyZero(const uint32_t k[4])
{
    return (k[0] | k[1] | k[2] | k[3]) == 0;
}

inline void KeyCopy(uint32_t dst[4], const uint32_t src[4])
{
    dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = src[3];
}

// False for an empty path (a CDO, an asset, an actor whose outer chain does not reach the
// world): such an actor has no identity and must never be published.
inline bool KeyFromPath(const char* worldLevelPath, uint32_t out[4])
{
    if (!worldLevelPath || !worldLevelPath[0]) return false;
    enemyid::Hash128(worldLevelPath, kObjectSeed, out);
    return !KeyZero(out);
}

// ---------------------------------------------------------------------------
// The class survey, as code
// ---------------------------------------------------------------------------
// Substring tests, case-insensitive, in the order that matters: a `SecretZoneDoor_BP`
// must classify as a door before anything else claims it, and a `MimicChest` as a chest.
// `Manager` / `Subsystem` / `Data` classes are rejected because a `SkullDoorQuestState
// Manager` is bookkeeping, not a door -- the same exclusion the rig's own name matcher
// already applies (proxy.cpp `RigNameMatches`).
inline bool HasCI(const char* hay, const char* needle)
{
    if (!hay || !needle || !needle[0]) return false;
    for (int i = 0; hay[i]; ++i) {
        int k = 0;
        for (; needle[k]; ++k) {
            char a = hay[i + k];
            char b = needle[k];
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b) break;
        }
        if (needle[k] == 0) return true;
    }
    return false;
}

inline bool IsBookkeepingClass(const char* cls)
{
    return HasCI(cls, "Manager") || HasCI(cls, "Subsystem") ||
           HasCI(cls, "Component") || HasCI(cls, "Data");
}

// The classification the survey table in the header comment describes.
inline Shape ShapeOfClass(const char* cls)
{
    if (!cls || !cls[0]) return SH_NONE;
    if (IsBookkeepingClass(cls)) return SH_NONE;
    if (HasCI(cls, "Chest")) return SH_CHEST;
    if (HasCI(cls, "Door") || HasCI(cls, "Gate")) return SH_DOOR;
    if (HasCI(cls, "Pickup") || HasCI(cls, "Drop")) return SH_PICKUP;
    if (HasCI(cls, "Barricade") || HasCI(cls, "Breakable")) return SH_BARRICADE;
    // LIFT AND ELEVATOR CONTROLS ARE NOT OBJECT STATE (Session 26).  Their `Activated` is
    // the input to a moving platform the mover lane already carries; writing it under the
    // lift's Blueprint (OW_Lever_00_lift_BP8, LabElevatorSwitch, OW_Lift_Lever_00_BP) was
    // followed both times by an access violation inside that Blueprint on the next frame --
    // the peer in run 15 and the host in run 16, both entering 500_AUT.
    if (HasCI(cls, "Lift") || HasCI(cls, "Elevator")) return SH_NONE;
    if (HasCI(cls, "Lever") || HasCI(cls, "Switch") || HasCI(cls, "Activatable"))
        return SH_ACTIVATABLE;
    return SH_NONE;
}

inline const char* ShapeName(Shape s)
{
    switch (s) {
    case SH_CHEST:       return "chest";
    case SH_DOOR:        return "door";
    case SH_PICKUP:      return "pickup";
    case SH_BARRICADE:   return "barricade";
    case SH_ACTIVATABLE: return "activatable";
    default:             return "none";
    }
}

inline bool ShapeValid(uint8_t s) { return s <= (uint8_t)SH_ACTIVATABLE; }

// The property that IS the object's state, per the survey.  A shape with no probe is a
// shape this lane cannot carry: `nullptr` and the caller reports it once, rather than
// guessing a name and writing a bool that means something else.
inline const char* StatePropFor(Shape s)
{
    switch (s) {
    case SH_CHEST:   return "Opened";
    case SH_DOOR:    return "Opened";
    case SH_PICKUP:  return "PickedUp";
    case SH_BARRICADE: return nullptr;   // the old lane's: a savable with a container value
    case SH_ACTIVATABLE: return "Activated";
    default:         return nullptr;
    }
}

// The SECOND probe, tried only when the first is absent.  `ItemPickup_Base_C` carries
// both `PickedUp` and `Acquired`, and a subclass may implement one and not the other.
inline const char* StatePropAltFor(Shape s)
{
    switch (s) {
    case SH_PICKUP: return "Acquired";
    case SH_DOOR:   return "DoorClosed";     // inverted; the caller uses the sign
    case SH_ACTIVATABLE: return "bActivated";
    default:        return nullptr;
    }
}

// The visual/restore function, in the order the ladder tries.  Never the interaction verb
// and never the payout: see the header comment.  `nullptr` means "property only".
inline const char* VisualFnFor(Shape s)
{
    switch (s) {
    case SH_CHEST:       return "SetToFinalState";
    case SH_DOOR:        return "SetToOpenState";
    case SH_ACTIVATABLE: return "Activate";
    default:             return nullptr;     // a pickup hides itself; a barricade restores
    }
}

// The first member of the LADDER that exists is used; all of them are visual/restore
// paths.  `Open` and `Interact` are deliberately absent from every list here.
inline const char* SecondVisualFnFor(Shape s)
{
    switch (s) {
    case SH_CHEST:    return "RestoreFromSave";
    case SH_DOOR:     return "RestoreFromSave";
    case SH_BARRICADE: return "RestoreFromSave";
    default:          return nullptr;
    }
}

// Names the lane must never call, kept in one place so the refusal is checkable in a
// test rather than being a property of the call sites.
inline bool ForbiddenFn(const char* fn)
{
    if (!fn || !fn[0]) return false;
    static const char* const kNo[] = {
        "Interact", "Grant", "GrantItemsAndActivate", "InsertKey",
        "CheckRequiredItems", "OpenDoor", "CloseDoor", "Open", "TryUseKey",
        "AddItemToInventory", "TryApplyDamage", "GrantNewbestiary",
    };
    for (unsigned i = 0; i < sizeof(kNo) / sizeof(kNo[0]); ++i) {
        if (strcmp(fn, kNo[i]) == 0) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// The absolute set: one row per key
// ---------------------------------------------------------------------------
// Insertion order, deliberately: the entity table's order is a scan artefact, so a body
// that depended on it would make a two-machine diff noisy and a golden vector
// meaningless.  `SameAs` is order-independent so change detection does not care.
class Snapshot {
public:
    Snapshot() { Clear(); }

    void Clear()
    {
        n_ = 0;
        overflow_ = false;
    }

    int  Count() const { return n_; }
    bool Full() const { return n_ >= kMaxEntries; }
    bool Overflowed() const { return overflow_; }

    // False when the table is full.  The caller must report it: the alternative is an
    // object that exists on both machines and is carried by neither.
    bool Set(const uint32_t key[4], uint8_t shape, uint8_t state)
    {
        int i = Find(key);
        if (i >= 0) {
            e_[i].shape = shape;
            e_[i].state = state;
            return true;
        }
        if (n_ >= kMaxEntries) {
            overflow_ = true;
            return false;
        }
        KeyCopy(e_[n_].key, key);
        e_[n_].shape = shape;
        e_[n_].state = state;
        ++n_;
        return true;
    }

    int Find(const uint32_t key[4]) const
    {
        for (int i = 0; i < n_; ++i) {
            if (KeyEq(e_[i].key, key)) return i;
        }
        return -1;
    }

    const Row* At(int i) const
    {
        if (i < 0 || i >= n_) return nullptr;
        return &e_[i];
    }

    const Row* Data() const { return e_; }

    bool SameAs(const Snapshot& o) const
    {
        if (n_ != o.n_) return false;
        for (int i = 0; i < n_; ++i) {
            int j = o.Find(e_[i].key);
            if (j < 0) return false;
            if (o.e_[j].state != e_[i].state || o.e_[j].shape != e_[i].shape) return false;
        }
        return true;
    }

private:
    Row  e_[kMaxEntries];
    int  n_;
    bool overflow_;
};

// ---------------------------------------------------------------------------
// Host side: the absolute body, split across ticks, with its own revision
// ---------------------------------------------------------------------------
// The K_ACTOR_STATES shape, for the reasons documented there: no produced count (the
// header's `n` is the container size and the receiver reads entries until the body ends),
// and a continuation is a CURSOR ACROSS TICKS rather than a fragment (a datagram carries
// whole rows or it does not go out).
//
// The difference from that lane is the clock: this one is RELIABLE, so a change is sent
// at once and there is no 1 Hz heartbeat.  `kRepairMs` re-sends the whole set when
// nothing has gone out for a while, which is the repair path for a peer that joined, a
// suspend load, or an ack the relay lost.
class Publisher {
public:
    struct Chunk {
        int  entries;   // rows written into this datagram
        int  bytes;     // bytes written, including the 6-byte header
        bool blocked;   // the next row cannot fit even in an empty datagram
    };

    Publisher() { Reset(); }

    void Reset()
    {
        set_.Clear();
        rev_ = 1;
        dirty_ = true;
        cursor_ = 0;
        lastSendMs_ = 0;
        haveSent_ = false;
    }

    uint32_t Revision() const { return rev_; }
    bool     Dirty() const { return dirty_; }
    int      Cursor() const { return cursor_; }
    int      Size() const { return set_.Count(); }
    bool     Overflowed() const { return set_.Overflowed(); }

    // Re-send the whole set: after a join, after a suspend load on the peer, and after a
    // world change.  The container here is still the truth; the peer's copy may not be.
    void ForceDirty() { dirty_ = true; cursor_ = 0; }

    // Adopt a freshly-scanned set.  Returns true when it differs from the set being
    // published, which is the "on change" half of the rate rule.  A change restarts the
    // pass at row 0: rows already sent belong to the old set, and a mixed body would not
    // be the absolute state the header promises.
    bool Observe(const Snapshot& s)
    {
        if (set_.SameAs(s)) return false;
        set_ = s;
        dirty_ = true;
        cursor_ = 0;
        return true;
    }

    // True when a datagram is due.  A change or a half-sent pass always wins; otherwise
    // only the repair interval can produce one.
    bool Due(uint32_t now_ms) const
    {
        if (dirty_) return true;
        if (cursor_ > 0) return true;
        if (!haveSent_) return true;
        return (uint32_t)(now_ms - lastSendMs_) >= kRepairMs;
    }

    // Build one datagram from the cursor.  Does NOT advance the cursor: the caller only
    // commits after the socket took it.
    Chunk Peek(uint8_t* body, int cap) const
    {
        Chunk c = {0, 0, false};
        wire::Writer w(body, cap);
        // `n` is the whole set size, not this datagram's share.
        wire::WriteObjectsHeader(w, rev_, (uint16_t)set_.Count());
        if (!w.ok) { c.blocked = true; return c; }

        int next = wire::WriteObjectsChunk(w, set_.Data(), set_.Count(), cursor_);
        if (!w.ok) { c.blocked = true; c.bytes = 0; return c; }
        c.entries = next - cursor_;
        c.bytes = w.len;
        if (next < set_.Count() && c.entries == 0) c.blocked = true;
        return c;
    }

    // Commit a successful send.  Returns true when that send completed a whole pass, so
    // the caller can log it and so the revision only names a body that went out in full.
    bool Commit(int entries, uint32_t now_ms)
    {
        if (entries < 0) return false;
        lastSendMs_ = now_ms;
        haveSent_ = true;
        cursor_ += entries;
        if (cursor_ >= set_.Count()) {
            cursor_ = 0;
            dirty_ = false;
            ++rev_;
            if (rev_ == 0) rev_ = 1;
            return true;
        }
        return false;
    }

private:
    Snapshot set_;
    uint32_t rev_;
    bool     dirty_;
    int      cursor_;
    uint32_t lastSendMs_;
    bool     haveSent_;
};

// ---------------------------------------------------------------------------
// Peer side: the local changes waiting to become INTENT kind 9
// ---------------------------------------------------------------------------
// The same three failures `actorstate::IntentQueue` documents, and the same answers:
//
//   ECHO.   Applying the host's body writes the peer's own property, so the local
//           detector sees the host's value moments later as if the peer had produced it.
//           A value equal to what the host last published is never sent back, and a local
//           change back to the host's value is an echo AND cancels a pending intent --
//           otherwise a peer that toggled twice leaves a stale intent behind.
//
//   RETRY.  An intent can cross a body already in flight.  A refused intent stays queued
//           and is re-sent with the freshest revision until the host echoes it or the
//           bounded attempt count says it will not.
//
//   BURST.  One interaction can touch a chest and the actor it activates in the same
//           frame.  A key is coalesced to its latest value, and the send rate is bounded
//           per tick and per second.
class IntentQueue {
public:
    static const int kMaxPending = 16;
    static const int kMaxEcho = kMaxEntries;
    static const int kMaxAttempts = 5;
    static const uint32_t kRetryMs = 1000;
    static const uint32_t kRefillMs = 250;
    static const int      kBucket = 4;
    static const int      kMaxPerTick = 2;

    struct Out {
        uint32_t key[4];
        uint8_t  shape;
        uint8_t  state;
        int      attempt;
    };

    enum Observed {
        kQueued,
        kCoalesced,
        kDuplicate,
        kEcho,
        kOverflow,
    };

    enum Resolve {
        kNoPending,
        kConfirmed,
        kHeld,
    };

    IntentQueue() { Reset(); }

    void Reset()
    {
        for (int i = 0; i < kMaxPending; ++i) {
            p_[i].used = false;
            p_[i].key[0] = p_[i].key[1] = p_[i].key[2] = p_[i].key[3] = 0;
            p_[i].shape = 0;
            p_[i].state = 0;
            p_[i].attempts = 0;
            p_[i].lastSendMs = 0;
        }
        for (int i = 0; i < kMaxEcho; ++i) {
            e_[i].used = false;
            e_[i].key[0] = e_[i].key[1] = e_[i].key[2] = e_[i].key[3] = 0;
            e_[i].state = 0;
        }
        echoNext_ = 0;
        tokens_ = kBucket;
        refillMs_ = 0;
        refillInit_ = false;
        overflowPending_ = false;
        overflowEcho_ = false;
    }

    // ---- what the host last published -------------------------------------
    void RememberEcho(const uint32_t key[4], uint8_t state)
    {
        for (int i = 0; i < kMaxEcho; ++i) {
            if (e_[i].used && KeyEq(e_[i].key, key)) {
                e_[i].state = state;
                return;
            }
        }
        for (int i = 0; i < kMaxEcho; ++i) {
            if (!e_[i].used) {
                e_[i].used = true;
                KeyCopy(e_[i].key, key);
                e_[i].state = state;
                return;
            }
        }
        // Full.  Cannot happen while the table matches the container bound, but the
        // newest value must still win if it does: dropping it is the ping-pong.
        Echo& e = e_[echoNext_++ % kMaxEcho];
        e.used = true;
        KeyCopy(e.key, key);
        e.state = state;
        overflowEcho_ = true;
    }

    bool EchoMatches(const uint32_t key[4], uint8_t state) const
    {
        for (int i = 0; i < kMaxEcho; ++i) {
            if (e_[i].used && e_[i].state == state && KeyEq(e_[i].key, key)) return true;
        }
        return false;
    }

    // ---- what the local game did -------------------------------------------
    Observed Observe(const uint32_t key[4], uint8_t shape, uint8_t state)
    {
        if (EchoMatches(key, state)) {
            // Back at the host's value: any queued intent for this key is now stale.
            Cancel(key);
            return kEcho;
        }

        int i = FindPending(key);
        if (i >= 0) {
            if (p_[i].state == state && p_[i].shape == shape) return kDuplicate;
            p_[i].state = state;
            p_[i].shape = shape;
            p_[i].attempts = 0;
            p_[i].lastSendMs = 0;
            return kCoalesced;
        }

        i = FreePending();
        if (i < 0) { overflowPending_ = true; return kOverflow; }
        Pending& p = p_[i];
        p.used = true;
        KeyCopy(p.key, key);
        p.shape = shape;
        p.state = state;
        p.attempts = 0;
        p.lastSendMs = 0;
        return kQueued;
    }

    // ---- what the host's body now says ------------------------------------
    // `shouldApply` tells the caller whether the host's value may be written locally:
    //
    //   no pending intent     -> apply
    //   host value == asked   -> apply (same value) and settle
    //   host value differs    -> HOLD: this body is indistinguishable from one that left
    //                            the host before the intent arrived, so applying it would
    //                            silently undo the peer's action.  The retry resolves it.
    Resolve OnHostEntry(const uint32_t key[4], uint8_t state, bool* shouldApply)
    {
        if (shouldApply) *shouldApply = true;
        int i = FindPending(key);
        if (i < 0) return kNoPending;
        if (state == p_[i].state) {
            p_[i].used = false;
            return kConfirmed;
        }
        if (shouldApply) *shouldApply = false;
        return kHeld;
    }

    // ---- what may go on the wire now --------------------------------------
    int Due(uint32_t now_ms, Out* out, int cap)
    {
        Refill(now_ms);
        if (cap > kMaxPerTick) cap = kMaxPerTick;
        int n = 0;
        while (n < cap && tokens_ > 0) {
            int i = PickFresh();
            if (i < 0) i = PickRetry(now_ms);
            if (i < 0) break;

            Pending& p = p_[i];
            ++p.attempts;
            p.lastSendMs = now_ms;
            KeyCopy(out[n].key, p.key);
            out[n].shape = p.shape;
            out[n].state = p.state;
            out[n].attempt = p.attempts;
            ++n;
            --tokens_;
        }
        return n;
    }

    bool GiveUp(const uint32_t key[4])
    {
        int i = FindPending(key);
        if (i < 0) return false;
        p_[i].used = false;
        return true;
    }

    int  Count() const
    {
        int n = 0;
        for (int i = 0; i < kMaxPending; ++i) if (p_[i].used) ++n;
        return n;
    }
    bool Empty() const { return Count() == 0; }
    bool PendingOverflowed() const { return overflowPending_; }
    bool EchoOverflowed() const { return overflowEcho_; }

private:
    struct Pending {
        bool     used;
        uint32_t key[4];
        uint8_t  shape;
        uint8_t  state;
        int      attempts;
        uint32_t lastSendMs;
    };

    struct Echo {
        bool     used;
        uint32_t key[4];
        uint8_t  state;
    };

    bool Cancel(const uint32_t key[4])
    {
        int i = FindPending(key);
        if (i < 0) return false;
        p_[i].used = false;
        return true;
    }

    int FindPending(const uint32_t key[4]) const
    {
        for (int i = 0; i < kMaxPending; ++i) {
            if (p_[i].used && KeyEq(p_[i].key, key)) return i;
        }
        return -1;
    }

    int FreePending() const
    {
        for (int i = 0; i < kMaxPending; ++i) if (!p_[i].used) return i;
        return -1;
    }

    int PickFresh() const
    {
        for (int i = 0; i < kMaxPending; ++i) {
            if (p_[i].used && p_[i].attempts == 0) return i;
        }
        return -1;
    }

    int PickRetry(uint32_t now_ms) const
    {
        for (int i = 0; i < kMaxPending; ++i) {
            if (!p_[i].used || p_[i].attempts == 0) continue;
            if (p_[i].attempts >= kMaxAttempts) continue;      // spent; the caller reaps it
            uint32_t age = now_ms - p_[i].lastSendMs;
            if (age >= kRetryMs) return i;
        }
        return -1;
    }

    void Refill(uint32_t now_ms)
    {
        if (!refillInit_) { refillInit_ = true; refillMs_ = now_ms; tokens_ = kBucket; return; }
        uint32_t elapsed = now_ms - refillMs_;
        if (elapsed < kRefillMs) return;
        uint32_t add = elapsed / kRefillMs;
        refillMs_ += add * kRefillMs;
        if (add >= (uint32_t)kBucket) tokens_ = kBucket;
        else {
            tokens_ += (int)add;
            if (tokens_ > kBucket) tokens_ = kBucket;
        }
    }

    Pending  p_[kMaxPending];
    Echo     e_[kMaxEcho];
    int      echoNext_;
    int      tokens_;
    uint32_t refillMs_;
    bool     refillInit_;
    bool     overflowPending_;
    bool     overflowEcho_;
};

// ---------------------------------------------------------------------------
// Peer side: the local detector
// ---------------------------------------------------------------------------
// The object's own property is the only signal a peer's action produces: the game writes
// its bool, not a message.  So the peer keeps the values it saw last, and every value
// that moved is handed to the queue.  An entry that DISAPPEARED is not an intent: this is
// a placed actor that left the world (an unloaded level), not an action.
class LocalDetector {
public:
    struct Stats {
        int changed;
        int echoes;
        int queued;
        int coalesced;
        int duplicates;
        int overflow;
    };

    LocalDetector() { Reset(); }

    void Reset()
    {
        seen_.Clear();
        have_ = false;
    }

    bool Known(const uint32_t key[4], uint8_t* out) const
    {
        if (!have_) return false;
        int j = seen_.Find(key);
        if (j < 0) return false;
        if (out) *out = seen_.At(j)->state;
        return true;
    }

    void Observe(const Snapshot& current, IntentQueue& q, Stats* st)
    {
        if (st) { st->changed = st->echoes = st->queued = 0;
                  st->coalesced = st->duplicates = st->overflow = 0; }
        if (!have_) {
            // The first sample is the baseline, never a burst of intents: at join time the
            // peer's world predates the host's body, and every open chest would otherwise
            // be offered as a change.
            seen_ = current;
            have_ = true;
            return;
        }

        for (int i = 0; i < current.Count(); ++i) {
            const Row* r = current.At(i);
            int j = seen_.Find(r->key);
            if (j >= 0 && seen_.At(j)->state == r->state) continue;
            if (st) ++st->changed;
            IntentQueue::Observed o = q.Observe(r->key, r->shape, r->state);
            switch (o) {
            case IntentQueue::kEcho:      if (st) ++st->echoes; break;
            case IntentQueue::kQueued:    if (st) ++st->queued; break;
            case IntentQueue::kCoalesced: if (st) ++st->coalesced; break;
            case IntentQueue::kDuplicate: if (st) ++st->duplicates; break;
            case IntentQueue::kOverflow:  if (st) ++st->overflow; break;
            }
            seen_.Set(r->key, r->shape, r->state);
        }
    }

    // After the caller writes a host body value, record it so the next poll does not have
    // to discover it.  Before the first sample this is a no-op: the first suspect sample
    // is the BASELINE, and turning one host-applied row into the baseline would make
    // every other row look like a local change.
    void Note(const uint32_t key[4], uint8_t shape, uint8_t state)
    {
        if (!have_) return;
        seen_.Set(key, shape, state);
    }

private:
    Snapshot seen_;
    bool     have_;
};

// ---------------------------------------------------------------------------
// The one-directional apply rule, named so it cannot be mistaken for an accident
// ---------------------------------------------------------------------------
// The ledger carries an ABSOLUTE state, but the apply is not symmetric, and that is a
// measurement, not a preference: the game's close/deactivate verbs are the INTERACTION
// path (`Interact` -> `CloseDoor`, `Interact` -> `InsertKey`), so calling one to mirror a
// `0` would run the verb the partner did not run.  Open/activate is expressible through a
// visual path (`SetToFinalState`, `SetToOpenState`, `SetActorHiddenInGame` + collision),
// close is not.  A `0` therefore never undoes a local open; it is logged once.
inline bool ApplyAllowed(uint8_t hostState, uint8_t localState, bool haveLocal)
{
    if (hostState == 0) return false;      // no loot-free close path: never undo an open
    if (haveLocal && localState == hostState) return false;   // already there
    return true;
}

} // namespace objects
} // namespace qd
