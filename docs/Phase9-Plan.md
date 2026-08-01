# Phase 9 — 엔진 구조화 세부 계획 (v1.0 마일스톤)

Phase 1~8에서 "동작하는 코드"를 쌓았고, 반복 패턴이 충분히 드러났다.
이 문서는 그 코드를 "엔진"으로 재구성하는 작업을 **커밋 단위로 쪼갠 실행 계획**이다.
계획 문서이므로 코드는 없고, 설계 결정과 검증 방법만 담는다.

---

## 1. 현재 상태 진단

Phase 8 종료 시점 코드의 한계. 각 항목이 Phase 9의 세부 단계 하나와 대응된다.

| # | 문제 | 증상 | 해결 단계 |
|---|------|------|-----------|
| 1 | 매 프레임 `WaitForGpu()` 완전 플러시 | CPU와 GPU가 번갈아 놀음. 6ms 프레임 중 실제 작업은 일부 | 9.1 |
| 2 | Renderer가 파일 정적 전역 30여 개 | 수명·순서가 암묵적. 두 번째 창/디바이스 불가능 | 9.2 |
| 3 | 디스크립터를 "슬롯 0번, 1번" 하드코딩 | 텍스처가 2장만 돼도 수동 관리 지옥 | 9.3 |
| 4 | 리소스 캐싱 없음 | 같은 텍스처를 두 오브젝트가 쓰면 두 번 로드 | 9.4 |
| 5 | Scene이 하드코딩 struct 배열 | 오브젝트 종류마다 코드 수정. 런타임 추가/삭제 불가 | 9.5 |
| 6 | 런타임 파라미터 조정 불가 | 조명 값 하나 바꾸는 데 재빌드 | 9.6 |

**건드리지 않는 것** (이번 단계 범위 밖):
- 셰이더/조명 모델 (Phase 11에서)
- 멀티스레드 커맨드 기록, Bindless (Phase 12에서)
- 씬 직렬화 (Phase 10 맵 에디터에서)

---

## 2. 진행 원칙

1. **한 단계 = 완결된 커밋 1~2개.** 각 단계가 끝날 때마다 빌드·실행되고, 화면 결과가 이전과 동일(또는 명시된 개선)해야 한다.
2. **기능 변경과 구조 변경을 한 커밋에 섞지 않는다.** 예: 9.2에서 클래스로 옮길 때 로직은 그대로 복사한다.
3. **각 단계 끝에서 검증 루틴 실행** — 실행 → 화면 확인 → 카메라 이동 → 리사이즈 → 종료 코드 0 + Debug Layer 클린.
4. 순서는 아래 표를 따른다. 의존성 때문이지 중요도 순이 아니다 (ImGui가 디스크립터 할당자를 요구하는 식).

## 3. 단계 개요

| 단계 | 상태 | 주제 | 핵심 학습 | 규모(예상) | 태그 |
|------|------|------|-----------|-----------|------|
| 9.1 | **완료** | Frame Resources + 티어링 측정 | 프레임 파이프라이닝, fence 심화 | 중 | - |
| 9.2 | **완료** | Renderer 클래스화 + Engine/Game 분리 | 수명 관리, 소유권, 엔진/게임 경계 | 대 | - |
| 9.3 | 대기 | Descriptor 할당자 | 디스크립터 힙 운영 전략 | 소 | - |
| 9.4 | 대기 | 리소스 매니저 | 핸들 기반 자원 관리, 캐싱 | 중 | - |
| 9.5 | 대기 | ECS | 조합(composition) 기반 설계 | 대 | - |
| 9.6 | 대기 | ImGui 통합 | 외부 라이브러리 통합, 디버그 UI | 중 | v1.0 |

---

## 4. 단계별 상세

### 9.1 Frame Resources + 티어링 측정

**목표**: CPU가 프레임 N+1을 준비하는 동안 GPU가 프레임 N을 그리게 한다.
매 프레임 완전 플러시를 없애는, Phase 2 fence 학습의 완성편.

