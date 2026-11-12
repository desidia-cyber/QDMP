// qdpending.h -- the peer's outbound progression intents.
//
// THE THREE FAILURES THIS IS SHAPED AROUND
// ----------------------------------------
//   * ECHO.  `PeerApplyFlags` writes the host's value into the peer's own save, so the
//     read-only mirror sees it a second later exactly as if the peer had produced it.
//     Turning that back into an intent makes the two machines ping-pong for the rest of
//     the session.  The mirror cannot tell a local write from an applied one, so every
//     value the host's body put into the save is remembered per key, and an observed
//     value equal to it is dropped.  (The write is the same proven path the host's own
//     `intent applied` line uses -- LIVE-TEST-LOG R21.)
//
//   * RETRY.  The host's acceptance gate is `based_on_rev >= the revision it last
//     published`.  A peer intent can cross a broadcast already in flight and arrive one
//     revision stale, and the host then refuses it.  A refused intent must not be
//     swallowed: SYNC-DESIGN 2.2 names "intent dropped without retry" as an acceptance
//     failure, because the progression is then lost for good.  So an unconfirmed intent
//     stays queued and is re-sent at 1 Hz with the freshest revision, until the host's
//     own body echoes it or a bounded attempt count says the host is not going to.
//
//   * BURST.  A quest node sets a state and two counters in the same frame, and the
//     mirror hands over all of them in one tick.  One datagram per key per tick is a
//     burst on the reliable lane, so a key is coalesced to its latest value and the
//     send rate is bounded both per tick and per second.
//
// Everything here is pure: no socket, no log, no game, no allocation.  The caller
// supplies the clock and performs the send, so the whole state machine is exercised
// off-target by test/test_pending.cpp.
#pragma once
#include <stdint.h>

namespace qd {
namespace pending {

inline bool SameName(const char* a, const char* b)
{
    if (!a || !b) return false;
    for (int i = 0; a[i] || b[i]; ++i) if (a[i] != b[i]) return false;
    return true;
}

inline void CopyName(char* dst, int cap, const char* src)
{
    int i = 0;
    if (src) for (; src[i] && i < cap - 1; ++i) dst[i] = src[i];
    dst[i] = 0;
}

class IntentQueue {
public:
    // 80 is the widest name buffer in the chain (qdsave.h's enumeration hands over a
    // char[80]; FlagBank stores 64), so a key is never truncated on its way here.
    static const int kMaxName = 80;

    // 64 simultaneous unacknowledged keys: a dialogue or a chest burst writes several keys
    // in one frame, and a host mid-load answers nothing for seconds.  Still bounded, so a
    // host that stops answering cannot grow the module's memory.  A full queue is REPORTED
    // (kOverflow) and the caller re-observes the key on its next pass -- it is deferred,
    // never lost.
    static const int kMaxPending = 64;

    // One echo record per key the host can push.  HostPushFlags' table is bounded by
    // flags::kMaxFlags (1024), so a table of the same size cannot forget a key that is
    // still live and let its echo come back as an intent.  Kept as a literal so this
    // header stays dependency-free; test_pending asserts the two agree.
    static const int kMaxEcho = 1024;

    // Five attempts at 1 s: a live round trip on the relay is tens of milliseconds, so
    // five consecutive refusals mean the host is not applying this key at all (full
    // bank, wrong save, or a gate this peer cannot satisfy) and more trying only spams.
    static const int kMaxAttempts = 5;

    // The host publishes at 1 Hz, so one retry per published revision is enough; any
    // faster cannot learn anything new.
    static const uint32_t kRetryMs = 1000;

    // Four per second sustained, with a burst of four.  Progression is not latency
    // sensitive (the mirror already only samples at 1 Hz), and this keeps a session's
    // intent lane far below the state lanes' budget.
    static const uint32_t kRefillMs = 250;
    static const int      kBucket   = 4;

    // And no more than two datagrams in any one frame: a burst is spread over frames
    // rather than emitted as a clump.
    static const int kMaxPerTick = 2;

    struct Out {
        char    name[kMaxName];
        int32_t value;
        int     attempt;    // 1-based count of sends, for the caller's log lines
    };

    IntentQueue() { Reset(); }

    void Reset()
    {
        for (int i = 0; i < kMaxPending; ++i) {
            p_[i].used = false;
            p_[i].name[0] = 0;
            p_[i].value = 0;
            p_[i].attempts = 0;
            p_[i].lastSendMs = 0;
        }
        for (int i = 0; i < kMaxEcho; ++i) {
            e_[i].used = false;
            e_[i].name[0] = 0;
            e_[i].value = 0;
        }
        echoNext_ = 0;
        tokens_ = kBucket;
        refillMs_ = 0;
        refillInit_ = false;
        overflowPending_ = false;
        overflowEcho_ = false;
    }

