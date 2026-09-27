#pragma once

#include <cstddef>
#include <cstdint>

// Save/restore of the game's simulation state for rollback (docs/02).
//
// A snapshot is the whole writable .data section of th06nc.exe (every
// simulation pool the game has is a static global there: players, bullets,
// items, entities, scene buffer, RNG) plus registered extra regions. Restore
// skips "volatile" bytes: anything calibration saw change while the
// simulation was NOT stepping (render, timing and audio state), and a fixed
// denylist (scheduler list heads, BGM/sound state).
//
// Calibration works because the game's own replay fast-forward steps the
// simulation up to 8 times per rendered frame and replays stay in sync --
// drawing never feeds back into simulation state, so bytes that change while
// only drawing runs are safe to leave alone on restore.

bool Snapshot_Init(int slotCount);

// Mod-owned memory that is simulation state (P2's player struct).
void Snapshot_AddRegion(void* ptr, size_t size);

// A heap block reached through a pointer global in the game.
void Snapshot_AddIndirectRegion(uintptr_t pointerRva, size_t size);

// Bytes [begin, end) of the region added `regionIndex`-th are neither
// restored nor compared (real-time state inside an otherwise simulation-
// owned object, docs/11).
void Snapshot_MarkExtraVolatile(size_t regionIndex, size_t begin, size_t end);

void Snapshot_BeginCalibration();
void Snapshot_CalibrationSample(); // once per rendered frame while stalled
void Snapshot_EndCalibration();

// Continuous calibration: stage-start calibration can't see draw-time writes
// into objects that don't exist yet (bullets, effects). Arm right after the
// last simulation step of a rendered frame; learn at the start of the next
// one. Whatever changed in between was written by drawing alone.
void Snapshot_ArmDrawLearning();
void Snapshot_LearnDrawChanges();

void Snapshot_Save(int frame);
bool Snapshot_Load(int frame);
bool Snapshot_Has(int frame);
void Snapshot_Clear();

// Compares live memory against a saved frame over exactly the bytes a
// restore would write. Only meaningful within one process (pointers match).
// Logs up to `maxReport` differing addresses; returns how many bytes differ.
// With `learnExtraDiffs`, differing bytes of the extra regions are marked
// volatile instead of counted (they hold no checksummed gameplay state).
size_t Snapshot_CompareLive(int frame, int maxReport, bool learnExtraDiffs = false);
