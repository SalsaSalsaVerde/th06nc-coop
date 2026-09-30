#pragma once

#include <cstdint>

// Per-system hashes of gameplay-relevant fields only (positions, states, HP,
// resources, RNG). Raw memory can't be compared across machines: the same
// structs hold heap pointers whose values differ per process. Separate
// regions make a desync report say *which* system diverged first.
enum ChecksumRegion {
    kChecksumRng,
    kChecksumPlayer1,
    kChecksumPlayer2,
    kChecksumShots, // both players' shots in flight (docs/17)
    kChecksumBullets,
    kChecksumEntities,
    kChecksumItems,
    kChecksumResources,
    kChecksumRegionCount
};

struct GameChecksum {
    uint64_t region[kChecksumRegionCount];
};

GameChecksum Checksum_Compute(const uint8_t* player2);
const char* Checksum_RegionName(int region);
