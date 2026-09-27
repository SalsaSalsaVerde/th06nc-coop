#pragma once

#include <cstdint>

class OverlayRenderer;

// How the two players look (docs/07): a color tint per player, the other
// player fading (with a dark outline) when close to you, and a focus ring
// on the other player while they hold focus (the game itself shows no
// hitbox). Purely visual: everything is applied inside the draw calls and
// undone right after, so the simulation -- and rollback -- never see it.
struct LookSettings {
    uint32_t color = 0xFFFFFF;   // 0xRRGGBB tint for this machine's player (P1 when local)
    uint32_t p2Color = 0xA0C8FF; // same-machine play: P2's tint
    bool proximityFade = true;
    bool outline = true;
    bool focusRing = true;
    // Depth offset of the outline copies relative to the sprite. The game's
    // sprite pipeline treats a larger z as nearer (docs/11), so a negative
    // value puts the outline behind the sprite.
    float outlineDepthOffset = -0.004f;
};

// Hooks the three player draw functions (and skips drawing a downed player,
// coop_rules.h).
bool PlayerLook_Install();
void PlayerLook_SetSettings(const LookSettings& settings);
const LookSettings& PlayerLook_Settings();

// Draws the focus rings; called from the Present hook with a frame begun.
void PlayerLook_DrawOverlay(OverlayRenderer& overlay);
