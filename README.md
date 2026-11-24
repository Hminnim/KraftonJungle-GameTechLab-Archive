# Krafton Jungle GameTechLab 4th — Custom C++ / DX11 Engine Archive

- **개발 기간**: 2026.08.31 ~
- **작성자**: 황성민 (`Hminnim` / `jameshwang16@gmail.com`)

크래프톤 정글 게임테크랩 4기 과정 중 팀 프로젝트를 통해 공동 개발한 **자체 C++ / DirectX 11 게임 엔진**의 주차별 최종 결과물(디폴트 및 비-디폴트 feature 브랜치 포함)을 통합 아카이빙한 저장소입니다.  
각 주차 폴더 내 `MY_CONTRIBUTION.md`에서 본인이 직접 설계·구현한 기능 상세를 확인할 수 있습니다.

---

## 핵심 구현 시스템 요약

### 1. 자체 3D 물리 & 충돌 엔진
- **관련 폴더**: [`Week06_MovementComponent_SweepBase`](./Week06_MovementComponent_SweepBase) · [`Week09_Custom3DCollision_FMOD_Portal`](./Week09_Custom3DCollision_FMOD_Portal)
- `UMovementComponent` 계층(`Projectile`, `Rotating`) 및 `PlaneConstraint` 이동 제약 구현
- `UShapeComponent`(Box / Sphere / Capsule) 및 `ECollisionResponse`(Ignore / Overlap / Block) 설계
- `CollisionDetector` 기반 3D Overlap 쿼리 및 **Box-Box / Capsule 포함 Shape Sweep** 충돌 감지 구현
- Portal 게임플레이 적용 과정에서 발생한 바닥 낙하 버그, 콜라이더 끼임, 텔레포트 직후 프레임 스파이크 해결

### 2. 상용 물리 통합 (PhysX Vehicle) & FBX 임포트 파이프라인
- **관련 폴더**: [`Week13_PhysX_Vehicle_FBXImport`](./Week13_PhysX_Vehicle_FBXImport)
- `FPhysXVehicleManager` 구현 및 자체 엔진 `PhysScene`·`BodyInstance` 연동
- `UWheeledVehicleMovementComponent`, `AWheeledVehicle`, `PlayerController` 입력 바인딩 및 `SkeletalMeshComponent` 휠 본 실시간 동기화
- 스켈레톤이 없는 리지드 메시 차량 FBX를 임포트 시 **스켈레탈 계층으로 자동 변환**하고 휠 자동 감지·직렬화 및 차체 충돌 형상 개선

### 3. 언리얼 스타일 스켈레탈 애니메이션 그래프 & 전용 에디터
- **관련 폴더**: [`Week11_AnimGraph_BlendSpace_Editor`](./Week11_AnimGraph_BlendSpace_Editor)
- `UAnimInstance`, `FAnimNodeBase`, `FAnimationRuntime`, `FQuat` Slerp 기반 애니메이션 평가 그래프 구축
- `SequencePlayer`, `TwoWayBlend`(Pose Blending), **1D `BlendSpace`** 가중치 블렌딩 구현
- `AnimNotify` / `AnimNotifyState` 큐 구현 및 Lua 스크립트(`LuaBindings`) 이벤트 바인딩
- 전용 ImGui 에디터 2종(`AnimationBlendEditorPanel`, `AnimationSequenceEditorPanel`) End-to-End 구현

