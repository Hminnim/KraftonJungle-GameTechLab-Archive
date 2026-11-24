#include "EnginePCH.h"
#include "Particles/ParticleEmitterInstances.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/ParticleModule.h"
#include "Particles/ParticleHelper.h"
#include "Particles/ParticleSpriteRender.h"
#include "Math/Transform.h"
#include "Core/HAL/UnrealMemory.h"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <vector>

bool FParticleEmitterInstance::Init(UParticleEmitter* InTemplate, uint32 Seed)
{
	Release();
	// 다시 Init해도 같은 Seed면 같은 결과가 나오도록 스트림을 처음으로 되돌린다
	RandomStream.Initialize(Seed);

	if (!InTemplate || InTemplate->ParticleStride == 0)
	{
		HTR_LOG(Warning, "[Particle] Init failed: the template is null or CacheEmitterModuleInfo() was not called");
		return false;
	}
	// 슬롯 시작 주소가 모두 정렬되려면 간격도 정렬 단위의 배수여야 한다
	if (InTemplate->ParticleStride % MemoryAlignment != 0)
	{
		HTR_LOG(Warning, "[Particle] Init failed: ParticleStride {} is not a multiple of {}", InTemplate->ParticleStride, MemoryAlignment);
		return false;
	}

	if (InTemplate->ReqInstanceBytes > 0)
	{
		InstanceData = static_cast<uint8*>(FMemory::Malloc(InTemplate->ReqInstanceBytes, MemoryAlignment));
		if (!InstanceData)
		{
			return false;
		}
		// 모듈은 처음 읽는 값이 0이라고 가정할 수 있어야 한다
		std::memset(InstanceData, 0, InTemplate->ReqInstanceBytes);
	}

	Template = InTemplate;
	ParticleSize = InTemplate->ParticleSize;
	ParticleStride = InTemplate->ParticleStride;
	InstancePayloadSize = InTemplate->ReqInstanceBytes;

	// 루프 길이와 지연은 Template과 RandomStream이 준비된 뒤에 정한다
	SetupEmitterDuration();
	UpdateTransforms();
	return true;
}

bool FParticleEmitterInstance::Resize(int32 NewMaxActiveParticles)
{
	if (!Template || NewMaxActiveParticles < 0 || NewMaxActiveParticles > MaxParticleLimit)
	{
		return false;
	}
	if (NewMaxActiveParticles <= MaxActiveParticles)
	{
		return true;
	}

	// 두 블록을 모두 늘릴 수 있을 때만 개수를 바꾼다. 실패해도 원래 블록은 유효하다.
	// 데이터가 먼저 늘어난 뒤 인덱스가 실패하면 데이터만 커진 상태로 남지만, 칸 수(MaxActiveParticles)는 그대로라서 안전하다.
	void* NewData = FMemory::Realloc(ParticleData, static_cast<uint64>(ParticleStride) * NewMaxActiveParticles, MemoryAlignment);
	if (!NewData)
	{
		return false;
	}
	ParticleData = static_cast<uint8*>(NewData);

	void* NewIndices = FMemory::Realloc(ParticleIndices, sizeof(uint16) * (static_cast<uint64>(NewMaxActiveParticles) + 1), MemoryAlignment);
	if (!NewIndices)
	{
		return false;
	}
	ParticleIndices = static_cast<uint16*>(NewIndices);

	// 새로 생긴 칸은 자기 번호를 가리키게 한다 (빈 슬롯 목록). 기존 칸의 순서는 유지된다.
	// 파티클 데이터는 0으로 채우지 않는다. 슬롯은 생성될 때 전체가 초기화된다.
	for (int32 i = MaxActiveParticles; i < NewMaxActiveParticles; ++i)
	{
		ParticleIndices[i] = static_cast<uint16>(i);
	}
	ParticleIndices[NewMaxActiveParticles] = UnusedIndex;

	MaxActiveParticles = NewMaxActiveParticles;
	return true;
}

