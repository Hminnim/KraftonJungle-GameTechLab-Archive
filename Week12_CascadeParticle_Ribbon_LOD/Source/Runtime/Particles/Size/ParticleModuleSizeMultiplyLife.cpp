#include "EnginePCH.h"
#include "Particles/Size/ParticleModuleSizeMultiplyLife.h"
#include "Particles/ParticleEmitterInstances.h"
#include "Particles/ParticleHelper.h"
#include "Particles/ParticleSerialization.h"

namespace
{
	void ApplySizeMultiplyLife(const UParticleModuleSizeMultiplyLife& Module, FBaseParticle& Particle, FRandomStream& Stream)
	{
		const FVector Scale = Module.LifeMultiplier.GetValue(Particle.RelativeTime, &Stream);
		if (Module.MultiplyX) Particle.Size.X *= Scale.X;
		if (Module.MultiplyY) Particle.Size.Y *= Scale.Y;
		if (Module.MultiplyZ) Particle.Size.Z *= Scale.Z;
	}
}

UParticleModuleSizeMultiplyLife::UParticleModuleSizeMultiplyLife()
{
	bSpawnModule = true;
	bUpdateModule = true;

	// UE InitializeDefaults: 시간 0과 1에 키 두 개, 값은 모두 1
	LifeMultiplier = FVectorDistribution::MakeCurve();
	LifeMultiplier.AddKey(0.0f, FVector(1.0f, 1.0f, 1.0f));
	LifeMultiplier.AddKey(1.0f, FVector(1.0f, 1.0f, 1.0f));
}

void UParticleModuleSizeMultiplyLife::Serialize(json& Handle, bool bIsLoading)
{
	Super::Serialize(Handle, bIsLoading);
	ParticleJson::SerializeDistribution(Handle, "LifeMultiplier", LifeMultiplier, bIsLoading);
}

void UParticleModuleSizeMultiplyLife::Spawn(const FParticleSpawnContext& Context)
{
	SPAWN_INIT
	(void)ActiveParticles;
	(void)ParticleStride;
	(void)CurrentOffset;
	// UE처럼 Size만 곱한다 (BaseSize는 Initial Size 그대로 둔다)
	ApplySizeMultiplyLife(*this, Particle, Context.Owner.RandomStream);
}

void UParticleModuleSizeMultiplyLife::Update(const FParticleUpdateContext& Context)
{
	BEGIN_UPDATE_LOOP;
	{
		ApplySizeMultiplyLife(*this, Particle, Context.Owner.RandomStream);
	}
	END_UPDATE_LOOP;
}
