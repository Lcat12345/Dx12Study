# Phase 12 — 플레이 모드 & 게임 런타임 세부 계획 (v1.3)

Phase 9~11에서 만든 Engine/Game 경계, 씬 직렬화, 에디터, 멀티패스 렌더러가
이번 단계의 재료다. 하지만 현재 하나뿐인 `Engine.exe`는 게임 실행 파일이 아니라
에디터와 게임 코드가 한 프로세스에 섞인 개발 프로그램이다.

이 문서는 [ROADMAP Phase 12](../ROADMAP.md)의 요구사항을 **커밋 단위로 쪼갠
실행 계획**이다. 목표는 Play 버튼을 하나 추가하는 데 그치지 않는다.
`Engine.lib + Game.lib + Editor.exe + Player.exe`라는 실제 빌드 경계를 만들고,
Player 패키지만 다른 폴더에 복사해 실행되는 상태까지 포함한다.

---

## 1. 현재 상태 진단

Phase 11 종료(v1.2) 시점 코드의 한계. 각 항목은 아래 세부 단계와 대응된다.

| # | 문제 | 현재 증상 | 해결 단계 |
|---|------|-----------|-----------|
| 1 | `DemoGame`이 에디터와 게임 역할을 동시에 가짐 | UI, 편집 입력, Spin, 렌더 호출이 한 `OnUpdate`에 섞임 | 12.1, 12.4 |
| 2 | 시스템 실행 분류가 없음 | Spin과 LightOrbit가 Edit 중에도 계속 진행 | 12.1 |
| 3 | 편집 카메라가 World의 ActiveCamera임 | 에디터 이동이 씬 데이터 자체를 바꾸고 저장됨 | 12.1 |
| 4 | 플레이 전용 시간이 없음 | `TotalSeconds()`가 Stop 이후에도 이어져 재진입 시 상태가 점프할 수 있음 | 12.1 |
| 5 | 씬 직렬화가 파일 경로에 결합됨 | Play 진입 스냅샷에 임시 파일이 필요함 | 12.0 |
| 6 | 선택 Entity와 편집 명령이 파일 전역 상태임 | World 복원 뒤 우연히 다른 Entity를 가리킬 수 있음 | 12.0, 12.2 |
| 7 | Engine이 ImGui를 무조건 초기화함 | Player가 Renderer를 링크하면 ImGui도 따라옴 | 12.3 |
| 8 | Renderer가 offscreen+overlay만 지원 | Player용 swap-chain 직접 presentation이 없음 | 12.3 |
| 9 | 프로젝트가 실행 파일 하나뿐임 | 코드 실행 조건을 꺼도 배포 바이너리에서 Editor 코드가 사라지지 않음 | 12.4 |
| 10 | 리소스 위치가 `Dx12Engine.slnx` 탐색에 의존 | 저장소 밖으로 exe를 복사하면 시작 단계에서 실패 | 12.5 |
| 11 | 셰이더를 런타임에 `.hlsl`에서 컴파일 | 배포 패키지에 소스와 개발용 컴파일 경로가 필요함 | 12.5 |
| 12 | `lpCmdLine`을 무시함 | Player 시작 Scene을 지정할 방법이 없음 | 12.6 |
| 13 | 패키징 산출물과 독립 실행 검증이 없음 | 빌드 성공이 곧 배포 성공이라는 잘못된 확신이 생김 | 12.7 |

### 현재 결합 지점

- `Core/Engine.cpp`가 Renderer 생성 직후 `InitializeOverlay()`를 호출하고,
  Window 메시지 훅을 ImGui에 직접 연결한다.
- `Graphics/Renderer.h`가 `ImGuiLayer.h`를 include하고 `unique_ptr<ImGuiLayer>`를
  멤버로 소유한다.
- `Graphics/Renderer::Render()`의 마지막 단계가 항상 `DrawOverlayPass()`다.
- `Game/DemoGame.cpp`가 Editor viewport hover, ImGui keyboard capture, 게임 시스템,
  Debug UI를 모두 처리한다.
- `Game/DebugUI.cpp`의 선택, 씬 경로, 편집 명령 큐가 파일 전역 상태다.
- `Core/Common.cpp`의 `GetProjectRoot()`는 exe 위에서 솔루션 파일을 찾는다.
- `ResourceManager::LoadShader()`는 실행 중 `D3DCompileFromFile()`을 호출한다.

이 목록은 단순한 정리 대상이 아니다. Player 프로젝트에서 ImGui를 빼고 저장소 없는
폴더에서 실행하려면 **반드시 끊어야 하는 의존성**이다.

### 목표 빌드 그래프

```text
Engine.lib
    ▲
    │
Game.lib
  ▲   ▲
  │   │
  │   └──────── Player.exe
  │                └─ Editor/ImGui 의존성 없음
  │
Editor.exe
  └─ Editor-only 코드 + ImGui
```

의존 방향은 아래 한 방향만 허용한다.

```text
Editor.exe ─┬─> Game.lib ─> Engine.lib
            └─> ImGui

Player.exe ───> Game.lib ─> Engine.lib
```

`Engine.lib`가 Game이나 Editor 헤더를 include하거나, `Game.lib`가 Editor/ImGui를
include하면 구조가 역전된 것이다. 실행 중 `if (isEditor)`로 코드를 건너뛰는 것은
링크 분리가 아니므로 해결로 인정하지 않는다.

---

## 2. 범위와 진행 원칙

### 이번 단계에 포함

- Always / Editor-only / Play-only 실행 컨텍스트
- World 밖의 EditorCamera와 World 안의 ActiveCamera 분리
- 파일과 메모리가 공유하는 씬 직렬화 코어
- Play 진입 스냅샷과 Stop 복원
- Editor offscreen / Player swap-chain presentation 경로
- Renderer와 ImGui의 컴파일·링크 분리
- `Engine.lib + Game.lib + Editor.exe + Player.exe` 프로젝트 구조
- 실행 파일 기준 리소스 경로
- 빌드 시 HLSL → CSO 컴파일과 런타임 bytecode 로드
- Player 시작 Scene 지정
- 작은 플레이 가능한 데모 규칙
- Player staging 폴더 생성과 저장소 독립 실행 검증

### 이번 단계에서 하지 않는 것

- DLL 기반 플러그인 또는 게임 코드 hot reload
- 에셋 압축·암호화·pak 파일·본격적인 asset cooking
- Job System과 비동기 로딩
- 고정 timestep 물리 루프, 네트워크 동기화, replay
- Undo/Redo와 Play 중 변경 사항을 Edit World에 선택적으로 반영하는 기능
- 다중 Scene additive loading
- 설치 프로그램 제작과 코드 서명
- DXC 전환. Phase 12의 컴파일된 셰이더는 현재 Shader Model 5.0/FXC 계약을
  유지하고, DXC는 ROADMAP Phase 13에 남긴다

### 진행 원칙

