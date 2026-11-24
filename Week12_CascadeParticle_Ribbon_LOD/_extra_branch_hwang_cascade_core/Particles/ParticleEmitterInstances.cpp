#include "EnginePCH.h"
#include "ParticleEmitterInstances.h"
#include "ParticleEmitter.h"
#include "ParticleEmitterInstanceOwner.h"
#include "ParticleHelper.h"
#include "ParticleLODLevel.h"
#include "ParticleModule.h"
#include "Core/HAL/UnrealMemory.h"
#include "Math/Transform.h"
#include "Render/MaterialInterface.h"

namespace
{
	constexpr int32 GMaxParticleInstanceCount = 65535;
	constexpr int32 GDefaultInitialParticleCapacity = 16;

	int32 ResolveModuleOffset(const UParticleEmitter* Template, UParticleModule* Module, int32 DefaultOffset)
	{
		if (!Template || !Module)
		{
			return DefaultOffset;
		}

		const uint32* FoundOffset = Template->ModuleOffsetMap.Find(Module);
		return FoundOffset ? static_cast<int32>(*FoundOffset) : DefaultOffset;
	}

	void UpdateEmitterTransforms(FParticleEmitterInstance& Instance, const UParticleModuleRequired* RequiredModule)
	{
		const FMatrix ComponentWorldMatrix = Instance.Component.GetComponentToWorld().GetLocalMatrix();
		const FMatrix OriginMatrix = RequiredModule
			? FTransform(RequiredModule->EmitterRotation, RequiredModule->EmitterOrigin).GetLocalMatrix()
			: FMatrix::Identity;

		if (RequiredModule && RequiredModule->bUseLocalSpace)
		{
			Instance.EmitterToSimulation = OriginMatrix;
			Instance.SimulationToWorld = ComponentWorldMatrix;
		}
		else
		{
			Instance.EmitterToSimulation = OriginMatrix * ComponentWorldMatrix;
			Instance.SimulationToWorld = FMatrix::Identity;
		}
	}
}

FParticleEmitterBuildInfo::FParticleEmitterBuildInfo()
	: RequiredModule(nullptr)
	, SpawnModule(nullptr)
	, MaxSize(0.0f, 0.0f)
	, SizeScaleBySpeed(0.0f, 0.0f)
	, MaxSizeScaleBySpeed(0.0f, 0.0f)
	, bEnableCollision(false)
	, CollisionResponse(0)
	, CollisionMode(0)
	, CollisionRadiusScale(1.0f)
	, CollisionRadiusBias(0.0f)
	, CollisionRandomSpread(0.0f)
	, CollisionRandomDistribution(1.0f)
	, Friction(0.0f)
	, ConstantAcceleration(FVector::ZeroVector)
	, MaxLifetime(0.0f)
	, MaxRotationRate(0.0f)
	, EstimatedMaxActiveParticleCount(0)
	, ScreenAlignment(0)
	, PivotOffset(0.0f, 0.0f)
{
}

FParticleEmitterInstance::FParticleEmitterInstance(IParticleEmitterInstanceOwner& InComponent)
	: SpriteTemplate(nullptr)
	, Component(InComponent)
	, CurrentLODLevel(nullptr)
	, CurrentLODLevelIndex(0)
	, TypeDataOffset(0)
	, TypeDataInstanceOffset(0)
	, SubUVDataOffset(0)
	, DynamicParameterDataOffset(0)
	, CameraPayloadOffset(0)
	, PayloadOffset(0)
	, Location(FVector::ZeroVector)
	, EmitterToSimulation(FMatrix::Identity)
	, SimulationToWorld(FMatrix::Identity)
	, SortMode(0)
	, ParticleData(nullptr)
	, ParticleIndices(nullptr)
	, InstanceData(nullptr)
	, InstancePayloadSize(0)
	, ParticleSize(0)
	, ParticleStride(0)
	, ActiveParticles(0)
	, ParticleCounter(0)
	, MaxActiveParticles(0)
	, SpawnFraction(0.0f)
	, SecondsSinceCreation(0.0f)
	, EmitterTime(0.0f)
	, LastDeltaTime(0.0f)
	, LastTickDurationMs(0.0f)
	, OldLocation(FVector::ZeroVector)
	, ParticleBoundingBox{ FVector::ZeroVector, FVector::ZeroVector }
	, LoopCount(0)
	, IsRenderDataDirty(0)
	, EmitterDuration(0.0f)
	, CurrentDelay(0.0f)
	, TrianglesToRender(0)
	, MaxVertexIndex(0)
	, CurrentMaterial(nullptr)
{
}

