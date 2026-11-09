// qdtravel.h -- the travel FOLLOW board (SYNC-GAPS G6, the half MapBoard does not do).
//
// WHY THIS IS BESIDE MapBoard AND NOT INSIDE IT
// ---------------------------------------------
// `MapBoard` (qdstream.h) answers one question: "are the two machines in the same
// world, and if not, may I apply the partner's cross-world state?"  It is deliberately
// passive -- announcement plus refusal -- because when it was written there was no
// measured way to make a machine take another machine's transition.
//
// There is one now: `FS_PlayerChar.TeleportToLocation(LocationData, GreenFX)`, measured
// from the cooked class (SYNC-RESEARCH 7).  So the second question is "the partner just
// took a transition; may I take the same one, and have I already taken it?"  That is a
// different state machine with a different failure mode, and mixing it into MapBoard
// would make the divergence debounce and the follow dedup share one reset.
//
// THE IDENTITY, AND WHY IT IS NAMED THIS WAY
// ------------------------------------------
// `dest` is the placed actor that carries the game's own `LocationData` (a teleporter,
// a checkpoint, a cutscene trigger), written as `<actor>/<world>`.
//
// Measured on the cooked corpus (analysis/pak/code, the property-tag walk of
// SYNC-RESEARCH section A):
//
//   * `Teleporter_C.LocationData` and `Checkpoint_C.LocationData` are both
//     `ObjectProperty` (size 8).  The object is a `FS_LocationData` primary data asset
//     (`/Script/QueensDomain`, its only Blueprint subclass `TeleporterData_C` declares
//     no properties of its own).  A data asset is the same object on both machines, but
//     it names the *destination data*, not the placed actor the wire comment asks for.
//   * `Checkpoint_C` does carry a 16-byte `StructProperty GUID`; `Teleporter_C` does
//     **not**.  A GUID identity therefore cannot cover the carrier set, so it is not the
//     scheme.  (A scheme that works for checkpoints but silently skips every teleporter
//     is worse than one scheme that works for both.)
//   * Neither actor class exposes a name inside `LocationData` that both machines can
//     read without resolving the asset first.
//
// What IS true on both machines: both run the same cooked world, so a placed actor has
// the same short object name in both (the same convention `ShortLevelName` /
// `LevelNameOf` use for levels).  So `dest` is the actor's own short name plus the map
// the sender announced -- `Teleporter_500_AUT_02/100_GR`.
//
// THE FAILURE THIS BOARD EXISTS TO PREVENT
// ----------------------------------------
// Three ways a naive follow is worse than no follow:
//
//   * It fights the player.  A follow that lands in the middle of the local player's
//     own teleport, a menu, a cutscene or a conversation is a teleport the player did
//     not ask for.  `busy` therefore skips, and the skip is not remembered, so the
//     follow can still happen once the player is free.
//   * It spins.  A destination whose actor is not in this world (its level is not
//     loaded, or the scheme did not resolve) will never resolve by trying again every
//     2 s for the rest of the session.  A failed destination is remembered and never
//     retried, and the set is a bounded ring so a bad spell cannot lock travel off.
//   * It echoes.  The follower's own pawn then reports the same destination, which the
//     sender would read as a new travel and follow back.  A destination this machine
//     followed is therefore *not announced* while the travel it caused is in progress;
//     the echo stops after one hop, not after a ping-pong.
//
// Everything here is pure: no UObject, no ProcessEvent, no clock read.  `test_travel.cpp`
// drives it with the cases above.
#pragma once
#include <stdint.h>

#include "qdstream.h"   // PlausibleLevelName / kMaxNameLen: the world half of dest

