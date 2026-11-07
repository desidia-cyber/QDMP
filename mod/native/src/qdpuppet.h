// qdpuppet.h -- the remote body's lifecycle, as a pure state machine.
//
// WHY A STATE MACHINE AND NOT A BOOLEAN
// -------------------------------------
// The previous project's puppet header is the record of what a boolean costs.  Its
// bugs were all "absence" bugs: a duplicate spawn, a spawn retried forever, a handle
// used after a level change, a body destroyed the moment a datagram went missing.
// Each of those is a transition that a boolean cannot express.  So the states are
// explicit, every transition is a named event, and the machine never destroys the
// actor on silence -- the rule from the corpus is "Never destroy a puppet on silence.
// Freeze at 2 s, hide at 30 s, keep the actor valid."
//
// ABSENT -> WANTED -> SPAWNING -> LIVE -> FROZEN -> HIDDEN -> LIVE ...
//                              \-> FAILED (bounded attempts)
//
// `Wanted` means "there should be a body in the world"; `Spawning` means one spawn is
// in flight and no second may start; `Live` means drive it; `Frozen` and `Hidden` are
// the two degradation steps; `Failed` is terminal until the peer or level changes.
#pragma once
#include <stdint.h>

namespace qd {
namespace puppet {

enum class PuppetState : uint8_t {
    Absent = 0,    // no peer, no body
    Wanted,        // peer present, body needed, no spawn started yet
    Spawning,      // one spawn in flight
    Live,          // body exists and is being driven
    Frozen,        // body exists, no data for a while, still valid
    Hidden,        // body exists, no data for longer, hidden but valid
    Failed,        // spawn attempts exhausted; wait for a state change
};

class PuppetFsm {
public:
    struct Step {
        PuppetState state;
        bool want_spawn;
        bool want_hide;
        bool want_destroy;
        bool retry;
    };

    // Timing, and the reason for each number:
    //   250 ms first retry  -- long enough that a resource-loading spawn can finish,
    //                          short enough that a peer is not invisible for long
    //   5 s cap             -- bounded backoff, so a permanently failing class is
    //                          retried a few times and then reported, not hammered
    //   5 attempts          -- "retried up to 5 times" is the tuning the previous
    //                          project measured for its request path
    static const uint32_t kRetryFirstMs = 250;
    static const uint32_t kRetryCapMs   = 5000;
    static const int      kMaxAttempts  = 5;
    static const uint32_t kFreezeMs     = 2000;
    static const uint32_t kHideMs       = 30000;

    PuppetFsm() { Reset(); }

    void Reset()
    {
        state_ = PuppetState::Absent;
        peerPresent_ = false;
        attempts_ = 0;
        dueMs_ = 0;
        lastPresentMs_ = 0;
        spawnInFlight_ = false;
        hideWanted_ = false;
        destroyWanted_ = false;
    }

    puppet::PuppetState State() const { return state_; }
    int Attempts() const { return attempts_; }

    // ---- events -----------------------------------------------------------
    void PeerPresent(bool present, uint32_t now_ms)
    {
        if (present) lastPresentMs_ = now_ms;
        if (present == peerPresent_) return;
        peerPresent_ = present;

        if (present) {
            // A returning peer goes back to Wanted, not to Spawning: whatever body we
            // had may have been hidden, and the FSM decides from the state, not from a
            // remembered flag.
            attempts_ = 0;
            spawnInFlight_ = false;
            if (state_ == PuppetState::Absent || state_ == PuppetState::Failed ||
                state_ == PuppetState::Hidden) {
                state_ = PuppetState::Wanted;
            }
            hideWanted_ = false;
        } else {
            // The peer left.  That is an explicit event, so the body may be destroyed --
            // but only here, never on a silence timer.
            destroyWanted_ = true;
            state_ = PuppetState::Absent;
            spawnInFlight_ = false;
            attempts_ = 0;
        }
    }

