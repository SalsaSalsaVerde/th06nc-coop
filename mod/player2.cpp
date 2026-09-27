#include "player2.h"
#include "player_look.h"
#include "anm_tables.h"
#include "config.h"
#include "coop_rules.h"
#include "game.h"
#include "game_chain.h"
#include "hooks.h"
#include "local_input.h"
#include "mod_log.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace {

using RegisterPlayerFn = uint64_t (*)();
using SceneTeardownFn = uint64_t (*)(int64_t);
using BulletUpdateFn = uint64_t (*)(int32_t* bulletManager);
using ItemUpdateFn = uint64_t (*)(void* itemManager);
using EnemyScriptFn = uint64_t (*)(void* scriptManager, uint8_t* enemy);
using ClearZoneTestFn = uint64_t (*)(uint8_t* player, float minX, float minY, float maxX, float maxY);
using PlayerHitTestFn = uint64_t (*)(uint8_t* player, float* pos, float* size, char mode);
using LaserHitTestFn = uint64_t (*)(uint64_t unused, float* pos, float* size, float* origin, float angle, char grazeAllowed);
using ShotDamageFn = int (*)(uint8_t* player, float* enemyPos, float* enemySize, uint8_t* outBombFlag);
using PlayerCallbackFn = uint64_t (*)(void* player);

RegisterPlayerFn g_origRegisterPlayer = nullptr;
SceneTeardownFn g_origSceneTeardown = nullptr;
BulletUpdateFn g_origBulletUpdate = nullptr;
ItemUpdateFn g_origItemUpdate = nullptr;
EnemyScriptFn g_origEnemyScript = nullptr;
ClearZoneTestFn g_origClearZoneTest = nullptr;
PlayerHitTestFn g_origPlayerHitTest = nullptr;
LaserHitTestFn g_origLaserHitTest = nullptr;
ShotDamageFn g_origShotDamage = nullptr;

// Everything about P2 that is simulation state lives here, so one snapshot
// region covers it (docs/02).
struct Player2State {
    alignas(16) uint8_t player[0xA2C0];
    uint32_t previousInput;
    uint8_t grazedBullets[Game::kBulletSlots / 8]; // P2's per-bullet graze flag (P1's is in the bullet)
    uint8_t lastItemCollector;                      // 0 = P1, 1 = P2
    uint8_t itemCollectorToggle;
    PlayerResources resources;                      // separate-resources mode only
};
static_assert(sizeof(Player2State::player) >= Game::kPlayerStructSize, "P2 buffer too small");

Player2State g_state;
uint8_t* const g_p2 = g_state.player;

enum NodeIndex { kNodeUpdate, kNodeDrawBombFlash, kNodeDraw, kNodeDrawOverlay, kNodeCount };
TaskNode g_nodes[kNodeCount];

bool g_active = false;
bool g_swapped = false; // P2's fields are currently sitting in P1's slots
Player2InputProvider g_inputProvider = &LocalInput_PollPlayer2;
Player2Listener g_listener;

// ---- P2 as a different character (docs/04) ---------------------------------

int g_wantedCharacter = -1;
bool g_enabled = true; // same-machine P2 ([player2] enabled, settings panel)
bool g_inStage = false;
int g_wantedShot = -1;

struct Sheet {
    bool differs = false;            // P2's character or shot type differs from P1's
    bool active = false;             // P2 uses its own sprite sheet (different character)
    int slot = -1;
    uint8_t character = 0;
    uint8_t shotType = 0;
    AnmTables::Entries p1Entries;    // the ID-table entries P1 draws with
    AnmTables::Entries p2Entries;    // the same IDs as P2 draws them
};
Sheet g_sheet;

// Stage-start P1 IDs live around base 0x420; this window comfortably covers
// both player sheets and is restored as a whole after loading P2's.
const int kSheetWindowBegin = 0x400;
const int kSheetWindowEnd = 0x600;

std::vector<int> SheetWindowIds() {
    std::vector<int> ids;
    for (int id = kSheetWindowBegin; id < kSheetWindowEnd; id++) ids.push_back(id);
    return ids;
}

int FindFreeAnmSlot() {
    for (int slot = Game::kAnmSlots - 1; slot > Game::kPlayerSheetSlot; slot--) {
        if (!AnmTables::IsSlotLoaded(slot)) return slot;
    }
    return -1;
}

