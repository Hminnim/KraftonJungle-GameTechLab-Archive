#include "EnginePCH.h"
#include "Particles/Beam/ParticleModuleBeamNoise.h"
#include "Particles/TypeData/ParticleModuleTypeDataBeam.h"
#include "Particles/ParticleHelper.h"
#include "Particles/ParticleSerialization.h"
#include <algorithm>
#include <cmath>

namespace
{
	// 노이즈 점 오프셋 Frequency + 1개를 뽑는다. 위치 값은 UE처럼 점 번호 / (Frequency + 1)이다 (Curve 분포일 때 쓰인다).
	void PickNoiseOffsets(const FVectorDistribution& Range, int32 Frequency, FVector* Offsets, FRandomStream& Stream)
	{
		const float StepSize = 1.0f / static_cast<float>(Frequency + 1);
		for (int32 Index = 0; Index <= Frequency; ++Index)
		{
			Offsets[Index] = Range.GetValue(StepSize * static_cast<float>(Index), &Stream);
		}
	}

	// bSmooth: 현재 오프셋을 목표 쪽으로 Speed(축마다 m/s) × DeltaTime만큼 옮긴다 (UE: ParticleSystemRender의 bSmoothNoise_Enabled 분기).
	// 목표와 축마다 LockRadius 안이거나, 이번 이동이 목표를 지나치면 목표에 붙인다.
	void MoveNoiseOffsets(FVector* Current, const FVector* Next, int32 Count, const FVector& Speed, float LockRadius, float DeltaTime)
	{
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FVector Remaining = Next[Index] - Current[Index];
			const FVector Dir = Remaining.GetSafeNormal();
			const FVector Step(Dir.X * Speed.X * DeltaTime, Dir.Y * Speed.Y * DeltaTime, Dir.Z * Speed.Z * DeltaTime);
			const FVector Check = Current[Index] + Step;
			const bool bInRadius = std::abs(Check.X - Next[Index].X) < LockRadius && std::abs(Check.Y - Next[Index].Y) < LockRadius &&
				std::abs(Check.Z - Next[Index].Z) < LockRadius;
			const bool bOvershoot = Step.Length() >= Remaining.Length();
			Current[Index] = (bInRadius || bOvershoot) ? Next[Index] : Check;
		}
	}
}

UParticleModuleBeamNoise::UParticleModuleBeamNoise()
{
	bSpawnModule = true;
	bUpdateModule = true;
}

int32 UParticleModuleBeamNoise::GetMaxFrequency() const
{
	if (!bLowFreq_Enabled)
	{
		return 0;
	}
	return std::clamp(Frequency, 0, MaxNoiseFrequency);
}

int32 UParticleModuleBeamNoise::GetTessellationFactor() const
{
	return std::clamp(NoiseTessellation, 1, MaxNoiseTessellation);
}

void UParticleModuleBeamNoise::Serialize(json& Handle, bool bIsLoading)
{
	if (bIsLoading)
	{
		// 옛 에셋은 NoiseSpeed / NoiseTangentStrength를 Distribution 객체로 저장했다. 상수 값만 꺼내 숫자로 바꾼 뒤 읽는다.
		for (const char* Key : { "NoiseSpeed", "NoiseTangentStrength" })
		{
			if (!Handle.contains(Key) || !Handle[Key].is_object()) continue;
			const json Constant = Handle[Key].value("Constant", json());
			if (Constant.is_number()) Handle[Key] = Constant;
			else if (Constant.is_array() && !Constant.empty() && Constant[0].is_number()) Handle[Key] = Constant[0];
			else Handle.erase(Key);
		}
	}
	Super::Serialize(Handle, bIsLoading);
	ParticleJson::SerializeDistribution(Handle, "NoiseRange", NoiseRange, bIsLoading);
}

void UParticleModuleBeamNoise::Spawn(const FParticleSpawnContext& Context)
{
	FParticleBeamEmitterInstance* Beam = FParticleBeamEmitterInstance::FromInstance(Context.Owner);
	if (!Beam || !bLowFreq_Enabled)
	{
		return;
	}
	FBeamNoisePayload* Noise = Beam->GetBeamNoiseFromParticle(Context.ParticleBase);
	// 레이아웃(인스턴스를 만들 때의 값)보다 많이 쓰지 않는다
	const int32 MaxFrequency = std::min(GetMaxFrequency(), Beam->NoiseMaxFrequency);
	if (!Noise || MaxFrequency <= 0)
	{
		return;	// Payload가 0으로 채워져 있으므로 Frequency 0 = 이 빔은 노이즈 없음
	}

	// 모든 빔이 같은 점 수를 쓴다. 빔마다 저장하는 것은 모듈이 꺼져 있던 빔(0)과 구분하기 위해서다.
	Noise->Frequency = MaxFrequency;
	Noise->NoiseRate = 0.0f;
	PickNoiseOffsets(NoiseRange, MaxFrequency, FParticleBeamEmitterInstance::GetNoiseOffsets(Noise), Beam->RandomStream);
	// bSmooth: 처음 목표도 뽑아 둔다 (UE: Spawn에서 NextNoisePoints를 같이 채운다)
	if (FVector* Next = Beam->GetNextNoiseOffsets(Noise))
	{
		PickNoiseOffsets(NoiseRange, MaxFrequency, Next, Beam->RandomStream);
	}
}

void UParticleModuleBeamNoise::Update(const FParticleUpdateContext& Context)
{
	FParticleBeamEmitterInstance* Beam = FParticleBeamEmitterInstance::FromInstance(Context.Owner);
	// 음수면 스폰 때 뽑은 오프셋을 유지한다 (UE: NoiseLockTime < 0)
	if (!Beam || !bLowFreq_Enabled || NoiseLockTime < 0.0f)
	{
		return;
	}

	// UE처럼 NoiseLockTime이 0이면 bSmooth여도 매 프레임 새로 뽑는다 (옮길 목표가 없다)
	const bool bLocked = NoiseLockTime > 1.e-4f;
	const FVector Speed(NoiseSpeed, NoiseSpeed, NoiseSpeed);

	BEGIN_UPDATE_LOOP;
	{
		FBeamNoisePayload* Noise = Beam->GetBeamNoiseFromParticle(&Particle);
		if (Noise && Noise->Frequency > 0)
		{
			FVector* Offsets = FParticleBeamEmitterInstance::GetNoiseOffsets(Noise);
			// 레이아웃에 목표 칸이 있고 지금도 bSmooth일 때만 (드래그 중 bSmooth를 막 켠 경우는 재시작 뒤부터)
			FVector* Next = (bSmooth && bLocked) ? Beam->GetNextNoiseOffsets(Noise) : nullptr;
			bool bRepick = true;	// NoiseLockTime이 0이면 매 프레임
			if (bLocked)
			{
				Noise->NoiseRate += DeltaTime;
				bRepick = Noise->NoiseRate > NoiseLockTime;
				if (bRepick)
				{
					Noise->NoiseRate = 0.0f;
				}
			}
			if (Next)
			{
				// 목표만 새로 뽑고, 현재 오프셋은 매 프레임 목표 쪽으로 움직인다
				if (bRepick) PickNoiseOffsets(NoiseRange, Noise->Frequency, Next, Beam->RandomStream);
				MoveNoiseOffsets(Offsets, Next, Noise->Frequency + 1, Speed, NoiseLockRadius, DeltaTime);
			}
			else if (bRepick)
			{
				PickNoiseOffsets(NoiseRange, Noise->Frequency, Offsets, Beam->RandomStream);
			}
		}
	}
	END_UPDATE_LOOP;
}
