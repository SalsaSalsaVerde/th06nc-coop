#include "settings_panel.h"
#include "config.h"
#include "coop_rules.h"
#include "game.h"
#include "mod_log.h"
#include "netplay.h"
#include "overlay_renderer.h"
#include "player2.h"
#include "player_look.h"
#include "text_renderer.h"

#include <windows.h>
#include <cstdio>

namespace {

bool g_open = false;
bool g_waitRelease = false; // swallow input after closing until everything is let go
bool g_prevF8 = false;
uint32_t g_prevInput = 0;
int g_selected = 0;

struct NamedColor {
    const char* name;
    uint32_t rgb;
};
const NamedColor kColors[] = {
    { "WHITE", 0xFFFFFF }, { "RED", 0xFF8080 }, { "ORANGE", 0xFFB070 }, { "YELLOW", 0xFFF080 },
    { "GREEN", 0x90FF90 }, { "CYAN", 0x80F0FF }, { "BLUE", 0xA0C8FF }, { "PURPLE", 0xD0A0FF },
    { "PINK", 0xFFA0D8 },
};
const int kColorCount = sizeof(kColors) / sizeof(kColors[0]);

const char* kCharacterNames[] = { "SAME AS P1", "REIMU A", "REIMU B", "MARISA A", "MARISA B" };

enum Item {
    kItemStartStage,
    kItemStartPoint,
    kItemBossHp,
    kItemInvincible,
    kItemTargeting,
    kItemResources,
    kItemRevive,
    kItemP1Lives,
    kItemP1Bombs,
    kItemP2Lives,
    kItemP2Bombs,
    kItemStartPower,
    kItemColor,
    kItemP2Color,
    kItemLocalP2,
    kItemP2Character,
    kItemCount
};

// A short label for a virtual-key code, for the panel's key line.
const char* KeyName(int vk) {
    static char buf[8][4];
    static int next = 0;
    char* out = buf[next++ % 8];
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
        out[0] = static_cast<char>(vk);
        out[1] = '\0';
    } else {
        snprintf(out, 4, "#%d", vk % 100);
    }
    return out;
}

bool IsGuest() {
    return Netplay_LocalPlayerIndex() == 1;
}

bool Online() {
    return Netplay_LocalPlayerIndex() >= 0;
}

// Rules items are the host's (or a lone player's) to set; the rest are
// personal, and the same-machine ones only matter offline.
bool Editable(int item) {
    switch (item) {
        case kItemColor:
            return true;
        case kItemP2Color:
        case kItemLocalP2:
        case kItemP2Character:
            return !Online();
        case kItemRevive:
        case kItemP2Lives:
        case kItemP2Bombs:
            return !IsGuest() && !CoopRules_Settings().sharedResources;
        default:
            return !IsGuest();
    }
}

int ColorIndex(uint32_t rgb) {
    for (int i = 0; i < kColorCount; i++) {
        if (kColors[i].rgb == rgb) return i;
    }
    return -1;
}

void FormatColor(char* out, size_t size, uint32_t rgb) {
    int index = ColorIndex(rgb);
    if (index >= 0) snprintf(out, size, "%s", kColors[index].name);
    else snprintf(out, size, "#%06X", rgb);
}

uint32_t StepColor(uint32_t rgb, int dir) {
    int index = ColorIndex(rgb);
    if (index < 0) index = 0;
    return kColors[(index + dir + kColorCount) % kColorCount].rgb;
}

int CharacterIndex() {
    int character = Player2_WantedCharacter();
    return character < 0 ? 0 : 1 + character * 2 + (Player2_WantedShotType() < 0 ? 0 : Player2_WantedShotType());
}

int StepInt(int value, int dir, int lo, int hi) {
    value += dir;
    if (value < lo) value = hi;
    if (value > hi) value = lo;
    return value;
}

// -1 (the game's option), then 1-9: zero lives isn't a start.
int8_t StepLives(int8_t value, int dir) {
    int next = StepInt(value, dir, -1, 9);
    if (next == 0) next = dir > 0 ? 1 : -1;
    return static_cast<int8_t>(next);
}

// The game's own (-1), then 0-128 in steps of 16.
int16_t StepPower(int16_t value, int dir) {
    const int16_t steps[] = { -1, 0, 16, 32, 48, 64, 80, 96, 112, 128 };
    const int count = sizeof(steps) / sizeof(steps[0]);
    int index = 0;
    while (index + 1 < count && steps[index + 1] <= value) index++;
    return steps[(index + dir + count) % count];
}

