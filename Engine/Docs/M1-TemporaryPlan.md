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
```

- `--benchmark`는 `100`, `500`, `1000`, `2000`만 허용한다.
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

- [ ] local AABB의 8개 corner와 world matrix로 보수적인 world bounds를 계산한다.
- [ ] camera view-projection plane과 directional-light view-projection plane을 각각 만든다.
- [ ] `BuildDrawQueues`를 main opaque, main transparent, shadow caster 결과로 분리한다.
- [ ] main에서 보이지 않아도 shadow에 영향을 주는 caster는 shadow queue에 남긴다.
- [ ] 컬링 뒤 보이는 opaque item만 batch로 묶는다.
- [ ] 잘못된/빈 bounds는 false negative가 없도록 visible로 취급하고 로그 counter로 센다.

종료 조건:

- main 밖/shadow 안, main 안/shadow 밖, 양쪽 밖의 세 fixture가 올바른 queue에 들어간다.
- 화면 경계의 큰/회전/비균일 scale mesh가 갑자기 사라지지 않는다.
- 컬링 단독 기여를 비교할 수 있도록 on/off 측정 행을 남긴다.

### 7. SRV heap 상한 제거

D3D12 descriptor heap 자체는 resize할 수 없으므로 단순 `64 -> 큰 수` 변경으로 끝내지
않는다. 이 작업은 별도 변경으로 유지한다.

- [ ] `DescriptorHandle`의 장기 식별자는 heap 주소가 아니라 slot index가 되게 한다.
- [ ] descriptor 작성/바인딩 시 allocator가 현재 heap의 CPU/GPU 주소를 resolve한다.
- [ ] capacity 부족 시 더 큰 heap을 만들고 live descriptor를 같은 index로 복사한다.
- [ ] 교체는 GPU idle인 frame 경계에서만 수행하며 heap generation을 증가시킨다.
- [ ] RenderTarget, DepthTarget, ResourceManager의 저장 handle을 index 기반으로 전환한다.
- [ ] ImGui DX12 backend의 heap pointer와 font descriptor를 안전한 frame 경계에서
  재생성한다. 중간 frame의 stale `ImTextureID` 사용을 막는다.
- [ ] free-list 재사용과 `FreeByCpuHandle`의 새 heap generation 동작을 검증한다.

종료 조건:

- 64 경계를 넘겨 128개 이상의 texture/SRV를 생성해도 Editor와 Player가 동작한다.
- heap 증가 전 생성된 texture, scene color, shadow map, ImGui font가 모두 유효하다.
- 증가 frame을 포함해 Debug Layer 메시지가 0이다.

### 8. M1 통합과 정리

- [ ] 최적화 토글을 각각 제공해 baseline, instancing only, culling only, both를 측정한다.
- [ ] 임시 benchmark 옵션이 일반 Player 시작 경로와 패키지를 바꾸지 않는지 확인한다.
- [ ] `Arena.scene`을 M1 기본 실행 대상으로 정하되 `--scene` 계약은 유지한다.
- [ ] 코드에 남은 고정 256/64 가정과 오래된 오류 문구를 제거한다.
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