void FParticleEmitterInstance::Release()
{
	FMemory::Free(ParticleData);
	FMemory::Free(ParticleIndices);
	FMemory::Free(InstanceData);
	ParticleData = nullptr;
	ParticleIndices = nullptr;
	InstanceData = nullptr;
	InstancePayloadSize = 0;
	ParticleSize = 0;
	ParticleStride = 0;
	ActiveParticles = 0;
	MaxActiveParticles = 0;
	ParticleCounter = 0;
	SpawnFraction = 0.0f;
	SecondsSinceCreation = 0.0f;
	EmitterTime = 0.0f;
	EmitterDuration = 0.0f;
	CurrentDelay = 0.0f;
	LoopCount = 0;
	BurstFired.clear();
	bHaltSpawning = false;
	LastDeltaTime = 0.0f;
	CurrentLODLevelIndex = 0;
	Template = nullptr;
}

void FParticleEmitterInstance::SetCurrentLODIndex(int32 NewLODIndex)
{
	if (!Template || Template->LODLevels.IsEmpty())
	{
		CurrentLODLevelIndex = 0;
		return;
	}
	const int32 ClampedIndex = std::clamp(NewLODIndex, 0, Template->GetNumLODLevels() - 1);
	if (CurrentLODLevelIndex == ClampedIndex)
	{
		return;
	}
	CurrentLODLevelIndex = ClampedIndex;
	SetupEmitterDuration();
	UpdateTransforms();
}

UParticleLODLevel* FParticleEmitterInstance::GetCurrentLODLevel() const
{
	if (!Template || Template->LODLevels.IsEmpty())
	{
		return nullptr;
	}
	if (UParticleLODLevel* LOD = Template->GetLODLevel(CurrentLODLevelIndex))
	{
		return LOD;
	}
	return Template->GetLODLevel(0);
}

// ---------------------------------------------------------------------------
// 한 프레임의 흐름
// ---------------------------------------------------------------------------

void FParticleEmitterInstance::Tick(float DeltaTime)
{
	if (!Template || !GetCurrentLODLevel())
	{
		return;
	}

	// 첫 Tick인지는 시간을 진행하기 전에 정한다 (UE와 같다)
	const bool bFirstTime = !(SecondsSinceCreation > 0.0f);

	// 이미터 시간과 루프 처리. EmitterTime에서 지연을 뺀 상태로 돌려준다.
	const float EmitterDelay = Tick_EmitterTimeSetup(DeltaTime);

	// 수명이 끝난 파티클을 제거한다
	KillParticles();
	// Base 값으로 복원하고 수명을 진행한다
	ResetParticleParameters(DeltaTime);
	// Update 모듈 (기존 파티클만 대상이다)
	Tick_ModuleUpdate(DeltaTime);
	// 새 파티클 생성
	SpawnFraction = Tick_SpawnParticles(DeltaTime, bFirstTime);
	// 위치와 회전 적분 (방금 생성된 파티클은 건너뜀 : 생성할 때 이동했기에)
	UpdateParticlePositions(DeltaTime);

	// 뺐던 지연을 되돌린다. 다음 Tick에서 다시 빼므로 지연은 이미터 시간에 누적되지 않는다.
	EmitterTime += EmitterDelay;
	LastDeltaTime = DeltaTime;
}

void FParticleEmitterInstance::SetupEmitterDuration()
{
	const UParticleLODLevel* LODLevel = GetCurrentLODLevel();
	const UParticleModuleRequired* Required = LODLevel ? LODLevel->RequiredModule : nullptr;
	if (!Required)
	{
		CurrentDelay = 0.0f;
		EmitterDuration = 0.0f;
		return;
	}

	// 난수는 지연, 길이 순서로 뽑는다 (UE와 같은 순서). 범위를 쓰지 않는 값은 난수를 쓰지 않는다.
	CurrentDelay = Required->EmitterDelay;
	if (Required->bEmitterDelayUseRange)
	{
		CurrentDelay = RandomStream.GetFraction(Required->EmitterDelayLow, Required->EmitterDelay);
	}

	float Duration = Required->EmitterDuration;
	if (Required->bEmitterDurationUseRange)
	{
		Duration = RandomStream.GetFraction(Required->EmitterDurationLow, Required->EmitterDuration);
	}
	// 길이는 지연을 포함한다. 한 루프 = 지연 + Duration이다.
	EmitterDuration = Duration + CurrentDelay;
}

