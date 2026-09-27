// Pass-through vertex shader for textured sprites. Same NDC-space-already
// convention as quad_vs.hlsl, but carries UVs instead of a per-vertex color.
struct VSInput {
    float2 pos : POSITION;
    float2 uv : TEXCOORD0;
};

struct PSInput {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

PSInput main(VSInput input) {
    PSInput output;
    output.pos = float4(input.pos, 0.0f, 1.0f);
    output.uv = input.uv;
    return output;
}
