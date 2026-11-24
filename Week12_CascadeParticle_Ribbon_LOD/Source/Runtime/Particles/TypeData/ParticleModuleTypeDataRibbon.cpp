#include "EnginePCH.h"
#include "Particles/TypeData/ParticleModuleTypeDataRibbon.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/ParticleHelper.h"
#include "Math/EngineMath.h"
#include "Render/Material.h"
#include "Render/RenderCommand.h"
#include "Render/RenderPacket.h"
#include "Asset/AssetManager.h"
#include "Core/ResourcePaths.h"
#include <cmath>

namespace
{
	FLinearColor LerpLinearColor(const FLinearColor& A, const FLinearColor& B, float Alpha)
	{
		return FLinearColor(
			FMath::Lerp(A.R, B.R, Alpha),
			FMath::Lerp(A.G, B.G, Alpha),
			FMath::Lerp(A.B, B.B, Alpha),
			FMath::Lerp(A.A, B.A, Alpha));
	}

	int32 GrowRibbonCapacity(int32 CurrentCapacity, int32 Needed, int32 MinInitial)
	{
		int32 NewCapacity = (CurrentCapacity > 0) ? CurrentCapacity : MinInitial;
		while (NewCapacity < Needed)
		{
			NewCapacity *= 2;
		}
		return NewCapacity;
	}
}

void TrailsBase_CalculateTangent(
	FBaseParticle* InPrevParticle, FRibbonTypeDataPayload* InPrevTrailData,
	FBaseParticle* InNextParticle, FRibbonTypeDataPayload* InNextTrailData,
	float InCurrNextDelta, FRibbonTypeDataPayload* InOutCurrTrailData)
{
	// Recalculate the current tangent...
	// Calculate the new tangent from the previous and next position...
	// LastSourcePosition will be that position of the first particle that will be spawned this frame
	FVector PositionDelta = InPrevParticle->Location - InNextParticle->Location;
	float TimeDelta = InPrevTrailData->SpawnTime - InNextTrailData->SpawnTime;
	// Disabling the check as it apparently is causing problems 
	//check(TimeDelta >= 0.0f);

	TimeDelta = (TimeDelta == 0.0f) ? 0.0032f : std::abs(TimeDelta);

	FVector NewTangent = (PositionDelta / TimeDelta);

	NewTangent *= InCurrNextDelta;
	NewTangent *= (1.0f / InOutCurrTrailData->SpawnedTessellationPoints);

	InOutCurrTrailData->Tangent = (FVector)NewTangent;
}

uint32 UParticleModuleTypeDataRibbon::RequiredBytes(UParticleModuleTypeDataBase* TypeData)
{
	return sizeof(FRibbonTypeDataPayload);
}

FParticleEmitterInstance* UParticleModuleTypeDataRibbon::CreateInstance() const
{
	return new FParticleRibbonEmitterInstance();
}

FParticleRibbonEmitterInstance::~FParticleRibbonEmitterInstance()
{
	DestroyRenderResources();
}

bool FParticleRibbonEmitterInstance::Init(UParticleEmitter* InTemplate, uint32 Seed)
{
	if (!FParticleEmitterInstance::Init(InTemplate, Seed))
	{
		return false;
	}

	UParticleLODLevel* LODLevel = InTemplate ? InTemplate->GetLODLevel(0) : nullptr;
	TrailTypeData = LODLevel ? Cast<UParticleModuleTypeDataRibbon>(LODLevel->TypeDataModule) : nullptr;
	if (!TrailTypeData)
	{
		HTR_LOG(Warning, "[Particle] Ribbon Init failed: TypeDataModule is null or not UParticleModuleTypeDataRibbon");
		Release();
		return false;
	}

	bDeadTrailsOnDeactivate = TrailTypeData->bDeadTrailsOnDeactivate;
	MaxTrailCount = (TrailTypeData->MaxTrailCount > 0) ? TrailTypeData->MaxTrailCount : 1;
	const uint32 TrailCapacity = static_cast<uint32>(MaxTrailCount);

	TrailSpawnTimes.Init(0.0f, TrailCapacity);
	LastSpawnTime.Init(0.0f, TrailCapacity);
	SourceDistanceTraveled.Init(0.0f, TrailCapacity);
	TiledUDistanceTraveled.Init(0.0f, TrailCapacity);

	CurrentSourcePosition.Init(FVector::ZeroVector, TrailCapacity);
	CurrentSourceRotation.Init(FQuat::Identity(), TrailCapacity);
	CurrentSourceUp.Init(FVector::ZeroVector, TrailCapacity);
	CurrentSourceTangent.Init(FVector::ZeroVector, TrailCapacity);
	CurrentSourceTangentStrength.Init(0.0f, TrailCapacity);

	LastSourcePosition.Init(FVector::ZeroVector, TrailCapacity);
	LastSourceRotation.Init(FQuat::Identity(), TrailCapacity);
	LastSourceUp.Init(FVector::ZeroVector, TrailCapacity);
	LastSourceTangent.Init(FVector::ZeroVector, TrailCapacity);
	LastSourceTangentStrength.Init(0.0f, TrailCapacity);

	SourceOffsets.Init(FVector::ZeroVector, TrailCapacity);
	SourceIndices.Init(INDEX_NONE, TrailCapacity);
	SourceTimes.Init(0.0f, TrailCapacity);
	LastSourceTimes.Init(0.0f, TrailCapacity);
	CurrentLifetimes.Init(0.0f, TrailCapacity);
	CurrentSizes.Init(0.0f, TrailCapacity);

	TrailCount = 0;
	VertexCount = 0;
	TriangleCount = 0;
	RunningTime = 0.0f;
	LastTickTime = 0.0f;
	bFirstUpdate = true;
	LastSelectedParticleIndex = INDEX_NONE;

	return true;
}

bool FParticleRibbonEmitterInstance::Resize(int32 NewMaxActiveParticles)
{	
	constexpr int32 MaxRibbonActiveParticles = 0x3FFF;
	if (NewMaxActiveParticles > MaxRibbonActiveParticles)
	{
		HTR_LOG(Warning, "[Particle] Ribbon Resize clamped {} -> {} (14-bit link index limit)", NewMaxActiveParticles, MaxRibbonActiveParticles);
		NewMaxActiveParticles = MaxRibbonActiveParticles;
	}
	return FParticleEmitterInstance::Resize(NewMaxActiveParticles);
}

void FParticleRibbonEmitterInstance::Release()
{
	VertexCount = 0;
	TriangleCount = 0;
	TrailCount = 0;
	MaxTrailCount = 0;
	RunningTime = 0.0f;
	LastTickTime = 0.0f;
	bDeadTrailsOnDeactivate = false;
	bFirstUpdate = true;

	TrailTypeData = nullptr;
	SourceEmitter = nullptr;
	LastSelectedParticleIndex = INDEX_NONE;

	TrailSpawnTimes.Empty();
	LastSpawnTime.Empty();
	SourceDistanceTraveled.Empty();
	TiledUDistanceTraveled.Empty();

	CurrentSourcePosition.Empty();
	CurrentSourceRotation.Empty();
	CurrentSourceUp.Empty();
	CurrentSourceTangent.Empty();
	CurrentSourceTangentStrength.Empty();

	LastSourcePosition.Empty();
	LastSourceRotation.Empty();
	LastSourceUp.Empty();
	LastSourceTangent.Empty();
	LastSourceTangentStrength.Empty();

	SourceOffsets.Empty();
	SourceIndices.Empty();
	SourceTimes.Empty();
	LastSourceTimes.Empty();
	CurrentLifetimes.Empty();
	CurrentSizes.Empty();

	DestroyRenderResources();

	FParticleEmitterInstance::Release();
}