// Loads the other character's sheet into a spare slot at the same base ID,
// captures the ID-table entries it registered, then puts P1's back exactly
// as they were.
bool LoadSheet(uint8_t character) {
    int slot = FindFreeAnmSlot();
    if (slot < 0) {
        ModLog("Player2: no free sprite slot for a second character sheet");
        return false;
    }
    std::vector<int> p1Ids = AnmTables::IdsOfSlot(Game::kPlayerSheetSlot);
    AnmTables::Entries before = AnmTables::Capture(SheetWindowIds());
    if (!AnmTables::LoadSlot(slot, Game::kPlayerSheetPaths[character], Game::kPlayerSheetBase)) {
        AnmTables::Apply(before);
        ModLog("Player2: loading %s into slot %d failed", Game::kPlayerSheetPaths[character], slot);
        return false;
    }
    std::vector<int> p2Ids = AnmTables::IdsOfSlot(slot);

    std::vector<int> ids = p1Ids;
    ids.insert(ids.end(), p2Ids.begin(), p2Ids.end());
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());

    g_sheet.p2Entries = AnmTables::Capture(ids);
    for (size_t i = 0; i < ids.size(); i++) {
        if (std::binary_search(p2Ids.begin(), p2Ids.end(), ids[i])) continue;
        memset(&g_sheet.p2Entries.sprites[i * Game::kAnmSpriteEntrySize], 0, Game::kAnmSpriteEntrySize);
        g_sheet.p2Entries.scripts[i] = 0;
        g_sheet.p2Entries.scriptBases[i] = 0;
    }
    AnmTables::Apply(before);
    g_sheet.p1Entries = AnmTables::Capture(ids);
    g_sheet.slot = slot;
    ModLog("Player2: loaded %s into sprite slot %d (%zu shared IDs)", Game::kPlayerSheetPaths[character], slot, ids.size());
    return true;
}

void UnloadSheet() {
    if (g_sheet.slot < 0) return;
    // The unloader clears every ID the file registered, which overlaps P1's.
    AnmTables::Entries keep = AnmTables::Capture(SheetWindowIds());
    AnmTables::UnloadSlot(g_sheet.slot);
    AnmTables::Apply(keep);
    g_sheet = Sheet();
}

// Swaps in P2's sprite IDs and character globals for one of P2's own
// callbacks, and back out afterwards.
struct SheetScope {
    uint8_t savedCharacter = 0;
    uint8_t savedShot = 0;
    SheetScope() {
        if (!g_sheet.differs) return;
        if (g_sheet.active) AnmTables::Apply(g_sheet.p2Entries);
        savedCharacter = *Game::At<uint8_t>(Game::kCharacter);
        savedShot = *Game::At<uint8_t>(Game::kShotType);
        *Game::At<uint8_t>(Game::kCharacter) = g_sheet.character;
        *Game::At<uint8_t>(Game::kShotType) = g_sheet.shotType;
    }
    ~SheetScope() {
        if (!g_sheet.differs) return;
        *Game::At<uint8_t>(Game::kCharacter) = savedCharacter;
        *Game::At<uint8_t>(Game::kShotType) = savedShot;
        if (g_sheet.active) AnmTables::Apply(g_sheet.p1Entries);
    }
    SheetScope(const SheetScope&) = delete;
    SheetScope& operator=(const SheetScope&) = delete;
};

// What the player init does per character, applied to P2's copy: speeds and
// shot functions from the per-combo stats table, bomb functions from the
// bomb table, then the animation VMs restarted on P2's own scripts.
void ApplyCharacter(uint8_t character, uint8_t shotType) {
    int combo = character * 2 + shotType;
    memcpy(g_p2 + Game::kPlayerStats, Game::At<uint8_t>(Game::kComboStats + combo * Game::kComboStatsSize),
           Game::kComboStatsSize);
    float diagonalDivisor = sqrtf(*Game::At<float>(Game::kConstTwo));
    float* speeds = reinterpret_cast<float*>(g_p2 + Game::kPlayerStats);
    speeds[2] = speeds[0] / diagonalDivisor;
    speeds[3] = speeds[1] / diagonalDivisor;
    memcpy(g_p2 + Game::kPlayerShotFns, g_p2 + Game::kPlayerStats + 0x10, 16);
    memcpy(g_p2 + Game::kPlayerBombFns, Game::At<uint8_t>(Game::kComboBombFns + combo * 0x10), 16);

    using SetScriptFn = void (*)(void* unused, uint8_t* vm, int scriptId);
    auto setScript = Game::Fn<SetScriptFn>(Game::kFnAnmSetScript);
    SheetScope scope;
    setScript(nullptr, g_p2 + Game::kPlayerMainVm, Game::kPlayerSheetBase);
    setScript(nullptr, g_p2 + Game::kPlayerOptionVmL, 0x4A0);
    setScript(nullptr, g_p2 + Game::kPlayerOptionVmR, 0x4A1);
}

