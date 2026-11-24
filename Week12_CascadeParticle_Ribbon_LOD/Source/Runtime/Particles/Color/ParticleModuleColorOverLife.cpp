#include "EnginePCH.h"
#include "Particles/Color/ParticleModuleColorOverLife.h"
#include "Particles/ParticleEmitterInstances.h"
#include "Particles/ParticleHelper.h"
#include "Particles/ParticleSerialization.h"
#include <algorithm>

namespace
{
	void ApplyColorOverLife(const UParticleModuleColorOverLife& Module, FBaseParticle& Particle, FRandomStream& Stream)
	{
		const FVector Color = Module.ColorOverLife.GetValue(Particle.RelativeTime, &Stream);
		float Alpha = Module.AlphaOverLife.GetValue(Particle.RelativeTime, &Stream);
		if (Module.bClampAlpha)
		{
			Alpha = std::clamp(Alpha, 0.0f, 1.0f);
		}
		Particle.Color.R = Color.X;
		Particle.Color.G = Color.Y;
		Particle.Color.B = Color.Z;
		Particle.Color.A = Alpha;
	}
}

UParticleModuleColorOverLife::UParticleModuleColorOverLife()
{
	bSpawnModule = true;
	bUpdateModule = true;

	// UE InitializeDefaults: 시간 0과 1에 키 두 개, 값은 모두 1
	ColorOverLife = FVectorDistribution::MakeCurve();
	ColorOverLife.AddKey(0.0f, FVector(1.0f, 1.0f, 1.0f));
	ColorOverLife.AddKey(1.0f, FVector(1.0f, 1.0f, 1.0f));
	AlphaOverLife = FFloatDistribution::MakeCurve();
	AlphaOverLife.AddKey(0.0f, 1.0f);
	AlphaOverLife.AddKey(1.0f, 1.0f);
}

void UParticleModuleColorOverLife::Serialize(json& Handle, bool bIsLoading)
{
	Super::Serialize(Handle, bIsLoading);
	ParticleJson::SerializeDistribution(Handle, "ColorOverLife", ColorOverLife, bIsLoading);
	ParticleJson::SerializeDistribution(Handle, "AlphaOverLife", AlphaOverLife, bIsLoading);
}

void UParticleModuleColorOverLife::Spawn(const FParticleSpawnContext& Context)
{
	SPAWN_INIT
	(void)ActiveParticles;
	(void)ParticleStride;
	(void)CurrentOffset;
	ApplyColorOverLife(*this, Particle, Context.Owner.RandomStream);
}

void UParticleModuleColorOverLife::Update(const FParticleUpdateContext& Context)
{
	BEGIN_UPDATE_LOOP;
	{
		ApplyColorOverLife(*this, Particle, Context.Owner.RandomStream);
	}
	END_UPDATE_LOOP;
}
