# Dx12Engine 개발 로드맵

DirectX 12 + C++ 기반 게임 엔진을 **단계적으로 직접 구현하며 학습**하기 위한 개발 순서 문서.

## 진행 원칙

1. **매 단계는 "실행되는 결과물"로 끝난다** — 코드만 쌓지 않고, 단계마다 화면으로 확인한다.
2. **추상화는 필요해질 때 한다** — 처음부터 엔진 구조를 잡지 않는다. 동작하는 코드를 먼저 만들고, 반복이 보일 때 클래스로 묶는다 (Phase 9에서 본격 구조화).
3. **커밋은 작게, 단계 완료 시 태그** — 기능 하나 = 커밋 하나. 단계 완료 시 `v0.x` 태그를 붙여 언제든 그 시점으로 돌아가 복습할 수 있게 한다.
4. **막히면 샘플 비교** — [DirectX-Graphics-Samples](https://github.com/microsoft/DirectX-Graphics-Samples)의 HelloWorld 시리즈와 항상 비교하며 진행한다.

## 전체 단계 요약

| 단계 | 주제 | 결과물 | 태그 |
|------|------|--------|------|
| Phase 0 | 프로젝트 셋업 | 빌드되는 빈 프로젝트 + 저장소 | v0.1 |
| Phase 1 | Win32 창 생성 | 검은 창이 뜨고 닫힌다 | v0.2 |
| Phase 2 | DX12 초기화 | 지정한 색으로 화면 클리어 | v0.3 |
| Phase 3 | 첫 삼각형 | 삼각형 렌더링 | v0.4 |
| Phase 4 | 버퍼와 3D 변환 | 회전하는 3D 큐브 | v0.5 |
| Phase 5 | 텍스처 | 텍스처 입힌 큐브 | v0.6 |
| Phase 6 | 카메라 & 입력 & 시간 | WASD/마우스로 돌아다니기 | v0.7 |
| Phase 7 | 조명 | Blinn-Phong 라이팅 | v0.8 |
| Phase 8 | 모델 로딩 | 외부 3D 모델 렌더링 | v0.9 |
| Phase 9 | 엔진 구조화 | Engine/Game 분리, ECS 기반 씬 시스템 | v1.0 |
| Phase 10 | 맵 에디터 | ImGui 기반 에디터로 씬 배치/편집 | v1.1 |
| Phase 11 | 중급 렌더링 | 스카이박스, 그림자, 노멀 매핑 | v1.2+ |
| Phase 12 | 플레이 모드 & 게임 런타임 | 에디터 없이 단독 실행되는 게임 | v1.3 |
| Phase 13 | 고급 주제 (선택) | 인스턴싱, 포스트 프로세싱, PBR 등 | - |

---

## Phase 0 — 프로젝트 셋업

**목표**: 개발 환경과 저장소를 준비하고, 빈 프로젝트가 빌드되는 것을 확인한다.

- [o] Visual Studio 2026 + "C++를 사용한 데스크톱 개발" 워크로드, 최신 Windows SDK 확인
- [o] Win32 빈 프로젝트 생성 (`Dx12Engine.sln`)
- [o] `git init` + `.gitignore` 작성 (`.vs/`, `x64/`, `out/`, `*.user` 등 제외)
- [o] GitHub 원격 저장소 연결, 첫 커밋/푸시
- [o] 폴더 구조 잡기 (처음엔 단순하게 시작)

```
Dx12Engine/                  # 저장소 루트
├─ Dx12Engine.slnx           # 솔루션
├─ Engine/                   # 엔진 프로젝트
│  ├─ Engine.vcxproj
│  ├─ Source/                # .cpp / .h
│  ├─ Shaders/               # .hlsl (Phase 3~)
│  └─ Assets/                # 텍스처, 모델 (Phase 5~)
├─ docs/                     # 학습 노트
└─ ROADMAP.md
```

**완료 기준**: 빈 `WinMain`이 빌드·실행되고, GitHub에 첫 커밋이 올라가 있다.

---

## Phase 1 — Win32 창 생성

**목표**: DirectX 이전에, 윈도우가 뜨는 원리(창 클래스, 메시지 루프)를 이해한다.

- [o] `WNDCLASSEX` 등록, `CreateWindow`로 창 생성
- [o] 윈도우 프로시저(`WndProc`)와 메시지 루프 (`PeekMessage` 방식 — 게임 루프용)
- [o] `WM_DESTROY`, `WM_SIZE` 등 기본 메시지 처리
- [o] 창 크기/타이틀 상수화

**핵심 개념**: 메시지 기반 구조, `GetMessage` vs `PeekMessage`(블로킹 여부가 게임 루프에 왜 중요한지)

**완료 기준**: 1280x720 창이 뜨고, X 버튼으로 정상 종료된다.

---

## Phase 2 — DX12 초기화 & 화면 클리어

**목표**: DX12 초기화 전 과정을 한 줄씩 이해한다. **엔진 전체에서 가장 학습량이 많은 단계** — 서두르지 말 것.

- [o] Debug Layer 활성화 (디버그 빌드에서만)
- [o] `DXGIFactory` → `Adapter` 열거 → `D3D12CreateDevice`
- [o] Command Queue / Command Allocator / Command List 생성
- [o] Swap Chain 생성 (더블 버퍼링, `DXGI_SWAP_EFFECT_FLIP_DISCARD`)
- [o] RTV Descriptor Heap 생성, 백버퍼마다 RTV 생성
- [o] Fence 기반 CPU-GPU 동기화 (`Signal` / `WaitForFenceValue`)
- [o] 렌더 루프: 리소스 배리어(Present→RenderTarget) → Clear → 배리어(→Present) → Present

**핵심 개념**:
- DX11과 달리 **커맨드를 기록해서 큐에 제출**하는 구조인 이유
- Descriptor / Descriptor Heap이라는 간접 참조 계층
- 리소스 스테이트와 배리어(Resource Barrier)
- Fence로 하는 명시적 동기화 — GPU가 아직 쓰는 리소스를 CPU가 건드리면 안 되는 이유

**완료 기준**: 매 프레임 지정한 색으로 클리어된 화면이 출력되고, 종료 시 Debug Layer 경고/에러가 없다.

---

## Phase 3 — 첫 삼각형

**목표**: 렌더링 파이프라인 전체(입력 → 셰이더 → 출력)를 한 번 관통한다.

- [o] HLSL 정점/픽셀 셰이더 작성 (`Shaders/`)
- [o] 셰이더 컴파일 (일단 `D3DCompileFromFile`로 시작, DXC 전환은 나중에)
- [o] Root Signature 생성 (처음엔 빈 것으로)
- [o] PSO(Pipeline State Object) 생성 — Input Layout, 셰이더, 래스터라이저/블렌드/뎁스 상태
- [o] Vertex Buffer 생성 (일단 Upload Heap에) + Vertex Buffer View
- [o] Viewport / Scissor Rect 설정, `DrawInstanced` 호출

**핵심 개념**: 파이프라인 상태를 PSO 하나에 미리 굽는 이유, Root Signature = 셰이더가 받는 리소스의 "함수 시그니처", NDC 좌표계

**완료 기준**: 색상 보간된 삼각형이 화면에 그려진다.

---

## Phase 4 — 버퍼와 3D 변환

**목표**: 정적인 2D에서 움직이는 3D로. 상수 버퍼로 CPU 데이터를 매 프레임 셰이더에 공급한다.

- [o] Index Buffer 도입
- [o] Constant Buffer 생성 (256바이트 정렬) + CBV Descriptor Heap
- [o] Root Signature에 CBV 바인딩 추가
- [o] DirectXMath로 World / View / Projection 행렬 구성, 매 프레임 갱신
- [o] Depth Buffer(DSV) 생성 및 뎁스 테스트 활성화
- [o] 큐브 지오메트리 직접 정의, 회전 애니메이션

**핵심 개념**: WVP 변환 파이프라인, 행 우선/열 우선과 HLSL `mul` 순서, 뎁스 버퍼가 없으면 생기는 현상 확인, Upload Heap에 CPU가 쓸 때의 동기화 문제(프레임마다 같은 버퍼를 덮어쓰면 왜 위험한가)

**완료 기준**: 회전하는 큐브가 올바른 깊이로 렌더링된다.

---

## Phase 5 — 텍스처

**목표**: 이미지 리소스를 GPU에 올리는 전 과정(업로드 힙 → 디폴트 힙 복사)을 이해한다.

- [o] 이미지 로딩 (stb_image 또는 DirectXTex — 첫 외부 라이브러리 도입)
- [o] Default Heap에 텍스처 리소스 생성, Upload Heap 경유 복사 (`UpdateSubresources`)
- [o] SRV 생성, Descriptor Table로 Root Signature에 바인딩
- [o] Static Sampler 설정, 셰이더에서 UV 샘플링
- [o] 큐브에 UV 좌표 추가

**핵심 개념**: Upload vs Default Heap(왜 두 번 거치는가), 복사 완료를 기다리는 동기화, Descriptor Table vs Root Descriptor

**완료 기준**: 텍스처가 입혀진 큐브가 렌더링된다.

---

## Phase 6 — 카메라 & 입력 & 시간

**목표**: "장면을 보는 것"에서 "장면 안을 돌아다니는 것"으로.

- [o] 고해상도 타이머 (`QueryPerformanceCounter`) — delta time, FPS 측정
- [o] 키보드/마우스 입력 처리 (`WM_INPUT` Raw Input 또는 `GetAsyncKeyState`로 시작)
- [o] FPS 스타일 카메라 (WASD 이동 + 마우스 회전, View 행렬 직접 구성)
- [o] 바닥 평면 + 큐브 여러 개를 씬에 배치 (이동감을 체감할 공간적 기준점 마련)
- [o] 창 리사이즈 대응 (Swap Chain / Depth Buffer 재생성, Projection 갱신)
- [o] 타이틀바에 FPS 표시

**핵심 개념**: 프레임 독립적 이동(`속도 × deltaTime`), View 행렬 = 카메라 변환의 역행렬, 리사이즈 시 GPU 리소스 재생성 절차(플러시가 먼저 필요한 이유)

**완료 기준**: 바닥과 여러 오브젝트가 있는 씬을 마우스와 WASD로 자유롭게 돌아다니며 실제로 이동하는 느낌을 확인할 수 있고, 창 크기를 바꿔도 깨지지 않는다.

---

## Phase 7 — 조명

**목표**: 셰이더 프로그래밍 본격 입문. 표면이 빛에 반응하게 만든다.

- [o] 정점에 노멀 추가, 노멀 변환(월드 행렬의 역전치) 이해
- [o] 방향광(Directional Light) + Blinn-Phong 반사 모델 (ambient / diffuse / specular)
- [o] 머티리얼 개념 도입 (색상, specular power 등을 CB로 전달)
- [o] 점광(Point Light) 추가, 감쇠(attenuation)
- [o] 조명용 상수 버퍼 구조 정리 (프레임 단위 CB vs 오브젝트 단위 CB 분리)

**핵심 개념**: 노멀과 내적으로 표현하는 빛의 세기, 왜 노멀은 역전치 행렬로 변환하는가, 상수 버퍼를 갱신 빈도별로 나누는 설계

**완료 기준**: 조명을 받는 물체가 카메라/광원 위치에 따라 자연스럽게 음영을 보인다.

---

## Phase 8 — 모델 로딩

**목표**: 하드코딩 지오메트리를 벗어나 외부 에셋을 불러온다.

- [o] OBJ 파서 직접 작성 (포맷 이해 목적 — 정점/노멀/UV/face 파싱)
- [o] Mesh 클래스로 정점/인덱스 버퍼 생성 로직 정리 (첫 렌더링 추상화)
- [] (선택) Assimp 도입으로 FBX 등 다른 포맷 지원
- [o] 여러 오브젝트를 각자의 World 행렬로 렌더링

**핵심 개념**: 인덱스 재구성(OBJ의 pos/uv/normal 인덱스 분리 문제), 버텍스 중복 제거, **좌표계 변환과 감김 방향** — OBJ(오른손)에서 Z를 음수화하는 것은 거울 변환이라 삼각형 방향이 뒤집힌다. 인덱스 순서도 함께 뒤집지 않으면 뒷면 컬링이 바깥면을 잘라내 모델의 **안쪽**이 보인다 (9.5 진행 중 발견)

**완료 기준**: 외부 모델 파일(예: 유명한 Stanford Bunny, teapot)이 조명을 받으며 렌더링된다.

---

## Phase 9 — 엔진 구조화 (v1.0 마일스톤)

**목표**: 지금까지의 코드를 "엔진"과 "게임"으로 분리한다. 여러 오브젝트와 컴포넌트의 반복이 충분히 드러난 지금이 추상화 적기.

> 📄 **세부 계획: [docs/Phase9-Plan.md](docs/Phase9-Plan.md)** — 아래 항목을 6개 단계(9.1~9.6)로 쪼갠 실행 계획. 단계별 설계 결정·함정·검증 기준 포함.

- [o] Engine 클래스 (초기화 / 게임 루프 / 종료), Game은 Engine을 상속하거나 콜백으로 연결 *(9.2: 상속 방식)*
- [o] Device / SwapChain / CommandQueue 등 래퍼 클래스 정리 *(9.2: 전역 0개, Source를 4개 폴더로 재배치)*
- [o] Frame Resource 패턴 — 프레임 인플라이트 개수만큼 커맨드 할당자·CB 링버퍼링 (Fence 동기화 최소화) *(9.1 완료: 0.20ms → 0.18ms)*
- [o] Descriptor 할당자 (힙에서 슬롯을 나눠주는 간단한 매니저) *(9.3: 범프+프리리스트, 핸들 산술 캡슐화)*
- [o] 리소스 매니저 (텍스처/메시/셰이더 캐싱, 중복 로드 방지) *(9.4: 텍스처 요청 13회 → 로드 2회)*
- [o] Entity / Component / System 기반의 간단한 ECS 구조 — Entity 생성·삭제, Component 추가·조회 *(9.5)*
- [o] 기본 Component 정의: Transform, MeshRenderer, Camera, Light *(+ Spin/ActiveCamera/Environment)*
- [o] 기본 System 구현 (Component를 순회하며 렌더링/카메라 갱신 등을 처리) *(렌더러는 ECS를 모름)*
- [o] ImGui 통합 — 디버그 UI로 오브젝트 위치, 조명 값 실시간 조정 *(9.6: docking, ThirdParty에 벤더링)*

**핵심 개념**: 프레임 파이프라이닝(CPU가 N프레임 준비하는 동안 GPU가 N-1 렌더), 소유권과 수명 관리(ComPtr), 엔진/게임 경계 설계, Entity=ID·Component=데이터·System=로직으로 나누는 조합(compose) 기반 설계가 상속 기반 구조보다 유리한 이유

**완료 기준**: `main.cpp`가 짧아지고, Entity에 Component를 조합해 씬에 오브젝트를 추가하는 코드가 몇 줄로 끝난다. ImGui로 씬을 실시간 조작할 수 있다.

---

## Phase 10 — 맵 에디터 (ImGui)

**목표**: 코드로만 씬을 구성하던 방식에서 벗어나, 에디터로 씬을 직접 구성할 수 있게 한다. ImGui는 창·목록·Inspector 같은 에디터 UI를 담당하고, 실제 3D 렌더링과 오브젝트 배치는 자체 엔진(Phase 9의 Scene/GameObject 구조)이 담당하는 역할 분리를 확립한다.

> 📄 **세부 계획: [docs/Phase10-Plan.md](docs/Phase10-Plan.md)** — 아래 항목을 5개 단계(10.1~10.5)로 쪼갠 실행 계획. 단계별 설계 결정·함정·검증 기준 포함.

- [o] DX12 렌더링 결과를 오프스크린 렌더 타겟(SRV)으로 뽑아 ImGui Scene Viewport 창에 표시 *(10.1: 씬 패스 → RT, UI 패스 → 백버퍼. 종횡비·카메라 입력이 창이 아니라 뷰포트를 따른다)*
- [o] 배경/지형/건축물 에셋 브라우저 (목록 탐색 + 미리보기) *(10.3: 지연 로드, 텍스처 미리보기. 메시 3D 썸네일은 트리거 대기)*
- [o] 에셋을 뷰포트에서 선택해 게임 월드에 Entity로 배치 *(10.4: 언프로젝션 + 레이-평면 교차. 기존 엔티티 클릭 선택은 바운딩 박스와 함께)*
- [o] Entity 목록 창 — 씬에 존재하는 Entity 탐색·선택 *(10.2: Name 컴포넌트로 이름 표시, 생성·복제·삭제)*
- [o] Inspector — 선택한 Entity의 Component 확인·값 편집(Transform 등), Component 추가/삭제 *(10.2: 구조 편집은 프레임 끝에 일괄 적용)*
- [o] 씬 저장 / 불러오기 (Entity·Component 데이터를 직렬화하는 ECS 기반 씬 포맷) *(10.5: 자체 텍스트 포맷, 핸들이 아니라 이름을 저장. 경로 정규화 정책 확정)*

트리거 대기 항목 — 미리 만들지 않고, 조건이 실제로 발생하는 순간 착수:

- [o] 멀티 머티리얼(Submesh) — **발화·완료 (Phase 11, 11.2와 11.3 사이).** 트리거가 정확히 예고한 대로 일어났다: `laevat.obj`가 전체 다운로드(`.obj` + `.mtl` + 텍스처 10장)로 교체되면서 `usemtl` 6개가 각각 다른 텍스처를 요구했고, 단일 재질 렌더링으로는 면의 40.5%가 엉뚱한 텍스처를 입고 있었다. OBJ `mtllib`/`usemtl` 파싱, `.mtl`의 `map_Kd` 해석, `Mesh`에 Submesh(인덱스 구간 + 텍스처) 목록, `DrawItem`에 인덱스 구간 추가. 자세한 내용은 [Phase11-Plan](docs/Phase11-Plan.md)
- [ ] 씬 계층 구조(Transform 부모-자식) — *트리거: 여러 엔티티를 한 덩어리로 움직이고 싶어지는 순간* (예: 머티리얼별로 쪼갠 모델을 엔티티 여러 개로 배치했을 때)
- [o] 메시 바운딩 박스(AABB) — **발화·완료 (11.0).** 로드 시점에 정점 범위를 구해 Mesh에 보관. 예고한 대로 하나의 데이터로 둘 다 풀렸다: 레이-AABB 교차(클릭 픽킹)와 배치 시 바닥 안착. 이후 배치의 크기 클램프·XZ 중심 정렬까지 같은 데이터를 쓴다
- [ ] OBJ 업 축(up-axis) 보정 — *트리거: Z-up 좌표 그대로 들어온 모델을 실제로 불러와 누워버리는 순간.* **OBJ 포맷에는 신뢰할 수 있는 up-axis 메타데이터가 없다.** DCC 내부가 Z-up이어도 익스포터가 Y-up으로 변환해 내보내는 경우가 많으므로 "Z-up 툴 = 누움"이 아니고, 문제는 **Z-up 좌표로 들어온 파일을 현재 로더가 구별할 수단이 없다는 것**이다. AABB로도 알 수 없다 — 상자만 봐서는 어느 축이 키인지 판단할 근거가 없다. 그래서 자동 추론이 아니라 **에셋별 import 설정(또는 axis preset)**이 필요하다.

  **순서가 핵심이다.** 배치가 끝난 뒤에 90도를 돌리면 `-min.y` 안착 계산이 무효가 되어 다시 뜨거나 파묻힌다. 변환은 반드시 bounds 계산보다 **먼저** 와야 한다:

  ```
  import-axis 변환 → 변환된 bounds 계산 → size fit → center/ground 배치
  ```

  즉 회전 프리셋 UI 하나로는 부족하고, 축 변환이 로드 파이프라인의 앞단(정점 또는 import 설정)에 들어가야 한다
- [ ] `map_Kd` 경로 복구 후보 확대 — *트리거: 확장자를 생략한 비-PNG 텍스처를 쓰는 모델이 들어오는 순간.* 11.2.5의 `ResolveMtlTexture`는 후보가 `이름 그대로` / `+ ".png"` 둘뿐이다. `foo.jpg`처럼 확장자가 적혀 있으면 첫 후보가 맞아 지금도 되지만, 생략된 JPG는 못 찾는다. 후보 목록을 늘리는 것으로 끝나는 문제다
- [ ] TGA 디코더 — *트리거: TGA 텍스처를 쓰는 모델이 들어오는 순간.* **위 항목과 다른 문제다.** 경로를 찾아도 Windows 기본 WIC에 TGA 코덱이 없어 `CreateDecoderFromFilename`이 실패한다. 후보 목록이 아니라 디코더를 붙여야 한다
- [ ] 비동기 에셋 로드 — *트리거: 큰 모델을 고를 때의 정지가 실제로 작업을 방해하는 순간.* 10.3에서 23 MB OBJ에 **5043 ms** 측정. UI 스레드가 통째로 멈춘다. 스레드가 필요해 Phase 13 Job System과 겹치는 주제
- [ ] 메시 3D 썸네일 — *트리거: 파일 이름만으로 에셋을 구분하기 어려워지는 순간.* 에셋마다 렌더 타겟이 필요하다 (10.3에서 범위 밖으로 명시)
- [ ] Object CB 동적화 — *트리거: 한 씬이 `kMaxObjects`(256)를 넘기는 순간.* 10.2에서 32 → 256으로 올려 시간을 벌었을 뿐이다. GPU가 읽는 중인 버퍼를 재할당하는 것이라 RT 재생성과 같은 지연 처리가 필요하다
- [ ] 렌더 타겟 리사이즈의 프레임별 폐기 큐 — *트리거: 뷰포트 스플리터를 끄는 동안의 전체 플러시가 거슬리는 순간.* 11.7 연속 측정에서 안정 시 약 6.05ms, resize 시 4x median/p95/max 13.95/14.59/15.64ms, 1x 13.83/14.69/15.46ms였다. 두 모드가 같아 원인은 MSAA 자체보다 기존 `WaitForGpu()` full flush다. 최대가 60 Hz 예산 안이고 입력 손실은 없어 대기 유지하되, 165 Hz 편집에서 약 70 fps로 떨어지는 것이 거슬리면 발화
- [ ] 삼각형 단위 픽킹 (또는 겹친 후보 순환 선택) — *트리거: 큰 메시의 AABB가 그 안의 오브젝트를 가려 클릭으로 못 고르는 상황이 실제로 작업을 방해할 때.* 11.0에서 확인됨: `Laevat`의 약 21 단위 상자가 `Sphere`와 `Torus`를 통째로 삼켜, 데모 씬 어디를 클릭해도 Laevat이 선택된다. 최근접 규칙 자체는 정확하고 뾰족한 모델에 상자를 씌운 결과다. 해결은 삼각형 교차이거나, 같은 지점을 다시 클릭하면 다음 후보로 넘어가는 순환 선택 — 후자가 훨씬 싸다. *(11.2.5 이후 기본 씬에서 laevat이 빠졌고 `BuildWorld`는 절차 메시만 쓰므로, 이 증상은 에셋 브라우저로 laevat을 직접 불러왔을 때만 재현된다. 트리거는 그대로 유효하다 — 조건이 사라진 게 아니라 기본 씬에서 상시 노출되지 않게 됐을 뿐이다)*

**핵심 개념**: 3D 렌더 결과를 텍스처로 만들어 ImGui에 표시하는 방법(오프스크린 렌더 타겟 → SRV → `ImGui::Image`), 에디터 UI와 게임 렌더링/월드 데이터의 책임 분리, 씬 데이터(Entity·Component)와 에디터 전용 상태(선택 Entity 등)를 구분하는 이유

**완료 기준**: 에디터에서 에셋을 골라 Entity로 배치하고 Component 값을 편집·추가·삭제한 뒤, 씬을 저장했다가 다시 불러와도 동일하게 복원된다.

---

## Phase 11 — 중급 렌더링

**목표**: 실제 게임 화면처럼 보이게 만드는 기법들. 기능 목표는 서로 분리되어 있지만,
현재 구현에서는 공통 렌더링 기반에 의존하므로 [세부 계획](docs/Phase11-Plan.md)의
순서에 따라 진행한다.

- [o] 스카이박스 (큐브맵, TextureCube 샘플링)
- [o] 블렌딩 / 투명 오브젝트 (알파 블렌딩, 렌더 순서 문제) *(11.6: 명시적 `BlendMode`, texture×material alpha, straight-alpha PSO와 depth write OFF. world AABB center의 camera-space depth로 매 프레임 후방→전방 인덱스 정렬하며 Object CB 슬롯은 원본 인덱스로 유지. scene v5와 Inspector RGBA 편집 지원)*
- [o] 노멀 매핑 (탄젠트 공간) *(11.3: `Vertex.tangent`는 `float4` — `w`가 mirrored UV의 bitangent 뒤집힘을 나른다. tangent 생성은 `CreateMesh` 한 곳에서 일어나 절차 메시와 파일 메시가 같은 vertex contract를 갖는다. UV 면적과 **기하 면적**을 따로 검사해 퇴화 삼각형을 배제 — 구의 극점 64개가 후자에만 걸린다. 예측 가능한 줄무늬 노멀맵으로 밝기비를 0.3% 이내로 대조)*
- [o] 그림자 매핑 (Depth 전용 패스, 첫 멀티패스 렌더링) *(11.4: 2048² depth-only pass, 고정 경계구 light volume. 11.5: `t2` comparison sampling, lit border, caster+receiver bias, linear comparison 3×3 PCF. 11.6.1: geometric normal과 광원 각도로 receiver bias를 최소값~4배까지 조절해 평면의 삼각형 모양 acne/모아레를 억제. Environment에서 enable/base bias/strength를 편집하고 scene v4+로 저장. directional 직접광만 가리며 기본 씬 런타임에서 Debug Layer 0 확인)*
- [o] MSAA 또는 렌더 타겟 해상도 분리 *(11.7: color/depth 4x + 단일 샘플 resolve texture. 색상 PSO별 1x/4x variant, Frame UI Off/4x 전환, 미지원 1x 폴백. 전환·리사이즈 반복 뒤 Debug Layer 0)*

**핵심 개념**: 멀티패스 렌더링 구조(그림자 맵이 사실상 "렌더 투 텍스처" 입문), 투명 오브젝트 정렬 문제

**완료 기준**: 그림자가 지는 씬을 스카이박스 아래에서 볼 수 있다.

---

## Phase 12 — 플레이 모드 & 게임 런타임

**목표**: 에디터로 만든 씬을 "게임으로" 실행한다. 에디터 안에서는 Play/Stop 토글로, 최종적으로는 에디터가 아예 없는 독립 실행 파일로.

지금까지 Engine/Game 경계는 만들었지만 **에디터/게임 경계는 없다** — 현재 실행 파일은 DemoGame이 곧 에디터다. 에디터 뷰포트로 씬을 보는 것과 게임을 플레이하는 것은 다른 일이고, 이 단계가 그 경계를 만든다. (에디터만으로 게임을 테스트하는 것은 개발 중의 편의이지 최종 형태가 아니다.)

> 📄 **세부 계획: [docs/Phase12-Plan.md](docs/Phase12-Plan.md)** — 아래 항목을
> 8개 단계(12.0~12.7)로 쪼갠 실행 계획. 실행 모드, 링크 경계, presentation,
> 리소스 패키징, 저장소 독립 검증 기준 포함.

시스템의 실행 조건만 나누는 것으로는 충분하지 않다. **분류는 곧 빌드·링크·패키징
경계**여야 하며, 배포용 Player에는 Editor-only 코드와 ImGui가 들어가지 않아야 한다.
목표 산출물 구조는 다음과 같다.

| 산출물 | 책임 | 의존성 규칙 |
|--------|------|-------------|
| `Engine.lib` | Win32 루프, ECS 기반, 렌더러, 리소스·저수준 런타임 | Game·Editor·ImGui를 모름 |
| `Game.lib` | 공유 Component, 씬 포맷, Always/Play-only 시스템, 데모 게임 규칙 | Engine에만 의존, Editor·ImGui를 모름 |
| `Editor.exe` | Editor-only 시스템, 편집 카메라, 선택·Inspector·Asset Browser, Play/Stop UI | Engine + Game + ImGui |
| `Player.exe` | 시작 Scene 로드, 게임 입력·시스템 실행, swap-chain presentation | Engine + Game, Editor·ImGui 링크 금지 |

- [ ] 시스템을 **Always / Editor-only / Play-only** 세 종류로 분류하고 등록·실행 경로를 분리한다. 지금 Spin이 에디터에서도 도는 것이 이 구분이 아직 없다는 증거다. Editor-only 소스는 실행 조건으로만 끄는 것이 아니라 Player 프로젝트의 컴파일·링크 입력에서 제외한다
- [ ] 에디터 카메라 / 게임 카메라 분리 — Edit에서는 World 밖의 EditorCamera, Editor Play와 Player에서는 씬의 ActiveCamera가 화면을 갖는다
- [ ] 플레이 전용 시간과 입력 컨텍스트 분리 — Play 시작 시 게임 시간을 0으로 초기화하고, Stop/Pause 중에는 Play-only 시스템의 시간과 입력이 진행되지 않는다
- [ ] 씬 직렬화를 파일 경로와 분리해 메모리 stream/string에서도 같은 포맷을 읽고 쓸 수 있게 한다
- [ ] Play/Stop 토글: Play 시 World를 메모리에 직렬화하고 Stop 시 복원한다. "플레이 중의 편집은 Stop과 함께 사라진다"까지가 사양이다. World 교체와 함께 선택 Entity·대기 중인 편집 명령 등 Editor-only 임시 상태도 안전하게 초기화한다
- [ ] Renderer의 scene pass와 presentation을 분리한다. Editor는 offscreen RT → ImGui viewport, Player 1x는 swap-chain back buffer 직접 렌더, Player 4x는 MSAA 임시 target → back buffer resolve를 사용한다. shadow/opaque/skybox/transparent 순서는 공유한다
- [ ] 프로젝트를 `Engine.lib + Game.lib + Editor.exe + Player.exe`로 분리하고 프로젝트 참조 방향을 위 표대로 고정한다. Renderer/Engine의 ImGui 초기화와 overlay pass는 Editor 쪽 어댑터로 분리해 Player 링크에서 ImGui 구현과 심볼이 사라지게 한다
- [ ] Player 시작 Scene 지정 — 커맨드라인의 명시적 `.scene` 경로(예: `--scene Assets/Scenes/Demo.scene`)를 우선하고, 인자가 없을 때 사용할 패키지 기본 Scene 정책을 정한다
- [ ] 리소스 루트를 **실행 파일 위치 기준**으로 바꾼다. 개발 저장소 탐색은 Editor/개발 빌드의 fallback일 뿐이며 Player 배포 경로의 전제가 되어서는 안 된다
- [ ] 패키징 단계에서 `Player.exe`, `Assets/`, 컴파일된 `Shaders/`, 필요한 비시스템 DLL과 시작 Scene을 하나의 staging 폴더로 복사한다. Player는 런타임 HLSL 소스 컴파일이나 `Dx12Engine.slnx` 탐색 없이 동작해야 한다
- [ ] 작은 데모 게임 하나 (WASD 이동 + 간단한 상호작용 규칙) — 런타임의 검증 기준은 "돌아간다"가 아니라 "논다"
- [ ] 독립 배포 검증 — 새 빈 폴더에 staging 결과만 복사하고 저장소·솔루션·소스·개발 경로를 찾을 수 없는 상태에서 지정 Scene으로 실행한다. 가능하면 개발 도구가 없는 다른 PC에서도 같은 패키지를 검증한다

**선행 조건**: 10.5 씬 저장/불러오기 (Player가 로드할 파일 포맷이자 Play/Stop 스냅샷 수단). Phase 11과는 독립이므로, 원하면 11보다 먼저 진행해도 된다.

**핵심 개념**: 에디터/런타임 분리(에셋과 씬은 같고 실행 형태만 다르다), 시뮬레이션 상태의 스냅샷/복원, 하나의 렌더러가 두 프레젠테이션 경로를 갖는 구조, 실행 조건 분리와 링크 단위 분리의 차이, 실행 파일 기준 리소스 탐색과 재현 가능한 패키징

**완료 기준**: 에디터에서 Play로 즉석 확인하고 같은 씬을 Player 단독 실행으로 플레이한다. `Player.exe`와 런타임 리소스만 새 빈 폴더 또는 다른 PC로 복사한 뒤, 솔루션·소스·저장소 구조·개발 머신 절대 경로 없이 시작 Scene을 정상 로드해 플레이할 수 있다. Player 바이너리에는 Editor-only 코드와 ImGui가 링크되지 않는다. **여기서 `v1.3` 태그.**

---

## Phase 13 — 고급 주제 (선택, 관심 순서대로)

여기부터는 정해진 순서 없이 흥미 있는 것을 골라 진행:

- **렌더링**: 인스턴싱, 포스트 프로세싱(블룸, 톤매핑), Deferred Rendering, PBR, HDR
- **DX12 심화**: DXC 셰이더 컴파일러 전환, Bindless(Descriptor Indexing), 멀티스레드 커맨드 기록, GPU 기반 컬링
- **엔진 기능**: 스켈레탈 애니메이션 (glTF 도입 — 멀티 머티리얼 Submesh가 아직 없다면 이때 반드시 함께), 파티클 시스템, 오디오(XAudio2), 물리(충돌 검출부터), 씬 직렬화(저장/로드)
- **ECS 심화**: Archetype 기반 메모리 레이아웃, Job System, 멀티스레드 ECS(Component 배열 병렬 순회)
- **에디터 심화**: 지형 스컬프팅/페인팅 (Phase 10 맵 에디터에 브러시 기반 지형 편집 추가)
- **도구**: 렌더독(RenderDoc)/PIX 프로파일링 습관화, 에셋 파이프라인

---

## 커밋 / 브랜치 전략

- **main 브랜치 단일 운용**으로 시작 (혼자 학습용이므로 단순하게)
- 커밋 메시지: `Phase2: Create swap chain and RTV heap` 처럼 단계 접두어 + 내용
- 각 Phase 완료 시 태그: `git tag v0.3 -m "Phase 2: clear screen"` → 나중에 특정 시점 코드로 바로 복습 가능
- 실험적 시도(예: 구조 리팩토링)는 브랜치 파서 진행, 실패해도 main은 안전하게

## 참고 자료

- [Microsoft DirectX-Graphics-Samples](https://github.com/microsoft/DirectX-Graphics-Samples) — 공식 샘플, HelloWorld 시리즈가 Phase 2~5와 대응
- [DirectX 12 공식 문서](https://learn.microsoft.com/en-us/windows/win32/direct3d12/direct3d-12-graphics)
- Frank Luna, *Introduction to 3D Game Programming with DirectX 12* — Phase 2~10 전반의 교과서
- [3dgep.com DX12 튜토리얼](https://www.3dgep.com/learning-directx-12-1/) — 초기화 과정 설명이 상세
- [braynzarsoft DX12 튜토리얼](https://www.braynzarsoft.net/viewtutorial/q16390-04-directx-12-braynzar-soft-tutorials)
- [Learn Win32 (Microsoft)](https://learn.microsoft.com/en-us/windows/win32/learnwin32/learn-to-program-for-windows) — Phase 1
