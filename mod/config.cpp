#include "config.h"
#include "mod_log.h"

#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

Config g_config;
char g_path[MAX_PATH] = {};

int ReadInt(const char* path, const char* section, const char* key, int fallback) {
    return static_cast<int>(GetPrivateProfileIntA(section, key, fallback, path));
}

float ReadFloat(const char* path, const char* section, const char* key, float fallback) {
    char buf[64] = {};
    GetPrivateProfileStringA(section, key, "", buf, sizeof(buf), path);
    if (buf[0] == '\0') return fallback;
    return static_cast<float>(atof(buf));
}

// [visual] colors as RRGGBB hex.
uint32_t ReadColor(const char* path, const char* key, uint32_t fallback) {
    char buf[32] = {};
    GetPrivateProfileStringA("visual", key, "", buf, sizeof(buf), path);
    const char* start = buf[0] == '#' ? buf + 1 : buf;
    if (*start == '\0') return fallback;
    char* end = nullptr;
    unsigned long value = strtoul(start, &end, 16);
    if (end == start) return fallback;
    return static_cast<uint32_t>(value) & 0xFFFFFF;
}

} // namespace

void Config_Load() {
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    char* lastSlash = strrchr(exePath, '\\');
    if (lastSlash) lastSlash[1] = '\0';
    char path[MAX_PATH] = {};
    snprintf(path, MAX_PATH, "%sth06nc_native_coop.ini", exePath);
    memcpy(g_path, path, sizeof(g_path));

    Config c;
    c.player2Enabled = ReadInt(path, "player2", "enabled", c.player2Enabled ? 1 : 0) != 0;
    c.player2SpawnOffsetX = ReadFloat(path, "player2", "spawn_offset_x", c.player2SpawnOffsetX);
    char character[32] = {};
    GetPrivateProfileStringA("player2", "character", "same", character, sizeof(character), path);
    const char* names[] = { "reimu_a", "reimu_b", "marisa_a", "marisa_b" };
    for (int i = 0; i < 4; i++) {
        if (_stricmp(character, names[i]) == 0) {
            c.player2Character = i / 2;
            c.player2ShotType = i % 2;
        }
    }
    c.p2PadIndex = ReadInt(path, "player2", "pad_index", c.p2PadIndex);
    c.p2KeyUp = ReadInt(path, "player2", "key_up", c.p2KeyUp);
    c.p2KeyDown = ReadInt(path, "player2", "key_down", c.p2KeyDown);
    c.p2KeyLeft = ReadInt(path, "player2", "key_left", c.p2KeyLeft);
    c.p2KeyRight = ReadInt(path, "player2", "key_right", c.p2KeyRight);
    c.p2KeyShoot = ReadInt(path, "player2", "key_shoot", c.p2KeyShoot);
    c.p2KeyBomb = ReadInt(path, "player2", "key_bomb", c.p2KeyBomb);
    c.p2KeyFocus = ReadInt(path, "player2", "key_focus", c.p2KeyFocus);

    c.coop.bossHpMultiplier = ReadFloat(path, "coop", "boss_hp_multiplier", c.coop.bossHpMultiplier);
    c.coop.invincible = ReadInt(path, "coop", "invincible", 0) != 0;
    c.coop.sharedResources = ReadInt(path, "coop", "shared_resources", 1) != 0;
    c.coop.reviveSeconds = ReadInt(path, "coop", "revive_seconds", c.coop.reviveSeconds);
    c.coop.startLives[0] = static_cast<int8_t>(ReadInt(path, "coop", "p1_start_lives", -1));
    c.coop.startLives[1] = static_cast<int8_t>(ReadInt(path, "coop", "p2_start_lives", -1));
    c.coop.startBombs[0] = static_cast<int8_t>(ReadInt(path, "coop", "p1_start_bombs", -1));
    c.coop.startBombs[1] = static_cast<int8_t>(ReadInt(path, "coop", "p2_start_bombs", -1));
    c.coop.startPower = static_cast<int16_t>(ReadInt(path, "coop", "start_power", -1));
    c.coop.startStage = static_cast<int8_t>(ReadInt(path, "coop", "start_stage", 1));
    char targeting[32] = {};
    GetPrivateProfileStringA("coop", "targeting", "nearest", targeting, sizeof(targeting), path);
    if (_stricmp(targeting, "host") == 0) c.coop.targeting = kTargetHost;
    else if (_stricmp(targeting, "alternate") == 0) c.coop.targeting = kTargetAlternate;
    else c.coop.targeting = kTargetNearest;

    c.look.color = ReadColor(path, "color", c.look.color);
    c.look.p2Color = ReadColor(path, "p2_color", c.look.p2Color);
    c.look.proximityFade = ReadInt(path, "visual", "proximity_fade", 1) != 0;
    c.look.outline = ReadInt(path, "visual", "outline", 1) != 0;
    c.look.focusRing = ReadInt(path, "visual", "focus_ring", 1) != 0;

    char mode[32] = {};
    GetPrivateProfileStringA("netplay", "mode", "rollback", mode, sizeof(mode), path);
    c.netplayRollback = _stricmp(mode, "lockstep") != 0;
    c.netplayInputDelay = ReadInt(path, "netplay", "input_delay", c.netplayInputDelay);
    c.netplayMaxRollback = ReadInt(path, "netplay", "max_rollback", c.netplayMaxRollback);
    if (c.netplayInputDelay < 0) c.netplayInputDelay = 0;
    if (c.netplayInputDelay > 10) c.netplayInputDelay = 10;
    if (c.netplayMaxRollback < 1) c.netplayMaxRollback = 1;
    if (c.netplayMaxRollback > 15) c.netplayMaxRollback = 15;
    c.syncTest = ReadInt(path, "synctest", "enabled", 0) != 0;
    c.syncTestDistance = ReadInt(path, "synctest", "distance", c.syncTestDistance);
    c.syncTestInterval = ReadInt(path, "synctest", "interval", c.syncTestInterval);
    if (c.syncTestDistance < 1) c.syncTestDistance = 1;
    if (c.syncTestDistance > c.netplayMaxRollback) c.syncTestDistance = c.netplayMaxRollback;
    if (c.syncTestInterval < 1) c.syncTestInterval = 1;
    g_config = c;
    if (c.syncTest) {
        ModLog("Config: SYNC TEST enabled -- distance %d, every %d frames", c.syncTestDistance, c.syncTestInterval);
    }

    ModLog("Config: %s -- player2.enabled=%d pad_index=%d spawn_offset_x=%.1f; netplay mode=%s delay=%d max_rollback=%d;"
           " coop boss_hp=x%.2f invincible=%d targeting=%d shared_resources=%d revive_seconds=%d",
           path, c.player2Enabled ? 1 : 0, c.p2PadIndex, c.player2SpawnOffsetX,
           c.netplayRollback ? "rollback" : "lockstep", c.netplayInputDelay, c.netplayMaxRollback,
           c.coop.bossHpMultiplier, c.coop.invincible ? 1 : 0, c.coop.targeting,
           c.coop.sharedResources ? 1 : 0, c.coop.reviveSeconds);
    ModLog("Config: visual color %06X p2_color %06X fade %d outline %d focus_ring %d", c.look.color,
           c.look.p2Color, c.look.proximityFade ? 1 : 0, c.look.outline ? 1 : 0, c.look.focusRing ? 1 : 0);
    ModLog("Config: start lives P1 %d P2 %d, bombs P1 %d P2 %d (-1 = game option)", c.coop.startLives[0],
           c.coop.startLives[1], c.coop.startBombs[0], c.coop.startBombs[1]);
}

const Config& Config_Get() {
    return g_config;
}

const char* Config_Path() {
    return g_path;
}