FParticleEmitterInstance::~FParticleEmitterInstance()
{
	if (ParticleData)
	{
		FMemory::Free(ParticleData);
		ParticleData = nullptr;
	}
	ParticleIndices = nullptr;

	if (InstanceData)
	{
		FMemory::Free(InstanceData);
		InstanceData = nullptr;
	}
}

void FParticleEmitterInstance::InitParameters(UParticleEmitter* InTemplate)
{
	SpriteTemplate = InTemplate;
	if (!SpriteTemplate)
	{
		CurrentLODLevel = nullptr;
		CurrentMaterial = nullptr;
		ParticleSize = 0;
		ParticleStride = 0;
		PayloadOffset = 0;
		InstancePayloadSize = 0;
		EmitterDurations.Empty();
		return;
	}

	SubUVDataOffset = SpriteTemplate->SubUVDataOffset;
	TypeDataOffset = SpriteTemplate->TypeDataOffset;
	TypeDataInstanceOffset = SpriteTemplate->TypeDataInstanceOffset;

	CurrentLODLevelIndex = 0;
	CurrentLODLevel = SpriteTemplate->GetCurrentLODLevel(this);
	if (!CurrentLODLevel)
	{
		CurrentLODLevel = SpriteTemplate->GetLODLevel(0);
	}

	PayloadOffset = sizeof(FBaseParticle);
	ParticleSize = SpriteTemplate->ParticleSize;
	if (ParticleSize <= 0)
	{
		ParticleSize = static_cast<int32>(FMemory::QuantizeSize(sizeof(FBaseParticle), 16));
	}
	ParticleStride = ParticleSize;

	InstancePayloadSize = SpriteTemplate->ReqInstanceBytes;
	if (InstanceData)
	{
		FMemory::Free(InstanceData);
		InstanceData = nullptr;
	}
	if (InstancePayloadSize > 0)
	{
		const uint64 AllocBytes = static_cast<uint64>(InstancePayloadSize);
		InstanceData = static_cast<uint8*>(FMemory::Malloc(AllocBytes));
		if (InstanceData)
		{
			FMemory::Memzero(InstanceData, AllocBytes);
		}
	}

	EmitterDurations.Empty();
	EmitterDurations.Reserve(SpriteTemplate->LODLevels.Num());
	for (int32 LODIdx = 0; LODIdx < SpriteTemplate->LODLevels.Num(); ++LODIdx)
	{
		const UParticleLODLevel* LODLevel = SpriteTemplate->LODLevels[LODIdx];
		const float Duration = (LODLevel && LODLevel->RequiredModule) ? LODLevel->RequiredModule->EmitterDuration : 0.0f;
		EmitterDurations.Add(Duration);
	}

	UParticleModuleRequired* RequiredModule = CurrentLODLevel ? CurrentLODLevel->RequiredModule : nullptr;
	if (RequiredModule)
	{
		CurrentMaterial = RequiredModule->Material;
		SortMode = static_cast<int32>(RequiredModule->SortMode);
		CurrentDelay = RequiredModule->EmitterDelay;
		EmitterDuration = (CurrentLODLevelIndex >= 0 && CurrentLODLevelIndex < EmitterDurations.Num())
			? EmitterDurations[CurrentLODLevelIndex]
			: RequiredModule->EmitterDuration;
	}
	else
	{
		CurrentMaterial = nullptr;
		SortMode = 0;
		CurrentDelay = 0.0f;
		EmitterDuration = 0.0f;
	}

	Location = Component.GetComponentToWorld().Location;
	OldLocation = Location;
	UpdateEmitterTransforms(*this, RequiredModule);
}

void FParticleEmitterInstance::Init()
{
	if (!SpriteTemplate)
	{
		return;
	}

	if (!CurrentLODLevel)
	{
		CurrentLODLevel = SpriteTemplate->GetCurrentLODLevel(this);
		if (!CurrentLODLevel)
		{
			CurrentLODLevel = SpriteTemplate->GetLODLevel(CurrentLODLevelIndex);
		}
	}

	ActiveParticles = 0;
	ParticleCounter = 0;
	SpawnFraction = 0.0f;
	SecondsSinceCreation = 0.0f;
	EmitterTime = 0.0f;
	LastDeltaTime = 0.0f;
	LoopCount = 0;
	bHaltSpawning = false;
	bEmitterIsDone = false;

	const int32 InitialMaxCount = (SpriteTemplate->PeakActiveParticles > 0) ? SpriteTemplate->PeakActiveParticles : GDefaultInitialParticleCapacity;
	Resize(InitialMaxCount, true);
}

