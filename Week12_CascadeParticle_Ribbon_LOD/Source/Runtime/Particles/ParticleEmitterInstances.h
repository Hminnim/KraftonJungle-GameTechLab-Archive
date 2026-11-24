#pragma once

#include "Core/Types.h"
#include "Math/RandomStream.h"
#include "Math/Matrix.h"
#include <functional>
#include <vector>

class UParticleEmitter;
class UParticleLODLevel;
class UParticleSystemComponent;
struct FParticleCollisionHit;
struct FBaseParticle;
class FRenderQueue;
struct FViewContext;
struct FBox;

// 한 프레임에 만들 파티클 계획 (ComputeSpawnPlan의 결과)
struct FParticleSpawnPlan
{
	// 이번 프레임에 만들 개수
	int32 Count = 0;
	// 첫 파티클의 SpawnTime. 이후 파티클은 Increment씩 줄어든다.
	float StartTime = 0.0f;
	// 파티클 사이의 시간 간격 (1 / Rate)
	float Increment = 0.0f;
	// 다음 프레임으로 넘길 소수 부분
	float NewFraction = 0.0f;
};

// 이미터 에셋(UParticleEmitter)을 컴포넌트마다 실행하는 시뮬레이션 상태 (UE: ParticleEmitterInstances.h의 FParticleEmitterInstance)
// 에셋은 공유되므로 파티클 메모리와 개수 같은 변하는 값은 모두 여기에 둔다.
// Tick이 한 프레임의 흐름(Kill → Reset → Update → Spawn → 위치 적분)을 실행한다.
struct FParticleEmitterInstance
{
	// ParticleIndices가 uint16이라 슬롯 번호는 0..65534까지 쓸 수 있다. 0xFFFF는 "사용하지 않는 칸" 표시다.
	static constexpr int32 MaxParticleLimit = 65535;
	// ParticleData, InstanceData의 시작 주소 정렬. FBaseParticle의 정렬과 같다.
	static constexpr uint32 MemoryAlignment = 16;
	static constexpr uint16 UnusedIndex = 0xFFFF;

	// 파티클 설정을 읽어 오는 이미터 에셋. 읽기 전용으로만 쓴다.
	UParticleEmitter* Template = nullptr;

	// 파티클 데이터. 슬롯 하나는 ParticleStride 바이트이고, 슬롯은 한 번 정해지면 이동하지 않는다.
	uint8* ParticleData = nullptr;
	// [0, ActiveParticles) = 살아 있는 파티클의 슬롯 번호, [ActiveParticles, MaxActiveParticles) = 비어 있는 슬롯 번호
	// 칸이 MaxActiveParticles + 1개인 것은 UE와 같고, 마지막 칸은 UnusedIndex로 채운다.
	uint16* ParticleIndices = nullptr;
	// 모듈의 이미터 단위 상태. Template->ReqInstanceBytes 크기이고 0으로 채워진다.
	uint8* InstanceData = nullptr;
	uint32 InstancePayloadSize = 0;

	// Template에서 읽어 온 파티클 크기와 간격 (UParticleEmitter::ParticleSize, ParticleStride)
	// 슬롯 초기화는 Stride가 아니라 Size만큼 0으로 채운다 (뒤쪽 올림 영역은 쓰지 않는다).
	uint32 ParticleSize = 0;
	uint32 ParticleStride = 0;

	int32 ActiveParticles = 0;
	int32 MaxActiveParticles = 0;

