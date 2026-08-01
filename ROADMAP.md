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
| Phase 12 | 고급 주제 (선택) | 인스턴싱, 포스트 프로세싱, PBR 등 | - |

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
- [ ] 배경/지형/건축물 에셋 브라우저 (목록 탐색 + 미리보기)
- [ ] 에셋을 뷰포트에서 선택해 게임 월드에 Entity로 배치
- [o] Entity 목록 창 — 씬에 존재하는 Entity 탐색·선택 *(10.2: Name 컴포넌트로 이름 표시, 생성·복제·삭제)*
- [o] Inspector — 선택한 Entity의 Component 확인·값 편집(Transform 등), Component 추가/삭제 *(10.2: 구조 편집은 프레임 끝에 일괄 적용)*
- [ ] 씬 저장 / 불러오기 (Entity·Component 데이터를 직렬화하는 ECS 기반 씬 포맷)

트리거 대기 항목 — 미리 만들지 않고, 조건이 실제로 발생하는 순간 착수:

- [ ] 멀티 머티리얼(Submesh) — *트리거: 에셋 브라우저에 여러 텍스처를 쓰는 실제 모델이 들어와 깨져 보이는 순간.* OBJ `usemtl` 파싱, Mesh를 Submesh(인덱스 구간 + 머티리얼) 목록으로 재구성. 데이터 모델 변경이라 무한정 미루면 비용이 커지는 종류 — 늦어도 glTF 도입(Phase 12 스켈레탈 애니메이션) 때는 함께 한다
- [ ] 씬 계층 구조(Transform 부모-자식) — *트리거: 여러 엔티티를 한 덩어리로 움직이고 싶어지는 순간* (예: 머티리얼별로 쪼갠 모델을 엔티티 여러 개로 배치했을 때)

**핵심 개념**: 3D 렌더 결과를 텍스처로 만들어 ImGui에 표시하는 방법(오프스크린 렌더 타겟 → SRV → `ImGui::Image`), 에디터 UI와 게임 렌더링/월드 데이터의 책임 분리, 씬 데이터(Entity·Component)와 에디터 전용 상태(선택 Entity 등)를 구분하는 이유

**완료 기준**: 에디터에서 에셋을 골라 Entity로 배치하고 Component 값을 편집·추가·삭제한 뒤, 씬을 저장했다가 다시 불러와도 동일하게 복원된다.

---

## Phase 11 — 중급 렌더링

**목표**: 실제 게임 화면처럼 보이게 만드는 기법들. 각 항목이 독립적이라 순서는 자유.

- [ ] 스카이박스 (큐브맵, TextureCube 샘플링)
- [ ] 블렌딩 / 투명 오브젝트 (알파 블렌딩, 렌더 순서 문제)
- [ ] 노멀 매핑 (탄젠트 공간)
- [ ] 그림자 매핑 (Depth 전용 패스, 첫 멀티패스 렌더링)
- [ ] MSAA 또는 렌더 타겟 해상도 분리

**핵심 개념**: 멀티패스 렌더링 구조(그림자 맵이 사실상 "렌더 투 텍스처" 입문), 투명 오브젝트 정렬 문제

**완료 기준**: 그림자가 지는 씬을 스카이박스 아래에서 볼 수 있다.

---

## Phase 12 — 고급 주제 (선택, 관심 순서대로)

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