### 4. 엔진 코어 인프라 (`FName`, 바이너리 메시 캐싱, 오디오 백엔드 추상화)
- **관련 폴더**: [`Week01_DX11_Core_And_Game`](./Week01_DX11_Core_And_Game) · [`Week02_Scene_Serialization`](./Week02_Scene_Serialization) · [`Week03_FName_Billboard_SubUV`](./Week03_FName_Billboard_SubUV) · [`Week04_ObjParser_BinaryCache_Viewer`](./Week04_ObjParser_BinaryCache_Viewer) · [`Week05_HiZ_Occlusion_LOD_FNameShard`](./Week05_HiZ_Occlusion_LOD_FNameShard) · [`Week09_Custom3DCollision_FMOD_Portal`](./Week09_Custom3DCollision_FMOD_Portal)
- **엔진 기초 & 씬 직렬화**: DX11 코어 모듈 분리, JSON 기반 Scene Save/Load/New 및 `FUUID`/`NextUUID` 무결성 복원
- **`FName` & `FNamePoolShard`**: CityHash 기반 `ComparisonIndex`/`DisplayIndex` 분리 비교, 중복 문자열 제거, `_Number` Actor 자동 넘버링, `FArchive` 직렬화 및 샤드 풀 리팩토링
- **OBJ 파서 & 바이너리 캐싱**: 중복 정점 제거, 파싱 속도 최적화, AABB 최적화, **해시 무결성 검증 `.bin` 캐싱** 및 독립 실행형 `_OBJVIEWER` 구축
- **오디오 백엔드 추상화**: `IAudioBackend` 인터페이스를 중심으로 `FMODCoreBackend`와 `NullAudioBackend` 분리 설계

### 5. Cascade 스타일 파티클 시스템 (코어 / Ribbon / LOD)
- **관련 폴더**: [`Week12_CascadeParticle_Ribbon_LOD`](./Week12_CascadeParticle_Ribbon_LOD)
- 언리얼 Cascade 구조를 분석해 `UDistribution`, `UParticleModule`, `UParticleEmitter`, `UParticleLODLevel`, `FParticleEmitterInstance` 코어 아키텍처 설계
- 이동 궤적 기반 동적 지오메트리 생성 **Ribbon Emitter**(`ParticleModuleTypeDataRibbon`) 구현
- 거리/성능 기반 **Particle System LOD** 전환 런타임 및 `ParticleSystemEditor` UI 통합

### 6. GPU 렌더링 파이프라인 & 최적화 (Hi-Z, Shadow, Decal, Lighting)
- **관련 폴더**: [`Week03_FName_Billboard_SubUV`](./Week03_FName_Billboard_SubUV) · [`Week05_HiZ_Occlusion_LOD_FNameShard`](./Week05_HiZ_Occlusion_LOD_FNameShard) · [`Week07_UberLit_LightSceneProxy`](./Week07_UberLit_LightSceneProxy) · [`Week08_ShadowAtlas_Caching_LOD`](./Week08_ShadowAtlas_Caching_LOD) · [`Week10_ForwardProjectedDecal`](./Week10_ForwardProjectedDecal)
- **GPU Hi-Z Occlusion Culling & Mesh LOD**: Compute Shader(`HzBuildCS`, `HzCullCS`) 기반 뎁스 피라미드 생성 및 멀티 뷰포트 오클루전 컬링, `Meshoptimizer` LOD 통합 및 버퍼 조회 병목 개선
- **실시간 그림자 시스템**: Directional/Point/Spot 통합 그림자, **Shadow Map Atlas** 패킹 및 프리뷰 셰이더, Dirty Flag 기반 **Shadow Caching**, 부하 대응 **Shadow LOD / Degradation**
- **Forward Projected Decal & Lighting**: Forward 데칼 패스의 조명·그림자·노멀 블렌딩 연동, Fake SpotLight, `LightSceneProxy` 4종 분리, UberLit(Gouraud/Lambert/Phong) 및 TBN 노멀 매핑, Billboard/SubUV 렌더러

---

<details>
<summary><strong>주차별 구현 내역 (W01 ~ W13) 펼치기</strong></summary>

