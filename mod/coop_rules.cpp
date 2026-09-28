#include "coop_rules.h"
#include "game.h"
#include "hooks.h"
#include "mod_log.h"
#include "player2.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

using EnemyUpdateFn = uint64_t (*)(void* enemyManager);
using PlayerFn = uint64_t (*)(uint8_t* player, uint64_t secondArg);
EnemyUpdateFn g_origEnemyUpdate = nullptr;
PlayerFn g_origPlayerUpdate = nullptr;

CoopSettings g_settings;
CoopSettings g_ownSettings;

const int kDpsWindow = 180; // frames the meter averages over (3 s)

struct RulesState {
    int32_t lastScaledMaxHp; // boss max HP after our last scaling; -1 = none
    uint8_t downed[2];       // P1, P2
    uint8_t lastGameOver;
    uint8_t lastContinues;   // the game's continues-used counter, last frame
    int32_t reviveTimer;
    uint32_t lastAlivePower[2]; // each player's power the last frame they were in play
    // Boss damage per player per frame, a ring over the last kDpsWindow frames.
    int32_t bossDamage[kDpsWindow][2];
    int32_t bossDamageSlot;
    int32_t bossFrames;   // frames the current boss has been up
    int32_t noBossFrames; // frames since a boss was last up
};
RulesState g_state = { -1, { 0, 0 }, 0, 0, 0 };
int g_bombLogsLeft = 20;
int g_downedLogsLeft = 30;
int g_downedLogTimer = 0;

int32_t* EntityInt(uint8_t* slot, uintptr_t offset) {
    return reinterpret_cast<int32_t*>(slot + offset);
}

uint8_t* FindBossSlot() {
    uint8_t* slot = Game::At<uint8_t>(Game::kEntityTable);
    for (int i = 0; i < Game::kEntitySlots; i++, slot += Game::kEntityStride) {
        if (slot[Game::kEntityBossFlags] & Game::kEntityBossBit) return slot;
    }
    return nullptr;
}

int32_t Scale(int32_t value) {
    return static_cast<int32_t>(lroundf(static_cast<float>(value) * g_settings.bossHpMultiplier));
}

// Each boss phase (non-spell or spell card) assigns a fresh max HP. The
// first frame a max HP shows up that this code didn't produce, scale the
// whole phase: current HP, max HP, the phase's boundary and its own max
// (overlay/55's approach, moved into the simulation so both netplay
// machines -- and re-simulated frames -- do it on the same frame).
void ScaleBossHp() {
    if (!Player2_IsActive() || g_settings.bossHpMultiplier == 1.0f) {
        g_state.lastScaledMaxHp = -1;
        return;
    }
    uint8_t* boss = FindBossSlot();
    if (!boss) {
        g_state.lastScaledMaxHp = -1;
        return;
    }
    int32_t maxHp = *EntityInt(boss, Game::kEntityMaxHp);
    if (maxHp <= 0 || maxHp == g_state.lastScaledMaxHp) return;

    int32_t hp = *EntityInt(boss, Game::kEntityHp);
    int32_t boundary = *EntityInt(boss, Game::kEntitySegmentBoundary);
    int32_t segmentMax = *EntityInt(boss, Game::kEntitySegmentMax);
    *EntityInt(boss, Game::kEntityMaxHp) = Scale(maxHp);
    *EntityInt(boss, Game::kEntitySegmentBoundary) = Scale(boundary);
    *EntityInt(boss, Game::kEntitySegmentMax) = Scale(segmentMax);
    *EntityInt(boss, Game::kEntityHp) = Scale(hp);
    g_state.lastScaledMaxHp = *EntityInt(boss, Game::kEntityMaxHp);
    ModLog("CoopRules: boss phase HP x%.2f (hp %d -> %d, max %d -> %d)", g_settings.bossHpMultiplier, hp,
           *EntityInt(boss, Game::kEntityHp), maxHp, g_state.lastScaledMaxHp);
}

uint64_t Detour_EnemyUpdate(void* enemyManager) {
    uint64_t result = g_origEnemyUpdate(enemyManager);
    ScaleBossHp();
    return result;
}

