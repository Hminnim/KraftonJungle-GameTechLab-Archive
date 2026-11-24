#pragma once

#include "Particles/ParticleModule.h"
#include "Particles/Collision/ParticleCollisionTypes.h"
#include "ParticleModuleCollision.generated.h"

// 충돌한 파티클마다 붙는 Payload (UE: FParticleCollisionPayload). 생성할 때 분포에서 한 번 뽑은 값을 담는다.
// 8바이트 정렬 규칙을 만족한다 (모든 필드 4바이트 단위, 합계 32바이트).
struct FParticleCollisionPayload
{
	FVector UsedDampingFactor;			// 충돌 후 속도에 곱하는 감쇠 (성분별)
	FVector UsedDampingFactorRotation;	// 충돌 후 회전 속도에 곱하는 감쇠 (스프라이트는 X만 쓴다)
	int32 UsedCollisions;				// 남은 충돌 횟수. 0이 되면 CollisionCompletionOption을 적용한다
	float Delay;						// 충돌 검사를 시작하기까지 남은 시간(초). 0 이하면 지났다
};
static_assert(sizeof(FParticleCollisionPayload) == 32, "FParticleCollisionPayload must stay 32 bytes");
static_assert(alignof(FParticleCollisionPayload) <= 8, "Payload type must not require more than 8-byte alignment");

// 파티클 충돌 (UE: ParticleModuleCollision). 매 프레임 파티클마다 이동 선분을 월드에 쏴서 맞으면 속도를 반사·감쇠한다.
//
// 동작 (UE와 같은 순서):
//  1) Update 모듈은 위치 적분(UpdateParticlePositions)보다 먼저 돈다. 그래서 선분은 OldLocation에서 이번 프레임에 도달할 예측 위치
//     (Location + Velocity * dt)에 파티클 크기만큼(Direction * Size / DirScalar)을 더한 점까지다.
//  2) 맞으면 BaseVelocity를 반사하고 감쇠한다 (영구 값). Velocity는 이번 프레임만 0으로 두고, 다음 프레임에 Reset이 BaseVelocity로 복원한다.
//  3) Location은 반사 방향으로 남은 거리 (1 - Hit.Time)만큼 옮긴다.
//  4) MaxCollisions를 다 쓰면 CollisionCompletionOption을 적용한다.
//
// UE와 다른 점:
//  - 충돌 대상은 오브젝트 타입이 아니라 CollisionTarget이다 (아래).
//  - DelayAmount는 초 단위이고 파티클이 살아 있는 동안 매 프레임 줄어든다. UE는 RelativeTime(수명 비율)과 비교하는 것으로 보이며(미확인),
//    수명이 0 이하인 파티클에서는 지연이 끝나지 않는다.
//  - 반사의 기준점은 월드 공간이든 로컬 공간이든 월드에서 본 Particle.Location이다 (UE는 로컬 공간에서 예측 위치를 기준으로 한다).
//  - 물리 임펄스, 트리거 볼륨 무시, Pawn 카운트, 가시성 / 거리 기준 충돌 끄기, 충돌 이벤트는 아직 없다.
UCLASS()
class UParticleModuleCollision : public UParticleModule
{
	GENERATED_BODY()
public:
	UParticleModuleCollision();

	using Super::Serialize;
	// UPROPERTY 값 + 분포 4개
	void Serialize(json& Handle, bool bIsLoading) override;

	uint32 RequiredBytes(UParticleModuleTypeDataBase* TypeData) override;
	void Spawn(const FParticleSpawnContext& Context) override;
	void Update(const FParticleUpdateContext& Context) override;

	// 충돌 후 속도 감쇠 (성분별). 파티클이 생길 때 이미터 시간으로 한 번 뽑는다. 1이면 그대로, 0이면 그 방향 속도가 사라진다.
	// 기본값 0.5는 UE의 기본 분포 범위를 확인하지 못해 임의로 정한 값이다.
	FVectorDistribution DampingFactor = FVectorDistribution::MakeConstant(FVector(0.5f, 0.5f, 0.5f));
	// 충돌 후 회전 속도 감쇠. 스프라이트는 X만, 메시는 성분별로 곱한다 (UE의 기본값 (1,1,1))
	FVectorDistribution DampingFactorRotation = FVectorDistribution::MakeConstant(FVector(1.0f, 1.0f, 1.0f));
	// 파티클당 최대 충돌 횟수. 생성할 때 한 번 뽑아 반올림한다 (UE: SetToSensibleDefaults의 min = max = 1)
	FFloatDistribution MaxCollisions = FFloatDistribution::MakeConstant(1.0f);
	// 충돌 검사를 시작하기까지의 지연(초). 생성할 때 한 번 뽑는다.
	FFloatDistribution DelayAmount = FFloatDistribution::MakeConstant(0.0f);