1. **한 단계 = 완결된 커밋 1~2개.** 구조 이동과 동작 변경은 가능한 한 다른
   커밋으로 분리한다.
2. **Player의 부재와 Player의 불완전함을 구분한다.** 12.4에서 프로젝트가 생기고,
   12.5~12.7에서 배포 가능한 실행 파일이 된다.
3. **실행 조건과 링크 경계를 각각 검증한다.** Edit에서 Play-only 시스템이 멈추는
   것과 Player에 Editor 코드가 들어가지 않는 것은 서로 다른 테스트다.
4. **직렬화 포맷은 하나만 둔다.** 파일 저장과 메모리 스냅샷이 각자 별도 포맷을
   가지면 둘 중 하나가 반드시 뒤처진다.
5. **scene pass는 공유하고 presentation만 갈라진다.** shadow → opaque → skybox →
   transparent 순서를 Editor와 Player가 복사해 갖지 않는다.
6. **Player는 저장소 fallback을 사용하지 않는다.** 개발 편의 fallback은 Editor나
   명시적인 개발 설정에서만 허용한다.
7. **각 단계 끝에서 Debug/Release x64 빌드와 실행을 확인한다.** 12.4 이후에는
   Editor와 Player를 각각 검사한다.
8. **패키지는 빌드 출력의 일부다.** 파일을 손으로 복사해서 한 번 성공한 것은
   재현 가능한 배포 절차로 인정하지 않는다.

---

## 3. 실행 모드와 소유권 계약

### 실행 모드별 동작

| 호스트 | 상태 | 카메라 | 시스템 | 출력 | ImGui |
|--------|------|--------|--------|------|-------|
| Editor | Edit | World 밖 `EditorCamera` | Always + Editor-only | offscreen → ImGui viewport | 포함 |
| Editor | Play | World의 `ActiveCamera` | Always + Play-only | offscreen → ImGui viewport | 포함 |
| Player | Running | World의 `ActiveCamera` | Always + Play-only | swap-chain | 미포함 |

### 시스템 분류

**Always**는 호스트와 모드에 관계없이 필요한 순수 런타임 처리다.

- 렌더 데이터 평탄화(`BuildRenderData`)
- 활성 Environment/Light 읽기
- 씬의 유효성 유지에 필요한 최소 처리

**Editor-only**는 편집 경험을 위한 코드이며 Player 프로젝트에 들어가지 않는다.

- EditorCamera 입력
- Entity 선택, 배치, Inspector, Asset Browser
- Scene viewport hover와 ImGui capture 판단
- File > New/Open/Save와 Play/Stop UI

**Play-only**는 게임 상태를 바꾸는 시스템이며 Edit 중에는 실행하지 않는다.

- 게임 카메라 이동과 게임 입력
- Spin과 LightOrbit
- 데모 상호작용 규칙

현재 규모에서는 범용 scheduler나 등록 매크로를 만들지 않는다. 아래처럼 명시적인
호출 묶음이면 분류와 순서를 충분히 읽을 수 있다.

```cpp
UpdateAlways(world, context);
if (mode == RunMode::Edit)
    UpdateEditor(editorSession, world, context);
else
    UpdatePlay(world, playSession, context);
```

Player는 `UpdateEditor`를 호출하지 않는 정도가 아니라 해당 함수가 들어 있는
Editor 프로젝트를 참조하지 않는다.

### 정적 라이브러리별 소유권

| 대상 | 포함할 코드 | 포함하면 안 되는 코드 |
|------|-------------|-----------------------|
| Engine | Window, Timer, World/ECS, GraphicsDevice, Renderer, ResourceManager, loaders | Components, 게임 규칙, DebugUI, ImGui |
| Game | Components, Scene 포맷, Always/Play-only systems, BuildWorld, 데모 규칙 | Window 생성, DebugUI, AssetBrowser, ImGui |
| Editor | EditorApp, EditorCamera, EditorSession, DebugUI, AssetBrowser, editor picking, ImGui backend | Player 진입점 |
| Player | PlayerApp, CLI/시작 Scene, Player 진입점 | DebugUI, AssetBrowser, ImGui 소스·헤더 |

`Scene`은 Game의 Component 형식을 직렬화하므로 `Game.lib`에 둔다. `World` 자체는
Component 타입을 모르는 ECS 컨테이너이므로 `Engine.lib`에 남는다.

---

## 4. 단계 개요

| 단계 | 상태 | 주제 | 핵심 학습 | 규모(예상) | 태그 |
|------|------|------|-----------|-----------|------|
| 12.0 | 완료 | 메모리 씬 직렬화 + EditorSession | 영속 상태와 임시 상태, stream 경계 | 중 | - |
| 12.1 | 완료 | 실행 컨텍스트 + 카메라/시스템 3분류 | 모드별 업데이트, 시간·입력 소유권 | 대 | - |
| 12.2 | 완료 | Play/Stop 스냅샷 | transactional restore, 시뮬레이션 수명 | 중 | - |
| 12.3 | 대기 | presentation 분리 + ImGui 결합 제거 | 렌더와 표시의 분리, callback 경계 | 대 | - |
| 12.4 | 대기 | Engine/Game/Editor/Player 프로젝트 분리 | 정적 라이브러리, 링크 단위 의존성 | 대 | - |
| 12.5 | 대기 | 컴파일된 셰이더 + 런타임 리소스 경로 | build-time content pipeline, 배포 root | 대 | - |
| 12.6 | 대기 | Player 시작 Scene + 데모 게임 | CLI, runtime bootstrap, 상호작용 | 중 | - |
| 12.7 | 대기 | staging 패키지 + 독립 실행 검증 | 재현 가능한 배포, binary audit | 중 | v1.3 |

의존성 때문에 순서를 바꾸지 않는다. 특히 12.3의 ImGui 결합 제거 없이 12.4에서
프로젝트부터 쪼개면 `Engine.lib`의 Renderer object file이 ImGui 심볼을 참조해
Player 링크에 다시 끌고 들어온다.

---

## 5. 단계별 상세

### 12.0 메모리 씬 직렬화 + EditorSession

**목표**: 디스크 저장과 Play 스냅샷이 같은 직렬화 코어를 사용하게 하고, World가
교체될 때 함께 정리해야 할 에디터 임시 상태를 명시적인 객체로 묶는다.

#### 작업 항목

- [x] `SerializeScene(std::ostream&, World&, const ResourceManager&, error)` 추가
- [x] `DeserializeScene(std::istream&, ResourceManager&, World&, error)` 추가
- [x] 기존 `SaveScene(path)` / `LoadScene(path)`는 파일을 연 뒤 위 코어를 호출하는
      얇은 wrapper로 변경