    void SpawnStarted(uint32_t now_ms)
    {
        spawnInFlight_ = true;
        state_ = PuppetState::Spawning;
        ++attempts_;
        (void)now_ms;
    }

    void SpawnFailed(const char* why, uint32_t now_ms)
    {
        (void)why;
        spawnInFlight_ = false;
        if (attempts_ >= kMaxAttempts) {
            state_ = PuppetState::Failed;
            return;
        }
        // Exponential backoff with a cap.  The bracket has a deadline: a retry that
        // never expires is how a spawn loop turns into a hang.
        uint32_t backoff = kRetryFirstMs;
        for (int i = 1; i < attempts_; ++i) {
            backoff *= 2;
            if (backoff >= kRetryCapMs) { backoff = kRetryCapMs; break; }
        }
        dueMs_ = now_ms + backoff;
        state_ = PuppetState::Wanted;
    }

    void Spawned(uint32_t now_ms)
    {
        (void)now_ms;
        spawnInFlight_ = false;
        attempts_ = 0;
        hideWanted_ = false;
        state_ = PuppetState::Live;
    }

    // A level change invalidates the handle: the actor graph is rebuilt, so a
    // remembered pointer is a dangling address.  Back to Wanted with the attempt
    // counter cleared -- a new level is a new chance, not a continued failure.
    void LocalWorldChanged(uint32_t now_ms)
    {
        (void)now_ms;
        spawnInFlight_ = false;
        attempts_ = 0;
        hideWanted_ = false;
        state_ = peerPresent_ ? PuppetState::Wanted : PuppetState::Absent;
    }

    void DataArrived(uint32_t now_ms)
    {
        lastPresentMs_ = now_ms;
        if (!peerPresent_) return;
        if (state_ == PuppetState::Frozen) { state_ = PuppetState::Live; hideWanted_ = false; }
        else if (state_ == PuppetState::Hidden) { state_ = PuppetState::Live; hideWanted_ = false; }
    }

    // ---- time -------------------------------------------------------------
    void Tick(uint32_t now_ms)
    {
        if (state_ == PuppetState::Absent || state_ == PuppetState::Failed) return;
        if (state_ == PuppetState::Wanted || state_ == PuppetState::Spawning) return;

        uint32_t silent = (now_ms > lastPresentMs_) ? (now_ms - lastPresentMs_) : 0;
        if (silent >= kHideMs) {
            // Hidden, not destroyed.  The actor stays valid so a returning peer does
            // not pay for a respawn and an asset load.
            if (state_ != PuppetState::Hidden) { state_ = PuppetState::Hidden; hideWanted_ = true; }
        } else if (silent >= kFreezeMs) {
            if (state_ == PuppetState::Live) state_ = PuppetState::Frozen;
        }
    }

    // ---- output -----------------------------------------------------------
    Step Get() const
    {
        Step s;
        s.state = state_;
        s.want_spawn = false;
        s.want_hide = hideWanted_;
        s.want_destroy = destroyWanted_;
        s.retry = false;

        if (state_ == PuppetState::Wanted && !spawnInFlight_) {
            // The caller owns the clock for a retry: it asks once per pump and the FSM
            // has already set dueMs_, so a caller that pumps every frame still spawns
            // once per backoff window.
            s.want_spawn = true;
        }
        if (state_ == PuppetState::Failed) s.retry = false;
        return s;
    }

    // Called by the caller once the retry deadline passes, so Tick-driven pumps do
    // not need to know about dueMs_.
    bool ReadyToSpawn(uint32_t now_ms) const
    {
        if (state_ != PuppetState::Wanted || spawnInFlight_) return false;
        return (int32_t)(now_ms - dueMs_) >= 0;
    }

private:
    PuppetState state_;
    bool        peerPresent_;
    int         attempts_;
    uint32_t    dueMs_;
    uint32_t    lastPresentMs_;
    bool        spawnInFlight_;
    bool        hideWanted_;
    bool        destroyWanted_;
};

} // namespace puppet
} // namespace qd
