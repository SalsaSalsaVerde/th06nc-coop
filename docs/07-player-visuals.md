# 07 — Player visuals: color, proximity fade, focus marker

Status: **live-tested** (docs/11, docs/13). All of it is cosmetic: applied
inside the draw calls and undone immediately, so the simulation, snapshots
and checksums never see it. Module: `mod/player_look.cpp`.

Settings (`[visual]`, each machine its own):

| Key | Default | Meaning |
|---|---|---|
| `color` | `FFFFFF` | tint for *your* player (P1 in same-machine play); sent to the partner online so they see you in it |
| `p2_color` | `A0C8FF` | same-machine play: P2's tint (online: the host's P2 before the guest's color arrives) |
| `proximity_fade` | 1 | the other player and their shots fade when close to you |
| `focus_ring` | 1 | the game's own focus marker on the other player while they hold focus |

(`outline` existed until 2026-09-27 — a halo behind the faded sprite; the
fade alone does the job, so it was removed and the key is ignored.)

## The sprite VM fields (new RE this pass)

The player draw (`FUN_14006b930`) draws, in order: the 80 player-shot VMs
(entries at `+0x420`, stride `0x170`, VM at `+8`, type 1 here / type 2 in the
overlay pass `FUN_14006bb40`), the bomb's own draw, then the **main VM**
(`+0x78C8`) and the **two option-orb VMs** (`+0x9F38`, `+0xA058`).

- The orbs were previously named "focus VMs" in `game.h`; they are the
  left/right options: the draw copies their positions from `+0x7880` /
  `+0x788C`, which the update sets to player x minus / plus an offset
  (smaller while focused). They're drawn only while `+0x79F8` is set and the
  state is 0 or 3. Renamed `kPlayerOptionVmL/R`.
- Each VM is drawn by `FUN_140006de0(unused, vm, 1)` → `FUN_140003c90`.
  From the latter (decompiled into `research/decompile/`): flags `+0xC4`
  (drawn only if bits 1 and 2 are set; `0x700` = blend bits), position
  `+0xC8` (float[3]; the draw adds the playfield origin `0x53CAC0` then
  restores), **color `+0xEC`** as a D3DCOLOR — bytes B, G, R, A, copied to
  all four vertices — and a color of 0 means "don't draw".
- Every player draw starts with `FUN_14007cae0(0xC6DB90, 0, 0.5, 0)`:
  flushes the sprite batch and sets the playfield viewport.
- There is **no hitbox sprite** anywhere in the player draws (true to
  EoSD). The focus flag is `+0x785C`, written every update from input bit 4.

## Color

`VmColorScope` multiplies the RGB of the main VM's and both orbs' color by
the player's tint (and the alpha by the fade), calls the original draw,
then restores the saved color. The game's own color changes (script-driven
flashes) survive because the saved value is put back. This is a multiply
tint, not a true hue rotation: white stays the tint color, dark stays dark.
Shots are not tinted.

Online, each player's `color` travels in the Loadout message (sent while in
the menus); the partner uses it for you. Before one arrives, the host's P2
is drawn with `p2_color`, the guest's P1 untinted.

## Proximity fade

Only the *other* player fades — online, the partner; same machine, P2 (P1
never fades). The overlay mod's formula (overlay/19, from th06_multi_net):
opacity 100% at ≥100 units apart, linearly down to **15%** at ≤50.

The player's shots fade with them: both player draw passes walk the 80
shot slots (`+0x410`, stride `0x170`, type at `+0x10`, VM at `+0x18`) and
draw each in-use slot's VM — type 1 shots in the main pass, type 2 in the
overlay pass. `ShotFadeScope` scales the alpha byte of every in-use slot's
VM color for the duration of the pass and puts the colors back; the RGB is
left alone (shots keep their own colors).

Why no outline: the sprite pipeline has no depth test, so a dark edge
can't be cut out from under a translucent body (docs/11); the halo that
replaced it was dropped once the fade was seen to work on its own.

## Focus marker

The game's own (docs/11 "focus marker"): the HUD keeps two sprite VMs and
runs scripts 0x641/0x642 on them over P1 while P1 holds focus. P2 gets a
copy of that pair, ticked and drawn after P2's overlay pass, with the
playfield view set first. The old Present-hook ring is gone.

## Downed players

The three player draws also skip a downed player (docs/06). These hooks
moved here from coop_rules.cpp (MinHook can only hook a function once).

## Test plan

1. Same machine, defaults: P2 is bluish. Walk P2 onto P1: P2 and P2's
   shots fade to ~15%, P1 unchanged. Hold P2's focus: the game's marker
   appears on P2 as it does on P1.
2. `color=FF8080` on the host, online: the guest sees the host reddish;
   neither sees their own player faded.
3. `proximity_fade=0`, `focus_ring=0` each switch their part off.
