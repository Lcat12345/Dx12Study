# Phase 12.4 project and link boundary

Phase 12.4 turns the source ownership rules into four x64 Visual Studio
targets. Physical source folders remain under `Engine/Source`; project items
and references are the authoritative ownership boundary.

| Target | Type | Owns | References |
|---|---|---|---|
| `Engine` | static library | Core, Graphics, Loaders | none |
| `Game` | static library | Components, Scene, execution contexts, systems, world construction | Engine |
| `Editor` | Windows application | EditorApp, editor session/UI/picking, ImGui layer and backends | Game, Engine |
| `Player` | Windows application | PlayerApp and runtime entry point | Game, Engine |

`Engine.vcxproj` deliberately excludes every `Source/Game` implementation and
`ImGuiLayer`. `Game.vcxproj` has no editor header or ImGui include directory.
Only `Editor.vcxproj` names `ThirdParty/imgui` or compiles the Win32/DX12 ImGui
backends. `Player.vcxproj` generates `Player.map` so the binary boundary can be
checked independently of the source layout.

## Host split

`EditorApp` preserves the Phase 12.3 offscreen scene plus overlay path and owns
the Edit/Play snapshot transaction. `PlayerApp` starts a `PlaySession`, runs
only the shared Play systems, and presents the same scene passes directly to
the swap chain without an overlay callback. Startup scene selection remains a
Phase 12.6 concern; Phase 12.4 uses the shared `BuildWorld` bootstrap.

## Build policy

- x64 is the only solution platform; stale Win32/x86 configurations were
  removed.
- All four targets use `/MDd` in Debug and `/MD` in Release.
- Outputs and intermediate files are separated by project name.
- Building `Player.vcxproj` builds only Player, Game, and Engine.
- The test executable links `Engine.lib` and `Game.lib` instead of compiling
  their implementation files a second time.

## Verification contract

Both Debug and Release solution builds must produce `Engine.lib`, `Game.lib`,
`Editor.exe`, and `Player.exe`. Phase 12 tests must pass against the two static
libraries. A scan of `Player.vcxproj` and `Player.map` must find no ImGui,
DebugUI, AssetBrowser, EditorSession, or EditorApp dependency.

Run `Tests/VerifyPhase12ProjectBoundary.ps1 -Configuration Debug` (or
`Release`) after a build to repeat the project and map checks.