void FParticleRibbonEmitterInstance::Tick(float DeltaTime)
{
	UParticleLODLevel* CurrentLOD = GetCurrentLODLevel();
	if (CurrentLOD)
	{
		if (UParticleModuleTypeDataRibbon* CurrentRibbon = Cast<UParticleModuleTypeDataRibbon>(CurrentLOD->TypeDataModule))
		{
			TrailTypeData = CurrentRibbon;
		}
	}
	if (!Template || !CurrentLOD || !TrailTypeData)
	{
		return;
	}

	bool bFirstTime = (SecondsSinceCreation > 0.0f) ? false : true;

	float EmitterDelay = Tick_EmitterTimeSetup(DeltaTime);

	UpdateSourceData(DeltaTime, bFirstTime);

	KillParticles();

	SpawnFraction = Tick_SpawnParticles(DeltaTime, bFirstTime);

	ResetParticleParameters(DeltaTime);

	Tick_ModuleUpdate(DeltaTime);

	UpdateParticlePositions(DeltaTime);

	Tick_RecalculateTangents(DeltaTime, GetCurrentLODLevel());
	DetermineVertexAndTriangleCount();

	EmitterTime += EmitterDelay;
	RunningTime += DeltaTime;
	// LastTick = WorldTimeSeconds

	bFirstUpdate = false;
}

void FParticleRibbonEmitterInstance::KillParticles()
{
	if (ActiveParticles <= 0 || !ParticleData || !ParticleIndices || !Template || Template->TypeDataOffset == INDEX_NONE)
	{
		return;
	}

	bool bHasForceKillParticles = false;
	for(int32 ParticleIdx = ActiveParticles - 1; ParticleIdx >= 0; ParticleIdx--)
	{
		const int32 CurrentIndex = ParticleIndices[ParticleIdx];
	
		DECLARE_PARTICLE_PTR(Particle, ParticleData + ParticleStride * CurrentIndex);
		FRibbonTypeDataPayload* TrailData = ((FRibbonTypeDataPayload*)((uint8*)Particle + Template->TypeDataOffset));
		if(Particle->RelativeTime > 1.0f)
		{
			if(TRAIL_EMITTER_IS_HEAD(TrailData->Flags) || TRAIL_EMITTER_IS_ONLY(TrailData->Flags))
			{
				int32 Next = TRAIL_EMITTER_GET_NEXT(TrailData->Flags);
				if(Next != TRAIL_EMITTER_NULL_NEXT)
				{
					DECLARE_PARTICLE_PTR(NextParticle, ParticleData + ParticleStride * Next);
					FRibbonTypeDataPayload* NextTrailData = ((FRibbonTypeDataPayload*)((uint8*)NextParticle + Template->TypeDataOffset));
					if(TRAIL_EMITTER_IS_END(NextTrailData->Flags))
					{
						if(TRAIL_EMITTER_IS_START(TrailData->Flags))
						{
							NextTrailData->Flags = TRAIL_EMITTER_SET_ONLY(NextTrailData->Flags);							
						}
						else if(TRAIL_EMITTER_IS_DEADTRAIL(TrailData->Flags))
						{
							NextTrailData->Flags = TRAIL_EMITTER_SET_DEADTRAIL(NextTrailData->Flags);
						}
					}
					else
					{
						if(TRAIL_EMITTER_IS_START(TrailData->Flags))
						{
							NextTrailData->Flags = TRAIL_EMITTER_SET_START(NextTrailData->Flags);
						}
						else
						{
							NextTrailData->Flags = TRAIL_EMITTER_SET_DEADTRAIL(NextTrailData->Flags);
						}
					}
					NextTrailData->Flags = TRAIL_EMITTER_SET_PREV(NextTrailData->Flags, TRAIL_EMITTER_NULL_PREV);
				}
			}
			else if(TRAIL_EMITTER_IS_END(TrailData->Flags))
			{
				int32 Prev = TRAIL_EMITTER_GET_PREV(TrailData->Flags);
				if(Prev != TRAIL_EMITTER_NULL_PREV)
				{
					DECLARE_PARTICLE_PTR(PrevParticle, ParticleData + ParticleStride * Prev);
					FRibbonTypeDataPayload* PrevTrailData = ((FRibbonTypeDataPayload*)((uint8*)PrevParticle + Template->TypeDataOffset));
					if(TRAIL_EMITTER_IS_START(PrevTrailData->Flags))
					{
						PrevTrailData->Flags = TRAIL_EMITTER_SET_ONLY(PrevTrailData->Flags);							
					}
					else if(TRAIL_EMITTER_IS_DEADTRAIL(PrevTrailData->Flags))
					{
						// Nothing to do in this case.
						PrevTrailData->TriangleCount = 0;
						PrevTrailData->RenderingInterpCount = 1;
					}
					else
					{
						PrevTrailData->Flags = TRAIL_EMITTER_SET_END(PrevTrailData->Flags);							
					}
					PrevTrailData->Flags = TRAIL_EMITTER_SET_NEXT(PrevTrailData->Flags, TRAIL_EMITTER_NULL_NEXT);
				}
			}
			else if(TRAIL_EMITTER_IS_MIDDLE(TrailData->Flags))
			{
				int32	Next = TRAIL_EMITTER_GET_NEXT(TrailData->Flags);
				int32	Prev = TRAIL_EMITTER_GET_PREV(TrailData->Flags);

				if(Prev != TRAIL_EMITTER_NULL_PREV)
				{
					DECLARE_PARTICLE_PTR(PrevParticle, ParticleData + ParticleStride * Prev);
					FRibbonTypeDataPayload* PrevTrailData = ((FRibbonTypeDataPayload*)((uint8*)PrevParticle + Template->TypeDataOffset));
					if(TRAIL_EMITTER_IS_START(PrevTrailData->Flags))
					{
						PrevTrailData->Flags = TRAIL_EMITTER_SET_ONLY(PrevTrailData->Flags);							
					}
					else if(TRAIL_EMITTER_IS_DEADTRAIL(PrevTrailData->Flags))
					{
						// Nothing to do in this case.
						PrevTrailData->TriangleCount = 0;
						PrevTrailData->RenderingInterpCount = 1;
					}
					else
					{
						PrevTrailData->Flags = TRAIL_EMITTER_SET_END(PrevTrailData->Flags);							
					}
					PrevTrailData->Flags = TRAIL_EMITTER_SET_NEXT(PrevTrailData->Flags, TRAIL_EMITTER_NULL_NEXT);
				}

				while(Next != TRAIL_EMITTER_NULL_NEXT)
				{
					DECLARE_PARTICLE_PTR(NextParticle, ParticleData + ParticleStride * Next);
					FRibbonTypeDataPayload* NextTrailData = ((FRibbonTypeDataPayload*)((uint8*)NextParticle + Template->TypeDataOffset));
					NextTrailData->Flags = TRAIL_EMITTER_SET_FORCEKILL(NextTrailData->Flags);						
					Next = TRAIL_EMITTER_GET_NEXT(NextTrailData->Flags);
					bHasForceKillParticles = true;
				}
			}

			if(!TRAIL_EMITTER_IS_FORCEKILL(TrailData->Flags))
			{
				int32 Next = TRAIL_EMITTER_GET_NEXT(TrailData->Flags);
				if(Next != TRAIL_EMITTER_NULL_NEXT)
				{
					DECLARE_PARTICLE_PTR(NextParticle, ParticleData + ParticleStride * Next);
					FRibbonTypeDataPayload* NextTrailData = ((FRibbonTypeDataPayload*)((uint8*)NextParticle + Template->TypeDataOffset));
					NextTrailData->Flags = TRAIL_EMITTER_SET_PREV(NextTrailData->Flags, TRAIL_EMITTER_NULL_PREV);
				}
				int32 Prev = TRAIL_EMITTER_GET_PREV(TrailData->Flags);
				if(Prev != TRAIL_EMITTER_NULL_PREV)
				{
					DECLARE_PARTICLE_PTR(PrevParticle, ParticleData + ParticleStride * Prev);
					FRibbonTypeDataPayload* PrevTrailData = ((FRibbonTypeDataPayload*)((uint8*)PrevParticle + Template->TypeDataOffset));
					PrevTrailData->Flags = TRAIL_EMITTER_SET_NEXT(PrevTrailData->Flags, TRAIL_EMITTER_NULL_NEXT);
				}
			}

			TrailData->Flags = TRAIL_EMITTER_SET_NEXT(TrailData->Flags, TRAIL_EMITTER_NULL_NEXT);
			TrailData->Flags = TRAIL_EMITTER_SET_PREV(TrailData->Flags, TRAIL_EMITTER_NULL_PREV);

			ParticleIndices[ParticleIdx] = ParticleIndices[ActiveParticles - 1];
			ParticleIndices[ActiveParticles - 1] = CurrentIndex;
			ActiveParticles--;				
		}
	}

	if(bHasForceKillParticles == true)
	{
		for(int32 ParticleIdx = ActiveParticles - 1; ParticleIdx >= 0; ParticleIdx--)
		{
			const int32 CurrentIndex = ParticleIndices[ParticleIdx];
			DECLARE_PARTICLE_PTR(Particle, ParticleData + ParticleStride * CurrentIndex);
			FRibbonTypeDataPayload* TrailData = ((FRibbonTypeDataPayload*)((uint8*)Particle + Template->TypeDataOffset));
			if(TRAIL_EMITTER_IS_FORCEKILL(TrailData->Flags))
			{
				TrailData->Flags = TRAIL_EMITTER_SET_NEXT(TrailData->Flags, TRAIL_EMITTER_NULL_NEXT);
				TrailData->Flags = TRAIL_EMITTER_SET_PREV(TrailData->Flags, TRAIL_EMITTER_NULL_PREV);

				ParticleIndices[ParticleIdx] = ParticleIndices[ActiveParticles - 1];
				ParticleIndices[ActiveParticles - 1] = CurrentIndex;
				ActiveParticles--;					
			}
		}
	}
}

