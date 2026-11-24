#include "EnginePCH.h"
#include "Particles/Collision/ParticleCollisionSelfTest.h"
#include "Particles/Collision/ParticleCollisionQuery.h"
#include "Particles/Collision/ParticleModuleCollision.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/ParticleModule.h"
#include "Particles/ParticleHelper.h"
#include "Math/Transform.h"
#include <cstring>
#include "Particles/ParticleEmitterInstances.h"
#include "Components/ParticleSystemComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Actor/StaticMeshActor.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Asset/AssetManager.h"
#include "Render/Mesh.h"
#include <cmath>

int32 RunParticleCollisionQuerySelfTest()
{
	int32 Failed = 0;
	auto Check = [&](bool bOk, const char* Name)
		{
			if (!bOk)
			{
				++Failed;
				HTR_LOG(Error, "[Particle] collision query FAIL {}", Name);
			}
		};
	auto Near = [&](double Got, double Want, double Tol, const char* Name)
		{
			if (std::fabs(Got - Want) > Tol)
			{
				++Failed;
				HTR_LOG(Error, "[Particle] collision query FAIL {} (got {}, want {})", Name, Got, Want);
			}
		};

	// 1) 월드 없이: 컴포넌트도 override도 없으면 아무것도 맞지 않는다. override가 있으면 그것을 쓴다.
	{
		FParticleEmitterInstance Instance;
		FParticleCollisionHit Hit;
		Check(!TraceParticleSegment(Instance, FVector(0.0f, 0.0f, 1.0f), FVector(0.0f, 0.0f, -1.0f), EParticleCollisionTarget::AllStaticMeshes, true, Hit),
			"no component and no override: miss");

		// z = 0 평면. 위에서 아래로 가로지르는 선분만 맞는다
		Instance.CollisionTraceOverride = [](const FVector& Start, const FVector& End, FParticleCollisionHit& Out)
			{
				if (!(Start.Z > 0.0f && End.Z < 0.0f)) return false;
				Out.Time = Start.Z / (Start.Z - End.Z);
				Out.Location = Start + (End - Start) * Out.Time;
				Out.Normal = FVector(0.0f, 0.0f, 1.0f);
				return true;
			};
		Check(TraceParticleSegment(Instance, FVector(0.0f, 0.0f, 2.0f), FVector(0.0f, 0.0f, -6.0f), EParticleCollisionTarget::AllStaticMeshes, true, Hit),
			"override: crossing segment hits");
		Near(Hit.Time, 0.25, 1e-6, "override: Time");
		Near(Hit.Location.Z, 0.0, 1e-6, "override: Location.Z");
		Check(!TraceParticleSegment(Instance, FVector(0.0f, 0.0f, 2.0f), FVector(0.0f, 0.0f, 1.0f), EParticleCollisionTarget::AllStaticMeshes, true, Hit),
			"override: segment above the plane misses");
	}

	// 2) 실제 월드: 큰 상자 하나를 바닥으로 둔다
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false);
	UStaticMesh* Cube = UAssetManager::GetAssetByPath<UStaticMesh>("Cube");
	Check(World != nullptr && Cube != nullptr, "world and Cube mesh exist");
	if (!World || !Cube)
	{
		if (GEngine && World) GEngine->DestroyWorld(World);
		HTR_LOG(Info, "[Particle] collision query self-test: {}", Failed == 0 ? "all passed" : "FAILED");
		return Failed;
	}

	AStaticMeshActor* Floor = World->SpawnActor<AStaticMeshActor>();
	UStaticMeshComponent* FloorMesh = Floor ? Floor->GetStaticMeshComponent() : nullptr;
	Check(FloorMesh != nullptr, "floor actor has a static mesh component");
	if (FloorMesh)
	{
		FloorMesh->SetStaticMesh(Cube);
		// 10 × 10 × 1 크기 상자를 윗면이 TopZ가 되게 둔다
		FloorMesh->SetRelativeScale3D(FVector(10.0f, 10.0f, 1.0f));
		FloorMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -0.5f));
		const float TopZ = -0.5f + 1.0f * Cube->GetMeshData().AABB.Max.Z;

		const FVector Start(0.0f, 0.0f, 5.0f);
		const FVector End(0.0f, 0.0f, -5.0f);
		FParticleCollisionHit Hit;

		Check(TraceParticleSegmentInWorld(World, Start, End, EParticleCollisionTarget::AllStaticMeshes, nullptr, Hit), "world: segment down onto the floor hits");
		Near(Hit.Time, (5.0 - TopZ) / 10.0, 1e-4, "world: Time");
		Near(Hit.Location.Z, TopZ, 1e-4, "world: Location.Z is the top face");
		Near(Hit.Normal.Z, 1.0, 1e-4, "world: Normal points up (against the segment)");
		Check(Hit.Component == FloorMesh, "world: hit component");

		// 위에서 아래로 쏘든 아래에서 위로 쏘든 법선은 선분의 반대쪽을 향한다
		FParticleCollisionHit Reverse;
		Check(TraceParticleSegmentInWorld(World, FVector(0.0f, 0.0f, -5.0f), FVector(0.0f, 0.0f, 5.0f), EParticleCollisionTarget::AllStaticMeshes, nullptr, Reverse), "world: segment from below hits");
		Check(Reverse.Normal.Z < 0.0f, "world: from below the normal faces down");

		Check(!TraceParticleSegmentInWorld(World, Start, FVector(0.0f, 0.0f, TopZ + 0.25f), EParticleCollisionTarget::AllStaticMeshes, nullptr, Hit),
			"world: segment that ends above the floor misses");
		Check(!TraceParticleSegmentInWorld(World, FVector(20.0f, 0.0f, 5.0f), FVector(20.0f, 0.0f, -5.0f), EParticleCollisionTarget::AllStaticMeshes, nullptr, Hit),
			"world: segment beside the floor misses");
		Check(!TraceParticleSegmentInWorld(World, Start, Start, EParticleCollisionTarget::AllStaticMeshes, nullptr, Hit), "world: zero-length segment misses");

		// 필터: 무시할 액터, 충돌 끄기, 블로킹 대상
		Check(!TraceParticleSegmentInWorld(World, Start, End, EParticleCollisionTarget::AllStaticMeshes, Floor, Hit), "world: ignored actor is skipped");
		Floor->SetActorEnableCollision(false);
		Check(!TraceParticleSegmentInWorld(World, Start, End, EParticleCollisionTarget::AllStaticMeshes, nullptr, Hit), "world: collision disabled actor is skipped");
		Floor->SetActorEnableCollision(true);

		FloorMesh->bBlockComponent = false;
		Check(!TraceParticleSegmentInWorld(World, Start, End, EParticleCollisionTarget::BlockingStaticMeshes, nullptr, Hit), "world: non-blocking mesh is skipped by BlockingStaticMeshes");
		Check(TraceParticleSegmentInWorld(World, Start, End, EParticleCollisionTarget::AllStaticMeshes, nullptr, Hit), "world: AllStaticMeshes ignores bBlockComponent");
		FloorMesh->bBlockComponent = true;
		Check(TraceParticleSegmentInWorld(World, Start, End, EParticleCollisionTarget::BlockingStaticMeshes, nullptr, Hit), "world: blocking mesh is hit by BlockingStaticMeshes");

		// 컴포넌트 경로: 파티클 컴포넌트가 다른 액터 소유이면 바닥에 맞고, 바닥 액터 소유이면 소스 액터 무시가 켜졌을 때 건너뛴다
		FParticleEmitterInstance Instance;
		AActor* OtherActor = World->SpawnActor<AActor>();
		auto* OtherPSC = OtherActor ? Cast<UParticleSystemComponent>(OtherActor->AddComponent(UParticleSystemComponent::StaticClass(), "OtherPSC")) : nullptr;
		auto* FloorPSC = Cast<UParticleSystemComponent>(Floor->AddComponent(UParticleSystemComponent::StaticClass(), "FloorPSC"));
		Check(OtherPSC != nullptr && FloorPSC != nullptr, "particle components were added");
		if (OtherPSC && FloorPSC)
		{
			Instance.OwnerComponent = OtherPSC;
			Check(TraceParticleSegment(Instance, Start, End, EParticleCollisionTarget::AllStaticMeshes, true, Hit), "component path: other actor's particles hit the floor");
			Instance.OwnerComponent = FloorPSC;
			Check(!TraceParticleSegment(Instance, Start, End, EParticleCollisionTarget::AllStaticMeshes, true, Hit), "component path: bIgnoreSourceActor skips the owner's own mesh");
			Check(TraceParticleSegment(Instance, Start, End, EParticleCollisionTarget::AllStaticMeshes, false, Hit), "component path: without bIgnoreSourceActor the owner's mesh is hit");
		}

		// 기울어진 면: 법선이 면에 수직인지 (피치 45도면 법선이 위쪽에서 앞뒤로 45도 기운다)
		FloorMesh->SetRelativeRotation(FRotator(45.0f, 0.0f, 0.0f));
		FParticleCollisionHit Tilted;
		if (TraceParticleSegmentInWorld(World, Start, End, EParticleCollisionTarget::AllStaticMeshes, nullptr, Tilted))
		{
			Near(std::sqrt(Tilted.Normal.X * Tilted.Normal.X + Tilted.Normal.Y * Tilted.Normal.Y + Tilted.Normal.Z * Tilted.Normal.Z), 1.0, 1e-4, "tilted: Normal is unit length");
			Near(Tilted.Normal.Z, std::sqrt(0.5), 1e-3, "tilted: Normal.Z is cos(45 degrees)");
			Near(std::fabs(Tilted.Normal.X), std::sqrt(0.5), 1e-3, "tilted: |Normal.X| is sin(45 degrees)");
			Near(Tilted.Normal.Y, 0.0, 1e-3, "tilted: Normal.Y is 0");
		}
		else
		{
			Check(false, "tilted: segment hits the rotated floor");
		}
	}

	if (GEngine) GEngine->DestroyWorld(World);
	HTR_LOG(Info, "[Particle] collision query self-test: {}", Failed == 0 ? "all passed" : "FAILED");
	return Failed;
}