void FormatStock(char* out, size_t size, int8_t value) {
    if (value < 0) snprintf(out, size, "GAME'S OPTION");
    else snprintf(out, size, "%d", value);
}

void Describe(int item, char* label, size_t labelSize, char* value, size_t valueSize) {
    const CoopSettings& s = CoopRules_Settings();
    const LookSettings& look = PlayerLook_Settings();
    switch (item) {
        case kItemStartStage:
            snprintf(label, labelSize, "START THE RUN AT");
            snprintf(value, valueSize, "STAGE %d", s.startStage);
            break;
        case kItemStartPoint: {
            const char* names[] = { "STAGE START", "MIDBOSS", "BOSS" };
            snprintf(label, labelSize, "START AT");
            snprintf(value, valueSize, "%s", names[s.startPoint >= 0 && s.startPoint <= kStartAtBoss ? s.startPoint : 0]);
            break;
        }
        case kItemStartPower:
            snprintf(label, labelSize, "START POWER");
            if (s.startPower < 0) snprintf(value, valueSize, "GAME'S (0)");
            else snprintf(value, valueSize, "%d/128", s.startPower);
            break;
        case kItemBossHp:
            snprintf(label, labelSize, "BOSS HP");
            snprintf(value, valueSize, "X%.2f", s.bossHpMultiplier);
            break;
        case kItemInvincible:
            snprintf(label, labelSize, "INVINCIBLE PRACTICE");
            snprintf(value, valueSize, "%s", s.invincible ? "ON" : "OFF");
            break;
        case kItemTargeting: {
            const char* names[] = { "NEAREST PLAYER", "ALWAYS P1", "ALTERNATE" };
            snprintf(label, labelSize, "ENEMIES AIM AT");
            snprintf(value, valueSize, "%s", names[s.targeting <= kTargetAlternate ? s.targeting : 0]);
            break;
        }
        case kItemResources:
            snprintf(label, labelSize, "LIVES/BOMBS/POWER");
            snprintf(value, valueSize, "%s", s.sharedResources ? "SHARED" : "PER PLAYER");
            break;
        case kItemRevive:
            snprintf(label, labelSize, "REVIVE DOWNED AFTER");
            if (s.sharedResources) snprintf(value, valueSize, "(PER PLAYER ONLY)");
            else if (s.reviveSeconds <= 0) snprintf(value, valueSize, "NEVER");
            else snprintf(value, valueSize, "%d S", s.reviveSeconds);
            break;
        case kItemP1Lives:
            snprintf(label, labelSize, s.sharedResources ? "START LIVES" : "P1 START LIVES");
            FormatStock(value, valueSize, s.startLives[0]);
            break;
        case kItemP1Bombs:
            snprintf(label, labelSize, s.sharedResources ? "START BOMBS" : "P1 START BOMBS");
            FormatStock(value, valueSize, s.startBombs[0]);
            break;
        case kItemP2Lives:
            snprintf(label, labelSize, "P2 START LIVES");
            if (s.sharedResources) snprintf(value, valueSize, "(PER PLAYER ONLY)");
            else FormatStock(value, valueSize, s.startLives[1]);
            break;
        case kItemP2Bombs:
            snprintf(label, labelSize, "P2 START BOMBS");
            if (s.sharedResources) snprintf(value, valueSize, "(PER PLAYER ONLY)");
            else FormatStock(value, valueSize, s.startBombs[1]);
            break;
        case kItemColor:
            snprintf(label, labelSize, Online() ? "YOUR COLOR" : "P1 COLOR");
            FormatColor(value, valueSize, look.color);
            break;
        case kItemP2Color:
            snprintf(label, labelSize, "P2 COLOR (SAME PC)");
            FormatColor(value, valueSize, look.p2Color);
            break;
        case kItemLocalP2:
            snprintf(label, labelSize, "P2 ON THIS PC");
            snprintf(value, valueSize, "%s", Player2_Enabled() ? "ON" : "OFF");
            break;
        case kItemP2Character:
            snprintf(label, labelSize, "P2 CHARACTER (SAME PC)");
            snprintf(value, valueSize, "%s", kCharacterNames[CharacterIndex()]);
            break;
        default:
            label[0] = value[0] = '\0';
            break;
    }
}

