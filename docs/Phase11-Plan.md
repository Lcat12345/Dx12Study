# Phase 11 — 중급 렌더링 세부 계획 (v1.2)

Phase 10에서 만든 오프스크린 Scene Viewport, 에셋 브라우저, ECS 씬과
직렬화가 이번 단계의 기반이다. 이 문서는 ROADMAP Phase 11 항목을 현재
코드의 실제 의존성에 맞춰 **커밋 단위로 쪼갠 실행 계획**이다.

Phase 11의 기능 목표는 서로 분리되어 있지만, 현재 구현에서는 깊이 전용 타깃,
복수 PSO, 메시 공간 정보처럼 여러 기능이 공유하는 기반이 있다. 따라서 이미
발화한 AABB 트리거와 최소한의 렌더 패스 기반을 먼저 만든 뒤 세부 계획의 순서에
따라 각 기능을 수직으로 완성한다.

---

## 1. 현재 상태 진단

Phase 10 종료(v1.1) 시점 코드의 한계. 각 항목은 아래 세부 단계 또는 트리거
판정 지점과 대응된다.

| # | 문제 | 증상 | 해결 단계 |
|---|------|------|-----------|
| 1 | 메시의 로컬 범위를 보관하지 않음 | 기존 엔티티를 클릭 선택할 수 없고 배치한 메시가 바닥에 반쯤 묻힘 | 11.0 |
| 2 | `RenderTarget`이 색상+깊이를 항상 함께 생성 | 그림자 맵처럼 깊이만 필요한 표면을 표현할 수 없음 | 11.1 |
| 3 | 렌더러에 루트 시그니처와 PSO가 각각 하나뿐 | 스카이박스·그림자·투명 오브젝트의 서로 다른 상태를 전환할 수 없음 | 11.1 |
| 4 | 텍스처 업로드가 2D 한 장에 고정 | 큐브맵의 6개 서브리소스와 `TextureCube` SRV를 만들 수 없음 | 11.2 |
| 5 | `Vertex`에 tangent가 없음 | 탄젠트 공간 normal map을 월드 공간 조명에 사용할 수 없음 | 11.3 |
| 6 | 메인 셰이더가 diffuse texture 하나만 바인딩 | normal map과 shadow map을 동시에 샘플링할 수 없음 | 11.3, 11.5 |
| 7 | 조명이 directional 1개 + point 1개로 평탄화됨 | 어떤 조명에 어떤 방식으로 그림자를 적용할지 사양이 없음 | 11.4 |
| 8 | 모든 `DrawItem`이 하나의 불투명 루프로 그려짐 | 알파 블렌딩과 후방→전방 정렬을 적용할 구분이 없음 | 11.6 |
| 9 | Scene Viewport의 색상 SRV가 곧 렌더 대상 | 멀티샘플 텍스처를 ImGui가 직접 샘플링할 수 없음 | 11.7 |

### 트리거 대기 항목 판정

[ROADMAP](../ROADMAP.md)의 트리거 항목은 이전 Phase의 미완료 체크리스트가
아니다. 조건이 실제로 발생한 순간 현재 Phase에 끼워 넣는 조건부 작업이다.

| 항목 | Phase 11 시작 시 판정 | 재판정 시점 |
|------|----------------------|-------------|
| 메시 바운딩 박스(AABB) | **발화** — 클릭 선택과 바닥 배치 문제가 모두 존재 | 11.0에서 구현 완료 |
| 삼각형 단위 픽킹 | **대기** (11.0이 새로 등록) — 큰 상자가 그 안의 물체를 삼키는 것을 확인했으나, 정상 씬에서 작업을 막지는 않음 | 실제로 고르지 못해 답답해지는 순간 |
| 멀티 머티리얼(Submesh) | **발화** — `laevat.obj`가 전체 다운로드로 교체되며 `usemtl` 6개가 각각 다른 텍스처를 요구 | 11.2.5로 끼워 넣어 완료 |
| 씬 계층 구조 | 대기 | 여러 엔티티를 한 묶음으로 이동해야 할 때 |
| 비동기 에셋 로드 | 대기 | 11.2~11.3 에셋을 반복 로드할 때 정지가 작업을 방해하는지 측정 |
| 메시 3D 썸네일 | 대기 | 파일명만으로 메시를 구분하기 어려워질 때 |
| Object CB 동적화 | 대기 | 한 씬의 고유 `DrawItem`이 256개를 넘을 때 |
| RT 프레임별 폐기 큐 | 대기 | 11.7 이후 뷰포트 연속 리사이즈 비용 재측정 |

그림자 패스가 같은 오브젝트를 한 번 더 그린다고 Object CB 슬롯이 두 배로
필요한 것은 아니다. 같은 프레임의 object constants를 두 패스가 재사용하므로
동적화 트리거는 계속 **고유 오브젝트 수 256개 초과**다.

**이번 단계에서 건드리지 않는 것**:

- point light 그림자 — 큐브 깊이 맵과 6방향 패스가 필요한 별도 주제
- cascaded shadow maps, variance shadow maps — 단일 directional shadow를 익힌 뒤의 확장
- alpha test, order-independent transparency, 삼각형 단위 투명 정렬
- HDR, tone mapping, 감마/sRGB 파이프라인
- mipmap 생성, DDS/BC 압축 텍스처 로더
- 범용 Render Graph — 현재 패스 수에서는 명시적인 함수가 더 읽기 쉽다
- 에디터/Player 분리와 배포 경로 정책 — Phase 12에서 처리

---

## 2. 진행 원칙

1. **한 단계 = 완결된 커밋 1~2개.** 각 단계가 끝날 때마다 Debug/Release 빌드,
   실행, 화면 확인, Debug Layer 메시지를 검증한다.
2. **구조 변경만 남기지 않는다.** 11.1을 제외한 각 단계는 에셋 입력부터
   셰이더 출력, Inspector, 씬 저장/불러오기까지 하나의 수직 기능으로 끝낸다.
3. **Renderer는 계속 ECS를 모른다.** `Game/Systems`가 World를 `DrawItem`,
   `CameraView`, `LightingData`로 평탄화하고 `Graphics/`는 그 데이터만 받는다.
4. **패스는 명시적으로 유지한다.** `DrawShadowPass`, `DrawOpaquePass`,
   `DrawSkyboxPass`, `DrawTransparentPass`, `ResolveScene`, `DrawOverlay` 순서를
   코드에서 그대로 읽을 수 있게 한다.
5. **기존 재질은 기본값으로 이전과 같아야 한다.** flat normal map, opaque
   blend mode, shadow 비활성 기본값으로 새 필드가 기존 씬의 외형을 바꾸지 않는다.
6. **씬 포맷은 이전 버전을 읽는다.** 새 필드 때문에 writer 버전을 올릴 때도
   v1 씬은 합리적인 기본값을 채워 로드한다. Phase 10 왕복 보장을 깨지 않는다.
   **현재 코드는 이것을 못 한다** — `LoadScene`이 `version != kSceneVersion`으로
   정확히 일치할 때만 통과시킨다(10.5에서 `scene 99`가 거부되는 것을 검증했다).
   버전을 처음 올리는 단계에서 `version > kSceneVersion`만 거부하도록 먼저
   바꾼다. 아래 11.2 작업 항목 참조.
7. **트리거를 단계 경계마다 다시 본다.** 조건이 발화하면 별도 번호를 억지로
   예약하지 않고, 해당 단계 앞에 끼워 넣고 ROADMAP의 상태와 이유를 갱신한다.
8. **리소스 경로를 흩뿌리지 않는다.** 새 렌더링 코드는 논리 에셋 이름만
   `ResourceManager`에 넘긴다. `GetProjectRoot()` 의존을 추가하지 않아 Phase 12의
   배포 경로 정책 변경이 한곳에 머물게 한다.

최종 프레임의 패스 순서는 다음과 같다.

```text
Directional Shadow Depth
    → Opaque Scene
    → Skybox
    → Transparent Scene
    → MSAA Resolve (4x일 때만)
    → ImGui Overlay
```

---

## 3. 단계 개요

| 단계 | 상태 | 주제 | 핵심 학습 | 규모(예상) | 태그 |
|------|------|------|-----------|-----------|------|
| 11.0 | 완료 | 메시 AABB + 기존 엔티티 클릭 선택 | 로컬/월드 공간, ray-AABB | 중 | - |
| 11.1 | 완료 | 깊이 타깃 + 복수 패스/PSO 기반 | attachment 역할, PSO 전환 | 중 | - |
| 11.2 | 완료 | 6면 PNG 큐브맵 + 스카이박스 | texture array, `TextureCube` | 대 | - |
| 11.3 | 완료 | 탄젠트 생성 + 노멀 매핑 | 탄젠트 공간, TBN | 대 | - |
| 11.4 | 완료 | directional shadow depth pass | depth-only 렌더링, light space | 대 | - |
| 11.5 | 완료 | 그림자 적용 + 품질 보정 | comparison sampler, bias, PCF | 중 | - |
| 11.6 | 예정 | 블렌딩 + 투명 정렬 | blend state, depth write, 정렬 | 중 | - |
| 11.7 | 예정 | 4x MSAA + resolve | multisample resource, resolve | 대 | v1.2 |

11.0과 11.1은 기능 목록 앞의 선행 기반이다. 나머지는 가능한 한 서로의 구현에
기대지 않는 수직 기능이지만, MSAA는 모든 색상 PSO가 확정된 뒤 적용해야 PSO를
여러 번 다시 만들지 않는다.

---

## 4. 단계별 상세

### 11.0 메시 AABB + 기존 엔티티 클릭 선택

**목표**: 모든 메시가 로컬 공간 AABB를 갖게 하고, 그 하나의 데이터로
뷰포트 기존 엔티티 선택과 바닥 배치 높이 보정을 함께 해결한다.

ROADMAP의 AABB 트리거는 이미 발화했다. 현재 뷰포트 클릭은 선택한 에셋을
`y=0` 평면에 놓기만 하며, 배치된 기존 엔티티를 클릭하는 경로가 없다.

**작업 항목**

- [ ] `Bounds` 또는 `Aabb` 자료형: `min`, `max`, `Center()`, `Extents()`
- [ ] `MeshData::vertices`에서 로컬 AABB 계산
- [ ] GPU 업로드 뒤 CPU 정점을 버려도 남도록 `Mesh`에 AABB 보관
- [ ] 절차 메시와 OBJ가 같은 계산 경로를 쓰도록 **`Mesh.cpp`의
      `CreateMesh(device, const MeshData&)`** 에서 계산.
      `AddMesh`가 아니다 — `LoadMesh`는 `AddMesh`를 거치지 않고 `LoadObj` 뒤
      바로 `CreateMesh`를 부른다. 두 함수는 형제이고 합류점은 `CreateMesh`뿐이라,
      `AddMesh`에 넣으면 OBJ 메시만 AABB가 비어 조용히 선택되지 않는다
- [ ] `RayAabb` 교차 함수와 가장 가까운 양의 hit distance 반환
- [ ] 엔티티 world matrix의 역행렬로 월드 ray를 메시 로컬 공간에 변환
- [ ] 살아 있는 `Transform + MeshRenderer`를 순회해 가장 가까운 엔티티 선택
- [ ] 배치 모드 ON이면 기존 동작대로 배치, OFF이면 클릭 선택
- [ ] 빈 공간 클릭 시 선택 해제
- [ ] 배치 시 identity rotation/scale 기준 `-bounds.min.y`만큼 들어 올림

**설계 결정**

- 월드 AABB를 매번 만드는 대신 ray를 로컬 공간으로 보낸다. 회전된 메시도
  로컬 AABB라는 동일한 상자에 대해 검사할 수 있어 world AABB보다 덜 느슨하다.
- ray origin과 direction에 같은 역변환을 적용하고 direction을 재정규화하지 않는다.
  그러면 affine transform 전후의 매개변수 순서를 유지해 hit 비교가 단순하다.
- 배치 직후의 Transform은 회전 0, 스케일 1이므로 `-min.y` 보정이면 충분하다.
  이미 회전된 물체를 자동으로 다시 바닥에 붙이는 기능은 이번 범위가 아니다.
- `Graphics/`는 Entity를 모른다. AABB 데이터는 `Mesh`에 있지만 엔티티 순회와
  선택은 `Game/Picking` 또는 에디터 UI 계층에서 수행한다.

**함정**

- 음수 스케일과 singular transform(축 스케일 0)은 역행렬을 불안정하게 만든다.
  Inspector는 이미 `DragFloat3("Scale", …, 0.01f, 100.0f)`로 하한을 두지만
  **씬 파일은 그 제한을 거치지 않는다** — 손으로 편집했거나 다른 빌드가 쓴
  파일에는 0이나 음수가 들어올 수 있다. 가드는 Inspector가 아니라 픽킹
  지점에 있어야 한다.