	// 지금까지 만든 파티클 수. 각 파티클의 Flags 하위 비트(생성 순번)로 기록된다 (UE: ParticleCounter)
	uint32 ParticleCounter = 0;
	// 지난 프레임까지 만들지 못하고 쌓인 파티클 수의 소수 부분 (UE: SpawnFraction)
	float SpawnFraction = 0.0f;
	// 버스트 목록의 각 항목을 이번 루프에 이미 발사했는지 (UE: BurstFired). 루프가 돌면 모두 false로 돌아간다.
	// 크기는 Spawn 모듈의 BurstList에 맞춰 필요할 때 조정한다.
	std::vector<uint8> BurstFired;
	// 이미터가 생성된 뒤 흐른 시간 (UE: SecondsSinceCreation)
	float SecondsSinceCreation = 0.0f;
	// 현재 루프 안에서 흐른 시간 (UE: EmitterTime). Tick 도중에는 지연(Delay)을 뺀 값이라 지연 중에는 음수이고,
	// Spawn 모듈의 Distribution이 이 값을 Time으로 쓴다. Tick이 끝나면 뺐던 지연을 다시 더해 원래 값으로 돌려놓는다 (UE와 같다).
	float EmitterTime = 0.0f;
	// 한 루프의 길이. 지연을 포함한다 (Duration + CurrentDelay). SetupEmitterDuration이 정한다 (UE: EmitterDuration)
	float EmitterDuration = 0.0f;
	// 이번 루프의 지연 시간(초) (UE: CurrentDelay)
	float CurrentDelay = 0.0f;
	// 지금까지 완료한 루프 수 (UE: LoopCount)
	int32 LoopCount = 0;
	// true면 새 파티클을 만들지 않는다. 이미 나온 파티클은 계속 시뮬레이션된다 (UE: bHaltSpawning)
	bool bHaltSpawning = false;
	// 지난 Tick의 DeltaTime (UE: LastDeltaTime)
	float LastDeltaTime = 0.0f;
	// 현재 선택된 LOD 번호 (0이 가장 상세한 레벨)
	int32 CurrentLODLevelIndex = 0;

	// 컴포넌트의 월드 행렬에서 스케일을 뺀 것. 컴포넌트가 Tick 전에 SetComponentToWorld로 갱신한다 (UE: Component.GetComponentToWorld().ToMatrixNoScale()).
	// 컴포넌트가 없는 인스턴스(자체 테스트 등)는 단위 행렬이라 이미터 로컬과 월드가 같다.
	FMatrix ComponentToWorld;
	// 이미터 로컬 → 파티클이 시뮬레이션되는 공간. 월드 공간 이미터는 월드, 로컬 공간 이미터는 컴포넌트 공간이다 (UE: EmitterToSimulation)
	FMatrix EmitterToSimulation;
	// 시뮬레이션 공간 → 월드. 월드 공간 이미터는 단위 행렬이다 (UE: SimulationToWorld)
	FMatrix SimulationToWorld;

	// 모듈이 Distribution에서 난수를 뽑을 때 쓰는 스트림. 이미터마다 하나다.
	// 시드는 기본 0이라 같은 설정이면 항상 같은 결과가 나온다. 컴포넌트가 생기면 인스턴스마다 다른 시드를 넣는다.
	FRandomStream RandomStream;

	// 이 인스턴스를 만든 컴포넌트 (UE: Component). 모듈이 월드와 소유 액터에 접근할 때 쓴다 (예: 충돌 쿼리).
	// 컴포넌트가 설정하며, 자체 테스트처럼 컴포넌트 없이 만든 인스턴스는 nullptr이다.
	UParticleSystemComponent* OwnerComponent = nullptr;

	// 충돌 모듈이 월드 공간 선분을 쏠 때 먼저 부르는 함수. 비어 있으면 OwnerComponent의 월드를 쿼리한다 (TraceParticleSegment).
	// 자체 테스트가 월드 없이 충돌 로직을 검증하려고 가짜 면을 넣는 자리다.
	std::function<bool(const FVector& Start, const FVector& End, FParticleCollisionHit& OutHit)> CollisionTraceOverride;