// Sets P2 up as the wanted character if it differs from P1's; falls back to
// a plain copy of P1 on any failure.
void SetUpCharacter() {
    uint8_t p1Character = *Game::At<uint8_t>(Game::kCharacter);
    uint8_t p1Shot = *Game::At<uint8_t>(Game::kShotType);
    uint8_t character = g_wantedCharacter >= 0 ? static_cast<uint8_t>(g_wantedCharacter) : p1Character;
    uint8_t shotType = g_wantedShot >= 0 ? static_cast<uint8_t>(g_wantedShot) : p1Shot;
    if (character > 1 || shotType > 1 || (character == p1Character && shotType == p1Shot)) return;

    if (character != p1Character) {
        if (!LoadSheet(character)) return;
        g_sheet.active = true;
    }
    g_sheet.differs = true;
    g_sheet.character = character;
    g_sheet.shotType = shotType;
    ApplyCharacter(character, shotType);
    ModLog("Player2: character %s%c (P1 is %s%c)", character ? "Marisa" : "Reimu", 'A' + shotType,
           p1Character ? "Marisa" : "Reimu", 'A' + p1Shot);
}

// ---- "run this code as if P2 were P1" --------------------------------------
//
// Several native routines read and write the player through P1's absolute
// address instead of a parameter (enemy aim, items, lasers). For those, P2's
// relevant fields are exchanged into P1's struct for the duration of one
// call and exchanged back afterwards; anything the routine wrote to "P1"
// ends up in P2.

struct FieldRange {
    uintptr_t offset;
    size_t size;
};

const FieldRange kAimFields[] = {
    { Game::kPlayerPosX, 12 },
};
const FieldRange kItemFields[] = {
    { Game::kPlayerPosX, 12 },
    { Game::kPlayerState, 1 },
    { Game::kPlayerBombing, 1 },
    { Game::kPlayerHurtbox, 24 },
};
const FieldRange kLaserFields[] = {
    { Game::kPlayerPosX, 12 },
    { Game::kPlayerRespawnTimer, 4 },
    { Game::kPlayerHitRadius, 4 },
    { Game::kPlayerDeathTimers, 8 },
    { Game::kPlayerState, 1 },
    { Game::kPlayerDeathAnim, 16 },
    { Game::kPlayerBombing, 1 },
};

template <size_t N>
void ExchangeWithPlayer1(const FieldRange (&ranges)[N]) {
    uint8_t* p1 = Game::Player1();
    for (const FieldRange& r : ranges) {
        uint8_t tmp[32];
        memcpy(tmp, p1 + r.offset, r.size);
        memcpy(p1 + r.offset, g_p2 + r.offset, r.size);
        memcpy(g_p2 + r.offset, tmp, r.size);
    }
    g_swapped = !g_swapped;
}

template <size_t N, typename Call>
auto RunAsPlayer2(const FieldRange (&ranges)[N], Call call) {
    ExchangeWithPlayer1(ranges);
    auto result = call();
    ExchangeWithPlayer1(ranges);
    return result;
}

// ---- helpers ------------------------------------------------------------

bool IsPlayer1(uint8_t* player) {
    return player == Game::Player1();
}

uint8_t StateOf(const uint8_t* player) {
    return player[Game::kPlayerState];
}

float PosX(const uint8_t* player) {
    return *reinterpret_cast<const float*>(player + Game::kPlayerPosX);
}

float PosY(const uint8_t* player) {
    return *reinterpret_cast<const float*>(player + Game::kPlayerPosY);
}

// The native bullet loop skips its graze/hit logic while a player is dying
// or respawning (state 1 or 2), so bullets pass through them; P2 gets the
// same treatment. The same states -- and being downed (docs/06) -- make a
// player an invalid aim target.
bool IsAlive(const uint8_t* player) {
    uint8_t state = StateOf(player);
    return state != 1 && state != 2 && !CoopRules_IsDowned(player);
}

// Separate resources (docs/06): the game's lives/bombs/power globals hold
// P2's own values for the duration of anything P2 does that spends or gains
// them -- its update (deaths, bombs, shot power) and its item pickups.
struct ResourceScope {
    bool active;
    ResourceScope() : active(!CoopRules_Settings().sharedResources) {
        if (active) Exchange();
    }
    ~ResourceScope() {
        if (active) Exchange();
    }
    ResourceScope(const ResourceScope&) = delete;
    ResourceScope& operator=(const ResourceScope&) = delete;

    static void Exchange() {
        PlayerResources& own = g_state.resources;
        uint8_t* lives = Game::At<uint8_t>(Game::kLives);
        uint8_t* bombs = Game::At<uint8_t>(Game::kBombs);
        uint32_t* power = Game::At<uint32_t>(Game::kPower);
        uint8_t* respawnBombs = Game::At<uint8_t>(Game::kStageStartBombs);
        uint8_t l = *lives, b = *bombs, r = *respawnBombs;
        uint32_t p = *power;
        *lives = own.lives;
        *bombs = own.bombs;
        *power = own.power;
        *respawnBombs = own.respawnBombs;
        own.lives = l;
        own.bombs = b;
        own.power = p;
        own.respawnBombs = r;
    }
};