- [x] 메모리 snapshot용 string/stream wrapper 추가
- [x] 파서는 계속 임시 World에 적재하고 마지막에만 대상 World와 교체
- [x] 파일 저장의 sibling `.tmp` + atomic replace 정책 유지
- [x] `EditorSession` 도입: 선택 Entity, scene path/status, popup 상태, 편집 명령 큐,
      placement 관련 임시 상태 중 World 수명과 함께 초기화할 항목 소유
- [x] `DebugUI.cpp`의 가변 파일 전역 상태를 `EditorSession&`를 통해 접근하도록 이동
- [x] `EditorSession::OnWorldReplaced()` 또는 동등한 단일 초기화 경계 정의
- [x] Scene version은 데이터 필드가 바뀌지 않으므로 5 유지

#### 설계 결정

- 메모리 snapshot은 **바이너리 `World` 복사**가 아니라 기존 텍스트 scene 포맷을
  사용한다. Component에 `std::vector`나 비트 패딩이 생겨도 raw memory layout에
  묶이지 않고, 파일 저장과 같은 검증 경로를 탄다.
- ResourceManager는 snapshot 대상이 아니다. Mesh/Texture handle은 직렬화 시 논리
  이름으로 바뀌며, 복원 시 같은 캐시에서 다시 resolve한다.
- `EditorSession`은 씬 데이터가 아니므로 저장하지 않는다. 다만 현재 scene path는
  Stop 후에도 유지해야 하고, 선택/명령 큐는 World 교체 시 버려야 한다. 객체 안에서
  필드별 reset 정책을 명시한다.
- 직렬화 코어는 경로를 모른다. 경로 생성과 대화상자는 Editor 책임이다.

#### 함정

- `std::ostringstream`와 파일 stream의 locale/precision이 달라지면 같은 World가
  다른 bytes를 만들 수 있다. float precision과 locale은 직렬화 코어 한 곳에서
  설정한다.
- Play snapshot 생성 중 에셋 이름 resolve가 실패하면 Play로 들어가면 안 된다.
- Load 실패 뒤 ResourceManager 캐시에 일부 에셋이 추가될 수는 있지만 live World는
  절대 반쯤 바뀌면 안 된다.
- World 교체 직전에 쌓인 Destroy/Place 명령을 남기면 복원된 World에 적용된다.
  명령 큐는 반드시 World와 함께 epoch가 바뀌는 Editor 임시 상태로 취급한다.

#### 검증

1. 같은 World를 파일과 메모리에 직렬화했을 때 본문 bytes가 동일하다.
2. v1~v5 fixture를 stream 경로로 읽어도 기존 기본값 정책이 유지된다.
3. 잘못된 메모리 snapshot을 읽었을 때 기존 World가 그대로 남는다.
4. 저장→메모리 복원→저장 결과가 동일하다.
5. World 교체 뒤 선택 Entity와 대기 명령은 비고, scene path 정책은 의도대로 남는다.

**완료 기준**: 파일과 메모리 경로가 하나의 parser/writer를 사용하고,
`DebugUI.cpp`에 World 수명과 결합된 가변 전역 상태가 남지 않는다.

---

### 12.1 실행 컨텍스트 + 카메라/시스템 3분류

**목표**: Edit와 Play가 같은 `OnUpdate` 안에서 우연히 섞이지 않도록 시간, 입력,
카메라, 시스템 호출 순서를 명시한다.

#### 작업 항목

- [x] Editor 상태 `RunMode { Edit, Play }` 정의
- [x] `FrameContext`: frame dt, host input, 렌더에 공통인 프레임 정보
- [x] `PlaySession`: play elapsed time, 필요한 input edge/state 소유
- [x] Always / Editor-only / Play-only 함수 묶음 정의
- [x] `SpinSystem`, `LightOrbitSystem`, 게임 카메라 입력을 Play-only로 이동
- [x] `LightOrbitSystem`이 Engine 전체 `TotalSeconds()` 대신 play elapsed time 사용
- [x] World 밖 `EditorCamera` 도입
- [x] Edit 렌더와 뷰포트 picking이 `EditorCamera`에서 같은 `CameraView`를 사용
- [x] Play 렌더는 World의 `ActiveCamera` 사용
- [x] Player용 입력 컨텍스트는 viewport hover나 ImGui capture 개념을 갖지 않음
- [x] Editor-only 구현 파일 목록과 향후 Editor 프로젝트 소유권을 문서화

#### 설계 결정

- EditorCamera는 Component가 아니다. 저장·복원되지 않고 Scene을 바꿔도 편집자의
  시점으로 유지된다.
- 첫 실행 시 기존 ActiveCamera 값을 EditorCamera 초기값으로 복사해 Phase 11 화면과
  사용자 경험을 보존할 수 있다. 이후 두 카메라는 독립적이다.
- Play 시작 시 `playElapsed = 0`; 프레임마다 Play 상태에서만 증가한다.
- 시스템 순서는 작은 명시적 함수로 유지한다. 아직 범용 scheduler, priority,
  dependency graph를 만들 이유가 없다.
- 입력은 Window의 raw state를 바로 읽는 대신 host가 `InputContext`로 평탄화한다.
  Editor는 ImGui/viewport 조건을 반영해 만들고 Player는 창 전체 입력으로 만든다.

#### 함정

- Edit 중 게임 카메라 Transform을 움직이면 Play 시작 화면이 사용자 모르게 바뀐다.
- Editor 렌더는 EditorCamera인데 picking이 ActiveCamera를 쓰면 클릭 위치가 어긋난다.
- Stop 뒤 재Play에서 `TotalSeconds()`를 계속 쓰면 공전 light가 즉시 다른 위치로 튄다.
- keyboard edge를 Edit에서 소비하고 Play에 재사용하면 Play 첫 프레임에 키가 유실되거나
  반대로 이전 입력이 재생될 수 있다.

#### 검증

1. Edit에서 10초 기다려도 Spin과 LightOrbit의 Scene Transform이 바뀌지 않는다.
2. EditorCamera를 움직여 저장해도 scene file의 ActiveCamera Transform은 변하지 않는다.
3. Play에서는 ActiveCamera가 화면을 갖고 WASD/mouse 입력에 반응한다.
4. Stop 후 5초 기다렸다 재Play하면 공전 물체가 매번 같은 초기 위치에서 시작한다.
5. Edit viewport picking ray와 실제 EditorCamera 화면이 일치한다.

**완료 기준**: 세 시스템 분류와 두 카메라가 코드 호출 경계로 드러나며,
Edit 중에는 Scene의 게임 상태가 진행되지 않는다.

구현 파일의 향후 프로젝트 소유권은
[`Engine/Docs/Phase12.1-ExecutionBoundary.md`](../Engine/Docs/Phase12.1-ExecutionBoundary.md)에
기록했다.

---

### 12.2 Play/Stop 스냅샷

**목표**: Editor 안에서 현재 Scene을 임시 게임 인스턴스로 실행하고, Stop 시 Play
중 변경을 모두 버린 원래 Edit World로 돌아간다.

#### 상태 전이

