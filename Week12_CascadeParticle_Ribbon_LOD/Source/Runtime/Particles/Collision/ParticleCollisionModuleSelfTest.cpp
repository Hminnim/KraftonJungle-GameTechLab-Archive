#include "EnginePCH.h"
#include "Particles/Collision/ParticleCollisionSelfTest.h"
#include "Particles/Collision/ParticleCollisionQuery.h"
#include "Particles/Collision/ParticleModuleCollision.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/ParticleModule.h"
#include "Particles/ParticleHelper.h"
#include "Particles/ParticleEmitterInstances.h"
#include "Math/Transform.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

// ---------------------------------------------------------------------------
// 충돌 모듈 (월드 없이: 인스턴스의 CollisionTraceOverride로 z = 0 평면을 쓴다)
// ---------------------------------------------------------------------------
namespace
{
	// z = 0 평면. 위에서 아래로 가로지르는 선분만 맞고 법선은 인자로 정한다.
	std::function<bool(const FVector&, const FVector&, FParticleCollisionHit&)> MakePlaneTrace(const FVector& Normal)
	{
		return [Normal](const FVector& Start, const FVector& End, FParticleCollisionHit& Out)
			{
				if (!(Start.Z > 0.0f && End.Z < 0.0f)) return false;
				Out.Time = Start.Z / (Start.Z - End.Z);
				Out.Location = Start + (End - Start) * Out.Time;
				Out.Normal = Normal;
				return true;
			};
	}

	// 충돌 모듈 하나를 넣은 이미터와 인스턴스. 파티클은 직접 슬롯에 써 넣고 Spawn / Update를 직접 부른다.
	struct FCollisionFixture
	{
		UParticleSystem* System = nullptr;
		UParticleEmitter* Emitter = nullptr;
		UParticleLODLevel* LOD = nullptr;
		UParticleModuleCollision* Module = nullptr;
		UParticleModuleRequired* Required = nullptr;
		FParticleEmitterInstance Inst;
		int32 Offset = INDEX_NONE;

		explicit FCollisionFixture(bool bLocalSpace = false)
		{
			System = NewObject<UParticleSystem>();
			Emitter = System->AddEmitter();
			LOD = Emitter->AddLODLevel();
			if (bLocalSpace)
			{
				Required = Cast<UParticleModuleRequired>(LOD->AddModule(UParticleModuleRequired::StaticClass()));
				Required->bUseLocalSpace = true;
			}
			Module = Cast<UParticleModuleCollision>(LOD->AddModule(UParticleModuleCollision::StaticClass()));
		}

		// 모듈 값을 다 정한 뒤에 부른다 (레이아웃 계산, 인스턴스 만들기, 평면 설치)
		void Begin(const FVector& PlaneNormal = FVector(0.0f, 0.0f, 1.0f))
		{
			Emitter->CacheEmitterModuleInfo();
			Offset = Emitter->GetModuleDataOffset(Module);
			Inst.Init(Emitter);
			Inst.Resize(4);
			Inst.CollisionTraceOverride = MakePlaneTrace(PlaneNormal);
		}

		~FCollisionFixture()
		{
			Inst.Release();
			DestroyParticleSystem(System);
		}

