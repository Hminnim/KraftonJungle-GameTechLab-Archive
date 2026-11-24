#pragma once

#include "UObject/Object.h"
#include "Core/NameTypes.h"
#include "Containers/Map.h"
#include "ParticleHelper.h"
#include "ParticleEmitter.generated.h"

class IParticleEmitterInstanceOwner;
class UParticleLODLevel;
class UParticleModule;
class UParticleSystemComponent;
struct FParticleEmitterInstance;

UCLASS()
class UParticleEmitter : public UObject
{
	GENERATED_BODY()

public:
	FName EmitterName;
	int32 SubUVDataOffset = 0;

	TArray<UParticleLODLevel*> LODLevels;
	
	TMap<UParticleModule*, uint32> ModuleOffsetMap;
	TMap<UParticleModule*, uint32> ModuleInstanceOffsetMap;

	TArray<class UMaterialInterface*> MeshMaterials;

	UPROPERTY()
	int32 PeakActiveParticles = 0;

	int32 ParticleSize = 0;
	int32 ReqInstanceBytes = 0;
	int32 TypeDataOffset;
	int32 TypeDataInstanceOffset;

	FParticleEmitterInstance* CreateInstance(IParticleEmitterInstanceOwner& InComponent);

	UParticleLODLevel* GetCurrentLODLevel(FParticleEmitterInstance* Instance);
	UParticleLODLevel* GetLODLevel(int32 LODLevel);

	void Build();
	void CacheEmitterModuleInfo();
};