bool Player2CanBeHit() {
    return IsAlive(g_p2);
}

bool BitTest(const uint8_t* bits, int i) {
    return (bits[i >> 3] >> (i & 7)) & 1;
}

void BitSet(uint8_t* bits, int i, bool value) {
    if (value) bits[i >> 3] |= static_cast<uint8_t>(1u << (i & 7));
    else bits[i >> 3] &= static_cast<uint8_t>(~(1u << (i & 7)));
}

void QueueSound(int id) {
    if (*Game::At<uint8_t>(Game::kSoundQueueLocked) != 0) return;
    int32_t* queue = Game::At<int32_t>(Game::kSoundQueue);
    for (int i = 0; i < Game::kSoundQueueLength; i++) {
        if (queue[i] < 0) {
            queue[i] = id;
            return;
        }
        if (queue[i] == id) return;
    }
}

// ---- lifecycle ------------------------------------------------------------

// P2's update runs the game's own player update on P2's struct, with P2's
// input swapped into the global input words for the duration. Everything the
// update calls (shot patterns, bombs) reads input from those same words.
// Diagnostic (docs/11): the first test reported P2 moving but not shooting
// or focusing. While P2 holds shoot/focus/bomb, log what the update saw
// and did, once a second, a few times per stage.
int g_inputLogsLeft = 12;
int g_inputLogTimer = 0;

int ShotSlotsInUse(const uint8_t* player) {
    int used = 0;
    for (int i = 0; i < Game::kPlayerShotSlotCount; i++) {
        const uint8_t* slot = player + Game::kPlayerShotSlots + static_cast<uintptr_t>(i) * Game::kPlayerShotSlotStride;
        if (*reinterpret_cast<const uint16_t*>(slot + 0x10) != 0) used++;
    }
    return used;
}

void LogInputDiagnostic(uint32_t p2Input) {
    if (g_inputLogsLeft <= 0) return;
    bool acting = (p2Input & (Game::kButtonShoot | Game::kButtonFocus | Game::kButtonBomb)) != 0;
    if (!acting) {
        g_inputLogTimer = 0;
        return;
    }
    if (g_inputLogTimer++ % 60 != 0) return;
    g_inputLogsLeft--;
    ModLog("Diag: P2 input %03X -> state %d, shot timer %d, focused %d, bombing %d, shots in use %d (P1: %d), pos (%.0f, %.0f)",
           p2Input, g_p2[Game::kPlayerState], *reinterpret_cast<int32_t*>(g_p2 + Game::kPlayerShotTimer),
           g_p2[Game::kPlayerFocused], g_p2[Game::kPlayerBombing], ShotSlotsInUse(g_p2), ShotSlotsInUse(Game::Player1()),
           PosX(g_p2), PosY(g_p2));
}

uint64_t Player2UpdateTick(void* player) {
    uint32_t* current = Game::At<uint32_t>(Game::kInputCurrent);
    uint32_t* previous = Game::At<uint32_t>(Game::kInputPrevious);
    uint32_t savedCurrent = *current;
    uint32_t savedPrevious = *previous;

    uint32_t p2Input = g_inputProvider();
    *current = p2Input;
    *previous = g_state.previousInput;
    uint64_t result;
    {
        SheetScope sheet;
        ResourceScope resources;
        result = Game::Fn<PlayerCallbackFn>(Game::kFnPlayerUpdate)(player);
    }
    g_state.previousInput = p2Input;

    *current = savedCurrent;
    *previous = savedPrevious;
    LogInputDiagnostic(p2Input);
    return result;
}

template <uintptr_t DrawFnRva>
uint64_t Player2DrawTick(void* player) {
    SheetScope scope;
    return Game::Fn<PlayerCallbackFn>(DrawFnRva)(player);
}

void Despawn() {
    if (!g_active) return;
    for (TaskNode& node : g_nodes) {
        Chain_Cut(&node);
    }
    UnloadSheet();
    g_active = false;
    ModLog("Player2: despawned");
    if (g_listener.onDespawned) g_listener.onDespawned();
}