void FParticleEmitterInstance::SetComponentToWorld(const FMatrix& InComponentToWorld)
{
	ComponentToWorld = InComponentToWorld;
	UpdateTransforms(); 
}

void FParticleEmitterInstance::UpdateTransforms()
{
	const UParticleLODLevel* LODLevel = GetCurrentLODLevel();
	const UParticleModuleRequired* Required = LODLevel ? LODLevel->RequiredModule : nullptr;

	// 이미터 로컬 → 컴포넌트 (UE: EmitterToComponent = RotationTranslation(EmitterRotation, EmitterOrigin))
	FMatrix EmitterToComponent;
	EmitterToComponent.SetIdentity();
	bool bLocalSpace = false;
	if (Required)
	{
		EmitterToComponent = FTransform(Required->EmitterRotation, Required->EmitterOrigin, FVector(1.0f, 1.0f, 1.0f)).GetLocalMatrix();
		bLocalSpace = Required->bUseLocalSpace;
	}

	if (bLocalSpace)
	{
		EmitterToSimulation = EmitterToComponent;
		SimulationToWorld = ComponentToWorld;
	}
	else
	{
		// 행 벡터 규약이라 왼쪽 행렬이 먼저 적용된다 (이미터 로컬 → 컴포넌트 → 월드)
		EmitterToSimulation = EmitterToComponent * ComponentToWorld;
		SimulationToWorld.SetIdentity();
	}
}

float FParticleEmitterInstance::Tick_EmitterTimeSetup(float DeltaTime)
{
	// 컴포넌트가 움직였을 수 있으므로 변환을 먼저 갱신한다
	UpdateTransforms();
	SecondsSinceCreation += DeltaTime;

	// 시간을 진행하고 루프 경계를 넘었는지 본다. Duration이 0 이하면 루프하지 않는다.
	EmitterTime += DeltaTime;
	const bool bLooped = (EmitterDuration > 0.0f) && (EmitterTime >= EmitterDuration);
	if (bLooped)
	{
		++LoopCount;
		ResetBurstList();
		// 넘친 시간은 다음 루프로 이어 간다 (경계에서 시간을 잃지 않는다)
		EmitterTime -= EmitterDuration;
	}

	// 지연 동안 EmitterTime이 음수가 되어 Spawn 모듈이 시간 0 이전을 보지 않게 한다. Tick이 끝에서 다시 더한다.
	EmitterTime -= CurrentDelay;
	return CurrentDelay;
}

bool FParticleEmitterInstance::CanSpawnNow(bool bFirstTime) const
{
	if (bHaltSpawning || !(EmitterTime >= 0.0f))
	{
		return false;
	}
	const UParticleLODLevel* LODLevel = GetCurrentLODLevel();
	if (!LODLevel || !LODLevel->bEnabled)
	{
		return false;
	}
	const int32 Loops = LODLevel->RequiredModule ? LODLevel->RequiredModule->EmitterLoops : 0;
	// Loops == 0은 무한 반복. 첫 Tick은 루프 횟수와 관계없이 생성을 허용한다.
	return Loops == 0 || LoopCount < Loops || SecondsSinceCreation < EmitterDuration * static_cast<float>(Loops) || bFirstTime;
}

