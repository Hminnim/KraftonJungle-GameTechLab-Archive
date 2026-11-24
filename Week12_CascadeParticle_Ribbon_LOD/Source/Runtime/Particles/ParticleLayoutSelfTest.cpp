#include "EnginePCH.h"
#include "Particles/ParticleLayoutSelfTest.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/ParticleHelper.h"
#include "Particles/ParticleEmitterInstances.h"
#include "Particles/ParticleDistribution.h"
#include "Components/ParticleSystemComponent.h"
#include "Math/Transform.h"
#include "Particles/ParticleSpriteRender.h"
#include "Particles/ParticleStats.h"
#include "Core/ResourcePaths.h"
#include "Render/RenderPacket.h"
#include "Asset/AssetManager.h"
#include "Render/Material.h"
#include "Particles/ParticleSerialization.h"
#include "Particles/TypeData/ParticleModuleTypeDataMesh.h"
#include "Render/Mesh.h"
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <cstring>
#include <cmath>
#include <string>

namespace
{
	// 시스템 → 이미터 → LOD 0 한 벌을 만들고, 범위를 벗어나면 모두 해제한다
	struct FLayoutFixture
	{
		UParticleSystem* System = nullptr;
		UParticleEmitter* Emitter = nullptr;
		UParticleLODLevel* LOD = nullptr;

		FLayoutFixture()
		{
			System = NewObject<UParticleSystem>();
			Emitter = System->AddEmitter();
			LOD = Emitter->AddLODLevel();
		}

		~FLayoutFixture()
		{
			// 소유 트리(이미터, LOD, 모듈)는 DestroyParticleSystem이 함께 지운다
			DestroyParticleSystem(System);
		}

		UParticleModule* Add(UClass* Class) { return LOD->AddModule(Class); }

		// 상수 Rate의 Spawn 모듈을 만들어 슬롯에 넣는다. 이미 있으면 Rate만 바꾼다.
		UParticleModuleSpawn* SetSpawnRate(float Rate)
		{
			if (!LOD->SpawnModule)
			{
				LOD->AddModule(UParticleModuleSpawn::StaticClass());
			}
			LOD->SpawnModule->Rate = FFloatDistribution::MakeConstant(Rate);
			return LOD->SpawnModule;
		}
	};
}

int32 RunParticleLayoutSelfTest()
{
	int32 Failed = 0;
	auto Expect = [&](int64 Got, int64 Want, const char* Name)
		{
			if (Got != Want)
			{
				++Failed;
				HTR_LOG(Error, "[Particle] layout FAIL {} (got {}, want {})", Name, Got, Want);
			}
		};

	const int64 Base = sizeof(FBaseParticle);

	// 1) 모듈 A(12 -> 16), B(0), C(8): 오프셋은 각 모듈을 더하기 직전의 크기. 크기는 8의 배수로 올림된다
	{
		FLayoutFixture F;
		UParticleModule* A = F.Add(UParticleModuleLayoutTestA::StaticClass());
		UParticleModule* B = F.Add(UParticleModuleLayoutTestB::StaticClass());
		UParticleModule* C = F.Add(UParticleModuleLayoutTestC::StaticClass());
		F.Emitter->CacheEmitterModuleInfo();

		Expect(F.Emitter->GetModuleDataOffset(A), Base, "A: offset");
		Expect(F.Emitter->GetModuleDataOffset(B), INDEX_NONE, "B: no payload");
		Expect(F.Emitter->GetModuleDataOffset(C), Base + 16, "C: offset (A 12 -> 16)");
		Expect(F.Emitter->ParticleSize, Base + 24, "A,B,C: ParticleSize");
		Expect(F.Emitter->ParticleStride, 160, "A,B,C: ParticleStride (152 -> 160)");
		Expect(F.Emitter->GetModuleInstanceDataOffset(A), 0, "A: instance offset");
		Expect(F.Emitter->GetModuleInstanceDataOffset(B), INDEX_NONE, "B: no instance data");
		Expect(F.Emitter->GetModuleInstanceDataOffset(C), 16, "C: instance offset");
		Expect(F.Emitter->ReqInstanceBytes, 24, "A,B,C: ReqInstanceBytes");
		HTR_LOG(Info, "[Particle] layout A,B,C: A@{} C@{} size={} stride={} instance={}",
			F.Emitter->GetModuleDataOffset(A), F.Emitter->GetModuleDataOffset(C),
			F.Emitter->ParticleSize, F.Emitter->ParticleStride, F.Emitter->ReqInstanceBytes);

		// 꺼진 모듈도 레이아웃에 포함된다
		A->bEnabled = false;
		F.Emitter->CacheEmitterModuleInfo();
		Expect(F.Emitter->GetModuleDataOffset(C), Base + 16, "disabled A: C offset unchanged");
		Expect(F.Emitter->ParticleSize, Base + 24, "disabled A: ParticleSize unchanged");
	}

	// 2) 순서를 바꾸면 오프셋이 바뀐다 (C, A)
	{
		FLayoutFixture F;
		UParticleModule* C = F.Add(UParticleModuleLayoutTestC::StaticClass());
		UParticleModule* A = F.Add(UParticleModuleLayoutTestA::StaticClass());
		F.Emitter->CacheEmitterModuleInfo();

		Expect(F.Emitter->GetModuleDataOffset(C), Base, "C,A: C offset");
		Expect(F.Emitter->GetModuleDataOffset(A), Base + 8, "C,A: A offset");
		Expect(F.Emitter->ParticleSize, Base + 24, "C,A: ParticleSize");
		Expect(F.Emitter->GetModuleInstanceDataOffset(C), 0, "C,A: C instance offset");
		Expect(F.Emitter->GetModuleInstanceDataOffset(A), 8, "C,A: A instance offset");
		HTR_LOG(Info, "[Particle] layout C,A: C@{} A@{} size={} stride={}",
			F.Emitter->GetModuleDataOffset(C), F.Emitter->GetModuleDataOffset(A),
			F.Emitter->ParticleSize, F.Emitter->ParticleStride);
	}

	// 3) TypeData(4 -> 8)가 가장 앞에 오고 그 뒤에 A, C가 이어진다
	{
		FLayoutFixture F;
		F.Add(UParticleModuleTypeDataLayoutTest::StaticClass());
		UParticleModule* A = F.Add(UParticleModuleLayoutTestA::StaticClass());
		UParticleModule* C = F.Add(UParticleModuleLayoutTestC::StaticClass());
		F.Emitter->CacheEmitterModuleInfo();

		Expect(F.Emitter->TypeDataOffset, Base, "TypeData: offset");
		Expect(F.Emitter->GetModuleDataOffset(A), Base + 8, "TypeData,A,C: A offset");
		Expect(F.Emitter->GetModuleDataOffset(C), Base + 24, "TypeData,A,C: C offset");
		Expect(F.Emitter->ParticleSize, Base + 32, "TypeData,A,C: ParticleSize");
		Expect(F.Emitter->ParticleStride, 160, "TypeData,A,C: ParticleStride (already a multiple of 16)");
		HTR_LOG(Info, "[Particle] layout TypeData,A,C: TypeData@{} A@{} C@{} size={} stride={}",
			F.Emitter->TypeDataOffset, F.Emitter->GetModuleDataOffset(A), F.Emitter->GetModuleDataOffset(C),
			F.Emitter->ParticleSize, F.Emitter->ParticleStride);
	}

	// 4) 모듈이 없으면 FBaseParticle만. 128은 이미 16의 배수라 올림이 없다
	{
		FLayoutFixture F;
		F.Emitter->CacheEmitterModuleInfo();
		Expect(F.Emitter->ParticleSize, Base, "empty: ParticleSize");
		Expect(F.Emitter->ParticleStride, Base, "empty: ParticleStride");
		Expect(F.Emitter->ReqInstanceBytes, 0, "empty: ReqInstanceBytes");
	}

	// 5) 다시 호출해도 같은 결과가 나온다 (이전 결과가 누적되지 않는다)
	{
		FLayoutFixture F;
		UParticleModule* A = F.Add(UParticleModuleLayoutTestA::StaticClass());
		F.Emitter->CacheEmitterModuleInfo();
		F.Emitter->CacheEmitterModuleInfo();
		Expect(F.Emitter->GetModuleDataOffset(A), Base, "twice: A offset");
		Expect(F.Emitter->ParticleSize, Base + 16, "twice: ParticleSize");
		Expect(F.Emitter->ReqInstanceBytes, 16, "twice: ReqInstanceBytes");
	}

	// 7) 1바이트 요구도 8바이트로 올림된다. 뒤따르는 모듈의 시작이 8의 배수가 된다
	{
		FLayoutFixture F;
		UParticleModule* D = F.Add(UParticleModuleLayoutTestD::StaticClass());
		UParticleModule* C = F.Add(UParticleModuleLayoutTestC::StaticClass());
		F.Emitter->CacheEmitterModuleInfo();
		Expect(F.Emitter->GetModuleDataOffset(D), Base, "D,C: D offset");
		Expect(F.Emitter->GetModuleDataOffset(C), Base + 8, "D,C: C offset (D 1 -> 8)");
		Expect(F.Emitter->ParticleSize, Base + 16, "D,C: ParticleSize");
		Expect(F.Emitter->ParticleStride, Base + 16, "D,C: ParticleStride");
		Expect(F.Emitter->GetModuleInstanceDataOffset(D), 0, "D,C: D instance offset");
		Expect(F.Emitter->GetModuleInstanceDataOffset(C), 8, "D,C: C instance offset (D 4 -> 8)");
		Expect(F.Emitter->ReqInstanceBytes, 16, "D,C: ReqInstanceBytes");
	}

	// 6) Required와 TypeData는 슬롯이 하나뿐이라 두 번째 추가가 거절된다
	{
		HTR_LOG(Info, "[Particle] layout: the next two 'slot is already used' warnings are expected");
		FLayoutFixture F;
		Expect(F.Add(UParticleModuleRequired::StaticClass()) != nullptr, 1, "Required: first add");
		Expect(F.Add(UParticleModuleRequired::StaticClass()) != nullptr, 0, "Required: second add rejected");
		Expect(F.Add(UParticleModuleTypeDataLayoutTest::StaticClass()) != nullptr, 1, "TypeData: first add");
		Expect(F.Add(UParticleModuleTypeDataLayoutTest::StaticClass()) != nullptr, 0, "TypeData: second add rejected");
		Expect(F.LOD->Modules.Num(), 0, "slot modules are not in Modules");
	}

	// 8) 다중 LOD: 모든 LOD의 같은 위치 모듈이 동일한 오프셋에 등록되고, 거리 판정·전환·직렬화가 일관되게 동작한다
	{
		FLayoutFixture F;
		F.Add(UParticleModuleRequired::StaticClass());
		F.SetSpawnRate(20.0f);
		UParticleModule* A0 = F.Add(UParticleModuleLayoutTestA::StaticClass());
		UParticleModule* C0 = F.Add(UParticleModuleLayoutTestC::StaticClass());
		F.Emitter->CacheEmitterModuleInfo();

		const int32 AddedIdx = F.System->AddLODLevel(1, 0.5f);
		Expect(AddedIdx, 1, "Multi-LOD: AddLODLevel(1)");
		Expect(F.System->GetNumLODLevels(), 2, "Multi-LOD: GetNumLODLevels == 2");
		UParticleLODLevel* LOD1 = F.Emitter->GetLODLevel(1);
		Expect(LOD1 != nullptr && LOD1->Modules.Num() == 2, 1, "Multi-LOD: LOD1 cloned modules");
		if (LOD1 && LOD1->Modules.Num() == 2)
		{
			Expect(F.Emitter->GetModuleDataOffset(LOD1->Modules[0]), F.Emitter->GetModuleDataOffset(A0), "Multi-LOD: LOD1 A0 offset matches LOD0");
			Expect(F.Emitter->GetModuleDataOffset(LOD1->Modules[1]), F.Emitter->GetModuleDataOffset(C0), "Multi-LOD: LOD1 C0 offset matches LOD0");
			Expect(F.Emitter->GetModuleInstanceDataOffset(LOD1->Modules[0]), F.Emitter->GetModuleInstanceDataOffset(A0), "Multi-LOD: LOD1 A0 instance offset matches LOD0");
			Expect(F.Emitter->GetModuleInstanceDataOffset(LOD1->Modules[1]), F.Emitter->GetModuleInstanceDataOffset(C0), "Multi-LOD: LOD1 C0 instance offset matches LOD0");
		}

		F.System->SetLODDistance(1, 15.0f);
		Expect(F.System->GetLODLevelForDistance(5.0f), 0, "Multi-LOD: distance 5m -> LOD 0");
		Expect(F.System->GetLODLevelForDistance(15.0f), 1, "Multi-LOD: distance 15m -> LOD 1");
		Expect(F.System->GetLODLevelForDistance(30.0f), 1, "Multi-LOD: distance 30m -> LOD 1");

		FString Error;
		UParticleSystem* CopySys = ParticleAsset::Duplicate(*F.System, Error);
		Expect(CopySys != nullptr, 1, "Multi-LOD: serialization round-trip");
		if (CopySys)
		{
			Expect(CopySys->GetNumLODLevels(), 2, "Multi-LOD: round-trip LOD count");
			Expect(CopySys->GetLODLevelForDistance(20.0f), 1, "Multi-LOD: round-trip LODDistances");
			DestroyParticleSystem(CopySys);
		}
	}

	HTR_LOG(Info, "[Particle] layout self-test: {}", Failed == 0 ? "all passed" : "FAILED");
	return Failed;
}

int32 RunParticleInstanceSelfTest()
{
	int32 Failed = 0;
	auto Expect = [&](int64 Got, int64 Want, const char* Name)
		{
			if (Got != Want)
			{
				++Failed;
				HTR_LOG(Error, "[Particle] instance FAIL {} (got {}, want {})", Name, Got, Want);
			}
		};
	auto IsAligned = [](const void* Ptr) { return reinterpret_cast<uintptr_t>(Ptr) % FParticleEmitterInstance::MemoryAlignment == 0; };

	// 레이아웃 계산이 끝난 이미터: stride 160, InstanceData 24바이트
	FLayoutFixture F;
	F.Add(UParticleModuleLayoutTestA::StaticClass());
	F.Add(UParticleModuleLayoutTestB::StaticClass());
	F.Add(UParticleModuleLayoutTestC::StaticClass());

	// 레이아웃을 계산하기 전에는 Init이 거절된다
	{
		FParticleEmitterInstance Instance;
		Expect(Instance.Init(F.Emitter), 0, "Init before CacheEmitterModuleInfo is rejected");
		Expect(Instance.Init(nullptr), 0, "Init with null is rejected");
	}

	F.Emitter->CacheEmitterModuleInfo();

	FParticleEmitterInstance Instance;
	Expect(Instance.Init(F.Emitter), 1, "Init");
	Expect(Instance.ParticleStride, 160, "Init: ParticleStride");
	Expect(Instance.InstancePayloadSize, 24, "Init: InstancePayloadSize");
	Expect(Instance.InstanceData != nullptr, 1, "Init: InstanceData allocated");
	Expect(IsAligned(Instance.InstanceData), 1, "Init: InstanceData aligned");
	Expect(Instance.ParticleData == nullptr, 1, "Init: no particle memory yet");
	Expect(Instance.MaxActiveParticles, 0, "Init: MaxActiveParticles");
	{
		bool bZero = true;
		for (uint32 i = 0; i < Instance.InstancePayloadSize; ++i) { bZero &= (Instance.InstanceData[i] == 0); }
		Expect(bZero, 1, "Init: InstanceData zero-filled");
	}

	// Resize(10): 데이터 10 * 160 바이트, 인덱스 11칸
	Expect(Instance.Resize(10), 1, "Resize(10)");
	Expect(Instance.MaxActiveParticles, 10, "Resize(10): MaxActiveParticles");
	Expect(IsAligned(Instance.ParticleData), 1, "Resize(10): ParticleData aligned");
	{
		bool bIdentity = true;
		for (int32 i = 0; i < 10; ++i) { bIdentity &= (Instance.ParticleIndices[i] == i); }
		Expect(bIdentity, 1, "Resize(10): indices are identity");
		Expect(Instance.ParticleIndices[10], FParticleEmitterInstance::UnusedIndex, "Resize(10): trailing sentinel");
	}
	HTR_LOG(Info, "[Particle] instance Resize(10): data={} bytes, indices={} entries, instance={} bytes",
		static_cast<uint64>(Instance.ParticleStride) * Instance.MaxActiveParticles, Instance.MaxActiveParticles + 1, Instance.InstancePayloadSize);

	// 다시 늘려도 기존 내용과 인덱스 순서가 유지되고, 새 칸만 초기화된다
	Instance.ParticleIndices[0] = 5;
	Instance.ParticleIndices[5] = 0;
	std::memset(Instance.ParticleData + 160 * 5, 0xAB, 160);
	Expect(Instance.Resize(20), 1, "Resize(20)");
	Expect(Instance.MaxActiveParticles, 20, "Resize(20): MaxActiveParticles");
	Expect(Instance.ParticleIndices[0], 5, "Resize(20): old index order kept (0)");
	Expect(Instance.ParticleIndices[5], 0, "Resize(20): old index order kept (5)");
	Expect(Instance.ParticleData[160 * 5 + 159], 0xAB, "Resize(20): slot data kept");
	{
		bool bIdentity = true;
		for (int32 i = 10; i < 20; ++i) { bIdentity &= (Instance.ParticleIndices[i] == i); }
		Expect(bIdentity, 1, "Resize(20): new indices are identity");
		Expect(Instance.ParticleIndices[20], FParticleEmitterInstance::UnusedIndex, "Resize(20): trailing sentinel moved");
	}

	// 줄이는 요청은 성공하지만 아무것도 바꾸지 않는다
	Expect(Instance.Resize(5), 1, "Resize(5) shrink is a no-op");
	Expect(Instance.MaxActiveParticles, 20, "Resize(5): MaxActiveParticles unchanged");

	// 범위를 벗어난 요청은 거절되고 기존 메모리는 그대로다
	uint8* DataBefore = Instance.ParticleData;
	Expect(Instance.Resize(FParticleEmitterInstance::MaxParticleLimit + 1), 0, "Resize(65536) rejected");
	Expect(Instance.Resize(-1), 0, "Resize(-1) rejected");
	Expect(Instance.MaxActiveParticles, 20, "rejected Resize: MaxActiveParticles unchanged");
	Expect(Instance.ParticleData == DataBefore, 1, "rejected Resize: ParticleData unchanged");

	// 상한 경계: 65535칸이면 마지막 슬롯 번호는 65534이고 끝 표시가 65535번 칸에 놓인다
	Expect(Instance.Resize(FParticleEmitterInstance::MaxParticleLimit), 1, "Resize(65535)");
	Expect(Instance.ParticleIndices[65534], 65534, "Resize(65535): last slot index");
	Expect(Instance.ParticleIndices[65535], FParticleEmitterInstance::UnusedIndex, "Resize(65535): sentinel");
	Expect(Instance.ParticleIndices[0], 5, "Resize(65535): old index order kept");

	// 다시 Init하면 이전 메모리를 버리고 처음 상태가 된다
	Expect(Instance.Init(F.Emitter), 1, "re-Init");
	Expect(Instance.MaxActiveParticles, 0, "re-Init: MaxActiveParticles reset");
	Expect(Instance.ParticleData == nullptr, 1, "re-Init: particle memory released");

	// 해제 후 모든 포인터와 개수가 비고, 여러 번 불러도 안전하다
	Instance.Resize(8);
	Instance.Release();
	Instance.Release();
	Expect(Instance.ParticleData == nullptr && Instance.ParticleIndices == nullptr && Instance.InstanceData == nullptr, 1, "Release: pointers cleared");
	Expect(Instance.MaxActiveParticles, 0, "Release: counts cleared");
	Expect(Instance.Resize(4), 0, "Resize after Release is rejected (no template)");

	HTR_LOG(Info, "[Particle] instance self-test: {}", Failed == 0 ? "all passed" : "FAILED");
	return Failed;
}

