#pragma once

// Steam-lobby-based peer discovery, ported unchanged from the overlay mod
// (overlay/26).
//
// Call Lobby_Init() once, after the game's own SteamAPI is up. No per-frame
// pump is needed: CCallback/CCallResult registration rides on the game's own
// SteamAPI_RunCallbacks(), which it already calls every frame.

enum class LobbyStatus {
    None,           // no lobby, nothing in progress
    Creating,       // CreateLobby (or JoinLobby) requested, awaiting the result
    WaitingForPeer, // in a lobby, but only the local user is in it so far
    Connected,      // a second member is present
};

void Lobby_Init();

// Host action: creates a friends-only 2-player lobby and, once created,
// opens Steam's own native invite overlay (Steam handles the friend
// picker itself -- no custom UI needed for that part). No-op if a lobby
// already exists or is being created/joined.
void Lobby_CreateAndInvite();

// Leaves the current lobby, if any, and resets to LobbyStatus::None.
void Lobby_Leave();

LobbyStatus Lobby_GetStatus();

// True (and fills both out params) once a second lobby member is present.
// outIsOwner reflects whether the LOCAL user is the lobby's owner
// (creator) -- used as the host-role rule for lobby-sourced connections,
// replacing the old SteamID64-numeric-comparison rule doc 00 flagged as a
// placeholder ("lobby owner is host" is unambiguous and matches how
// players will actually think about it: whoever made the lobby is host).
bool Lobby_GetPeer(unsigned long long* outPeerSteamId64, bool* outIsOwner);

// For the status indicator / a future dropdown UI and for logging: number
// of members currently in the lobby (0 if none), and each member's
// display name. outNameSize should be at least 128.
int Lobby_GetMemberCount();
bool Lobby_GetMemberName(int index, char* outName, int outNameSize);