namespace qd {
namespace travel {

// Wire sizes (`wire::TravelBody`: map[64], dest[96]).  A dest that does not fit is
// refused rather than truncated: a truncated name resolves to a different actor.
static const int kDestCap = 96;
static const int kMapCap  = 64;
static const int kActorCap = 64;
static const int kWorldCap = 32;

// A bounded refusal set.  A session crosses a handful of destinations; a ring means a
// destination that genuinely failed cannot be retried, while the set can never grow
// without limit or permanently lock out a new destination once a few have failed.
static const int kMaxGaveUp = 16;

// How long the echo latch survives after the travel a follow caused has ended.  The
// travel itself already extends it; this covers the frame or two in which the game has
// not yet set `Teleporting` after `TeleportToLocation` returned, and it is short enough
// (two heartbeats) that a deliberate trip back to the same actor is announced normally.
static const uint32_t kEchoGraceMs = 2000;

// What to do with one inbound announcement.  Named rather than a bool so the caller
// logs the reason a follow did not happen -- "no teleport" would hide every guard.
enum Verdict {
    V_NONE = 0,      // empty dest: not a travel announcement
    V_FOLLOW,        // act: resolve the actor and drive the local pawn
    V_DUPLICATE,     // this (seq, dest) was already acted on: a heartbeat
    V_BUSY,          // the local player is travelling / in a menu / cutscene / dialogue
    V_WRONG_WORLD,   // the destination is in a different world than this machine
    V_GAVE_UP,       // this destination failed to resolve before: never retry
    V_BAD_DEST       // dest is not exactly "<actor>/<world>"
};

// The two halves of a dest.  Split on the single '/'.
struct DestParts {
    char actor[kActorCap];
    char world[kWorldCap];
};

inline bool SameText(const char* a, const char* b)
{
    if (!a || !b) return a == b;
    int i = 0;
    for (; a[i] || b[i]; ++i) if (a[i] != b[i]) return false;
    return true;
}

inline void CopyText(char* dst, int cap, const char* src)
{
    if (!dst || cap <= 0) return;
    int i = 0;
    if (src) for (; src[i] && i < cap - 1; ++i) dst[i] = src[i];
    dst[i] = 0;
}

// Printable, non-slash, non-space.  Actor names are FNames, so the printable set is
// wider than a level name's, but a slash would make the split ambiguous and a space
// would be the Blueprint *pin* display name rather than the object name.
inline bool PlausibleActorName(const char* s)
{
    if (!s || !s[0]) return false;
    int n = 0;
    for (; s[n]; ++n) {
        if (n >= kActorCap - 1) return false;
        unsigned char c = (unsigned char)s[n];
        if (c < 0x21 || c > 0x7e || c == '/') return false;
    }
    return n > 0;
}

// "<actor>/<world>".  Exactly one '/', both halves non-empty and plausible.
inline bool SplitDest(const char* dest, DestParts* out)
{
    if (!dest || !dest[0] || !out) return false;
    int slash = -1;
    for (int i = 0; dest[i]; ++i) {
        unsigned char c = (unsigned char)dest[i];
        if (c < 0x21 || c > 0x7e) return false;
        if (c == '/') {
            if (slash >= 0) return false;      // two slashes: not our format
            slash = i;
        }
    }
    if (slash <= 0 || !dest[slash + 1]) return false;
    if (slash >= kActorCap) return false;      // the actor half does not fit its buffer

    int k = 0;
    for (int i = 0; i < slash; ++i, ++k) out->actor[k] = dest[i];
    out->actor[k] = 0;

    k = 0;
    for (int i = slash + 1; dest[i]; ++i, ++k) {
        if (k >= kWorldCap - 1) return false;  // world half too long: refuse, never cut
        out->world[k] = dest[i];
    }
    out->world[k] = 0;

    return PlausibleActorName(out->actor) && stream::PlausibleLevelName(out->world);
}

// The announce side's formatter.  Returns false (and leaves `out` empty) when either
// half cannot be written, so a caller can never publish a half-name.
inline bool MakeDest(const char* actor, const char* world, char* out, int cap)
{
    if (!out || cap <= 0) return false;
    out[0] = 0;
    if (!PlausibleActorName(actor)) return false;
    if (!stream::PlausibleLevelName(world)) return false;

    int a = 0; for (; actor[a]; ++a) {}
    int w = 0; for (; world[w]; ++w) {}
    if (a + 1 + w + 1 > cap) return false;     // +1 '/', +1 terminator

    int k = 0;
    for (int i = 0; i < a; ++i) out[k++] = actor[i];
    out[k++] = '/';
    for (int i = 0; i < w; ++i) out[k++] = world[i];
    out[k] = 0;
    return true;
}

// ---------------------------------------------------------------------------
// Board -- the follow decision and its memory.
// ---------------------------------------------------------------------------
class Board {
public:
    Board() { Reset(); }

    void Reset()
    {
        lastDest_[0] = 0;
        lastSeq_ = 0;
        haveLast_ = false;
        for (int i = 0; i < kMaxGaveUp; ++i) gaveUp_[i][0] = 0;
        gaveUpN_ = 0;
        gaveUpNext_ = 0;
        busyDest_[0] = 0;
        followed_[0] = 0;
        followedUntil_ = 0;
        followedActive_ = false;
        announced_[0] = 0;
    }

