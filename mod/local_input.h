#pragma once

#include <cstdint>

// Reads the local second player's devices (XInput pad Config::p2PadIndex plus
// the configured keyboard keys) as a game input mask. Keyboard only counts
// while the game window is focused.
uint32_t LocalInput_PollPlayer2();
