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
    std::vector<uint8_t> pin;  // 1 = restored even when volatile (still not compared)
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
// Pinned bytes are simulation state that drawing also writes (the players'
// sprite VMs): excluded from comparison like any volatile byte, but always
// restored, because the game reads them back in gameplay (docs/17).
std::vector<uint8_t> g_pinned;             // 1 bit per .data byte
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

bool IsPinned(size_t i) {
    return (g_pinned[i >> 3] >> (i & 7)) & 1;
}

bool Restorable(size_t i) {
    return !IsVolatile(i) || IsPinned(i);
}

void MarkFixedDenylist() {
    MarkVolatileRva(Game::kUpdateListHead, Game::kUpdateListHead + 0x40);
    MarkVolatileRva(Game::kDrawListHead, Game::kDrawListHead + 0x40);
    for (const Game::RvaRange& range : Game::kNeverRestore) {
        MarkVolatileRva(range.begin, range.end);
    }
    // Player 1's sprite VMs (main sprite, the two orbs, each shot's) are
    // ticked by the draw passes, so re-simulated frames (no draws) differ
    // there: excluded from comparison (docs/11). They ARE restored: the
    // whole player struct is pinned (docs/17). P2's are learned.
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
    size_t volatileBytes = 0, pinnedVolatile = 0;
    size_t i = 0;
    while (i < g_dataSize) {
        if (!Restorable(i)) {
            volatileBytes++;
            i++;
            continue;
        }
        size_t start = i;
        while (i < g_dataSize && Restorable(i)) {
            if (IsVolatile(i)) pinnedVolatile++;
            i++;
        }
        g_restoreRanges.push_back({ static_cast<uint32_t>(start), static_cast<uint32_t>(i - start) });
    }
    ModLog("Snapshot: %zu volatile bytes excluded (%zu more restored as pinned), %zu restore ranges", volatileBytes,
           pinnedVolatile, g_restoreRanges.size());
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
    g_pinned.assign(g_dataSize / 8 + 1, 0);
    MarkFixedDenylist();
    BuildRestoreRanges();
    g_slotCount = slotCount;
    ModLog("Snapshot: .data at %p, %zu bytes, %d slots (allocated on first use)", g_dataBase, g_dataSize, slotCount);
    return true;
}

void Snapshot_AddRegion(void* ptr, size_t size) {
    g_extras.push_back({ ptr, 0, size, std::vector<uint8_t>(size, 0), std::vector<uint8_t>(size, 0) });
    g_extrasSize += size;
}

void Snapshot_AddIndirectRegion(uintptr_t pointerRva, size_t size) {
    g_extras.push_back({ nullptr, pointerRva, size, std::vector<uint8_t>(size, 0), std::vector<uint8_t>(size, 0) });
    g_extrasSize += size;
}

void Snapshot_MarkExtraVolatile(size_t regionIndex, size_t begin, size_t end) {
    if (regionIndex >= g_extras.size()) return;
    ExtraRegion& region = g_extras[regionIndex];
    for (size_t i = begin; i < end && i < region.size; i++) region.skip[i] = 1;
}

void Snapshot_PinRva(uintptr_t beginRva, uintptr_t endRva) {
    uintptr_t dataRva = reinterpret_cast<uintptr_t>(g_dataBase) - Game::Base();
    if (g_pinned.empty() || endRva <= dataRva || beginRva >= dataRva + g_dataSize) return;
    size_t begin = beginRva > dataRva ? beginRva - dataRva : 0;
    size_t end = endRva - dataRva < g_dataSize ? endRva - dataRva : g_dataSize;
    for (size_t i = begin; i < end; i++) g_pinned[i >> 3] |= static_cast<uint8_t>(1u << (i & 7));
    BuildRestoreRanges();
}

