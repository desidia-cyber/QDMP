//! The monotonic progression flag bank.
//!
//! This is the **client-side mirror of `mod/native/src/qdflags.h`**, which was read
//! against this file transition by transition. The native class and this one must
//! implement the same three rules, because they hold opposite ends of the same
//! container and a disagreement shows up as a lost boss kill:
//!
//! 1. **Never lower.** Progression is monotonic. A peer write that would regress is
//!    refused, and the refusal is *visible* (the call returns `false`) rather than a
//!    silent clamp.
//! 2. **The host is the only writer.** [`FlagBank::set_local`] is the host's path and
//!    may set anything, including a reset, because the host is the authority.
//!    [`FlagBank::apply_remote`] and [`FlagBank::apply_intent`] are the peer path and
//!    may only raise.
//! 3. **An intent is accepted only if it is current**: `based_on_rev >=
//!    last_sent_rev`. A peer that has not seen the host's latest state cannot act on
//!    its stale view, which makes double-application structurally impossible rather
//!    than merely unlikely (WIRE.md §3.10, `sync-docs-audit.md` rule E4).
//!
//! The bank is name-indexed rather than a struct because the game composes keys
//! (`<instance>` + `<property>`), and the whole progression payload for a save slice
//! is a handful of `int32` story facts and counters.
//!
//! Two implementation details are deliberately identical and one is not:
//!
//! * `revision()` starts at **1**, not 0, and never becomes 0 (both sides skip 0 on
//!   increment) — `0` is the "unspecified revision" sentinel on `WORLD_FLAGS`.
//! * Enumeration is ordered by **unsigned byte comparison** of the name. Rust's
//!   `BTreeMap<String>` ordering is exactly that, so both encoders produce the same
//!   bytes without a sort at the call site, which is what makes `doc/vectors.txt`
//!   meaningful.
//! * **Names longer than 63 bytes** (`kMaxName - 1`) are truncated at a UTF-8
//!   boundary here. The native copies 63 raw bytes, which for ASCII is the same
//!   result; for a multi-byte name the native would split a code point and then never
//!   match the key again, so the truncation here is the safer half of an edge case
//!   that a save key never reaches. Lookup normalises the same way, so a truncated
//!   key is still reachable.

use std::collections::BTreeMap;

/// `INTENT` kind 1, mode byte: the requested value is added to the host's value.
pub const MODE_ADDITIVE: u8 = 0;
/// `INTENT` kind 1, mode byte: the requested value replaces the host's value.
pub const MODE_ABSOLUTE: u8 = 1;

/// `qdflags.h` `kMaxFlags`: the bank refuses a 513th key rather than growing.
pub const MAX_FLAGS: usize = 512;

/// `qdflags.h` `kMaxName - 1`: the longest name the native bank stores.
pub const MAX_NAME_BYTES: usize = 63;

/// How a write combines with the value already held.
///
/// Maps 1:1 onto `qdflags.h`'s `bool absolute` and onto the `INTENT` mode byte
/// (WIRE.md §3.10).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum FlagMode {
    /// Added once, and never allowed to lower. Used for counters whose game node is
    /// `NSDCM_Add`.
    Additive,
    /// Set outright, in either direction. Legitimate for the authority, refused on
    /// the peer path when it would lower.
    Absolute,
}

impl FlagMode {
    pub fn to_wire(self) -> u8 {
        match self {
            FlagMode::Additive => MODE_ADDITIVE,
            FlagMode::Absolute => MODE_ABSOLUTE,
        }
    }

    pub fn is_absolute(self) -> bool {
        self == FlagMode::Absolute
    }

    /// Decoding is strict: WIRE.md §3.10 defines exactly two modes, and an
    /// unrecognised one would be applied with a guessed meaning.
    pub fn from_wire(v: u8) -> crate::Result<Self> {
        match v {
            MODE_ADDITIVE => Ok(FlagMode::Additive),
            MODE_ABSOLUTE => Ok(FlagMode::Absolute),
            other => Err(crate::Error::OutOfRange { what: "intent mode", value: i64::from(other) }),
        }
    }
}

/// The named `i32` flag space published by `WORLD_FLAGS` (WIRE.md §3.6).
#[derive(Debug, Clone)]
pub struct FlagBank {
    entries: BTreeMap<String, i32>,
    rev: u32,
    dirty: bool,
}

impl Default for FlagBank {
    fn default() -> Self {
        Self::new()
    }
}