	// 횟수를 다 쓴 뒤의 동작 (UE 기본 Kill)
	UPROPERTY(EditAnywhere)
	EParticleCollisionComplete CollisionCompletionOption = EParticleCollisionComplete::Kill;

	// 무엇과 충돌하는가
	UPROPERTY(EditAnywhere)
	EParticleCollisionTarget CollisionTarget = EParticleCollisionTarget::AllStaticMeshes;

	// 선분을 파티클 크기만큼 늘릴 때 나누는 값: End = 예측 위치 + 방향 * Size / DirScalar. 클수록 덜 늘어난다 (UE 기본 3.5)
	UPROPERTY(EditAnywhere, meta=(ClampMin=0))
	float DirScalar = 3.5f;

	// true면 법선이 수직에 가까운 충돌만 횟수를 줄인다 (바닥에서만 세고 벽은 세지 않을 때)
	UPROPERTY(EditAnywhere)
	bool bOnlyVerticalNormalsDecrementCount = false;

	// 수직 판정 허용 오차: |법선.Z| + 이 값 < 1이면 수직이 아니라고 본다 (UE 기본 0.1)
	UPROPERTY(EditAnywhere, meta=(ClampMin=0))
	float VerticalFudgeFactor = 0.1f;

	// true면 이 파티클 컴포넌트를 소유한 액터의 메시와는 충돌하지 않는다 (UE 기본 true)
	UPROPERTY(EditAnywhere)
	bool bIgnoreSourceActor = true;

	// true면 bBlockComponent가 켜진 도형 컴포넌트(박스, 구, 캡슐)와도 충돌한다. 꺼진 도형(트리거, 겹침 판정용)은 맞지 않는다.
	// false면 정적 메시만 맞는다. (UE의 bIgnoreTriggerVolumes와 같은 효과를 이 플래그로 얻는다)
	UPROPERTY(EditAnywhere)
	bool bCollideWithBlockingShapes = true;

	// 카메라에서 이 거리(월드 단위)보다 먼 파티클은 충돌하지 않는다. 0이면 끈다 (UE: MaxCollisionDistance, 기본 1000).
	// 카메라 위치는 컴포넌트가 마지막으로 렌더에 제출될 때 기록한 것이고, 아직 모르면 거리 제한 없이 충돌한다.
	// UE와 다름: UE는 먼 파티클에 IgnoreCollisions 플래그를 세워 다시 충돌하지 않지만 여기서는 그 프레임만 건너뛴다.
	UPROPERTY(EditAnywhere, meta=(ClampMin=0))
	float MaxCollisionDistance = 0.0f;

	// true면 최근 0.1초 안에 렌더에 제출되지 않은 컴포넌트는 충돌하지 않는다 (UE: bCollideOnlyIfVisible)
	// UE와 다름: UE의 기본은 true다. 렌더되지 않는 동안 충돌과 이벤트가 조용히 사라지는 것이 놀라운 동작이라 기본을 false로 둔다.
	UPROPERTY(EditAnywhere)
	bool bCollideOnlyIfVisible = false;

	// true면 Pawn이 소유한 컴포넌트에 맞아도 충돌 횟수를 줄이지 않는다 (UE: bPawnsDoNotDecrementCount, 기본 true)
	// UE와 다름: 기본을 false로 둔다 (지금까지의 동작 유지).
	UPROPERTY(EditAnywhere)
	bool bPawnsDoNotDecrementCount = false;

	// true면 충돌할 때 컴포넌트에 FParticleCollisionEvent를 보고한다. 컴포넌트는 Tick이 끝날 때 OnParticleCollide로 전달한다.
	// UE는 별도의 EventGenerator 모듈로 켜고 필터를 정하지만 여기서는 이 모듈의 옵션이다.
	UPROPERTY(EditAnywhere)
	bool bGenerateCollisionEvents = false;

	// 어떤 충돌에 이벤트를 만들지
	UPROPERTY(EditAnywhere)
	EParticleCollisionEventFilter EventFilter = EParticleCollisionEventFilter::EveryCollision;
};