// A new run: the game just set lives/bombs from its own options. Apply the
// per-player starting stock from the co-op settings (docs/06); P2's pool
// starts as a copy of P1's.
void ApplyStartingStock() {
    const CoopSettings& s = CoopRules_Settings();
    uint8_t* lives = Game::At<uint8_t>(Game::kLives);
    uint8_t* bombs = Game::At<uint8_t>(Game::kBombs);
    if (s.startLives[0] > 0) *lives = static_cast<uint8_t>(s.startLives[0] - 1); // the byte doesn't count the life in play
    if (s.startBombs[0] >= 0) *bombs = static_cast<uint8_t>(s.startBombs[0]);
    if (s.startPower >= 0) *Game::At<uint32_t>(Game::kPower) = static_cast<uint32_t>(s.startPower);
    *Game::At<uint8_t>(Game::kStageStartLives) = *lives;
    *Game::At<uint8_t>(Game::kStageStartBombs) = *bombs;
    uint32_t* hud = Game::At<uint32_t>(Game::kHudDirtyFlags);
    *hud = (*hud & ~0x15u) | 0x2Au; // lives, bombs, power changed (the bits the player update sets)

    g_state.resources = { *lives, *bombs, *Game::At<uint32_t>(Game::kPower), *bombs };
    if (!s.sharedResources) {
        if (s.startLives[1] > 0) g_state.resources.lives = static_cast<uint8_t>(s.startLives[1] - 1);
        if (s.startBombs[1] >= 0) g_state.resources.bombs = static_cast<uint8_t>(s.startBombs[1]);
    }
}

void Spawn() {
    Despawn();

    memcpy(g_p2, Game::Player1(), Game::kPlayerStructSize);
    *reinterpret_cast<float*>(g_p2 + Game::kPlayerPosX) += Config_Get().player2SpawnOffsetX;
    SetUpCharacter();

    Chain_InitNode(&g_nodes[kNodeUpdate], Game::kPriorityPlayerUpdate, &Player2UpdateTick, g_p2);
    Chain_InitNode(&g_nodes[kNodeDrawBombFlash], Game::kPriorityPlayerDrawBombFlash,
                   &Player2DrawTick<Game::kFnPlayerDrawBombFlash>, g_p2);
    Chain_InitNode(&g_nodes[kNodeDraw], Game::kPriorityPlayerDraw,
                   &Player2DrawTick<Game::kFnPlayerDraw>, g_p2);
    Chain_InitNode(&g_nodes[kNodeDrawOverlay], Game::kPriorityPlayerDrawOverlay,
                   &Player2DrawTick<Game::kFnPlayerDrawOverlay>, g_p2);

    auto** nodePtrs = reinterpret_cast<TaskNode**>(g_p2 + Game::kPlayerNodePtrs);
    for (int i = 0; i < kNodeCount; i++) {
        nodePtrs[i] = &g_nodes[i];
    }

    Chain_Insert(Game::kUpdateListHead, &g_nodes[kNodeUpdate]);
    Chain_Insert(Game::kDrawListHead, &g_nodes[kNodeDrawBombFlash]);
    Chain_Insert(Game::kDrawListHead, &g_nodes[kNodeDraw]);
    Chain_Insert(Game::kDrawListHead, &g_nodes[kNodeDrawOverlay]);

    g_state.previousInput = 0;
    memset(g_state.grazedBullets, 0, sizeof(g_state.grazedBullets));
    g_state.lastItemCollector = 0;
    g_state.itemCollectorToggle = 0;
    // A run starts with score 0 (a continue sets it to the continue count),
    // so that's when P2's own resources start fresh -- from P1's, which the
    // game just set to the starting values. Later stages keep them.
    static bool s_resourcesInitialized = false;
    if (!s_resourcesInitialized || *Game::At<uint32_t>(Game::kScore) == 0) {
        ApplyStartingStock();
        s_resourcesInitialized = true;
        CoopRules_ResetRun();
    }
    // The scene init just recorded P1's stage-start bombs (what a respawn
    // refills to); record P2's the same way.
    g_state.resources.respawnBombs = g_state.resources.bombs;
    g_active = true;
    PlayerLook_OnStageStart();
    g_inputLogsLeft = 12;
    g_inputLogTimer = 0;
    ModLog("Player2: spawned at (%.1f, %.1f)", PosX(g_p2), PosY(g_p2));
    ModLog("Diag: at spawn P2 shot timer %d, respawn timer %d, bombing %d, shot fns %p/%p, bomb fn %p; P1 shot fns %p/%p",
           *reinterpret_cast<int32_t*>(g_p2 + Game::kPlayerShotTimer),
           *reinterpret_cast<int32_t*>(g_p2 + Game::kPlayerRespawnTimer), g_p2[Game::kPlayerBombing],
           *reinterpret_cast<void**>(g_p2 + Game::kPlayerShotFns), *reinterpret_cast<void**>(g_p2 + Game::kPlayerShotFns + 8),
           *reinterpret_cast<void**>(g_p2 + Game::kPlayerBombFns),
           *reinterpret_cast<void**>(Game::Player1() + Game::kPlayerShotFns),
           *reinterpret_cast<void**>(Game::Player1() + Game::kPlayerShotFns + 8));
    if (g_listener.onSpawned) g_listener.onSpawned();
}

