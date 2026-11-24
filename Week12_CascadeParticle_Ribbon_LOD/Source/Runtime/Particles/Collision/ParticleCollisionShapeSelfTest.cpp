#include "EnginePCH.h"
#include "Particles/Collision/ParticleCollisionSelfTest.h"
#include "Particles/Collision/ParticleCollisionQuery.h"
#include "Particles/Collision/ParticleModuleCollision.h"
#include "Components/ParticleSystemComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Actor/StaticMeshActor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Actor/ParticleSystemActor.h"
#include "Serialization/JsonArchive.h"
#include "UObject/UObjectIterator.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/ParticleModule.h"
#include "Particles/ParticleHelper.h"
#include "Particles/ParticleEmitterInstances.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Asset/AssetManager.h"
#include "Render/Mesh.h"
#include <cmath>
#include <cstring>
#include <functional>

namespace
{
	// 충돌 모듈 하나가 든 이미터 하나. 월드의 컴포넌트를 OwnerComponent로 쓰고, 필요하면 쿼리 대체(CollisionTraceOverride)를 넣는다.
	struct FShapeFixture
	{
		UParticleSystem* System = nullptr;
		UParticleEmitter* Emitter = nullptr;
		UParticleLODLevel* LOD = nullptr;
		UParticleModuleCollision* Module = nullptr;
		FParticleEmitterInstance Inst;
		int32 Offset = INDEX_NONE;

		FShapeFixture()
		{
			System = NewObject<UParticleSystem>();
			Emitter = System->AddEmitter();
			LOD = Emitter->AddLODLevel();
			Module = Cast<UParticleModuleCollision>(LOD->AddModule(UParticleModuleCollision::StaticClass()));
		}

		void Begin(UParticleSystemComponent* Component)
		{
			Emitter->CacheEmitterModuleInfo();
			Offset = Emitter->GetModuleDataOffset(Module);
			Inst.Init(Emitter);
			Inst.Resize(4);
			Inst.OwnerComponent = Component;
		}

		~FShapeFixture()
		{
			Inst.OwnerComponent = nullptr;
			Inst.Release();
			DestroyParticleSystem(System);
		}

		// Old z, 현재 z, 속도 z로 위에서 아래로 떨어지는 파티클 하나
		FBaseParticle* AddFalling(float OldZ, float Z, float VelocityZ, float X = 0.0f)
		{
			uint8* Slot = Inst.ParticleData + static_cast<size_t>(Inst.ParticleStride) * Inst.ParticleIndices[Inst.ActiveParticles];
			std::memset(Slot, 0, Inst.ParticleSize);
			FBaseParticle* P = reinterpret_cast<FBaseParticle*>(Slot);
			Reset(*P, OldZ, Z, VelocityZ, X);
			P->OneOverMaxLifetime = 0.1f;
			const FParticleSpawnContext Context{ Inst, Offset, 0.0f, P };
			Module->Spawn(Context);
			++Inst.ActiveParticles;
			return P;
		}

		static void Reset(FBaseParticle& P, float OldZ, float Z, float VelocityZ, float X = 0.0f)
		{
			P.Location = FVector(X, 0.0f, Z);
			P.OldLocation = FVector(X, 0.0f, OldZ);
			P.Velocity = FVector(0.0f, 0.0f, VelocityZ);
			P.BaseVelocity = P.Velocity;
		}

		void Update(float DeltaTime)
		{
			const FParticleUpdateContext Context{ Inst, Offset, DeltaTime };
			Module->Update(Context);
		}

		FParticleCollisionPayload& PayloadOf(FBaseParticle& P)
		{
			return *reinterpret_cast<FParticleCollisionPayload*>(reinterpret_cast<uint8*>(&P) + Offset);
		}
	};

	// z = 0 평면. 위에서 아래로 가로지르는 선분만 맞고 맞은 컴포넌트는 인자로 정한다.
	std::function<bool(const FVector&, const FVector&, FParticleCollisionHit&)> PlaneTraceHitting(UPrimitiveComponent* Component)
	{
		return [Component](const FVector& Start, const FVector& End, FParticleCollisionHit& Out)
			{
				if (!(Start.Z > 0.0f && End.Z < 0.0f)) return false;
				Out.Time = Start.Z / (Start.Z - End.Z);
				Out.Location = Start + (End - Start) * Out.Time;
				Out.Normal = FVector(0.0f, 0.0f, 1.0f);
				Out.Component = Component;
				return true;
			};
	}
}

