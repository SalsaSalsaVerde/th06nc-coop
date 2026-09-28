# 06 — Separate lives/bombs/power, downed and revive

Status: **built, played online** (docs/13-15). The default since
2026-09-28 (`[coop] shared_resources=0`); shared was the default before.

## Modes

- `shared_resources=1`: one pool, exactly the game's own
  globals. Either player dying spends a shared life; either bombing spends a
  shared bomb; items feed the shared power. Last life lost by anyone = game
  over. Nothing below applies.
- `shared_resources=0` (default): each player has their own lives, bombs and power.
  P1's live in the game's globals (so the game's HUD shows P1's). P2's live
  in `Player2State::resources` and are shown in the status text, top left:
  `P2  LIVES n  BOMBS n  POWER n/128`.

Score and graze stay shared in both modes (one HUD, one hiscore).

## How the pools are kept apart

Everything the game does with lives/bombs/power reads and writes three
globals: lives `0x549D40` (byte), bombs `0x549D41` (byte), power `0x53CAD8`
(dword). `ResourceScope` (player2.cpp) exchanges those three with P2's pool
for the duration of any game code that acts **as P2**, and exchanges them
back after:

| Game code run as P2 | What it does with resources |
|---|---|
| P2's player update (`FUN_140069f30`) | bombing spends a bomb; the death sequence's end spends a life, resets bombs, drops power; power level picks the shot pattern |
| The item pass when P2 is the collector (`FUN_140044240`) | power/bomb/1-up items add to the collector's pool |

The hit tests (`FUN_14006c090`, `FUN_14006c2b0`) do **not** need it: a hit
only sets fields in the player struct (state 2, timers, death animation) —
checked in the decompile; the resource consequences happen later, in that
player's own update. Laser graze adds score (shared anyway).

P2's pool is saved in snapshots (it's inside `Player2State`) and mixed into
the `resources` checksum region, so rollback and desync detection cover it.

### Respawn bomb refill

A respawn doesn't refill bombs to a constant: outside Extra/practice it
refills to `0xC6E001`, a copy of the bombs global that the scene init
(`FUN_14003c250`) takes at every stage start (and `0xC6E000` for lives).
So each player needs their own copy: `PlayerResources::respawnBombs` is
swapped with `0xC6E001` by `ResourceScope`, and set from P2's bombs at every
P2 spawn, the same moment the game records P1's.

Online, the guest's scene init records its **own** values before adopting
the host's at the barrier, so the guest now also overwrites `0xC6E000/1`
with the host's (this was a latent desync in shared mode too, whenever the
two games' starting-bombs options differed).

### When P2's pool is (re)filled

At P2's spawn (every stage start), if this is the first spawn or the score
is 0 (a new run — a continue sets score to the continue count, never 0),
P2's pool is copied from P1's globals, which the game just set to the
starting values. Later stages carry the pool over, like the game's own.

### Per-player starting stock

`[coop] p1_start_lives / p1_start_bombs / p2_start_lives / p2_start_bombs`
(HUD units; -1 = the game's option). Applied at the new-run spawn, after the
game set its own starting values: P1's to the globals (and the stage-start
copies, and the HUD dirty bits `0xA` the player update uses), P2's to its
pool (separate resources only — shared uses P1's). A continue revives each
downed player with their configured stock (else what the continue gave).
They're part of `CoopSettings`, so online the host's apply to both.

Online, the host's READY message carries P2's pool and the guest adopts it
along with P1's resources (docs/02), so both machines start each stage with
the same pools.

## Downed and revive

With separate resources, running out of lives no longer ends the game while
the partner is still playing:

1. The player update for the dying player reaches the game's "no lives
   left" branch, which sets the game-over flag `0x53D401` and marks the
   player out (`+0xA2B4`).
2. `Detour_PlayerUpdate` (coop_rules.cpp) sees the flag go 0 → 1 during
   that call. If the partner isn't downed, it clears the flag again and
   marks this player **downed**.
3. A downed player is **parked** every frame instead of updated: state 2
   (dying — bullets pass through, items ignore it, enemies don't aim at
   it), out flag set, moved to (-4096, -4096). Its three draw functions are
   skipped, so it disappears.
4. While exactly one player is downed and the survivor isn't mid-death, a
   timer counts up; after `revive_seconds` the downed player is **revived**
   with one life. The timer restarts from zero whenever the survivor dies.
   The status text shows `PARTNER DOWN - REVIVE IN n`.
5. Revive = state 2, out flag cleared, respawn timer 0, death counter
   `0x1E`, lives = 1 in that player's pool. The next update then takes the
   game's own respawn branch (which spends that life, gives the respawn
   invulnerability and resets bombs) — so the revived player plays on the
   last life, with the HUD showing 1 remaining.
6. If the survivor also runs out while the partner is downed, they are
   marked downed too and the game-over flag is left set: the game's normal
   continue screen appears. On a continue (flag 1 → 0), every downed player
   is revived with the lives the continue just handed out (+1, since the
   respawn spends one).

`revive_seconds=0` means a downed player stays down until a continue.

The rules state (`downed[2]`, timer, last game-over flag) is registered as a
snapshot region, so rollback restores it.

## Untested assumptions / risks

- That the game-over flag is only acted on by the in-game menu/GUI, not by
  code that already ran between the flag being set and our clearing it in
  the same player-update call. (It's cleared before the call returns.)
- That the continue screen writes P1's lives global and nothing else needs
  resetting for a revived player. If a continue leaves P2 with the wrong
  stock, look at `TickRevive`.
- That parking a player at (-4096, -4096) in state 2 is invisible to every
  system (options, focus sprite). The three player draws are skipped, which
  should cover options and the hitbox sprite.
- Game-over after continue: score is set to the continue count, so P2's pool
  is **not** re-initialized by the score==0 rule — it's handled by the
  revive-on-continue path instead.
- Replays of a stage with separate resources will not be right (P2 isn't
  recorded at all — see docs/00 B5).

## Test plan

1. Local, `shared_resources=0`, `revive_seconds=10`: let P2 die until out
   of lives. P2 should vanish, P1 keeps playing, the countdown shows, P2
   comes back after 10 s with the invulnerability blink.
2. Same, but let P1 run out first: the game's HUD lives stay at 0, P1
   vanishes, P2 plays, P1 returns.
3. Both out: continue screen; pick continue — both reappear.
4. `p1_start_lives=5 p2_start_lives=2 p2_start_bombs=0`, separate: new
   run shows 5 on the HUD and `P2 LIVES 2 BOMBS 0`. P2 dies once: P2 is at
   1 life and gets P2's stage-start bombs back (0), not P1's.
5. P2 collects power items: P2's POWER line grows, P1's HUD power doesn't.
   P2 bombs: P2's BOMBS drops, the HUD's doesn't.
6. Sync test (`[synctest] enabled=1`) with separate resources and a P2
   death during the test window: no failures.
