#pragma once

#include "UObject/Object.h"
#include "ParticleEmitter.generated.h"

class UParticleLODLevel;
class UParticleModule;

// 파티클을 만들어 내는 단위. 한 에셋에 여러 개 둘 수 있다 (UE: Classes/Particles/ParticleEmitter.h)
UCLASS()
class UParticleEmitter : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere)
	FString EmitterName = "Emitter";

	// 소유 관계라 UPROPERTY가 아니다 (UParticleLODLevel의 설명 참고). UE: UPROPERTY(instanced)
	TArray<UParticleLODLevel*> LODLevels;

	// 소유한 LOD 레벨과 그 아래 모듈을 모두 지운다 (UParticleSystem::DeleteOwnedObjects 참고)
	void DeleteOwnedObjects();

	UParticleLODLevel* AddLODLevel();

	// InsertIndex 위치에 새 LOD 레벨을 끼워 넣고, SourceLOD(없으면 인접 LOD)로부터 모듈 구성을 복사한다.
	UParticleLODLevel* InsertLODLevel(int32 InsertIndex, const UParticleLODLevel* SourceLOD = nullptr, float SpawnRateScale = 1.0f);

	// 지정한 LOD 레벨을 지우고 뒤쪽 LOD 번호를 당긴다. 최소 1개의 LOD는 남겨야 하므로 마지막 하나는 지우지 않는다.
	bool RemoveLODLevel(int32 LODIndex);

	// TargetLODIndex 레벨의 모듈을 SourceLODIndex 레벨로부터 다시 복사하고 생성량을 SpawnRateScale만큼 조절한다.
	bool RegenerateLODLevel(int32 TargetLODIndex, int32 SourceLODIndex, float SpawnRateScale = 0.5f);

	int32 GetNumLODLevels() const { return LODLevels.Num(); }
	UParticleLODLevel* GetLODLevel(int32 Index) const { return LODLevels.IsValidIndex(Index) ? LODLevels[Index] : nullptr; }

	// ---- 모듈 구성에서 계산하는 메모리 레이아웃 캐시 ----
	// 모듈 목록으로부터 언제든 다시 만들 수 있으므로 저장하지 않는다. 파티클 메모리 구성은 이 값을 읽기만 한다.

	// 파티클 한 개의 크기 = FBaseParticle + 모듈 Payload 합계
	uint32 ParticleSize = 0;
	// ParticleData에서 파티클 사이 간격. ParticleSize를 16의 배수로 올린 값이다.
	// UE와 다름: UE의 CPU 스프라이트는 ParticleSize를 그대로 쓰고, 정렬 올림은 하지 않는다.
	uint32 ParticleStride = 0;
	// 이미터 인스턴스마다 한 번 필요한 InstanceData 크기 합계
	uint32 ReqInstanceBytes = 0;
	// TypeData 모듈의 Payload / InstanceData 오프셋. 요구하지 않으면 INDEX_NONE
	int32 TypeDataOffset = INDEX_NONE;
	int32 TypeDataInstanceOffset = INDEX_NONE;
	// 모듈 → 파티클 안에서의 Payload 시작 오프셋 (Payload가 없는 모듈은 등록하지 않는다)
	TMap<UParticleModule*, uint32> ModuleOffsetMap;
	// 모듈 → InstanceData 안에서의 시작 오프셋
	TMap<UParticleModule*, uint32> ModuleInstanceOffsetMap;

	// 모듈이 요구하는 바이트를 모아 위 캐시를 다시 계산한다 (UE: CacheEmitterModuleInfo)
	// 모듈을 추가·삭제·이동하거나 모듈의 요구 크기가 바뀐 뒤에는 반드시 다시 호출하고, 이 이미터로 만든 인스턴스를 재생성해야 한다.
	void CacheEmitterModuleInfo();

	// 모듈의 Payload 시작 오프셋. Payload를 요구하지 않는 모듈이면 INDEX_NONE
	int32 GetModuleDataOffset(UParticleModule* Module) const;
	// 모듈의 InstanceData 시작 오프셋. 요구하지 않으면 INDEX_NONE
	int32 GetModuleInstanceDataOffset(UParticleModule* Module) const;
};
