#pragma once

// A one-line on-screen status readout (lobby, waiting-for-partner, desync
// warnings), drawn with the game's own bitmap font through a D3D11 Present
// hook (ported from the overlay mod, overlay/14 and overlay/51).
//
// Call from the init thread (not DllMain): waits for the game window, then
// patches IDXGISwapChain::Present.
void StatusOverlay_Install();

// Supplies the text each frame ('\n' separates lines). Writes an empty
// string to draw nothing.
using StatusTextProvider = void (*)(char* out, int outSize, float* r, float* g, float* b);
void StatusOverlay_SetProvider(StatusTextProvider provider);

// A second block in the bottom-left corner (the boss DPS meter).
void StatusOverlay_SetCornerProvider(StatusTextProvider provider);

// Draws things placed on the playfield (player_look.h's focus rings) each
// frame, under the status text.
class OverlayRenderer;
using OverlayWorldDrawer = void (*)(OverlayRenderer& overlay);
void StatusOverlay_SetWorldDrawer(OverlayWorldDrawer drawer);
