# Expanded scope: native second player + rollback netplay — gap analysis (2026-09-27)

This project is the successor to `E:\Ai Coding Projects\Touhou Coop Mod`
(referred to below as **the overlay mod**). The overlay mod's docs 00-61
are the research record this project builds on; doc numbers below prefixed
`overlay/` refer to that repo's `docs/`.

## Goal

A co-op mod for `th06nc.exe` (Touhou Koumakyou: New Classic) where:

1. The second player is a **real player simulated by the game's own
   engine** — native sprite, shots, bombs, hitbox, graze, deaths, item
   collection — not a sprite drawn over the frame.
2. Everything the overlay mod had to fake or hand-synchronize (bomb bullet
   clears, enemy kills, boss HP, enemy targeting, deaths) instead falls out
   of both clients running **the same deterministic simulation** from the
   same inputs.
3. Netplay uses **rollback**: each client simulates ahead with predicted
   remote input, and re-simulates from a saved state when the real input
   arrives and differs.

## Where the overlay mod stands (what this replaces)

The overlay mod runs two *independent* simulations and papers over the
differences (overlay/00 "Netcode architecture decision"):

| Feature | Overlay mod | Native + rollback |
|---|---|---|
| Remote player | Sprite drawn in `Present` hook | Second native player instance |
| Remote shots | Cosmetic particles, damage reported separately | Native shots, native damage |
| Enemy kills | Per-slot damage reports + position fallback (overlay/25) | Identical sim on both sides |
| Boss HP | Host-authoritative writes + gating (overlay/24, 41, 47, 48) | Identical sim |
| Bomb clears | Networked effect points + our own overlap test (overlay/61) | Native clear zones |
| Enemy aim | Hook redirecting aim target (overlay/28) | Native aim at nearest/chosen player, same on both sides |
| RNG | Host seed force-written every frame (overlay/32) | Same seed + same inputs = same RNG |
| Stage sync | Hold gate at stage boundaries (overlay/46, 53) | Lockstep/rollback keeps frames aligned by construction |
| Deaths | Edge-detected lives counter (overlay/33) | Native, identical |

Almost all of the overlay mod's gameplay-sync code is scaffolding around the
fact that the two games disagree. With one shared simulation, that whole
category disappears. What carries over directly: the DLL-proxy injection,
MinHook setup, Steam lobby and Steam Networking Messages transport
(overlay/15, 26), the installer, `mem_scan.py`, the Ghidra scripts, the
PKGL/ANM asset tooling, and every address/struct finding.

## The finding that makes a native second player tractable

overlay/23 considered this approach and set it aside: th06_multi_net (the
original-EoSD co-op mod) gets its second player by adding a `g_Player2` to
the *source*, and doing the equivalent to a closed binary looked like
patching every one of the player's hundreds of references.

Measured properly this session (new `tools/ghidra_scripts/FindRefsInRange.java`
over the whole player struct, RVA `0x549FF0`, size `0xA2B8`):