- ray가 상자 안에서 시작하면 진입 거리가 음수일 수 있다. 이때 탈출 거리 또는
  0을 hit로 취급해야 카메라가 큰 메시 안에 있어도 선택할 수 있다.
- 바닥 메시 자체가 클릭을 가로챌 수 있다. 가장 가까운 hit가 바닥일 때의 선택은
  정상이며, 배치 모드에서만 레이-평면 경로를 우선한다.

**검증**

1. cube, pyramid, 크기가 다른 OBJ를 배치했을 때 바닥 아래로 파묻히지 않는다.
2. 앞뒤로 겹친 두 메시를 클릭하면 카메라에 가까운 엔티티가 선택된다.
3. 이동·회전·비균일 스케일된 메시도 화면에 보이는 위치에서 선택된다.
4. 빈 공간 클릭은 선택을 해제하고, 배치 모드 클릭은 기존처럼 새 엔티티를 만든다.
5. 저장→불러오기 뒤에도 같은 위치에서 선택할 수 있다. AABB는 메시에서 다시 온다.

**완료 기준**: 에디터 목록을 거치지 않고 뷰포트에서 기존 메시 엔티티를
선택할 수 있고, 새로 배치한 메시의 로컬 최저점이 `y=0`에 놓인다.

**결과**

- `Aabb`(min/max/Center/Extents/IsEmpty)를 `Mesh.h`에, 계산은 **`CreateMesh`**
  에 넣었다. 계획 검토에서 짚은 대로 `AddMesh`가 아니라 여기가 절차 메시와
  OBJ의 합류점이다. CPU 정점이 사라지기 직전의 마지막 지점이기도 하다
- 빈 메시는 0이 아니라 **뒤집힌 상자**(`min > max`)로 둔다. 0 상자는 원점에
  앉아 그쪽을 향한 레이를 삼킨다
- `RayToLocalSpace`는 방향을 **재정규화하지 않는다.** 길이를 그대로 두면
  `t`가 월드 레이 단위로 남아 서로 다른 오브젝트의 hit를 그냥 비교할 수 있다.
  원점은 `TransformCoord`, 방향은 `TransformNormal` — 바꿔 쓰면 오브젝트
  위치만큼 레이가 두 번 밀린다
- `XMMatrixInverse`의 determinant로 특이 행렬을 걸러낸다. Inspector는 스케일
  하한이 있지만 씬 파일은 그 제한을 거치지 않는다는, 계획 검토에서 짚은 지점
- 상자 안에서 시작한 레이는 진입 거리가 음수다. 0으로 보고해야 카메라가 큰
  메시 안에 있어도 그것을 고를 수 있다
- 선택은 구조 편집이 아니므로(배열 모양이 안 바뀐다) 명령 큐를 거치지 않고
  즉시 반영한다. 뒤에 그려지는 Entities/Inspector가 같은 프레임에 따라온다

**검증**

| 항목 | 결과 |
|------|------|
| 배치 높이 | Sphere.obj 배치 시 `Position Y = 1.000` (= `-min.y`), 바닥에 정확히 접함 |
| 클릭 선택 | 흩어놓은 구 3개를 각각 클릭 → Entity #3 / #4 / #5 |
| 빈 공간 | `Nothing selected.` |
| 비균일 스케일 | `Pyramid_squashed` (Scale 4.000 / 0.800 / 4.000) 화면에 보이는 위치에서 선택됨 |
| 저장→재시작→불러오기 | 같은 위치 클릭으로 동일 엔티티 선택. AABB는 씬 파일이 아니라 메시에서 다시 온다 |
| 원거리 클리핑 | far plane 밖 엔티티는 선택되지 않음 (아래) |
| 디버그 레이어 | 메시지 0건, Debug/Release 종료 코드 0 |

**픽킹 범위를 렌더링 범위에 맞춘다** (리뷰 지적)

`RayFromNdc`는 방향을 `(far point − near point)`로 만든다. 따라서 **`t = 0`이
근평면, `t = 1`이 원평면**이고 `0..1`이 정확히 렌더러가 그리는 깊이 구간이다.
`RayToLocalSpace`가 원점과 방향에 같은 행렬을 적용하므로
`O + t·D → O′ + t·D′` 로 **`t`가 로컬 공간까지 그대로 보존된다** — 방향을
재정규화하지 않는 이유가 여기서 두 번째로 값을 한다.

처음 구현은 양수 거리를 전부 받아들여서, `farZ` 너머에 있어 **화면에 그려지지도
않은** 엔티티가 선택될 수 있었다. 빈 배경을 클릭했는데 보이지 않는 물체가
잡히는 셈이다. `distance > 1.0f`를 제외하도록 고쳤다.

같은 씬에서 `farZ`만 바꾼 A/B로 확인했다. 카메라 (0, 3.5, −22),
`InsideFar`는 상자 z 70..170, `BeyondFar`는 450..550.

| `farZ` | 클릭 지점 | 결과 |
|--------|-----------|------|
| 200 (원평면 z=178) | 화면에 보이는 구 | `Entity #2` (InsideFar) |
| 200 | 잘려서 **안 보이는** 구의 상자 위치 | **Nothing selected** |
| 600 | **같은 좌표** | `Entity #3` (BeyondFar) |

세 번째 줄이 두 번째 줄과 같은 픽셀이라는 점이 핵심이다 — 레이는 실제로 그
상자를 통과하고 있고, 제외한 것은 원평면이 그림에서 지운 것뿐이다.

**드러난 한계 — AABB가 그 안의 오브젝트를 삼킨다**

데모 씬에서 어디를 클릭해도 `Laevat`이 선택됐다. 8 단위로 맞춘 뒤 2.6배
스케일이라 약 21 단위 상자이고, `Sphere`(-7,3,-4)와 `Torus`(7,3,-4)가 그
상자 **안에** 있다. 상자 앞면이 두 물체보다 카메라에 가까우니 최근접
규칙상 Laevat이 이긴다 — 동작은 정확하고, 뾰족한 모델에 상자를 씌운
결과가 그럴 뿐이다.

계획대로 bounds-only를 유지하고 ROADMAP에 **삼각형 단위 픽킹** 트리거를
등록했다. 검증은 Laevat을 지운 데모 씬과 새 씬에서 진행했다.

---

### 11.1 깊이 타깃 + 복수 패스/PSO 기반

**목표**: 현재 화면을 바꾸지 않으면서 깊이 전용 표면과 여러 렌더 상태를
표현할 최소 구조를 만든다. 이후 단계가 `Renderer::DrawScene` 하나를 계속
복제하지 않게 한다.

**작업 항목**

- [o] `DepthTarget` 클래스 추가: resource, DSV, 선택적 SRV, 크기, sample count
- [o] 기존 `RenderTarget`의 깊이 리소스를 `DepthTarget`으로 교체
- [o] scene depth는 DSV 전용, shadow depth는 DSV+SRV가 가능하도록 생성 옵션 분리
- [o] shader-readable depth는 typeless resource + DSV/SRV view format 조합 사용
- [o] PSO 생성을 공통 descriptor를 복사·수정하는 helper로 정리
- [o] 단일 `m_pipelineState`를 역할별 PSO 저장 구조로 변경
- [o] viewport/scissor, object CB, mesh buffer 바인딩 공통 코드 추출
- [o] 최종 패스 순서가 드러나는 명시적 함수 골격 마련
- [o] 현 단계에서는 opaque scene + ImGui만 실행해 이전 결과 유지

**설계 결정**

- `RenderTarget`에 `hasColor` 같은 플래그를 넣고 null RTV를 허용하지 않는다.
  `DepthTarget`은 독립 타입이고, 기존 scene `RenderTarget`은 색상 attachment와
  `DepthTarget`을 조합한다.
- 아직 Render Graph, pass dependency compiler, resource-state tracker는 만들지 않는다.
  리소스 배리어와 호출 순서는 각 명시적 패스가 책임진다.
- PSO는 최소한 `Opaque`, `Skybox`, `Transparent`, `ShadowDepth` 역할로 구분한다.
  아직 존재하지 않는 셰이더의 PSO는 해당 기능 단계에서 추가한다.
- 루트 시그니처는 무조건 하나로 통합하지 않는다. scene 계열이 공유할 수 있으면
  공유하되, shadow depth처럼 필요한 입력이 다른 패스는 작은 별도 시그니처를 허용한다.

**함정**

- DSV와 SRV를 함께 만들 깊이 리소스는 resource format을 `D32_FLOAT`로 바로
  만들면 SRV view format과 호환되지 않는다. typeless resource가 필요하다.
- descriptor slot은 리사이즈에도 유지하고 view 내용만 다시 써야 한다.
- PSO의 `RTVFormats`, `DSVFormat`, `SampleDesc`는 실제 패스 attachment와 정확히
  일치해야 한다. 일치하지 않으면 Debug Layer 오류 또는 draw 실패다.

**검증**

1. 리팩터 전후 동일 카메라의 Scene Viewport 스크린샷을 픽셀 비교한다.
2. viewport 리사이즈, 접기/펼치기, 종료를 반복해 lifetime 오류가 없는지 확인한다.
3. Debug Layer 경고·에러 0, Debug/Release 빌드 성공.

**완료 기준**: 기존 씬 외형과 조작은 그대로이고, 색상 없이 DSV만 소유할 수 있는
타입과 역할별 PSO를 추가할 자리가 생긴다.

**결과**

- `Graphics/DepthTarget` 신설. `RenderTarget`은 **색상만** 남았고, 깊이는
  패스가 둘을 조합해 쓴다. 하나의 클래스에 `hasColor` 플래그를 다는 대신
  타입을 나눈 계획대로다
- **하나의 깊이 버퍼에 포맷 이름이 셋이다.** 샘플링하지 않는 깊이는 그냥
  `D32_FLOAT`여도 되지만, 샘플링할 깊이는 그럴 수 없다 — depth-stencil
  포맷의 SRV는 무효다. 그래서 리소스를 `R32_TYPELESS`(해석 없는 비트)로 만들고
  DSV는 `D32_FLOAT`, SRV는 `R32_FLOAT`로 각자 읽는 법을 지정한다.
  샘플링하지 않을 때는 `DENY_SHADER_RESOURCE`까지 붙여 드라이버가 압축 레이아웃을
  쓸 수 있게 한다
- `SceneShadedPsoTemplate()` — 모든 씬 PSO가 공유하는 상태를 한곳에.
  특히 `RTVFormats`/`DSVFormat`/`SampleDesc`는 실제 attachment와 어긋나면
  디버그 레이어 에러이거나 조용한 draw 실패라, 맞출 곳이 패스마다가 아니라
  한 곳이어야 한다. 11.7의 1x/4x variant도 이 템플릿 한 줄로 갈린다
- `m_pipelineState` 하나 → `PsoRole { Opaque, Skybox, Transparent, ShadowDepth }`
  로 인덱싱하는 배열. 아직 없는 셰이더의 슬롯은 null이고, 해당 단계가 채운다
- `BindScenePass(frame, role)` / `DrawItems(frame, items)`로 공통 바인딩을
  뽑았다. 패스를 추가하는 일이 "역할을 고르는 것"이 되고, 여섯 개의 bind 호출을
  복사해 동기화를 유지하는 일이 아니게 된다
- `Render()`가 최종 패스 순서를 주석으로 나열한다. 각 패스는 자기 배리어를
  자기가 책임진다 — Render Graph도 리소스 상태 추적기도 아직 만들지 않는다
- `DSVFormat`은 **뷰 포맷**이라는 점을 상수 이름(`kSceneDepthViewFormat`)에
  남겼다. 리소스 포맷과 갈리는 순간이 곧 온다

**검증**

1. **리팩터 전후 픽셀 비교** — 애니메이션이 없는 정적 씬(`Spin` 없음 →
   `SpinSystem`도 `LightOrbitSystem`도 아무것도 못 움직인다)을 만들어 같은
   카메라에서 캡처했다.

   | | |
   |---|---|
   | 비교한 뷰포트 픽셀 | 317,460 |
   | 다른 픽셀 | **0** |
   | 최대 채널 합 차이 | **0** |

   패널 영역은 제외했다 — 프레임 타임 그래프와 fps는 매 실행 달라지고
   렌더러가 바뀌었는지에 대해 아무것도 말해주지 않는다.
2. 뷰포트 그립 드래그(660×500 → 확대 → 축소), 접기/펼치기, OS 창 리사이즈,
   정상 종료를 연속 수행 — 크래시·lifetime 오류 없음, 종료 코드 0
3. 디버그 레이어 경고·에러 0건, Debug/Release 빌드

---

### 11.2 6면 PNG 큐브맵 + 스카이박스

