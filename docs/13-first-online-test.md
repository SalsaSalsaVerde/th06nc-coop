# 13 — First online test: what the logs showed and what changed (2026-09-27)

Two machines, two attempts, four logs (`p1_*` = host, `p2_*` = guest; the
user was the guest). Headline: **lockstep held for two full stages** —
every checksum matched from stage 1 frame 0 to stage 2 frame 12774, with
bombs, a boss kill, a continue and a stage transition in between, zero
rollbacks, zero predicted frames. The two failures had nothing to do with
the simulation being non-deterministic; both are protocol gaps at the
stage start, and both are fixed below.

## Attempt 1 (rollback mode): "desynced at the very start, fell back to local"

Not a desync. The log says `MISMATCH -- local P1=0A P2=1A ... peer P1=0A
P2=1B` on both sides: the host's game had spawned P2 as **Marisa A**, the
guest had picked **Marisa B**. Timeline (clocks differ by an hour, aligned
on the lobby connect):

| host clock | event |
|---|---|
| 17:08:32.2 | host receives "partner selected MarisaA" |
| 17:08:35.16 | **host's stage starts** (scene init; P2 spawned as Marisa A) |
| 17:08:35.86 | host receives "partner selected MarisaB" — 0.7 s too late |
| 17:08:40.2 (guest clock 16:08:40) | guest's stage starts |

The guest was still on the character screen when the host started. The
menu-time selection messages were sent unreliably every 10 frames and only
while in the menus, so the host's picture of the guest froze at whatever
had last arrived. Rollback itself never ran: `Degraded` from the barrier.

## Attempt 2 (lockstep): desync at stage 3 frame 1260, "resources"

Stage 1 and 2 clean. In stage 3, at frame ~1200, both games hit game over
and continued on the same frame — but:

```
host:  CoopRules: continue used (2 so far), lives now 2
guest: CoopRules: continue used (2 so far), lives now 0
```

then `DESYNC at frame 1260 in resources`. The continue is the game-over
task `FUN_14000bc20`; on "yes" it does

```
continues++
lives = *0xC6E000     // "stage start" lives
bombs = *0xC6E001
```

and those two bytes are written by the scene init `FUN_14003c250` **only
when the scene state (`0xC6DFBC`) isn't 3** — the next-stage path — so
they hold the values from the run's *first* stage all run long: a continue
always refills to the starting stock. The mod's guest side, at every stage
barrier, overwrote them with the host's *current* lives (the comment said
"redo the scene init's copy", true for stage 1 only). By stage 3 the host
had 0 lives in reserve, so the guest's copy became 0, while the host's own
copy still said 2. The next continue handed out 2 lives on the host and 0
on the guest; the shared pool diverged, and from there the two games were
different games (the guest's P2 died again and reached a second continue
prompt the host never saw — "P2 stopped receiving inputs" was the guest's
game sitting on its own continue screen).

The bytes weren't in the resources checksum, which is why the difference
sat unnoticed from the stage start until the continue.

## Fixes (protocol v4)

1. **Run-start stock travels in READY.** `ReadyMsg` carries the host's
   `0xC6E000/1`; the guest adopts those instead of the host's current
   lives/bombs. Both bytes joined the `resources` checksum, so a wrong copy
   shows at frame 0 of the stage rather than at the next continue.
2. **Committed loadouts.** Each machine sends its selection once more,
   reliably, the moment its stage starts (`LoadoutMsg.committed = 1`,
   session = the one about to start). Menu-time selections now go out
   reliably on change too (plus the periodic unreliable copy).
3. **The guest waits for the host's commit before building the stage.** A
   guest whose scene init runs before the host's commit has arrived spins
   there (pumping the transport) for up to 20 s. The screen holds its last
   frame meanwhile — nothing draws inside the scene switch — and the log
   says `waiting for the host to start the stage`. P1 and the difficulty
   are the game's own objects (the character selects the sprite sheet the
   scene init loads, the difficulty the stage scripts), so the guest can't
   redo them afterwards; this is the only place they can be gotten right.
   In the common case (both press start within seconds) the wait is a few
   hundred ms at most; at stage transitions it's one network trip.
4. **The host re-derives P2 at the barrier.** A host that started before
   the guest's final pick spawned P2 from the last menu-time message. P2
   is the mod's own object, so at the barrier — when the guest's READY
   (which carries the guest's own pick) differs — `Player2_ReapplyLoadout`
   rebuilds P2 from P1's freshly restored stage-start state as the right
   character (same as the spawn: struct copy, offset, stats/shot/bomb
   functions, sprite sheet, VM scripts, task-node pointers) and the
   barrier's P2 reference copy is retaken. No re-registration, no tasks
   touched.
5. **Clearer degrade reasons.** The status line names the cause: different
   builds, different stages, or "the host changed their pick after you
   started -- both quit to the title and start again" (the one case left
   that can't be repaired in place: the guest waited 20 s, gave up, and the
   host then picked something else).

## What "recover" can mean

A checksum mismatch mid-stage can't be healed in lockstep or rollback:
both machines have a full, different game state and no way to copy one to
the other (9 MB of `.data` plus heap tasks). What the mod does is contain
it: the stage plays on with the same inputs, the status line says
`DESYNC AT FRAME n`, and the **next stage start re-synchronizes** — the
barrier restarts from a fresh scene init with the host's resources (and
now the host's run-start stock) adopted by the guest. So a desync costs at
most the rest of the current stage. The right response to seeing one is
still to send both logs, since the region name says which system diverged
first.

## Numbers from the lockstep run

| | stage 1 | stage 2 | stage 3 (until the continue) |
|---|---|---|---|
| frames | 10014 | 12774 | 1260 |
| checksums compared | every 60 frames, all matched | all matched | matched to 1200 |
| rollbacks / predicted | 0 / 0 | 0 / 0 | 0 / 0 |
| stalls (frames waited for input) | 1163 | 1218 | 85 |

Stalls of ~10% at input delay 2 are the cost of lockstep over a real
connection; rollback mode is what removes them, and it now gets its first
real run.

## Also in this build

- Proximity fade floor 20% → **15%**, and it now covers the faded player's
  **shots** too (both draw passes walk the 80 shot slots; each in-use
  slot's VM alpha is scaled for the pass, colors untouched).
- The halo ("outline") is gone; `[visual] outline` is ignored.