bool FParticleEmitterInstance::HasCompleted() const
{
	const UParticleLODLevel* LODLevel = GetCurrentLODLevel();
	if (!LODLevel)
	{
		return true;
	}
	// 생성을 멈춘 이미터나 꺼진 LOD 레벨은 남은 파티클만 없어지면 끝난다
	if (bHaltSpawning || !LODLevel->bEnabled)
	{
		return ActiveParticles == 0;
	}
	const int32 Loops = LODLevel->RequiredModule ? LODLevel->RequiredModule->EmitterLoops : 0;
	if (Loops == 0 || SecondsSinceCreation < EmitterDuration * static_cast<float>(Loops))
	{
		return false;
	}
	return ActiveParticles == 0;
}

void FParticleEmitterInstance::KillParticle(int32 KillIndex)
{
	if (KillIndex < 0 || KillIndex >= ActiveParticles)
	{
		return;
	}
	const uint16 SlotIndex = ParticleIndices[KillIndex];
	ParticleIndices[KillIndex] = ParticleIndices[ActiveParticles - 1];
	ParticleIndices[ActiveParticles - 1] = SlotIndex;
	--ActiveParticles;
}

bool FParticleEmitterInstance::ValidateParticleIndices() const
{
	if (MaxActiveParticles == 0 && ParticleIndices == nullptr)
	{
		return ActiveParticles == 0;
	}
	if (ParticleIndices == nullptr || MaxActiveParticles < 0 || ActiveParticles < 0 || ActiveParticles > MaxActiveParticles)
	{
		HTR_LOG(Error, "[Particle] ValidateParticleIndices: invalid counts (active {}, max {})", ActiveParticles, MaxActiveParticles);
		return false;
	}

	// 슬롯 번호가 한 번씩만 나오는지 확인한다
	std::vector<uint8> Seen(static_cast<size_t>(MaxActiveParticles), 0);
	for (int32 i = 0; i < MaxActiveParticles; ++i)
	{
		const uint16 Slot = ParticleIndices[i];
		if (Slot >= MaxActiveParticles)
		{
			HTR_LOG(Error, "[Particle] ValidateParticleIndices: slot {} at position {} is out of range (max {})", Slot, i, MaxActiveParticles);
			return false;
		}
		if (Seen[Slot])
		{
			HTR_LOG(Error, "[Particle] ValidateParticleIndices: slot {} appears twice (second at position {})", Slot, i);
			return false;
		}
		Seen[Slot] = 1;
	}
	if (ParticleIndices[MaxActiveParticles] != UnusedIndex)
	{
		HTR_LOG(Error, "[Particle] ValidateParticleIndices: the last entry is {} instead of the unused marker", ParticleIndices[MaxActiveParticles]);
		return false;
	}
	return true;
}

void FParticleEmitterInstance::KillParticles()
{
	// 뒤에서 앞으로 돈다. 교환으로 현재 위치에 들어오는 파티클은 마지막 칸에서 온 것이라 이미 검사한 파티클이다.
	for (int32 i = ActiveParticles - 1; i >= 0; --i)
	{
		const int32 CurrentIndex = ParticleIndices[i];
		if (CurrentIndex >= MaxActiveParticles)
		{
			HTR_LOG(Error, "[Particle] KillParticles: corrupted particle index {} at position {}", CurrentIndex, i);
			return;
		}
		const FBaseParticle& Particle = *reinterpret_cast<const FBaseParticle*>(ParticleData + static_cast<size_t>(CurrentIndex) * ParticleStride);
		if (Particle.RelativeTime > 1.0f)
		{
			KillParticle(i);
		}
	}
}

void FParticleEmitterInstance::ResetParticleParameters(float DeltaTime)
{
	// 정순으로 모든 파티클을 처리한다. Freeze 상태여도 복원과 수명 진행은 한다 (그래서 BEGIN_UPDATE_LOOP를 쓰지 않는다).
	// UE는 JustSpawned 파티클의 RelativeTime 진행을 건너뛰지만(ParticleEmitterInstances.cpp:1706-1711),
	// 여기서는 JustSpawned가 같은 Tick의 마지막 단계(UpdateParticlePositions)에서 지워지므로 이 단계에서는 항상 꺼져 있어 검사하지 않는다.
	for (int32 i = 0; i < ActiveParticles; ++i)
	{
		DECLARE_PARTICLE(Particle, ParticleData + static_cast<size_t>(ParticleIndices[i]) * ParticleStride);
		Particle.Velocity = Particle.BaseVelocity;
		Particle.Size = Particle.BaseSize;
		Particle.RotationRate = Particle.BaseRotationRate;
		Particle.Color = Particle.BaseColor;
		Particle.RelativeTime += Particle.OneOverMaxLifetime * DeltaTime;
	}
}

