#pragma once

#include "coop_rules.h"
#include "player_look.h"

// Settings read once at startup from th06nc_native_coop.ini next to the game
// exe. Every key is optional; missing keys keep the defaults below.
struct Config {
    bool player2Enabled = true;
    float player2SpawnOffsetX = 48.0f; // P2 spawns this far right of P1
    int player2Character = -1;         // 0 Reimu, 1 Marisa, -1 same as P1 (local play only)
    int player2ShotType = -1;          // 0 A, 1 B, -1 same as P1

    int p2PadIndex = 1;                 // XInput user index for P2, -1 = none
    int p2KeyUp = 'I';
    int p2KeyDown = 'K';
    int p2KeyLeft = 'J';
    int p2KeyRight = 'L';
    int p2KeyShoot = 'U';
    int p2KeyBomb = 'O';
    int p2KeyFocus = 'Y';

    CoopSettings coop; // [coop]; online, the host's are used
    LookSettings look; // [visual]; each machine its own (colors are exchanged)

    bool netplayRollback = true; // false = delay-based lockstep
    int netplayInputDelay = 2;   // frames between pressing a button and it taking effect
    int netplayMaxRollback = 8;  // frames the simulation may run ahead of confirmed input

    // Single-machine rollback self-test (docs/02): every `interval` frames,
    // roll back `distance` frames, re-simulate them with the same inputs and
    // check the result is byte-identical.
    bool syncTest = false;
    int syncTestDistance = 4;
    int syncTestInterval = 10;
};

void Config_Load();
const Config& Config_Get();
// Full path of th06nc_native_coop.ini (the settings panel writes back to it).
const char* Config_Path();
