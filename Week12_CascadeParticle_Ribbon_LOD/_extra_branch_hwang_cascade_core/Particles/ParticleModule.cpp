#include "EnginePCH.h"
#include "ParticleModule.h"
#include "ParticleEmitter.h"
#include "ParticleEmitterInstanceOwner.h"
#include "ParticleEmitterInstances.h"
#include "ParticleLODLevel.h"
#include "Math/RandomStream.h"

void UParticleModule::CompileModule(FParticleEmitterBuildInfo& EmitterInfo)
{
	if(bSpawnModule)
	{
		EmitterInfo.SpawnModules.Add(this);
	}
}

void UParticleModule::Spawn(const FSpawnContext& Context)
{
}

void UParticleModule::Update(const FUpdateContext& Context)
{
}

void UParticleModule::FinalUpdate(const FUpdateContext& Context)
{
}

uint32 UParticleModule::RequiredBytes(UParticleModuleTypeDataBase* TypeData)
{
	return 0;
}

uint32 UParticleModule::RequiredBytesPerInstance()
{
	return 0;
}

UParticleModuleRequired::UParticleModuleRequired()
{
	bSpawnModule = false;
	bUpdateModule = false;
}

UParticleModuleSpawn::UParticleModuleSpawn()
{
	bSpawnModule = false;
	bUpdateModule = false;
}

void UParticleModuleSpawn::GetSpawnAmount( float EmitterTime, float DeltaTime, float& InOutSpawnFraction, int32& OutSpawnCount, UObject* Data, FRandomStream* InRandomStream)
{
	OutSpawnCount = 0;
	if (!bEnabled || DeltaTime <= 0.0f)
	{
		return;
	}

	float SpawnRate = Rate.IsCreated() ? Rate.GetValue(EmitterTime, Data, InRandomStream) : DefaultSpawnRate;
	if (SpawnRate <= 0.0f)
	{
		return;
	}

	const float Scale = RateScale.IsCreated() ? FMath::Max(0.0f, RateScale.GetValue(EmitterTime, Data, InRandomStream)) : 1.0f;

	const float EffectiveRate = SpawnRate * Scale;
	if (EffectiveRate <= 0.0f)
	{
		return;
	}

	const float TotalSpawnFloat = InOutSpawnFraction + EffectiveRate * DeltaTime;
	const int32 SpawnCount = static_cast<int32>(TotalSpawnFloat);
	InOutSpawnFraction = TotalSpawnFloat - static_cast<float>(SpawnCount);
	OutSpawnCount = SpawnCount;
}

int32 UParticleModuleSpawn::GetBurstCount(float OldEmitterTime, float NewEmitterTime, FRandomStream* InRandomStream) const
{
	if (!bEnabled)
	{
		return 0;
	}

	int32 TotalBurstCount = 0;
	for (int32 Index = 0; Index < BurstList.Num(); ++Index)
	{
		const FParticleBurst& Burst = BurstList[Index];
		const bool bTriggeredAtZero = (OldEmitterTime <= 0.0f && Burst.Time == 0.0f && NewEmitterTime >= 0.0f);
		const bool bTriggeredInInterval = (Burst.Time > OldEmitterTime && Burst.Time <= NewEmitterTime);
		if (!bTriggeredAtZero && !bTriggeredInInterval)
		{
			continue;
		}

		if (Burst.CountLow >= 0 && Burst.CountLow < Burst.Count)
		{
			const float Alpha = InRandomStream ? InRandomStream->GetFraction() : (static_cast<float>(FMath::Rand()) / (static_cast<float>(RAND_MAX) + 1.0f));
			const int32 Range = Burst.Count - Burst.CountLow + 1;
			const int32 Offset = FMath::Min(Range - 1, static_cast<int32>(Alpha * static_cast<float>(Range)));
			TotalBurstCount += Burst.CountLow + Offset;
		}
		else
		{
			TotalBurstCount += FMath::Max(0, Burst.Count);
		}
	}

	return TotalBurstCount;
}

float UParticleModuleSpawn::GetMaximumSpawnRate()
{
	float MaxRate = DefaultSpawnRate;
	if (Rate.IsCreated())
	{
		float MinOut = 0.0f;
		float MaxOut = 0.0f;
		Rate.GetOutRange(MinOut, MaxOut);
		MaxRate = FMath::Max(0.0f, MaxOut);
	}

	float MaxScale = 1.0f;
	if (RateScale.IsCreated())
	{
		float MinScale = 0.0f;
		float MaxScaleOut = 0.0f;
		RateScale.GetOutRange(MinScale, MaxScaleOut);
		MaxScale = FMath::Max(0.0f, MaxScaleOut);
	}

	return MaxRate * MaxScale;
}

UParticleModuleLifetime::UParticleModuleLifetime()
{
	bSpawnModule = true;
}

float UParticleModuleLifetime::GetLifetimeValue(const FContext& Context, float InTime, UObject* Data)
{
	if (!LifeTime.IsCreated())
	{
		return 1.0f;
	}

	return LifeTime.GetValue(InTime, Data, &Context.Owner.Component.GetRandomStream());
}

void UParticleModuleLifetime::CompileModule(FParticleEmitterBuildInfo& EmitterInfo)
{
	float MinLifetime = 0.0f;
	float MaxLifetime = 1.0f;
	if (LifeTime.IsCreated())
	{
		LifeTime.GetOutRange(MinLifetime, MaxLifetime);
	}
	EmitterInfo.MaxLifetime = MaxLifetime;
	EmitterInfo.SpawnModules.Add(this);
}