		FBaseParticle* AddParticle(const FVector& Location, const FVector& Old, const FVector& Velocity, const FVector& Size = FVector(0.0f, 0.0f, 0.0f))
		{
			uint8* Slot = Inst.ParticleData + static_cast<size_t>(Inst.ParticleStride) * Inst.ParticleIndices[Inst.ActiveParticles];
			std::memset(Slot, 0, Inst.ParticleSize);
			FBaseParticle* P = reinterpret_cast<FBaseParticle*>(Slot);
			P->Location = Location;
			P->OldLocation = Old;
			P->Velocity = Velocity;
			P->BaseVelocity = Velocity;
			P->Size = Size;
			P->BaseSize = Size;
			P->OneOverMaxLifetime = 0.1f;
			const FParticleSpawnContext Context{ Inst, Offset, 0.0f, P };
			Module->Spawn(Context);
			++Inst.ActiveParticles;
			return P;
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
}

int32 RunParticleCollisionModuleSelfTest()
{
	int32 Failed = 0;
	auto Check = [&](bool bOk, const char* Name)
		{
			if (!bOk)
			{
				++Failed;
				HTR_LOG(Error, "[Particle] collision module FAIL {}", Name);
			}
		};
	auto Near = [&](double Got, double Want, double Tol, const char* Name)
		{
			if (std::fabs(Got - Want) > Tol)
			{
				++Failed;
				HTR_LOG(Error, "[Particle] collision module FAIL {} (got {}, want {})", Name, Got, Want);
			}
		};

	// 공통 장면: dt = 0.1, 속도 (0,0,-2), 이전 위치 z = 0.3, 현재 z = 0.1 → 예측 z = -0.1. 선분 0.3 → -0.1이 z = 0을 0.75 지점에서 가로지른다.
	const float Dt = 0.1f;
	const FVector Down(0.0f, 0.0f, -2.0f);

	// 1) 레이아웃: Payload 32바이트가 FBaseParticle 바로 뒤에 붙는다
	{
		FCollisionFixture F;
		F.Begin();
		Check(F.Emitter->ParticleSize == sizeof(FBaseParticle) + 32u, "layout: particle size is 128 + 32");
		Check(F.Offset == static_cast<int32>(sizeof(FBaseParticle)), "layout: payload offset is 128");
	}

	// 2) 반사와 감쇠, 횟수
	{
		FCollisionFixture F;
		F.Module->DampingFactor = FVectorDistribution::MakeConstant(FVector(0.5f, 0.5f, 0.5f));
		F.Module->MaxCollisions = FFloatDistribution::MakeConstant(2.0f);
		F.Begin();
		FBaseParticle* P = F.AddParticle(FVector(0.0f, 0.0f, 0.1f), FVector(0.0f, 0.0f, 0.3f), Down);
		Check(F.PayloadOf(*P).UsedCollisions == 2, "reflect: payload has the sampled MaxCollisions");
		Near(F.PayloadOf(*P).UsedDampingFactor.X, 0.5, 1e-6, "reflect: payload has the sampled damping");
		F.Update(Dt);
		Near(P->BaseVelocity.Z, 1.0, 1e-5, "reflect: BaseVelocity is mirrored and damped (2 * 0.5)");
		Near(P->Velocity.Z, 0.0, 1e-6, "reflect: Velocity is 0 for this frame");
		Near(P->Location.Z, 0.15, 1e-5, "reflect: Location moved by the remaining (1 - Time) of the reflected move");
		Check(F.PayloadOf(*P).UsedCollisions == 1, "reflect: one collision used");
		Check((P->Flags & STATE_Particle_CollisionHasOccurred) != 0, "reflect: CollisionHasOccurred is set");
		Check(F.Inst.ActiveParticles == 1, "reflect: the particle is alive");

		// 다음 프레임의 Reset이 Velocity를 반사된 BaseVelocity로 복원한다 (이미터 Reset 단계와 같은 대입)
		P->Velocity = P->BaseVelocity;
		Near(P->Velocity.Z, 1.0, 1e-5, "reflect: the next Reset restores Velocity from the new BaseVelocity");
	}

	// 3) 마지막 충돌의 완료 동작
	{
		struct FCase { EParticleCollisionComplete Option; bool bAlive; uint32 FlagsWanted; uint32 FlagsUnwanted; const char* Name; };
		const FCase Cases[] = {
			{ EParticleCollisionComplete::Kill, false, 0, 0, "complete Kill" },
			{ EParticleCollisionComplete::Freeze, true, STATE_Particle_Freeze, 0, "complete Freeze" },
			{ EParticleCollisionComplete::HaltCollisions, true, STATE_Particle_IgnoreCollisions, STATE_Particle_Freeze | STATE_Particle_FreezeTranslation | STATE_Particle_FreezeRotation, "complete HaltCollisions" },
			{ EParticleCollisionComplete::FreezeTranslation, true, STATE_Particle_FreezeTranslation, STATE_Particle_Freeze | STATE_Particle_FreezeRotation, "complete FreezeTranslation" },
			{ EParticleCollisionComplete::FreezeRotation, true, STATE_Particle_FreezeRotation, STATE_Particle_Freeze | STATE_Particle_FreezeTranslation, "complete FreezeRotation" },
			{ EParticleCollisionComplete::FreezeMovement, true, STATE_Particle_FreezeTranslation | STATE_Particle_FreezeRotation, STATE_Particle_Freeze, "complete FreezeMovement" },
		};
		for (const FCase& C : Cases)
		{
			FCollisionFixture F;
			F.Module->MaxCollisions = FFloatDistribution::MakeConstant(1.0f);
			F.Module->CollisionCompletionOption = C.Option;
			F.Begin();
			FBaseParticle* P = F.AddParticle(FVector(0.0f, 0.0f, 0.1f), FVector(0.0f, 0.0f, 0.3f), Down);
			F.Update(Dt);
			Check((F.Inst.ActiveParticles == 1) == C.bAlive, C.Name);
			if (C.bAlive)
			{
				Check((P->Flags & C.FlagsWanted) == C.FlagsWanted, C.Name);
				Check((P->Flags & C.FlagsUnwanted) == 0, C.Name);
				Check((P->Flags & STATE_Particle_CollisionHasOccurred) != 0, C.Name);
				Near(P->Location.Z, 0.0, 1e-5, C.Name);
			}
		}
	}

	// 4) 수직 법선만 횟수를 줄이는 옵션
	{
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			FCollisionFixture F;
			F.Module->MaxCollisions = FFloatDistribution::MakeConstant(2.0f);
			F.Module->bOnlyVerticalNormalsDecrementCount = true;
			F.Begin(Pass == 0 ? FVector(0.0f, 0.0f, 1.0f) : FVector(0.8f, 0.0f, 0.6f));
			FBaseParticle* P = F.AddParticle(FVector(0.0f, 0.0f, 0.1f), FVector(0.0f, 0.0f, 0.3f), Down);
			F.Update(Dt);
			Check(F.PayloadOf(*P).UsedCollisions == (Pass == 0 ? 1 : 2), Pass == 0 ? "vertical normal decrements the count" : "slanted normal does not decrement the count");
		}
	}

