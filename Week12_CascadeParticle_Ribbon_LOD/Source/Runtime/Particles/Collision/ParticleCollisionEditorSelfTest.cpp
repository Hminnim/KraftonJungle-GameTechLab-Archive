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
#include "Asset/AssetManager.h"
#include "UObject/UObjectHash.h"
#include <algorithm>
#include <cmath>

// 에디터가 충돌 모듈을 쓰려고 기대하는 것들: 모듈 메뉴 등록, 예제 에셋
int32 RunParticleCollisionEditorSelfTest()
{
	int32 Failed = 0;
	auto Check = [&](bool bOk, const char* Name)
		{
			if (!bOk)
			{
				++Failed;
				HTR_LOG(Error, "[Particle] collision editor FAIL {}", Name);
			}
		};

	// 1) 모듈 클래스가 리플렉션에 등록되어 에디터의 모듈 추가 메뉴에 나온다 (메뉴는 UParticleModule의 파생 클래스 중 만들 수 있는 것을 모은다)
	{
		UClass* Class = FindClass("UParticleModuleCollision");
		Check(Class != nullptr && Class->Constructor != nullptr && Class->IsChildOf(UParticleModule::StaticClass()), "module class is registered and constructible");
		Check(Class != nullptr && Class->Name.find("Test") == FString::npos, "module name is not filtered out as a test module");
	}

	// 2) 예제 에셋 CollisionDemo.particle (있을 때만): 불러와서 가짜 평면 위에서 시뮬레이션하면 파티클이 바닥에서 멈춘다
	{
		const FString Suffix = "CollisionDemo.particle";
		FString DemoPath;
		for (const FString& Path : UAssetManager::GetAssetPaths(EAssetType::ParticleSystem))
		{
			if (Path.size() >= Suffix.size() && Path.compare(Path.size() - Suffix.size(), Suffix.size(), Suffix) == 0)
			{
				DemoPath = Path;
				break;
			}
		}
		if (DemoPath.empty())
		{
			HTR_LOG(Info, "[Particle] collision editor: CollisionDemo.particle not found, demo asset check skipped");
		}
		else
		{
			UParticleSystem* Demo = UAssetManager::LoadParticleSystem(DemoPath);
			UParticleEmitter* Emitter = Demo && Demo->GetNumEmitters() > 0 ? Demo->GetEmitter(0) : nullptr;
			UParticleLODLevel* LOD = Emitter ? Emitter->GetLODLevel(0) : nullptr;
			UParticleModuleCollision* CollisionModule = nullptr;
			if (LOD)
			{
				for (UParticleModule* Module : LOD->Modules)
				{
					if (!CollisionModule) CollisionModule = Cast<UParticleModuleCollision>(Module);
				}
			}
			Check(CollisionModule != nullptr, "demo asset: loads with a collision module");
			if (CollisionModule)
			{
				// 에셋의 완료 동작(Freeze, Kill 등)이나 횟수를 사용자가 바꿔도 통과하도록 "충돌 모듈이 파티클을 바닥에서 막는가"만 본다:
				// 모듈을 켠 실행에서는 파티클이 바닥 아래로 내려가지 않고, 끈 실행에서는 내려간다.
				const bool bWasEnabled = CollisionModule->bEnabled;
				const auto Simulate = [&](bool bCollisionEnabled)
					{
						CollisionModule->bEnabled = bCollisionEnabled;
						Emitter->CacheEmitterModuleInfo();
						FParticleEmitterInstance Inst;
						Inst.Init(Emitter);
						Inst.CollisionTraceOverride = [](const FVector& Start, const FVector& End, FParticleCollisionHit& Out)
							{
								if (!(Start.Z > 0.0f && End.Z < 0.0f)) return false;
								Out.Time = Start.Z / (Start.Z - End.Z);
								Out.Location = Start + (End - Start) * Out.Time;
								Out.Normal = FVector(0.0f, 0.0f, 1.0f);
								return true;
							};
						float LowestZ = 1.0e9f;
						for (int32 Frame = 0; Frame < 150; ++Frame)
						{
							Inst.Tick(1.0f / 60.0f);
							for (int32 i = 0; i < Inst.ActiveParticles; ++i)
							{
								const FBaseParticle& P = *reinterpret_cast<const FBaseParticle*>(Inst.ParticleData + static_cast<size_t>(Inst.ParticleStride) * Inst.ParticleIndices[i]);
								LowestZ = std::min(LowestZ, P.Location.Z);
							}
						}
						Inst.Release();
						return LowestZ;
					};
				const float LowestWithCollision = Simulate(true);
				const float LowestWithout = Simulate(false);
				CollisionModule->bEnabled = bWasEnabled;
				Emitter->CacheEmitterModuleInfo();
				Check(LowestWithCollision >= -0.02f, "demo asset: with the collision module no particle goes below the floor");
				Check(LowestWithout < -0.5f, "demo asset: without the collision module particles fall through the floor");
			}
		}
	}

	HTR_LOG(Info, "[Particle] collision editor self-test: {}", Failed == 0 ? "all passed" : "FAILED");
	return Failed;
}
