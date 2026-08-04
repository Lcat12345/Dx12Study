# Phase 12.1 execution and future project boundary

Phase 12.1 separates **when code runs** before Phase 12.4 separates which binary
can link it. The editor currently exposes `Run > Edit/Play`; Play-state rollback
is intentionally deferred to the Phase 12.2 memory snapshot transaction.

## Frame and system ownership

| Group | Current entry points | Contract |
|---|---|---|
| Always | `DemoGame::CaptureHostFrame`, `DemoGame::RunAlways`, `OnRender` | Consume host input every frame, build editor UI, select presentation settings, flatten render data |
| Editor-only | `DemoGame::RunEditorOnly`, `RunEditorSystems` | Move only the out-of-World `EditorCamera`; never advance Scene gameplay state |
| Play-only | `DemoGame::RunPlayOnly`, `RunPlaySystems` | Advance the ActiveCamera, Spin, and LightOrbit from `PlaySession` input/time |

The editor host produces an `InputContext` once and masks mouse/keyboard capture
through `MakeEditorFrameContext`. The future Player host calls
`MakePlayerFrameContext`, whose signature has no viewport-hover or ImGui state.

The `CameraView` selected in `DemoGame::OnUpdate` is handed both to DebugUI
picking and to `Renderer::Render`. Edit selects `EditorCamera`; Play selects the
World's `ActiveCamera`. Moving the edit view therefore cannot change serialized
Scene data.

## Phase 12.4 source ownership

The following implementation files are Editor-only and are owned by
`Editor.vcxproj`. They are not linked by Player:

- `Source/Game/AssetBrowser.cpp`
- `Source/Game/DebugUI.cpp`
- `Source/Game/EditorSession.cpp`
- `Source/Game/Picking.cpp`
- `Source/Editor/EditorApp.cpp`
- `Source/Editor/Main.cpp`
- `Source/Graphics/ImGuiLayer.cpp`
- Dear ImGui and its Win32/DX12 backend translation units

Shared Game/runtime implementation remains in `BuildWorld.cpp`, `Scene.cpp`,
`Systems.cpp`, and `ExecutionContext.cpp`. `MakePlayerFrameContext` and
`RunPlaySystems` are intentionally free of editor and ImGui types. Phase 12.3
removed the remaining Renderer/Engine ownership of `ImGuiLayer`; Phase 12.4
now enforces this list as a project and link boundary. See
[`Phase12.3-PresentationBoundary.md`](Phase12.3-PresentationBoundary.md).
