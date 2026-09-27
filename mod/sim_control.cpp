#include "sim_control.h"
#include "game.h"
#include "hooks.h"
#include "mod_log.h"
#include "settings_panel.h"

namespace {

using SimStepFn = uint64_t (*)(uint32_t* outCode);
using InputPollFn = uint32_t (*)();
using SoundFlushFn = void (*)();
using PlayBgmFn = uint64_t (*)(void* context, const char* path);

SimStepFn g_origSimStep = nullptr;
InputPollFn g_origInputPoll = nullptr;
SoundFlushFn g_origSoundFlush = nullptr;
PlayBgmFn g_origPlayBgm = nullptr;

FrameDriver g_driver = nullptr;
bool g_forced = false;
bool g_muted = false;
uint32_t g_forcedP1 = 0;
uint32_t g_forcedP2 = 0;

// The draw pass takes random numbers from the game's one RNG (the screen
// shake after a bomb, for one), so a frame that is re-simulated without
// drawing would leave the RNG elsewhere than the live frame did, and two
// machines would drift apart with every rollback (docs/11). The RNG is put
// back to where the last simulation step left it before the next one runs.
uint16_t g_rngSeedAfterStep = 0;
uint32_t g_rngCounterAfterStep = 0;
bool g_rngSaved = false;

uint64_t Detour_SimStep(uint32_t* outCode) {
    uint16_t* seed = Game::At<uint16_t>(Game::kRngSeed);
    uint32_t* counter = Game::At<uint32_t>(Game::kRngCounter);
    if (g_rngSaved) {
        *seed = g_rngSeedAfterStep;
        *counter = g_rngCounterAfterStep;
    }
    uint64_t result = g_driver ? g_driver(outCode) : g_origSimStep(outCode);
    g_rngSeedAfterStep = *seed;
    g_rngCounterAfterStep = *counter;
    g_rngSaved = true;
    return result;
}

// The per-frame supervisor task does `previous = current; current = poll();`.
// While a forced step runs, P1's synchronized input replaces the device read.
uint32_t Detour_InputPoll() {
    if (g_forced) {
        return g_forcedP1;
    }
    return SettingsPanel_FilterInput(g_origInputPoll());
}

// Plays (and clears) up to 3 queued sound effects at the end of each update
// list walk. Re-simulated frames must not replay them.
void Detour_SoundFlush() {
    if (g_muted) {
        int32_t* queue = Game::At<int32_t>(Game::kSoundQueue);
        for (int i = 0; i < Game::kSoundQueueLength; i++) {
            queue[i] = -1;
        }
        return;
    }
    g_origSoundFlush();
}

// A re-simulated frame that starts a track (boss music) would restart it;
// the track already started when that frame first ran.
uint64_t Detour_PlayBgm(void* context, const char* path) {
    if (g_muted) return 0;
    return g_origPlayBgm(context, path);
}

} // namespace

bool SimControl_Install() {
    bool ok = true;
    ok &= Hooks_Install(Game::kFnSimStep, reinterpret_cast<void*>(&Detour_SimStep),
                        reinterpret_cast<void**>(&g_origSimStep), "SimStep");
    ok &= Hooks_Install(Game::kFnInputPoll, reinterpret_cast<void*>(&Detour_InputPoll),
                        reinterpret_cast<void**>(&g_origInputPoll), "InputPoll");
    ok &= Hooks_Install(Game::kFnSoundFlush, reinterpret_cast<void*>(&Detour_SoundFlush),
                        reinterpret_cast<void**>(&g_origSoundFlush), "SoundFlush", /*returnsValue=*/false);
    ok &= Hooks_Install(Game::kFnPlayBgm, reinterpret_cast<void*>(&Detour_PlayBgm),
                        reinterpret_cast<void**>(&g_origPlayBgm), "PlayBgm");
    return ok;
}

void SimControl_SetDriver(FrameDriver driver) {
    g_driver = driver;
}

uint64_t SimControl_StepNative(uint32_t* outCode) {
    return g_origSimStep(outCode);
}

uint64_t SimControl_StepForced(uint32_t* outCode, uint32_t p1Input, uint32_t p2Input, bool muted) {
    g_forced = true;
    g_muted = muted;
    g_forcedP1 = p1Input;
    g_forcedP2 = p2Input;
    uint64_t result = g_origSimStep(outCode);
    g_forced = false;
    g_muted = false;
    return result;
}

uint32_t SimControl_PollLocalDevices() {
    return g_origInputPoll();
}

uint32_t SimControl_ForcedPlayer2Input() {
    return g_forced ? g_forcedP2 : 0;
}
