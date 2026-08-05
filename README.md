# Dx12Engine

Win32와 DirectX 12로 바닥부터 작성한 3D 게임 엔진. ECS 기반 런타임, ImGui
Scene Editor, 그리고 **에디터 없이 단독 실행되는 Player**로 구성된다.

에디터에서 만든 Scene을 그대로 Player로 플레이할 수 있고, `Player.exe`와
런타임 리소스만 복사한 폴더에서도 — 저장소·솔루션·소스·개발 경로 없이 —
동작한다.

```
Engine.lib   Win32 루프, ECS, 렌더러, 리소스        Game·Editor·ImGui를 모름
Game.lib     Component, Scene 포맷, 게임 시스템     Engine에만 의존
Editor.exe   편집 UI, 선택/Inspector, Play/Stop     Engine + Game + ImGui
Player.exe   Scene 로드, 게임 실행, 화면 출력       Editor·ImGui 링크 금지
```

이 경계는 규칙이 아니라 **빌드 실패로 강제된다.** 패키징 단계가 링크 결과와
프로젝트 구성을 검사해서, Editor 전용 소스가 Player에 닿으면 빌드가 멈춘다.

## 기능

- ECS World와 Component 조합, Scene 저장/불러오기 (하위 버전 호환)
- Edit/Play 전환과 Play 상태 롤백 — 플레이 중 편집은 Stop과 함께 사라진다
- Blinn-Phong 조명, 큐브맵 skybox, normal mapping (탄젠트 생성 포함)
- Directional shadow map + PCF, alpha blend, 1x/4x MSAA
- OBJ 로더(멀티 머티리얼 submesh), PNG 텍스처, 절차 메시
- Asset Browser와 뷰포트 클릭 배치·선택

## 빌드

**요구 환경**: Windows 10/11 x64, Visual Studio C++ Desktop 워크로드,
Windows SDK(`fxc.exe` 포함), DirectX 12 GPU.

```powershell
msbuild .\Dx12Engine.slnx /m /p:Configuration=Release /p:Platform=x64
```

Visual Studio에서는 `Dx12Engine.slnx`를 열고 시작 프로젝트로 `Editor` 또는
`Player`를 지정한다.

## 실행

```text
Output/x64/Release/Editor/Editor.exe          에디터
Output/x64/Release/Player/Player.exe          Player (개발 빌드)
Output/x64/Release/PlayerPackage/Player.exe   배포용 패키지
```

Release 빌드는 `PlayerPackage/`를 자동으로 만든다. 이 폴더를 통째로 복사하면
다른 PC에서도 그대로 실행된다.

```powershell
.\Player.exe                                  # 기본 Scene
.\Player.exe --scene Assets\Scenes\Demo.scene # Scene 지정
```

## 테스트

```powershell
.\Output\Tests\x64\Release\EngineTests.exe     # 기능·회귀 테스트
.\Engine\Tests\VerifyPlayerPackage.ps1         # 저장소 밖 실행 검증
```

## 문서

| 문서 | 내용 |
|---|---|
| [WIKI.md](WIKI.md) | 사용설명서 — 에디터 조작, Scene·에셋, 배포, 문제 해결 |
| [ROADMAP.md](ROADMAP.md) | 개발 순서와 각 단계의 완료 상태 |
| [docs/](docs/) | Phase별 상세 설계·검증 기록 |
| [Engine/Docs/](Engine/Docs/) | 경계 계약 문서 (실행·프로젝트·패키지) |
