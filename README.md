# th06nc native co-op

A co-op mod for **Touhou Koumakyou: New Classic** (`th06nc.exe`) where the
second player is a real player simulated by the game itself — native sprite,
shots, bombs that clear bullets, deaths, graze, item collection, enemies
aiming at whichever player is closer — playable on one machine, or online
through a Steam lobby with rollback netcode.

Successor to the overlay-based mod in `..\Touhou Coop Mod` (whose `docs/`
are the research record this builds on; referenced here as `overlay/NN`).

**Status (2026-09-27): local play live-tested and working; the rollback
self-test passes; the first two-machine test held lockstep sync for two
full stages (docs/13), and rollback mode is next.** See `docs/10` for the
test checklist and `docs/00` for what's done.

## Docs

- `docs/00-scope-and-gap-analysis.md` — what changes vs. the overlay mod,
  the findings that make it feasible, gap list, risks
- `docs/01-native-player2.md` — how the second player exists
- `docs/02-netplay-rollback.md` — lockstep/rollback netplay, snapshots, the
  sync test, test plan
- `docs/03-player2-shared-systems.md` — P2 in enemy aim, items, lasers,
  graze
- `docs/04-different-characters.md` — P2 as another character; each player
  picks their own online
- `docs/05-coop-rules.md` — boss HP multiplier, invincible practice,
  targeting mode
- `docs/06-separate-resources-and-revive.md` — optional per-player
  lives/bombs/power, downed players and the revive timer
- `docs/07-player-visuals.md` — player colors, the other player fading
  near you, focus ring
- `docs/08-settings-panel-checkpoints-replays.md` — the in-game co-op
  settings panel (F8), stage checkpoints, replay safety
- `docs/09-installer.md` — the single-file installer
- `docs/10-test-checklist.md` — what to check in the game, in order
- `docs/11-first-live-test-fixes.md` — what the first live tests found and
  how it was fixed
- `docs/12-stage-timeline-and-start-points.md` — the stage timeline;
  midboss / boss start points
- `docs/13-first-online-test.md` — the first two-machine test: what the
  logs showed, the continue desync, the late-pick race, and the fixes

## Install

Put `th06nc_native_coop_installer.exe` in the `th06nc` game folder and run
it. It installs, updates, replaces the older overlay mod (keeping it as
`steam_api64.previous_proxy.dll`), or removes the mod, and writes default
settings (`th06nc_native_coop.ini`) if there are none (docs/09). Log:
`th06nc_native_coop.log` in the game folder.

## Build

```
powershell -File mod\build.ps1
powershell -File mod\installer\build.ps1
```

The installer embeds whatever `mod\build\steam_api64.dll` is, so build the
mod first. `mod\deploy.ps1` is the developer shortcut (copies the built DLL
straight in; `-Restore` puts the previous proxy back).

## Playing

- **Same machine**: Player 2 uses a second controller (XInput index 1) or
  T/F/G/H to move + O (shoot) / P (bomb) / I (focus). The game's own second
  layout uses W/A/S/D and J/K/L, so P2's keys stay clear of those. P2's
  character is set in the
  ini (`[player2] character=`).
- **Online**: host presses **F9** (creates a Steam lobby and opens the
  invite dialog), friend accepts. Each picks their own character; the
  host's difficulty is used. Both start the stage and the game waits until
  both have (a guest who starts first sees the screen hold until the host
  starts). The host is Player 1, the guest Player 2; each uses their
  normal controls. **F10** leaves the lobby.
- **Settings**: **F8** in the game's menus opens the co-op settings panel
  (rules, starting stock, checkpoint stage, colors); online the host's
  rules apply to both. Closing it saves to the ini.

## Layout

- `mod/` — the proxy DLL (`steam_api64.dll`), MinHook, build/deploy scripts
- `tools/ghidra_scripts/` — headless Ghidra helpers (copied from the overlay
  repo, plus `FindRefsInRange.java`)
- `research/` — decompiler output (not committed)
