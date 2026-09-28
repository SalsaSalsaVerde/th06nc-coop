#pragma once

#include <cstdint>

// Co-op run settings from the original spec that change the simulation
// (docs/05). Online, the host's settings are used by both machines.
struct CoopSettings {
    float bossHpMultiplier = 2.0f; // boss HP x this while two players are in the stage
    bool invincible = false;       // practice: bullets and enemies can't kill anyone
    uint8_t targeting = 0;         // CoopTargeting
    bool sharedResources = true;   // one pool of lives/bombs/power; false = each player has their own
    int32_t reviveSeconds = 30;    // separate resources only: surviving this long revives a downed partner (0 = never)
    // Starting stock at the start of a run, per player (P1, P2), as the HUD
    // counts them; -1 = the game's own option. With shared resources only
    // P1's apply (docs/06).
    int8_t startLives[2] = { -1, -1 };
    int8_t startBombs[2] = { -1, -1 };
    int16_t startPower = -1; // power (0-128) both players start a run with; -1 = the game's (0)
    int8_t startStage = 1;   // checkpoint: a new co-op run starts at this stage (1-6)
    int8_t startPoint = 0;   // CoopStartPoint: where in that stage the run starts
    // Separate resources: the power a downed player comes back with when
    // revived by the timer -- -1 = what they had before going down, else
    // 0-128 (the game's tiers: 0 8 16 32 48 64 80 96 128) (docs/15).
    int16_t revivePower = -1;
};

enum CoopStartPoint : int8_t {
    kStartAtStage = 0,   // the stage's beginning
    kStartAtMidboss = 1, // the timeline record that spawns the first enemy the timeline then waits for
    kStartAtBoss = 2,    // the game's own boss marker
};

bool CoopSettings_Equal(const CoopSettings& a, const CoopSettings& b);

enum CoopTargeting : uint8_t {
    kTargetNearest = 0,  // each enemy aims at the nearer living player
    kTargetHost = 1,     // enemies always aim at P1
    kTargetAlternate = 2 // enemies in even table slots aim at P1, odd at P2
};

bool CoopRules_Install();

// The settings in effect (online: the host's, adopted by the guest).
void CoopRules_SetSettings(const CoopSettings& settings);
const CoopSettings& CoopRules_Settings();

// This machine's own choice (ini, settings panel): applied now, and what
// CoopRules_RestoreOwnSettings goes back to after playing as a guest.
void CoopRules_SetOwnSettings(const CoopSettings& settings);
const CoopSettings& CoopRules_OwnSettings();
void CoopRules_RestoreOwnSettings();

// Mod-owned simulation state, for snapshots.
void* CoopRules_StateRegion(size_t* size);

bool CoopRules_InvincibleNow();

// Separate resources only: a player who ran out of lives while the partner
// was still in is "downed" -- frozen, hidden, out of play -- until revived
// (partner survives reviveSeconds) or the game continues.
bool CoopRules_IsDowned(const uint8_t* player);

// Frames until the downed player is revived, or -1 if nobody is downed or
// revive is off.
int CoopRules_ReviveFramesLeft();

// A new run started (not the next stage of the same run): nobody is downed.
void CoopRules_ResetRun();

// Boss DPS meter (docs/15): the shot-damage hook reports each player's
// damage to each enemy; damage to the tracked boss is tallied per frame in
// the rules state (so rollbacks rewind it). The text is for the overlay's
// bottom-left block; empty while no boss is up.
void CoopRules_RecordShotDamage(int player, int damage, const float* enemyPos);
void CoopRules_DpsText(char* out, int outSize, float* r, float* g, float* b);

// Call at the entry of the gameplay scene init, before the stage loads.
// For a fresh co-op run from the menu (not a replay or practice), starts it
// at the checkpoint stage instead of stage 1 (docs/08).
void CoopRules_OnSceneInit(bool coopActive);

// Call right after the gameplay scene init returned: applies the start
// point (midboss/boss) to a fresh run's first stage by pointing the game's
// own timeline jump at the chosen record (docs/12).
void CoopRules_AfterSceneInit();

// Every native player-death path (bullets, lasers, enemy contact) only
// kills a player whose state is 0; in state 3 (bombing) touching bullets are
// cancelled instead. While invincible practice is on, a normal-state player
// is presented as state 3 for the duration of one such check.
class InvincibleScope {
public:
    explicit InvincibleScope(uint8_t* player);
    ~InvincibleScope();
    InvincibleScope(const InvincibleScope&) = delete;
    InvincibleScope& operator=(const InvincibleScope&) = delete;

private:
    uint8_t* m_player = nullptr;
};