bool FParticleEmitterInstance::Resize(int32 NewMaxActiveParticles, bool bSetMaxActiveCount)
{
	if (NewMaxActiveParticles < 0)
	{
		NewMaxActiveParticles = 0;
	}
	if (NewMaxActiveParticles > GMaxParticleInstanceCount)
	{
		NewMaxActiveParticles = GMaxParticleInstanceCount;
	}

	if (NewMaxActiveParticles == MaxActiveParticles && (NewMaxActiveParticles == 0 || ParticleData != nullptr))
	{
		return true;
	}

	if (NewMaxActiveParticles == 0)
	{
		if (ParticleData)
		{
			FMemory::Free(ParticleData);
			ParticleData = nullptr;
		}
		ParticleIndices = nullptr;
		ActiveParticles = 0;
		MaxActiveParticles = 0;
		return true;
	}

	if (ParticleStride <= 0)
	{
		ParticleSize = static_cast<int32>(FMemory::QuantizeSize(sizeof(FBaseParticle), 16));
		ParticleStride = ParticleSize;
		PayloadOffset = sizeof(FBaseParticle);
	}

	const uint64 NewParticleDataBytes = FMemory::QuantizeSize(static_cast<uint64>(NewMaxActiveParticles) * static_cast<uint64>(ParticleStride), 16);
	const uint64 NewIndicesBytes = static_cast<uint64>(NewMaxActiveParticles) * sizeof(uint16);
	const uint64 TotalBytes = NewParticleDataBytes + NewIndicesBytes;

	uint8* NewData = static_cast<uint8*>(FMemory::Malloc(TotalBytes));
	if (!NewData)
	{
		return false;
	}

	FMemory::Memzero(NewData, TotalBytes);
	uint16* NewIndices = reinterpret_cast<uint16*>(NewData + NewParticleDataBytes);

	if (ParticleData && ParticleIndices && MaxActiveParticles > 0)
	{
		if (NewMaxActiveParticles >= MaxActiveParticles)
		{
			const uint64 CopyDataBytes = static_cast<uint64>(MaxActiveParticles) * static_cast<uint64>(ParticleStride);
			const uint64 CopyIndexBytes = static_cast<uint64>(MaxActiveParticles) * sizeof(uint16);
			FMemory::Memcpy(NewData, ParticleData, CopyDataBytes);
			FMemory::Memcpy(NewIndices, ParticleIndices, CopyIndexBytes);

			for (int32 Index = MaxActiveParticles; Index < NewMaxActiveParticles; ++Index)
			{
				NewIndices[Index] = static_cast<uint16>(Index);
			}
		}
		else
		{
			const int32 PreservedCount = FMath::Min(ActiveParticles, NewMaxActiveParticles);
			for (int32 i = 0; i < PreservedCount; ++i)
			{
				const uint16 SrcIndex = ParticleIndices[i];
				const uint8* SrcPtr = ParticleData + static_cast<size_t>(SrcIndex) * static_cast<size_t>(ParticleStride);
				uint8* DstPtr = NewData + static_cast<size_t>(i) * static_cast<size_t>(ParticleStride);
				FMemory::Memcpy(DstPtr, SrcPtr, static_cast<uint64>(ParticleStride));
			}

			for (int32 Index = 0; Index < NewMaxActiveParticles; ++Index)
			{
				NewIndices[Index] = static_cast<uint16>(Index);
			}
			ActiveParticles = PreservedCount;
		}

		FMemory::Free(ParticleData);
	}
	else
	{
		for (int32 Index = 0; Index < NewMaxActiveParticles; ++Index)
		{
			NewIndices[Index] = static_cast<uint16>(Index);
		}
		ActiveParticles = 0;
	}

	ParticleData = NewData;
	ParticleIndices = NewIndices;
	MaxActiveParticles = NewMaxActiveParticles;

	if (bSetMaxActiveCount && SpriteTemplate && MaxActiveParticles > SpriteTemplate->PeakActiveParticles)
	{
		SpriteTemplate->PeakActiveParticles = MaxActiveParticles;
	}

	return true;
}

