#pragma once

#include "UObject/Object.h"
#include "Math/Vector.h"
#include "Math/Box.h"
#include "ParticleSystem.generated.h"

class UParticleEmitter;

UCLASS()
class UParticleSystem : public UObject
{
	GENERATED_BODY()

public:
	TArray<UParticleEmitter*> Emitters;

	float UpdateTime_FPS = 60.0f;
	float UpdateTime_Delta = 1.0f / 60.0f;

	float WarmupTime = 0.0f;
	float WarmupTickRate = 0.0f;

	bool bUseFixedRelativeBoundingBox = false;
	FBox FixedRelativeBoundingBox{ FVector(-100.0f, -100.0f, -100.0f), FVector(100.0f, 100.0f, 100.0f) };

	void BuildEmitters();
};