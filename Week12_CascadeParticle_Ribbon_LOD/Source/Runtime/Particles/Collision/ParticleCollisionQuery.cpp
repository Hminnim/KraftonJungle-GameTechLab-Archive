#include "EnginePCH.h"
#include "Particles/Collision/ParticleCollisionQuery.h"
#include "Particles/ParticleEmitterInstances.h"
#include "Components/ParticleSystemComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/ShapeComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Engine/Scene.h"
#include "Engine/PrimitiveSceneProxy.h"
#include "GameFramework/Actor.h"
#include "UObject/UObjectIterator.h"
#include "Collision/Ray.h"
#include "Render/Mesh.h"
#include <algorithm>
#include <cmath>

namespace
{
	constexpr float ShapeEps = 1.0e-9f;

	// 선분 Start + Delta * t (t in [0, Nearest))가 구에 맞는 가장 이른 t. 시작점이 구 안이면 t = 0이고 법선은 중심에서 바깥쪽이다.
	bool SegmentVsSphere(const FWorldSphere& S, const FVector& Start, const FVector& Delta, float Nearest, float& OutT, FVector& OutNormal)
	{
		const FVector OC = Start - S.Center;
		const float C = OC.Dot(OC) - S.Radius * S.Radius;
		if (C <= 0.0f)
		{
			OutT = 0.0f;
			OutNormal = OC.GetSafeNormal();
			if (OutNormal.SizeSquared() <= ShapeEps) OutNormal = -Delta.GetSafeNormal();
			return 0.0f < Nearest;
		}
		const float A = Delta.Dot(Delta);
		const float B = 2.0f * Delta.Dot(OC);
		const float Disc = B * B - 4.0f * A * C;
		if (A <= ShapeEps || Disc < 0.0f) return false;
		const float T = (-B - std::sqrt(Disc)) / (2.0f * A);
		if (T < 0.0f || T >= Nearest) return false;
		OutT = T;
		OutNormal = (Start + Delta * T - S.Center).GetSafeNormal();
		return true;
	}

	// 선분이 박스(회전축을 가진 상자)에 맞는 가장 이른 t. 박스 축 기준 slab 판정이다. 시작점이 박스 안이면 t = 0이고 법선은 선분의 반대 방향이다.
	bool SegmentVsBox(const FWorldBox& B, const FVector& Start, const FVector& Delta, float Nearest, float& OutT, FVector& OutNormal)
	{
		const FVector O = Start - B.Center;
		float TEnter = 0.0f;
		float TExit = Nearest;
		int32 HitAxis = -1;
		float HitSign = 0.0f;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const float Origin = O.Dot(B.Axis[Axis]);
			const float Direction = Delta.Dot(B.Axis[Axis]);
			const float Extent = B.Extent[Axis];
			if (std::fabs(Direction) <= ShapeEps)
			{
				if (std::fabs(Origin) > Extent) return false;
				continue;
			}
			float Near = (-Extent - Origin) / Direction;
			float Far = (Extent - Origin) / Direction;
			if (Near > Far) std::swap(Near, Far);
			if (Near > TEnter)
			{
				TEnter = Near;
				HitAxis = Axis;
				// 들어가는 면: 진행 방향이 +축이면 -축 면으로 들어가므로 법선은 -축이다
				HitSign = Direction > 0.0f ? -1.0f : 1.0f;
			}
			TExit = std::min(TExit, Far);
			if (TEnter > TExit) return false;
		}
		if (HitAxis < 0)
		{
			// 시작점이 박스 안
			OutT = 0.0f;
			OutNormal = -Delta.GetSafeNormal();
			return 0.0f < Nearest;
		}
		if (TEnter >= Nearest) return false;
		OutT = TEnter;
		OutNormal = B.Axis[HitAxis] * HitSign;
		return true;
	}

	// 선분이 캡슐에 맞는 가장 이른 t. 엔진의 구 스윕(반지름 0)을 쓴다.
	bool SegmentVsCapsule(const FWorldCapsule& C, const FVector& Start, const FVector& Delta, float Nearest, float& OutT, FVector& OutNormal)
	{
		const float Length = Delta.Size();
		if (!(Length > ShapeEps)) return false;
		const FRay Ray{ Start, Delta / Length };
		float Distance = 0.0f;
		FVector Normal;
		if (!FCollision::SweepSphereCapsule(Ray, Length * Nearest, 0.0f, C, Distance, Normal)) return false;
		const float T = Distance / Length;
		if (T >= Nearest) return false;
		OutT = T;
		OutNormal = Normal.GetSafeNormal();
		return true;
	}

	bool SegmentBoundsOverlap(const FBox& Bounds, const FVector& Start, const FVector& End)
	{
		constexpr float Pad = 1.0e-4f;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const float Lo = std::min(Start[Axis], End[Axis]) - Pad;
			const float Hi = std::max(Start[Axis], End[Axis]) + Pad;
			if (Hi < Bounds.Min[Axis] || Lo > Bounds.Max[Axis]) return false;
		}
		return true;
	}
}