void FParticleEmitterInstance::Tick(float DeltaTime, bool bSuppressSpawning)
{
	if (!bEnabled || !SpriteTemplate || DeltaTime < 0.0f)
	{
		return;
	}

	UParticleLODLevel* LODLevel = SpriteTemplate->GetCurrentLODLevel(this);
	if (!LODLevel)
	{
		LODLevel = CurrentLODLevel ? CurrentLODLevel : SpriteTemplate->GetLODLevel(CurrentLODLevelIndex);
	}
	if (!LODLevel || !LODLevel->bEnabled)
	{
		return;
	}
	CurrentLODLevel = LODLevel;

	UParticleModuleRequired* RequiredModule = CurrentLODLevel->RequiredModule;
	if (!RequiredModule || !RequiredModule->bEnabled)
	{
		return;
	}

	OldLocation = Location;
	Location = Component.GetComponentToWorld().Location;
	UpdateEmitterTransforms(*this, RequiredModule);

	SecondsSinceCreation += DeltaTime;
	LastDeltaTime = DeltaTime;

	if (CurrentDelay > 0.0f)
	{
		CurrentDelay -= DeltaTime;
		if (CurrentDelay > 0.0f)
		{
			return;
		}

		DeltaTime = -CurrentDelay;
		CurrentDelay = 0.0f;
	}

	const bool bFirstTime = (EmitterTime == 0.0f && LoopCount == 0);
	const float ActiveDeltaTime = Tick_EmitterTimeSetup(DeltaTime, CurrentLODLevel);

	if (ActiveParticles > 0)
	{
		Tick_ModuleUpdate(ActiveDeltaTime, CurrentLODLevel);
		Tick_ModulePostUpdate(ActiveDeltaTime, CurrentLODLevel);
	}

	Tick_SpawnParticles(ActiveDeltaTime, CurrentLODLevel, bSuppressSpawning, bFirstTime);
	Tick_ModuleFinalUpdate(ActiveDeltaTime, CurrentLODLevel);
}

void FParticleEmitterInstance::CheckEmitterFinished()
{
	if (bHaltSpawning && ActiveParticles == 0 && bKillOnCompleted)
	{
		bEmitterIsDone = true;
	}
}

float FParticleEmitterInstance::Tick_EmitterTimeSetup(float DeltaTime, UParticleLODLevel* InCurrentLODLevel)
{
	if (!InCurrentLODLevel || !InCurrentLODLevel->RequiredModule)
	{
		return DeltaTime;
	}

	UParticleModuleRequired* RequiredModule = InCurrentLODLevel->RequiredModule;
	if (CurrentLODLevelIndex >= 0 && CurrentLODLevelIndex < EmitterDurations.Num())
	{
		EmitterDuration = EmitterDurations[CurrentLODLevelIndex];
	}
	else
	{
		EmitterDuration = RequiredModule->EmitterDuration;
	}

	if (EmitterDuration <= 0.0f)
	{
		EmitterTime += DeltaTime;
		return DeltaTime;
	}

	const float NewEmitterTime = EmitterTime + DeltaTime;
	if (NewEmitterTime >= EmitterDuration)
	{
		const int32 LoopsCompleted = static_cast<int32>(NewEmitterTime / EmitterDuration);
		LoopCount += LoopsCompleted;

		if (RequiredModule->EmitterLoops > 0 && LoopCount >= RequiredModule->EmitterLoops)
		{
			bHaltSpawning = true;
			EmitterTime = EmitterDuration;
			return DeltaTime;
		}

		EmitterTime = std::fmod(NewEmitterTime, EmitterDuration);
	}
	else
	{
		EmitterTime = NewEmitterTime;
	}

	return DeltaTime;
}

