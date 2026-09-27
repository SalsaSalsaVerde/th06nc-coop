#include "netplay.h"
#include "checksum.h"
#include "config.h"
#include "coop_rules.h"
#include "game.h"
#include "game_chain.h"
#include "hooks.h"
#include "lobby.h"
#include "local_input.h"
#include "mod_log.h"
#include "player2.h"
#include "player_look.h"
#include "sim_control.h"
#include "snapshot.h"
#include "transport.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

enum class State {
    Offline,            // no peer
    Idle,               // peer connected, local menus
    Barrier,            // stage started here, waiting for the peer to reach it too
    Running,            // synchronized stage
    Degraded,           // peer lost mid-stage: keep playing locally until the stage ends
    SyncTestCalibrate,  // single-machine rollback self-test: calibrating
    SyncTest,           // single-machine rollback self-test: running
};

const char* StateName(State s) {
    switch (s) {
        case State::Offline: return "Offline";
        case State::Idle: return "Idle";
        case State::Barrier: return "Barrier";
        case State::Running: return "Running";
        case State::Degraded: return "Degraded";
        case State::SyncTestCalibrate: return "SyncTestCalibrate";
        case State::SyncTest: return "SyncTest";
    }
    return "?";
}

const uint8_t kProtocolVersion = 3;
enum MsgType : uint8_t { kMsgReady = 1, kMsgInputs = 2, kMsgChecksum = 3, kMsgLoadout = 4 };

#pragma pack(push, 1)
struct MsgHeader {
    uint8_t type;
    uint8_t version;
    uint16_t session;
};
// Sent while in the menus: what this player has selected, so the stage can
// start with the host's character as P1 and the guest's as P2 on both
// machines, at the host's difficulty.
struct LoadoutMsg {
    MsgHeader header;
    uint8_t character;
    uint8_t shotType;
    uint8_t difficulty;
    CoopSettings coop; // the sender's; the guest adopts the host's
    uint32_t color;    // the sender's player color (visual only, docs/07)
};
struct ReadyMsg {
    MsgHeader header;
    uint32_t buildStamp;
    uint8_t character; // P1's
    uint8_t shotType;
    uint8_t p2Character;
    uint8_t p2ShotType;
    uint8_t difficulty;
    int32_t stage;
    uint16_t rngSeed;
    uint32_t rngCounter;
    // Shared resources at the start of the stage; the guest adopts the
    // host's (the two games' starting-lives options may differ).
    uint8_t lives;
    uint8_t bombs;
    uint32_t power;
    uint32_t score;
    int32_t graze;
    CoopSettings coop;
    // P2's own pool (separate resources); adopted from the host like P1's.
    uint8_t p2Lives;
    uint8_t p2Bombs;
    uint32_t p2Power;
};
const int kMaxInputsPerPacket = 32;
struct InputsMsg {
    MsgHeader header;
    int32_t firstFrame;
    int32_t senderFrame;     // next frame the sender will simulate
    int32_t senderAdvantage; // sender's frame minus the newest frame it has from us
    uint8_t count;
    uint16_t masks[kMaxInputsPerPacket];
};
struct ChecksumMsg {
    MsgHeader header;
    int32_t frame;
    uint64_t region[kChecksumRegionCount];
};
#pragma pack(pop)

const int kRing = 256;
const int kChecksumSlots = 16;
const int kCalibrationFrames = 30;
const int kBarrierTimeoutFrames = 60 * 120;
const int kChecksumInterval = 60;
const int kStatsInterval = 600;
const uint32_t kDisconnectMs = 5000;
// Gameplay buttons only: shoot, bomb, focus, directions, skip. The pause bit
// (0x8) is stripped -- a pause menu allocates a task on the heap mid-stage,
// which snapshots can't undo.
const uint32_t kNetplayInputMask = 0x1F7;

struct InputRing {
    int frame[kRing];
    uint16_t mask[kRing];
    void Clear() {
        for (int& f : frame) f = -1;
    }
    bool Has(int f) const { return f >= 0 && frame[f % kRing] == f; }
    uint16_t Get(int f) const { return mask[f % kRing]; }
    void Set(int f, uint16_t m) {
        frame[f % kRing] = f;
        mask[f % kRing] = m;
    }
};

struct ChecksumEntry {
    int frame = -1;
    GameChecksum sum = {};
    bool sent = false;
    bool compared = false;
};

struct Stats {
    int rollbacks = 0;
    int rollbackFrames = 0;
    int predictedFrames = 0;
    int stalls = 0;
    int unsafeRollbacks = 0;
};

State g_state = State::Offline;
bool g_networkReady = false;
bool g_connected = false;
uint16_t g_session = 0;

// Stage start (P2 spawn) happens mid-frame; the barrier begins after that
// frame's step returns, so calibration never sees simulation writes.
bool g_pendingBarrier = false;
std::vector<uint8_t> g_p1AtInit;
std::vector<uint8_t> g_p2AtInit;

int g_barrierFrames = 0;
int g_readyResendTimer = 0;
bool g_peerReadyValid = false;
ReadyMsg g_peerReady = {};
uint32_t g_partnerColor = 0;
bool g_partnerColorValid = false;

int g_frame = 0;          // next frame to simulate
int g_localNext = 0;      // next local input frame to record
int g_lastConfirmed = -1; // every remote input <= this is known
int g_remoteFrame = 0;    // peer's reported next frame
int g_peerAdvantage = 0;
int g_rollbackTarget = -1;
int g_framesSinceSyncStall = 0;
InputRing g_local;
InputRing g_remote;
uint16_t g_usedRemote[kRing];
bool g_usedPrediction[kRing];
uint64_t g_listSignature[kRing];
ChecksumEntry g_localSums[kChecksumSlots];
ChecksumEntry g_remoteSums[kChecksumSlots];
int g_firstDesyncFrame = -1;
Stats g_stats;

