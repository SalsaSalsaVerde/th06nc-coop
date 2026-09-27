# 12 — The stage timeline, and midboss / boss start points

Status: **built, untested** (the game couldn't be launched when this was
written). Off by default: `[coop] start_point=stage`.

## The stage timeline

A stage's enemies are scheduled by the ECL file's *timeline*, a list of
records the enemy manager runs against a stage clock. The scene init
(`FUN_14003c250`) loads the ECL, builds the sub-pointer table at
`0xABADA0` (one entry per sub, offsets from the header at `0xABAD98`) and
points `0xABAD90` at the timeline (header `+2`, a byte offset).

Each record is an array of shorts:

| Index | Meaning |
|---|---|
| `[0]` | stage time to run at; negative ends the timeline |
| `[1]` | argument: the sub to run for spawns, the enemy index for waits, the dialogue for the boss intro |
| `[2]` | opcode |
| `[3]` | record size in bytes (next record = this + size) |
| `[4..]` | payload: x/y/z as floats at byte 8, then two shorts and an int passed to the spawn |

Opcodes seen in the tick (`FUN_140037b80`, the enemy manager's per-frame
timeline step):

| Opcode | Does |
|---|---|
| 0–3 | spawn an enemy running sub `[1]` at the given position (2/3 flag it `0x40`) |
| 4–7 | the same with each coordinate randomized when it is above a threshold |
| 8 | boss intro: dialogue `[1]` (adjusted per character), sets the scene state |
| 9 | hold the clock while the dialogue is up |
| 10 | set a field on the enemy registered under index `[1]` |
| 0xB | set the power global |
| 0xC | hold the clock while enemy `[1]` (a registered slot, e.g. the midboss) is alive |
| 0xD | marker: nothing at tick time |

The enemy manager is the object at `0xAEE0B0` (its entity table is at
`+8` = `0xAEE0B8`); the timeline cursor is at `+0x10C128` (`0xBFA1D8`) and
the stage clock at `+0x10C0BC` (`0xBFA16C`, advanced by `FUN_1400012F0`).
The tick runs every record whose time equals the clock, and stops at the
first record whose time is still ahead.

### The game's own "jump to the boss"

At the start of the tick:

```
if (0xABADB0 && 0xABADA8) { cursor = 0xABADA8; clock = cursor->time; 0xABADB0 = 0; }
```

The scene init sets `0xABADA8` to the timeline's first opcode-0xD record
(the boss section marker), and `FUN_1400407C0` sets the flag — the game's
own skip to the boss (used by its practice/replay UI). Everything before
the target record is simply never run.

### Spell practice, for comparison

Spell practice doesn't use the timeline at all. With `0x53D405` set,
`FUN_140037b80` looks up the chosen spell (`0x53D408`) in the spell table
(`0xC6E3E0`, `0x28` bytes per entry: `[0]` spell id, `[1]` stage index,
`[2..3]` the boss sub that plays just that spell, `[4]` difficulty, `[6]`
availability), spawns the boss from the practice template
(`0xBFA1E0`) running that sub, sets the clock to 200000 so no timeline
record ever fires, and marks `0xAEE09F` so it happens once. When the spell
ends, the flag makes the stage end (`0xAEE0AE = 1` from the script's
spell-end opcode `0x5E` and from `FUN_140038870` when a boss-flagged enemy
dies). A run can't continue past a spell started this way, so this path
is not used for checkpoints.

## Start points (this mod)

`[coop] start_point = stage | midboss | boss`, also on the F8 panel
("START AT"), part of the co-op settings the host sends to the guest.
Applied by `CoopRules_AfterSceneInit` right after the scene init of a
**fresh run's first stage** (the same conditions as the stage checkpoint:
stage index 0 before the checkpoint, no replay/practice, no continues,
score 0), by pointing the game's own jump at the chosen record and setting
its flag; the next timeline tick does the rest.

- **boss**: the record the scene init already chose (the first 0xD marker).
  If the stage has none, the run starts normally (logged).
- **midboss**: `FindMidbossRecord` walks the timeline from the start and
  takes the last spawn record (opcode 0–7) before the first opcode-0xC
  wait, provided that wait comes before any boss intro (opcode 8) or boss
  marker. That is the record which spawns the enemy the timeline then
  waits for — the midboss. Stages whose timeline has no such wait before
  the boss start normally.

What the jump doesn't touch: the background (its own script keeps its own
clock, so the scenery starts from the stage's beginning), the music (the
boss intro starts the boss track as usual), and the players' stock
(`start_power` pairs well with a late start).

## To verify

1. `start_point=boss`, stage 1: the run should open on Rumia's intro.
2. `start_point=midboss`, stage 1: Rumia's midboss appearance; stage 2:
   Daiyousei; stage 3: Meiling's midboss section; stages without a
   midboss wait fall back with a log line.
3. The log line `CoopRules: start point ... timeline jumps to time N` shows
   what was chosen; the boss and midboss times should match the stage.
4. Online: both machines log the same jump.