int32 RunParticleMacroSelfTest()
{
	int32 Failed = 0;
	auto Expect = [&](int64 Got, int64 Want, const char* Name)
		{
			if (Got != Want)
			{
				++Failed;
				HTR_LOG(Error, "[Particle] macro FAIL {} (got {}, want {})", Name, Got, Want);
			}
		};

	// 모듈 A의 Payload(12 -> 16바이트)가 파티클 안 128번째 바이트부터 놓인다
	FLayoutFixture F;
	UParticleModule* A = F.Add(UParticleModuleLayoutTestA::StaticClass());
	F.Emitter->CacheEmitterModuleInfo();
	const int32 PayloadOffset = F.Emitter->GetModuleDataOffset(A);
	Expect(PayloadOffset, sizeof(FBaseParticle), "payload offset of A");

	FParticleEmitterInstance Instance;
	Instance.Init(F.Emitter);
	Instance.Resize(5);
	const uint32 Stride = Instance.ParticleStride;

	// 슬롯 번호와 살아 있는 순서를 일부러 다르게 둔다: i=0..4 가 슬롯 2,0,4,1,3
	auto ResetState = [&]()
		{
			const uint16 Order[5] = { 2, 0, 4, 1, 3 };
			std::memset(Instance.ParticleData, 0, static_cast<size_t>(Stride) * 5);
			for (int32 i = 0; i < 5; ++i)
			{
				Instance.ParticleIndices[i] = Order[i];
				reinterpret_cast<FBaseParticle*>(Instance.ParticleData + Stride * i)->Location.X = static_cast<float>(i);
			}
			Instance.ActiveParticles = 5;
		};
	auto PayloadAt = [&](int32 Slot, int32 Index) -> float
		{
			float Value;
			std::memcpy(&Value, Instance.ParticleData + Stride * Slot + PayloadOffset + Index * sizeof(float), sizeof(float));
			return Value;
		};

	// 1) 뒤에서 앞으로 순회한다: i=4..0 이면 슬롯 3,1,4,0,2
	{
		ResetState();
		FParticleUpdateContext Context{ Instance, PayloadOffset, 0.016f };
		std::vector<int32> Visited;
		BEGIN_UPDATE_LOOP
			(void)DeltaTime;
			Visited.push_back(CurrentIndex);
		END_UPDATE_LOOP
		const std::vector<int32> Want = { 3, 1, 4, 0, 2 };
		Expect(Visited == Want, 1, "update loop visits from the back");
	}

	// 2) Freeze 상태인 파티클은 본문을 건너뛴다
	{
		ResetState();
		reinterpret_cast<FBaseParticle*>(Instance.ParticleData + Stride * 4)->Flags |= STATE_Particle_Freeze;
		FParticleUpdateContext Context{ Instance, PayloadOffset, 0.016f };
		std::vector<int32> Visited;
		BEGIN_UPDATE_LOOP
			(void)DeltaTime;
			Visited.push_back(CurrentIndex);
		END_UPDATE_LOOP
		const std::vector<int32> Want = { 3, 1, 0, 2 };
		Expect(Visited == Want, 1, "frozen particle is skipped");
	}

	// 3) PARTICLE_ELEMENT가 요구한 순서대로 Payload를 가리키고, 파티클이 바뀌면 CurrentOffset이 처음으로 돌아온다.
	//    슬롯 1은 CONTINUE_UPDATE_LOOP로 중간에 빠져나간다. 되돌리지 않으면 다음 파티클의 X가 128이 아닌 위치에 놓인다.
	{
		ResetState();
		FParticleUpdateContext Context{ Instance, PayloadOffset, 0.016f };
		std::vector<int64> XOffsets, YOffsets;
		BEGIN_UPDATE_LOOP
			(void)DeltaTime;
			PARTICLE_ELEMENT(float, X);
			XOffsets.push_back(reinterpret_cast<const uint8*>(&X) - ParticleBase);
			X = static_cast<float>(CurrentIndex);
			if (CurrentIndex == 1)
			{
				CONTINUE_UPDATE_LOOP
			}
			PARTICLE_ELEMENT(float, Y);
			YOffsets.push_back(reinterpret_cast<const uint8*>(&Y) - ParticleBase);
			Y = static_cast<float>(CurrentIndex * 10);
		END_UPDATE_LOOP
		bool bXAll = true, bYAll = true;
		for (int64 O : XOffsets) { bXAll &= (O == PayloadOffset); }
		for (int64 O : YOffsets) { bYAll &= (O == PayloadOffset + 4); }
		Expect(XOffsets.size(), 5, "payload: X visited 5 times");
		Expect(YOffsets.size(), 4, "payload: Y skipped once by CONTINUE_UPDATE_LOOP");
		Expect(bXAll, 1, "payload: X is always at the payload start");
		Expect(bYAll, 1, "payload: Y is always 4 bytes after X");
		Expect(PayloadAt(3, 0) == 3.0f && PayloadAt(3, 1) == 30.0f, 1, "payload: slot 3 values stored");
		Expect(PayloadAt(1, 0) == 1.0f && PayloadAt(1, 1) == 0.0f, 1, "payload: slot 1 has X but no Y");
	}

	// 4) KILL_CURRENT_PARTICLE: 짝수 슬롯(2,0,4)을 죽이면 살아 있는 구간에는 1,3만 남고 번호는 한 번씩만 존재한다
	{
		ResetState();
		FParticleUpdateContext Context{ Instance, PayloadOffset, 0.016f };
		int32 VisitCount[5] = {};
		BEGIN_UPDATE_LOOP
			(void)DeltaTime;
			++VisitCount[CurrentIndex];
			if (CurrentIndex % 2 == 0)
			{
				KILL_CURRENT_PARTICLE
				CONTINUE_UPDATE_LOOP
			}
		END_UPDATE_LOOP
		bool bEachOnce = true;
		for (int32 Count : VisitCount) { bEachOnce &= (Count == 1); }
		Expect(bEachOnce, 1, "kill: every particle visited exactly once");
		Expect(Instance.ActiveParticles, 2, "kill: ActiveParticles");
		bool bAliveOdd = true;
		for (int32 i = 0; i < Instance.ActiveParticles; ++i) { bAliveOdd &= (Instance.ParticleIndices[i] % 2 == 1); }
		Expect(bAliveOdd, 1, "kill: only odd slots remain alive");
		int32 Seen = 0;
		for (int32 i = 0; i < 5; ++i) { Seen |= 1 << Instance.ParticleIndices[i]; }
		Expect(Seen, 0b11111, "kill: indices stay a permutation of 0..4");
		Expect(Instance.ParticleIndices[5], FParticleEmitterInstance::UnusedIndex, "kill: sentinel untouched");
	}

	// 5) SPAWN_INIT: 새 파티클 한 개의 FBaseParticle과 Payload에 접근한다
	{
		ResetState();
		FBaseParticle* Slot = reinterpret_cast<FBaseParticle*>(Instance.ParticleData + Stride * 2);
		FParticleSpawnContext Context{ Instance, PayloadOffset, 0.0f, Slot };
		SPAWN_INIT
		(void)ActiveParticles;
		(void)ParticleStride;
		Particle.Location.X = 7.0f;
		PARTICLE_ELEMENT(float, X);
		PARTICLE_ELEMENT(float, Y);
		X = 3.0f;
		Y = 4.0f;
		Expect(Slot->Location.X == 7.0f, 1, "spawn: Particle refers to the slot");
		Expect(PayloadAt(2, 0) == 3.0f && PayloadAt(2, 1) == 4.0f, 1, "spawn: payload written");
		Expect(CurrentOffset, PayloadOffset + 8, "spawn: CurrentOffset advanced");
		DECLARE_PARTICLE_CONST(Same, Slot);
		Expect(&Same == Slot, 1, "DECLARE_PARTICLE_CONST");
	}

	// 6) UE의 prefetch 패턴 분석: 실제 prefetch 대신 "어느 슬롯을 prefetch하려 했는지"만 기록해서 다음에 방문하는 슬롯과 비교한다.
	//    UE는 역순 루프(BEGIN_UPDATE_LOOP)에서도 ParticleIndices[i+1]을 prefetch한다 (예: ParticleModules.cpp:2541).
	{
		ResetState();
		Expect(Instance.ParticleIndices[5], FParticleEmitterInstance::UnusedIndex, "prefetch: sentinel after the last index");

		// UE 방식: 루프 전에 [0], 루프 안에서 [i+1]
		FParticleUpdateContext Context{ Instance, PayloadOffset, 0.016f };
		std::vector<int32> Visited, UETargets;
		const int32 PreLoopTarget = Instance.ParticleIndices[0];
		BEGIN_UPDATE_LOOP
			(void)DeltaTime;
			UETargets.push_back(ParticleIndices[i + 1]);
			Visited.push_back(CurrentIndex);
		END_UPDATE_LOOP

		// 반복 k에서 prefetch한 슬롯이 반복 k+1에서 방문하는 슬롯과 같으면 "다음 파티클을 미리 올린 것"이다
		int32 UEHitsNext = 0, UEEqualsPrevious = 0;
		for (size_t k = 0; k < Visited.size(); ++k)
		{
			if (k + 1 < Visited.size() && UETargets[k] == Visited[k + 1]) { ++UEHitsNext; }
			if (k > 0 && UETargets[k] == Visited[k - 1]) { ++UEEqualsPrevious; }
		}
		Expect(UEHitsNext, 0, "prefetch: UE [i+1] in a reverse loop never targets the next visited particle");
		Expect(UEEqualsPrevious, 4, "prefetch: UE [i+1] targets the previously visited particle");
		Expect(UETargets[0], FParticleEmitterInstance::UnusedIndex, "prefetch: first iteration reads the sentinel");
		Expect(PreLoopTarget == Visited[0], 0, "prefetch: pre-loop [0] is not the first visited particle");

		// 실제 사용하는 GetParticlePrefetchIndex: 거리 D에서 D번 뒤에 방문할 파티클을 가리키고 범위 밖이면 INDEX_NONE이다
		ResetState();
		std::vector<int32> Visited2;
		std::vector<int32> TargetSlot1, TargetSlot2, TargetSlot3;
		BEGIN_UPDATE_LOOP
			(void)DeltaTime;
			Visited2.push_back(CurrentIndex);
			const int32 T1 = GetParticlePrefetchIndex(i, 1), T2 = GetParticlePrefetchIndex(i, 2), T3 = GetParticlePrefetchIndex(i, 3);
			TargetSlot1.push_back(T1 == INDEX_NONE ? -1 : ParticleIndices[T1]);
			TargetSlot2.push_back(T2 == INDEX_NONE ? -1 : ParticleIndices[T2]);
			TargetSlot3.push_back(T3 == INDEX_NONE ? -1 : ParticleIndices[T3]);
		END_UPDATE_LOOP
		int32 FixedHitsNext = 0;
		for (size_t k = 0; k < Visited2.size(); ++k)
		{
			if (k + 1 < Visited2.size()) { FixedHitsNext += (TargetSlot1[k] == Visited2[k + 1]); } else { FixedHitsNext += (TargetSlot1[k] == -1); }
			if (k + 2 < Visited2.size()) { FixedHitsNext += (TargetSlot2[k] == Visited2[k + 2]); } else { FixedHitsNext += (TargetSlot2[k] == -1); }
			if (k + 3 < Visited2.size()) { FixedHitsNext += (TargetSlot3[k] == Visited2[k + 3]); } else { FixedHitsNext += (TargetSlot3[k] == -1); }
		}
		Expect(FixedHitsNext, 15, "prefetch: GetParticlePrefetchIndex(i, D) is the particle visited D iterations later, INDEX_NONE past the end");
		Expect(GetParticlePrefetchIndex(0, 1) == INDEX_NONE, 1, "prefetch: no read of ParticleIndices[-1]");
		Expect(GetParticlePrefetchIndex(7, 8) == INDEX_NONE && GetParticlePrefetchIndex(8, 8) == 0, 1, "prefetch: boundary at i == D");

		// 정순 루프에서는 UE의 [i+1]이 맞다 (메시 이미터의 UpdateBoundingBox, ParticleEmitterInstances.cpp:3490-3494)
		int32 ForwardHitsNext = 0;
		std::vector<int32> ForwardVisited, ForwardTargets;
		for (int32 i = 0; i < Instance.ActiveParticles; ++i)
		{
			ForwardVisited.push_back(Instance.ParticleIndices[i]);
			ForwardTargets.push_back(Instance.ParticleIndices[i + 1]);
		}
		for (size_t k = 0; k + 1 < ForwardVisited.size(); ++k) { if (ForwardTargets[k] == ForwardVisited[k + 1]) { ++ForwardHitsNext; } }
		Expect(ForwardHitsNext, 4, "prefetch: forward loop [i+1] targets the next visited particle");

		HTR_LOG(Info, "[Particle] prefetch pattern: reverse loop UE [i+1] hits next={}/4 previous={}/4, GetParticlePrefetchIndex checks={}/15, forward [i+1] hits next={}/4",
			UEHitsNext, UEEqualsPrevious, FixedHitsNext, ForwardHitsNext);
	}

	HTR_LOG(Info, "[Particle] macro self-test: {}", Failed == 0 ? "all passed" : "FAILED");
	return Failed;
}

FTickTestProbe GTickTestProbe;

UParticleModuleTickTest::UParticleModuleTickTest()
{
	bSpawnModule = true;
	bUpdateModule = true;
}

void UParticleModuleTickTest::Spawn(const FParticleSpawnContext& Context)
{
	SPAWN_INIT
	(void)ActiveParticles;
	(void)ParticleStride;
	GTickTestProbe.Log.push_back('S');

	Particle.OneOverMaxLifetime = GTickTestProbe.Lifetime > 0.0f ? 1.0f / GTickTestProbe.Lifetime : 0.0f;
	Particle.BaseVelocity = GTickTestProbe.Velocity;
	Particle.Velocity = GTickTestProbe.Velocity;

	PARTICLE_ELEMENT(float, SeenSpawnTime);
	PARTICLE_ELEMENT(float, UpdateCount);
	SeenSpawnTime = Context.SpawnTime;
	UpdateCount = 0.0f;
}

void UParticleModuleTickTest::Update(const FParticleUpdateContext& Context)
{
	GTickTestProbe.Log.push_back('U');
	BEGIN_UPDATE_LOOP
		(void)DeltaTime;
		PARTICLE_ELEMENT(float, SeenSpawnTime);
		PARTICLE_ELEMENT(float, UpdateCount);
		(void)SeenSpawnTime;
		UpdateCount += 1.0f;
		Particle.Velocity = Particle.Velocity * GTickTestProbe.UpdateVelocityScale;
	END_UPDATE_LOOP
}