void Change(int item, int dir) {
    if (!Editable(item)) return;
    CoopSettings s = CoopRules_OwnSettings();
    LookSettings look = PlayerLook_Settings();
    bool rules = true;
    switch (item) {
        case kItemBossHp: {
            float value = s.bossHpMultiplier + 0.25f * static_cast<float>(dir);
            if (value < 0.25f) value = 8.0f;
            if (value > 8.0f) value = 0.25f;
            s.bossHpMultiplier = value;
            break;
        }
        case kItemStartStage: s.startStage = static_cast<int8_t>(StepInt(s.startStage, dir, 1, 6)); break;
        case kItemStartPoint: s.startPoint = static_cast<int8_t>(StepInt(s.startPoint, dir, 0, kStartAtBoss)); break;
        case kItemStartPower: s.startPower = StepPower(s.startPower, dir); break;
        case kItemInvincible: s.invincible = !s.invincible; break;
        case kItemTargeting: s.targeting = static_cast<uint8_t>(StepInt(s.targeting, dir, 0, kTargetAlternate)); break;
        case kItemResources: s.sharedResources = !s.sharedResources; break;
        case kItemRevive: s.reviveSeconds = StepInt(s.reviveSeconds / 5 * 5, dir * 5, 0, 120); break;
        case kItemP1Lives: s.startLives[0] = StepLives(s.startLives[0], dir); break;
        case kItemP2Lives: s.startLives[1] = StepLives(s.startLives[1], dir); break;
        case kItemP1Bombs: s.startBombs[0] = static_cast<int8_t>(StepInt(s.startBombs[0], dir, -1, 9)); break;
        case kItemP2Bombs: s.startBombs[1] = static_cast<int8_t>(StepInt(s.startBombs[1], dir, -1, 9)); break;
        default: rules = false; break;
    }
    if (rules) {
        CoopRules_SetOwnSettings(s);
        return;
    }
    switch (item) {
        case kItemColor: look.color = StepColor(look.color, dir); break;
        case kItemP2Color: look.p2Color = StepColor(look.p2Color, dir); break;
        case kItemLocalP2: Player2_SetEnabled(!Player2_Enabled()); break;
        case kItemP2Character: {
            int index = StepInt(CharacterIndex(), dir, 0, 4);
            if (index == 0) Player2_SetLoadout(-1, -1);
            else Player2_SetLoadout((index - 1) / 2, (index - 1) % 2);
            break;
        }
        default: break;
    }
    PlayerLook_SetSettings(look);
}

void WriteInt(const char* section, const char* key, int value) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", value);
    WritePrivateProfileStringA(section, key, buf, Config_Path());
}

void WriteString(const char* section, const char* key, const char* value) {
    WritePrivateProfileStringA(section, key, value, Config_Path());
}

// Everything the panel can change, back into the ini, so the next launch
// starts with it.
void Save() {
    const CoopSettings& s = CoopRules_OwnSettings();
    const LookSettings& look = PlayerLook_Settings();
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", s.bossHpMultiplier);
    WriteString("coop", "boss_hp_multiplier", buf);
    WriteInt("coop", "invincible", s.invincible ? 1 : 0);
    const char* targeting[] = { "nearest", "host", "alternate" };
    WriteString("coop", "targeting", targeting[s.targeting <= kTargetAlternate ? s.targeting : 0]);
    WriteInt("coop", "shared_resources", s.sharedResources ? 1 : 0);
    WriteInt("coop", "revive_seconds", s.reviveSeconds);
    WriteInt("coop", "p1_start_lives", s.startLives[0]);
    WriteInt("coop", "p1_start_bombs", s.startBombs[0]);
    WriteInt("coop", "p2_start_lives", s.startLives[1]);
    WriteInt("coop", "p2_start_bombs", s.startBombs[1]);
    WriteInt("coop", "start_power", s.startPower);
    WriteInt("coop", "start_stage", s.startStage);
    const char* points[] = { "stage", "midboss", "boss" };
    WriteString("coop", "start_point", points[s.startPoint >= 0 && s.startPoint <= kStartAtBoss ? s.startPoint : 0]);
    snprintf(buf, sizeof(buf), "%06X", look.color);
    WriteString("visual", "color", buf);
    snprintf(buf, sizeof(buf), "%06X", look.p2Color);
    WriteString("visual", "p2_color", buf);
    WriteInt("player2", "enabled", Player2_Enabled() ? 1 : 0);
    const char* characters[] = { "same", "reimu_a", "reimu_b", "marisa_a", "marisa_b" };
    WriteString("player2", "character", characters[CharacterIndex()]);
    ModLog("SettingsPanel: saved to %s", Config_Path());
}