    // ---- receive side -----------------------------------------------------
    // Evaluate one inbound announcement.  `localMap` is OUR map name, so a dest whose
    // world half names another world is refused without touching the game.  `busy` is
    // the local guard (travelling / menu / cutscene / dialogue) computed by the caller.
    Verdict OnPeer(uint32_t seq, const char* dest, const char* localMap, bool busy)
    {
        if (!dest || !dest[0]) return V_NONE;

        // A heartbeat repeats (seq, dest); it must not teleport twice.
        if (haveLast_ && seq == lastSeq_ && SameText(dest, lastDest_)) return V_DUPLICATE;

        // A destination that failed before is never retried.  This is the "do not spin"
        // rule: the actor is not going to appear because another heartbeat went by.
        if (GaveUp(dest)) return V_GAVE_UP;

        DestParts p;
        if (!SplitDest(dest, &p)) {
            // Remember it so a heartbeat does not produce one warning per announcement.
            Remember(seq, dest);
            return V_BAD_DEST;
        }

        // BUSY is checked before the world, not after.  A local transition leaves the
        // map name one frame stale; calling that a "wrong world" would permanently
        // refuse a destination that only needed the teleport to finish.
        if (busy) {
            if (!SameText(dest, busyDest_)) {
                CopyText(busyDest_, kDestCap, dest);
                return V_BUSY;          // log once per destination, not per heartbeat
            }
            return V_NONE;
        }
        busyDest_[0] = 0;

        // The sender derives the world half from its own map name, so a match is the
        // "same map before the transition" precondition, and a mismatch is the hard
        // map switch: the actor is not in this world and trying again cannot help.
        // An empty local map is "our world is not known yet" (a load, a teardown), not
        // a mismatch: refusing there would permanently give up on a transient.
        if (!localMap || !localMap[0]) return V_NONE;
        if (!SameText(p.world, localMap)) {
            Remember(seq, dest);
            AddGaveUp(dest);
            return V_WRONG_WORLD;
        }

        Remember(seq, dest);
        return V_FOLLOW;
    }

    // The caller ran the follow.  `ok` closes the loop: success arms the echo guard,
    // failure remembers the destination so the next heartbeat is a V_GAVE_UP, not
    // another attempt.
    void OnFollowResult(bool ok, uint32_t now)
    {
        if (!haveLast_) return;
        if (ok) {
            CopyText(followed_, kDestCap, lastDest_);
            followedUntil_ = now + kEchoGraceMs;
            followedActive_ = true;
        } else {
            AddGaveUp(lastDest_);
        }
    }

    // ---- announce side ----------------------------------------------------
    // Feed the destination the LOCAL player is taking ("" / travelling=false when not
    // travelling).  Returns true when the value that should be sent changed, so the
    // caller bumps `seq`.  A destination this machine is currently following is
    // suppressed to "" -- announcing it would invite the partner to teleport back.
    bool NoteLocal(const char* dest, bool travelling, uint32_t now)
    {
        char want[kDestCap];
        want[0] = 0;

        bool echo = followedActive_ && travelling && dest && dest[0] &&
                    SameText(dest, followed_);
        if (travelling && dest && dest[0] && !echo) CopyText(want, kDestCap, dest);

        // While the followed travel is running, keep the latch open: the travel can be
        // much longer than the grace, and ending the suppression early is exactly the
        // heartbeat that echoes.  When it ends, the grace covers the frames before the
        // game clears its own teleport state.
        if (echo) followedUntil_ = now + kEchoGraceMs;
        if (followedActive_ && !travelling && (int32_t)(now - followedUntil_) >= 0) {
            followedActive_ = false;
            followed_[0] = 0;
        }

        bool changed = !SameText(want, announced_);
        CopyText(announced_, kDestCap, want);
        return changed;
    }

    const char* Announce() const { return announced_; }
    uint32_t LastSeq() const { return lastSeq_; }
    const char* LastDest() const { return lastDest_; }
    bool HasActed() const { return haveLast_; }
    bool Following() const { return followedActive_; }
    const char* Followed() const { return followed_; }

private:
    void Remember(uint32_t seq, const char* dest)
    {
        lastSeq_ = seq;
        haveLast_ = true;
        CopyText(lastDest_, kDestCap, dest);
    }

    bool GaveUp(const char* dest) const
    {
        for (int i = 0; i < gaveUpN_; ++i) if (SameText(gaveUp_[i], dest)) return true;
        return false;
    }

    void AddGaveUp(const char* dest)
    {
        if (!dest || !dest[0] || GaveUp(dest)) return;
        if (gaveUpN_ < kMaxGaveUp) {
            CopyText(gaveUp_[gaveUpN_], kDestCap, dest);
            ++gaveUpN_;
        } else {
            // Ring: the oldest refusal gives way.  A feature that can be locked off for
            // the rest of a session by sixteen failed teleports is a bug of its own.
            CopyText(gaveUp_[gaveUpNext_], kDestCap, dest);
            gaveUpNext_ = (gaveUpNext_ + 1) % kMaxGaveUp;
        }
    }

    char     lastDest_[kDestCap];
    uint32_t lastSeq_;
    bool     haveLast_;

    char     gaveUp_[kMaxGaveUp][kDestCap];
    int      gaveUpN_;
    int      gaveUpNext_;

