# Native Player 2 (2026-09-27)

Implements gap-list items A1-A6 from docs/00. Built, **not yet live-tested**.

## How P2 exists

- **Spawn**: hook `FUN_1400694d0` (the game's player registration). After it
  returns successfully — P1 is zeroed, registered and initialized by then —
  copy P1's `0xA2B8`-byte struct into a mod-owned buffer, shift P2's X by
  `spawn_offset_x`, and register four mod-owned task nodes pointing at the
  copy: the update at priority 7 on the update list, and the game's own three
  draw callbacks at priorities 7/9/11 on the draw list. Same priorities as
  P1's nodes, inserted after them, so P2 always runs immediately after P1.
- **Why a copy, not the native init**: the player init callback reloads
  `data/player00.anm` into sprite slot 5, unloading it first. Running it for
  P2 would free P1's sprite data mid-stage. The copy shares slot 5 with P1.
- **Despawn**: hook `FUN_14003cf20` (scene teardown, which is where the game
  cuts P1's four nodes). After it runs, cut P2's four nodes with the game's
  own cut routine (`FUN_140012e10`). Our nodes aren't heap-flagged and have no
  deleted callback, so the cut only unlinks them — in particular it does NOT
  run the player teardown callback, which would unload P1's sprite slot.
- **Update with P2's input**: P2's update node is a small wrapper that swaps
  P2's input mask into the game's global current/previous input words, calls
  the game's player update (`FUN_140069f30`) on P2's struct, then restores
  P1's words. The update and everything it calls (shot patterns, the bomb
  trigger) read input only from those two words.

## Where P2 is added to shared systems

| System | Native behavior | Hook |
|---|---|---|
| Bomb clears bullets | Bullet loop / hit test / lasers ask `FUN_14006bfd0(P1, box)` whether a bullet is inside a bomb clear zone | Answer "P1's zones OR P2's zones" |
| Bullets / enemy bodies hit player | `FUN_14006c090(P1, pos, size, mode)`; triggers death on the instance it's given | Also test P2; combine (2 = in a clear zone wins over 1 = hit) |
| Bullets that never grazed P1 | Never hit-tested (the graze gate is inline P1-only code) | After the native bullet update, hit-test every live bullet against P2; hits become state 5 (cancelled) |
| Player shots damage enemies | `FUN_14006b420(P1, enemy, size, &flag)` returns damage | Add P2's damage; also update P2's homing target (`+0x7740`) the way the enemy loop updates P1's |

Enemy aim (nearest living player), items, lasers and P2's graze are added
the same way in docs/03.

## Shared (not yet per-player)

- Lives, bombs, power, graze, score are the game's single globals. P2's
  bombs spend the shared stock; P2's deaths spend shared lives; game over
  when the shared lives run out. (The original spec lists shared
  lives/bombs as one of the intended modes.)
- No HUD for P2 (nothing to show while resources are shared).

P2 can be a different character/shot type than P1 — see docs/04.

## Controls

P2 reads XInput controller index `pad_index` (default 1, the second
controller; the game reads index 0 for P1) plus keyboard I/J/K/L move, U
shoot, O bomb, Y focus. Keyboard input only counts while the game window is
focused. All configurable, see `mod/th06nc_native_coop.example.ini`.

## First live test — what to check, in order

Install: `powershell -File mod\deploy.ps1` (backs up the overlay mod's DLL to
`steam_api64.previous_proxy.dll`; `deploy.ps1 -Restore` swaps back). Log:
`th06nc_native_coop.log` in the game folder.

1. Log shows `Fingerprint: ... recognized`, then `Hooks: installed ...`
   lines and no `failed`. If the fingerprint line says mismatch, the game
   updated and nothing was installed.
2. Start Stage 1. Log shows `Player2: spawned`. A second copy of your
   character appears to the right of P1.
3. Move P2 with IJKL or the second controller; P1 unaffected and vice
   versa.
4. P2 shoots (U / A button); shots damage enemies and the boss.
5. P2 bombs (O / B button): bomb animation plays at P2, bullets near P2's
   bomb turn into point items, the shared bomb count goes down.
6. Let a bullet hit P2: P2's death animation plays, shared lives drop, P2
   respawns. Grazing with P2 raises the graze counter.
7. Move P2 closer to enemies than P1: aimed shots go at P2. Take P2 to the
   top of the screen: items fly to P2. A laser should kill P2 too.
8. Clear or quit the stage: log shows `Player2: despawned`; return to the
   title and start again — no crash, P2 reappears.

Most likely failures and what they'd mean:
- **Crash at stage start** → something in the struct copy is instance-
  unsafe (a self-pointer), or node insertion is wrong. The log's last line
  says how far it got.
- **P2 visible but mirrors P1's input** → the update reads input from
  somewhere other than the two swapped words.
- **P2 frozen / invisible** → a node isn't linked into the list the
  scheduler actually walks (see docs/00 D4 on the two lists).
- **Crash on returning to title** → teardown order; P2's nodes cut after
  something they depend on was freed.
