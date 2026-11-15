// qddialogue2.h -- the dialogue SPECTATOR lane (wire kind 0x50).
//
// THE FAILURE BEING DESIGNED AGAINST
// ----------------------------------
// qddialogue.h already publishes WHICH NPC each player is talking to, and blocks that
// NPC on the partner's machine.  What it does not do is show the partner the conversation
// itself: measured on the rig (Session 29, run 12), the user report was "a dialogue box on
// one instance but not on the other".  The talking player reads a line; the other player
// keeps walking around with no idea that a quest is being decided.
//
// WHAT IS ON THE WIRE, AND WHAT IS DELIBERATELY NOT
// ------------------------------------------------
// A conversation in this game is a CURSOR, not a stream (SYNC-GAPS D2.5):
//   (dialogue asset, sequence index, entry index)
// The lines themselves live in the cooked `FS_DialogueData` asset (61 of them), so the
// asset's OBJECT NAME is the identity and the text is not sent -- both machines loaded the
// same package and resolve the same strings.  What travels is the cursor plus the number of
// options the talker is being offered, so the spectator can render the same moment.
//
// THE ONE RULE THAT COSTS A QUEST IF IGNORED
// ------------------------------------------
// The spectator must NEVER run the dialogue's own logic.  A dialogue writes
// `NamedSaveData` (`NamedSaveDataValueChanges`) and runs `ActionToRun` /
// `ActionToRunAtEnd`; a second machine running StartDialogue would double a turn-in or a
// gift, which is exactly the failure the lock in qddialogue.h exists to prevent.  So this
// lane drives the DISPLAY only, and the outcomes arrive on the flag lane the talking
// machine already uses.
//
// WHY A "DECLINED" RECORD
// -----------------------
// Two cases must not be re-decided every 1 Hz heartbeat:
//   * the partner is talking and this player is in combat: hijacking the camera and
//     input of someone being attacked would get them killed.  The decision is recorded
//     against that publication so the same one is not re-tried;
//   * the lane hid the view (dialogue ended, partner vanished): acting again on the same
//     publication is the "two machines replay each other's cutscene" bug from Session 16
//     in this lane's shape.
// A genuine next line is a NEW seq, which is what the sender bumps on every change.
#pragma once
#include <stdint.h>

namespace qd {
namespace dlgview {

static const int      kSlots    = 2;
static const uint32_t kStaleMs  = 5000;    // senders republish at 1 Hz
static const uint8_t  kSlotFree = 0xFF;
static const uint16_t kNoOption = 0xFFFF;
static const int      kAssetCap = 96;
static const int      kMaxOptions = 32;

// `flags` bits on the wire.  A spectator renders the option list only when the talker
// says it is showing one, and marks the chosen entry only when the talker says which.
static const uint8_t F_OPTIONS   = 0x01;
static const uint8_t F_CHOSEN    = 0x02;
static const uint8_t F_HIDE_NAME = 0x04;

// One sender's published view.  `present` = this slot has ever published; `talking` is
// the only field a decision reads for liveness; the cursor fields are what the receiver
// re-creates on its own game's dialogue widget.
struct View {
    bool     present;
    uint32_t seq;
    bool     talking;
    uint8_t  slot;
    uint8_t  npc[16];          // the NPC's stable placed-object id (qddialogue.h StableId)
    char     asset[kAssetCap]; // the FS_DialogueData object's own name
    uint16_t sequence;         // the talker's CurrentDialogueSequenceIndex
    uint16_t entry;            // the entry index inside that sequence (0 when unreadable)
    uint16_t options;          // how many options the talker is offering now
    uint16_t chosen;           // kNoOption until the talker picks
    uint8_t  flags;
    uint32_t rx_ms;
};

// What the receiving machine should DO this frame.
enum Verdict {
    DV_NONE = 0,   // nothing to change
    DV_SHOW,       // put the partner's line on this machine's dialogue widget, take the camera
    DV_UPDATE,     // the cursor advanced: re-drive the widget
    DV_HIDE        // hide it and give the camera and the input back
};

class Board {
public:
    Board() { Reset(); }

    void Reset()
    {
        for (int i = 0; i < kSlots; ++i) Clear(s_[i]);
        Clear(shown_);
        showing_ = false;
        declined_ = false;
        declinedSlot_ = 0;
        declinedSeq_ = 0;
    }

    // ---- remote publish ---------------------------------------------------
    // Latest-wins by `seq`; a repeat of the same seq still refreshes liveness, which is
    // what the 1 Hz republish is for.  Returns true when the state changed.
    bool Accept(uint8_t slot, uint32_t seq, bool talking, const uint8_t npc[16],
                const char* asset, uint16_t sequence, uint16_t entry,
                uint16_t options, uint16_t chosen, uint8_t flags, uint32_t now_ms)
    {
        if (slot >= kSlots) return false;
        View& t = s_[slot];
        if (t.present && (int32_t)(seq - t.seq) <= 0) {
            if (seq == t.seq) t.rx_ms = now_ms;
            return false;
        }
        t.present = true;
        t.seq = seq;
        t.talking = talking;
        t.slot = slot;
        for (int i = 0; i < 16; ++i) t.npc[i] = npc ? npc[i] : 0;
        Copy(t.asset, kAssetCap, asset);
        t.sequence = sequence;
        t.entry = entry;
        t.options = options > kMaxOptions ? (uint16_t)kMaxOptions : options;
        t.chosen = chosen;
        t.flags = flags;
        t.rx_ms = now_ms;
        return true;
    }