**작업 항목**
- [o] 측정 먼저: `ALLOW_TEARING` 지원 확인 → 스왑체인 플래그 → `Present(0, ALLOW_TEARING)` 토글(V키). 개선 전 CPU 프레임타임을 타이틀바로 기록 — **개선을 숫자로 확인하기 위한 사전 작업**
- [o] FrameResource 단위 정의: 프레임마다 커맨드 할당자 1개 + Object CB 슬롯들 + Pass CB 1개 + fence 값
- [o] 인플라이트 프레임 수 `kFramesInFlight = 2`로 링 순환
- [o] 프레임 시작: 이 FrameResource의 fence 값이 완료됐는지만 대기 (완전 플러시 아님)
- [o] `WaitForGpu()`는 리사이즈·종료·텍스처 업로드 전용으로 강등
- [o] 개선 후 프레임타임 재측정, 전후 비교 기록

**설계 결정**
- `kFramesInFlight = 2` 권장. 3은 처리량이 더 나오지만 입력 지연 1프레임 추가. 상수 하나로 바꿀 수 있게만 해 둔다.
- CB는 "프레임마다 독립된 업로드 버퍼" 방식 (거대한 링 버퍼 1개보다 단순하고 학습에 명확).
- 매핑 포인터는 프레임 리소스별로 유지 (Map 1회 원칙 유지).

**함정**
- fence 대기를 잘못 걸면 **에러 없이** GPU가 읽는 중인 CB를 덮어쓴다. Debug Layer도 못 잡는다 — 증상은 간헐적 화면 떨림. 검증: 인위적으로 `Sleep(50)`을 넣어 GPU를 뒤처지게 한 뒤에도 화면이 멀쩡한지 확인.
- 리사이즈는 여전히 **완전 플러시 후** 진행해야 한다.
- Pass CB도 프레임 수만큼 복제해야 한다는 걸 빼먹기 쉽다 (Object CB만 복제하는 실수).

**완료 기준**: 티어링 모드에서 FPS가 유의미하게 상승(수치 기록), vsync 모드에서 화면 결과 동일, `Sleep` 스트레스 테스트 통과.

#### 측정 결과 (완료)

Release 빌드, 1280x720, 티어링(uncapped). vsync 켜면 165fps에 고정되어 측정 불가.

| | fps | 프레임타임 |
|---|---|---|
| 개선 전 (매 프레임 완전 플러시) | 5009 ~ 5117 | 0.20 ms |
| 개선 후 (Frame Resources) | 5541 ~ 5685 | 0.18 ms |

**약 11% 개선.** 기대보다 작은데, 이유가 중요하다 — 이 씬은 **CPU 병목**이다.
창을 2544x1361로 키워도 프레임타임이 0.18ms 그대로였다. 픽셀을 4배 그려도
안 느려진다는 건 GPU가 놀면서 CPU를 기다리고 있다는 뜻이다.

Frame Resources의 이득은 **겹칠 GPU 작업이 얼마나 있느냐에 비례**한다.
오브젝트 13개·삼각형 5천 개인 지금은 겹칠 게 거의 없다.
Phase 11(그림자 = 멀티패스)에서 GPU 부하가 오르면 이 구조가 본격적으로 값을 한다.
**지금 하는 이유는 성능이 아니라, 나중에 되돌리기 어려운 구조라서다.**

검증: Debug 빌드(Debug Layer 활성)로 uncapped 8초 연속(약 26,000프레임) +
리사이즈 3회 후에도 화면 정상, 종료 코드 0.
계획에 적었던 `Sleep(50)` 방식은 폐기했다 — CPU를 재우면 GPU가 아니라 CPU가
뒤처져서 검증하려던 상황과 반대가 된다. 대신 uncapped 연속 실행으로
CPU가 2프레임 링을 계속 앞질러 돌게 해 fence 대기 경로를 실제로 태웠다.

---

### 9.2 Renderer 클래스화 + Engine/Game 분리

**목표**: 파일 정적 전역을 클래스 멤버로. `wWinMain`이 10줄 안팎이 되도록 Engine이 루프를 소유한다.

