# 14 — Second online test: rollback's first real run, pause, items (2026-09-27, evening)

Logs: `th06nc_native_coop_{lockstep,rollback}_{p1,p2}.log` (p1 = host,
p2 = the user as guest). Summary: lockstep played stages 1–5 with one
desync in stage 4; rollback played stage 1 and most of stage 2 before a
desync. The committed-loadout fix from docs/13 worked every time (`waiting
for the host to start the stage ... host started after 2547 ms`, and the
host re-derived P2 when the guest's pick came late). All fixes below are
**built, not yet live-tested**; protocol v5.

## Rollback desync: a refused rollback during a bomb

Stage 2, frame 7680, region `player1`. On the guest, just before it:

```
task list changed during frame 7674 (on PREDICTED input ...)
task list changed during frame 7676 (on PREDICTED input ...)
rollback to 7676 skipped -- task list changed since then -- desync likely
```

P1 had just bombed. The guest had simulated those frames on a predicted P1
input, the real input differed, and the rollback was refused because the
scheduler's task list had changed in between — a heap object the snapshot
can't rewind. From there the two games were different games (the host's P1
later ran out of lives, the guest's didn't: "one saw the continue screen").

Which task: the screen shake. `FUN_140077890` (start a shake) has exactly
five callers, all in the four characters' bomb functions; each call
allocates a state block and a priority-0xE task node and pushes the state
onto a `std::vector` at `0xC6E380`. Bombs start several of them, so the
list changed every 5–15 frames during every bomb — every bomb was a window
in which any misprediction became a desync.

The shake is cosmetic (camera offsets; it already ran on a private random
sequence, docs/11). Now:

- shake nodes are left out of the task-list signature (`game_chain.cpp`),
  so a rollback across one is allowed;
- the vector's three pointers `0xC6E380–0xC6E398` are never restored
  (previously only half of the end pointer was, as "shake counter");
- re-simulated frames don't tick shakes (they keep pace with rendered
  frames) and don't start a second copy of a shake the live run already
  started on that frame (`sim_control.cpp`, keyed by netplay frame).

Every other task-list change now logs the tick RVAs added/removed (`task
list changed during frame N (tick RVAs: +77AB0 ...)`, first 60 per stage),
so the next log will say if anything else does this.

## Rollback bug: every rollback stopped after one frame

Every rollback logged `game requested exit while re-simulating frame N`.
The game's step returns a `bool` (the caller tests `AL`); the mod tested
the whole 64-bit register, whose upper bits are garbage, so the
re-simulation loop broke off after its first frame. Harmless in this run
only because all but a handful of rollbacks were one frame long; any
longer rollback left the state behind its frame counter. Fixed: only the
low byte is tested.

## Lockstep desync in stage 4 — and pausing

Stage 4, frame 12420: `rng`, `player1`, `bullets` and `entities` all at
once, `resources` equal, no rollbacks (lockstep has none). Every moving
system diverging together while nothing changed hands is what one game
freezing looks like — the pause the user described (the Steam overlay
opens the pause menu on that machine only).

How pausing works (new RE):

- The gameplay scene object is the static `0x53CAB0` (`+0x24` = stage
  index); its `+0x950` byte (`0x53D400`) is the pause state.
- The scene task `FUN_14003baa0` opens the pause menu when input bit
  `0x400` is freshly pressed **or** while the byte `0xC6DB90` is set, and
  returns 3 while paused, which ends the frame's update walk early.
- `0xC6DB90` is set by the main loop (`FUN_140046da0`) before every
  rendered frame while the Steam overlay's `GameOverlayActivated` callback
  (id 331; object at `*0xC6E360`, active flag `+0x20`) says it's open;
  the step clears it.
- Esc produces `0x600` (pause + cancel); `0x8` is Ctrl/';', not pause as
  an old comment said. Q = `0x800`, R = `0x4000` (pause-menu shortcuts).
- The pause menu `FUN_14000a850` runs from the menu task on the static
  `0x429270` and reads P1's input word. **Nothing is heap-allocated**: the
  old reason for stripping pause ("a pause menu allocates a heap task")
  was wrong, and snapshots cover all of it.

So pause is now an ordinary synchronized input:

- the netplay input mask keeps `0x200 | 0x400 | 0x800 | 0x4000`;
- either player's pause press is merged into P1's word, and while paused
  the guest's menu buttons are too, so either player can pause, navigate,
  resume or quit, and both games do it on the same frame;
- online, the overlay byte is taken away before every step and its rising
  edge becomes one pause press in the local player's input — the overlay
  pauses both games (`Steam overlay opened -- pausing both games`).

## Items: one player locked out while the other auto-collects

The old rule ran the native item update once per frame as one "collector"
and, while any item was homing, kept that collector — so while one player
vacuumed (above the line, or respawning, which auto-collects everything in
the vanilla game), the other couldn't touch anything. The continue's
full-power drops are the visible case: they spawn at the last player to
die, who respawns invulnerable and collects them all.

Now each homing item remembers whom it flies to (`itemOwner[1024]`, in
P2's snapshotted state), and the update runs in two passes: as this
frame's collector over everything except items homing to the partner, then
as the partner over only its own homing items (items are hidden from a pass
by clearing their in-use byte for its duration; the active-item count is
summed). The collector is whoever qualifies for auto-collect, alternating
when both or neither do — so two players above the line split the items.

And on a continue with separate resources, both players get full power
(the solo game gives it back through those drops; the partner's pool would
otherwise stay at 0).

## Checkpoint needing both players

It didn't: the guest adopts the host's settings, start stage and point
included (the log line now prints them). What failed was the check that
the scene init is a fresh run — it refused when the continues counter or
score were nonzero, and both still hold the previous run's values when a
new game starts from the menu, so the checkpoint only worked on the first
run after launching the game (`checkpoint not applied -- a continue` on
both machines). Stage index 0 with no replay/practice is always a fresh
run; those two checks are gone.

## Netcode in the F8 panel, host's choice

`NETCODE` (rollback / lockstep) and `INPUT DELAY` (0–10) are panel items,
saved to `[netplay]`. Online the host's apply to both machines: they
travel in the Loadout and READY messages and are fixed at each stage start
(the guest's panel shows the host's, greyed). `max_rollback` stays per
machine (it sizes the snapshot buffers).

## Visuals

- Fade floor back up to **20%**.
- The focus marker fades with its player: P2's copy (drawn by the mod) and
  P1's (drawn by the HUD draw `FUN_14003e600`, hooked, when P1 is the
  partner — the guest's view).

## The revive timer and transitions (question from the user)

Safe. The timer only advances in P1's update, which runs during dialogue
and the stage-clear screen but not between stages (teardown and the next
scene init happen inside one frame, with no player update in between). A
revive during dialogue just plays the respawn there. The downed flag
survives into the next stage, so a player downed at a stage's end stays
out until the timer finishes. One quirk: if the timer fires in the last
frames of a stage, before the respawn has run, the next stage starts that
player fresh without spending the life the revive handed out — one extra
life. Both machines do the same, so it can't desync.
