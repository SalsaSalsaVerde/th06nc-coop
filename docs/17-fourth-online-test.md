# 17 — Fourth online test: the rollback desync found; run mode follows the host (2026-09-29)

Logs: the host's `th06nc_native_coop.log` (the user) and the guest's
`th06nc_native_coop_p2desync.log`. Rollback, stage 1, one desync at the
midboss again — but this time the docs/15 forensics were in both logs.

## What the trail showed

Both machines' `Trail` lines agree, region for region, through frame 2241.
At 2242:

```
host   Trail 2242: 74B9/6216 105 101 --  rng C1F153E0 ... resources 38A55BF8
guest  Trail 2242: 1DB9/6211 105 101 R-  rng 5CC6C980 ... resources 0BA2B714
```

- The guest's 2242 was a **re-simulation** (`R-`: rolled back from 2246 to
  2242, confirmed inputs). The host simulated it once.
- The host used **10** random numbers in that frame, the guest **5**.
- Only `rng` and `resources` (score) differ. Players, **bullets (none in
  flight), enemies with their HP, items: identical**, and they stay
  identical for the next 38 frames while the RNG drifts.

That is exactly the docs/15 prediction: "shots hitting an invulnerable boss
(the midboss during its entrance) spawn hit sparks without changing its
HP". One shot hit the midboss on the host and not in the guest's
re-simulation. Player shots were in no checksum region, so the trail
couldn't show them — it can now (the `shots` region, below).

## Why a re-simulated shot behaves differently

The shot-damage function `FUN_14006b420` (decompiled) is the hit test for
one player's 80 shot slots (`+0x410`, stride `0x170`). Per slot it reads:

| slot offset | field |
|---|---|
| `+0x10` u16 | state: 0 free, 1 flying, 2 hit, 3 (type-3 shots) |
| `+0x12` u16 | type (2 = piercing: keeps hitting, damage quartered each time) |
| `+0x13C/+0x140` | logical x, y |
| `+0x160/+0x164` | half-size |
| `+0x15C` u16 | damage |
| `+0x04` float | age (type-2 shots spark every 6 frames) |
| **`+0x50` u16 = VM `+0x38`** | **the sprite VM's script id** |

On a hit of a flying shot it does `script id += 0x20` (the hit animation),
resets the VM (`FUN_140002110`), re-points its script, **runs one VM tick
(`FUN_140007020`, which draws on the game RNG, docs/15)**, spawns the hit
spark (`FUN_14002af80`, type 5, more RNG), then sets state 2 and slows the
shot. The VM tick only runs if the script lookup (`anm + 0x23190 +
id * 8`) is non-null.

And the shot slot's VM (`+0x18`, 0x120 bytes) is precisely what docs/11
**excluded from restore** — "pure animation state" — and what draw
learning keeps excluding, because the draw passes tick it. So after a
rollback a slot is put back to state 1 (flying) with a VM that already has
the hit animation's script id from the future. When the re-simulated hit
adds 0x20 again, the lookup lands elsewhere (null → no VM tick, or a
different script), the RNG count differs, and the two machines' shot
pools drift from there.

## Fixes

1. **Pinned restore.** Snapshots gain a "pinned" set: bytes restored even
   though calibration or draw learning marked them volatile (they stay
   excluded from the sync test's comparison, since the draw passes tick
   them). Both player structs are pinned whole (`Snapshot_PinRva` for
   P1's static struct, `Snapshot_PinExtra` for P2's in the mod's region).
   The sync test's `Snapshot_CompareLive` now skips volatile bytes inside
   restore ranges, so pinning adds no false failures.
2. **`shots` checksum region**: state, type and position of every shot
   slot in use, both players. In the wire checksum, the trail and the
   desync report, so a shot divergence is named as such.
3. **Restore self-check.** After every rollback's restore to frame T, the
   checksum regions are compared with the trail's record of frame T-1
   (the state T started from). A difference is logged as `RESTORE to T
   differs ...: <regions>` — a snapshot gap found on one machine, without
   a peer. 20 reports per session.

Not proven live yet; the next rollback run tells. If a `RESTORE` line
appears, the named region's fields are still not restored faithfully; if
the trail splits again with `shots` equal, the cause is elsewhere.

## Run mode follows the host

The guest already took the host's character and difficulty at its scene
init (docs/04, docs/13). The rest of the run mode was still each game's
own: the stage the menu chose (main game 0, Extra 6, a practice stage),
the practice flag (`0x53D404`, which also ends the run after one stage),
and the options menu's training mode (`0x53D414`: deaths don't cost lives,
power never below 8 — set from option byte `0xC6DFDB` by the menu start
function `FUN_14004f320`, read by the scene init, the player update, the
item collector and the HUD). A difference in any of these desyncs or
degrades the session.

Now the committed Loadout (sent at stage start) carries the stage number
and the three flags; the guest, which waits for it before building its
stage (docs/13), writes the host's values over its own before the scene
init runs (`AdoptHostRunMode`), so the stage is built in the host's mode.
Spell practice isn't adopted (its stage is a boss fight the other side
can't build) and stays a mismatch. READY carries the flags too, and a
remaining difference degrades with "different game modes". Protocol v7.