void GatherParticleCollisionShapes(UWorld* World, const AActor* IgnoreActor, FParticleCollisionShapeList& Out)
{
	Out.Shapes.Reset();
	if (!World)
	{
		return;
	}
	for (TObjectIterator<UShapeComponent> It; It; ++It)
	{
		UShapeComponent* Shape = *It;
		AActor* Owner = Shape ? Shape->GetOwner() : nullptr;
		if (!Shape || !Owner || Owner == IgnoreActor || !Shape->bBlockComponent || !Shape->IsRegistered() ||
			Shape->GetWorld() != World || !Owner->GetActorEnableCollision())
		{
			continue;
		}
		FParticleCollisionShape Entry;
		Entry.Component = Shape;
		Entry.Bounds = Shape->GetWorldAABB();
		if (auto* Sphere = Cast<USphereComponent>(Shape))
		{
			Entry.Type = FParticleCollisionShape::EType::Sphere;
			Entry.Sphere = FCollision::ToWorld(Sphere);
		}
		else if (auto* Capsule = Cast<UCapsuleComponent>(Shape))
		{
			Entry.Type = FParticleCollisionShape::EType::Capsule;
			Entry.Capsule = FCollision::ToWorld(Capsule);
		}
		else if (auto* Box = Cast<UBoxComponent>(Shape))
		{
			Entry.Type = FParticleCollisionShape::EType::Box;
			Entry.Box = FCollision::ToWorld(Box);
		}
		else
		{
			continue;
		}
		Out.Shapes.Add(Entry);
	}
}

