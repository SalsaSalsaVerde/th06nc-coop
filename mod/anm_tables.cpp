#include "anm_tables.h"
#include "game.h"

#include <algorithm>
#include <cstring>

namespace AnmTables {

namespace {

uint8_t* Manager() {
    return *Game::At<uint8_t*>(Game::kAnmManagerPtr);
}

uint8_t* SlotFile(int slot) {
    uint8_t* mgr = Manager();
    if (!mgr || slot < 0 || slot >= Game::kAnmSlots) return nullptr;
    return *reinterpret_cast<uint8_t**>(mgr + Game::kAnmSlotFiles + slot * 8);
}

} // namespace

bool IsSlotLoaded(int slot) {
    return SlotFile(slot) != nullptr;
}

// Mirrors FUN_140002be0: the file header holds the sprite count (+0) and
// script count (+4); at +0x40 come one offset per sprite (each sprite
// record starts with its local index), then one {localIndex, offset} pair
// per script. Global ID = local index + the slot's base.
std::vector<int> IdsOfSlot(int slot) {
    std::vector<int> ids;
    uint8_t* mgr = Manager();
    uint8_t* file = SlotFile(slot);
    if (!mgr || !file) return ids;
    int base = *reinterpret_cast<int32_t*>(mgr + Game::kAnmSlotBases + slot * 4);
    int spriteCount = *reinterpret_cast<int32_t*>(file + 0);
    int scriptCount = *reinterpret_cast<int32_t*>(file + 4);
    const uint32_t* list = reinterpret_cast<const uint32_t*>(file + 0x40);
    for (int i = 0; i < spriteCount; i++) {
        ids.push_back(*reinterpret_cast<const int32_t*>(file + list[i]) + base);
    }
    const uint32_t* scripts = list + spriteCount;
    for (int i = 0; i < scriptCount; i++) {
        ids.push_back(static_cast<int>(scripts[i * 2]) + base);
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    ids.erase(std::remove_if(ids.begin(), ids.end(),
                             [](int id) { return id < 0 || id >= Game::kAnmMaxIds; }),
              ids.end());
    return ids;
}

Entries Capture(const std::vector<int>& ids) {
    Entries e;
    uint8_t* mgr = Manager();
    if (!mgr) return e;
    e.ids = ids;
    e.sprites.resize(ids.size() * Game::kAnmSpriteEntrySize);
    e.scripts.resize(ids.size());
    e.scriptBases.resize(ids.size());
    for (size_t i = 0; i < ids.size(); i++) {
        int id = ids[i];
        memcpy(&e.sprites[i * Game::kAnmSpriteEntrySize], mgr + Game::kAnmSprites + id * Game::kAnmSpriteEntrySize,
               Game::kAnmSpriteEntrySize);
        e.scripts[i] = *reinterpret_cast<uint64_t*>(mgr + Game::kAnmScripts + id * 8);
        e.scriptBases[i] = *reinterpret_cast<int32_t*>(mgr + Game::kAnmScriptBases + id * 4);
    }
    return e;
}

void Apply(const Entries& e) {
    uint8_t* mgr = Manager();
    if (!mgr) return;
    for (size_t i = 0; i < e.ids.size(); i++) {
        int id = e.ids[i];
        memcpy(mgr + Game::kAnmSprites + id * Game::kAnmSpriteEntrySize, &e.sprites[i * Game::kAnmSpriteEntrySize],
               Game::kAnmSpriteEntrySize);
        *reinterpret_cast<uint64_t*>(mgr + Game::kAnmScripts + id * 8) = e.scripts[i];
        *reinterpret_cast<int32_t*>(mgr + Game::kAnmScriptBases + id * 4) = e.scriptBases[i];
    }
}

bool LoadSlot(int slot, const char* path, int baseId) {
    using LoadFn = int (*)(void* unused, int slot, const char* path, int baseId);
    return Game::Fn<LoadFn>(Game::kFnAnmLoad)(nullptr, slot, path, baseId) == 0;
}

void UnloadSlot(int slot) {
    using UnloadFn = void (*)(uint8_t* manager, int slot);
    uint8_t* mgr = Manager();
    if (mgr) Game::Fn<UnloadFn>(Game::kFnAnmUnload)(mgr, slot);
}

} // namespace AnmTables
