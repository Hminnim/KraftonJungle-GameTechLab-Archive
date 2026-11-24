#pragma once

#include "Particles/ParticleModule.h"
#include "Particles/ParticleDistribution.h"
#include "ParticleModuleSizeMultiplyLife.generated.h"

// 수명에 따라 크기에 배율을 곱한다 (UE: Size/ParticleModuleSizeMultiplyLife). 에디터 이름은 "Size By Life"다.
// 곡선의 가로축은 파티클 수명 비율이다 (0 = 태어날 때, 1 = 죽을 때. FBaseParticle::RelativeTime).
// 매 프레임 크기를 BaseSize(Initial Size)로 되돌린 뒤 곱하므로 배율이 쌓이지 않는다. 1이면 Initial Size 그대로다.
// 빔은 Size.X가 두께이므로 X 배율로 두께가 바뀐다.
// 기본값은 UE InitializeDefaults와 같다: 배율 곡선 (0, 1) → (1, 1), 세 축 모두 곱한다.
UCLASS()
class UParticleModuleSizeMultiplyLife : public UParticleModule
{
	GENERATED_BODY()
public:
	UParticleModuleSizeMultiplyLife();

	using Super::Serialize;
	void Serialize(json& Handle, bool bIsLoading) override;

	// 태어날 때의 배율 (UE: Spawn)
	void Spawn(const FParticleSpawnContext& Context) override;
	// 매 프레임 지금 수명 비율의 배율 (UE: Update)
	void Update(const FParticleUpdateContext& Context) override;

	// 축마다 곱할지 (UE 기본 모두 true)
	UPROPERTY(EditAnywhere)
	bool MultiplyX = true;
	UPROPERTY(EditAnywhere)
	bool MultiplyY = true;
	UPROPERTY(EditAnywhere)
	bool MultiplyZ = true;

	// 수명 비율(0 ~ 1)마다 크기 배율
	FVectorDistribution LifeMultiplier;
};