float FParticleEmitterInstance::Tick_SpawnParticles(
	float DeltaTime,
	UParticleLODLevel* InCurrentLODLevel,
	bool bSuppressSpawning,
	bool bFirstTime)
{
	if (bSuppressSpawning || bHaltSpawning || !InCurrentLODLevel)
	{
		return 0.0f;
	}

	UParticleModuleSpawn* SpawnModule = InCurrentLODLevel->SpawnModule;
	if (!SpawnModule || !SpawnModule->bEnabled)
	{
		return 0.0f;
	}

	int32 RateSpawnCount = 0;
	SpawnModule->GetSpawnAmount(EmitterTime, DeltaTime, SpawnFraction, RateSpawnCount, Component.GetDistributionData(), &Component.GetRandomStream());

	int32 BurstSpawnCount = 0;
	if (DeltaTime > 0.0f || bFirstTime)
	{
		if (EmitterDuration > 0.0f && EmitterTime < DeltaTime && LoopCount > 0)
		{
			const float PrevEndOldTime = FMath::Max(0.0f, EmitterDuration - (DeltaTime - EmitterTime));
			if (PrevEndOldTime > 0.0f)
			{
				BurstSpawnCount += SpawnModule->GetBurstCount(PrevEndOldTime, EmitterDuration, &Component.GetRandomStream());
			}
			BurstSpawnCount += SpawnModule->GetBurstCount(0.0f, EmitterTime, &Component.GetRandomStream());
		}
		else
		{
			const float OldEmitterTime = FMath::Max(0.0f, EmitterTime - DeltaTime);
			BurstSpawnCount += SpawnModule->GetBurstCount(OldEmitterTime, EmitterTime, &Component.GetRandomStream());
		}
	}

	const int32 TotalSpawnCount = RateSpawnCount + BurstSpawnCount;
	if (TotalSpawnCount > 0)
	{
		const float Increment = (TotalSpawnCount > 1 && DeltaTime > 0.0f) ? (DeltaTime / static_cast<float>(TotalSpawnCount)) : 0.0f;

		FVector InitialLocation = FVector::ZeroVector;
		if (!InCurrentLODLevel->RequiredModule || !InCurrentLODLevel->RequiredModule->bUseLocalSpace)
		{
			InitialLocation = EmitterToSimulation.TransformPosition(FVector::ZeroVector);
		}
		else
		{
			InitialLocation = InCurrentLODLevel->RequiredModule->EmitterOrigin;
		}

		SpawnParticles(TotalSpawnCount, 0.0f, Increment, InitialLocation, FVector::ZeroVector, nullptr);
	}

	return SpawnFraction;
}

void FParticleEmitterInstance::Tick_ModuleUpdate(float DeltaTime, UParticleLODLevel* InCurrentLODLevel)
{
	if (!InCurrentLODLevel || ActiveParticles <= 0 || !ParticleData || !ParticleIndices)
	{
		return;
	}

	for (int32 ModuleIdx = 0; ModuleIdx < InCurrentLODLevel->UpdateModules.Num(); ++ModuleIdx)
	{
		UParticleModule* Module = InCurrentLODLevel->UpdateModules[ModuleIdx];
		if (!Module || !Module->bEnabled || !Module->bUpdateModule)
		{
			continue;
		}

		const int32 Offset = ResolveModuleOffset(SpriteTemplate, Module, PayloadOffset);
		const UParticleModule::FUpdateContext UpdateContext(*this, Offset, DeltaTime);
		Module->Update(UpdateContext);
	}
}

void FParticleEmitterInstance::Tick_ModulePostUpdate(float DeltaTime, UParticleLODLevel* InCurrentLODLevel)
{
	if (!ParticleData || !ParticleIndices || ActiveParticles <= 0)
	{
		return;
	}

	for (int32 i = ActiveParticles - 1; i >= 0; --i)
	{
		const int32 CurrentIndex = ParticleIndices[i];
		uint8* ParticleBase = ParticleData + static_cast<size_t>(CurrentIndex) * static_cast<size_t>(ParticleStride);
		FBaseParticle& Particle = *reinterpret_cast<FBaseParticle*>(ParticleBase);

		if ((Particle.Flags & STATE_Particle_Freeze) == 0)
		{
			Particle.RelativeTime += Particle.OneOverMaxLifeTime * DeltaTime;

			if ((Particle.Flags & STATE_Particle_FreezeTranslation) == 0)
			{
				Particle.OldLocation = Particle.Location;
				Particle.Location += Particle.Velocity * DeltaTime;
			}

			if ((Particle.Flags & STATE_Particle_FreezeRotation) == 0)
			{
				Particle.Rotation += Particle.RotationRate * DeltaTime;
			}
		}

		Particle.Flags &= ~STATE_Particle_JustSpawned;
	}
}