void Snapshot_PinExtra(size_t regionIndex, size_t begin, size_t end) {
    if (regionIndex >= g_extras.size()) return;
    ExtraRegion& region = g_extras[regionIndex];
    for (size_t i = begin; i < end && i < region.size; i++) region.pin[i] = 1;
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

// The extra regions too (P2's struct, the rules state, the GUI object): the
// player draw writes into the player struct, and the HUD draws the GUI's
// VMs. The sync test always learned these; netplay now does as well
// (docs/15).
std::vector<uint8_t> g_extrasBaseline;
std::vector<const void*> g_extrasBaselinePtr;

const uint8_t* ExtraLive(const ExtraRegion& region) {
    return static_cast<const uint8_t*>(region.ptr ? region.ptr : *Game::At<void*>(region.pointerRva));
}

void Snapshot_ArmDrawLearning() {
    if (!EnsureAllocated()) return;
    memcpy(g_calibrationBaseline, g_dataBase, g_dataSize);
    g_extrasBaseline.resize(g_extrasSize);
    g_extrasBaselinePtr.resize(g_extras.size());
    size_t offset = 0;
    for (size_t i = 0; i < g_extras.size(); i++) {
        const uint8_t* live = ExtraLive(g_extras[i]);
        g_extrasBaselinePtr[i] = live;
        if (live) memcpy(g_extrasBaseline.data() + offset, live, g_extras[i].size);
        offset += g_extras[i].size;
    }
    g_drawLearningArmed = true;
}

void Snapshot_LearnDrawChanges() {
    if (!g_drawLearningArmed) return;
    g_drawLearningArmed = false;
    size_t added = DiffAgainstBaseline();
    size_t offset = 0, extraAdded = 0;
    for (size_t i = 0; i < g_extras.size() && offset + g_extras[i].size <= g_extrasBaseline.size(); i++) {
        ExtraRegion& region = g_extras[i];
        const uint8_t* live = ExtraLive(region);
        if (live && live == g_extrasBaselinePtr[i]) {
            const uint8_t* base = g_extrasBaseline.data() + offset;
            for (size_t b = 0; b < region.size; b++) {
                if (live[b] != base[b] && !region.skip[b]) {
                    region.skip[b] = 1;
                    extraAdded++;
                }
            }
        }
        offset += region.size;
    }
    if (added > 0) {
        ModLog("Snapshot: drawing changed %zu more bytes -- now excluded from restores", added);
        BuildRestoreRanges();
    }
    if (extraAdded > 0) ModLog("Snapshot: drawing changed %zu bytes of the extra regions -- excluded too", extraAdded);
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
                if (!region.skip[b] || region.pin[b]) out[b] = saved[b];
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

namespace {

// Values that are addresses differ between two processes (ASLR, heap), so
// cross-machine hashes treat anything shaped like a user-mode pointer as 0.
bool LooksLikeAnyPointer(uint64_t v) {
    return v >= 0x0000010000000000ull && v < 0x0000800000000000ull;
}

uint64_t MixWord(uint64_t hash, uint64_t v) {
    hash ^= v;
    hash *= 0x100000001B3ull;
    hash ^= hash >> 29;
    return hash;
}

uint64_t HashWords(const uint8_t* p, size_t size, const uint8_t* skipBits) {
    uint64_t hash = 0xCBF29CE484222325ull;
    for (size_t q = 0; q + 8 <= size; q += 8) {
        uint8_t mask = skipBits ? skipBits[q >> 3] : 0;
        if (mask == 0xFF) continue;
        uint64_t v;
        memcpy(&v, p + q, 8);
        if (mask == 0 && LooksLikeAnyPointer(v)) v = 0;
        for (int b = 0; b < 8; b++) {
            if (mask & (1u << b)) v &= ~(0xFFull << (b * 8));
        }
        hash = MixWord(hash, v);
    }
    return hash;
}

} // namespace

size_t Snapshot_HashCount() {
    return (g_dataSize + 4095) / 4096 + g_extras.size();
}

void Snapshot_Hashes(uint64_t* out) {
    size_t pages = (g_dataSize + 4095) / 4096;
    for (size_t pg = 0; pg < pages; pg++) {
        size_t begin = pg * 4096;
        size_t size = begin + 4096 <= g_dataSize ? 4096 : g_dataSize - begin;
        out[pg] = g_volatile.empty() ? 0 : HashWords(g_dataBase + begin, size, g_volatile.data() + (begin >> 3));
    }
    for (size_t e = 0; e < g_extras.size(); e++) {
        const ExtraRegion& r = g_extras[e];
        const uint8_t* ptr = static_cast<const uint8_t*>(r.ptr ? r.ptr : *Game::At<void*>(r.pointerRva));
        out[pages + e] = ptr ? HashWords(ptr, r.size, nullptr) : 0;
    }
}

uintptr_t Snapshot_DataRva() {
    return reinterpret_cast<uintptr_t>(g_dataBase) - Game::Base();
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
            if (live[i] == saved[i] || IsVolatile(range.offset + i)) continue; // pinned bytes: restored, not compared
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
