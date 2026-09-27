# Netplay: lockstep and rollback (2026-09-27)

Implements gap-list items B1-B4, C1-C5 and D1, D4-D6 from docs/00. Built,
**not yet live-tested.** Every piece below compiles; the findings it rests
on come from decompiling the current binary this session.

## The frame structure it hooks

- `FUN_14003d8b0` is one **rendered** frame. It calls `FUN_14003d700` once
  (and during replay fast-forward up to 7 more times) and then walks the
  draw list (`0x53C050`) and presents.
- `FUN_14003d700` walks the **update list** (`0x53C090`) exactly once:
  **one simulation frame**. It ends by flushing the sound-effect queue
  (`FUN_1400781d0`).
- The priority-0 supervisor task on that list (`FUN_14007b7f0`) refreshes
  input: `previous = current; current = FUN_140013310();` — the poll that
  reads keyboard + XInput pad 0 and returns the mask. That poll has exactly
  one caller.

So the game already separates "simulate a frame" from "draw a frame", and
already simulates several frames per drawn frame (replay fast-forward) —
which is also the evidence that drawing never feeds back into simulation
state.

## What the mod does with it (`sim_control.cpp`)

| Hook | Purpose |
|---|---|
| `FUN_14003d700` (sim step) | Hand control to the netplay driver: step 0 times (stall — the game just redraws), once, or many times (re-simulation) per call |
| `FUN_140013310` (input poll) | During a forced step, return P1's synchronized input instead of reading devices |
| `FUN_1400781d0` (sound flush) | During re-simulated frames, clear the sound queue instead of playing it |
| `FUN_14007d8d0` (play BGM) | During re-simulated frames, don't (re)start music — boss themes are started mid-stage by stage scripts and dialogue |
| `FUN_14003c250` (gameplay scene init) | Seed the RNG identically on both machines before the stage init consumes it |

P2's input is forced through Player 2's input provider (docs/01), which
swaps it into the same global input words around P2's update.

## Roles

The lobby owner (host, F9 to create + invite) plays **P1** on both
machines; the guest plays **P2** on both machines. Each player uses their
game's normal controls; the mod routes local input to whichever player they
are. Both machines simulate both players.

## Session lifecycle (`netplay.cpp`)

1. **Offline / Idle** — menus are local. When a peer connects, both sides:
   reset the session counter, and switch the CRT math flag (`0xABAB9C`,
   `_set_FMA3_enable`) to 0. `sin`/`cos`/`atan2` and ~10 other CRT math
   functions branch on it between FMA3 and SSE2 code paths with different
   results; forcing SSE2 makes every x64 CPU agree.
2. **Gameplay scene init** (`FUN_14003c250`) consumes RNG before it even
   registers the players, and the two machines arrive with different RNG
   states (menus use RNG too). The hook seeds it from values both sides
   already share — a hash of both SteamIDs and the upcoming session number —
   so no message is needed and nothing can race.