```text
Edit
  │ Play: serialize World → snapshot, playElapsed = 0
  ▼
Play
  │ Stop: deserialize snapshot → temporary World → swap
  ▼
Edit
```

#### 작업 항목

- [x] `EnterPlay()`에서 현재 World를 메모리 snapshot으로 직렬화
- [x] snapshot 실패 시 World와 mode를 건드리지 않고 오류 표시
- [x] Play 전환과 동시에 play elapsed/input edge 초기화
- [x] Play 중 Inspector 편집 허용 여부 확정. 기본 사양은 허용하되 Stop 시 폐기
- [x] `StopPlay()`에서 snapshot을 임시 World로 읽고 성공 시 교체
- [x] 복원 실패 시 현재 Play World를 보존하고 오류를 보여 데이터 손실 방지
- [x] 복원 후 `EditorSession::OnWorldReplaced()` 호출
- [x] File > New/Open은 Play 중 비활성화하거나 먼저 Stop할지 정책 고정
- [x] 창 종료 시 Play snapshot을 자동으로 저장 파일에 쓰지 않음
- [x] toolbar/menu에 Play/Stop 상태와 오류 표시

#### 설계 결정

- Play는 Editor World의 별도 복사본을 두 개 동시에 유지하지 않는다. 진입 시 snapshot
  문자열 하나를 보관하고 live World 자체를 시뮬레이션한다. Stop 때 snapshot으로
  새 World를 만든다.
- Play 중 변경은 Stop과 함께 사라진다. “Apply Changes”는 Undo/Redo와 component diff가
  필요한 별도 에디터 기능이므로 이번 범위가 아니다.
- scene path는 편집 문서의 정체성이므로 Play/Stop으로 바뀌지 않는다.
- snapshot은 메모리에만 있고 autosave가 아니다. crash recovery와 혼동하지 않는다.
- ActiveCamera가 없는 Scene은 Play 진입을 거부하지 않고 마지막 유효 `CameraView`를
  유지한다. Player에서도 같은 fallback을 쓸지는 12.6에서 최종 확정한다.

#### 함정

- `Entity{index,generation}`은 World 교체 뒤 의미가 없다. 같은 숫자가 살아 있어도 같은
  논리 Entity라는 보장이 없으므로 선택을 유효성 검사만 해서는 부족하다.
- Play 중 Save를 허용하면 사용자가 임시 상태를 영구 상태로 오해할 수 있다. 이번 단계는
  Save/Save As를 비활성화하고 명확한 tooltip을 제공하는 쪽을 권장한다.
- ActiveCamera 조회 실패 때 미초기화 Camera로 덮어쓰면 안 된다. Editor host는 마지막
  유효 Camera를 보존하고, Stop 성공 시 즉시 EditorCamera로 되돌린다.

#### 검증

1. Entity 위치·재질·Component를 Play 중 바꾼 뒤 Stop하면 시작 전 값으로 돌아온다.
2. Play 중 생성/삭제한 Entity가 Stop 뒤 각각 사라지거나 복원된다.
3. Play→Stop을 100회 반복해 Entity 수, scene bytes, resource cache가 안정적이다.
4. Stop 뒤 선택과 queued edit가 복원된 World에 오적용되지 않는다.
5. Play 중 저장 메뉴와 Scene 교체 정책이 UI에 명확히 드러난다.

**완료 기준**: Play는 게임 상태를 실제로 진행하고, Stop은 디스크 I/O 없이 시작 직전
Scene을 정확히 복원한다.

구현은 `EditorSession::EnterPlay()`/`StopPlay()`가 snapshot과 mode를 함께 소유하며,
`DemoGame`은 전환 성공 뒤에만 해당 모드의 Camera를 선택한다. 자동 테스트는 snapshot
실패 원자성, Entity/Component/선택/대기 명령 복원, 100회 반복 안정성을 검증한다.

---

### 12.3 presentation 분리 + ImGui 결합 제거

**목표**: shadow/opaque/skybox/transparent scene pass는 공유하면서 최종 출력만
offscreen 또는 swap-chain으로 선택하고, Engine/Renderer가 ImGui 타입을 모르게 한다.

#### 작업 항목

- [ ] `Renderer.h`에서 `ImGuiLayer.h` include와 `unique_ptr<ImGuiLayer>` 제거
- [ ] `Renderer::InitializeOverlay`, `Overlay`, ImGui 전용 메시지 처리 제거
- [ ] `Engine` 생성자와 loop에서 ImGui 초기화/NewFrame 호출 제거
- [ ] ImGui frame 시작과 Window message hook 등록을 Editor host로 이동
- [ ] Editor가 소유하는 `ImGuiLayer` 또는 `EditorOverlay` 작성
- [ ] scene pass 기록과 presentation 기록을 함수 경계로 분리
- [ ] generic overlay recording callback 또는 동등한 ImGui 비의존 확장 지점 제공
- [ ] Editor offscreen target 경로 유지: scene resolve texture를 ImGui SRV로 전달
- [ ] Player 1x 경로: current back buffer를 scene colour target으로 직접 사용
- [ ] Player 4x 경로: multisample colour/depth에 렌더한 뒤 current back buffer로 resolve
- [ ] swap-chain resize 시 Player depth/MSAA attachment를 함께 재생성
- [ ] Editor panel resize는 기존 offscreen colour/depth/resolve만 재생성
- [ ] 두 presentation 모두 같은 `BuildDrawQueues`, pass constants, PSO, pass 순서 사용

#### API 방향

구체적인 이름은 구현 중 조정할 수 있지만 책임은 아래처럼 보여야 한다.

```cpp
enum class SceneOutput
{
    OffscreenTexture,
    SwapChain
};

renderer.RenderFrame(camera, lighting, items, output, overlayRecorder);
```

`overlayRecorder`가 필요하다면 타입은 `ID3D12GraphicsCommandList*`를 받는 일반 callback
수준으로 두고, Engine 쪽에는 ImGui 타입이나 함수 이름이 나타나지 않게 한다.

#### 리소스 상태 계약

Editor 1x:

```text
offscreen colour: PIXEL_SHADER_RESOURCE → RENDER_TARGET
                 → PIXEL_SHADER_RESOURCE
back buffer:      PRESENT → RENDER_TARGET(UI) → PRESENT
```

Editor 4x:

```text
MSAA colour:      RESOLVE_SOURCE → RENDER_TARGET → RESOLVE_SOURCE
resolve texture:  PIXEL_SHADER_RESOURCE → RESOLVE_DEST
                 → PIXEL_SHADER_RESOURCE
back buffer:      PRESENT → RENDER_TARGET(UI) → PRESENT
```

Player 1x:

```text
back buffer:      PRESENT → RENDER_TARGET(scene) → PRESENT
```

Player 4x:

```text
MSAA colour:      RESOLVE_SOURCE → RENDER_TARGET → RESOLVE_SOURCE
back buffer:      PRESENT → RESOLVE_DEST → PRESENT
```

