#pragma once

#include <cstddef>
#include <cstdint>

// Peer-to-peer messages over Steam Networking Messages (Steam's relay, no
// port forwarding), to the peer found through the Steam lobby (lobby.h).
// All calls from the game's main thread, after the game's own SteamAPI_Init.

bool Transport_Init();

// Adopts the lobby's peer once one is present. Returns true while a peer is
// known. Call every frame.
bool Transport_UpdatePeer();
bool Transport_IsHost();
uint64_t Transport_PeerId();
uint64_t Transport_LocalId();

void Transport_Send(const void* data, size_t size, bool reliable);

// Delivers every pending message from the peer to `handler`.
using TransportHandler = void (*)(const uint8_t* data, size_t size);
void Transport_Receive(TransportHandler handler);

// Milliseconds since the last message from the peer (0 if none yet).
uint32_t Transport_MsSinceLastReceive();
