#include "snapshot.h"
#include "game.h"
#include "mod_log.h"

#include <windows.h>
#include <cstring>
#include <vector>

namespace {

struct Range {
    uint32_t offset;
    uint32_t length;
};

struct ExtraRegion {
    void* ptr;          // fixed region, or nullptr for indirect
    uintptr_t pointerRva;
    size_t size;
    std::vector<uint8_t> skip; // 1 = volatile byte: neither restored nor compared
};

struct Slot {
    int frame = -1;
    uint8_t* data = nullptr;               // copy of .data
    std::vector<uint8_t> extras;           // extra regions, back to back
    std::vector<void*> indirectPointers;   // pointer values at save time, per extra region
};

uint8_t* g_dataBase = nullptr;
size_t g_dataSize = 0;
std::vector<uint8_t> g_volatile;           // 1 bit per .data byte
std::vector<Range> g_restoreRanges;
std::vector<ExtraRegion> g_extras;
size_t g_extrasSize = 0;
std::vector<Slot> g_slots;
int g_slotCount = 0;
uint8_t* g_calibrationBaseline = nullptr;
bool g_calibrating = false;

// ~9 MB per slot, so nothing is committed until netplay actually starts.
bool EnsureAllocated() {
    if (g_calibrationBaseline) return true;
    g_calibrationBaseline = static_cast<uint8_t*>(VirtualAlloc(nullptr, g_dataSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!g_calibrationBaseline) {
        ModLog("Snapshot: failed to allocate the calibration buffer (%zu bytes)", g_dataSize);
        return false;
    }
    g_slots.resize(static_cast<size_t>(g_slotCount));
    for (Slot& slot : g_slots) {
        slot.data = static_cast<uint8_t*>(VirtualAlloc(nullptr, g_dataSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        if (!slot.data) {
            ModLog("Snapshot: failed to allocate %zu bytes for a slot", g_dataSize);
            g_slots.clear();
            return false;
        }
    }
    ModLog("Snapshot: allocated %d slots of %zu bytes", g_slotCount, g_dataSize);
    return true;
}

// A value that points at committed memory outside the game module (and
// outside this DLL): a heap object the snapshot doesn't cover. Restoring
// such a slot only ever puts back a stale address (docs/11).
bool LooksLikeHeapPointer(uint64_t value) {
    if (value < 0x10000 || value >= 0x00007FF000000000ull || (value & 0xF) != 0) return false; // allocations are 16-aligned
    uintptr_t base = Game::Base();
    if (value >= base && value < base + Game::kExpectedSizeOfImage) return false;
    MEMORY_BASIC_INFORMATION info;
    if (VirtualQuery(reinterpret_cast<void*>(value), &info, sizeof(info)) != sizeof(info)) return false;
    if (info.State != MEM_COMMIT || (info.Protect & PAGE_GUARD) || info.Protect == PAGE_NOACCESS) return false;
    return info.Type != MEM_IMAGE;
}

// Simulation-owned pointers into stable heap data: a difference there is
// a desync to report, never a slot to stop restoring.
bool IsGameCursor(uintptr_t rva) {
    return rva == Game::kTimelineCursor || rva == Game::kTimelineJumpTarget;
}

bool IsVolatile(size_t i) {
    return (g_volatile[i >> 3] >> (i & 7)) & 1;
}

bool AlwaysRestored(size_t i) {
    uintptr_t rva = reinterpret_cast<uintptr_t>(g_dataBase) - Game::Base() + i;
    for (const Game::RvaRange& range : Game::kAlwaysRestore) {
        if (rva >= range.begin && rva < range.end) return true;
    }
    return false;
}

void MarkVolatile(size_t begin, size_t end) {
    for (size_t i = begin; i < end && i < g_dataSize; i++) {
        if (AlwaysRestored(i)) continue;
        g_volatile[i >> 3] |= static_cast<uint8_t>(1u << (i & 7));
    }
}

void MarkVolatileRva(uintptr_t beginRva, uintptr_t endRva) {
    uintptr_t dataRva = reinterpret_cast<uintptr_t>(g_dataBase) - Game::Base();
    if (endRva <= dataRva || beginRva >= dataRva + g_dataSize) return;
    size_t begin = beginRva > dataRva ? beginRva - dataRva : 0;
    MarkVolatile(begin, endRva - dataRva);
}

void MarkFixedDenylist() {
    MarkVolatileRva(Game::kUpdateListHead, Game::kUpdateListHead + 0x40);
    MarkVolatileRva(Game::kDrawListHead, Game::kDrawListHead + 0x40);
    for (const Game::RvaRange& range : Game::kNeverRestore) {
        MarkVolatileRva(range.begin, range.end);
    }
    // Player 1's sprite VMs (main sprite, the two orbs, each shot's): pure
    // animation state whose scripts draw on the animation manager's own
    // random source (a heap object outside the snapshot), so re-simulated
    // frames differ there cosmetically (docs/11). P2's are learned.
    uintptr_t p1 = Game::kPlayerStruct;
    MarkVolatileRva(p1 + Game::kPlayerMainVm, p1 + Game::kPlayerMainVm + Game::kVmSize);
    MarkVolatileRva(p1 + Game::kPlayerOptionVmL, p1 + Game::kPlayerOptionVmL + Game::kVmSize);
    MarkVolatileRva(p1 + Game::kPlayerOptionVmR, p1 + Game::kPlayerOptionVmR + Game::kVmSize);
    for (int i = 0; i < Game::kPlayerShotSlotCount; i++) {
        uintptr_t slot = p1 + Game::kPlayerShotSlots + static_cast<uintptr_t>(i) * Game::kPlayerShotSlotStride;
        MarkVolatileRva(slot + Game::kShotSlotVm, slot + Game::kShotSlotVm + Game::kVmSize);
    }
}

void BuildRestoreRanges() {
    g_restoreRanges.clear();
    size_t volatileBytes = 0;
    size_t i = 0;
    while (i < g_dataSize) {
        if (IsVolatile(i)) {
            volatileBytes++;
            i++;
            continue;
        }
        size_t start = i;
        while (i < g_dataSize && !IsVolatile(i)) i++;
        g_restoreRanges.push_back({ static_cast<uint32_t>(start), static_cast<uint32_t>(i - start) });
    }
    ModLog("Snapshot: %zu volatile bytes excluded, %zu restore ranges", volatileBytes, g_restoreRanges.size());
}

bool FindDataSection() {
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(Game::Base());
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(Game::Base() + dos->e_lfanew);
    IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, section++) {
        if (memcmp(section->Name, ".data", 6) == 0) {
            g_dataBase = reinterpret_cast<uint8_t*>(Game::Base() + section->VirtualAddress);
            g_dataSize = section->Misc.VirtualSize;
            return true;
        }
    }
    return false;
}

Slot* SlotFor(int frame) {
    if (g_slots.empty() || frame < 0) return nullptr;
    return &g_slots[static_cast<size_t>(frame) % g_slots.size()];
}

} // namespace

bool Snapshot_Init(int slotCount) {
    if (!FindDataSection()) {
        ModLog("Snapshot: .data section not found");
        return false;
    }
    g_volatile.assign(g_dataSize / 8 + 1, 0);
    MarkFixedDenylist();
    BuildRestoreRanges();
    g_slotCount = slotCount;
    ModLog("Snapshot: .data at %p, %zu bytes, %d slots (allocated on first use)", g_dataBase, g_dataSize, slotCount);
    return true;
}

void Snapshot_AddRegion(void* ptr, size_t size) {
    g_extras.push_back({ ptr, 0, size, std::vector<uint8_t>(size, 0) });
    g_extrasSize += size;
}

void Snapshot_AddIndirectRegion(uintptr_t pointerRva, size_t size) {
    g_extras.push_back({ nullptr, pointerRva, size, std::vector<uint8_t>(size, 0) });
    g_extrasSize += size;
}

void Snapshot_MarkExtraVolatile(size_t regionIndex, size_t begin, size_t end) {
    if (regionIndex >= g_extras.size()) return;
    ExtraRegion& region = g_extras[regionIndex];
    for (size_t i = begin; i < end && i < region.size; i++) region.skip[i] = 1;
}

void Snapshot_BeginCalibration() {
    if (!EnsureAllocated()) return;
    memcpy(g_calibrationBaseline, g_dataBase, g_dataSize);
    g_calibrating = true;
}

// Marks every byte that differs from the baseline as volatile, then makes
// the current contents the new baseline. Returns how many bytes became
// volatile that weren't before.
size_t DiffAgainstBaseline() {
    size_t newlyVolatile = 0;
    const uint64_t* current = reinterpret_cast<const uint64_t*>(g_dataBase);
    const uint64_t* baseline = reinterpret_cast<const uint64_t*>(g_calibrationBaseline);
    size_t words = g_dataSize / 8;
    for (size_t w = 0; w < words; w++) {
        if (current[w] == baseline[w]) continue;
        uint64_t diff = current[w] ^ baseline[w];
        for (size_t b = 0; b < 8; b++) {
            size_t i = w * 8 + b;
            if (((diff >> (b * 8)) & 0xFF) && !IsVolatile(i)) {
                MarkVolatile(i, i + 1);
                newlyVolatile++;
            }
        }
    }
    for (size_t i = words * 8; i < g_dataSize; i++) {
        if (g_dataBase[i] != g_calibrationBaseline[i] && !IsVolatile(i)) {
            MarkVolatile(i, i + 1);
            newlyVolatile++;
        }
    }
    memcpy(g_calibrationBaseline, g_dataBase, g_dataSize);
    return newlyVolatile;
}

void Snapshot_CalibrationSample() {
    if (!g_calibrating) return;
    DiffAgainstBaseline();
}

// Every 8-byte slot holding a heap address. Unused: see EndCalibration.
[[maybe_unused]] size_t ExcludeHeapPointerSlots() {
    size_t found = 0;
    const uint64_t* words = reinterpret_cast<const uint64_t*>(g_dataBase);
    size_t count = g_dataSize / 8;
    for (size_t w = 0; w < count; w++) {
        if (IsVolatile(w * 8) || !LooksLikeHeapPointer(words[w])) continue;
        MarkVolatile(w * 8, w * 8 + 8);
        found++;
    }
    return found;
}

void Snapshot_EndCalibration() {
    if (!g_calibrating) return;
    g_calibrating = false;
    // Not ExcludeHeapPointerSlots(): the stage timeline's cursor is a heap
    // pointer (into the loaded ECL) and must be restored, or a rewound
    // stage re-runs with its cursor already past the records its clock
    // says are due (docs/11). Library pointer blocks are denylisted by
    // address instead, and the sync test learns any slot that
    // re-simulates to a different allocation.
    BuildRestoreRanges();
}

bool g_drawLearningArmed = false;

void Snapshot_ArmDrawLearning() {
    if (!EnsureAllocated()) return;
    memcpy(g_calibrationBaseline, g_dataBase, g_dataSize);
    g_drawLearningArmed = true;
}

void Snapshot_LearnDrawChanges() {
    if (!g_drawLearningArmed) return;
    g_drawLearningArmed = false;
    size_t added = DiffAgainstBaseline();
    if (added > 0) {
        ModLog("Snapshot: drawing changed %zu more bytes -- now excluded from restores", added);
        BuildRestoreRanges();
    }
}

void Snapshot_Save(int frame) {
    if (!EnsureAllocated()) return;
    Slot* slot = SlotFor(frame);
    if (!slot) return;
    memcpy(slot->data, g_dataBase, g_dataSize);

    slot->extras.resize(g_extrasSize);
    slot->indirectPointers.resize(g_extras.size());
    size_t offset = 0;
    for (size_t i = 0; i < g_extras.size(); i++) {
        const ExtraRegion& region = g_extras[i];
        void* src = region.ptr ? region.ptr : *Game::At<void*>(region.pointerRva);
        slot->indirectPointers[i] = src;
        if (src) memcpy(slot->extras.data() + offset, src, region.size);
        offset += region.size;
    }
    slot->frame = frame;
}

bool Snapshot_Load(int frame) {
    Slot* slot = SlotFor(frame);
    if (!slot || slot->frame != frame) return false;

    for (const Range& range : g_restoreRanges) {
        memcpy(g_dataBase + range.offset, slot->data + range.offset, range.length);
    }

    size_t offset = 0;
    for (size_t i = 0; i < g_extras.size(); i++) {
        const ExtraRegion& region = g_extras[i];
        void* dst = region.ptr ? region.ptr : *Game::At<void*>(region.pointerRva);
        if (dst && dst == slot->indirectPointers[i]) {
            uint8_t* out = static_cast<uint8_t*>(dst);
            const uint8_t* saved = slot->extras.data() + offset;
            for (size_t b = 0; b < region.size; b++) {
                if (!region.skip[b]) out[b] = saved[b];
            }
        } else if (dst != slot->indirectPointers[i]) {
            ModLog("Snapshot: region %zu moved between save and load (%p -> %p), not restored",
                   i, slot->indirectPointers[i], dst);
        }
        offset += region.size;
    }
    return true;
}

bool Snapshot_Has(int frame) {
    Slot* slot = SlotFor(frame);
    return slot && slot->frame == frame;
}

void Snapshot_Clear() {
    for (Slot& slot : g_slots) slot.frame = -1;
}

size_t Snapshot_CompareLive(int frame, int maxReport, bool learnExtraDiffs) {
    Slot* slot = SlotFor(frame);
    if (!slot || slot->frame != frame) return 0;

    size_t differing = 0;
    int reported = 0;
    size_t pointerSlots = 0;
    uintptr_t dataRva = reinterpret_cast<uintptr_t>(g_dataBase) - Game::Base();
    for (const Range& range : g_restoreRanges) {
        const uint8_t* live = g_dataBase + range.offset;
        const uint8_t* saved = slot->data + range.offset;
        if (memcmp(live, saved, range.length) == 0) continue;
        for (uint32_t i = 0; i < range.length; i++) {
            if (live[i] == saved[i]) continue;
            if (learnExtraDiffs) {
                // A slot that re-simulated to a different heap address is a
                // per-frame allocation: stop restoring it (docs/11).
                size_t word = (range.offset + i) & ~static_cast<size_t>(7);
                uint64_t liveWord, savedWord;
                memcpy(&liveWord, g_dataBase + word, 8);
                memcpy(&savedWord, slot->data + word, 8);
                if (LooksLikeHeapPointer(liveWord) && LooksLikeHeapPointer(savedWord) && !IsGameCursor(dataRva + word)) {
                    if (!IsVolatile(word)) {
                        MarkVolatile(word, word + 8);
                        pointerSlots++;
                        if (reported < maxReport) {
                            ModLog("  .data +0x%llX (rva 0x%llX) is a heap pointer that re-simulated differently -- now excluded",
                                   static_cast<unsigned long long>(word), static_cast<unsigned long long>(dataRva + word));
                            reported++;
                        }
                    }
                    i = static_cast<uint32_t>(word + 8 - range.offset) - 1;
                    continue;
                }
            }
            differing++;
            if (reported < maxReport) {
                ModLog("  diff .data +0x%llX (rva 0x%llX): expected %02X got %02X",
                       static_cast<unsigned long long>(range.offset + i),
                       static_cast<unsigned long long>(dataRva + range.offset + i), saved[i], live[i]);
                reported++;
            }
        }
    }

    size_t offset = 0;
    size_t learned = 0;
    for (size_t r = 0; r < g_extras.size(); r++) {
        ExtraRegion& region = g_extras[r];
        const uint8_t* live = static_cast<const uint8_t*>(region.ptr ? region.ptr : *Game::At<void*>(region.pointerRva));
        if (live && live == slot->indirectPointers[r]) {
            const uint8_t* saved = slot->extras.data() + offset;
            for (size_t i = 0; i < region.size; i++) {
                if (region.skip[i] || live[i] == saved[i]) continue;
                if (learnExtraDiffs) {
                    // Mod-owned regions and the GUI object hold no checksummed
                    // gameplay state; a byte that re-simulates differently
                    // there (the GUI's real-time FPS value, say) is treated
                    // like a draw-time write from now on.
                    region.skip[i] = 1;
                    learned++;
                    if (reported < maxReport) {
                        ModLog("  extra region %zu +0x%zX re-simulated differently (%02X vs %02X) -- now excluded",
                               r, i, saved[i], live[i]);
                        reported++;
                    }
                    continue;
                }
                differing++;
                if (reported < maxReport) {
                    ModLog("  diff extra region %zu +0x%zX: expected %02X got %02X", r, i, saved[i], live[i]);
                    reported++;
                }
            }
        }
        offset += region.size;
    }
    if (learned) ModLog("Snapshot: %zu bytes of extra regions now excluded from restores", learned);
    if (pointerSlots) {
        ModLog("Snapshot: %zu heap-pointer slots now excluded from restores", pointerSlots);
        BuildRestoreRanges();
    }
    return differing;
}