#### 설계 결정

- “Player가 백버퍼에 직접 그린다”는 1x 경로를 뜻한다. multisample back buffer는
  flip-model swap chain과 맞지 않으므로 4x는 별도 MSAA surface가 필요하다.
- scene pass 함수는 output target의 RTV/DSV, viewport/scissor, sample desc만 받고
  Editor/Player를 분기하지 않는다.
- ImGui가 사용하는 descriptor allocator는 generic Engine 서비스로 노출할 수 있지만,
  ImGui descriptor 할당 코드는 Editor에 둔다.
- Editor overlay가 없더라도 Renderer가 정상 Present할 수 있어야 한다.

#### 함정

- 4x resolve destination인 back buffer는 `RESOLVE_DEST`여야 하며, 기존 overlay 경로의
  `RENDER_TARGET` 전이를 그대로 복사하면 Debug Layer 오류가 난다.
- scene colour/depth/PSO의 sample count와 quality는 항상 일치해야 한다.
- Player 1x에서 offscreen target을 거쳤다가 fullscreen copy하는 구현은 결과는 보이지만
  ROADMAP의 direct presentation 학습 목표를 충족하지 않는다.
- Renderer object file에 ImGui 함수 호출 하나라도 남으면 static library 링크 시 Player에
  해당 object와 미해결 ImGui 심볼이 함께 들어올 수 있다.

#### 검증

1. Editor 화면, picking, panel resize, 1x/4x 전환이 Phase 11과 동일하다.
2. overlay callback이 없는 경로도 1x/4x에서 정상 Present한다.
3. 같은 Camera/Scene의 Editor offscreen과 swap-chain 결과가 UI 영역을 제외하고 같다.
4. 각 경로의 resize 및 1x↔4x 반복 전환 후 Debug Layer 메시지가 0이다.
5. `Engine`과 `Renderer` public header에서 `imgui` 문자열과 include가 사라진다.

**완료 기준**: Renderer는 ImGui 없이 한 프레임을 완성할 수 있고, Editor와 Player
presentation이 scene pass 코드를 복제하지 않는다.

---

### 12.4 Engine/Game/Editor/Player 프로젝트 분리

**목표**: 논리적으로 분리한 코드를 Visual Studio 프로젝트와 링크 산출물 수준에서도
분리한다.

#### 목표 디렉터리 예시

```text
Dx12Engine/
├─ Engine/
│  ├─ Engine.vcxproj          # StaticLibrary → Engine.lib
│  └─ Source/                 # Core, Graphics, Loaders
├─ Game/
│  ├─ Game.vcxproj            # StaticLibrary → Game.lib
│  └─ Source/                 # Components, Scene, Systems, DemoRules
├─ Editor/
│  ├─ Editor.vcxproj          # Application → Editor.exe
│  └─ Source/                 # EditorApp, DebugUI, AssetBrowser, ImGuiLayer, Main
├─ Player/
│  ├─ Player.vcxproj          # Application → Player.exe
│  └─ Source/                 # PlayerApp, command line, Main
├─ ThirdParty/imgui/          # Editor.vcxproj만 컴파일
├─ Assets/
├─ Shaders/
└─ Dx12Engine.slnx
```

현재 `Assets/`와 `Shaders/`가 Engine 프로젝트 아래에 있으므로 실제 이동 여부는 12.5의
패키징 경로와 함께 결정한다. 중요한 것은 물리 폴더 이름보다 **프로젝트 item과 참조
방향**이다.

#### 작업 항목

- [ ] 기존 `Engine.vcxproj`를 `ConfigurationType=StaticLibrary`로 전환
- [ ] `Game.vcxproj` 정적 라이브러리 프로젝트 생성
- [ ] `Editor.vcxproj`, `Player.vcxproj` Windows Application 프로젝트 생성
- [ ] `Dx12Engine.slnx`에 네 프로젝트와 참조 관계 등록
- [ ] Game source를 Engine project item에서 제거하고 Game project item으로 이동
- [ ] Editor-only source와 ImGui source를 Editor project item으로 이동
- [ ] 현재 `DemoGame`을 `EditorApp`과 공유 Game/runtime 코드로 분해
- [ ] Player는 자체 `wWinMain`과 `PlayerApp`을 가짐
- [ ] 네 프로젝트의 include directory와 preprocessor 정책 정리
- [ ] Runtime Library(`/MDd`/`/MD` 또는 선택한 정책)를 프로젝트 간 일치
- [ ] Debug/Release x64 output과 intermediate 디렉터리를 프로젝트별로 분리
- [ ] x64를 Phase 12 공식 배포 target으로 고정. Win32/x86 구성은 일관되게 지원하거나
      솔루션에서 제거해 유령 configuration을 남기지 않음
- [ ] Editor만 `ThirdParty/imgui` include/source를 가짐
- [ ] Player project reference가 Game과 Engine 두 개뿐인지 확인

#### 링크 경계 검증

- Player project 파일에 `imgui`, `DebugUI`, `AssetBrowser`, `EditorApp` 경로가 없다.
- Engine/Game public header를 include하는 최소 Player translation unit이 ImGui include
  directory 없이 컴파일된다.
- Player link map 또는 symbol scan에 `ImGui::`, `ImGui_Impl`, `DebugUI` 심볼이 없다.
- Player 단독 project build가 Editor project를 빌드하지 않는다.
- Editor와 Player가 각각 자기 `wWinMain` 하나만 가진다.

#### 함정

- static library는 사용된 object file만 최종 링크에 들어간다. 하지만 Renderer.cpp가
  ImGui 호출을 품고 있으면 Renderer를 사용하는 순간 같은 object 전체가 들어오므로
  12.3이 먼저 끝나야 한다.
- 서로 다른 CRT 설정은 `LNK2038 RuntimeLibrary mismatch` 또는 경계를 넘는 할당/해제
  문제를 만든다.
- Game public header가 Editor header를 include하면 Player source가 직접 ImGui를 쓰지
  않아도 include path와 컴파일 의존성이 생긴다.
- 소스 이동과 동작 변경을 한 커밋에 섞으면 regression 원인을 찾기 어렵다. 먼저 파일을
  이동해 동일 동작을 확인하고, 다음 커밋에서 bootstrap을 바꾼다.

#### 검증

1. 솔루션 전체 Debug/Release x64 빌드: 네 target 모두 경고 0, 오류 0.
2. Editor 실행 결과가 12.3과 동일하다.
3. Player 단독 rebuild가 Editor와 ImGui 소스를 컴파일하지 않는다.
4. Player link map에 Editor/ImGui 심볼이 없다.
5. Engine.lib와 Game.lib가 각각 생성되고 두 exe가 같은 라이브러리를 참조한다.

**완료 기준**: `Editor.exe`와 `Player.exe`가 별도 진입점으로 빌드되고,
Player의 compile/link graph에 Editor-only 코드와 ImGui가 없다.

