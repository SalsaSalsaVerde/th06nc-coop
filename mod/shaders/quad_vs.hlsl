// Minimal pass-through vertex shader for the overlay renderer.
// Positions are already in NDC clip space (computed CPU-side each frame),
// so there's no transform matrix here at all -- this just forwards
// position/color to the rasterizer.
struct VSInput {
    float2 pos : POSITION;
    float4 color : COLOR;
};

struct PSInput {
    float4 pos : SV_POSITION;
    float4 color : COLOR;
};

PSInput main(VSInput input) {
    PSInput output;
    output.pos = float4(input.pos, 0.0f, 1.0f);
    output.color = input.color;
    return output;
}
