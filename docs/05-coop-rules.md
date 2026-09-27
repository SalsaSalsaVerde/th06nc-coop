# Co-op run settings: boss HP, invincible practice, targeting (2026-09-27)

Covers the original spec's lobby settings that change the simulation.
Built, **not yet live-tested.** Settings live in `[coop]` in
`th06nc_native_coop.ini`; online, the host's values are used by both
machines (sent with the host's menu selection, adopted by the guest,
verified in the stage-start handshake; the guest goes back to its own after
disconnecting).

All three run **inside the simulation**, so both netplay machines apply them
on the same frame, and re-simulated frames reproduce them. Their small
state lives in a region registered with snapshots (`coop_rules.cpp`).

## Boss HP multiplier (default 2x)

With two real players both damaging the boss natively, a boss dies about
twice as fast. After each frame's enemy update (`FUN_140038cd0`, hooked),
the tracked boss (entity flags `+0xBD` bit `0x08`) is checked: the first
frame its max HP (`+0x238`) holds a value this code didn't produce — a new
non-spell or spell-card phase — current HP (`+0x234`), max HP, the phase
boundary (`+0xA0`) and the phase max (`+0x23C`) are all multiplied
(overlay/55's approach). Only while P2 is in the stage.

## Invincible practice

Every native player-death path (bullet/enemy contact `FUN_14006c090`, laser
`FUN_14006c2b0`) only kills a player whose state is 0; in state 3
(bombing), touching bullets are cancelled instead. With `invincible=1`, a
normal-state player is presented as state 3 for the duration of each of
those checks and put back afterwards. Bullets that touch a player vanish;
nobody dies. (Side effect inherited from the native bombing behavior:
touching an enemy damages it slightly.)

## Targeting

- `nearest` (default): each enemy aims at the nearer living player
  (docs/03).
- `host`: enemies always aim at P1.
- `alternate`: enemies in odd entity-table slots aim at P2, even at P1
  (deterministic, so both machines agree).

A dead/respawning player is never targeted.

## Built since (elsewhere)

- Separate lives/bombs/power per player, per-player starting stock and the
  revive timer — docs/06.
- Player colors, the other player's proximity translucency/outline and
  focus ring — docs/07.

## Not built yet

- Midboss / stage-spell checkpoints (stage checkpoints: docs/08).