bool GameWindowFocused() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

void Close() {
    g_open = false;
    g_waitRelease = true;
    Save();
}

} // namespace

uint32_t SettingsPanel_FilterInput(uint32_t input) {
    bool f8 = GameWindowFocused() && (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
    bool f8Pressed = f8 && !g_prevF8;
    g_prevF8 = f8;

    if (g_open && Player2_InStage()) Close(); // a stage started anyway: get out of the way
    if (!g_open) {
        if (f8Pressed && !Player2_InStage()) {
            g_open = true;
            g_prevInput = input; // held buttons don't count as presses
            ModLog("SettingsPanel: opened");
            return 0;
        }
        if (g_waitRelease) {
            if (input != 0) return 0;
            g_waitRelease = false;
        }
        return input;
    }

    uint32_t pressed = input & ~g_prevInput;
    g_prevInput = input;
    if (f8Pressed || (pressed & (Game::kButtonBomb | Game::kButtonMenu))) {
        Close();
        return 0;
    }
    if (pressed & Game::kButtonUp) g_selected = (g_selected + kItemCount - 1) % kItemCount;
    if (pressed & Game::kButtonDown) g_selected = (g_selected + 1) % kItemCount;
    if (pressed & Game::kButtonLeft) Change(g_selected, -1);
    if (pressed & (Game::kButtonRight | Game::kButtonShoot)) Change(g_selected, 1);
    return 0;
}

bool SettingsPanel_IsOpen() {
    return g_open;
}

void SettingsPanel_Draw(OverlayRenderer& overlay) {
    if (!g_open) return;
    TextRenderer_EnsureLoaded(overlay);

    const float left = 0.20f;
    const float top = 0.16f;
    const float charW = 16.0f / 1456.0f; // one glyph cell = 16px at the 1456x816 window
    const float charH = 16.0f / 816.0f;
    const float lineStep = 0.032f;
    const float valueX = left + 0.34f;
    float height = lineStep * (kItemCount + 6);
    overlay.DrawQuad(OverlayQuad{ 0.5f, top + height * 0.5f - 0.02f, 0.33f, height * 0.5f + 0.02f, 0.03f, 0.03f, 0.08f, 0.85f });

    DrawText(overlay, "CO-OP SETTINGS", left, top, charW * 1.5f, charH * 1.5f, 1.0f, 1.0f, 0.9f, 0.6f);
    const char* who = IsGuest() ? "RULES ARE SET BY THE HOST" : (Online() ? "YOU ARE THE HOST: THESE ARE THE RULES" : "PLAYING ON THIS PC");
    DrawText(overlay, who, left, top + lineStep * 1.2f, charW * 0.8f, charH * 0.8f, 0.8f);

    for (int item = 0; item < kItemCount; item++) {
        char label[40], value[40], line[48];
        Describe(item, label, sizeof(label), value, sizeof(value));
        float y = top + lineStep * (item + 2.6f);
        float alpha = Editable(item) ? 1.0f : 0.55f;
        snprintf(line, sizeof(line), "%s%s", item == g_selected ? "> " : "  ", label);
        DrawText(overlay, line, left, y, charW, charH, alpha);
        snprintf(line, sizeof(line), "%s%s%s", Editable(item) ? "< " : "  ", value, Editable(item) ? " >" : "");
        DrawText(overlay, line, valueX, y, charW, charH, alpha);
    }
    DrawText(overlay, "UP/DOWN SELECT   LEFT/RIGHT CHANGE   BOMB OR F8 CLOSES AND SAVES", left,
             top + lineStep * (kItemCount + 3.0f), charW * 0.75f, charH * 0.75f, 0.8f);
    const Config& c = Config_Get();
    char keys[96];
    snprintf(keys, sizeof(keys), "P2 KEYS: %s%s%s%s MOVE   %s SHOOT   %s BOMB   %s FOCUS   (OR PAD %d)",
             KeyName(c.p2KeyUp), KeyName(c.p2KeyLeft), KeyName(c.p2KeyDown), KeyName(c.p2KeyRight),
             KeyName(c.p2KeyShoot), KeyName(c.p2KeyBomb), KeyName(c.p2KeyFocus), c.p2PadIndex);
    DrawText(overlay, keys, left, top + lineStep * (kItemCount + 3.8f), charW * 0.75f, charH * 0.75f, 0.8f);
}