    char     busyDest_[kDestCap];
    char     followed_[kDestCap];
    uint32_t followedUntil_;
    bool     followedActive_;
    char     announced_[kDestCap];
};

// ---------------------------------------------------------------------------
// HardBoard -- a hard map switch: both arrive, then resume (SYNC-GAPS G6).
// ---------------------------------------------------------------------------
// `Board` above follows a travel INSIDE one UWorld.  A hard transition into another
// UWorld (900_SipehaWorld, 990_B33sment, the 997/998/999 cutscene maps) is a different
// problem: there is no destination actor to resolve on the follower, so the follower
// must take the same map load.  The game's own entry for that is
// `UFS_GameInstanceBase.LoadMapWithLoadingScreen` (measured; WIRE.md 3.15a), not
// `OpenLevel`.
//
// The handshake exists because "both machines are in the same world" is not implied by
// "one machine asked to load".  A load takes seconds, can fail, and can be followed by
// a second transition.  Applying absolute state (flags, actor states, inventory, enemy
// rows) while the two worlds differ is worse than dropping it, so:
//
//   HARD_BEGIN  one machine crossed; the other loads the named map.
//   ARRIVED     each machine says "my world is now that map".
//   RESUME      the host releases the pair once both have arrived.
//
// The host is authoritative on a collision: if both crossed to different maps at the
// same instant, the host's map wins and the peer re-follows once.  Everything here is
// pure; `test_travel.cpp` drives the cases.
static const uint32_t kHardDeadlineMs = 60000;   // a load slower than this has failed
static const int      kHardMaxGaveUp  = 8;

enum HardVerdict {
    HV_NONE = 0,          // nothing to do
    HV_ANNOUNCE_BEGIN,    // the local world changed: send HARD_BEGIN
    HV_LOAD,              // the peer began a hard switch: load the named map
    HV_ANNOUNCE_ARRIVE,   // our world is the announced map: send ARRIVED
    HV_RESUME,            // both arrived: the host may send RESUME
    HV_REFUSED,           // implausible map, or one already given up on
    HV_IGNORED            // an echo / a repeat / a different world's news
};

class HardBoard {
public:
    HardBoard() { Reset(); }

    void Reset()
    {
        local_[0] = 0;
        target_[0] = 0;
        active_ = false;
        localArrived_ = false;
        peerArrived_ = false;
        resuming_ = false;
        startedMs_ = 0;
        seq_ = 0;
        announcePhase_ = 0;
        suppressNextLocal_ = false;
        for (int i = 0; i < kHardMaxGaveUp; ++i) gaveUp_[i][0] = 0;
        gaveUpN_ = 0;
        gaveUpNext_ = 0;
    }

    // ---- local observation ------------------------------------------------
    // Feed the local world name.  Returns true when the LOCAL world changed to a
    // different map: the caller must send HARD_BEGIN.  The change a follow caused is
    // suppressed (otherwise the two machines would ping-pong).
    bool OnLocalMap(const char* map, uint32_t now)
    {
        if (!map) map = "";
        if (SameText(map, local_)) {
            if (active_ && localArrived_ && !SameText(map, target_)) localArrived_ = false;
            return false;
        }
        bool had = local_[0] != 0;
        CopyText(local_, kDestCap, map);

        // A map this machine is IN is reachable.  A previous failure must not block a
        // return to it: measured (Session 7), the boot race where both machines announce
        // the same transition left `Ruins_Level_00` in the peer's gave-up ring, and the
        // deliberate return home was then refused for the rest of the session.
        RemoveGaveUp(map);

        // The load this board asked for: it is an arrival, not a new transition.
        if (active_ && SameText(map, target_)) {
            localArrived_ = true;
            suppressNextLocal_ = true;
            announcePhase_ = 2;               // ARRIVED (both machines must say it)
            return false;
        }
        if (suppressNextLocal_) {
            suppressNextLocal_ = false;
            return false;
        }
        if (!had || !map[0]) return false;    // the first observation is a baseline

        // A genuine local hard switch.  We are already in the new world, so this
        // machine has "arrived"; the peer has not until it says so.  Announcing the
        // begin is what lets it follow.
        seq_ = 0;
        CopyText(target_, kDestCap, map);
        active_ = true;
        localArrived_ = true;
        peerArrived_ = false;
        resuming_ = false;
        startedMs_ = now;
        announcePhase_ = 1;                   // HARD_BEGIN
        return true;
    }

