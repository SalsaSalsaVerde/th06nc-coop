// Textured-sprite pixel shader. Samples straight RGBA (with alpha
// premultiplied by the source pixel's own alpha channel, same convention
// as quad_ps.hlsl's per-vertex alpha) and applies a uniform alpha
// multiplier on top, set per-draw from the CPU side -- this is what the
// "translucent when near the local player" guidance-doc requirement
// (docs/00-overview.md's tracked-but-not-yet-implemented list) will hook
// into once that lands; for now it's just always 1.0.
Texture2D spriteTexture : register(t0);
SamplerState spriteSampler : register(s0);

cbuffer SpriteParams : register(b0) {
    float alphaMultiplier;
    float3 _padding;
};

struct PSInput {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET {
    float4 texColor = spriteTexture.Sample(spriteSampler, input.uv);
    texColor.a *= alphaMultiplier;
    return texColor;
}