int32 RunParticleTickSelfTest()
{
	int32 Failed = 0;
	auto Expect = [&](int64 Got, int64 Want, const char* Name)
		{
			if (Got != Want)
			{
				++Failed;
				HTR_LOG(Error, "[Particle] tick FAIL {} (got {}, want {})", Name, Got, Want);
			}
		};
	auto ExpectNear = [&](double Got, double Want, double Tol, const char* Name)
		{
			if (std::fabs(Got - Want) > Tol)
			{
				++Failed;
				HTR_LOG(Error, "[Particle] tick FAIL {} (got {}, want {})", Name, Got, Want);
			}
		};

	// 1) 생성 계획: dt = 1/64, Rate = 32이면 프레임마다 0.5개씩 쌓여 0, 1, 0, 1이 된다 (2의 거듭제곱이라 부동소수 오차가 없다)
	{
		const float Dt = 1.0f / 64.0f;
		float Fraction = 0.0f;
		int32 Counts[4];
		float StartTimeOnSecond = -1.0f;
		for (int32 f = 0; f < 4; ++f)
		{
			const FParticleSpawnPlan Plan = FParticleEmitterInstance::ComputeSpawnPlan(Fraction, 32.0f, Dt);
			Counts[f] = Plan.Count;
			if (f == 1) { StartTimeOnSecond = Plan.StartTime; }
			Fraction = Plan.NewFraction;
		}
		Expect(Counts[0], 0, "plan: frame 0 count");
		Expect(Counts[1], 1, "plan: frame 1 count");
		Expect(Counts[2], 0, "plan: frame 2 count");
		Expect(Counts[3], 1, "plan: frame 3 count");
		ExpectNear(StartTimeOnSecond, 0.0, 1e-9, "plan: frame 1 StartTime (the carried half particle completes exactly at frame end)");
		ExpectNear(Fraction, 0.0, 1e-9, "plan: fraction after 4 frames");

		// Rate = 128: 한 프레임에 2개, 첫 파티클은 1/128초 전, 둘째는 프레임 끝에 생성
		const FParticleSpawnPlan Two = FParticleEmitterInstance::ComputeSpawnPlan(0.0f, 128.0f, Dt);
		Expect(Two.Count, 2, "plan: rate 128 count");
		ExpectNear(Two.StartTime, 1.0 / 128.0, 1e-9, "plan: rate 128 StartTime");
		ExpectNear(Two.Increment, 1.0 / 128.0, 1e-9, "plan: rate 128 Increment");

		// Rate가 0 이하면 아무것도 만들지 않고 소수 부분을 그대로 둔다
		const FParticleSpawnPlan None = FParticleEmitterInstance::ComputeSpawnPlan(0.25f, 0.0f, Dt);
		Expect(None.Count, 0, "plan: rate 0 count");
		ExpectNear(None.NewFraction, 0.25, 1e-9, "plan: rate 0 keeps fraction");
	}

	// 2) 프레임 시간이 달라도 1초 동안의 생성 수는 Rate에 수렴한다 (오차 1개 이내)
	{
		const int32 FrameCounts[] = { 30, 60, 120 };
		for (int32 c = 0; c < 3; ++c)
		{
			const float Dt = 1.0f / static_cast<float>(FrameCounts[c]);
			float Fraction = 0.0f;
			int32 Total = 0;
			for (int32 f = 0; f < FrameCounts[c]; ++f)
			{
				const FParticleSpawnPlan Plan = FParticleEmitterInstance::ComputeSpawnPlan(Fraction, 25.0f, Dt);
				Total += Plan.Count;
				Fraction = Plan.NewFraction;
			}
			Expect(std::abs(Total - 25) <= 1, 1, "plan: 1 second total is within 1 of the rate");
			HTR_LOG(Info, "[Particle] tick spawn plan: rate 25, {} frames/s -> {} particles in 1 s, leftover {}", FrameCounts[c], Total, Fraction);
		}
	}

	// 3) 모듈 목록: Spawn/Update를 구현한 모듈만 같은 순서로 모인다
	{
		FLayoutFixture F;
		F.Add(UParticleModuleLayoutTestA::StaticClass());
		UParticleModule* T1 = F.Add(UParticleModuleTickTest::StaticClass());
		F.Add(UParticleModuleLayoutTestB::StaticClass());
		UParticleModule* T2 = F.Add(UParticleModuleTickTest::StaticClass());
		F.Emitter->CacheEmitterModuleInfo();
		Expect(F.LOD->Modules.Num(), 4, "lists: Modules");
		Expect(F.LOD->SpawnModules.Num(), 2, "lists: SpawnModules");
		Expect(F.LOD->UpdateModules.Num(), 2, "lists: UpdateModules");
		Expect(F.LOD->UpdateModules[0] == T1 && F.LOD->UpdateModules[1] == T2, 1, "lists: order is kept");
	}

	// 4) Tick 순서와 새 파티클 처리
	//    dt = 1/64, Rate = 64이면 프레임마다 정확히 1개 생성. 수명 2초(OneOverMaxLifetime = 0.5), 속도 (1,0,0), Update가 속도를 2배로 만든다.
	{
		FLayoutFixture F;
		F.Add(UParticleModuleTickTest::StaticClass());
		F.Emitter->CacheEmitterModuleInfo();
		const int32 PayloadOffset = static_cast<int32>(sizeof(FBaseParticle));
		Expect(F.Emitter->ParticleSize, sizeof(FBaseParticle) + 8, "tick: particle size");

		GTickTestProbe.Log.clear();
		GTickTestProbe.Lifetime = 2.0f;
		GTickTestProbe.Velocity = FVector(1.0f, 0.0f, 0.0f);
		GTickTestProbe.UpdateVelocityScale = 2.0f;

		FParticleEmitterInstance Inst;
		Expect(Inst.Init(F.Emitter), 1, "tick: Init");
		F.SetSpawnRate(64.0f);
		const float Dt = 1.0f / 64.0f;

		auto Slot = [&](int32 PositionInIndices) -> FBaseParticle&
			{
				return *reinterpret_cast<FBaseParticle*>(Inst.ParticleData + static_cast<size_t>(Inst.ParticleStride) * Inst.ParticleIndices[PositionInIndices]);
			};
		auto PayloadFloat = [&](int32 PositionInIndices, int32 Index) -> float
			{
				float V;
				std::memcpy(&V, Inst.ParticleData + static_cast<size_t>(Inst.ParticleStride) * Inst.ParticleIndices[PositionInIndices] + PayloadOffset + Index * 4, 4);
				return V;
			};
		auto LogString = [&]() { return std::string(GTickTestProbe.Log.begin(), GTickTestProbe.Log.end()); };

		Inst.Tick(Dt);
		// 첫 프레임: Update는 (파티클 0개로) 먼저, Spawn은 그 뒤
		Expect(LogString() == "US", 1, "tick: first frame runs Update before Spawn");
		Expect(Inst.ActiveParticles, 1, "tick: 1 particle after frame 1");
		Expect(Inst.MaxActiveParticles, 3, "tick: Resize grew the slots (1 + 1 + 1)");
		Expect(PayloadFloat(0, 1) == 0.0f, 1, "tick: new particle was not updated in its spawn frame");
		ExpectNear(PayloadFloat(0, 0), 0.0, 1e-9, "tick: SpawnTime passed to the module");
		ExpectNear(Slot(0).Location.X, 0.0, 1e-9, "tick: new particle position (SpawnTime 0)");
		Expect((Slot(0).Flags & STATE_Particle_JustSpawned) == 0, 1, "tick: JustSpawned cleared at frame end");
		Expect(Slot(0).Flags & STATE_CounterMask, 0, "tick: spawn counter of the first particle");
		ExpectNear(Slot(0).RelativeTime, 0.0, 1e-9, "tick: RelativeTime at birth");

		Inst.Tick(Dt);
		Expect(LogString() == "USUS", 1, "tick: second frame order");
		Expect(Inst.ActiveParticles, 2, "tick: 2 particles after frame 2");
		Expect(PayloadFloat(0, 1) == 1.0f, 1, "tick: first particle updated once");
		Expect(PayloadFloat(1, 1) == 0.0f, 1, "tick: second particle not updated in its spawn frame");
		ExpectNear(Slot(0).RelativeTime, 0.5 / 64.0, 1e-9, "tick: RelativeTime advanced by OneOverMaxLifetime * dt");
		// Reset이 매 프레임 Velocity를 Base로 되돌린 뒤 Update가 2배로 만들므로 4배, 8배가 되지 않는다
		ExpectNear(Slot(0).Velocity.X, 2.0, 1e-9, "tick: Velocity = Base * 2 (not accumulated)");
		ExpectNear(Slot(0).BaseVelocity.X, 1.0, 1e-9, "tick: BaseVelocity untouched");
		// 위치는 Update가 만든 속도(2)로 적분된다: 2 * dt
		ExpectNear(Slot(0).Location.X, 2.0 / 64.0, 1e-9, "tick: position integrated with the updated velocity");
		ExpectNear(Slot(0).OldLocation.X, 0.0, 1e-9, "tick: OldLocation is the previous position");
		ExpectNear(Slot(1).Location.X, 0.0, 1e-9, "tick: new particle skipped integration");
		Expect(Slot(1).Flags & STATE_CounterMask, 1, "tick: spawn counter of the second particle");

		// Freeze된 파티클은 위치가 움직이지 않고 Update 모듈도 건너뛰지만 수명은 진행한다
		Slot(0).Flags |= STATE_Particle_Freeze;
		const float FrozenX = Slot(0).Location.X;
		const float FrozenTime = Slot(0).RelativeTime;
		Inst.Tick(Dt);
		ExpectNear(Slot(0).Location.X, FrozenX, 1e-9, "tick: frozen particle does not move");
		Expect(Slot(0).RelativeTime > FrozenTime, 1, "tick: frozen particle still ages");
		Expect(PayloadFloat(0, 1) == 1.0f, 1, "tick: frozen particle skipped by Update modules");
	}

	// 5) 수명이 끝난 파티클이 제거되고 인덱스가 순열로 유지된다
	//    수명 0.125초, dt = 1/64, Rate = 64 → 한 프레임에 0.125씩 진행. 프레임 s에 생긴 파티클은 s + 10 프레임 시작에 제거된다.
	{
		FLayoutFixture F;
		F.Add(UParticleModuleTickTest::StaticClass());
		F.Emitter->CacheEmitterModuleInfo();
		GTickTestProbe.Log.clear();
		GTickTestProbe.Lifetime = 0.125f;
		GTickTestProbe.Velocity = FVector(0.0f, 0.0f, 0.0f);
		GTickTestProbe.UpdateVelocityScale = 1.0f;

		FParticleEmitterInstance Inst;
		Inst.Init(F.Emitter);
		F.SetSpawnRate(64.0f);
		const float Dt = 1.0f / 64.0f;
		for (int32 f = 1; f <= 30; ++f)
		{
			Inst.Tick(Dt);
			if (f == 9) { Expect(Inst.ActiveParticles, 9, "kill: no particle dies in the first 9 frames"); }
			if (f == 10) { Expect(Inst.ActiveParticles, 10, "kill: frame 10 spawns one and kills none yet (age 1.0 is not > 1)"); }
			if (f == 11) { Expect(Inst.ActiveParticles, 10, "kill: frame 11 kills the first particle (age 1.125) and spawns one"); }
		}
		Expect(Inst.ActiveParticles, 10, "kill: steady state is 10 alive");
		Expect(Inst.ParticleCounter, 30, "kill: 30 particles were created in total");

		int32 Seen = 0;
		bool bPermutation = true;
		for (int32 i = 0; i < Inst.MaxActiveParticles; ++i)
		{
			const uint16 S = Inst.ParticleIndices[i];
			if (S >= Inst.MaxActiveParticles) { bPermutation = false; continue; }
			if (Seen & (1 << S)) { bPermutation = false; }
			Seen |= (1 << S);
		}
		Expect(bPermutation, 1, "kill: ParticleIndices stays a permutation of the slots");
		Expect(Inst.ParticleIndices[Inst.MaxActiveParticles], FParticleEmitterInstance::UnusedIndex, "kill: trailing sentinel kept");
		HTR_LOG(Info, "[Particle] tick kill: after 30 frames alive={} created={} slots={}", Inst.ActiveParticles, Inst.ParticleCounter, Inst.MaxActiveParticles);

		// KillParticle의 범위 검사
		const int32 Before = Inst.ActiveParticles;
		Inst.KillParticle(-1);
		Inst.KillParticle(Inst.ActiveParticles);
		Expect(Inst.ActiveParticles, Before, "kill: out-of-range KillParticle is ignored");
	}

	// 6) 꺼진 모듈은 Spawn / Update가 호출되지 않지만 파티클은 생성된다
	{
		FLayoutFixture F;
		UParticleModule* M = F.Add(UParticleModuleTickTest::StaticClass());
		M->bEnabled = false;
		F.Emitter->CacheEmitterModuleInfo();
		GTickTestProbe.Log.clear();
		FParticleEmitterInstance Inst;
		Inst.Init(F.Emitter);
		F.SetSpawnRate(64.0f);
		Inst.Tick(1.0f / 64.0f);
		Expect(static_cast<int64>(GTickTestProbe.Log.size()), 0, "disabled: module not called");
		Expect(Inst.ActiveParticles, 1, "disabled: the particle is still created");
	}

	// 7) 상한: 빈 칸이 모자라는 만큼만 만들고 나머지는 버린다
	{
		FLayoutFixture F;
		F.Emitter->CacheEmitterModuleInfo();
		FParticleEmitterInstance Inst;
		Inst.Init(F.Emitter);
		Inst.Resize(FParticleEmitterInstance::MaxParticleLimit);
		Inst.ActiveParticles = FParticleEmitterInstance::MaxParticleLimit - 5;
		const float Fraction = Inst.Spawn(1.0f / 64.0f, 6400.0f);
		Expect(Inst.ActiveParticles, FParticleEmitterInstance::MaxParticleLimit, "limit: filled up to the limit only");
		ExpectNear(Fraction, 0.0, 1e-6, "limit: dropped particles are not carried over");
	}

	HTR_LOG(Info, "[Particle] tick self-test: {}", Failed == 0 ? "all passed" : "FAILED");
	return Failed;
}

int32 RunParticleDistributionSelfTest()
{
	int32 Failed = 0;
	auto Expect = [&](int64 Got, int64 Want, const char* Name)
		{
			if (Got != Want)
			{
				++Failed;
				HTR_LOG(Error, "[Particle] distribution FAIL {} (got {}, want {})", Name, Got, Want);
			}
		};
	auto ExpectNear = [&](double Got, double Want, double Tol, const char* Name)
		{
			if (std::fabs(Got - Want) > Tol)
			{
				++Failed;
				HTR_LOG(Error, "[Particle] distribution FAIL {} (got {}, want {})", Name, Got, Want);
			}
		};

	// 1) FRandomStream: 같은 시드는 같은 수열, 다른 시드는 다른 수열, 값은 [0, 1)
	{
		FRandomStream A(12345), B(12345), C(54321);
		bool bSame = true, bDifferent = false, bInRange = true;
		for (int32 i = 0; i < 1000; ++i)
		{
			const float VA = A.GetFraction(), VB = B.GetFraction(), VC = C.GetFraction();
			bSame &= (VA == VB);
			bDifferent |= (VA != VC);
			bInRange &= (VA >= 0.0f && VA < 1.0f);
		}
		Expect(bSame, 1, "stream: same seed gives the same sequence");
		Expect(bDifferent, 1, "stream: different seed gives a different sequence");
		Expect(bInRange, 1, "stream: values are in [0, 1)");

		FRandomStream D(12345);
		const float First = D.GetFraction();
		D.Initialize(12345);
		Expect(D.GetFraction() == First, 1, "stream: Initialize restarts the sequence");
		Expect(D.GetSeed(), 12345, "stream: GetSeed");
	}

	// 2) Constant는 Time과 스트림에 상관없이 같은 값을 돌려주고 스트림을 쓰지 않는다
	{
		const FFloatDistribution Dist = FFloatDistribution::MakeConstant(3.5f);
		FRandomStream S(1), Reference(1);
		Expect(Dist.GetValue(0.0f, &S) == 3.5f && Dist.GetValue(99.0f, &S) == 3.5f && Dist.GetValue(-5.0f, nullptr) == 3.5f, 1, "constant: value");
		Expect(S.GetFraction() == Reference.GetFraction(), 1, "constant: stream untouched");
	}

	// 3) Uniform: 범위 안, 평균이 중간 근처, 같은 시드 재현
	{
		const FFloatDistribution Dist = FFloatDistribution::MakeUniform(2.0f, 6.0f);
		FRandomStream S(7), S2(7);
		double Sum = 0.0;
		bool bInRange = true, bReproduced = true;
		const int32 N = 10000;
		for (int32 i = 0; i < N; ++i)
		{
			const float V = Dist.GetValue(0.0f, &S);
			Sum += V;
			bInRange &= (V >= 2.0f && V <= 6.0f);
			bReproduced &= (V == Dist.GetValue(0.0f, &S2));
		}
		Expect(bInRange, 1, "uniform: every value in [Min, Max]");
		Expect(bReproduced, 1, "uniform: same seed reproduces");
		// 평균의 표준편차는 범위 4 / sqrt(12 * 10000) = 0.0115. 허용 오차 0.1은 약 9 sigma다.
		ExpectNear(Sum / N, 4.0, 0.1, "uniform: mean near the middle");
		Expect(Dist.GetValue(0.0f, nullptr) == 2.0f, 1, "uniform: null stream returns Min");
	}

	// 4) Min == Max인 Uniform은 난수를 쓰지 않는다. 벡터는 값을 뽑을 때 난수를 X, Y, Z 순서로 3개 쓴다.
	{
		const FFloatDistribution Degenerate = FFloatDistribution::MakeUniform(5.0f, 5.0f);
		Expect(!Degenerate.NeedsRandom(), 1, "degenerate: NeedsRandom is false");
		FRandomStream S(9), Reference(9);
		Expect(Degenerate.GetValue(0.0f, &S) == 5.0f, 1, "degenerate: value");
		Expect(S.GetFraction() == Reference.GetFraction(), 1, "degenerate: stream untouched");

		const FVectorDistribution VecDist = FVectorDistribution::MakeUniform(FVector(0.0f, 10.0f, 20.0f), FVector(1.0f, 11.0f, 21.0f));
		FRandomStream V(9), R(9);
		const FVector Value = VecDist.GetValue(0.0f, &V);
		const float RX = R.GetFraction(), RY = R.GetFraction(), RZ = R.GetFraction();
		ExpectNear(Value.X, 0.0f + RX, 1e-6, "vector uniform: X uses the 1st draw");
		ExpectNear(Value.Y, 10.0f + RY, 1e-6, "vector uniform: Y uses the 2nd draw");
		ExpectNear(Value.Z, 20.0f + RZ, 1e-6, "vector uniform: Z uses the 3rd draw");
		Expect(V.GetFraction() == R.GetFraction(), 1, "vector uniform: exactly 3 draws");
	}

	// 5) Curve: 키 사이는 선형 보간, 범위 밖은 끝 키로 고정, 순서 없이 넣어도 정렬, 같은 Time은 덮어쓰기
	{
		FFloatDistribution Dist = FFloatDistribution::MakeCurve();
		Expect(Dist.GetValue(1.0f, nullptr) == 0.0f, 1, "curve: empty returns zero");
		Dist.AddKey(3.0f, 20.0f);
		Dist.AddKey(0.0f, 0.0f);
		Dist.AddKey(1.0f, 10.0f);
		Expect(Dist.Keys.Num(), 3, "curve: three keys");
		bool bSorted = true;
		for (int32 i = 1; i < Dist.Keys.Num(); ++i) { bSorted &= (Dist.Keys[i - 1].Time < Dist.Keys[i].Time); }
		Expect(bSorted, 1, "curve: keys are sorted after out-of-order AddKey");
		ExpectNear(Dist.GetValue(0.5f, nullptr), 5.0, 1e-6, "curve: Time 0.5");
		ExpectNear(Dist.GetValue(1.0f, nullptr), 10.0, 1e-6, "curve: Time on a key");
		ExpectNear(Dist.GetValue(2.0f, nullptr), 15.0, 1e-6, "curve: Time 2");
		ExpectNear(Dist.GetValue(-1.0f, nullptr), 0.0, 1e-6, "curve: before the first key");
		ExpectNear(Dist.GetValue(9.0f, nullptr), 20.0, 1e-6, "curve: after the last key");
		Dist.AddKey(1.0f, 30.0f);
		Expect(Dist.Keys.Num(), 3, "curve: same Time overwrites");
		ExpectNear(Dist.GetValue(1.0f, nullptr), 30.0, 1e-6, "curve: overwritten value");

		FVectorDistribution VecCurve = FVectorDistribution::MakeCurve();
		VecCurve.AddKey(0.0f, FVector(0.0f, 0.0f, 0.0f));
		VecCurve.AddKey(1.0f, FVector(2.0f, 4.0f, 8.0f));
		const FVector Mid = VecCurve.GetValue(0.25f, nullptr);
		ExpectNear(Mid.X, 0.5, 1e-6, "vector curve: X");
		ExpectNear(Mid.Y, 1.0, 1e-6, "vector curve: Y");
		ExpectNear(Mid.Z, 2.0, 1e-6, "vector curve: Z");
	}

	HTR_LOG(Info, "[Particle] distribution self-test: {}", Failed == 0 ? "all passed" : "FAILED");
	return Failed;
}

int32 RunParticleIndexValiditySelfTest()
{
	int32 Failed = 0;
	auto Expect = [&](int64 Got, int64 Want, const char* Name)
		{
			if (Got != Want)
			{
				++Failed;
				HTR_LOG(Error, "[Particle] index validity FAIL {} (got {}, want {})", Name, Got, Want);
			}
		};

	HTR_LOG(Info, "[Particle] index validity: the next 'ValidateParticleIndices' errors are expected (deliberate corruption)");

	// 1) 메모리가 없는 인스턴스, 새로 할당한 인스턴스는 정상이다
	{
		FLayoutFixture F;
		F.Emitter->CacheEmitterModuleInfo();
		FParticleEmitterInstance Inst;
		Expect(Inst.ValidateParticleIndices(), 1, "empty instance is valid");
		Inst.Init(F.Emitter);
		Inst.Resize(8);
		Expect(Inst.ValidateParticleIndices(), 1, "fresh allocation is valid");

		// 2) Kill 뒤에도 순열이다
		Inst.ActiveParticles = 5;
		Inst.KillParticle(1);
		Inst.KillParticle(0);
		Expect(Inst.ActiveParticles, 3, "after two kills");
		Expect(Inst.ValidateParticleIndices(), 1, "valid after kills");
		Inst.Resize(20);
		Expect(Inst.ValidateParticleIndices(), 1, "valid after Resize");

		// 3) 일부러 망가뜨린 경우: 중복, 범위 밖, 표시 칸, 개수
		const uint16 Saved = Inst.ParticleIndices[2];
		Inst.ParticleIndices[2] = Inst.ParticleIndices[3];
		Expect(Inst.ValidateParticleIndices(), 0, "duplicate slot is detected");
		Inst.ParticleIndices[2] = Saved;
		Expect(Inst.ValidateParticleIndices(), 1, "restored");

		Inst.ParticleIndices[0] = 100;
		Expect(Inst.ValidateParticleIndices(), 0, "out-of-range slot is detected");

		// [0]을 덮어썼으니 항등 순열로 다시 만든다
		for (int32 i = 0; i < Inst.MaxActiveParticles; ++i) { Inst.ParticleIndices[i] = static_cast<uint16>(i); }
		Expect(Inst.ValidateParticleIndices(), 1, "identity permutation is valid");

		Inst.ParticleIndices[Inst.MaxActiveParticles] = 0;
		Expect(Inst.ValidateParticleIndices(), 0, "broken marker entry is detected");
		Inst.ParticleIndices[Inst.MaxActiveParticles] = FParticleEmitterInstance::UnusedIndex;

		Inst.ActiveParticles = Inst.MaxActiveParticles + 1;
		Expect(Inst.ValidateParticleIndices(), 0, "ActiveParticles above Max is detected");
		Inst.ActiveParticles = 0;
		Expect(Inst.ValidateParticleIndices(), 1, "valid again");
	}

	// 4) 생성과 소멸을 오래 반복해도 매 프레임 순열이다
	{
		FLayoutFixture F;
		F.Add(UParticleModuleTickTest::StaticClass());
		F.Emitter->CacheEmitterModuleInfo();
		GTickTestProbe.Log.clear();
		GTickTestProbe.Lifetime = 0.1f;
		GTickTestProbe.Velocity = FVector(0.0f, 0.0f, 0.0f);
		GTickTestProbe.UpdateVelocityScale = 1.0f;

		FParticleEmitterInstance Inst;
		Inst.Init(F.Emitter);
		F.SetSpawnRate(300.0f);
		int32 InvalidFrames = 0;
		int32 MaxActive = 0;
		for (int32 f = 0; f < 300; ++f)
		{
			Inst.Tick(1.0f / 64.0f);
			if (!Inst.ValidateParticleIndices()) { ++InvalidFrames; }
			MaxActive = (Inst.ActiveParticles > MaxActive) ? Inst.ActiveParticles : MaxActive;
		}
		Expect(InvalidFrames, 0, "300 ticks of spawn and kill keep a valid permutation");
		Expect(MaxActive > 5, 1, "the loop really had particles alive");
	}

	HTR_LOG(Info, "[Particle] index validity self-test: {}", Failed == 0 ? "all passed" : "FAILED");
	return Failed;
}

