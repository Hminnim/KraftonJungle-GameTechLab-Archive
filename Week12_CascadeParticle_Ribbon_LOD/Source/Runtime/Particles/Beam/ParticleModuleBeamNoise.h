#pragma once

#include "Particles/ParticleModule.h"
#include "Particles/ParticleDistribution.h"
#include "ParticleModuleBeamNoise.generated.h"

// 빔 중심선을 흔드는 모듈 (UE: Beam/ParticleModuleBeamNoise). 번개, 전기처럼 지직거리는 빔을 만든다.
// 빔 이미터(Beam Data)에 붙였을 때만 동작한다. 에디터에서 이미터 우클릭 → Beam → Noise로 붙인다.
//
// 동작 (UE와 같다):
//  - 시작점과 끝점 사이를 Frequency + 1 구간으로 나눈 위치에 노이즈 점 Frequency개를 둔다.
//  - 점마다 오프셋을 NoiseRange에서 뽑고, 그리는 위치 = 직선 위 위치 + (컴포넌트 회전으로 돌린 오프셋)이다.
//  - 오프셋 Frequency + 1번째 칸은 끝점용이다 (bTargetNoise일 때만 쓴다).
//  - NoiseLockTime: 0이면 매 프레임, 양수면 그 초마다 다시 뽑고, 음수면 스폰 때 뽑은 값을 유지한다.
//  - bSmooth (NoiseLockTime > 0일 때만): 다시 뽑은 값을 목표(Next)로 두고, 현재 오프셋이 NoiseSpeed로 목표를 향해 움직인다.
//    목표와 축마다 NoiseLockRadius 안이면 목표에 붙는다. 틱틱 바뀌지 않고 꿈틀거린다.
//  - NoiseTessellation: 노이즈 점 사이를 Hermite 곡선으로 그만큼 잘게 나눈다 (1이면 꺾인 직선).
//  - 켜져 있으면 빔은 InterpolationPoints 대신 이 점들로 중심선을 그린다.
// 오프셋은 UE처럼 빔 TypeData의 Payload에 둔다 (이 모듈 자신의 Payload는 없다).
//
// UE와 다름 (bSmooth 이동): UE는 렌더 스레드가 그릴 때 오프셋을 옮기고, 한 번에 옮기는 거리에 마지막으로 다시 뽑은 뒤 지난 시간(NoiseRate)을 곱한다.
//   여기서는 Update에서 옮기고 프레임 시간(DeltaTime)을 곱해 NoiseSpeed가 초당 거리(m/s)가 되게 한다. 목표를 지나칠 만큼 움직이면 목표에 붙인다.
// 아직 없는 UE 설정: bOscillate, bUseNoiseTangents, FrequencyDistance, bApplyNoiseScale, NoiseScale, bNRScaleEmitterTime
// 간단한 빔에 필요 없어 뺀 UE 설정:
//  - Frequency_LowRange: 빔마다 점 수를 무작위로 고른다. 모든 빔이 Frequency개를 쓴다.
//  - NoiseRangeScale: NoiseRange에 곱하는 배율. NoiseRange 값으로 같은 결과를 낸다.
//  - NoiseTension: 접선 계산에 곱하지만 UE도 바로 정규화해서 부호만 남는다. 노이즈 점 접선 = 정규화(다음 점 - 이전 점)이다.
UCLASS()
class UParticleModuleBeamNoise : public UParticleModule
{
	GENERATED_BODY()
public:
	UParticleModuleBeamNoise();

	using Super::Serialize;
	void Serialize(json& Handle, bool bIsLoading) override;

	// 빔마다 노이즈 점 수를 정하고 오프셋을 처음 뽑는다 (UE: Spawn)
	void Spawn(const FParticleSpawnContext& Context) override;
	// NoiseLockTime 규칙대로 오프셋을 다시 뽑는다 (UE: Update)
	void Update(const FParticleUpdateContext& Context) override;

	// UE 상한 (FDynamicBeam2EmitterData::MaxNoiseFrequency, UParticleModuleBeamNoise::MaxNoiseTessellation)
	static constexpr int32 MaxNoiseFrequency = 250;
	static constexpr int32 MaxNoiseTessellation = 500;

	// 노이즈를 켠다 (UE 기본 false)
	UPROPERTY(EditAnywhere)
	bool bLowFreq_Enabled = false;

	// 노이즈 점 수 (UE 기본 0. 0이면 노이즈가 없다)
	UPROPERTY(EditAnywhere, meta=(ClampMin=0, ClampMax=250))
	int32 Frequency = 0;

	// 오프셋을 목표로 부드럽게 옮긴다 (UE 기본 false). NoiseLockTime > 0일 때만 쓰인다.
	UPROPERTY(EditAnywhere)
	bool bSmooth = false;

	// bSmooth에서 목표에 붙는 거리 (m, 축마다). UE 기본 1cm를 m로 옮긴 값이다.
	UPROPERTY(EditAnywhere, meta=(ClampMin=0))
	float NoiseLockRadius = 0.01f;

	// 오프셋을 다시 뽑는 간격(초). 0 = 매 프레임, 음수 = 고정 (UE 기본 0)
	UPROPERTY(EditAnywhere)
	float NoiseLockTime = 0.0f;

	// 노이즈 점 사이를 나누는 수 (UE 기본 1, 0은 1로 본다)
	UPROPERTY(EditAnywhere, meta=(ClampMin=0, ClampMax=500))
	int32 NoiseTessellation = 1;

	// 끝점도 흔든다 (UE 기본 false)
	UPROPERTY(EditAnywhere)
	bool bTargetNoise = false;

	// 노이즈 점 오프셋의 범위 (m, 컴포넌트 로컬 축). UE 기본 상수 (50, 50, 50)cm를 m로 옮긴 값이다. 흔들리게 하려면 Uniform으로 둔다.
	FVectorDistribution NoiseRange = FVectorDistribution::MakeConstant(FVector(0.5f, 0.5f, 0.5f));

	// bSmooth에서 오프셋이 목표로 움직이는 속도 (m/s, 모든 축 같은 값). UE 기본 상수 (50, 50, 50)cm를 m로 옮긴 값이다.
	// UE는 벡터 Distribution이지만 시간에 따라 바꿀 일이 없어 상수 하나로 둔다.
	UPROPERTY(EditAnywhere, meta=(ClampMin=0))
	float NoiseSpeed = 0.5f;

	// Tessellation 곡선의 접선 길이 (m). UE 기본 상수 250cm를 m로 옮긴 값이다.
	// UE는 float Distribution이지만 시간에 따라 바꿀 일이 없어 상수 하나로 둔다.
	UPROPERTY(EditAnywhere, meta=(ClampMin=0))
	float NoiseTangentStrength = 2.5f;

	// 레이아웃과 스폰에 쓰는 노이즈 점 수 (Frequency를 [0, MaxNoiseFrequency]로 자른 값).
	// 노이즈가 꺼져 있으면(bLowFreq_Enabled == false) 0이다.
	int32 GetMaxFrequency() const;
	// 실제 Tessellation 값 (1 ~ MaxNoiseTessellation)
	int32 GetTessellationFactor() const;
};