	FParticleEmitterInstance()
	{
		ComponentToWorld.SetIdentity();
		EmitterToSimulation.SetIdentity();
		SimulationToWorld.SetIdentity();
	}
	// TypeData 인스턴스(Beam, Mesh, Ribbon)가 상속하고 컴포넌트가 기반 포인터로 delete하므로 가상 소멸자다
	virtual ~FParticleEmitterInstance() { Release(); }

	// 메모리를 소유하므로 복사하면 같은 블록을 두 번 해제하게 된다
	FParticleEmitterInstance(const FParticleEmitterInstance&) = delete;
	FParticleEmitterInstance& operator=(const FParticleEmitterInstance&) = delete;

	// ---- TypeData 인스턴스가 바꾸는 부분 (UE: FParticleBeamEmitterInstance 등이 FParticleEmitterInstance를 상속) ----
	// 기본 구현은 Sprite 이미터의 동작이다. 인스턴스는 UParticleModuleTypeDataBase::CreateInstance가 만든다.

	// 이 이미터를 직접 그렸으면 true. false면 컴포넌트가 Sprite 경로(빌보드 사각형)로 그린다.
	// ComponentDistanceSquared는 이미터 사이의 그리기 순서(반투명 정렬)에 쓰는 카메라-컴포넌트 거리 제곱이다.
	virtual bool SubmitRender(FRenderQueue& RenderQueue, const FViewContext& ViewContext, float ComponentDistanceSquared) { return false; }

	// 활성 파티클이 월드에서 차지하는 상자를 합친다. 기본은 Sprite 크기만큼 부풀린 점들이다 (ExpandSpriteWorldBounds).
	// 빔(시작점~끝점)이나 메시(메시 크기)처럼 모양이 다르면 덮도록 바꾼다. 컬링이 이 상자로 걸러낸다.
	virtual bool ExpandWorldBounds(FBox& InOutBox, bool& bInOutHasAny) const;

	// Template의 레이아웃 캐시를 읽어 InstanceData를 만든다. 파티클 메모리는 Resize가 만든다.
	// Template->CacheEmitterModuleInfo()를 먼저 호출해 두어야 하며, 그렇지 않으면 false를 돌려준다.
	// 이미 초기화돼 있으면 기존 메모리를 모두 버리고 다시 시작한다.
	// Seed는 RandomStream의 시드다. 루프 길이와 지연을 뽑는 SetupEmitterDuration보다 먼저 적용되므로 같은 Seed는 같은 이미터 동작을 만든다.
	// 컴포넌트는 인스턴스마다 다른 Seed를 넘긴다.
	virtual bool Init(UParticleEmitter* InTemplate, uint32 Seed = 0);

	// 파티클 칸 수를 NewMaxActiveParticles 이상으로 늘린다. 줄이지는 않는다.
	// 성공하면 ParticleData 주소가 바뀔 수 있어서, 이전에 얻은 FBaseParticle*는 모두 쓸 수 없다.
	// 0보다 작거나 MaxParticleLimit를 넘거나 할당에 실패하면 false이고 기존 메모리와 개수는 그대로다.
	virtual bool Resize(int32 NewMaxActiveParticles);

	// 모든 메모리를 해제하고 초기 상태로 돌린다. 여러 번 불러도 안전하다.
	virtual void Release();

	// ---- 한 프레임의 흐름 (UE: FParticleEmitterInstance::Tick) ----

	// 1) KillParticles → 2) ResetParticleParameters → 3) Tick_ModuleUpdate → 4) Tick_SpawnParticles → 5) UpdateParticlePositions
	// Update가 Spawn보다 앞이라서, 이번 프레임에 생긴 파티클은 Update 모듈을 거치지 않는다 (생성할 때 SpawnTime만큼 이미 이동했다).
	// TypeData 인스턴스는 오버라이드해서 기반 Tick 앞뒤에 자기 처리(빔 끝점 계산, 리본 꼬리 갱신 등)를 넣는다.
	virtual void Tick(float DeltaTime);

