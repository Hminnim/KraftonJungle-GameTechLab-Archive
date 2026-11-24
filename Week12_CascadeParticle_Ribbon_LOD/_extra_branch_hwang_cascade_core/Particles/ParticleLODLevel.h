#pragma once

#include "UObject/Object.h"
#include "ParticleLODLevel.generated.h"

class UParticleModule;
class UParticleModuleRequired;
class UParticleModuleTypeDataBase;
class UParticleModuleSpawn;

UCLASS()
class UParticleLODLevel : public UObject
{
	GENERATED_BODY()

public:
	int32 Level = 0;
	uint32 bEnabled = true;

	UParticleModuleSpawn* SpawnModule = nullptr;
	UParticleModuleRequired* RequiredModule = nullptr;	
	UParticleModuleTypeDataBase* TypeDataModule = nullptr;

	TArray<UParticleModule*> Modules;
	TArray<UParticleModule*> UpdateModules;
	TArray<UParticleModule*> SpawnModules;
	TArray<UParticleModuleSpawn*> SpawningModules;

	virtual void UpdateModuleLists();
	virtual bool GenerateFromLODLevel(UParticleLODLevel* SourceLODLevel, float Percentage, bool bGenerateModuleData = true);
	virtual int32 CalculateMaxActiveParticleCount();
	virtual void SetLevelIndex(int32 InLevelIndex);

	void ConvertToSpawnModule();

	int32 GetModuleIndex(UParticleModule* InModule);
	UParticleModule* GetModuleAtIndex(int32 InIndex);
	bool IsModuleEditable(UParticleModule* InModule);

	void CompileModules(struct FParticleEmitterBuildInfo& EmitterBuildInfo);
};