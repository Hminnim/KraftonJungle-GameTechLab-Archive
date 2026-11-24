# [W05] GPU Hi-Z Occlusion Culling, Mesh LOD & `FNamePoolShard`

- **기간**: 2026-09-25 ~ 09-30
- **원본 레포지토리 / 브랜치**: `ksang9609/podoEngine-w5` (`main (+ feature/FName, feature/Occlusion)`)

## 본인 구현 내역

- Compute Shader(`HzBuildCS.hlsl`, `HzCullCS.hlsl`) 기반 GPU Hi-Z Depth Pyramid 생성 및 오클루전 컬링, 멀티 뷰포트 대응
- `Meshoptimizer` 통합 StaticMesh LOD 생성/전환 및 `FindImmutableBufferOrAdd` GPU 리소스 조회 병목 분석·개선
- `FName` API 리팩토링 및 샤드 기반 `FNamePoolShard` 구현 (`_extra_branch_feature_FName/`에 보존)