uint64_t Detour_RegisterPlayer() {
    uint64_t result = g_origRegisterPlayer();
    if (result == 0) g_inStage = true;
    bool wanted = g_enabled || (g_listener.forceSpawn && g_listener.forceSpawn());
    // A replay only recorded P1's input: a second player would change what
    // happens and break it (docs/08).
    if (wanted && *Game::At<uint8_t>(Game::kReplayFlag)) {
        ModLog("Player2: replay playing back -- no second player");
        wanted = false;
    }
    if (result == 0 && wanted) {
        Spawn();
    }
    return result;
}

uint64_t Detour_SceneTeardown(int64_t arg) {
    uint64_t result = g_origSceneTeardown(arg);
    Despawn();
    g_inStage = false;
    return result;
}

// ---- bombs, hits, damage --------------------------------------------------

// A bullet inside ANY player's bomb clear zones is cancelled into a point
// item. The bullet loop, the hit test and the laser code all ask this about
// P1 only; answering for both players makes P2's bombs clear natively.
uint64_t Detour_ClearZoneTest(uint8_t* player, float minX, float minY, float maxX, float maxY) {
    uint64_t result = g_origClearZoneTest(player, minX, minY, maxX, maxY);
    if ((result & 0xFF) == 0 && g_active && !g_swapped && IsPlayer1(player)) {
        result = g_origClearZoneTest(g_p2, minX, minY, maxX, maxY);
    }
    return result;
}

// Enemy bodies and already-grazed bullets are tested against P1 here; test
// P2 too. The native test triggers the death sequence on whichever player
// instance it's given. 2 (inside a clear zone) outranks 1 (hit) so the
// caller still turns the bullet into an item.
uint64_t HitTestAs(uint8_t* player, float* pos, float* size, char mode) {
    InvincibleScope invincible(player);
    return g_origPlayerHitTest(player, pos, size, mode);
}

uint64_t Detour_PlayerHitTest(uint8_t* player, float* pos, float* size, char mode) {
    uint64_t r1 = HitTestAs(player, pos, size, mode);
    if (!g_active || g_swapped || !IsPlayer1(player) || !Player2CanBeHit()) return r1;
    uint64_t r2 = HitTestAs(g_p2, pos, size, mode);
    if (r1 == 2 || r2 == 2) return 2;
    return (r1 != 0 || r2 != 0) ? 1 : 0;
}

// Lasers test P1 through its absolute address (hit, graze, and the death
// sequence all inline). Run the same test again with P2 in P1's place. The
// caller ignores the result; everything happens inside.
uint64_t Detour_LaserHitTest(uint64_t unused, float* pos, float* size, float* origin, float angle, char grazeAllowed) {
    uint64_t result;
    {
        InvincibleScope invincible(Game::Player1());
        result = g_origLaserHitTest(unused, pos, size, origin, angle, grazeAllowed);
    }
    if (!g_active || g_swapped) return result;
    RunAsPlayer2(kLaserFields, [&] {
        InvincibleScope invincible(Game::Player1()); // P2's state sits in P1's slot here
        return g_origLaserHitTest(unused, pos, size, origin, angle, grazeAllowed);
    });
    return result;
}

// Called once per damageable enemy per frame. Adds P2's shots/options, and
// keeps P2's homing target updated the same way the enemy loop updates P1's.
int Detour_ShotDamage(uint8_t* player, float* enemyPos, float* enemySize, uint8_t* outBombFlag) {
    int damage = g_origShotDamage(player, enemyPos, enemySize, outBombFlag);
    if (!g_active || g_swapped || !IsPlayer1(player)) return damage;

    uint8_t p2BombFlag = 0;
    damage += g_origShotDamage(g_p2, enemyPos, enemySize, outBombFlag ? &p2BombFlag : nullptr);
    if (outBombFlag && p2BombFlag) *outBombFlag = 1;

    float* lastHit = reinterpret_cast<float*>(g_p2 + Game::kPlayerLastEnemyHit);
    if (lastHit[1] < enemyPos[1]) {
        lastHit[0] = enemyPos[0];
        lastHit[1] = enemyPos[1];
        lastHit[2] = enemyPos[2];
    }
    return damage;
}

// ---- bullets: P2 graze and hits -------------------------------------------