// ---- downed / revive (docs/06) ----------------------------------------------
//
// The player update's final death (respawn branch reached with 0 lives) sets
// the global game-over flag, marks the player out (+0xA2B4) and starts a
// respawn. When separate resources are on and the partner is still in, that
// is undone and the player is parked instead: state 2 (dying -- bullets
// pass through, items ignore it, enemies don't target it), off-screen, its
// update skipped and its draws skipped (player_look.cpp). Revive = one life and the death counter set so
// the next update runs the game's own respawn.

int PlayerIndex(const uint8_t* player) {
    if (player == Game::Player1()) return 0;
    if (Player2_IsActive() && player == Player2_Struct()) return 1;
    return -1;
}

uint8_t* PlayerByIndex(int index) {
    return index == 0 ? Game::Player1() : Player2_Struct();
}

uint8_t& GameOverFlag() {
    return *Game::At<uint8_t>(Game::kGameOverFlag);
}

void ParkDowned(uint8_t* player) {
    player[Game::kPlayerState] = 2;
    player[Game::kPlayerOutFlag] = 1;
    *reinterpret_cast<int32_t*>(player + Game::kPlayerDeathCounter) = 0;
    float* pos = reinterpret_cast<float*>(player + Game::kPlayerPosX);
    pos[0] = -4096.0f;
    pos[1] = -4096.0f;
}

// Lives are set to 1 and the death counter past its end, so the next update
// takes the native respawn branch (which spends that life). Power comes back
// per the revivePower setting. Runs from P1's update, before it, where the
// resource globals hold P1's own pool.
void Revive(int index, uint8_t lives) {
    uint8_t* player = PlayerByIndex(index);
    g_state.downed[index] = 0;
    player[Game::kPlayerState] = 2;
    player[Game::kPlayerOutFlag] = 0;
    *reinterpret_cast<int32_t*>(player + Game::kPlayerRespawnTimer) = 0;
    *reinterpret_cast<int32_t*>(player + Game::kPlayerDeathCounter) = 0x1E;
    uint32_t power = g_settings.revivePower < 0 ? g_state.lastAlivePower[index]
                                                : static_cast<uint32_t>(g_settings.revivePower);
    if (power > 128) power = 128;
    if (index == 0) {
        *Game::At<uint8_t>(Game::kLives) = lives;
        *Game::At<uint32_t>(Game::kPower) = power;
    } else {
        Player2_Resources()->lives = lives;
        Player2_Resources()->power = power;
    }
    uint32_t* hud = Game::At<uint32_t>(Game::kHudDirtyFlags);
    *hud = (*hud & ~0x15u) | 0x2Au; // lives, bombs, power changed
    ModLog("CoopRules: P%d revived (power %u)", index + 1, power);
}

// Boss DPS: one ring slot per frame, advanced from P1's update.
void TickBossDamage() {
    if (FindBossSlot()) {
        g_state.noBossFrames = 0;
        g_state.bossFrames++;
    } else if (++g_state.noBossFrames > 60 && g_state.bossFrames != 0) {
        g_state.bossFrames = 0;
        memset(g_state.bossDamage, 0, sizeof(g_state.bossDamage));
    }
    g_state.bossDamageSlot = (g_state.bossDamageSlot + 1) % kDpsWindow;
    g_state.bossDamage[g_state.bossDamageSlot][0] = 0;
    g_state.bossDamage[g_state.bossDamageSlot][1] = 0;
}

