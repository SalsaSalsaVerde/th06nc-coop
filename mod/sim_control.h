#pragma once

#include <cstdint>

// Control over the game's simulation frame (docs/02).
//
// The game's per-render-frame function (FUN_14003d8b0) calls FUN_14003d700
// once per simulation frame -- up to 8 times per render frame during replay
// fast-forward -- then walks the draw list. FUN_14003d700 walks the update
// list exactly once. Hooking it lets netplay stall (step zero times: the
// game just redraws the same frame), or re-simulate (restore a snapshot and
// step several times before the draw list runs).

// Decides how many simulation frames to run for one call from the game.
// Receives the game's out-parameter; returns what FUN_14003d700 would.
// nullptr = run the game's own step unchanged.
using FrameDriver = uint64_t (*)(uint32_t* outCode);

bool SimControl_Install();
void SimControl_SetDriver(FrameDriver driver);

// One native simulation frame with the game's own inputs.
uint64_t SimControl_StepNative(uint32_t* outCode);

// One native simulation frame with both players' inputs forced. `muted`
// marks a re-simulated frame: no sound effects, no music starts, and the
// cosmetic screen shake neither ticks nor starts twice (docs/14). The low
// byte of the result is the game's "leave the frame loop" flag; the rest of
// the register is garbage (the game's step returns a bool).
uint64_t SimControl_StepForced(uint32_t* outCode, uint32_t p1Input, uint32_t p2Input, bool muted);

// The netplay frame number the next forced step simulates (-1 = none), so a
// re-simulated frame can tell the shakes the live run already started.
void SimControl_SetStepFrame(int frame);

// The local devices' input mask as the game itself reads it (keyboard +
// XInput controller 0). Call at most once per simulated frame.
uint32_t SimControl_PollLocalDevices();

// P2's forced input for the frame currently being simulated (valid only
// inside SimControl_StepForced).
uint32_t SimControl_ForcedPlayer2Input();
