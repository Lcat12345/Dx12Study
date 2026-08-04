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
- the runtime `Assets/` tree, including the default Scene
- build-time compiled `Shaders/*.cso`
- explicitly deployed DLLs found beside `Player.exe`, when present
- `package-manifest.txt`

The manifest records configuration, platform, source commit and dirty/clean
state, default Scene, runtime DLL policy, and a sorted package file list. It has
no timestamp or development-machine absolute path. Staging fails on source,
project, HLSL, or ImGui files, re-audits the Player link map for Editor and
ImGui symbols, and rejects an absolute repository path embedded in Player.exe.
Release uses `/PDBALTPATH:%_PDB%`, so its CodeView record names `Player.pdb`
without recording the development output directory.

Release uses the static CRT. The audited package currently contains no DLLs;
`Player.exe` imports only `SHELL32.dll`, `KERNEL32.dll`, `USER32.dll`,
`ole32.dll`, `d3d12.dll`, and `dxgi.dll`, which are Windows system components.

`Demo.scene` uses the tracked `Test` skybox so a clean checkout can build the
default package without the optional, locally downloaded `ColdSunset` asset.

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