void FParticleEmitterInstance::Tick_ModuleUpdate(float DeltaTime)
{
	UParticleLODLevel* LODLevel = GetCurrentLODLevel();
	for (UParticleModule* Module : LODLevel->UpdateModules)
	{
		if (Module && Module->bEnabled && Module->bUpdateModule)
		{
			// Payload가 없는 모듈의 Offset은 INDEX_NONE이고 그런 모듈은 PARTICLE_ELEMENT를 쓰지 않는다
			const FParticleUpdateContext Context{ *this, Template->GetModuleDataOffset(Module), DeltaTime };
			Module->Update(Context);
		}
	}
}

float FParticleEmitterInstance::Tick_SpawnParticles(float DeltaTime, bool bFirstTime)
{
	// 지연 중이거나 루프를 모두 돈 이미터는 만들지 않고 쌓인 소수 부분을 그대로 둔다
	if (!CanSpawnNow(bFirstTime))
	{
		return SpawnFraction;
	}

	// 생성 속도는 Spawn 모듈이 정한다. 모듈이 없거나 꺼져 있으면 만들지 않는다.
	const UParticleLODLevel* LODLevel = GetCurrentLODLevel();
	const UParticleModuleSpawn* SpawnModule = LODLevel ? LODLevel->SpawnModule : nullptr;
	if (!SpawnModule || !SpawnModule->bEnabled)
	{
		return SpawnFraction;
	}
	const float RateScale = SpawnModule->RateScale.GetValue(EmitterTime, &RandomStream);
	float Rate = SpawnModule->Rate.GetValue(EmitterTime, &RandomStream) * RateScale;
	Rate = Rate > 0.0f ? Rate : 0.0f;
	return Spawn(DeltaTime, Rate, GetCurrentBurstCount());
}

void FParticleEmitterInstance::ResetBurstList()
{
	std::fill(BurstFired.begin(), BurstFired.end(), static_cast<uint8>(0));
}

int32 FParticleEmitterInstance::GetCurrentBurstCount()
{
	const UParticleLODLevel* LODLevel = GetCurrentLODLevel();
	const UParticleModuleSpawn* SpawnModule = LODLevel ? LODLevel->SpawnModule : nullptr;
	if (!SpawnModule || SpawnModule->BurstList.Num() == 0)
	{
		return 0;
	}
	// 모듈의 버스트 목록이 바뀌었으면 발사 기록을 새로 만든다
	if (static_cast<int32>(BurstFired.size()) != SpawnModule->BurstList.Num())
	{
		BurstFired.assign(static_cast<size_t>(SpawnModule->BurstList.Num()), 0);
	}

	int32 Total = 0;
	for (int32 i = 0; i < SpawnModule->BurstList.Num(); ++i)
	{
		const FParticleBurst& Burst = SpawnModule->BurstList[i];
		if (BurstFired[i] || !(EmitterTime >= Burst.Time))
		{
			continue;
		}
		BurstFired[i] = 1;
		int32 Count = Burst.Count;
		if (Burst.CountLow > -1)
		{
			Count = RandomStream.GetInt(Burst.CountLow, Burst.Count);
		}
		Total += Count > 0 ? Count : 0;
	}
	return Total;
}

