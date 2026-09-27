# 10 — Live test checklist

Nothing below has run in the game yet. Work top to bottom: each block
depends on the ones above it. After any failure, the useful thing to send
back is `th06nc_native_coop.log` (game folder) plus what you saw.

The installed ini has `[player2] enabled=1`, so a second player appears in
every stage from the start. P2's keys: **T/F/G/H** move, **O** shoot, **P**
bomb, **I** focus (or a second controller).

## 1. Boots at all

- [ ] Game launches from Steam; the log shows `Fingerprint: th06nc.exe
      build recognized` and `Hooks: installed ...` lines, no `failed`.
- [ ] Title menu works normally; no status text in the corner while offline
      and no lobby open.
- [ ] **F8** opens the settings panel; up/down/left/right work, the menu
      behind doesn't move; **Esc** closes it without also backing out of the
      title menu; `th06nc_native_coop.ini` now reflects the changes.

## 2. Local second player (one PC)

- [ ] Start a stage: P2 appears 48 units right of P1, bluish tint.
- [ ] P2 moves, shoots, and its shots damage enemies and the boss.
- [ ] P2 focus: slower, orbs close in, and a **focus ring + hitbox square**
      appears on P2 (and on P1 when P1 focuses). Check the ring sits on the
      sprite, not offset.
- [ ] Walk P2 onto P1: P2, P2's shots and P2's focus marker fade to ~20%;
      P1 doesn't.
- [ ] Enemy bullets kill P2; P2 grazes (graze counter + sound); P2 dies and
      respawns with invulnerability.
- [ ] Enemies aim at whoever is nearer.
- [ ] **P2's bomb** clears bullets the way that character's bomb should
      (Reimu A: only where the orbs are; Marisa: the beam area, etc.) and
      turns them into items.
- [ ] Items: P2 collects items it touches; going above the collect line
      pulls items to P2.
- [ ] Boss HP is about twice as long (multiplier 2x) — log line
      `boss phase HP x2.00`.
- [ ] Different character: `[player2] character=marisa_b` with P1 as Reimu:
      P2 has Marisa's sprite and shot.
- [ ] Stage transitions: clear stage 1, P2 is still there in stage 2.
- [ ] Quit to title mid-stage and start again: no crash, P2 back.

## 3. Rules (panel or ini)

- [ ] Invincible practice on: nothing kills either player.
- [ ] Targeting `always P1` / `alternate` behave differently from nearest.
- [ ] Per-player resources: status line `P2 LIVES n BOMBS n POWER n`; P2's
      bombs/deaths/power pickups change P2's line, not the HUD.
- [ ] P2 runs out of lives: P2 vanishes, countdown `PARTNER DOWN - REVIVE
      IN n`, P2 returns after the timer. Same with P1 running out.
- [ ] Both out: normal continue screen; continuing brings both back.
- [ ] Starting stock (e.g. P1 5 lives, P2 2 lives 0 bombs) at a new run.
- [ ] Checkpoint: start stage 4 → a normal new game starts at stage 4.
      Stage practice is unaffected.
- [ ] Watch an old replay: **no P2**, the replay plays out as recorded.

## 4. Rollback self-test (one PC)

- [ ] `[synctest] enabled=1`, start a stage and play a minute (die, bomb,
      let P2 bomb, reach a boss). Status text `SYNCTEST: n CHECKS, 0
      FAILED`. Any failure: the log names the differing bytes — send it.

## 5. Online (needs a friend; both install this)

- [ ] Host F9, friend accepts invite: both show `CO-OP: CONNECTED`.
- [ ] Host's panel changes (e.g. boss HP) appear greyed out on the guest.
- [ ] Both start the same stage: brief "waiting for partner", then play.
      Host is P1, guest is P2, each with their own character and color.
      Starting at different times is fine: whoever is second still gets
      their final pick used (docs/13); a guest who starts first sees the
      screen hold until the host starts (up to 20 s).
- [x] `[netplay] mode=lockstep` first: play a stage; no `DESYNC` message.
      (2026-09-27: two stages clean; a continue in stage 3 desynced —
      fixed, docs/13.)
- [ ] A continue online: both come back with the same lives, no `DESYNC`;
      with per-player resources both are at full power.
- [ ] Pause online: Esc on either machine pauses both; either player can
      move the cursor and resume; opening the Steam overlay pauses both.
- [ ] Items: while one player sits above the collection line, the other
      can still pick up items by touching them.
- [ ] Checkpoint set only on the host starts both games there, also on
      the second run without restarting the game.
- [ ] F8 NETCODE / INPUT DELAY on the host switch both games at the next
      stage start (log: `mode ... (the host's)`).
- [ ] Then `mode=rollback`: feels responsive; still no desync; log's
      `Netplay stats` show rollbacks happening, with bombs going off, and no
      `game requested exit while re-simulating` lines.
- [ ] Deaths, bombs, boss kills, stage clears all match on both screens.
- [ ] Guest quits mid-stage: host keeps playing locally ("PLAYING
      LOCALLY"), back to normal next stage.

## Known not to work yet

- Midboss / stage-spell checkpoints (only stage checkpoints).
- Bullets that re-aim mid-flight still only aim at P1.
- Saved replays of co-op stages won't play back correctly.
- The focus ring's placement assumes the 1456x816 window layout.
