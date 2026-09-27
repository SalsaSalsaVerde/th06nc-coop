#include "overlay_renderer.h"
#include "shaders/quad_vs.h"
#include "shaders/quad_ps.h"
#include "shaders/sprite_vs.h"
#include "shaders/sprite_ps.h"
#include "mod_log.h"

OverlayRenderer::~OverlayRenderer() {
    ReleaseSizeDependentResources();
    ReleaseDeviceResources();
}

void OverlayRenderer::ReleaseDeviceResources() {
    if (m_vertexBuffer) { m_vertexBuffer->Release(); m_vertexBuffer = nullptr; }
    if (m_rasterizerState) { m_rasterizerState->Release(); m_rasterizerState = nullptr; }
    if (m_depthStencilState) { m_depthStencilState->Release(); m_depthStencilState = nullptr; }
    if (m_blendState) { m_blendState->Release(); m_blendState = nullptr; }
    if (m_inputLayout) { m_inputLayout->Release(); m_inputLayout = nullptr; }
    if (m_ps) { m_ps->Release(); m_ps = nullptr; }
    if (m_vs) { m_vs->Release(); m_vs = nullptr; }
    if (m_spriteVertexBuffer) { m_spriteVertexBuffer->Release(); m_spriteVertexBuffer = nullptr; }
    if (m_spriteParamsBuffer) { m_spriteParamsBuffer->Release(); m_spriteParamsBuffer = nullptr; }
    if (m_spriteSampler) { m_spriteSampler->Release(); m_spriteSampler = nullptr; }
    if (m_spriteInputLayout) { m_spriteInputLayout->Release(); m_spriteInputLayout = nullptr; }
    if (m_spritePs) { m_spritePs->Release(); m_spritePs = nullptr; }
    if (m_spriteVs) { m_spriteVs->Release(); m_spriteVs = nullptr; }
    if (m_context) { m_context->Release(); m_context = nullptr; }
    if (m_device) { m_device->Release(); m_device = nullptr; }
    m_deviceResourcesReady = false;
}

void OverlayRenderer::ReleaseSizeDependentResources() {
    if (m_rtv) { m_rtv->Release(); m_rtv = nullptr; }
    m_sizeResourcesReady = false;
}

bool OverlayRenderer::CreateDeviceResources() {
    if (FAILED(m_device->CreateVertexShader(g_quad_vs, g_quad_vs_size, nullptr, &m_vs))) {
        return false;
    }
    if (FAILED(m_device->CreatePixelShader(g_quad_ps, g_quad_ps_size, nullptr, &m_ps))) {
        return false;
    }

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    if (FAILED(m_device->CreateInputLayout(layout, 2, g_quad_vs, g_quad_vs_size, &m_inputLayout))) {
        return false;
    }

    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.Usage = D3D11_USAGE_DYNAMIC;
    vbDesc.ByteWidth = sizeof(Vertex) * kMaxVertices;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(m_device->CreateBuffer(&vbDesc, nullptr, &m_vertexBuffer))) {
        return false;
    }

    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(m_device->CreateBlendState(&blendDesc, &m_blendState))) {
        return false;
    }

    D3D11_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable = FALSE;
    dsDesc.StencilEnable = FALSE;
    if (FAILED(m_device->CreateDepthStencilState(&dsDesc, &m_depthStencilState))) {
        return false;
    }

    D3D11_RASTERIZER_DESC rsDesc = {};
    rsDesc.FillMode = D3D11_FILL_SOLID;
    rsDesc.CullMode = D3D11_CULL_NONE;
    rsDesc.DepthClipEnable = TRUE;
    if (FAILED(m_device->CreateRasterizerState(&rsDesc, &m_rasterizerState))) {
        return false;
    }

    // Sprite (textured-quad) pipeline.
    if (FAILED(m_device->CreateVertexShader(g_sprite_vs, g_sprite_vs_size, nullptr, &m_spriteVs))) {
        return false;
    }
    if (FAILED(m_device->CreatePixelShader(g_sprite_ps, g_sprite_ps_size, nullptr, &m_spritePs))) {
        return false;
    }
    D3D11_INPUT_ELEMENT_DESC spriteLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    if (FAILED(m_device->CreateInputLayout(spriteLayout, 2, g_sprite_vs, g_sprite_vs_size, &m_spriteInputLayout))) {
        return false;
    }
    D3D11_BUFFER_DESC spriteVbDesc = {};
    spriteVbDesc.Usage = D3D11_USAGE_DYNAMIC;
    spriteVbDesc.ByteWidth = sizeof(SpriteVertex) * 6;
    spriteVbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    spriteVbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(m_device->CreateBuffer(&spriteVbDesc, nullptr, &m_spriteVertexBuffer))) {
        return false;
    }
    // Matches shaders/sprite_ps.hlsl's SpriteParams cbuffer (float4 tint:
    // rgb multiplier + alpha multiplier).
    D3D11_BUFFER_DESC paramsDesc = {};
    paramsDesc.Usage = D3D11_USAGE_DYNAMIC;
    paramsDesc.ByteWidth = 16;
    paramsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    paramsDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(m_device->CreateBuffer(&paramsDesc, nullptr, &m_spriteParamsBuffer))) {
        return false;
    }
    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT; // pixel art -- no smoothing
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    if (FAILED(m_device->CreateSamplerState(&sampDesc, &m_spriteSampler))) {
        return false;
    }

    return true;
}