void FParticleEmitterInstance::Tick_ModuleFinalUpdate(float DeltaTime, UParticleLODLevel* InCurrentLODLevel)
{
	if (InCurrentLODLevel && ActiveParticles > 0 && ParticleData && ParticleIndices)
	{
		for (int32 ModuleIdx = 0; ModuleIdx < InCurrentLODLevel->UpdateModules.Num(); ++ModuleIdx)
		{
			UParticleModule* Module = InCurrentLODLevel->UpdateModules[ModuleIdx];
			if (!Module || !Module->bEnabled || !Module->bFinalUpdateModule)
			{
				continue;
			}

			const int32 Offset = ResolveModuleOffset(SpriteTemplate, Module, PayloadOffset);
			const UParticleModule::FUpdateContext FinalContext(*this, Offset, DeltaTime);
			Module->FinalUpdate(FinalContext);
		}
	}

	KillParticles();

	if (ActiveParticles > 0 && ParticleData && ParticleIndices)
	{
		const bool bUseLocalSpace = (InCurrentLODLevel && InCurrentLODLevel->RequiredModule) ? InCurrentLODLevel->RequiredModule->bUseLocalSpace : false;

		FVector MinBound(FLT_MAX, FLT_MAX, FLT_MAX);
		FVector MaxBound(-FLT_MAX, -FLT_MAX, -FLT_MAX);

		for (int32 i = 0; i < ActiveParticles; ++i)
		{
			const uint16 ParticleIndex = ParticleIndices[i];
			const FBaseParticle& Particle = *reinterpret_cast<const FBaseParticle*>(ParticleData + static_cast<size_t>(ParticleIndex) * static_cast<size_t>(ParticleStride));

			const FVector WorldPos = bUseLocalSpace ? SimulationToWorld.TransformPosition(Particle.Location) : Particle.Location;
			const FVector HalfExtent(std::fabs(Particle.Size.X) * 0.5f, std::fabs(Particle.Size.Y) * 0.5f, std::fabs(Particle.Size.Z) * 0.5f);

			MinBound.X = FMath::Min(MinBound.X, WorldPos.X - HalfExtent.X);
			MinBound.Y = FMath::Min(MinBound.Y, WorldPos.Y - HalfExtent.Y);
			MinBound.Z = FMath::Min(MinBound.Z, WorldPos.Z - HalfExtent.Z);
			MaxBound.X = FMath::Max(MaxBound.X, WorldPos.X + HalfExtent.X);
			MaxBound.Y = FMath::Max(MaxBound.Y, WorldPos.Y + HalfExtent.Y);
			MaxBound.Z = FMath::Max(MaxBound.Z, WorldPos.Z + HalfExtent.Z);
		}

		ParticleBoundingBox = FBox{ MinBound, MaxBound };
	}
	else
	{
		ParticleBoundingBox = FBox{ Location, Location };
	}

	CheckEmitterFinished();
	IsRenderDataDirty = 1;
}

void FParticleEmitterInstance::SetCurrentLODIndex(int32 InLODIndex, bool bInFullyProcess)
{
	if (!SpriteTemplate)
	{
		return;
	}

	UParticleLODLevel* NewLODLevel = SpriteTemplate->GetLODLevel(InLODIndex);
	if (!NewLODLevel)
	{
		return;
	}

	CurrentLODLevelIndex = InLODIndex;
	CurrentLODLevel = NewLODLevel;

	if (CurrentLODLevel->RequiredModule)
	{
		CurrentMaterial = CurrentLODLevel->RequiredModule->Material;
		SortMode = static_cast<int32>(CurrentLODLevel->RequiredModule->SortMode);
	}

	if (bInFullyProcess && CurrentLODLevelIndex < EmitterDurations.Num())
	{
		EmitterDuration = EmitterDurations[CurrentLODLevelIndex];
	}
}

void FParticleEmitterInstance::GetAllocatedSize(int32& OutNum, int32& OutMax)
{
	OutNum = ActiveParticles * ParticleStride;
	OutMax = MaxActiveParticles * ParticleStride + InstancePayloadSize;
}

