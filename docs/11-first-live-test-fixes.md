# 11 — First live tests: what broke and why (2026-09-27)

The first runs of the native co-op build in the game (the user's four
launches, then my own runs driving the game by desktop control while the
user was away) reported: the settings panel text too dark to read; the
stage checkpoint doing nothing; P2 always the same character as P1; P2
moving but not shooting or focusing; P1 "spamming bombs" from the start of
the stage; the game crashing about seven seconds in; and the proximity
fade painting P2 black. Later, the rollback self-test found two real
snapshot problems. Everything below is fixed in the build noted, except
where marked open.

## Crash on the first bullet: hooks must preserve the caller's volatile registers

Windows logged all four crashes at the same spot: `th06nc.exe+0x6C130`, an
access violation. That is inside the game's player hit test
(`FUN_14006c090`, hooked), at

```
14006c0fb  CALL  FUN_14006bfd0      ; bomb clear-zone test (hooked)
...
14006c11d  TEST  R9B,R9B            ; the mode argument, still in R9
14006c122  MINSS XMM6,[R8]          ; the size pointer, still in R8
14006c130  SUBSS XMM1,[RDX+4]       ; the position pointer, still in RDX  <- faults
```

The hit test never reloads RDX, R8 and R9 after calling the clear-zone
test: `th06nc.exe` is built with whole-program optimization, so the
compiler knew that function leaves them alone and kept the caller's
arguments in them. My C++ replacement for the clear-zone test follows the
normal x64 convention and uses those registers freely. The first live
bullet (mode 1, the only path that re-reads RDX) took the game down —
seven seconds in, when stage 1's first bullets appear.

Every hook in the mod had the same exposure. Fix: every hook now enters
through a stub in `mod/hook_thunks.asm` (32 of them, assembled with
`ml64`). A stub saves RAX, RCX, RDX, R8–R11 and XMM0–XMM5, calls the C++
detour with identical arguments (the register arguments untouched, up to
eight stack arguments copied), then restores everything but the return
value — and RAX too for a hooked function that returns nothing
(`Hooks_Install(..., returnsValue=false)`, used for the sound flush).
`hooks.cpp` hands out the stubs; a hook's C++ code is unchanged.

Checked while at it: the direct callers of every other hooked function
(the bullet loop, the enemy update, the enemy-script callers, the
supervisor around the input poll, the sim step, the scheduler) reload
from memory or non-volatile registers after the call, so this was the only
clobber with a visible symptom. The stubs cover the callers not examined.
Confirmed fixed: no crash through several minutes of stage 2.

## "P1 spams bombs" and "P2 has no shoot/focus key": the game's second keyboard layout

Not a bug in the input plumbing. `th06nc` reads **two** keyboard layouts
for Player 1: arrows + Z/X/Shift, and **W/A/S/D + J (shoot) / K (bomb) /
L (focus)**, plus Q, R, Enter, Ctrl, Space, ';', PgUp/PgDn/Home/End and
the numpad as arrows (the poll `FUN_140013310` builds the mask from DxLib's
key table at `0xC6E080`, indexed by scan code; the mask bits: Z = `0x8101`,
X = `0x202`, Shift = `4`, Esc = `8`, arrows `0x10/0x20/0x40/0x80`, Enter
`0x100`, Q `0x800`, R `0x4000`; `0x8000` on Z is the replay fast-forward
bit). My default P2 keys were I/J/K/L for movement: every P2 move left,
down or right was also P1's shoot, bomb or focus. The log proved it — each
unprompted bomb arrived with K's exact bit pattern `0x202`.

