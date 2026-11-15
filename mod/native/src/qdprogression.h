// qdprogression.h -- the three progression gaps the co-op mode still had:
//
//   1. QUEST/KEY ITEMS ONLY cross between the players.  Normal loot, consumables,
//      equipment and money stay per-player.
//   2. A KILL PAYS BOTH PLAYERS.  Whichever machine ran the death, each player gets that
//      kill's XP once.
//   3. A SAVE IS MIRRORED.  When either player saves, the other saves its own local slot
//      immediately, through the game's own save function.
//
// WHY THIS IS ONE HEADER AND THREE MACHINES
// -----------------------------------------
// The three share exactly one thing: they are all per-player PROGRESSION, so they all sit
// on the boundary between "shared world fact" (the flag bank, the actor states, the shared
// key) and "this player's own game" (their loot, their XP, their save slot).  Everything
// below the boundary -- the wire, the socket, the UObject calls -- is the caller's; what
// lives here is the part that must be right before a game is involved and that a live run
// cannot debug: WHICH item is a quest item, WHEN an award is a duplicate, and WHICH save
// dispatch is an echo.
//
// WHY THE ITEM TEST IS A PATH TEST AND NOT A TABLE
// ------------------------------------------------
// `analysis/23-quest-progression-sync-map.md` §4 measured the whole quest-item universe
// from the cooked pak: **503 key-item assets in 26 families, all under
// `Content/Items/KeyItems/`**.  `analysis/pak/answers-2.md` C7.3 decodes the item
// definition's own `ItemCategory` enum as
// `{Consumables, Accessories, Weapons, Daggers, GemsAndBatteries, Keys, Food, Treasure,
// All}`, so `Keys` is ordinal 5.  Both facts are used, in that order:
//
//   * the soft object path (what `InventoryContents` stores and what the drop lane
//     publishes) is tested first, because it needs no UObject and therefore no fault
//     guard on a path that runs on the game thread for every container entry;
//   * the item definition's `Category` byte is the fallback, so a key asset that a future
//     build places outside `Items/KeyItems/` is still recognised.  A `Category` read that
//     does not resolve is NOT an error and NOT a "yes": an item whose kind cannot be
//     established stays local, which is the safe direction (a key the partner lacks locks
//     that player out; an ordinary item the partner wrongly receives is a dupe).
//
// WHAT STAYS LOCAL, AND WHY THAT IS THE TASK AND NOT A LIMITATION
// ---------------------------------------------------------------
// Measured and recorded in `SYNC-DESIGN` §2.4 / `SYNC-GAPS` §4: loot, consumables,
// equipment, money, the shop, the journal, weapon levels and grimoire ink are per-player by
// nature.  The pre-existing `INVENTORY` lane mirrored the WHOLE container and the `LOOT`
// lane shared ONE drop set with tombstones, so both crossed far more than the two players
// should share.  Both are narrowed HERE, at one predicate, rather than by writing a second
// lane: the state machines in `qdinventory.h` / `qdinvintent.h` / `qdloot.h` are proven and
// their wire format is unchanged, so only the set of paths they ever see changes.
//
// Pure: `test/test_progression.cpp` drives every machine below with no game.
#pragma once
#include <stdint.h>
#include <string.h>

