#include "lobby.h"
#include "mod_log.h"

#include <cstring>

#include <steam/steam_api_flat.h>
#include <steam/isteammatchmaking.h>

namespace {

// All CCallback/CCallResult registration below goes through
// SteamAPI_RegisterCallback/SteamAPI_RegisterCallResult under the hood
// (see steam_api_common.h) -- the exact same mechanism a normal
// Steamworks C++ game uses, and th06nc.exe already has a live
// SteamAPI_RunCallbacks() pump running for its own achievements/stats
// integration. Registering here just adds our objects to that same
// dispatch list; it does NOT require (and must NOT use) manual dispatch
// mode, which would need th06nc.exe's own callback pump switched over too
// and isn't ours to change.
class LobbySystem {
public:
    LobbySystem()
        : m_joinRequestedCallback(this, &LobbySystem::OnGameLobbyJoinRequested),
          m_lobbyEnterCallback(this, &LobbySystem::OnLobbyEnter),
          m_chatUpdateCallback(this, &LobbySystem::OnLobbyChatUpdate) {}

    void CreateAndInvite() {
        if (m_status != LobbyStatus::None) {
            ModLog("Lobby: CreateAndInvite ignored, already in status %d", static_cast<int>(m_status));
            return;
        }
        ISteamMatchmaking* mm = SteamAPI_SteamMatchmaking();
        if (!mm) {
            ModLog("Lobby: ISteamMatchmaking unavailable");
            return;
        }
        m_status = LobbyStatus::Creating;
        SteamAPICall_t call = SteamAPI_ISteamMatchmaking_CreateLobby(mm, k_ELobbyTypeFriendsOnly, 2);
        m_lobbyCreatedResult.Set(call, this, &LobbySystem::OnLobbyCreated);
        ModLog("Lobby: CreateLobby requested");
    }

    void Leave() {
        if (m_lobbyId != 0) {
            ISteamMatchmaking* mm = SteamAPI_SteamMatchmaking();
            if (mm) {
                SteamAPI_ISteamMatchmaking_LeaveLobby(mm, m_lobbyId);
            }
            ModLog("Lobby: left lobby %llu", static_cast<unsigned long long>(m_lobbyId));
        }
        m_lobbyId = 0;
        m_peerSteamId = 0;
        m_memberCount = 0;
        m_status = LobbyStatus::None;
    }

    LobbyStatus GetStatus() const { return m_status; }

    bool GetPeer(unsigned long long* outPeer, bool* outIsOwner) const {
        if (m_status != LobbyStatus::Connected) return false;
        *outPeer = m_peerSteamId;
        *outIsOwner = m_localIsOwner;
        return true;
    }

    int GetMemberCount() const { return m_memberCount; }

    bool GetMemberName(int index, char* outName, int outSize) const {
        if (index < 0 || index >= m_memberCount) return false;
        strncpy_s(outName, outSize, m_memberNames[index], _TRUNCATE);
        return true;
    }

private:
    void OnLobbyCreated(LobbyCreated_t* result, bool ioFailure) {
        if (ioFailure || result->m_eResult != k_EResultOK) {
            ModLog("Lobby: CreateLobby failed (eResult=%d, ioFailure=%d)",
                   result ? static_cast<int>(result->m_eResult) : -1, ioFailure ? 1 : 0);
            m_status = LobbyStatus::None;
            return;
        }
        m_lobbyId = result->m_ulSteamIDLobby;
        ModLog("Lobby: created %llu, opening invite overlay", static_cast<unsigned long long>(m_lobbyId));
        ISteamFriends* friendsIface = SteamAPI_SteamFriends();
        if (friendsIface) {
            SteamAPI_ISteamFriends_ActivateGameOverlayInviteDialog(friendsIface, m_lobbyId);
        }
        RefreshMembers();
    }

    void OnGameLobbyJoinRequested(GameLobbyJoinRequested_t* data) {
        uint64_steamid lobbyId = data->m_steamIDLobby.ConvertToUint64();
        ModLog("Lobby: join requested for lobby %llu (via friend %llu)",
               static_cast<unsigned long long>(lobbyId),
               static_cast<unsigned long long>(data->m_steamIDFriend.ConvertToUint64()));
        ISteamMatchmaking* mm = SteamAPI_SteamMatchmaking();
        if (!mm) return;
        m_status = LobbyStatus::Creating; // reused as "join in progress"
        SteamAPI_ISteamMatchmaking_JoinLobby(mm, lobbyId);
    }