// Runs once per frame, from P1's update call (P1's node always exists).
void TickRevive() {
    TickBossDamage();
    uint8_t gameOver = GameOverFlag();
    uint8_t continues = *Game::At<uint8_t>(Game::kContinuesUsed);
    bool continued = continues > g_state.lastContinues;
    g_state.lastContinues = continues;
    if (continued) ModLog("CoopRules: continue used (%d so far), lives now %d", continues, *Game::At<uint8_t>(Game::kLives));
    if (continued || (g_state.lastGameOver && !gameOver)) {
        // Continued after a game over: everyone comes back with the starting
        // lives the continue just handed out. (The game-over flag alone
        // isn't a reliable signal: the game may clear it before P1's update
        // sees it set. The continues counter is, docs/11.)
        // The respawn branch spends one life, so hand out one more.
        uint8_t lives = static_cast<uint8_t>(*Game::At<uint8_t>(Game::kLives) + 1);
        for (int i = 0; i < 2; i++) {
            // A configured starting stock counts like the HUD, which is what
            // the byte shows after the respawn spends one.
            int8_t configured = g_settings.startLives[i];
            if (g_state.downed[i]) Revive(i, configured > 0 ? static_cast<uint8_t>(configured) : lives);
        }
        // The continue resets power to 0 and drops full-power items at the
        // last player to die, who respawns invulnerable and so collects
        // everything on screen -- in a solo game that's the player getting
        // full power back. With separate pools the partner would stay at 0,
        // so both simply get full power (docs/14).
        if (continued && !g_settings.sharedResources && Player2_IsActive()) {
            *Game::At<uint32_t>(Game::kPower) = 128;
            Player2_Resources()->power = 128;
            uint32_t* hud = Game::At<uint32_t>(Game::kHudDirtyFlags);
            *hud = (*hud & ~0x10u) | 0x20u; // power changed
            ModLog("CoopRules: continue -- both players at full power");
        }
    }
    g_state.lastGameOver = gameOver;

    if (g_settings.reviveSeconds <= 0 || g_state.downed[0] == g_state.downed[1]) {
        g_state.reviveTimer = 0;
        return;
    }
    const uint8_t* survivor = PlayerByIndex(g_state.downed[0] ? 1 : 0);
    if (survivor[Game::kPlayerState] == 2) {
        g_state.reviveTimer = 0; // the survivor died: start over
        return;
    }
    if (++g_state.reviveTimer >= g_settings.reviveSeconds * 60) {
        g_state.reviveTimer = 0;
        Revive(g_state.downed[0] ? 0 : 1, 1);
    }
}

uint64_t Detour_PlayerUpdate(uint8_t* player, uint64_t secondArg) {
    int index = PlayerIndex(player);
    if (index < 0) return g_origPlayerUpdate(player, secondArg);
    if (index == 0) TickRevive();
    // Diagnostic (docs/11): the game-over flag as each downed player sees it.
    if (g_state.downed[index] && g_downedLogsLeft > 0 && (g_downedLogTimer++ % 120) == 0) {
        g_downedLogsLeft--;
        ModLog("Diag: P%d downed -- game over flag %d, continues %d, lives %d, score %u", index + 1, GameOverFlag(),
               *Game::At<uint8_t>(Game::kContinuesUsed), *Game::At<uint8_t>(Game::kLives),
               *Game::At<uint32_t>(Game::kScore));
    }
    if (g_state.downed[index]) {
        ParkDowned(player);
        return 1;
    }

    uint8_t gameOverBefore = GameOverFlag();
    uint8_t bombingBefore = player[Game::kPlayerBombing];
    // In play (normal or invulnerable): remember the power -- inside P2's
    // update the resource globals hold P2's pool (player2.cpp ResourceScope).
    if (player[Game::kPlayerState] == 0 || player[Game::kPlayerState] == 3) {
        g_state.lastAlivePower[index] = *Game::At<uint32_t>(Game::kPower);
    }
    uint32_t inputNow = *Game::At<uint32_t>(Game::kInputCurrent);
    uint32_t inputBefore = *Game::At<uint32_t>(Game::kInputPrevious);
    uint64_t result = g_origPlayerUpdate(player, secondArg);
    // Diagnostic (docs/11): the first test reported unprompted bombs.
    if (!bombingBefore && player[Game::kPlayerBombing] && g_bombLogsLeft > 0) {
        g_bombLogsLeft--;
        ModLog("Diag: P%d started a bomb -- input now %03X, last frame %03X, bombs left %d, state %d, respawn timer %d",
               index + 1, inputNow, inputBefore, *Game::At<uint8_t>(Game::kBombs), player[Game::kPlayerState],
               *reinterpret_cast<int32_t*>(player + Game::kPlayerRespawnTimer));
    }
    if (!g_settings.sharedResources && Player2_IsActive() && !gameOverBefore && GameOverFlag()) {
        g_state.downed[index] = 1;
        g_state.reviveTimer = 0;
        ParkDowned(player);
        if (!g_state.downed[1 - index]) {
            GameOverFlag() = 0;
            ModLog("CoopRules: P%d is out of lives -- downed (revive in %d s)", index + 1, g_settings.reviveSeconds);
        } else {
            // Both out: the real game over. Leaving this player downed too
            // means a continue revives both from the fresh stock (TickRevive).
            ModLog("CoopRules: P%d is out of lives with the partner down -- game over", index + 1);
        }
    }
    return result;
}

} // namespace

