# [W08] 통합 실시간 그림자 시스템 (Shadow Atlas, Caching, LOD/Degradation)

- **기간**: 2026-10-16 ~ 10-21
- **원본 레포지토리 / 브랜치**: `VirtualExplorer7613/Techlab4_Week08_Team6` (`main`)

## 본인 구현 내역

- Directional/Point(`TextureCube` 프로젝션 보정)/Spot 그림자 맵 및 Caster/Receiver 분리
- Point/Spot Shadow Map Atlas 패킹 및 `AtlasDepthPreview.hlsl` 기반 에디터 깊이 맵 프리뷰 UI
- 정적 광원 대상 **Shadow Map Caching**(Dirty Flag 부분 갱신), **Shadow LOD / Degradation**, 동적 해상도 변경 구현
