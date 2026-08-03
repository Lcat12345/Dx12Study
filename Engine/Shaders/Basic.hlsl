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
    float    gNormalStrength;    // 0 = ignore the normal map, 1 = as authored
    // Three scalars, not a float3 - see the C++ struct for why the difference
    // matters here.
    float    _padObj0, _padObj1, _padObj2;
};

cbuffer PassConstants : register(b1)
{
    float4x4 gViewProj;
    // The same projection with the camera's TRANSLATION removed, for the
    // skybox: the background must turn with the camera but never approach it.
    float4x4 gSkyViewProj;
    // The directional light's view-projection, for shadow mapping.
    float4x4 gShadowViewProj;
    float3   gEyePosW;          float _pad0;
    float3   gAmbientLight;     float _pad1;
    float3   gDirLightDirection; float _pad2; // direction the light TRAVELS
    float3   gDirLightColor;    float _pad3;
    float3   gPointLightPos;    float gPointLightRange;
    float3   gPointLightColor;  float _pad4;
    float2   gShadowTexelSize;
    float    gShadowBias;
    float    gShadowStrength;
};

Texture2D    gDiffuse   : register(t0);
// Directions in TANGENT space, packed [-1,1] -> [0,1]. Always bound: a
// material with no map of its own gets the flat 1x1 default.
//
// This is LINEAR data, not colour. If diffuse textures later move to an sRGB
// format, this one must not follow - an sRGB read would curve the numbers and
// bend every normal.
Texture2D    gNormalMap : register(t1);
Texture2D    gShadowMap : register(t2);
SamplerState gSampler   : register(s0);
SamplerComparisonState gShadowSampler : register(s1);

struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD;
    float4 tangent  : TANGENT;  // xyz direction, w bitangent handedness
};

struct PSInput
{
    float4 positionH : SV_Position; // clip space, for the rasterizer
    float3 positionW : POSITION;    // world space, for lighting math
    float3 normalW   : NORMAL;      // world space
    float2 uv        : TEXCOORD;
    float4 tangentW  : TANGENT;     // xyz world space, w carried through
    float4 positionLightH : TEXCOORD1; // light clip space for shadow lookup
};

PSInput VSMain(VSInput input)
{
    PSInput output;

    // Lighting happens in WORLD space, so the pixel shader needs the world
    // position and normal - not just the final clip-space position.
    const float4 positionW = mul(float4(input.position, 1.0), gWorld);
    output.positionW = positionW.xyz;
    output.positionH = mul(positionW, gViewProj);
    output.positionLightH = mul(positionW, gShadowViewProj);

    // Normals use the inverse transpose, NOT the world matrix (see C++).
    // Cast to 3x3: normals are directions, translation must not apply.
    output.normalW = mul(input.normal, (float3x3)gWorldInvTranspose);

    // The TANGENT uses gWorld, not the inverse transpose - the opposite of
    // the normal, and not a typo.
    //
    // A normal is defined by being perpendicular to the surface, and that
    // relationship is what the inverse transpose preserves. A tangent is
    // defined by LYING IN the surface, along a real direction between two
    // points on it, so it transforms like any other difference of positions:
    // by the world matrix. Under non-uniform scale the two stop being
    // perpendicular, which is why the pixel shader re-orthogonalizes.
    //
    // input.tangent.w was decided once, in LOCAL space, by comparing the
    // mesh's own bitangent to cross(localNormal, localTangent) - a
    // comparison that assumes an orientation-PRESERVING transform out to
    // world space. A transform with a negative determinant (a mirror - e.g.
    // one negative Transform.scale component) reverses that orientation, so
    // the stored w now names the WRONG side unless corrected here.
    //
    // determinant() is unaffected by the row/col-major transpose already
    // baked into gWorld - det(A) == det(A^T) - so this reads the transform's
    // true handedness regardless of that storage detail.
    const float handedness = determinant((float3x3)gWorld) < 0.0 ? -1.0 : 1.0;
    output.tangentW = float4(mul(input.tangent.xyz, (float3x3)gWorld),
                             input.tangent.w * handedness);

    output.uv = input.uv;
    return output;
}

