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
const float kHaloMaxAlpha = 0.6f;
const float kHaloScale = 1.22f;

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

// A halo behind the faded sprite: the main sprite drawn once more, scaled
// up, in the player's color. The sprite pipeline has no depth test, so a
// dark outline can't be cut out from under a translucent body (black copies
// under or over it both read as a black silhouette, docs/11); a light halo
// in the player's own color keeps the faded player visible on any
// background instead. Placed like the player draw places the sprite.
void DrawHalo(uint8_t* player, float alpha, uint32_t tint) {
    uint8_t* vm = player + Game::kPlayerMainVm;
    if (VmColor(vm) == 0) return;
    float savedPos[3], savedScale[2];
    memcpy(savedPos, vm + Game::kVmPos, sizeof(savedPos));
    memcpy(savedScale, vm + Game::kVmScale, sizeof(savedScale));
    uint32_t savedColor = VmColor(vm);

    Game::Fn<SetViewFn>(Game::kFnSetPlayfieldView)(Game::At<void>(Game::kPlayfieldView), 0.0f,
                                                  *Game::At<float>(Game::kConstHalf), 0);
    uint32_t a = static_cast<uint32_t>(static_cast<float>(savedColor >> 24) * alpha + 0.5f);
    VmColor(vm) = ((a ? a : 1) << 24) | (tint & 0xFFFFFF);
    float* scale = reinterpret_cast<float*>(vm + Game::kVmScale);
    scale[0] = savedScale[0] * kHaloScale;
    scale[1] = savedScale[1] * kHaloScale;
    float* pos = reinterpret_cast<float*>(vm + Game::kVmPos);
    pos[0] = Pos(player)[0];
    pos[1] = Pos(player)[1];
    pos[2] = Game::kPlayerSpriteZ + g_settings.outlineDepthOffset;
    Game::Fn<DrawVmFn>(Game::kFnDrawVm)(0, vm, 1);

    VmColor(vm) = savedColor;
    memcpy(vm + Game::kVmScale, savedScale, sizeof(savedScale));
    memcpy(vm + Game::kVmPos, savedPos, sizeof(savedPos));
}

uint64_t Detour_Draw(uint8_t* player, uint64_t secondArg) {
    int index = PlayerIndex(player);
    if (index < 0 || !Player2_IsActive()) return g_origDraw(player, secondArg);
    if (CoopRules_IsDowned(player)) return 1;

    float alpha = FadeFor(index);
    uint32_t tint = ColorOf(index);
    if (g_settings.outline && alpha < 1.0f && Visible(player)) {
        DrawHalo(player, (1.0f - alpha) / (1.0f - kFadeNearOpacity) * kHaloMaxAlpha, tint);
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

// ---- focus marker: the game's own, on P2 -------------------------------
//
// The HUD keeps two sprite VMs for the focus marker and, every frame, moves
// them onto Player 1 (by absolute address) and starts scripts 0x641/0x642
// on them while P1 holds focus, ending them when it stops (docs/13). P2
// gets its own pair, run the same way, drawn after P2's overlay pass.

using AnmTickFn = int (*)(void* anmManager, uint8_t* vm);
using SetScriptFn = void (*)(void* unused, uint8_t* vm, int scriptId);

struct FocusRing {
    alignas(16) uint8_t vm[2][0x120];
    bool initialized = false;
};
FocusRing g_p2Ring;

bool RingRunning(const uint8_t* vm) {
    return *reinterpret_cast<void* const*>(vm + Game::kVmScriptPtr) != nullptr;
}

void UpdateAndDrawP2Ring(uint8_t* player) {
    uint8_t* gui = *Game::At<uint8_t*>(Game::kGuiObjectPtr);
    void* anm = *Game::At<void*>(Game::kAnmManagerPtr);
    if (!gui || !anm) return;
    if (!g_p2Ring.initialized) {
        // The HUD's VMs as a template (sprite sheet, scale, color), minus
        // whatever script they are running.
        for (int i = 0; i < 2; i++) {
            memcpy(g_p2Ring.vm[i], gui + Game::kGuiFocusRingVm + i * 0x120, 0x120);
            *reinterpret_cast<void**>(g_p2Ring.vm[i] + Game::kVmScriptPtr) = nullptr;
        }
        g_p2Ring.initialized = true;
    }
    uint8_t state = player[Game::kPlayerState];
    bool focused = g_settings.focusRing && player[Game::kPlayerFocused] && (state == 0 || state == 3) && Visible(player);
    bool viewSet = false;
    for (int i = 0; i < 2; i++) {
        uint8_t* vm = g_p2Ring.vm[i];
        if (focused && !RingRunning(vm)) {
            Game::Fn<SetScriptFn>(Game::kFnAnmSetScript)(nullptr, vm, Game::kAnmScriptFocusRing + i);
        } else if (!focused && RingRunning(vm)) {
            *reinterpret_cast<uint16_t*>(vm + Game::kVmEndFlag) = 1;
        }
        if (!RingRunning(vm)) continue;
        float* pos = reinterpret_cast<float*>(vm + Game::kVmPos);
        pos[0] = Pos(player)[0];
        pos[1] = Pos(player)[1];
        Game::Fn<AnmTickFn>(Game::kFnAnmTick)(anm, vm);
        if (!RingRunning(vm)) continue;
        if (!viewSet) {
            Game::Fn<SetViewFn>(Game::kFnSetPlayfieldView)(Game::At<void>(Game::kPlayfieldView), 0.0f,
                                                          *Game::At<float>(Game::kConstHalf), 0);
            viewSet = true;
        }
        Game::Fn<DrawVmFn>(Game::kFnDrawVm)(0, vm, 1);
    }
}

uint64_t Detour_DrawOverlay(uint8_t* player, uint64_t secondArg) {
    int index = PlayerIndex(player);
    if (index >= 0 && CoopRules_IsDowned(player)) return 1;
    uint64_t result = g_origDrawOverlay(player, secondArg);
    if (index == 1 && Player2_IsActive()) UpdateAndDrawP2Ring(player);
    return result;
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
        { Game::kFnPlayerDrawOverlay, reinterpret_cast<void*>(&Detour_DrawOverlay), reinterpret_cast<void**>(&g_origDrawOverlay), "PlayerDrawOverlay" },
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

void PlayerLook_OnStageStart() {
    g_p2Ring.initialized = false;
}

const LookSettings& PlayerLook_Settings() {
    return g_settings;
}

void PlayerLook_DrawOverlay(OverlayRenderer&) {
    // Nothing: the focus marker is the game's own now (above). Kept so the
    // Present hook's drawer list stays as it is.
}
