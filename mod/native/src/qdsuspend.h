// qdsuspend.h -- holding the peer's timed suspend save, and repairing a load (SYNC-GAPS §4).
//
// WHAT THE GAME DOES
// ------------------
// `FS_GameInstance_C` runs a looping timer that writes a suspend save every ~120 s,
// alternating `SuspendSlot0` / `SuspendSlot1`, gated by `BlockSuspendSaves` (measured:
// 16-live-pass §2).  On death or on Continue the game can *load* that suspend save.
//
// WHY THAT BREAKS CO-OP
// ---------------------
// A suspend load replaces `FS_SaveSlot_C` with a snapshot the peer took at a different
// moment.  The peer's progression then rewinds while the host's does not, and every
// subsequent absolute body is a rollback.  The host's own timed save is harmless (the
// host is the authority and the peer applies its flags anyway).
//
// THE RULE
// --------
// While a session is live the peer holds `BlockSuspendSaves` (calling the game's own
// switch, not writing a byte), so its scheduled save cannot fire.  That gate is a game
// behaviour, not a lock: a death / Continue can still load a suspend slot.  When it
// does, the peer announces `LOADED` and the host immediately re-applies its absolute
// state (flags, actor states, inventory) rather than waiting for the 1 Hz cycle; the
// peer announces `REAPPLIED` when it has applied the resulting bodies.
//
// Pure: `test_suspend.cpp` drives the phases and the stale/ended cases.
#pragma once
#include <stdint.h>

namespace qd {
namespace suspend {

static const uint32_t kReapplyDeadlineMs = 15000;  // a reapply that never lands is an error
static const uint32_t kHoldHeartbeatMs   = 1000;

enum Phase {
    PH_IDLE = 0,       // no session / not holding
    PH_HOLD,           // this peer has called BlockSuspendSaves
    PH_LOADED,         // a suspend load happened; the host must reapply
    PH_WAITING,        // the host applied; waiting for the peer's REAPPLIED
    PH_REAPPLIED,      // the peer confirmed
    PH_GAVE_UP         // the deadline expired; reported, never silent
};

class Board {
public:
    Board() { Reset(); }

    void Reset()
    {
        phase_ = PH_IDLE;
        seq_ = 0;
        slot_[0] = 0;
        since_ = 0;
        reapplyDue_ = false;
        giveUpReported_ = false;
    }

    // ---- local decisions ---------------------------------------------------
    // The peer should hold while a session is live.  Returns true when the hold state
    // changed (the caller must call the game's own BlockSuspendSaves).
    bool WantHold(bool sessionLive, bool isPeer)
    {
        bool want = sessionLive && isPeer;
        if (want && (phase_ == PH_IDLE || phase_ == PH_GAVE_UP)) {
            phase_ = PH_HOLD;
            ++seq_;
            return true;
        }
        if (!want && phase_ != PH_IDLE) {
            phase_ = PH_IDLE;
            ++seq_;
            reapplyDue_ = false;
            return true;
        }
        return false;
    }

    bool Holding() const { return phase_ == PH_HOLD || phase_ == PH_LOADED || phase_ == PH_WAITING; }

    // The local save layer re-initialised onto a suspend slot.  Only the peer reports
    // it (the host's suspend load is the authority and is not repaired).
    bool OnLocalSuspendLoaded(const char* slot, uint32_t now)
    {
        if (phase_ == PH_LOADED || phase_ == PH_WAITING) return false;   // one report
        Copy(slot_, sizeof(slot_), slot);
        phase_ = PH_LOADED;
        since_ = now;
        giveUpReported_ = false;
        ++seq_;
        return true;
    }

    // ---- inbound ----------------------------------------------------------
    // The peer says it loaded a suspend save.  On the host this arms a reapply.
    bool OnPeerLoaded(uint32_t seq, const char* slot, uint32_t now)
    {
        if (phase_ == PH_LOADED || phase_ == PH_WAITING) return false;
        Copy(slot_, sizeof(slot_), slot);
        phase_ = PH_LOADED;
        since_ = now;
        reapplyDue_ = true;
        giveUpReported_ = false;
        (void)seq;
        ++seq_;
        return true;
    }

    // The peer says it applied the re-applied state.  Accept from LOADED as well as
    // WAITING: the host answers a LOADED directly with REAPPLIED.
    bool OnPeerReapplied(uint32_t seq)
    {
        if (phase_ != PH_WAITING && phase_ != PH_LOADED) return false;
        phase_ = PH_REAPPLIED;
        reapplyDue_ = false;
        (void)seq;
        ++seq_;
        return true;
    }

    // The host has pushed the absolute bodies.
    bool TakeReapply()
    {
        if (!reapplyDue_) return false;
        reapplyDue_ = false;
        phase_ = PH_REAPPLIED;
        ++seq_;
        return true;
    }

    // ---- time --------------------------------------------------------------
    // Returns true when the reapply deadline expired (reported once).
    bool Tick(uint32_t now)
    {
        if (phase_ != PH_LOADED && phase_ != PH_WAITING) return false;
        if (giveUpReported_) return false;
        if ((uint32_t)(now - since_) < kReapplyDeadlineMs) return false;
        giveUpReported_ = true;
        phase_ = PH_GAVE_UP;
        reapplyDue_ = false;
        ++seq_;
        return true;
    }

    // ---- wire --------------------------------------------------------------
    uint8_t StateToSend() const
    {
        switch (phase_) {
        case PH_HOLD:      return 1;   // HOLD
        case PH_LOADED:    return 2;   // LOADED
        case PH_REAPPLIED: return 3;   // REAPPLIED
        default:           return 0;   // CLEAR
        }
    }
    uint32_t Seq() const { return seq_; }
    const char* Slot() const { return slot_; }
    Phase Current() const { return phase_; }
    bool ReapplyDue() const { return reapplyDue_; }

private:
    static void Copy(char* d, int cap, const char* s)
    {
        if (!d || cap <= 0) return;
        int i = 0;
        if (s) for (; s[i] && i < cap - 1; ++i) d[i] = s[i];
        d[i] = 0;
    }

    Phase    phase_;
    uint32_t seq_;
    char     slot_[64];
    uint32_t since_;
    bool     reapplyDue_;
    bool     giveUpReported_;
};

} // namespace suspend
} // namespace qd
