#include "EnginePCH.h"
#include "Particles/Collision/ParticleCollisionSelfTest.h"
#include "Particles/Collision/ParticleCollisionQuery.h"
#include "Particles/Collision/ParticleCollisionEvent.h"
#include "Particles/Collision/ParticleModuleCollision.h"
#include "Components/ParticleSystemComponent.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/ParticleModule.h"
#include "Particles/ParticleHelper.h"
#include "Particles/ParticleEmitterInstances.h"
#include <cmath>
#include <cstring>
#include <functional>

namespace
{
	// z = 0 평면. 위에서 아래로 가로지르는 선분만 맞는다.
	std::function<bool(const FVector&, const FVector&, FParticleCollisionHit&)> PlaneTrace()
	{
		return [](const FVector& Start, const FVector& End, FParticleCollisionHit& Out)
			{
				if (!(Start.Z > 0.0f && End.Z < 0.0f)) return false;
				Out.Time = Start.Z / (Start.Z - End.Z);
				Out.Location = Start + (End - Start) * Out.Time;
				Out.Normal = FVector(0.0f, 0.0f, 1.0f);
				return true;
			};
	}

	// 충돌 모듈 하나가 든 이미터 하나. 인스턴스의 OwnerComponent만 컴포넌트로 두고(컴포넌트는 인스턴스를 소유하지 않는다), 파티클은 직접 쓴다.
	struct FEventFixture
	{
		UParticleSystem* System = nullptr;
		UParticleEmitter* Emitter = nullptr;
		UParticleLODLevel* LOD = nullptr;
		UParticleModuleCollision* Module = nullptr;
		UParticleSystemComponent* Component = nullptr;
		FParticleEmitterInstance Inst;
		int32 Offset = INDEX_NONE;

		FEventFixture()
		{
			System = NewObject<UParticleSystem>();
			Emitter = System->AddEmitter();
			LOD = Emitter->AddLODLevel();
			Module = Cast<UParticleModuleCollision>(LOD->AddModule(UParticleModuleCollision::StaticClass()));
			Component = NewObject<UParticleSystemComponent>();
		}

		void Begin()
		{
			Emitter->CacheEmitterModuleInfo();
			Offset = Emitter->GetModuleDataOffset(Module);
			Inst.Init(Emitter);
			Inst.Resize(4);
			Inst.CollisionTraceOverride = PlaneTrace();
			Inst.OwnerComponent = Component;
		}

		~FEventFixture()
		{
			Inst.OwnerComponent = nullptr;
			Inst.Release();
			delete Component;
			DestroyParticleSystem(System);
		}

		// 기준 장면: 속도 (0,0,-2), 이전 z = 0.3, 현재 z = 0.1 → dt 0.1이면 선분이 z = 0을 0.75 지점에서 가로지른다
		FBaseParticle* AddFallingParticle(uint32 Id)
		{
			uint8* Slot = Inst.ParticleData + static_cast<size_t>(Inst.ParticleStride) * Inst.ParticleIndices[Inst.ActiveParticles];
			std::memset(Slot, 0, Inst.ParticleSize);
			FBaseParticle* P = reinterpret_cast<FBaseParticle*>(Slot);
			ResetGeometry(*P);
			P->OneOverMaxLifetime = 0.1f;
			const FParticleSpawnContext Context{ Inst, Offset, 0.0f, P };
			Module->Spawn(Context);
			P->Flags |= (Id & STATE_CounterMask);
			++Inst.ActiveParticles;
			return P;
		}

		static void ResetGeometry(FBaseParticle& P)
		{
			P.Location = FVector(0.0f, 0.0f, 0.1f);
			P.OldLocation = FVector(0.0f, 0.0f, 0.3f);
			P.Velocity = FVector(0.0f, 0.0f, -2.0f);
			P.BaseVelocity = P.Velocity;
		}

		void Update(float DeltaTime)
		{
			const FParticleUpdateContext Context{ Inst, Offset, DeltaTime };
			Module->Update(Context);
		}
	};
}

