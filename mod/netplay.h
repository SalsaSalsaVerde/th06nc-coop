#pragma once

#include <cstdint>

// Two-machine co-op over Steam (docs/02). The lobby owner (host) plays P1 and
// the guest plays P2 on BOTH machines: each machine runs the same
// deterministic simulation with both players' inputs, exchanging only
// inputs. Modes (th06nc_native_coop.ini [netplay] mode):
//   rollback -- simulate ahead on predicted remote input, re-simulate from a
//               snapshot when the real input differs
//   lockstep -- wait for the remote input before simulating each frame
//
// Menus are local; synchronization starts at every stage start (a barrier
// where both machines wait for each other and the host's RNG seed is
// adopted) and ends at every stage teardown.
//
// Hotkeys (game window focused): F9 create a lobby and open Steam's invite
// dialog, F10 leave the lobby.

bool Netplay_Install();

// One line of status text for the on-screen overlay (status_overlay.h);
// empty when there is nothing worth showing.
void Netplay_StatusText(char* out, int outSize, float* r, float* g, float* b);

// Which player this machine controls while connected (0 host, 1 guest), or
// -1 when not connected (same-machine play, sync test).
int Netplay_LocalPlayerIndex();

// The partner's chosen player color (0xRRGGBB), once they've sent it.
bool Netplay_PartnerColor(uint32_t* rgb);

// This machine's netcode choice ([netplay] mode / input_delay, the F8
// panel). Online the host's is used by both machines, fixed at each stage
// start (docs/14).
bool Netplay_OwnRollback();
int Netplay_OwnInputDelay();
void Netplay_SetOwnNetcode(bool rollback, int inputDelay);
// The host's choice as last told, on a connected guest.
bool Netplay_HostNetcode(bool* rollback, int* inputDelay);
