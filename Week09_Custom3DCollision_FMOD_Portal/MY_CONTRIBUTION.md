# [W09] 자체 3D Collision & Sweep 엔진, `IAudioBackend`/FMOD & Portal 게임플레이

- **기간**: 2026-10-22 ~ 10-28
- **원본 레포지토리 / 브랜치**: `808Jade/week9_team7` (`develop`)

## 본인 구현 내역

- `CollisionDetector`(1,100+ LOC), `UShapeComponent`(Box/Sphere/Capsule), `ECollisionResponse`, Overlap 쿼리, **Box-Box/Capsule Shape Sweep**, `PlaneConstraint` 구현
- `IAudioBackend` 인터페이스 추상화 → `FMODCoreBackend` / `NullAudioBackend`, `AudioDevice`, `UAudioComponent` 구현
- `APortalActor` 텔레포트·바닥 충돌 마진·낙하 버그·프레임 스파이크 해결, `APortalCubeActor` Hold/Unhold/사출 물리 구현