int32 RunParticleEmitterTimeSelfTest()
{
	int32 Failed = 0;
	auto Expect = [&](int64 Got, int64 Want, const char* Name)
		{
			if (Got != Want)
			{
				++Failed;
				HTR_LOG(Error, "[Particle] emitter time FAIL {} (got {}, want {})", Name, Got, Want);
			}
		};
	auto ExpectNear = [&](double Got, double Want, double Tol, const char* Name)
		{
			if (std::fabs(Got - Want) > Tol)
			{
				++Failed;
				HTR_LOG(Error, "[Particle] emitter time FAIL {} (got {}, want {})", Name, Got, Want);
			}
		};

	// 프레임 시간은 2의 거듭제곱 역수라 float 누적에 오차가 없다
	const float Dt = 1.0f / 64.0f;

	// 1) Required 모듈이 없으면 루프하지 않고 시간만 흐른다
	{
		FLayoutFixture F;
		F.Emitter->CacheEmitterModuleInfo();
		FParticleEmitterInstance Inst;
		Inst.Init(F.Emitter);
		for (int32 i = 0; i < 128; ++i) { Inst.Tick(Dt); }
		ExpectNear(Inst.EmitterTime, 2.0, 1e-9, "no required: time keeps flowing");
		Expect(Inst.LoopCount, 0, "no required: never loops");
		Expect(Inst.HasCompleted(), 0, "no required: never completes");
	}

	// 2) Duration 1초, 무한 반복: 64번째 Tick에서 루프하고 EmitterTime은 0으로 돌아온다
	{
		FLayoutFixture F;
		UParticleModuleRequired* Req = static_cast<UParticleModuleRequired*>(F.Add(UParticleModuleRequired::StaticClass()));
		Req->EmitterDuration = 1.0f;
		Req->EmitterLoops = 0;
		F.Emitter->CacheEmitterModuleInfo();
		FParticleEmitterInstance Inst;
		Inst.Init(F.Emitter);
		ExpectNear(Inst.EmitterDuration, 1.0, 1e-9, "loop: duration without delay");
		for (int32 i = 0; i < 63; ++i) { Inst.Tick(Dt); }
		Expect(Inst.LoopCount, 0, "loop: not yet after 63 ticks");
		ExpectNear(Inst.EmitterTime, 63.0 / 64.0, 1e-9, "loop: time after 63 ticks");
		Inst.Tick(Dt);
		Expect(Inst.LoopCount, 1, "loop: first loop at tick 64");
		ExpectNear(Inst.EmitterTime, 0.0, 1e-9, "loop: overflow carried (exactly 0 here)");
		for (int32 i = 0; i < 64; ++i) { Inst.Tick(Dt); }
		Expect(Inst.LoopCount, 2, "loop: second loop at tick 128");

		// 넘친 시간은 다음 루프로 이어진다: dt 0.4, Duration 1 → 3번째 Tick에서 1.2 → 루프 후 0.2
		FParticleEmitterInstance Coarse;
		Coarse.Init(F.Emitter);
		Coarse.Tick(0.4f); Coarse.Tick(0.4f);
		Expect(Coarse.LoopCount, 0, "overflow: two ticks do not loop");
		Coarse.Tick(0.4f);
		Expect(Coarse.LoopCount, 1, "overflow: third tick loops");
		ExpectNear(Coarse.EmitterTime, 0.2, 1e-5, "overflow: remainder carried into the next loop");
	}

	// 3) 지연: 길이에 포함되고, 지연 동안 생성하지 않고, EmitterTime이 Tick마다 누적되지 않는다
	{
		FLayoutFixture F;
		UParticleModuleRequired* Req = static_cast<UParticleModuleRequired*>(F.Add(UParticleModuleRequired::StaticClass()));
		Req->EmitterDuration = 1.0f;
		Req->EmitterDelay = 0.5f;
		Req->EmitterLoops = 0;
		F.Emitter->CacheEmitterModuleInfo();
		FParticleEmitterInstance Inst;
		Inst.Init(F.Emitter);
		ExpectNear(Inst.EmitterDuration, 1.5, 1e-9, "delay: loop length = duration + delay");
		ExpectNear(Inst.CurrentDelay, 0.5, 1e-9, "delay: CurrentDelay");
		F.SetSpawnRate(64.0f);

		for (int32 i = 0; i < 31; ++i) { Inst.Tick(Dt); }
		Expect(Inst.ActiveParticles, 0, "delay: nothing spawned during the delay (31 ticks)");
		ExpectNear(Inst.EmitterTime, 31.0 / 64.0, 1e-9, "delay: EmitterTime restored after every tick (no accumulation)");
		Inst.Tick(Dt);
		Expect(Inst.ActiveParticles, 1, "delay: spawning starts at tick 32 (EmitterTime reaches 0)");

		// 첫 루프가 끝나는 96번째 Tick까지 64개, 그 뒤 지연 32 Tick 동안은 늘지 않고 128번째 Tick에 다시 시작한다
		for (int32 i = 33; i <= 95; ++i) { Inst.Tick(Dt); }
		Expect(Inst.ParticleCounter, 64, "delay: 64 particles in the first loop's active part");
		Inst.Tick(Dt);
		Expect(Inst.LoopCount, 1, "delay: loop at tick 96");
		Expect(Inst.ParticleCounter, 64, "delay: the loop tick itself starts a new delay");
		for (int32 i = 97; i <= 127; ++i) { Inst.Tick(Dt); }
		Expect(Inst.ParticleCounter, 64, "delay: no spawn during the second delay");
		Inst.Tick(Dt);
		Expect(Inst.ParticleCounter, 65, "delay: spawning resumes at tick 128");
	}

	// 4) 루프 횟수: 정해진 횟수를 돌면 생성을 멈추고, 파티클이 모두 사라져야 완료
	{
		FLayoutFixture F;
		UParticleModuleRequired* Req = static_cast<UParticleModuleRequired*>(F.Add(UParticleModuleRequired::StaticClass()));
		Req->EmitterDuration = 1.0f;
		Req->EmitterLoops = 2;
		F.Emitter->CacheEmitterModuleInfo();
		FParticleEmitterInstance Inst;
		Inst.Init(F.Emitter);
		F.SetSpawnRate(64.0f);
		Expect(Inst.HasCompleted(), 0, "loops: not completed at the start");
		for (int32 i = 0; i < 127; ++i) { Inst.Tick(Dt); }
		Expect(Inst.ParticleCounter, 127, "loops: one particle per tick for 127 ticks");
		Inst.Tick(Dt);
		// 128번째 Tick은 두 번째 루프가 끝나는 Tick이라 생성하지 않는다 (UE와 같다)
		Expect(Inst.LoopCount, 2, "loops: two loops done");
		Expect(Inst.ParticleCounter, 127, "loops: the tick that finishes the last loop does not spawn");
		Expect(Inst.HasCompleted(), 0, "loops: particles are still alive, so not completed");
		for (int32 i = 0; i < 20; ++i) { Inst.Tick(Dt); }
		Expect(Inst.ParticleCounter, 127, "loops: no spawn after the last loop");
		Inst.ActiveParticles = 0;
		Expect(Inst.HasCompleted(), 1, "loops: completed once every particle is gone");
	}

	// 5) 범위 난수: 지연, 길이 순서로 뽑고 같은 시드면 같은 값
	{
		FLayoutFixture F;
		UParticleModuleRequired* Req = static_cast<UParticleModuleRequired*>(F.Add(UParticleModuleRequired::StaticClass()));
		Req->EmitterDuration = 3.0f;
		Req->EmitterDurationLow = 1.0f;
		Req->bEmitterDurationUseRange = true;
		Req->EmitterDelay = 1.5f;
		Req->EmitterDelayLow = 0.5f;
		Req->bEmitterDelayUseRange = true;
		F.Emitter->CacheEmitterModuleInfo();

		FParticleEmitterInstance A, B;
		A.Init(F.Emitter);
		B.Init(F.Emitter);
		Expect(A.EmitterDuration == B.EmitterDuration && A.CurrentDelay == B.CurrentDelay, 1, "range: same seed gives the same values");

		FRandomStream Reference(0);
		const float ExpectedDelay = Reference.GetFraction(0.5f, 1.5f);
		const float ExpectedDuration = Reference.GetFraction(1.0f, 3.0f);
		ExpectNear(A.CurrentDelay, ExpectedDelay, 1e-6, "range: delay is the first draw");
		ExpectNear(A.EmitterDuration, ExpectedDuration + ExpectedDelay, 1e-6, "range: duration is the second draw, plus delay");
		Expect(A.CurrentDelay >= 0.5f && A.CurrentDelay <= 1.5f, 1, "range: delay in range");

		// 범위를 쓰지 않으면 난수를 쓰지 않는다
		FLayoutFixture G;
		UParticleModuleRequired* Fixed = static_cast<UParticleModuleRequired*>(G.Add(UParticleModuleRequired::StaticClass()));
		Fixed->EmitterDuration = 2.0f;
		G.Emitter->CacheEmitterModuleInfo();
		FParticleEmitterInstance C;
		C.Init(G.Emitter);
		FRandomStream Untouched(0);
		Expect(C.RandomStream.GetFraction() == Untouched.GetFraction(), 1, "range: fixed values do not touch the stream");
	}

	HTR_LOG(Info, "[Particle] emitter time self-test: {}", Failed == 0 ? "all passed" : "FAILED");
	return Failed;
}

int32 RunParticleSpawnModuleSelfTest()
{
	int32 Failed = 0;
	auto Expect = [&](int64 Got, int64 Want, const char* Name)
		{
			if (Got != Want)
			{
				++Failed;
				HTR_LOG(Error, "[Particle] spawn module FAIL {} (got {}, want {})", Name, Got, Want);
			}
		};
	auto ExpectNear = [&](double Got, double Want, double Tol, const char* Name)
		{
			if (std::fabs(Got - Want) > Tol)
			{
				++Failed;
				HTR_LOG(Error, "[Particle] spawn module FAIL {} (got {}, want {})", Name, Got, Want);
			}
		};
	const float Dt = 1.0f / 64.0f;
	auto SlotAt = [](FParticleEmitterInstance& Inst, int32 PositionInIndices) -> FBaseParticle&
		{
			return *reinterpret_cast<FBaseParticle*>(Inst.ParticleData + static_cast<size_t>(Inst.ParticleStride) * Inst.ParticleIndices[PositionInIndices]);
		};

	// 1) Rate: RateScale 곱, 음수는 0, 꺼진 모듈과 없는 모듈은 생성하지 않음
	{
		FLayoutFixture F;
		F.Emitter->CacheEmitterModuleInfo();
		FParticleEmitterInstance Inst;
		Inst.Init(F.Emitter);
		for (int32 i = 0; i < 64; ++i) { Inst.Tick(Dt); }
		Expect(Inst.ActiveParticles, 0, "rate: no Spawn module spawns nothing");

		UParticleModuleSpawn* S = F.SetSpawnRate(64.0f);
		S->RateScale = FFloatDistribution::MakeConstant(0.5f);
		FParticleEmitterInstance Half;
		Half.Init(F.Emitter);
		for (int32 i = 0; i < 64; ++i) { Half.Tick(Dt); }
		Expect(Half.ActiveParticles, 32, "rate: Rate 64 * RateScale 0.5 gives 32 in one second");

		S->Rate = FFloatDistribution::MakeConstant(-10.0f);
		FParticleEmitterInstance Negative;
		Negative.Init(F.Emitter);
		for (int32 i = 0; i < 64; ++i) { Negative.Tick(Dt); }
		Expect(Negative.ActiveParticles, 0, "rate: negative rate is treated as zero");

		S->Rate = FFloatDistribution::MakeConstant(64.0f);
		S->bEnabled = false;
		FParticleEmitterInstance Off;
		Off.Init(F.Emitter);
		for (int32 i = 0; i < 64; ++i) { Off.Tick(Dt); }
		Expect(Off.ActiveParticles, 0, "rate: disabled Spawn module spawns nothing");
	}

	// 2) Rate가 EmitterTime에 따라 변한다: Time 0에서 0, Time 1에서 128이면 선형으로 늘어난다
	{
		FLayoutFixture F;
		UParticleModuleSpawn* S = F.SetSpawnRate(0.0f);
		S->Rate = FFloatDistribution::MakeCurve();
		S->Rate.AddKey(0.0f, 0.0f);
		S->Rate.AddKey(1.0f, 128.0f);
		F.Emitter->CacheEmitterModuleInfo();
		FParticleEmitterInstance Inst;
		Inst.Init(F.Emitter);
		for (int32 i = 0; i < 32; ++i) { Inst.Tick(Dt); }
		const int32 FirstHalf = Inst.ActiveParticles;
		for (int32 i = 0; i < 32; ++i) { Inst.Tick(Dt); }
		const int32 SecondHalf = Inst.ActiveParticles - FirstHalf;
		// 적분값은 앞 반 0.5초가 16개, 뒤 반이 48개다 (이산화 오차 허용)
		Expect(FirstHalf >= 14 && FirstHalf <= 18, 1, "curve rate: first half is about 16");
		Expect(SecondHalf >= 46 && SecondHalf <= 50, 1, "curve rate: second half is about 48");
		HTR_LOG(Info, "[Particle] spawn module curve rate: first half {}, second half {}", FirstHalf, SecondHalf);
	}

	// 3) 버스트: 정해진 시각에 한 번, Rate 0이어도 발사
	{
		FLayoutFixture F;
		UParticleModuleSpawn* S = F.SetSpawnRate(0.0f);
		FParticleBurst B;
		B.Count = 10;
		B.Time = 0.5f;
		S->BurstList.Add(B);
		F.Emitter->CacheEmitterModuleInfo();
		FParticleEmitterInstance Inst;
		Inst.Init(F.Emitter);
		for (int32 i = 0; i < 31; ++i) { Inst.Tick(Dt); }
		Expect(Inst.ActiveParticles, 0, "burst: nothing before its time");
		Inst.Tick(Dt);
		Expect(Inst.ActiveParticles, 10, "burst: fires at EmitterTime 0.5 (tick 32)");
		for (int32 i = 0; i < 100; ++i) { Inst.Tick(Dt); }
		Expect(Inst.ActiveParticles, 10, "burst: fires only once without a loop");
		Expect(Inst.ParticleCounter, 10, "burst: counter");
	}

	// 4) 버스트는 루프마다 다시 발사한다
	{
		FLayoutFixture F;
		UParticleModuleRequired* Req = static_cast<UParticleModuleRequired*>(F.Add(UParticleModuleRequired::StaticClass()));
		Req->EmitterDuration = 1.0f;
		Req->EmitterLoops = 0;
		UParticleModuleSpawn* S = F.SetSpawnRate(0.0f);
		FParticleBurst B;
		B.Count = 10;
		B.Time = 0.0f;
		S->BurstList.Add(B);
		F.Emitter->CacheEmitterModuleInfo();
		FParticleEmitterInstance Inst;
		Inst.Init(F.Emitter);
		Inst.Tick(Dt);
		Expect(Inst.ParticleCounter, 10, "burst loop: first burst at the first tick");
		for (int32 i = 2; i <= 63; ++i) { Inst.Tick(Dt); }
		Expect(Inst.ParticleCounter, 10, "burst loop: nothing more inside the first loop");
		Inst.Tick(Dt);
		Expect(Inst.LoopCount, 1, "burst loop: loop at tick 64");
		Expect(Inst.ParticleCounter, 20, "burst loop: the loop tick fires the burst again");
		for (int32 i = 65; i <= 128; ++i) { Inst.Tick(Dt); }
		Expect(Inst.ParticleCounter, 30, "burst loop: third burst at the next loop");
	}

	// 5) CountLow: [CountLow, Count]에서 난수. 같은 시드면 같은 값이고 스트림 사용은 한 번
	{
		FLayoutFixture F;
		UParticleModuleSpawn* S = F.SetSpawnRate(0.0f);
		FParticleBurst B;
		B.Count = 8;
		B.CountLow = 2;
		B.Time = 0.0f;
		S->BurstList.Add(B);
		F.Emitter->CacheEmitterModuleInfo();
		FParticleEmitterInstance Inst;
		Inst.Init(F.Emitter);
		Inst.Tick(Dt);
		FRandomStream Reference(0);
		const int32 Expected = Reference.GetInt(2, 8);
		Expect(Inst.ParticleCounter, Expected, "burst range: count is the first draw of the stream");
		Expect(Expected >= 2 && Expected <= 8, 1, "burst range: in [CountLow, Count]");
		Expect(Inst.RandomStream.GetFraction() == Reference.GetFraction(), 1, "burst range: exactly one draw");
	}

	// 5b) FRandomStream::GetInt의 경계
	{
		FRandomStream R(3), Ref(3);
		Expect(R.GetInt(5, 5), 5, "GetInt: single value");
		Expect(R.GetFraction() == Ref.GetFraction() ? 0 : 1, 1, "GetInt: a one-value range still draws once");
		FRandomStream Inverted(3), Same(3);
		Expect(Inverted.GetInt(9, 2), 9, "GetInt: inverted range returns Min");
		Expect(Inverted.GetFraction() == Same.GetFraction(), 1, "GetInt: inverted range does not draw");
		bool bAll = true;
		FRandomStream Wide(11);
		int32 SeenMask = 0;
		for (int32 i = 0; i < 2000; ++i)
		{
			const int32 V = Wide.GetInt(0, 3);
			bAll &= (V >= 0 && V <= 3);
			SeenMask |= 1 << V;
		}
		Expect(bAll, 1, "GetInt: always in range");
		Expect(SeenMask, 15, "GetInt: every value in the range is reachable");
	}

	// 6) 상한: 버스트를 먼저 채우고 남는 칸을 Rate 분량에 쓴다
	{
		FLayoutFixture F;
		F.Emitter->CacheEmitterModuleInfo();
		FParticleEmitterInstance Inst;
		Inst.Init(F.Emitter);
		Inst.Resize(FParticleEmitterInstance::MaxParticleLimit);
		Inst.ActiveParticles = FParticleEmitterInstance::MaxParticleLimit - 10;
		Inst.Spawn(Dt, 64.0f * 20.0f, 4);
		Expect(Inst.ActiveParticles, FParticleEmitterInstance::MaxParticleLimit, "limit: filled to the limit");
		Expect(Inst.ParticleCounter, 10, "limit: 4 burst + 6 rate particles");
	}

	// 7) Lifetime 모듈
	{
		FLayoutFixture F;
		UParticleModuleLifetime* L = static_cast<UParticleModuleLifetime*>(F.Add(UParticleModuleLifetime::StaticClass()));
		L->Lifetime = FFloatDistribution::MakeConstant(2.0f);
		F.SetSpawnRate(128.0f);
		F.Emitter->CacheEmitterModuleInfo();
		FParticleEmitterInstance Inst;
		Inst.Init(F.Emitter);
		Inst.Tick(Dt);
		Expect(Inst.ActiveParticles, 2, "lifetime: two particles in the first tick");
		ExpectNear(SlotAt(Inst, 0).OneOverMaxLifetime, 0.5, 1e-6, "lifetime: OneOverMaxLifetime = 1 / 2");
		// 첫 파티클은 프레임 끝보다 1/128초 먼저 생성됐다: RelativeTime = SpawnTime * OneOver
		ExpectNear(SlotAt(Inst, 0).RelativeTime, (1.0 / 128.0) * 0.5, 1e-7, "lifetime: RelativeTime offset by SpawnTime");
		ExpectNear(SlotAt(Inst, 1).RelativeTime, 0.0, 1e-9, "lifetime: second particle born at frame end");
	}
	{
		// 수명 모듈 둘: 수명을 더한다 (2 + 2 = 4)
		FLayoutFixture F;
		UParticleModuleLifetime* A = static_cast<UParticleModuleLifetime*>(F.Add(UParticleModuleLifetime::StaticClass()));
		UParticleModuleLifetime* B = static_cast<UParticleModuleLifetime*>(F.Add(UParticleModuleLifetime::StaticClass()));
		A->Lifetime = FFloatDistribution::MakeConstant(2.0f);
		B->Lifetime = FFloatDistribution::MakeConstant(2.0f);
		F.SetSpawnRate(64.0f);
		F.Emitter->CacheEmitterModuleInfo();
		FParticleEmitterInstance Inst;
		Inst.Init(F.Emitter);
		Inst.Tick(Dt);
		ExpectNear(SlotAt(Inst, 0).OneOverMaxLifetime, 0.25, 1e-6, "lifetime: two modules add the lifetimes");
	}
	{
		// 0 이하의 수명은 OneOverMaxLifetime 0: 나이로 죽지 않는다
		FLayoutFixture F;
		UParticleModuleLifetime* L = static_cast<UParticleModuleLifetime*>(F.Add(UParticleModuleLifetime::StaticClass()));
		L->Lifetime = FFloatDistribution::MakeConstant(0.0f);
		F.SetSpawnRate(64.0f);
		F.Emitter->CacheEmitterModuleInfo();
		FParticleEmitterInstance Inst;
		Inst.Init(F.Emitter);
		for (int32 i = 0; i < 640; ++i) { Inst.Tick(Dt); }
		Expect(Inst.ActiveParticles, 640, "lifetime: zero lifetime never dies by age (640 ticks, 640 alive)");
		ExpectNear(SlotAt(Inst, 0).OneOverMaxLifetime, 0.0, 1e-9, "lifetime: zero lifetime gives OneOverMaxLifetime 0");
	}
	{
		// 수명은 파티클의 RelativeTime이 아니라 이미터 시간으로 평가한다: 곡선 (0 -> 1초, 1 -> 3초)에서 EmitterTime 0.5 -> 2초
		FLayoutFixture F;
		UParticleModuleLifetime* L = static_cast<UParticleModuleLifetime*>(F.Add(UParticleModuleLifetime::StaticClass()));
		L->Lifetime = FFloatDistribution::MakeCurve();
		L->Lifetime.AddKey(0.0f, 1.0f);
		L->Lifetime.AddKey(1.0f, 3.0f);
		F.SetSpawnRate(64.0f);
		F.Emitter->CacheEmitterModuleInfo();
		FParticleEmitterInstance Inst;
		Inst.Init(F.Emitter);
		for (int32 i = 0; i < 32; ++i) { Inst.Tick(Dt); }
		ExpectNear(SlotAt(Inst, Inst.ActiveParticles - 1).OneOverMaxLifetime, 0.5, 1e-5, "lifetime curve: evaluated at EmitterTime 0.5");
		// 첫 파티클은 EmitterTime 1/64에서 생성됐다: 수명 1 + 2/64
		ExpectNear(SlotAt(Inst, 0).OneOverMaxLifetime, 1.0 / (1.0 + 2.0 / 64.0), 1e-5, "lifetime curve: the first particle used the lifetime at EmitterTime 1/64");
	}
	{
		// 범위 난수: [1, 3]에서 뽑는다
		FLayoutFixture F;
		UParticleModuleLifetime* L = static_cast<UParticleModuleLifetime*>(F.Add(UParticleModuleLifetime::StaticClass()));
		L->Lifetime = FFloatDistribution::MakeUniform(1.0f, 3.0f);
		F.SetSpawnRate(64.0f);
		F.Emitter->CacheEmitterModuleInfo();
		FParticleEmitterInstance Inst;
		Inst.Init(F.Emitter);
		Inst.Tick(Dt);
		FRandomStream Reference(0);
		const double Expected = 1.0 / Reference.GetFraction(1.0f, 3.0f);
		ExpectNear(SlotAt(Inst, 0).OneOverMaxLifetime, Expected, 1e-5, "lifetime range: first draw of the stream");
	}

	HTR_LOG(Info, "[Particle] spawn module self-test: {}", Failed == 0 ? "all passed" : "FAILED");
	return Failed;
}

