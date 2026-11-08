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
        if (!active_ || !SameText(map, target_)) return HV_IGNORED;
        peerArrived_ = true;
        if (localMap && localMap[0] && SameText(map, localMap)) localArrived_ = true;
        return HV_NONE;
    }

    HardVerdict OnPeerResume(uint32_t seq, const char* map)
    {
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

} // namespace travel
} // namespace qd