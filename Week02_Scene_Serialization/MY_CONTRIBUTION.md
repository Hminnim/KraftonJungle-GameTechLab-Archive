# [W02] 씬 직렬화(Save/Load/New) 및 UUID 관리

- **기간**: 2026-09-09
- **원본 레포지토리 / 브랜치**: `ShinYongJun02/WEEK2TEAM3` (`main`)

## 본인 구현 내역

- `USceneManager`, `FJsonParser`, `FUUID`, `UEngineStatics` 기반 Scene New / Save / Load 구현
- 씬 로드 시 기존 Primitive `UUID` 및 글로벌 `NextUUID` 복원, `ClearScene` Factory 패턴 교체
- FPS 제한 로직 수정 및 마우스 이동 DeltaTime 의존성 제거
