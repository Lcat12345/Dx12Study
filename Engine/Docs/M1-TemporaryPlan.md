# M1 임시 실행 플랜

> 상태: 임시 문서  
> 기준: `ROADMAP.md`의 M1 — 몰려오는 적 (`v1.4`)  
> 폐기 조건: M1 완료 기준을 충족하고 결과를 `ROADMAP.md`에 반영한 뒤 삭제

## 목차

- [판단](#판단)
- [현재 상태](#현재-상태)
- [범위와 원칙](#범위와-원칙)
- [작업 분할](#작업-분할)
- [통합 검증](#통합-검증)
- [완료 조건](#완료-조건)

## 판단

M1은 한 번의 변경으로 안전하게 완료하기 어렵다. 게임플레이 수직 슬라이스뿐 아니라
GPU 자원 수명, 셰이더와 PSO, 가시성 판정, descriptor heap 수명, 로깅과 성능 측정까지
서로 다른 실패 영역을 동시에 건드린다.

따라서 아래 8개 작업을 순서대로 수행한다. 각 작업은 독립적으로 빌드ㆍ테스트할 수
있어야 하며, 앞 단계의 측정 결과가 다음 단계의 입력이 된다. 특히 게임 루프를 먼저
만들고 실제 상한을 재현한다는 `ROADMAP.md`의 순서를 유지한다.

## 현재 상태

| 영역 | 확인된 상태 | M1에서 필요한 변화 |
|---|---|---|
| 플레이 시스템 | 카메라, `Spin`, 조명 이동, 상호작용만 실행 | 이동, 스폰, 추적, 전투, HP, XP, 생존 상태 |
| 절차 메시 | `#sphere` 등은 있으나 캡슐 recipe가 없음 | 재사용 가능한 `#capsule` 추가 |
| Object CB | 프레임당 256 슬롯 고정, 초과 시 예외 | 요청 기반 증가와 GPU-safe 재생성 |
| 드로우 | main/shadow 모두 item마다 1 draw | 동일 메시ㆍ머티리얼의 opaque batch 인스턴싱 |
| 가시성 | 투명 정렬만 있고 모든 item을 제출 | main/shadow 절두체를 분리한 컬링 |
| SRV heap | 64 슬롯 고정, handle이 CPU/GPU 주소를 저장 | heap 교체 뒤에도 유효한 논리 handle과 안전한 증가 |
| 관측성 | 1초 평균 FPS와 예외 MessageBox뿐 | 공용 파일 로그와 재현 가능한 프레임 샘플 |
| ECS 삭제 | storage가 swap-and-pop 방식 | 반복 중 삭제하지 않는 2단계 사망/획득 처리 |

이 결합도 때문에 Object CB, 인스턴싱, 컬링, SRV 증가를 한 변경에 넣으면 성능 회귀나
Debug Layer 오류의 원인을 분리할 수 없다.

## 범위와 원칙

- M1은 절차 메시 기반으로만 만든다. FBX, 스키닝, 물리, 오디오는 각각 M2 이후 범위다.
- HUD는 M4 범위이므로 만들지 않는다. M1 상태는 창 제목과 파일 로그로 확인한다.
- Scene v6는 M2에 예약되어 있으므로 M1에서 scene schema를 올리지 않는다.
- `Arena.scene`은 바닥ㆍ조명ㆍ카메라 같은 정적 무대만 담는다. 플레이어와 런타임 전투
  상태는 arena 초기화 코드가 생성한다.
- 게임 규칙은 `Game.lib`에 두고, `Player.exe`는 입력 전달과 시작 scene 선택만 담당한다.
- 적 이동과 접촉은 XZ 평면의 거리 판정으로 구현한다. M3의 물리 body를 미리 만들지
  않는다.
- 적 생성은 고정 seed를 지원해야 한다. 기능 테스트와 성능 비교에서 같은 배치를
  재현하기 위함이다.
- `World::ForEach` 중에는 entity를 삭제하지 않는다. 삭제/획득 대상을 모은 뒤 순회를
  마치고 적용한다.
- 인스턴싱은 우선 opaque에만 적용한다. transparent는 기존 back-to-front 순서를
  유지한다.
- 기능 기준과 성능 기준을 섞지 않는다. Debug에서 기능ㆍDebug Layer를 확인하고,
  Release에서 프레임타임을 측정한다.

## 작업 분할

### 1. 파일 로그와 측정 기반

목표는 이후 실패와 성능 변화를 Player 밖에서도 재현 가능하게 남기는 것이다.

- [x] `Engine.lib`에 thread-safe하지 않은 단순 단일 프로세스 logger를 추가한다.
- [x] 기본 로그 경로는 실행 파일 옆 `Logs/`로 정하고, 테스트용 환경 변수로 덮어쓸 수
  있게 한다.
- [x] Editor와 Player의 시작, 종료, 처리되지 않은 예외를 같은 형식으로 기록한다.
- [x] 기존 `DX12ENGINE_RUNTIME_PATH_LOG` 패키지 검증 계약은 유지한다.
- [x] 프레임 샘플 수집기에 warm-up, sample count, median/p95/max 계산을 추가한다.
- [x] VSync, MSAA, 해상도, adapter, 적 수, draw 수, visible 수를 결과 한 행에 기록한다.

종료 조건:

- [x] 의도적으로 잘못된 scene을 넘긴 Player의 오류가 MessageBox와 파일에 모두 남는다.
- [x] 고정 샘플 입력의 median/p95/max 단위 테스트가 통과한다.
- [x] 기존 패키지 검증이 로그 파일 생성 때문에 오염되지 않는다.

### 2. 저밀도 arena 게임 루프

먼저 적 100마리 이하에서 완결된 5분 루프를 만든다. 이 단계에서는 렌더러 상한을
고치지 않는다.

- [x] `#capsule` 절차 메시와 `Assets/Scenes/Arena.scene`을 추가한다.
- [x] 런타임 전용 component를 정의한다: player marker, enemy, health, contact damage,
  attack cooldown, XP pickup, arena state.
- [x] WASD 입력을 XZ 평면 이동으로 변환하고, 카메라는 player를 고정 offset으로
  추적한다.
- [x] 화면 밖 ring에 시간 곡선과 고정 seed로 적을 생성한다.
- [x] 적 추적, 접촉 damage cooldown, 최근접 적 자동 공격을 구현한다.
- [x] 사망 대상을 2단계로 제거하고 XP pickup을 생성한다.
- [x] pickup 획득, player 사망, 생존 timer 정지를 구현한다.
- [x] HP, XP, 생존 시간, 현재/누적 적 수를 창 제목과 로그에 표시한다.

성능상 규칙:

- 적 추적과 pickup 처리는 각각 선형 순회여야 한다.
- 최근접 탐색은 player 공격 시점에만 1회 수행한다.
- entity별 파일 로그는 금지하고 상태 요약만 주기적으로 기록한다.

종료 조건:

- [x] logic-only 테스트에서 이동, ring spawn, 추적, damage cooldown, 최근접 공격, 사망,
  XP 획득이 고정 `dt`와 seed로 재현된다.
- [x] Editor의 Play/Stop snapshot 복원이 새 런타임 component 때문에 깨지지 않는다.
- [x] 적 100마리로 실제 플레이가 가능하고 기존 회귀 테스트가 통과한다.

### 3. 상한 재현과 기준선 고정

이 단계의 목적은 최적화가 아니라 현재 실패 지점을 숫자로 남기는 것이다.

- [x] 시작 적 수를 100/500/1000/2000으로 고정하는 benchmark 옵션을 Player에
  추가한다.
- [x] warm-up 뒤 동일 시간/프레임 수만 측정하고 CSV 또는 TSV 로그를 남긴다.
- [x] 256 DrawItem 초과 예외가 발생하는 최초 N과 SRV 64 초과 재현 테스트를 기록한다.
- [x] CPU frame time 외에 draw call, root CBV bind, main visible, shadow visible을 센다.

종료 조건:

- [x] 최적화 전 기준표가 문서에 붙일 수 있는 형태로 생성된다.
- [x] 실패는 crash만 남기지 않고 마지막 적 수와 자원 사용량을 파일 로그에 남긴다.

#### Benchmark 실행 계약

```powershell
Player.exe --benchmark 100
Player.exe --benchmark 500 --benchmark-output Logs\custom-baseline.tsv
Player.exe --benchmark 2000 --culling off
```

- `--benchmark`는 `100`, `500`, `1000`, `2000`만 허용한다.
- `--culling`은 `on`(기본)과 `off`만 허용하며 benchmark 모드와 무관하게 쓸 수 있다.
  `off`는 M1.6 이전처럼 모든 item을 두 pass에 제출한다. 한 바이너리로 같은 비교의
  on 행과 off 행을 만들기 위한 토글이다.
- `--scene`이 없으면 `Arena.scene`을 선택하고, 고정 seed에서 초기 적 수와 최대 적 수를
  같은 값으로 설정한다.
- VSync를 끄고 120 frame을 warm-up한 뒤 600 frame만 측정한다. 완료되면 Player가
  자동 종료된다.
- 기본 결과는 Player 실행 파일 옆 `Logs/Player-benchmark.tsv`에 append한다. 실패도 같은
  스키마에 `status=failed`, 마지막 DrawItem 수, Object/SRV 사용량, 오류를 남긴다.

#### 최적화 전 기준선

2026-08-05, Release, 1280×720, MSAA 4x, VSync off,
NVIDIA GeForce RTX 5070 Ti에서 측정했다. 원본 행은
[M1-Baseline.tsv](M1-Baseline.tsv)에 보존한다.

| 적 N | 상태 | median/p95/max (ms) | draw | root CBV | main/shadow | DrawItem | Object CB | SRV |
|---:|---|---|---:|---:|---:|---:|---:|---:|
| 100 | 성공 | 0.292 / 2.443 / 4.159 | 209 | 212 | 104 / 104 | 104 | 104 / 256 | 7 / 64 |
| 500 | 상한 실패 | 측정 전 실패 | 0 | 0 | 0 / 0 | 504 | 504 / 256 | 7 / 64 |
| 1000 | 상한 실패 | 측정 전 실패 | 0 | 0 | 0 / 0 | 1004 | 1004 / 256 | 7 / 64 |
| 2000 | 상한 실패 | 측정 전 실패 | 0 | 0 | 0 / 0 | 2004 | 2004 / 256 | 7 / 64 |

재현 테스트에서 Arena의 정적 3개, player 1개를 포함해 적 252마리는 정확히 256
DrawItem으로 통과하고, 적 253마리는 257 DrawItem으로 최초 실패한다. 독립 SRV fixture는
64번째 할당까지 통과하고 65번째에서 `used=64 capacity=64`로 실패한다.

### 4. Object CB 동적화

- [x] `kMaxObjects`를 런타임 capacity로 바꾸고 최초 capacity는 256을 유지한다.
- [x] 필요량이 capacity를 넘으면 다음 2의 거듭제곱으로 증가를 요청한다.
- [x] `RenderFrame`의 fence wait 뒤, viewport deferred resize와 같은 안전 지점에서 모든
  `FrameResource`의 object CB를 다시 만든다.
- [x] 증가 frame에서만 `WaitForGpu()`를 허용하고 평상시 full flush가 없음을 센다.
- [x] map pointer와 `MaxDrawItems()` 같은 Editor 통계를 새 capacity와 동기화한다.

종료 조건:

- 257, 1000, 2000 item을 연속으로 제출해도 예외와 Debug Layer 메시지가 없다.
- capacity 증가 뒤 두 frame-in-flight가 모두 올바른 transform을 그린다.
- 증가가 없는 frame에서는 추가 GPU wait가 발생하지 않는다.

### 5. Opaque 인스턴싱

- [x] batch key를 mesh, submesh range, texture/normal texture, material 값으로 정의한다.
- [x] main과 shadow가 공유할 per-frame instance upload buffer를 추가한다.
- [x] transform과 inverse-transpose를 per-instance vertex stream 또는 structured input으로
  전달한다. 머티리얼은 batch당 한 번 바인딩한다.
- [x] `PsoRole` 테이블을 sample/role/draw-variant 축으로 확장한다.
- [x] Basic과 ShadowDepth에 instanced VS variant를 추가한다.
- [x] opaque singleton은 기존 경로 또는 1-instance batch 중 측정상 나은 쪽을 쓴다.
- [x] transparent와 skybox는 기존 경로를 유지한다.

종료 조건:

- 같은 enemy mesh/material N개가 main/shadow 각각 한 batch로 제출된다.
- 비균일 scale의 normal과 shadow 위치가 기존 경로와 일치한다.
- draw call 감소와 root CBV/descriptor bind 감소를 별도 counter로 확인한다.
- 인스턴싱 단독 적용 전후 성능 행을 남긴다.

#### 인스턴싱 단독 결과

2026-08-05, Release, 1280×720, MSAA 4x, VSync off,
NVIDIA GeForce RTX 5070 Ti에서 기준선과 같은 120 frame warm-up 및 600 frame 측정으로
기록했다. 원본 행은 [M1-Instancing.tsv](M1-Instancing.tsv)에 보존한다. opaque singleton도
동일한 1-instance batch 경로를 사용해 드로우 경로와 PSO 전환을 단순하게 유지했다.

| 적 N | median/p95/max (ms) | draw | root CBV | main/shadow | DrawItem | Object CB | SRV |
|---:|---|---:|---:|---:|---:|---:|---:|
| 100 | 0.277 / 2.623 / 4.201 | 11 | 9 | 104 / 104 | 104 | 104 / 256 | 7 / 64 |
| 500 | 0.373 / 2.576 / 4.096 | 11 | 9 | 504 / 504 | 504 | 504 / 512 | 7 / 64 |
| 1000 | 0.611 / 2.961 / 3.994 | 11 | 9 | 1004 / 1004 | 1004 | 1004 / 1024 | 7 / 64 |
| 2000 | 1.037 / 3.134 / 4.114 | 11 | 9 | 2004 / 2004 | 2004 | 2004 / 2048 | 7 / 64 |

WARP 회귀 fixture에서 같은 mesh/material 64개와 material/range가 다른 2개를 main과
shadow 각각 정확히 3 batch로 제출했다. 비균일 scale의 transform과 inverse-transpose를
두 pass가 같은 instance buffer에서 읽었고 Debug Layer message count는 0이었다.

### 6. Main/Shadow 프러스텀 컬링

- [x] local AABB의 8개 corner와 world matrix로 보수적인 world bounds를 계산한다.
- [x] camera view-projection plane과 directional-light view-projection plane을 각각 만든다.
- [x] `BuildDrawQueues`를 main opaque, main transparent, shadow caster 결과로 분리한다.
- [x] main에서 보이지 않아도 shadow에 영향을 주는 caster는 shadow queue에 남긴다.
- [x] 컬링 뒤 보이는 opaque item만 batch로 묶는다.
- [x] 잘못된/빈 bounds는 false negative가 없도록 visible로 취급하고 로그 counter로 센다.

종료 조건:

- [x] main 밖/shadow 안, main 안/shadow 밖, 양쪽 밖의 세 fixture가 올바른 queue에 들어간다.
- [x] 화면 경계의 큰/회전/비균일 scale mesh가 갑자기 사라지지 않는다.
- [x] 컬링 단독 기여를 비교할 수 있도록 on/off 측정 행을 남긴다.

#### 구현 결정

`Graphics/Frustum.h`의 순수 함수 3개가 판정의 전부다. 렌더러 밖에서 시험할 수 있는
형태로 분리했기 때문에 queue 분류를 device 없이 단위 테스트로 고정할 수 있다.

- world bounds는 프레임당 item마다 한 번만 만든다. 그림자 볼륨 fit과 두 컬링 판정이
  같은 8-corner box를 쓰는데, 이를 두 번 계산했을 때는 컬링으로 아낀 것보다 비용이
  더 컸다.
- 그림자 볼륨은 컬링 **전에**, 그리고 컬링 결과가 아니라 모든 opaque item에 맞춘다.
  카메라를 따라가는 볼륨은 플레이어가 돌 때마다 그림자 맵 배율을 바꾸고, 배율이
  프레임마다 흔들리는 그림자 맵은 모든 경계에서 기어다닌다.
- caster 판정은 camera frustum을 빛의 반대 방향으로 쓸어낸 볼륨이다. 안쪽 법선이
  쓸기 방향과 같은 평면만 남기면 결과는 실제 쓸린 볼륨을 항상 포함한다. 화면 밖에
  있어도 화면 안으로 그림자를 던질 수 있는 caster는 이 볼륨에 들어온다.
- main에서 보이는 opaque item은 무조건 caster로 둔다. 기하학적으로도 참이지만,
  batch가 이 포함 관계에 의존하므로 코드에 명시해 불변식으로 만들었다.
- batch는 한 벌만 만든다. 각 batch의 instance run을 main-visible 먼저 정렬해 두면
  main pass는 앞부분만, shadow pass는 전체를 그린다. 두 pass가 다른 집합을 그리면서도
  프레임당 instance 기록은 "그려지는 item당 하나"로 유지된다.
- 평면 판정에는 world 단위 `1e-4` 여유를 준다. 컬링은 최적화이므로 반올림과 다투다
  지면 두 번 그릴지언정 빠뜨리면 안 된다.

#### 컬링 단독 결과

2026-08-05, Release, 1280×720, MSAA 4x, VSync off,
NVIDIA GeForce RTX 5070 Ti. 원본 행은 [M1-Culling.tsv](M1-Culling.tsv)에 보존한다.

on/off를 N마다 바로 이어서 짝지어 12회씩 돌리고, 12개 median의 중앙값을 적었다. 컬링
off는 M1.6 이전 렌더러와 같은 queue를 제출하므로 이 표의 off 열이 인스턴싱 단독
기준선이다. 절대값이 [M1-Instancing.tsv](M1-Instancing.tsv)와 다른 것은 세션이 다르기
때문이며, 짝지어 잰 on/off 사이만 비교에 쓴다.

한 셀의 median조차 실행마다 ±15%씩 흔들린다(예: off 2000은 1.060~1.308). 4회로는
부호가 뒤집혔고 12회에서야 안정됐다. 아래 차이는 그 흔들림보다 작으므로 **추세로만**
읽어야 한다.

| 적 N | off median (ms) | on median (ms) | 변화 | main visible | shadow visible |
|---:|---:|---:|---:|---:|---:|
| 100 | 0.679 | 0.665 | -2% | 104 → 42 | 104 |
| 500 | 0.773 | 0.731 | -5% | 504 → 215 | 504 |
| 1000 | 0.921 | 0.869 | -6% | 1004 → 451 | 1004 |
| 2000 | 1.139 | 1.128 | -1% | 2004 → 911 | 2004 |

draw call은 두 설정 모두 11이다. 컬링은 batch 수가 아니라 batch당 instance 수를 줄인다.
object/instance capacity도 두 설정에서 같다. instance capacity가 두 배가 되지 않는 것이
main/shadow가 instance run을 공유한다는 증거이며, 회귀 테스트가 이 값을 고정한다.

**보이는 item을 55% 줄였는데 프레임타임은 0~6%만 줄었다.** 이 구간에서 컬링이 지우지
못하는 항목이 프레임타임의 대부분이라는 뜻이고, 그중 하나는 이미 특정된다.
`UpdateObjectConstants`는 여전히 제출된 item 전부에 176바이트를 쓰는데, 인스턴싱 경로의
b0는 batch당 한 번만 읽힌다. 2000마리에서 이 기록의 거의 전부가 아무도 읽지 않는
바이트다. 컬링을 더 조여도 여기는 줄지 않으므로 다음 지렛대는 8번 작업에 둔다.

`shadow_culled`는 모든 행에서 0이다. 추측이 아니라 Arena의 배치 때문이다. Arena Camera의
forward는 약 `(0, -0.659, 0.752)`, Arena Sun의 방향은 약 `(-0.325, -0.783, 0.530)`으로
사잇각이 24°밖에 되지 않는다. 태양이 카메라 뒤에서 카메라가 보는 쪽으로 비치므로 쓸기
방향이 시선 반대 방향과 거의 같고, far plane을 제외한 모든 평면이 쓸기에서 풀린다.
즉 **화면 앞의 거의 모든 것이 화면 안으로 그림자를 던질 수 있다** — 판정이 아니라 이
조명 배치의 사실이다. 태양이 시선을 가로지르는 scene에서는 같은 코드가 caster를 줄인다.
그림자 쪽에서 더 얻으려면 광원 볼륨을 카메라가 보는 영역에 맞춰야 하는데, 그림자 맵
배율 안정성을 포기하는 변경이므로 M1.6 범위 밖으로 둔다.


### 7. SRV heap 상한 제거

D3D12 descriptor heap 자체는 resize할 수 없으므로 단순 `64 -> 큰 수` 변경으로 끝내지
않는다. 이 작업은 별도 변경으로 유지한다.

- [x] `DescriptorHandle`의 장기 식별자는 heap 주소가 아니라 slot index가 되게 한다.
- [x] descriptor 작성/바인딩 시 allocator가 현재 heap의 CPU/GPU 주소를 resolve한다.
- [x] capacity 부족 시 더 큰 heap을 만들고 live descriptor를 같은 index로 복사한다.
- [x] 교체는 GPU idle인 frame 경계에서만 수행하며 heap generation을 증가시킨다.
- [x] RenderTarget, DepthTarget, ResourceManager의 저장 handle을 index 기반으로 전환한다.
- [x] ImGui DX12 backend의 heap pointer와 font descriptor를 안전한 frame 경계에서
  **rebind**한다. 중간 frame의 stale `ImTextureID` 사용을 막는다.
  (원래 계획의 "재생성"은 불가능하다. 아래 참조.)
- [x] free-list 재사용과 `FreeByCpuHandle`의 새 heap generation 동작을 검증한다.

종료 조건:

- [x] 64 경계를 넘겨 128개 이상의 texture/SRV를 생성해도 Editor와 Player가 동작한다.
- [x] heap 증가 전 생성된 texture, scene color, shadow map, ImGui font가 모두 유효하다.
- [x] 증가 frame을 포함해 Debug Layer 메시지가 0이다.

#### 구현 결정

**shader-visible heap은 복사원이 될 수 없다.** 이것이 설계 전체를 결정했다. 실측한
Debug Layer 메시지는 다음과 같다.

```
ID3D12Device::CopyDescriptorsSimple: SrcDescriptorRangeStart points to a
descriptor heap type that is CPU write only, so reading it (in this case a
copy source) is invalid.
```

그래서 저장소를 둘로 나눴다.

- **staging**: CPU 전용 heap을 **page 단위로 append**한다. 절대 옮기거나 교체하지
  않으므로 예전에 넘겨준 CPU handle이 영원히 유효하다. `FreeByCpuHandle`이 heap 성장
  이후에도 동작하는 이유가 이것이다. 성장에 GPU 개입이 전혀 없어 command list 기록
  도중에도 `Allocate()`를 부를 수 있다 — ImGui backend가 `RenderDrawData` 안에서
  descriptor를 할당하므로 이건 선택이 아니라 요구사항이다.
- **shader-visible**: `SetDescriptorHeaps`가 type당 하나만 받으므로 단일 heap이어야
  하고, 따라서 성장은 곧 교체다. GPU idle인 frame 경계에서만 바꾸고 generation을
  올린다. 내용은 staging에서 **같은 index로** 복사한다.

descriptor 기록은 프레임 중 아무 때나 일어나므로, 기록이 끝나고 **제출 직전**에
`PublishPendingWrites()`로 staging→visible 복사를 한다. 기록 중은 안전하다: descriptor는
GPU가 list를 *실행*할 때 읽히기 때문이다.

교체된 heap은 곧바로 버리지 않고 한 세대 더 살려둔다. 포인터를 캐시한 host layer가
그 프레임 안에서 아직 바인딩할 수 있기 때문이다.

**heap 교체 권한은 host당 정확히 한 곳만 가진다.** `RenderFrame`은 아무것도 하지 않는
host(Player, 모든 테스트)를 위해 스스로 성장시키지만, 캐싱 layer를 가진 host는
`SetHostManagesDescriptorHeapGrowth(true)`로 이를 가져가야 한다. 둘 다 성장시키면
host가 이미 handle을 resolve해 둔 프레임 도중에 heap이 또 바뀌고, 그러면
`SetGraphicsRootDescriptorTable`이 바인딩되지 않은 heap의 handle을 받는다. 실제로
이 상태를 만들어 Debug Layer 메시지 63개를 재현했고, 소유권을 배타적으로 바꾼 뒤 0이
됐다.

**성장 시점은 `RenderFrame` 안, host 업데이트가 끝난 뒤다.** 한 프레임이 로드하는 것
— scene 하나 분량의 texture, asset preview — 은 그 시점에 이미 전부 할당돼 있으므로,
기록 직전인 여기가 그것을 감당해야 하는 마지막 지점이다. host가 성장 시점을 따로
고르게 하면 그 host가 이미 handle을 resolve해 둔 프레임 도중에 heap이 또 바뀔 수 있어,
`SetGraphicsRootDescriptorTable`이 바인딩되지 않은 heap의 handle을 받는다(실제로 이
상태에서 Debug Layer 메시지 63개를 재현했다). 그래서 성장은 renderer 한 곳에서만 하고,
heap이 바뀌면 `SetDescriptorHeapChangedCallback`으로 host에 알린다.

reserve는 256 slot이다. 경계 이후에도 할당은 일어나므로(ImGui backend가 `RenderDrawData`
안에서 할당한다) 그만큼은 이미 사용 중인 heap에 들어가야 한다. descriptor 하나가
32바이트라 256칸은 8 KB이고, 모자라면 조용한 오작동이 아니라 예외다.

성장 용량은 `D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_1`(1,000,000)으로
clamp한다. 2의 거듭제곱으로만 올리면 524,288을 넘는 순간 1,048,576을 요청해 실제 한계의
절반쯤에서 먼저 실패한다. 그 위는 명확한 hardware-limit 예외로 처리한다.

#### ImGui: 재생성이 아니라 rebind

계획의 "backend 재생성"은 ImGui 1.92에서 **동작하지 않는다**. `ImGui_ImplDX12_Shutdown()`
+ `Init()`을 두 시점에서 시도해 둘 다 ImGui **core**의 `NewFrame()`에서 access violation을
확인했다.

- 첫 frame 이전
- font texture 생성 후 약 1000 frame 경과 시점

`Shutdown`이 `InvalidateDeviceObjects`를 통해 font texture를 `ImTextureStatus_Destroyed`로
표시하는데 core가 여기서 복구하지 못한다. upstream에는 heap 포인터를 갱신하는 API가
없다.

그래서 vendor에 최소 확장 두 개를 추가했다. 위치는 모두
`ThirdParty/imgui/backends/imgui_impl_dx12.{h,cpp}`이고,
`[Dx12Engine LOCAL PATCH - not upstream. Re-check on every Dear ImGui update.]`
주석으로 표시했다. ImGui 갱신 시 확인할 diff는 이 블록들이다.

**1. 논리 texture id (`SrvDescriptorResolveFn`).** 기본적으로 `ImTextureID`는 raw GPU
주소라서, draw list에 기록된 뒤 heap이 바뀌면 죽는다. resolve 콜백을 설정하면 id는
불투명한 **slot index**가 되고 draw 시점에 현재 heap 기준으로 주소로 바뀐다. bind 지점은
`RenderDrawData` 안 한 곳뿐이라 패치는 그 한 줄이다. 이것이 프레임 중 아무 때나 성장해도
안전하게 만드는 핵심이다.

engine 쪽도 같은 계약을 따른다. `SceneTextureId()`, `ShadowTextureId()`,
`ResourceManager::TextureSrvIndex()`가 주소가 아니라 slot index를 돌려준다. 반면 scene
pass가 직접 바인딩할 때 쓰는 `TextureSRV()`는 여전히 주소이고, 기록 시점에 resolve된다.

논리 id에는 딸린 조건이 하나 있다. slot 0은 실재하는 slot이고(`ResourceManager`가 처음
만드는 `#white`), ImGui의 기본 invalid id도 0이다. 그대로 두면 slot 0을 `ImGui::Image()`로
표시하는 순간 `ImDrawCmd::GetTexID()`의 assert에 걸린다. `imgui.h`가 바로 이 경우를 위해
안내하는 대로 `imconfig.h`에 다음을 정의했다.

```cpp
#define ImTextureID_Invalid ((ImTextureID)-1)
```

이건 backend 패치가 아니라 ImGui가 지정한 설정 지점이다. overlay 테스트가 slot 0을 매
frame 실제로 그리고, `static_assert`로 이 정의가 사라지면 컴파일이 실패하게 고정했다
(정의를 주석 처리하면 실제로 컴파일 에러가 나는 것을 확인했다).

**2. `ImGui_ImplDX12_RebindDescriptorHeap()`.** backend가 `SetDescriptorHeaps`로 직접
바인딩하는 `bd->pd3dSrvDescHeap`과 복사본 `InitInfo.SrvDescriptorHeap`을 새 heap으로
바꾼다. 논리 id 계약 아래에서는 캐시된 주소가 하나도 없으므로 이것으로 끝이다(계약을
쓰지 않는 경우를 위해 texture handle을 같은 index로 rebase하는 경로도 남겨 뒀다).
texture resource와 font atlas와 status는 건드리지 않으므로 font를 `Destroyed`로 만드는
문제가 발생하지 않는다.

검증:

- 자동 테스트 `functional/imgui-overlay-srv-heap-growth`가 실제 `ImGuiLayer`를 만들어
  font가 생성될 때까지 frame을 돌린 뒤, **reserve보다 큰 512개 burst**를 프레임 경계
  이후에 할당하고 다시 그린다. heap generation이 올라가는 것, 성장 이후에도 계속 그려지는
  것, Debug Layer 메시지 0을 확인한다. 이를 위해 EngineTests가 ImGui를 직접 컴파일한다.
- Editor 실측: page size를 4로 낮추고 성장을 강제해 약 2400 frame 돌렸다. `debug_messages=0`,
  정상 종료. `frame_summary` 로그에 `debug_messages`와 `has_debug_layer`를 추가해 이
  조건을 Editor와 Player 양쪽에서 로그로 확인할 수 있게 했다.

### 8. M1 통합과 정리

- [ ] 최적화 토글을 각각 제공해 baseline, instancing only, culling only, both를 측정한다.
- [ ] 임시 benchmark 옵션이 일반 Player 시작 경로와 패키지를 바꾸지 않는지 확인한다.
- [ ] `Arena.scene`을 M1 기본 실행 대상으로 정하되 `--scene` 계약은 유지한다.
- [ ] 코드에 남은 고정 256/64 가정과 오래된 오류 문구를 제거한다.
- [ ] `UpdateObjectConstants`가 제출된 item 전부에 쓰는 것을 줄인다. 인스턴싱 경로의
  b0는 batch당 한 번만 읽히므로 2000마리에서 기록의 대부분은 읽히지 않는다. 6번
  작업의 측정에서 나온 항목이다.
- [ ] 결과표와 Debug Layer 결과를 `ROADMAP.md`의 M1 기록에 반영한다.
- [ ] M1 체크박스와 `v1.4` 완료 조건을 모두 확인한 뒤 이 임시 문서를 삭제한다.

## 통합 검증

### 자동 검증

각 작업 완료 시 다음을 반복한다.

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' `
  ..\Dx12Engine.slnx /m /p:Configuration=Debug /p:Platform=x64
..\Output\Tests\x64\Debug\EngineTests.exe

& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' `
  ..\Dx12Engine.slnx /m /p:Configuration=Release /p:Platform=x64
..\Output\Tests\x64\Release\EngineTests.exe

powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File Tests\VerifyPlayerPackage.ps1
```

### 성능 매트릭스

Release, 동일 adapter와 해상도, VSync off, 고정 seed에서 측정한다.

| 적 N | baseline | instancing only | culling only | both |
|---:|---:|---:|---:|---:|
| 100 | median/p95/max | median/p95/max | median/p95/max | median/p95/max |
| 500 | median/p95/max | median/p95/max | median/p95/max | median/p95/max |
| 1000 | median/p95/max | median/p95/max | median/p95/max | median/p95/max |
| 2000 | median/p95/max | median/p95/max | median/p95/max | median/p95/max |

각 행에는 draw call, root bind, main visible, shadow visible, object capacity, SRV capacity도
함께 기록한다. 프레임타임 수치만으로 인스턴싱과 컬링의 기여를 추측하지 않는다.

### 수동 검증

- 적 1000마리 설정으로 5분 생존을 실제 시도한다.
- 이동 중 카메라 추적, ring spawn, 접촉 damage, 자동 공격, XP 획득을 확인한다.
- 적과 pickup의 반복 생성/삭제 뒤 stale entity 접근이나 메모리 증가가 없는지 본다.
- MSAA 1x/4x, VSync on/off, Editor offscreen과 Player swap-chain을 각각 확인한다.
- Debug Layer message count가 0인지 로그와 테스트에서 확인한다.

## 완료 조건

M1은 다음 항목이 모두 참일 때만 완료한다.

- [ ] 적 1000마리가 몰려오는 arena에서 60 Hz를 유지한다.
- [ ] 5분 생존 플레이를 실제로 시도할 수 있다.
- [ ] 적 100/500/1000/2000의 median/p95/max 비교표가 있다.
- [ ] 인스턴싱과 컬링의 기여가 분리되어 있다.
- [ ] Object CB와 SRV heap이 기존 고정 상한을 넘겨도 안전하게 증가한다.
- [ ] Debug Layer 메시지가 0이다.
- [ ] 기존 자동 테스트와 Player 패키지 검증이 모두 통과한다.
