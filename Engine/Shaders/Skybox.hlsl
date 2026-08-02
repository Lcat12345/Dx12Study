// Skybox.hlsl : the background, drawn as a cube seen from the inside.
//
// Shares the scene's root signature, so the constant buffer layout below
// must match Basic.hlsl exactly. Only b1 and t0 are read - b0 is bound but
// unused, which costs nothing and saves a second root signature.
//
// Two tricks make a box look like an infinitely distant sky:
//
//   1. The view matrix arrives with its TRANSLATION removed (gSkyViewProj),
//      so moving the camera does not move the box relative to it. Only
//      rotation has any effect - exactly how a horizon behaves.
//   2. The output depth is forced to the far plane, so every piece of real
//      geometry wins the depth test against it.

cbuffer ObjectConstants : register(b0)
{
    float4x4 gWorld;
    float4x4 gWorldInvTranspose;
    float4   gDiffuseAlbedo;
    float3   gSpecularColor;
    float    gShininess;
};

cbuffer PassConstants : register(b1)
{
    float4x4 gViewProj;
    float4x4 gSkyViewProj;
    float3   gEyePosW;          float _pad0;
    float3   gAmbientLight;     float _pad1;
    float3   gDirLightDirection; float _pad2;
    float3   gDirLightColor;    float _pad3;
    float3   gPointLightPos;    float gPointLightRange;
    float3   gPointLightColor;  float _pad4;
};

// TextureCube, not Texture2D: sampled by a DIRECTION rather than by uv. The
// hardware picks the face and the texel from the vector's dominant axis.
TextureCube  gSky     : register(t0);
SamplerState gSampler : register(s0);

struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD;
};

struct PSInput
{
    float4 positionH : SV_Position;
    // The cube's own local position doubles as the lookup direction: a unit
    // cube's surface points outward from its centre in every direction.
    float3 direction : TEXCOORD0;
};

PSInput VSMain(VSInput input)
{
    PSInput output;

    const float4 clip = mul(float4(input.position, 1.0f), gSkyViewProj);

    // z = w makes z/w = 1 after the perspective divide - the far plane
    // exactly. Paired with LESS_EQUAL depth testing (not LESS) so the sky
    // still passes where nothing has been drawn.
    output.positionH = float4(clip.xy, clip.w, clip.w);
    output.direction = input.position;
    return output;
}

float4 PSMain(PSInput input) : SV_Target
{
    return gSky.Sample(gSampler, normalize(input.direction));
}