**작업 항목**
- [o] `GraphicsDevice`: 디바이스 + 큐 + fence (생성 순서가 생성자 안으로)
- [o] `SwapChain`: 스왑체인 + 백버퍼 RTV + 리사이즈 절차
- [o] `Renderer`: 위 둘을 소유. 기존 `Renderer::` 함수들이 메서드가 됨. FrameResource 배열도 여기로
- [o] `Window`: 창 생성 + WndProc + 입력 상태 (Main.cpp에서 분리)
- [o] `Engine`: Initialize → 루프(Tick) → Shutdown. Timer 소유
- [o] `Game`(`DemoGame`): Engine을 **상속**, `OnInit`/`OnUpdate`/`OnRender` 오버라이드
- [o] Main.cpp는 `DemoGame().Run()` 정도만 남긴다

**설계 결정**
- **상속 방식 권장** (`class DemoGame : public Engine`). 콜백(std::function) 방식은 게임이 여럿일 때 장점이 있지만 지금은 게임이 하나고, 가상 함수가 코드 추적이 쉽다. Phase 10 에디터도 같은 방식으로 얹을 수 있다.
- 래퍼는 **얇게**. D3D 호출을 숨기는 게 목적이 아니라 수명과 생성 순서를 명시하는 게 목적. `Get()`으로 원시 포인터 노출 허용.
- 복사 금지(`delete`), 이동도 일단 금지. 소유권 문제를 단순하게 유지.
- 파일 구조 제안 (필터도 동일하게):
  ```
  Source/
  ├─ Core/       Engine, Window, Timer
  ├─ Graphics/   Renderer, GraphicsDevice, SwapChain, FrameResource, Mesh, Image
  ├─ Loaders/    ObjLoader
  └─ Game/       Main, DemoGame, Scene, Camera
  ```

**함정**
- 소멸 순서: 멤버 선언 순서의 역순으로 파괴된다. 디바이스가 리소스보다 먼저 죽으면 안 되므로 **디바이스를 가장 위에 선언**.
- 이 단계는 파일 이동이 커서 diff가 크다. **로직 변경을 절대 섞지 말 것** — 옮기기 전후 화면이 픽셀 단위로 같아야 한다.

**완료 기준**: `wWinMain` 10줄 내외, 전역 변수 0개(익명 네임스페이스의 상수 제외), 화면 결과 이전과 동일.

#### 결과 (완료)

파일 8개 → **27개**, 4개 폴더로 재배치. `Source`를 include 경로에 추가해
`#include "Graphics/Mesh.h"` 형태로 통일 (상대경로 `../` 없음).

| 폴더 | 파일 | 비고 |
|---|---|---|
| `Core/` | Common, Timer, Window, Engine | D3D12를 모르는 건 Timer·Window뿐 |
| `Graphics/` | GraphicsDevice, SwapChain, FrameResource, Renderer, Mesh, Image | Renderer.cpp 510줄 |
| `Loaders/` | ObjLoader | |
| `Game/` | Main(40줄), DemoGame, Scene, Camera | |

- **가변 전역 0개.** 남은 건 익명 네임스페이스의 `constexpr` 상수와 자유 함수뿐
- 소멸 순서를 선언 순서로 고정: `GraphicsDevice` → `SwapChain` → 나머지.
  팩토리가 가장 먼저 선언되어 가장 나중에 파괴된다
- `Window`는 `GWLP_USERDATA`에 `this`를 심어 정적 `WndProcThunk`가
  인스턴스를 찾아간다. 키 입력은 `ConsumeKeyPress`로 엣지만 노출해
  Window가 Renderer를 몰라도 게임이 vsync를 토글할 수 있다
- `Engine`은 Scene/Camera를 모른다. `OnInit`/`OnUpdate`/`OnRender` 세 훅만
  두고 렌더 호출은 게임이 한다 — Phase 10 에디터가 같은 Renderer를
  재사용할 수 있는 경계

**검증**: 로직 무변경이 목표였으므로 화면이 이전과 동일한지 확인.
W 전진 59.8% 픽셀 변화, V 토글 3602fps, 리사이즈 884x781 정상, 종료 코드 0.
`wWinMain`은 COM 초기화와 try/catch 포함 약 25줄 — "10줄"에는 못 미치지만
본체는 `DemoGame game(...); game.Run(...)` 두 줄이다.

---

### 9.3 Descriptor 할당자

**목표**: "슬롯 몇 번" 하드코딩을 없애고, 힙에서 슬롯을 받아 쓰는 구조로.