impl FlagBank {
    pub fn new() -> Self {
        Self { entries: BTreeMap::new(), rev: 1, dirty: false }
    }

    /// Empty the bank and reset the revision, exactly as `qdflags.h::Clear` does.
    pub fn clear(&mut self) {
        self.entries.clear();
        self.rev = 1;
        self.dirty = false;
    }

    pub fn len(&self) -> usize {
        self.entries.len()
    }

    pub fn is_empty(&self) -> bool {
        self.entries.is_empty()
    }

    /// The container revision. Starts at 1; 0 means "unspecified" on the wire and is
    /// never produced here.
    pub fn revision(&self) -> u32 {
        self.rev
    }

    /// Whether a local write happened since the last [`Self::clear_dirty`]. The peer
    /// uses this to decide whether a `WORLD_FLAGS` body needs sending "on change"
    /// rather than waiting for the 1 Hz tick.
    pub fn is_dirty(&self) -> bool {
        self.dirty
    }

    pub fn clear_dirty(&mut self) {
        self.dirty = false;
    }

    pub fn get(&self, name: &str) -> Option<i32> {
        self.entries.get(&normalise_name(name)).copied()
    }

    pub fn cap_reached(&self) -> bool {
        self.entries.len() >= MAX_FLAGS
    }

    /// Ordered enumeration by name — the order a `WORLD_FLAGS` body must use.
    pub fn entries(&self) -> impl Iterator<Item = (&str, i32)> {
        self.entries.iter().map(|(k, v)| (k.as_str(), *v))
    }

    /// An owned, ordered copy of the bank, for building a `WORLD_FLAGS` body.
    pub fn snapshot(&self) -> Vec<(String, i32)> {
        self.entries.iter().map(|(k, v)| (k.clone(), *v)).collect()
    }

    /// A local, host-originated write (the `SetLocal` path).
    ///
    /// Returns whether the bank changed. `false` therefore means "no change" *or*
    /// "the bank is full" — the native returns the same ambiguity, and the caller
    /// distinguishes them with [`Self::cap_reached`].
    ///
    /// An absolute write may lower a value (the host is the authority and a reset is
    /// legitimate). An additive write may not: `current + value` is clamped at
    /// `current`, which is what stops a negative counter delta from rewriting
    /// progress.
    pub fn set_local(&mut self, name: &str, value: i32, mode: FlagMode) -> bool {
        if name.is_empty() {
            // The native refutes a null name; an empty key is the same mistake.
            return false;
        }
        let key = normalise_name(name);
        match self.entries.get(&key).copied() {
            None => {
                if self.cap_reached() {
                    return false;
                }
                self.entries.insert(key, value);
                self.mark_dirty();
                true
            }
            Some(current) => {
                let mut next = if mode.is_absolute() {
                    value
                } else {
                    current.wrapping_add(value)
                };
                if !mode.is_absolute() && next < current {
                    next = current;
                }
                if next == current {
                    return false;
                }
                self.entries.insert(key, next);
                self.mark_dirty();
                true
            }
        }
    }

    /// Apply one entry of a remote `WORLD_FLAGS` body (the `ApplyRemote` path).
    ///
    /// Returns whether the stored value changed. A stale broadcast, a value that would
    /// lower the key, and an unchanged value all return `false`; only the last of
    /// those still adopts a newer revision, because seeing the same value at a newer
    /// revision is still evidence that the peer is current.
    ///
    /// `rev == 0` means "unspecified" and bypasses the staleness check (both sides
    /// use it for a first send before any revision has been minted).
    pub fn apply_remote(&mut self, name: &str, value: i32, rev: u32) -> bool {
        if name.is_empty() {
            return false;
        }
        if rev != 0 && rev < self.rev {
            return false;
        }
        let key = normalise_name(name);
        let Some(current) = self.entries.get(&key).copied() else {
            if self.cap_reached() {
                return false;
            }
            self.entries.insert(key, value);
            if rev > self.rev {
                self.rev = rev;
            }
            return true;
        };
        if value < current {
            // Return *before* adopting `rev`, exactly as the native does: a refused
            // value is not evidence that we are current.
            return false;
        }
        if value == current {
            if rev > self.rev {
                self.rev = rev;
            }
            return false;
        }
        self.entries.insert(key, value);
        if rev > self.rev {
            self.rev = rev;
        }
        true
    }

