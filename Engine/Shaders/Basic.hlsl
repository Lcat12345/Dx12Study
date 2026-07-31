// Basic.hlsl : Phase 7 - Blinn-Phong lighting.
//
// Constant buffers are split BY UPDATE FREQUENCY:
//   b0 = per object (world matrix, material)  - written once per object
//   b1 = per frame  (camera, lights)          - written once per frame
// Uploading the lights once instead of once per object is the whole point.

cbuffer ObjectConstants : register(b0)
{
    float4x4 gWorld;
    float4x4 gWorldInvTranspose; // for normals - see the C++ side
    float4   gDiffuseAlbedo;     // material tint, multiplied with the texture
    float3   gSpecularColor;
    float    gShininess;         // specular exponent: high = small tight highlight
};

cbuffer PassConstants : register(b1)
{
    float4x4 gViewProj;
    float3   gEyePosW;          float _pad0;
    float3   gAmbientLight;     float _pad1;
    float3   gDirLightDirection; float _pad2; // direction the light TRAVELS
    float3   gDirLightColor;    float _pad3;
    float3   gPointLightPos;    float gPointLightRange;
    float3   gPointLightColor;  float _pad4;
};

Texture2D    gDiffuse : register(t0);
SamplerState gSampler : register(s0);

struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD;
};

struct PSInput
{
    float4 positionH : SV_Position; // clip space, for the rasterizer
    float3 positionW : POSITION;    // world space, for lighting math
    float3 normalW   : NORMAL;      // world space
    float2 uv        : TEXCOORD;
};

PSInput VSMain(VSInput input)
{
    PSInput output;

    // Lighting happens in WORLD space, so the pixel shader needs the world
    // position and normal - not just the final clip-space position.
    const float4 positionW = mul(float4(input.position, 1.0), gWorld);
    output.positionW = positionW.xyz;
    output.positionH = mul(positionW, gViewProj);

    // Normals use the inverse transpose, NOT the world matrix (see C++).
    // Cast to 3x3: normals are directions, translation must not apply.
    output.normalW = mul(input.normal, (float3x3)gWorldInvTranspose);

    output.uv = input.uv;
    return output;
}

// One light's contribution. 'toLight' points FROM the surface TOWARD the light.
float3 BlinnPhong(float3 lightColor, float3 toLight, float3 normal,
                  float3 toEye, float3 albedo)
{
    // Diffuse: how directly the surface faces the light. The dot product IS
    // the cosine between them, so a surface turned away gets 0.
    const float nDotL = max(dot(normal, toLight), 0.0);
    const float3 diffuse = albedo * lightColor * nDotL;

    // Specular (Blinn): instead of comparing the reflected ray to the eye
    // (classic Phong), compare the normal to the HALFWAY vector between
    // light and eye - cheaper and better behaved at grazing angles.
    const float3 halfway = normalize(toLight + toEye);
    const float  spec    = pow(max(dot(normal, halfway), 0.0), gShininess);

    // Gate the highlight by nDotL so unlit faces cannot shine.
    const float3 specular = gSpecularColor * lightColor * spec * nDotL;

    return diffuse + specular;
}

float4 PSMain(PSInput input) : SV_Target
{
    // Interpolation across the triangle shortens normals - renormalize.
    const float3 normal = normalize(input.normalW);
    const float3 toEye  = normalize(gEyePosW - input.positionW);

    const float3 albedo = gDiffuse.Sample(gSampler, input.uv).rgb * gDiffuseAlbedo.rgb;

    // Ambient stands in for all the bounced light we do not simulate,
    // so surfaces facing away from every light are not pure black.
    float3 color = gAmbientLight * albedo;

    // --- directional light: infinitely far, so direction is constant ---
    color += BlinnPhong(gDirLightColor, -gDirLightDirection, normal, toEye, albedo);

    // --- point light: has a position, so it fades with distance ---
    float3 toPointLight = gPointLightPos - input.positionW;
    const float distance = length(toPointLight);
    if (distance < gPointLightRange)
    {
        toPointLight /= distance; // normalize (we already paid for length)

        // Linear falloff squared - not physically correct (that would be
        // 1/d^2), but it reaches exactly zero at the range limit instead
        // of leaving a visible edge.
        float attenuation = saturate(1.0 - distance / gPointLightRange);
        attenuation *= attenuation;

        color += BlinnPhong(gPointLightColor * attenuation, toPointLight,
                            normal, toEye, albedo);
    }

    return float4(color, 1.0);
}
