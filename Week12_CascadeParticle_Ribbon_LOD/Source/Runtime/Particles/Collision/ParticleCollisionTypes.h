#pragma once

#include "Core/Types.h"
#include "ParticleCollisionTypes.generated.h"

// 충돌 대상 (UE는 오브젝트 타입 쿼리이고 기본은 WorldStatic이다. 이 엔진에는 오브젝트 타입이 없어서 둘로 나눈다)
UENUM()
enum class EParticleCollisionTarget : uint8
{
	AllStaticMeshes,		// 충돌이 켜진 액터의 모든 정적 메시 (바닥, 벽처럼 bBlockComponent를 따로 켜지 않은 메시 포함)
	BlockingStaticMeshes,	// bBlockComponent가 켜진 정적 메시만
};

// 충돌 모듈이 어떤 충돌에 이벤트를 만들지 (UE: EventGenerator의 FirstTimeOnly / LastTimeOnly에 해당)
UENUM()
enum class EParticleCollisionEventFilter : uint8
{
	EveryCollision,		// 충돌할 때마다
	FirstCollisionOnly,	// 파티클의 첫 충돌만
	LastCollisionOnly,	// 횟수를 다 쓰는 마지막 충돌만
};

// 충돌 횟수(MaxCollisions)를 다 쓴 파티클에 할 일 (UE: EParticleCollisionComplete)
UENUM()
enum class EParticleCollisionComplete : uint8
{
	Kill,				// 파티클을 없앤다
	Freeze,				// 이동과 회전을 모두 멈춘다 (Update 모듈도 건너뛴다)
	HaltCollisions,		// 충돌 검사만 멈추고 계속 움직인다
	FreezeTranslation,	// 위치 이동을 멈춘다
	FreezeRotation,		// 회전을 멈춘다
	FreezeMovement,		// 위치 이동과 회전을 멈춘다 (Freeze와 달리 Update 모듈은 계속 돈다)
};