int32 RunParticleCollisionShapeSelfTest()
{
	int32 Failed = 0;
	auto Check = [&](bool bOk, const char* Name)
		{
			if (!bOk)
			{
				++Failed;
				HTR_LOG(Error, "[Particle] collision shape FAIL {}", Name);
			}
		};
	auto Near = [&](double Got, double Want, double Tol, const char* Name)
		{
			if (std::fabs(Got - Want) > Tol)
			{
				++Failed;
				HTR_LOG(Error, "[Particle] collision shape FAIL {} (got {}, want {})", Name, Got, Want);
			}
		};

	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false);
	UStaticMesh* Cube = UAssetManager::GetAssetByPath<UStaticMesh>("Cube");
	Check(World != nullptr && Cube != nullptr, "world and Cube mesh exist");
	if (!World || !Cube)
	{
		if (GEngine && World) GEngine->DestroyWorld(World);
		HTR_LOG(Info, "[Particle] collision shape self-test: {}", Failed == 0 ? "all passed" : "FAILED");
		return Failed;
	}

	// 도형 하나를 가진 액터를 만든다. 도형은 루트 컴포넌트이고 bBlockComponent가 켜져 있다.
	const auto MakeShapeActor = [&](UClass* ShapeClass, const char* Name, const FVector& Location, AActor** OutActor) -> UShapeComponent*
		{
			AActor* Actor = World->SpawnActor<AActor>();
			if (OutActor) *OutActor = Actor;
			auto* Shape = Actor ? Cast<UShapeComponent>(Actor->AddComponent(ShapeClass, Name)) : nullptr;
			if (Shape)
			{
				Actor->SetRootComponent(Shape);
				Shape->SetRelativeLocation(Location);
				Shape->bBlockComponent = true;
			}
			return Shape;
		};

	AActor* BoxActor = nullptr;
	AActor* SphereActor = nullptr;
	AActor* CapsuleActor = nullptr;
	auto* Box = Cast<UBoxComponent>(MakeShapeActor(UBoxComponent::StaticClass(), "Box", FVector(0.0f, 0.0f, 0.0f), &BoxActor));
	auto* Sphere = Cast<USphereComponent>(MakeShapeActor(USphereComponent::StaticClass(), "Sphere", FVector(10.0f, 0.0f, 0.0f), &SphereActor));
	auto* Capsule = Cast<UCapsuleComponent>(MakeShapeActor(UCapsuleComponent::StaticClass(), "Capsule", FVector(20.0f, 0.0f, 0.0f), &CapsuleActor));
	Check(Box && Sphere && Capsule, "shape components were created");
	if (!Box || !Sphere || !Capsule)
	{
		if (GEngine) GEngine->DestroyWorld(World);
		HTR_LOG(Info, "[Particle] collision shape self-test: {}", Failed == 0 ? "all passed" : "FAILED");
		return Failed;
	}
	Box->BoxExtent = FVector(1.0f, 1.0f, 1.0f);
	Sphere->SphereRadius = 1.0f;
	Capsule->CapsuleHalfHeight = 2.0f;
	Capsule->CapsuleRadius = 0.5f;

	FParticleCollisionShapeList List;
	GatherParticleCollisionShapes(World, nullptr, List);
	Check(List.Shapes.Num() == 3, "gather: the three blocking shapes are collected");

	const auto Trace = [&](const FVector& Start, const FVector& End, FParticleCollisionHit& Hit, const FParticleCollisionShapeList* Shapes)
		{
			return TraceParticleSegmentInWorld(World, Start, End, EParticleCollisionTarget::AllStaticMeshes, nullptr, Hit, Shapes);
		};
	FParticleCollisionHit Hit;

	// 1) 박스
	Check(Trace(FVector(0.0f, 0.0f, 3.0f), FVector(0.0f, 0.0f, -3.0f), Hit, &List), "box: segment down onto the top face hits");
	Near(Hit.Location.Z, 1.0, 1e-4, "box: top face height");
	Near(Hit.Time, 2.0 / 6.0, 1e-4, "box: Time");
	Near(Hit.Normal.Z, 1.0, 1e-4, "box: normal points up");
	Check(Hit.Component == Box, "box: hit component");
	Check(Trace(FVector(3.0f, 0.0f, 0.2f), FVector(-3.0f, 0.0f, 0.2f), Hit, &List), "box: segment from the side hits");
	Near(Hit.Location.X, 1.0, 1e-4, "box: side face position");
	Near(Hit.Normal.X, 1.0, 1e-4, "box: side normal");
	Check(!Trace(FVector(0.0f, 0.0f, 3.0f), FVector(0.0f, 0.0f, 1.5f), Hit, &List), "box: segment that stops above misses");
	Check(Trace(FVector(0.0f, 0.0f, 0.0f), FVector(0.0f, 0.0f, -3.0f), Hit, &List), "box: segment starting inside hits at once");
	Near(Hit.Time, 0.0, 1e-6, "box: inside start has Time 0");
	Check(!Trace(FVector(0.0f, 0.0f, 3.0f), FVector(0.0f, 0.0f, -3.0f), Hit, nullptr), "box: without a shape list nothing is hit (shapes have no render proxy)");

	// 2) 회전한 박스: 45도 돌리면 마름모라서 +x쪽에서 y = 0.3으로 쏘면 (+,+) 사분면의 면에 맞는다 (x + y = sqrt(2))
	Box->SetRelativeRotation(FRotator(0.0f, 45.0f, 0.0f));
	GatherParticleCollisionShapes(World, nullptr, List);
	Check(Trace(FVector(4.0f, 0.3f, 0.0f), FVector(-4.0f, 0.3f, 0.0f), Hit, &List), "rotated box: segment hits the diamond face");
	Near(Hit.Location.X, std::sqrt(2.0) - 0.3, 1e-3, "rotated box: hit position on the rotated face");
	Near(Hit.Normal.X, std::sqrt(0.5), 1e-3, "rotated box: normal X");
	Near(Hit.Normal.Y, std::sqrt(0.5), 1e-3, "rotated box: normal Y");
	Box->SetRelativeRotation(FRotator(0.0f, 0.0f, 0.0f));
	GatherParticleCollisionShapes(World, nullptr, List);

	// 3) 구
	Check(Trace(FVector(10.0f, 0.0f, 3.0f), FVector(10.0f, 0.0f, -3.0f), Hit, &List), "sphere: segment down onto the top hits");
	Near(Hit.Location.Z, 1.0, 1e-4, "sphere: top height");
	Near(Hit.Normal.Z, 1.0, 1e-4, "sphere: normal at the top");
	Check(Hit.Component == Sphere, "sphere: hit component");
	Check(Trace(FVector(13.0f, 0.0f, 0.0f), FVector(7.0f, 0.0f, 0.0f), Hit, &List), "sphere: segment from the side hits");
	Near(Hit.Location.X, 11.0, 1e-4, "sphere: side position");
	Near(Hit.Normal.X, 1.0, 1e-4, "sphere: side normal");
	Check(!Trace(FVector(13.0f, 1.5f, 0.0f), FVector(7.0f, 1.5f, 0.0f), Hit, &List), "sphere: segment passing beside misses");

	// 4) 캡슐 (HalfHeight 2는 반구 끝까지)
	Check(Trace(FVector(23.0f, 0.0f, 0.0f), FVector(17.0f, 0.0f, 0.0f), Hit, &List), "capsule: segment from the side hits");
	Near(Hit.Location.X, 20.5, 1e-3, "capsule: side position (radius 0.5)");
	Near(Hit.Normal.X, 1.0, 1e-3, "capsule: side normal");
	Check(Trace(FVector(20.0f, 0.0f, 5.0f), FVector(20.0f, 0.0f, -5.0f), Hit, &List), "capsule: segment down onto the cap hits");
	Near(Hit.Location.Z, 2.0, 1e-3, "capsule: top of the cap");
	Near(Hit.Normal.Z, 1.0, 1e-3, "capsule: cap normal");
	Check(Hit.Component == Capsule, "capsule: hit component");

	// 5) 메시와 도형 중 가까운 쪽: 구(윗면 z = 1) 아래에 바닥(윗면 z = -0.5)
	AStaticMeshActor* Floor = World->SpawnActor<AStaticMeshActor>();
	UStaticMeshComponent* FloorMesh = Floor ? Floor->GetStaticMeshComponent() : nullptr;
	Check(FloorMesh != nullptr, "floor mesh actor exists");
	if (FloorMesh)
	{
		FloorMesh->SetStaticMesh(Cube);
		FloorMesh->SetRelativeScale3D(FVector(10.0f, 10.0f, 1.0f));
		FloorMesh->SetRelativeLocation(FVector(10.0f, 0.0f, -1.0f));
		Check(Trace(FVector(10.0f, 0.0f, 3.0f), FVector(10.0f, 0.0f, -3.0f), Hit, &List) && Hit.Component == Sphere, "nearest: the sphere above the floor is hit first");
		Check(Trace(FVector(12.0f, 0.0f, 3.0f), FVector(12.0f, 0.0f, -3.0f), Hit, &List) && Hit.Component == FloorMesh, "nearest: beside the sphere the floor is hit");
		Near(Hit.Location.Z, -0.5, 1e-3, "nearest: floor top height");
	}

	// 6) 걸러내기: 블로킹이 꺼진 도형, 무시한 액터, 액터 충돌 끄기
	Sphere->bBlockComponent = false;
	GatherParticleCollisionShapes(World, nullptr, List);
	Check(List.Shapes.Num() == 2, "filter: a non-blocking shape is not collected");
	Sphere->bBlockComponent = true;
	GatherParticleCollisionShapes(World, BoxActor, List);
	Check(List.Shapes.Num() == 2, "filter: the ignored actor's shape is not collected");
	CapsuleActor->SetActorEnableCollision(false);
	GatherParticleCollisionShapes(World, nullptr, List);
	Check(List.Shapes.Num() == 2, "filter: an actor with collision disabled is not collected");
	CapsuleActor->SetActorEnableCollision(true);
	GatherParticleCollisionShapes(World, nullptr, List);
	Check(List.Shapes.Num() == 3, "filter: collected again after the filters are lifted");

	// 7) 충돌 모듈을 통한 끝에서 끝까지: 월드의 박스 윗면(z = 1)에 파티클이 떨어진다. 선분 1.3 → 0.9가 z = 1을 0.75 지점에서 지난다.
	{
		AActor* EmitterActor = World->SpawnActor<AActor>();
		auto* EmitterComponent = EmitterActor ? Cast<UParticleSystemComponent>(EmitterActor->AddComponent(UParticleSystemComponent::StaticClass(), "Emitter")) : nullptr;
		Check(EmitterComponent != nullptr, "emitter component exists");
		if (EmitterComponent)
		{
			FShapeFixture F;
			F.Module->DampingFactor = FVectorDistribution::MakeConstant(FVector(0.5f, 0.5f, 0.5f));
			F.Module->MaxCollisions = FFloatDistribution::MakeConstant(2.0f);
			F.Begin(EmitterComponent);
			FBaseParticle* P = F.AddFalling(1.3f, 1.1f, -2.0f);
			F.Update(0.1f);
			Near(P->BaseVelocity.Z, 1.0, 1e-3, "module: a particle bounces off the box through the world query");
			F.Module->bCollideWithBlockingShapes = false;
			FShapeFixture::Reset(*P, 1.3f, 1.1f, -2.0f);
			F.Update(0.1f);
			Near(P->BaseVelocity.Z, -2.0, 1e-3, "module: bCollideWithBlockingShapes = false ignores the box");
		}
	}

	// 8) 렌더 조건 옵션 (쿼리 대체로 z = 0 평면을 쓰고, 컴포넌트의 렌더 기록만 바꾼다)
	{
		UParticleSystemComponent* Component = NewObject<UParticleSystemComponent>();

		// 카메라 거리: 멀면 이번 프레임은 충돌하지 않는다
		{
			FShapeFixture F;
			F.Module->MaxCollisions = FFloatDistribution::MakeConstant(4.0f);
			F.Module->MaxCollisionDistance = 10.0f;
			F.Begin(Component);
			F.Inst.CollisionTraceOverride = PlaneTraceHitting(nullptr);
			FBaseParticle* P = F.AddFalling(0.3f, 0.1f, -2.0f);
			Component->NoteRendered(FVector(0.0f, 0.0f, 100.0f));
			F.Update(0.1f);
			Near(P->BaseVelocity.Z, -2.0, 1e-6, "distance: a far particle does not collide");
			Component->NoteRendered(FVector(0.0f, 0.0f, 5.0f));
			F.Update(0.1f);
			Near(P->BaseVelocity.Z, 1.0, 1e-4, "distance: a near particle collides");
			F.Module->MaxCollisionDistance = 0.0f;
			FShapeFixture::Reset(*P, 0.3f, 0.1f, -2.0f);
			Component->NoteRendered(FVector(0.0f, 0.0f, 100.0f));
			F.Update(0.1f);
			Near(P->BaseVelocity.Z, 1.0, 1e-4, "distance: 0 turns the limit off");
		}

		// 가시성: 렌더된 적이 없거나 오래되면 충돌하지 않는다
		{
			UParticleSystemComponent* Fresh = NewObject<UParticleSystemComponent>();
			FShapeFixture F;
			F.Module->MaxCollisions = FFloatDistribution::MakeConstant(3.0f);
			F.Module->bCollideOnlyIfVisible = true;
			F.Begin(Fresh);
			F.Inst.CollisionTraceOverride = PlaneTraceHitting(nullptr);
			FBaseParticle* P = F.AddFalling(0.3f, 0.1f, -2.0f);
			F.Update(0.1f);
			Near(P->BaseVelocity.Z, -2.0, 1e-6, "visible: a component that was never rendered does not collide");
			Fresh->NoteRendered(FVector(0.0f, 0.0f, 0.0f));
			F.Update(0.1f);
			Check(P->BaseVelocity.Z > 0.0f, "visible: collides right after being rendered");
			FShapeFixture::Reset(*P, 0.3f, 0.1f, -2.0f);
			Fresh->TickComponent(0.5f);
			F.Update(0.1f);
			Near(P->BaseVelocity.Z, -2.0, 1e-6, "visible: collision stops once the last render is older than 0.1 s");
			Check(!Fresh->WasRecentlyRendered(), "visible: WasRecentlyRendered is false after 0.5 s");
			delete Fresh;
		}
		delete Component;
	}

	// 9) Pawn: 켜면 Pawn 소유 컴포넌트에 맞아도 횟수가 줄지 않는다
	{
		APawn* Pawn = World->SpawnActor<APawn>();
		auto* PawnShape = Pawn ? Cast<UBoxComponent>(Pawn->AddComponent(UBoxComponent::StaticClass(), "PawnBox")) : nullptr;
		Check(PawnShape != nullptr, "pawn: a pawn with a component exists");
		if (PawnShape)
		{
			UParticleSystemComponent* Component = NewObject<UParticleSystemComponent>();
			for (int32 Pass = 0; Pass < 3; ++Pass)
			{
				FShapeFixture F;
				F.Module->MaxCollisions = FFloatDistribution::MakeConstant(2.0f);
				F.Module->bPawnsDoNotDecrementCount = Pass == 0;
				F.Begin(Component);
				// Pass 0, 1: Pawn의 컴포넌트에 맞음 (켬 / 끔), Pass 2: Pawn이 아닌 컴포넌트에 맞음 (켬)
				F.Inst.CollisionTraceOverride = PlaneTraceHitting(Pass == 2 ? static_cast<UPrimitiveComponent*>(Box) : static_cast<UPrimitiveComponent*>(PawnShape));
				if (Pass == 2) F.Module->bPawnsDoNotDecrementCount = true;
				FBaseParticle* P = F.AddFalling(0.3f, 0.1f, -2.0f);
				F.Update(0.1f);
				const int32 Wanted = Pass == 0 ? 2 : 1;
				Check(F.PayloadOf(*P).UsedCollisions == Wanted,
					Pass == 0 ? "pawn: hitting a pawn does not use up a collision when enabled" :
					Pass == 1 ? "pawn: hitting a pawn uses a collision when disabled" : "pawn: hitting a non-pawn still uses a collision when enabled");
			}
			delete Component;
		}
	}

	// 파티클 시스템 액터: 루트는 파티클 컴포넌트이고, 클릭용 에디터 아이콘(빌보드)이 달려 있다. 아이콘에는 파티클이 맞지 않는다.
	{
		AParticleSystemActor* PsActor = World->SpawnActor<AParticleSystemActor>();
		Check(PsActor != nullptr, "particle actor: spawned");
		if (PsActor)
		{
			UParticleSystemComponent* PsComp = PsActor->GetParticleSystemComponent();
			UBillboardComponent* Icon = PsActor->GetBillboardComponent();
			Check(PsComp != nullptr && PsActor->GetRootComponent() == PsComp, "particle actor: the particle component is the root");
			Check(Icon != nullptr && Icon->IsEditorIcon(), "particle actor: has an editor icon billboard");
			Check(Icon != nullptr && Icon->GetMaterial(0) != nullptr, "particle actor: the icon has a material");
			Check(Icon != nullptr && !Icon->bBlockComponent, "particle actor: particles do not collide with the icon");
			Check(Icon != nullptr && Icon->IsHiddenInGame(), "particle actor: the icon is hidden in game");
		}
	}

	// 예제 씬(있을 때만): 파티클 액터가 저장된 그대로 불러와지고, 아이콘 빌보드가 모든 파티클 액터에 붙어 있다
	if (fs::exists("Assets/Scenes/ParticleShowcase.scene"))
	{
		UWorld* SceneWorld = UWorld::CreateWorld(EWorldType::Editor, false);
		if (SceneWorld && FJsonArchive::LoadWorld(SceneWorld, "Assets/Scenes/ParticleShowcase.scene"))
		{
			int32 ActorCount = 0, WithIcon = 0, WithTemplate = 0, PlainParticleActors = 0;
			for (TObjectIterator<AParticleSystemActor> It; It; ++It)
			{
				if (It->GetWorld() != SceneWorld) continue;
				++ActorCount;
				const UBillboardComponent* Icon = It->GetBillboardComponent();
				if (Icon && Icon->IsEditorIcon() && Icon->GetMaterial(0) && Icon->GetAttachParent() == It->GetParticleSystemComponent()) ++WithIcon;
				if (It->GetParticleSystemComponent() && It->GetParticleSystemComponent()->GetTemplate()) ++WithTemplate;
			}
			for (TObjectIterator<UParticleSystemComponent> It; It; ++It)
				if (It->GetWorld() == SceneWorld && !Cast<AParticleSystemActor>(It->GetOwner())) ++PlainParticleActors;
			Check(ActorCount > 0, "showcase scene: has particle actors");
			Check(WithIcon == ActorCount, "showcase scene: every particle actor has its icon billboard attached to the particle component");
			Check(WithTemplate == ActorCount, "showcase scene: every particle actor kept its template");
			Check(PlainParticleActors == 0, "showcase scene: no particle component is left on a plain actor");
		}
		else
		{
			Check(false, "showcase scene: could not be loaded");
		}
		if (GEngine && SceneWorld) GEngine->DestroyWorld(SceneWorld);
	}

	// 10) 저장과 불러오기: 새 옵션 4개
	{
		UParticleModuleCollision* A = NewObject<UParticleModuleCollision>();
		A->bCollideWithBlockingShapes = false;
		A->MaxCollisionDistance = 25.0f;
		A->bCollideOnlyIfVisible = true;
		A->bPawnsDoNotDecrementCount = true;
		json Data = json::object();
		A->Serialize(Data, false);
		UParticleModuleCollision* B = NewObject<UParticleModuleCollision>();
		B->Serialize(Data, true);
		Check(!B->bCollideWithBlockingShapes, "save/load: bCollideWithBlockingShapes");
		Near(B->MaxCollisionDistance, 25.0, 1e-6, "save/load: MaxCollisionDistance");
		Check(B->bCollideOnlyIfVisible, "save/load: bCollideOnlyIfVisible");
		Check(B->bPawnsDoNotDecrementCount, "save/load: bPawnsDoNotDecrementCount");
		delete A;
		delete B;
	}

	if (GEngine) GEngine->DestroyWorld(World);
	HTR_LOG(Info, "[Particle] collision shape self-test: {}", Failed == 0 ? "all passed" : "FAILED");
	return Failed;
}