    /// Apply a peer's `INTENT` kind 1 (`set flag`) request, as the host (the
    /// `ApplyIntent` path).
    ///
    /// `last_sent_rev` is the revision of the last state the host published; the
    /// request is refused when `based_on_rev < last_sent_rev` (WIRE.md §3.10). Pass
    /// [`Self::revision`] when the bank has just been published.
    ///
    /// The additive arithmetic happens here, once, and the result is published
    /// absolutely, so no receiver ever applies a delta (rule E2).
    pub fn apply_intent(
        &mut self,
        name: &str,
        value: i32,
        mode: FlagMode,
        based_on_rev: u32,
        last_sent_rev: u32,
    ) -> bool {
        if name.is_empty() {
            return false;
        }
        if based_on_rev < last_sent_rev {
            return false;
        }
        let key = normalise_name(name);
        let Some(current) = self.entries.get(&key).copied() else {
            // An intent may create a key (the game composes keys per instance), but
            // only when the table has room.
            if self.cap_reached() {
                return false;
            }
            self.entries.insert(key, value);
            self.mark_dirty();
            return true;
        };

        let mut next = if mode.is_absolute() { value } else { current.wrapping_add(value) };
        if !mode.is_absolute() && next < current {
            next = current;
        }
        if next < current {
            // An absolute request that lowers a progression key is refused.
            return false;
        }
        if next == current {
            return false;
        }
        self.entries.insert(key, next);
        self.mark_dirty();
        true
    }

    /// `qdflags.h::MarkDirty`: bump the revision (skipping 0) and raise the flag.
    fn mark_dirty(&mut self) {
        self.dirty = true;
        self.rev = self.rev.wrapping_add(1);
        if self.rev == 0 {
            self.rev = 1;
        }
    }
}

