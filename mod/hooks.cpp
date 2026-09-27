#include "hooks.h"
#include "game.h"
#include "mod_log.h"

#include <MinHook.h>

// hook_thunks.asm: 32 stubs, each calling g_hookThunkTargets[i].
extern "C" {
void* g_hookThunkTargets[32] = {};
uint8_t g_hookThunkVoid[32] = {};
#define THUNK_LIST(X)     X(0) X(1) X(2) X(3) X(4) X(5) X(6) X(7) X(8) X(9) X(10) X(11) X(12) X(13) X(14) X(15)     X(16) X(17) X(18) X(19) X(20) X(21) X(22) X(23) X(24) X(25) X(26) X(27) X(28) X(29) X(30) X(31)
#define DECLARE_THUNK(n) void HookThunk##n();
THUNK_LIST(DECLARE_THUNK)
}

namespace {
#define THUNK_ADDRESS(n) reinterpret_cast<void*>(&HookThunk##n),
void* const kThunks[] = { THUNK_LIST(THUNK_ADDRESS) };
const int kThunkCount = sizeof(kThunks) / sizeof(kThunks[0]);
int g_thunksUsed = 0;
} // namespace

bool Fingerprint_Check() {
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(Game::Base());
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(Game::Base() + dos->e_lfanew);
    uint32_t stamp = nt->FileHeader.TimeDateStamp;
    uint32_t size = nt->OptionalHeader.SizeOfImage;
    if (stamp != Game::kExpectedTimeDateStamp || size != Game::kExpectedSizeOfImage) {
        ModLog("Fingerprint: th06nc.exe build mismatch (TimeDateStamp=0x%08X SizeOfImage=0x%X, expected 0x%08X/0x%X)"
               " -- the game was probably updated; every address is stale, installing NOTHING",
               stamp, size, Game::kExpectedTimeDateStamp, Game::kExpectedSizeOfImage);
        return false;
    }
    ModLog("Fingerprint: th06nc.exe build recognized (0x%08X)", stamp);
    return true;
}

bool Hooks_Install(uintptr_t rva, void* detour, void** original, const char* name, bool returnsValue) {
    static bool s_initialized = false;
    if (!s_initialized) {
        MH_STATUS init = MH_Initialize();
        if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
            ModLog("Hooks: MH_Initialize failed: %s", MH_StatusToString(init));
            return false;
        }
        s_initialized = true;
    }

    if (g_thunksUsed >= kThunkCount) {
        ModLog("Hooks: out of thunk slots for %s -- raise the count in hook_thunks.asm and hooks.cpp", name);
        return false;
    }
    int slot = g_thunksUsed++;
    g_hookThunkTargets[slot] = detour;
    g_hookThunkVoid[slot] = returnsValue ? 0 : 1;

    void* target = Game::At<void>(rva);
    MH_STATUS status = MH_CreateHook(target, kThunks[slot], original);
    if (status != MH_OK) {
        ModLog("Hooks: MH_CreateHook(%s @ +0x%llX) failed: %s", name,
               static_cast<unsigned long long>(rva), MH_StatusToString(status));
        return false;
    }
    status = MH_EnableHook(target);
    if (status != MH_OK) {
        ModLog("Hooks: MH_EnableHook(%s) failed: %s", name, MH_StatusToString(status));
        return false;
    }
    ModLog("Hooks: installed %s @ +0x%llX", name, static_cast<unsigned long long>(rva));
    return true;
}
