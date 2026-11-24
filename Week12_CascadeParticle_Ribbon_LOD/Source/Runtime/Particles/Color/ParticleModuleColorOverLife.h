#pragma once

#include "Particles/ParticleModule.h"
#include "Particles/ParticleDistribution.h"
#include "ParticleModuleColorOverLife.generated.h"

// 수명에 따라 색과 알파를 바꾼다 (UE: Color/ParticleModuleColorOverLife). 에디터 이름은 "Color Over Life"다.
// 곡선의 가로축은 파티클 수명 비율이다 (0 = 태어날 때, 1 = 죽을 때. FBaseParticle::RelativeTime).
// UE처럼 색을 곱하지 않고 덮어쓴다. Initial Color가 있어도 이 모듈의 값이 보인다 (매 프레임 BaseColor로 되돌린 뒤 덮어쓴다).
// 기본값은 UE InitializeDefaults와 같다: 색 곡선 (0, 1) → (1, 1), 알파 곡선 (0, 1) → (1, 1). 둘 다 흰색 불투명으로 일정하다.
UCLASS()
class UParticleModuleColorOverLife : public UParticleModule
{
	GENERATED_BODY()
public:
	UParticleModuleColorOverLife();

	using Super::Serialize;
	void Serialize(json& Handle, bool bIsLoading) override;

	// 태어날 때의 색 (UE: Spawn)
	void Spawn(const FParticleSpawnContext& Context) override;
	// 매 프레임 지금 수명 비율의 색 (UE: Update)
	void Update(const FParticleUpdateContext& Context) override;

	// 알파를 0 ~ 1로 자른다 (UE 기본 true)
	UPROPERTY(EditAnywhere)
	bool bClampAlpha = true;

	// 수명 비율(0 ~ 1)마다 RGB
	FVectorDistribution ColorOverLife;
	// 수명 비율(0 ~ 1)마다 알파
	FFloatDistribution AlphaOverLife;
};
