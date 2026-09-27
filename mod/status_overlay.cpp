#include "status_overlay.h"
#include "mod_log.h"
#include "overlay_renderer.h"
#include "text_renderer.h"

#include <windows.h>
#include <d3d11.h>
#include <cstring>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace {

using PresentFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);

PresentFn g_origPresent = nullptr;
OverlayRenderer* g_overlay = nullptr;
StatusTextProvider g_provider = nullptr;
OverlayWorldDrawer g_worldDrawer = nullptr;
int g_recursionDepth = 0;

void Render(IDXGISwapChain* swapChain) {
    char text[320] = {};
    float r = 1.0f, g = 1.0f, b = 1.0f;
    if (g_provider) g_provider(text, sizeof(text), &r, &g, &b);

    if (!g_overlay) g_overlay = new OverlayRenderer();
    g_overlay->EnsureInitialized(swapChain);
    g_overlay->BeginFrame();
    if (g_worldDrawer) g_worldDrawer(*g_overlay);
    if (text[0] == '\0') {
        g_overlay->EndFrame();
        return;
    }
    OverlayQuad light = { 0.02f, 0.03f, 0.010f, 0.010f, r, g, b, 0.9f };
    g_overlay->DrawQuad(light);
    TextRenderer_EnsureLoaded(*g_overlay);
    float y = 0.022f;
    for (char* line = text; line && *line;) {
        char* next = strchr(line, '\n');
        if (next) *next++ = '\0';
        DrawText(*g_overlay, line, 0.04f, y, 0.011f, 0.017f, 0.9f);
        y += 0.022f;
        line = next;
    }
    g_overlay->EndFrame();
}

// In the overlay mod, a throwaway device created while the game was still
// creating its own made Present recurse into the hook (overlay/14).
// WaitForGameWindow avoids that; the depth guard is the safety net.
HRESULT __stdcall Detour_Present(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags) {
    if (++g_recursionDepth > 3) {
        g_recursionDepth--;
        return S_OK;
    }
    __try {
        Render(swapChain);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static bool logged = false;
        if (!logged) ModLog("StatusOverlay: drawing crashed (0x%08X), skipping", GetExceptionCode());
        logged = true;
    }
    HRESULT hr = g_origPresent(swapChain, syncInterval, flags);
    g_recursionDepth--;
    return hr;
}

LRESULT CALLBACK DummyWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void** GetSwapChainVTable() {
    WNDCLASSEXA wc = { sizeof(WNDCLASSEXA) };
    wc.lpfnWndProc = DummyWndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = "th06nc_native_coop_dummy";
    RegisterClassExA(&wc);
    HWND hwnd = CreateWindowExA(0, wc.lpszClassName, "", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100,
                                nullptr, nullptr, wc.hInstance, nullptr);

    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount = 1;
    scd.BufferDesc.Width = 100;
    scd.BufferDesc.Height = 100;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferDesc.RefreshRate.Numerator = 60;
    scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;

    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
    IDXGISwapChain* swapChain = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 1,
                                               D3D11_SDK_VERSION, &scd, &swapChain, &device, nullptr, &context);
    void** vtable = (SUCCEEDED(hr) && swapChain) ? *reinterpret_cast<void***>(swapChain) : nullptr;
    if (swapChain) swapChain->Release();
    if (context) context->Release();
    if (device) device->Release();
    DestroyWindow(hwnd);
    UnregisterClassA(wc.lpszClassName, wc.hInstance);
    return vtable;
}

struct FindWindowState {
    DWORD pid;
    HWND found;
};

BOOL CALLBACK FindOwnWindow(HWND hwnd, LPARAM param) {
    auto* state = reinterpret_cast<FindWindowState*>(param);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == state->pid && IsWindowVisible(hwnd)) {
        state->found = hwnd;
        return FALSE;
    }
    return TRUE;
}

void WaitForGameWindow() {
    FindWindowState state = { GetCurrentProcessId(), nullptr };
    for (int waited = 0; waited < 15000; waited += 200) {
        EnumWindows(FindOwnWindow, reinterpret_cast<LPARAM>(&state));
        if (state.found) {
            Sleep(1000); // let the game finish creating its device, not just the window
            return;
        }
        Sleep(200);
    }
}

} // namespace

void StatusOverlay_SetProvider(StatusTextProvider provider) {
    g_provider = provider;
}

void StatusOverlay_SetWorldDrawer(OverlayWorldDrawer drawer) {
    g_worldDrawer = drawer;
}

void StatusOverlay_Install() {
    WaitForGameWindow();
    void** vtable = GetSwapChainVTable();
    if (!vtable) {
        ModLog("StatusOverlay: couldn't get the swap chain vtable -- no on-screen status");
        return;
    }
    DWORD oldProtect = 0;
    if (!VirtualProtect(&vtable[8], sizeof(void*), PAGE_READWRITE, &oldProtect)) {
        ModLog("StatusOverlay: VirtualProtect failed (%lu)", GetLastError());
        return;
    }
    g_origPresent = reinterpret_cast<PresentFn>(vtable[8]);
    vtable[8] = reinterpret_cast<void*>(&Detour_Present);
    DWORD ignored = 0;
    VirtualProtect(&vtable[8], sizeof(void*), oldProtect, &ignored);
    ModLog("StatusOverlay: Present hooked");
}