int32 RunParticleInitialModulesSelfTest()
{
	int32 Failed = 0;
	auto Expect = [&](int64 Got, int64 Want, const char* Name)
		{
			if (Got != Want)
			{
				++Failed;
				HTR_LOG(Error, "[Particle] initial modules FAIL {} (got {}, want {})", Name, Got, Want);
			}
		};
	auto ExpectNear = [&](double Got, double Want, double Tol, const char* Name)
		{
			if (std::fabs(Got - Want) > Tol)
			{
				++Failed;
				HTR_LOG(Error, "[Particle] initial modules FAIL {} (got {}, want {})", Name, Got, Want);
			}
		};
	auto ExpectVec = [&](const FVector& Got, double X, double Y, double Z, const char* Name)
		{
			ExpectNear(Got.X, X, 1e-5, Name);
			ExpectNear(Got.Y, Y, 1e-5, Name);
			ExpectNear(Got.Z, Z, 1e-5, Name);
		};
	const float Dt = 1.0f / 64.0f;
	auto SlotAt = [](FParticleEmitterInstance& Inst, int32 PositionInIndices) -> FBaseParticle&
		{
			return *reinterpret_cast<FBaseParticle*>(Inst.ParticleData + static_cast<size_t>(Inst.ParticleStride) * Inst.ParticleIndices[PositionInIndices]);
		};

	// 1) Location: 원점에 더한다. 같은 모듈이 둘이면 합쳐진다.
	{
		FLayoutFixture F;
		UParticleModuleLocation* A = static_cast<UParticleModuleLocation*>(F.Add(UParticleModuleLocation::StaticClass()));
		UParticleModuleLocation* B = static_cast<UParticleModuleLocation*>(F.Add(UParticleModuleLocation::StaticClass()));
		A->StartLocation = FVectorDistribution::MakeConstant(FVector(1.0f, 2.0f, 3.0f));
		B->StartLocation = FVectorDistribution::MakeConstant(FVector(1.0f, 2.0f, 3.0f));
		F.SetSpawnRate(64.0f);
		F.Emitter->CacheEmitterModuleInfo();
		FParticleEmitterInstance Inst;
		Inst.Init(F.Emitter);
		Inst.Tick(Dt);
		ExpectVec(SlotAt(Inst, 0).Location, 2.0, 4.0, 6.0, "location: two modules add");
	}

	// 2) Location Uniform: 범위 안, 난수 3개
	{
		FLayoutFixture F;
		UParticleModuleLocation* L = static_cast<UParticleModuleLocation*>(F.Add(UParticleModuleLocation::StaticClass()));
		L->StartLocation = FVectorDistribution::MakeUniform(FVector(0.0f, 10.0f, 20.0f), FVector(1.0f, 11.0f, 21.0f));
		F.SetSpawnRate(64.0f);
		F.Emitter->CacheEmitterModuleInfo();
		FParticleEmitterInstance Inst;
		Inst.Init(F.Emitter);
		Inst.Tick(Dt);
		FRandomStream Ref(0);
		const float RX = Ref.GetFraction(), RY = Ref.GetFraction(), RZ = Ref.GetFraction();
		ExpectVec(SlotAt(Inst, 0).Location, 0.0 + RX, 10.0 + RY, 20.0 + RZ, "location uniform: X, Y, Z draws in order");
		Expect(Inst.RandomStream.GetFraction() == Ref.GetFraction(), 1, "location uniform: exactly 3 draws");
	}

	// 3) Velocity: Velocity와 BaseVelocity에 모두 더한다. Reset 뒤에도 속도가 유지되어 이동한다.
	{
		FLayoutFixture F;
		UParticleModuleVelocity* V = static_cast<UParticleModuleVelocity*>(F.Add(UParticleModuleVelocity::StaticClass()));
		V->StartVelocity = FVectorDistribution::MakeConstant(FVector(1.0f, 0.0f, 0.0f));
		F.SetSpawnRate(64.0f);
		F.Emitter->CacheEmitterModuleInfo();
		FParticleEmitterInstance Inst;
		Inst.Init(F.Emitter);
		Inst.Tick(Dt);
		ExpectVec(SlotAt(Inst, 0).Velocity, 1.0, 0.0, 0.0, "velocity: Velocity");
		ExpectVec(SlotAt(Inst, 0).BaseVelocity, 1.0, 0.0, 0.0, "velocity: BaseVelocity");
		ExpectNear(SlotAt(Inst, 0).Location.X, 0.0, 1e-9, "velocity: SpawnTime 0 means no pre-move");
		Inst.Tick(Dt);
		Inst.Tick(Dt);
		ExpectNear(SlotAt(Inst, 0).Location.X, 2.0 / 64.0, 1e-6, "velocity: moves 1 unit/s after Reset restores the base");
	}

	// 4) Velocity가 Spawn 시점에 정해지므로 프레임 중간에 생성된 파티클은 PostSpawn에서 SpawnTime만큼 먼저 이동한다
	{
		FLayoutFixture F;
		UParticleModuleVelocity* V = static_cast<UParticleModuleVelocity*>(F.Add(UParticleModuleVelocity::StaticClass()));
		V->StartVelocity = FVectorDistribution::MakeConstant(FVector(1.0f, 0.0f, 0.0f));
		F.SetSpawnRate(128.0f);
		F.Emitter->CacheEmitterModuleInfo();
		FParticleEmitterInstance Inst;
		Inst.Init(F.Emitter);
		Inst.Tick(Dt);
		ExpectNear(SlotAt(Inst, 0).Location.X, 1.0 / 128.0, 1e-7, "velocity: first particle pre-moved by SpawnTime");
		ExpectNear(SlotAt(Inst, 1).Location.X, 0.0, 1e-9, "velocity: second particle born at frame end");
	}

	// 5) 방사 속도: 파티클 위치(원점 기준) 방향으로 더한다. 모듈 순서가 결과를 바꾼다. 원점에서는 0이다.
	{
		FLayoutFixture F;
		UParticleModuleLocation* L = static_cast<UParticleModuleLocation*>(F.Add(UParticleModuleLocation::StaticClass()));
		L->StartLocation = FVectorDistribution::MakeConstant(FVector(3.0f, 0.0f, 0.0f));
		UParticleModuleVelocity* V = static_cast<UParticleModuleVelocity*>(F.Add(UParticleModuleVelocity::StaticClass()));
		V->StartVelocityRadial = FFloatDistribution::MakeConstant(2.0f);
		F.SetSpawnRate(64.0f);
		F.Emitter->CacheEmitterModuleInfo();
		FParticleEmitterInstance Inst;
		Inst.Init(F.Emitter);
		Inst.Tick(Dt);
		ExpectVec(SlotAt(Inst, 0).Velocity, 2.0, 0.0, 0.0, "radial: Location before Velocity gives outward velocity");
		ExpectVec(SlotAt(Inst, 0).BaseVelocity, 2.0, 0.0, 0.0, "radial: BaseVelocity");
	}
	{
		// Velocity가 Location보다 앞: 그 시점에 파티클은 원점이라 방사 성분이 0이다. NaN도 아니다.
		FLayoutFixture F;
		UParticleModuleVelocity* V = static_cast<UParticleModuleVelocity*>(F.Add(UParticleModuleVelocity::StaticClass()));
		V->StartVelocityRadial = FFloatDistribution::MakeConstant(2.0f);
		UParticleModuleLocation* L = static_cast<UParticleModuleLocation*>(F.Add(UParticleModuleLocation::StaticClass()));
		L->StartLocation = FVectorDistribution::MakeConstant(FVector(3.0f, 0.0f, 0.0f));
		F.SetSpawnRate(64.0f);
		F.Emitter->CacheEmitterModuleInfo();
		FParticleEmitterInstance Inst;
		Inst.Init(F.Emitter);
		Inst.Tick(Dt);
		ExpectVec(SlotAt(Inst, 0).Velocity, 0.0, 0.0, 0.0, "radial: Velocity before Location sees the origin (zero, not NaN)");
	}

	// 6) 난수 순서: StartVelocity(3개) 다음에 StartVelocityRadial(1개)
	{
		FLayoutFixture F;
		UParticleModuleVelocity* V = static_cast<UParticleModuleVelocity*>(F.Add(UParticleModuleVelocity::StaticClass()));
		V->StartVelocity = FVectorDistribution::MakeUniform(FVector(0.0f, 0.0f, 0.0f), FVector(1.0f, 1.0f, 1.0f));
		V->StartVelocityRadial = FFloatDistribution::MakeUniform(0.0f, 10.0f);
		F.SetSpawnRate(64.0f);
		F.Emitter->CacheEmitterModuleInfo();
		FParticleEmitterInstance Inst;
		Inst.Init(F.Emitter);
		Inst.Tick(Dt);
		FRandomStream Ref(0);
		const float RX = Ref.GetFraction(), RY = Ref.GetFraction(), RZ = Ref.GetFraction();
		(void)Ref.GetFraction();
		// 파티클이 원점이라 방사 성분은 0이고, 속도는 앞 3개 난수다
		ExpectVec(SlotAt(Inst, 0).Velocity, RX, RY, RZ, "velocity draws: vector first");
		Expect(Inst.RandomStream.GetFraction() == Ref.GetFraction(), 1, "velocity draws: 4 draws in total");
	}

	// 7) Size: Size와 BaseSize에 더하고, 프레임이 지나도 유지된다 (Reset이 BaseSize로 복원)
	{
		FLayoutFixture F;
		UParticleModuleSize* S = static_cast<UParticleModuleSize*>(F.Add(UParticleModuleSize::StaticClass()));
		S->StartSize = FVectorDistribution::MakeConstant(FVector(2.0f, 3.0f, 4.0f));
		F.SetSpawnRate(64.0f);
		F.Emitter->CacheEmitterModuleInfo();
		FParticleEmitterInstance Inst;
		Inst.Init(F.Emitter);
		Inst.Tick(Dt);
		ExpectVec(SlotAt(Inst, 0).Size, 2.0, 3.0, 4.0, "size: Size");
		ExpectVec(SlotAt(Inst, 0).BaseSize, 2.0, 3.0, 4.0, "size: BaseSize");
		for (int32 i = 0; i < 5; ++i) { Inst.Tick(Dt); }
		ExpectVec(SlotAt(Inst, 0).Size, 2.0, 3.0, 4.0, "size: still the same after Reset");
	}

	// 8) 꺼진 모듈은 적용되지 않는다
	{
		FLayoutFixture F;
		UParticleModuleSize* S = static_cast<UParticleModuleSize*>(F.Add(UParticleModuleSize::StaticClass()));
		S->StartSize = FVectorDistribution::MakeConstant(FVector(2.0f, 3.0f, 4.0f));
		S->bEnabled = false;
		F.SetSpawnRate(64.0f);
		F.Emitter->CacheEmitterModuleInfo();
		FParticleEmitterInstance Inst;
		Inst.Init(F.Emitter);
		Inst.Tick(Dt);
		ExpectVec(SlotAt(Inst, 0).Size, 0.0, 0.0, 0.0, "disabled: size stays zero");
	}

	HTR_LOG(Info, "[Particle] initial modules self-test: {}", Failed == 0 ? "all passed" : "FAILED");
	return Failed;
}

int32 RunParticleColorModuleSelfTest()
{
	int32 Failed = 0;
	auto ExpectNear = [&](double Got, double Want, const char* Name)
		{
			if (std::fabs(Got - Want) > 1e-5)
			{
				++Failed;
				HTR_LOG(Error, "[Particle] color module FAIL {} (got {}, want {})", Name, Got, Want);
			}
		};
	auto ExpectTrue = [&](bool bValue, const char* Name)
		{
			if (!bValue)
			{
				++Failed;
				HTR_LOG(Error, "[Particle] color module FAIL {}", Name);
			}
		};
	const float Dt = 1.0f / 64.0f;
	auto SlotAt = [](FParticleEmitterInstance& Inst, int32 PositionInIndices) -> FBaseParticle&
		{
			return *reinterpret_cast<FBaseParticle*>(Inst.ParticleData + static_cast<size_t>(Inst.ParticleStride) * Inst.ParticleIndices[PositionInIndices]);
		};

	// 1) 상수 색: RGB는 StartColor, A는 StartAlpha. 1을 넘는 값도 그대로 두고 Reset 뒤에도 유지된다.
	{
		FLayoutFixture F;
		UParticleModuleColor* C = static_cast<UParticleModuleColor*>(F.Add(UParticleModuleColor::StaticClass()));
		C->StartColor = FVectorDistribution::MakeConstant(FVector(0.25f, 2.5f, 0.75f));
		C->StartAlpha = FFloatDistribution::MakeConstant(0.5f);
		F.SetSpawnRate(64.0f);
		F.Emitter->CacheEmitterModuleInfo();
		FParticleEmitterInstance Inst;
		Inst.Init(F.Emitter);
		Inst.Tick(Dt);
		FBaseParticle& P = SlotAt(Inst, 0);
		ExpectNear(P.Color.R, 0.25, "color: R");
		ExpectNear(P.Color.G, 2.5, "color: G is not clamped to 1");
		ExpectNear(P.Color.B, 0.75, "color: B");
		ExpectNear(P.Color.A, 0.5, "color: A");
		ExpectNear(P.BaseColor.G, 2.5, "color: BaseColor");
		for (int32 i = 0; i < 4; ++i) { Inst.Tick(Dt); }
		ExpectNear(SlotAt(Inst, 0).Color.G, 2.5, "color: kept after Reset");
	}

	// 2) 색 모듈이 둘이면 마지막 모듈이 이긴다 (더하지 않고 대입)
	{
		FLayoutFixture F;
		UParticleModuleColor* A = static_cast<UParticleModuleColor*>(F.Add(UParticleModuleColor::StaticClass()));
		UParticleModuleColor* B = static_cast<UParticleModuleColor*>(F.Add(UParticleModuleColor::StaticClass()));
		A->StartColor = FVectorDistribution::MakeConstant(FVector(1.0f, 0.0f, 0.0f));
		B->StartColor = FVectorDistribution::MakeConstant(FVector(0.0f, 1.0f, 0.0f));
		F.SetSpawnRate(64.0f);
		F.Emitter->CacheEmitterModuleInfo();
		FParticleEmitterInstance Inst;
		Inst.Init(F.Emitter);
		Inst.Tick(Dt);
		ExpectNear(SlotAt(Inst, 0).Color.R, 0.0, "two color modules: last one wins (R)");
		ExpectNear(SlotAt(Inst, 0).Color.G, 1.0, "two color modules: last one wins (G)");
	}

	// 3) 난수 순서: StartColor(3개) 다음 StartAlpha(1개)
	{
		FLayoutFixture F;
		UParticleModuleColor* C = static_cast<UParticleModuleColor*>(F.Add(UParticleModuleColor::StaticClass()));
		C->StartColor = FVectorDistribution::MakeUniform(FVector(0.0f, 0.0f, 0.0f), FVector(1.0f, 1.0f, 1.0f));
		C->StartAlpha = FFloatDistribution::MakeUniform(0.0f, 1.0f);
		F.SetSpawnRate(64.0f);
		F.Emitter->CacheEmitterModuleInfo();
		FParticleEmitterInstance Inst;
		Inst.Init(F.Emitter);
		Inst.Tick(Dt);
		FRandomStream Ref(0);
		const float R = Ref.GetFraction(), G = Ref.GetFraction(), B = Ref.GetFraction(), A = Ref.GetFraction();
		ExpectNear(SlotAt(Inst, 0).Color.R, R, "color draws: R first");
		ExpectNear(SlotAt(Inst, 0).Color.G, G, "color draws: G second");
		ExpectNear(SlotAt(Inst, 0).Color.B, B, "color draws: B third");
		ExpectNear(SlotAt(Inst, 0).Color.A, A, "color draws: A fourth");
		ExpectTrue(Inst.RandomStream.GetFraction() == Ref.GetFraction(), "color draws: exactly 4 draws");
	}

	// 4) 색 모듈이 없으면 PreSpawn의 0(투명한 검정)이다. 보이게 하려면 색 모듈이 필요하다.
	{
		FLayoutFixture F;
		F.SetSpawnRate(64.0f);
		F.Emitter->CacheEmitterModuleInfo();
		FParticleEmitterInstance Inst;
		Inst.Init(F.Emitter);
		Inst.Tick(Dt);
		ExpectNear(SlotAt(Inst, 0).Color.A, 0.0, "no color module: alpha is zero");
	}

	HTR_LOG(Info, "[Particle] color module self-test: {}", Failed == 0 ? "all passed" : "FAILED");
	return Failed;
}