bool CoopRules_Install() {
    struct HookSpec {
        uintptr_t rva;
        void* detour;
        void** original;
        const char* name;
    };
    const HookSpec hooks[] = {
        { Game::kFnEnemyUpdate, reinterpret_cast<void*>(&Detour_EnemyUpdate), reinterpret_cast<void**>(&g_origEnemyUpdate), "EnemyUpdate" },
        { Game::kFnPlayerUpdate, reinterpret_cast<void*>(&Detour_PlayerUpdate), reinterpret_cast<void**>(&g_origPlayerUpdate), "PlayerUpdate" },
    };
    bool ok = true;
    for (const HookSpec& h : hooks) {
        ok &= Hooks_Install(h.rva, h.detour, h.original, h.name);
    }
    return ok;
}

bool CoopSettings_Equal(const CoopSettings& a, const CoopSettings& b) {
    return a.bossHpMultiplier == b.bossHpMultiplier && a.invincible == b.invincible && a.targeting == b.targeting &&
           a.sharedResources == b.sharedResources && a.reviveSeconds == b.reviveSeconds &&
           a.startLives[0] == b.startLives[0] && a.startLives[1] == b.startLives[1] &&
           a.startBombs[0] == b.startBombs[0] && a.startBombs[1] == b.startBombs[1] &&
           a.startPower == b.startPower && a.startStage == b.startStage && a.startPoint == b.startPoint &&
           a.revivePower == b.revivePower;
}

void CoopRules_RecordShotDamage(int player, int damage, const float* enemyPos) {
    if (damage <= 0 || player < 0 || player > 1) return;
    const uint8_t* boss = FindBossSlot();
    if (!boss || reinterpret_cast<const uint8_t*>(enemyPos) != boss + Game::kEntityPos) return;
    g_state.bossDamage[g_state.bossDamageSlot][player] += damage;
}

void CoopRules_DpsText(char* out, int outSize, float* r, float* g, float* b) {
    out[0] = '\0';
    if (g_state.bossFrames <= 0 || !FindBossSlot()) return;
    int window = g_state.bossFrames < kDpsWindow ? g_state.bossFrames : kDpsWindow;
    if (window < 30) return;
    long long sum[2] = { 0, 0 };
    for (int i = 0; i < window; i++) {
        int slot = (g_state.bossDamageSlot - i + kDpsWindow) % kDpsWindow;
        sum[0] += g_state.bossDamage[slot][0];
        sum[1] += g_state.bossDamage[slot][1];
    }
    long long dps1 = sum[0] * 60 / window, dps2 = sum[1] * 60 / window;
    *r = 1.0f; *g = 0.55f; *b = 0.25f;
    if (Player2_IsActive()) {
        snprintf(out, outSize, "BOSS DPS %lld\nP1 %lld  P2 %lld", dps1 + dps2, dps1, dps2);
    } else {
        snprintf(out, outSize, "BOSS DPS %lld", dps1);
    }
}

void CoopRules_SetSettings(const CoopSettings& settings) {
    g_settings = settings;
    for (int i = 0; i < 2; i++) {
        if (g_settings.startLives[i] < -1 || g_settings.startLives[i] == 0 || g_settings.startLives[i] > 9) g_settings.startLives[i] = -1;
        if (g_settings.startBombs[i] < -1 || g_settings.startBombs[i] > 9) g_settings.startBombs[i] = -1;
    }
    // Online these come off the network: clamp everything (NaN included).
    if (!(g_settings.bossHpMultiplier >= 0.25f)) g_settings.bossHpMultiplier = 0.25f;
    if (g_settings.bossHpMultiplier > 8.0f) g_settings.bossHpMultiplier = 8.0f;
    if (g_settings.reviveSeconds < 0) g_settings.reviveSeconds = 0;
    if (g_settings.reviveSeconds > 600) g_settings.reviveSeconds = 600;
    if (g_settings.startPower < -1 || g_settings.startPower > 128) g_settings.startPower = -1;
    if (g_settings.startStage < 1 || g_settings.startStage > 6) g_settings.startStage = 1;
    if (g_settings.startPoint < 0 || g_settings.startPoint > kStartAtBoss) g_settings.startPoint = kStartAtStage;
    if (g_settings.targeting > kTargetAlternate) g_settings.targeting = kTargetNearest;
}

