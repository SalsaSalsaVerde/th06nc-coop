#pragma once

#include <cstdint>

class OverlayRenderer;

// The co-op "lobby" settings screen (docs/08): F8 in the game's menus opens
// a panel over the game where the host (or a player on their own) sets the
// co-op rules -- boss HP, invincibility, targeting, shared or separate
// resources, revive time, starting stock -- plus their own player color and
// the same-machine P2. Online the guest sees the host's rules read-only
// (they're sent to it continuously); its own color it can still change.
// Closing saves everything back to th06nc_native_coop.ini.
//
// Driven by the game's own input (so a controller works too): while the
// panel is open, the game's menu gets no input.

// Called with each device read of the game's input poll outside a stage;
// returns what the game should see.
uint32_t SettingsPanel_FilterInput(uint32_t input);

bool SettingsPanel_IsOpen();

// Draws the panel if open; called from the Present hook with a frame begun.
void SettingsPanel_Draw(OverlayRenderer& overlay);