bool g_prevF9 = false;
bool g_prevF10 = false;

bool g_pendingSyncTest = false;
struct RecordedInputs {
    uint32_t p1;
    uint32_t p2;
};
RecordedInputs g_syncTestInputs[kRing];
struct SyncTestStats {
    int checks = 0;
    int failures = 0;
    int skipped = 0;
};
SyncTestStats g_syncTestStats;

using SceneInitFn = uint64_t (*)(void* scene);
SceneInitFn g_origSceneInit = nullptr;

// Menu selections (docs/04). The host's selection is P1 on both machines,
// the guest's is P2; the guest's game temporarily adopts the host's
// selection for the stage and gets its own back when the stage ends.
struct Selection {
    uint8_t character;
    uint8_t shotType;
    uint8_t difficulty;
};
bool g_peerSelectionValid = false;
Selection g_peerSelection = {};
bool g_ownSelectionOverridden = false;
Selection g_ownSelection = {};
int g_selectionSendTimer = 0;

Selection ReadSelection() {
    return Selection{ *Game::At<uint8_t>(Game::kCharacter), *Game::At<uint8_t>(Game::kShotType),
                      *Game::At<uint8_t>(Game::kDifficulty) };
}

void WriteSelection(const Selection& s) {
    *Game::At<uint8_t>(Game::kCharacter) = s.character;
    *Game::At<uint8_t>(Game::kShotType) = s.shotType;
    *Game::At<uint8_t>(Game::kDifficulty) = s.difficulty;
}

bool IsHost() {
    return Transport_IsHost();
}

const Config& Cfg() {
    return Config_Get();
}

MsgHeader Header(MsgType type, uint16_t session) {
    return MsgHeader{ type, kProtocolVersion, session };
}

bool GameWindowFocused() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