bool OverlayRenderer::CreateSizeDependentResources(IDXGISwapChain* swapChain) {
    ID3D11Texture2D* backBuffer = nullptr;
    if (FAILED(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer)))) {
        return false;
    }

    D3D11_TEXTURE2D_DESC desc = {};
    backBuffer->GetDesc(&desc);
    m_backBufferWidth = desc.Width;
    m_backBufferHeight = desc.Height;

    HRESULT hr = m_device->CreateRenderTargetView(backBuffer, nullptr, &m_rtv);
    backBuffer->Release();
    return SUCCEEDED(hr);
}

void OverlayRenderer::EnsureInitialized(IDXGISwapChain* swapChain) {
    if (!m_deviceResourcesReady) {
        if (FAILED(swapChain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&m_device)))) {
            return;
        }
        m_device->GetImmediateContext(&m_context);
        m_deviceResourcesReady = CreateDeviceResources();
        if (!m_deviceResourcesReady) return;
    }

    DXGI_SWAP_CHAIN_DESC scd = {};
    swapChain->GetDesc(&scd);
    if (!m_formatLogged) {
        m_formatLogged = true;
        ModLog("Overlay: back buffer %ux%u, DXGI format %d", scd.BufferDesc.Width, scd.BufferDesc.Height,
               static_cast<int>(scd.BufferDesc.Format));
    }
    if (!m_sizeResourcesReady ||
        scd.BufferDesc.Width != m_backBufferWidth ||
        scd.BufferDesc.Height != m_backBufferHeight) {
        ReleaseSizeDependentResources();
        m_sizeResourcesReady = CreateSizeDependentResources(swapChain);
    }
}

void OverlayRenderer::BeginFrame() {
    if (!m_deviceResourcesReady || !m_sizeResourcesReady) return;
    m_vertexCount = 0;

    // Save whatever the game currently has bound so we can put it back.
    UINT savedCount = 1;
    m_context->OMGetRenderTargets(1, &m_savedRtv, &m_savedDsv);
    m_context->RSGetViewports(&savedCount, &m_savedViewport);

    ID3D11RenderTargetView* rtvs[] = { m_rtv };
    m_context->OMSetRenderTargets(1, rtvs, nullptr);

    D3D11_VIEWPORT vp = {};
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    vp.Width = static_cast<float>(m_backBufferWidth);
    vp.Height = static_cast<float>(m_backBufferHeight);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &vp);

    m_context->IASetInputLayout(m_inputLayout);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->VSSetShader(m_vs, nullptr, 0);
    m_context->PSSetShader(m_ps, nullptr, 0);
    m_context->OMSetBlendState(m_blendState, nullptr, 0xFFFFFFFF);
    m_context->OMSetDepthStencilState(m_depthStencilState, 0);
    m_context->RSSetState(m_rasterizerState);
}

void OverlayRenderer::DrawQuad(const OverlayQuad& quad) {
    if (!m_deviceResourcesReady || !m_sizeResourcesReady) return;
    if (m_vertexCount + 6 > kMaxVertices) return;

    // Fractions of client area -> NDC (-1..1, Y flipped since screen Y
    // increases downward but NDC Y increases upward).
    float cx = quad.centerXFrac * 2.0f - 1.0f;
    float cy = 1.0f - quad.centerYFrac * 2.0f;
    float hw = quad.halfWidthFrac * 2.0f;
    float hh = quad.halfHeightFrac * 2.0f;

    Vertex tl = { cx - hw, cy + hh, quad.r, quad.g, quad.b, quad.a };
    Vertex tr = { cx + hw, cy + hh, quad.r, quad.g, quad.b, quad.a };
    Vertex bl = { cx - hw, cy - hh, quad.r, quad.g, quad.b, quad.a };
    Vertex br = { cx + hw, cy - hh, quad.r, quad.g, quad.b, quad.a };

    Vertex* v = &m_vertexScratch[m_vertexCount];
    v[0] = tl; v[1] = tr; v[2] = bl;
    v[3] = tr; v[4] = br; v[5] = bl;
    m_vertexCount += 6;
}