const CoopSettings& CoopRules_Settings() {
    return g_settings;
}

void CoopRules_SetOwnSettings(const CoopSettings& settings) {
    CoopRules_SetSettings(settings);
    g_ownSettings = g_settings;
}

const CoopSettings& CoopRules_OwnSettings() {
    return g_ownSettings;
}

void CoopRules_RestoreOwnSettings() {
    if (!CoopSettings_Equal(g_settings, g_ownSettings)) ModLog("CoopRules: back to this machine's own co-op settings");
    g_settings = g_ownSettings;
}

void* CoopRules_StateRegion(size_t* size) {
    *size = sizeof(g_state);
    return &g_state;
}

bool CoopRules_InvincibleNow() {
    return g_settings.invincible;
}

bool CoopRules_IsDowned(const uint8_t* player) {
    int index = PlayerIndex(player);
    return index >= 0 && g_state.downed[index] != 0;
}

int CoopRules_ReviveFramesLeft() {
    if (g_settings.reviveSeconds <= 0 || g_state.downed[0] == g_state.downed[1]) return -1;
    return g_settings.reviveSeconds * 60 - g_state.reviveTimer;
}

bool g_startPointPending = false;

void CoopRules_OnSceneInit(bool coopActive) {
    g_startPointPending = false;
    int32_t* stage = Game::At<int32_t>(Game::kStageNumber);
    uint8_t replay = *Game::At<uint8_t>(Game::kReplayFlag);
    uint8_t practice = *Game::At<uint8_t>(Game::kPracticeFlag);
    uint8_t spell = *Game::At<uint8_t>(Game::kSpellPracticeFlag);
    uint8_t continues = *Game::At<uint8_t>(Game::kContinuesUsed);
    uint32_t score = *Game::At<uint32_t>(Game::kScore);
    ModLog("CoopRules: scene init -- stage index %d, replay %d, practice %d, spell practice %d, continues %d, score %u,"
           " checkpoint stage %d point %d, co-op %d",
           *stage, replay, practice, spell, continues, score, g_settings.startStage, g_settings.startPoint,
           coopActive ? 1 : 0);
    // Solo runs use the checkpoint too, as a practice tool: the jump is the
    // same whether or not a second player is in the scene.
    if (g_settings.startStage <= 1 && g_settings.startPoint == kStartAtStage) return;

    // Stage index 0 at a scene init is always a new run from the menu: a
    // continue doesn't re-init the scene, and later stages have higher
    // indexes. The continues counter and score still hold the previous run's
    // values at this point (the start function doesn't reset them), so they
    // must not be consulted -- they blocked the checkpoint on every run after
    // the first in a session (docs/14).
    const char* why = nullptr;
    if (*stage != 0) why = "not the first stage of a run";
    else if (replay) why = "a replay";
    else if (practice) why = "stage practice";
    else if (spell) why = "spell practice";
    if (why) {
        ModLog("CoopRules: checkpoint not applied -- %s", why);
        return;
    }
    if (g_settings.startStage > 1) {
        *stage = g_settings.startStage - 1;
        ModLog("CoopRules: checkpoint -- the run starts at stage %d", g_settings.startStage);
    }
    g_startPointPending = g_settings.startPoint != kStartAtStage;
}

// Stage 1's timeline (docs/12) shows how a midboss looks: one spawn
// (2498: sub 20) followed by a thousand frames with no record at all,
// while ordinary waves come every few frames. So a "section" is a spawn
// record followed by a silence of at least kSectionGap frames, and the
// midboss is the section start with the longest silence before the boss
// intro. The boss is the first intro record (opcode 8) -- stage 1 has no
// 0xD marker.
const int kSectionGap = 300;

const int16_t* NextRecord(const int16_t* record) {
    if (record[3] <= 0) return nullptr;
    return reinterpret_cast<const int16_t*>(reinterpret_cast<const uint8_t*>(record) + record[3]);
}

