#include "player_look.h"
#include "coop_rules.h"
#include "game.h"
#include "hooks.h"
#include "netplay.h"
#include "overlay_renderer.h"
#include "player2.h"

#include <cmath>
#include <cstring>

namespace {

using PlayerFn = uint64_t (*)(uint8_t* player, uint64_t secondArg);
using DrawVmFn = uint64_t (*)(uint64_t unused, uint8_t* vm, uint8_t flag);
using SetViewFn = void (*)(void* view, float minZ, float maxZ, uint8_t flag);

PlayerFn g_origDrawBombFlash = nullptr;
PlayerFn g_origDraw = nullptr;
PlayerFn g_origDrawOverlay = nullptr;

LookSettings g_settings;

// The overlay mod's near-player fade (overlay/19, from th06_multi_net):
// linear from 20% opacity at 50 units to fully opaque at 100.
const float kFadeNear = 50.0f;
const float kFadeFar = 100.0f;
const float kFadeNearOpacity = 0.20f;
const float kOutlineMaxAlpha = 0.75f;
const float kOutlineOffset = 1.5f;
// The outline copies sit a little farther back than the sprite. Drawn at the
// same depth they won the depth test and painted the sprite black (docs/11).
const float kOutlineDepthOffset = 0.004f;

int PlayerIndex(const uint8_t* player) {
    if (player == Game::Player1()) return 0;
    if (Player2_IsActive() && player == Player2_Struct()) return 1;
    return -1;
}

uint8_t* PlayerByIndex(int index) {
    return index == 0 ? Game::Player1() : Player2_Struct();
}

float* Pos(uint8_t* player) {
    return reinterpret_cast<float*>(player + Game::kPlayerPosX);
}

uint32_t& VmColor(uint8_t* vm) {
    return *reinterpret_cast<uint32_t*>(vm + Game::kVmColor);
}

uint32_t ColorOf(int index) {
    int local = Netplay_LocalPlayerIndex();
    if (local < 0) return index == 0 ? g_settings.color : g_settings.p2Color;
    if (index == local) return g_settings.color;
    uint32_t partner = 0;
    if (Netplay_PartnerColor(&partner)) return partner;
    return index == 0 ? 0xFFFFFF : g_settings.p2Color;
}

// Same-machine play has no "you": P1 is treated as the one P2 fades near.
bool IsOther(int index) {
    int local = Netplay_LocalPlayerIndex();
    return local < 0 ? index == 1 : index != local;
}

bool Visible(uint8_t* player) {
    return *Game::At<uint8_t>(Game::kGameOverFlag) == 0 && player[Game::kPlayerOutFlag] == 0 &&
           !CoopRules_IsDowned(player);
}

float FadeFor(int index) {
    if (!g_settings.proximityFade || !IsOther(index)) return 1.0f;
    uint8_t* self = PlayerByIndex(1 - index);
    if (!Visible(self)) return 1.0f;
    float* a = Pos(PlayerByIndex(index));
    float* b = Pos(self);
    float dist = sqrtf((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]));
    if (dist >= kFadeFar) return 1.0f;
    if (dist < kFadeNear) dist = kFadeNear;
    return kFadeNearOpacity + (dist - kFadeNear) / (kFadeFar - kFadeNear) * (1.0f - kFadeNearOpacity);
}

// Multiplies a VM's color by a tint and an alpha for one draw call.
class VmColorScope {
public:
    VmColorScope(uint8_t* vm, uint32_t tint, float alpha) : m_vm(vm), m_saved(VmColor(vm)) {
        if (m_saved == 0) return; // 0 = not drawn at all; keep it that way
        auto mul = [](uint32_t channel, uint32_t factor) { return (channel * factor + 127) / 255; };
        uint32_t b = mul(m_saved & 0xFF, tint & 0xFF);
        uint32_t g = mul((m_saved >> 8) & 0xFF, (tint >> 8) & 0xFF);
        uint32_t r = mul((m_saved >> 16) & 0xFF, (tint >> 16) & 0xFF);
        uint32_t a = static_cast<uint32_t>(static_cast<float>(m_saved >> 24) * alpha + 0.5f);
        uint32_t color = (a << 24) | (r << 16) | (g << 8) | b;
        VmColor(vm) = color != 0 ? color : 1;
    }
    ~VmColorScope() { VmColor(m_vm) = m_saved; }
    VmColorScope(const VmColorScope&) = delete;
    VmColorScope& operator=(const VmColorScope&) = delete;

private:
    uint8_t* m_vm;
    uint32_t m_saved;
};