- **W01 (08.31 ~ 09.03)** — [`Week01_DX11_Core_And_Game`](./Week01_DX11_Core_And_Game/MY_CONTRIBUTION.md): DX11 기초 엔진 모듈 분리(`URenderer`, `UInputManager`, `CollisionManager`), 턴제 알까기 게임 및 스킬 드래프트
- **W02 (09.09)** — [`Week02_Scene_Serialization`](./Week02_Scene_Serialization/MY_CONTRIBUTION.md): JSON 기반 Scene Save/Load/New 직렬화, `FUUID`/`NextUUID` 복원, Factory 기반 ClearScene
- **W03 (09.11 ~ 09.16)** — [`Week03_FName_Billboard_SubUV`](./Week03_FName_Billboard_SubUV/MY_CONTRIBUTION.md): 자체 `FVector`, CityHash 기반 `FName` 풀 시스템(Numbering/Serialize), Billboard & SubUV 렌더러
- **W04 (09.17 ~ 09.22)** — [`Week04_ObjParser_BinaryCache_Viewer`](./Week04_ObjParser_BinaryCache_Viewer/MY_CONTRIBUTION.md): OBJ 파서 속도 최적화, 중복 정점 제거, 해시 검증 `.bin` 캐싱, AABB 최적화, 독립 `_OBJVIEWER`
- **W05 (09.25 ~ 09.30)** — [`Week05_HiZ_Occlusion_LOD_FNameShard`](./Week05_HiZ_Occlusion_LOD_FNameShard/MY_CONTRIBUTION.md): `FNamePoolShard`, `Meshoptimizer` LOD, Compute Shader 기반 GPU Hi-Z Occlusion Culling(멀티 뷰포트)
- **W06 (10.03 ~ 10.22)** — [`Week06_MovementComponent_SweepBase`](./Week06_MovementComponent_SweepBase/MY_CONTRIBUTION.md): `UMovementComponent` 계층(Projectile/Rotating), `CollisionShape` & World Sweep 기반 설계
- **W07 (10.10 ~ 10.14)** — [`Week07_UberLit_LightSceneProxy`](./Week07_UberLit_LightSceneProxy/MY_CONTRIBUTION.md): `LightComponent`/`LightSceneProxy` 4종 분리, UberLit(Gouraud/Lambert/Phong), TBN 노멀 매핑
- **W08 (10.16 ~ 10.21)** — [`Week08_ShadowAtlas_Caching_LOD`](./Week08_ShadowAtlas_Caching_LOD/MY_CONTRIBUTION.md): Directional/Point/Spot 그림자, Shadow Map Atlas & 프리뷰, Shadow Caching, Shadow LOD/Degradation
- **W09 (10.22 ~ 10.28)** — [`Week09_Custom3DCollision_FMOD_Portal`](./Week09_Custom3DCollision_FMOD_Portal/MY_CONTRIBUTION.md): 자체 3D Collision & Sweep 엔진(Box/Sphere/Capsule), `IAudioBackend`/FMOD 추상화, Portal 물리·텔레포트 해결
- **W10 (10.30 ~ 11.04)** — [`Week10_ForwardProjectedDecal`](./Week10_ForwardProjectedDecal/MY_CONTRIBUTION.md): Forward Projected Decal(조명·그림자·노멀 연동, Fade, Stat, Serialize), Fake SpotLight
- **W11 (11.05 ~ 11.11)** — [`Week11_AnimGraph_BlendSpace_Editor`](./Week11_AnimGraph_BlendSpace_Editor/MY_CONTRIBUTION.md): AnimGraph, Pose Blending, 1D BlendSpace, AnimNotify/NotifyState(Lua 바인딩), 전용 ImGui 에디터 2종
- **W12 (11.17 ~ 11.18)** — [`Week12_CascadeParticle_Ribbon_LOD`](./Week12_CascadeParticle_Ribbon_LOD/MY_CONTRIBUTION.md): Cascade 스타일 파티클 코어(`Distribution`/`Module`/`Emitter`), Ribbon Emitter, Particle System LOD
- **W13 (11.23 ~ 11.24)** — [`Week13_PhysX_Vehicle_FBXImport`](./Week13_PhysX_Vehicle_FBXImport/MY_CONTRIBUTION.md): PhysX Vehicle 통합(`FPhysXVehicleManager`, `UWheeledVehicleMovementComponent`), Rigid→Skeletal 차량 FBX 임포트

</details>