**목표**: WIC가 읽을 수 있는 PNG 여섯 장을 하나의 cube texture로 업로드하고,
카메라를 둘러싼 배경으로 샘플링한다.

**에셋 정책**

```text
Assets/Skyboxes/<name>/px.png
Assets/Skyboxes/<name>/nx.png
Assets/Skyboxes/<name>/py.png
Assets/Skyboxes/<name>/ny.png
Assets/Skyboxes/<name>/pz.png
Assets/Skyboxes/<name>/nz.png
```

DDS 로더는 추가하지 않는다. 이 단계의 학습 목표는 texture array subresource와
`TextureCube` SRV이며, DDS의 압축 포맷·mip chain·컨테이너 파싱은 별도 주제다.

**작업 항목**

- [o] **씬 포맷 버전 정책 선행 작업** (Phase 11에서 포맷을 처음 바꾸는 단계다)
      - `LoadScene`의 버전 검사를 `!=`에서 `>`로 — 미래 버전만 거부하고
        과거 버전은 읽는다. 지금은 v1 씬이 통째로 거부된다
      - `kSceneVersion`을 2로 올리고, 읽은 버전을 파서가 알 수 있게 보관
      - v1 파일에 없는 새 키워드는 기본값으로 남긴다(= 아무것도 안 하면 된다)
      - 검증: 10.5에서 만든 v1 씬이 그대로 열리고, 다시 저장하면 v2가 된다
- [o] `CubeTextureHandle`과 cube texture cache 추가
- [o] 논리 이름 하나를 위 6개 파일 경로로 해석하는 `LoadCubeTexture`
- [o] 여섯 이미지가 정사각형이고 크기·포맷이 같은지 검증
- [o] `DepthOrArraySize = 6`인 texture resource 생성
- [o] `GetCopyableFootprints`로 6개 서브리소스 업로드
- [o] `D3D12_SRV_DIMENSION_TEXTURECUBE` SRV 생성
- [o] 방향을 확인할 색상·문자 레이블 테스트 큐브맵 에셋 추가
- [o] Asset Browser가 유효한 `Skyboxes/<name>` 디렉터리를 논리 에셋 하나로 표시
- [o] skybox 전용 VS/PS, root signature 또는 scene signature 조합 추가
      *(scene signature 재사용 + `PassConstants`에 `skyViewProj` 추가)*
- [o] translation을 제거한 camera view와 far-depth 출력
- [o] skybox PSO: depth `LESS_EQUAL`, depth write OFF, 내부 면이 보이는 cull 설정
- [o] `Environment`에 선택적 skybox 필드 추가
- [o] Inspector 선택과 씬 저장/불러오기 지원

**설계 결정**

- 2D와 cube handle을 타입으로 구분해 잘못된 SRV 차원을 재질에 바인딩하지 않는다.
- cube texture의 cache key와 씬 파일에는 여섯 개의 실제 경로가 아니라
  `Skyboxes/<name>`이라는 논리 이름 하나만 저장한다.
- skybox는 ECS 메시 엔티티로 만들지 않고 scene의 `Environment` 속성으로 둔다.
  배경은 위치·충돌·일반 Material을 갖는 오브젝트가 아니기 때문이다.
- opaque geometry 뒤, transparent geometry 앞에 그린다. depth를 far plane으로
  보내 이미 그려진 픽셀을 통과하지 않게 한다.
- 첫 에셋은 미관보다 방향 검증이 목적이다. 최종용 에셋을 추가한다면 직접 제작했거나
  재배포 가능한 라이선스인지 기록한다.

**함정**

- Direct3D cube face의 방향과 이미지의 위쪽 방향을 혼동하면 face 경계가 이어지지 않는다.
  각 면에 `+X/-X/+Y/-Y/+Z/-Z`와 위쪽 화살표가 있는 테스트 에셋으로 먼저 교정한다.
- skybox에 일반 scene view를 쓰면 카메라 이동에 따라 배경이 가까워진다.
  rotation만 남긴 view를 사용해야 한다.
- scene color가 MSAA가 되기 전 단계이므로 sample count는 아직 1이다.

**검증**

1. 카메라를 회전하면 올바른 face가 보이고 모서리 방향이 이어진다.
2. 카메라를 멀리 이동해도 스카이박스의 겉보기 위치와 크기는 변하지 않는다.
3. opaque 물체 뒤에서는 보이지 않고 빈 배경 픽셀만 채운다.
4. skybox가 없는 v1 씬은 기존 clear color를 배경으로 정상 로드된다.
5. 저장→재시작→불러오기 뒤 같은 cube texture가 복원된다.

**완료 기준**: Scene Viewport가 단색 clear color 대신 선택한 큐브맵에 둘러싸이고,
카메라 이동·회전과 깊이 가림이 올바르다.

**결과**

- 씬 포맷 버전 정책부터. `version != kSceneVersion`을 **`>`** 로 바꿔
  "미래 버전만 거부"로 만들고 `kSceneVersion`을 2로 올렸다. 이걸 먼저 하지
  않았으면 skybox 필드를 추가하는 순간 v1 씬이 전부 거부됐다
- skybox는 **`environment` 줄의 선택적 꼬리**다. 세 숫자에서 멈추는 리더도
  유효한 Environment를 얻는다 — 그것이 이 변경을 additive하게 만든다
- **리소스에는 "큐브"라는 개념이 없다.** `DepthOrArraySize = 6`인 2D 텍스처를
  만들고, `D3D12_SRV_DIMENSION_TEXTURECUBE` **뷰**가 그것을 방향으로
  샘플링되게 한다. 같은 6장을 `TEXTURE2DARRAY`로 보면 인덱스로 샘플링된다
- 6면은 GPU에 무엇을 만들기 **전에** 전부 디코드하고 정사각형·동일 크기를
  검사한다. 실패가 반쯤 만들어진 리소스를 남기지 않는다
- 업로드는 `GetCopyableFootprints`로 6개 서브리소스 레이아웃을 받아 한 업로드
  버퍼에 담는다. 오프셋을 `edge * edge * 4`로 계산하지 않는 이유가 이것이다
- 스카이박스는 **씬 루트 시그니처를 그대로 쓴다.** 두 번째 시그니처 대신
  `PassConstants`에 `skyViewProj`를 하나 더 넣었다. b0는 안 쓰지만 바인딩만 해둔다
- 하늘이 무한히 멀어 보이게 하는 두 가지: 뷰 행렬의 **translation 행 제거**
  (`skyView.r[3] = (0,0,0,1)`)와 정점 셰이더의 `z = w`(= 깊이 1.0)
- 그래서 PSO가 템플릿에서 셋만 바꾼다: `CULL_MODE_FRONT`(상자 안에 있으니
  보이는 건 뒷면), `LESS_EQUAL`(클리어된 깊이도 1.0이라 `LESS`로는 절대 통과
  못 한다), `DEPTH_WRITE_MASK_ZERO`(먼 평면을 써버리면 이후에 그릴 것이 가려진다)
- opaque **뒤에** 그린다. 먼저 그리면 모든 픽셀을 셰이딩한 뒤 대부분을 덮어쓴다
- 씬 타깃의 상태 전이가 이제 여러 패스를 감싸므로 `Render()`로 올라갔다.
  두 패스가 같은 타깃에 그리는 순간 "누가 소유하는가"는 답이 없는 질문이 된다

**검증**

1. **v1 씬 하위 호환** — 10.5 시절 형식 그대로인 `scene 1` 파일이 v2 리더로
   정상 로드되고, skybox 없이 기존 clear color 배경이 나온다
2. **방향 검증** — 각 면에 축 이름과 UP 화살표가 있는 테스트 큐브맵을 만들었다
   (`Assets/Skyboxes/Test/`, 직접 생성한 것이라 라이선스 문제 없음).
   카메라를 오른쪽으로 돌리면 `+X`(빨강), 위로 올리면 `+Y`(초록)가 나오고
   **UP 화살표가 seam을 가로질러 이어진다** — 면 순서와 상하 반전을 한 번에 잡는다
3. **카메라 이동 독립성** — 같은 씬에서 카메라 z만 −22 → +60(82 유닛)으로
   옮겨 하늘 영역을 픽셀 비교:

   | | |
   |---|---|
   | 비교한 하늘 픽셀 | 162,500 |
   | 다른 픽셀 | **0** |

   회전은 동일하므로 이동만으로는 배경이 **한 픽셀도** 움직이지 않는다
4. **깊이 가림** — 바닥과 큐브가 하늘을 정상적으로 가리고, 하늘은 빈 배경
   픽셀만 채운다
5. **저장→재시작→불러오기** — 파일에 `scene 2`와
   `environment ... skybox "test"`가 남고, 다른 프로세스에서 열면 같은 큐브맵이
   복원된다. 경로 정규화 정책대로 이름은 소문자 `test`로 저장된다
6. skybox 해제/재지정, 회전 반복 후 디버그 레이어 0건, Debug/Release 빌드

**측정이 한 번 틀렸던 것**: 이동 독립성 비교에서 처음엔 162,500 픽셀이 전부
달랐다. Open 메뉴 항목을 잘못 눌러 skybox가 **없는** v1 씬과 비교하고 있었다
(상태 표시줄이 `V1Legacy.scene`이라고 말해주고 있었다). 올바른 씬으로 다시 재니
차이가 0이었다.

---

### 11.2.5 멀티 머티리얼 Submesh — **트리거 발화로 끼워 넣음**