---

### 12.5 컴파일된 셰이더 + 실행 파일 기준 리소스 경로

**목표**: Player가 소스 저장소와 런타임 HLSL 컴파일 없이 exe 옆의 배포 리소스만
읽도록 바꾼다.

#### 런타임 디렉터리 계약

```text
Player/
├─ Player.exe
├─ Assets/
│  ├─ Scenes/
│  ├─ Skyboxes/
│  └─ ...
├─ Shaders/
│  ├─ Basic.VS.cso
│  ├─ Basic.PS.cso
│  ├─ Skybox.VS.cso
│  ├─ Skybox.PS.cso
│  └─ ShadowDepth.VS.cso
└─ *.dll                    # 필요한 비시스템 DLL이 있을 때만
```

#### 작업 항목

- [ ] `GetExecutableDir()` 구현: `GetModuleFileNameW` 길이/오류 처리 포함
- [ ] `RuntimePaths` 또는 동등한 값 객체: root, assetDir, shaderDir
- [ ] ResourceManager 생성 시 asset/shader root를 주입
- [ ] Scene/AssetBrowser/Renderer의 전역 `GetAssetDir`/`GetShaderDir` 호출 제거
- [ ] Player는 `exeDir`를 유일한 runtime root로 사용
- [ ] Editor는 exe 옆 리소스를 우선하고, 명시적인 개발 설정에서만 repo fallback 허용
- [ ] HLSL entry point별 build-time compile 규칙 추가
- [ ] Debug/Release 설정별 FXC flag와 출력 `.cso` 이름 고정
- [ ] `D3DCompileFromFile` 기반 `LoadShader`를 compiled bytecode loader로 교체
- [ ] shader cache key를 `.cso` 논리 경로로 변경
- [ ] build가 `.hlsl` 변경을 추적해 필요한 `.cso`만 다시 생성하는지 확인
- [ ] `.cso`를 Player output의 `Shaders/`로 자동 복사
- [ ] Assets를 Player output의 `Assets/`로 자동 복사할 item/target 정의
- [ ] 필요한 비시스템 DLL 목록을 명시하고 없으면 빈 목록이라고 기록

#### 셰이더 컴파일 계약

현재 Shader Model 계약을 유지한다.

| 출력 | 원본 | Entry | Target |
|------|------|-------|--------|
| `Basic.VS.cso` | `Basic.hlsl` | `VSMain` | `vs_5_0` |
| `Basic.PS.cso` | `Basic.hlsl` | `PSMain` | `ps_5_0` |
| `Skybox.VS.cso` | `Skybox.hlsl` | `VSMain` | `vs_5_0` |
| `Skybox.PS.cso` | `Skybox.hlsl` | `PSMain` | `ps_5_0` |
| `ShadowDepth.VS.cso` | `ShadowDepth.hlsl` | `VSMain` | `vs_5_0` |

Debug는 symbol/debug flag, Release는 최적화 flag를 사용하되 파일 이름과 runtime lookup은
같게 유지한다. DXC/Shader Model 6 전환은 여기 섞지 않는다.

#### 설계 결정

- runtime path는 전역 탐색 함수보다 생성자 주입을 우선한다. 테스트와 Editor/Player의
  다른 root 정책을 같은 ResourceManager로 표현할 수 있다.
- Player에서 repo fallback을 제공하지 않는다. fallback이 있으면 staging에 파일이 빠져도
  개발 머신에서는 성공해 배포 결함이 숨는다.
- Assets는 논리 이름과 상대 구조를 유지한다. scene file에는 계속 상대 논리 이름만
  저장하고 절대 경로를 넣지 않는다.
- Release CRT는 `/MT`로 묶거나 `/MD` redistributable을 staging하는 정책 중 하나를
  명시적으로 선택한다. “개발 PC에 있으니 됨”은 허용하지 않는다.

#### 함정

- current working directory를 root로 쓰면 Visual Studio, Explorer, 터미널에서 실행할 때
  각각 다른 결과가 난다. 기준은 항상 exe 위치다.
- HLSL 하나에 여러 entry point가 있으므로 source 파일 하나당 output 하나라는 단순
  build item으로는 부족하다.
- Debug용 shader와 Release용 shader output이 같은 중간 경로를 쓰면 병렬 빌드에서
  서로 덮어쓸 수 있다.
- Editor fallback이 조용히 발동하면 패키지 누락을 발견하지 못한다. 로그/상태에 실제
  선택된 runtime root를 표시한다.

#### 검증

1. HLSL을 수정하면 대응 `.cso` timestamp와 내용만 갱신된다.
2. package에서 `.hlsl`을 제거해도 Editor와 Player의 runtime shader 생성이 성공한다.
3. 실행 working directory를 다른 폴더로 바꿔도 같은 Assets/Shader를 연다.
4. Player 폴더에서 `.cso` 하나를 제거하면 파일 이름과 runtime root가 포함된 오류를 낸다.
5. Player 경로 코드가 `Dx12Engine.slnx`, source tree, 개발 머신 절대 경로를 찾지 않는다.

**완료 기준**: Player가 exe 옆 Assets와 compiled Shaders만 읽고, HLSL source compile과
저장소 탐색 없이 기본 Scene의 모든 Phase 11 pass를 생성한다.

---

### 12.6 Player 시작 Scene + 작은 데모 게임

**목표**: Player가 지정된 Scene을 로드해 게임 시스템만 실행하고, 사용자가 실제로
이동하고 상호작용할 수 있게 한다.

#### Player bootstrap

- [ ] `CommandLineToArgvW` 또는 동등한 wide-character argument parser 사용
- [ ] `--scene <path>` 지원
- [ ] 상대 경로는 exe runtime root 기준으로 resolve
- [ ] 인자가 없으면 패키지 기본 Scene(예: `Assets/Scenes/Demo.scene`) 사용
- [ ] 알 수 없는 option, 빠진 값, Scene load 실패 시 명확한 메시지와 non-zero exit
- [ ] Scene에 ActiveCamera가 없으면 시작 실패. EditorCamera fallback 금지
- [ ] Player는 Always + Play-only 시스템만 호출
- [ ] Player input은 창 전체를 대상으로 하며 ImGui capture/viewport hover 조건 없음
- [ ] ESC/창 닫기로 정상 종료 코드 0
- [ ] Player title/log에 시작 Scene과 runtime root 표시

#### 데모 게임 규칙

학습 범위를 유지하기 위해 물리나 새 참조 시스템을 만들지 않는다. 권장 데모는 다음과
같다.

- WASD + mouse로 ActiveCamera 이동
- 카메라 전방의 제한 거리 ray로 AABB 대상 탐색
- E를 누르면 가장 가까운 `Spin` 대상의 speed를 0과 기본값 사이에서 토글
- 화면의 회전 상태가 바뀌어 상호작용 결과를 즉시 확인

