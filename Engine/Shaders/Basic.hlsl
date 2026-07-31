// Basic.hlsl : Phase 4 - transform vertices by the WVP matrix.

// Constant buffer bound to register b0 (see the root signature).
// Layout here must match the CPU-side ObjectConstants struct exactly.
cbuffer ObjectConstants : register(b0)
{
    float4x4 gWorldViewProj;
};

struct VSInput
{
    float3 position : POSITION;
    float4 color    : COLOR;
};

struct PSInput
{
    float4 position : SV_Position;
    float4 color    : COLOR;
};

PSInput VSMain(VSInput input)
{
    PSInput output;

    // Object space -> clip space in one multiply. The CPU pre-multiplied
    // World * View * Projection so the GPU only does this once per vertex.
    //
    // mul(vector, matrix) treats the vector as a ROW vector, which is the
    // DirectXMath convention. The C++ side transposes the matrix before
    // uploading, because HLSL reads matrices column-major by default.
    output.position = mul(float4(input.position, 1.0), gWorldViewProj);
    output.color    = input.color;
    return output;
}

float4 PSMain(PSInput input) : SV_Target
{
    return input.color;
}