	// 5) 지연: 지연이 남아 있는 동안은 충돌하지 않고, 끝나는 프레임부터 검사한다
	{
		FCollisionFixture F;
		F.Module->MaxCollisions = FFloatDistribution::MakeConstant(2.0f);
		F.Module->DelayAmount = FFloatDistribution::MakeConstant(0.15f);
		F.Begin();
		FBaseParticle* P = F.AddParticle(FVector(0.0f, 0.0f, 0.1f), FVector(0.0f, 0.0f, 0.3f), Down);
		Check((P->Flags & STATE_Particle_DelayCollisions) != 0, "delay: flag is set at spawn");
		F.Update(Dt);
		Near(P->BaseVelocity.Z, -2.0, 1e-6, "delay: no collision while the delay remains");
		Check((P->Flags & STATE_Particle_DelayCollisions) != 0, "delay: flag stays set");
		F.Update(Dt);
		Near(P->BaseVelocity.Z, 1.0, 1e-5, "delay: collision starts in the frame the delay ends (0.15 - 2 * 0.1 < 0)");
		Check((P->Flags & STATE_Particle_DelayCollisions) == 0, "delay: flag cleared");
	}

	// 6) 충돌을 끈 파티클은 건너뛴다
	{
		FCollisionFixture F;
		F.Module->MaxCollisions = FFloatDistribution::MakeConstant(2.0f);
		F.Begin();
		FBaseParticle* P = F.AddParticle(FVector(0.0f, 0.0f, 0.1f), FVector(0.0f, 0.0f, 0.3f), Down);
		P->Flags |= STATE_Particle_IgnoreCollisions;
		F.Update(Dt);
		Near(P->BaseVelocity.Z, -2.0, 1e-6, "ignore flag: particle is skipped");
		Check(F.PayloadOf(*P).UsedCollisions == 2, "ignore flag: count unchanged");
	}

	// 7) 선분이 평면에 닿지 않으면 아무것도 바꾸지 않는다
	{
		FCollisionFixture F;
		F.Begin();
		FBaseParticle* P = F.AddParticle(FVector(0.0f, 0.0f, 1.1f), FVector(0.0f, 0.0f, 1.3f), Down);
		F.Update(Dt);
		Near(P->BaseVelocity.Z, -2.0, 1e-6, "miss: velocity unchanged");
		Near(P->Location.Z, 1.1, 1e-6, "miss: location unchanged");
	}

	// 8) 로컬 공간: 컴포넌트가 z = +5에 있으면 월드 바닥 z = 0은 로컬 z = -5다. 결과는 월드 결과에서 5를 뺀 것과 같아야 한다.
	{
		FCollisionFixture F(true);
		F.Module->DampingFactor = FVectorDistribution::MakeConstant(FVector(0.5f, 0.5f, 0.5f));
		F.Module->MaxCollisions = FFloatDistribution::MakeConstant(2.0f);
		F.Begin();
		F.Inst.SetComponentToWorld(FMatrix::MakeTranslation(FVector(0.0f, 0.0f, 5.0f)));
		FBaseParticle* P = F.AddParticle(FVector(0.0f, 0.0f, 0.1f - 5.0f), FVector(0.0f, 0.0f, 0.3f - 5.0f), Down);
		F.Update(Dt);
		Near(P->BaseVelocity.Z, 1.0, 1e-4, "local space: BaseVelocity is mirrored");
		Near(P->Location.Z, 0.15 - 5.0, 1e-4, "local space: Location is moved in local coordinates");
	}

