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
    // The game itself binds a second layout -- W/A/S/D move, J shoot,
    // K bomb, L focus, plus Q, R and the numpad -- so P2's keys stay clear
    // of those (docs/11).
    int p2KeyUp = 'T';
    int p2KeyDown = 'G';
    int p2KeyLeft = 'F';
    int p2KeyRight = 'H';
    int p2KeyShoot = 'O';
    int p2KeyBomb = 'P';
    int p2KeyFocus = 'I';

    CoopSettings coop; // [coop]; online, the host's are used
    LookSettings look; // [visual]; each machine its own (colors are exchanged)
    bool bossDps = true; // [visual] boss_dps: the DPS meter in the bottom-left during bosses

    bool netplayRollback = true; // false = delay-based lockstep
    int netplayInputDelay = 2;   // frames between pressing a button and it taking effect
    int netplayMaxRollback = 8;  // frames the simulation may run ahead of confirmed input

    // Single-machine rollback self-test (docs/02): every `interval` frames,
    // roll back `distance` frames, re-simulate them with the same inputs and
    // check the result is byte-identical.
    bool syncTest = false;
    int syncTestDistance = 4;
    int syncTestInterval = 10;
    bool syncTestPerturb = false; // [synctest] perturb: first re-run each window on wrong inputs (docs/15)
};

void Config_Load();
const Config& Config_Get();
// Full path of th06nc_native_coop.ini (the settings panel writes back to it).
const char* Config_Path();
