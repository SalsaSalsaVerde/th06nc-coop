#pragma once
#include <d3d11.h>

// Draws simple flat-colored quads directly into the game's own back buffer,
// right before it gets presented to the screen. Positions/sizes are given
// as fractions of the client area (0..1), not raw pixels, so this works
// regardless of actual window/back-buffer resolution.
struct OverlayQuad {
    float centerXFrac;
    float centerYFrac;
    float halfWidthFrac;
    float halfHeightFrac;
    float r, g, b, a;
};

// A loaded sprite texture, created once (e.g. at startup) and reused every
// frame via DrawSprite. Opaque to callers -- just pass the handle back.
struct SpriteTexture {
    ID3D11ShaderResourceView* srv = nullptr;
};

struct OverlaySprite {
    SpriteTexture texture;
    float centerXFrac;
    float centerYFrac;
    float halfWidthFrac;
    float halfHeightFrac;
    float alpha; // uniform alpha multiplier
    // UV sub-rect within the bound texture, 0..1. Defaults to the whole
    // texture (0,0)-(1,1) -- set narrower to draw one frame out of a
    // multi-frame sprite sheet (see docs/23-sprite-animation.md).
    float uMin = 0.0f, vMin = 0.0f, uMax = 1.0f, vMax = 1.0f;
    // Multiplied into the texture's color (1 = as is). The font sheet's
    // glyphs are white, so this colors text.
    float r = 1.0f, g = 1.0f, b = 1.0f;
};

class OverlayRenderer {
public:
    ~OverlayRenderer();

    // Call once per Present call, before any DrawQuad/DrawSprite calls.
    // Lazily creates device-dependent resources on first call, and
    // recreates the size-dependent ones (render target view) if the back
    // buffer size changed since last frame (e.g. window resize).
    void EnsureInitialized(IDXGISwapChain* swapChain);

    void BeginFrame();
    // Quads and sprites draw in call order: quads are batched, and the batch
    // is flushed before every sprite and at EndFrame (docs/11).
    void DrawQuad(const OverlayQuad& quad);
    void DrawSprite(const OverlaySprite& sprite);
    void EndFrame();

    // Creates a texture from raw RGBA8 pixel data (top-to-bottom row
    // order, 4 bytes/pixel, straight alpha). Must be called after
    // EnsureInitialized has run at least once (needs a live device).
    // Returns a texture with a null SRV on failure.
    SpriteTexture CreateSpriteTexture(unsigned int width, unsigned int height, const unsigned char* rgba);

    // Same, from data already in a GPU format (a DDS payload: BC7, BC3, ...),
    // with the given bytes per row (per row of 4x4 blocks for BCn).
    SpriteTexture CreateTextureFromData(unsigned int width, unsigned int height, DXGI_FORMAT format,
                                        const void* data, unsigned int rowPitch);

private:
    bool CreateDeviceResources();
    void FlushQuads();
    bool CreateSizeDependentResources(IDXGISwapChain* swapChain);
    void ReleaseSizeDependentResources();
    void ReleaseDeviceResources();

    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;
    ID3D11RenderTargetView* m_rtv = nullptr;
    ID3D11VertexShader* m_vs = nullptr;
    ID3D11PixelShader* m_ps = nullptr;
    ID3D11InputLayout* m_inputLayout = nullptr;
    ID3D11Buffer* m_vertexBuffer = nullptr;
    ID3D11BlendState* m_blendState = nullptr;
    ID3D11DepthStencilState* m_depthStencilState = nullptr;
    ID3D11RasterizerState* m_rasterizerState = nullptr;

    // Sprite (textured-quad) pipeline -- separate shaders/input layout/
    // vertex buffer from the flat-color quad pipeline above, since the
    // vertex format (UV instead of per-vertex color) and shaders differ.
    // Drawn as its own batch, after all DrawQuad calls' batch, since
    // switching shaders/textures mid-batch isn't free and sprite count
    // per frame is small.
    ID3D11VertexShader* m_spriteVs = nullptr;
    ID3D11PixelShader* m_spritePs = nullptr;
    ID3D11InputLayout* m_spriteInputLayout = nullptr;
    ID3D11Buffer* m_spriteVertexBuffer = nullptr;
    ID3D11Buffer* m_spriteParamsBuffer = nullptr; // cbuffer: alphaMultiplier
    ID3D11SamplerState* m_spriteSampler = nullptr;

    UINT m_backBufferWidth = 0;
    UINT m_backBufferHeight = 0;
    bool m_deviceResourcesReady = false;
    bool m_sizeResourcesReady = false;

    struct Vertex {
        float x, y;
        float r, g, b, a;
    };
    static const UINT kMaxQuadsPerFrame = 16;
    static const UINT kMaxVertices = kMaxQuadsPerFrame * 6;
    Vertex m_vertexScratch[kMaxVertices];
    UINT m_vertexCount = 0;
    bool m_formatLogged = false;

    struct SpriteVertex {
        float x, y;
        float u, v;
    };
    // Sprites are drawn one texture/alpha at a time (each is its own
    // Map+Draw call, immediately, rather than batched into a scratch
    // array like DrawQuad) since each can have a different texture bound
    // -- simplest correct approach given the current small per-frame count.

    // Saved pipeline state, restored in EndFrame so we don't disturb
    // whatever the game itself expects to be bound going into its next
    // frame (it very likely rebinds its own state before drawing anyway,
    // but restoring costs little and avoids a class of subtle bugs).
    ID3D11RenderTargetView* m_savedRtv = nullptr;
    ID3D11DepthStencilView* m_savedDsv = nullptr;
    D3D11_VIEWPORT m_savedViewport = {};
};
