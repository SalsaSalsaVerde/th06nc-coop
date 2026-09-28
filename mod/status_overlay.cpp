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
StatusTextProvider g_cornerProvider = nullptr;
OverlayWorldDrawer g_worldDrawer = nullptr;
int g_recursionDepth = 0;

// The playfield starts about 27% of the way across the window (overlay/14:
// x 400/1456), and the boss's HP bar runs along its top, so both text
// blocks stay inside the left margin: small glyphs, lines wrapped to fit.
const float kCharW = 11.0f / 1456.0f;
const float kCharH = 11.0f / 816.0f;
const float kLineStep = 0.017f;
const int kMaxColumns = 26;
const float kLeft = 0.012f;

// Word-wraps `in` into `out` so no line is longer than kMaxColumns.
void Wrap(const char* in, char* out, size_t outSize, int* lines, size_t* longest) {
    size_t o = 0, col = 0, lastSpaceOut = static_cast<size_t>(-1);
    *lines = 1;
    *longest = 0;
    for (const char* c = in; *c && o + 2 < outSize; c++) {
        if (*c == '\n') {
            out[o++] = '\n';
            (*lines)++;
            col = 0;
            lastSpaceOut = static_cast<size_t>(-1);
            continue;
        }
        if (*c == ' ') lastSpaceOut = o;
        out[o++] = *c;
        col++;
        if (col > static_cast<size_t>(kMaxColumns) && lastSpaceOut != static_cast<size_t>(-1)) {
            out[lastSpaceOut] = '\n';
            (*lines)++;
            col = o - lastSpaceOut - 1;
            lastSpaceOut = static_cast<size_t>(-1);
        }
        if (col > *longest) *longest = col;
    }
    out[o] = '\0';
    // Recount the longest line after wrapping.
    *longest = 0;
    size_t current = 0;
    for (const char* c = out; *c; c++) {
        if (*c == '\n') current = 0;
        else if (++current > *longest) *longest = current;
    }
}

// One text block on a dark backing at (kLeft, top); `bottom` anchors it by
// its lower edge instead. A colored bar marks the block's state.
void DrawBlock(const char* raw, float r, float g, float b, float anchorY, bool bottom) {
    char text[512];
    int lines = 0;
    size_t longest = 0;
    Wrap(raw, text, sizeof(text), &lines, &longest);
    float boxW = 0.016f + kCharW * static_cast<float>(longest) + 0.006f;
    float boxH = 0.008f + kLineStep * static_cast<float>(lines);
    float top = bottom ? anchorY - boxH : anchorY;
    OverlayQuad backing = { kLeft + boxW * 0.5f, top + boxH * 0.5f, boxW * 0.5f, boxH * 0.5f, 0.0f, 0.0f, 0.0f, 0.55f };
    g_overlay->DrawQuad(backing);
    OverlayQuad light = { kLeft + 0.006f, top + 0.004f + kLineStep * 0.5f, 0.003f, kLineStep * 0.4f, r, g, b, 0.95f };
    g_overlay->DrawQuad(light);
    TextRenderer_EnsureLoaded(*g_overlay);
    float y = top + 0.005f;
    for (char* line = text; line && *line;) {
        char* next = strchr(line, '\n');
        if (next) *next++ = '\0';
        DrawText(*g_overlay, line, kLeft + 0.013f, y, kCharW, kCharH, 1.0f);
        y += kLineStep;
        line = next;
    }
}

void Render(IDXGISwapChain* swapChain) {
    char text[320] = {};
    float r = 1.0f, g = 1.0f, b = 1.0f;
    if (g_provider) g_provider(text, sizeof(text), &r, &g, &b);
    char corner[160] = {};
    float cr = 1.0f, cg = 1.0f, cb = 1.0f;
    if (g_cornerProvider) g_cornerProvider(corner, sizeof(corner), &cr, &cg, &cb);

    if (!g_overlay) g_overlay = new OverlayRenderer();
    g_overlay->EnsureInitialized(swapChain);
    g_overlay->BeginFrame();
    TextRenderer_EnsureLoaded(*g_overlay); // up front, so the first message isn't blank
    if (g_worldDrawer) g_worldDrawer(*g_overlay);
    if (text[0] != '\0') DrawBlock(text, r, g, b, 0.012f, false);
    if (corner[0] != '\0') DrawBlock(corner, cr, cg, cb, 0.97f, true);
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

void StatusOverlay_SetCornerProvider(StatusTextProvider provider) {
    g_cornerProvider = provider;
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