void PollHotkeys() {
    bool focused = GameWindowFocused();
    bool f9 = focused && (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
    bool f10 = focused && (GetAsyncKeyState(VK_F10) & 0x8000) != 0;
    if (f9 && !g_prevF9) Lobby_CreateAndInvite();
    if (f10 && !g_prevF10) Lobby_Leave();
    g_prevF9 = f9;
    g_prevF10 = f10;
}

const char* g_stateReason = "";

// P2's input comes from the frame's forced input whenever the mod is
// stepping the simulation itself (online, sync test, degraded), and from the
// local second-player devices only in plain offline play.
void UpdateInputProvider() {
    bool forced = g_connected || (g_state != State::Offline && g_state != State::Idle);
    Player2_SetInputProvider(forced ? &SimControl_ForcedPlayer2Input : nullptr);
}

void SetState(State s, const char* reason) {
    if (s == g_state) return;
    ModLog("Netplay: %s -> %s (%s)", StateName(g_state), StateName(s), reason);
    g_state = s;
    g_stateReason = reason;
    UpdateInputProvider();
}

void LogStats(const char* when) {
    ModLog("Netplay stats (%s): frame=%d confirmed=%d rollbacks=%d (frames resimulated %d, unsafe %d) predicted=%d stalls=%d",
           when, g_frame, g_lastConfirmed, g_stats.rollbacks, g_stats.rollbackFrames, g_stats.unsafeRollbacks,
           g_stats.predictedFrames, g_stats.stalls);
}

// ---- checksums ------------------------------------------------------------

ChecksumEntry* SumSlot(ChecksumEntry* table, int frame) {
    return &table[(frame / kChecksumInterval) % kChecksumSlots];
}

void CompareSums(int frame) {
    ChecksumEntry* local = SumSlot(g_localSums, frame);
    ChecksumEntry* remote = SumSlot(g_remoteSums, frame);
    if (local->frame != frame || remote->frame != frame || local->compared || !local->sent) return;
    local->compared = true;

    bool match = true;
    for (int r = 0; r < kChecksumRegionCount; r++) {
        if (local->sum.region[r] != remote->sum.region[r]) {
            match = false;
            if (g_firstDesyncFrame < 0) {
                ModLog("Netplay: DESYNC at frame %d in %s (local %016llX, peer %016llX)", frame,
                       Checksum_RegionName(r), static_cast<unsigned long long>(local->sum.region[r]),
                       static_cast<unsigned long long>(remote->sum.region[r]));
            }
        }
    }
    if (!match && g_firstDesyncFrame < 0) {
        g_firstDesyncFrame = frame;
    } else if (match && (frame == 0 || frame % (kChecksumInterval * 10) == 0)) {
        ModLog("Netplay: checksums match at frame %d", frame);
    }
}

void StoreLocalSum(int frame) {
    ChecksumEntry* entry = SumSlot(g_localSums, frame);
    // Once sent, a frame's inputs were all confirmed when it was computed;
    // re-simulating it can't legitimately change it.
    if (entry->frame == frame && entry->sent) return;
    entry->frame = frame;
    entry->sum = Checksum_Compute(Player2_IsActive() ? Player2_Struct() : nullptr);
    entry->sent = false;
    entry->compared = false;
}

// A frame's checksum is only meaningful once every input up to it is known
// (and any rollback those inputs triggered has been applied).
void SendConfirmedSums() {
    for (ChecksumEntry& entry : g_localSums) {
        if (entry.frame < 0 || entry.sent || entry.frame > g_lastConfirmed) continue;
        ChecksumMsg msg = {};
        msg.header = Header(kMsgChecksum, g_session);
        msg.frame = entry.frame;
        memcpy(msg.region, entry.sum.region, sizeof(msg.region));
        Transport_Send(&msg, sizeof(msg), true);
        entry.sent = true;
        CompareSums(entry.frame);
    }
}

// ---- inputs ---------------------------------------------------------------

uint16_t PredictRemote() {
    return g_lastConfirmed >= 0 ? g_remote.Get(g_lastConfirmed) : 0;
}

void RecordLocalInputs() {
    while (g_localNext <= g_frame + Cfg().netplayInputDelay) {
        uint16_t mask = static_cast<uint16_t>(SimControl_PollLocalDevices() & kNetplayInputMask);
        g_local.Set(g_localNext, mask);
        g_localNext++;
    }
}

void SendInputs() {
    InputsMsg msg = {};
    msg.header = Header(kMsgInputs, g_session);
    int last = g_localNext - 1;
    int first = last - kMaxInputsPerPacket + 1;
    if (first < 0) first = 0;
    msg.firstFrame = first;
    msg.count = static_cast<uint8_t>(last - first + 1);
    for (int i = 0; i < msg.count; i++) {
        msg.masks[i] = g_local.Get(first + i);
    }
    msg.senderFrame = g_frame;
    msg.senderAdvantage = g_frame - g_remoteFrame;
    size_t size = offsetof(InputsMsg, masks) + msg.count * sizeof(uint16_t);
    Transport_Send(&msg, size, false);
}

void OnInputs(const InputsMsg& msg) {
    if (msg.senderFrame > g_remoteFrame) g_remoteFrame = msg.senderFrame;
    g_peerAdvantage = msg.senderAdvantage;
    for (int i = 0; i < msg.count; i++) {
        int f = msg.firstFrame + i;
        if (g_remote.Has(f) || f < g_frame - kRing / 2) continue;
        uint16_t mask = msg.masks[i];
        g_remote.Set(f, mask);
        if (f < g_frame && g_usedPrediction[f % kRing] && g_usedRemote[f % kRing] != mask) {
            if (g_rollbackTarget < 0 || f < g_rollbackTarget) g_rollbackTarget = f;
        }
    }
    while (g_remote.Has(g_lastConfirmed + 1)) g_lastConfirmed++;
}

// ---- stepping -------------------------------------------------------------

uint64_t StepFrame(int f, uint32_t* outCode, bool resimulating) {
    bool haveRemote = g_remote.Has(f);
    uint16_t remote = haveRemote ? g_remote.Get(f) : PredictRemote();
    g_usedPrediction[f % kRing] = !haveRemote;
    g_usedRemote[f % kRing] = remote;
    if (!haveRemote && !resimulating) g_stats.predictedFrames++;

    uint64_t signatureBefore = Chain_Signature();
    g_listSignature[f % kRing] = signatureBefore;
    if (Cfg().netplayRollback) Snapshot_Save(f);

    uint16_t local = g_local.Get(f);
    uint32_t p1 = IsHost() ? local : remote;
    uint32_t p2 = IsHost() ? remote : local;
    uint64_t result = SimControl_StepForced(outCode, p1, p2, resimulating);

    if (!resimulating && Chain_Signature() != signatureBefore) {
        ModLog("Netplay: task list changed during frame %d%s", f,
               haveRemote ? "" : " (on PREDICTED input -- a rollback past this frame would be unsafe)");
    }
    if (f % kChecksumInterval == 0) StoreLocalSum(f);
    return result;
}

void DoRollback() {
    int target = g_rollbackTarget;
    g_rollbackTarget = -1;
    if (target >= g_frame) return;
    if (!Snapshot_Has(target)) {
        g_stats.unsafeRollbacks++;
        ModLog("Netplay: rollback to %d impossible (snapshot gone, now at %d) -- desync likely", target, g_frame);
        return;
    }
    if (g_listSignature[target % kRing] != Chain_Signature()) {
        g_stats.unsafeRollbacks++;
        ModLog("Netplay: rollback to %d skipped -- task list changed since then -- desync likely", target);
        return;
    }
    Snapshot_Load(target);
    int end = g_frame;
    for (int f = target; f < end; f++) {
        uint32_t code = 0;
        if (StepFrame(f, &code, true) != 0) {
            ModLog("Netplay: game requested exit while re-simulating frame %d", f);
            break;
        }
    }
    g_stats.rollbacks++;
    g_stats.rollbackFrames += end - target;
}

bool CanStep(int f) {
    if (!g_local.Has(f)) return false;
    if (g_remote.Has(f)) return true;
    if (!Cfg().netplayRollback) return false;
    return f - g_lastConfirmed <= Cfg().netplayMaxRollback;
}

// Both sides see the same round-trip skew in "my frame minus the newest
// frame I have from you", so comparing the two advantages isolates actual
// speed difference. The side that's ahead drops a frame now and then.
bool ShouldStallForTimeSync() {
    if (!Cfg().netplayRollback) return false;
    int localAdvantage = g_frame - g_remoteFrame;
    g_framesSinceSyncStall++;
    if ((localAdvantage - g_peerAdvantage) / 2 >= 1 && g_framesSinceSyncStall >= 4) {
        g_framesSinceSyncStall = 0;
        return true;
    }
    return false;
}

void ResetRun() {
    g_frame = 0;
    g_local.Clear();
    g_remote.Clear();
    for (int f = 0; f < Cfg().netplayInputDelay; f++) g_local.Set(f, 0);
    g_localNext = Cfg().netplayInputDelay;
    g_lastConfirmed = -1;
    g_remoteFrame = 0;
    g_peerAdvantage = 0;
    g_rollbackTarget = -1;
    g_framesSinceSyncStall = 0;
    memset(g_usedPrediction, 0, sizeof(g_usedPrediction));
    for (ChecksumEntry& e : g_localSums) e = ChecksumEntry();
    for (ChecksumEntry& e : g_remoteSums) e = ChecksumEntry();
    g_firstDesyncFrame = -1;
    g_stats = Stats();
    Snapshot_Clear();
}

// ---- barrier --------------------------------------------------------------

// Bools read off the wire may hold any byte value.
void NormalizeBools(CoopSettings& coop) {
    uint8_t raw[2];
    memcpy(&raw[0], &coop.invincible, 1);
    memcpy(&raw[1], &coop.sharedResources, 1);
    coop.invincible = raw[0] != 0;
    coop.sharedResources = raw[1] != 0;
}

ReadyMsg MakeReady() {
    ReadyMsg msg = {};
    msg.header = Header(kMsgReady, static_cast<uint16_t>(g_session + 1));
    msg.buildStamp = Game::kExpectedTimeDateStamp;
    msg.character = *Game::At<uint8_t>(Game::kCharacter);
    msg.shotType = *Game::At<uint8_t>(Game::kShotType);
    Player2_CurrentLoadout(&msg.p2Character, &msg.p2ShotType);
    msg.difficulty = *Game::At<uint8_t>(Game::kDifficulty);
    msg.stage = *Game::At<int32_t>(Game::kStageNumber);
    msg.rngSeed = *Game::At<uint16_t>(Game::kRngSeed);
    msg.rngCounter = *Game::At<uint32_t>(Game::kRngCounter);
    msg.lives = *Game::At<uint8_t>(Game::kLives);
    msg.bombs = *Game::At<uint8_t>(Game::kBombs);
    msg.power = *Game::At<uint32_t>(Game::kPower);
    msg.score = *Game::At<uint32_t>(Game::kScore);
    msg.graze = *Game::At<int32_t>(Game::kGraze);
    msg.coop = CoopRules_Settings();
    const PlayerResources* p2 = Player2_Resources();
    msg.p2Lives = p2->lives;
    msg.p2Bombs = p2->bombs;
    msg.p2Power = p2->power;
    return msg;
}

void EnterBarrier() {
    // The stage-start frame ticked P1 (and possibly P2) with each machine's
    // own unsynchronized input. Undo that tick so both machines start from
    // the same freshly-initialized players.
    memcpy(Game::Player1(), g_p1AtInit.data(), Game::kPlayerStructSize);
    if (Player2_IsActive()) memcpy(Player2_Struct(), g_p2AtInit.data(), Game::kPlayerStructSize);
    *Game::At<uint32_t>(Game::kInputCurrent) = 0;
    *Game::At<uint32_t>(Game::kInputPrevious) = 0;

    g_barrierFrames = 0;
    g_readyResendTimer = 0;
    Snapshot_BeginCalibration();
    SetState(State::Barrier, "stage started");
}

void StartRunning() {
    ReadyMsg local = MakeReady();
    const ReadyMsg& peer = g_peerReady;
    g_peerReadyValid = false;

    bool match = peer.buildStamp == local.buildStamp && peer.character == local.character &&
                 peer.shotType == local.shotType && peer.p2Character == local.p2Character &&
                 peer.p2ShotType == local.p2ShotType && peer.difficulty == local.difficulty &&
                 peer.stage == local.stage && CoopSettings_Equal(peer.coop, local.coop);
    if (!match) {
        ModLog("Netplay: MISMATCH -- local P1=%d%c P2=%d%c diff=%d stage=%d build=%08X, peer P1=%d%c P2=%d%c diff=%d stage=%d build=%08X."
               " Both players must start the same game mode and stage. Playing this stage locally.",
               local.character, 'A' + local.shotType, local.p2Character, 'A' + local.p2ShotType,
               local.difficulty, local.stage, local.buildStamp,
               peer.character, 'A' + peer.shotType, peer.p2Character, 'A' + peer.p2ShotType,
               peer.difficulty, peer.stage, peer.buildStamp);
        Snapshot_EndCalibration();
        SetState(State::Degraded, "the two games started differently");
        return;
    }

    if (!IsHost()) {
        *Game::At<uint8_t>(Game::kLives) = peer.lives;
        *Game::At<uint8_t>(Game::kBombs) = peer.bombs;
        *Game::At<uint32_t>(Game::kPower) = peer.power;
        *Game::At<uint32_t>(Game::kScore) = peer.score;
        *Game::At<int32_t>(Game::kGraze) = peer.graze;
        // The scene init's stage-start copy (what respawns refill bombs
        // to) was taken from this machine's own values: redo it.
        *Game::At<uint8_t>(Game::kStageStartLives) = peer.lives;
        *Game::At<uint8_t>(Game::kStageStartBombs) = peer.bombs;
        *Player2_Resources() = { peer.p2Lives, peer.p2Bombs, peer.p2Power, peer.p2Bombs };
    }

    // Both machines seeded the scene init identically (Detour_SceneInit), so
    // the post-init RNG should already agree. If it doesn't, the init itself
    // diverged; adopt the host's value and let the checksums tell the story.
    if (peer.rngSeed != local.rngSeed || peer.rngCounter != local.rngCounter) {
        ModLog("Netplay: post-init RNG differs (local %04X/%u, peer %04X/%u) -- stage init diverged",
               local.rngSeed, local.rngCounter, peer.rngSeed, peer.rngCounter);
        if (!IsHost()) {
            *Game::At<uint16_t>(Game::kRngSeed) = peer.rngSeed;
            *Game::At<uint32_t>(Game::kRngCounter) = peer.rngCounter;
        }
    }
    Snapshot_EndCalibration();
    g_session++;
    ResetRun();
    ModLog("Netplay: session %u starting -- %s, stage %d, seed %04X, mode %s, delay %d",
           g_session, IsHost() ? "host=P1" : "guest=P2", local.stage, peer.rngSeed,
           Cfg().netplayRollback ? "rollback" : "lockstep", Cfg().netplayInputDelay);
    SetState(State::Running, "both players at stage start");
}

void BarrierTick() {
    Snapshot_CalibrationSample();
    g_barrierFrames++;
    if (g_barrierFrames < kCalibrationFrames) return;

    if (--g_readyResendTimer <= 0) {
        ReadyMsg msg = MakeReady();
        Transport_Send(&msg, sizeof(msg), true);
        g_readyResendTimer = 120;
    }
    if (g_peerReadyValid && g_peerReady.header.session == static_cast<uint16_t>(g_session + 1)) {
        StartRunning();
    } else if (g_barrierFrames >= kBarrierTimeoutFrames) {
        // The simulation is frozen while waiting, so the player can't even
        // open the pause menu -- don't hold them forever.
        Snapshot_EndCalibration();
        SetState(State::Degraded, "peer never reached the stage (2 min) -- playing this stage locally");
    } else if (g_barrierFrames % 600 == 0) {
        ModLog("Netplay: still waiting for the peer to reach the stage (%d frames; F10 leaves the lobby)", g_barrierFrames);
    }
}

// ---- messages -------------------------------------------------------------

void OnMessage(const uint8_t* data, size_t size) {
    if (size < sizeof(MsgHeader)) return;
    MsgHeader header;
    memcpy(&header, data, sizeof(header));
    if (header.version != kProtocolVersion) {
        static bool warned = false;
        if (!warned) ModLog("Netplay: peer runs protocol v%d, we run v%d -- ignoring it", header.version, kProtocolVersion);
        warned = true;
        return;
    }
    switch (header.type) {
        case kMsgReady:
            if (size == sizeof(ReadyMsg)) {
                memcpy(&g_peerReady, data, sizeof(ReadyMsg));
                NormalizeBools(g_peerReady.coop);
                g_peerReadyValid = true;
            }
            break;
        case kMsgInputs: {
            if (size < offsetof(InputsMsg, masks)) return;
            InputsMsg msg = {};
            memcpy(&msg, data, size < sizeof(msg) ? size : sizeof(msg));
            if (msg.count > kMaxInputsPerPacket) return;
            if (size < offsetof(InputsMsg, masks) + msg.count * sizeof(uint16_t)) return;
            if (g_state == State::Running && header.session == g_session) OnInputs(msg);
            break;
        }
        case kMsgChecksum:
            if (size == sizeof(ChecksumMsg) && header.session == g_session) {
                ChecksumMsg msg;
                memcpy(&msg, data, sizeof(msg));
                ChecksumEntry* entry = SumSlot(g_remoteSums, msg.frame);
                entry->frame = msg.frame;
                memcpy(entry->sum.region, msg.region, sizeof(msg.region));
                CompareSums(msg.frame);
            }
            break;
        case kMsgLoadout:
            if (size == sizeof(LoadoutMsg)) {
                LoadoutMsg msg;
                memcpy(&msg, data, sizeof(msg));
                Selection s{ msg.character, msg.shotType, msg.difficulty };
                if (!g_peerSelectionValid || memcmp(&s, &g_peerSelection, sizeof(s)) != 0) {
                    ModLog("Netplay: partner selected %s%c, difficulty %d", s.character ? "Marisa" : "Reimu",
                           'A' + s.shotType, s.difficulty);
                }
                g_peerSelection = s;
                g_peerSelectionValid = true;
                g_partnerColor = msg.color & 0xFFFFFF;
                g_partnerColorValid = true;
                NormalizeBools(msg.coop);
                if (!IsHost() && !CoopSettings_Equal(CoopRules_Settings(), msg.coop)) {
                    const CoopSettings& host = msg.coop;
                    ModLog("Netplay: using the host's co-op settings (boss HP x%.2f, invincible %d, targeting %d,"
                           " shared resources %d, revive %d s, start lives %d/%d, bombs %d/%d)",
                           host.bossHpMultiplier, host.invincible ? 1 : 0, host.targeting,
                           host.sharedResources ? 1 : 0, host.reviveSeconds, host.startLives[0],
                           host.startLives[1], host.startBombs[0], host.startBombs[1]);
                    CoopRules_SetSettings(host);
                }
            }
            break;
        default:
            break;
    }
}

// In the menus, keep the partner told what this player has selected.
void SendSelection() {
    if (--g_selectionSendTimer > 0) return;
    g_selectionSendTimer = 10;
    Selection s = ReadSelection();
    LoadoutMsg msg = {};
    msg.header = Header(kMsgLoadout, g_session);
    msg.character = s.character;
    msg.shotType = s.shotType;
    msg.difficulty = s.difficulty;
    msg.coop = CoopRules_Settings();
    msg.color = PlayerLook_Settings().color;
    Transport_Send(&msg, sizeof(msg), false);
}

// ---- per-frame driver -----------------------------------------------------

void UpdateConnection() {
    static bool s_initAttempted = false;
    if (!s_initAttempted) {
        s_initAttempted = true;
        g_networkReady = Transport_Init();
        if (g_networkReady) Lobby_Init();
    }
    if (!g_networkReady) return;
    PollHotkeys();
    bool connected = Transport_UpdatePeer();
    if (connected == g_connected) return;
    g_connected = connected;
    if (!connected) g_partnerColorValid = false;
    UpdateInputProvider();
    if (connected) {
        g_session = 0; // both sides count sessions from this connection
        int32_t* useFma3 = Game::At<int32_t>(Game::kCrtUseFma3);
        if (*useFma3 != 0) {
            ModLog("Netplay: switching CRT math from FMA3 to SSE2 paths (identical results on every CPU)");
            *useFma3 = 0;
        }
        if (g_state == State::Offline) SetState(State::Idle, "peer connected");
    } else {
        // Not mid-stage: resources and downed state were set up under the
        // host's rules. The stage teardown restores them instead.
        if (g_state == State::Offline || g_state == State::Idle) CoopRules_RestoreOwnSettings();
        g_peerSelectionValid = false;
        if (g_state == State::Barrier || g_state == State::Running) {
            SetState(State::Degraded, "peer left the lobby");
        } else if (g_state == State::Idle) {
            SetState(State::Offline, "peer left the lobby");
        }
    }
}

// Peer gone mid-stage: keep each player's own character under local control
// (the host keeps P1, the guest keeps P2) and idle the other.
uint64_t DegradedTick(uint32_t* outCode) {
    uint32_t local = SimControl_PollLocalDevices() & kNetplayInputMask;
    return IsHost() ? SimControl_StepForced(outCode, local, 0, false)
                    : SimControl_StepForced(outCode, 0, local, false);
}

uint64_t RunningTick(uint32_t* outCode) {
    if (Transport_MsSinceLastReceive() > kDisconnectMs) {
        LogStats("peer silent");
        SetState(State::Degraded, "no packets from the peer for 5 s");
        return DegradedTick(outCode);
    }

    if (Cfg().netplayRollback && g_rollbackTarget >= 0) DoRollback();
    SendConfirmedSums();

    RecordLocalInputs();
    bool step = CanStep(g_frame) && !ShouldStallForTimeSync();
    SendInputs();
    if (!step) {
        g_stats.stalls++;
        return 0;
    }

    uint64_t result = StepFrame(g_frame, outCode, false);
    g_frame++;
    if (g_frame % kStatsInterval == 0) LogStats("periodic");
    return result;
}

// ---- single-machine rollback self-test --------------------------------------

void LogSyncTestStats(const char* when) {
    ModLog("SyncTest stats (%s): frame=%d checks=%d failures=%d skipped=%d",
           when, g_frame, g_syncTestStats.checks, g_syncTestStats.failures, g_syncTestStats.skipped);
}

void SyncTestCalibrateTick() {
    Snapshot_CalibrationSample();
    if (++g_barrierFrames < kCalibrationFrames) return;
    Snapshot_EndCalibration();
    Snapshot_Clear();
    g_frame = 0;
    g_syncTestStats = SyncTestStats();
    SetState(State::SyncTest, "calibrated");
}

// Rolls back `distance` frames, re-simulates them with the recorded inputs
// and checks the result is byte-identical to what was just computed. A
// mismatch means some simulation state isn't captured by snapshots (heap
// state, a missed region) or isn't deterministic; the logged addresses say
// where.
void RunSyncCheck() {
    int from = g_frame - Cfg().syncTestDistance;
    if (!Snapshot_Has(from) || g_listSignature[from % kRing] != Chain_Signature()) {
        g_syncTestStats.skipped++;
        return;
    }
    Snapshot_Save(g_frame);
    GameChecksum reference = Checksum_Compute(Player2_IsActive() ? Player2_Struct() : nullptr);

    Snapshot_Load(from);
    for (int f = from; f < g_frame; f++) {
        uint32_t code = 0;
        Snapshot_Save(f);
        const RecordedInputs& in = g_syncTestInputs[f % kRing];
        SimControl_StepForced(&code, in.p1, in.p2, true);
    }

    g_syncTestStats.checks++;
    size_t differing = Snapshot_CompareLive(g_frame, g_syncTestStats.failures < 5 ? 12 : 0);
    if (differing == 0) {
        if (g_syncTestStats.checks == 1 || g_syncTestStats.checks % 100 == 0) {
            ModLog("SyncTest: frame %d OK (%d checks so far)", g_frame, g_syncTestStats.checks);
        }
        return;
    }
    g_syncTestStats.failures++;
    GameChecksum after = Checksum_Compute(Player2_IsActive() ? Player2_Struct() : nullptr);
    char regions[256] = {};
    for (int r = 0; r < kChecksumRegionCount; r++) {
        if (reference.region[r] != after.region[r]) {
            strcat_s(regions, Checksum_RegionName(r));
            strcat_s(regions, " ");
        }
    }
    ModLog("SyncTest: FAILED at frame %d -- %zu bytes differ after re-simulating %d-%d; gameplay regions affected: %s",
           g_frame, differing, from, g_frame - 1, regions[0] ? regions : "(none -- only non-checksummed state)");
}

uint64_t SyncTestTick(uint32_t* outCode) {
    Snapshot_LearnDrawChanges();
    uint32_t p1 = SimControl_PollLocalDevices();
    uint32_t p2 = LocalInput_PollPlayer2();
    g_syncTestInputs[g_frame % kRing] = { p1, p2 };
    g_listSignature[g_frame % kRing] = Chain_Signature();
    Snapshot_Save(g_frame);
    uint64_t result = SimControl_StepForced(outCode, p1, p2, false);
    g_frame++;
    if (g_frame > Cfg().syncTestDistance && g_frame % Cfg().syncTestInterval == 0) RunSyncCheck();
    if (g_frame % kStatsInterval == 0) LogSyncTestStats("periodic");
    Snapshot_ArmDrawLearning();
    return result;
}

uint64_t Driver(uint32_t* outCode) {
    UpdateConnection();
    if (g_networkReady) Transport_Receive(&OnMessage);

    uint64_t result = 0;
    switch (g_state) {
        case State::Offline:
            result = SimControl_StepNative(outCode);
            break;
        case State::Idle:
            SendSelection();
            result = SimControl_StepNative(outCode);
            break;
        case State::Barrier:
            BarrierTick();
            return 0;
        case State::Running:
            result = RunningTick(outCode);
            break;
        case State::Degraded:
            result = DegradedTick(outCode);
            break;
        case State::SyncTestCalibrate:
            SyncTestCalibrateTick();
            return 0;
        case State::SyncTest:
            result = SyncTestTick(outCode);
            break;
    }

    if (g_pendingBarrier) {
        g_pendingBarrier = false;
        if (g_connected) EnterBarrier();
    }
    if (g_pendingSyncTest) {
        g_pendingSyncTest = false;
        if (!g_connected) {
            g_barrierFrames = 0;
            Snapshot_BeginCalibration();
            SetState(State::SyncTestCalibrate, "stage started with [synctest] enabled");
        }
    }
    return result;
}

// ---- stage start ----------------------------------------------------------

// Online, P1 is the host's selection and P2 the guest's on both machines,
// at the host's difficulty. The player init reads the selection globals, so
// the guest's game takes on the host's selection before the scene init runs
// and gets its own back when the stage ends.
void ApplySelectionsForStage() {
    if (!g_connected) {
        Player2_SetLoadout(Cfg().player2Character, Cfg().player2ShotType);
        return;
    }
    if (IsHost()) {
        if (g_peerSelectionValid) {
            Player2_SetLoadout(g_peerSelection.character, g_peerSelection.shotType);
        } else {
            Player2_SetLoadout(-1, -1);
        }
        return;
    }
    if (!g_ownSelectionOverridden) {
        g_ownSelection = ReadSelection();
        g_ownSelectionOverridden = true;
    }
    if (g_peerSelectionValid) WriteSelection(g_peerSelection);
    Player2_SetLoadout(g_ownSelection.character, g_ownSelection.shotType);
}

void RestoreOwnSelection() {
    if (!g_ownSelectionOverridden) return;
    WriteSelection(g_ownSelection);
    g_ownSelectionOverridden = false;
}

// The gameplay scene init consumes RNG before it registers the players, and
// the two machines arrive with different RNG states (menus use it too). Seed
// it identically on both, from values both sides know without a message:
// the two SteamIDs and the number of the session about to start.
uint64_t Detour_SceneInit(void* scene) {
    if (*Game::At<uint8_t>(Game::kReplayFlag)) {
        // A replay plays back alone, from its own recorded seed.
        return g_origSceneInit(scene);
    }
    ApplySelectionsForStage();
    CoopRules_OnSceneInit(g_connected || Player2_Enabled());
    if (g_connected) {
        uint64_t a = Transport_LocalId();
        uint64_t b = Transport_PeerId();
        uint64_t lo = a < b ? a : b;
        uint64_t hi = a < b ? b : a;
        uint64_t hash = 0xCBF29CE484222325ull;
        uint64_t values[] = { lo, hi, static_cast<uint64_t>(g_session + 1) };
        for (uint64_t v : values) {
            for (int i = 0; i < 8; i++) {
                hash = (hash ^ ((v >> (i * 8)) & 0xFF)) * 0x100000001B3ull;
            }
        }
        uint16_t seed = static_cast<uint16_t>(hash ^ (hash >> 16) ^ (hash >> 32) ^ (hash >> 48));
        *Game::At<uint16_t>(Game::kRngSeed) = seed;
        *Game::At<uint32_t>(Game::kRngCounter) = 0;
        ModLog("Netplay: gameplay scene init, stage seed %04X for session %u", seed, g_session + 1);
    }
    return g_origSceneInit(scene);
}

// ---- Player 2 lifecycle ---------------------------------------------------

bool ForceSpawnPlayer2() {
    return g_connected;
}

void OnPlayer2Spawned() {
    if (!g_connected) {
        if (Cfg().syncTest) g_pendingSyncTest = true;
        return;
    }
    g_p1AtInit.assign(Game::Player1(), Game::Player1() + Game::kPlayerStructSize);
    g_p2AtInit.assign(Player2_Struct(), Player2_Struct() + Game::kPlayerStructSize);
    g_pendingBarrier = true;
}

void OnPlayer2Despawned() {
    g_pendingBarrier = false;
    g_pendingSyncTest = false;
    if (g_state == State::Running) LogStats("stage end");
    if (g_state == State::SyncTest) LogSyncTestStats("stage end");
    if (g_state == State::Barrier || g_state == State::SyncTestCalibrate) Snapshot_EndCalibration();
    if (g_state != State::Offline && g_state != State::Idle) {
        SetState(g_connected ? State::Idle : State::Offline, "stage torn down");
    }
    if (!g_connected) CoopRules_RestoreOwnSettings();
    RestoreOwnSelection();
}

} // namespace