    // ---- inbound ----------------------------------------------------------
    // `busy` is the local guard (travelling / menu / cutscene / dialogue).  A busy
    // player is skipped and NOT remembered, so the follow can still happen later.
    HardVerdict OnPeerBegin(uint32_t seq, const char* map, const char* localMap, bool busy)
    {
        if (!map || !map[0]) return HV_REFUSED;
        if (!stream::PlausibleLevelName(map)) return HV_REFUSED;
        if (GaveUp(map)) return HV_REFUSED;
        if (seq != 0 && seq == seq_ && SameText(map, target_)) return HV_IGNORED;

        if (localMap && localMap[0] && SameText(map, localMap)) {
            // We are already there: tell the sender so, and do not load anything.
            seq_ = seq;
            CopyText(target_, kDestCap, map);
            localArrived_ = true;
            peerArrived_ = true;
            active_ = false;
            announcePhase_ = 2;               // ARRIVED
            return HV_ANNOUNCE_ARRIVE;
        }
        if (busy) return HV_NONE;             // not remembered: retry when free

        seq_ = seq;
        CopyText(target_, kDestCap, map);
        active_ = true;
        localArrived_ = false;
        peerArrived_ = true;                  // the sender crossed, so it is there
        resuming_ = false;
        startedMs_ = 0;                       // armed by the caller's `Arm(now)`
        announcePhase_ = 0;
        return HV_LOAD;
    }

    // The caller is about to attempt the load.
    void Arm(uint32_t now) { if (active_) startedMs_ = now; }

    HardVerdict OnPeerArrived(uint32_t seq, const char* map, const char* localMap)
    {
        (void)seq;                            // the map is the identity; the seq is for logs
        if (!active_ || !SameText(map, target_)) return HV_IGNORED;
        peerArrived_ = true;
        if (localMap && localMap[0] && SameText(map, localMap)) localArrived_ = true;
        return HV_NONE;
    }

    HardVerdict OnPeerResume(uint32_t seq, const char* map)
    {
        (void)seq;
        if (!SameText(map, target_)) return HV_IGNORED;
        if (!localArrived_) return HV_REFUSED;   // never resume into a map we are not in
        resuming_ = true;
        active_ = false;
        return HV_RESUME;
    }

    // ---- local outcome ----------------------------------------------------
    void OnLoadResult(bool ok, uint32_t now)
    {
        if (ok) return;                       // ARRIVED is observed from the world name
        if (active_) { AddGaveUp(target_); active_ = false; }
        localArrived_ = false;
        (void)now;
    }

    // The host has sent RESUME; the transition is over for it too, or `ReadyToResume`
    // would queue another RESUME on every heartbeat for the rest of the session.
    void MarkResumed()
    {
        active_ = false;
        resuming_ = true;
    }

    // This machine is in the target world WITHOUT a load: the target is a streamed sublevel
    // of the world it is already in, so the map name never changes and `OnLocalMap` can
    // never observe an arrival (Session 14, R102).  Called only by the streamed-travel
    // branch, which has just done what the game does instead of loading.
    void MarkLocalArrived() { localArrived_ = true; }

    // True when the deadline expired and the transition was abandoned.
    bool Tick(uint32_t now)
    {
        if (!active_ || startedMs_ == 0) return false;
        if ((uint32_t)(now - startedMs_) < kHardDeadlineMs) return false;
        // A transition whose target is the map this machine is ALREADY IN is not a failed
        // load: the boot race arms exactly that (both machines observe the same start
        // transition and announce it).  Remembering it would refuse the next deliberate
        // journey to that map for the rest of the session -- measured live, Session 7,
        // where the return home was refused after the boot announcement timed out.
        if (!SameText(target_, local_)) AddGaveUp(target_);
        active_ = false;
        localArrived_ = false;
        return true;
    }

    // ---- announce side ----------------------------------------------------
    // The phase the caller should send next (0 = none, 1 = begin, 2 = arrived,
    // 3 = resume), and the map it refers to.
    uint8_t TakeAnnounce()
    {
        uint8_t p = announcePhase_;
        announcePhase_ = 0;
        return p;
    }
    void NoteAnnounce(uint8_t phase) { if (phase > announcePhase_) announcePhase_ = phase; }
    const char* TargetMap() const { return target_; }
    const char* LocalMap() const { return local_; }

    // ---- queries ----------------------------------------------------------
    bool Active() const { return active_; }
    bool BothArrived() const { return localArrived_ && peerArrived_; }
    bool ReadyToResume() const
    {
        return active_ && BothArrived() && !resuming_ && !GaveUp(target_);
    }
    bool Resuming() const { return resuming_; }
    bool LocalArrived() const { return localArrived_; }
    bool PeerArrived() const { return peerArrived_; }
    bool IsTarget(const char* map) const { return SameText(map, target_); }
    bool GaveUp(const char* map) const
    {
        for (int i = 0; i < gaveUpN_; ++i) if (SameText(gaveUp_[i], map)) return true;
        return false;
    }
    uint32_t Seq() const { return seq_; }

private:
    void AddGaveUp(const char* map)
    {
        if (!map || !map[0] || GaveUp(map)) return;
        if (gaveUpN_ < kHardMaxGaveUp) {
            CopyText(gaveUp_[gaveUpN_], kDestCap, map);
            ++gaveUpN_;
        } else {
            CopyText(gaveUp_[gaveUpNext_], kDestCap, map);
            gaveUpNext_ = (gaveUpNext_ + 1) % kHardMaxGaveUp;
        }
    }

