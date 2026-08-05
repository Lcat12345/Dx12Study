# Phase 12.6 Player Scene bootstrap and demo game

Phase 12.6 makes a Scene file the Player's only world bootstrap and runs the
same Play systems used by the Editor. Player no longer calls `BuildWorld`.

## Startup contract

`CommandLineToArgvW` preserves Unicode arguments. `--scene <path>` selects a
Scene; relative paths resolve against the directory containing `Player.exe`,
not the working directory. With no option, the shared MSBuild
`DefaultPlayerScene` property chooses the startup Scene. It named
`Assets/Scenes/Demo.scene` through Phase 12; M1 moved it to
`Assets/Scenes/Arena.scene` so a plain launch opens the game rather than the
rendering demo. The option contract itself did not change, and `--scene`
still reaches `Demo.scene`, which the package still ships.

Unknown options, a missing value, Scene parse or asset failures, and a Scene
without a usable `ActiveCamera` all fail before the window loop with a clear
message and non-zero exit code. The runtime root and resolved Scene path are
written to the debugger, and the Scene file name remains in the Player title.
ESC and the window close button continue to post exit code 0.

## Shared demo rules

Both `EditorApp` Play mode and `PlayerApp` call the same `RunPlaySystems`
function. Its input remains host-neutral:

- WASD and mouse move the Scene's `ActiveCamera`.
- An E key edge fires a normalized ray from that camera.
- The nearest mesh carrying `Spin` within 10 world units is toggled between
  stopped and the demo speed. Holding E cannot toggle repeatedly.
- Picking reuses the existing local-space ray/AABB path and ignores non-Spin
  meshes, so no physics or persistent entity-reference format was added.

Editor camera vertical E/Q movement remains an Edit-only control. In Play, E
is reserved for interaction. Player input covers the whole window and has no
ImGui capture or viewport-hover dependency.

`Demo.scene` uses only deployable content and procedural meshes. It includes
an initially stopped centered target plus skybox, normal-mapped opaque,
shadowed, and alpha-blended content so the Player presentation paths remain
visible in one startup Scene.

## Verification

- Release x64 solution build: 0 warnings, 0 errors.
- Engine tests: 25/25 passed, including default/relative/absolute argument
  selection, invalid argument diagnostics, Demo Scene loading, render feature
  coverage, key-edge toggling, and maximum interaction distance.
- Existing Player swap-chain presentation and D3D12 debug-layer regression
  tests remain clean for both 1x and 4x MSAA.
