# 07 — Player visuals: color, proximity fade + outline, focus ring

Status: **built, not live-tested.** All of it is cosmetic: applied inside the
draw calls (or the Present hook) and undone immediately, so the simulation,
snapshots and checksums never see it. Module: `mod/player_look.cpp`.

Settings (`[visual]`, each machine its own):

| Key | Default | Meaning |
|---|---|---|
| `color` | `FFFFFF` | tint for *your* player (P1 in same-machine play); sent to the partner online so they see you in it |
| `p2_color` | `A0C8FF` | same-machine play: P2's tint (online: the host's P2 before the guest's color arrives) |
| `proximity_fade` | 1 | the other player fades when close to you |
| `outline` | 1 | dark outline around the other player while faded |
| `focus_ring` | 1 | ring + hitbox on the other player while they hold focus |

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

## Proximity fade and outline

Only the *other* player fades — online, the partner; same machine, P2 (P1
never fades). The overlay mod's formula (overlay/19, from th06_multi_net):
opacity 100% at ≥100 units apart, linearly down to 20% at ≤50.

Outline: before the faded draw, the main VM is drawn four times offset by
±1.5 units in pure black (the VM color multiplies the texture, so black
keeps only the sprite's shape) at up to 75% alpha, scaling with the fade.
The faded sprite is then drawn on top — so the outline shows *through* the
translucent body too (a darker body with a crisp edge rather than a pure
ring; a true ring would need a stencil or shader). Positioned exactly like
the draw positions the main VM (player pos, z 0.49), with the playfield
view set first.

## Focus ring

Drawn in the Present hook (status overlay) because the game has nothing to
reuse: overlay/39's recipe — two dotted rings converging from 1.5x and
0.3x to 1x over 18 frames while fading in over 6 — plus a white square the
size of the real hit radius (`+0x774C`, not the old 3-unit placeholder) on a
dark border. Shown for the other player online, and for both players in
same-machine play. Game units map to the window with overlay/14's measured
playfield fractions (x 400-1060 / 1456, y 20-790 / 816 for 384x448 units),
verified live in the overlay mod on a 1456x816 window — likely wrong on
other aspect ratios / fullscreen letterboxing.

## Downed players

The three player draws also skip a downed player (docs/06). These hooks
moved here from coop_rules.cpp (MinHook can only hook a function once).

## Untested / risks

- Whether the tint reads well on the actual sprites (the defaults are a
  guess); whether the blend mode of the orbs is additive (then the fade
  still works, the tint less so).
- The outline draws call the game's sprite draw outside its usual order;
  if the sprite batch misbehaves (wrong texture, flicker), set `outline=0`.
- Focus-ring placement on non-1456x816 windows.

## Test plan

1. Same machine, defaults: P2 is bluish. Walk P2 onto P1: P2 fades to ~20%
   with a dark edge, P1 unchanged. Hold P2's focus: ring + hitbox on P2;
   hold P1's focus: ring on P1 too.
2. `color=FF8080` on the host, online: the guest sees the host reddish;
   neither sees their own player faded.
3. `outline=0`, `proximity_fade=0`, `focus_ring=0` each switch their part
   off.
