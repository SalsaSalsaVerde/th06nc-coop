# th06nc co-op mod

A two-player co-op mod for **Touhou Koumakyou ~ the Embodiment of Scarlet
Devil: New Classic** (Steam, `th06nc.exe`). Play on one PC(local co-op), or online with a friend through a Steam lobby.

Unofficial fan project, not affiliated with the game's developers or
publisher. You need your own copy of the game; the mod contains none of its
files.

## Status

- **Online, lockstep netcode:** played through stages 1–6 in sync.
- **Online, rollback netcode:** works, but has had an occasional desync;
  the cause was pinned down and fixed in docs/17, awaiting a live test.
  If you see `DESYNC AT FRAME n` in the corner, both players' logs help.
- **Same PC:** works (off by default, see below).

The mod checks the game's build at startup; if a game update changes the
executable, it logs that and stays out of the way until it's updated.

## Install

1. Download `th06nc_native_coop_installer.exe` from the
   [Releases](../../releases) page.
2. Put it in the game folder (Steam → the game → Manage → Browse local
   files) and run it. It installs, updates or removes the mod, and writes
   default settings (`th06nc_native_coop.ini`) if you have none.
3. Start the game from Steam as usual. The log is
   `th06nc_native_coop.log` in the same folder.

Both players online need the same mod version.

## Playing

- **Online:** the host presses **F9** in the game (creates a Steam lobby and
  opens the invite dialog); the friend accepts the invite. Each picks their
  own character; the host's difficulty, game mode and rules are used. Both
  start the stage and the game waits until both have. The host is Player 1, the guest
  Player 2; each uses their normal controls. **F10** leaves the lobby.
  Either player can pause (Esc, or opening the Steam overlay) and it pauses
  both games.
- **Same PC** (off by default: **F8** → LOCAL 2-PLAYER): Player 2 uses a
  second controller, or T/F/G/H to move + O shoot / P bomb / I focus.
- **Settings:** **F8** in the game's menus opens the co-op settings panel:
  netcode (rollback or lockstep) and input delay, boss HP multiplier,
  shared or per-player lives/bombs/power, revive timer and power after a
  revive, starting stock, a checkpoint stage (and midboss/boss start),
  invincible practice, enemy targeting, colors. Online the host's settings
  apply to both. Closing the panel saves to the ini; RESET TO DEFAULTS
  (press twice) puts everything back. The checkpoint and starting
  lives/bombs/power also apply to single-player runs, handy for practice.

On screen: a small status block top-left (connection, netcode, your
partner's lives/bombs/power when resources are per player), and a boss DPS
meter bottom-left during bosses.

## Known limitations

- Saved replays of co-op stages don't play back correctly (old solo replays
  are fine).
- Bullets that re-aim mid-flight still only aim at Player 1.
- Checkpoints are per stage, plus midboss/boss start points; individual
  spell cards aren't selectable.

## Building from source

Requires Visual Studio 2022 Build Tools (C++ workload) and the Steamworks
SDK headers (path set in `mod/build.ps1`).

```
powershell -File mod\build.ps1
powershell -File mod\installer\build.ps1
```

The installer embeds whatever `mod\build\steam_api64.dll` is, so build the
mod first. `mod\deploy.ps1` is the developer shortcut (copies the built DLL
straight into the game folder; `-Restore` puts the previous proxy back).

The mod is a proxy `steam_api64.dll` (it forwards every Steam API call to
the real DLL, renamed `steam_api64_orig.dll`) that hooks the game with
MinHook.

## Docs

The `docs/` folder is the engineering record — how each system works, and
what every test found:

- `00` scope and gap analysis · `01` the native second player · `02`
  lockstep/rollback netplay, snapshots, the sync test · `03` P2 in enemy
  aim, items, lasers, graze · `04` P2 as another character · `05` boss HP,
  invincible practice, targeting · `06` per-player resources, downed and
  revive · `07` player visuals · `08` settings panel, checkpoints, replays
  · `09` installer · `10` test checklist · `11` first live tests · `12`
  stage timeline and start points · `13`–`15` the online tests and fixes ·
  `16` the runtime font · `17` the rollback desync found, run mode
  follows the host

References to `overlay/NN` point at the research notes of the earlier
overlay-based version of this mod (not included here).

## Credits

- [MinHook](https://github.com/TsudaKageyu/minhook) by Tsuda Kageyu
  (BSD-2-Clause), for function hooking.
- [th06_multi_net](https://github.com/RUEEE/th06_multi_net), an EoSD co-op
  mod whose readable source informed the near-player fade and several
  research questions.
- Built with help from Claude (Anthropic).

## License

MIT (see `LICENSE`), for this project's own code and docs. MinHook keeps
its own license.
