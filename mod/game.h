#pragma once

#include <cstdint>
#include <windows.h>

// Every address the mod depends on, as an RVA into th06nc.exe. Valid ONLY for
// the build identified by kExpectedTimeDateStamp/kExpectedSizeOfImage below;
// Fingerprint_Check() refuses to install anything on a different build (a
// Steam update relocates everything -- see overlay/44, overlay/54).
namespace Game {

const uint32_t kExpectedTimeDateStamp = 0x6AA8420A;
const uint32_t kExpectedSizeOfImage = 0xCBC000;

// Player struct (overlay/12, overlay/44). One global instance; everything the
// player's own code does is relative to a pointer to it (docs/00).
const uintptr_t kPlayerStruct = 0x549FF0;
const size_t kPlayerStructSize = 0xA2B8; // FUN_1400694d0 zeroes 0x7854 + 8 + 0x2A5C
const uintptr_t kPlayerPosX = 0x7730;
const uintptr_t kPlayerPosY = 0x7734;
const uintptr_t kPlayerLastEnemyHit = 0x7740; // float[3], homing-shot target
const uintptr_t kPlayerState = 0x7898;        // u8: 0 normal, 1/2 dying/respawning, 3 bombing
const uintptr_t kPlayerDeathCounter = 0x7858; // int32: frames into the death animation; respawn at > 0x1D
const uintptr_t kPlayerOutFlag = 0xA2B4;      // u8: set by the final death (out of lives)
const uintptr_t kPlayerBombing = 0x9EC8;
const uintptr_t kPlayerShotTimer = 0x40C;     // int32: -1 idle; set to 0 by a shoot press, counts up while held
const uintptr_t kPlayerShotSlots = 0x410;     // 80 shot slots of 0x170 bytes; +0x10 nonzero = in use
const uintptr_t kPlayerShotSlotStride = 0x170;
const int kPlayerShotSlotCount = 80;      // u8
// Shot slot fields the shot-damage function FUN_14006b420 reads (docs/17):
// a slot hits while state == 1 (flying) or its type is 2 (piercing); a hit
// sets state 2, and switches the slot's VM (+0x18) to the hit animation by
// adding 0x20 to the script id at VM+0x38 -- so the VM is gameplay state.
const uintptr_t kShotSlotState = 0x10;         // u16: 0 free, 1 flying, 2 hit, 3 = (type-3 shots)
const uintptr_t kShotSlotType = 0x12;          // u16
const uintptr_t kShotSlotPos = 0x13C;          // float x, y (z at +0x144)
const uintptr_t kShotSlotSize = 0x160;         // float half-width, half-height
const uintptr_t kPlayerNodePtrs = 0x78A8;     // 4 task-node pointers (P1's are read by teardown)

// Scheduler (overlay/08): priority-sorted doubly-linked lists of 64-byte
// task nodes. The player registers its update on the first list and its
// three draw passes on the second.
const uintptr_t kUpdateListHead = 0x53C090;
const uintptr_t kDrawListHead = 0x53C050;

// Input mask (overlay/08, re-verified overlay/44): current and previous frame.
// Classic Touhou bit layout.
const uintptr_t kInputCurrent = 0xABAE80;
const uintptr_t kInputPrevious = 0xABAE84;
const uint32_t kButtonShoot = 0x001;
const uint32_t kButtonBomb = 0x002;
const uint32_t kButtonFocus = 0x004;
const uint32_t kButtonMenu = 0x008;
const uint32_t kButtonUp = 0x010;
const uint32_t kButtonDown = 0x020;
const uint32_t kButtonLeft = 0x040;
const uint32_t kButtonRight = 0x080;

// Sprite/animation manager (docs/04), reached through a pointer global.
const uintptr_t kAnmManagerPtr = 0xABAC30;
const int kAnmSlots = 128;
const uintptr_t kAnmSlotFiles = 0x8;           // loaded file pointer per slot
const uintptr_t kAnmSlotBases = 0x408;         // int32 base ID per slot
const uintptr_t kAnmSprites = 0x608;           // 0x40-byte sprite entry per ID
const size_t kAnmSpriteEntrySize = 0x40;
const uintptr_t kAnmScriptBases = 0x20F90;     // int32 per ID
const uintptr_t kAnmScripts = 0x23090;         // script pointer per ID
const int kAnmMaxIds = 0x800;
const uintptr_t kFnAnmLoad = 0x2440;           // (unused, slot, path, baseId) -> 0 ok
// (path, fromDisk = 0, uint32 *outSize) -> malloc'd copy of the archive file
// with that file name (decrypted, decompressed), or 0 -- the anm loader's read.
// It searches the 16 archive slots of the file system object below.
const uintptr_t kFnReadArchiveFile = 0x3A0C0;
// The file system object: archive slots at +0x10 (16 pointers), their paths
// at +0x90 (0x20 each). The game uses slots 0 (th06IN.dat at startup, then
// the title's), 1, 2, 4, 6 (docs/16).
const uintptr_t kArchiveSystem = 0xC6DB90;
const uintptr_t kFnArchiveOpen = 0x7CFD0;      // (system, slot, path) -> 0 ok (also 0 if already open)
const uintptr_t kFnArchiveClose = 0x7CF20;     // (system, slot)
const uintptr_t kFnAnmUnload = 0x2BE0;         // (manager, slot)
const uintptr_t kFnAnmSetScript = 0x2B40;      // (unused, vm, scriptId)
const uintptr_t kFnAnmTick = 0x7020;           // (anmManager, vm) -> runs the VM's script one frame

// Player sprites: the player init loads data/player00.anm (Reimu) or
// player01.anm (Marisa) into slot 5 at base 0x420.
const int kPlayerSheetSlot = 5;
const int kPlayerSheetBase = 0x420;
const char* const kPlayerSheetPaths[2] = { "data/player00.anm", "data/player01.anm" };

// Per character/shot-type tables the player init reads (index = char*2+shot).
const uintptr_t kComboStats = 0x429000;        // 0x20 each -> player +0x7860
const size_t kComboStatsSize = 0x20;
const uintptr_t kComboBombFns = 0x426EB0;      // 0x10 each -> player +0x9ED0
const uintptr_t kConstTwo = 0x352AD0;          // sqrt of it divides diagonal speed

// Player fields the init sets per character.
const uintptr_t kPlayerShotFns = 0x7720;       // current unfocused/focused shot fns (16 bytes)
const uintptr_t kPlayerStats = 0x7860;         // speeds (+0x00), diagonal speeds (+0x08), shot fns (+0x10)
const uintptr_t kPlayerBombFns = 0x9ED0;       // bomb update, bomb draw
const uintptr_t kPlayerMainVm = 0x78C8;        // animation VM, script 0x420
// The two option orbs, left and right: the draw places them at +0x7880 /
// +0x788C (player x -/+ an offset that shrinks while focused) and draws them
// only while +0x79F8 is set and the state is 0 or 3.
const uintptr_t kPlayerOptionVmL = 0x9F38;     // script 0x4A0
const uintptr_t kPlayerOptionVmR = 0xA058;     // script 0x4A1
const uintptr_t kPlayerFocused = 0x785C;       // bool, focus held (set every update from input bit 4)

// Sprite VM (0x120 bytes) fields read by the sprite draw FUN_140006de0 ->
// FUN_140003c90 (docs/07).
const size_t kVmSize = 0x120;
const uintptr_t kShotSlotVm = 0x18;            // a shot slot's VM: the draw passes slot+0x18 (type at +0x10, logical pos at +0x13C)
const uintptr_t kVmFlags = 0xC4;               // u32: drawn only if bits 1 and 2 are both set
const uintptr_t kVmPos = 0xC8;                 // float[3]
const uintptr_t kVmScale = 0xE4;               // float[2]: x and y scale of the sprite
const uintptr_t kVmColor = 0xEC;               // D3DCOLOR (B, G, R, A bytes); 0 = not drawn
const uintptr_t kVmEndFlag = 0x8A;             // u16: set to 1 to make the script play its ending
const uintptr_t kVmScriptPtr = 0xF8;           // current script instruction; null = not running
const float kPlayerSpriteZ = 0.49f;            // the player draw's z for the main VM (0x3EFAE148)

// Enemy bullet array (overlay/60): 640 slots of 0x620 bytes.
const uintptr_t kBulletArray = 0x436EF8;
const int kBulletSlots = 640;
const uintptr_t kBulletStride = 0x620;
const uintptr_t kBulletPos = 0x30;       // float[3]
const uintptr_t kBulletState = 0x44;     // u16: 0 empty, 1 live, 2-4 spawning, 5 cancelled
const uintptr_t kBulletHitSize = 0x5F4;  // float[2], passed to the hit test by the bullet loop
const uint16_t kBulletStateLive = 1;
const uint16_t kBulletStateCancelled = 5;

// Functions.
const uintptr_t kFnRegisterPlayer = 0x694D0;   // Player::RegisterChain equivalent
const uintptr_t kFnSceneTeardown = 0x3CF20;    // cuts the player's (and others') task nodes
const uintptr_t kFnChainCut = 0x12E10;         // (unused, node) -> unlinks a node from either list
const uintptr_t kFnPlayerUpdate = 0x69F30;     // (player) -> per-frame player update
const uintptr_t kFnPlayerDrawBombFlash = 0x6B850; // (player) draw pass, priority 7
const uintptr_t kFnPlayerDraw = 0x6B930;       // (player) draw pass, priority 9
const uintptr_t kFnPlayerDrawOverlay = 0x6BB40; // (player) draw pass, priority 11
const uintptr_t kFnDrawVm = 0x6DE0;            // (unused, vm, char) -> draws one sprite VM
const uintptr_t kFnSetPlayfieldView = 0x7CAE0; // (view, minZ, maxZ, char) -> flush batch, playfield viewport
const uintptr_t kPlayfieldView = 0xC6DB90;     // its first argument in every player draw
const uintptr_t kFnBulletUpdate = 0x10FA0;     // (bulletManager) bullet chain update
const uintptr_t kFnClearZoneTest = 0x6BFD0;    // (player, minX, minY, maxX, maxY) -> any bomb clear zone overlaps
const uintptr_t kFnPlayerHitTest = 0x6C090;    // (player, pos, size, mode) -> 0 none, 1 hit, 2 in a clear zone
const uintptr_t kFnShotDamage = 0x6B420;       // (player, enemyPos, enemySize, outBombFlag) -> damage
const uintptr_t kFnEnemyScript = 0x21F10;      // (scriptManager, enemy) runs one enemy's script for this frame
const uintptr_t kFnEnemyUpdate = 0x38CD0;      // (enemyManager) enemy chain update: scripts, movement, collisions
const uintptr_t kFnItemUpdate = 0x44240;       // (itemManager) item update, called from the bullet update
const uintptr_t kFnLaserHitTest = 0x6C2B0;     // (?, pos, size, origin, angle, grazeAllowed) vs P1's absolute fields

// Player-struct fields other code reads/writes by P1's absolute address,
// grouped by who needs them (docs/03).
const uintptr_t kPlayerRespawnTimer = 0x773C;
const uintptr_t kPlayerHitRadius = 0x774C;    // float, also the graze-radius term
const uintptr_t kPlayerDeathTimers = 0x7854;  // 8 bytes, written on death
const uintptr_t kPlayerDeathAnim = 0x79E8;    // 16 bytes, written on death
const uintptr_t kPlayerHurtbox = 0xA29C;      // float min[3], max[3]

// Read-only float constants in .rdata the collision code uses.
const uintptr_t kConstHalf = 0x352A44;             // scales hitbox sizes to half-extents
const uintptr_t kConstGrazeMargin = 0x352BC8;
const uintptr_t kConstItemCollectLine = 0x352C84;  // items home to a player above this y

const uintptr_t kGraze2 = 0x549D20;            // u32, second graze counter (lifetime)
const uintptr_t kHudDirtyFlags = 0xABAE38;     // u32
const uintptr_t kSoundQueueLocked = 0x5542E4;  // u8
const int kSoundGraze = 0x1E;

// Frame control (docs/02).
const uintptr_t kFnSimStep = 0x3D700;          // (outCode) walks the update list once = one simulation frame
const uintptr_t kFnInputPoll = 0x13310;        // () -> keyboard + XInput pad 0 mask
const uintptr_t kFnSoundFlush = 0x781D0;       // plays and clears the queued sound effects
const uintptr_t kFnGameplaySceneInit = 0x3C250; // (scene) consumes RNG, records the replay seed, registers the player
const uintptr_t kFnPlayBgm = 0x7D8D0;          // (context, path) -> 0; stage scripts and dialogue call it mid-stage
const uintptr_t kSoundQueue = 0x5542C0;        // int32[3], -1 = empty
const int kSoundQueueLength = 3;

// Session globals (overlay/17, overlay/44).
const uintptr_t kCharacter = 0x53CAD0;         // u8: 0 Reimu, 1 Marisa
const uintptr_t kShotType = 0x53CAD1;          // u8: 0 A, 1 B
const uintptr_t kDifficulty = 0x53D410;        // u8: 0-3, 4 = Extra
const uintptr_t kStageNumber = 0x53CAD4;       // int32, 0-based: a new game writes 0, Extra 6 (docs/11)
const uintptr_t kContinuesUsed = 0x549D2C;     // u8
const uintptr_t kLives = 0x549D40;             // u8
const uintptr_t kBombs = 0x549D41;             // u8
// Lives/bombs the run started with: FUN_14003c250 copies them at scene init
// except on the next-stage path (scene state 0xC6DFBC == 3), so they hold
// the values of the run's first stage all run long. A continue (the
// game-over task FUN_14000bc20) refills lives and bombs from them; a respawn
// refills bombs from kStageStartBombs (outside Extra and practice). Online
// the guest adopts the host's (docs/13).
const uintptr_t kStageStartLives = 0xC6E000;   // u8
const uintptr_t kStageStartBombs = 0xC6E001;   // u8
const uintptr_t kPower = 0x53CAD8;             // u32
const uintptr_t kScore = 0x53D3E8;             // u32
const uintptr_t kGraze = 0x549D1C;             // u32
const uintptr_t kRngCounter = 0xABAE60;        // u32
// Run-mode flags, set by the menu's start functions (docs/08):
// FUN_14004ede0 starts a replay (replay = 1, the rest from its header),
// FUN_14004f320 a game from the menu (replay = 0, practice from the menu),
// FUN_140053ad0 a spell practice (spell practice = 1, stage from the spell).
const uintptr_t kReplayFlag = 0x53D3DC;        // u8: a replay is playing back
const uintptr_t kPracticeFlag = 0x53D404;      // u8: stage practice
const uintptr_t kSpellPracticeFlag = 0x53D405; // u8: spell practice
// The options-menu game mode (option byte 0xC6DFDB, replay header byte 6):
// set by FUN_14004f320 before the scene init reads it; deaths don't cost
// lives and power never drops below 8. Online the guest adopts the host's
// (docs/17).
const uintptr_t kTrainingMode = 0x53D414;      // u8
// Stage timeline (docs/12): the ECL's timeline is a list of records of
// shorts {time, arg, opcode, size, ...}; time < 0 ends it. The enemy
// manager (0xAEE0B0, entities at +8) ticks it in FUN_140037b80 against the
// stage clock, running records whose time equals the clock. Setting the
// jump flag makes the next tick continue from the jump target record with
// the clock set to that record's time (the game's own "skip to the boss";
// the scene init points the target at the first opcode-0xD record).
const uintptr_t kTimelineStart = 0xABAD90;     // const short*: first record
const uintptr_t kTimelineJumpTarget = 0xABADA8; // short*: record to continue from, or null
const uintptr_t kTimelineJumpFlag = 0xABADB0;  // u8: apply the jump at the next tick
const uintptr_t kEnemyManager = 0xAEE0B0;
const uintptr_t kStageClock = 0xBFA16C;        // int32: enemy manager +0x10C0BC
const uintptr_t kTimelineCursor = 0xBFA1D8;    // short*: enemy manager +0x10C128
const int16_t kTimelineOpSpawnLast = 7;        // opcodes 0-7 spawn an enemy running sub `arg`
const int16_t kTimelineOpBossIntro = 8;        // boss dialogue `arg`
const int16_t kTimelineOpWaitEnemy = 0xC;      // hold the clock until enemy `arg` is gone
const int16_t kTimelineOpBossMarker = 0xD;     // the game's own jump target

const uintptr_t kGameOverFlag = 0x53D401;      // u8: set by a player's final death, cleared by continuing
const uintptr_t kRngSeed = 0xABAE64;           // u16

// The CRT's _set_FMA3_enable flag. sin/cos/atan2 and ~10 other math functions
// branch on it: nonzero picks FMA3 code paths, whose results differ from the
// SSE2 paths. A PC with FMA3 and one without would compute different bullet
// angles from identical inputs; 0 forces SSE2 everywhere.
const uintptr_t kCrtUseFma3 = 0xABAB9C;        // int32

// Heap objects holding simulation state, reached through a pointer global.
const uintptr_t kGuiObjectPtr = 0xABAE28;      // dialogue/HUD state; gates bombing during dialogue
// The focus marker (docs/13): two HUD sprite VMs the HUD tick moves onto
// Player 1 and starts on scripts 0x641/0x642 while P1 holds focus.
const uintptr_t kGuiFocusRingVm = 0x120;       // first of two VMs, 0x120 apart
const int kAnmScriptFocusRing = 0x641;
const size_t kGuiObjectSize = 0x4228;
const size_t kGuiFpsValue = 0x3D4C;            // float, the "60.00fps" readout (real time, not simulation)

// Regions within .data that must never be rolled back: audio state, and the
// input poll's own device-reading state (keyboard state buffer, key mapping
// table, gamepad flags, "held shoot means focus" counter). The simulation
// only sees the poll's returned mask, which netplay records; restoring the
// poll's internals would just make the next real device read inconsistent.
struct RvaRange {
    uintptr_t begin;
    uintptr_t end;
};
const RvaRange kNeverRestore[] = {
    { 0x5542B0, 0x5542E8 }, // BGM fade/handle + sound-effect queue
    { 0xC6E078, 0xC6E290 }, // gamepad flags, key mapping table, keyboard layout, keyboard state
    { 0xABAD70, 0xABAD74 }, // gamepad focus-hold counter
    { 0xABAC38, 0xABAC39 }, // poll flag
    { 0xABAE8C, 0xABAE8D }, // key mapping initialized
    // Sound-library (DxLib) state: buffer pointers reallocated as sounds
    // play. Restoring stale ones crashed the mixer (docs/11). Every code
    // reference into this block comes from library functions.
    { 0x5FD000, 0x5FE800 },
    // More library state (referenced only by library code, per
    // tools/ghidra_scripts/MapLibraryData): counters the sync test saw
    // advance with sound playback that a muted re-simulation skips.
    { 0x7E0098, 0x7E3D6C },
    { 0x7E60B0, 0x7FAACC },
    { 0x58E3BC, 0x58E7E8 },
    { 0x555000, 0x556200 }, // sound channel flags, set as effects play
    { 0x9881D8, 0x9886E8 },
    // Written by the heap-resident screen-shake task: cosmetic.
    { 0x53CAC0, 0x53CAD0 }, // camera / shake offsets
    { 0x549F70, 0x549F80 }, // shake amplitude
    // The std::vector of live shake states (begin, end, capacity): the
    // shake start pushes onto it, the shake's delete callback erases from it.
    // Its contents are heap, so its pointers must never be rewound (docs/14).
    { 0xC6E380, 0xC6E398 },
    // The main loop sets this byte every rendered frame while the Steam
    // overlay is open; netplay turns it into a synchronized pause (docs/14).
    { 0xC6DB90, 0xC6DB91 },
};

// The screen shake (docs/11): FUN_140077890 allocates a state block and a
// task node (priority 0xE) on the heap and inserts the node into the update
// list; the node's tick writes the camera offsets from random numbers. A
// snapshot can't restore either allocation, so the tick is given a private
// random sequence (sim_control.cpp) and its outputs are never restored.
// Only the four bomb functions start one (docs/14).
const uintptr_t kFnScreenShakeTick = 0x77AB0;
const uintptr_t kFnScreenShakeStart = 0x77890; // (unused, duration, strength, ...) -> state or 0; callers ignore it

// Pause (docs/14). The gameplay scene object is the static at 0x53CAB0 (its
// +0x24 is the stage index); its +0x950 byte is the pause state. The scene
// task (FUN_14003baa0) opens the pause menu on a fresh press of input bit
// 0x400 (Esc gives 0x600: pause + cancel) or while the overlay byte is set,
// and returns 3 while paused, which ends that frame's update walk early.
// The pause menu itself (FUN_14000a850, run by the menu task on the static
// object 0x429270) reads P1's input word. Nothing is heap-allocated.
const uintptr_t kPauseState = 0x53D400;       // u8, scene +0x950
const uintptr_t kOverlayPauseRequest = 0xC6DB90; // u8, set by the main loop while the overlay is open
const uint32_t kInputPause = 0x400;
const uint32_t kInputCancel = 0x200;
const uint32_t kInputQuickQuit = 0x800;       // Q, in the pause menu
const uint32_t kInputQuickRetry = 0x4000;     // R, in the pause menu

// HUD draw (the draw-list task that draws the sidebar and P1's focus marker:
// the two VMs at GUI +0x120/+0x240, looped at 0x14003e8f1).
const uintptr_t kFnHudDraw = 0x3E600;
// Entity table (overlay/24, overlay/44) and item pool (overlay/60).
const uintptr_t kEntityTable = 0xAEE0B8;
const int kEntitySlots = 256;
const uintptr_t kEntityStride = 0x10B0;
const uintptr_t kEntityPos = 0xB0;             // float x, y
const uintptr_t kEntityFlags = 0xBC;           // high bit = active
const uintptr_t kEntityHp = 0x234;             // int32
const uintptr_t kEntityMaxHp = 0x238;          // int32
const uintptr_t kEntitySegmentBoundary = 0xA0; // int32, HP the current phase ends at
const uintptr_t kEntitySegmentMax = 0x23C;     // int32
const uintptr_t kEntityBossFlags = 0xBD;
const uint8_t kEntityBossBit = 0x08;           // the currently tracked boss
const uintptr_t kItemPool = 0xBFB2F8;
const int kItemSlots = 1024;
const uintptr_t kItemStride = 0x160;
const uintptr_t kItemActiveCount = 0xBFB2F0;   // u32, recounted by every item update (item [0] = in use, [0xC] = 1 homing)

const uint16_t kPriorityPlayerUpdate = 7;
const uint16_t kPriorityPlayerDrawBombFlash = 7;
const uint16_t kPriorityPlayerDraw = 9;
const uint16_t kPriorityPlayerDrawOverlay = 11;

inline uintptr_t Base() {
    static uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    return base;
}

template <typename T>
inline T* At(uintptr_t rva) {
    return reinterpret_cast<T*>(Base() + rva);
}

template <typename T>
inline T Fn(uintptr_t rva) {
    return reinterpret_cast<T>(Base() + rva);
}

inline uint8_t* Player1() {
    return At<uint8_t>(kPlayerStruct);
}

} // namespace Game