void FParticleRibbonEmitterInstance::KillParticle(int32 KillIndex)
{
	FParticleEmitterInstance::KillParticle(KillIndex);
}

float FParticleRibbonEmitterInstance::Tick_SpawnParticles(float DeltaTime, bool bFirstTime)
{
	return FParticleEmitterInstance::Tick_SpawnParticles(DeltaTime, bFirstTime);
}

float FParticleRibbonEmitterInstance::Spawn(float DeltaTime, float Rate, int32 BurstCount)
{
	UParticleLODLevel* LODLevel = GetCurrentLODLevel();
	if (!Template || !LODLevel || !TrailTypeData || Template->TypeDataOffset == INDEX_NONE)
	{
		return SpawnFraction;
	}

	int32 TrailIdx = 0;

	FParticleSpawnPlan Plan = ComputeSpawnPlan(SpawnFraction, Rate, DeltaTime);

	float MovementSpawnRate = 0.0f;
	int32 MovementSpawnCount = 0;
	float SpawnRate = Rate;
	int32 SpawnCount = 0;
	float OldLeftOver = SpawnFraction;

	const int32 LocalMaxParticleTrailCount = TrailTypeData->MaxParticleInTrailCount;
	float SafteyLeftover = OldLeftOver;
	int32 SpawnNumber = Plan.Count;
	float SliceIncrement = Plan.Increment;
	float SpawnStartTime = Plan.StartTime;
	SpawnFraction = Plan.NewFraction;

	int32 TotalCount = MovementSpawnCount + SpawnNumber + BurstCount;
	if (TotalCount <= 0)
	{
		return SpawnFraction;
	}
	bool bNoLivingParticles = (ActiveParticles == 0);

	// Don't allow more than TrailCount
	if(LocalMaxParticleTrailCount > 0)
	{
		int32 KillCount = (TotalCount + ActiveParticles) - LocalMaxParticleTrailCount;
		if(KillCount > 0)
		{
			KillParticles(TrailIdx, KillCount);
		}

		TotalCount = FMath::Min<int32>(TotalCount, LocalMaxParticleTrailCount);
	}

	// Handle growing arrays
	bool bProcessSpawn = true;
	int32 NewCount = ActiveParticles + TotalCount;
	if(NewCount > MaxActiveParticles)
	{
		bProcessSpawn = Resize(NewCount + FMath::TruncToInt(std::sqrt((float)NewCount)) + 1);
	}

	if(bProcessSpawn == false || !ParticleData || !ParticleIndices)
	{
		return SafteyLeftover;
	}

	const int32 FreeSlots = MaxActiveParticles - ActiveParticles;
	if (SpawnNumber > FreeSlots)
	{
		SpawnNumber = FreeSlots;
	}

	FBaseParticle* StartParticle = nullptr;
	int32 StartIndex = -1;
	FRibbonTypeDataPayload* StartTrailData = nullptr;
	// GetTrailStart
	for(int32 FindTrailIdx = 0; FindTrailIdx < ActiveParticles; FindTrailIdx++)
	{
		int32 CheckIndex = ParticleIndices[FindTrailIdx];
		DECLARE_PARTICLE_PTR(CheckParticle, ParticleData + ParticleStride * CheckIndex);	
		FRibbonTypeDataPayload* CheckTrailData = ((FRibbonTypeDataPayload*)((uint8*)CheckParticle + Template->TypeDataOffset));
		if(TRAIL_EMITTER_IS_START(CheckTrailData->Flags))
		{
			if(CheckTrailData->TrailIndex == TrailIdx)
			{
				StartIndex = CheckIndex;
				StartParticle = CheckParticle;
				StartTrailData = CheckTrailData;
				break;
			}
		}
	}

	bNoLivingParticles = (StartParticle == nullptr);
	bool bTilingTrail = !FMath::IsNearlyZero(TrailTypeData->TilingDistance);

	float ElapsedTiem = RunningTime;
	if((SpawnRate > 0.0f) && (SpawnNumber > 0))
	{
		float Increment = (SpawnRate > 0.0f) ? (1.f / SpawnRate) : 0.0f;
		float StartTime = DeltaTime + OldLeftOver * Increment - Increment;

		// Temp : Up is Z-Up
		FVector CurrentUp = FVector(0.0f, 0.0f, 1.0f);

		float InvCount = 1.0f / SpawnNumber;

		// Spawn loop
		for(int32 SpawnIdx = 0; SpawnIdx < SpawnNumber; SpawnIdx++)
		{
			int32 ParticleIndex = ParticleIndices[ActiveParticles];
			if (ParticleIndex < 0 || ParticleIndex >= MaxActiveParticles)
			{
				break;
			}
			DECLARE_PARTICLE_PTR(Particle, ParticleData + ParticleStride * ParticleIndex);
			FRibbonTypeDataPayload* TrailData = ((FRibbonTypeDataPayload*)((uint8*)Particle + Template->TypeDataOffset));

			float SpawnTime = StartTime - SpawnIdx * Increment;
			float TimeStep = FMath::Clamp<float>(InvCount * (SpawnIdx + 1), 0.0f, 1.0f);
			float StoredSpawnTime = DeltaTime * TimeStep;

			PreSpawn(Particle);
			if(LODLevel->TypeDataModule)
			{
				LODLevel->TypeDataModule->Spawn({ *this, Template->TypeDataOffset, SpawnTime, Particle });
			}

			for(int32 ModuleIndex = 0; ModuleIndex < LODLevel->SpawnModules.Num(); ModuleIndex++)
			{
				UParticleModule* SpawnModule = LODLevel->SpawnModules[ModuleIndex];
				if(SpawnModule && SpawnModule->bEnabled)
				{
					SpawnModule->Spawn({ *this, Template->GetModuleDataOffset(SpawnModule), SpawnTime, Particle });
				}
			}
			PostSpawn(Particle, SpawnTime);

			if (Particle->RelativeTime > 1.0f)
			{
				continue;
			}

			//GetParticleLifetimeandSize
			Particle->Size.Y = Particle->Size.X;
			Particle->BaseSize = Particle->Size;

			TrailData->Flags = TRAIL_EMITTER_SET_NEXT(TrailData->Flags, TRAIL_EMITTER_NULL_NEXT);
			TrailData->Flags = TRAIL_EMITTER_SET_PREV(TrailData->Flags, TRAIL_EMITTER_NULL_PREV);

			TrailData->TrailIndex = TrailIdx;
			TrailData->Tangent = -Particle->Velocity * DeltaTime;
			TrailData->SpawnTime = ElapsedTiem + StoredSpawnTime;
			TrailData->SpawnDelta = SpawnIdx * Increment;

			TrailData->Up = CurrentUp;

			TrailData->bMovementSpawned = false;

			TrailData->bInterpolatedSpawn = false;
			TrailData->SpawnedTessellationPoints = 1;

			bool bAddedParticle = false;
			if(bNoLivingParticles)
			{
				TrailData->Flags = TRAIL_EMITTER_SET_ONLY(TrailData->Flags);
				TiledUDistanceTraveled[TrailIdx] = 0.0f;
				TrailData->TiledU = 0.0f;
				bNoLivingParticles = false;
				bAddedParticle = true;
			}
			else if(StartParticle)
			{
				bAddedParticle = AddParticleHelper(TrailIdx, StartIndex, (FRibbonTypeDataPayload*)StartTrailData, ParticleIndex, (FRibbonTypeDataPayload*)TrailData);
			}

			if(bAddedParticle)
			{
				if(bTilingTrail == true)
				{
					if(StartParticle == nullptr)
					{
						TrailData->TiledU = 0.0f;
					}
					else
					{
						FVector PositionDelta = Particle->Location - StartParticle->Location;
						TiledUDistanceTraveled[TrailIdx] += PositionDelta.Size();
						TrailData->TiledU = TiledUDistanceTraveled[TrailIdx] / TrailTypeData->TilingDistance;
					}
				}

				StartParticle = Particle;
				StartIndex = ParticleIndex;
				StartTrailData = TrailData;

				ActiveParticles++;

				if(StartTrailData->Tangent.IsNearlyZero())
				{
					FBaseParticle* NextSpawnedParticle = nullptr;
					FRibbonTypeDataPayload* NextSpawnedTrailData = nullptr;
					FRibbonTypeDataPayload* TempPayload = nullptr;
					GetParticleInTrail(true, StartParticle, StartTrailData, GET_Next, GET_Spawned, NextSpawnedParticle, TempPayload);
					NextSpawnedTrailData = (FRibbonTypeDataPayload*)(TempPayload);
					if(NextSpawnedParticle != nullptr)
					{
						const FVector PositionDelta = (StartParticle->Location - NextSpawnedParticle->Location);
						const float TimeDelta = StartTrailData->SpawnTime - NextSpawnedTrailData->SpawnTime;
						if (std::fabs(TimeDelta) > 1e-6f)
						{
							StartTrailData->Tangent = PositionDelta;
						}
					}
				}
				
				if((TrailTypeData->bEnablePreviousTangentRecalculation == true) && (TrailTypeData->bTangentRecalculationEveryFrame == false))
				{
					FBaseParticle* NextSpawnedParticle = nullptr;
					FRibbonTypeDataPayload* NextSpawnedTrailData = nullptr;

					FBaseParticle* NextNextSpawnedParticle = nullptr;
					FRibbonTypeDataPayload* NextNextSpawnedTrailData = nullptr;

					FRibbonTypeDataPayload* TempPayload = nullptr;

					GetParticleInTrail(true, StartParticle, StartTrailData, GET_Next, GET_Spawned, NextSpawnedParticle, TempPayload);
					NextSpawnedTrailData = (FRibbonTypeDataPayload*)(TempPayload);
					GetParticleInTrail(true, NextSpawnedParticle, NextSpawnedTrailData, GET_Next, GET_Spawned, NextNextSpawnedParticle, TempPayload);
					NextNextSpawnedTrailData = (FRibbonTypeDataPayload*)(TempPayload);


					if(NextSpawnedParticle != nullptr)
					{
						if(NextNextSpawnedParticle != nullptr)
						{
							const FVector PositionDelta(StartParticle->Location - NextNextSpawnedParticle->Location);
							const float TimeDelta = StartTrailData->SpawnTime - NextNextSpawnedTrailData->SpawnTime;
							const float SegmentDelta = NextSpawnedTrailData->SpawnTime - NextNextSpawnedTrailData->SpawnTime;
							if (std::fabs(TimeDelta) > 1e-6f)
							{
								NextSpawnedTrailData->Tangent = (PositionDelta / TimeDelta) * SegmentDelta;
							}
						}
						else
						{
							const FVector PositionDelta = (StartParticle->Location - NextSpawnedParticle->Location);
							const float TimeDelta = StartTrailData->SpawnTime - NextSpawnedTrailData->SpawnTime;
							if (std::fabs(TimeDelta) > 1e-6f)
							{
								NextSpawnedTrailData->Tangent = PositionDelta;
							}
						}
					}
				}

				TrailSpawnTimes[0] = TrailData->SpawnTime;
			}
		}
	}

	return SpawnFraction;
}

