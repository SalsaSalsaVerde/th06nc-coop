#pragma once

#include <cstddef>
#include <cstdint>

// A second player simulated by the game's own player code (docs/01).
//
// Lifecycle: spawned right after the game registers and initializes P1 at
// stage start (by copying P1's freshly-initialized struct -- the native init
// would reload P1's sprite slot, see docs/00), torn down whenever the game
// tears P1 down.

using Player2InputProvider = uint32_t (*)();

struct Player2Listener {
    bool (*forceSpawn)() = nullptr;  // spawn even when [player2] enabled=0
    void (*onSpawned)() = nullptr;   // called inside the simulation frame that started the stage
    void (*onDespawned)() = nullptr; // called inside the frame that tore the stage down
};

// Installs all Player 2 hooks. Call once, after Fingerprint_Check passed.
bool Player2_Install();

void Player2_SetListener(const Player2Listener& listener);

// Where P2's per-frame input mask comes from. Defaults to local devices
// (LocalInput_PollPlayer2); netplay replaces it with the synchronized stream.
void Player2_SetInputProvider(Player2InputProvider provider);

bool Player2_IsActive();

// Same-machine P2 on/off (from [player2] enabled); online the partner is
// always P2 regardless. Takes effect at the next stage start.
void Player2_SetEnabled(bool enabled);
bool Player2_Enabled();

// A gameplay stage is running (between P1's registration and teardown).
bool Player2_InStage();
uint8_t* Player2_Struct();

// P2's character (0 Reimu, 1 Marisa) and shot type (0 A, 1 B) for the next
// spawn; -1 = the same as P1 (docs/04). A different character loads its
// sprite sheet into a spare slot and swaps it in around P2's own update and
// draw passes.
void Player2_SetLoadout(int character, int shotType);
int Player2_WantedCharacter();
int Player2_WantedShotType();

// The loadout the active P2 actually spawned with.
void Player2_CurrentLoadout(uint8_t* character, uint8_t* shotType);

// Re-derives the active P2 from P1's stage-start state as another character
// (the guest's pick arrived after the host started the stage, docs/13). Only
// valid while the simulation sits at the stage's first frame.
bool Player2_ReapplyLoadout(int character, int shotType);

// P2's own lives/bombs/power when the co-op setting sharedResources is off
// (docs/06). Swapped into the game's resource globals around P2's update
// and P2's item pickups; persists across stages within a run.
struct PlayerResources {
    uint8_t lives;
    uint8_t bombs;
    uint32_t power;
    uint8_t respawnBombs; // this player's copy of Game::kStageStartBombs
};
PlayerResources* Player2_Resources();

// All of P2's mod-owned simulation state (the player struct plus the
// previous-frame input used for edge detection), for snapshots.
void* Player2_StateRegion(size_t* size);
