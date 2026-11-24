# Krafton Jungle GameTechLab 4th — Custom C++ / DX11 Engine Archive (W01 ~ W13)

- **개발 기간**: 2026.08.31 ~ 2026.11.24 (13주)
- **작성자**: 황성민 (`Hminnim` / `jameshwang16@gmail.com`)
- **포지션 포커스**: Game Engine Core & Client Programming (C++, DirectX 11, HLSL, PhysX, FMOD, Lua, ImGui)

이 레포지토리는 크래프톤 정글 게임테크랩 4기 13주 과정 동안 13개 팀 프로젝트를 통해 공동 개발한 **자체 C++ / DirectX 11 게임 엔진**의 주차별 최종 결과물(디폴트 및 비-디폴트 feature 브랜치 포함)을 한곳에 통합 아카이빙한 저장소입니다.  
각 주차 폴더 내 `MY_CONTRIBUTION.md`에서 본인이 직접 설계·구현한 기능 요약을 확인할 수 있습니다.

---

## 핵심 구현 시스템 요약 (엔진 코어 / 클라이언트 중심)

| 도메인 | 핵심 구현 기능 | 아카이브 폴더 |
| :--- | :--- | :--- |
| **자체 3D 물리 & 충돌** | 3D Collision & Shape Sweep 엔진(Box/Sphere/Capsule, Overlap/Block, `PlaneConstraint`), Portal 물리/프레임 스파이크 해결 | [`Week06_MovementComponent_SweepBase`](./Week06_MovementComponent_SweepBase), [`Week09_Custom3DCollision_FMOD_Portal`](./Week09_Custom3DCollision_FMOD_Portal) |
| **상용 물리 통합** | **PhysX Vehicle**(`FPhysXVehicleManager`, `UWheeledVehicleMovementComponent`), Rigid Mesh 차량 FBX → Skeletal 계층 자동 변환·휠 감지 | [`Week13_PhysX_Vehicle_FBXImport`](./Week13_PhysX_Vehicle_FBXImport) |
| **스켈레탈 애니메이션** | 언리얼 스타일 **AnimGraph**(`UAnimInstance`, `FAnimNodeBase`), Pose Blending, **1D BlendSpace**, **AnimNotify/State**(Lua 바인딩), 전용 ImGui 에디터 2종 | [`Week11_AnimGraph_BlendSpace_Editor`](./Week11_AnimGraph_BlendSpace_Editor) |
| **엔진 코어 인프라** | CityHash 기반 **`FName` / `FNamePoolShard`**, OBJ 중복 정점 제거 + **해시 검증 `.bin` 캐싱**, **`IAudioBackend`**(FMOD/Null) 추상화 | [`Week03_FName_Billboard_SubUV`](./Week03_FName_Billboard_SubUV), [`Week04_ObjParser_BinaryCache_Viewer`](./Week04_ObjParser_BinaryCache_Viewer), [`Week05_HiZ_Occlusion_LOD_FNameShard`](./Week05_HiZ_Occlusion_LOD_FNameShard), [`Week09_Custom3DCollision_FMOD_Portal`](./Week09_Custom3DCollision_FMOD_Portal) |
| **VFX / 파티클** | Cascade 스타일 파티클 코어(`Distribution`/`Module`/`Emitter`/`LODLevel`), **Ribbon Emitter**, **Particle LOD** | [`Week12_CascadeParticle_Ribbon_LOD`](./Week12_CascadeParticle_Ribbon_LOD) |
| **GPU 렌더링 & 최적화** | Compute Shader **Hi-Z Occlusion Culling**(멀티 뷰포트), Mesh LOD, **Shadow Atlas / Caching / Degradation**, **Forward Projected Decal**, UberLit | [`Week05_HiZ_Occlusion_LOD_FNameShard`](./Week05_HiZ_Occlusion_LOD_FNameShard), [`Week07_UberLit_LightSceneProxy`](./Week07_UberLit_LightSceneProxy), [`Week08_ShadowAtlas_Caching_LOD`](./Week08_ShadowAtlas_Caching_LOD), [`Week10_ForwardProjectedDecal`](./Week10_ForwardProjectedDecal) |