void FParticleRibbonEmitterInstance::PreSpawn(FBaseParticle* Particle)
{
	FParticleEmitterInstance::PreSpawn(Particle);

	if(Template && Template->TypeDataOffset != INDEX_NONE)
	{
		FRibbonTypeDataPayload* TrailData = ((FRibbonTypeDataPayload*)((uint8*)Particle + Template->TypeDataOffset));
		TrailData->Flags = TRAIL_EMITTER_SET_NEXT(TrailData->Flags, TRAIL_EMITTER_NULL_NEXT);
		TrailData->Flags = TRAIL_EMITTER_SET_PREV(TrailData->Flags, TRAIL_EMITTER_NULL_PREV);
	}
}

void FParticleRibbonEmitterInstance::PostSpawn(FBaseParticle* Particle, float SpawnTime)
{
	FParticleEmitterInstance::PostSpawn(Particle, SpawnTime);
}

void FParticleRibbonEmitterInstance::DestroyRenderResources()
{
	RenderVertexBuffer.reset();
	RenderIndexBuffer.reset();
	VertexBufferCapacity = 0;
	IndexBufferCapacity = 0;

	StagingVertices.Empty();
	StagingIndices.Empty();
}

bool FParticleRibbonEmitterInstance::EnsureRenderResources(int32 NeededVertices, int32 NeededIndices)
{
	if (NeededVertices <= 0 || NeededIndices <= 0)
	{
		return false;
	}

	if (!RenderVertexBuffer || NeededVertices > VertexBufferCapacity)
	{
		const int32 NewVertexCapacity = GrowRibbonCapacity(VertexBufferCapacity, NeededVertices, 128);
		RenderVertexBuffer = RenderCommand::CreateDynamicVertexBuffer(
			sizeof(FParticleSpriteVertex) * static_cast<size_t>(NewVertexCapacity),
			sizeof(FParticleSpriteVertex));
		if (!RenderVertexBuffer)
		{
			VertexBufferCapacity = 0;
			return false;
		}
		VertexBufferCapacity = NewVertexCapacity;
	}

	if (!RenderIndexBuffer || NeededIndices > IndexBufferCapacity)
	{
		const int32 NewIndexCapacity = GrowRibbonCapacity(IndexBufferCapacity, NeededIndices, 384);
		RenderIndexBuffer = RenderCommand::CreateDynamicIndexBuffer(static_cast<uint32>(NewIndexCapacity));
		if (!RenderIndexBuffer)
		{
			IndexBufferCapacity = 0;
			return false;
		}
		IndexBufferCapacity = NewIndexCapacity;
	}

	return RenderVertexBuffer != nullptr && RenderIndexBuffer != nullptr;
}