3. **Barrier** — after the stage-start frame completes, each machine:
   - undoes that frame's player tick (it ran on each machine's own
     unsynchronized input) by restoring both players to their
     just-initialized copies, and zeroes the input words;
   - stalls (the sim doesn't step) and spends 30 frames **calibrating
     snapshots** (below);
   - sends READY (both players' loadouts, difficulty, stage, game build,
     post-init RNG, shared resources) and waits for the peer's. Each player
     picks their own character in their own menu; the guest's game adopts
     the host's selection for P1 before the stage init runs (docs/04). A
     mismatch means the games started differently → play the stage
     locally. Differing post-init RNG is logged as "stage init diverged"
     and the guest adopts the host's; the guest also adopts the host's
     lives/bombs/power/score/graze.
   - Times out after 2 minutes, or F10 leaves the lobby.
4. **Running** — every simulation frame both players' inputs drive the
   step. Local input is sampled once per simulated frame, scheduled
   `input_delay` frames ahead, and every packet carries the newest 32 local
   frames (loss up to 31 consecutive packets is invisible). The pause bit
   is stripped (a pause menu allocates a heap task — see "Limits").
   - **Lockstep**: frame N waits for the peer's input for N.
   - **Rollback**: frame N runs on a predicted remote input (the last
     confirmed one) as long as it's at most `max_rollback` frames past the
     last confirmed frame. Each frame's state is snapshotted before it
     steps. When the real input for frame N arrives and differs from the
     prediction, the next driver call restores N's snapshot and
     re-simulates up to the present with sound muted, before anything is
     drawn.
   - **Time sync** (rollback only): each packet carries the sender's
     "my frame minus your newest frame I have"; comparing both sides'
     values cancels out latency, and the side running ahead drops a frame
     occasionally.
   - **Desync detection**: every 60 frames, once every input up to that
     frame is confirmed, both sides exchange per-system checksums
     (`checksum.cpp`: RNG, P1, P2, bullets, entities, items, resources —
     gameplay fields only, since raw memory holds per-process heap
     pointers). A mismatch is logged with the system that diverged and
     shown on screen.
5. **Degraded** — peer silent for 5 s, left the lobby, loadout mismatch, or
   barrier timeout: the stage continues locally (the host keeps P1, the
   guest keeps P2, the other idles) until it ends.
6. **Stage teardown** ends the session; the next stage start is a new
   barrier. Resources (lives, score, power) carry over identically because
   the simulation was identical.

## Snapshots (`snapshot.cpp`)

- A snapshot is the **entire `.data` section** of `th06nc.exe` (9.03 MB
  virtual; every simulation pool is a static global there) plus P2's
  mod-owned state and the heap-allocated GUI object (`*0xABAE28`,
  `0x4228` bytes — dialogue state that gates bombing). About 1 ms to copy.
- Restore skips **volatile bytes**: anything that changed during the 30
  calibration frames at the barrier, when only drawing ran. Drawing can't
  affect simulation (replay fast-forward proves it), so those bytes are
  render/timing/audio state. Plus a fixed denylist (`Game::kNeverRestore`):
  both scheduler list heads, the BGM/sound-queue block
  (`0x5542B0-0x5542E8`), and the input poll's device-reading state
  (keyboard state buffer and key mapping `0xC6E078-0xC6E290`, gamepad
  focus-hold counter `0xABAD70`) — the simulation only sees the poll's
  returned mask, which is recorded.
- The sync test also keeps learning: bytes that change between the end of
  one frame's simulation and the start of the next (i.e. while only drawing
  and window/Steam housekeeping ran) are added to the volatile set as they
  appear — covers draw-time writes into bullets/effects that didn't exist
  yet during the stage-start calibration.
- Slots are allocated on first use (`max_rollback + 2` × 9 MB).
- **Task-list guard**: task nodes are heap objects, outside snapshots. Each
  frame records a signature of both scheduler lists; a rollback whose
  target frame had a different list is refused and logged ("unsafe
  rollback") instead of corrupting the lists.

## Single-machine sync test

`[synctest] enabled=1` (not connected to anyone): at every stage start the
mod calibrates, then every `interval` frames rolls back `distance` frames,
re-simulates them with the recorded inputs (P1 and local P2), and compares
the result **byte for byte** with the state it had just computed. Within one
process raw memory is comparable, so any difference names the exact address
that either isn't captured by snapshots or isn't deterministic. On screen:
`SYNCTEST: N CHECKS, M FAILED, K SKIPPED`. This is GGPO's SyncTest idea and
the fastest way to validate rollback before involving a second machine.

## Limits and known risks

- **Heap-resident simulation state** other than the GUI object would make
  rollback diverge. The sync test is designed to find it.
- **Mid-stage task creation** (pause menu, possibly dialogue/boss
  transitions) blocks rollback across that frame. Frequency unknown; the
  log reports every task-list change during a session.
- **BGM**: re-simulated frames don't restart music, but if a rollback
  changes *whether* a track starts (a boss dying a frame later on corrected
  input), the music follows the first, predicted timeline.
- **Replays** record only P1's input, so a replay saved after a netplay
  stage won't play back correctly.
- Lives/bombs/power are shared (docs/01).
- No reconnect; a dropped connection degrades to local play until the
  stage ends.

## Test plan

1. **Sync test, one machine**: set `[synctest] enabled=1`, play a stage
   with P2 idle, then with P2 moving/shooting/bombing. Expect
   `FAILED 0`. Any failure: the log lists differing addresses — send it.
2. **Lockstep, two machines**: `[netplay] mode=lockstep`. Host presses F9
   and invites; each picks a character (the host's difficulty is used),
   both start Stage 1. Expect "WAITING FOR PARTNER..." then play. Log
   should show `checksums match at frame 0` and no `DESYNC`.
3. **Rollback, two machines**: `mode=rollback`, `input_delay=2`. Same
   checks, plus the periodic stats line (rollbacks, unsafe rollbacks).
4. If step 2 desyncs: the checksum region named first is the lead. If it
   desyncs at frame 0, the stage init or barrier reset is the cause, not
   the netcode.
