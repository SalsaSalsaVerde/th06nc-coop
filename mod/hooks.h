#pragma once

#include <cstdint>

// True only if the running th06nc.exe is the exact build every RVA in game.h
// was taken from. Logs the mismatch otherwise.
bool Fingerprint_Check();

// MH_Initialize once, then create+enable a hook at an RVA in th06nc.exe.
// `original` receives the trampoline. Logs and returns false on failure.
bool Hooks_Install(uintptr_t rva, void* detour, void** original, const char* name);