bool FParticleRibbonEmitterInstance::SubmitRender(FRenderQueue& RenderQueue, const FViewContext& ViewContext, float ComponentDistanceSquared)
{
	if (!TrailTypeData || !Template || Template->TypeDataOffset == INDEX_NONE)
	{
		return false;
	}

	// 리본 파티클이 2개 미만이거나 그릴 세그먼트가 없을 때도 Sprite 폴백을 막기 위해 true를 반환한다.
	if (!TrailTypeData->bRenderGeometry || ActiveParticles < 2 || !ParticleData || !ParticleIndices || VertexCount <= 0 || TrailCount <= 0)
	{
		return true;
	}

	StagingVertices.Reset();
	StagingIndices.Reset();
	StagingVertices.Reserve(VertexCount);
	if (TriangleCount > 0)
	{
		StagingIndices.Reserve(TriangleCount * 3);
	}

	const FParticleSpriteCamera Camera = FParticleSpriteCamera::FromView(ViewContext.CameraPosition, ViewContext.CameraForward);
	const bool bLocalSpace = SimulatesInLocalSpace();
	const bool bTextureTileDistance = !FMath::IsNearlyZero(TrailTypeData->TilingDistance);
	const int32 TypeDataOffset = Template->TypeDataOffset;

	auto ToWorldPos = [this, bLocalSpace](const FVector& Pos) -> FVector
	{
		return bLocalSpace ? SimulationToWorld.TransformPosition(Pos) : Pos;
	};
	auto ToWorldDir = [this, bLocalSpace](const FVector& Dir) -> FVector
	{
		return bLocalSpace ? SimulationToWorld.TransformVectorNoScale(Dir).GetSafeNormal() : Dir.GetSafeNormal();
	};
	auto ComputeWorkingUp = [&ViewContext, &Camera, &ToWorldPos, &ToWorldDir](
		const FVector& SimPos, const FVector& SimTangent, const FVector& SimUp) -> FVector
	{
		const FVector WorldPos = ToWorldPos(SimPos);
		const FVector DirToCamera = (WorldPos - ViewContext.CameraPosition).GetSafeNormal();
		const FVector WorldTangent = ToWorldDir(SimTangent);
		FVector WorkingUp = FVector::Cross(WorldTangent, DirToCamera).GetSafeNormal();
		if (WorkingUp.IsNearlyZero())
		{
			WorkingUp = ToWorldDir(SimUp);
		}
		if (WorkingUp.IsNearlyZero())
		{
			WorkingUp = Camera.Up;
		}
		return WorkingUp;
	};
	auto EmitSlice = [this](const FVector& WorldPos, const FVector& WorldUp, float HalfWidth, float U, const FLinearColor& Color)
	{
		FParticleSpriteVertex TopVertex;
		TopVertex.Position = WorldPos + WorldUp * HalfWidth;
		TopVertex.UV = FVector2(U, 0.0f);
		TopVertex.Color = Color;
		StagingVertices.Add(TopVertex);

		FParticleSpriteVertex BottomVertex;
		BottomVertex.Position = WorldPos - WorldUp * HalfWidth;
		BottomVertex.UV = FVector2(U, 1.0f);
		BottomVertex.Color = Color;
		StagingVertices.Add(BottomVertex);
	};

	for (int32 ParticleIdx = 0; ParticleIdx < ActiveParticles; ++ParticleIdx)
	{
		const int32 HeadIndex = ParticleIndices[ParticleIdx];
		if (HeadIndex < 0 || HeadIndex >= MaxActiveParticles)
		{
			continue;
		}

		DECLARE_PARTICLE_PTR(Particle, ParticleData + ParticleStride * HeadIndex);
		FRibbonTypeDataPayload* TrailPayload = reinterpret_cast<FRibbonTypeDataPayload*>(reinterpret_cast<uint8*>(Particle) + TypeDataOffset);
		if (TRAIL_EMITTER_IS_HEAD(TrailPayload->Flags) == 0)
		{
			continue;
		}
		if (TRAIL_EMITTER_GET_NEXT(TrailPayload->Flags) == TRAIL_EMITTER_NULL_NEXT || TrailPayload->TriangleCount <= 0)
		{
			continue;
		}

		const int32 TrailStartVertex = StagingVertices.Num();
		const float SegmentCountFloat = static_cast<float>(TrailPayload->TriangleCount) * 0.5f;
		const float TextureIncrement = (SegmentCountFloat > 0.0f) ? (1.0f / SegmentCountFloat) : 1.0f;
		float Tex_U = 0.0f;

		FBaseParticle* PackingParticle = Particle;
		FBaseParticle* PrevParticle = nullptr;
		FRibbonTypeDataPayload* PrevTrailPayload = nullptr;
		FVector PrevWorkingUp = Camera.Up;

		FVector InitialTangent = TrailPayload->Tangent;
		if (InitialTangent.IsNearlyZero())
		{
			const int32 FirstNextIdx = TRAIL_EMITTER_GET_NEXT(TrailPayload->Flags);
			if (FirstNextIdx >= 0 && FirstNextIdx < MaxActiveParticles)
			{
				DECLARE_PARTICLE_PTR(NextParticle, ParticleData + ParticleStride * FirstNextIdx);
				InitialTangent = PackingParticle->Location - NextParticle->Location;
			}
		}
		FVector WorkingUp = ComputeWorkingUp(PackingParticle->Location, InitialTangent, TrailPayload->Up);

		int32 Steps = 0;
		while (TrailPayload && Steps < MaxActiveParticles)
		{
			++Steps;

			const float CurrSize = std::fabs(PackingParticle->Size.X);
			const int32 InterpCount = TrailPayload->RenderingInterpCount;
			if (InterpCount > 1 && PrevParticle && PrevTrailPayload)
			{
				const FVector CurrPosition = PackingParticle->Location;
				const FVector CurrTangent = TrailPayload->Tangent;
				const FVector CurrUp = WorkingUp;
				const FLinearColor CurrColor = PackingParticle->Color;

				const FVector PrevPosition = PrevParticle->Location;
				const FVector PrevTangent = PrevTrailPayload->Tangent;
				const FVector PrevUp = PrevWorkingUp;
				const FLinearColor PrevColor = PrevParticle->Color;
				const float PrevSize = std::fabs(PrevParticle->Size.X);

				const float InvCount = 1.0f / static_cast<float>(InterpCount);
				for (int32 SpawnIdx = InterpCount - 1; SpawnIdx >= 0; --SpawnIdx)
				{
					const float TimeStep = InvCount * static_cast<float>(SpawnIdx);
					const FVector InterpSimPos = FMath::CubicInterp<FVector>(CurrPosition, CurrTangent, PrevPosition, PrevTangent, TimeStep);
					const FVector InterpWorldPos = ToWorldPos(InterpSimPos);

					FVector InterpUp = FMath::Lerp<FVector>(CurrUp, PrevUp, TimeStep).GetSafeNormal();
					if (InterpUp.IsNearlyZero())
					{
						InterpUp = CurrUp;
					}

					const FLinearColor InterpColor = LerpLinearColor(CurrColor, PrevColor, TimeStep);
					const float InterpSize = FMath::Lerp<float>(CurrSize, PrevSize, TimeStep);
					const float CurrTileU = bTextureTileDistance
						? FMath::Lerp<float>(TrailPayload->TiledU, PrevTrailPayload->TiledU, TimeStep)
						: Tex_U;

					EmitSlice(InterpWorldPos, InterpUp, InterpSize, CurrTileU, InterpColor);
					Tex_U += TextureIncrement;
				}
			}
			else
			{
				const FVector WorldPos = ToWorldPos(PackingParticle->Location);
				const float CurrTileU = bTextureTileDistance ? TrailPayload->TiledU : Tex_U;
				EmitSlice(WorldPos, WorkingUp, CurrSize, CurrTileU, PackingParticle->Color);
				Tex_U += TextureIncrement;
			}

			PrevParticle = PackingParticle;
			PrevTrailPayload = TrailPayload;
			PrevWorkingUp = WorkingUp;

			const int32 NextIdx = TRAIL_EMITTER_GET_NEXT(TrailPayload->Flags);
			if (NextIdx == TRAIL_EMITTER_NULL_NEXT || NextIdx < 0 || NextIdx >= MaxActiveParticles)
			{
				TrailPayload = nullptr;
				PackingParticle = nullptr;
			}
			else
			{
				DECLARE_PARTICLE_PTR(NextParticle, ParticleData + ParticleStride * NextIdx);
				PackingParticle = NextParticle;
				TrailPayload = reinterpret_cast<FRibbonTypeDataPayload*>(reinterpret_cast<uint8*>(NextParticle) + TypeDataOffset);

				FVector FallbackTangent = TrailPayload->Tangent;
				if (FallbackTangent.IsNearlyZero() && PrevParticle)
				{
					FallbackTangent = PrevParticle->Location - PackingParticle->Location;
				}
				WorkingUp = ComputeWorkingUp(PackingParticle->Location, FallbackTangent, TrailPayload->Up);
			}
		}

		const int32 TrailVertexCount = StagingVertices.Num() - TrailStartVertex;
		const int32 SliceCount = TrailVertexCount / 2;
		for (int32 Slice = 0; Slice + 1 < SliceCount; ++Slice)
		{
			const uint32 V0 = static_cast<uint32>(TrailStartVertex + Slice * 2);
			const uint32 V1 = V0 + 1;
			const uint32 V2 = V0 + 2;
			const uint32 V3 = V0 + 3;
			StagingIndices.Add(V0);
			StagingIndices.Add(V1);
			StagingIndices.Add(V2);
			StagingIndices.Add(V1);
			StagingIndices.Add(V3);
			StagingIndices.Add(V2);
		}
	}

	if (StagingVertices.Num() < 4 || StagingIndices.Num() < 3)
	{
		return true;
	}

	UMaterial* Material = GetParticleRenderMaterial(*this, ResourcePaths::ParticleSpriteMaterial);
	if (!Material || !EnsureRenderResources(StagingVertices.Num(), StagingIndices.Num()))
	{
		return true;
	}

	RenderCommand::UpdateBufferData(
		RenderVertexBuffer.get(),
		StagingVertices.GetData(),
		static_cast<uint32>(sizeof(FParticleSpriteVertex) * StagingVertices.Num()));
	RenderCommand::UpdateBufferData(
		RenderIndexBuffer.get(),
		StagingIndices.GetData(),
		static_cast<uint32>(sizeof(uint32) * StagingIndices.Num()));

	FRenderPacket Packet;
	Packet.Model = RenderQueue.StoreWorldMatrix(FMatrix::Identity);
	Packet.DynamicVertexBuffer = RenderVertexBuffer.get();
	Packet.DynamicIndexBuffer = RenderIndexBuffer.get();
	Packet.Material = Material;
	Packet.IndexCount = static_cast<uint32>(StagingIndices.Num());
	Packet.CameraToParticleDistance = ComponentDistanceSquared;
	RenderQueue.Add(Packet);
	return true;
}

