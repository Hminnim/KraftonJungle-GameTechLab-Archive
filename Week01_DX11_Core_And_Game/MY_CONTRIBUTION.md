# [W01] DirectX 11 기초 엔진 모듈 분리 및 턴제 알까기 게임

- **기간**: 2026-08-31 ~ 09-03
- **원본 레포지토리 / 브랜치**: `Hminnim/GameTechLabWeek1` (`main`)

## 본인 구현 내역

- 단일 `main.cpp`에서 `URenderer`, `UWindow`, `GameTimer`, `UInputManager`, `CollisionManager`(Singleton), `UGameSetting`, `UPrimitive`/`UBall` 모듈 분리
- `ShaderW0.hlsl` 픽셀 좌표계 변환, 해상도 비율 연동 스케일링, 중력 계수 보정
- `UGameManager`(턴 상태 머신 `ETurnState`, 승패/무승부/재시작 판정), `UPlayerController`, `UDraftScene`(후공 선픽 스킬 드래프트 및 인게임 연동)
