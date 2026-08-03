// FrameResource.h : the per-frame data the CPU may only touch once the GPU
// has released it.
#pragma once

#include <d3d12.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include <cstdint>

#include "Core/Common.h"

// --- constant buffers, split BY UPDATE FREQUENCY ---
// Lights and camera are identical for every object in a frame, so uploading
// them once (pass CB) instead of once per object (object CB) is both less
// work and a clearer separation of concerns.

struct ObjectConstants // b0 - written once per object per frame
{
    DirectX::XMFLOAT4X4 world;
    DirectX::XMFLOAT4X4 worldInvTranspose;
    DirectX::XMFLOAT4   diffuseAlbedo;
    DirectX::XMFLOAT3   specularColor;
    float               shininess;
    float               normalStrength;
    // Three loose floats, NOT an XMFLOAT3.
    //
    // HLSL packs a scalar wherever it fits in the current 16-byte register,
    // but moves a float3 to the next register if it would straddle one. After
    // normalStrength the register has 12 bytes left - just enough that
    // whether a float3 "straddles" depends on reading the rule the same way
    // the compiler does. Scalars have no such ambiguity: they pack, and both
    // sides are 176 bytes. A disagreement here is not an error, it is a
    // material whose colours come from the wrong offset.
    float               _pad0, _pad1, _pad2;
};

// Layout must match the HLSL cbuffer exactly. In HLSL a float3 followed by a
// float packs into one 16-byte register, which is why every pair below is
// written together.
struct PassConstants // b1 - written once per frame
{
    DirectX::XMFLOAT4X4 viewProj;
    // View-projection with the camera's translation stripped out. The skybox
    // uses it so the background rotates with the camera but never gets
    // nearer - which is what "infinitely far away" looks like.
    DirectX::XMFLOAT4X4 skyViewProj;
    // Where the directional light looked from this frame. The shadow pass
    // draws with it and 11.5's lighting pass reads it back to ask "was this
    // pixel the closest thing the light saw?" - one value written once, so
    // the two cannot drift apart.
    DirectX::XMFLOAT4X4 shadowViewProj;
    DirectX::XMFLOAT3   eyePosW;           float pad0 = 0.0f;
    DirectX::XMFLOAT3   ambientLight;      float pad1 = 0.0f;
    DirectX::XMFLOAT3   dirLightDirection; float pad2 = 0.0f;
    DirectX::XMFLOAT3   dirLightColor;     float pad3 = 0.0f;
    DirectX::XMFLOAT3   pointLightPos;     float pointLightRange = 0.0f;
    DirectX::XMFLOAT3   pointLightColor;   float pad4 = 0.0f;
};

// The object CB holds one 256-byte slot each, so 256 objects cost 64 KB per
// frame set - not worth being clever about. Placing entities by hand blew
// past 32 immediately; a genuinely dynamic buffer waits until a scene needs
// more than this, because resizing it means the same deferred-recreation
// dance the render target needs.
constexpr UINT kMaxObjects   = 256;
constexpr UINT kObjectCBSize = Align(sizeof(ObjectConstants), 256);
constexpr UINT kPassCBSize   = Align(sizeof(PassConstants), 256);

// How many frames the CPU may run ahead of the GPU.
//
// Everything the CPU writes per frame - the command allocator's memory and
// the constant buffers - is still being READ by the GPU until that frame
// finishes. With one copy the CPU has no choice but to wait for the GPU
// every frame. With one copy PER FRAME IN FLIGHT the CPU can start frame
// N+1 while the GPU is still drawing frame N, and only has to wait when it
// laps back around to a set the GPU has not released.
//
// 2 is the usual choice: 3 raises throughput slightly but adds a frame of
// input latency.
constexpr UINT kFramesInFlight = 2;

struct FrameResource
{
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator;
    Microsoft::WRL::ComPtr<ID3D12Resource>         objectCB;
    uint8_t*                                       objectCBMapped = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource>         passCB;
    uint8_t*                                       passCBMapped   = nullptr;
    // Fence value signalled when the GPU finishes the frame that used this
    // set. 0 means "never submitted yet".
    UINT64                                         fenceValue     = 0;
};
