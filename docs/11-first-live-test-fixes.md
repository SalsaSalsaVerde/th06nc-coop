# 11 — First live test: what broke and why (2026-09-27)

The first run of the native co-op build in the game (four launches, same
PC, keyboard) reported: the settings panel text too dark to read; the
stage checkpoint doing nothing; P2 always the same character as P1; P2
moving but not shooting or focusing; P1 "spamming bombs" from the start of
the stage; the game crashing about seven seconds in; and the proximity
fade painting P2 black.

## Crash: hooks must preserve the caller's volatile registers

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
from memory or non-volatile registers after the call, so the crash was the
only clobber with a visible symptom. The stubs cover the callers not
examined.

## Checkpoint: stages are numbered from 0

`0x53CAD4` is 0-based: the supervisor's start-game path writes `0` for a
normal game and `6` for Extra (`14007bccb` / `14007bc9c`), stage practice
decrements the 1-based menu choice (`14007bcd3`), and the stage-6 special
case in the enemy code compares against `5`. The checkpoint code waited
for stage `1` and so never fired. It now applies at index 0 and writes
`start_stage - 1`, and logs every value it checks (stage index, replay,
practice, spell practice, continues used, score) so a refusal names its
reason. The "fresh run" test also requires `0x549D2C` (continues used) to
be 0; where the score is reset for a new run is still unconfirmed, so if
the log says "the score isn't 0" on a fresh game, drop that condition.

## Dark text: quads were drawn over sprites

`OverlayRenderer` batches flat quads and drew the whole batch at
`EndFrame`, while textured sprites (text) draw immediately. So the settings
panel's dark background quad was painted *on top of* its own text —
"really dark and hard to see" was the text showing through an 85% black
box. (The old overlay mod never put a quad under text, so it never showed.)
The batch is now flushed before every sprite, keeping call order. Also:
the sprite shader gained a color tint (the glyphs are white, so text can
be colored), the status text sits on a dark backing box, and both the
status text and the panel use exactly one 16px glyph cell per character at
the 1456x816 window so nothing is resampled.

## Fade painting P2 black: the outline copies won the depth test

The four black outline copies were drawn at the sprite's own z (0.49);
the game's sprite pipeline depth-tests with "less", so the later, real
sprite lost against them and only the black copies showed. The copies now
sit 0.004 farther back. If the pipeline turns out to compare the other
way, this will look like no outline at all rather than black — then
negate the offset.

## P2's character

The last launch's log shows the different-loadout path did run: `Player2:
character MarisaB (P1 is MarisaA)`. Marisa A and Marisa B share one sprite
sheet, so P2 *looked* identical; the difference is the shot, which P2
wasn't firing (below). The earlier launches had no loadout line, i.e. the
same character and shot as P1 was selected. A real test of the sprite swap
needs P1 Reimu with P2 Marisa (or the reverse).

## P2 not shooting or focusing, and the bombs: still open

Nothing in the update code explains it: P2's update sees P2's input word
for the whole call (movement, which works, reads the same word), the focus
flag `+0x785C` is set from bit 4 of that word, and shooting is gated only
on bit 0 plus the GUI's dialogue timer and the per-player shot timer
`+0x40C`. The unprompted bombs are equally unexplained: a bomb needs a
rising edge of bit 1 in the input word, which only the poll (verified
returning the mask in EAX) and P2's temporary swap write.

Both may have been side effects of the register clobber above (corrupted
state before the crash). If they persist, this build logs enough to
pin them down:

- `Diag: P2 input ... -> state, shot timer, focused, bombing, shots in use`
  once a second while P2 holds shoot/focus/bomb (up to 12 per stage), plus
  P2's and P1's shot/bomb function pointers at spawn.
- `Diag: P%d started a bomb -- input now, last frame, bombs left, state,
  respawn timer` whenever either player's bombing flag goes 0 → 1 (up to
  20 per run).

## Also learned

- The input poll `FUN_140013310` composes the keyboard mask and tail-calls
  `FUN_140012f00` (pad merge), whose return value is the mask; the
  supervisor stores it (`14007b8b7`). Key bits: Z = `0x8101` (shoot,
  `0x100`, and `0x8000` = replay fast-forward), X = `0x202`, Shift = `4`,
  Esc = `8`, arrows `0x10/0x20/0x40/0x80`.
- The overlay renderer logs the back buffer size and DXGI format once
  (`Overlay: back buffer ...`) — useful if text ever looks dim on an HDR
  swap chain.
