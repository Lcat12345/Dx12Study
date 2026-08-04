# Phase 12.5 compiled shaders and runtime content

Phase 12.5 makes the executable directory the deployable content boundary.
`Player` never searches for `Dx12Engine.slnx`, uses the current working
directory, or compiles HLSL at runtime.

## Runtime path contract

`GetExecutableDir()` uses a growing `GetModuleFileNameW` buffer and reports
Win32 failures instead of accepting a truncated path. `RuntimePaths` carries
the selected `root`, `assetDir`, and `shaderDir` into `Engine`, `Renderer`, and
`ResourceManager` through constructors.

The resulting layout for each executable target is:

```text
<exe directory>/
├─ Editor.exe or Player.exe
├─ Assets/
└─ Shaders/
   ├─ Basic.VS.cso
   ├─ Basic.PS.cso
   ├─ Skybox.VS.cso
   ├─ Skybox.PS.cso
   └─ ShadowDepth.VS.cso
```

Player always selects this root. Editor selects it first and may use the
source-tree `Assets/` directory only when the explicit
`AllowRepositoryAssetFallback=true` build property defines
`EDITOR_REPOSITORY_ASSET_FALLBACK`. Editor shaders never fall back to HLSL.
Both hosts report the selected paths through `OutputDebugStringW`.

## Build-time content pipeline

`BuildSettings.props` invokes the Windows SDK FXC tool once per entry point.
Debug outputs use `/Zi /Od`; Release outputs use `/O3`. Each target maps one
HLSL input to one configuration-specific intermediate `.cso`, so MSBuild can
skip unaffected outputs. After a host build, compiled shaders and the complete
relative `Assets/` tree are copied beside that host executable.

Runtime shader cache keys are logical `.cso` paths. Loading a missing shader
reports both the resolved file path and runtime root. No `.hlsl` is copied to
runtime output and `d3dcompiler.dll` is not a Player dependency.

Release uses the static `/MT` CRT consistently across Engine, Game, Editor,
Player, and tests. There are therefore no required non-system runtime DLLs in
Phase 12.5.

## Verification

- Release x64 rebuild: 0 warnings, 0 errors.
- The Player and Editor output roots each contain all five `.cso` files and a
  structure-preserving `Assets/` copy, with no `.hlsl` files.
- A second unchanged build leaves every `.cso` timestamp unchanged.
- Engine tests validate executable-relative paths, bytecode caching, missing
  shader diagnostics, both presentation paths, and a clean D3D12 debug layer.
