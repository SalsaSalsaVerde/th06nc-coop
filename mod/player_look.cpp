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
using HudDrawFn = uint64_t (*)(void* hud);
HudDrawFn g_origHudDraw = nullptr;

LookSettings g_settings;

// The overlay mod's near-player fade (overlay/19, from th06_multi_net):
// linear from 20% opacity at 50 units to fully opaque at 100.
const float kFadeNear = 50.0f;
const float kFadeFar = 100.0f;
const float kFadeNearOpacity = 0.20f;

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

// The player's shots fade with the player. Both player draw passes walk
// the 80 shot slots (type 1 shots in the main pass, type 2 in the overlay
// pass, docs/07) and draw each in-use slot's VM; their alpha is scaled for
// the duration of the pass, colors untouched.
class ShotFadeScope {
public:
    ShotFadeScope(uint8_t* player, float alpha) {
        if (alpha >= 1.0f) return;
        for (int i = 0; i < Game::kPlayerShotSlotCount; i++) {
            uint8_t* slot = player + Game::kPlayerShotSlots + static_cast<uintptr_t>(i) * Game::kPlayerShotSlotStride;
            uint8_t* vm = slot + Game::kShotSlotVm;
            uint32_t color = VmColor(vm);
            m_saved[i] = color;
            if (*reinterpret_cast<uint16_t*>(slot + 0x10) == 0 || color == 0) continue;
            uint32_t a = static_cast<uint32_t>(static_cast<float>(color >> 24) * alpha + 0.5f);
            VmColor(vm) = ((a ? a : 1) << 24) | (color & 0xFFFFFF);
            m_player = player;
        }
    }
    ~ShotFadeScope() {
        if (!m_player) return;
        for (int i = 0; i < Game::kPlayerShotSlotCount; i++) {
            uint8_t* slot = m_player + Game::kPlayerShotSlots + static_cast<uintptr_t>(i) * Game::kPlayerShotSlotStride;
            VmColor(slot + Game::kShotSlotVm) = m_saved[i];
        }
    }
    ShotFadeScope(const ShotFadeScope&) = delete;
    ShotFadeScope& operator=(const ShotFadeScope&) = delete;

private:
    uint8_t* m_player = nullptr;
    uint32_t m_saved[Game::kPlayerShotSlotCount] = {};
};

uint64_t Detour_Draw(uint8_t* player, uint64_t secondArg) {
    int index = PlayerIndex(player);
    if (index < 0 || !Player2_IsActive()) return g_origDraw(player, secondArg);
    if (CoopRules_IsDowned(player)) return 1;

    float alpha = FadeFor(index);
    uint32_t tint = ColorOf(index);
    VmColorScope body(player + Game::kPlayerMainVm, tint, alpha);
    VmColorScope left(player + Game::kPlayerOptionVmL, tint, alpha);
    VmColorScope right(player + Game::kPlayerOptionVmR, tint, alpha);
    ShotFadeScope shots(player, alpha);
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
        VmColorScope fade(vm, 0xFFFFFF, FadeFor(1)); // the marker fades with its player
        Game::Fn<DrawVmFn>(Game::kFnDrawVm)(0, vm, 1);
    }
}

// P1's focus marker is the HUD's: its draw task draws the two GUI VMs the
// HUD tick keeps on P1. When P1 is the other player (the guest's view),
// they fade with P1 for the duration of that draw.
//
// With separate resources the HUD would show P1's pool on both machines; the
// guest sees its own instead. The HUD draw (and FUN_140041f50, which it
// calls) reads lives, bombs and power straight from the globals, so P2's
// pool is swapped in for the draw alone -- outside any simulation step.
class HudResourceScope {
public:
    HudResourceScope() {
        m_active = Netplay_LocalPlayerIndex() == 1 && Player2_IsActive() && !CoopRules_Settings().sharedResources;
        if (m_active) Exchange();
    }
    ~HudResourceScope() {
        if (m_active) Exchange();
    }
    HudResourceScope(const HudResourceScope&) = delete;
    HudResourceScope& operator=(const HudResourceScope&) = delete;

private:
    static void Exchange() {
        PlayerResources* p2 = Player2_Resources();
        uint8_t* lives = Game::At<uint8_t>(Game::kLives);
        uint8_t* bombs = Game::At<uint8_t>(Game::kBombs);
        uint32_t* power = Game::At<uint32_t>(Game::kPower);
        uint8_t l = *lives, b = *bombs;
        uint32_t w = *power;
        *lives = p2->lives;
        *bombs = p2->bombs;
        *power = p2->power;
        p2->lives = l;
        p2->bombs = b;
        p2->power = w;
    }
    bool m_active = false;
};

uint64_t Detour_HudDraw(void* hud) {
    HudResourceScope resources;
    uint8_t* gui = *Game::At<uint8_t*>(Game::kGuiObjectPtr);
    float alpha = Player2_IsActive() && gui ? FadeFor(0) : 1.0f;
    if (alpha >= 1.0f) return g_origHudDraw(hud);
    VmColorScope a(gui + Game::kGuiFocusRingVm, 0xFFFFFF, alpha);
    VmColorScope b(gui + Game::kGuiFocusRingVm + Game::kVmSize, 0xFFFFFF, alpha);
    return g_origHudDraw(hud);
}

uint64_t Detour_DrawOverlay(uint8_t* player, uint64_t secondArg) {
    int index = PlayerIndex(player);
    if (index >= 0 && CoopRules_IsDowned(player)) return 1;
    uint64_t result = 0;
    {
        ShotFadeScope shots(player, index >= 0 && Player2_IsActive() ? FadeFor(index) : 1.0f);
        result = g_origDrawOverlay(player, secondArg);
    }
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
        { Game::kFnHudDraw, reinterpret_cast<void*>(&Detour_HudDraw), reinterpret_cast<void**>(&g_origHudDraw), "HudDraw" },
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