    void Forget(uint8_t slot)
    {
        if (slot < kSlots) Clear(s_[slot]);
    }

    bool Live(uint8_t slot, uint32_t now_ms) const
    {
        if (slot >= kSlots) return false;
        const View& t = s_[slot];
        return t.present && t.talking && !Stale(t, now_ms);
    }

    // The partner's published view, or nullptr when it is not live.
    const View* Partner(uint8_t self, uint32_t now_ms) const
    {
        if (self >= kSlots) return nullptr;
        int other = (self == 0) ? 1 : 0;
        if (!Live((uint8_t)other, now_ms)) return nullptr;
        return &s_[other];
    }

    // ---- decision ---------------------------------------------------------
    // `localBusy` = this machine's own conversation / cutscene / map transition / death:
    // a player in one of those must not also be a spectator.  `inCombat` is the deliberate
    // refusal: a player being attacked keeps their camera and their legs, because a
    // cinematic that froze them there would be a way to die while reading.
    Verdict Decide(uint8_t self, bool localBusy, bool inCombat, uint32_t now_ms)
    {
        const View* p = Partner(self, now_ms);
        bool live = p && p->asset[0] != 0;

        if (!live) {
            if (showing_) { showing_ = false; return DV_HIDE; }
            return DV_NONE;
        }
        // This exact publication was already declined or hidden: do not act on it again.
        if (declined_ && declinedSlot_ == p->slot && declinedSeq_ == p->seq) {
            if (showing_) { showing_ = false; return DV_HIDE; }
            return DV_NONE;
        }
        if (showing_) {
            // The player we are spectating FOR became unsafe or unavailable.
            if (localBusy || inCombat) {
                showing_ = false;
                Declined(*p);
                return DV_HIDE;
            }
            if (!SameCursor(shown_, *p)) { shown_ = *p; return DV_UPDATE; }
            return DV_NONE;
        }
        if (localBusy || inCombat) { Declined(*p); return DV_NONE; }
        shown_ = *p;
        showing_ = true;
        return DV_SHOW;
    }

    // The game closed the widget itself (or the world changed underneath it).  The camera
    // and input are the caller's, and it still owes a restore; this only stops the board
    // from believing it is still showing.
    void NoteClosed() { showing_ = false; }

    // The caller could not ACT on the last DV_SHOW (the asset is not loaded here, or the
    // widget refused it).  Record that publication as declined so the 1 Hz republish does
    // not re-attempt it every tick; a genuine next line is a new seq and is tried again.
    void Decline(uint8_t self, uint32_t now_ms)
    {
        const View* p = Partner(self, now_ms);
        if (p) Declined(*p);
        showing_ = false;
    }

    bool     Showing() const { return showing_; }
    const View& Shown() const { return shown_; }

    // ---- queries used by the applier and its logs -------------------------
    static bool StaleView(const View& v, uint32_t now_ms) { return Stale(v, now_ms); }
    static void Copy(char* d, int cap, const char* s)
    {
        if (!d || cap <= 0) return;
        int i = 0;
        if (s) for (; s[i] && i < cap - 1; ++i) d[i] = s[i];
        d[i] = 0;
    }
    static bool SameId(const uint8_t* a, const uint8_t* b)
    {
        for (int i = 0; i < 16; ++i) if (a[i] != b[i]) return false;
        return true;
    }

private:
    static void Clear(View& v)
    {
        v.present = false;
        v.seq = 0;
        v.talking = false;
        v.slot = kSlotFree;
        v.asset[0] = 0;
        v.sequence = 0;
        v.entry = 0;
        v.options = 0;
        v.chosen = kNoOption;
        v.flags = 0;
        v.rx_ms = 0;
        for (int i = 0; i < 16; ++i) v.npc[i] = 0;
    }

    static bool Stale(const View& v, uint32_t now_ms)
    {
        uint32_t age = (now_ms > v.rx_ms) ? now_ms - v.rx_ms : 0;
        return age > kStaleMs;
    }

    // The CURSOR, not the seq: a sender that bumps its heartbeat sequence without the
    // conversation moving must not make the widget re-render.
    static bool SameCursor(const View& a, const View& b)
    {
        if (a.slot != b.slot) return false;
        if (!SameId(a.npc, b.npc)) return false;
        for (int i = 0; i < kAssetCap; ++i) if (a.asset[i] != b.asset[i]) return false;
        return a.sequence == b.sequence && a.entry == b.entry &&
               a.options == b.options && a.chosen == b.chosen && a.flags == b.flags;
    }

    void Declined(const View& v)
    {
        declined_ = true;
        declinedSlot_ = v.slot;
        declinedSeq_ = v.seq;
    }

    View   s_[kSlots];
    View   shown_;
    bool   showing_;
    bool   declined_;
    uint8_t  declinedSlot_;
    uint32_t declinedSeq_;
};

} // namespace dlgview
} // namespace qd
