// DLL-proxy entry point for th06nc.exe (overlay/13): built as steam_api64.dll,
// forwarding every real export to the renamed steam_api64_orig.dll via
// steam_api64_proxy.def. DllMain only spawns a thread; hook installation
// suspends threads, which isn't safe under the loader lock.
#include <windows.h>

#include "config.h"
#include "coop_rules.h"
#include "hooks.h"
#include "mod_log.h"
#include "netplay.h"
#include "player2.h"
#include "player_look.h"
#include "settings_panel.h"
#include "sim_control.h"
#include "status_overlay.h"

namespace {

void DrawOverlays(OverlayRenderer& overlay) {
    PlayerLook_DrawOverlay(overlay);
    SettingsPanel_Draw(overlay);
}

void Initialize() {
    Config_Load();
    if (!Fingerprint_Check()) return;
    if (!Player2_Install()) {
        ModLog("Init: Player 2 hooks incomplete -- see failures above");
        return;
    }
    Player2_SetEnabled(Config_Get().player2Enabled);
    Player2_SetLoadout(Config_Get().player2Character, Config_Get().player2ShotType);
    CoopRules_SetOwnSettings(Config_Get().coop);
    if (!CoopRules_Install()) {
        ModLog("Init: co-op rules hook failed -- boss HP won't be scaled");
    }
    PlayerLook_SetSettings(Config_Get().look);
    if (!PlayerLook_Install()) {
        ModLog("Init: player draw hooks failed -- no colors/fade, and a downed player stays drawn");
    }
    if (!SimControl_Install() || !Netplay_Install()) {
        ModLog("Init: netplay unavailable -- see failures above (local Player 2 still works)");
        return;
    }
    StatusOverlay_SetProvider(&Netplay_StatusText);
    StatusOverlay_SetWorldDrawer(&DrawOverlays);
    StatusOverlay_Install();
}

DWORD WINAPI InitThread(LPVOID) {
    __try {
        Initialize();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ModLog("InitThread: CRASHED with exception code 0x%08X", GetExceptionCode());
    }
    return 0;
}

} // namespace

BOOL APIENTRY DllMain(HINSTANCE hinstDLL, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);
        char modulePath[MAX_PATH] = {};
        GetModuleFileNameA(hinstDLL, modulePath, MAX_PATH);
        ModLog("native co-op proxy loaded (%s)", modulePath);
        HANDLE thread = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
    }
    return TRUE;
}