    void RemoveGaveUp(const char* map)
    {
        if (!map || !map[0]) return;
        for (int i = 0; i < gaveUpN_; ++i) {
            if (!SameText(gaveUp_[i], map)) continue;
            for (int k = i; k + 1 < gaveUpN_; ++k) CopyText(gaveUp_[k], kDestCap, gaveUp_[k + 1]);
            --gaveUpN_;
            return;
        }
    }

    char     local_[kDestCap];
    char     target_[kDestCap];
    bool     active_;
    bool     localArrived_;
    bool     peerArrived_;
    bool     resuming_;
    uint32_t startedMs_;
    uint32_t seq_;
    uint8_t  announcePhase_;
    bool     suppressNextLocal_;
    char     gaveUp_[kHardMaxGaveUp][kDestCap];
    int      gaveUpN_;
    int      gaveUpNext_;
};

// ---------------------------------------------------------------------------
// RefuseCurrentWorld -- never hard-load the world this machine is in.
// ---------------------------------------------------------------------------
// MEASURED (Session 11 R92): `LoadMapWithLoadingScreen(<the map we are already in>)`
// tears down the current UWorld while it is the current world, and the GAME faults with
// a stack entirely inside QueensDomain-Win64-Shipping (no WINMM frame, so it is not a
// mod pointer): `UE4CC-Windows-C2CD747E...`, EXCEPTION_ACCESS_VIOLATION, 214 s in.
//
// The handshake already answers "we are already there" with an arrival instead of a
// load, but two other paths reach the loader with a name they did not cross-check: the
// `wrong world` half of an in-world announcement, and the rig's own `qd.hard` lever.
// The guard belongs at the one place that performs the load, so no caller can add a
// third path that faults the game.
inline bool RefuseCurrentWorld(const char* target, const char* localMap)
{
    return target && target[0] && localMap && localMap[0] && SameText(target, localMap);
}

// ---------------------------------------------------------------------------
// ArrivalBoard -- where the player STANDS after a hard transition (Session 12)
// ---------------------------------------------------------------------------
// THE BUG THIS BOARD EXISTS TO FIX
// --------------------------------
// After a hard load the follower was a pawn at the origin, falling, and every teleport
// was undone by the game's own respawn (`scan: local at 0,0,-2729`).  Measured, and why
// there is nothing to "pass through" from the leader:
//
//   * `LoadMapWithLoadingScreen` has exactly two parameters -- `Map: NameProperty`,
//     `MapPackage: StrProperty` -- and its exec thunk (`0x00E58F70`) marshals those two
//     into the body (`0x00E34F30`), which only sets `bLoadingMap`, records
//     `CurrentlyLoadingMapPackage` and starts the async load.  No spawn travels in it.
//   * No cooked package names it (all 19,213 of them), and the only native caller of the
//     body is its own thunk.  The GAME never calls it, so there is no leader-side
//     parameter to capture.
//   * Only `Ruins_Level_00` places a `PlayerStart`.  `900_SipehaWorld`, `990_B33sment`,
//     `780_SZ_Depths`, `760_SZ_AUT`, `100_GR` and `997_FinalZoneCutscene` place none, so
//     `AGameModeBase::RestartPlayer` spawns DefaultPawnClass at the IDENTITY transform:
//     the pawn at the origin, inside no geometry, which falls, dies on the kill Z and
//     respawns there -- the loop Session 11 measured.
//
// WHAT THE GAME DOES INSTEAD, AND WHY THIS CAN USE IT
// ---------------------------------------------------
// The spawn point for those worlds is CONTENT in the destination world, resolved by
// class: `900_SipehaWorld` places exactly one `PlayerTeleportTargetPos`, and
// `990_B33sment` exactly one `B33smentTeleportPoint` (cooked export tables), and
// `FS_PlayerChar` resolves both with `GetAllActorsOfClass`.  Both machines load the same
// world, so both can find the same marker without a wire field -- and if the marker is
// missing, the partner's streamed position is the next best answer, which is the
// fallback the design names.
//
// THE RULES, AND WHY EACH ONE IS THERE
// ------------------------------------
//   * Only when the engine's own fallback happened.  A pawn at the origin is the exact
//     signature of "no PlayerStart"; a pawn standing anywhere else was placed by the game
//     and must not be moved.  (Both machines in `Ruins_Level_00` stand where the save put
//     them, ~4 km from the origin, so the hub is untouched.)  "Anywhere else" has to be
//     earned: the previous world's pawn reads at its old position for a second or two after
//     the swap (`kPlacedSettleMs`), and the same position in a PlayerStart-less world is the
//     VOID the player falls out of (`kFallCmPerPoll`).
//   * Only inside a window.  The game respawns the stranded pawn every few seconds, so
//     one placement is not enough and an unbounded one would fight the player forever.
//   * The partner first, the world's own marker second.  A partner that is standing
//     somewhere real is the most precise answer, and it is the design's named fallback;
//     the marker is the deterministic answer when BOTH machines were stranded, and it
//     is the same answer on both, which is what puts them next to each other.  A partner
//     row only counts once the load has settled (`kPeerSettleMs`): before that it still
//     describes the world we left.
//   * Bounded, and it stops on its own.  A pawn that is standing, a window that expired,
//     or `kArrivalMaxTries` placements end it, so a wrong guess costs a few seconds and
//     never becomes a loop.
static const uint32_t kArrivalWindowMs   = 45000;  // a respawn loop is ~5-10 s
static const int      kArrivalMaxTries   = 5;
static const float    kArrivalOriginRad  = 150.0f; // |x|,|y| of the identity spawn
static const uint32_t kPeerFreshMs       = 1500;   // a row older than this is another world
// How long after OUR arrival a partner row has to have arrived before it can be trusted as
// a position in the new world.  A world load is asynchronous: the sender keeps sending rows
// from the pawn it is destroying for a second or two after the map name changes, and those
// rows carry a mix of the two worlds.  Measured Session 12: the follower placed itself at
// the partner's HUB coordinates with the NEW world's Z, and stood in the void.
static const uint32_t kPeerSettleMs      = 3000;
// How long a pawn that is NOT at the origin has to stay there before it counts as "the
// world placed the player".  A world swap leaves the previous world's player readable for a
// frame or two, at the position it was standing in before the load; concluding from one poll
// skips the placement entirely (measured Session 12: the leader was never placed for exactly
// that reason, while the follower -- whose load finishes later -- was).
static const uint32_t kPlacedSettleMs    = 2000;
// A pawn that is falling is not standing, however far from the origin it is.  This is the
// rule that separates the two worlds Session 12 measured: in the hub the game restores the
// saved position and the player stands there (4106,-1744, unchanged for as long as you
// look), while in a world with no PlayerStart the same position is the VOID -- the player
// keeps the transform it had and drops out of it.  A drop of this many cm between two
// one-second polls is a fall, not a step.
static const float    kFallCmPerPoll     = 150.0f;
static const int      kArrivalWhyCap     = 32;

enum ArriveVerdict {
    AR_NONE = 0,      // not armed: no hard transition is settling
    AR_WAIT,          // armed, but nothing can be decided yet
    AR_PLACE_PEER,    // place at the partner's streamed position
    AR_PLACE_ANCHOR,  // place at the destination world's own arrival marker
    AR_DONE           // the player is standing, or the window/tries ended
};

struct ArrivalInput {
    bool     havePawn;
    float    px, py, pz;    // the local pawn's own position
    bool     havePeer;
    const char* peerMap;    // the partner's announced world
    const char* localMap;   // ours
    uint32_t peerAgeMs;     // age of the partner's last position row
    uint32_t peerRowMs;     // when that row ARRIVED (absolute, same clock as Arm)
    float    peerX, peerY;  // where the partner is (already offset by the caller)
    bool     haveAnchor;    // the world places an arrival marker
    float    anchorX, anchorY;
    const char* anchorWhy;  // its class, for the log
};

class ArrivalBoard {
public:
    ArrivalBoard() { Reset(); }

