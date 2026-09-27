#include "checksum.h"
#include "game.h"
#include "player2.h"

#include <cstring>

namespace {

const uint64_t kFnvOffset = 0xCBF29CE484222325ull;
const uint64_t kFnvPrime = 0x100000001B3ull;

void Mix(uint64_t& hash, const void* data, size_t size) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; i++) {
        hash = (hash ^ bytes[i]) * kFnvPrime;
    }
}

template <typename T>
void MixValue(uint64_t& hash, T value) {
    Mix(hash, &value, sizeof(value));
}

uint64_t HashPlayer(const uint8_t* player) {
    uint64_t hash = kFnvOffset;
    Mix(hash, player + Game::kPlayerPosX, 12);             // x, y, z
    Mix(hash, player + 0x773C, 4);                         // respawn/invulnerability timer
    Mix(hash, player + Game::kPlayerState, 1);
    Mix(hash, player + 0x7AC4, 4);                         // bomb frame
    Mix(hash, player + Game::kPlayerBombing, 1);
    return hash;
}

} // namespace

GameChecksum Checksum_Compute(const uint8_t* player2) {
    GameChecksum sum = {};

    uint64_t rng = kFnvOffset;
    MixValue(rng, *Game::At<uint16_t>(Game::kRngSeed));
    MixValue(rng, *Game::At<uint32_t>(Game::kRngCounter));
    sum.region[kChecksumRng] = rng;

    sum.region[kChecksumPlayer1] = HashPlayer(Game::Player1());
    sum.region[kChecksumPlayer2] = player2 ? HashPlayer(player2) : 0;

    uint64_t bullets = kFnvOffset;
    const uint8_t* bullet = Game::At<uint8_t>(Game::kBulletArray);
    for (int i = 0; i < Game::kBulletSlots; i++, bullet += Game::kBulletStride) {
        uint16_t state = *reinterpret_cast<const uint16_t*>(bullet + Game::kBulletState);
        if (state == 0) continue;
        MixValue(bullets, i);
        MixValue(bullets, state);
        Mix(bullets, bullet + Game::kBulletPos, 8);
    }
    sum.region[kChecksumBullets] = bullets;

    uint64_t entities = kFnvOffset;
    const uint8_t* entity = Game::At<uint8_t>(Game::kEntityTable);
    for (int i = 0; i < Game::kEntitySlots; i++, entity += Game::kEntityStride) {
        if ((entity[Game::kEntityFlags] & 0x80) == 0) continue;
        MixValue(entities, i);
        Mix(entities, entity + Game::kEntityPos, 8);
        Mix(entities, entity + Game::kEntityHp, 4);
    }
    sum.region[kChecksumEntities] = entities;

    uint64_t items = kFnvOffset;
    const uint8_t* item = Game::At<uint8_t>(Game::kItemPool);
    for (int i = 0; i < Game::kItemSlots; i++, item += Game::kItemStride) {
        if (item[0] == 0) continue;
        MixValue(items, i);
        Mix(items, item + 0x10, 8);  // x, y
        Mix(items, item + 0x34, 1);  // item type
    }
    sum.region[kChecksumItems] = items;

    uint64_t resources = kFnvOffset;
    MixValue(resources, *Game::At<uint8_t>(Game::kLives));
    MixValue(resources, *Game::At<uint8_t>(Game::kBombs));
    MixValue(resources, *Game::At<uint32_t>(Game::kPower));
    MixValue(resources, *Game::At<uint32_t>(Game::kScore));
    MixValue(resources, *Game::At<uint32_t>(Game::kGraze));
    // What a continue refills to (docs/13): set once per run, so a stage
    // start that adopts the wrong value only shows at the next continue.
    MixValue(resources, *Game::At<uint8_t>(Game::kStageStartLives));
    MixValue(resources, *Game::At<uint8_t>(Game::kStageStartBombs));
    const PlayerResources* p2 = Player2_Resources();
    MixValue(resources, p2->lives);
    MixValue(resources, p2->bombs);
    MixValue(resources, p2->power);
    sum.region[kChecksumResources] = resources;

    return sum;
}

const char* Checksum_RegionName(int region) {
    static const char* names[kChecksumRegionCount] = {
        "rng", "player1", "player2", "bullets", "entities", "items", "resources"
    };
    return (region >= 0 && region < kChecksumRegionCount) ? names[region] : "?";
}
