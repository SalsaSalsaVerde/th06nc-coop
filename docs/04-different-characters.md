# Player 2 as a different character, and loadout sync online (2026-09-27)

Implements gap-list item A12 from docs/00. Built, **not yet live-tested.**

## How the game loads player sprites

The sprite/animation manager (`*0xABAC30`, th06's `AnmManager`) has 128
file slots. Loading an `.anm` file (`FUN_140002440(_, slot, path, base)`):

- stores the file pointer at `+0x8 + slot*8` and the base ID at
  `+0x408 + slot*4`;
- writes the **slot number into the file's header** and creates the
  file's texture at `+0x20620 + slot*8` — textures are per slot;
- registers every sprite at `+0x608 + (base + localIndex)*0x40` (each entry
  records its slot's texture) and every script pointer at
  `+0x23090 + (base + localIndex)*8` (plus an int at `+0x20F90 + id*4`).

The player init loads `data/player00.anm` (Reimu) or `player01.anm`
(Marisa) into slot 5 at base `0x420`, then points its animation VMs at
scripts `0x420` (body), `0x4A0`, `0x4A1` (focus indicator) with
`FUN_140002b40(_, vm, scriptId)`. Shots and bombs set their VMs the same way,
by ID. The unloader (`FUN_140002be0`) clears every ID the file registered.

So the two sheets can be loaded at the same time (separate slots and
textures); they only collide in the shared ID tables.

## What the mod does

At P2 spawn, if P2's wanted character differs from P1's:

1. Capture the ID-table window `0x400-0x600`, load the other sheet into the
   highest free slot at base `0x420`, and read back which IDs each sheet
   registered (enumerated exactly like the game's own unloader does).
2. Capture P2's entries for the union of both sheets' IDs (zeroing IDs only
   P1's sheet has), then restore the window, so P1's entries are exactly as
   before.
3. Apply the per-character player fields the native init sets, to P2's
   copy: the `0x20`-byte stats record from `0x429000 + combo*0x20` (speeds
   and the two shot functions) into `+0x7860`, diagonal speeds recomputed
   the same way (`/ sqrtf(*0x352AD0)`), current shot functions into
   `+0x7720`, bomb functions from `0x426EB0 + combo*0x10` into `+0x9ED0`
   (`combo = character*2 + shot`).
4. Restart P2's three animation VMs on their scripts with P2's entries
   applied.

Every one of P2's own callbacks (the update and all three draw passes) runs
inside a scope that applies P2's ID-table entries and sets the
character/shot-type globals to P2's, and restores P1's on the way out. A
different shot type with the same character only swaps the globals. At
despawn the extra sheet is unloaded (restoring the ID window around the
unload, since it clears IDs P1 shares).

Local play: `[player2] character=same|reimu_a|reimu_b|marisa_a|marisa_b`.

## Online: each player picks their own character

- While both are in the menus, each side sends its current selection
  (character, shot type, difficulty) ten times a second.
- At gameplay scene init: the **host** gives P2 the guest's selection; the
  **guest's game adopts the host's selection** for P1 and difficulty (the
  player init reads those globals) and gives P2 its own original selection.
  The guest's own selection is written back when the stage ends, so its
  menus show what it picked.
- The READY handshake now carries both players' loadouts, difficulty and
  stage; any difference means the two games started differently (e.g. one
  picked a different selection at the last moment, or a different stage)
  and that stage is played locally.
- READY also carries lives, bombs, power, score and graze; the guest adopts
  the host's. The two games' "starting lives" options may differ, and
  resources carry over between stages.

## Known gaps

- Effects spawned by P2 that draw with player-sheet sprites outside P2's
  own callbacks (if any) would show P1's sheet.
- A different stage selection (e.g. practice mode on another stage) isn't
  synchronized — the handshake just refuses it.
