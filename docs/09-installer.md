# 09 — Installer

`mod/installer/` builds `th06nc_native_coop_installer.exe`, a single console
exe with the mod DLL and the example settings embedded as resources (same
design as overlay/27, extended).

Drop it in the `th06nc` folder and double-click it. It detects the folder's
state and acts:

| Folder state | What it does |
|---|---|
| Only the real `steam_api64.dll` | renames it `steam_api64_orig.dll`, writes the mod as `steam_api64.dll` |
| The **overlay** co-op mod installed | asks, then replaces it; the overlay DLL is kept as `steam_api64.previous_proxy.dll` |
| Native co-op installed | update (or "already up to date"), or remove |
| Anything else | touches nothing and explains |

In every install/update it writes `th06nc_native_coop.ini` with the default
settings **only if there isn't one** — your settings survive updates.
Remove = delete the mod DLL, rename `steam_api64_orig.dll` back; settings,
log and the overlay backup stay.

A DLL counts as "a co-op mod" only if it contains that mod's log-file name
(`th06nc_native_coop.log` / `th06nc_mod_proxy.log`), so the real Steam DLL
is never deleted. File-in-use errors say to close the game.

Flags: `--path <dir>`, `--install` (install/update/replace without asking),
`--uninstall`, `--no-pause`.

To go back to the overlay mod: run this installer's remove, then the
overlay mod's installer — or copy `steam_api64.previous_proxy.dll` over
`steam_api64.dll` (what `mod/deploy.ps1 -Restore` does).

## Building

Always the mod first, since the installer embeds whatever is in
`mod/build/steam_api64.dll`:

```
powershell -File mod\build.ps1
powershell -File mod\installer\build.ps1
```

Output: `mod/installer/build/th06nc_native_coop_installer.exe`.

## Verified (2026-09-27)

In a scratch folder (fake `th06nc.exe`, real Steam DLL): fresh install →
update ("already up to date") → uninstall restores the real DLL
byte-for-byte; overlay-installed folder → replaced with the overlay DLL
backed up byte-for-byte; unrecognized DLL → refused; an existing ini is
never overwritten. Then installed into the real game folder over the
overlay mod.
