// Basic.hlsl : Phase 5 - sample a texture instead of interpolating colors.

cbuffer ObjectConstants : register(b0)
{
    float4x4 gWorldViewProj;
};

// t0 = the SRV we put in the shader-visible heap.
// s0 = a STATIC sampler declared in the root signature itself, so it costs
//      no descriptor heap space and never needs binding at draw time.
Texture2D    gDiffuse : register(t0);
SamplerState gSampler : register(s0);

struct VSInput
{
    float3 position : POSITION;
    float2 uv       : TEXCOORD;
};

struct PSInput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD;
};

PSInput VSMain(VSInput input)
{
    PSInput output;
    output.position = mul(float4(input.position, 1.0), gWorldViewProj);
    // UVs are interpolated across the triangle just like colors were.
    output.uv = input.uv;
    return output;
}

float4 PSMain(PSInput input) : SV_Target
{
    // The sampler decides how to read between texels (filtering) and what
    // happens outside [0,1] (addressing) - both configured in the root sig.
    return gDiffuse.Sample(gSampler, input.uv);
}
