#include "EnginePCH.h"
#include "ParticleLODLevel.h"
#include "ParticleModule.h"
#include "ParticleEmitterInstances.h"

void UParticleLODLevel::UpdateModuleLists()
{
	UpdateModules.Empty();
	SpawnModules.Empty();
	SpawningModules.Empty();

	UParticleModule* Module;
	int32 TypeDataModuleIndex = -1;

	for(int32 i = 0; i < Modules.Num(); ++i)
	{
		Module = Modules[i];
		if(!Module)
		{
			continue;
		}

		if(Module->bSpawnModule)
		{
			SpawnModules.Add(Module);
		}
		if(Module->bUpdateModule || Module->bFinalUpdateModule)
		{
			UpdateModules.Add(Module);
		}

		if(Module->IsA(UParticleModuleTypeDataBase::StaticClass()))
		{
			TypeDataModule = CastChecked<UParticleModuleTypeDataBase>(Module);
			if(!Module->bSpawnModule && !Module->bUpdateModule)
			{
				TypeDataModuleIndex = i;
			}
		}
		else if(Module->IsA(UParticleModuleSpawn::StaticClass()))
		{
			UParticleModuleSpawn* SpawnBase = CastChecked<UParticleModuleSpawn>(Module);
			SpawningModules.Add(SpawnBase);
		}	
	}

	if(TypeDataModuleIndex != -1)
	{
		Modules.RemoveAt(TypeDataModuleIndex);
	}

	if(TypeDataModule)
	{

	}
}

bool UParticleLODLevel::GenerateFromLODLevel(UParticleLODLevel* SourceLODLevel, float Percentage, bool bGenerateModuleData)
{
	// Already modules in place
	if(Modules.Num() > 0)
	{
		return false;
	}

	bool bResult = true;

	

	return false;
}

int32 UParticleLODLevel::CalculateMaxActiveParticleCount()
{
	return int32();
}

void UParticleLODLevel::SetLevelIndex(int32 InLevelIndex)
{
	Level = InLevelIndex;
}

void UParticleLODLevel::ConvertToSpawnModule()
{
}

int32 UParticleLODLevel::GetModuleIndex(UParticleModule* InModule)
{
	if (!InModule)
	{
		return INDEX_NONE;
	}

	return Modules.Find(InModule);
}

UParticleModule* UParticleLODLevel::GetModuleAtIndex(int32 InIndex)
{
	if (InIndex < 0 || InIndex >= Modules.Num())
	{
		return nullptr;
	}

	return Modules[InIndex];
}

bool UParticleLODLevel::IsModuleEditable(UParticleModule* InModule)
{
	return false;
}

void UParticleLODLevel::CompileModules(FParticleEmitterBuildInfo& EmitterBuildInfo)
{
	EmitterBuildInfo.RequiredModule = RequiredModule;
	EmitterBuildInfo.SpawnModule = SpawnModule;

	if (RequiredModule)
	{
		RequiredModule->CompileModule(EmitterBuildInfo);
	}
	if (SpawnModule && SpawnModule->bEnabled)
	{
		SpawnModule->CompileModule(EmitterBuildInfo);
	}

	const int32 ModuleCount = Modules.Num();
	for(int32 ModuleIndex = 0; ModuleIndex < ModuleCount; ++ModuleIndex)
	{
		UParticleModule* Module = Modules[ModuleIndex];
		if(Module && Module->bEnabled)
		{
			Module->CompileModule(EmitterBuildInfo);
		}
	}

	//EmitterBuildInfo.EstimatedMaxActiveParticleCount = CalculateMaxActiveParticleCount();
}