// Percentage-closer filtering: each comparison asks whether this receiver
// was visible to the light, then the 3x3 average softens one hard texel edge.
float DirectionalVisibility(float4 positionLightH, float3 geometryNormal)
{
    if (gShadowStrength <= 0.0 || positionLightH.w <= 0.0)
    {
        return 1.0;
    }

    const float3 ndc = positionLightH.xyz / positionLightH.w;
    const float2 uv = float2(ndc.x * 0.5 + 0.5, -ndc.y * 0.5 + 0.5);

    // A fixed receiver bias is sufficient only when the surface faces the
    // light. At a grazing angle, one shadow texel spans a much larger depth
    // interval and the stored caster depth repeatedly crosses the receiver
    // depth, exposing the shadow texel grid as acne/triangular moire.
    //
    // Use the GEOMETRIC normal, not the sampled normal map: normal maps bend
    // lighting but do not change the surface written into the shadow map.
    // Letting texture detail alter bias would imprint that texture into the
    // shadow test. gShadowBias remains the editable minimum and grows smoothly
    // to 4x at grazing angles.
    const float nDotL = saturate(dot(geometryNormal, -gDirLightDirection));
    const float receiverBias = gShadowBias * (1.0 + 3.0 * (1.0 - nDotL));

    // D3D NDC depth is already [0,1]. Outside the light's fitted volume is
    // not represented by this map, so it must be treated as lit.
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 ||
        ndc.z < 0.0 || ndc.z > 1.0)
    {
        return 1.0;
    }

    float visibility = 0.0;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            const float2 offset = float2(float(x), float(y)) * gShadowTexelSize;
            visibility += gShadowMap.SampleCmpLevelZero(
                gShadowSampler, uv + offset, ndc.z - receiverBias);
        }
    }
    visibility /= 9.0;

    return lerp(1.0, visibility, saturate(gShadowStrength));
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
    const float3 geometryNormal = normalize(input.normalW);

    // --- rebuild the tangent frame, per pixel ---
    // Interpolation tilts the tangent out of the surface, and non-uniform
    // scale skewed it in the vertex shader, so subtract off whatever part now
    // points along the normal. Doing this per pixel rather than trusting the
    // interpolated value is what keeps the frame square across a triangle.
    float3 T = input.tangentW.xyz - geometryNormal * dot(geometryNormal, input.tangentW.xyz);
    const float tangentLengthSq = dot(T, T);

    float3 normal = geometryNormal;
    if (tangentLengthSq > 1e-12)
    {
        T = T * rsqrt(tangentLengthSq);
        // w carries the mirrored-UV flip decided when the mesh was built.
        const float3 B = cross(geometryNormal, T) * input.tangentW.w;

        // Unpack [0,1] back to [-1,1]. The flat default is (128,128,255),
        // which lands on (0, 0, 1) - straight out of the surface - so a
        // material with no map falls through this unchanged.
        float3 sampled = gNormalMap.Sample(gSampler, input.uv).xyz * 2.0 - 1.0;

        // Strength leans only on the SIDEWAYS components. Scaling z as well
        // would just rescale the whole vector, which normalize then undoes -
        // it would do nothing at all.
        sampled.xy *= gNormalStrength;

        // Tangent space -> world. Rows T, B, N means the result is
        // sampled.x*T + sampled.y*B + sampled.z*N.
        normal = normalize(mul(sampled, float3x3(T, B, geometryNormal)));
    }

    const float3 toEye  = normalize(gEyePosW - input.positionW);

    const float4 diffuseSample = gDiffuse.Sample(gSampler, input.uv);
    const float3 albedo = diffuseSample.rgb * gDiffuseAlbedo.rgb;

    // Ambient stands in for all the bounced light we do not simulate,
    // so surfaces facing away from every light are not pure black.
    float3 color = gAmbientLight * albedo;

    // --- directional light: infinitely far, so direction is constant ---
    const float directionalVisibility =
        DirectionalVisibility(input.positionLightH, geometryNormal);
    color += directionalVisibility *
             BlinnPhong(gDirLightColor, -gDirLightDirection, normal, toEye, albedo);

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

    return float4(color, saturate(diffuseSample.a * gDiffuseAlbedo.a));
}
