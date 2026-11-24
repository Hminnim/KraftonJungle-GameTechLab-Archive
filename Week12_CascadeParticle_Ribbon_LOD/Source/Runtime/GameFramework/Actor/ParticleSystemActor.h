#pragma once

#include "GameFramework/Actor.h"
#include "Components/ParticleSystemComponent.h"
#include "Components/BillboardComponent.h"
#include "ParticleSystemActor.generated.h"

// 레벨에 파티클 시스템을 두는 액터 (UE: AEmitter).
// 파티클은 그릴 메시가 없고 입자가 없거나 작을 때는 클릭할 곳이 없으므로, 에디터에서만 보이는 아이콘(빌보드)을 달아
// 뷰포트에서 클릭해 고를 수 있게 한다. 아이콘은 게임에서는 숨긴다.
UCLASS()
class AParticleSystemActor : public AActor
{
	GENERATED_BODY()

public:
	AParticleSystemActor();
	~AParticleSystemActor() override = default;

	UParticleSystemComponent* GetParticleSystemComponent() const { return ParticleSystemComponent; }
	UBillboardComponent* GetBillboardComponent() const { return BillboardComponent; }

private:
	UParticleSystemComponent* ParticleSystemComponent = nullptr;
	UBillboardComponent* BillboardComponent = nullptr;
};