    // ---- what the host told us ---------------------------------------------
    // The value a host body put into the peer's save.  Remembered rather than inferred:
    // the mirror sees the write, not the writer.
    void RememberEcho(const char* name, int32_t value)
    {
        for (int i = 0; i < kMaxEcho; ++i) {
            if (e_[i].used && SameName(e_[i].name, name)) {
                e_[i].value = value;      // host values never lower, so this only raises
                return;
            }
        }
        for (int i = 0; i < kMaxEcho; ++i) {
            if (!e_[i].used) {
                e_[i].used = true;
                CopyName(e_[i].name, kMaxName, name);
                e_[i].value = value;
                return;
            }
        }
        // Full.  Cannot happen while the table matches the host's container cap, but the
        // newest value must still win if it does: dropping it is the ping-pong.
        Echo& e = e_[echoNext_++ % kMaxEcho];
        e.used = true;
        CopyName(e.name, kMaxName, name);
        e.value = value;
        overflowEcho_ = true;
    }

    bool EchoMatches(const char* name, int32_t value) const
    {
        for (int i = 0; i < kMaxEcho; ++i) {
            if (e_[i].used && e_[i].value == value && SameName(e_[i].name, name)) return true;
        }
        return false;
    }

    // ---- what the local game did -------------------------------------------
    enum Observed {
        kQueued,      // a new intent
        kCoalesced,   // an unconfirmed key rose again: latest value wins
        kDuplicate,   // the same value is already waiting
        kEcho,        // the value the host applied: not a local change
        kOverflow,    // the pending list is full; the caller must log the drop
    };

    // The caller has already established that `value` is a rise (the mirror keeps the
    // previous value).  Echo suppression happens here because only this table knows
    // which rises were the host's.
    Observed Observe(const char* name, int32_t value)
    {
        if (EchoMatches(name, value)) return kEcho;

        int i = FindPending(name);
        if (i >= 0) {
            // Never lower a queued absolute intent: a rise is only a rise.  (The mirror
            // compares against its previous sample, which can sit below the value already
            // queued if a host write landed in between.)
            if (p_[i].value >= value) return kDuplicate;
            // A second, higher value arrived before the first was confirmed: send the
            // latest, on a fresh attempt budget, rather than the one already overtaken.
            p_[i].value = value;
            p_[i].attempts = 0;
            p_[i].lastSendMs = 0;
            return kCoalesced;
        }

        i = FreePending();
        if (i < 0) { overflowPending_ = true; return kOverflow; }
        Pending& p = p_[i];
        p.used = true;
        CopyName(p.name, kMaxName, name);
        p.value = value;
        p.attempts = 0;
        p.lastSendMs = 0;
        return kQueued;
    }

    // ---- what the host now holds -------------------------------------------
    enum Resolve {
        kNoPending,
        kConfirmed,    // the host holds this value: the intent is settled
        kSuperseded,   // the host is ahead: the authority already implies our value
    };

    // Called for every entry of every host body.  A value below a pending intent does
    // not settle it (the host is behind, and the retry must still happen); an equal or
    // higher value does, because progression is monotone.
    Resolve Applied(const char* name, int32_t value, int32_t* asked_out)
    {
        int i = FindPending(name);
        if (i < 0) return kNoPending;
        if (value < p_[i].value) return kNoPending;
        Resolve r = (value == p_[i].value) ? kConfirmed : kSuperseded;
        if (asked_out) *asked_out = p_[i].value;
        p_[i].used = false;
        return r;
    }

    // ---- what may go on the wire now ---------------------------------------
    // Fills up to `cap` (never more than kMaxPerTick) intents that are due, spending the
    // rate-limit bucket as it goes.  Each returned entry has already had its attempt
    // counted and its retry clock started, so a caller that fails to send still cannot
    // emit it twice in one tick.
    //
    // Order: work that has never been sent goes before a retry.  A local change made
    // while the host is behind on an older key must not wait behind that key's retries.
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
            CopyName(out[n].name, kMaxName, p.name);
            out[n].value = p.value;
            out[n].attempt = p.attempts;
            ++n;
            --tokens_;
        }
        return n;
    }

    // Drop an intent that has spent its attempts.  Separate from Due so the caller can
    // log the give-up with the name and value it just failed to have confirmed.
    bool GiveUp(const char* name)
    {
        int i = FindPending(name);
        if (i < 0) return false;
        p_[i].used = false;
        return true;
    }

    int  Count() const { int n = 0; for (int i = 0; i < kMaxPending; ++i) if (p_[i].used) ++n; return n; }
    bool Empty() const { return Count() == 0; }
    bool PendingOverflowed() const { return overflowPending_; }
    bool EchoOverflowed() const { return overflowEcho_; }

private:
    struct Pending {
        bool     used;
        char     name[kMaxName];
        int32_t  value;
        int      attempts;
        uint32_t lastSendMs;
    };

    struct Echo {
        bool     used;
        char     name[kMaxName];
        int32_t  value;
    };

    int FindPending(const char* name) const
    {
        for (int i = 0; i < kMaxPending; ++i) {
            if (p_[i].used && SameName(p_[i].name, name)) return i;
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

} // namespace pending
} // namespace qd