void Netplay_StatusText(char* out, int outSize, float* r, float* g, float* b) {
    out[0] = '\0';
    auto set = [&](float rr, float gg, float bb) { *r = rr; *g = gg; *b = bb; };
    const char* role = IsHost() ? "P1 (HOST)" : "P2 (GUEST)";
    switch (g_state) {
        case State::Offline:
            if (Lobby_GetStatus() == LobbyStatus::Creating) {
                sprintf_s(out, outSize, "CO-OP: OPENING LOBBY...");
                set(0.9f, 0.6f, 0.1f);
            } else if (Lobby_GetStatus() == LobbyStatus::WaitingForPeer) {
                sprintf_s(out, outSize, "CO-OP: WAITING FOR PARTNER TO JOIN (F10 CLOSES)");
                set(0.9f, 0.9f, 0.1f);
            }
            break;
        case State::Idle:
            sprintf_s(out, outSize, "CO-OP: CONNECTED - YOU ARE %s", role);
            set(0.1f, 0.85f, 0.2f);
            break;
        case State::Barrier:
            sprintf_s(out, outSize, "CO-OP: WAITING FOR PARTNER TO REACH THE STAGE...");
            set(0.9f, 0.9f, 0.1f);
            break;
        case State::Running:
            if (g_firstDesyncFrame >= 0) {
                sprintf_s(out, outSize, "CO-OP: DESYNC AT FRAME %d - SEE LOG", g_firstDesyncFrame);
                set(0.95f, 0.2f, 0.2f);
            } else {
                sprintf_s(out, outSize, "CO-OP %s  %s  DELAY %d", role,
                          Cfg().netplayRollback ? "ROLLBACK" : "LOCKSTEP", Cfg().netplayInputDelay);
                set(0.1f, 0.85f, 0.2f);
            }
            break;
        case State::Degraded:
            sprintf_s(out, outSize, "CO-OP: PLAYING LOCALLY - %s", g_stateReason);
            set(0.95f, 0.2f, 0.2f);
            break;
        case State::SyncTestCalibrate:
            sprintf_s(out, outSize, "SYNCTEST: CALIBRATING...");
            set(0.9f, 0.9f, 0.1f);
            break;
        case State::SyncTest:
            sprintf_s(out, outSize, "SYNCTEST: %d CHECKS, %d FAILED, %d SKIPPED", g_syncTestStats.checks,
                      g_syncTestStats.failures, g_syncTestStats.skipped);
            if (g_syncTestStats.failures > 0) set(0.95f, 0.2f, 0.2f); else set(0.1f, 0.85f, 0.2f);
            break;
    }
    // Separate resources: the game's HUD only shows P1's pool (docs/06).
    if (Player2_IsActive() && !CoopRules_Settings().sharedResources) {
        if (out[0] == '\0') set(0.8f, 0.8f, 0.9f);
        const PlayerResources* p2 = Player2_Resources();
        size_t len = strlen(out);
        // The HUD shows the lives byte plus one (the life in play).
        sprintf_s(out + len, outSize - len, "%sP2  LIVES %d  BOMBS %d  POWER %u/128", len ? "\n" : "",
                  p2->lives + 1, p2->bombs, p2->power);
    }
    int reviveFrames = CoopRules_ReviveFramesLeft();
    if (reviveFrames >= 0) {
        size_t len = strlen(out);
        sprintf_s(out + len, outSize - len, "%sPARTNER DOWN - REVIVE IN %d", len ? "\n" : "", (reviveFrames + 59) / 60);
    }
    for (char* c = out; *c; c++) {
        if (*c >= 'a' && *c <= 'z') *c = static_cast<char>(*c - 'a' + 'A');
    }
}

