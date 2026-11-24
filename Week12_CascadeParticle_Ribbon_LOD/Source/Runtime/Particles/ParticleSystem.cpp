#include "EnginePCH.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/ParticleModule.h"
#include <algorithm>

namespace
{
	constexpr float DefaultLODDistanceStep = 10.0f;
	constexpr int32 MaxSystemLODLevels = 8;
}

void UParticleSystem::Serialize(json& Handle, bool bIsLoading)
{
	Super::Serialize(Handle, bIsLoading);
	if (!bIsLoading)
	{
		json Distances = json::array();
		for (float Dist : LODDistances)
		{
			Distances.push_back(Dist);
		}
		Handle["LODDistances"] = Distances;
		return;
	}

	if (Handle.contains("LODDistances") && Handle["LODDistances"].is_array())
	{
		LODDistances.Empty();
		for (const json& Entry : Handle["LODDistances"])
		{
			if (Entry.is_number())
			{
				LODDistances.Add(std::max(0.0f, Entry.get<float>()));
			}
		}
	}
}

UParticleEmitter* UParticleSystem::AddEmitter()
{
	// Outer를 에셋으로 두어 소유 체인의 시작점으로 삼는다
	UParticleEmitter* Emitter = NewObject<UParticleEmitter>(this);
	if (Emitter)
	{
		Emitters.Add(Emitter);
	}
	return Emitter;
}

bool UParticleSystem::RemoveEmitter(UParticleEmitter* Emitter)
{
	const int32 Index = Emitter ? Emitters.Find(Emitter) : INDEX_NONE;
	if (Index == INDEX_NONE)
	{
		return false;
	}
	Emitters.RemoveAt(Index);
	Emitter->DeleteOwnedObjects();
	delete Emitter;
	return true;
}

namespace
{
	// UPROPERTY와 모듈이 직접 쓰는 값(분포 등)을 Serialize로 옮긴다. 저장 파일과 같은 경로라 저장되는 값은 모두 복사된다.
	void CopySettings(UObject* Source, UObject* Dest)
	{
		json Data = json::object();
		Source->Serialize(Data, false);
		Dest->Serialize(Data, true);
	}
}

UParticleEmitter* UParticleSystem::DuplicateEmitter(UParticleEmitter* Source)
{
	const int32 SourceIndex = Source ? Emitters.Find(Source) : INDEX_NONE;
	if (SourceIndex == INDEX_NONE)
	{
		return nullptr;
	}

	UParticleEmitter* Copy = NewObject<UParticleEmitter>(this);
	if (!Copy)
	{
		return nullptr;
	}
	CopySettings(Source, Copy);

	for (UParticleLODLevel* SourceLOD : Source->LODLevels)
	{
		if (!SourceLOD)
		{
			continue;
		}
		UParticleLODLevel* LOD = Copy->AddLODLevel();
		const int32 Level = LOD->Level;
		CopySettings(SourceLOD, LOD);
		LOD->Level = Level;

		const auto CopyModule = [LOD](UParticleModule* SourceModule)
		{
			if (!SourceModule)
			{
				return;
			}
			if (UParticleModule* Module = LOD->AddModule(SourceModule->GetClass()))
			{
				CopySettings(SourceModule, Module);
			}
		};
		CopyModule(SourceLOD->RequiredModule);
		CopyModule(SourceLOD->SpawnModule);
		CopyModule(SourceLOD->TypeDataModule);
		for (UParticleModule* Module : SourceLOD->Modules)
		{
			CopyModule(Module);
		}
	}

	Copy->CacheEmitterModuleInfo();
	Emitters.Insert(Copy, static_cast<uint32>(SourceIndex + 1));
	SyncLODDistances();
	return Copy;
}

int32 UParticleSystem::GetNumLODLevels() const
{
	int32 Count = 0;
	for (const UParticleEmitter* Emitter : Emitters)
	{
		if (Emitter)
		{
			Count = std::max(Count, Emitter->GetNumLODLevels());
		}
	}
	return Count;
}

float UParticleSystem::GetLODDistance(int32 LODIndex) const
{
	return LODDistances.IsValidIndex(LODIndex) ? LODDistances[LODIndex] : 0.0f;
}

void UParticleSystem::SetLODDistance(int32 LODIndex, float Distance)
{
	SyncLODDistances();
	if (!LODDistances.IsValidIndex(LODIndex) || LODIndex == 0)
	{
		return;
	}
	const float MinDist = LODDistances[LODIndex - 1];
	LODDistances[LODIndex] = std::max(MinDist, Distance);
	for (int32 i = LODIndex + 1; i < LODDistances.Num(); ++i)
	{
		if (LODDistances[i] < LODDistances[i - 1])
		{
			LODDistances[i] = LODDistances[i - 1];
		}
	}
}