계획서 원칙 7("조건이 발화하면 별도 번호를 억지로 예약하지 않고 해당 단계
앞에 끼워 넣는다")대로 11.3 착수 전 게이트에서 발화해 여기 들어왔다.

**게이트 판정 근거**

`laevat.obj`가 전체 다운로드(`.obj` + `.mtl` + 텍스처 10장)로 교체되면서
조건이 갖춰졌다.

| 항목 | 값 |
|------|-----|
| 선언 | `mtllib laevat.mtl`, 고유 `usemtl` **6개** |
| 면 분포 | 40.5% / 30.7% / 15.9% / 8.0% / 4.5% / 0.4% — 무시할 재질이 없다 |
| `.mtl` | 6개 재질이 각각 다른 diffuse (face / iris / body / cloth_01 / cloth_02 / hair) |
| 함께 온 것 | 노멀 맵 4장 — **11.3에 직결** |
| 발화 전 상태 | 단일 `Crate.png`로 렌더 → 면의 40.5%가 엉뚱한 텍스처 |

"여러 텍스처를 쓰는 실제 모델이 들어와 깨져 보이는 순간"이라는 트리거 문구
그대로였다. 가상의 필요가 아니라 손에 들어온 에셋이 조건을 만들었다.

**한 일**

- `Graphics/Handles.h` 신설. `Mesh`가 submesh의 텍스처를 가리켜야 하는데
  `ResourceManager.h`가 `Mesh.h`를 include하고 있어 순환이 됐다. 핸들 세
  종류를 자기 헤더로 뺐다
- `Submesh { indexOffset, indexCount, texture, materialName }`.
  **정점/인덱스 버퍼는 하나로 공유하고 draw만 쪼갠다** —
  `StartIndexLocation`이 그 일을 한다
- 모든 메시는 **최소 하나의 submesh**를 갖는다. 절차 메시와 단일 재질 OBJ도
  전체를 덮는 submesh 하나가 자동으로 생기므로, 그리는 쪽에 "submesh가 있나?"
  분기가 없다
- OBJ 로더가 `mtllib`/`usemtl`을 파싱한다. 로더는 `ResourceManager`를 모르므로
  텍스처 **경로**만 넘기고, 핸들 변환은 매니저가 한다
- `.mtl`의 경로가 세 가지로 틀려 있었다:
  ```
  map_Kd laevat_textures/T_actor_laevat_face_01_D
  ```
  없는 하위 폴더, 빠진 확장자, 다른 대소문자. 파일 **이름만** 믿고 `.obj`
  옆에서 찾되 `.png`를 붙여보는 식으로 해결했다. 대소문자는 기존 경로 정규화
  정책이 흡수한다

  **경로 복구와 디코더 지원은 서로 다른 문제다.** 지금 상태를 정확히 적으면:

  | `map_Kd`가 쓴 것 | 결과 |
  |---|---|
  | `foo.jpg` (확장자 명시) | **된다** — 첫 후보가 그대로 맞고 WIC가 JPEG를 디코딩 |
  | `foo` (확장자 생략) | PNG만 복구 후보라 **PNG일 때만** 된다 |
  | `foo.tga` | 경로는 찾지만 **디코딩이 안 된다** — Windows 기본 WIC에 TGA 코덱이 없다 |

  즉 확장자를 생략한 비-PNG는 *경로 복구* 문제이고, TGA는 그와 별개인
  *코덱* 문제다. 전자는 후보 목록을 늘리면 되고, 후자는 디코더를 붙여야 한다
- `BuildRenderData`가 submesh당 `DrawItem` 하나를 만든다. 텍스처는 submesh의
  것이 이기고, **색·스페큘러·광택은 항상 MeshRenderer의 것**이다 — 그쪽이
  Inspector가 편집하는 값이다
- 첫 `usemtl` **앞의** 면은 이름 없는 submesh로 받는다. 없으면 조용히
  사라지는데, 에러 없이 지오메트리가 없어지는 것이 최악이다
- Asset Browser 스캔을 재귀로. 다운로드 모델은 폴더로 오므로 최상위만
  훑으면 안 보인다. `Skyboxes`와 `Scenes`는 제외

**검증**

| 항목 | 결과 |
|------|------|
| 드로우 콜 분할 | 15 drawn → **20 drawn** (laevat 1 → 6), 19 엔티티 그대로 |
| 화면 | 분홍 머리·은색 갑옷·피부가 각자의 텍스처로. 이전에는 전부 갈색 체커 |
| 단일 재질 회귀 | Sphere/Torus/절차 메시는 이전과 동일하게 보인다 |
| 디버그 레이어 | 엔진 PID 기준 0건 |
| 빌드 | Debug/Release |

**측정 함정 하나**: DBWIN 캡처는 **시스템 전역**이라 `Error: 31`이 여섯 번
잡혔는데, PID를 확인해보니 Discord였다. 엔진 PID로는 0건. 앞으로 이 캡처는
PID로 걸러 읽어야 한다.

**뒤이어: 기본 씬에서 laevat 제거**

Submesh를 발화시킨 그 모델을 `BuildWorld`에서 뺐다. 세 가지 이유가 겹친다.

- 재배포할 수 없는 43 MB 서드파티 에셋이라 `.gitignore`로 로컬 전용이 됐다.
  엔진이 **기본으로 만드는 씬**이 저장소에 없는 파일에 의존하면, 없을 때
  창도 못 띄우고 죽는다 — 실제로 오늘 종료 코드 −1로 그랬다
- 24 MB `.obj` 파싱이 **매 실행마다** 들어갔다

| | 첫 창이 뜰 때까지 |
|---|---|
| laevat 포함 | **6,421 ms** |
| 제거 후 | **501 / 434 / 427 ms** |

  14배다. 자동 검증을 수십 번 돌리는 이 프로젝트에서는 그 자체로 값이 크다
- 216,912 정점짜리 캐릭터는 렌더링을 가르치는 기본 씬의 소재가 아니다

기능을 잃지는 않았다. 에셋 브라우저에 `laevat\laevat.obj`가 그대로 보이고,
클릭하면 그때 로드된다 — 10.3에서 만든 지연 로드가 정확히 이 용도다.
씬은 18 엔티티 / 14 drawn(=20−6)으로 줄었고 나머지는 그대로다.

**이어서: `BuildWorld`를 절차 메시만으로**

laevat을 뺐어도 `Sphere.obj`와 `Torus.obj`가 남아 있었고, 이 둘도 똑같은
문제를 안고 있었다. `.gitignore` 110행의 `*.obj`는 VS 템플릿이 넣어준
**컴파일러 출력 제외 규칙**인데, 3D 모델도 같은 확장자라 함께 걸린다.
확인해보니 새로 클론한 사람이 받는 에셋은 9개뿐이다.

```
Engine/Assets/.gitkeep, Crate.png, Floor.png
Engine/Assets/Skyboxes/Test/{px,nx,py,ny,pz,nz}.png
```

`.obj`는 **한 개도 없다.** 즉 클론 직후 실행하면 laevat 때와 똑같이 죽는다.
"기본 씬은 저장소에 없는 파일에 의존하면 안 된다"는 방금 세운 원칙을
절반만 지킨 셈이었다.

그런데 그냥 지울 수는 없었다. **큐브·피라미드·바닥은 전부 평면이라
곡면이 하나도 안 남는다.** 평면만 있으면 스페큘러 하이라이트가 면 단위로
켜지고 꺼질 뿐이라 부드러운 노멀이 무슨 일을 하는지 보이지 않고, 바로 다음
11.3 노멀 매핑과 11.5 그림자를 확인할 곳이 없어진다. 그래서 **구와 도넛을
절차 메시로 추가**했다 — `#sphere`, `#torus`.

와인딩이 이 작업의 유일한 함정이다. 래스터라이저는 시계 방향을 앞면으로
보는데, 매개변수 두 개짜리 격자에서 부호를 뒤집으면 도형이 통째로
뒤집힌다. 큐브의 규칙(**바깥에서 봤을 때** 좌상→우상→우하→좌하)을 그대로
쓰려면 두 매개변수가 각각 어느 방향으로 움직이는지 알아야 한다.

| | 첫 매개변수 | 둘째 매개변수 | 결과 |
|---|---|---|---|
| 구 | φ 증가 = **아래** | θ 증가 = **오른쪽** | 큐브와 동일한 순서 |
| 도넛 | α 증가 = **위** | β 증가 = **오른쪽** | 위/아래가 반대 → `top`이 `major+1` |

도넛만 행이 뒤집히는 이유는 α가 링을 도는 각이고 β가 튜브를 도는 각이라,
α=0·β=0에서 ∂P/∂α = +Y(위), ∂P/∂β = +Z(오른쪽)이기 때문이다.

검증은 눈으로 하지 않았다. 뒤집힌 구는 여전히 동그랗게 보여서 스크린샷으로
구별이 안 된다. 대신 **큐브를 기준자로 삼았다** — Phase 4부터 맞게 그려져
온 도형이니 그 부호가 정답이다. 삼각형마다
`cross(v1−v0, v2−v0)`와 저장된 노멀의 내적 부호를 재서 큐브와 같은지 봤다.

| 도형 | 삼각형 | 일치 | 불일치 | 퇴화 | 최소 내적 |
|---|---|---|---|---|---|
| cube | 12 | 12 | 0 | 0 | 1.0000 |
| pyramid | 6 | 6 | 0 | 0 | 1.0000 |
| floor | 2 | 2 | 0 | 0 | 1.0000 |
| **sphere** | 1024 | 960 | **0** | 64 | 0.9988 |
| **torus** | 2304 | 2304 | **0** | 0 | 0.9987 |

퇴화 64개는 극 두 줄 × slices 32 = 예측과 정확히 일치한다. UV 구는 극에서
한 점으로 모이므로 그 줄의 삼각형은 넓이가 0이고, 래스터라이저가 버린다.
극을 특별 취급하는 분기를 넣지 않는 대가로 공짜다. 최소 내적이 1.0이
아닌 것도 정상 — 곡면에서는 면 노멀과 정점 노멀 평균이 살짝 다르다.

경계 상자도 해석적으로 맞췄다.

| | 예측 | 실측 |
|---|---|---|
| sphere(r=1) | ±1 전 축 | `(−1,−1,−1)~(1,1,1)` |
| torus(R=1,r=0.4) | XY ±1.4, Z ±0.4 | `(−1.4,−1.4,−0.4)~(1.4,1.4,0.4)` |
| 튜브 반지름 오차 | 0 | **1.49e−07** (float 정밀도) |

화면으로는 도넛이 결정적이었다. **구멍 너머로 바닥이 보이고** 튜브 안쪽
먼 벽이 제대로 음영진다. 와인딩이 뒤집혔다면 가까운 면이 잘려나가고
속이 빈 껍데기가 이상하게 밝은 채로 보였을 것이다.

마지막으로 목적 자체를 검증했다. `Sphere.obj`·`Torus.obj`를 잠시 옮겨두고
Release를 실행했더니 **405 ms만에 창이 뜨고 그대로 돌았다.** 클론 직후와
같은 조건이다.

`.obj`를 지우지는 않았다 — 에셋 브라우저에서 여전히 불러올 수 있는 선택
에셋이고, 파일 메시 경로가 죽지 않았음을 확인하는 데도 쓴다.

**드러난 결함: 에디터로 배치하면 큰 모델이 사라진다**

`BuildWorld`에서 laevat을 빼자 **에셋 브라우저가 원래 고장나 있었다는 게**
드러났다. `LoadMesh`의 캐시 키는 파일 이름뿐이고 `fitToSize`는 키에 들어가지
않는다. 그래서 예전에는:

- `BuildWorld`가 **먼저** `LoadMesh(L"laevat/laevat.obj", 8.0f)`로 올림
- 브라우저가 나중에 같은 이름을 요청 → 캐시 히트 → **이미 보정된 메시**

브라우저가 제대로 동작한 게 아니라 `BuildWorld`의 부작용에 얹혀 있었다.
이제 아무도 미리 안 올리니 브라우저가 첫 로더가 되고,
`LoadMesh(entry.fileName)`은 `fitToSize` 기본값이 `0.0f`라 원본이 온다.

원본 laevat.obj의 실측(정점 216,912개 직접 스캔, 로더의 Z 부호 반전 반영):

| | 값 |
|---|---|
| 크기 | **38,593 × 43,863 × 64,453** |
| 중심 | (0, 9,386, −34,123) |
| 카메라 far plane | 200 |

far plane를 넘는 정도가 아니라 **전부 카메라 뒤**에 놓인다. 로드는 성공하고
엔티티도 생기는데 화면에는 흔적이 없다.

고치는 방법으로 브라우저에 `fitToSize`를 넘기는 건 택하지 않았다. 정점에
굽는 방식이라 캐시에 축소본이 박히고 원본으로 되돌릴 수 없으며, 인스펙터에
숫자가 안 보인다. 대신 **배치할 때 `Transform.scale`을 계산**한다 — 크기는
Transform이 갖는 것이지 정점이 갖는 게 아니고, 사용자가 보고 고칠 수 있다.

범위 밖일 때만 클램프하므로 절차 메시와 정상 크기 파일은 scale 1 그대로다.
상한 20은 80유닛 바닥의 1/4이고, 옛 하드코딩 배치의 실효 크기
(fitToSize 8 × scale 2.6 = 20.8)와도 거의 같다. **하한 0.25도 같은 이유로
필요하다** — 밀리미터 단위로 만든 파일은 천 배 작게 도착해 똑같이 안 보인다.

XZ 중심 정렬도 함께 넣었다. 기존 `-min.y` 안착은 Y만 봤는데, 원점이 형상
밖에 있는 모델은 클릭한 자리가 아닌 엉뚱한 곳에 놓인다. 모든 오프셋은 같은
scale을 거친다.

예측과 실측:

| | 예측 | 인스펙터 실측 |
|---|---|---|
| scale | 20 / 64,452.5 = **3.1031e−4** | **0.000310** |
| position.y | `−min.y × scale` = **3.893** | **3.893** |
| 월드 크기 | 11.98 × 13.61 × 20.00 | 바닥에 안착, 화면에 보임 |
| 드로우 | 14 → **20** (+6 submesh) | 19 엔티티 / 20 drawn |

**딸려 나온 인스펙터 결함 2개.** scale이 4자릿수를 넘나들게 되자
`DragFloat3("Scale", …, 0.05f, 0.01f, 100.0f)`의 문제가 드러났다.

- 기본 포맷 `%.3f`가 3.1e−4를 **`0.000`으로 표시** — 퇴화한 오브젝트로 읽힌다
- `v_min`이 **0.01**이라 그 필드를 건드리는 순간 32배로 튄다
- 드래그 속도 0.05는 3.1e−4짜리 값에서 한 픽셀에 160배

`ImGuiSliderFlags_Logarithmic` + `"%.5g"` + 하한 1e−5로 바꿔 드래그를
곱셈으로 만들었다. 컨트롤 하나가 3.1e−4와 100을 모두 편집하려면 이 방법뿐이다.

씬 직렬화는 확인만 하고 손대지 않았다. `Scene.cpp`가 `file.precision(9)`에
기본 부동소수 포맷이라 3.1e−4도 왕복하고, `operator>>`는 지수 표기를 읽는다.

**같은 버그의 다른 입구: `Assign mesh`**

배치를 고쳐도 인스펙터의 `Assign mesh`는 `renderer->mesh = browserMesh;`
한 줄이라 Transform을 안 건드린다. scale 1짜리 기존 엔티티에 laevat을
assign하면 **다시 안 보인다.**

여기서 자동 클램프는 오히려 틀리다. `Backdrop`처럼 scale `(6, 3, 0.5)`를
일부러 준 엔티티의 값을 메시 교체가 덮어쓰면 사용자 작업을 지우는 것이다.
**배치는 "새로 만드니 기본값을 달라", assign은 "있는 걸 고치니 건드리지
말라"** 로 의미가 다르다.

그래서 몰래 하지 않고 **`Fit` 버튼**으로 분리했다 — 사용자가 요청할 때만
적용된다. 규칙이 갈라지지 않도록 클램프는 `FitScaleFor(bounds)` 한 곳으로
빼서 배치와 버튼이 같은 함수를 쓴다.

`Fit`은 scale과 **Y만** 바꾼다. scale만 맞추면 laevat의 발이 바닥 아래
3.9 유닛에 묻히는데 그건 아무도 "fit"이라고 부르지 않는다. 반대로 XZ는
건드리지 않는다 — 사용자가 놓은 자리이고, 크기가 바뀐다고 옮길 이유가 없다.
(배치는 클릭 지점이 곧 의도이므로 XZ 중심 정렬을 한다. 두 동작의 차이는
이 의미 차이에서 나온다.)

검증 — `Crate_00`(scale 1, position −8, 1, −8)에 laevat을 assign:

| 단계 | drawn | mesh handle | scale | position |
|---|---|---|---|---|
| 시작 | 14 | 0 (cube) | 1 | (−8, **1**, −8) |
| `Assign mesh` | **19** (=14−1+6) | **5** | 1 | (−8, 1, −8) — **화면에서 사라짐** |
| `Fit` | 19 | 5 | **0.000310** | (−8, **3.893**, −8) — **다시 보임** |

X·Z가 −8 그대로인 것이 설계대로 동작했다는 증거다.

---

### 11.3 탄젠트 생성 + 노멀 매핑

**목표**: OBJ와 절차 메시 모두 일관된 tangent basis를 갖게 하고, 재질의
normal map이 픽셀 단위 표면 방향을 바꾸도록 한다.

**착수 전 Submesh 게이트**

검증용 실제 모델의 `.obj`/`.mtl`을 확인한다. 둘 이상의 `usemtl` 구간이 실제로
필요하고 현재 한 재질 렌더링 때문에 깨져 보이면 ROADMAP의 Submesh 트리거가
발화한 것이다. 이 경우 11.3 앞에 다음 작업을 끼워 넣는다.

- OBJ `mtllib`/`usemtl` 파싱
- `Mesh`에 인덱스 구간 + material slot인 Submesh 목록 보관
- 한 `MeshRenderer`가 submesh별 draw item을 생성하도록 데이터 경계 갱신
- diffuse/normal texture를 submesh material별로 연결

단일 머티리얼 모델로 충분하면 트리거를 인위적으로 발생시키지 않고 계속 대기한다.

**작업 항목**

- [o] `Vertex`에 `XMFLOAT4 tangent` 추가 (`xyz` 방향, `w` handedness)
- [o] position/uv 차이로 삼각형 tangent와 bitangent 계산
- [o] 공유 정점에 기여도를 누적한 뒤 normal에 대해 Gram-Schmidt 직교화
- [o] bitangent 방향과 `cross(normal, tangent)`를 비교해 `w` 결정
- [o] UV 면적이 0에 가까운 삼각형의 fallback tangent 처리
- [o] **기하 면적이 0인 삼각형을 누적에서 제외** — UV determinant 검사만으로는
      부족하다. 7314689의 `MakeSphereMeshData`는 극점에 **퇴화 삼각형 64개를
      의도적으로 남긴다**(극 두 줄 × slices 32). 래스터라이저는 이들을 버리지만
      tangent 누적 루프는 버리지 않으므로, `cross(e1, e2)`가 0인 삼각형이
      극점 정점의 tangent/bitangent를 오염시키거나 정규화 단계에서 0으로
      나누어 **NaN**을 만든다. 한 정점의 NaN은 그 정점을 쓰는 모든 삼각형으로
      번진다. UV 검사와 **기하 검사 둘 다** 필요하다
- [o] 극점처럼 기여 삼각형이 전부 퇴화한 정점의 tangent를 어떻게 채울지 결정
      → normal로부터 임의의 직교축 생성(`AnyPerpendicular`). 인접 링 복사는
      "인접"을 알려면 연결 정보가 필요한데 `MeshData`에는 없다
- [o] OBJ normal 생성 이후 tangent 생성 순서 보장
- [o] cube, pyramid, floor를 포함한 절차 메시도 같은 후처리 사용
- [o] input layout과 HLSL `VSInput`을 새 stride에 맞게 변경
- [o] `Material`에 normal texture와 normal strength 추가
- [o] 코드 생성 1×1 flat normal texture `(128, 128, 255)` 추가
- [o] scene root signature에 diffuse와 normal SRV를 각각 바인딩
- [o] VS에서 world-space TBN을 만들고 PS에서 sampled normal 변환
- [o] Inspector와 씬 저장/불러오기 지원

**설계 결정**

- tangent는 `float4`다. mirrored UV에서 bitangent 방향이 뒤집히는 정보를 `w`에
  저장하지 않으면 normal map의 일부 섬이 반대로 빛난다.
- normal texture가 없는 재질도 항상 flat normal SRV를 바인딩한다. 셰이더 분기와
  비어 있는 descriptor table을 피하고 기존 재질 외형을 유지한다.
- normal map은 선형 데이터로 취급한다. 이후 diffuse에 sRGB 처리를 도입하더라도
  normal texture를 sRGB로 샘플링해서는 안 된다.
- tangent 생성은 OBJ loader 안에만 넣지 않는다. 모든 `MeshData`가 업로드 전에
  같은 보정 경로를 지나야 절차 메시와 파일 메시의 vertex contract가 같다.

**함정**

- 현재 input layout offset은 `Vertex`의 메모리 배치에 하드코딩되어 있다.
  C++ 구조체와 HLSL semantic을 함께 바꾸지 않으면 오류 없이 잘못 그려진다.
- 비균일 스케일에서는 tangent도 normal과 마찬가지로 변환 후 다시 직교화해야 한다.
- normal map의 Y축 방향은 제작 도구의 OpenGL/DirectX 관례에 따라 반대일 수 있다.
  Inspector 뒤집기 옵션을 먼저 추가하지 말고 검증 에셋의 관례를 명시한다.

**검증**

1. flat normal map 재질은 normal mapping 이전 스크린샷과 거의 동일하다.
2. known normal map을 적용한 floor/cube에서 조명 이동에 따라 요철 방향이 바뀐다.
3. mirrored UV와 비균일 스케일 모델에서 조명 방향이 seam을 기준으로 뒤집히지 않는다.
4. normal texture가 없거나 로드 실패한 기존 씬은 flat normal로 안전하게 보인다.
5. 저장→불러오기 뒤 normal texture와 strength가 복원된다.

**완료 기준**: 실제 기하를 늘리지 않고도 normal map에 따라 조명 표면 방향이
변하고, normal map이 없는 모든 기존 재질은 이전과 동일하게 보인다.

#### 결과

**tangent 생성은 렌더러를 거치지 않고 먼저 검증했다.** `Mesh.cpp`만 링크하는
독립 실행 파일로 `GenerateTangents`를 직접 돌렸다 — 화면에서 디버깅하는 것보다
훨씬 싸고, 실패가 어디서 났는지 애매하지 않다.

| 도형 | 정점 | NaN | 비단위 | 비수직 | `w`≠±1 | 최대 길이오차 | 최대 `n·t` |
|---|---|---|---|---|---|---|---|
| cube | 24 | 0 | 0 | 0 | 0 | 0 | 0 |
| pyramid | 16 | 0 | 0 | 0 | 0 | 0 | 0 |
| floor | 4 | 0 | 0 | 0 | 0 | 0 | 0 |
| sphere | 561 | **0** | 0 | 0 | 0 | 1.19e−07 | 8.94e−08 |
| torus | 1225 | **0** | 0 | 0 | 0 | 1.19e−07 | 1.04e−07 |

바닥의 tangent는 손으로 유도되는 값과 정확히 일치한다: uv의 +U가 +X를 따라
가므로 `(1, 0, 0)`, `w = 1`.

**퇴화 삼각형에 대한 앞선 서술을 정정한다.** 계획 단계에서 구의 극점 퇴화
삼각형이 **NaN**을 만든다고 적었는데, 실제로 계산해보니 그렇지 않다.
극점 quad는 `p0 == p1`이라 첫 변이 0인데:

```
t = (e1*dv2 - e2*dv1) * r,  dv1 = 0  ->  정확히 0    (무해)
b = (e2*du1 - e1*du2) * r            ->  크고 무의미  (오염)
```

tangent 누적은 quad의 나머지 진짜 삼각형이 살려주고, 오염되는 것은
**bitangent → 즉 `w` handedness**다. 잘못된 `w`는 그 텍스처 섬을 반대쪽에서
비춘다. NaN은 **한 정점의 모든 삼각형이 기각될 때** 나오는 다른 실패이고,
그건 fallback이 담당한다. 두 경로를 따로 만들어 확인했다:

| 상황 | 결과 |
|---|---|
| 기하 면적 0 (한 점에 모인 세 정점, uv는 정상) | `(0, 0, −1)` 유한·단위·수직 |
| UV 면적 0 (진짜 삼각형, uv 셋이 동일) | `(0, 0, −1)` 유한·단위·수직 |

두 검사가 서로를 대신할 수 없다는 뜻이기도 하다 — 첫 줄은 UV 검사가 놓치고,
둘째 줄은 기하 검사가 놓친다.

**노멀 매핑 자체는 예측 가능한 에셋으로 쟀다.** `TestNormal.png`(256×256)는
탄젠트 공간 `(∓0.6, 0, 0.8)`을 번갈아 담은 세로 줄무늬다. 바닥의 tangent가
`(1,0,0)`, `B = cross(N,T)·w = (0,0,−1)`이므로 탄젠트 ±X가 월드 ±X로 그대로
가고, 태양의 `toLight = (−0.596, 0.745, −0.298)`에서 결과가 손으로 나온다.

| | 예측 (red 채널, ambient 0.18 + dir 0.85·n·L) | 실측 |
|---|---|---|
| 밝은 줄무늬 | `0.994 / 0.814` = **1.222×** | **1.219** (90분위) |
| 어두운 줄무늬 | `0.383 / 0.814` = **0.471×** | **0.469** (10분위) |

오차 0.3~0.4%.

**측정 함정 하나.** 처음 잰 값은 전부 1.0 미만이라 예측과 어긋났다. 원인은
셰이더가 아니라 비교 방법이었다 — before/after가 **다른 실행**이었고, 궤도를
도는 점광원 `Lamp`가 그 사이 움직였다. 점광원도 노멀에 반응하므로 예측식에
없는 항이 통째로 달라진 것이다. `Lamp`를 지워 바닥 조명을 정적으로 만들고
**한 실행 안에서** 다시 재자 위 표가 나왔다. 시간에 의존하는 씬에서
before/after를 비교하려면 움직이는 것을 먼저 없애야 한다.

**검증 기준별 결과**

| # | 기준 | 결과 |
|---|---|---|
| 1 | flat normal map은 이전과 거의 동일 | 기본 씬 육안 동일. flat 텍셀 `(128,128,255)`의 잔차는 0.2° — *pre-11.3 빌드와의 픽셀 단위 대조는 하지 않았다* |
| 2 | known normal map으로 조명 방향이 바뀐다 | **수치 일치 0.3~0.4%** (위 표) |
| 3 | 비균일 스케일 / mirrored UV | 비균일 스케일: `Pyramid_squashed`(4, 0.8, 4)에서 두 면이 반대 방향으로 밝기 변화, 얼룩 없음. mirrored UV: 실제 에셋으로는 미검증이나, 시접 케이스를 손으로 구성해 리뷰에서 지적된 상쇄 버그를 재현·수정 (아래 "리뷰로 드러난 결함" 참고) |
| 4 | normal texture 없는 기존 씬 | 18 엔티티 / 14 drawn 그대로, 외형 동일 |
| 5 | 저장→불러오기 복원 | `scene 3`으로 `normal "testnormal.png" strength 1` 기록, 재로드 시 인스펙터에 복원. `scene 2` 파일도 정상 로드되어 `flat` 유지 |

**남긴 것**

- `Engine/Assets/TestNormal.png` — 재사용 가능한 검증 에셋
- `Engine/Assets/Scenes/LegacyV2.scene` — 버전 2 하위호환 회귀 픽스처

**설계 메모 두 가지**

- root signature에 **1개짜리 테이블 두 개**를 뒀다. descriptor table은 힙의
  **연속** 구간을 가리키는데, 한 재질의 diffuse와 normal은 각자 로드될 때
  빈 슬롯을 받은 독립 텍스처라 이웃일 리가 없다. 2개짜리 테이블 하나면
  diffuse 옆의 엉뚱한 텍스처가 노멀맵으로 바인딩된다. 매 드로우마다 두
  descriptor를 연속 영역으로 복사하는 방법도 있지만 텍스처 두 장에서는
  루트 파라미터 하나가 훨씬 싸다.
- object CB에 `float3` 패딩 대신 **스칼라 3개**를 넣었다. HLSL은 스칼라를
  현재 16바이트 레지스터에 채워 넣지만 `float3`는 경계를 걸치면 다음
  레지스터로 민다. `normalStrength` 뒤에 12바이트가 남는 이 자리는 "걸치는가"
  판정이 읽는 사람에 따라 갈리는 지점이라 애매함이 없는 쪽을 골랐다. 어긋나도
  에러가 아니라 **엉뚱한 오프셋에서 읽은 재질 색**이 된다.

**리뷰로 드러난 결함 3건, 모두 수정**

커밋 직후 코드 리뷰에서 P2 세 건이 나왔다. 셋 다 재현해서 확정한 뒤 고쳤다.

1. **mirrored UV 시접(seam) 정점에서 tangent가 상쇄된다.** 같은 정점을
   handedness가 반대인(uv 행렬식 부호가 다른) 두 삼각형이 공유하면 — 좌우
   대칭 캐릭터가 팔 한쪽 텍스처를 뒤집어 재사용할 때 흔한 패턴이다 — 단순
   누적은 두 방향을 평균 내는 게 아니라 **뺀다**. 깔끔한 미러라면 정확히
   0으로 상쇄되어 fallback의 **임의 방향**이 대신 채워진다.

   재현: 시접 변을 공유하는 두 삼각형(둘 다 노멀 +Z, uv 행렬식은 −0.5/+0.5로
   반대)을 손으로 구성해 확인 — 수정 전 시접 정점의 tangent는 `(0, 1, 0)`,
   어느 삼각형의 실제 방향과도 무관한 `AnyPerpendicular` fallback이었다.

   고침: `GenerateTangents`에 pass 0을 추가해, 한 정점을 서로 다른
   handedness의 삼각형이 건드리면 **복제**한다 — 컨텐츠 파이프라인이 애초에
   uv 시접에서 정점을 공유시키지 않는 것과 같은 해법이다. `indices` 배열은
   **크기가 그대로**이고 값만 새 정점으로 리다이렉트되므로, 이미 계산된
   submesh의 offset/count 범위는 그대로 유효하다.

   같은 케이스 재검증: 시접 정점(원본)은 왼쪽 삼각형의 실제 방향
   `(1,0,0)/w=1`을 그대로 갖고, 오른쪽용 복제 정점은 `(−1,0,0)/w=−1`을
   갖는다. 정점 6개(4+2 복제), 인덱스 6개(그대로) — 예측과 일치.

2. **음수 스케일(미러)에서 bitangent가 뒤집힌다.** `tangent.w`는 로컬
   공간에서, orientation을 보존하는 변환을 가정하고 한 번 결정된다. 월드
   행렬의 행렬식이 음수면(예: 축 하나가 음수 스케일) 그 가정이 깨지는데
   `w`는 그대로 셰이더로 넘어간다.

   고침: [Basic.hlsl](../Engine/Shaders/Basic.hlsl)의 `VSMain`에서
   `determinant((float3x3)gWorld)`의 부호를 `w`에 곱한다. `det(A) = det(Aᵀ)`라
   `gWorld`에 이미 들어있는 행/열 우선 전치와 무관하게 정확하다.

   지금 인스펙터의 Scale 필드는 하한 `1e-5`라 음수를 만들 수 없어 **당장은
   도달 불가능**하지만, 손으로 편집한 씬 파일이나 향후 축 변환(업 축 트리거,
   ROADMAP)이 음수 스케일로 미러링을 구현할 가능성이 있어 셰이더 쪽을
   먼저 고쳐 두는 편이 쌌다.

3. **UV 퇴화 판정이 절댓값이었다.** `기하 판정은 삼각형 자기 크기에 상대적`
   이라고 문서에 적어 놓고 UV 판정은 `|det| <= 1e-12` 고정값이었다 — 같은
   레이아웃을 촘촘한 아틀라스에 작게 우겨넣으면(흔한 경우) 진짜 퇴화가
   아닌데도 기각될 수 있었다.

   고침: 기하 판정과 같은 형태로 `det² > 1e-12 · (uv 변 길이제곱의 곱)`으로
   바꿨다. `du1=dv1=du2=dv2=0`인 완전 퇴화 케이스는 여전히 `0 > 0`으로
   기각되어 동작이 보존된다.

   `windingtest.cpp`로 전체 스위트(도형 5개 + fallback 2개 + 시접 케이스)를
   재확인 — NaN 0, 결과 불변. 엔진 안에서도 11.3의 줄무늬 측정(밝은 1.219 /
   어두운 0.469)을 **좌표를 다시 읽어** 재확인해 정확히 재현됨을 확인했다.
   (자동화 함정: ImGui가 `imgui.ini`에 헤더 접힘 상태를 실행 간 영속시켜서,
   이전 실행이 Transform 헤더를 접어 뒀는지에 따라 그 아래 버튼들의 Y좌표가
   달라졌다 — 고정 좌표 대신 매 실행 스크린샷에서 다시 읽어야 했다.)

---

### 11.4 directional shadow depth pass

**목표**: directional light 시점에서 opaque scene의 깊이를 별도 shadow map에
그린다. 아직 메인 화면에 그림자를 적용하지 않고 depth-only pass 자체를 검증한다.

**그림자 범위 결정**

- 그림자는 **directional light 하나만** 만든다.
- point light는 기존 조명만 유지하고 shadow를 만들지 않는다.
- opaque mesh는 기본적으로 caster다. skybox와 transparent mesh는 제외한다.
- 모든 world-space mesh AABB를 합친 scene bounds에 여유 margin을 더해
  directional light의 orthographic projection을 만든다.

**작업 항목**

- [o] 고정 기본 해상도 2048×2048 shadow `DepthTarget` 생성
- [o] `R32_TYPELESS` resource, `D32_FLOAT` DSV, `R32_FLOAT` SRV 조합
      *(11.1의 `DepthTarget`에 SRV allocator를 넘기는 것만으로 끝났다 — 그때
      옵션으로 만들어 둔 것의 회수)*
- [o] 프레임별 `ShadowPassConstants` 또는 pass CB의 별도 256바이트 슬롯
      → **pass CB에 `shadowViewProj` 필드 추가**를 골랐다. 11.5의 조명 패스도
      같은 행렬을 읽어야 하는데, 별도 버퍼면 두 곳이 어긋날 수 있다
- [o] directional direction과 scene bounds에서 light view/projection 계산
- [o] position만 사용하는 shadow VS와 pixel shader 없는 depth-only PSO
- [o] `NumRenderTargets = 0`, shadow DSV만 `OMSetRenderTargets`에 바인딩
- [o] shadow viewport/scissor와 depth clear
- [o] opaque caster를 기존 object CB로 다시 draw
- [o] `DEPTH_WRITE → PIXEL_SHADER_RESOURCE` 상태 전이
- [o] ImGui 임시 디버그 이미지 또는 PIX/RenderDoc로 shadow map 확인
      → ImGui 패널. 이미지만으로는 볼륨 크기를 알 수 없어 centre/radius도 함께

**설계 결정**

- light view/projection은 `LightingData`와 `DrawItem` 경계 안의 정보로 계산한다.
  Renderer가 Entity나 `Light` component를 직접 조회하지 않는다.
- shadow pass는 material texture와 pixel shader가 필요 없다. object world matrix와
  light view-projection만 받는 작은 계약으로 유지한다.
- 이번 단계에서는 모든 opaque object가 shadow caster다. per-object cast/receive
  플래그는 실제로 제외할 물체가 생길 때 추가한다.
- scene bounds는 **`DrawItem`에 필드를 추가하지 않고** 구한다. Renderer는 이미
  `ResourceManager`를 갖고 있으므로 `GetMesh(item.mesh)`의 로컬 AABB를
  `item.world`로 변환하면 된다 — Entity는 여전히 모른다. `DrawItem`에 bounds
  center가 실제로 필요해지는 것은 정렬 키가 생기는 11.6이고, 그때 넣는다.

**함정**

- light direction이 world up과 거의 평행하면 `LookToLH`의 up vector가 퇴화한다.
  방향에 따라 안전한 보조 up axis를 선택한다.
- scene bounds가 비었거나 한 점으로 줄면 orthographic near/far와 폭이 0이 된다.
  최소 크기를 보장하고 빈 씬에서는 shadow pass를 건너뛴다.
- shadow SRV를 읽던 다음 프레임에 다시 DSV로 쓸 때 역방향 배리어가 필요하다.

**검증**

1. shadow map에 각 caster의 깊이 실루엣이 나타난다.
2. directional light 회전 시 깊이 실루엣과 light projection이 함께 변한다.
3. point light 이동은 shadow map을 바꾸지 않는다.
4. skybox와 transparent item은 shadow map에 기록되지 않는다.
5. 리소스 상태와 DSV/SRV 동시 사용 관련 Debug Layer 메시가 0건이다.

**완료 기준**: 메인 Scene Viewport와 독립적인 directional light depth texture가
매 프레임 생성되고 shader resource 상태로 전달된다.

#### 결과

**깊이 값을 눈으로 보지 않고 손으로 계산해서 맞췄다.** 그림자 맵은 화면에
아무것도 내놓지 않는 패스라 "돌긴 하나"를 확인하기가 어렵다. 대신 디버그
패널에 엔진이 실제로 쓴 볼륨을 띄우고, 그 숫자로 특정 픽셀의 깊이를
예측한 뒤 스크린샷에서 그 픽셀을 읽었다.

엔진이 보고한 값: `centre (0, 3, 0)`, `radius 56.686`.

그로부터 손으로 따라간 계산:

```
dir     = (0.5963, -0.7454, 0.2981)        태양의 pitch/yaw에서
backoff = radius + 1        = 57.686
eye     = centre - dir*backoff = (-34.40, 46.00, -17.20)
near/far= 0.1 / (backoff + radius + 0.1) = 0.1 / 114.472
```

맵 중심을 지나는 광선은 `(0,3,0)`을 통과해 바닥(y=0)의 `(2.40, 0, 1.20)`에
닿는다. 그 지점의 view depth는 `61.71`이므로

```
z = (61.71 - 0.1) / (114.472 - 0.1) = 0.5387   ->  8비트로 137
```

| | 예측 | 중심 픽셀 실측 | 7×7 평균 |
|---|---|---|---|
| 기본 태양 | **137** | **137** | 137.0 |

**한 줄만 다른 씬 3개로 나머지를 갈랐다.** 크레이트가 계속 회전하면
프레임마다 맵이 달라져 비교 자체가 성립하지 않는다. `spin` 컴포넌트를 뺀
정적 씬을 만들고, 거기서 딱 한 줄씩 바꾼 변형을 준비했다.

| 씬 | A와의 차이 |
|---|---|
| `ShadowA` | 기준 |
| `ShadowB` | 태양 회전만 — 정확히 수직 아래 `(-π/2, 0, 0)` |
| `ShadowC` | **점광원 위치만** |

한 실행 안에서 셋을 차례로 불러와 그림자 맵 이미지를 픽셀 단위로 비교:

| 비교 | 기대 | 실측 |
|---|---|---|
| A vs B (태양 회전) | 달라야 함 | **52.2% 픽셀 변화, 최대 델타 201** |
| A vs C (점광원 이동) | 같아야 함 | **0 픽셀 변화 (0.0%)** |

B는 덤으로 **up 벡터 퇴화 경로**를 밟는다 — 방향이 world up과 정확히
평행이라 `LookToLH`가 무너지는 조건이다. 보조 축 `(0,0,1)`로 넘어가고,
결과는 쓰레기가 아니라 올바른 행렬이었다. 중심 픽셀 예측 `135.1` → 실측
**135**. `radius`는 두 씬에서 `56.686`으로 동일했다 — 경계**구**를 쓴 이유가
바로 이것으로, 빛이 돌아도 볼륨 크기가 숨쉬지 않는다.

**검증 기준별 결과**

| # | 기준 | 결과 |
|---|---|---|
| 1 | caster의 깊이 실루엣이 나타난다 | 픽셀의 60%가 1.0(클리어), 40%가 실제 깊이. 중심 픽셀이 예측과 일치 |
| 2 | 광원 회전 시 맵이 함께 변한다 | **52.2% 변화** |
| 3 | 점광원 이동은 맵을 바꾸지 않는다 | **0 픽셀 변화** |
| 4 | skybox·transparent는 기록되지 않는다 | skybox는 `items`에 없고 자기 패스에서 그려진다. transparent는 11.6 전까지 존재하지 않으므로 "opaque caster"와 `items`가 같은 집합이다 — 코드 수준에서 참이고, 맵의 클리어 영역이 배경이 아무것도 채우지 않았음을 보여준다 |
| 5 | Debug Layer 메시지 0건 | **`debug layer: 0 messages`** |

**5번을 위해 카운터를 새로 만들었다.** 처음에는 DBWIN 캡처로 확인하려 했는데
0바이트 파일이 나왔고, 그건 "메시지 없음"과 "이 프로세스를 못 봤음"을
구별해주지 않는다. (11.2.5에서 이 캡처가 Discord의 메시지를 잡아 `Error: 31`
여섯 건을 오보한 전력도 있다.) 그래서 엔진이 `ID3D12InfoQueue::
GetNumStoredMessages()`를 직접 읽어 stats 패널에 띄우게 했다.

`HasDebugLayer()`를 따로 둔 것이 핵심이다. 카운트 0은 그 자체로 애매하다 —
"아무 문제 없음"과 "아무도 안 보고 있었음"이 똑같이 0이고, 후자를 전자로
보고하는 것이 근거 없이 무결을 주장하는 방식이다. 레이어가 없으면
`debug layer: off`로, 있으면 `0 messages`로 표시한다.

이건 이 단계용 임시 도구가 아니다. D3D12 검증 오류는 예외를 던지지도,
그림을 바꾸지도 않고 그저 "다른 드라이버에서는 다르게 동작할 수 있다"만
의미한다. 화면에 숫자로 있으면 그게 생긴 프레임에 알아차린다.

**설계 메모**

- shadow 패스는 `BindScenePass`를 **쓰지 않는다.** 그 함수는 씬 뷰포트와
  머티리얼 테이블을 바인딩하는데 둘 다 여기서는 틀리다 — 그림자 맵은 자기
  정사각 해상도를 갖고, 샘플할 픽셀 셰이더가 없으니 바인딩할 텍스처도 없다.
  공유 헬퍼를 억지로 늘리는 대신 필요한 네 줄을 직접 썼다.
- `m_shadowMapIsShaderResource` 플래그가 필요한 이유: `DepthTarget`은
  리소스를 `DEPTH_WRITE`로 만드는데 첫 프레임 이후로는 매번 읽기 상태로
  끝난다. 리소스가 있지도 않은 상태에서 전이하는 배리어는 no-op이 아니라
  Debug Layer 오류다.
- scene bounds는 `DrawItem`에 필드를 **추가하지 않고** 구했다 — Renderer가
  이미 `ResourceManager`를 들고 있어 `GetMesh(item.mesh).bounds`가 바로
  손에 있다. 로컬 상자를 행렬로 밀면 상자가 아니므로 **여덟 꼭짓점** 전부를
  통과시킨다. 회전한 물체에는 헐겁지만 절대 작지는 않고, 그림자 볼륨에서
  중요한 방향은 그쪽이다.

**남긴 것**

- `Engine/Assets/Scenes/ShadowA.scene` — spin이 없는 정적 씬. 프레임 간
  비교가 필요한 회귀 측정의 기준선이다. B/C는 이 파일에서 한 줄만 바꾸면
  재현되므로 남기지 않았다

**리뷰로 드러난 결함 3건, 모두 수정**

커밋 직후 코드 리뷰에서 P2 세 건이 나왔다. 셋 다 재현해서 확정한 뒤 고쳤다.

1. **shadow map이 회색이 아니라 빨간색으로 표시됐다.** `R32_FLOAT`는 채널이
   하나뿐인데 `D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING`은 항등 매핑이라
   G·B는 0, A는 1로 채워진다. 디버그 패널의 `ImGui::Image`가 RGBA를 그대로
   샘플링하면 `(depth, 0, 0, 1)` — 순수 빨강이다. 실제 스크린샷에서도 확인:
   중심 픽셀이 `(255, 0, 0)`이었다. `.r`만 읽는 11.5의 조명 셰이더는 이 문제를
   전혀 겪지 않았을 것이므로 화면으로 처음 검증할 때만 드러났다.

   고침: `D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING`으로 R을 R·G·B 셋 다에
   복제하고 A는 강제로 1. `.r`이 여전히 첫 컴포넌트이므로 깊이 샘플링 용도는
   영향이 없다. 재검증: 같은 중심 픽셀이 `(137, 137, 137)` — 11.4에서 예측한
   값 137과 정확히 일치. 색만 고쳐지고 깊이 데이터는 전혀 안 건드렸다는 뜻이다.

2. **caster가 없으면 `sceneCenter`가 초기화되지 않은 채 UI로 나간다.**
   `ComputeSceneBounds`는 `outCenter`를 **참일 때만** 쓰는데, 호출부의
   `XMFLOAT3 sceneCenter;`는 기본 초기화가 없어 스택 쓰레기 값을 그대로
   갖고 있었다. 그리고 `m_shadowSceneCenter = sceneCenter;`는
   `m_shadowCastersExist` 여부와 무관하게 매 프레임 실행된다 — 빈 씬에서
   임의 값, 운이 나쁘면 NaN이 인스펙터에 찍힐 수 있었다.

   고침: `XMFLOAT3 sceneCenter = { 0.0f, 0.0f, 0.0f };`로 명시적 초기화.
   재검증: `File > New`로 빈 씬을 만들었더니 패널에 정확히
   `centre 0.00 0.00 0.00`, `radius 0.000` — 결정론적인 0이다.

3. **caster가 없을 때 clear·상태 전이 없이 조기 반환했다.** 두 가지 증상이
   겹친다. 첫 프레임부터 빈 씬이면 `DepthTarget`이 만든 `DEPTH_WRITE`
   상태 그대로인데 디버그 패널의 `ImGui::Image`가 SRV로 그 프레임에 바로
   샘플링한다 — 리소스가 있지도 않은 상태로 읽는 것. 또는 caster가 있던
   씬에서 `File > New`로 넘어가면 clear를 안 하니 그림자 맵에 **이전 씬의
   깊이가 그대로 남는다.**

   고침: caster 유무 판정을 clear·상태 전이 **뒤로** 옮겼다. draw 루프만
   caster가 있을 때로 남기고, clear(1.0, far)와
   `DEPTH_WRITE → PIXEL_SHADER_RESOURCE` 전이는 항상 실행한다. "caster
   없음"의 정직한 그림은 전부 far plane인 흰 화면이다 — 실제로 빈 절두체를
   도는 패스가 냈을 결과와 같다.

   재검증: caster가 있던 씬(`ShadowA`)을 불러온 뒤 `File > New`로 다시 빈
   씬으로 넘어갔다. 그림자 맵은 **여전히 순백색** — 이전 프레임의 흔적이
   없다. `debug layer: 0 messages`도 두 케이스(빈 씬에서 시작 / caster 있는
   씬에서 빈 씬으로 전환) 모두 유지됐다 — 4번 항목에서 잘못된 상태 읽기가
   있었다면 여기서 잡혔을 것이다.

---

### 11.5 그림자 적용 + 품질 보정

**목표**: 메인 lighting shader가 shadow map을 샘플링해 directional light의
직접광만 가리고, bias와 PCF로 기본적인 품질 문제를 제어한다.

**작업 항목**

- [o] scene `PassConstants`에 light view-projection과 shadow texel size 추가
- [o] shadow map을 scene root signature의 별도 SRV로 바인딩
- [o] border가 lit(깊이 1)인 comparison sampler 추가
- [o] world position을 light clip/NDC/texture UV로 변환
- [o] shadow 범위 밖 좌표는 lit로 처리
- [o] directional contribution에만 visibility 곱하기
- [o] shadow PSO rasterizer에 constant/slope-scaled depth bias 적용
- [o] 3×3 percentage-closer filtering(PCF)
- [o] shadow enable, bias, strength를 renderer 또는 environment 설정으로 노출
- [o] 필요할 경우 light projection extent를 shadow texel 단위로 안정화
      → 런타임 관찰 결과 카메라 이동이 light matrix를 바꾸지 않아 적용하지 않음

**설계 결정**

- ambient와 point light에는 directional shadow visibility를 곱하지 않는다.
  그림자는 해당 directional light의 직접광만 차단한다.
- 첫 구현부터 3×3 PCF까지 포함한다. 단일 비교 샘플은 동작 검증에는 유용하지만
  최종 완료 화면의 계단이 너무 커 별도 완성 상태로 두지 않는다.
- bias 기본값은 장면 크기와 shadow 해상도에 맞춘 작은 값으로 시작하고 Inspector에서
  조정한다. 엔진 내부 magic number로 숨기지 않는다.

**함정**

- bias가 작으면 shadow acne, 크면 물체가 그림자에서 떨어지는 peter-panning이 생긴다.
- UV의 Y방향과 D3D NDC depth `[0, 1]` 규칙을 지켜야 한다.
- shadow map 경계에서 WRAP sampler를 쓰면 반대편 깊이가 섞인다. border/clamp 계열과
  comparison sampling을 별도로 사용한다.
- normal bias나 receiver-plane bias는 필요성이 측정되기 전에는 추가하지 않는다.

**검증**

1. cube가 floor에 그림자를 만들고 이동·회전 시 그림자가 따라온다.
2. directional light를 끄면 해당 그림자도 사라지고 point light는 그대로 비춘다.
3. 앞면과 비스듬한 면에서 acne가 과도하지 않고 물체가 그림자에서 뜨지 않는다.
4. shadow map 경계 밖에 검은 테두리나 반복된 그림자가 생기지 않는다.
5. 카메라 이동 중 그림자 떨림을 관찰하고 필요할 때만 texel snapping을 적용한다.

**완료 기준**: 에디터에서 배치한 opaque 물체가 directional light 기준 그림자를
만들고 받으며, 기본 씬에서 acne·경계 반복·심한 계단 현상이 눈에 띄지 않는다.

**구현 결과 — 기존 11.4 계약을 읽기 경로까지 닫았다**

11.4가 만든 `shadowViewProj`와 `DepthTarget` SRV를 그대로 재사용했다. scene root
signature에는 전역 shadow table `t2`를 하나 추가하고, `s1`은 comparison sampler로
분리했다. 주소 모드는 `BORDER`, border depth는 1(lit), 비교는 `LESS_EQUAL`이다.
따라서 PCF 탭이 맵 밖으로 나가도 반대편 깊이를 wrap하지 않는다. `PassConstants`에는
실제 shadow target의 width/height로 계산한 texel size와 receiver bias, strength를
한 16바이트 레지스터로 덧붙였다. C++ 크기 304바이트는 `static_assert`로 고정했고
세 scene shader의 cbuffer 선언도 같은 순서로 맞췄다.

vertex shader가 world position을 light clip으로 한 번 변환해 pixel shader에 넘긴다.
pixel shader는 perspective divide 뒤 X를 `[0,1]`, Y를 뒤집어 `[0,1]` UV로 바꾸고,
D3D depth `[0,1]`까지 함께 범위를 검사한다. 유효한 픽셀은 texel 간격의 3×3 위치에서
아홉 번 `SampleCmpLevelZero`를 실행한다. comparison tap 자체는 linear filtering이라
point comparison에서 보였던 큰 바닥 그림자의 양자화된 계단도 완화된다. 최종
visibility는 directional Blinn-Phong 항에만 곱한다. ambient와 point 항은 그 계산의
앞뒤에 독립적으로 더해지므로 directional shadow에 가려지지 않는다.

bias는 두 층이다. shadow PSO rasterizer가 caster에 constant `1000`과 slope-scaled
`1.0`을 적용하고, Environment의 receiver bias 기본값 `0.001`은 Inspector에서 직접
조정한다. 같은 Environment에 Enabled와 Strength(`[0,1]`)도 두었고, scene format v4의
`shadow <0|1> <bias> <strength>` tail로 저장한다. v1~v3에는 tail이 없으므로 구조체의
`true / 0.001 / 1.0` 기본값을 그대로 쓴다.

**런타임 검증**

- 기본 씬의 cube·pyramid·sphere·torus가 floor에 접한 그림자를 만들고, 회전하는
  물체의 그림자가 따라 움직였다. 앞면 acne나 눈에 띄는 peter-panning은 없었다.
- Environment의 Enabled를 끄자 바닥 그림자만 즉시 사라졌고 장면 조명은 유지됐다.
  다시 말해 shadow visibility가 조명 전체를 검게 만드는 경로가 아니다.
- point comparison의 눈에 띄는 texel 계단을 확인한 뒤 linear comparison 3×3로
  바꿔 재검증했다. 경계 wrap이나 검은 테두리는 보이지 않았다.
- Debug 실행에서 shader cache `5/5`, `debug layer: 0 messages`. SDK `fxc`로
  Basic VS/PS, Skybox VS/PS, ShadowDepth VS를 각각 컴파일했고 Debug/Release x64
  빌드는 경고·오류 없이 완료됐다.
- 카메라는 shadow volume 계산 입력이 아니어서 이동 중 light projection이 변하지
  않았다. 카메라 연동 떨림이 관찰되지 않았으므로 texel snapping은 추가하지 않았다.

---

### 11.6 블렌딩 + 투명 오브젝트 정렬

**목표**: 불투명과 반투명 draw를 분리하고, 일반적인 알파 블렌딩에 필요한
깊이 쓰기 규칙과 후방→전방 정렬을 적용한다.

**작업 항목**

- [ ] `Material::BlendMode { Opaque, AlphaBlend }`
- [ ] `DrawItem`에 render layer와 정렬 키 또는 계산에 필요한 bounds center 추가
      (11.0에서 미리 넣지 않고 **쓰는 데가 생기는 여기서** 추가한다.
      11.4의 scene bounds는 ResourceManager 조회로 해결된다)
- [ ] `BuildRenderData`에서 opaque/transparent 분류
- [ ] 카메라 공간 깊이 기준 transparent sort key 계산
- [ ] opaque는 기존 PSO로 먼저 draw
- [ ] skybox 뒤에 transparent pass 실행
- [ ] transparent PSO: `SRC_ALPHA`, `INV_SRC_ALPHA`, `ADD`
- [ ] transparent PSO: depth test ON, depth write OFF
- [ ] HLSL 출력 alpha에 texture alpha × material alpha 반영
- [ ] Inspector blend mode와 alpha 편집
- [ ] 씬 저장/불러오기 지원, 구버전 기본값은 Opaque

**설계 결정**

- `diffuseAlbedo.a < 1`만으로 blend mode를 추측하지 않는다. 렌더 상태는 재질에
  명시적으로 저장해야 opaque 정렬과 depth write를 안정적으로 결정할 수 있다.
- 정렬 기준은 AABB world center의 camera-space depth다. 단순 유클리드 거리보다
  화면 깊이 순서와 직접 대응한다.
- 정렬은 매 프레임 renderer에 넘기기 전 또는 renderer 내부의 임시 index 목록에서
  수행한다. World의 엔티티 저장 순서를 바꾸지 않는다.
- alpha test와 한 메시 내부 삼각형 정렬은 범위 밖이다. 교차하는 투명 메시에서
  완벽하지 않다는 한계를 문서에 남긴다.

**함정**

- transparent depth write를 켜면 먼저 그린 투명 표면이 뒤의 투명 표면을 완전히 막는다.
- 정렬 후 object CB index와 draw item index가 어긋나면 다른 물체의 transform/material을
  읽는다. 정렬된 draw 순서와 constants 주소의 대응을 함께 유지한다.
- skybox를 transparent 뒤에 그리면 투명 픽셀 합성의 배경이 아직 clear color가 된다.

**검증**

1. 겹친 반투명 quad/cube를 앞뒤 양쪽에서 보아 뒤쪽부터 합성된다.
2. opaque 물체는 transparent 물체 뒤에서 정상적으로 가리고 depth를 기록한다.
3. transparent 물체 뒤의 skybox가 알파에 맞춰 비친다.
4. 불투명 재질 100개 정도의 외형과 순서는 이전 단계와 동일하다.
5. 저장→불러오기 뒤 blend mode와 alpha가 복원된다.

**완료 기준**: 불투명 draw 뒤에 정렬된 반투명 draw가 실행되고, 카메라를
이동해 순서가 바뀌어도 일반적인 겹침 장면이 올바르게 합성된다.

---

### 11.7 4x MSAA + resolve

**목표**: Scene Viewport를 4배 멀티샘플링하되 ImGui에는 항상 단일 샘플
resolve texture를 전달한다. Phase 11에서 확정된 모든 색상 패스가 같은
sample count로 그려지게 한다.

**작업 항목**

- [ ] `CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS)`로 4x 지원 확인
- [ ] scene target에 sample count와 quality 저장
- [ ] 4x color render texture와 같은 sample count의 scene depth 생성
- [ ] 단일 샘플 resolve texture와 SRV 생성
- [ ] opaque/skybox/transparent PSO의 1x/4x variant 생성 또는 캐시
- [ ] scene 색상 패스 종료 후 `ResolveSubresource`
- [ ] resolve texture를 `PIXEL_SHADER_RESOURCE`로 전환해 ImGui에 전달
- [ ] 1x에서는 불필요한 resolve 없이 기존 단일 샘플 경로 사용
- [ ] Inspector 또는 설정 UI에서 Off/4x 선택
- [ ] 지원되지 않는 장치에서는 1x로 안전하게 폴백하고 상태 표시
- [ ] viewport 리사이즈 시 MSAA color/depth/resolve를 함께 재생성
- [ ] shadow map은 sample count 1 유지

**설계 결정**

- ImGui의 `SceneTextureId`는 sample count와 관계없이 항상 단일 샘플 SRV다.
  UI 계층이 MSAA 여부를 알 필요가 없게 한다.
- MSAA 전환은 드문 설정 변경이므로 우선 `WaitForGpu()` 뒤 안전하게 교체한다.
  전환 자체를 비동기화하지 않는다.
- PSO sample count는 attachment와 일치해야 하므로 scene 색상 PSO별 1x/4x variant를
  미리 만들거나 첫 사용 시 캐시한다. shadow depth PSO에는 variant가 없다.
- ROADMAP의 “MSAA 또는 렌더 타겟 해상도 분리” 중 MSAA를 선택한다. resolution scale은
  이후 성능 요구가 생겼을 때 독립 설정으로 추가한다.

**리소스 상태 흐름**

4x 경로:

```text
MSAA Color:   RESOLVE_SOURCE ↔ RENDER_TARGET
Resolve Tex:  PIXEL_SHADER_RESOURCE ↔ RESOLVE_DEST
Scene Depth:  DEPTH_WRITE
```

1x 경로:

```text
Scene Color:  PIXEL_SHADER_RESOURCE ↔ RENDER_TARGET
Scene Depth:  DEPTH_WRITE
```

**함정**

- multisample texture에는 일반 `Texture2D` SRV를 만들어 ImGui에 넘길 수 없다.
  resolve가 누락되면 잘못된 view dimension 또는 미정의 결과가 된다.
- color와 depth sample count가 다르면 `OMSetRenderTargets` 이후 draw가 유효하지 않다.
- PSO의 `SampleDesc`도 실제 color/depth와 같아야 한다.
- viewport 리사이즈가 이제 color, depth, resolve 세 리소스를 폐기하므로 기존 full flush
  비용이 커질 수 있다. 아래 트리거 재판정을 반드시 수행한다.

**검증**

1. 동일한 카메라에서 1x/4x 이미지를 캡처해 기하 경계의 계단이 감소한다.
2. 1x↔4x를 반복 전환해 descriptor나 lifetime 오류가 없다.
3. viewport 접기/펼치기와 연속 리사이즈 뒤에도 ImGui 이미지가 정상이다.
4. 미지원 폴백 경로를 강제로 실행해 1x로 정상 렌더링된다.
5. 모든 scene PSO와 attachment sample count 관련 Debug Layer 메시지가 0건이다.

**RT 폐기 큐 트리거 재판정**

MSAA ON/OFF 각각에서 viewport 스플리터를 연속 드래그하며 frame time을 기록한다.
full flush spike가 반복적으로 편집을 방해하면 ROADMAP의 “렌더 타겟 리사이즈의
프레임별 폐기 큐” 트리거가 발화한 것으로 간주하고 v1.2 전에 편입한다.
체감과 측정 모두 문제가 없으면 계속 대기시킨다.

**완료 기준**: 지원 장치에서 4x MSAA Scene Viewport를 볼 수 있고, ImGui는 resolve된
단일 샘플 texture만 읽으며, 1x 폴백과 리사이즈도 안전하다.

---

## 5. 트리거 재판정 체크포인트

트리거는 계획서 끝에서 한 번만 보는 것이 아니라 관련 위험이 커지는 단계 직전에
검사한다.

| 시점 | 확인할 항목 | 발화 조건 | 발화 시 처리 |
|------|-------------|-----------|---------------|
| 11.2 에셋 확정 후 | 비동기 로드 | cube 6면 로드가 UI 작업을 반복적으로 막음 | 에셋 decode/upload 상태 머신을 11.2 뒤에 편입 |
| 11.3 착수 전 | Submesh | 실제 검증 OBJ가 여러 `usemtl`을 쓰며 현재 그림이 깨짐 | Submesh 데이터 모델을 11.3 앞에 편입 |
| Submesh 구현 후 | 씬 계층 | 모델을 여러 엔티티로 쪼개 함께 움직여야 함 | 부모-자식 Transform을 함께 설계 |
| 각 데모 씬 완성 후 | Object CB 동적화 | 고유 draw item이 256개 초과 | 프레임별 동적 capacity와 지연 폐기 구현 |
| 에셋 수 증가 후 | 메시 3D 썸네일 | 파일명만으로 선택이 실제로 어려움 | 별도 preview RT/pass 설계 |
| 11.7 측정 후 | RT 폐기 큐 | 리사이즈 full flush가 체감 가능한 반복 정지 | frame별 deferred release queue 구현 |

비동기 에셋 로드는 CPU decode만 다른 스레드로 옮기는 것으로 끝나지 않는다.
ResourceManager cache의 동기화, GPU upload command 기록 시점, 완료 전 placeholder,
에러 전달을 함께 설계해야 한다. 트리거가 발화하면 작은 부가 작업이 아니라 별도
세부 단계로 다룬다.

---

## 6. 마일스톤 완료 체크리스트 (v1.2)

- [o] 뷰포트 클릭으로 가장 가까운 기존 메시 엔티티를 선택할 수 있음
- [o] 새 메시가 로컬 AABB 기준으로 바닥 위에 정확히 배치됨
- [o] 6장 PNG가 cube texture로 업로드되고 스카이박스로 표시됨
- [o] 카메라 이동에는 고정되고 회전에는 반응하는 스카이박스
- [o] OBJ와 절차 메시 모두 tangent basis를 가지며 normal map이 동작
- [o] normal map 없는 기존 재질은 이전 외형 유지
- [o] directional light depth-only shadow pass 동작
- [o] opaque 물체가 그림자를 만들고 받으며 point light는 영향받지 않음
- [o] bias + 3×3 PCF로 기본 씬의 acne와 계단 현상이 허용 범위
- [ ] opaque/transparent PSO 분리, transparent 후방→전방 정렬
- [ ] 4x MSAA 결과가 resolve texture를 통해 ImGui에 표시됨
- [ ] 1x 폴백, viewport 리사이즈, 접기/펼치기 정상
- [o] v1 씬 로드 및 새 필드 기본값 적용, 최신 씬 저장/불러오기 왕복
- [ ] Renderer는 여전히 ECS를 모르고 `DrawItem` 등 평탄화 데이터만 받음
- [ ] 새 코드가 `GetProjectRoot()`를 직접 호출하지 않고 ResourceManager 경계를 사용
- [ ] 모든 트리거의 대기/발화 상태와 근거가 ROADMAP에 갱신됨
- [ ] Debug Layer 경고·에러 0, 종료 코드 0, Debug/Release 빌드

Phase 11의 ROADMAP 완료 기준인 **“그림자가 지는 씬을 스카이박스 아래에서
볼 수 있다”**를 충족하면서, 목록에 포함된 normal mapping, blending, MSAA까지
모두 독립적으로 켜고 검증할 수 있어야 한다.

---

## 7. Phase 12와의 연결

- AABB는 Phase 12의 작은 데모 게임에서 클릭 상호작용이나 간단한 공간 판정의
  기반으로 재사용할 수 있다. 다만 물리/충돌 시스템으로 확대하지 않는다.
- Phase 11에서 추가한 `Graphics/` 리소스와 패스 코드는 이후 Engine 정적 라이브러리로
  이동할 대상이다. 에디터 UI 상태가 Renderer나 ResourceManager에 들어가지 않게 한다.
- skybox, normal texture, blend mode가 추가된 씬 포맷은 Editor와 Player가 함께
  읽어야 한다. 논리 에셋 이름을 저장하고 런타임 핸들이나 절대 경로를 저장하지 않는다.
- Phase 12에서 `GetProjectRoot()` 정책을 “exe 옆 배포 리소스 우선, 개발 리포 탐색
  fallback”으로 바꿀 수 있도록 Phase 11은 경로 탐색 호출 지점을 늘리지 않는다.
- MSAA resolve texture는 에디터에서 ImGui로 전달되지만, Phase 12 Player에서는
  resolve 결과를 백버퍼로 보내는 프레젠테이션 경로로 재사용할 수 있다.
- shadow/skybox/transparent 패스 순서는 Editor와 Player가 공유하고, 마지막
  presentation 단계만 ImGui viewport와 swap-chain direct 경로로 갈라져야 한다.

Phase 11은 렌더링 품질을 확장하지만 에디터/게임 실행 경계를 만들지는 않는다.
`DemoGame::OnUpdate`의 시스템 구분, 에디터/게임 카메라, Play/Stop 스냅샷,
Engine 정적 라이브러리와 Editor/Player 프로젝트 분리는 그대로 Phase 12의 책임이다.
