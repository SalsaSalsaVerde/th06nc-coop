// Textured-sprite pixel shader: the texture's straight RGBA times a
// per-draw tint (rgb multiplier, alpha multiplier). The font sheet's glyphs
// are white, so the tint colors text.
Texture2D spriteTexture : register(t0);
SamplerState spriteSampler : register(s0);

cbuffer SpriteParams : register(b0) {
    float4 tint;
};

struct PSInput {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET {
    float4 texColor = spriteTexture.Sample(spriteSampler, input.uv);
    texColor.rgb *= tint.rgb;
    texColor.a *= tint.a;
    return texColor;
}