bool FParticleRibbonEmitterInstance::ExpandWorldBounds(FBox& InOutBox, bool& bInOutHasAny) const
{
	return ExpandSpriteWorldBounds(*this, InOutBox, bInOutHasAny);
}

void FParticleRibbonEmitterInstance::UpdateSourceData(float DeltaTime, bool bFirstTime)
{
	(void)DeltaTime;

	FVector Position;
	FQuat Rotation;
	FVector Up;
	FVector Tangent;
	float TangentStrength = 0.0f;

	const float ElapsedTime = RunningTime;
	const int32 NumTrails = CurrentSourcePosition.Num();
	for (int32 TrailIdx = 0; TrailIdx < MaxTrailCount && TrailIdx < NumTrails; ++TrailIdx)
	{
		if (ResolveSourcePoint(TrailIdx, Position, Rotation, Up, Tangent, TangentStrength))
		{
			if (bFirstTime)
			{
				LastSourcePosition[TrailIdx] = Position;
				LastSourceTangent[TrailIdx] = FVector::ZeroVector;
				LastSourceTangentStrength[TrailIdx] = TangentStrength;
				LastSourceUp[TrailIdx] = Up;
				TrailSpawnTimes[TrailIdx] = RunningTime;
			}

			CurrentSourcePosition[TrailIdx] = Position;
			CurrentSourceRotation[TrailIdx] = Rotation;
			const float ElapsedTimeSinceSpawned = ElapsedTime - TrailSpawnTimes[TrailIdx];
			if (ElapsedTimeSinceSpawned > 1e-6f)
			{
				CurrentSourceTangent[TrailIdx] = (CurrentSourcePosition[TrailIdx] - LastSourcePosition[TrailIdx]) / ElapsedTimeSinceSpawned;
			}
			else
			{
				CurrentSourceTangent[TrailIdx] = FVector(1.0f, 0.0f, 0.0f);
			}
			CurrentSourceTangentStrength[TrailIdx] = TangentStrength;
			CurrentSourceUp[TrailIdx] = Up;

			if (bFirstTime)
			{
				LastSourceRotation[TrailIdx] = CurrentSourceRotation[TrailIdx];
			}
		}
	}
}

bool FParticleRibbonEmitterInstance::ResolveSourcePoint(int32 InTrailIdx, FVector& OutPosition, FQuat& OutRotation, FVector& OutUp, FVector& OutTangent, float& OutTangentStrength)
{
	OutPosition = EmitterToSimulation.TransformPosition(FVector::ZeroVector);
	if (SourceOffsets.IsValidIndex(InTrailIdx))
	{
		OutPosition += EmitterToSimulation.TransformVectorNoScale(SourceOffsets[InTrailIdx]);
	}

	OutRotation = FQuat(MatrixToRotator(EmitterToSimulation));
	OutUp = EmitterToSimulation.TransformVectorNoScale(FVector(0.0f, 0.0f, 1.0f)).GetSafeNormal();
	OutTangent = EmitterToSimulation.TransformVectorNoScale(FVector(1.0f, 0.0f, 0.0f)).GetSafeNormal();
	OutTangentStrength = OutTangent.SizeSquared();
	return true;
}

bool FParticleRibbonEmitterInstance::AddParticleHelper(int32 InTrailIdx, int32 StartParticleIndex, FRibbonTypeDataPayload* StartTrailData, int32 ParticleIndex, FRibbonTypeDataPayload* TrailData)
{
	bool bAddedParticle = false;

	TrailData->TrailIndex = InTrailIdx;
	if(TRAIL_EMITTER_IS_ONLY(StartTrailData->Flags))
	{
		StartTrailData->Flags = TRAIL_EMITTER_SET_END(StartTrailData->Flags);
		StartTrailData->Flags = TRAIL_EMITTER_SET_NEXT(StartTrailData->Flags, TRAIL_EMITTER_NULL_NEXT);
		StartTrailData->Flags = TRAIL_EMITTER_SET_PREV(StartTrailData->Flags, ParticleIndex);

		TrailData->Flags = TRAIL_EMITTER_SET_PREV(TrailData->Flags, TRAIL_EMITTER_NULL_PREV);
		TrailData->Flags = TRAIL_EMITTER_SET_NEXT(TrailData->Flags, StartParticleIndex);
		TrailData->Flags = TRAIL_EMITTER_SET_START(TrailData->Flags);

		bAddedParticle = true;
	}
	else
	{
		StartTrailData->Flags = TRAIL_EMITTER_SET_MIDDLE(StartTrailData->Flags);
		StartTrailData->Flags = TRAIL_EMITTER_SET_PREV(StartTrailData->Flags, ParticleIndex);

		TrailData->Flags = TRAIL_EMITTER_SET_PREV(TrailData->Flags, TRAIL_EMITTER_NULL_PREV);
		TrailData->Flags = TRAIL_EMITTER_SET_NEXT(TrailData->Flags, StartParticleIndex);
		TrailData->Flags = TRAIL_EMITTER_SET_START(TrailData->Flags);

		bAddedParticle = true;
	}

	return bAddedParticle;
}