int32 RunParticleComponentSelfTest()
{
	int32 Failed = 0;
	auto ExpectNear = [&](double Got, double Want, double Tol, const char* Name)
		{
			if (std::fabs(Got - Want) > Tol)
			{
				++Failed;
				HTR_LOG(Error, "[Particle] component FAIL {} (got {}, want {})", Name, Got, Want);
			}
		};
	auto ExpectInt = [&](int64 Got, int64 Want, const char* Name)
		{
			if (Got != Want)
			{
				++Failed;
				HTR_LOG(Error, "[Particle] component FAIL {} (got {}, want {})", Name, Got, Want);
			}
		};
	auto ExpectVec = [&](const FVector& Got, double X, double Y, double Z, const char* Name)
		{
			ExpectNear(Got.X, X, 1e-4, Name);
			ExpectNear(Got.Y, Y, 1e-4, Name);
			ExpectNear(Got.Z, Z, 1e-4, Name);
		};
	const float Dt = 1.0f / 64.0f;
	auto SlotAt = [](FParticleEmitterInstance& Inst, int32 PositionInIndices) -> FBaseParticle&
		{
			return *reinterpret_cast<FBaseParticle*>(Inst.ParticleData + static_cast<size_t>(Inst.ParticleStride) * Inst.ParticleIndices[PositionInIndices]);
		};
	auto Translation = [](float X, float Y, float Z)
		{
			return FTransform(FRotator(0.0f, 0.0f, 0.0f), FVector(X, Y, Z), FVector(1.0f, 1.0f, 1.0f)).GetLocalMatrix();
		};

	// 1) 행렬 규약: 두 행렬의 곱은 왼쪽이 먼저 적용된다 (EmitterToComponent * ComponentToWorld에 의존한다)
	{
		FMatrix First = Translation(1.0f, 0.0f, 0.0f);
		FMatrix Second = FTransform(FRotator(0.0f, 90.0f, 0.0f), FVector(0.0f, 0.0f, 0.0f), FVector(1.0f, 1.0f, 1.0f)).GetLocalMatrix();
		const FVector Direct = Second.TransformPosition(First.TransformPosition(FVector(0.0f, 0.0f, 0.0f)));
		const FVector Combined = (First * Second).TransformPosition(FVector(0.0f, 0.0f, 0.0f));
		ExpectVec(Combined, Direct.X, Direct.Y, Direct.Z, "matrix order: (A * B) applies A first");
	}

	// 2) 월드 공간(기본): 이동만 있는 컴포넌트 변환 + Location 모듈 + EmitterOrigin
	{
		FLayoutFixture F;
		UParticleModuleRequired* Req = static_cast<UParticleModuleRequired*>(F.Add(UParticleModuleRequired::StaticClass()));
		Req->EmitterOrigin = FVector(0.0f, 5.0f, 0.0f);
		UParticleModuleLocation* L = static_cast<UParticleModuleLocation*>(F.Add(UParticleModuleLocation::StaticClass()));
		L->StartLocation = FVectorDistribution::MakeConstant(FVector(1.0f, 0.0f, 0.0f));
		F.SetSpawnRate(64.0f);
		F.Emitter->CacheEmitterModuleInfo();
		FParticleEmitterInstance Inst;
		Inst.Init(F.Emitter);
		Inst.SetComponentToWorld(Translation(10.0f, 20.0f, 30.0f));
		Inst.Tick(Dt);
		ExpectVec(SlotAt(Inst, 0).Location, 11.0, 25.0, 30.0, "world space: translation + EmitterOrigin + local offset");
		// 컴포넌트가 움직여도 이미 나온 파티클은 남는다
		Inst.SetComponentToWorld(Translation(100.0f, 0.0f, 0.0f));
		Inst.Tick(Dt);
		ExpectVec(SlotAt(Inst, 0).Location, 11.0, 25.0, 30.0, "world space: existing particles stay when the component moves");
		ExpectVec(SlotAt(Inst, 1).Location, 101.0, 5.0, 0.0, "world space: new particles spawn at the new position");
	}

	// 3) 로컬 공간: 파티클 좌표는 컴포넌트 기준이고 SimulationToWorld가 컴포넌트 행렬이다
	{
		FLayoutFixture F;
		UParticleModuleRequired* Req = static_cast<UParticleModuleRequired*>(F.Add(UParticleModuleRequired::StaticClass()));
		Req->bUseLocalSpace = true;
		UParticleModuleLocation* L = static_cast<UParticleModuleLocation*>(F.Add(UParticleModuleLocation::StaticClass()));
		L->StartLocation = FVectorDistribution::MakeConstant(FVector(1.0f, 0.0f, 0.0f));
		F.SetSpawnRate(64.0f);
		F.Emitter->CacheEmitterModuleInfo();
		FParticleEmitterInstance Inst;
		Inst.Init(F.Emitter);
		Inst.SetComponentToWorld(Translation(10.0f, 20.0f, 30.0f));
		Inst.Tick(Dt);
		ExpectVec(SlotAt(Inst, 0).Location, 1.0, 0.0, 0.0, "local space: particle position is in component space");
		ExpectVec(Inst.SimulationToWorld.TransformPosition(SlotAt(Inst, 0).Location), 11.0, 20.0, 30.0, "local space: SimulationToWorld gives the world position");
	}

	// 4) 회전: 길이가 보존되고 방향이 바뀐다 (yaw 90도에서 X 오프셋은 X축 밖으로 돈다)
	{
		FLayoutFixture F;
		UParticleModuleLocation* L = static_cast<UParticleModuleLocation*>(F.Add(UParticleModuleLocation::StaticClass()));
		L->StartLocation = FVectorDistribution::MakeConstant(FVector(3.0f, 0.0f, 0.0f));
		F.SetSpawnRate(64.0f);
		F.Emitter->CacheEmitterModuleInfo();
		FParticleEmitterInstance Inst;
		Inst.Init(F.Emitter);
		Inst.SetComponentToWorld(FTransform(FRotator(0.0f, 90.0f, 0.0f), FVector(0.0f, 0.0f, 0.0f), FVector(1.0f, 1.0f, 1.0f)).GetLocalMatrix());
		Inst.Tick(Dt);
		const FVector P = SlotAt(Inst, 0).Location;
		ExpectNear(P.X * P.X + P.Y * P.Y + P.Z * P.Z, 9.0, 1e-3, "rotation: length preserved");
		ExpectNear(P.X, 0.0, 1e-4, "rotation: X offset leaves the X axis at yaw 90");
	}

	// 5) 방사 속도는 시뮬레이션 공간의 이미터 원점 기준이다 (월드 원점 기준이 아니다)
	{
		FLayoutFixture F;
		UParticleModuleLocation* L = static_cast<UParticleModuleLocation*>(F.Add(UParticleModuleLocation::StaticClass()));
		L->StartLocation = FVectorDistribution::MakeConstant(FVector(0.0f, 3.0f, 0.0f));
		UParticleModuleVelocity* V = static_cast<UParticleModuleVelocity*>(F.Add(UParticleModuleVelocity::StaticClass()));
		V->StartVelocityRadial = FFloatDistribution::MakeConstant(2.0f);
		F.SetSpawnRate(64.0f);
		F.Emitter->CacheEmitterModuleInfo();
		FParticleEmitterInstance Inst;
		Inst.Init(F.Emitter);
		Inst.SetComponentToWorld(Translation(10.0f, 0.0f, 0.0f));
		Inst.Tick(Dt);
		ExpectVec(SlotAt(Inst, 0).Velocity, 0.0, 2.0, 0.0, "radial: direction is from the emitter origin, not the world origin");
	}

	// 6) 시드: 같은 시드는 같은 길이와 지연, Init을 다시 해도 같다, 다른 시드는 다르다
	{
		FLayoutFixture F;
		UParticleModuleRequired* Req = static_cast<UParticleModuleRequired*>(F.Add(UParticleModuleRequired::StaticClass()));
		Req->EmitterDuration = 3.0f;
		Req->EmitterDurationLow = 1.0f;
		Req->bEmitterDurationUseRange = true;
		F.Emitter->CacheEmitterModuleInfo();
		FParticleEmitterInstance A, B, C;
		A.Init(F.Emitter, 7);
		B.Init(F.Emitter, 7);
		C.Init(F.Emitter, 8);
		const float FirstA = A.EmitterDuration;
		ExpectNear(A.EmitterDuration, B.EmitterDuration, 0.0, "seed: same seed gives the same duration");
		ExpectInt(A.EmitterDuration != C.EmitterDuration ? 1 : 0, 1, "seed: different seed gives a different duration");
		A.Init(F.Emitter, 7);
		ExpectNear(A.EmitterDuration, FirstA, 0.0, "seed: re-Init restarts the sequence");
	}

	// 7) 컴포넌트: 인스턴스 생성, 월드 위치 따라 생성, 생성 중단과 완료, 시드 재현, 템플릿 교체
	{
		UParticleSystem* System = NewObject<UParticleSystem>();
		System->SetPath("SelfTestParticle.particle");
		UParticleEmitter* Emitter = System->AddEmitter();
		UParticleLODLevel* LOD = Emitter->AddLODLevel();
		LOD->AddModule(UParticleModuleRequired::StaticClass());
		UParticleModuleSpawn* Spawn = static_cast<UParticleModuleSpawn*>(LOD->AddModule(UParticleModuleSpawn::StaticClass()));
		Spawn->Rate = FFloatDistribution::MakeConstant(64.0f);
		UParticleModuleLifetime* Life = static_cast<UParticleModuleLifetime*>(LOD->AddModule(UParticleModuleLifetime::StaticClass()));
		Life->Lifetime = FFloatDistribution::MakeConstant(0.25f);
		UParticleModuleLocation* Loc = static_cast<UParticleModuleLocation*>(LOD->AddModule(UParticleModuleLocation::StaticClass()));
		Loc->StartLocation = FVectorDistribution::MakeUniform(FVector(-1.0f, -1.0f, -1.0f), FVector(1.0f, 1.0f, 1.0f));

		UParticleSystemComponent* Comp = NewObject<UParticleSystemComponent>();
		Comp->RandomSeed = 42;
		Comp->SetTemplate(System);
		ExpectInt(Comp->GetNumEmitterInstances(), 0, "component: no instances before the first tick");
		Comp->SetRelativeLocation(FVector(10.0f, 0.0f, 0.0f));
		for (int32 i = 0; i < 64; ++i) { Comp->TickComponent(Dt); }
		ExpectInt(Comp->GetNumEmitterInstances(), 1, "component: one instance per emitter, created on the first tick");
		const int32 Steady = Comp->GetActiveParticleCount();
		// 수명 0.25초 = 16프레임이라 정상 상태에서 약 16개가 살아 있다
		ExpectInt(Steady >= 14 && Steady <= 18 ? 1 : 0, 1, "component: steady alive count is about 16");
		FParticleEmitterInstance* Inst = Comp->GetEmitterInstance(0);
		bool bAllNearComponent = Inst != nullptr;
		for (int32 i = 0; Inst && i < Inst->ActiveParticles; ++i)
		{
			const FVector P = SlotAt(*Inst, i).Location;
			bAllNearComponent &= (P.X >= 9.0f - 1e-3f && P.X <= 11.0f + 1e-3f && std::fabs(P.Y) <= 1.0f + 1e-3f);
		}
		ExpectInt(bAllNearComponent ? 1 : 0, 1, "component: particles spawn around the component's world position");

		// 같은 시드의 두 번째 컴포넌트는 같은 결과를 낸다
		UParticleSystemComponent* Twin = NewObject<UParticleSystemComponent>();
		Twin->RandomSeed = 42;
		Twin->SetTemplate(System);
		Twin->SetRelativeLocation(FVector(10.0f, 0.0f, 0.0f));
		for (int32 i = 0; i < 64; ++i) { Twin->TickComponent(Dt); }
		FParticleEmitterInstance* TwinInst = Twin->GetEmitterInstance(0);
		bool bSame = Inst && TwinInst && Inst->ActiveParticles == TwinInst->ActiveParticles;
		for (int32 i = 0; bSame && i < Inst->ActiveParticles; ++i)
		{
			bSame &= (SlotAt(*Inst, i).Location.X == SlotAt(*TwinInst, i).Location.X);
		}
		ExpectInt(bSame ? 1 : 0, 1, "component: same RandomSeed reproduces the same particles");

		// 생성을 멈추면 파티클이 모두 사라질 때 완료된다
		ExpectInt(Comp->HasCompleted() ? 1 : 0, 0, "component: running emitter is not completed");
		Comp->StopEmitting();
		for (int32 i = 0; i < 40; ++i) { Comp->TickComponent(Dt); }
		ExpectInt(Comp->GetActiveParticleCount(), 0, "component: all particles die after StopEmitting");
		ExpectInt(Comp->HasCompleted() ? 1 : 0, 1, "component: completed once stopped and empty");
		Comp->StartEmitting();
		for (int32 i = 0; i < 4; ++i) { Comp->TickComponent(Dt); }
		ExpectInt(Comp->GetActiveParticleCount() > 0 ? 1 : 0, 1, "component: StartEmitting resumes spawning");

		// 템플릿을 비우면 인스턴스가 사라진다. 컴포넌트를 먼저 지우고 에셋을 지운다 (수명 규칙).
		Comp->SetTemplate(nullptr);
		ExpectInt(Comp->GetNumEmitterInstances(), 0, "component: clearing the template destroys the instances");
		Comp->TickComponent(Dt);
		ExpectInt(Comp->GetNumEmitterInstances(), 0, "component: no template, no instances");
		delete Twin;
		delete Comp;
		DestroyParticleSystem(System);
	}

	HTR_LOG(Info, "[Particle] component self-test: {}", Failed == 0 ? "all passed" : "FAILED");
	return Failed;
}

namespace
{
	// 모든 모듈 종류와 값 종류(상수, 균일, 곡선, 버스트)를 쓰는 에셋. 기본값이 아닌 값만 넣어 읽기가 기본값으로 때우지 못하게 한다.
	UParticleSystem* MakeSerializationSampleSystem()
	{
		UParticleSystem* System = NewObject<UParticleSystem>();
		for (int32 EmitterIndex = 0; EmitterIndex < 2; ++EmitterIndex)
		{
			UParticleEmitter* Emitter = System->AddEmitter();
			Emitter->EmitterName = EmitterIndex == 0 ? "Sparks" : "Smoke";
			UParticleLODLevel* LOD = Emitter->AddLODLevel();

			UParticleModuleRequired* Required = static_cast<UParticleModuleRequired*>(LOD->AddModule(UParticleModuleRequired::StaticClass()));
			Required->EmitterDuration = 2.5f;
			Required->EmitterDurationLow = 1.5f;
			Required->bEmitterDurationUseRange = true;
			Required->EmitterLoops = 3;
			Required->EmitterDelay = 0.25f;
			Required->bUseLocalSpace = (EmitterIndex == 1);
			Required->EmitterOrigin = FVector(1.0f, 2.0f, 3.0f);
			Required->EmitterRotation = FRotator(10.0f, 20.0f, 30.0f);
			// 이미터 1: Additive 에셋 머티리얼의 텍스처만 바꾼 인스턴스, 이미터 0: 비워 둔다 (엔진 기본 스프라이트 머티리얼)
			if (EmitterIndex == 1)
			{
				Required->SetMaterial(UAssetManager::GetAssetByPath<UMaterial>(ResourcePaths::ParticleSpriteAdditiveMaterial));
				Required->SetMaterialTexture(UAssetManager::Get().LoadTexture(ResourcePaths::DefaultParticleTexture), ResourcePaths::ParticleSpriteMaterial);
			}

			UParticleModuleSpawn* Spawn = static_cast<UParticleModuleSpawn*>(LOD->AddModule(UParticleModuleSpawn::StaticClass()));
			Spawn->Rate = FFloatDistribution::MakeCurve();
			Spawn->Rate.AddKey(0.0f, 10.0f);
			Spawn->Rate.AddKey(2.0f, 50.0f);
			Spawn->RateScale = FFloatDistribution::MakeUniform(0.5f, 1.5f);
			FParticleBurst B1; B1.Count = 12; B1.CountLow = 4; B1.Time = 0.5f;
			FParticleBurst B2; B2.Count = 3; B2.CountLow = -1; B2.Time = 1.25f;
			Spawn->BurstList.Add(B1);
			Spawn->BurstList.Add(B2);

			UParticleModuleLifetime* Life = static_cast<UParticleModuleLifetime*>(LOD->AddModule(UParticleModuleLifetime::StaticClass()));
			Life->Lifetime = FFloatDistribution::MakeUniform(0.75f, 1.75f);
			UParticleModuleLocation* Loc = static_cast<UParticleModuleLocation*>(LOD->AddModule(UParticleModuleLocation::StaticClass()));
			Loc->StartLocation = FVectorDistribution::MakeUniform(FVector(-1.0f, -2.0f, -3.0f), FVector(1.0f, 2.0f, 3.0f));
			UParticleModuleVelocity* Vel = static_cast<UParticleModuleVelocity*>(LOD->AddModule(UParticleModuleVelocity::StaticClass()));
			Vel->StartVelocity = FVectorDistribution::MakeConstant(FVector(0.0f, 0.0f, 4.0f));
			Vel->StartVelocityRadial = FFloatDistribution::MakeUniform(1.0f, 2.0f);
			UParticleModuleSize* Size = static_cast<UParticleModuleSize*>(LOD->AddModule(UParticleModuleSize::StaticClass()));
			Size->StartSize = FVectorDistribution::MakeCurve();
			Size->StartSize.AddKey(0.0f, FVector(1.0f, 1.0f, 1.0f));
			Size->StartSize.AddKey(1.0f, FVector(2.0f, 3.0f, 4.0f));
			UParticleModuleColor* Color = static_cast<UParticleModuleColor*>(LOD->AddModule(UParticleModuleColor::StaticClass()));
			Color->StartColor = FVectorDistribution::MakeConstant(FVector(0.25f, 2.5f, 0.5f));
			Color->StartAlpha = FFloatDistribution::MakeConstant(0.75f);
			Color->bEnabled = false;
			LOD->AddModule(UParticleModuleTickTest::StaticClass());
			Emitter->CacheEmitterModuleInfo();
		}
		return System;
	}

	// 같은 시드로 이미터를 N Tick 돌려 살아 있는 파티클 수와 위치 합계를 돌려준다 (동작이 같은지 비교하는 용도)
	void RunSerializationFingerprint(UParticleSystem& System, int32& OutActive, double& OutSum)
	{
		OutActive = 0;
		OutSum = 0.0;
		for (int32 EmitterIndex = 0; EmitterIndex < System.GetNumEmitters(); ++EmitterIndex)
		{
			FParticleEmitterInstance Inst;
			if (!Inst.Init(System.GetEmitter(EmitterIndex), 5u + static_cast<uint32>(EmitterIndex))) { continue; }
			for (int32 i = 0; i < 100; ++i) { Inst.Tick(1.0f / 64.0f); }
			OutActive += Inst.ActiveParticles;
			for (int32 i = 0; i < Inst.ActiveParticles; ++i)
			{
				const FBaseParticle& P = *reinterpret_cast<const FBaseParticle*>(Inst.ParticleData + static_cast<size_t>(Inst.ParticleStride) * Inst.ParticleIndices[i]);
				OutSum += static_cast<double>(P.Location.X) + 2.0 * P.Location.Y + 3.0 * P.Location.Z + 5.0 * P.Color.G;
			}
		}
	}
}

