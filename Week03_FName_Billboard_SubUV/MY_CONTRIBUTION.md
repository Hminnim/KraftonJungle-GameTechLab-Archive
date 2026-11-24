# [W03] 언리얼 스타일 `FName` 시스템 & Billboard / SubUV 렌더링

- **기간**: 2026-09-11 ~ 09-16
- **원본 레포지토리 / 브랜치**: `Hminnim/Macaw` (`Develop`)

## 본인 구현 내역

- 자체 `FVector` / `FMath` 교체
- CityHash 기반 `FName` 풀 구현: `ComparisonIndex`/`DisplayIndex` 분리 비교, 해시 버킷 확장, 중복 문자열 제거, `_Number` 파싱 및 Actor 자동 넘버링, `FArchive` 직렬화, Outliner 연동
- `Billboard.hlsl`, `FBillboardRenderer`, `UBillboardSubsystem`, `UBillboardComponent`, `USubUVComponent` 구현 및 렌더 파이프라인 통합
