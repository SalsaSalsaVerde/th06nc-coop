#include "hooks.h"
#include "game.h"
#include "mod_log.h"

#include <MinHook.h>

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

bool Hooks_Install(uintptr_t rva, void* detour, void** original, const char* name) {
    static bool s_initialized = false;
    if (!s_initialized) {
        MH_STATUS init = MH_Initialize();
        if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
            ModLog("Hooks: MH_Initialize failed: %s", MH_StatusToString(init));
            return false;
        }
        s_initialized = true;
    }

    void* target = Game::At<void>(rva);
    MH_STATUS status = MH_CreateHook(target, detour, original);
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
