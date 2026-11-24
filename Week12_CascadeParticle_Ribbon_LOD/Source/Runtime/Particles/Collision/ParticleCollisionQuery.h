#pragma once

#include "Core/Types.h"
#include "Containers/Array.h"
#include "Math/Vector.h"
#include "Math/Box.h"
#include "Physics/Collision.h"
#include "Particles/Collision/ParticleCollisionTypes.h"

class UWorld;
class AActor;
class UPrimitiveComponent;
struct FParticleEmitterInstance;

// 파티클 충돌 선분 쿼리의 결과 (UE: FHitResult 중 파티클 충돌 모듈이 쓰는 부분)
struct FParticleCollisionHit
{
	// 선분 위 비율. 0은 시작점, 1은 끝점이다 (UE: Hit.Time)
	float Time = 1.0f;
	// 월드 공간 충돌 지점
	FVector Location = FVector(0.0f, 0.0f, 0.0f);
	// 월드 공간 법선. 정규화되어 있고 항상 선분의 반대 방향을 향한다 (파티클이 온 쪽)
	FVector Normal = FVector(0.0f, 0.0f, 1.0f);
	// 맞은 컴포넌트
	UPrimitiveComponent* Component = nullptr;
};

// 충돌 대상으로 모은 도형 하나 (구, 캡슐, 박스). 월드 공간 값과 월드 AABB를 미리 계산해 둔다.
struct FParticleCollisionShape
{
	enum class EType : uint8 { Sphere, Capsule, Box };
	EType Type = EType::Sphere;
	FWorldSphere Sphere{};
	FWorldCapsule Capsule{};
	FWorldBox Box{};
	FBox Bounds;
	UPrimitiveComponent* Component = nullptr;
};

// 한 번 모아서 여러 선분에 쓰는 도형 목록 (도형은 렌더 프록시가 없어 BVH에 없으므로 컴포넌트에서 직접 모은다)
struct FParticleCollisionShapeList
{
	TArray<FParticleCollisionShape> Shapes;
};

// 월드의 충돌 도형을 모은다. bBlockComponent가 켜진 도형만 모으고(꺼진 도형은 트리거나 겹침 판정용), 등록되어 있고 같은 월드이며
// 액터 충돌이 켜져 있고 IgnoreActor 소유가 아닌 것만 넣는다. 호출마다 객체 배열을 새로 만들므로 파티클마다 부르지 말고
// 충돌 모듈 Update 한 번에 한 번만 부른다.
void GatherParticleCollisionShapes(UWorld* World, const AActor* IgnoreActor, FParticleCollisionShapeList& Out);

// 월드 공간 선분 Start → End를 쏴서 가장 먼저 맞는 정적 메시 삼각형(또는 Shapes의 도형)을 찾는다.
// - 삼각형 단위로 정확하고 법선이 있다 (UWorld::SweepSingle은 메시를 AABB로만 판정한다).
// - 표시 여부와 상관없이 충돌한다 (UE와 같다). 파티클 시스템, 빌보드, 라이트처럼 정적 메시가 아닌 것은 맞지 않는다.
// - IgnoreActor가 소유한 컴포넌트는 건너뛴다.
// - Shapes가 있으면 구, 캡슐, 박스도 함께 판정하고 더 가까운 쪽을 돌려준다.
// UWorld를 고치지 않고 FScene::BVH를 읽기만 하므로 공용 파일을 바꾸지 않는다.
bool TraceParticleSegmentInWorld(UWorld* World, const FVector& Start, const FVector& End, EParticleCollisionTarget Target,
	const AActor* IgnoreActor, FParticleCollisionHit& OutHit, const FParticleCollisionShapeList* Shapes = nullptr);

// 충돌 모듈이 부르는 진입점. Instance.CollisionTraceOverride가 있으면 그것을 쓰고(자체 테스트용),
// 없으면 Instance.OwnerComponent의 월드를 쿼리한다. 컴포넌트가 없으면 false다.
// bIgnoreSourceActor가 true면 이 컴포넌트를 소유한 액터의 컴포넌트는 맞지 않는다.
bool TraceParticleSegment(const FParticleEmitterInstance& Instance, const FVector& Start, const FVector& End,
	EParticleCollisionTarget Target, bool bIgnoreSourceActor, FParticleCollisionHit& OutHit,
	const FParticleCollisionShapeList* Shapes = nullptr);