	// 수명이 끝난(RelativeTime > 1) 파티클을 살아 있는 구간 밖으로 보낸다. 슬롯은 이동하지 않고 인덱스만 교환한다.
	virtual void KillParticles();

	// 파티클 하나를 죽인다. KillIndex는 ParticleIndices의 위치이고 슬롯 번호가 아니다.
	// 살아 있는 구간의 마지막 칸과 교환하므로 이 호출 뒤 같은 위치에는 다른 파티클이 와 있다.
	virtual void KillParticle(int32 KillIndex);

	// ParticleIndices가 일관된 상태인지 검사한다 (UE: ParticleEmitterInstances.cpp:2119의 UsedIndices 검사에 해당).
	//  - 0 <= ActiveParticles <= MaxActiveParticles
	//  - [0, MaxActiveParticles)이 슬롯 번호 0..MaxActiveParticles-1의 순열이다 (범위를 벗어난 값, 중복이 없다)
	//  - 마지막 칸(MaxActiveParticles)이 UnusedIndex다
	// 메모리가 없는 상태(Max = 0)는 정상으로 본다. 어긋난 곳이 있으면 첫 위치를 로그로 남기고 false를 돌려준다.
	// 비용이 O(MaxActiveParticles)라서 Tick에서 부르지 않는다. 자체 테스트와 디버깅에서 직접 부른다.
	bool ValidateParticleIndices() const;

	// Base 값을 현재 값으로 복원하고(Velocity, Size, RotationRate, Color) 수명 진행(RelativeTime)을 반영한다.
	// Update 모듈은 이 복원값 위에 계산하므로 모듈 실행 순서가 바뀌어도 값이 프레임마다 누적되지 않는다.
	void ResetParticleParameters(float DeltaTime);

	// UpdateModules를 순서대로 실행한다 (꺼진 모듈은 건너뜀)
	virtual void Tick_ModuleUpdate(float DeltaTime);

	// 파티클이 컴포넌트 좌표계에서 시뮬레이션되는 이미터인지 (Required의 bUseLocalSpace). 렌더가 월드 위치를 만들 때 쓴다.
	bool SimulatesInLocalSpace() const;

	// 컴포넌트 행렬(스케일 제외)을 저장하고 EmitterToSimulation / SimulationToWorld를 다시 계산한다 (UE: UpdateTransforms).
	// Tick이 시작할 때도 불리므로 컴포넌트는 Tick 전에 SetComponentToWorld만 불러도 된다.
	void SetComponentToWorld(const FMatrix& InComponentToWorld);
	void UpdateTransforms();

	// 이미터 시간(SecondsSinceCreation, EmitterTime, LoopCount)을 DeltaTime만큼 진행하고 루프를 처리한다 (UE: Tick_EmitterTimeSetup).
	// 돌려주는 값은 이번 Tick의 지연 시간이다. EmitterTime에서는 이미 뺐고, Tick이 끝에서 다시 더한다.
	virtual float Tick_EmitterTimeSetup(float DeltaTime);

	// 인스턴스 생성 시 한 번 루프 길이와 지연을 정한다 (UE: SetupEmitterDuration). RandomStream을 쓸 수 있다.
	// Required 모듈이 없으면 Duration 0(루프 안 함), Delay 0이다.
	void SetupEmitterDuration();

	// 생성 횟수를 모두 채웠고 살아 있는 파티클도 없으면 true (UE: HasCompleted). 무한 반복이면 항상 false다.
	bool HasCompleted() const;

	// 지금 파티클을 만들어도 되는지 (EmitterTime >= 0이고 루프 횟수가 남았는지). Tick_SpawnParticles가 쓴다.
	// bFirstTime이 true면 루프 횟수를 넘겼어도 허용한다 (UE와 같다).
	bool CanSpawnNow(bool bFirstTime) const;

