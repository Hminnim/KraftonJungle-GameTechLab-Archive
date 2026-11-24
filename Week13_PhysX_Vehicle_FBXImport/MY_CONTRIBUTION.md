# [W13] PhysX 차량 물리 시스템 & Rigid-to-Skeletal FBX 임포트

- **기간**: 2026-11-23 ~ 11-24
- **원본 레포지토리 / 브랜치**: `grukall/Week13_Team7` (`feat/Vehicle`)

## 본인 구현 내역

- `FPhysXVehicleManager` 구현 및 자체 엔진 `PhysScene`·`BodyInstance` 연동
- `UWheeledVehicleMovementComponent`(714 LOC), `AWheeledVehicle`, `PlayerController` 입력 및 `SkeletalMeshComponent` 휠 본 동기화
- `FbxImporter` 스케일링, 리지드 메시 차량의 스켈레탈 계층 자동 변환, 휠 자동 감지·직렬화, 차체 충돌 개선
