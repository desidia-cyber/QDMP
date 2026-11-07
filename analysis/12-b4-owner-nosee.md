# B4 — the flag that hides the player's own body (CLOSED)

Status: **CLOSED**. The flag is a *named* engine bit, proven from its own setter
implementation. The earlier live candidate at `0x4F4` was an artefact of a
too-narrow filter in the diff script, not the flag.

---

## 1. The answer

| Flag | Location | Setter implementation | Confidence |
|---|---|---|---|
| **`bOwnerNoSee`** | **bit 5 (0x20) of byte `[component + 0x212]`** | `UPrimitiveComponent::SetOwnerNoSee` @ **`0x027D8340`** | **HIGH** |
| **`bOnlyOwnerSee`** | **bit 6 (0x40) of byte `[component + 0x212]`** | `UPrimitiveComponent::SetOnlyOwnerSee` @ **`0x027D8310`** | **HIGH** |
| `bRenderInMainPass` | bit 2 (0x04) of `[component + 0x212]` | `SetRenderInMainPass` @ `0x027D87E0` | HIGH |
| `bCastShadow` | bit 4 (0x10) of `[component + 0x213]` | `SetCastShadow` @ `0x027D6620` | HIGH |
| `bHiddenInGame` (propagating) | bit 2 (0x04) of `[component + 0x14D]` | `SetHiddenInGame` @ `0x027FAD80` | HIGH |

**For a first-person player, the body is hidden by `bOwnerNoSee` on the full-body
mesh** (`CharacterMesh0`), not by scale, not by `bHidden`, and not by a render pass
toggle. `bOnlyOwnerSee` is the mirror flag used for the first-person arms.

## 2. The evidence (disassembly)

`SetOwnerNoSee` — the whole function; it is a pure bit-field write:

```
027D8340  movzx r8d, byte ptr [rcx + 0x212]   ; load flag byte
027D8348  movzx eax, r8b
027D834C  shr   al, 5                          ; <- bit 5
027D834F  and   al, 1
027D8351  cmp   al, dl                         ; already equal?
027D8353  je    027D836B
027D8355  and   r8b, 0xdf                      ; clear bit 5 (masks 0x20 off)
027D8359  shl   dl, 5
027D835C  or    r8b, dl                        ; set bit 5 from the argument
027D835F  mov   byte ptr [rcx + 0x212], r8b
027D8366  jmp   02781260                        ; MarkRenderStateDirty()
```

`SetOnlyOwnerSee` is the same shape with `shr al,6` / `and r8b,0xbf` / `shl dl,6`:
bit 6 of the same byte.

`SetRenderInMainPass` (`0x027D87E0`) uses `shr al,2`, `and r8b,0xfb`, `shl dl,2`
— bit 2 of `+0x212`. That the three engine setters share one byte and use
distinct bits is itself a consistency check on the offset.

### How the address was reached (mechanical, no guessing)

```
name "SetOwnerNoSee"
  -> native-function pair table (analysis/re/nativepairs.bin)
  -> exec thunk 0x02E8F860
  -> last direct call -> 0x027D8340 (the real C++ implementation)
```

Caveat discovered while doing this: each module ships **two** pair-table copies.
In copy `@0x04066798` the entry points at a generic Blueprint-VM dispatcher
(`0x0119D280`); copy `@0x04068170` points at the real exec thunk. Do not accept the
first table you match — validate the thunk shape and its last call target.

## 3. Why the live `0x4F4` candidate was wrong

`_live/frida_substrate2.js` lines 113–120 diff `Arms` vs `CharacterMesh0` over
`0x800` bytes, but only keep differences where **both** bytes are `<= 0x0f`:

```js
if (a[k] !== b[k] && a[k] <= 0x0f && b[k] <= 0x0f) { diffs.push(...) }
```

`bOwnerNoSee` lives at `+0x212`. Setting bit 5 makes that byte at least `0x20`,
which the `<= 0x0f` filter discards. So the diff could never have reported the
real flag; `0x4F4` is a different field that happened to satisfy the filter.
This is the documented "right number at the wrong address" failure mode.

## 4. What the mod does with it

- **Owner's own body** (`CharacterMesh0`): leave `bOwnerNoSee = 1`. Do not touch it.
- **Owner's first-person arms** (`Arms`): `bOnlyOwnerSee = 1` is the game's existing
  behaviour; leave it.
- **Remote puppet**: a spawned puppet has no owning player, so `bOwnerNoSee` is
  irrelevant to it. What matters is that the body is *visible*: ensure
  `bOnlyOwnerSee = 0` (otherwise it is hidden from every non-owner, i.e. everyone)
  and `bHiddenInGame = 0`. In practice the defaults inherited from
  `AFS_PlayerCharacter` already do this — the puppet is a *configuration* job, not
  an art job, exactly as `11-live-object-graph.md` concluded.

## 5. What would falsify this

A live read of the two components showing `bOwnerNoSee` unset on `CharacterMesh0`
while the body is still hidden. That would move the hiding mechanism to a
different flag (candidate: `bRenderInMainPass`, or a per-view visibility override).
The static evidence here is about *which bit is which flag*; the remaining live
confirmation is only *which components carry which value*.

## 6. Reproduce

`analysis/re/puppet_b4.py` looks up `SetOwnerNoSee` / `SetOnlyOwnerSee` /
`SetRenderInMainPass` / `SetCastShadow` / `SetHiddenInGame` in
`analysis/re/nativepairs.bin`, follows each exec thunk's last call, and prints the
implementation disassembly above.