	// Spawn을 호출하고 새 SpawnFraction을 돌려준다. bFirstTime은 이 이미터의 첫 Tick이면 true.
	virtual float Tick_SpawnParticles(float DeltaTime, bool bFirstTime);

	// 지금 발사할 버스트의 개수 합을 돌려주고 해당 항목을 발사 완료로 표시한다 (UE: GetCurrentBurstRateOffset).
	// EmitterTime >= Time인 항목 중 아직 안 쏜 것이 대상이다. CountLow가 -1이 아니면 [CountLow, Count]에서 난수로 뽑는다.
	int32 GetCurrentBurstCount();

	// 모든 버스트를 발사 전으로 되돌린다. 루프가 돌 때 부른다 (UE: ResetBurstList).
	void ResetBurstList();

	// Rate(초당 개수)로 이번 프레임의 생성 개수를 정하고 BurstCount개를 더해 파티클을 만든다. 새 SpawnFraction을 돌려준다.
	// 버스트 파티클은 SpawnTime이 0이다 (UE의 bUseLegacySpawningBehavior == false와 같다).
	// 칸이 모자라면 늘리고, 늘리지 못하면 아무것도 만들지 않고 기존 SpawnFraction을 돌려준다.
	// 상한을 넘으면 버스트를 먼저 채우고 남은 칸만 Rate 분량에 쓴다 (UE와 같다).
	virtual float Spawn(float DeltaTime, float Rate, int32 BurstCount = 0);

	// Count개의 파티클을 만든다. 첫 파티클의 SpawnTime은 StartTime이고 이후 Increment씩 줄어든다.
	virtual void SpawnParticles(int32 Count, float StartTime, float Increment);

	// 슬롯을 0으로 채우고 초기 위치를 정한다. Spawn 모듈보다 먼저 호출한다.
	virtual void PreSpawn(FBaseParticle* Particle);

	// Spawn 모듈이 끝난 뒤 호출한다. 생성 시점부터 프레임 끝까지의 이동(SpawnTime * Velocity)을 반영하고
	// 생성 순번과 JustSpawned 상태를 기록한다.
	// 가상인 이유: TypeData 인스턴스가 파티클 생성 직후 자기 값을 정한다.
	//  - 빔처럼 파티클마다 값을 정해야 하는 인스턴스는 재정의해서 TypeData의 Spawn을 먼저 부른다 (UE와 같다).
	//  - 메시 인스턴스는 모듈 Spawn이 끝난 뒤 InitialOrientation을 정한다 (UE: FParticleMeshEmitterInstance::PostSpawn).
	virtual void PostSpawn(FBaseParticle* Particle, float SpawnTime);

	// 위치와 회전을 한 프레임 적분한다 (UE: UpdateBoundingBox 안의 적분 부분). 경계 상자 계산은 하지 않는다.
	// 방금 생성된 파티클(JustSpawned)은 이미 이동했으므로 건너뛰고 JustSpawned를 지운다.
	virtual void UpdateParticlePositions(float DeltaTime);

	// 생성 속도(초당 개수)와 프레임 시간으로 이번 프레임의 생성 계획을 계산한다. 인스턴스 상태를 읽거나 바꾸지 않는다.
	// Rate <= 0이면 Count = 0이고 NewFraction은 OldFraction 그대로다.
	static FParticleSpawnPlan ComputeSpawnPlan(float OldFraction, float Rate, float DeltaTime);

	// 현재 활성 LOD 번호를 바꾼다. 살아 있는 파티클 메모리는 유지하고 새 LOD의 Required 설정(길이, 변환)을 갱신한다.
	void SetCurrentLODIndex(int32 NewLODIndex);
	int32 GetCurrentLODIndex() const { return CurrentLODLevelIndex; }

	// 현재 LOD 레벨. 범위를 벗어나면 LOD 0으로 폴백하고, Template이나 LOD 0이 없으면 nullptr.
	UParticleLODLevel* GetCurrentLODLevel() const;
};