int32 RunParticleCollisionEventSelfTest()
{
	int32 Failed = 0;
	auto Check = [&](bool bOk, const char* Name)
		{
			if (!bOk)
			{
				++Failed;
				HTR_LOG(Error, "[Particle] collision event FAIL {}", Name);
			}
		};
	auto Near = [&](double Got, double Want, double Tol, const char* Name)
		{
			if (std::fabs(Got - Want) > Tol)
			{
				++Failed;
				HTR_LOG(Error, "[Particle] collision event FAIL {} (got {}, want {})", Name, Got, Want);
			}
		};

	const float Dt = 0.1f;

	// 1) 이벤트 값
	{
		FEventFixture F;
		F.Module->MaxCollisions = FFloatDistribution::MakeConstant(2.0f);
		F.Module->bGenerateCollisionEvents = true;
		F.Begin();
		F.AddFallingParticle(7);
		F.Update(Dt);
		const TArray<FParticleCollisionEvent>& Events = F.Component->GetCollisionEvents();
		Check(Events.Num() == 1, "values: one event for one collision");
		if (Events.Num() == 1)
		{
			const FParticleCollisionEvent& E = Events[0];
			Near(E.Location.Z, 0.0, 1e-5, "values: Location is the hit point");
			Near(E.Normal.Z, 1.0, 1e-6, "values: Normal");
			Near(E.Velocity.Z, -2.0, 1e-6, "values: Velocity is the velocity before the collision");
			Near(E.Time, 0.75, 1e-5, "values: Time");
			Check(E.ParticleId == 7, "values: ParticleId is the spawn counter");
			Check(!E.bFinalCollision, "values: not the final collision (2 allowed)");
			Check(E.EmitterIndex == -1, "values: emitter index is -1 when the component does not own the instance");
		}
	}

	// 2) 끄면 이벤트가 없다
	{
		FEventFixture F;
		F.Module->MaxCollisions = FFloatDistribution::MakeConstant(2.0f);
		F.Module->bGenerateCollisionEvents = false;
		F.Begin();
		F.AddFallingParticle(0);
		F.Update(Dt);
		Check(F.Component->GetCollisionEvents().IsEmpty(), "disabled: no event");
	}

	// 3) 필터: 한 파티클이 3번 충돌한다 (완료 동작은 Freeze라 세 번째에서 멈춘다). 충돌마다 위치만 되돌려 같은 장면을 반복한다.
	{
		struct FCase { EParticleCollisionEventFilter Filter; int32 Wanted; int32 FinalFlagged; const char* Name; };
		const FCase Cases[] = {
			{ EParticleCollisionEventFilter::EveryCollision, 3, 1, "filter EveryCollision" },
			{ EParticleCollisionEventFilter::FirstCollisionOnly, 1, 0, "filter FirstCollisionOnly" },
			{ EParticleCollisionEventFilter::LastCollisionOnly, 1, 1, "filter LastCollisionOnly" },
		};
		for (const FCase& C : Cases)
		{
			FEventFixture F;
			F.Module->MaxCollisions = FFloatDistribution::MakeConstant(3.0f);
			F.Module->CollisionCompletionOption = EParticleCollisionComplete::Freeze;
			F.Module->bGenerateCollisionEvents = true;
			F.Module->EventFilter = C.Filter;
			F.Begin();
			FBaseParticle* P = F.AddFallingParticle(0);
			for (int32 k = 0; k < 3; ++k)
			{
				FEventFixture::ResetGeometry(*P);
				F.Update(Dt);
			}
			const TArray<FParticleCollisionEvent>& Events = F.Component->GetCollisionEvents();
			Check(Events.Num() == C.Wanted, C.Name);
			int32 FinalCount = 0;
			for (const FParticleCollisionEvent& E : Events) FinalCount += E.bFinalCollision ? 1 : 0;
			Check(FinalCount == C.FinalFlagged, C.Name);
		}
	}

	// 4) 마지막 충돌이 Kill이어도 이벤트는 나간다
	{
		FEventFixture F;
		F.Module->MaxCollisions = FFloatDistribution::MakeConstant(1.0f);
		F.Module->CollisionCompletionOption = EParticleCollisionComplete::Kill;
		F.Module->bGenerateCollisionEvents = true;
		F.Begin();
		F.AddFallingParticle(0);
		F.Update(Dt);
		Check(F.Inst.ActiveParticles == 0, "kill: the particle is gone");
		Check(F.Component->GetCollisionEvents().Num() == 1 && F.Component->GetCollisionEvents()[0].bFinalCollision, "kill: the final event is still reported");
	}

	// 5) 상한: 1024개를 넘으면 버리고 센다
	{
		UParticleSystemComponent* Component = NewObject<UParticleSystemComponent>();
		FParticleEmitterInstance Source;
		for (int32 i = 0; i < UParticleSystemComponent::MaxCollisionEventsPerTick + 76; ++i)
		{
			Component->ReportCollisionEvent(Source, FParticleCollisionEvent{});
		}
		Check(Component->GetCollisionEvents().Num() == UParticleSystemComponent::MaxCollisionEventsPerTick, "cap: the queue holds at most the limit");
		Check(Component->GetDroppedCollisionEventCount() == 76, "cap: the dropped events are counted");
		delete Component;
	}

	// 6) 컴포넌트 Tick 전체: 이미터 번호, 델리게이트 호출 수, 다음 Tick의 큐 비우기
	{
		UParticleSystem* System = NewObject<UParticleSystem>();
		UParticleEmitter* Emitter = System->AddEmitter();
		UParticleLODLevel* LOD = Emitter->AddLODLevel();
		auto* Lifetime = Cast<UParticleModuleLifetime>(LOD->AddModule(UParticleModuleLifetime::StaticClass()));
		auto* Location = Cast<UParticleModuleLocation>(LOD->AddModule(UParticleModuleLocation::StaticClass()));
		auto* Velocity = Cast<UParticleModuleVelocity>(LOD->AddModule(UParticleModuleVelocity::StaticClass()));
		auto* Collision = Cast<UParticleModuleCollision>(LOD->AddModule(UParticleModuleCollision::StaticClass()));
		LOD->AddModule(UParticleModuleSpawn::StaticClass());
		LOD->SpawnModule->Rate = FFloatDistribution::MakeConstant(60.0f);
		Lifetime->Lifetime = FFloatDistribution::MakeConstant(10.0f);
		Location->StartLocation = FVectorDistribution::MakeConstant(FVector(0.0f, 0.0f, 0.5f));
		Velocity->StartVelocity = FVectorDistribution::MakeConstant(FVector(0.0f, 0.0f, -1.0f));
		Collision->MaxCollisions = FFloatDistribution::MakeConstant(1.0f);
		Collision->CollisionCompletionOption = EParticleCollisionComplete::Kill;
		Collision->bGenerateCollisionEvents = true;
		Emitter->CacheEmitterModuleInfo();

		UParticleSystemComponent* Component = NewObject<UParticleSystemComponent>();
		Component->SetTemplate(System);
		const float TickDt = 1.0f / 60.0f;
		Component->TickComponent(TickDt);
		FParticleEmitterInstance* Instance = Component->GetEmitterInstance(0);
		Check(Instance != nullptr && Instance->OwnerComponent == Component, "tick: the instance knows its component");
		Check(Component->GetCollisionEvents().IsEmpty(), "tick: no event on the first tick");
		if (Instance)
		{
			Instance->CollisionTraceOverride = PlaneTrace();
		}
		int32 DelegateCalls = 0;
		const FDelegateHandle Handle = Component->OnParticleCollide.Add([&](UParticleSystemComponent* Source, const FParticleCollisionEvent&) { if (Source == Component) ++DelegateCalls; });
		int32 TotalEvents = 0;
		int32 TicksWithoutEvents = 0;
		bool bAllFromEmitter0 = true;
		bool bDeliveredEveryTick = true;
		for (int32 Frame = 0; Frame < 90; ++Frame)
		{
			const int32 Before = DelegateCalls;
			Component->TickComponent(TickDt);
			const int32 Count = Component->GetCollisionEvents().Num();
			TotalEvents += Count;
			TicksWithoutEvents += Count == 0 ? 1 : 0;
			bDeliveredEveryTick &= (DelegateCalls - Before) == Count;
			for (const FParticleCollisionEvent& E : Component->GetCollisionEvents()) bAllFromEmitter0 &= E.EmitterIndex == 0;
		}
		Check(TotalEvents > 30, "tick: particles reaching the floor produced events");
		Check(bDeliveredEveryTick && DelegateCalls == TotalEvents, "tick: the delegate is called once per event, in the tick that produced it");
		Check(TicksWithoutEvents > 20, "tick: the queue is cleared at the start of every tick (early ticks have none)");
		Check(bAllFromEmitter0, "tick: EmitterIndex is the emitter's index in the component");
		Component->OnParticleCollide.Remove(Handle);
		Check(!Component->OnParticleCollide.IsBound(), "tick: the handler can be removed");
		delete Component;
		DestroyParticleSystem(System);
	}

	// 7) 저장과 불러오기: 새 옵션이 살아남는다
	{
		UParticleModuleCollision* A = NewObject<UParticleModuleCollision>();
		A->bGenerateCollisionEvents = true;
		A->EventFilter = EParticleCollisionEventFilter::LastCollisionOnly;
		json Data = json::object();
		A->Serialize(Data, false);
		UParticleModuleCollision* B = NewObject<UParticleModuleCollision>();
		B->Serialize(Data, true);
		Check(B->bGenerateCollisionEvents, "save/load: bGenerateCollisionEvents");
		Check(B->EventFilter == EParticleCollisionEventFilter::LastCollisionOnly, "save/load: EventFilter");
		delete A;
		delete B;
	}

	HTR_LOG(Info, "[Particle] collision event self-test: {}", Failed == 0 ? "all passed" : "FAILED");
	return Failed;
}
