# [W07] UberLit 셰이더 파이프라인 & LightSceneProxy 아키텍처

- **기간**: 2026-10-10 ~ 10-14
- **원본 레포지토리 / 브랜치**: `Kimyoungkyun-jg/week7_team8` (`hwang`)

## 본인 구현 내역

- `LightComponentBase`/`LightComponent` 및 Ambient/Directional/Point/Spot 4종 라이트 컴포넌트·`LightSceneProxy` 분리 설계
- `UberLit.hlsl`(Gouraud/Lambert/Phong 셰이딩 모델) 및 TBN 행렬 기반 노멀 매핑, TBN threshold 수치 안정성 수정
