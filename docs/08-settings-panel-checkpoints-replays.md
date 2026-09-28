# 08 — Settings panel ("lobby"), checkpoints, replays

Status: **built, not live-tested.**

## Settings panel (the spec's lobby settings)

The Steam lobby itself is unchanged (F9 create + invite, F10 leave —
docs/02). What the spec's "lobby where the host sets the settings" adds is
an in-game screen: **F8 in the game's menus** (never during a stage) opens a
panel over the game (`mod/settings_panel.cpp`):

| Item | Who can change it |
|---|---|
| Start the run at stage N (checkpoint) | host / alone |
| Boss HP multiplier (0.25-8, steps of 0.25) | host / alone |
| Invincible practice | host / alone |
| Enemies aim at: nearest / always P1 / alternate | host / alone |
| Lives/bombs/power: shared / per player | host / alone |
| Revive downed after (0 = never, 5-120 s) | host / alone, per-player resources only |
| P1 (or shared) start lives / bombs | host / alone |
| P2 start lives / bombs | host / alone, per-player resources only |
| Start power (game's, 0-128 in 16s) | host / alone |
| Your color (preset list) | everyone — online it's sent to the partner |
| P2 color, P2 on this PC, P2 character | same-machine play only |

Online the guest sees the host's rules greyed out: the host's settings are
already sent continuously in the Loadout message while both are in the
menus (docs/02), so a change on the host shows up on the guest within ten
frames. Each player still picks their own character in the game's own
menu.

**Controls** are the game's own input bits (keyboard or controller):
up/down select, left/right (or shoot) change, bomb / Esc / F8 close. The
panel hooks into the input poll (`sim_control.cpp`): while it's open the
game receives no input, so its menu doesn't move underneath; after closing,
input stays swallowed until every button is released (so the closing
button doesn't also act as "back" in the game menu).

**Closing saves** everything to `th06nc_native_coop.ini` with
`WritePrivateProfileString` (keys as in the example ini), so the next launch
starts with the same choices.

`coop_rules` now separates *own* settings (ini + panel) from the settings
*in effect* (the host's, when playing as a guest). A guest who disconnects
goes back to its own — at the end of the stage if it was mid-stage, since
switching shared/separate resources mid-stage would break the pools.

## Checkpoints

Built: **stage checkpoints** (`start_stage`, 1-6). At the entry of the
gameplay scene init (`FUN_14003c250`, already hooked for the netplay seed),
a fresh co-op run — stage 1, score 0 (the scene init never resets the score,
and a continue sets it to the continue count), not a replay, not stage or
spell practice — has its stage number (`0x53CAD4`, 1-based: the same
function loads `eff06.anm` for 6, `eff07.anm` for 7 = Extra) replaced by the
checkpoint. Both netplay machines do this with the host's setting, so the
READY stage check still matches. `start_power` pairs with it (starting
stage 5 at 0 power is rough).

Solo runs (not connected, local 2-player off) use the checkpoint stage and
start point too, as a practice tool; the starting stock and power don't
apply there, since they're set when Player 2 spawns.

The flags that tell the run modes apart (new RE, docs in `game.h`):

| Global | Set by | Meaning |
|---|---|---|
| `0x53D3DC` | `FUN_14004ede0` = 1, `FUN_14004f320` = 0 | replay playing back (also gates replay fast-forward, key `0x8000`, in `FUN_14003d8b0`) |
| `0x53D404` | `FUN_14004f320` from the menu | stage practice (lives/bombs forced to 2/3 like Extra) |
| `0x53D405` | `FUN_140053ad0` | spell practice (stage and difficulty from the chosen spell card) |
| `0x53D414` | option `0xC6DFDB` / replay header byte 6 == 1 | a mode where deaths don't cost lives and power never drops below 8 (probably New Classic's training option) |
| `0xC6DFD4/5` | options menu | the game's starting lives / bombs options |

Midboss and boss start points came later (docs/12). Not built: **individual stage-spell checkpoints** (e.g. stage 4's books).
Those need jumping the stage's enemy timeline (ECL) and background scroll
to a point mid-stage, which is new RE. Two leads for whoever picks it up:

- New Classic has **spell practice** (`FUN_140053ad0`: spell id < 0x86,
  table at `0xC6E3E0`, 0x28-byte entries, byte 1 = stage, byte 4 =
  difficulty; sets `0x53D405` and `0xC6E358`). Whatever the stage code does
  with that id to skip straight to a boss spell is exactly the machinery a
  boss-spell checkpoint needs; finding where `0x53D405`/`0xC6E358` are read
  in the enemy/ECL code is the first step.
- For stage portions and midbosses, the ECL timeline's time/instruction
  pointer in the enemy manager, fast-forwarded without spawning, plus the
  background (`std`) position.

## Replays

- **Replay playback never gets a second player** (`Detour_RegisterPlayer`
  checks `0x53D3DC`): a replay only has P1's input, and P2 would change
  what happens. Netplay's scene-init seeding and selection swap are skipped
  too, so a replay plays back with its own seed even while connected.
- **Saving a replay of a co-op stage still works but it won't play back
  correctly** (P2 isn't in it). Not blocked: the save prompt is the game's,
  and a solo-looking replay of a co-op run is harmless. Checkpoint runs
  likewise record as if from stage 1's start.

## Test plan

1. In the title menu, F8: the panel appears, the menu cursor doesn't move
   while navigating it; Esc closes without also backing out of the menu;
   `th06nc_native_coop.ini` now has the values.
2. Online: host changes boss HP in the panel; the guest's panel shows it
   greyed out within a second; the guest can change only its color, and
   the host sees the guest's P2 in that color at the next stage.
3. `start_stage=4`, start a normal game with P2 on: the run starts at stage
   4. Stage practice and replays are unaffected.
4. Watch a replay with `[player2] enabled=1`: no P2 appears; the replay
   plays out exactly as recorded.