- **The player's own code never uses the player's absolute address.** The
  player is registered as four scheduler task nodes (`FUN_1400694d0`, the
  equivalent of th06's `Player::RegisterChain`) — one update
  (`FUN_140069f30`, calc list, priority 7) and three draw passes
  (`FUN_14006b850` p7, `FUN_14006b930` p9, `FUN_14006bb40` p11) — and every
  one receives the struct as its context argument (`node+0x38`). Movement,
  shooting (the player's own 80 shot slots live *inside* the struct at
  `+0x410`, stride `0x170`), bombs (per-character function pointers at
  `+0x9ED0`/`+0x9ED8`), death/respawn, and drawing are all
  instance-relative. A second struct with its own four task nodes is a
  second native player.
- **Outside the player's own code, 603 references in 25 functions touch the
  struct by absolute address**, and they collapse into a short list:
  - ~500 are just **reads of player position** (`+0x7730/0x7734/0x7738`)
    for aiming — the stage-script VM (`FUN_140021f10`, 268) and a dozen
    enemy bullet-pattern functions. These want "the targeted player's
    position", which can be provided per enemy without patching each read
    (see "Aim" below).
  - **Bullets** (`FUN_140010fa0`, 10 refs), **lasers** (`FUN_14006c2b0`,
    17), **enemy contact + shots-vs-enemy** (`FUN_140038cd0`, 5),
    **items** (`FUN_140044240`, 10). These are the actual player-interaction
    points and each needs a "for both players" treatment.
  - The player's own registration/teardown and a few HUD/state readers.
- **Three interaction functions already take the player as a parameter**,
  so they work on a second instance unmodified — they only need to be
  *called* for it:
  - `FUN_14006c090(player, pos, size, mode)` — bullet/enemy touches player
    (hit test; triggers the native death sequence on that instance).
  - `FUN_14006b420(player, enemyPos, enemySize, outFlag)` — that player's
    shots/options damage an enemy (returns damage).
  - `FUN_14006bfd0(player, minX, minY, maxX, maxY)` — **the native bomb
    bullet-clear test**: does a bullet's box overlap any of that player's 16
    clear zones at `+0x7754` (`{w, h, cx, cy}`, stride `0x10`)? This is the
    mechanism overlay/61 was looking for and didn't find. Each character's
    bomb function just keeps these zones positioned (ReimuA: 48×48 per
    amulet, growing to 256 on pop; MarisaA: 128×128 per star; ReimuB: a
    62-wide pillar and a 384×62 band; MarisaB: a full-width band). Bullets
    inside a zone become point items via the bullet loop's own code.

So a native second player is: second struct + second set of task nodes +
input for it + "also do this for P2" at roughly five interaction sites.
That is a few hundred lines of hooks, not a binary rewrite.

**Side finding for the overlay mod:** overlay/61's `GetLocalBombEffectPoints`
reads the wrong arrays (the damage-dealing attack-object array and a
per-character motion array, with a guessed 20.0 radius). The correct source
is the clear-zone array above. Worth a small follow-up fix there
independent of this project.

### Constraints found on the way

- **Don't run the native player init/teardown for P2.** The init callback
  (`FUN_140069870`) reloads `data/player00.anm` into sprite slot 5, which
  first *unloads* it — P1's sprites would be pulled out from under it. The
  teardown callback (`FUN_140069f00`) unloads slot 5. P2 is therefore created
  by copying P1's struct right after P1's native init, and P2's task nodes
  carry no added/deleted callbacks.
- **P2 as a different character needs its own sprite slot.** Same-character
  P2 shares slot 5 for free. A Reimu+Marisa pair needs the other
  character's `.anm` loaded into a free slot and P2's animation VMs pointed
  at it — th06_multi_net does the same with `player00b.anm`. Deferred.
- **The bullet loop only hit-tests bullets that have already grazed P1.**
  The graze check is inline code reading P1's absolute position, and the hit
  test (`FUN_14006c090`) is gated behind the per-bullet "grazed" flag. P2
  therefore needs its own pass over the bullet array after the native update
  (hit test on every live bullet), not just a hooked hit test.
- **Input is one global** (`0xABAE80` current, `0xABAE84` previous, classic
  bit layout: shoot 1, bomb 2, focus 4, menu 8, up 0x10, down 0x20, left
  0x40, right 0x80). P2's update runs with P2's input swapped into those two
  words and restored afterwards — no patching of the input readers needed.
- **Per-player resources are globals.** Lives (`0x549D40`), bombs
  (`0x549D41`), power (`0x53CAD8`), graze, score, character/shot type
  (`0x53CAD0/1`). With the swap approach P2 shares them until they're also
  swapped around P2's update (and around P2-attributable events like item
  pickup). th06_multi_net keeps separate lives/bombs/power per player.

## Rollback: what it needs on top of a native second player

Rollback netcode (GGPO model) needs, in order of risk:

