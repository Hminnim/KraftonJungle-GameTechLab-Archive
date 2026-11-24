#include "EnginePCH.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/ParticleModule.h"
#include "Particles/ParticleHelper.h"
#include <algorithm>

namespace
{
	// Payload와 InstanceData는 모듈마다 8의 배수로 올림해서 쌓는다.
	// FBaseParticle이 16의 배수 크기이므로 모든 모듈의 시작 위치가 8의 배수가 되어,
	// float, int, bool은 물론 double, uint64, 포인터 같은 8바이트 정렬 타입도 안전하게 읽을 수 있다.
	// 모듈이 요구한 크기가 8의 배수가 아니면 그만큼 낭비되지만 오류는 아니다.
	constexpr uint32 PayloadAlignment = 8;

	uint32 SanitizeRequiredBytes(uint32 Bytes)
	{
		return AlignUp(Bytes, PayloadAlignment);
	}
}

UParticleLODLevel* UParticleEmitter::AddLODLevel()
{
	UParticleLODLevel* LODLevel = NewObject<UParticleLODLevel>(this);
	if (!LODLevel)
	{
		return nullptr;
	}
	// 배열 위치가 곧 LOD 번호다 (0이 가장 상세한 레벨)
	LODLevel->Level = LODLevels.Num();
	LODLevels.Add(LODLevel);
	return LODLevel;
}

UParticleLODLevel* UParticleEmitter::InsertLODLevel(int32 InsertIndex, const UParticleLODLevel* SourceLOD, float SpawnRateScale)
{
	const int32 ClampedIndex = std::clamp(InsertIndex, 0, LODLevels.Num());
	if (!SourceLOD && !LODLevels.IsEmpty())
	{
		const int32 SourceIdx = std::clamp(ClampedIndex > 0 ? ClampedIndex - 1 : 0, 0, LODLevels.Num() - 1);
		SourceLOD = LODLevels[SourceIdx];
	}

	UParticleLODLevel* NewLOD = NewObject<UParticleLODLevel>(this);
	if (!NewLOD)
	{
		return nullptr;
	}
	NewLOD->Level = ClampedIndex;
	if (SourceLOD)
	{
		NewLOD->CopyFrom(*SourceLOD, SpawnRateScale);
	}

	LODLevels.Insert(NewLOD, static_cast<uint32>(ClampedIndex));
	for (int32 i = 0; i < LODLevels.Num(); ++i)
	{
		if (LODLevels[i])
		{
			LODLevels[i]->Level = i;
		}
	}
	CacheEmitterModuleInfo();
	return NewLOD;
}

bool UParticleEmitter::RemoveLODLevel(int32 LODIndex)
{
	if (LODLevels.Num() <= 1 || !LODLevels.IsValidIndex(LODIndex))
	{
		return false;
	}
	if (UParticleLODLevel* Removed = LODLevels[LODIndex])
	{
		Removed->DeleteOwnedObjects();
		delete Removed;
	}
	LODLevels.RemoveAt(LODIndex);
	for (int32 i = 0; i < LODLevels.Num(); ++i)
	{
		if (LODLevels[i])
		{
			LODLevels[i]->Level = i;
		}
	}
	CacheEmitterModuleInfo();
	return true;
}

bool UParticleEmitter::RegenerateLODLevel(int32 TargetLODIndex, int32 SourceLODIndex, float SpawnRateScale)
{
	if (TargetLODIndex == SourceLODIndex || !LODLevels.IsValidIndex(TargetLODIndex) || !LODLevels.IsValidIndex(SourceLODIndex))
	{
		return false;
	}
	UParticleLODLevel* TargetLOD = LODLevels[TargetLODIndex];
	const UParticleLODLevel* SourceLOD = LODLevels[SourceLODIndex];
	if (!TargetLOD || !SourceLOD)
	{
		return false;
	}
	TargetLOD->CopyFrom(*SourceLOD, SpawnRateScale);
	TargetLOD->Level = TargetLODIndex;
	CacheEmitterModuleInfo();
	return true;
}