기존 AABB/ray와 Spin Component를 재사용하므로 새 물리 시스템이나 Entity reference
serialization이 필요 없다. 구체적인 대상 이름이나 배치는 `Demo.scene`에 둔다.

#### 설계 결정

- `--scene` 절대 경로는 개발 편의를 위해 허용할 수 있지만, v1.3 배포 검증은 반드시
  package 안의 상대 경로로 수행한다.
- 상호작용 입력은 key edge다. 키를 누르고 있는 동안 매 프레임 켜짐/꺼짐을 반복하면
  결과가 무작위처럼 보인다.
- Player는 Scene 저장 기능을 제공하지 않는다.
- Editor Play와 Player는 같은 Game system 함수를 호출한다. 두 구현을 따로 만들지 않는다.

#### 함정

- 현재 `PickEntity`는 camera far plane까지 후보를 찾는다. 게임 상호작용은 별도의 최대
  거리 제한이 필요하다.
- Player 시작 직후 이전 Editor input edge가 존재할 수는 없지만, Editor Play 진입에는
  전환 버튼을 누른 입력이 게임 입력으로 새어 들어가지 않게 해야 한다.
- 기본 Scene 경로가 code와 package target 두 곳에서 다르면 한쪽만 갱신된다. 하나의
  build property 또는 manifest에서 공유한다.

#### 검증

1. 인자 없음 → 기본 Demo.scene 로드.
2. `--scene Assets/Scenes/ShadowA.scene` → 지정 Scene 로드.
3. 잘못된 option, 누락 파일, 미래 scene version 각각 non-zero 종료와 명확한 오류.
4. WASD/mouse 이동과 E 상호작용이 Editor Play와 Player에서 동일하다.
5. Edit에서는 같은 Spin 대상이 정지해 있고 Play에서만 움직인다.
6. Player 1x/4x 양쪽에서 skybox, normal map, shadow, transparent pass가 유지된다.

**완료 기준**: 같은 Scene을 Editor Play와 Player에서 실행해 동일한 게임 규칙으로
이동·상호작용할 수 있다.

---

### 12.7 staging 패키지 + 저장소 독립 실행 검증

**목표**: “내 개발 폴더에서 실행됨”이 아니라, 자동 생성한 Player 패키지만으로
저장소가 없는 환경에서 실행됨을 증명한다.

#### staging 산출물

권장 출력은 configuration별 독립 디렉터리다.

```text
Output/x64/Release/PlayerPackage/
├─ Player.exe
├─ Assets/
│  ├─ Scenes/Demo.scene
│  ├─ Skyboxes/...
│  └─ ...
├─ Shaders/*.cso
├─ *.dll
└─ package-manifest.txt
```

`package-manifest.txt`에는 최소한 build configuration, commit, 기본 Scene, 파일 목록을
기록한다. 재현에 필요하지 않은 timestamp를 넣어 매 빌드 diff를 만들지는 않는다.

#### 작업 항목

- [ ] Player Release build 뒤 staging을 만드는 MSBuild target 또는 명시적 build script
- [ ] 이전 staging의 stale file이 남지 않는 clean/recreate 정책
- [ ] `Player.exe`, Assets, `.cso`, 필요한 DLL, 기본 Scene 자동 복사
- [ ] package manifest 생성
- [ ] 금지 파일 검사: `.slnx`, `.vcxproj`, `.cpp`, `.h`, `.hlsl`, ImGui ini/source
- [ ] Player link map/symbol 검사로 Editor/ImGui 부재 재확인
- [ ] 새 빈 임시 폴더에 staging 내용만 복사
- [ ] working directory를 package 밖으로 둔 상태에서 Player 실행
- [ ] repo 경로를 탐색하지 않았음을 runtime path log로 확인
- [ ] 기본 Scene과 `--scene` 지정 Scene 각각 실행
- [ ] 1x/4x, resize, 입력, 상호작용, 종료 코드 검증
- [ ] 가능하면 Visual Studio와 저장소가 없는 다른 PC에서 같은 zip/package 실행
- [ ] 결과와 package 크기, 포함 DLL 정책을 계획서의 결과 섹션에 기록

#### 독립성 검증 절차

1. Release x64 전체 rebuild.
2. `PlayerPackage` 생성.
3. manifest와 금지 파일 검사.
4. package를 새 빈 폴더에 복사.
5. 저장소와 무관한 working directory에서 `Player.exe` 실행.
6. 기본 Scene 로드, 플레이, 상호작용, resize, 정상 종료.
7. 지정 Scene 인자로 재실행.
8. 로그에 package 내부 root 외 개발 경로가 없는지 확인.

다른 PC 검증을 할 수 없더라도 새 빈 폴더 테스트는 필수다. 반대로 다른 PC에서
성공했더라도 손으로 복사한 파일 조합이면 자동 staging 검증을 대체하지 못한다.

#### 함정

- `Copy if newer`만 쓰면 원본에서 삭제된 파일이 staging에 영원히 남을 수 있다.
  clean package를 기준으로 하거나 manifest와 실제 파일을 대조한다.
- 개발 머신에 설치된 VC runtime, PIX/RenderDoc layer, 환경 변수에 우연히 의존할 수 있다.
- repo fallback이 남아 있으면 빈 폴더를 repo 아래에 만들었을 때 테스트가 거짓 성공한다.
  package는 저장소 바깥 경로에 둔다.
- DLL을 무조건 복사하지 않는다. Windows system DLL은 패키징 대상이 아니며,
  재배포 가능한 비시스템 dependency만 명시적으로 포함한다.

#### 검증 기록 형식

| 항목 | 결과 | 근거 |
|------|------|------|
| Release x64 build | 대기 | 경고/오류 수 |
| Player ImGui 미링크 | 대기 | project graph + link map |
| package 금지 파일 | 대기 | 검사 결과 |
| 기본 Scene 시작 | 대기 | 실행 로그/화면 |
| 지정 Scene 시작 | 대기 | 실행 인자/로그 |
| repo 밖 실행 | 대기 | package 경로 + runtime root |
| 다른 PC 실행 | 선택 | OS/GPU/종료 코드 |

**완료 기준**: `Player.exe`와 runtime resource만 들어 있는 package를 새 빈 폴더 또는
다른 PC로 복사한 뒤, 솔루션·소스·저장소 구조·개발 머신 절대 경로 없이 정상 시작하고
플레이하며 종료한다. **여기서 `v1.3` 태그.**

---

## 6. 단계별 커밋 경계

권장 커밋은 기능을 추적할 수 있는 최소 단위다.