void FParticleEmitterInstance::SpawnParticles(int32 Count, float StartTime, float Increment, const FVector& InitialLocation, const FVector& InitialVelocity, FParticleEventInstancePayload* EventPayload)
{
	if (Count <= 0 || !CurrentLODLevel || ParticleStride <= 0)
	{
		return;
	}

	if (ActiveParticles + Count > MaxActiveParticles)
	{
		const int32 GrownCapacity = FMath::Max(ActiveParticles + Count, MaxActiveParticles > 0 ? MaxActiveParticles * 2 : GDefaultInitialParticleCapacity);
		if (!Resize(GrownCapacity, true))
		{
			return;
		}
	}

	if (!ParticleData || !ParticleIndices)
	{
		return;
	}

	for (int32 i = 0; i < Count; ++i)
	{
		if (ActiveParticles >= MaxActiveParticles)
		{
			break;
		}

		const uint16 ParticleIndex = ParticleIndices[ActiveParticles];
		uint8* ParticleBase = ParticleData + static_cast<size_t>(ParticleIndex) * static_cast<size_t>(ParticleStride);
		FBaseParticle* Particle = reinterpret_cast<FBaseParticle*>(ParticleBase);

		const float SpawnTime = StartTime + static_cast<float>(i) * Increment;
		PreSpawn(Particle, InitialLocation, InitialVelocity);

		if (ParticleStride > PayloadOffset)
		{
			FMemory::Memzero(ParticleBase + PayloadOffset, static_cast<uint64>(ParticleStride - PayloadOffset));
		}

		for (int32 ModuleIdx = 0; ModuleIdx < CurrentLODLevel->SpawnModules.Num(); ++ModuleIdx)
		{
			UParticleModule* Module = CurrentLODLevel->SpawnModules[ModuleIdx];
			if (!Module || !Module->bEnabled)
			{
				continue;
			}

			const int32 Offset = ResolveModuleOffset(SpriteTemplate, Module, PayloadOffset);
			const UParticleModule::FSpawnContext SpawnContext(*this, Offset, SpawnTime, Particle);
			Module->Spawn(SpawnContext);
		}

		const float InterpPercentage = (Count > 1) ? (static_cast<float>(i) / static_cast<float>(Count)) : 0.0f;
		PostSpawn(Particle, InterpPercentage, SpawnTime);

		ActiveParticles++;
		ParticleCounter++;
	}
}

void FParticleEmitterInstance::PreSpawn(FBaseParticle* Particle, const FVector& InitialLocation, const FVector& InitialVelocity)
{
	if (!Particle)
	{
		return;
	}

	FMemory::Memzero(Particle, sizeof(FBaseParticle));
	Particle->Location = InitialLocation;
	Particle->OldLocation = InitialLocation;
	Particle->BaseVelocity = InitialVelocity;
	Particle->Velocity = InitialVelocity;
	Particle->BaseSize = FVector::ZeroVector;
	Particle->Size = FVector::ZeroVector;
	Particle->BaseColor = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);
	Particle->Color = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);
	Particle->RelativeTime = 0.0f;
	Particle->OneOverMaxLifeTime = 0.0f;
	Particle->Flags = STATE_Particle_JustSpawned;
}

void FParticleEmitterInstance::PostSpawn(FBaseParticle* Particle, float InterpolationPercentage, float SpawnTime)
{
	if (!Particle)
	{
		return;
	}

	if (SpawnTime > 0.0f)
	{
		if (Particle->RelativeTime == 0.0f)
		{
			Particle->RelativeTime += Particle->OneOverMaxLifeTime * SpawnTime;
		}
		Particle->Location += Particle->Velocity * SpawnTime;
	}
}

void FParticleEmitterInstance::KillParticles()
{
	if (!ParticleData || !ParticleIndices || ActiveParticles <= 0)
	{
		return;
	}

	for (int32 i = ActiveParticles - 1; i >= 0; --i)
	{
		const int32 CurrentIndex = ParticleIndices[i];
		const uint8* ParticleBase = ParticleData + static_cast<size_t>(CurrentIndex) * static_cast<size_t>(ParticleStride);
		const FBaseParticle& Particle = *reinterpret_cast<const FBaseParticle*>(ParticleBase);

		if (Particle.RelativeTime >= 1.0f)
		{
			KILL_CURRENT_PARTICLE;
		}
	}
}

FParticleSpriteEmitterInstance::FParticleSpriteEmitterInstance(IParticleEmitterInstanceOwner& InComponent)
	: FParticleEmitterInstance(InComponent)
{
}

FParticleSpriteEmitterInstance::~FParticleSpriteEmitterInstance()
{
}

void FParticleSpriteEmitterInstance::GetAllocatedSize(int32& OutNum, int32& OutMax)
{
	FParticleEmitterInstance::GetAllocatedSize(OutNum, OutMax);
}