void UParticleEmitter::CacheEmitterModuleInfo()
{
	ModuleOffsetMap.Empty();
	ModuleInstanceOffsetMap.Empty();
	ParticleSize = sizeof(FBaseParticle);
	ReqInstanceBytes = 0;
	TypeDataOffset = INDEX_NONE;
	TypeDataInstanceOffset = INDEX_NONE;

	for (int32 LODIndex = 0; LODIndex < LODLevels.Num(); ++LODIndex)
	{
		if (UParticleLODLevel* LOD = LODLevels[LODIndex])
		{
			LOD->Level = LODIndex;
			LOD->UpdateModuleLists();
			if (LOD->TypeDataModule)
			{
				// TypeData가 다른 모듈 설정(예: 빔의 Noise)에 따라 크기를 정할 수 있게 먼저 정보를 모은다 (UE와 같은 순서)
				LOD->TypeDataModule->CacheModuleInfo(this);
			}
		}
	}

	// LOD 전환 시 살아 있는 파티클 메모리를 그대로 이어 쓰므로 모든 LOD의 같은 위치 모듈에 동일한 오프셋을 등록한다 (UE와 같은 방식).
	if (UParticleLODLevel* LOD0 = GetLODLevel(0))
	{
		// TypeData의 Payload가 가장 앞에 온다 (UE와 같은 순서)
		uint32 MaxTypeDataBytes = 0;
		uint32 MaxTypeDataInstanceBytes = 0;
		for (UParticleLODLevel* LOD : LODLevels)
		{
			if (LOD && LOD->TypeDataModule)
			{
				MaxTypeDataBytes = std::max(MaxTypeDataBytes, SanitizeRequiredBytes(LOD->TypeDataModule->RequiredBytes(nullptr)));
				MaxTypeDataInstanceBytes = std::max(MaxTypeDataInstanceBytes, SanitizeRequiredBytes(LOD->TypeDataModule->RequiredBytesPerInstance()));
			}
		}
		if (MaxTypeDataBytes > 0)
		{
			TypeDataOffset = static_cast<int32>(ParticleSize);
			ParticleSize += MaxTypeDataBytes;
		}
		if (MaxTypeDataInstanceBytes > 0)
		{
			TypeDataInstanceOffset = static_cast<int32>(ReqInstanceBytes);
			ReqInstanceBytes += MaxTypeDataInstanceBytes;
		}

		// Modules 배열 순서대로 쌓는다. 순서가 바뀌면 오프셋도 바뀐다.
		// 꺼진 모듈(bEnabled == false)도 포함한다. 껐다 켜거나 LOD가 바뀌어도 파티클 메모리 레이아웃이 바뀌지 않게 하기 위해서다.
		int32 MaxModuleCount = LOD0->Modules.Num();
		for (const UParticleLODLevel* LOD : LODLevels)
		{
			if (LOD)
			{
				MaxModuleCount = std::max(MaxModuleCount, LOD->Modules.Num());
			}
		}

		for (int32 ModuleIdx = 0; ModuleIdx < MaxModuleCount; ++ModuleIdx)
		{
			uint32 MaxBytes = 0;
			uint32 MaxInstanceBytes = 0;
			for (UParticleLODLevel* LOD : LODLevels)
			{
				if (!LOD || !LOD->Modules.IsValidIndex(ModuleIdx))
				{
					continue;
				}
				UParticleModule* Module = LOD->Modules[ModuleIdx];
				if (!Module || Module->IsA(UParticleModuleTypeDataBase::StaticClass()))
				{
					continue;
				}
				MaxBytes = std::max(MaxBytes, SanitizeRequiredBytes(Module->RequiredBytes(LOD->TypeDataModule)));
				MaxInstanceBytes = std::max(MaxInstanceBytes, SanitizeRequiredBytes(Module->RequiredBytesPerInstance()));
			}

			if (MaxBytes > 0)
			{
				const uint32 Offset = ParticleSize;
				for (UParticleLODLevel* LOD : LODLevels)
				{
					if (LOD && LOD->Modules.IsValidIndex(ModuleIdx))
					{
						UParticleModule* Module = LOD->Modules[ModuleIdx];
						if (Module && !Module->IsA(UParticleModuleTypeDataBase::StaticClass()))
						{
							ModuleOffsetMap.Add(Module, Offset);
						}
					}
				}
				ParticleSize += MaxBytes;
			}
			if (MaxInstanceBytes > 0)
			{
				const uint32 InstanceOffset = ReqInstanceBytes;
				for (UParticleLODLevel* LOD : LODLevels)
				{
					if (LOD && LOD->Modules.IsValidIndex(ModuleIdx))
					{
						UParticleModule* Module = LOD->Modules[ModuleIdx];
						if (Module && !Module->IsA(UParticleModuleTypeDataBase::StaticClass()))
						{
							ModuleInstanceOffsetMap.Add(Module, InstanceOffset);
						}
					}
				}
				ReqInstanceBytes += MaxInstanceBytes;
			}
		}
	}

	ParticleStride = AlignUp(ParticleSize, 16);
}

int32 UParticleEmitter::GetModuleDataOffset(UParticleModule* Module) const
{
	const uint32* Offset = ModuleOffsetMap.Find(Module);
	return Offset ? static_cast<int32>(*Offset) : INDEX_NONE;
}

int32 UParticleEmitter::GetModuleInstanceDataOffset(UParticleModule* Module) const
{
	const uint32* Offset = ModuleInstanceOffsetMap.Find(Module);
	return Offset ? static_cast<int32>(*Offset) : INDEX_NONE;
}

void UParticleEmitter::DeleteOwnedObjects()
{
	for (UParticleLODLevel* LODLevel : LODLevels)
	{
		if (LODLevel)
		{
			LODLevel->DeleteOwnedObjects();
			delete LODLevel;
		}
	}
	LODLevels.Empty();
}