int Netplay_LocalPlayerIndex() {
    if (!g_connected && (g_state == State::Offline || g_state == State::Idle ||
                         g_state == State::SyncTestCalibrate || g_state == State::SyncTest)) {
        return -1;
    }
    return IsHost() ? 0 : 1;
}

bool Netplay_PartnerColor(uint32_t* rgb) {
    if (!g_partnerColorValid || Netplay_LocalPlayerIndex() < 0) return false;
    *rgb = g_partnerColor;
    return true;
}

bool Netplay_Install() {
    const Config& c = Config_Get();
    if (!Snapshot_Init(c.netplayMaxRollback + 2)) return false;
    if (!Hooks_Install(Game::kFnGameplaySceneInit, reinterpret_cast<void*>(&Detour_SceneInit),
                       reinterpret_cast<void**>(&g_origSceneInit), "GameplaySceneInit")) {
        return false;
    }
    size_t p2Size = 0;
    void* p2State = Player2_StateRegion(&p2Size);
    Snapshot_AddRegion(p2State, p2Size);
    size_t rulesSize = 0;
    void* rulesState = CoopRules_StateRegion(&rulesSize);
    Snapshot_AddRegion(rulesState, rulesSize);
    Snapshot_AddIndirectRegion(Game::kGuiObjectPtr, Game::kGuiObjectSize);

    Player2Listener listener;
    listener.forceSpawn = &ForceSpawnPlayer2;
    listener.onSpawned = &OnPlayer2Spawned;
    listener.onDespawned = &OnPlayer2Despawned;
    Player2_SetListener(listener);

    SimControl_SetDriver(&Driver);
    ModLog("Netplay: installed (F9 = host a lobby and invite, F10 = leave)");
    return true;
}