const int16_t* FindBossRecord() {
    const int16_t* record = *Game::At<const int16_t*>(Game::kTimelineStart);
    for (int scanned = 0; record && record[0] >= 0 && scanned < 4096; scanned++, record = NextRecord(record)) {
        if (record[2] == Game::kTimelineOpBossIntro || record[2] == Game::kTimelineOpBossMarker) return record;
    }
    return nullptr;
}

const int16_t* FindMidbossRecord() {
    const int16_t* record = *Game::At<const int16_t*>(Game::kTimelineStart);
    const int16_t* best = nullptr;
    int bestGap = 0;
    for (int scanned = 0; record && record[0] >= 0 && scanned < 4096; scanned++) {
        if (record[2] == Game::kTimelineOpBossIntro || record[2] == Game::kTimelineOpBossMarker) break;
        const int16_t* next = NextRecord(record);
        if (!next) break;
        bool spawn = record[2] >= 0 && record[2] <= Game::kTimelineOpSpawnLast;
        int gap = next[0] >= 0 ? next[0] - record[0] : 0;
        if (spawn && gap >= kSectionGap) {
            ModLog("Timeline: section at time %d (sub %d), then %d silent frames", record[0], record[1], gap);
            if (gap > bestGap) {
                bestGap = gap;
                best = record;
            }
        }
        record = next;
    }
    return best;
}

// Logs the stage's timeline (time/opcode/arg per record) once per stage,
// to pick start points from real data (docs/12).
void LogTimeline() {
    const int16_t* record = *Game::At<const int16_t*>(Game::kTimelineStart);
    if (!record) return;
    char line[256] = {};
    size_t len = 0;
    int count = 0;
    for (int scanned = 0; record[0] >= 0 && scanned < 2000; scanned++) {
        int n = snprintf(line + len, sizeof(line) - len, " %d:%d/%d", record[0], record[2], record[1]);
        if (n < 0 || len + n >= sizeof(line) - 24) {
            ModLog("Timeline:%s", line);
            len = 0;
            line[0] = '\0';
            n = snprintf(line, sizeof(line), " %d:%d/%d", record[0], record[2], record[1]);
        }
        len += n;
        count++;
        if (record[3] <= 0) break;
        record = reinterpret_cast<const int16_t*>(reinterpret_cast<const uint8_t*>(record) + record[3]);
    }
    if (len) ModLog("Timeline:%s", line);
    ModLog("Timeline: %d records (time:opcode/arg; opcodes 0-7 spawn sub, 8 boss intro, 9 wait dialogue, 12 wait enemy, 13 boss marker)", count);
}

void CoopRules_AfterSceneInit() {
    LogTimeline();
    if (!g_startPointPending) return;
    g_startPointPending = false;
    const int16_t** target = Game::At<const int16_t*>(Game::kTimelineJumpTarget);
    const int16_t* record = nullptr;
    const char* name = "";
    if (g_settings.startPoint == kStartAtBoss) {
        record = FindBossRecord();
        name = "boss";
    } else if (g_settings.startPoint == kStartAtMidboss) {
        record = FindMidbossRecord();
        name = "midboss";
    }
    if (!record) {
        ModLog("CoopRules: start point '%s' -- this stage's timeline has no such section, starting normally", name);
        return;
    }
    *target = record;
    *Game::At<uint8_t>(Game::kTimelineJumpFlag) = 1;
    ModLog("CoopRules: start point '%s' -- timeline jumps to time %d (opcode %d, sub %d)", name, record[0],
           record[2], record[1]);
}

void CoopRules_ResetRun() {
    g_bombLogsLeft = 20;
    g_state.downed[0] = 0;
    g_state.downed[1] = 0;
    g_state.reviveTimer = 0;
    g_state.lastGameOver = 0;
    g_state.lastContinues = *Game::At<uint8_t>(Game::kContinuesUsed);
}

InvincibleScope::InvincibleScope(uint8_t* player) {
    if (!g_settings.invincible || player[Game::kPlayerState] != 0) return;
    m_player = player;
    m_player[Game::kPlayerState] = 3;
}

InvincibleScope::~InvincibleScope() {
    if (m_player) m_player[Game::kPlayerState] = 0;
}