| 순서 | 권장 커밋 | 포함하지 않을 것 |
|------|-----------|------------------|
| 1 | `refactor: scene serializer를 stream 기반으로 분리` | Play UI |
| 2 | `refactor: editor session이 임시 상태를 소유` | 시스템 동작 변경 |
| 3 | `refactor: editor camera와 실행 컨텍스트 분리` | 프로젝트 이동 |
| 4 | `feat: editor play stop snapshot` | Player 프로젝트 |
| 5 | `refactor: renderer presentation과 overlay 분리` | 파일 대규모 이동 |
| 6 | `refactor: Engine Game Editor Player 프로젝트 분리` | 리소스 정책 변경 |
| 7 | `build: shader 사전 컴파일과 runtime path 도입` | 데모 규칙 |
| 8 | `feat: player scene bootstrap과 데모 상호작용` | 패키지 검증 문서 |
| 9 | `build: player staging과 독립 배포 검증` | Phase 13 기능 |

실제 diff 크기에 따라 한 단계를 두 커밋으로 나눌 수 있지만, 뒤 단계 기능을 앞 단계
구조 커밋에 섞지 않는다.

---

## 7. 트리거 재판정 체크포인트

Phase 12는 실행 파일과 배포 경계를 만들지만, 아래 주제를 자동으로 끌어들이지 않는다.

| 시점 | 확인할 항목 | 발화 조건 | 발화 시 처리 |
|------|-------------|-----------|---------------|
| 12.0 snapshot 측정 후 | binary scene format | text serialize/parse가 Play 전환을 체감 가능하게 막음 | 별도 versioned binary snapshot 검토. 디스크 포맷은 유지 |
| 12.1 시스템 분류 후 | 범용 scheduler | 시스템 수와 순서 조건이 명시적 함수로 관리되지 않음 | Phase 13 Job/System scheduler로 이동 |
| 12.3 presentation 후 | render graph | pass/resource state 분기가 두 경로에서 반복되고 누락됨 | Phase 13 render graph 후보로 승격 |
| 12.5 asset copy 후 | asset manifest/cooking | 원본 Assets 전체 복사가 패키지 크기나 누락 관리에 실제 문제 | 별도 asset build pipeline 설계 |
| 12.6 Demo.scene 후 | Entity reference | 상호작용 규칙이 다른 Entity를 안정적으로 참조해야 함 | scene-stable ID와 reference serialization 설계 |
| 12.7 package 측정 후 | installer/patcher | 수동 zip 전달이 실제 배포를 방해함 | Phase 13 도구 항목으로 등록 |

비동기 로딩, dynamic DLL game module, hot reload는 Player 프로젝트가 생겼다는 이유만으로
필요해지지 않는다. 실제 로딩 정지나 반복 개발 비용이 측정될 때 별도 단계로 다룬다.

---

## 8. 마일스톤 완료 체크리스트 (v1.3)

### 실행 의미

- [ ] Edit / Play / Player 세 모드의 카메라와 시스템 표가 실제 코드와 일치
- [ ] Edit 중 Spin, LightOrbit, 게임 입력이 Scene 상태를 변경하지 않음
- [ ] EditorCamera가 Scene 저장 데이터 밖에 존재
- [ ] Play 진입마다 play elapsed time이 0부터 시작
- [ ] Play 중 변경이 Stop과 함께 모두 폐기
- [ ] Stop 복원 뒤 선택 Entity와 pending editor command가 안전하게 초기화
- [ ] 파일 저장과 Play snapshot이 같은 scene serializer 사용

### 렌더링

- [ ] Editor는 offscreen Scene texture를 ImGui viewport에 표시
- [ ] Player 1x는 back buffer에 직접 scene 렌더
- [ ] Player 4x는 MSAA colour/depth에서 back buffer로 resolve
- [ ] shadow → opaque → skybox → transparent 순서가 Editor/Player에서 공유
- [ ] resize와 1x↔4x 전환 후 Debug Layer 경고·오류 0
- [ ] Phase 11 skybox, normal mapping, shadow, transparency가 Player에서도 유지

### 빌드·링크 경계

- [ ] `Engine.lib`, `Game.lib`, `Editor.exe`, `Player.exe` 네 산출물 생성
- [ ] Engine은 Game/Editor/ImGui를 모름
- [ ] Game은 Editor/ImGui를 모름
- [ ] Player project가 Engine + Game에만 의존
- [ ] Player link map/symbol에 Editor-only와 ImGui가 없음
- [ ] Player 단독 rebuild가 Editor/ImGui를 컴파일하지 않음
- [ ] Debug/Release x64 전체 빌드 경고 0, 오류 0

### 리소스·배포

- [ ] Player runtime root가 exe 위치 기준
- [ ] Player가 `Dx12Engine.slnx`와 개발 저장소를 탐색하지 않음
- [ ] 모든 runtime shader가 build-time compiled `.cso`
- [ ] package에 HLSL/source/project 파일이 없음
- [ ] Assets, compiled Shaders, 필요한 DLL, 기본 Scene이 자동 staging됨
- [ ] `--scene`과 기본 Scene 시작 모두 정상
- [ ] 새 빈 저장소 밖 폴더에서 package만으로 실행·플레이·종료 코드 0
- [ ] 가능하면 개발 도구가 없는 다른 PC에서 같은 package 검증

ROADMAP 완료 기준인 다음 문장을 그대로 만족해야 한다.

> `Player.exe`와 런타임 리소스만 새 빈 폴더 또는 다른 PC로 복사한 뒤,
> 솔루션·소스·저장소 구조·개발 머신 절대 경로 없이 시작 Scene을 정상 로드해
> 플레이할 수 있다. Player 바이너리에는 Editor-only 코드와 ImGui가 링크되지 않는다.

---

## 9. Phase 13과의 연결

- `Engine.lib`와 `Game.lib` 경계는 이후 Job System, plugin, test executable을 붙일 기반이다.
  하지만 Phase 12에서는 정적 링크만 다룬다.
- compiled shader pipeline은 이후 DXC/Shader Model 6 전환 지점이 된다. 파일 이름과
  logical shader ID를 유지하면 compiler만 교체할 수 있다.
- executable-relative `RuntimePaths`는 이후 asset manifest, pak, DLC mount point로
  확장할 수 있지만 현재는 폴더 구조 하나만 지원한다.
- Player direct presentation은 post-processing을 붙일 때 다시 intermediate target을
  요구할 수 있다. 그때는 “Player는 무조건 back buffer direct”를 고집하지 않고 최종
  presentation 계약을 유지한다.
- Play snapshot은 Undo/Redo나 Apply Play Changes가 아니다. 그 기능들은 stable Entity ID와
  component diff가 준비된 뒤 별도 에디터 단계로 다룬다.
- 데모 상호작용이 Entity 간 참조를 요구하기 시작하면 scene-stable ID가 발화한다.

Phase 12의 핵심 결과는 기능 하나가 아니라 **개발 프로그램과 배포 게임의 경계가
빌드 산출물로 증명되는 것**이다. Editor에서 버튼을 눌렀을 때 게임처럼 보이는 것만으로는
완료가 아니며, 저장소 밖의 Player package가 같은 Scene을 독립적으로 실행해야 한다.