    void Reset()
    {
        armed_ = false;
        map_[0] = 0;
        until_ = 0;
        armedMs_ = 0;
        lastMs_ = 0;
        tries_ = 0;
        farSinceMs_ = 0;
        lastPz_ = 0.0f;
        haveLastPz_ = false;
        verdict_ = AR_NONE;
        why_[0] = 0;
        tx_ = ty_ = 0.0f;
        said_ = false;
    }

    // A hard transition just put this machine in `map`.
    void Arm(const char* map, uint32_t now)
    {
        armed_ = true;
        CopyText(map_, kMapCap, map ? map : "");
        until_ = now + kArrivalWindowMs;
        armedMs_ = now;
        tries_ = 0;
        farSinceMs_ = 0;
        lastPz_ = 0.0f;
        haveLastPz_ = false;
        verdict_ = AR_NONE;
        why_[0] = 0;
        tx_ = ty_ = 0.0f;
        said_ = false;
    }

    void Disarm() { armed_ = false; }

    // Called about once a second while a transition is settling.
    ArriveVerdict Decide(uint32_t now, const ArrivalInput& in)
    {
        verdict_ = AR_NONE;
        if (!armed_) return verdict_;
        if ((int32_t)(now - until_) >= 0) { verdict_ = AR_DONE; armed_ = false; return verdict_; }
        if (!in.havePawn) { haveLastPz_ = false; verdict_ = AR_WAIT; return verdict_; }
        // A position that did not read is not a spawn: wait for the next poll rather than
        // concluding the engine placed the player.
        if (!(in.px == in.px) || !(in.py == in.py)) { verdict_ = AR_WAIT; return verdict_; }

        // Falling is the game's own statement that the player has nowhere to stand.
        bool falling = haveLastPz_ && (in.pz < lastPz_ - kFallCmPerPoll);
        lastPz_ = in.pz;
        haveLastPz_ = true;

        // The engine placed the player somewhere real (a world WITH a PlayerStart, or the
        // game restoring a saved position): the game has already answered, and moving the
        // player here would be the mod pretending to be the spawn path.  It has to hold:
        // one poll is not evidence (kPlacedSettleMs), and a fall disqualifies it whatever
        // the coordinates say (kFallCmPerPoll).
        if (!AtOrigin(in.px, in.py) && !falling) {
            if (farSinceMs_ == 0) { farSinceMs_ = now ? now : 1; verdict_ = AR_WAIT; return verdict_; }
            if ((uint32_t)(now - farSinceMs_) < kPlacedSettleMs) { verdict_ = AR_WAIT; return verdict_; }
            verdict_ = AR_DONE;
            armed_ = false;
            return verdict_;
        }
        farSinceMs_ = 0;    // at the identity spawn, or falling out of the previous pose

        // A partner standing somewhere real in THIS world is the most precise anchor and
        // is the named fallback.  A partner that is itself at the identity spawn says
        // nothing about where to stand, so it is not used.
        //
        // The row must also be a SETTLED one: at least `kPeerSettleMs` after this machine's
        // own arrival.  The map and the position travel on different lanes, so a row can be
        // "fresh" by arrival time and still describe the world we just left -- measured
        // Session 12, where the follower placed itself at the partner's hub coordinates with
        // the new world's Z and stood in the void.
        if (in.havePeer && in.localMap && in.localMap[0] &&
            SameText(in.peerMap, in.localMap) && in.peerAgeMs <= kPeerFreshMs &&
            (int32_t)(in.peerRowMs - (armedMs_ + kPeerSettleMs)) >= 0 &&
            !AtOrigin(in.peerX, in.peerY)) {
            if (tries_ >= kArrivalMaxTries) { verdict_ = AR_DONE; armed_ = false; return verdict_; }
            verdict_ = AR_PLACE_PEER;
            CopyText(why_, kArrivalWhyCap, "partner");
            tx_ = in.peerX; ty_ = in.peerY;
            return verdict_;
        }

        if (in.haveAnchor) {
            if (tries_ >= kArrivalMaxTries) { verdict_ = AR_DONE; armed_ = false; return verdict_; }
            verdict_ = AR_PLACE_ANCHOR;
            CopyText(why_, kArrivalWhyCap, in.anchorWhy ? in.anchorWhy : "marker");
            tx_ = in.anchorX; ty_ = in.anchorY;
            return verdict_;
        }

        verdict_ = AR_WAIT;   // the partner may still arrive in this world
        return verdict_;
    }