1. **Deterministic simulation from (initial state, per-frame inputs).**
   Already confirmed for single-player by the replay system (overlay/06, 07:
   replays are a seed plus a run-length input log and re-simulate exactly).
   Two new determinism risks:
   - **CRT math dispatch.** The MSVC x64 CRT picks FMA3 code paths for
     `sin`/`cos`/`atan2`/etc. at runtime based on the CPU. Two machines with
     different CPUs can compute different bullet angles from the same inputs.
     Must be forced to one path on both sides (the CRT's `_set_FMA3_enable(0)`
     equivalent inside the game's own CRT instance). Needs checking against
     the binary.
   - **Anything wall-clock- or frame-rate-dependent in the sim.** None found
     so far (boss timers are frame counters, overlay/18).
2. **Input is the only thing crossing the network.** Each frame, both
   players' input masks are fed to the sim. Local input is read by the game's
   own poll; it must be intercepted and replaced by the frame's
   synchronized/predicted input for both P1 and P2.
3. **Save and restore the full simulation state, every frame.** Known state
   blocks (all static globals, not heap): player structs (`0xA2B8` each),
   bullet manager (`0x436EF0`, `0xF5010`), item pool (`0xBFB2F8`, 1024 ×
   `0x160`), entity table (256 × `0x10B0`), scene buffer (`0xC53460`,
   `0x1A728`), RNG (`0xABAE64`), boss/stage/score globals, effect
   pools. Practical approach: snapshot every writable section of the
   `th06nc.exe` image each frame (~9 MB per overlay/11; a memcpy of that is
   roughly 1-3 ms), minus a denylist of non-simulation state (D3D objects,
   audio, Steam, file-IO queues, the task-list nodes themselves if they
   aren't stable). Anything the sim keeps on the heap has to be found and
   added. This is the biggest RE unknown.
4. **Advance one simulation frame on demand, without rendering.** The
   scheduler has separate lists (`0x53C090` and `0x53C050`); the player
   registration puts its update on the first and draw passes on the second,
   so resimulation = run only the update list N times. The main-loop
   dispatcher overlay/54 found (`FUN_14003d8b0`) walks `0x53C050`, which
   needs reconciling with that before relying on it.
5. **Suppress side effects during resimulated frames.** Sound effects
   (queue at `0x5542C0`), Steam achievements (fired from inside the player
   and scene code), replay recording, file writes. Visual effects are part
   of sim state and roll back with it.
6. **Transport and session management.** Steam Networking Messages (already
   working in the overlay mod), input packets carrying a short redundant
   window of recent frames, frame advantage / time sync to keep both sides
   running at the same speed, desync detection via periodic state checksums.

**Lockstep first.** A delay-based lockstep mode (th06_multi_net's model:
wait for the peer's input for frame N before simulating frame N) needs items
1, 2 and 6 but not 3-5. It is the natural stepping stone: it proves
determinism across two machines with the native second player, and it's a
usable netplay mode on its own for low-latency pairs. Rollback then adds
save/restore/resimulate on top of the same input pipeline.

## Alternative considered: build on the original-EoSD decompilation

`cardanawandra/th06` (compilable reconstructed source for original EoSD
1.02h) plus th06_multi_net already has a working native second player and
delay-based netplay; adding rollback to source code is far easier than to a
closed binary. **Not chosen** as the main path because it is a different
game from the user's target: the 2002 engine and art, not New Classic, and
it needs original 1.02h data files. It remains the best *reference* for
struct layouts, the second-player duplication checklist (its `g_Player2`
touch points: 21 in Player, 12 in enemy scripts, 10 in items, 7 in bullets,
4 in enemies), and resource/revive rules.

## Gap list

Legend: **done** = implemented and compiles; **built** = implemented, not
live-tested; **RE** = needs reverse engineering first; **design** = needs a
decision.

### A. Native second player (local, one machine)
| # | Item | Status |
|---|---|---|
| A1 | P2 struct + task nodes registered alongside P1, torn down with it | built (docs/01) |
| A2 | P2 input from a second device, swapped in around P2's update | built (docs/01) |
| A3 | Bomb clear zones: P2's zones clear bullets (hook `FUN_14006bfd0`) | built (docs/01) |
| A4 | Bullets hit P2 (post-pass over bullet array using native hit test) | built (docs/01) |
| A5 | Enemy contact hits P2 (hook `FUN_14006c090`) | built (docs/01) |
| A6 | P2 shots damage enemies/boss (hook `FUN_14006b420`) + homing target | built (docs/01) |
| A7 | Lasers hit/graze P2 (`FUN_14006c2b0` run as P2) | built (docs/03) |
| A8 | Items: P2 collects, auto-collect above the line / on bomb | built (docs/03) |
| A9 | Aim: enemies aim at the nearest living player | built (docs/03); mid-flight re-aim still P1 |
| A10 | P2 graze (score + graze counter + sound) | built (docs/03) |
| A11 | Separate per-player lives/bombs/power | built (docs/06), optional; shared by default |
| A12 | P2 as a different character (own sprite slot, ID-table swap) + online loadout sync | built (docs/04) |
| A13 | HUD for P2's resources | built as a status-text line (docs/06) |
| A14 | Revive / spirit mode / out-of-lives rules | built: downed + revive timer (docs/06) |
| A15 | Proximity translucency + outline, focus hitbox for the other player, per-player color | built (docs/07) |

### B. Deterministic input pipeline
| # | Item | Status |
|---|---|---|
| B1 | Build fingerprint check (refuse to hook an unknown binary) | done |
| B2 | Intercept the game's input poll; feed P1/P2 masks per frame | built (docs/02: poll `FUN_140013310` has one caller) |
| B3 | Force deterministic CRT math path | built (docs/02: `_set_FMA3_enable` flag at `0xABAB9C`) |
| B4 | Seed agreement at stage start | built (docs/02: both derive it, scene-init hook) |
| B5 | Replays vs. co-op | replay playback never spawns P2 or reseeds (docs/08); saved co-op replays still won't play back right |

### C. Lockstep netplay
| # | Item | Status |
|---|---|---|
| C1 | Transport (Steam Networking Messages) + lobby | built (ported) |
| C2 | Input exchange with redundancy, fixed input delay | built |
| C3 | Hold the frame until the peer's input for it arrives | built (hook the sim step `FUN_14003d700`) |
| C4 | Menus/stage start synchronized | built (menus local, barrier at every stage start) |
| C5 | Desync detection (state checksum exchange) | built |

### D. Rollback
| # | Item | Status |
|---|---|---|
| D1 | State snapshot/restore of the image's writable sections | built |
| D2 | Denylist of non-sim state within those sections | built (self-calibrating at the barrier + fixed list) |
| D3 | Heap-resident sim state (if any) | GUI object covered; the sync test hunts for the rest |
| D4 | Run N update-only frames without rendering | built (the game's own sim step, as replay fast-forward uses it) |
| D5 | Side-effect suppression during resimulation | built: sound effects and BGM starts |
| D6 | Prediction, rollback, time sync | built |
| D7 | Single-machine sync test | built |

### E. The original spec (`TouhouCoopModGuidance.txt`)
| Spec item | Status |
|---|---|
| Lobby where the host sets the settings | built: Steam lobby (F9/F10) + in-game settings panel (F8, docs/08); host's rules sent to the guest |
| Rollback netcode | built (docs/02), lockstep fallback |
| Difficulty | the host's menu choice, adopted by the guest (docs/02) |
| Character selection per player | built (docs/04) |
| Starting lives/bombs per player; shared lives/bombs toggle | built (docs/06) |
| Invincible practice | built (docs/05) |
| Revive timer | built (docs/06) |
| Boss HP multiplier (default 2x) | built (docs/05) |
| Targeting mode | built (docs/05) |
| Hue color per player | built as a multiply tint (docs/07) |
| Other player translucent + outlined near you | built (docs/07) |
| Showing the other player's focus | built: focus ring + hitbox (docs/07); the native orbs also close in |
| Checkpoints | stage checkpoints built and tested (docs/08); midboss and boss start points built, untested (docs/12); individual stage spells not built |

## Milestone order

1. **Local native P2** (A1-A6 first, then A7-A10). Testable on one machine
   with two controllers or a shared keyboard. Proves the approach before any
   netcode exists.
2. **Input pipeline** (B1-B5). Record-and-replay both players' inputs
   locally and check the result is identical run to run; that's the
   determinism test without needing a second machine.
3. **Lockstep netplay** (C1-C5). First two-machine milestone.
4. **Rollback** (D1-D6), with lockstep kept as a fallback mode.
5. Rules and polish (A11-A15).

## Biggest risks

- **Hidden absolute references in code paths the range scan can't see**
  (jump-table code Ghidra didn't define as functions — the scan found 89
  such references). Mitigation: A1-A6 are testable locally in minutes;
  crashes or P2 behaving like P1 will show up immediately.
- **Heap-resident simulation state** would break rollback silently (desyncs
  after the first rollback). Mitigation: lockstep first, plus state
  checksums from day one so a desync is detected and reported, not
  guessed at.
- **CRT FMA dispatch** could make two different PCs disagree even in
  lockstep. Mitigation: checksum exchange detects it; fix is forcing one code
  path.
- **Game updates** move every address. Mitigation: B1 refuses to install
  hooks on an unrecognized build.