---

## 주차별 디렉토리 인덱스

| 주차 | 기간 | 디렉토리 | 원본 브랜치 | 요약 |
| :---: | :---: | :--- | :---: | :--- |
| **W01** | 08.31~09.03 | [`Week01_DX11_Core_And_Game`](./Week01_DX11_Core_And_Game) | `main` | DX11 기초 엔진 모듈 분리, 턴제 게임플레이·스킬 드래프트 |
| **W02** | 09.09 | [`Week02_Scene_Serialization`](./Week02_Scene_Serialization) | `main` | Scene Save/Load/New 직렬화, `FUUID`/`NextUUID` 복원 |
| **W03** | 09.11~09.16 | [`Week03_FName_Billboard_SubUV`](./Week03_FName_Billboard_SubUV) | `Develop` | 자체 `FVector`, `FName` 풀 시스템, Billboard/SubUV 렌더러 |
| **W04** | 09.17~09.22 | [`Week04_ObjParser_BinaryCache_Viewer`](./Week04_ObjParser_BinaryCache_Viewer) | `main` | OBJ 파서 최적화, 중복 정점 제거, 해시 검증 `.bin` 캐싱, 독립 ObjViewer |
| **W05** | 09.25~09.30 | [`Week05_HiZ_Occlusion_LOD_FNameShard`](./Week05_HiZ_Occlusion_LOD_FNameShard) | `main` + `feature/*` | `FNamePoolShard`, Mesh LOD, GPU Hi-Z Occlusion Culling(멀티 뷰포트) |
| **W06** | 10.03~10.22 | [`Week06_MovementComponent_SweepBase`](./Week06_MovementComponent_SweepBase) | `feature/movementcomponent` | `UMovementComponent` 계층, `CollisionShape` & World Sweep 기반 |
| **W07** | 10.10~10.14 | [`Week07_UberLit_LightSceneProxy`](./Week07_UberLit_LightSceneProxy) | `hwang` | `LightComponent`/`LightSceneProxy` 4종, UberLit, TBN 노멀 매핑 |
| **W08** | 10.16~10.21 | [`Week08_ShadowAtlas_Caching_LOD`](./Week08_ShadowAtlas_Caching_LOD) | `main` | Directional/Point/Spot Shadow, Atlas, Caching, LOD/Degradation |
| **W09** | 10.22~10.28 | [`Week09_Custom3DCollision_FMOD_Portal`](./Week09_Custom3DCollision_FMOD_Portal) | `develop` | 자체 3D Collision & Sweep 엔진, `IAudioBackend`/FMOD, Portal 물리 |
| **W10** | 10.30~11.04 | [`Week10_ForwardProjectedDecal`](./Week10_ForwardProjectedDecal) | `main` | Forward Projected Decal(조명·그림자·노멀 연동), Fake SpotLight |
| **W11** | 11.05~11.11 | [`Week11_AnimGraph_BlendSpace_Editor`](./Week11_AnimGraph_BlendSpace_Editor) | `main` | AnimGraph, 1D BlendSpace, AnimNotify/State(Lua), 전용 에디터 2종 |
| **W12** | 11.17~11.18 | [`Week12_CascadeParticle_Ribbon_LOD`](./Week12_CascadeParticle_Ribbon_LOD) | `development` + `hwang` | Cascade 파티클 코어, Ribbon Emitter, Particle LOD |
| **W13** | 11.23~11.24 | [`Week13_PhysX_Vehicle_FBXImport`](./Week13_PhysX_Vehicle_FBXImport) | `feat/Vehicle` | PhysX Vehicle 통합, Rigid→Skeletal 차량 FBX 임포트 |