SpriteTexture OverlayRenderer::CreateSpriteTexture(unsigned int width, unsigned int height, const unsigned char* rgba) {
    SpriteTexture tex;
    if (!m_device || width == 0 || height == 0 || !rgba) return tex;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = rgba;
    initData.SysMemPitch = width * 4;

    ID3D11Texture2D* texture = nullptr;
    if (FAILED(m_device->CreateTexture2D(&desc, &initData, &texture))) {
        return tex;
    }
    HRESULT hr = m_device->CreateShaderResourceView(texture, nullptr, &tex.srv);
    texture->Release();
    if (FAILED(hr)) {
        tex.srv = nullptr;
    }
    return tex;
}

void OverlayRenderer::DrawSprite(const OverlaySprite& sprite) {
    if (!m_deviceResourcesReady || !m_sizeResourcesReady) return;
    if (!sprite.texture.srv) return;
    FlushQuads(); // keep call order: a panel background must land under its text

    float cx = sprite.centerXFrac * 2.0f - 1.0f;
    float cy = 1.0f - sprite.centerYFrac * 2.0f;
    float hw = sprite.halfWidthFrac * 2.0f;
    float hh = sprite.halfHeightFrac * 2.0f;

    float u0 = sprite.uMin, v0 = sprite.vMin, u1 = sprite.uMax, v1 = sprite.vMax;
    SpriteVertex verts[6] = {
        { cx - hw, cy + hh, u0, v0 }, // top-left
        { cx + hw, cy + hh, u1, v0 }, // top-right
        { cx - hw, cy - hh, u0, v1 }, // bottom-left
        { cx + hw, cy + hh, u1, v0 }, // top-right
        { cx + hw, cy - hh, u1, v1 }, // bottom-right
        { cx - hw, cy - hh, u0, v1 }, // bottom-left
    };

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(m_context->Map(m_spriteVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        return;
    }
    memcpy(mapped.pData, verts, sizeof(verts));
    m_context->Unmap(m_spriteVertexBuffer, 0);

    D3D11_MAPPED_SUBRESOURCE mappedParams;
    if (SUCCEEDED(m_context->Map(m_spriteParamsBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedParams))) {
        float params[4] = { sprite.r, sprite.g, sprite.b, sprite.alpha };
        memcpy(mappedParams.pData, params, sizeof(params));
        m_context->Unmap(m_spriteParamsBuffer, 0);
    }

    UINT stride = sizeof(SpriteVertex);
    UINT offset = 0;
    ID3D11Buffer* vbs[] = { m_spriteVertexBuffer };
    m_context->IASetVertexBuffers(0, 1, vbs, &stride, &offset);
    m_context->IASetInputLayout(m_spriteInputLayout);
    m_context->VSSetShader(m_spriteVs, nullptr, 0);
    m_context->PSSetShader(m_spritePs, nullptr, 0);
    ID3D11ShaderResourceView* srvs[] = { sprite.texture.srv };
    m_context->PSSetShaderResources(0, 1, srvs);
    ID3D11SamplerState* samplers[] = { m_spriteSampler };
    m_context->PSSetSamplers(0, 1, samplers);
    ID3D11Buffer* cbs[] = { m_spriteParamsBuffer };
    m_context->PSSetConstantBuffers(0, 1, cbs);
    m_context->Draw(6, 0);
}

void OverlayRenderer::FlushQuads() {
    if (m_vertexCount == 0) return;
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(m_context->Map(m_vertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        memcpy(mapped.pData, m_vertexScratch, sizeof(Vertex) * m_vertexCount);
        m_context->Unmap(m_vertexBuffer, 0);

        // Re-bind the flat-color pipeline every time: a DrawSprite in
        // between binds the sprite pipeline's shaders and input layout.
        UINT stride = sizeof(Vertex);
        UINT offset = 0;
        ID3D11Buffer* vbs[] = { m_vertexBuffer };
        m_context->IASetVertexBuffers(0, 1, vbs, &stride, &offset);
        m_context->IASetInputLayout(m_inputLayout);
        m_context->VSSetShader(m_vs, nullptr, 0);
        m_context->PSSetShader(m_ps, nullptr, 0);
        m_context->Draw(m_vertexCount, 0);
    }
    m_vertexCount = 0;
}

void OverlayRenderer::EndFrame() {
    if (!m_deviceResourcesReady || !m_sizeResourcesReady) return;
    FlushQuads();

    // Restore the game's own state.
    ID3D11RenderTargetView* rtvs[] = { m_savedRtv };
    m_context->OMSetRenderTargets(1, rtvs, m_savedDsv);
    m_context->RSSetViewports(1, &m_savedViewport);

    if (m_savedRtv) { m_savedRtv->Release(); m_savedRtv = nullptr; }
    if (m_savedDsv) { m_savedDsv->Release(); m_savedDsv = nullptr; }
}