    void OnLobbyEnter(LobbyEnter_t* data) {
        m_lobbyId = data->m_ulSteamIDLobby;
        ModLog("Lobby: entered %llu (chatRoomEnterResponse=%u)",
               static_cast<unsigned long long>(m_lobbyId), data->m_EChatRoomEnterResponse);
        m_status = LobbyStatus::WaitingForPeer;
        RefreshMembers();
    }

    void OnLobbyChatUpdate(LobbyChatUpdate_t* /*data*/) {
        RefreshMembers();
    }

    void RefreshMembers() {
        if (!m_lobbyId) return;
        ISteamMatchmaking* mm = SteamAPI_SteamMatchmaking();
        ISteamFriends* friendsIface = SteamAPI_SteamFriends();
        ISteamUser* user = SteamAPI_SteamUser_v023();
        if (!mm || !friendsIface || !user) return;

        uint64_steamid localId = SteamAPI_ISteamUser_GetSteamID(user);
        int count = SteamAPI_ISteamMatchmaking_GetNumLobbyMembers(mm, m_lobbyId);
        if (count > kMaxMembers) count = kMaxMembers;
        m_memberCount = count;

        uint64_steamid owner = SteamAPI_ISteamMatchmaking_GetLobbyOwner(mm, m_lobbyId);
        m_localIsOwner = (owner == localId);

        uint64_steamid peer = 0;
        for (int i = 0; i < count; i++) {
            uint64_steamid memberId = SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex(mm, m_lobbyId, i);
            const char* name = SteamAPI_ISteamFriends_GetFriendPersonaName(friendsIface, memberId);
            strncpy_s(m_memberNames[i], sizeof(m_memberNames[i]), name ? name : "?", _TRUNCATE);
            if (memberId != localId) {
                peer = memberId;
            }
        }

        if (peer != 0) {
            bool wasConnected = (m_status == LobbyStatus::Connected);
            m_peerSteamId = peer;
            m_status = LobbyStatus::Connected;
            if (!wasConnected) {
                ModLog("Lobby: peer connected (%llu), local role = %s",
                       static_cast<unsigned long long>(peer), m_localIsOwner ? "HOST (lobby owner)" : "client");
            }
        } else {
            m_status = LobbyStatus::WaitingForPeer;
        }
    }

    static const int kMaxMembers = 2;
    LobbyStatus m_status = LobbyStatus::None;
    uint64_steamid m_lobbyId = 0;
    uint64_steamid m_peerSteamId = 0;
    bool m_localIsOwner = false;
    int m_memberCount = 0;
    char m_memberNames[kMaxMembers][128] = {};

    CCallResult<LobbySystem, LobbyCreated_t> m_lobbyCreatedResult;
    CCallback<LobbySystem, GameLobbyJoinRequested_t> m_joinRequestedCallback;
    CCallback<LobbySystem, LobbyEnter_t> m_lobbyEnterCallback;
    CCallback<LobbySystem, LobbyChatUpdate_t> m_chatUpdateCallback;
};

LobbySystem* g_lobby = nullptr;

} // namespace

void Lobby_Init() {
    if (!g_lobby) {
        g_lobby = new LobbySystem();
        ModLog("Lobby: initialized (callbacks registered)");
    }
}

void Lobby_CreateAndInvite() {
    if (g_lobby) g_lobby->CreateAndInvite();
}

void Lobby_Leave() {
    if (g_lobby) g_lobby->Leave();
}

LobbyStatus Lobby_GetStatus() {
    return g_lobby ? g_lobby->GetStatus() : LobbyStatus::None;
}

bool Lobby_GetPeer(unsigned long long* outPeerSteamId64, bool* outIsOwner) {
    if (!g_lobby) return false;
    return g_lobby->GetPeer(outPeerSteamId64, outIsOwner);
}

int Lobby_GetMemberCount() {
    return g_lobby ? g_lobby->GetMemberCount() : 0;
}

bool Lobby_GetMemberName(int index, char* outName, int outNameSize) {
    return g_lobby ? g_lobby->GetMemberName(index, outName, outNameSize) : false;
}