bool TraceParticleSegmentInWorld(UWorld* World, const FVector& Start, const FVector& End, EParticleCollisionTarget Target,
	const AActor* IgnoreActor, FParticleCollisionHit& OutHit, const FParticleCollisionShapeList* Shapes)
{
	if (!World)
	{
		return false;
	}
	// 길이가 0인 선분은 방향이 없어서 쏠 수 없다
	const FVector Delta = End - Start;
	if (!(Delta.SizeSquared() > 1.0e-12f))
	{
		return false;
	}

	FScene& Scene = World->GetScene();
	// 이동한 프리미티브의 BVH가 낡았으면 한 번 갱신한다 (UWorld::SweepSingle은 호출마다 한다. 파티클은 호출이 많아서 필요할 때만 한다)
	if (Scene.HasPendingPrimitiveUpdates())
	{
		Scene.UpdateAllTransforms();
	}

	// 방향을 정규화하지 않은 레이: t가 선분 위 비율이 되어 [0, 1] 범위가 곧 선분이다
	const FRay WorldRay{ Start, Delta };
	const FPreparedRay PreparedRay(WorldRay);

	float NearestT = 1.0f;
	FVector BestNormal(0.0f, 0.0f, 1.0f);
	UPrimitiveComponent* BestComponent = nullptr;

	Scene.BVH.TraceClosest(
		[&](const FBox& Bounds, float& OutEnterT)
		{
			return RayIntersectsAABB(PreparedRay, Bounds.Min, Bounds.Max, OutEnterT);
		},
		[&](FPrimitiveSceneProxy* Proxy, float& InOutNearestT)
		{
			// 정적 메시 프록시만 GetMesh가 값을 준다. 파티클 컴포넌트, 빌보드, 라이트 프록시는 여기서 걸러진다.
			UStaticMesh* Mesh = Proxy ? Proxy->GetMesh() : nullptr;
			UPrimitiveComponent* Component = Proxy ? Proxy->GetComponent() : nullptr;
			AActor* Owner = Component ? Component->GetOwner() : nullptr;
			if (!Mesh || !Owner || Owner == IgnoreActor || Owner->GetWorld() != World || !Owner->GetActorEnableCollision())
			{
				return false;
			}
			if (Target == EParticleCollisionTarget::BlockingStaticMeshes && !Component->bBlockComponent)
			{
				return false;
			}

			// 월드 선분을 메시 로컬로 옮긴다. 방향은 정규화하지 않으므로 t가 그대로 유지된다.
			const FMatrix& WorldToLocal = Proxy->GetWorldToLocal();
			const FRay LocalRay{ WorldToLocal.TransformPosition(WorldRay.Origin), WorldToLocal.TransformVector(WorldRay.Direction) };
			float T = InOutNearestT;
			FVector LocalNormal;
			if (!RayIntersectsMesh(LocalRay, Mesh->GetMeshData(), T, &LocalNormal))
			{
				return false;
			}

			// 법선은 점이 아니라 면의 방향이라 역행렬의 전치로 월드에 옮긴다 (비균등 스케일에서도 면에 수직이 유지된다)
			FVector WorldNormal = WorldToLocal.GetTransposed().TransformVector(LocalNormal).GetSafeNormal();
			// 삼각형 감김 방향에 따라 법선이 뒤집혀 있을 수 있으므로 선분의 반대쪽을 향하게 한다
			if (WorldNormal.Dot(Delta) > 0.0f)
			{
				WorldNormal = -WorldNormal;
			}
			InOutNearestT = T;
			BestNormal = WorldNormal;
			BestComponent = Component;
			return true;
		},
		NearestT);

	// 도형: 메시보다 가까우면 그쪽이 이긴다. 월드 AABB로 먼저 걸러낸다.
	if (Shapes)
	{
		for (const FParticleCollisionShape& Shape : Shapes->Shapes)
		{
			if (!SegmentBoundsOverlap(Shape.Bounds, Start, End))
			{
				continue;
			}
			float T = 0.0f;
			FVector Normal;
			bool bHit = false;
			switch (Shape.Type)
			{
			case FParticleCollisionShape::EType::Sphere:
				bHit = SegmentVsSphere(Shape.Sphere, Start, Delta, NearestT, T, Normal);
				break;
			case FParticleCollisionShape::EType::Capsule:
				bHit = SegmentVsCapsule(Shape.Capsule, Start, Delta, NearestT, T, Normal);
				break;
			case FParticleCollisionShape::EType::Box:
				bHit = SegmentVsBox(Shape.Box, Start, Delta, NearestT, T, Normal);
				break;
			}
			if (bHit)
			{
				// 법선은 선분의 반대쪽을 향하게 한다 (메시와 같은 규칙)
				if (Normal.Dot(Delta) > 0.0f)
				{
					Normal = -Normal;
				}
				NearestT = T;
				BestNormal = Normal;
				BestComponent = Shape.Component;
			}
		}
	}

	if (!BestComponent)
	{
		return false;
	}
	OutHit.Time = NearestT;
	OutHit.Location = Start + Delta * NearestT;
	OutHit.Normal = BestNormal;
	OutHit.Component = BestComponent;
	return true;
}

bool TraceParticleSegment(const FParticleEmitterInstance& Instance, const FVector& Start, const FVector& End,
	EParticleCollisionTarget Target, bool bIgnoreSourceActor, FParticleCollisionHit& OutHit, const FParticleCollisionShapeList* Shapes)
{
	if (Instance.CollisionTraceOverride)
	{
		return Instance.CollisionTraceOverride(Start, End, OutHit);
	}
	UParticleSystemComponent* Component = Instance.OwnerComponent;
	if (!Component)
	{
		return false;
	}
	return TraceParticleSegmentInWorld(Component->GetWorld(), Start, End, Target, bIgnoreSourceActor ? Component->GetOwner() : nullptr, OutHit, Shapes);
}
