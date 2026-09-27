#include "transport.h"
#include "lobby.h"
#include "mod_log.h"

#include <windows.h>
#include <steam/steam_api_flat.h>

namespace {

// Distinct from the overlay mod's channel 0, so a stray old build on the
// other end is ignored rather than misparsed.
const int kChannel = 6;

ISteamNetworkingMessages* g_messages = nullptr;
SteamNetworkingIdentity g_peer;
uint64_t g_peerId = 0;
uint64_t g_localId = 0;
bool g_isHost = false;
ULONGLONG g_lastReceiveTick = 0;

void __cdecl OnSessionRequest(SteamNetworkingMessagesSessionRequest_t* request) {
    if (!g_messages) return;
    uint64_t from = request->m_identityRemote.GetSteamID64();
    unsigned long long lobbyPeer = 0;
    bool isOwner = false;
    bool known = (g_peerId != 0 && from == g_peerId) ||
                 (Lobby_GetPeer(&lobbyPeer, &isOwner) && from == lobbyPeer);
    if (!known) {
        ModLog("Transport: rejected session from %llu (not the lobby peer)", static_cast<unsigned long long>(from));
        return;
    }
    bool accepted = SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser(g_messages, request->m_identityRemote);
    ModLog("Transport: accepted session from %llu (%d)", static_cast<unsigned long long>(from), accepted ? 1 : 0);
}

} // namespace

bool Transport_Init() {
    g_messages = SteamAPI_SteamNetworkingMessages_SteamAPI_v002();
    if (!g_messages) {
        ModLog("Transport: ISteamNetworkingMessages unavailable -- netplay disabled");
        return false;
    }
    ISteamNetworkingUtils* utils = SteamAPI_SteamNetworkingUtils_SteamAPI_v004();
    if (utils) {
        SteamAPI_ISteamNetworkingUtils_SetGlobalCallback_MessagesSessionRequest(utils, &OnSessionRequest);
    }
    ISteamUser* user = SteamAPI_SteamUser_v023();
    if (user) {
        g_localId = SteamAPI_ISteamUser_GetSteamID(user);
        ModLog("Transport: ready, local SteamID64 %llu", static_cast<unsigned long long>(g_localId));
    }
    return true;
}

bool Transport_UpdatePeer() {
    if (!g_messages) return false;
    unsigned long long peer = 0;
    bool isOwner = false;
    if (!Lobby_GetPeer(&peer, &isOwner)) {
        if (g_peerId != 0) {
            ModLog("Transport: lobby peer gone");
            g_peerId = 0;
        }
        return false;
    }
    if (peer != g_peerId) {
        g_peerId = peer;
        g_peer.SetSteamID64(peer);
        g_isHost = isOwner;
        g_lastReceiveTick = 0;
        ModLog("Transport: peer %llu, local role %s", peer, g_isHost ? "HOST (P1)" : "GUEST (P2)");
    }
    return true;
}

bool Transport_IsHost() {
    return g_isHost;
}

uint64_t Transport_PeerId() {
    return g_peerId;
}

uint64_t Transport_LocalId() {
    return g_localId;
}

void Transport_Send(const void* data, size_t size, bool reliable) {
    if (!g_messages || g_peerId == 0) return;
    int flags = reliable ? k_nSteamNetworkingSend_Reliable : k_nSteamNetworkingSend_UnreliableNoDelay;
    SteamAPI_ISteamNetworkingMessages_SendMessageToUser(g_messages, g_peer, data, static_cast<uint32>(size), flags, kChannel);
}

void Transport_Receive(TransportHandler handler) {
    if (!g_messages) return;
    SteamNetworkingMessage_t* batch[32];
    for (;;) {
        int count = SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel(g_messages, kChannel, batch, 32);
        if (count <= 0) return;
        for (int i = 0; i < count; i++) {
            SteamNetworkingMessage_t* msg = batch[i];
            if (msg->m_identityPeer.GetSteamID64() == g_peerId) {
                g_lastReceiveTick = GetTickCount64();
                handler(static_cast<const uint8_t*>(msg->m_pData), static_cast<size_t>(msg->m_cbSize));
            }
            msg->m_pfnRelease(msg);
        }
        if (count < 32) return;
    }
}

uint32_t Transport_MsSinceLastReceive() {
    if (g_lastReceiveTick == 0) return 0;
    return static_cast<uint32_t>(GetTickCount64() - g_lastReceiveTick);
}