// A dark outline: the main sprite drawn four times, offset, in black (the
// VM color multiplies the texture, so black keeps only its shape), before
// the faded sprite itself. Placed like the player draw places it.
void DrawOutline(uint8_t* player, float alpha) {
    uint8_t* vm = player + Game::kPlayerMainVm;
    if (VmColor(vm) == 0) return;
    float savedPos[3];
    memcpy(savedPos, vm + Game::kVmPos, sizeof(savedPos));
    uint32_t savedColor = VmColor(vm);

    Game::Fn<SetViewFn>(Game::kFnSetPlayfieldView)(Game::At<void>(Game::kPlayfieldView), 0.0f,
                                                  *Game::At<float>(Game::kConstHalf), 0);
    uint32_t a = static_cast<uint32_t>(static_cast<float>(savedColor >> 24) * alpha + 0.5f);
    VmColor(vm) = (a ? a : 1) << 24;
    const float offsets[4][2] = { { -kOutlineOffset, 0 }, { kOutlineOffset, 0 }, { 0, -kOutlineOffset }, { 0, kOutlineOffset } };
    float* pos = reinterpret_cast<float*>(vm + Game::kVmPos);
    for (const auto& o : offsets) {
        pos[0] = Pos(player)[0] + o[0];
        pos[1] = Pos(player)[1] + o[1];
        pos[2] = Game::kPlayerSpriteZ + kOutlineDepthOffset;
        Game::Fn<DrawVmFn>(Game::kFnDrawVm)(0, vm, 1);
    }
    VmColor(vm) = savedColor;
    memcpy(vm + Game::kVmPos, savedPos, sizeof(savedPos));
}

uint64_t Detour_Draw(uint8_t* player, uint64_t secondArg) {
    int index = PlayerIndex(player);
    if (index < 0 || !Player2_IsActive()) return g_origDraw(player, secondArg);
    if (CoopRules_IsDowned(player)) return 1;

    float alpha = FadeFor(index);
    uint32_t tint = ColorOf(index);
    if (g_settings.outline && alpha < 1.0f && Visible(player)) {
        DrawOutline(player, (1.0f - alpha) / (1.0f - kFadeNearOpacity) * kOutlineMaxAlpha);
    }
    VmColorScope body(player + Game::kPlayerMainVm, tint, alpha);
    VmColorScope left(player + Game::kPlayerOptionVmL, tint, alpha);
    VmColorScope right(player + Game::kPlayerOptionVmR, tint, alpha);
    return g_origDraw(player, secondArg);
}

template <PlayerFn* Original>
uint64_t Detour_DrawSkipDowned(uint8_t* player, uint64_t secondArg) {
    int index = PlayerIndex(player);
    if (index >= 0 && CoopRules_IsDowned(player)) return 1;
    return (*Original)(player, secondArg);
}

// ---- focus ring (overlay/39's recipe, drawn with the Present hook) ----------

// Game units -> fractions of the window (overlay/14, measured on the
// 1456x816 window: playfield x 400-1060, y 20-790; 384x448 game units).
const float kPlayfieldLeftFrac = 400.0f / 1456.0f;
const float kPlayfieldTopFrac = 20.0f / 816.0f;
const float kPlayfieldWidthFrac = 660.0f / 1456.0f;
const float kPlayfieldHeightFrac = 770.0f / 816.0f;
const float kGameWidth = 384.0f;
const float kGameHeight = 448.0f;

const int kRingRevealFrames = 18;
const int kRingFadeFrames = 6;
const int kRingDots = 12;
const float kRingRadius = 10.0f; // game units, at scale 1

int g_focusFrames[2] = { 0, 0 };

float Interpolate(float from, float to, float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return from + (to - from) * t;
}

void ToScreen(float x, float y, float* outX, float* outY) {
    *outX = kPlayfieldLeftFrac + (x / kGameWidth) * kPlayfieldWidthFrac;
    *outY = kPlayfieldTopFrac + (y / kGameHeight) * kPlayfieldHeightFrac;
}

void Channels(uint32_t rgb, float* r, float* g, float* b) {
    *r = static_cast<float>((rgb >> 16) & 0xFF) / 255.0f;
    *g = static_cast<float>((rgb >> 8) & 0xFF) / 255.0f;
    *b = static_cast<float>(rgb & 0xFF) / 255.0f;
}