P2's keys now default to **T/F/G/H to move, O shoot, P bomb, I focus**
(letters the game doesn't read); `Config_Load` warns when a configured P2
key is one the game binds, and the settings panel shows P2's keys. The
installed ini was updated in place (the installer keeps an existing ini).
Confirmed: P2 shoots and focuses with the new keys (log: `Diag: P2 input
001 -> ... shots in use 8`, `input 004 -> focused 1`) and P1 stays still.

## Checkpoint: stages are numbered from 0

`0x53CAD4` is 0-based: the supervisor's start-game path writes `0` for a
normal game and `6` for Extra (`14007bccb` / `14007bc9c`), stage practice
decrements the 1-based menu choice (`14007bcd3`), and the stage-6 special
case in the enemy code compares against `5`. The checkpoint code waited
for stage `1` and never fired. It now applies at index 0, writes
`start_stage - 1`, and logs every value it checks (stage index, replay,
practice, spell practice, continues used, score). Confirmed: `start_stage=2`
starts a new game in stage 2, with score 0 and continues 0 at scene init.

## Dark text: quads were drawn over sprites

`OverlayRenderer` batched flat quads and drew the whole batch at
`EndFrame`, while textured sprites (text) draw immediately. So the settings
panel's dark background quad was painted *over* its own text. The batch is
now flushed before every sprite, keeping call order. Also: the sprite
shader gained a color tint (the glyphs are white), the status text sits on
a dark backing box, and both the status text and the panel use one 16px
glyph cell per character at the 1456x816 window. Confirmed readable.

## Fade painting P2 black: no depth test in the sprite pipeline

The "dark outline" was four black copies of the sprite drawn offset behind
the faded body. A 20%-opaque body over black reads as black. Drawing the
copies *after* the body at a farther z (hoping the depth test would keep
only the fringe) also gave a black silhouette: the sprite pipeline
(`FUN_140003000`, the non-rotated path the player uses) has **no depth
test** — draw order is paint order, z is just copied into the vertices. A
dark outline cut out from under a translucent body would need a stencil
the game's batch doesn't offer.

Replaced with a **halo**: the sprite drawn once more, scaled 1.22x (VM
scale fields `+0xE4/+0xE8`), in the player's own color at up to 60%,
*before* the faded body. Confirmed: P2 next to P1 shows as a translucent,
blue-tinted ghost with a soft halo, Marisa's shape still readable. The
`outline` key keeps its name (1 = halo on); `outline_depth` is vestigial.

## P2's character

The different-loadout path was working all along: the user's last launch
had P2 = Marisa B with P1 = Marisa A, which share one sprite sheet, and P2
wasn't firing, so the B shot was invisible too. With P1 Reimu and P2
Marisa B the sheet swap is visible (own screenshot).

## Rollback self-test, round one: drawing consumed the game's RNG

`[synctest] enabled=1`, stage 2: 147 checks, 10 failed. One failure
touched the `rng` region: at frame 320, four frames after P1's bomb, the
RNG seed and call counter differed, as did the four screen-shake floats at
`0x53CAC0` and a shake counter. The bomb's screen shake takes random
offsets from the game's one RNG **during the draw pass**; re-simulated
frames don't draw, so their RNG ran four calls per frame behind the live
run. In netplay both machines would have drifted apart at every rollback.

Fix (`sim_control.cpp`): the RNG seed and counter are saved when a
simulation step ends and put back before the next one starts, so drawing
(and stalling) can't move the simulation's sequence. The shake still looks
random. Confirmed: the next run passed 60 checks with 0 failures up to the
first bomb.

The other failures were three bytes at GUI object `+0x3D4C`: the
`60.00fps` readout, a real-time float the supervisor task writes inside the
simulation, which stage-start calibration (stalled, nothing simulated)
can't see. Excluded explicitly (`Snapshot_MarkExtraVolatile`), and the
sync test now treats any byte of the mod-owned or GUI extra regions that
re-simulates differently as volatile from then on (they hold no
checksummed gameplay state).

## Rollback self-test, round two: heap pointers in .data (crash `+0xD162A`)

With the RNG fixed, the next failures after a bomb were pointer-sized:
`0x5FD760`, `0x5FD9C8`, `0x5FDC1C` held heap addresses that
re-simulation re-allocated (`...C7FE7B10` vs `...C8011850`). Restoring a
snapshot then put the *old* addresses back, and the game crashed ten
seconds later at `th06nc.exe+0xD162A` in `FUN_1400d0670`, dereferencing
that area. Every function that references `0x5FD000-0x5FE800` is library
code (DxLib's sound system, addresses `0x1400C1000+` / `0x14026xxxx`; the
game's own code ends near `0x1400A0000`) — it is the sound mixer's buffer
state, allocated as effects play. Exactly the "heap-resident state" risk
from docs/00, but in the library, not the game.

Fixes (untested — Steam refused the next launch because the user was
playing another game on a second PC, which a launch here would have
disconnected):

1. `{0x5FD000, 0x5FE800}` added to `kNeverRestore`.
2. At the end of calibration, every 8-byte slot of `.data` whose value is
   a committed address outside the module (`LooksLikeHeapPointer`, via
   `VirtualQuery`) is excluded from restores: the object behind it isn't
   in the snapshot, so restoring the address can only be stale.
3. The sync test excludes any slot that re-simulated to a *different*
   heap address (both values pointer-like), rebuilding the restore ranges.
4. `tools/ghidra_scripts/MapLibraryData.java` maps the runs of `.data`
   referenced only by library code, to denylist library state wholesale
   rather than one block at a time (results pending).

Open question: pointer slots that are still zero at stage start and get a
heap address later (the sound buffers on the first bomb) are caught only
by the sync test's learning or by an explicit denylist entry — in real
netplay the first rollback after such an allocation would still restore a
stale pointer unless the block is denylisted. The library-data map is the
answer to that.

## Also verified in these runs

- Separate resources: `P2 LIVES 3 BOMBS 3 POWER 0/128` status line; P1
  and P2 each downed when out of lives; both down → the game's continue
  screen; the user saw a revive complete.
- Invincible practice: three minutes in stage 2 without a death.
- Boss HP x1.00 / x2.00 read from the panel's saved value.
- The overlay logs the back buffer (1280x720, DXGI format 28 =
  R8G8B8A8_UNORM here).

## Still open

- The focus ring: P2's focused flag is confirmed but the ring itself
  wasn't observed (my screenshots came after the key was released).
- Continuing after both players are down: the prompt appears; what a
  continue does to both pools is unverified.
- Sync test on the pointer-safeguard build; then a full stage with bombs
  from both players.
- P1 bombs themselves show P1's own bomb; P2's bomb (P key) worked in the
  log but P2's bomb count in the status line wasn't checked.

## Rollback self-test: the rest of the way to 0 failures (later the same day)

Iterating with the user (short sync-test runs, one fix per run):

- **Heap pointers, second thought.** The calibration-time rule "never
  restore a heap pointer" stopped restoring the stage timeline's cursor
  (a pointer into the loaded ECL), so a rewound stage skipped the spawns
  its clock said were due. Dropped; library pointer blocks are denylisted
  by address instead, the sync test still learns slots that re-simulate
  to a different allocation, and the timeline cursor/jump target are
  never learned.
- **Sprite VMs drift cosmetically.** Animation scripts use the animation
  manager's own random source (a heap object), so VM fields re-simulate
  differently. P1's main VM, orb VMs and each shot's VM are excluded --
  the shot's VM is at slot `+0x18` (type at `+0x10`, logical position at
  `+0x13C`); an earlier `+8` excluded the in-use flag and lost every
  shot on rewind.
- **The screen shake is a heap task.** `FUN_140077890` allocates the
  shake's state and a priority-0xE task node on the heap and inserts it
  into the update list; its tick (`+0x77AB0`) writes the camera offsets
  from the game's RNG. Neither allocation can be restored, so the tick
  now runs on a private seed (`Detour_ScreenShakeTick`) and its outputs
  (`0x53CAC0`, `0x549F78`, `0xC6E388`) are never restored.
- **Library counters.** Sound-playback bookkeeping that a muted
  re-simulation skips: `0x555000-0x556200` (channel flags) and the
  library-only blocks `0x58E3BC-0x58E7E8`, `0x7E0098-0x7E3D6C`,
  `0x7E60B0-0x7FAACC`, `0x9881D8-0x9886E8` (from MapLibraryData).
- Diagnostics kept: full diff listings for the first failures that
  touch checksummed state; the bullet manager's address (`0x436EF0`,
  bullets at `+8`).

Result: a stage with bombs, shakes and kills, 0 failures. One unexplained
one-off remains from an earlier run: 34 bytes at `0x42F1B0` (floats at the
player's position, small counters) present live but not re-simulated,
eleven seconds after a bomb; referenced from no game code directly.
