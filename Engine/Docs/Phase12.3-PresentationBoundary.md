# Phase 12.3 presentation and overlay boundary

Phase 12.3 keeps one scene-pass sequence while allowing an editor host and a
future Player host to present it differently. `Renderer::RenderFrame` accepts a
`SceneOutput` and an optional command-list callback. Neither `Engine` nor
`Renderer` includes, constructs, advances, or calls `ImGuiLayer`.

## Ownership

| Owner | Responsibility |
|---|---|
| `Engine` | Window, clock, renderer, and host-neutral loop |
| `Renderer` | Scene attachments, shared scene passes, resource transitions, submit and present |
| Editor host (`DemoGame`) | `ImGuiLayer` lifetime, window message hook, UI frame start, overlay recording callback |
| Future Player host | Select `SceneOutput::SwapChain` and omit the callback |

The editor drains the renderer before destroying its host-owned overlay. This
preserves GPU lifetime safety without returning overlay ownership to the
renderer.

## Shared pass sequence

`RecordScenePasses` records the same sequence for both outputs:

1. directional shadow depth
2. opaque scene
3. skybox
4. transparent scene

The selected `SceneAttachments` supplies only colour/depth handles, dimensions,
viewport/scissor, and the colour resource's resting state.

## Presentation paths

- Offscreen 1x transitions the scene texture back to shader-resource state.
- Offscreen 4x resolves into its shader-readable single-sample texture.
- Swap-chain 1x draws the scene directly into the current back buffer.
- Swap-chain 4x draws into a window-sized multisample colour/depth pair and
  resolves into the current back buffer.
- The optional generic callback records after the selected scene presentation.
  An empty callback is a supported path in both 1x and 4x.

Window resize recreates only swap-chain presentation attachments. Editor panel
resize recreates only offscreen colour/depth/resolve attachments. MSAA changes
recreate both attachment sets with a shared sample count and quality.

## Verification

`functional/presentation-paths` creates a hidden window and WARP renderer, then
records offscreen and swap-chain output without an overlay in 1x, in 4x when
supported, and after a window resize. A Debug build requires the D3D12 Debug
Layer message count to remain zero. The test project compiles `Renderer.h`
without an ImGui include directory, exercising the intended public-header
boundary.