void DrawRing(OverlayRenderer& overlay, float cx, float cy, float scale, float alpha, uint32_t rgb) {
    float r, g, b;
    Channels(rgb, &r, &g, &b);
    for (int i = 0; i < kRingDots; i++) {
        float angle = 6.2831853f * static_cast<float>(i) / static_cast<float>(kRingDots);
        float sx, sy;
        ToScreen(cx + kRingRadius * scale * cosf(angle), cy + kRingRadius * scale * sinf(angle), &sx, &sy);
        overlay.DrawQuad(OverlayQuad{ sx, sy, 0.0030f, 0.0030f * 1456.0f / 816.0f, r, g, b, alpha });
    }
}

// The hitbox itself: a white square the size of the hit radius, on a dark
// one slightly larger so it reads on any background.
void DrawHitbox(OverlayRenderer& overlay, uint8_t* player, float alpha) {
    float radius = *reinterpret_cast<float*>(player + Game::kPlayerHitRadius);
    if (!(radius >= 1.0f)) radius = 1.0f;
    if (radius > 8.0f) radius = 8.0f;
    float sx, sy;
    ToScreen(Pos(player)[0], Pos(player)[1], &sx, &sy);
    float hw = radius / kGameWidth * kPlayfieldWidthFrac;
    float hh = radius / kGameHeight * kPlayfieldHeightFrac;
    float border = 1.0f / kGameWidth * kPlayfieldWidthFrac;
    overlay.DrawQuad(OverlayQuad{ sx, sy, hw + border, hh + border * 1456.0f / 816.0f, 0.1f, 0.1f, 0.1f, alpha });
    overlay.DrawQuad(OverlayQuad{ sx, sy, hw, hh, 1.0f, 1.0f, 1.0f, alpha });
}

} // namespace

bool PlayerLook_Install() {
    struct HookSpec {
        uintptr_t rva;
        void* detour;
        void** original;
        const char* name;
    };
    const HookSpec hooks[] = {
        { Game::kFnPlayerDrawBombFlash, reinterpret_cast<void*>(&Detour_DrawSkipDowned<&g_origDrawBombFlash>), reinterpret_cast<void**>(&g_origDrawBombFlash), "PlayerDrawBombFlash" },
        { Game::kFnPlayerDraw, reinterpret_cast<void*>(&Detour_Draw), reinterpret_cast<void**>(&g_origDraw), "PlayerDraw" },
        { Game::kFnPlayerDrawOverlay, reinterpret_cast<void*>(&Detour_DrawSkipDowned<&g_origDrawOverlay>), reinterpret_cast<void**>(&g_origDrawOverlay), "PlayerDrawOverlay" },
    };
    bool ok = true;
    for (const HookSpec& h : hooks) {
        ok &= Hooks_Install(h.rva, h.detour, h.original, h.name);
    }
    return ok;
}

void PlayerLook_SetSettings(const LookSettings& settings) {
    g_settings = settings;
}

const LookSettings& PlayerLook_Settings() {
    return g_settings;
}

void PlayerLook_DrawOverlay(OverlayRenderer& overlay) {
    if (!g_settings.focusRing || !Player2_IsActive()) {
        g_focusFrames[0] = g_focusFrames[1] = 0;
        return;
    }
    for (int index = 0; index < 2; index++) {
        uint8_t* player = PlayerByIndex(index);
        uint8_t state = player[Game::kPlayerState];
        bool show = IsOther(index) || Netplay_LocalPlayerIndex() < 0; // same machine: both are someone's "other"
        if (!show || !Visible(player) || (state != 0 && state != 3) || !player[Game::kPlayerFocused]) {
            g_focusFrames[index] = 0;
            continue;
        }
        int frames = ++g_focusFrames[index];
        float alpha = Interpolate(0.0f, 0.9f, static_cast<float>(frames) / kRingFadeFrames);
        float reveal = static_cast<float>(frames) / kRingRevealFrames;
        uint32_t rgb = ColorOf(index);
        DrawRing(overlay, Pos(player)[0], Pos(player)[1], Interpolate(1.5f, 1.0f, reveal), alpha, rgb);
        DrawRing(overlay, Pos(player)[0], Pos(player)[1], Interpolate(0.3f, 1.0f, reveal), alpha, rgb);
        DrawHitbox(overlay, player, alpha);
    }
}