void FParticleRibbonEmitterInstance::Tick_RecalculateTangents(float DeltaTime, UParticleLODLevel* CurrentLODLevel)
{
	if (!TrailTypeData || !ParticleData || !ParticleIndices || !Template || Template->TypeDataOffset == INDEX_NONE)
	{
		return;
	}
	if (TrailTypeData->bTangentRecalculationEveryFrame == true)
	{
		for (int32 TrailIdx = 0; TrailIdx < MaxTrailCount; TrailIdx++)
		{
			// Find the Start particle of the current trail...
			FBaseParticle* StartParticle = nullptr;
			FRibbonTypeDataPayload* StartTrailData = nullptr;
			int32 StartIndex = -1;
			// GetTrailStart
			for(int32 FindTrailIdx = 0; FindTrailIdx < ActiveParticles; FindTrailIdx++)
			{
				int32 CheckIndex = ParticleIndices[FindTrailIdx];
				DECLARE_PARTICLE_PTR(CheckParticle, ParticleData + ParticleStride * CheckIndex);
				FRibbonTypeDataPayload* CheckTrailData = ((FRibbonTypeDataPayload*)((uint8*)CheckParticle + Template->TypeDataOffset));
				if(TRAIL_EMITTER_IS_START(CheckTrailData->Flags))
				{
					if(CheckTrailData->TrailIndex == TrailIdx)
					{
						StartIndex = CheckIndex;
						StartParticle = CheckParticle;
						StartTrailData = CheckTrailData;
						break;
					}
				}
			}

			// Recalculate tangents at each particle to properly handle moving particles...
			if ((StartParticle != nullptr) && (TRAIL_EMITTER_IS_ONLY(StartTrailData->Flags) == 0))
			{
				// For trails, particles go:
				//     START, next, next, ..., END
				// Coming from the end,
				//     END, prev, prev, ..., START
				FBaseParticle* PrevParticle = StartParticle;
				FRibbonTypeDataPayload* PrevTrailData = StartTrailData;
				FBaseParticle* CurrParticle = nullptr;
				FRibbonTypeDataPayload* CurrTrailData = nullptr;
				FBaseParticle* NextParticle = nullptr;
				FRibbonTypeDataPayload* TempPayload = nullptr;
				FRibbonTypeDataPayload* NextTrailData = nullptr;

				GetParticleInTrail(true, PrevParticle, PrevTrailData, GET_Next, GET_Any, CurrParticle, TempPayload);
				CurrTrailData = (FRibbonTypeDataPayload*)(TempPayload);

				// Deal with the start particle...
				if (CurrParticle != nullptr)
				{
					TrailsBase_CalculateTangent(PrevParticle, PrevTrailData, CurrParticle, CurrTrailData, 
						(PrevTrailData->SpawnTime - CurrTrailData->SpawnTime), 
						PrevTrailData);
				}

				int32 Steps = 0;
				while (CurrParticle != nullptr && Steps < MaxActiveParticles)
				{
					++Steps;
					// Grab the next particle in the trail...
					GetParticleInTrail(true, CurrParticle, CurrTrailData, GET_Next, GET_Any, NextParticle, TempPayload);
					NextTrailData = (FRibbonTypeDataPayload*)(TempPayload);

					if (NextParticle != nullptr)
					{
						TrailsBase_CalculateTangent(PrevParticle, PrevTrailData, NextParticle, NextTrailData, 
							(CurrTrailData->SpawnTime - NextTrailData->SpawnTime),
							CurrTrailData);
					}
					else
					{
						// The start particle... should we recalc w/ the current source position???
						TrailsBase_CalculateTangent(PrevParticle, PrevTrailData, CurrParticle, CurrTrailData, 
							(PrevTrailData->SpawnTime - CurrTrailData->SpawnTime),
							CurrTrailData);
					}

					// Move up the chain...
					PrevParticle = CurrParticle;
					PrevTrailData = CurrTrailData;
					CurrParticle = NextParticle;
					CurrTrailData = NextTrailData;
				}
			}
		}
	}
}

void FParticleRibbonEmitterInstance::KillParticles(int32 InTrailIdx, int32 InKillCount)
{
	if(ActiveParticles)
	{
		int32 KilledCount = 0;

		{
			// Find the end particle
			FRibbonTypeDataPayload* EndTrailData = nullptr;
			FBaseParticle* EndParticle = nullptr;
			int32 EndIndex;		
			for(int32 FindTrailIdx = 0; FindTrailIdx < ActiveParticles; FindTrailIdx++)
			{
				int32 CheckIndex = ParticleIndices[FindTrailIdx];
				DECLARE_PARTICLE_PTR(CheckParticle, ParticleData + ParticleStride * CheckIndex);
				FRibbonTypeDataPayload* CheckTrailData = ((FRibbonTypeDataPayload*)((uint8*)CheckParticle + Template->TypeDataOffset));
				if(TRAIL_EMITTER_IS_END(CheckTrailData->Flags))
				{
					if(CheckTrailData->TrailIndex == InTrailIdx)
					{
						EndIndex = CheckIndex;
						EndParticle = CheckParticle;
						EndTrailData = CheckTrailData;
						break;
					}
				}
			}
			if(EndParticle)
			{
				if(EndTrailData && EndTrailData->TrailIndex == InTrailIdx)
				{
					while((EndTrailData != NULL) && (KilledCount < InKillCount))
					{
						// Mark it for death...
						EndParticle->RelativeTime = 1.1f;
						KilledCount++;
						// See if there is a 'prev'
						int32 Prev = TRAIL_EMITTER_GET_PREV(EndTrailData->Flags);
						if(Prev != TRAIL_EMITTER_NULL_PREV && Prev >= 0 && Prev < MaxActiveParticles)
						{
							EndParticle = (FBaseParticle*)(ParticleData + (ParticleStride * Prev));
							EndTrailData = ((FRibbonTypeDataPayload*)((uint8*)EndParticle + Template->TypeDataOffset));
							if(TRAIL_EMITTER_IS_START(EndTrailData->Flags))
							{
								// Don't kill the start, no matter what...
								EndTrailData = NULL;
							}
							else if(TRAIL_EMITTER_IS_DEADTRAIL(EndTrailData->Flags))
							{
								// Nothing to do in this case.
								EndTrailData->TriangleCount = 0;
								EndTrailData->RenderingInterpCount = 1;
							}
						}
						else
						{
							EndTrailData = NULL;
						}
					}

					if(EndTrailData == NULL)
					{
						// Force it to exit the loop...
						KilledCount = InKillCount;
					}
				}
			}
		}
		if(KilledCount > 0)
		{
			// Now use the standard KillParticles call...
			KillParticles();
		}
	}
}