**작업 항목**
- [ ] CPU 전용 힙(RTV/DSV)용: 프리 리스트 있는 단순 할당자 (Allocate/Free)
- [ ] 셰이더 가시 힙(CBV/SRV/UAV)용: 정적 구간(텍스처 SRV 등 수명 긴 것) + 필요 시 프레임 링 구간
- [ ] 기존 RTV/DSV/SRV 생성 코드를 할당자 경유로 교체
- [ ] ImGui가 쓸 슬롯 1개를 미리 확보할 수 있는 구조인지 확인 (9.6 대비)

**설계 결정**
- 처음부터 범용으로 만들지 않는다. `DescriptorAllocator(type, capacity)` 하나로 시작, 부족해지면 그때 확장.
- 핸들은 인덱스 + CPU/GPU 핸들 쌍을 담은 작은 struct로 반환.

**완료 기준**: `GetCPUDescriptorHandleForHeapStart().ptr + N * size` 패턴이 코드에서 사라짐. 화면 동일.

---

### 9.4 리소스 매니저

**목표**: 텍스처/메시/셰이더를 경로 기준으로 캐싱하고, 핸들로 참조한다.

**작업 항목**
- [ ] `ResourceManager`: `LoadTexture(path)` / `LoadMesh(path)` / `LoadShader(path, entry, target)` — 같은 경로 재요청 시 캐시 반환
- [ ] 반환은 **핸들(인덱스 기반)**. Scene의 `meshIndex`가 이미 이 방식이므로 자연스럽게 확장
- [ ] 텍스처 업로드 경로 정리: 업로드 힙 버퍼를 즉시 플러시하지 않고 모아서 한 번에 (선택 — 부담되면 현행 유지)
- [ ] Mesh/Texture 생성 코드가 Renderer 내부가 아닌 ResourceManager로 이동

**설계 결정**
- 핸들 = `{ uint32 index }` 얇은 타입. 세대(generation) 카운터는 **아직 넣지 않는다** — 이번 단계에서 리소스 해제를 구현하지 않기 때문 (로드만 있고 언로드 없음). 해제가 필요해지는 시점(Phase 10 에디터)에 세대를 추가.
- `shared_ptr` 기반 관리는 채택하지 않음: 소유자가 ResourceManager 하나뿐이라 참조 카운트가 무의미.

**완료 기준**: 같은 텍스처를 쓰는 오브젝트를 2개 만들어도 로드는 1회(로그로 확인). 화면 동일.

---

### 9.5 ECS

**목표**: `SceneObject` struct를 Entity + Component 조합으로 대체한다. ROADMAP의 결정사항(ECS 기반 씬 시스템)을 구현.

**작업 항목**
- [ ] `Entity`: 32비트 ID + 세대(generation). 파괴된 ID 재사용 시 stale 참조 감지
- [ ] 컴포넌트 저장소: 컴포넌트 타입별 컨테이너, Add/Get/Remove/순회
- [ ] 기본 Component 4종 (데이터만, 로직 없음):
  - `Transform` — position/rotation/scale, 월드 행렬 계산은 시스템에서
  - `MeshRenderer` — 메시 핸들 + 머티리얼
  - `CameraComponent` — FOV, near/far (현재 Camera struct 흡수)
  - `Light` — 종류(방향광/점광) + 색 + 파라미터 (현재 Pass CB 하드코딩 값을 데이터로)
- [ ] 기본 System 3종 (로직만, 데이터 없음):
  - `SpinSystem` — 기존 spinSpeed 데모 회전 (Transform 순회)
  - `CameraSystem` — 입력 → 카메라 Transform 갱신
  - `RenderSystem` — MeshRenderer+Transform 순회 → Renderer에 그리기 요청 목록 전달
- [ ] `BuildScene`을 Entity 생성 코드로 재작성
- [ ] 조명도 Entity가 됐으므로 Renderer의 조명 하드코딩 제거 (Light 컴포넌트에서 Pass CB 구성)

**설계 결정**
- **직접 구현, 최소 기능** (EnTT 등 라이브러리 금지 — 학습 목적). 저장소는 "타입별 dense 배열 + entity→index 맵" 수준이면 충분. 아키타입/쿼리 캐싱/이벤트는 만들지 않는다.
- 순회 성능은 이번 단계 목표가 아니다. 오브젝트 30개다.
- Renderer는 ECS를 **모르게 유지**: RenderSystem이 "그릴 것 목록(메시 핸들, 월드 행렬, 머티리얼)"을 평평한 배열로 만들어 넘긴다. 이 경계가 Phase 10 에디터에서 렌더러 재사용의 기반이 된다.

