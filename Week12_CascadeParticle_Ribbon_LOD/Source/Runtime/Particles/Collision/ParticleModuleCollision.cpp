#include "EnginePCH.h"
#include "Particles/Collision/ParticleModuleCollision.h"
#include "Particles/Collision/ParticleCollisionQuery.h"
#include "Particles/Collision/ParticleCollisionEvent.h"
#include "Components/ParticleSystemComponent.h"
#include "GameFramework/Pawn.h"
#include "Particles/ParticleStats.h"
#include "Particles/TypeData/ParticleModuleTypeDataMesh.h"
#include "Particles/ParticleEmitterInstances.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/ParticleHelper.h"
#include "Particles/ParticleSerialization.h"
#include <cmath>

namespace
{
	// 법선 N인 면에 대한 반사: V - 2 (V . N) N (UE: MirrorByVector). N은 단위 벡터여야 한다.
	FVector Mirror(const FVector& V, const FVector& N)
	{
		return V - N * (2.0f * V.Dot(N));
	}

	// 메시 이미터의 회전 속도 Payload. 메시 TypeData가 아니면 같은 오프셋이 다른 TypeData의 Payload일 수 있어 nullptr이다.
	FMeshRotationPayload* MeshRotationPayloadOf(FParticleEmitterInstance& Owner, FBaseParticle& Particle)
	{
		UParticleEmitter* Template = Owner.Template;
		UParticleLODLevel* LODLevel = Template ? Template->GetLODLevel(0) : nullptr;
		if (!LODLevel || !LODLevel->TypeDataModule || !LODLevel->TypeDataModule->IsA(UParticleModuleTypeDataMesh::StaticClass()) ||
			Template->TypeDataOffset == INDEX_NONE)
		{
			return nullptr;
		}
		return reinterpret_cast<FMeshRotationPayload*>(reinterpret_cast<uint8*>(&Particle) + Template->TypeDataOffset);
	}
}

UParticleModuleCollision::UParticleModuleCollision()
{
	bSpawnModule = true;
	bUpdateModule = true;
}

void UParticleModuleCollision::Serialize(json& Handle, bool bIsLoading)
{
	Super::Serialize(Handle, bIsLoading);
	ParticleJson::SerializeDistribution(Handle, "DampingFactor", DampingFactor, bIsLoading);
	ParticleJson::SerializeDistribution(Handle, "DampingFactorRotation", DampingFactorRotation, bIsLoading);
	ParticleJson::SerializeDistribution(Handle, "MaxCollisions", MaxCollisions, bIsLoading);
	ParticleJson::SerializeDistribution(Handle, "DelayAmount", DelayAmount, bIsLoading);
}

uint32 UParticleModuleCollision::RequiredBytes(UParticleModuleTypeDataBase* TypeData)
{
	return sizeof(FParticleCollisionPayload);
}

void UParticleModuleCollision::Spawn(const FParticleSpawnContext& Context)
{
	SPAWN_INIT
	(void)ActiveParticles;
	(void)ParticleStride;
	PARTICLE_ELEMENT(FParticleCollisionPayload, CollisionPayload);

	// 난수는 DampingFactor, DampingFactorRotation, MaxCollisions, DelayAmount 순서로 쓴다 (UE와 같은 순서)
	CollisionPayload.UsedDampingFactor = DampingFactor.GetValue(Context.Owner.EmitterTime, &Context.Owner.RandomStream);
	CollisionPayload.UsedDampingFactorRotation = DampingFactorRotation.GetValue(Context.Owner.EmitterTime, &Context.Owner.RandomStream);
	CollisionPayload.UsedCollisions = static_cast<int32>(std::floor(MaxCollisions.GetValue(Context.Owner.EmitterTime, &Context.Owner.RandomStream) + 0.5f));
	CollisionPayload.Delay = DelayAmount.GetValue(Context.Owner.EmitterTime, &Context.Owner.RandomStream);

	// 지연이 있으면 그동안 충돌 검사를 건너뛴다. 슬롯은 0으로 채워져 있어서 CollisionHasOccurred는 꺼져 있다.
	if (CollisionPayload.Delay > 0.0f)
	{
		Particle.Flags |= STATE_Particle_DelayCollisions;
	}
}

