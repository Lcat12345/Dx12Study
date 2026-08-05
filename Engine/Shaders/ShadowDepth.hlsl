// ShadowDepth.hlsl : the scene as the directional light sees it.
//
// There is no pixel shader in this file, and that is the point. The only
// output of this pass is the depth buffer, and the rasterizer writes depth on
// its own - a pixel shader would run for every covered pixel to produce a
// colour nobody reads.
//
// Shares the scene root signature, so the two constant buffers must match
// Basic.hlsl exactly even though this shader reads two fields out of them.
// b0's material half and b1's lights are bound and ignored, which costs
// nothing and saves maintaining a second root signature.

cbuffer ObjectConstants : register(b0)
{
    float4x4 gWorld;
    float4x4 gWorldInvTranspose;
    float4   gDiffuseAlbedo;
    float3   gSpecularColor;
    float    gShininess;
    float    gNormalStrength;
    float    _padObj0, _padObj1, _padObj2;
};

cbuffer PassConstants : register(b1)
{
    float4x4 gViewProj;
    float4x4 gSkyViewProj;
    // The light's own view-projection. Written by the same code that writes
    // the camera's, so the shadow pass and the lighting pass cannot
    // disagree about where the light was this frame.
    float4x4 gShadowViewProj;
    float3   gEyePosW;          float _pad0;
    float3   gAmbientLight;     float _pad1;
    float3   gDirLightDirection; float _pad2;
    float3   gDirLightColor;    float _pad3;
    float3   gPointLightPos;    float gPointLightRange;
    float3   gPointLightColor;  float _pad4;
    float2   gShadowTexelSize;
    float    gShadowBias;
    float    gShadowStrength;
};

// Only POSITION is read. The others stay declared so this shader agrees with
// the shared input layout; the input assembler skips what no stage uses.
struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD;
    float4 tangent  : TANGENT;
};

struct InstanceInput
{
    float4 world0 : INSTANCEWORLD0;
    float4 world1 : INSTANCEWORLD1;
    float4 world2 : INSTANCEWORLD2;
    float4 world3 : INSTANCEWORLD3;
    float4 worldInvTranspose0 : INSTANCEWORLDINVTRANSPOSE0;
    float4 worldInvTranspose1 : INSTANCEWORLDINVTRANSPOSE1;
    float4 worldInvTranspose2 : INSTANCEWORLDINVTRANSPOSE2;
    float4 worldInvTranspose3 : INSTANCEWORLDINVTRANSPOSE3;
};

// SV_Position only - there is nothing downstream to interpolate for.
float4 VSMain(VSInput input) : SV_Position
{
    const float4 positionW = mul(float4(input.position, 1.0), gWorld);
    return mul(positionW, gShadowViewProj);
}


float4 VSInstanced(VSInput input, InstanceInput instance) : SV_Position
{
    const float4x4 world = float4x4(
        instance.world0, instance.world1, instance.world2, instance.world3);
    const float4 positionW = mul(float4(input.position, 1.0), world);
    return mul(positionW, gShadowViewProj);
}
