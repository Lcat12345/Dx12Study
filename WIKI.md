# Dx12Engine 사용설명서

이 문서는 `Dx12Engine.slnx`를 기준으로 엔진을 빌드하고, Editor에서 Scene을
제작하며, Player로 실행하고 배포하는 방법을 설명한다. 현재 기준은 Phase 12.7
완료 상태다.

## 목차

- [엔진 개요](#엔진-개요)
- [요구 환경](#요구-환경)
- [프로젝트 구조](#프로젝트-구조)
- [빠른 시작](#빠른-시작)
- [빌드와 산출물](#빌드와-산출물)
- [Editor 사용법](#editor-사용법)
- [Scene과 Component](#scene과-component)
- [에셋 사용법](#에셋-사용법)
- [Play 모드](#play-모드)
- [Player 사용법](#player-사용법)
- [배포 방법](#배포-방법)
- [테스트와 검증](#테스트와-검증)
- [문제 해결](#문제-해결)
- [Phase 12 구현 감사](#phase-12-구현-감사)
- [관련 문서](#관련-문서)

## 엔진 개요

Dx12Engine은 Win32와 DirectX 12로 작성된 학습용 3D 엔진이다. 현재 다음 기능을
제공한다.

- ECS 기반 World와 Component 조합
- ImGui 기반 Scene Editor
- Scene 저장과 불러오기
- Edit/Play 전환과 Play 상태 롤백
- OBJ, PNG, procedural mesh 리소스
- Blinn-Phong 조명, skybox, normal mapping
- directional shadow와 PCF
- opaque/alpha blend 렌더링
- 1x/4x MSAA와 VSync 전환
- Editor와 분리된 독립 Player
- Release PlayerPackage 자동 생성

Editor와 Player는 같은 Scene 형식과 게임 시스템을 사용한다. Editor-only UI와
ImGui는 Player 바이너리에 링크되지 않는다.

## 요구 환경

공식 빌드 대상은 **Windows x64**다.

| 항목 | 요구사항 |
|---|---|
| 운영체제 | Windows 10 또는 Windows 11 x64 |
| IDE/빌드 | Visual Studio의 C++ Desktop 개발 도구 |
| 툴셋 | 프로젝트에 설정된 `v145` |
| SDK | Windows 10 SDK, `fxc.exe` 포함 |
| 그래픽 | DirectX 12 지원 GPU와 드라이버 |
| 구성 | `Debug|x64`, `Release|x64` |

Release는 정적 CRT(`/MT`)를 사용한다. 현재 Player 배포본에는 별도의 Visual C++
Redistributable이 필요하지 않다.

## 프로젝트 구조

솔루션은 네 실행 단위와 테스트 프로젝트로 나뉜다.

| 프로젝트 | 산출물 | 역할 | 의존성 |
|---|---|---|---|
| Engine | `Engine.lib` | Win32, ECS, 렌더러, 리소스, loader | 없음 |
| Game | `Game.lib` | Component, Scene, 게임 시스템, picking | Engine |
| Editor | `Editor.exe` | Editor UI, EditorCamera, Asset Browser | Engine, Game, ImGui |
| Player | `Player.exe` | Scene bootstrap, 게임 입력, 직접 presentation | Engine, Game |
| EngineTests | `EngineTests.exe` | Phase 12 기능 및 회귀 검증 | Engine, Game |

주요 디렉터리는 다음과 같다.

```text
Dx12Engine/
├─ Dx12Engine.slnx
├─ WIKI.md
├─ ROADMAP.md
├─ docs/
├─ Engine/
│  ├─ Assets/
│  ├─ Shaders/
│  ├─ Source/
│  │  ├─ Core/
│  │  ├─ Graphics/
│  │  ├─ Game/
│  │  ├─ Editor/
│  │  └─ Player/
│  ├─ Tests/
│  └─ Docs/
├─ ThirdParty/
└─ Output/
```

## 빠른 시작

1. `Dx12Engine.slnx`를 Visual Studio에서 연다.
2. 구성은 `Debug` 또는 `Release`, 플랫폼은 `x64`를 선택한다.
3. 솔루션을 빌드한다.
4. Editor를 사용하려면 `Editor` 프로젝트를 시작 프로젝트로 지정한다.
5. 독립 게임을 실행하려면 `Player` 프로젝트를 시작하거나 Release
   `PlayerPackage/Player.exe`를 실행한다.

기본 실행 파일 위치:

```text
Output/x64/Debug/Editor/Editor.exe
Output/x64/Debug/Player/Player.exe
Output/x64/Release/Editor/Editor.exe
Output/x64/Release/Player/Player.exe
```

## 빌드와 산출물

### Visual Studio 빌드

솔루션 탐색기에서 **솔루션 빌드**를 실행한다. 프로젝트 참조가 Engine, Game,
Editor/Player 순서를 자동으로 처리한다.

### 명령줄 빌드

Developer PowerShell에서 솔루션 루트를 기준으로 실행한다.

```powershell
msbuild .\Dx12Engine.slnx /m /p:Configuration=Debug /p:Platform=x64
msbuild .\Dx12Engine.slnx /m /p:Configuration=Release /p:Platform=x64
```

### 셰이더와 런타임 콘텐츠

빌드 중 `Basic.hlsl`, `Skybox.hlsl`, `ShadowDepth.hlsl`의 entry point가 FXC로
컴파일된다. 실행 디렉터리에는 HLSL 대신 다음 `.cso`가 복사된다.

```text
Shaders/
├─ Basic.VS.cso
├─ Basic.PS.cso
├─ Skybox.VS.cso
├─ Skybox.PS.cso
└─ ShadowDepth.VS.cso
```

`Assets/`도 실행 파일 옆에 복사된다. Player는 반드시 실행 파일 위치를 runtime
root로 사용한다. 현재 working directory는 리소스 탐색에 사용하지 않는다.

## Editor 사용법

### 주요 패널

| 패널 | 용도 |
|---|---|
| Frame | FPS, frame time, mode, VSync, 4x MSAA, Debug Layer 상태 |
| Scene | 실제 Scene 렌더와 picking/배치 viewport |
| Entities | Entity 생성, 복제, 삭제, 선택 |
| Inspector | Component 추가·삭제와 값 편집 |
| Assets | Mesh, Texture, Skybox 탐색과 할당 |
| Shadow map | shadow depth texture와 범위 확인 |

패널 배치는 `imgui.ini`에 저장된다. 이 파일은 사용자별 UI 상태이며 배포 패키지에
포함되지 않는다.

### Editor 카메라 조작

Scene 패널 위에서 조작한다.

| 입력 | 동작 |
|---|---|
| 마우스 오른쪽 버튼 + 이동 | 시점 회전 |
| `W`, `A`, `S`, `D` | 전후좌우 이동 |
| `E`, `Q` | 위/아래 이동 |
| `Shift` | 빠른 이동 |
| `V` | VSync 전환 |

ImGui가 키보드 입력을 사용 중이면 Editor 카메라 입력은 차단된다. Edit 상태의
EditorCamera는 World 밖에 있으므로 Scene에 저장되지 않는다.

### Entity 편집

1. **Entities** 패널에서 Entity를 선택한다.
2. **Inspector**에서 Component 값을 편집한다.
3. **Add Component**로 필요한 Component를 추가한다.
4. Component 제목 오른쪽의 닫기 버튼으로 해당 Component를 제거한다.

**New**, **Duplicate**, **Delete**는 구조 변경을 안전하게 다음 처리 경계에
적용한다. Duplicate는 대상 Entity가 가진 Component 값을 복사한다.

### Scene 파일 명령

상단 **File** 메뉴에서 다음 명령을 사용한다.

- **New**: Camera, Sun, Environment가 포함된 빈 Scene 생성
- **Open**: `Assets/Scenes/*.scene` 목록에서 Scene 열기
- **Save**: 현재 경로에 저장; 새 Scene이면 Save As로 전환
- **Save As...**: `Assets/Scenes/` 아래에 새 `.scene` 저장

Play 중에는 New/Open/Save/Save As를 사용할 수 없다.

Scene 로드는 임시 World에 먼저 수행되며 전체 파싱 성공 후에만 현재 World를
교체한다. 저장은 sibling `.tmp` 파일을 거친 원자적 교체 방식이다.

## Scene과 Component

Scene은 UTF-8 line-based text 형식이며 현재 버전은 `scene 5`다. 빈 줄과 `#`으로
시작하는 주석은 무시된다.

```text
scene 5
entity
  name "Crate"
  transform 0 1 0 0 0 0 1 1 1
  meshrenderer mesh "#cube" texture "Crate.png" ...
  spin 1.2
```

Editor에서 사용할 수 있는 Component:

| Component | 역할 |
|---|---|
| Name | Entity 표시 이름 |
| Transform | 위치, Euler 회전, 크기 |
| Mesh Renderer | Mesh와 Material |
| Camera | FOV, near/far plane |
| Light | Directional 또는 Point 조명 |
| Spin | Y축 회전 속도 |
| Active Camera | Play/Player가 사용할 카메라 tag |
| Environment | ambient, skybox, shadow 설정 |

Material은 albedo, specular, shininess, texture, normal map/strength,
opaque/alpha blend 설정을 가진다.

Player로 실행할 Scene에는 사용 가능한 **Active Camera**가 반드시 있어야 한다.
없으면 Player는 명확한 오류와 non-zero 종료 코드로 시작을 중단한다.

## 에셋 사용법

### 지원 구조

- Mesh: `Assets/` 아래 `.obj`
- Texture/normal map: `Assets/` 아래 `.png`
- Skybox: `Assets/Skyboxes/<이름>/` 아래 여섯 PNG
- Scene: `Assets/Scenes/` 아래 `.scene`

Skybox 폴더에는 다음 파일이 모두 필요하다.

```text
px.png  nx.png  py.png  ny.png  pz.png  nz.png
```

`#floor`, `#cube`, `#pyramid`, `#sphere`, `#torus`는 파일 없이 생성되는
procedural mesh 이름이다.

### Asset Browser

1. 새 에셋 파일을 `Engine/Assets/` 아래에 추가한다.
2. Editor를 다시 빌드하거나 실행 디렉터리의 `Assets/`에 복사한다.
3. **Assets > Refresh**를 누른다.
4. 목록에서 에셋을 선택해 로드와 정보를 확인한다.
5. Inspector의 **Assign mesh**, **Assign texture**, **Assign normal** 또는
   **Assign skybox**를 사용한다.

Mesh를 선택하고 **Place on click**을 켜면 Scene viewport의 바닥을 클릭해 새
Entity를 배치할 수 있다. 선택한 Texture가 있으면 함께 할당된다.

현재 빌드와 패키징은 `Assets/` 전체를 복사한다. 선택적 로컬 에셋이 많으면 패키지
크기가 커질 수 있으며, asset cooking/manifest 기반 선별은 아직 구현 범위 밖이다.

## Play 모드

상단 **Run > Play**로 시작하고 **Run > Edit**로 종료한다.

Play 진입 시 현재 World가 메모리에 직렬화된다. Play 중 Inspector 변경과 게임
시스템 변경은 허용되지만 Edit로 돌아가면 snapshot으로 복원되어 모두 사라진다.
파일로 자동 저장되지 않는다.

| 입력 | 동작 |
|---|---|
| 마우스 오른쪽 버튼 + 이동 | ActiveCamera 회전 |
| `W`, `A`, `S`, `D` | ActiveCamera 이동 |
| `Shift` | 빠른 이동 |
| `E` | 전방 10 unit 안의 가장 가까운 Spin 대상 상호작용 |
| `V` | VSync 전환 |

Play에서는 `E`가 상호작용 키이므로 Edit 모드의 수직 이동과 의미가 다르다.
Play 시간은 진입할 때마다 0에서 시작한다.

## Player 사용법

### 기본 Scene 실행

```powershell
.\Player.exe
```

기본 Scene은 빌드 설정의 `Assets/Scenes/Demo.scene`이다.

### Scene 지정

```powershell
.\Player.exe --scene Assets\Scenes\ShadowA.scene
```

상대 경로는 working directory가 아니라 `Player.exe`가 있는 runtime root를 기준으로
해석된다. 절대 경로도 지원하지만 배포 검증에는 package 내부 상대 경로를 사용한다.

알 수 없는 option, `--scene` 값 누락, Scene/asset load 실패, ActiveCamera 부재는
오류 메시지와 non-zero 종료 코드로 처리된다.

### Player 입력

| 입력 | 동작 |
|---|---|
| 마우스 오른쪽 버튼 + 이동 | ActiveCamera 회전 |
| `W`, `A`, `S`, `D` | 이동 |
| `Shift` | 빠른 이동 |
| `E` | Spin 대상 상호작용 |
| `M` | 1x/4x MSAA 전환 |
| `V` | VSync 전환 |
| `Esc` | 정상 종료 |

## 배포 방법

Release Player 빌드가 다음 staging 폴더를 매번 깨끗하게 다시 만든다.

```text
Output/x64/Release/PlayerPackage/
├─ Player.exe
├─ Assets/
├─ Shaders/
└─ package-manifest.txt
```

수동 배포는 **PlayerPackage 폴더의 내용 전체를 ZIP으로 압축**하면 된다.

```powershell
$package = '.\Output\x64\Release\PlayerPackage'
$zip = '.\Output\Dx12Engine-Player.zip'
Compress-Archive -Path "$package\*" -DestinationPath $zip -Force
Get-FileHash $zip -Algorithm SHA256
```

배포하지 않는 파일:

- `.pdb`, `.map`, `.lib`
- `.cpp`, `.h`, `.hlsl`
- `.slnx`, `.vcxproj`
- `imgui.ini`와 ImGui source

`package-manifest.txt`에는 configuration, platform, commit, clean/dirty 상태, 기본
Scene, DLL 정책, 파일 목록이 기록된다. 정식 배포 전에는 변경사항을 커밋하고 Release를
다시 빌드해 `source_state=clean`인지 확인하는 것을 권장한다.

현재 Release는 static CRT이며 Player가 직접 import하는 것은 Windows system DLL뿐이다.
패키지를 압축 해제한 뒤 `Player.exe`를 실행하면 된다.

## 테스트와 검증

### Engine 테스트

```powershell
.\Output\Tests\x64\Debug\EngineTests.exe
.\Output\Tests\x64\Release\EngineTests.exe
```

현재 25개 테스트가 다음 범위를 검증한다.

- EditorSession과 World 교체 초기화
- Edit/Play/Player 입력 및 카메라 경계
- Play/Stop snapshot과 transactional restore
- Scene v1~v5 호환성과 atomic save
- Player CLI와 Demo interaction
- 1x/4x presentation, resize, D3D12 Debug Layer
- executable-relative runtime path와 compiled shader cache

### 프로젝트와 링크 경계

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\Engine\Tests\VerifyPhase12ProjectBoundary.ps1 `
  -Configuration Release
```

### 저장소 밖 PlayerPackage 검증

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\Engine\Tests\VerifyPlayerPackage.ps1
```

검증 스크립트는 package만 시스템 임시 폴더로 복사하고 package 밖 working
directory에서 기본 Scene과 명시적 Scene을 실행한다. resize, E interaction,
1x/4x MSAA 전환, runtime root, 종료 코드 0을 확인한다.

## 문제 해결

### FXC를 찾지 못함

Windows SDK의 shader compiler가 설치되어 있는지 확인한다. 프로젝트는
`$(WindowsSdkDir)/bin/<SDK version>/x64/fxc.exe`를 사용한다.

### Player에서 shader 또는 asset 누락

`Player.exe`만 단독 복사하지 말고 `Assets/`와 `Shaders/`가 함께 있는
`PlayerPackage`를 사용한다. 경로와 대소문자도 Scene의 논리 이름과 일치해야 한다.

### Player가 ActiveCamera 오류로 종료

Scene의 카메라 Entity에 `Transform`, `Camera`, `Active Camera` Component를
추가하고 다시 저장한다.

### Scene을 읽을 수 없음

첫 줄의 version이 현재 지원 범위인 1~5인지 확인한다. 미래 버전은 추측해서 읽지
않고 거부한다. 파일은 UTF-8이어야 한다.

### Editor 입력이 동작하지 않음

Scene 패널 위에 커서를 두고, 텍스트 입력 등 ImGui가 키보드를 점유하고 있지 않은지
확인한다. 마우스 회전은 오른쪽 버튼을 누른 상태에서 수행한다.

### 4x MSAA를 선택할 수 없음

GPU와 format 조합에서 4x sample quality를 지원하지 않으면 자동으로 1x를 사용한다.
Frame 패널에 unsupported 상태가 표시된다.

### 패키지 manifest가 dirty

`source_state=dirty`는 빌드 당시 Git working tree에 미커밋 변경이 있었다는 뜻이다.
변경을 검토·커밋한 뒤 Release를 다시 빌드한다.

## Phase 12 구현 감사

2026-08-04 기준으로 계획, 코드, 프로젝트 그래프, 테스트를 다시 대조했다.

| 단계 | 구현 상태 | 확인 근거 |
|---|---|---|
| 12.0 | 완료 | stream serializer, EditorSession, transactional load tests |
| 12.1 | 완료 | FrameContext, PlaySession, EditorCamera, system 분류 tests |
| 12.2 | 완료 | EnterPlay/StopPlay snapshot 및 rollback tests |
| 12.3 | 완료 | Editor offscreen, Player direct/resolve presentation tests |
| 12.4 | 완료 | 네 프로젝트 분리와 Debug/Release link map 검사 |
| 12.5 | 완료 | FXC build pipeline, executable-relative runtime path tests |
| 12.6 | 완료 | Player CLI, Demo Scene, E interaction tests |
| 12.7 | 완료 | clean staging, manifest/binary audit, repo 밖 실행 검증 |

재검증 결과:

- Debug x64 전체 빌드: 경고 0, 오류 0
- Release x64 전체 빌드: 경고 0, 오류 0
- Debug 테스트: 25/25 통과
- Release 테스트: 25/25 통과
- Debug/Release 프로젝트·링크 경계 검사: 통과
- 저장소 밖 기본/지정 Scene Player 실행: 종료 코드 0
- Release package: 40 files, 51.60 MiB, 별도 runtime DLL 없음

필수 구현 누락은 발견되지 않았다. 다음 두 항목은 구현 결함이 아닌 릴리스 운영
상태로 남아 있다.

- 개발 도구가 없는 별도 PC에서의 수동 실행은 선택 검증이며 아직 결과가 기록되지 않음
- 현재 변경사항이 미커밋 상태이므로 `v1.3` Git tag는 아직 생성하지 않음

## 관련 문서

- [전체 ROADMAP](ROADMAP.md)
- [Phase 12 상세 계획](docs/Phase12-Plan.md)
- [실행 컨텍스트와 시스템 경계](Engine/Docs/Phase12.1-ExecutionBoundary.md)
- [presentation 경계](Engine/Docs/Phase12.3-PresentationBoundary.md)
- [프로젝트와 링크 경계](Engine/Docs/Phase12.4-ProjectBoundary.md)
- [runtime content](Engine/Docs/Phase12.5-RuntimeContent.md)
- [Player bootstrap](Engine/Docs/Phase12.6-PlayerBootstrap.md)
- [Player package](Engine/Docs/Phase12.7-PlayerPackage.md)