void FParticleRibbonEmitterInstance::DetermineVertexAndTriangleCount()
{
	VertexCount = 0;
	TriangleCount = 0;
	TrailCount = 0;
	HeadOnlyParticles = 0;

	if (!ParticleData || !ParticleIndices || !Template || Template->TypeDataOffset == INDEX_NONE || !TrailTypeData || ActiveParticles <= 0)
	{
		return;
	}

	int32 Sheets = 1;
	int32 TheTrailCount = 0;
	int32 IndexCount = 0;

	int32 TempVertexCount;
	bool bApplyDistanceTEsselation = !FMath::IsNearlyZero(TrailTypeData->DistanceTessellationStepSize);
	float DistTessStep = TrailTypeData->DistanceTessellationStepSize;
	const float ScaleStepFactor = 0.5f;
	bool bScaleTessellation = TrailTypeData->bEnableTangentDiffInterpScale;

	float DistDiff = 0.0f;
	float CheckTangent = 0.0f;
	bool bCheckTangentValue = !FMath::IsNearlyZero(TrailTypeData->TangentTessellationScalar) || bScaleTessellation;

	for(int32 ii = 0; ii < ActiveParticles; ii++)
	{
		int32 LocalIndexCount = 0;
		int32 ParticleCount = 0;
		int32 LocalVertexCount = 0;

		bool bProcessParticle = false;
		const int32 ParticleIndex = ParticleIndices[ii];
		if (ParticleIndex < 0 || ParticleIndex >= MaxActiveParticles)
		{
			continue;
		}

		DECLARE_PARTICLE_PTR(Particle, ParticleData + ParticleStride * ParticleIndex);
		FBaseParticle* CurrParticle = Particle;
		FRibbonTypeDataPayload* CurrTrailData = ((FRibbonTypeDataPayload*)((uint8*)Particle + Template->TypeDataOffset));
		if(TRAIL_EMITTER_IS_HEADONLY(CurrTrailData->Flags))
		{
			CurrTrailData->RenderingInterpCount = 0;
			CurrTrailData->TriangleCount = 0;
			++HeadOnlyParticles;
		}
		else if(TRAIL_EMITTER_IS_END(CurrTrailData->Flags))
		{
			FBaseParticle* PrevParticle = nullptr;
			FRibbonTypeDataPayload* PrevTrailData = nullptr;
			int32 Prev = TRAIL_EMITTER_GET_PREV(CurrTrailData->Flags);
			if(Prev != TRAIL_EMITTER_NULL_PREV && Prev >= 0 && Prev < MaxActiveParticles)
			{
				DECLARE_PARTICLE_PTR(InnerParticle, ParticleData + ParticleStride * Prev);
				PrevParticle = InnerParticle;
				PrevTrailData = ((FRibbonTypeDataPayload*)((uint8*)InnerParticle + Template->TypeDataOffset));

				bool bDone = false;
				int32 Steps = 0;
				while(!bDone && Steps < MaxActiveParticles)
				{
					++Steps;
					ParticleCount++;

					float CheckDistance = (CurrParticle->Location - PrevParticle->Location).Size();
					FVector SrcTangent = CurrTrailData->Tangent.GetSafeNormal();
					FVector PrevTangent = PrevTrailData->Tangent.GetSafeNormal();
					if(bCheckTangentValue == true)
					{
						CheckTangent = (SrcTangent | PrevTangent);
						CheckTangent = (CheckTangent - 1.0f) * -0.5f;
					}

					if(bApplyDistanceTEsselation == true)
					{
						DistDiff = CheckDistance / DistTessStep;
						if(bScaleTessellation && (CheckTangent < ScaleStepFactor))
						{
							DistDiff *= (2.0f * FMath::Clamp<float>(CheckTangent, 0.0f, 0.5f));
						}
					}

					float TangDiff = CheckTangent * TrailTypeData->TangentTessellationScalar;
					const int32 MaxTessellation = FMath::Clamp(TrailTypeData->MaxTessellationBetweenParticles, 1, 65535);
					const int32 DistInterpCount = FMath::Min(FMath::Max(0, FMath::TruncToInt(DistDiff)), MaxTessellation);
					const int32 TangInterpCount = FMath::Min(FMath::Max(0, FMath::TruncToInt(TangDiff)), MaxTessellation);
					int32 InterpCount = FMath::Clamp(DistInterpCount + TangInterpCount, 1, MaxTessellation);

					CurrTrailData->RenderingInterpCount = InterpCount;
					if(CheckTangent <= 0.5f)
					{
						CurrTrailData->PinchScaleFactor = 1.0f;
					}
					else
					{
						CurrTrailData->PinchScaleFactor = 1.0f - (CheckTangent * 0.5f);
					}

					TempVertexCount = 2 * InterpCount * Sheets;
					VertexCount += TempVertexCount;
					LocalVertexCount += TempVertexCount;
					LocalIndexCount += TempVertexCount;

					CurrParticle = PrevParticle;
					CurrTrailData = PrevTrailData;
					Prev = TRAIL_EMITTER_GET_PREV(CurrTrailData->Flags);
					if(Prev != TRAIL_EMITTER_NULL_PREV && Prev >= 0 && Prev < MaxActiveParticles)
					{
						DECLARE_PARTICLE_PTR(PrevParticlePtr, ParticleData + ParticleStride * Prev);
						PrevParticle = PrevParticlePtr;
						PrevTrailData = ((FRibbonTypeDataPayload*)((uint8*)PrevParticle + Template->TypeDataOffset));
					}
					else
					{
						// The START particle will have a previous index of NULL, so we're done
						bDone = true;
					}
				}
				bProcessParticle = true;
			}
			else
			{
				bProcessParticle = false;
			}
		}

		if(bProcessParticle == true)
		{
			ParticleCount++;
			TempVertexCount = 2 * Sheets;
			VertexCount += TempVertexCount;
			LocalVertexCount += TempVertexCount;
			LocalIndexCount += TempVertexCount;
			
			LocalIndexCount += ((Sheets - 1) * 4);
		
			CurrTrailData->TriangleCount = LocalIndexCount - 2;

			CurrTrailData->RenderingInterpCount = 1;
			
			IndexCount += LocalIndexCount;
			TheTrailCount++;
		}
	}

	TrailCount = TheTrailCount;
	if(TheTrailCount > 0)
	{
		IndexCount += 4 * (TheTrailCount - 1);
		TriangleCount = IndexCount - (2 * TheTrailCount);
	}
	else
	{
		IndexCount = 0;
		TriangleCount = 0;
	}
}

void FParticleRibbonEmitterInstance::GetParticleLifetimeAndSize(int32 InTrailIdx, const FBaseParticle* InParticle, bool bInNoLivingParticles, float& OutOneOverMaxLifetime, float& OutSize)
{

	if(bInNoLivingParticles == true)
	{
		UParticleLODLevel* LODLevel = Template->LODLevels[0];		

		// Find the lifetime module
		float CurrLifetime = 0.0f;
		for(int32 ModuleIdx = 0; ModuleIdx < LODLevel->SpawnModules.Num(); ModuleIdx++)
		{
			UParticleModuleLifetime* LifetimeModule = Cast<UParticleModuleLifetime>(LODLevel->SpawnModules[ModuleIdx]);
			if(LifetimeModule != nullptr)
			{
				float MaxLifetime = LifetimeModule->Lifetime.GetValue(EmitterTime, &RandomStream);
				if(CurrLifetime > 0.f)
				{
					// Another module already modified lifetime.
					CurrLifetime = 1.f / (MaxLifetime + (1.f / CurrLifetime));
				}
				else
				{
					// First module to modify lifetime.
					CurrLifetime = (MaxLifetime > 0.f) ? (1.f / MaxLifetime) : 0.f;
				}

				break;	// consider only the first lifetime module
			}
		}
		if(CurrLifetime == 0.0f)
		{
			// We can't allow this...
			CurrLifetime = 1.0f;
		}

		if((1.0f / CurrLifetime) < 0.001f)
		{
			CurrLifetime = 1.f / 0.001f;
		}

		CurrentLifetimes[InTrailIdx] = CurrLifetime;
		CurrentSizes[InTrailIdx] = InParticle->Size.X;
	}
	OutOneOverMaxLifetime = CurrentLifetimes[InTrailIdx];
	OutSize = CurrentSizes[InTrailIdx];
}

bool FParticleRibbonEmitterInstance::GetParticleInTrail(bool bSkipStartingParticle, FBaseParticle* InStartingFromParticle, FRibbonTypeDataPayload* InStartingTrailData, EGetTrailDirection InGetDirection, EGetTrailParticleOption InGetOption, FBaseParticle*& OutParticle, FRibbonTypeDataPayload*& OutTrailData)
{
	OutParticle = nullptr;
	OutTrailData = nullptr;

	if((InStartingFromParticle == nullptr) || (InStartingTrailData == nullptr))
	{
		return false;
	}

	bool bDone = false;
	FBaseParticle* CheckParticle = InStartingFromParticle;
	FRibbonTypeDataPayload* CheckTrailData = InStartingTrailData;
	bool bCheckIt = !bSkipStartingParticle;
	int32 Steps = 0;
	while(!bDone && Steps < MaxActiveParticles)
	{
		++Steps;
		if(bCheckIt == true) 
		{
			bool bItsGood = false;
			switch(InGetOption)
			{
			case FParticleRibbonEmitterInstance::GET_Any:
				bItsGood = true;
				break;
			case FParticleRibbonEmitterInstance::GET_Spawned:
				if(CheckTrailData->bInterpolatedSpawn == false)
				{
					bItsGood = true;
				}
				break;
			case FParticleRibbonEmitterInstance::GET_Interpolated:
				if(CheckTrailData->bInterpolatedSpawn == true)
				{
					bItsGood = true;
				}
				break;
			case FParticleRibbonEmitterInstance::GET_Start:
				if(TRAIL_EMITTER_IS_START(CheckTrailData->Flags))
				{
					bItsGood = true;
				}
				break;
			case FParticleRibbonEmitterInstance::GET_End:
				if(TRAIL_EMITTER_IS_END(CheckTrailData->Flags))
				{
					bItsGood = true;
				}
				break;
			default:
				break;
			}

			if(bItsGood == true)
			{
				OutParticle = CheckParticle;
				OutTrailData = CheckTrailData;
				bDone = true;
			}
		}
		int32 Index = -1;
		if(!bDone)
		{
			if(InGetDirection == GET_Prev)
			{
				Index = TRAIL_EMITTER_GET_PREV(CheckTrailData->Flags);
				if(Index == TRAIL_EMITTER_NULL_PREV)
				{
					Index = -1;

				}
			}
			else
			{
				Index = TRAIL_EMITTER_GET_NEXT(CheckTrailData->Flags);
				if(Index == TRAIL_EMITTER_NULL_NEXT)
				{
					Index = -1;

				}
			}
		}

		if(Index >= 0 && Index < MaxActiveParticles && ParticleData)
		{
			DECLARE_PARTICLE_PTR(TempPartice, ParticleData + ParticleStride * Index)
			CheckParticle = TempPartice;
			CheckTrailData = ((FRibbonTypeDataPayload*)((uint8*)CheckParticle + Template->TypeDataOffset));
			bCheckIt = true;
		}
		else
		{
			bDone = true;
		}
	}

	return ((OutParticle != NULL) && (OutTrailData != NULL));
}
