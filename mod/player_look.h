#pragma once

#include <cstdint>

class OverlayRenderer;

// How the two players look (docs/07): a color tint per player, the other
// player (and their shots) fading when close to you, and the game's own
// focus marker on the other player while they hold focus. Purely visual:
// everything is applied inside the draw calls and undone right after, so
// the simulation -- and rollback -- never see it.
struct LookSettings {
    uint32_t color = 0xFFFFFF;   // 0xRRGGBB tint for this machine's player (P1 when local)
    uint32_t p2Color = 0xA0C8FF; // same-machine play: P2's tint
    bool proximityFade = true;
    bool focusRing = true; // the game's own focus marker, on P2 too
};

// Hooks the three player draw functions (and skips drawing a downed player,
// coop_rules.h).
bool PlayerLook_Install();
void PlayerLook_SetSettings(const LookSettings& settings);
const LookSettings& PlayerLook_Settings();

// Called at every P2 spawn: the HUD object the focus marker copies from is
// re-created per stage.
void PlayerLook_OnStageStart();

// Present-hook drawer; draws nothing now (the focus marker is drawn by the
// game's own sprite pipeline).
void PlayerLook_DrawOverlay(OverlayRenderer& overlay);
