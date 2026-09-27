# Player 2 in enemy aim, items, lasers and graze (2026-09-27)

Implements gap-list items A7-A10 from docs/00 on top of docs/01. Built,
**not yet live-tested.**

## The "run as P2" technique

Some native routines touch the player only through P1's absolute address
(docs/00's reference scan). Rather than patch every instruction, those
routines are run a second time — or instead of the first time — with the
relevant P2 fields **exchanged into P1's struct** for the duration of the
call, then exchanged back. Whatever the routine wrote to "P1" (a death, a
graze) lands in P2. Exchanging twice is the identity, so P1 is untouched.

While P2's fields sit in P1's slots, the other P2 hooks (clear zones, hit
test, shot damage) stand down, so nothing is counted twice or computed from
swapped data.

| Routine | Fields exchanged | When |
|---|---|---|
| Enemy script (`FUN_140021f10`, once per enemy per frame) | position (12 bytes) | P2 is the nearer living player |
| Item update (`FUN_140044240`) | position, state, bombing, hurtbox box | P2 is this frame's item collector |
| Laser vs player (`FUN_14006c2b0`, per laser) | position, respawn timer, hit radius, death timers, state, death animation, bombing | always, after P1's own test |

Field offsets come straight from what each routine reads and writes in the
decompile (docs/00's `FindRefsInRange` scan, per function).

## Enemy aim

Every aim calculation — script variables (the overlay mod's
`FUN_14002e260`), aimed bullet patterns (a dozen shooter functions) — reads
P1's position and nothing else, all from inside the enemy's script call.
Each enemy aims at the **nearest living player** (dying/respawning players,
state 1/2, aren't targets), the same rule th06_multi_net uses. Deterministic
(a pure function of positions), so both netplay machines agree.

Not covered: bullets that re-aim at the player *mid-flight* (an ex-flag
handled inside the bullet update, `FUN_140010fa0`) still re-aim at P1 — that
code runs once for all bullets, so there's no per-bullet call to wrap.

## Items

Items home toward the player once it's above the collection line
(`0x352C84`) — or all at once when a bomb starts — and are collected by
touching its hurtbox box. One player "collects" per frame:

1. If exactly one player qualifies (above the line, or bombing), that one —
   so P2's bomb or P2 going to the top of the screen pulls items to P2.
2. If both qualify, keep last frame's collector (items don't flip-flop).
3. If neither qualifies but items are still homing, keep the last collector
   (native items keep homing after the player drops back down).
4. Otherwise alternate every frame, so both players pick up falling items
   by touch.

Collector choice and the alternation bit live in P2's snapshotted state.
Resources collected by P2 go to the same shared globals as P1's.

## Lasers

The laser test is self-contained: reads the player's position and hit
radius, grazes (shared graze/score + sound), or kills (sets that player's
death state and timers). It doesn't touch the laser, and the caller ignores
its result, so running it a second time for P2 is safe.

## Graze

The bullet loop's graze block is inline P1-only code, so P2's graze is
recomputed in the post-update pass (docs/01) with the same formula: smaller
bullet half-size × 0.5-scale constant + graze margin (`0x352BC8`) + the
player's hit radius (`+0x774C`). Graze counters only count while that
player isn't bombing (the native check reads the grazing player's own
bombing flag, `+0x9EC8` — `0x553EB8` for P1); score +500 and the graze
sound always apply. P2's per-bullet "already grazed" bits live in P2's
snapshotted state. The graze spark effect is skipped.

## Still P1-only

- Mid-flight re-aim (above).
- Score/resources are shared; P2 has no HUD of its own.
- P2 is always P1's character and shot type.