void UParticleSystem::SyncLODDistances()
{
	const int32 NumLODs = GetNumLODLevels();
	if (NumLODs <= 0)
	{
		LODDistances.Empty();
		return;
	}
	while (LODDistances.Num() < NumLODs)
	{
		const int32 NewIdx = LODDistances.Num();
		const float NextDist = (NewIdx == 0) ? 0.0f : (LODDistances[NewIdx - 1] + DefaultLODDistanceStep);
		LODDistances.Add(NextDist);
	}
	if (LODDistances.Num() > NumLODs)
	{
		LODDistances.SetNum(NumLODs);
	}
	LODDistances[0] = 0.0f;
	for (int32 i = 1; i < LODDistances.Num(); ++i)
	{
		if (LODDistances[i] < LODDistances[i - 1])
		{
			LODDistances[i] = LODDistances[i - 1];
		}
	}
}

int32 UParticleSystem::GetLODLevelForDistance(float Distance) const
{
	const int32 NumLODs = GetNumLODLevels();
	if (NumLODs <= 1)
	{
		return 0;
	}
	const int32 CheckCount = std::min(NumLODs, LODDistances.Num());
	for (int32 LODIndex = CheckCount - 1; LODIndex >= 1; --LODIndex)
	{
		if (Distance >= LODDistances[LODIndex])
		{
			return LODIndex;
		}
	}
	return 0;
}

int32 UParticleSystem::AddLODLevel(int32 InsertIndex, float SpawnRateScale)
{
	const int32 CurrentCount = GetNumLODLevels();
	if (CurrentCount <= 0 || CurrentCount >= MaxSystemLODLevels)
	{
		return INDEX_NONE;
	}
	SyncLODDistances();
	const int32 TargetIndex = (InsertIndex == INDEX_NONE) ? CurrentCount : std::clamp(InsertIndex, 0, CurrentCount);

	for (UParticleEmitter* Emitter : Emitters)
	{
		if (Emitter)
		{
			Emitter->InsertLODLevel(TargetIndex, nullptr, SpawnRateScale);
		}
	}

	float NewDistance = 0.0f;
	if (TargetIndex == 0)
	{
		NewDistance = 0.0f;
		if (!LODDistances.IsEmpty() && LODDistances[0] <= 0.0f)
		{
			LODDistances[0] = (LODDistances.Num() > 1 && LODDistances[1] > 0.0f)
				? LODDistances[1] * 0.5f
				: DefaultLODDistanceStep;
		}
	}
	else if (TargetIndex >= LODDistances.Num())
	{
		NewDistance = LODDistances.Last() + DefaultLODDistanceStep;
	}
	else
	{
		NewDistance = (LODDistances[TargetIndex - 1] + LODDistances[TargetIndex]) * 0.5f;
	}
	LODDistances.Insert(NewDistance, static_cast<uint32>(TargetIndex));
	SyncLODDistances();
	return TargetIndex;
}

bool UParticleSystem::RemoveLODLevel(int32 LODIndex)
{
	const int32 CurrentCount = GetNumLODLevels();
	if (CurrentCount <= 1 || LODIndex < 0 || LODIndex >= CurrentCount)
	{
		return false;
	}
	SyncLODDistances();
	bool bRemovedAny = false;
	for (UParticleEmitter* Emitter : Emitters)
	{
		if (Emitter)
		{
			bRemovedAny |= Emitter->RemoveLODLevel(LODIndex);
		}
	}
	if (bRemovedAny && LODDistances.IsValidIndex(LODIndex))
	{
		LODDistances.RemoveAt(LODIndex);
	}
	SyncLODDistances();
	return bRemovedAny;
}

bool UParticleSystem::RegenerateLODLevel(int32 TargetLODIndex, int32 SourceLODIndex, float SpawnRateScale)
{
	bool bRegeneratedAny = false;
	for (UParticleEmitter* Emitter : Emitters)
	{
		if (Emitter)
		{
			bRegeneratedAny |= Emitter->RegenerateLODLevel(TargetLODIndex, SourceLODIndex, SpawnRateScale);
		}
	}
	return bRegeneratedAny;
}

void UParticleSystem::DeleteOwnedObjects()
{
	for (UParticleEmitter* Emitter : Emitters)
	{
		if (Emitter)
		{
			Emitter->DeleteOwnedObjects();
			delete Emitter;
		}
	}
	Emitters.Empty();
}

void DestroyParticleSystem(UParticleSystem* System)
{
	if (System)
	{
		System->DeleteOwnedObjects();
		delete System;
	}
}