void UParticleModuleLifetime::Spawn(const FSpawnContext& Context)
{
	SPAWN_INIT;
	{
		FParticleEmitterInstance* Owner = &Context.Owner;
		FRandomStream* InRandomStream = &Owner->Component.GetRandomStream();
		UObject* Data = Owner->Component.GetDistributionData();

		const float MaxLifetime = LifeTime.IsCreated()
			? LifeTime.GetValue(Owner->EmitterTime, Data, InRandomStream)
			: 1.0f;

		if (Particle.OneOverMaxLifeTime > 0.f)
		{
			const float CombinedLifetime = MaxLifetime + (1.f / Particle.OneOverMaxLifeTime);
			Particle.OneOverMaxLifeTime = (CombinedLifetime > 0.f) ? (1.f / CombinedLifetime) : 0.f;
		}
		else
		{
			Particle.OneOverMaxLifeTime = (MaxLifetime > 0.f) ? (1.f / MaxLifetime) : 0.f;
		}

		Particle.RelativeTime = (Particle.RelativeTime > 1.0f)
			? Particle.RelativeTime
			: (Context.SpawnTime * Particle.OneOverMaxLifeTime);
	}
}

UParticleModuleSize::UParticleModuleSize()
{
	bSpawnModule = true;
}

void UParticleModuleSize::CompileModule(FParticleEmitterBuildInfo& EmitterInfo)
{
	if (StartSize.IsCreated())
	{
		FVector MinSize = FVector::ZeroVector;
		FVector MaxSize = FVector::ZeroVector;
		StartSize.GetRange(MinSize, MaxSize);
		EmitterInfo.MaxSize.X = MaxSize.X;
		EmitterInfo.MaxSize.Y = MaxSize.Y;
	}
	else
	{
		EmitterInfo.MaxSize = FVector2(1.0f, 1.0f);
	}
	EmitterInfo.SpawnModules.Add(this);
}

void UParticleModuleSize::Spawn(const FSpawnContext& Context)
{
	FParticleEmitterInstance* Owner = &Context.Owner;
	FRandomStream* InRandomStream = &Owner->Component.GetRandomStream();
	UObject* Data = Owner->Component.GetDistributionData();

	SPAWN_INIT;
	const FVector Size = StartSize.IsCreated()
		? StartSize.GetValue(Owner->EmitterTime, Data, InRandomStream)
		: FVector::OneVector;

	Particle.Size += Size;
	Particle.BaseSize += Size;
}

UParticleModuleVelocity::UParticleModuleVelocity()
{
	bSpawnModule = true;
}

void UParticleModuleVelocity::Spawn(const FSpawnContext& Context)
{
	FParticleEmitterInstance* Owner = &Context.Owner;
	FRandomStream* InRandomStream = &Owner->Component.GetRandomStream();
	UObject* Data = Owner->Component.GetDistributionData();

	SPAWN_INIT;
	{
		FVector Vel = StartVelocity.IsCreated()
			? StartVelocity.GetValue(Owner->EmitterTime, Data, InRandomStream)
			: FVector::ZeroVector;

		const FVector4 Origin4 = Owner->EmitterToSimulation.GetOrigin();
		const FVector EmitterOrigin(Origin4.X, Origin4.Y, Origin4.Z);
		const FVector FromOrigin = (Particle.Location - EmitterOrigin).GetSafeNormal();

		FVector OwnerScale = FVector::OneVector;
		if (bApplyOwnerScale)
		{
			OwnerScale = Owner->Component.GetComponentToWorld().Scale;
		}

		UParticleLODLevel* LODLevel = Owner->SpriteTemplate ? Owner->SpriteTemplate->GetCurrentLODLevel(Owner) : nullptr;
		if (!LODLevel)
		{
			LODLevel = Owner->CurrentLODLevel;
		}

		if (LODLevel && LODLevel->RequiredModule && LODLevel->RequiredModule->bUseLocalSpace)
		{
			if (bInWorldSpace)
			{
				Vel = Owner->SimulationToWorld.Inverse().TransformVector(Vel);
			}
			else
			{
				Vel = Owner->EmitterToSimulation.TransformVector(Vel);
			}
		}
		else if (!bInWorldSpace)
		{
			Vel = Owner->EmitterToSimulation.TransformVector(Vel);
		}

		Vel *= OwnerScale;

		if (StartVelocityRadial.IsCreated())
		{
			const float RadialVel = StartVelocityRadial.GetValue(Owner->EmitterTime, Data, InRandomStream);
			Vel += FromOrigin * RadialVel * OwnerScale;
		}

		Particle.Velocity += Vel;
		Particle.BaseVelocity += Vel;
	}
}

UParticleModuleColor::UParticleModuleColor()
{
	bSpawnModule = true;
}

void UParticleModuleColor::Spawn(const FSpawnContext& Context)
{
	FParticleEmitterInstance* Owner = &Context.Owner;
	FRandomStream* InRandomStream = &Owner->Component.GetRandomStream();
	UObject* Data = Owner->Component.GetDistributionData();

	SPAWN_INIT;
	{
		const FVector ColorVec = StartColor.IsCreated()
			? StartColor.GetValue(Owner->EmitterTime, Data, InRandomStream)
			: FVector::OneVector;
		float Alpha = StartAlpha.IsCreated()
			? StartAlpha.GetValue(Owner->EmitterTime, Data, InRandomStream)
			: 1.0f;

		if (bClampAlpha)
		{
			Alpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
		}

		Particle.Color = FLinearColor(ColorVec.X, ColorVec.Y, ColorVec.Z, Alpha);
		Particle.BaseColor = Particle.Color;
	}
}

void UParticleModuleColor::CompileModule(FParticleEmitterBuildInfo& EmitterInfo)
{
	EmitterInfo.DynamicColor = StartColor;
	EmitterInfo.DynamicAlpha = StartAlpha;
	EmitterInfo.SpawnModules.Add(this);
}