FParticleSpawnPlan FParticleEmitterInstance::ComputeSpawnPlan(float OldFraction, float Rate, float DeltaTime)
{
	FParticleSpawnPlan Plan;
	Plan.NewFraction = OldFraction;
	if (!(Rate > 0.0f))
	{
		return Plan;
	}

	const float Increment = 1.0f / Rate;
	const float NewLeftover = OldFraction + DeltaTime * Rate;
	const int32 Number = static_cast<int32>(std::floor(NewLeftover));

	Plan.Count = Number;
	Plan.Increment = Increment;
	// 쌓여 있던 소수 부분(OldFraction)이 이미 파티클 하나의 일부를 채웠으므로 첫 파티클은 프레임 끝에서 그만큼 덜 지난 시점에 생성된 것으로 본다
	Plan.StartTime = DeltaTime + OldFraction * Increment - Increment;
	Plan.NewFraction = NewLeftover - static_cast<float>(Number);
	return Plan;
}

float FParticleEmitterInstance::Spawn(float DeltaTime, float Rate, int32 BurstCount)
{
	const FParticleSpawnPlan Plan = ComputeSpawnPlan(SpawnFraction, Rate, DeltaTime);
	int32 Number = Plan.Count;
	if (BurstCount < 0)
	{
		BurstCount = 0;
	}
	if (Number <= 0 && BurstCount <= 0)
	{
		return Plan.NewFraction;
	}

	// 상한을 넘는 분량은 만들지 않고 버린다. 버스트를 먼저 채우고 남는 칸을 Rate 분량에 쓴다.
	if (ActiveParticles + Number + BurstCount > MaxParticleLimit)
	{
		int32 MaxNew = MaxParticleLimit - ActiveParticles;
		BurstCount = (BurstCount < MaxNew) ? BurstCount : MaxNew;
		MaxNew -= BurstCount;
		Number = (Number < MaxNew) ? Number : MaxNew;
	}

	// 칸이 모자라면 늘린다. 늘리는 폭은 UE와 같은 공식(개수 + 4제곱근 + 1)이다.
	// UE와 다름: UE는 NewCount >= Max에서 늘리고(ParticleEmitterInstances.cpp:2073), 여기서는 필요한 칸 수를 넘을 때(>)만 늘린다.
	// 상한(MaxParticleLimit)에 도달한 이미터가 매 프레임 같은 크기의 Resize를 시도하지 않게 하려는 것이다.
	const int32 NewCount = ActiveParticles + Number + BurstCount;
	if (NewCount > MaxActiveParticles)
	{
		const int32 Growth = static_cast<int32>(std::sqrt(std::sqrt(static_cast<float>(NewCount)))) + 1;
		const int32 Target = (NewCount + Growth < MaxParticleLimit) ? (NewCount + Growth) : MaxParticleLimit;
		if (!Resize(Target))
		{
			return SpawnFraction;
		}
	}

	SpawnParticles(Number, Plan.StartTime, Plan.Increment);
	// 버스트는 프레임 안에서 시각을 나누지 않고 모두 SpawnTime 0으로 만든다
	SpawnParticles(BurstCount, 0.0f, 0.0f);
	return Plan.NewFraction;
}

void FParticleEmitterInstance::SpawnParticles(int32 Count, float StartTime, float Increment)
{
	UParticleLODLevel* LODLevel = GetCurrentLODLevel();
	if (!LODLevel || !ParticleData || !ParticleIndices)
	{
		return;
	}

	if (Count > MaxActiveParticles - ActiveParticles)
	{
		Count = MaxActiveParticles - ActiveParticles;
	}

	float SpawnTime = StartTime;
	for (int32 i = 0; i < Count; ++i)
	{
		// ParticleIndices[ActiveParticles]는 비어 있는 슬롯 번호다
		const uint16 NextFreeIndex = ParticleIndices[ActiveParticles];
		if (NextFreeIndex >= MaxActiveParticles)
		{
			HTR_LOG(Error, "[Particle] SpawnParticles: corrupted free slot index {} at position {}", NextFreeIndex, ActiveParticles);
			return;
		}

		FBaseParticle* Particle = reinterpret_cast<FBaseParticle*>(ParticleData + static_cast<size_t>(NextFreeIndex) * ParticleStride);
		const int32 CurrentParticleIndex = ActiveParticles++;

		PreSpawn(Particle);
		for (UParticleModule* Module : LODLevel->SpawnModules)
		{
			if (Module && Module->bEnabled && Module->bSpawnModule)
			{
				const FParticleSpawnContext Context{ *this, Template->GetModuleDataOffset(Module), SpawnTime, Particle };
				Module->Spawn(Context);
			}
		}
		PostSpawn(Particle, SpawnTime);

		// 생성 모듈이 RelativeTime을 1보다 크게 정하면 "만들지 않음"을 뜻한다
		if (Particle->RelativeTime > 1.0f)
		{
			KillParticle(CurrentParticleIndex);
			continue;
		}

		// UE와 같이 죽은 파티클은 SpawnTime을 소모하지 않는다 (위의 continue가 이 줄을 건너뜀)
		SpawnTime -= Increment;
	}
}

