# [W12] Cascade 스타일 파티클 시스템 (코어, Ribbon Emitter, Particle LOD)

- **기간**: 2026-11-17 ~ 11-18
- **원본 레포지토리 / 브랜치**: `HyeokJo/week12_team9` (`development (+ hwang)`)

## 본인 구현 내역

- 언리얼 Cascade 스타일 코어(`UDistribution`, `UParticleModule`, `UParticleEmitter`, `UParticleLODLevel`, `FParticleEmitterInstance`) 구현 (`_extra_branch_hwang_cascade_core/`에 보존)
- `ParticleModuleTypeDataRibbon`(1,540+ LOC) 동적 리본/트레일 지오메트리 생성 구현
- 거리/성능 기반 Particle System LOD 전환 및 `ParticleSystemEditor` UI 통합·쇼케이스 씬 제작