namespace qd {
namespace progression {

// ---------------------------------------------------------------------------
// Small text helpers (no CRT allocation: this runs on the game thread)
// ---------------------------------------------------------------------------
inline bool SameText(const char* a, const char* b)
{
    if (!a || !b) return a == b;
    for (int i = 0; a[i] || b[i]; ++i) if (a[i] != b[i]) return false;
    return true;
}

inline int CopyText(char* dst, int cap, const char* src)
{
    if (!dst || cap <= 0) return 0;
    int i = 0;
    if (src) for (; src[i] && i < cap - 1; ++i) dst[i] = src[i];
    dst[i] = 0;
    return i;
}

// ASCII case-insensitive "does `hay` contain `needle`".  UE object paths preserve case but
// different tools spell the folder differently (`Items/KeyItems` from the pak, `/Game/
// Items/KeyItems` from the save), so a case difference must not change the answer.
inline bool ContainsCI(const char* hay, const char* needle)
{
    if (!hay || !needle || !needle[0]) return false;
    int nl = 0;
    while (needle[nl]) ++nl;
    for (int i = 0; hay[i]; ++i) {
        int k = 0;
        for (; k < nl && hay[i + k]; ++k) {
            char a = hay[i + k], b = needle[k];
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b) break;
        }
        if (k == nl) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// 1. Which inventory entries cross the wire
// ---------------------------------------------------------------------------
// The folder, not a list of 26 family names: the analysis measured 26 families and 503
// assets in it, and a list would go stale the first time content is added.
static const char* const kQuestItemFolder = "Items/KeyItems/";

// `ItemCategory::Keys` from `analysis/pak/answers-2.md` C7.3.
static const uint8_t kItemCategoryKeys = 5;

// The path form, used for everything that has no item object in hand.
inline bool IsQuestItemPath(const char* path)
{
    return ContainsCI(path, kQuestItemFolder);
}

// The form a container walk uses: the path first (free), the item definition's own
// category second (one reflective byte read), the answer "local" when neither says yes.
// `catBytes` is the caller's resolution of `Category`; pass `haveCat == false` when the
// item object is not resolvable, and the path answer stands alone.
inline bool IsQuestItem(const char* path, bool haveCat, uint8_t cat)
{
    if (IsQuestItemPath(path)) return true;
    if (!haveCat) return false;
    return cat == kItemCategoryKeys;
}

// ---------------------------------------------------------------------------
// 2. The peer's key-item count must FOLLOW the host's, in both directions
// ---------------------------------------------------------------------------
// The existing lane is additive on purpose (`qdinventory.h`: a key the peer lacks locks
// that player out, and a grant of a key it already has is harmless), so a HOST consumption
// is the one state it could not express: the host publishes the lower count, the peer's
// grant target falls below its own count, `need <= 0`, and the item simply stays.  The
// requirement is "when either player gains or loses a quest/key item, the partner gets the
// same change", so the fall has to be applied too.
//
// It is applied with the game's own consumer (`SaveFunctions_C.
// TryConsumingItemFromInventory`, already proven by WrConsumeItem) and it is deliberately
// CONSERVATIVE: a removal is only considered when the peer never contributed this path, so
// a count difference that the peer's own pickup explains is left alone.  The price is that
// a key both players picked up independently is not lowered by the host's use -- the
// direction that is harmless, since a key kept is not a player locked out.
inline int32_t RemovalNeeded(int32_t local, int32_t host,
                             int32_t peerOwn, int32_t consumeOutstanding)
{
    if (local <= 0) return 0;
    if (peerOwn > 0) return 0;                 // the difference may be the peer's own gain
    if (consumeOutstanding > 0) return 0;      // a use of the peer's own is still in flight
    int32_t want = host;
    if (want < 0) want = 0;
    return local > want ? local - want : 0;
}

// The host's published set, as the peer sees it.  Bounded like the publisher's own table
// (`qdinventory.h` kMaxEntries = 512, provisioned for the measured universe) so no entry
// the host can send is ever dropped here.
static const int kMaxPaths = 512;
static const int kMaxPath  = 200;

struct HostCount {
    bool    used;
    char    path[kMaxPath];
    int32_t count;
};

class HostView {
public:
    HostView() { Reset(); }

    void Reset()
    {
        n_ = 0;
        for (int i = 0; i < kMaxPaths; ++i) {
            e_[i].used = false;
            e_[i].path[0] = 0;
            e_[i].count = 0;
        }
        overflow_ = false;
    }

    // One entry of the host's absolute body.  Returns false only when the table is full or
    // the path does not fit; both are reported by the caller, never silent.
    bool Set(const char* path, int32_t count)
    {
        if (!path || !path[0]) return false;
        int i = Find(path);
        if (i >= 0) { e_[i].count = count; return true; }
        if (n_ >= kMaxPaths) { overflow_ = true; return false; }
        if (CopyText(e_[n_].path, kMaxPath, path) <= 0) { overflow_ = true; return false; }
        e_[n_].used = true;
        e_[n_].count = count;
        ++n_;
        return true;
    }

    const HostCount* At(int i) const
    {
        if (i < 0 || i >= n_) return nullptr;
        return e_[i].path[0] ? &e_[i] : nullptr;
    }
    int  Count() const { return n_; }
    int32_t Get(const char* path) const
    {
        int i = Find(path);
        return i >= 0 ? e_[i].count : 0;
    }
    bool Overflowed() const { return overflow_; }

private:
    int Find(const char* path) const
    {
        for (int i = 0; i < n_; ++i) {
            if (e_[i].used && SameText(e_[i].path, path)) return i;
        }
        return -1;
    }

    HostCount e_[kMaxPaths];
    int       n_;
    bool      overflow_;
};

// ---------------------------------------------------------------------------
// 3. One kill pays both players, exactly once each
// ---------------------------------------------------------------------------
// WHAT IS MEASURED, AND WHY NOTHING IS ASSUMED
// --------------------------------------------
// `analysis/pak/blueprint-surface.md` names the only XP surface the build exposes: the
// save's own `XPAmount` / `SavedPlayerLevel` fields, and on `FS_PlayerChar_C` the
// `Player_GiveXP` / `AddXP` UFUNCTIONs plus the `XPGained` delegate -- and NONE of those
// names is a native symbol (`exe_names.txt` has no `Player_GiveXP`), so the award is
// Blueprint state and the amount lives only in the running player.  Which is exactly the
// shape the task describes: "the partner's copy may or may not award".
//
// So the amount is never derived from the enemy data: each machine watches its OWN player's
// XP counter across the death, and the DAILY (delta) it observes is what it reports.  That
// makes the two outcomes self-selecting:
//
//   * the game paid both machines -> both observe a positive delta -> neither pays, and the
//     two reports cross and are ignored.  Exactly once each, with no suppression flag.
//   * the game paid only the machine that ran the death -> that machine reports a positive
//     delta, the other observes zero and pays the reported amount.  Exactly once each.
//   * the game paid neither (a mirrored copy killed by the mod's HP write) -> neither
//     reports, nothing crosses, and the log says so.  No invented award.
//
// THE ORDER PROBLEM, AND HOW IT IS SOLVED
// ---------------------------------------
// The two events that must be matched arrive in an arbitrary order: the local death (a
// `StoreEnemySaveState` with hp 0) and the partner's report (a datagram).  A record is
// therefore created by EITHER one, and the decision is taken once, after the measurement
// window, so a report that arrives first waits for the local measurement rather than
// paying straight away.  A report for a death this machine never observed (the enemy has no
// copy here) is paid after a bounded grace period -- reported in the log, because that is
// the one case where the award is not backed by a local measurement.
//
// THE SEQUENCE NUMBER
// -------------------
// An enemy's key is stable across its respawn (the spawner's authored GUID), so the key
// alone cannot separate two deaths of the same enemy.  Each record carries a per-key
// ordinal that bumps when a death arrives for a key whose record is already settled and
// older than the same-death window.  A receiver ignores an ordinal it has already paid, so
// a reliable-lane retransmit is a no-op and a respawned kill is a new award.
static const int      kMaxDeathXp     = 32;
static const uint32_t kSameDeathMs    = 8000;   // the window DeathMarkAdd uses
static const uint32_t kSettleMs       = 400;    // the Blueprint award lands within a frame
static const uint32_t kRemoteGraceMs  = 3000;   // no local death: pay after this
static const uint32_t kRecordLifeMs   = 180000; // recycle an old record
static const int32_t  kMaxXpPerKill   = 1000000;

enum XpAction {
    XP_NONE = 0,
    XP_SEND_AWARD = 1,   // tell the partner what this machine's game just paid
    XP_PAY_LOCAL  = 2,   // pay this machine's own player
};

struct XpOut {
    uint8_t  action;
    uint32_t guid[4];
    uint16_t seq;
    int32_t  xp;
};

class KillXp {
public:
    KillXp() { Reset(); }

    void Reset()
    {
        for (int i = 0; i < kMaxDeathXp; ++i) Clear(r_[i]);
        n_ = 0;
        overflow_ = false;
    }

    // A death was observed on this machine.  `xpBefore` is the local player's XP counter
    // read at that instant (ignored when the counter is unreadable).  Returns false when
    // this is the SAME death already recorded (the game writes its persistence record
    // several times per death -- measured, five times in 2.5 s), which is the dedupe the
    // drop lane already relies on.
    bool LocalDeath(const uint32_t guid[4], int32_t xpBefore, uint32_t now)
    {
        Rec* r = Find(guid);
        if (r) {
            // THE ORDER REVERSAL: the partner's report created this record before the local
            // death arrived.  Attach the measurement to it instead of starting a second one,
            // so the report still waits for the local answer rather than paying blind.
            if (r->remoteOnly && !r->settled) {
                r->remoteOnly = false;
                r->before = xpBefore;
                r->paid = 0;
                r->settled = false;
                r->awarded = false;
                r->at = now;
                return true;
            }
            if ((uint32_t)(now - r->at) < kSameDeathMs && !r->settled) {
                r->at = now;                       // the same death, still measuring
                return false;
            }
            if ((uint32_t)(now - r->at) < kSameDeathMs) return false;   // settled, same death
            // A later death of the same key: new ordinal, fresh accounting.
            uint16_t next = (uint16_t)(r->seq + 1);
            bool keepPeer = r->peerXp != 0 && r->peerSeq == next;
            r->seq = next;
            r->before = xpBefore;
            r->paid = 0;
            r->settled = false;
            r->awarded = false;
            r->at = now;
            r->remoteOnly = false;
            if (!keepPeer) { r->peerXp = 0; r->peerSeq = 0; }
            return true;
        }
        r = Alloc(now);
        if (!r) { overflow_ = true; return false; }
        SetGuid(r, guid);
        r->seq = 1;
        r->before = xpBefore;
        r->paid = 0;
        r->peerXp = 0;
        r->peerSeq = 0;
        r->settled = false;
        r->awarded = false;
        r->at = now;
        r->remoteOnly = false;
        return true;
    }

    // The partner reports what ITS game paid for this death.  Returns true when the report
    // created or updated a record; false when it is a retransmit already accounted for.
    bool PeerAward(const uint32_t guid[4], uint16_t seq, int32_t xp, uint32_t now)
    {
        if (xp <= 0 || xp > kMaxXpPerKill) return false;
        Rec* r = Find(guid);
        if (r && seq != 0 && r->peerSeq >= seq && r->peerXp > 0) return false;  // replayed
        if (!r) {
            r = Alloc(now);
            if (!r) { overflow_ = true; return false; }
            SetGuid(r, guid);
            r->seq = seq;
            r->before = 0;
            r->paid = 0;
            r->peerXp = xp;
            r->peerSeq = seq;
            r->settled = false;
            r->awarded = false;
            r->at = now;
            // No local death yet.  If one arrives inside the grace window it attaches to
            // this record and the local measurement decides; otherwise the grace deadline
            // settles it with paid = 0 and the award is paid.
            r->remoteOnly = true;
            return true;
        }
        r->peerXp = xp;
        r->peerSeq = seq;
        return true;
    }

    // Settle measurements and emit at most `cap` actions.  `xpNow` is the local counter's
    // current reading; `haveXp == false` means the counter is not readable, and the lane
    // then does nothing at all (it must not invent an award it cannot verify).
    int Tick(uint32_t now, bool haveXp, int32_t xpNow, XpOut* out, int cap)
    {
        if (!out || cap <= 0) return 0;
        int n = 0;
        for (int i = 0; i < kMaxDeathXp && n < cap; ++i) {
            Rec& r = r_[i];
            if (!r.used) continue;
            if ((uint32_t)(now - r.at) > kRecordLifeMs) { Clear(r); continue; }
            if (r.awarded) continue;

            if (!r.settled) {
                if (r.remoteOnly) {
                    if ((uint32_t)(now - r.at) < kRemoteGraceMs) continue;
                    if (!haveXp) continue;      // cannot verify: wait, do not guess
                    r.paid = 0;
                    r.settled = true;
                } else {
                    if ((uint32_t)(now - r.at) < kSettleMs) continue;
                    if (!haveXp) {
                        // The counter went away mid-measurement (a world change).  Drop the
                        // record rather than pay an unverified award.
                        Clear(r);
                        continue;
                    }
                    int32_t d = xpNow - r.before;
                    if (d < 0) d = 0;           // a reload lowered it; not an award
                    if (d > kMaxXpPerKill) d = kMaxXpPerKill;
                    r.paid = d;
                    r.settled = true;
                }
            }

            if (r.peerXp > 0) {
                // The partner paid something.  If this machine's game also paid, both
                // players already have it and neither action is needed.
                if (r.paid <= 0) Emit(&r, XP_PAY_LOCAL, r.peerXp, out, &n);
            } else if (r.paid > 0) {
                Emit(&r, XP_SEND_AWARD, r.paid, out, &n);
            }
            r.awarded = true;
        }
        return n;
    }

    // Test/diagnostic access.
    int   Count() const
    {
        int n = 0;
        for (int i = 0; i < kMaxDeathXp; ++i) n += r_[i].used ? 1 : 0;
        return n;
    }
    bool  Overflowed() const { return overflow_; }
    int32_t PaidFor(const uint32_t guid[4]) const
    {
        for (int i = 0; i < kMaxDeathXp; ++i)
            if (r_[i].used && SameGuid(r_[i].guid, guid)) return r_[i].paid;
        return 0;
    }
    int32_t PeerXpFor(const uint32_t guid[4]) const
    {
        for (int i = 0; i < kMaxDeathXp; ++i)
            if (r_[i].used && SameGuid(r_[i].guid, guid)) return r_[i].peerXp;
        return 0;
    }
    bool  Settled(const uint32_t guid[4]) const
    {
        for (int i = 0; i < kMaxDeathXp; ++i)
            if (r_[i].used && SameGuid(r_[i].guid, guid)) return r_[i].settled;
        return false;
    }
    bool  Awarded(const uint32_t guid[4]) const
    {
        for (int i = 0; i < kMaxDeathXp; ++i)
            if (r_[i].used && SameGuid(r_[i].guid, guid)) return r_[i].awarded;
        return false;
    }

private:
    struct Rec {
        bool     used;
        uint32_t guid[4];
        uint16_t seq;
        int32_t  before;
        int32_t  paid;
        int32_t  peerXp;
        uint16_t peerSeq;
        bool     settled;
        bool     awarded;
        bool     remoteOnly;
        uint32_t at;
    };

    static bool SameGuid(const uint32_t a[4], const uint32_t b[4])
    {
        return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
    }
    static void SetGuid(Rec* r, const uint32_t g[4])
    {
        for (int i = 0; i < 4; ++i) r->guid[i] = g[i];
    }
    static void Clear(Rec& r)
    {
        r.used = false;
        r.guid[0] = r.guid[1] = r.guid[2] = r.guid[3] = 0;
        r.seq = 0;
        r.before = 0;
        r.paid = 0;
        r.peerXp = 0;
        r.peerSeq = 0;
        r.settled = false;
        r.awarded = false;
        r.remoteOnly = false;
        r.at = 0;
    }

    Rec* Find(const uint32_t guid[4])
    {
        for (int i = 0; i < kMaxDeathXp; ++i)
            if (r_[i].used && SameGuid(r_[i].guid, guid)) return &r_[i];
        return nullptr;
    }

    Rec* Alloc(uint32_t now)
    {
        for (int i = 0; i < kMaxDeathXp; ++i) {
            if (r_[i].used) continue;
            r_[i].used = true;
            if (i >= n_) n_ = i + 1;
            return &r_[i];
        }
        for (int i = 0; i < kMaxDeathXp; ++i) {
            if ((uint32_t)(now - r_[i].at) <= kRecordLifeMs) continue;
            Clear(r_[i]);
            r_[i].used = true;
            return &r_[i];
        }
        return nullptr;
    }

    void Emit(Rec* r, uint8_t action, int32_t xp, XpOut* out, int* n)
    {
        XpOut& o = out[*n];
        o.action = action;
        for (int i = 0; i < 4; ++i) o.guid[i] = r->guid[i];
        o.seq = r->seq;
        o.xp = xp;
        ++(*n);
    }

    Rec r_[kMaxDeathXp];
    int n_;
    bool overflow_;
};

// ---------------------------------------------------------------------------
// 4. A save on one machine is a save on both
// ---------------------------------------------------------------------------
// WHAT COUNTS AS "A SAVE"
// -----------------------
// `analysis/pak/blueprint-surface.md` enumerates the whole save surface of this build.
// `SaveFunctions_C` (a `BlueprintFunctionLibrary`) exports `SaveGameToDisk`, and
// `FS_GameInstance_C` exports `GI_SaveGameToDisk` / `Save Game to Disk` / `GI_QueueSystemSave`
// / `GI_SaveSystemSaveToDisk`.  Every route a player can take -- a checkpoint rest, the
// game's own autosave, a menu "save", a scripted save -- ends in one of those, so the
// detector is the DISPATCH, not a per-route hook: one comparison per ProcessEvent call, and
// no route can be missed by name.
//
// WHICH SAVES ARE NOT MIRRORED
// ----------------------------
// `qdsuspend.h` already owns the timed SUSPEND save, and a suspend save is a snapshot of one
// machine's world at a different instant -- mirroring it is the rollback that lane exists to
// prevent.  It is therefore refused by name (`WriteSuspendSave`, `GI_WriteSuspendSaveImmediately`,
// `SuspendSaveLoop`, `AsyncSuspendSaveFinished`).  `BlockSuspendSaves` (the peer's hold) is
// left exactly as that lane sets it: this machine's own save is a NORMAL slot write, which the
// hold does not cover.
//
// AND THE ECHO
// ------------
// Applying the partner's save runs the local save dispatch, which the detector would see as
// a local save and re-broadcast -- a two-machine save loop, each write poking the other.  The
// echo is suppressed by BRACKETING the write this machine performs: the caller says
// `BeginOwnSave()` before the game's own save call and `EndOwnSave()` after it, and every
// dispatch inside that bracket is the echo of that one save.  A bracket rather than a count
// or a flag, because one logical save dispatches MORE THAN ONE of the names above in this
// build (`GI_SaveGameToDisk` runs the library's `Save Game to Disk`, which runs
// `SaveGameToDisk`), so a count would have to guess the nesting depth and a plain flag would
// have to guess when it ends.  A short window after the bracket closes covers a writer that
// defers its dispatch by a frame; it is deliberately far shorter than any plausible
// re-save, so a player's own second save still goes out.
static const uint32_t kSaveDedupMs   = 1500;   // the partner repeating the same request
static const uint32_t kSaveDeferMs   = 20000;  // how long a blocked save may wait
static const uint32_t kSaveEchoMs    = 800;    // a deferred echo lands inside this

// What the CALLER measured about the local game.  Every field is a fact the caller can read;
// nothing here is inferred by this header.
struct SaveGate {
    bool loading;     // a world transition / hard travel load is in progress
    bool travelling;  // the travel handshake has not settled
    bool menu;        // a menu world, the save menu, or a paused modal is open
    bool dead;        // the local player is dead or the death flow is running
    bool cutscene;    // a cutscene is running
    bool noSave;      // there is no active save object to write
    bool suspendHold; // the game is mid-suspend-write
    // The caller's own reason string when it has one (the game's busy predicate names the
    // exact fact).  Reported verbatim, because a paraphrased reason is a reason the next
    // reader has to go and look up.
    const char* busy;

    bool Blocked() const
    {
        return loading || travelling || menu || dead || cutscene || noSave || suspendHold ||
               (busy != nullptr && busy[0] != 0);
    }
    // The reason, for the log.  Names the FIRST blocking fact so the line is actionable.
    const char* Why() const
    {
        if (busy && busy[0]) return busy;
        if (noSave)      return "no active save";
        if (loading)     return "a world load is in progress";
        if (travelling)  return "a travel handshake has not settled";
        if (dead)        return "the local player is dead";
        if (cutscene)    return "a cutscene is running";
        if (menu)        return "a menu is open";
        if (suspendHold) return "the game is writing a suspend save";
        return "";
    }
};

class SaveBoard {
public:
    SaveBoard() { Reset(); }

    void Reset()
    {
        lastLocalMs_ = 0;
        haveLocal_ = false;
        ownDepth_ = 0;
        echoUntil_ = 0;
        want_ = false;
        wantAt_ = 0;
        peerSeq_ = 0;
        peerAt_ = 0;
        havePeer_ = false;
        gaveUp_ = false;
        savedForPeer_ = 0;
        echoed_ = 0;
    }

    // The caller is about to run the game's own save on the partner's behalf.  Everything
    // dispatched inside the bracket is that save's own echo.
    void BeginOwnSave(uint32_t now)
    {
        (void)now;
        ++ownDepth_;
    }
    void EndOwnSave(uint32_t now)
    {
        if (ownDepth_ > 0) --ownDepth_;
        if (ownDepth_ == 0) echoUntil_ = now + kSaveEchoMs;
    }
    bool OwnSaveActive() const { return ownDepth_ > 0; }

    // The local save dispatch was observed.  Returns true when the partner must be told.
    // False when the dispatch is the echo of a save this machine performed FOR the partner.
    bool OnLocalSave(uint32_t now)
    {
        if (ownDepth_ > 0) { ++echoed_; return false; }
        if ((int32_t)(now - echoUntil_) < 0) { ++echoed_; return false; }
        haveLocal_ = true;
        lastLocalMs_ = now;
        return true;
    }

    // The partner says it saved.  Returns false for a duplicate of the request already
    // being served; true when a save is now owed.
    bool OnPeerSave(uint32_t seq, uint32_t now)
    {
        if (havePeer_ && (int32_t)(seq - peerSeq_) <= 0) return false;
        if (havePeer_ && (uint32_t)(now - peerAt_) < kSaveDedupMs) {
            peerSeq_ = seq;
            peerAt_ = now;
            return false;
        }
        havePeer_ = true;
        peerSeq_ = seq;
        peerAt_ = now;
        if (want_) return false;                 // already owed; one save serves both
        want_ = true;
        wantAt_ = now;
        gaveUp_ = false;
        return true;
    }

    // Decide what to do this tick.  Returns true when the caller must run the game's own
    // save now; the board then accounts for the echo it will cause.
    bool TakeSave(uint32_t now, const SaveGate& g, const char** why)
    {
        if (why) *why = "";
        if (!want_) return false;
        if (g.Blocked()) {
            if (why) *why = g.Why();
            if ((uint32_t)(now - wantAt_) > kSaveDeferMs) {
                if (!gaveUp_) {
                    gaveUp_ = true;
                    want_ = false;
                    return false;                // reported by GaveUp()
                }
                want_ = false;
            }
            return false;
        }
        want_ = false;
        gaveUp_ = false;
        ++savedForPeer_;
        return true;
    }

    // True once, on the tick the deferral deadline expires.
    bool TakeGaveUp()
    {
        if (!gaveUp_ || want_) return false;
        gaveUp_ = false;
        return true;
    }

    uint32_t LastLocalMs() const { return lastLocalMs_; }
    bool     HaveLocal() const { return haveLocal_; }
    bool     Want() const { return want_; }
    uint32_t SavedForPeer() const { return savedForPeer_; }
    uint32_t Echoes() const { return echoed_; }

private:
    uint32_t lastLocalMs_;
    bool     haveLocal_;
    int      ownDepth_;
    uint32_t echoUntil_;
    bool     want_;
    uint32_t wantAt_;
    uint32_t peerSeq_;
    uint32_t peerAt_;
    bool     havePeer_;
    bool     gaveUp_;
    uint32_t savedForPeer_;
    uint32_t echoed_;
};

// The save dispatch names that are NOT mirrored (qdsuspend.h owns those).
inline bool IsSuspendSaveVerb(const char* name)
{
    if (!name) return false;
    static const char* const kSuspend[] = {
        "WriteSuspendSave", "GI_WriteSuspendSaveImmediately", "SuspendSaveLoop",
        "GI_StartSuspendSaveLoop", "StartSuspendSaveLoop", "CancelSuspendSaveLoop",
        "AsyncSuspendSaveFinished", "UnsafeDeleteSuspendSavesOnSlot",
    };
    for (unsigned i = 0; i < sizeof(kSuspend) / sizeof(kSuspend[0]); ++i)
        if (SameText(name, kSuspend[i])) return true;
    return false;
}

inline int TextLen(const char* s)
{
    int n = 0;
    if (s) while (s[n]) ++n;
    return n;
}

// The save dispatch names that ARE mirrored.  Both the library and the game instance, so a
// build that routes a rest through one and a menu through the other is covered either way.
//
// The LENGTH is tested first: this runs once per ProcessEvent dispatch, which is the hottest
// hook in the module, so it must not walk three string comparisons for every Blueprint call
// in the game.  The three names are 14 and 17 bytes; nothing else of that length is a save.
inline bool IsSaveVerb(const char* name)
{
    if (!name) return false;
    const int n = TextLen(name);
    if (n < 14 || n > 17) return false;
    if (IsSuspendSaveVerb(name)) return false;
    static const char* const kV[] = {
        "SaveGameToDisk", "GI_SaveGameToDisk", "Save Game to Disk",
    };
    for (unsigned i = 0; i < sizeof(kV) / sizeof(kV[0]); ++i)
        if (SameText(name, kV[i])) return true;
    return false;
}

// A system save (`FS_SystemSave`, options/input bindings) is per-machine settings, not
// progression: refused explicitly so a future caller cannot send one through this lane.
inline bool IsSystemSaveVerb(const char* name)
{
    if (!name) return false;
    static const char* const kV[] = {
        "GI_SaveSystemSaveToDisk", "SaveSystemSaveToDisk", "QueueSaveSystemSaveToDisk",
        "GI_QueueSystemSave", "QueueSystemSave", "Save System Save to Disk",
    };
    for (unsigned i = 0; i < sizeof(kV) / sizeof(kV[0]); ++i)
        if (SameText(name, kV[i])) return true;
    return false;
}

} // namespace progression
} // namespace qd
