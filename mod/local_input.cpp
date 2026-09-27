#include "local_input.h"
#include "config.h"
#include "game.h"

#include <windows.h>
#include <Xinput.h>

namespace {

using XInputGetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);

XInputGetStateFn ResolveXInput() {
    static XInputGetStateFn fn = []() -> XInputGetStateFn {
        const char* dlls[] = { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll" };
        for (const char* name : dlls) {
            HMODULE mod = LoadLibraryA(name);
            if (!mod) continue;
            auto proc = reinterpret_cast<XInputGetStateFn>(GetProcAddress(mod, "XInputGetState"));
            if (proc) return proc;
        }
        return nullptr;
    }();
    return fn;
}

bool GameWindowFocused() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

bool KeyDown(int vk) {
    return vk > 0 && (GetAsyncKeyState(vk) & 0x8000) != 0;
}

uint32_t PollPad(int index) {
    XInputGetStateFn getState = ResolveXInput();
    if (!getState || index < 0 || index > 3) return 0;
    XINPUT_STATE state = {};
    if (getState(static_cast<DWORD>(index), &state) != ERROR_SUCCESS) return 0;

    const XINPUT_GAMEPAD& pad = state.Gamepad;
    const SHORT deadzone = 16000;
    uint32_t mask = 0;
    if ((pad.wButtons & XINPUT_GAMEPAD_DPAD_UP) || pad.sThumbLY > deadzone) mask |= Game::kButtonUp;
    if ((pad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) || pad.sThumbLY < -deadzone) mask |= Game::kButtonDown;
    if ((pad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) || pad.sThumbLX < -deadzone) mask |= Game::kButtonLeft;
    if ((pad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) || pad.sThumbLX > deadzone) mask |= Game::kButtonRight;
    if (pad.wButtons & XINPUT_GAMEPAD_A) mask |= Game::kButtonShoot;
    if (pad.wButtons & XINPUT_GAMEPAD_B) mask |= Game::kButtonBomb;
    if (pad.wButtons & (XINPUT_GAMEPAD_X | XINPUT_GAMEPAD_RIGHT_SHOULDER | XINPUT_GAMEPAD_LEFT_SHOULDER)) {
        mask |= Game::kButtonFocus;
    }
    return mask;
}

} // namespace

uint32_t LocalInput_PollPlayer2() {
    const Config& c = Config_Get();
    uint32_t mask = PollPad(c.p2PadIndex);
    if (GameWindowFocused()) {
        if (KeyDown(c.p2KeyUp)) mask |= Game::kButtonUp;
        if (KeyDown(c.p2KeyDown)) mask |= Game::kButtonDown;
        if (KeyDown(c.p2KeyLeft)) mask |= Game::kButtonLeft;
        if (KeyDown(c.p2KeyRight)) mask |= Game::kButtonRight;
        if (KeyDown(c.p2KeyShoot)) mask |= Game::kButtonShoot;
        if (KeyDown(c.p2KeyBomb)) mask |= Game::kButtonBomb;
        if (KeyDown(c.p2KeyFocus)) mask |= Game::kButtonFocus;
    }
    return mask;
}
