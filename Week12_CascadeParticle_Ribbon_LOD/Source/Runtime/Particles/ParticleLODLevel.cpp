#include "EnginePCH.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/ParticleModule.h"

void UParticleLODLevel::UpdateModuleLists()
{
	SpawnModules.Empty();
	UpdateModules.Empty();
	for (UParticleModule* Module : Modules)
	{
		if (!Module)
		{
			continue;
		}
		if (Module->bSpawnModule)
		{
			SpawnModules.Add(Module);
		}
		if (Module->bUpdateModule)
		{
			UpdateModules.Add(Module);
		}
	}
}

UParticleModule* UParticleLODLevel::AddModule(UClass* ModuleClass)
{
	// 추상 클래스는 Constructor가 없다 (AnimNotify와 같은 규칙)
	if (!ModuleClass || !ModuleClass->IsChildOf(UParticleModule::StaticClass()) || !ModuleClass->Constructor)
	{
		HTR_LOG(Warning, "[Particle] Cannot add module: '{}' is not a concrete UParticleModule class",
			ModuleClass ? ModuleClass->Name : FString("null"));
		return nullptr;
	}

	// 슬롯이 하나뿐인 모듈은 객체를 만들기 전에 거절한다
	const bool bIsRequired = ModuleClass->IsChildOf(UParticleModuleRequired::StaticClass());
	const bool bIsTypeData = ModuleClass->IsChildOf(UParticleModuleTypeDataBase::StaticClass());
	const bool bIsSpawn = ModuleClass->IsChildOf(UParticleModuleSpawn::StaticClass());
	if ((bIsRequired && RequiredModule) || (bIsTypeData && TypeDataModule) || (bIsSpawn && SpawnModule))
	{
		HTR_LOG(Warning, "[Particle] Cannot add '{}': the slot is already used", ModuleClass->Name);
		return nullptr;
	}

	// Outer를 LODLevel로 두어 System → Emitter → LODLevel → Module 소유 체인을 만든다
	UParticleModule* Module = NewObject<UParticleModule>(this, ModuleClass);
	if (!Module)
	{
		return nullptr;
	}

	if (bIsRequired)
	{
		RequiredModule = static_cast<UParticleModuleRequired*>(Module);
	}
	else if (bIsSpawn)
	{
		SpawnModule = static_cast<UParticleModuleSpawn*>(Module);
	}
	else if (bIsTypeData)
	{
		TypeDataModule = static_cast<UParticleModuleTypeDataBase*>(Module);
	}
	else
	{
		Modules.Add(Module);
	}
	return Module;
}

bool UParticleLODLevel::RemoveModule(UParticleModule* Module)
{
	if (!Module)
	{
		return false;
	}
	if (Module == RequiredModule)
	{
		HTR_LOG(Warning, "[Particle] Cannot remove the Required module");
		return false;
	}

	if (Module == SpawnModule)
	{
		SpawnModule = nullptr;
	}
	else if (Module == TypeDataModule)
	{
		TypeDataModule = nullptr;
	}
	else
	{
		const int32 Index = Modules.Find(Module);
		if (Index == INDEX_NONE)
		{
			HTR_LOG(Warning, "[Particle] Cannot remove '{}': it does not belong to this LOD level", Module->GetClass()->Name);
			return false;
		}
		Modules.RemoveAt(Index);
	}
	delete Module;
	// 지운 모듈을 가리키지 않게 바로 다시 만든다 (CacheEmitterModuleInfo도 다시 부르지만 그 전까지 안전하게)
	UpdateModuleLists();
	return true;
}

void UParticleLODLevel::DeleteOwnedObjects()
{
	for (UParticleModule* Module : Modules)
	{
		delete Module;
	}
	Modules.Empty();
	delete RequiredModule;
	delete SpawnModule;
	delete TypeDataModule;
	RequiredModule = nullptr;
	SpawnModule = nullptr;
	TypeDataModule = nullptr;
	SpawnModules.Empty();
	UpdateModules.Empty();
	// SpawnModules, UpdateModules는 소유하지 않는 참조라 지우지 않는다
}

namespace
{
	void CopyObjectSettings(UObject* Source, UObject* Dest)
	{
		json Data = json::object();
		Source->Serialize(Data, false);
		Dest->Serialize(Data, true);
	}

	void ScaleFloatDistribution(FFloatDistribution& Dist, float Scale)
	{
		Dist.Constant *= Scale;
		Dist.Min *= Scale;
		Dist.Max *= Scale;
		for (auto& Key : Dist.Keys)
		{
			Key.Value *= Scale;
		}
	}
}

void UParticleLODLevel::CopyFrom(const UParticleLODLevel& Source, float SpawnRateScale)
{
	if (&Source == this)
	{
		return;
	}

	const int32 PreservedLevel = Level;
	DeleteOwnedObjects();
	CopyObjectSettings(const_cast<UParticleLODLevel*>(&Source), this);
	Level = PreservedLevel;

	const auto CloneModule = [this](UParticleModule* SourceModule) -> UParticleModule*
	{
		if (!SourceModule)
		{
			return nullptr;
		}
		UParticleModule* Cloned = AddModule(SourceModule->GetClass());
		if (Cloned)
		{
			CopyObjectSettings(SourceModule, Cloned);
		}
		return Cloned;
	};

	CloneModule(Source.RequiredModule);
	CloneModule(Source.SpawnModule);
	CloneModule(Source.TypeDataModule);
	for (UParticleModule* SourceModule : Source.Modules)
	{
		CloneModule(SourceModule);
	}

	if (SpawnModule && SpawnRateScale != 1.0f)
	{
		ScaleFloatDistribution(SpawnModule->RateScale, SpawnRateScale);
	}

	UpdateModuleLists();
}
