# 15 — Third online test: a rollback desync of a new kind, items, HUD, DPS (2026-09-27, late)

Logs: `th06nc_native_coop_p1.log` (host) and `_p2.log` (the user, guest).
**Lockstep played stages 1–6 without a single desync**, three continues
included (the docs/13 fix held, and "both players at full power" fired on
each). No `game requested exit while re-simulating` lines: the docs/14
re-simulation fix held. Rollback desynced once, in stage 1.

## The rollback desync

```
checksums match at frame 1800
CoopRules: boss phase HP x2.00 (hp 6000 -> 12000)      <- the midboss arrives
DESYNC at frame 1980 in rng
```

Nothing like the earlier cases: no refused rollback, no task-list change,
and only the RNG differed — players, bullets, enemies (HP included) and
items were still equal. The host had rolled back 47 times by frame 1800
(the guest's inputs), the guest twice.

What that pattern points at: something that draws random numbers but
doesn't change anything checksummed, whose state a rollback didn't rewind
correctly. New RE this pass:

- **The sprite-animation tick `FUN_140007020` draws from the game's RNG**:
  anm opcode `0x10` ("random sprite": `sprite = base + rand % n`) uses the
  same seed/counter as gameplay (`0xABAE64/60`). So any animation that runs
  a different number of times — or from a different point of its script —
  on the two machines shifts gameplay randomness. The earlier assumption
  that animations have their own random source (docs/11) was wrong.
- Shots hitting an invulnerable boss (the midboss during its entrance)
  spawn hit sparks without changing its HP — a way for RNG to diverge
  while every checksummed system still agrees.

Two concrete gaps between the sync test (which passes) and real rollback,
both closed now:

1. **Draw learning.** The sync test keeps learning, every frame, which
   bytes the draw pass alone writes, and stops rewinding them. Netplay only
   learned at the stage-start barrier — so bytes drawing writes in objects
   created later (the player draw writes VM positions into the player
   struct, the HUD draws the GUI object's VMs) were rewound online but not
   in the tested setup. Rollback sessions now learn continuously too.
2. **Extra regions.** The sync test stopped rewinding bytes of P2's
   struct, the rules state and the GUI object that re-simulated
   differently; netplay rewound all of them. Draw learning now covers the
   extra regions as well.

Neither is proven to be *this* desync, so the next rollback run carries
forensics:

- **Frame trail.** Every simulated frame records, after its step, the RNG
  seed/counter, both inputs, whether it was re-simulated or predicted, and
  all seven checksum regions. At the first desync both machines log the 90
  frames before it (`Trail N: seed/counter P1 P2 RP r0..r6`). Diffing the
  two logs gives the exact frame the RNG split and whether that frame had
  been re-simulated.
- **Page hashes.** Each checksum frame also hashes every 4 KB page of the
  restorable `.data` (pointer-shaped words counted as 0, since addresses
  differ per process) plus each extra region; logged at the first desync
  (`Pages NNNN: ...`). Diffing names the memory that differs.
- **Recent rollbacks** (`rollback from E back to T`) around the desync.
- **Misprediction sync test** (`[synctest] perturb=1`, one machine): before
  each check, the window is first run on deliberately wrong inputs
  (left/right swapped, shoot and focus toggled, for both players), then
  rewound and checked as usual. Anything a snapshot doesn't rewind keeps
  what the wrong run did to it — exactly what a real misprediction does and
  what the identical-input sync test can never show.

## Items: still one player at a time

The docs/14 rewrite gave auto-collect to a player in state 3 (invulnerable:
respawning or bombing). The game doesn't: its test is only "above the
collection line, in state 0 or 3" (`FUN_140044240`, the collect line
constant `0x352C84` against the player's y). So whenever one player was
invulnerable, that player was the collector every frame and the other
never got a pass for loose items — for the whole respawn blink after every
death. Fixed; and items born homing (bomb-cancelled bullets, spawned in
state 1) now go to the nearer player instead of a stale owner.

## HUD: each player sees their own pool

With separate resources the guest's HUD showed P1's lives/bombs/power. The
HUD draw `FUN_14003e600` (and `FUN_140041f50`, which it calls) reads power,
graze, lives and bombs straight from the globals, so on the guest P2's pool
is swapped in for the duration of that draw only (outside any simulation
step). The status text now shows the **partner's** pool: P2's on the host,
P1's on the guest.

### A second HUD on the left (future scope)

Feasible. The HUD is drawn by one task from the GUI object's sprite VMs and
the globals above; the window's left margin (x < 27%) is empty. A mirrored
set for the partner would mean: map which GUI VMs draw the lives/bombs
stars and the power readout (one RE pass over `FUN_14003e600`/`41f50`),
then call the same draw a second time with the partner's pool swapped in
and the VM positions shifted left — or draw copies of those VMs, as the
focus marker is done. Roughly one session, most of it placing things live
on screen. The status text would then move out of the way.

## Overlay size

The status block now uses 11-pixel glyphs (was 16) and word-wraps at 26
columns, so it stays in the left margin instead of reaching into the
playfield over the boss HP bar. Messages were shortened.

## Boss DPS meter

Bottom-left, while a boss is up: `BOSS DPS n` and each player's share,
averaged over the last 3 s. The shot-damage hook reports P1's and P2's
damage separately; damage to the tracked boss is tallied per frame in a
ring inside the rules state — so a rollback rewinds it and it never counts
twice. `[visual] boss_dps=0` turns it off. (Raw damage per second, before
the boss HP multiplier.)

## Power after a revive

`[coop] revive_power` / F8 `POWER AFTER REVIVE`: `last` (default — the
power the player had the last frame they were in play, i.e. before the hit
that downed them), or a tier: 0 8 16 32 48 64 80 96 128 (the game's
shot-power table at `0x34E150`). Applies to timer revives with separate
resources; a continue still gives both full power. Host's setting online.

Protocol v6 (the co-op settings grew).
