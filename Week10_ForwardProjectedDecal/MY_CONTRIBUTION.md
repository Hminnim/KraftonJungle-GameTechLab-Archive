# [W10] Forward Projected Decal 시스템 & Fake SpotLight

- **기간**: 2026-10-30 ~ 11-04
- **원본 레포지토리 / 브랜치**: `dev-ghlee/Techlab4_Week10_Team4` (`main`)

## 본인 구현 내역

- `ForwardDecal.hlsl`, `UDecalComponent`, `FDecalSceneProxy`, `ADecalActor` 기반 Forward 프로젝션 데칼 패스 구현
- 데칼 조명·그림자 연동, 노멀 텍스처 슬롯 및 노멀 블렌딩, Fade, ShowFlag, `StatOverlay`, 직렬화 구현
- 데칼 셰이더 분기를 활용한 경량 `AFakeSpotLightActor` 구현
