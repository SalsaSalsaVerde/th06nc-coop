#pragma once

#include <cstdint>
#include <vector>

// The game's sprite/animation manager (the object at *0xABAC30; th06's
// AnmManager). Loaded .anm files occupy numbered slots; each file registers
// its sprites and scripts in global ID tables at `base + localIndex`.
// Textures are per slot, so two files can be loaded at once as long as the
// ID table entries they both use are swapped in for whoever is drawing
// (docs/04).
namespace AnmTables {

// Snapshot of the ID-table entries for a set of IDs.
struct Entries {
    std::vector<int> ids;
    std::vector<uint8_t> sprites; // 0x40 per ID
    std::vector<uint64_t> scripts;
    std::vector<int32_t> scriptBases;
};

bool IsSlotLoaded(int slot);

// The IDs a loaded slot registered (sprites and scripts), exactly as the
// game's own unloader enumerates them.
std::vector<int> IdsOfSlot(int slot);

Entries Capture(const std::vector<int>& ids);
void Apply(const Entries& entries);

// The game's own loader/unloader (both also rewrite the ID tables).
bool LoadSlot(int slot, const char* path, int baseId);
void UnloadSlot(int slot);

} // namespace AnmTables