void UParticleModuleCollision::Update(const FParticleUpdateContext& Context)
{
	FParticleEmitterInstance& Owner = Context.Owner;
	if (Owner.ActiveParticles <= 0)
	{
		return;
	}

	// 최근 렌더되지 않은 컴포넌트는 충돌을 건너뛴다 (옵션). 컴포넌트를 모르는 인스턴스(자체 테스트)는 항상 충돌한다.
	UParticleSystemComponent* OwnerComponent = Owner.OwnerComponent;
	if (bCollideOnlyIfVisible && OwnerComponent && !OwnerComponent->WasRecentlyRendered())
	{
		return;
	}
	// 카메라 거리 제한 (옵션). 카메라를 모르면 제한 없이 충돌한다.
	FVector CameraPosition(0.0f, 0.0f, 0.0f);
	const bool bLimitByDistance = MaxCollisionDistance > 0.0f && OwnerComponent && OwnerComponent->GetCameraPosition(CameraPosition);
	const float MaxDistanceSquared = MaxCollisionDistance * MaxCollisionDistance;

	// 충돌 도형 목록은 파티클마다가 아니라 이 Update에서 한 번만 모은다. 자체 테스트용 쿼리 대체가 있으면 월드를 쓰지 않으므로 모으지 않는다.
	FParticleCollisionShapeList ShapeList;
	const FParticleCollisionShapeList* Shapes = nullptr;
	if (bCollideWithBlockingShapes && OwnerComponent && !Owner.CollisionTraceOverride)
	{
		GatherParticleCollisionShapes(OwnerComponent->GetWorld(), bIgnoreSourceActor ? OwnerComponent->GetOwner() : nullptr, ShapeList);
		Shapes = &ShapeList;
		if (FParticleStats::IsCollecting()) FParticleStats::Pending().CollisionShapes += static_cast<uint32>(ShapeList.Shapes.Num());
	}

	// 로컬 공간 이미터는 파티클 값이 컴포넌트 공간이라 월드로 옮겨서 쏘고 결과를 되돌린다. 컴포넌트 행렬에는 스케일이 없어서 역행렬이 안전하다.
	const bool bLocalSpace = Owner.SimulatesInLocalSpace();
	const FMatrix SimulationToWorld = Owner.SimulationToWorld;
	const FMatrix WorldToSimulation = bLocalSpace ? SimulationToWorld.Inverse() : SimulationToWorld;
	const float SafeDirScalar = DirScalar > 1.0e-4f ? DirScalar : 1.0f;

	BEGIN_UPDATE_LOOP
		PARTICLE_ELEMENT(FParticleCollisionPayload, CollisionPayload);

		// 이미 멈췄거나 충돌을 끈 파티클
		if ((Particle.Flags & STATE_Particle_CollisionIgnoreCheck) != 0)
		{
			CONTINUE_UPDATE_LOOP
		}

		// 지연: 남은 시간을 줄이고, 끝나면 이 프레임부터 검사한다
		if ((Particle.Flags & STATE_Particle_DelayCollisions) != 0)
		{
			CollisionPayload.Delay -= DeltaTime;
			if (CollisionPayload.Delay > 0.0f)
			{
				CONTINUE_UPDATE_LOOP
			}
			Particle.Flags &= ~STATE_Particle_DelayCollisions;
		}

		// 이동 선분. 이 시점에는 이번 프레임 이동이 아직 Location에 반영되지 않았으므로 직접 예측한다.
		FVector Location = Particle.Location + Particle.Velocity * DeltaTime;
		FVector OldLocation = Particle.OldLocation;
		if (bLocalSpace)
		{
			Location = SimulationToWorld.TransformPosition(Location);
			OldLocation = SimulationToWorld.TransformPosition(OldLocation);
		}
		const FVector Move = Location - OldLocation;
		const float MoveLength = Move.Size();
		if (!(MoveLength > 1.0e-6f))
		{
			CONTINUE_UPDATE_LOOP
		}
		const FVector Direction = Move / MoveLength;
		const FVector End = Location + (Direction * Particle.Size) / SafeDirScalar;

		// 카메라에서 너무 먼 파티클은 이번 프레임 충돌을 건너뛴다
		if (bLimitByDistance && (Location - CameraPosition).SizeSquared() > MaxDistanceSquared)
		{
			CONTINUE_UPDATE_LOOP
		}

		FParticleCollisionHit Hit;
		if (FParticleStats::IsCollecting()) ++FParticleStats::Pending().CollisionTraces;
		if (!TraceParticleSegment(Owner, OldLocation, End, CollisionTarget, bIgnoreSourceActor, Hit, Shapes))
		{
			CONTINUE_UPDATE_LOOP
		}

		if (FParticleStats::IsCollecting()) ++FParticleStats::Pending().CollisionHits;

		// 횟수를 줄일지: 바닥만 세고 싶을 때 수직에 가깝지 않은 법선은 세지 않는다
		bool bDecrementCount = true;
		if (bOnlyVerticalNormalsDecrementCount && Hit.Normal.SizeSquared() > 0.0f &&
			std::fabs(Hit.Normal.Z) + VerticalFudgeFactor < 1.0f)
		{
			bDecrementCount = false;
		}
		// Pawn(캐릭터)에 맞은 충돌은 횟수를 쓰지 않는다 (옵션)
		if (bPawnsDoNotDecrementCount && Hit.Component && Cast<APawn>(Hit.Component->GetOwner()) != nullptr)
		{
			bDecrementCount = false;
		}
		if (bDecrementCount)
		{
			--CollisionPayload.UsedCollisions;
		}

		// 이벤트: 파티클을 바꾸기 전에(속도는 충돌 직전 값, Kill이어도 이벤트는 나간다) 컴포넌트에 보고한다
		if (bGenerateCollisionEvents && Owner.OwnerComponent)
		{
			const bool bFinalCollision = CollisionPayload.UsedCollisions <= 0;
			const bool bFirstCollision = (Particle.Flags & STATE_Particle_CollisionHasOccurred) == 0;
			const bool bReport = EventFilter == EParticleCollisionEventFilter::EveryCollision ||
				(EventFilter == EParticleCollisionEventFilter::FirstCollisionOnly && bFirstCollision) ||
				(EventFilter == EParticleCollisionEventFilter::LastCollisionOnly && bFinalCollision);
			if (bReport)
			{
				FParticleCollisionEvent Event;
				Event.EmitterTime = Owner.EmitterTime;
				Event.Location = Hit.Location;
				Event.Normal = Hit.Normal;
				Event.Velocity = bLocalSpace ? SimulationToWorld.TransformVector(Particle.Velocity) : Particle.Velocity;
				Event.Time = Hit.Time;
				Event.HitComponent = Hit.Component;
				Event.ParticleId = Particle.Flags & STATE_CounterMask;
				Event.bFinalCollision = bFinalCollision;
				Owner.OwnerComponent->ReportCollisionEvent(Owner, Event);
			}
		}

		if (CollisionPayload.UsedCollisions > 0)
		{
			// 반사: 영구 값인 BaseVelocity를 반사하고 감쇠한다. Velocity는 이번 프레임만 0이고 다음 Reset이 BaseVelocity로 되돌린다.
			FVector BaseVelocity = Particle.BaseVelocity;
			if (bLocalSpace)
			{
				BaseVelocity = SimulationToWorld.TransformVector(BaseVelocity);
			}
			BaseVelocity = Mirror(BaseVelocity, Hit.Normal) * CollisionPayload.UsedDampingFactor;
			Particle.BaseVelocity = bLocalSpace ? WorldToSimulation.TransformVector(BaseVelocity) : BaseVelocity;

			// 회전 속도 감쇠. 스프라이트는 X만, 메시는 Payload의 회전 속도를 성분별로 곱한다.
			Particle.BaseRotationRate *= CollisionPayload.UsedDampingFactorRotation.X;
			if (FMeshRotationPayload* MeshRotation = MeshRotationPayloadOf(Owner, Particle))
			{
				MeshRotation->RotationRate = MeshRotation->RotationRate * CollisionPayload.UsedDampingFactorRotation;
			}

			// 남은 거리 (1 - Hit.Time)만큼 반사 방향으로 옮긴다. 기준은 월드에서 본 현재 위치다.
			const FVector NewVelocity = Mirror(Direction, Hit.Normal) * MoveLength * CollisionPayload.UsedDampingFactor;
			Particle.Velocity = FVector(0.0f, 0.0f, 0.0f);
			const FVector CurrentWorldLocation = bLocalSpace ? SimulationToWorld.TransformPosition(Particle.Location) : Particle.Location;
			const FVector NewWorldLocation = CurrentWorldLocation + NewVelocity * (1.0f - Hit.Time);
			Particle.Location = bLocalSpace ? WorldToSimulation.TransformPosition(NewWorldLocation) : NewWorldLocation;
		}
		else
		{
			// 마지막 충돌: 충돌 지점에 놓고 완료 동작을 적용한다
			Particle.Location = bLocalSpace ? WorldToSimulation.TransformPosition(Hit.Location) : Hit.Location;
			switch (CollisionCompletionOption)
			{
			case EParticleCollisionComplete::Kill:
				KILL_CURRENT_PARTICLE
				CONTINUE_UPDATE_LOOP
			case EParticleCollisionComplete::Freeze:
				Particle.Flags |= STATE_Particle_Freeze;
				break;
			case EParticleCollisionComplete::HaltCollisions:
				Particle.Flags |= STATE_Particle_IgnoreCollisions;
				break;
			case EParticleCollisionComplete::FreezeTranslation:
				Particle.Flags |= STATE_Particle_FreezeTranslation;
				break;
			case EParticleCollisionComplete::FreezeRotation:
				Particle.Flags |= STATE_Particle_FreezeRotation;
				break;
			case EParticleCollisionComplete::FreezeMovement:
				Particle.Flags |= STATE_Particle_FreezeTranslation | STATE_Particle_FreezeRotation;
				break;
			}
		}
		Particle.Flags |= STATE_Particle_CollisionHasOccurred;
	END_UPDATE_LOOP
}
