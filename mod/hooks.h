#pragma once

#include <cstdint>

// True only if the running th06nc.exe is the exact build every RVA in game.h
// was taken from. Logs the mismatch otherwise.
bool Fingerprint_Check();

// MH_Initialize once, then create+enable a hook at an RVA in th06nc.exe.
// `original` receives the trampoline. Logs and returns false on failure.
//
// The detour is entered through a register-preserving stub (hook_thunks.asm,
// docs/11): the game's callers may keep values in volatile registers across
// calls to internal functions, which a plain C++ detour would clobber.
// `returnsValue` is false for a hooked function that returns nothing, so the
// stub restores RAX as well.
bool Hooks_Install(uintptr_t rva, void* detour, void** original, const char* name, bool returnsValue = true);