void FParticleEmitterInstance::PreSpawn(FBaseParticle* Particle)
{
	// 슬롯의 사용 영역 전체(FBaseParticle + Payload)를 0으로 채운다. 모듈은 Payload의 처음 값이 0이라고 가정할 수 있다.
	// 초기 위치는 시뮬레이션 공간에서의 이미터 원점이다 (UE: InitialLocation = EmitterToSimulation.GetOrigin())
	FMemory::Memzero(Particle, ParticleSize);
	Particle->Location = EmitterToSimulation.TransformPosition(FVector(0.0f, 0.0f, 0.0f));
}

void FParticleEmitterInstance::PostSpawn(FBaseParticle* Particle, float SpawnTime)
{
	// 생성 시점에서 프레임 끝까지 속도로 이동한 만큼을 미리 반영한다
	Particle->OldLocation = Particle->Location;
	Particle->Location += Particle->Velocity * SpawnTime;

	// 생성 순번은 Flags의 하위 비트에 기록한다 (Ribbon/Beam이 Kill로 섞인 순서를 복원할 때 쓴다)
	Particle->Flags |= (ParticleCounter++) & STATE_CounterMask;
	Particle->Flags |= STATE_Particle_JustSpawned;
}

void FParticleEmitterInstance::UpdateParticlePositions(float DeltaTime)
{
	for (int32 i = 0; i < ActiveParticles; ++i)
	{
		DECLARE_PARTICLE(Particle, ParticleData + static_cast<size_t>(ParticleIndices[i]) * ParticleStride);

		Particle.OldLocation = Particle.Location;

		const bool bJustSpawned = (Particle.Flags & STATE_Particle_JustSpawned) != 0;
		Particle.Flags &= ~STATE_Particle_JustSpawned;

		float NewRotation = Particle.Rotation;
		// 방금 생성된 파티클은 PostSpawn에서 이미 SpawnTime만큼 이동했으므로 이번 프레임 적분을 건너뛴다
		if ((Particle.Flags & STATE_Particle_Freeze) == 0 && !bJustSpawned)
		{
			if ((Particle.Flags & STATE_Particle_FreezeTranslation) == 0)
			{
				Particle.Location += Particle.Velocity * DeltaTime;
			}
			if ((Particle.Flags & STATE_Particle_FreezeRotation) == 0)
			{
				NewRotation = Particle.Rotation + Particle.RotationRate * DeltaTime;
			}
		}
		// 회전은 +/- 2π 안으로 감는다
		Particle.Rotation = std::fmod(NewRotation, 2.0f * PI);
	}
}

bool FParticleEmitterInstance::SimulatesInLocalSpace() const
{
	const UParticleLODLevel* LODLevel = GetCurrentLODLevel();
	return LODLevel && LODLevel->RequiredModule && LODLevel->RequiredModule->bUseLocalSpace;
}

bool FParticleEmitterInstance::ExpandWorldBounds(FBox& InOutBox, bool& bInOutHasAny) const
{
	return ExpandSpriteWorldBounds(*this, InOutBox, bInOutHasAny);
}