int32 RunParticleAssetSelfTest()
{
	int32 Failed = 0;
	auto Expect = [&](bool bCondition, const char* Name)
		{
			if (!bCondition)
			{
				++Failed;
				HTR_LOG(Error, "[Particle] asset FAIL {}", Name);
			}
		};

	GTickTestProbe.Lifetime = 0.5f;
	GTickTestProbe.Velocity = FVector(0.0f, 0.0f, 0.0f);
	GTickTestProbe.UpdateVelocityScale = 1.0f;

	// 1) 전체 왕복: 시스템 → JSON 문자열 → 시스템. 다시 쓴 JSON이 원본과 같고, 같은 시드의 시뮬레이션 결과도 같다.
	{
		UParticleSystem* Original = MakeSerializationSampleSystem();
		const json First = ParticleAsset::ToJson(*Original);
		const json Parsed = json::parse(First.dump(2), nullptr, false);
		Expect(!Parsed.is_discarded(), "dump output parses back");

		FString Error;
		UParticleSystem* Loaded = ParticleAsset::FromJson(Parsed, Error);
		Expect(Loaded != nullptr, "FromJson succeeds");
		if (Loaded)
		{
			Expect(ParticleAsset::ToJson(*Loaded) == First, "round trip: re-saved JSON equals the original");
			Expect(Loaded->GetNumEmitters() == 2, "round trip: two emitters");
			UParticleEmitter* E1 = Loaded->GetEmitter(1);
			Expect(E1 && E1->EmitterName == "Smoke", "round trip: emitter name (UPROPERTY)");
			UParticleLODLevel* LOD = E1 ? E1->GetLODLevel(0) : nullptr;
			Expect(LOD && LOD->Level == 0 && LOD->Modules.Num() == 6, "round trip: LOD level and module list");
			Expect(LOD && LOD->RequiredModule && LOD->RequiredModule->bUseLocalSpace && LOD->RequiredModule->EmitterLoops == 3 &&
				LOD->RequiredModule->EmitterOrigin.Z == 3.0f && LOD->RequiredModule->EmitterRotation.Yaw == 20.0f &&
				LOD->RequiredModule->bEmitterDurationUseRange, "round trip: Required properties");
			Expect(LOD && LOD->SpawnModule && LOD->SpawnModule->BurstList.Num() == 2 && LOD->SpawnModule->BurstList[0].CountLow == 4 &&
				LOD->SpawnModule->BurstList[1].CountLow == -1 && LOD->SpawnModule->BurstList[1].Time == 1.25f &&
				LOD->SpawnModule->Rate.Keys.Num() == 2, "round trip: Spawn rate curve and burst list");
			Expect(LOD && !LOD->Modules[4]->bEnabled, "round trip: disabled flag (UPROPERTY on the base module)");
			UParticleLODLevel* LOD0 = Loaded->GetEmitter(0)->GetLODLevel(0);
			{
				const UMaterial* Saved = Original->GetEmitter(1)->GetLODLevel(0)->RequiredModule->Material;
				const UMaterial* Mat = LOD ? LOD->RequiredModule->Material : nullptr;
				Expect(Mat && Mat != Saved && Mat->bIsInstance && Mat->PSOType == EPSOType::ParticleSprite_Additive &&
					!Mat->Textures.IsEmpty() && Saved && Mat->Textures[0] == Saved->Textures[0], "round trip: Material instance (Additive base + texture by asset path)");
				Expect(LOD0 && LOD0->RequiredModule->Material == nullptr, "round trip: null Material stays null");
			}
			Expect(E1 && E1->ParticleStride != 0 && E1->ModuleOffsetMap.Num() == 1, "round trip: layout cache rebuilt (TickTest payload)");
			Expect(LOD && LOD->SpawnModules.Num() == 6 && LOD->UpdateModules.Num() == 1, "round trip: Spawn/Update module lists rebuilt");

			int32 ActiveA = 0, ActiveB = 0;
			double SumA = 0.0, SumB = 0.0;
			RunSerializationFingerprint(*Original, ActiveA, SumA);
			RunSerializationFingerprint(*Loaded, ActiveB, SumB);
			Expect(ActiveA == ActiveB && SumA == SumB && ActiveA > 0, "round trip: same seed gives identical simulation");
			HTR_LOG(Info, "[Particle] asset round trip: alive {} / {}, checksum {} / {}", ActiveA, ActiveB, SumA, SumB);
			DestroyParticleSystem(Loaded);
		}
		DestroyParticleSystem(Original);
	}

	// 2) 깨진 파일은 실패로 끝나고 부분 트리를 남기지 않는다 (에러 로그는 예상된 것)
	{
		HTR_LOG(Info, "[Particle] asset: the next load failures are expected (deliberately broken files)");
		UParticleSystem* Original = MakeSerializationSampleSystem();
		const json Good = ParticleAsset::ToJson(*Original);
		FString Error;

		auto ExpectRejected = [&](json Broken, const char* Name)
			{
				FString LocalError;
				UParticleSystem* Result = ParticleAsset::FromJson(Broken, LocalError);
				Expect(Result == nullptr && !LocalError.empty(), Name);
				DestroyParticleSystem(Result);
			};
		{ json B = Good; B["Type"] = "Other"; ExpectRejected(B, "wrong Type"); }
		{ json B = Good; B["Version"] = 99; ExpectRejected(B, "wrong Version"); }
		{ json B = Good; B.erase("Emitters"); ExpectRejected(B, "missing Emitters"); }
		{ json B = Good; B["Emitters"][0]["LODLevels"] = json::array(); ExpectRejected(B, "empty LODLevels"); }
		{ json B = Good; B["Emitters"][1]["LODLevels"][0]["Required"]["Class"] = "UNoSuchModule"; ExpectRejected(B, "unknown module class (second emitter, partial tree is freed)"); }
		{ json B = Good; B["Emitters"][0]["LODLevels"][0]["Modules"][0]["Class"] = "UParticleModuleRequired"; ExpectRejected(B, "slot-only class in the Modules list"); }
		{ json B = Good; B["Emitters"][0]["LODLevels"][0]["Required"]["Class"] = "UParticleModuleLifetime"; ExpectRejected(B, "wrong class in the Required slot"); }
		{ json B = Good; B["Emitters"][0]["LODLevels"][0]["Modules"][0] = nullptr; ExpectRejected(B, "null in the Modules list"); }
		{ json B = Good; B["Emitters"] = json::array(); for (int32 i = 0; i < 65; ++i) { B["Emitters"].push_back(Good["Emitters"][0]); } ExpectRejected(B, "too many emitters"); }

		// 필드 하나만 틀린 경우는 그 필드만 기본값을 유지하고 나머지는 읽는다
		{
			json B = Good;
			B["Emitters"][0]["LODLevels"][0]["Spawn"]["Data"]["Rate"]["Kind"] = "Bogus";
			B["Emitters"][0]["LODLevels"][0]["Spawn"]["Data"]["BurstList"][0]["Count"] = "many";
			UParticleSystem* Partial = ParticleAsset::FromJson(B, Error);
			Expect(Partial != nullptr, "malformed fields do not reject the whole asset");
			if (Partial)
			{
				UParticleModuleSpawn* S = Partial->GetEmitter(0)->GetLODLevel(0)->SpawnModule;
				Expect(S && S->Rate.Kind == EDistributionKind::Constant && S->Rate.Constant == 20.0f, "malformed Rate keeps the default");
				Expect(S && S->BurstList.Num() == 0, "malformed burst list keeps the default (empty)");
				Expect(S && S->RateScale.Kind == EDistributionKind::Uniform, "the other fields are still read");
				DestroyParticleSystem(Partial);
			}
		}
		DestroyParticleSystem(Original);
	}

	// 3) 에셋 매니저와 파일. 프로젝트의 Assets 폴더를 건드리지 않도록 파일은 임시 폴더에만 쓰고,
	//    등록·형식 감지·캐시는 RegisterAsset으로 직접 등록해서 확인한다 (SaveParticleSystem의 확장자 검사는 파일을 쓰기 전에 거절한다).
	{
		const FString Key = "ParticleSelfTest/RoundTrip.particle";
		UParticleSystem* System = MakeSerializationSampleSystem();
		Expect(!UAssetManager::SaveParticleSystem("ParticleSelfTest/NotAParticle.txt", *System), "save rejects a wrong extension");

		const std::filesystem::path TempDir = std::filesystem::temp_directory_path() / "HitoriParticleSelfTest";
		const std::filesystem::path TempFile = TempDir / "RoundTrip.particle";
		Expect(UAssetManager::WriteParticleSystemFile(TempFile, *System), "WriteParticleSystemFile writes the file (temp folder)");
		{
			std::ifstream In(TempFile);
			const json FromDisk = json::parse(In, nullptr, false);
			FString Error;
			UParticleSystem* Reloaded = ParticleAsset::FromJson(FromDisk, Error);
			Expect(Reloaded != nullptr && ParticleAsset::ToJson(*Reloaded) == ParticleAsset::ToJson(*System), "the file on disk reloads to the same asset");
			DestroyParticleSystem(Reloaded);
		}
		// 파일 하나와 그 폴더만 지운다. 폴더가 비어 있지 않으면 remove는 아무것도 하지 않는다.
		std::error_code Ec;
		std::filesystem::remove(TempFile, Ec);
		std::filesystem::remove(TempDir, Ec);

		UAssetManager::Get().RegisterAsset(Key, System);
		Expect(System->GetPath() == Key, "registered asset has its path");
		Expect(UAssetManager::GetAssetType(Key) == EAssetType::ParticleSystem, "GetAssetType detects a loaded ParticleSystem");
		Expect(UAssetManager::LoadParticleSystem(Key) == System, "LoadParticleSystem returns the registered asset (same pointer)");

		// 컴포넌트가 저장된 에셋을 Template으로 받는다 (씬 저장은 UObject* 참조를 에셋 경로로 쓰므로 경로가 있어야 한다)
		UParticleSystemComponent* Comp = NewObject<UParticleSystemComponent>();
		Comp->SetTemplate(System);
		Expect(Comp->GetTemplate() == System, "component template set");
		delete Comp;

		UAssetManager::UnregisterAsset(Key);
		Expect(UAssetManager::FindLoadedAsset<UParticleSystem>(Key) == nullptr, "UnregisterAsset removes the registration");
		DestroyParticleSystem(System);
	}


	// 4) 복제(PIE가 월드를 복제하는 경로): 복제본은 같은 Template을 참조하고, 시뮬레이션 인스턴스는 복사하지 않는다
	{
		UParticleSystem* System = MakeSerializationSampleSystem();
		UParticleSystemComponent* Source = NewObject<UParticleSystemComponent>();
		Source->RandomSeed = 9;
		Source->SetTemplate(System);
		for (int32 i = 0; i < 20; ++i) { Source->TickComponent(1.0f / 64.0f); }
		Expect(Source->GetNumEmitterInstances() == 2, "duplicate: the source has live instances");

		UParticleSystemComponent* Copy = Cast<UParticleSystemComponent>(StaticDuplicateObject(Source, nullptr));
		Expect(Copy != nullptr, "duplicate: StaticDuplicateObject returns a component");
		if (Copy)
		{
			Expect(Copy->GetTemplate() == System, "duplicate: the asset stays a shared reference (same pointer, not deep-copied)");
			Expect(Copy->RandomSeed == 9, "duplicate: RandomSeed copied");
			Expect(Copy->GetNumEmitterInstances() == 0, "duplicate: instances are not copied");
			Copy->TickComponent(1.0f / 64.0f);
			Expect(Copy->GetNumEmitterInstances() == 2, "duplicate: the copy builds its own instances on its first tick");
			Expect(Copy->GetEmitterInstance(0) != Source->GetEmitterInstance(0), "duplicate: the copy owns different instance objects");
			delete Copy;
		}
		delete Source;
		DestroyParticleSystem(System);
	}

	HTR_LOG(Info, "[Particle] asset self-test: {}", Failed == 0 ? "all passed" : "FAILED");
	return Failed;
}