// Mirrors the bullet loop's inline graze block for P1 (FUN_140010fa0): graze
// radius = smaller bullet half-size + margin + the player's hit radius.
// Graze counters only count while that player isn't bombing; score and the
// graze sound always apply. The spark effect is skipped.
bool GrazeByPlayer2(uint8_t* bullet) {
    const float* pos = reinterpret_cast<const float*>(bullet + Game::kBulletPos);
    const float* size = reinterpret_cast<const float*>(bullet + Game::kBulletHitSize);
    float half = *Game::At<float>(Game::kConstHalf);
    float margin = *Game::At<float>(Game::kConstGrazeMargin);
    float hitRadius = *reinterpret_cast<const float*>(g_p2 + Game::kPlayerHitRadius);
    float smaller = size[0] <= size[1] ? size[0] : size[1];
    float radius = smaller * half + margin + hitRadius;
    float dx = PosX(g_p2) - pos[0];
    float dy = PosY(g_p2) - pos[1];
    if (dx * dx + dy * dy >= radius * radius) return false;

    if (g_p2[Game::kPlayerBombing] == 0) {
        int32_t* graze = Game::At<int32_t>(Game::kGraze);
        int32_t* graze2 = Game::At<int32_t>(Game::kGraze2);
        if (*graze < 99999) (*graze)++;
        if (*graze2 < 999999) (*graze2)++;
    }
    *Game::At<uint32_t>(Game::kScore) += 500;
    uint32_t* hud = Game::At<uint32_t>(Game::kHudDirtyFlags);
    *hud = (*hud & ~0x40u) | 0x80u;
    QueueSound(Game::kSoundGraze);
    return true;
}

// The bullet loop only grazes and hit-tests against P1 (inline code reading
// P1's absolute address, with the hit test gated behind P1's graze), so
// bullets reaching P2 without passing near P1 would never be tested. After
// the native update, run P2's graze and hit test over every live bullet.
// Cancelled bullets (state 5) are skipped, so a bullet already handled in
// the native pass isn't counted twice.
uint64_t Detour_BulletUpdate(int32_t* bulletManager) {
    static bool s_loggedBase = false;
    if (!s_loggedBase) {
        s_loggedBase = true;
        ModLog("Diag: bullet manager at %p (rva 0x%llX)", bulletManager,
               static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(bulletManager) - Game::Base()));
    }
    uint64_t result = g_origBulletUpdate(bulletManager);
    if (!g_active) return result;

    bool canBeHit = Player2CanBeHit();
    uint8_t* bullets = Game::At<uint8_t>(Game::kBulletArray);
    for (int i = 0; i < Game::kBulletSlots; i++) {
        uint8_t* bullet = bullets + static_cast<uintptr_t>(i) * Game::kBulletStride;
        uint16_t* state = reinterpret_cast<uint16_t*>(bullet + Game::kBulletState);
        if (*state != Game::kBulletStateLive) {
            BitSet(g_state.grazedBullets, i, false);
            continue;
        }
        if (!canBeHit) continue;
        if (!BitTest(g_state.grazedBullets, i) && GrazeByPlayer2(bullet)) {
            BitSet(g_state.grazedBullets, i, true);
        }
        uint64_t hit = HitTestAs(g_p2, reinterpret_cast<float*>(bullet + Game::kBulletPos),
                                 reinterpret_cast<float*>(bullet + Game::kBulletHitSize), 1);
        if (hit != 0) {
            *state = Game::kBulletStateCancelled;
        }
    }
    return result;
}

// ---- enemy aim ------------------------------------------------------------

// Every aim calculation in an enemy's script (script variables, aimed
// bullet patterns) reads P1's position by absolute address. Each enemy aims
// at the nearest living player (th06_multi_net's rule): when that's P2, P2's
// position sits in P1's slot for the duration of that one enemy's script.
bool EnemyTargetsPlayer2(const uint8_t* enemy) {
    if (!IsAlive(g_p2)) return false;
    if (!IsAlive(Game::Player1())) return true;
    switch (CoopRules_Settings().targeting) {
        case kTargetHost:
            return false;
        case kTargetAlternate: {
            uintptr_t offset = reinterpret_cast<uintptr_t>(enemy) - reinterpret_cast<uintptr_t>(Game::At<uint8_t>(Game::kEntityTable));
            return ((offset / Game::kEntityStride) & 1) != 0;
        }
        default:
            break;
    }
    const float* e = reinterpret_cast<const float*>(enemy + Game::kEntityPos);
    float d1x = PosX(Game::Player1()) - e[0], d1y = PosY(Game::Player1()) - e[1];
    float d2x = PosX(g_p2) - e[0], d2y = PosY(g_p2) - e[1];
    return d2x * d2x + d2y * d2y < d1x * d1x + d1y * d1y;
}

uint64_t Detour_EnemyScript(void* scriptManager, uint8_t* enemy) {
    if (!g_active || g_swapped || !EnemyTargetsPlayer2(enemy)) {
        return g_origEnemyScript(scriptManager, enemy);
    }
    return RunAsPlayer2(kAimFields, [&] { return g_origEnemyScript(scriptManager, enemy); });
}

// ---- items ----------------------------------------------------------------

// Items home toward "the player" (P1's absolute fields) once that player is
// above the collection line or bombing, and are collected by touching its
// hurtbox. One player collects per frame:
//   - whichever one qualifies for auto-collect (if both, keep the last one);
//   - if neither does, items still homing keep going to the last collector;
//   - otherwise alternate every frame, so both players can collect by touch.
bool QualifiesForAutoCollect(const uint8_t* player) {
    uint8_t state = StateOf(player);
    if (state == 3) return true;
    return state == 0 && PosY(player) < *Game::At<float>(Game::kConstItemCollectLine);
}