/// Truncate to [`MAX_NAME_BYTES`] on a code-point boundary.
///
/// ASCII save keys (the only kind this wire carries) truncate byte-for-byte, which is
/// the native behaviour. A multi-byte name truncates one code point earlier than the
/// native would, which keeps it valid UTF-8 instead of persisting half a character.
fn normalise_name(name: &str) -> String {
    if name.len() <= MAX_NAME_BYTES {
        return name.to_owned();
    }
    let mut end = MAX_NAME_BYTES;
    while end > 0 && !name.is_char_boundary(end) {
        end -= 1;
    }
    name[..end].to_owned()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn fresh_bank_revision_is_one() {
        // qdflags.h::Clear sets rev_ = 1; a 0 here would be indistinguishable from
        // the "unspecified" sentinel on the wire.
        let b = FlagBank::new();
        assert_eq!(b.revision(), 1);
        assert!(b.is_empty());
        assert!(!b.is_dirty());
        assert!(!b.cap_reached());
    }

    #[test]
    fn clear_resets_to_revision_one() {
        let mut b = FlagBank::new();
        b.set_local("DoorA", 1, FlagMode::Absolute);
        b.clear();
        assert_eq!(b.revision(), 1);
        assert_eq!(b.get("DoorA"), None);
        assert!(!b.is_dirty());
    }

    #[test]
    fn local_absolute_write_may_lower_and_bumps_the_revision() {
        let mut b = FlagBank::new();
        assert!(b.set_local("Coins", 100, FlagMode::Absolute));
        assert_eq!(b.revision(), 2);
        assert!(b.set_local("Coins", 40, FlagMode::Absolute));
        assert_eq!(b.revision(), 3);
        assert_eq!(b.get("Coins"), Some(40));
        assert!(b.is_dirty());
        b.clear_dirty();
        assert!(!b.is_dirty());
    }

    #[test]
    fn local_additive_write_accumulates() {
        let mut b = FlagBank::new();
        b.set_local("CheckpointUnlockCount", 1, FlagMode::Additive);
        assert_eq!(b.revision(), 2);
        assert!(b.set_local("CheckpointUnlockCount", 2, FlagMode::Additive));
        assert_eq!(b.get("CheckpointUnlockCount"), Some(3));
    }

    #[test]
    fn local_additive_write_can_never_lower() {
        // qdflags.h: `if (!absolute && next < value) next = value;`
        let mut b = FlagBank::new();
        b.set_local("Kills", 10, FlagMode::Absolute);
        let rev = b.revision();
        assert!(!b.set_local("Kills", -20, FlagMode::Additive), "no change");
        assert_eq!(b.get("Kills"), Some(10));
        assert_eq!(b.revision(), rev, "a no-op must not bump the revision");
    }

    #[test]
    fn local_write_that_changes_nothing_returns_false() {
        let mut b = FlagBank::new();
        b.set_local("A", 1, FlagMode::Absolute);
        let rev = b.revision();
        b.clear_dirty();
        assert!(!b.set_local("A", 1, FlagMode::Absolute));
        assert_eq!(b.revision(), rev);
        assert!(!b.is_dirty());
    }

    #[test]
    fn local_write_creates_a_key_with_the_given_value_in_both_modes() {
        let mut b = FlagBank::new();
        assert!(b.set_local("NewAbsolute", 7, FlagMode::Absolute));
        assert!(b.set_local("NewAdditive", 9, FlagMode::Additive));
        assert_eq!(b.get("NewAbsolute"), Some(7));
        // qdflags.h assigns `value` on insert for both modes, not 0 + value.
        assert_eq!(b.get("NewAdditive"), Some(9));
    }

    #[test]
    fn remote_write_refuses_to_lower() {
        let mut b = FlagBank::new();
        b.set_local("BossKilled", 1, FlagMode::Absolute);
        let rev = b.revision();
        assert!(!b.apply_remote("BossKilled", 0, rev + 1));
        assert_eq!(b.get("BossKilled"), Some(1));
        // The native returns before adopting the revision on a refusal.
        assert_eq!(b.revision(), rev);
    }

    #[test]
    fn remote_write_raises_and_adopts_the_revision() {
        let mut b = FlagBank::new();
        b.set_local("BossKilled", 0, FlagMode::Absolute);
        let rev = b.revision();
        assert!(b.apply_remote("BossKilled", 1, rev + 4));
        assert_eq!(b.get("BossKilled"), Some(1));
        assert_eq!(b.revision(), rev + 4);
    }

    #[test]
    fn remote_equal_value_returns_false_but_still_adopts_the_revision() {
        let mut b = FlagBank::new();
        b.set_local("A", 1, FlagMode::Absolute);
        let rev = b.revision();
        assert!(!b.apply_remote("A", 1, rev + 2));
        assert_eq!(b.revision(), rev + 2, "seeing the same value at a newer rev is evidence");
    }

    #[test]
    fn remote_stale_revision_is_refused() {
        let mut b = FlagBank::new();
        b.set_local("A", 1, FlagMode::Absolute);
        let rev = b.revision();
        assert!(!b.apply_remote("B", 9, rev - 1));
        assert_eq!(b.get("B"), None, "a stale broadcast must not insert");
        assert_eq!(b.revision(), rev);
    }

    #[test]
    fn remote_revision_zero_is_not_stale() {
        // Both sides use 0 for "unspecified", e.g. a first send before any mint.
        let mut b = FlagBank::new();
        b.set_local("A", 5, FlagMode::Absolute);
        let rev = b.revision();
        assert!(b.apply_remote("A", 6, 0));
        assert_eq!(b.get("A"), Some(6));
        assert_eq!(b.revision(), rev, "rev 0 must not move the revision");
    }

    #[test]
    fn remote_write_creates_a_key_and_does_not_dirty_the_bank() {
        // A remote value is not re-broadcast, so it must not look like a local change.
        let mut b = FlagBank::new();
        b.clear_dirty();
        assert!(b.apply_remote("Fresh", 3, 1));
        assert_eq!(b.get("Fresh"), Some(3));
        assert!(!b.is_dirty());
    }

    #[test]
    fn intent_is_refused_when_the_sender_was_stale() {
        let mut b = FlagBank::new();
        b.set_local("A", 1, FlagMode::Absolute);
        let rev = b.revision();
        assert!(!b.apply_intent("ItemTaken", 1, FlagMode::Absolute, rev - 1, rev));
        assert_eq!(b.get("ItemTaken"), None);
    }

    #[test]
    fn intent_is_accepted_when_the_sender_is_current() {
        // The gate is `>=`, so an intent based on the revision just published lands.
        let mut b = FlagBank::new();
        b.set_local("A", 1, FlagMode::Absolute);
        let rev = b.revision();
        assert!(b.apply_intent("DoorA", 1, FlagMode::Absolute, rev, rev));
        assert_eq!(b.get("DoorA"), Some(1));
        assert_eq!(b.revision(), rev + 1, "an applied intent is the host's own write");
    }

    #[test]
    fn intent_absolute_cannot_lower() {
        let mut b = FlagBank::new();
        b.set_local("BossKilled", 1, FlagMode::Absolute);
        let rev = b.revision();
        assert!(!b.apply_intent("BossKilled", 0, FlagMode::Absolute, rev, rev));
        assert_eq!(b.get("BossKilled"), Some(1));
    }

    #[test]
    fn intent_additive_accumulates_once() {
        let mut b = FlagBank::new();
        b.set_local("Kills", 10, FlagMode::Absolute);
        let rev = b.revision();
        assert!(b.apply_intent("Kills", 5, FlagMode::Additive, rev, rev));
        assert_eq!(b.get("Kills"), Some(15));
    }

    #[test]
    fn intent_additive_never_lowers() {
        let mut b = FlagBank::new();
        b.set_local("Kills", 10, FlagMode::Absolute);
        let rev = b.revision();
        b.clear_dirty();
        assert!(!b.apply_intent("Kills", -20, FlagMode::Additive, rev, rev));
        assert_eq!(b.get("Kills"), Some(10));
        assert!(!b.is_dirty());
    }

    #[test]
    fn intent_can_create_a_key() {
        let mut b = FlagBank::new();
        let rev = b.revision();
        assert!(b.apply_intent("RustyBrokeBarricade", 1, FlagMode::Absolute, rev, rev));
        assert_eq!(b.get("RustyBrokeBarricade"), Some(1));
    }

    #[test]
    fn the_bank_refuses_a_513th_key() {
        let mut b = FlagBank::new();
        for i in 0..MAX_FLAGS {
            assert!(b.set_local(&format!("k{i:04}"), 1, FlagMode::Absolute), "key {i}");
        }
        assert!(b.cap_reached());
        assert!(!b.set_local("overflow", 1, FlagMode::Absolute));
        assert_eq!(b.len(), MAX_FLAGS);
        // An existing key can still be written at the cap.
        assert!(b.set_local("k0000", 2, FlagMode::Absolute));
        // And a remote insert is refused too, rather than growing the table.
        assert!(!b.apply_remote("overflow", 1, 9));
    }

    #[test]
    fn enumeration_is_ordered_by_unsigned_bytes_regardless_of_insertion_order() {
        let mut a = FlagBank::new();
        for (k, v) in [("Zeta", 1), ("Alpha", 2), ("Mid", 3)] {
            a.set_local(k, v, FlagMode::Absolute);
        }
        let mut b = FlagBank::new();
        for (k, v) in [("Mid", 3), ("Zeta", 1), ("Alpha", 2)] {
            b.set_local(k, v, FlagMode::Absolute);
        }
        assert_eq!(a.snapshot(), b.snapshot());
        assert_eq!(
            a.snapshot(),
            vec![("Alpha".to_owned(), 2), ("Mid".to_owned(), 3), ("Zeta".to_owned(), 1)]
        );
        let names: Vec<&str> = a.entries().map(|(k, _)| k).collect();
        assert_eq!(names, vec!["Alpha", "Mid", "Zeta"]);
    }

    #[test]
    fn revision_wrap_skips_zero() {
        let mut b = FlagBank::new();
        b.rev = u32::MAX;
        assert!(b.set_local("A", 1, FlagMode::Absolute));
        assert_eq!(b.revision(), 1);
    }

    #[test]
    fn names_are_capped_at_the_native_length() {
        let long = "x".repeat(100);
        let mut b = FlagBank::new();
        assert!(b.set_local(&long, 1, FlagMode::Absolute));
        // Inserted under the truncated key, and reachable through both spellings.
        assert_eq!(b.get(&long), Some(1));
        assert_eq!(b.get(&"x".repeat(MAX_NAME_BYTES)), Some(1));
        assert_eq!(b.entries().next().unwrap().0.len(), MAX_NAME_BYTES);
    }

    #[test]
    fn an_empty_name_is_refused() {
        let mut b = FlagBank::new();
        assert!(!b.set_local("", 1, FlagMode::Absolute));
        assert!(!b.apply_remote("", 1, 1));
        assert!(!b.apply_intent("", 1, FlagMode::Absolute, 1, 1));
        assert!(b.is_empty());
    }

    #[test]
    fn flag_mode_wire_roundtrip_and_strict_decode() {
        assert_eq!(FlagMode::Absolute.to_wire(), MODE_ABSOLUTE);
        assert_eq!(FlagMode::Additive.to_wire(), MODE_ADDITIVE);
        assert_eq!(FlagMode::from_wire(0).unwrap(), FlagMode::Additive);
        assert_eq!(FlagMode::from_wire(1).unwrap(), FlagMode::Absolute);
        assert!(FlagMode::from_wire(2).is_err());
    }
}
