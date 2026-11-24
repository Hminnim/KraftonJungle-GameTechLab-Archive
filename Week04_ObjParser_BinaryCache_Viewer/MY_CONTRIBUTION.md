# [W04] 고성능 OBJ 파서, 해시 검증 바이너리 메시 캐싱 & 독립 ObjViewer

- **기간**: 2026-09-17 ~ 09-22
- **원본 레포지토리 / 브랜치**: `PortalCube/JungleTechLab_Week4_Team7` (`main`)

## 본인 구현 내역

- `FObjParser` Raw 파싱 → `FVertexData` 변환, 중복 정점 제거(Vertex Deduplication), 파싱 속도 최적화, 섹션별 머티리얼 매핑
- OBJ 바이너리(`.bin`) 베이킹 및 해시 무결성 검증 캐싱 구현
- `FAxisAlignedBoundingBox` 계산 최적화, `premake5.lua` `_OBJVIEWER` 타깃 분리 및 독립 `FObjViewerApplication`·에디터 OBJ 임포트 구현