**함정**
- 템플릿 과설계 경계. `GetComponent<T>()` 한 층이면 충분하고, 그 이상(컴포넌트 시그니처 비트마스크, 쿼리 DSL)은 이번 범위에서 금지.
- kMaxObjects=32 제한이 이제 실제 제약이 된다 — Object CB 슬롯 수를 엔티티 수와 연동하도록 조정.

**완료 기준**: ROADMAP 완료 기준 그대로 — "Entity에 Component를 조합해 오브젝트를 추가하는 코드가 몇 줄로 끝난다". 기존 씬(바닥+큐브 8+벽+피라미드 2+구+토러스+조명)이 전부 Entity로 재현되어 화면 동일.

---

### 9.6 ImGui 통합

**목표**: 디버그 UI로 씬을 실시간 조작한다. v1.0 마일스톤 마감.

**작업 항목**
- [ ] ImGui 소스를 `ThirdParty/imgui/`에 벤더링 (서브모듈 대신 소스 복사 — 빌드 단순)
- [ ] `imgui_impl_win32` + `imgui_impl_dx12` 백엔드 연결
- [ ] WndProc에 ImGui 핸들러 체인 (입력을 ImGui가 먼저 소비 — 마우스 캡처 충돌 처리)
- [ ] 셰이더 가시 힙에서 ImGui 폰트용 슬롯 할당 (9.3의 할당자 사용)
- [ ] 디버그 패널: 엔티티 목록 → 선택 → Transform/머티리얼/조명 값 편집 (9.5의 ECS 순회가 UI 코드가 됨)
- [ ] FPS/프레임타임 그래프 (타이틀바 표시를 UI로 이전)

**설계 결정**
- **docking 브랜치 권장**: Phase 10 맵 에디터가 도킹·다중 뷰포트를 쓰게 되므로 처음부터 docking으로. (기본 브랜치로 시작했다가 갈아타는 비용이 더 크다)
- ImGui 렌더 패스는 씬 렌더 후 같은 커맨드 리스트에 이어서 기록.
- 조작 충돌 규칙: ImGui 창 위에서는 우클릭 시점 회전 비활성 (`WantCaptureMouse`).

**함정**
- Frame Resources와의 상호작용: ImGui 백엔드도 인플라이트 프레임 수를 알아야 한다 (`NumFramesInFlight` 파라미터 일치).
- 한글 폰트는 넣지 않는다 (글리프 로딩 복잡도). UI 텍스트는 영문.

**완료 기준**: ROADMAP 완료 기준 — "ImGui로 씬을 실시간 조작". 조명 색을 슬라이더로 바꾸면 즉시 반영, 엔티티 위치 편집 가능. **여기서 `v1.0` 태그.**

---

## 5. 마일스톤 완료 체크리스트 (v1.0)

- [ ] `wWinMain`이 10줄 내외
- [o] 매 프레임 완전 플러시 없음 (프레임타임 개선 수치 기록됨 — 9.1)
- [ ] 전역 변수 없음 — 모든 D3D 객체가 클래스 소유
- [ ] Entity 추가가 몇 줄로 끝남
- [ ] ImGui로 조명/Transform 실시간 편집
- [ ] Debug Layer 경고·에러 0, 종료 코드 0
- [ ] Debug/Release 모두 빌드·실행
- [ ] docs/에 배운 것 정리 (프레임 파이프라이닝 타임라인 그림 권장)

## 6. Phase 10과의 연결

이 구조화가 끝나면 맵 에디터에 필요한 것이 전부 준비된다:

- **오프스크린 렌더 타겟** ← 9.3 할당자가 RTV/SRV 슬롯을 유연하게 내줌
- **에셋 브라우저** ← 9.4 리소스 매니저의 캐시 목록이 곧 브라우저 데이터
- **Inspector** ← 9.6 디버그 패널이 원형
- **씬 저장/로드** ← 9.5 Entity+Component가 직렬화 단위