    // The caller applied a placement.  `ok=false` also counts as an attempt: a marker the
    // game refuses must not be retried for the rest of the window.
    void Applied(bool ok, uint32_t now)
    {
        (void)ok;
        ++tries_;
        lastMs_ = now;
        said_ = false;
        haveLastPz_ = false;    // the placement moved the pawn: the fall check starts over
    }

    static bool AtOrigin(float x, float y)
    {
        if (!(x == x) || !(y == y)) return false;   // NaN: no measurement, not a spawn
        return (x > -kArrivalOriginRad && x < kArrivalOriginRad &&
                y > -kArrivalOriginRad && y < kArrivalOriginRad);
    }

    bool Armed() const { return armed_; }
    const char* Map() const { return map_; }
    const char* Why() const { return why_; }
    float TargetX() const { return tx_; }
    float TargetY() const { return ty_; }
    int Tries() const { return tries_; }
    // One line per decision, not one per pump.
    bool MarkSaid() { if (said_) return false; said_ = true; return true; }
    uint32_t Until() const { return until_; }
    uint32_t LastMs() const { return lastMs_; }

private:
    bool     armed_;
    char     map_[kMapCap];
    uint32_t until_;
    uint32_t armedMs_;
    uint32_t lastMs_;
    uint32_t farSinceMs_;   // when a non-origin pawn was first seen (0 = at the origin)
    float    lastPz_;
    bool     haveLastPz_;
    int      tries_;
    ArriveVerdict verdict_;
    char     why_[kArrivalWhyCap];
    float    tx_, ty_;
    bool     said_;
};

} // namespace travel
} // namespace qd