#pragma once

#include "Core/Types.h"
#include "Math/Vector.h"

class UPrimitiveComponent;

// 파티클이 무언가에 맞았을 때 컴포넌트에 쌓이는 이벤트 (UE: FParticleEventCollideData)
// 충돌 모듈의 bGenerateCollisionEvents가 켜져 있을 때만 만들어진다. 값은 모두 월드 공간이다.
struct FParticleCollisionEvent
{
	// 컴포넌트 안에서 몇 번째 이미터인가 (UE: EmitterName). 컴포넌트가 이벤트를 받을 때 정한다. 못 찾으면 -1.
	int32 EmitterIndex = -1;
	// 충돌한 시점의 이미터 시간(초)
	float EmitterTime = 0.0f;
	// 충돌 지점
	FVector Location = FVector(0.0f, 0.0f, 0.0f);
	// 충돌 면의 법선 (정규화, 파티클이 온 쪽을 향함)
	FVector Normal = FVector(0.0f, 0.0f, 1.0f);
	// 충돌 직전의 파티클 속도.
	// UE와 다름: UE는 반사 분기에서 속도를 0으로 둔 뒤 이벤트를 만들어서 이벤트의 속도가 0이다(코드 순서로 확인). 여기서는 충돌 직전 값이다.
	FVector Velocity = FVector(0.0f, 0.0f, 0.0f);
	// 이동 선분 위 충돌 위치 비율 (0 = 시작, 1 = 끝) (UE: Time)
	float Time = 0.0f;
	// 맞은 컴포넌트 (UE: Item/Component). 이벤트가 전달되는 Tick 동안만 유효하다고 보고 쓴다.
	UPrimitiveComponent* HitComponent = nullptr;
	// 파티클 생성 순번. 같은 파티클의 여러 충돌을 이어서 볼 수 있다 (UE: ParticleCount)
	uint32 ParticleId = 0;
	// 이 충돌로 MaxCollisions를 다 썼는가
	bool bFinalCollision = false;
};