int32 RunParticleSpriteRenderSelfTest()
{
	int32 Failed = 0;
	auto Check = [&](bool bCondition, const char* Name)
		{
			if (!bCondition)
			{
				++Failed;
				HTR_LOG(Error, "[Particle] sprite FAIL {}", Name);
			}
		};
	auto Near = [](double A, double B, double Tol = 1e-4) { return std::fabs(A - B) <= Tol; };
	auto NearVec = [&](const FVector& V, double X, double Y, double Z) { return Near(V.X, X) && Near(V.Y, Y) && Near(V.Z, Z); };

	// 카메라 축: X가 앞이면 Y가 오른쪽, Z가 위쪽 (FViewContext::BuildBillboardMatrix와 같은 규약)
	{
		const FParticleSpriteCamera Cam = FParticleSpriteCamera::FromView(FVector(0.0f, 0.0f, 0.0f), FVector(1.0f, 0.0f, 0.0f));
		Check(NearVec(Cam.Right, 0.0, 1.0, 0.0) && NearVec(Cam.Up, 0.0, 0.0, 1.0), "camera axes for forward +X");
		const FParticleSpriteCamera Down = FParticleSpriteCamera::FromView(FVector(0.0f, 0.0f, 5.0f), FVector(0.0f, 0.0f, -1.0f));
		Check(NearVec(Down.Right, 0.0, 1.0, 0.0), "camera looking straight down falls back to the Y axis as right");
		const FParticleSpriteCamera Zero = FParticleSpriteCamera::FromView(FVector(1.0f, 2.0f, 3.0f), FVector(0.0f, 0.0f, 0.0f));
		Check(NearVec(Zero.Right, 0.0, 1.0, 0.0) && NearVec(Zero.Position, 1.0, 2.0, 3.0), "zero forward keeps default axes");
	}

	// 파티클 세 개를 직접 써 넣은 인스턴스
	FLayoutFixture F;
	UParticleModuleRequired* Req = static_cast<UParticleModuleRequired*>(F.Add(UParticleModuleRequired::StaticClass()));
	F.Emitter->CacheEmitterModuleInfo();
	FParticleEmitterInstance Inst;
	Inst.Init(F.Emitter);
	Inst.Resize(8);
	auto SetParticle = [&](int32 Position, const FVector& Location, float Size, float Rotation, const FLinearColor& Color)
		{
			FBaseParticle& P = *reinterpret_cast<FBaseParticle*>(Inst.ParticleData + static_cast<size_t>(Inst.ParticleStride) * Inst.ParticleIndices[Position]);
			std::memset(&P, 0, sizeof(P));
			P.Location = Location;
			P.Size = FVector(Size, Size, Size);
			P.Rotation = Rotation;
			P.Color = Color;
		};
	FLinearColor Red; Red.R = 1.0f; Red.G = 0.0f; Red.B = 0.0f; Red.A = 0.5f;
	SetParticle(0, FVector(5.0f, 0.0f, 0.0f), 2.0f, 0.0f, Red);
	SetParticle(1, FVector(20.0f, 0.0f, 0.0f), 2.0f, 0.0f, Red);
	SetParticle(2, FVector(10.0f, 0.0f, 0.0f), 2.0f, 1.5707963f, Red);
	Inst.ActiveParticles = 3;

	const FParticleSpriteCamera Cam = FParticleSpriteCamera::FromView(FVector(0.0f, 0.0f, 0.0f), FVector(1.0f, 0.0f, 0.0f));
	TArray<FParticleSortItem> Scratch;
	TArray<FParticleSpriteVertex> Out;
	Out.SetNum(16 * 4);

	// 정렬 없음: ParticleIndices 순서. 첫 파티클의 모서리: 중심 (5,0,0), 크기 2 → (5, ±1, ±1)
	{
		const int32 Count = FillSpriteVertices(Inst, Cam, false, Out.GetData(), 16, Scratch);
		Check(Count == 3, "unsorted: three particles");
		Check(NearVec(Out[0].Position, 5.0, -1.0, -1.0) && NearVec(Out[2].Position, 5.0, 1.0, 1.0), "unsorted: corners of the first particle");
		Check(Near(Out[0].UV.X, 0.0) && Near(Out[0].UV.Y, 1.0) && Near(Out[2].UV.X, 1.0) && Near(Out[2].UV.Y, 0.0), "UV: (0,0) is top-left");
		Check(Near(Out[0].Color.R, 1.0) && Near(Out[0].Color.A, 0.5), "color copied");
		Check(Near(Out[4].Position.X, 20.0) && Near(Out[8].Position.X, 10.0), "unsorted: order follows ParticleIndices");
		// 회전: 90도면 (-0.5,-0.5) 모서리가 (0.5,-0.5)로 간다 → (10, +1, -1)
		Check(NearVec(Out[8].Position, 10.0, 1.0, -1.0), "rotation by 90 degrees about the view axis");
	}
	// 알파용 정렬: 먼 것부터 (20, 10, 5)
	{
		const int32 Count = FillSpriteVertices(Inst, Cam, true, Out.GetData(), 16, Scratch);
		Check(Count == 3, "sorted: three particles");
		Check(Near(Out[0].Position.X, 20.0) && Near(Out[4].Position.X, 10.0) && Near(Out[8].Position.X, 5.0), "sorted: back to front");
	}
	// 같은 거리는 위치 순서가 유지된다 (프레임마다 깜빡이지 않게)
	{
		SetParticle(0, FVector(7.0f, 0.0f, 0.0f), 2.0f, 0.0f, Red);
		SetParticle(1, FVector(7.0f, 0.0f, 0.0f), 4.0f, 0.0f, Red);
		FillSpriteVertices(Inst, Cam, true, Out.GetData(), 16, Scratch);
		Check(Near(Out[4].Position.Y, -1.0) && Near(Out[8].Position.Y, -2.0), "ties keep their original order (size 2 first, then size 4)");
	}
	// 용량 제한
	{
		const int32 Count = FillSpriteVertices(Inst, Cam, false, Out.GetData(), 2, Scratch);
		Check(Count == 2, "capacity clamps the number of particles written");
		Check(FillSpriteVertices(Inst, Cam, false, nullptr, 2, Scratch) == 0 && FillSpriteVertices(Inst, Cam, false, Out.GetData(), 0, Scratch) == 0, "null buffer or zero capacity writes nothing");
	}
	// 로컬 공간: 월드 위치는 SimulationToWorld를 거친다
	{
		Req->bUseLocalSpace = true;
		Inst.SetComponentToWorld(FTransform(FRotator(0.0f, 0.0f, 0.0f), FVector(100.0f, 0.0f, 0.0f), FVector(1.0f, 1.0f, 1.0f)).GetLocalMatrix());
		SetParticle(0, FVector(5.0f, 0.0f, 0.0f), 2.0f, 0.0f, Red);
		Inst.ActiveParticles = 1;
		const FParticleSpriteCamera Far = FParticleSpriteCamera::FromView(FVector(0.0f, 0.0f, 0.0f), FVector(1.0f, 0.0f, 0.0f));
		FillSpriteVertices(Inst, Far, false, Out.GetData(), 16, Scratch);
		Check(NearVec(Out[0].Position, 105.0, -1.0, -1.0), "local space: positions go through SimulationToWorld");
		Req->bUseLocalSpace = false;
		Inst.SetComponentToWorld(FMatrix::Identity);
	}
	// 경계: 크기만큼 부풀린다
	{
		SetParticle(0, FVector(5.0f, 0.0f, 0.0f), 2.0f, 0.0f, Red);
		SetParticle(1, FVector(20.0f, 3.0f, 0.0f), 2.0f, 0.0f, Red);
		Inst.ActiveParticles = 2;
		FBox Box{ FVector(0.0f, 0.0f, 0.0f), FVector(0.0f, 0.0f, 0.0f) };
		bool bHasAny = false;
		Check(ExpandSpriteWorldBounds(Inst, Box, bHasAny) && bHasAny, "bounds: particles found");
		Check(NearVec(Box.Min, 3.0, -2.0, -2.0) && NearVec(Box.Max, 22.0, 5.0, 2.0), "bounds: union of the particle boxes inflated by size");
		Inst.ActiveParticles = 0;
		FBox Empty{ FVector(0.0f, 0.0f, 0.0f), FVector(0.0f, 0.0f, 0.0f) };
		bool bAny = false;
		Check(!ExpandSpriteWorldBounds(Inst, Empty, bAny) && !bAny, "bounds: no particles, no box");
	}
	// 인덱스 패턴
	{
		uint32 Indices[12] = {};
		FillSpriteIndices(Indices, 2);
		const uint32 Expected[12] = { 0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7 };
		bool bSame = true;
		for (int32 i = 0; i < 12; ++i) { bSame &= (Indices[i] == Expected[i]); }
		Check(bSame, "index pattern (0,1,2,0,2,3) + 4k");
	}

	// 컴포넌트: 경계가 파티클을 덮고, 렌더 패킷이 이미터마다 하나 나오며, 재질이 블렌드를 따른다
	{
		UParticleSystem* System = NewObject<UParticleSystem>();
		System->SetPath("SpriteSelfTest.particle");
		UParticleEmitter* Emitter = System->AddEmitter();
		UParticleLODLevel* LOD = Emitter->AddLODLevel();
		UParticleModuleRequired* Required = static_cast<UParticleModuleRequired*>(LOD->AddModule(UParticleModuleRequired::StaticClass()));
		Required->SetMaterial(UAssetManager::GetAssetByPath<UMaterial>(ResourcePaths::ParticleSpriteAdditiveMaterial));
		UParticleModuleSpawn* Spawn = static_cast<UParticleModuleSpawn*>(LOD->AddModule(UParticleModuleSpawn::StaticClass()));
		Spawn->Rate = FFloatDistribution::MakeConstant(64.0f);
		UParticleModuleLifetime* Life = static_cast<UParticleModuleLifetime*>(LOD->AddModule(UParticleModuleLifetime::StaticClass()));
		Life->Lifetime = FFloatDistribution::MakeConstant(1.0f);
		UParticleModuleLocation* Loc = static_cast<UParticleModuleLocation*>(LOD->AddModule(UParticleModuleLocation::StaticClass()));
		Loc->StartLocation = FVectorDistribution::MakeConstant(FVector(3.0f, 0.0f, 0.0f));
		UParticleModuleSize* Size = static_cast<UParticleModuleSize*>(LOD->AddModule(UParticleModuleSize::StaticClass()));
		Size->StartSize = FVectorDistribution::MakeConstant(FVector(1.0f, 1.0f, 1.0f));
		UParticleModuleColor* Color = static_cast<UParticleModuleColor*>(LOD->AddModule(UParticleModuleColor::StaticClass()));
		Color->StartColor = FVectorDistribution::MakeConstant(FVector(1.0f, 1.0f, 1.0f));
		Emitter->CacheEmitterModuleInfo();

		UParticleSystemComponent* Comp = NewObject<UParticleSystemComponent>();
		Comp->SetTemplate(System);
		Comp->SetRelativeLocation(FVector(10.0f, 0.0f, 0.0f));
		Check(Near(Comp->CalcLocalBounds().Max.X, 0.0), "component: empty bounds before any particle exists");
		for (int32 i = 0; i < 10; ++i) { Comp->TickComponent(1.0f / 64.0f); }
		const FBox Local = Comp->CalcLocalBounds();
		// 파티클은 월드 (13,0,0)에 크기 1 → 컴포넌트(10,0,0) 기준 (3,0,0) 주변 [2, 4]
		Check(Local.Min.X <= 3.0f - 1.0f + 1e-3f && Local.Max.X >= 3.0f + 1.0f - 1e-3f, "component: local bounds cover the particles");

		FRenderQueue Queue;
		FViewContext View;
		View.CameraPosition = FVector(0.0f, 0.0f, 0.0f);
		View.CameraForward = FVector(1.0f, 0.0f, 0.0f);
		Comp->SubmitToRenderQueue(Queue, View);
		{
			// stat particle: 켜면 규모·변동·렌더 수치가 모이고, 끄면 모이지 않는다
			FString StatMessage;
			FStatOverlay::ExecCommand("stat particle on", StatMessage);
			Check(FStatOverlay::IsEnabled(EStatFlags::Particle), "stat: 'stat particle on' enables the group");
			FParticleStats::EndFrame();
			UParticleSystemComponent* StatComp = NewObject<UParticleSystemComponent>();
			StatComp->SetTemplate(System);
			for (int32 i = 0; i < 10; ++i) { StatComp->TickComponent(1.0f / 64.0f); }
			FRenderQueue StatQueue;
			StatComp->SubmitToRenderQueue(StatQueue, View);
			FViewContext StatHidden = View;
			StatHidden.bShowParticles = false;
			StatComp->SubmitToRenderQueue(StatQueue, StatHidden);
			const int32 StatActive = StatComp->GetActiveParticleCount();
			FParticleStats::EndFrame();
			const FParticleFrameStats& Stat = FParticleStats::Last();
			Check(Stat.Components == 10 && Stat.EmitterInstances == 10, "stat: components and emitters are counted per tick");
			Check(StatActive > 0 && Stat.Spawned == Stat.Killed + static_cast<uint32>(StatActive), "stat: spawned = killed + alive");
			Check(Stat.SubmitCalls == 1 && Stat.HiddenSubmits == 1, "stat: one drawn and one hidden submit");
			Check(Stat.SpritePackets == static_cast<uint32>(StatQueue.Num()), "stat: sprite packets match the queue");
			if (StatQueue.Num() > 0)
			{
				Check(Stat.SpriteParticles > 0 && Stat.SpriteParticles <= static_cast<uint64>(StatActive), "stat: sprite particles drawn");
				Check(Stat.SpriteVertices == Stat.SpriteParticles * 4, "stat: four vertices per sprite particle");
			}
			FStatOverlay::ExecCommand("stat particle off", StatMessage);
			StatComp->TickComponent(1.0f / 64.0f);
			Check(FParticleStats::Pending().Components == 0, "stat: nothing is collected while the group is off");
			delete StatComp;
		}
		{
			// Show 메뉴의 Particles를 끈 뷰: 패킷이 나오지 않고, 시뮬레이션은 계속 돈다
			FRenderQueue HiddenQueue;
			FViewContext HiddenView = View;
			HiddenView.bShowParticles = false;
			Comp->SubmitToRenderQueue(HiddenQueue, HiddenView);
			Check(HiddenQueue.Num() == 0, "show flag: no packet is submitted while Particles is off");
			Check(Comp->GetActiveParticleCount() > 0, "show flag: particles stay alive while Particles is off (only the submit is skipped)");
			if (Queue.Num() != 0)
			{
				FRenderQueue ShownAgain;
				Comp->SubmitToRenderQueue(ShownAgain, View);
				Check(ShownAgain.Num() == 1, "show flag: packets come back when Particles is turned on again");
			}
		}
		if (Queue.Num() == 0)
		{
			// 렌더 장치가 아직 없으면 동적 버퍼를 만들 수 없다. 장치가 있을 때만 이 검사를 한다.
			HTR_LOG(Info, "[Particle] sprite: no packet was submitted (render device not ready?) - packet checks skipped");
		}
		else
		{
			Check(Queue.Num() == 1, "submit: one packet per emitter");
			const FRenderPacket& Packet = Queue[0];
			Check(Packet.DynamicVertexBuffer && Packet.DynamicIndexBuffer && !Packet.Mesh && !Packet.SkeletalMesh, "submit: dynamic buffers, no mesh");
			Check(Packet.IndexCount == static_cast<uint32>(6 * Comp->GetActiveParticleCount()), "submit: six indices per active particle");
			Check(Packet.Material && Packet.Material->PSOType == EPSOType::ParticleSprite_Additive, "submit: the packet uses Required.Material (Additive)");
			Check(Packet.Material->Textures.Num() == 1 && Packet.Material->Textures[0] != nullptr, "submit: the default sprite texture (White1x1) loaded");
			Queue.Sort();
			Check(Queue.Num() == 1 && Queue.GetFirstTranslucentIndex() == 0, "submit: the packet survives sorting and counts as translucent");
		}
		delete Comp;
		DestroyParticleSystem(System);
	}

	// 메시 이미터: 섹션마다 인스턴싱 패킷이 나오고, 인스턴스 수가 활성 파티클 수와 같으며, 경계가 메시 크기를 덮고, 메시 설정이 저장/로드를 거쳐 유지된다
	{
		UStaticMesh* CubeMesh = UAssetManager::GetAssetByPath<UStaticMesh>("Sphere");
		if (!CubeMesh || !CubeMesh->GetVertexBuffer(0) || !CubeMesh->GetIndexBuffer(0))
		{
			HTR_LOG(Info, "[Particle] mesh: the Sphere mesh is not ready - mesh checks skipped");
		}
		else
		{
			UParticleSystem* System = NewObject<UParticleSystem>();
			System->SetPath("MeshSelfTest.particle");
			UParticleEmitter* Emitter = System->AddEmitter();
			UParticleLODLevel* LOD = Emitter->AddLODLevel();
			LOD->AddModule(UParticleModuleRequired::StaticClass());
			UParticleModuleTypeDataMesh* MeshData = static_cast<UParticleModuleTypeDataMesh*>(LOD->AddModule(UParticleModuleTypeDataMesh::StaticClass()));
			MeshData->Mesh = CubeMesh;
			UParticleModuleSpawn* Spawn = static_cast<UParticleModuleSpawn*>(LOD->AddModule(UParticleModuleSpawn::StaticClass()));
			Spawn->Rate = FFloatDistribution::MakeConstant(64.0f);
			UParticleModuleLifetime* Life = static_cast<UParticleModuleLifetime*>(LOD->AddModule(UParticleModuleLifetime::StaticClass()));
			Life->Lifetime = FFloatDistribution::MakeConstant(1.0f);
			UParticleModuleLocation* Loc = static_cast<UParticleModuleLocation*>(LOD->AddModule(UParticleModuleLocation::StaticClass()));
			Loc->StartLocation = FVectorDistribution::MakeConstant(FVector(3.0f, 0.0f, 0.0f));
			UParticleModuleSize* Size = static_cast<UParticleModuleSize*>(LOD->AddModule(UParticleModuleSize::StaticClass()));
			Size->StartSize = FVectorDistribution::MakeConstant(FVector(2.0f, 2.0f, 2.0f));
			Emitter->CacheEmitterModuleInfo();

			UParticleSystemComponent* Comp = NewObject<UParticleSystemComponent>();
			Comp->SetTemplate(System);
			for (int32 i = 0; i < 10; ++i) { Comp->TickComponent(1.0f / 64.0f); }
			Check(Comp->GetActiveParticleCount() > 0, "mesh: particles were spawned");

			// 경계: 파티클은 월드 (3,0,0)에 크기 2. 메시 반경 × 2를 덮어야 한다 (회전과 무관한 구)
			const FBox& MeshBox = CubeMesh->GetRenderBounds();
			const float MaxExtent = std::max(std::fabs(MeshBox.Min.X), std::fabs(MeshBox.Max.X));
			const FBox Local = Comp->CalcLocalBounds();
			Check(Local.Min.X <= 3.0f - MaxExtent * 2.0f + 1e-3f && Local.Max.X >= 3.0f + MaxExtent * 2.0f - 1e-3f, "mesh: bounds cover the scaled mesh");

			FRenderQueue Queue;
			FViewContext View;
			View.CameraPosition = FVector(0.0f, 0.0f, 0.0f);
			View.CameraForward = FVector(1.0f, 0.0f, 0.0f);
			Comp->SubmitToRenderQueue(Queue, View);
			const int32 SectionCount = CubeMesh->GetMeshData(0).Sections.Num();
			Check(Queue.Num() == SectionCount && SectionCount > 0, "mesh: one packet per mesh section");
			bool bAllInstanced = Queue.Num() > 0;
			for (int32 i = 0; i < Queue.Num(); ++i)
			{
				const FRenderPacket& Packet = Queue[i];
				bAllInstanced &= Packet.Mesh == CubeMesh && Packet.InstanceBuffer != nullptr && Packet.Material != nullptr
					&& Packet.InstanceCount == static_cast<uint32>(Comp->GetActiveParticleCount()) && !Packet.DynamicVertexBuffer;
			}
			Check(bAllInstanced, "mesh: packets are instanced draws of the mesh (InstanceCount = active particles)");
			Check(Queue.Num() == 0 || Queue[0].InstanceBuffer == Queue[Queue.Num() - 1].InstanceBuffer, "mesh: all sections share one instance buffer");

			// 메시가 없으면 Sprite로 떨어지지 않고 아무것도 내보내지 않는다
			MeshData->Mesh = nullptr;
			FRenderQueue EmptyQueue;
			Comp->SubmitToRenderQueue(EmptyQueue, View);
			Check(EmptyQueue.Num() == 0, "mesh: no mesh means nothing is drawn (no sprite fallback)");
			MeshData->Mesh = CubeMesh;

			// 머티리얼 덮어쓰기가 패킷의 재질로 나온다 (슬롯 0만 덮어쓴다)
			// 에셋 경로가 있는 머티리얼을 쓴다. 경로가 없는 머티리얼은 저장할 때 속성 복사본이 되어 로드하면 다른 객체가 된다.
			UMaterial* Replacement = UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
			MeshData->MaterialOverrides.Add(Replacement);
			FRenderQueue OverrideQueue;
			Comp->SubmitToRenderQueue(OverrideQueue, View);
			Check(OverrideQueue.Num() > 0 && OverrideQueue[0].Material == Replacement, "mesh: the material override reaches the packet");

			// 반투명 머티리얼: 인스턴싱 패킷이 반투명 구간으로 분류된다 (그리기 전에 먼 파티클부터 놓는 정렬도 이 경로에서 일어난다)
			UMaterial* TranslucentMaterial = UMaterial::CreateInstance(Replacement);
			if (TranslucentMaterial)
			{
				TranslucentMaterial->SetBlendState(EBlendState::AlphaBlend);
				MeshData->MaterialOverrides.Reset();
				MeshData->MaterialOverrides.Add(TranslucentMaterial);
				FRenderQueue TranslucentQueue;
				Comp->SubmitToRenderQueue(TranslucentQueue, View);
				const bool bInstancedTranslucent = TranslucentQueue.Num() > 0 && TranslucentQueue[0].Material == TranslucentMaterial
					&& TranslucentQueue[0].Material->PSOType == EPSOType::StaticMesh_Translucent
					&& TranslucentQueue[0].InstanceCount == static_cast<uint32>(Comp->GetActiveParticleCount());
				Check(bInstancedTranslucent, "mesh: a translucent material still produces an instanced packet");
				TranslucentQueue.Sort();
				Check(TranslucentQueue.Num() > 0 && TranslucentQueue.GetFirstTranslucentIndex() == 0, "mesh: the translucent packet lands in the translucent range");
				MeshData->MaterialOverrides.Reset();
				MeshData->MaterialOverrides.Add(Replacement);
			}

			// 저장/로드를 거쳐도 메시 설정이 유지된다
			MeshData->RollPitchYawRange = FVectorDistribution::MakeConstant(FVector(0.0f, 0.0f, 45.0f));
			const json Saved = ParticleAsset::ToJson(*System);
			FString LoadError;
			UParticleSystem* Loaded = ParticleAsset::FromJson(Saved, LoadError);
			Check(Loaded != nullptr, "mesh: the asset with a mesh emitter reloads");
			if (Loaded)
			{
				UParticleLODLevel* LoadedLOD = Loaded->GetEmitter(0) ? Loaded->GetEmitter(0)->GetLODLevel(0) : nullptr;
				UParticleModuleTypeDataMesh* LoadedMesh = LoadedLOD ? Cast<UParticleModuleTypeDataMesh>(LoadedLOD->TypeDataModule) : nullptr;
				Check(LoadedMesh != nullptr && LoadedMesh->Mesh == CubeMesh, "mesh: the Mesh setting survives save and load");
				Check(LoadedMesh != nullptr && LoadedMesh->MaterialOverrides.Num() == 1 && LoadedMesh->MaterialOverrides[0] == Replacement,
					"mesh: the material overrides survive save and load");
				Check(LoadedMesh != nullptr && Near(LoadedMesh->RollPitchYawRange.GetValue(0.0f, nullptr).Z, 45.0), "mesh: RollPitchYawRange survives save and load");
				Check(ParticleAsset::ToJson(*Loaded) == Saved, "mesh: re-saved JSON equals the original");
				DestroyParticleSystem(Loaded);
			}
			delete Comp;
			DestroyParticleSystem(System);
		}
	}

	// 메시 회전 Payload: TypeData가 Payload 자리를 잡고, 모듈이 생성 때 채우고, Tick이 적분한다
	{
		UParticleSystem* System = NewObject<UParticleSystem>();
		System->SetPath("MeshRotationSelfTest.particle");
		UParticleEmitter* Emitter = System->AddEmitter();
		UParticleLODLevel* LOD = Emitter->AddLODLevel();
		LOD->AddModule(UParticleModuleRequired::StaticClass());
		UParticleModuleTypeDataMesh* MeshData = static_cast<UParticleModuleTypeDataMesh*>(LOD->AddModule(UParticleModuleTypeDataMesh::StaticClass()));
		MeshData->RollPitchYawRange = FVectorDistribution::MakeConstant(FVector(0.0f, 0.0f, 30.0f));
		UParticleModuleSpawn* Spawn = static_cast<UParticleModuleSpawn*>(LOD->AddModule(UParticleModuleSpawn::StaticClass()));
		Spawn->Rate = FFloatDistribution::MakeConstant(64.0f);
		UParticleModuleLifetime* Life = static_cast<UParticleModuleLifetime*>(LOD->AddModule(UParticleModuleLifetime::StaticClass()));
		Life->Lifetime = FFloatDistribution::MakeConstant(5.0f);
		UParticleModuleMeshRotation* StartRot = static_cast<UParticleModuleMeshRotation*>(LOD->AddModule(UParticleModuleMeshRotation::StaticClass()));
		StartRot->StartRotation = FVectorDistribution::MakeConstant(FVector(0.0f, 0.0f, 10.0f));
		UParticleModuleMeshRotationRate* StartRate = static_cast<UParticleModuleMeshRotationRate*>(LOD->AddModule(UParticleModuleMeshRotationRate::StaticClass()));
		StartRate->StartRotationRate = FVectorDistribution::MakeConstant(FVector(0.0f, 0.0f, 90.0f));
		Emitter->CacheEmitterModuleInfo();

		Check(Emitter->TypeDataOffset == static_cast<int32>(sizeof(FBaseParticle)), "mesh rotation: the TypeData payload starts right after FBaseParticle");
		Check(Emitter->ParticleSize == sizeof(FBaseParticle) + 40u, "mesh rotation: payload is 36 bytes rounded to 40");
		Check(Emitter->ParticleStride == 176u, "mesh rotation: stride is 176");

		FParticleMeshEmitterInstance Inst;
		Check(Inst.Init(Emitter), "mesh rotation: the instance initializes");
		const float Step = 1.0f / 64.0f;
		Inst.Tick(Step);
		Inst.Tick(Step);
		Check(Inst.ActiveParticles >= 1, "mesh rotation: particles exist");
		if (Inst.ActiveParticles >= 1)
		{
			FBaseParticle* First = reinterpret_cast<FBaseParticle*>(Inst.ParticleData + static_cast<size_t>(Inst.ParticleIndices[0]) * Inst.ParticleStride);
			const FMeshRotationPayload* Payload = reinterpret_cast<const FMeshRotationPayload*>(reinterpret_cast<const uint8*>(First) + Emitter->TypeDataOffset);
			Check(Near(Payload->InitialOrientation.Z, 30.0), "mesh rotation: InitialOrientation comes from RollPitchYawRange");
			Check(Near(Payload->RotationRate.Z, 90.0), "mesh rotation: RotationRate comes from the module");
			// 시작 10도 + 두 Tick 동안 90도/초
			Check(Near(Payload->Rotation.Z, 10.0 + 2.0 * 90.0 / 64.0), "mesh rotation: Tick integrates Rotation by RotationRate * dt");
			Check(Near(Payload->Rotation.X, 0.0) && Near(Payload->Rotation.Y, 0.0), "mesh rotation: unused axes stay zero");
		}
		DestroyParticleSystem(System);

		// 메시 이미터가 아니면 회전 모듈은 아무것도 하지 않는다: Sprite 이미터에 붙여도 레이아웃과 파티클이 그대로다
		UParticleSystem* SpriteSystem = NewObject<UParticleSystem>();
		SpriteSystem->SetPath("MeshRotationSpriteSelfTest.particle");
		UParticleEmitter* SpriteEmitter = SpriteSystem->AddEmitter();
		UParticleLODLevel* SpriteLOD = SpriteEmitter->AddLODLevel();
		SpriteLOD->AddModule(UParticleModuleRequired::StaticClass());
		UParticleModuleSpawn* SpriteSpawn = static_cast<UParticleModuleSpawn*>(SpriteLOD->AddModule(UParticleModuleSpawn::StaticClass()));
		SpriteSpawn->Rate = FFloatDistribution::MakeConstant(64.0f);
		UParticleModuleLifetime* SpriteLife = static_cast<UParticleModuleLifetime*>(SpriteLOD->AddModule(UParticleModuleLifetime::StaticClass()));
		SpriteLife->Lifetime = FFloatDistribution::MakeConstant(5.0f);
		SpriteLOD->AddModule(UParticleModuleMeshRotation::StaticClass());
		SpriteLOD->AddModule(UParticleModuleMeshRotationRate::StaticClass());
		SpriteEmitter->CacheEmitterModuleInfo();
		Check(SpriteEmitter->TypeDataOffset == INDEX_NONE && SpriteEmitter->ParticleSize == sizeof(FBaseParticle), "mesh rotation: a sprite emitter reserves no payload for the modules");
		FParticleEmitterInstance SpriteInst;
		Check(SpriteInst.Init(SpriteEmitter), "mesh rotation: the sprite instance initializes");
		SpriteInst.Tick(Step);
		Check(SpriteInst.ActiveParticles >= 1 && SpriteInst.ValidateParticleIndices(), "mesh rotation: the modules are harmless on a sprite emitter");
		DestroyParticleSystem(SpriteSystem);
	}

	HTR_LOG(Info, "[Particle] sprite self-test: {}", Failed == 0 ? "all passed" : "FAILED");
	return Failed;
}
