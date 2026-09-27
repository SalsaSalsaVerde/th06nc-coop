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

bool IsVolatile(size_t i) {
    return (g_volatile[i >> 3] >> (i & 7)) & 1;
}

void MarkVolatile(size_t begin, size_t end) {
    for (size_t i = begin; i < end && i < g_dataSize; i++) {
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
    g_extras.push_back({ ptr, 0, size });
    g_extrasSize += size;
}

void Snapshot_AddIndirectRegion(uintptr_t pointerRva, size_t size) {
    g_extras.push_back({ nullptr, pointerRva, size });
    g_extrasSize += size;
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

void Snapshot_EndCalibration() {
    if (!g_calibrating) return;
    g_calibrating = false;
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
            memcpy(dst, slot->extras.data() + offset, region.size);
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

size_t Snapshot_CompareLive(int frame, int maxReport) {
    Slot* slot = SlotFor(frame);
    if (!slot || slot->frame != frame) return 0;

    size_t differing = 0;
    int reported = 0;
    uintptr_t dataRva = reinterpret_cast<uintptr_t>(g_dataBase) - Game::Base();
    for (const Range& range : g_restoreRanges) {
        const uint8_t* live = g_dataBase + range.offset;
        const uint8_t* saved = slot->data + range.offset;
        if (memcmp(live, saved, range.length) == 0) continue;
        for (uint32_t i = 0; i < range.length; i++) {
            if (live[i] == saved[i]) continue;
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
    for (size_t r = 0; r < g_extras.size(); r++) {
        const ExtraRegion& region = g_extras[r];
        const uint8_t* live = static_cast<const uint8_t*>(region.ptr ? region.ptr : *Game::At<void*>(region.pointerRva));
        if (live && live == slot->indirectPointers[r]) {
            const uint8_t* saved = slot->extras.data() + offset;
            for (size_t i = 0; i < region.size; i++) {
                if (live[i] == saved[i]) continue;
                differing++;
                if (reported < maxReport) {
                    ModLog("  diff extra region %zu +0x%zX: expected %02X got %02X", r, i, saved[i], live[i]);
                    reported++;
                }
            }
        }
        offset += region.size;
    }
    return differing;
}
