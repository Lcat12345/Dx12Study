# Phase 12.7 Player staging and repository-independent verification

Phase 12.7 turns the Release Player output into a reproducible staging package
and proves that it starts without the repository, solution, source tree, or
development working directory.

## Package contract

Every Release x64 Player build recreates
`Output/x64/Release/PlayerPackage`. Recreating the directory, rather than
copying newer files over it, prevents deleted runtime content from surviving as
a stale package file. The package contains only:

- `Player.exe`
- the assets named by `Engine/Tests/PlayerPackage.contents`, including every
  Scene the package can start with
- build-time compiled `Shaders/*.cso`
- explicitly deployed DLLs found beside `Player.exe`, when present
- `package-manifest.txt`

## Why the asset set is declared

Staging originally copied `Assets/` wholesale, which made the package a
function of the developer's working folder rather than of the commit: 51 of
its 51.6 MB were files git does not track, including the 43 MB `laevat/` tree
that was gitignored precisely because it cannot be redistributed - and which no
shipped Scene references.

Two rules now hold instead, both enforced by staging failing rather than by
convention:

1. Only entries listed in `PlayerPackage.contents` are staged, and an entry
   matching no file is an error. Adding an asset to the package is a
   deliberate, reviewable change.
2. Every staged asset must be tracked by git. Without this the manifest's
   `commit=` line would promise a reproducibility it could not deliver.

An asset a shipped Scene needs but the list omits is caught by the independent
verification below, which runs both Scenes: a missing texture surfaces as a
failed load, not as a silent white fallback. The manifest records the policy on
its `asset_policy=` line.

The manifest records configuration, platform, source commit and dirty/clean
state, default Scene, runtime DLL policy, and a sorted package file list. It has
no timestamp or development-machine absolute path. Staging fails on source,
project, HLSL, or ImGui files, audits the Player link map as described below,
and rejects an absolute repository path embedded in Player.exe.

## How the link boundary is audited

As a **whitelist**, not a list of forbidden names.

The audit was originally six hardcoded strings (`ImGui::`, `DebugUI`, ...)
searched for in `Player.map`. That only catches names somebody remembered to
add: a new Editor-only file misfiled into `Game.vcxproj` would link into
`Player.exe` and pass, because its name is not on the list. A blacklist cannot
notice a name it has never heard of - and the point of this boundary is to keep
out code that does not exist yet.

Two checks replace it, both derived from the project files, so neither goes
stale as sources are added:

1. Every translation unit reaching `Player.exe` must come from `Engine.lib`,
   `Game.lib`, or the Player project's own source list. The `.map` names each
   contributor in a trailing `Lib:Object.obj` column; anything compiled
   directly into the exe is compared against `Player.vcxproj`.
2. No source may be compiled by both `Editor.vcxproj` and any of
   `Engine`/`Game`/`Player`. This is what "Game.lib does not know the Editor"
   means in build terms, and it fails at staging with the offending path.

Verified by simulating the case the blacklist missed: registering a new
`Source/Editor/Gizmo.cpp` in both `Editor.vcxproj` and `Game.vcxproj` is
rejected with `Game.vcxproj compiles Editor-only sources, so they would reach
Player.exe: Source\Editor\Gizmo.cpp`.
Release uses `/PDBALTPATH:%_PDB%`, so its CodeView record names `Player.pdb`
without recording the development output directory.

Release uses the static CRT. The audited package currently contains no DLLs;
`Player.exe` imports only `SHELL32.dll`, `KERNEL32.dll`, `USER32.dll`,
`ole32.dll`, `d3d12.dll`, and `dxgi.dll`, which are Windows system components.

`Demo.scene` uses the tracked `Test` skybox so a clean checkout can build the
default package without the optional, locally downloaded `ColdSunset` asset -
now enforced by the tracked-asset rule above rather than left to the Scene
author's care.

## Independent verification

Run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File Engine/Tests/VerifyPlayerPackage.ps1
```

The verifier audits the manifest and forbidden-file policy, copies only the
staging contents to a new system temporary directory, and uses a separate
working directory. It launches the copied Player once with the default Scene
and once with `--scene Assets/Scenes/ShadowA.scene`. Each run verifies the
Scene-bearing window title, resizes the window, sends the E interaction edge,
uses M to exercise 1x and 4x MSAA, closes the window, and requires exit code 0.

For this automated check only, `DX12ENGINE_RUNTIME_PATH_LOG` asks Player to
mirror its debugger runtime-path message to a selected file. The verifier
requires both logs to name the isolated package as runtime root and rejects any
repository path.

## Verification result (2026-08-04)

- Release x64 solution build: 0 warnings, 0 errors.
- Engine tests: 25/25 passed, including both presentation paths and a clean
  D3D12 debug layer.
- Player project/link boundary audit: passed; no Editor or ImGui symbol.
- Package forbidden-file and manifest audit: passed.
- Repository-independent default and explicit Scene runs: exit code 0 after
  resize, interaction, and 1x/4x presentation exercise.
- Local package: 40 files, 51.60 MiB. Optional local assets are included because
  Phase 12 deliberately stages the complete Assets tree; cooking/manifest-based
  asset selection remains a later trigger.
- Separate PC without development tools: not tested; the required fresh-folder
  verification on this machine passed.
