#include "EnginePCH.h"
#include "ParticleEmitter.h"
#include "ParticleEmitterInstances.h"
#include "ParticleEmitterInstanceOwner.h"
#include "ParticleLODLevel.h"
#include "ParticleModule.h"
#include "Core/HAL/UnrealMemory.h"

FParticleEmitterInstance* UParticleEmitter::CreateInstance(IParticleEmitterInstanceOwner& InComponent)
{
	if (ParticleSize <= 0)
	{
		Build();
	}

	UParticleLODLevel* HighLODLevel = GetLODLevel(0);
	FParticleEmitterInstance* Instance = nullptr;

	if (HighLODLevel && HighLODLevel->TypeDataModule)
	{
		Instance = HighLODLevel->TypeDataModule->CreateInstance(this, InComponent);
	}

	if (!Instance)
	{
		Instance = new FParticleSpriteEmitterInstance(InComponent);
	}

	Instance->InitParameters(this);
	return Instance;
}

UParticleLODLevel* UParticleEmitter::GetCurrentLODLevel(FParticleEmitterInstance* Instance)
{
	if (Instance)
	{
		UParticleLODLevel* LOD = GetLODLevel(Instance->CurrentLODLevelIndex);
		if (LOD)
		{
			return LOD;
		}
	}

	return GetLODLevel(0);
}

UParticleLODLevel* UParticleEmitter::GetLODLevel(int32 LODLevel)
{
	if(LODLevel < 0 || LODLevel >= LODLevels.Num())
	{
		return nullptr;
	}

	return LODLevels[LODLevel];
}

void UParticleEmitter::Build()
{
	const int32 LODCount = LODLevels.Num();
	if (LODCount > 0)
	{
		for (int32 LODIdx = 0; LODIdx < LODCount; ++LODIdx)
		{
			if (UParticleLODLevel* LOD = LODLevels[LODIdx])
			{
				LOD->SetLevelIndex(LODIdx);
				LOD->UpdateModuleLists();
			}
		}

		UParticleLODLevel* HighLODLevel = LODLevels[0];
		if (HighLODLevel && HighLODLevel->TypeDataModule != nullptr)
		{
			if (HighLODLevel->TypeDataModule->RequiresBuild())
			{
				FParticleEmitterBuildInfo EmitterBuildInfo;
				HighLODLevel->CompileModules(EmitterBuildInfo);
				HighLODLevel->TypeDataModule->Build(EmitterBuildInfo);
			}

			HighLODLevel->TypeDataModule->CacheModuleInfo(this);
		}

		CacheEmitterModuleInfo();
	}
}

void UParticleEmitter::CacheEmitterModuleInfo()
{
	ModuleOffsetMap.Empty();
	ModuleInstanceOffsetMap.Empty();
	MeshMaterials.Empty();
	ParticleSize = sizeof(FBaseParticle);
	ReqInstanceBytes = 0;
	TypeDataOffset = 0;
	TypeDataInstanceOffset = -1;

	UParticleLODLevel* HighLODLevel = GetLODLevel(0);
	if (!HighLODLevel)
	{
		ParticleSize = static_cast<int32>(FMemory::QuantizeSize(static_cast<uint64>(ParticleSize), 16));
		return;
	}

	UParticleModuleTypeDataBase* HighTypeData = HighLODLevel->TypeDataModule;

	if (HighTypeData)
	{
		int32 ReqBytes = HighTypeData->RequiredBytes(static_cast<UParticleModuleTypeDataBase*>(nullptr));
		if (ReqBytes)
		{
			TypeDataOffset = ParticleSize;
			ParticleSize += ReqBytes;
		}

		int32 TempInstanceBytes = HighTypeData->RequiredBytesPerInstance();
		if (TempInstanceBytes)
		{
			TypeDataInstanceOffset = ReqInstanceBytes;
			ReqInstanceBytes += TempInstanceBytes;
		}
	}

	for (int32 ModuleIdx = 0; ModuleIdx < HighLODLevel->Modules.Num(); ++ModuleIdx)
	{
		UParticleModule* ParticleModule = HighLODLevel->Modules[ModuleIdx];
		if (!ParticleModule)
		{
			continue;
		}

		if (ParticleModule->IsA(UParticleModuleTypeDataBase::StaticClass()) == false)
		{
			int32 ReqBytes = ParticleModule->RequiredBytes(HighTypeData);
			if (ReqBytes)
			{
				ModuleOffsetMap.Add(ParticleModule, ParticleSize);
				ParticleSize += ReqBytes;
			}

			int32 TempInstanceBytes = ParticleModule->RequiredBytesPerInstance();
			if (TempInstanceBytes > 0)
			{
				ModuleInstanceOffsetMap.Add(ParticleModule, ReqInstanceBytes);

				for (int32 LODIdx = 1; LODIdx < LODLevels.Num(); LODIdx++)
				{
					UParticleLODLevel* CurLODLevel = LODLevels[LODIdx];
					if (CurLODLevel && ModuleIdx < CurLODLevel->Modules.Num() && CurLODLevel->Modules[ModuleIdx])
					{
						ModuleInstanceOffsetMap.Add(CurLODLevel->Modules[ModuleIdx], ReqInstanceBytes);
					}
				}
				ReqInstanceBytes += TempInstanceBytes;
			}
		}
	}

	ParticleSize = static_cast<int32>(FMemory::QuantizeSize(static_cast<uint64>(ParticleSize), 16));
}