bool AnyItemHoming() {
    const uint8_t* item = Game::At<uint8_t>(Game::kItemPool);
    for (int i = 0; i < Game::kItemSlots; i++, item += Game::kItemStride) {
        if (item[0] != 0 && item[0xC] == 1) return true;
    }
    return false;
}

uint8_t ChooseItemCollector() {
    bool p1 = QualifiesForAutoCollect(Game::Player1());
    bool p2 = QualifiesForAutoCollect(g_p2);
    if (p1 != p2) {
        g_state.lastItemCollector = p2 ? 1 : 0;
        return g_state.lastItemCollector;
    }
    if (p1 || AnyItemHoming()) return g_state.lastItemCollector;
    g_state.itemCollectorToggle ^= 1;
    return g_state.itemCollectorToggle;
}

uint64_t Detour_ItemUpdate(void* itemManager) {
    if (!g_active || g_swapped || ChooseItemCollector() == 0) {
        return g_origItemUpdate(itemManager);
    }
    return RunAsPlayer2(kItemFields, [&] {
        ResourceScope resources;
        return g_origItemUpdate(itemManager);
    });
}

} // namespace

bool Player2_Install() {
    struct HookSpec {
        uintptr_t rva;
        void* detour;
        void** original;
        const char* name;
    };
    const HookSpec hooks[] = {
        { Game::kFnRegisterPlayer, reinterpret_cast<void*>(&Detour_RegisterPlayer), reinterpret_cast<void**>(&g_origRegisterPlayer), "RegisterPlayer" },
        { Game::kFnSceneTeardown, reinterpret_cast<void*>(&Detour_SceneTeardown), reinterpret_cast<void**>(&g_origSceneTeardown), "SceneTeardown" },
        { Game::kFnBulletUpdate, reinterpret_cast<void*>(&Detour_BulletUpdate), reinterpret_cast<void**>(&g_origBulletUpdate), "BulletUpdate" },
        { Game::kFnItemUpdate, reinterpret_cast<void*>(&Detour_ItemUpdate), reinterpret_cast<void**>(&g_origItemUpdate), "ItemUpdate" },
        { Game::kFnEnemyScript, reinterpret_cast<void*>(&Detour_EnemyScript), reinterpret_cast<void**>(&g_origEnemyScript), "EnemyScript" },
        { Game::kFnClearZoneTest, reinterpret_cast<void*>(&Detour_ClearZoneTest), reinterpret_cast<void**>(&g_origClearZoneTest), "ClearZoneTest" },
        { Game::kFnPlayerHitTest, reinterpret_cast<void*>(&Detour_PlayerHitTest), reinterpret_cast<void**>(&g_origPlayerHitTest), "PlayerHitTest" },
        { Game::kFnLaserHitTest, reinterpret_cast<void*>(&Detour_LaserHitTest), reinterpret_cast<void**>(&g_origLaserHitTest), "LaserHitTest" },
        { Game::kFnShotDamage, reinterpret_cast<void*>(&Detour_ShotDamage), reinterpret_cast<void**>(&g_origShotDamage), "ShotDamage" },
    };
    bool ok = true;
    for (const HookSpec& h : hooks) {
        ok &= Hooks_Install(h.rva, h.detour, h.original, h.name);
    }
    return ok;
}

void Player2_SetListener(const Player2Listener& listener) {
    g_listener = listener;
}

void* Player2_StateRegion(size_t* size) {
    *size = sizeof(g_state);
    return &g_state;
}

void Player2_SetInputProvider(Player2InputProvider provider) {
    g_inputProvider = provider ? provider : &LocalInput_PollPlayer2;
}

bool Player2_IsActive() {
    return g_active;
}

void Player2_SetEnabled(bool enabled) {
    g_enabled = enabled;
}

bool Player2_Enabled() {
    return g_enabled;
}

bool Player2_InStage() {
    return g_inStage;
}

int Player2_WantedCharacter() {
    return g_wantedCharacter;
}

int Player2_WantedShotType() {
    return g_wantedShot;
}

void Player2_SetLoadout(int character, int shotType) {
    g_wantedCharacter = character;
    g_wantedShot = shotType;
}

PlayerResources* Player2_Resources() {
    return &g_state.resources;
}

void Player2_CurrentLoadout(uint8_t* character, uint8_t* shotType) {
    if (g_sheet.differs) {
        *character = g_sheet.character;
        *shotType = g_sheet.shotType;
    } else {
        *character = *Game::At<uint8_t>(Game::kCharacter);
        *shotType = *Game::At<uint8_t>(Game::kShotType);
    }
}

uint8_t* Player2_Struct() {
    return g_p2;
}