	// 9) 저장과 불러오기: UPROPERTY와 분포가 모두 살아남는다
	{
		UParticleModuleCollision* A = NewObject<UParticleModuleCollision>();
		A->DampingFactor = FVectorDistribution::MakeConstant(FVector(0.1f, 0.2f, 0.3f));
		A->DampingFactorRotation = FVectorDistribution::MakeConstant(FVector(0.4f, 0.5f, 0.6f));
		A->MaxCollisions = FFloatDistribution::MakeConstant(5.0f);
		A->DelayAmount = FFloatDistribution::MakeConstant(0.7f);
		A->CollisionCompletionOption = EParticleCollisionComplete::FreezeMovement;
		A->CollisionTarget = EParticleCollisionTarget::BlockingStaticMeshes;
		A->DirScalar = 2.5f;
		A->bOnlyVerticalNormalsDecrementCount = true;
		A->VerticalFudgeFactor = 0.2f;
		A->bIgnoreSourceActor = false;
		json Data = json::object();
		A->Serialize(Data, false);
		UParticleModuleCollision* B = NewObject<UParticleModuleCollision>();
		B->Serialize(Data, true);
		FRandomStream Stream;
		const FVector D = B->DampingFactor.GetValue(0.0f, &Stream);
		const FVector DR = B->DampingFactorRotation.GetValue(0.0f, &Stream);
		Near(D.X, 0.1, 1e-6, "save/load: DampingFactor.X");
		Near(D.Z, 0.3, 1e-6, "save/load: DampingFactor.Z");
		Near(DR.Y, 0.5, 1e-6, "save/load: DampingFactorRotation.Y");
		Near(B->MaxCollisions.GetValue(0.0f, &Stream), 5.0, 1e-6, "save/load: MaxCollisions");
		Near(B->DelayAmount.GetValue(0.0f, &Stream), 0.7, 1e-6, "save/load: DelayAmount");
		Check(B->CollisionCompletionOption == EParticleCollisionComplete::FreezeMovement, "save/load: CollisionCompletionOption");
		Check(B->CollisionTarget == EParticleCollisionTarget::BlockingStaticMeshes, "save/load: CollisionTarget");
		Near(B->DirScalar, 2.5, 1e-6, "save/load: DirScalar");
		Check(B->bOnlyVerticalNormalsDecrementCount, "save/load: bOnlyVerticalNormalsDecrementCount");
		Near(B->VerticalFudgeFactor, 0.2, 1e-6, "save/load: VerticalFudgeFactor");
		Check(!B->bIgnoreSourceActor, "save/load: bIgnoreSourceActor");
		delete A;
		delete B;
	}

	// 10) Tick 전체: 모듈 순서(Reset -> Update -> Spawn -> 위치 적분) 안에서 파티클이 바닥 아래로 내려가지 않는다
	{
		struct FRun { bool bCollisionEnabled; int32 MaxCollisions; const char* Name; };
		const FRun Runs[] = {
			{ true, 100, "tick bounce" },
			{ true, 1, "tick kill on first hit" },
			{ false, 1, "tick without the collision module" },
		};
		for (const FRun& Run : Runs)
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
			Collision->DampingFactor = FVectorDistribution::MakeConstant(FVector(0.8f, 0.8f, 0.8f));
			Collision->MaxCollisions = FFloatDistribution::MakeConstant(static_cast<float>(Run.MaxCollisions));
			Collision->bEnabled = Run.bCollisionEnabled;
			Emitter->CacheEmitterModuleInfo();

			FParticleEmitterInstance Inst;
			Inst.Init(Emitter);
			Inst.CollisionTraceOverride = MakePlaneTrace(FVector(0.0f, 0.0f, 1.0f));
			float LowestZ = 1.0e9f;
			bool bBounced = false;
			for (int32 Frame = 0; Frame < 90; ++Frame)
			{
				Inst.Tick(1.0f / 60.0f);
				for (int32 i = 0; i < Inst.ActiveParticles; ++i)
				{
					const FBaseParticle& P = *reinterpret_cast<const FBaseParticle*>(Inst.ParticleData + static_cast<size_t>(Inst.ParticleStride) * Inst.ParticleIndices[i]);
					LowestZ = std::min(LowestZ, P.Location.Z);
					bBounced |= P.Velocity.Z > 0.1f;
				}
			}
			if (Run.bCollisionEnabled && Run.MaxCollisions > 1)
			{
				Check(LowestZ >= -0.02f, "tick bounce: no particle ends a frame below the floor");
				Check(bBounced, "tick bounce: some particle moved upward after hitting the floor");
				Check(Inst.ActiveParticles >= 80, "tick bounce: bouncing particles stay alive");
			}
			else if (Run.bCollisionEnabled)
			{
				Check(Inst.ActiveParticles < 45, "tick kill: particles that reached the floor were killed");
				Check(Inst.ActiveParticles > 10, "tick kill: particles still in flight are alive");
			}
			else
			{
				Check(Inst.ActiveParticles >= 80, "tick without collision: nothing is killed");
				Check(LowestZ < -0.5f, "tick without collision: particles fall through the floor");
			}
			Inst.Release();
			DestroyParticleSystem(System);
		}
	}

	HTR_LOG(Info, "[Particle] collision module self-test: {}", Failed == 0 ? "all passed" : "FAILED");
	return Failed;
}
