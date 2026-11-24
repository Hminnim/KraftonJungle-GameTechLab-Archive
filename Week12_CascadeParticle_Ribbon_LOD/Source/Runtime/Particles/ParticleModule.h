#pragma once

#include "UObject/Object.h"
#include "Particles/ParticleDistribution.h"
#include "Math/Rotator.h"
#include "Render/Texture2D.h"
#include "ParticleModule.generated.h"

class UParticleModuleTypeDataBase;
class UParticleEmitter;
struct FParticleEmitterInstance;
struct FParticleSpawnContext;
struct FParticleUpdateContext;

// 모든 파티클 모듈의 기반 클래스 (UE: Classes/Particles/ParticleModule.h)
// 모듈은 에셋의 일부라서 같은 에셋을 쓰는 모든 컴포넌트가 공유한다. 런타임 상태(파티클별 값)는
// 모듈 멤버가 아니라 파티클 메모리(Payload)에 둔다.
UCLASS()
class UParticleModule : public UObject
{
	GENERATED_BODY()
public:
	// 이 모듈이 Spawn / Update를 구현하는지. 파생 클래스가 생성자에서 true로 바꾼다 (UE: bSpawnModule, bUpdateModule)
	// 이 플래그로 LODLevel의 SpawnModules / UpdateModules 목록이 만들어진다. 플래그가 false면 Spawn / Update는 호출되지 않는다.
	bool bSpawnModule = false;
	bool bUpdateModule = false;

	// 새 파티클 한 개를 초기화한다. 이미터가 슬롯을 0으로 채운 직후, 위치 보정(PostSpawn) 직전에 모듈 순서대로 호출한다. (UE: Spawn)
	// 파티클의 값은 Context.ParticleBase에, Payload는 Context.Offset부터 있다. SPAWN_INIT과 PARTICLE_ELEMENT로 읽는다.
	virtual void Spawn(const FParticleSpawnContext& Context) {}

	// 살아 있는 모든 파티클을 한 프레임 갱신한다. 모듈 순서대로 호출한다. (UE: Update)
	// BEGIN_UPDATE_LOOP와 END_UPDATE_LOOP 사이에 본문을 쓴다.
	virtual void Update(const FParticleUpdateContext& Context) {}

	// 꺼진 모듈은 Spawn/Update 실행에서만 제외한다 (UE: bEnabled)
	// Payload 크기 계산에는 포함한다. 모듈을 껐다 켜도 파티클 메모리 레이아웃이 바뀌지 않게 하기 위해서다.
	UPROPERTY(EditAnywhere)
	bool bEnabled = true;

	// 파티클마다 필요한 추가 바이트(Payload). FBaseParticle 뒤에 이어 붙는다 (UE: RequiredBytes)
	// 이미터 타입(TypeData)에 따라 크기가 달라질 수 있어 TypeData를 받는다. Sprite 이미터면 nullptr이다.
	// 돌려준 크기는 8의 배수로 올림되어 쌓이므로 모듈의 Payload는 8바이트 경계에서 시작한다.
	// float, int, bool, double, uint64, 포인터는 둘 수 있고 alignas(16) 타입은 둘 수 없다.
	virtual uint32 RequiredBytes(UParticleModuleTypeDataBase* TypeData) { return 0; }

	// 이미터 인스턴스마다 한 번만 필요한 추가 바이트(InstanceData) (UE: RequiredBytesPerInstance)
	// 파티클마다가 아니라 이미터 단위로 누적하는 상태(예: 이전 프레임 위치)에 쓴다. Payload와 같이 8의 배수로 올림된다.
	virtual uint32 RequiredBytesPerInstance() { return 0; }

	// 에디터에서 이 모듈을 이미터에 새로 붙였을 때 한 번 호출한다 (UE: SetToSensibleDefaults, Cascade가 모듈 추가 직후 부른다).
	// 자기 값뿐 아니라 이미터의 다른 모듈 값을 이 모듈에 맞게 바꿔도 된다. 기본 구현은 아무것도 하지 않는다.
	// 에셋을 불러올 때는 호출하지 않으므로 저장된 값을 덮어쓰지 않는다.
	virtual void SetToSensibleDefaults(UParticleEmitter* Owner) {}
};

class UMaterial;

// 이미터마다 반드시 하나 있는 필수 설정 (UE: ParticleModuleRequired). 머티리얼, Local/World 공간 같은 설정 항목은 아직 없다.
// 지금은 이미터의 수명 주기(Duration, Loops, Delay)만 가진다. 이미터는 Duration초 동안 한 번의 "루프"를 돌고, Loops번 반복한 뒤 생성을 멈춘다.
// UE와 다름: bUseLegacyEmitterTime, bDurationRecalcEachLoop, bDelayFirstLoopOnly는 없다.
//  - 이미터 시간은 항상 UE의 기본 방식(bUseLegacyEmitterTime == false)으로 계산한다.
//  - Duration과 Delay의 범위 난수는 인스턴스를 만들 때 한 번만 뽑는다 (루프마다 다시 뽑지 않는다).
//  - Delay는 모든 루프의 앞에 붙는다 (첫 루프에만 붙이는 옵션 없음).
UCLASS()
class UParticleModuleRequired : public UParticleModule
{
	GENERATED_BODY()
public:
	// 한 루프의 길이(초). 0 이하면 루프하지 않고 시간이 계속 흐른다.
	UPROPERTY(EditAnywhere, meta=(ClampMin=0))
	float EmitterDuration = 1.0f;
	// bEmitterDurationUseRange가 true면 [EmitterDurationLow, EmitterDuration] 중 난수로 정한다
	UPROPERTY(EditAnywhere, meta=(ClampMin=0))
	float EmitterDurationLow = 0.0f;
	UPROPERTY(EditAnywhere)
	bool bEmitterDurationUseRange = false;

	// 루프 횟수. 0이면 무한 반복한다.
	UPROPERTY(EditAnywhere, meta=(ClampMin=0))
	int32 EmitterLoops = 0;

	// 이미터가 생성을 시작하기 전에 기다리는 시간(초). 루프마다 앞에 붙는다.
	UPROPERTY(EditAnywhere, meta=(ClampMin=0))
	float EmitterDelay = 0.0f;
	// bEmitterDelayUseRange가 true면 [EmitterDelayLow, EmitterDelay] 중 난수로 정한다
	UPROPERTY(EditAnywhere, meta=(ClampMin=0))
	float EmitterDelayLow = 0.0f;
	UPROPERTY(EditAnywhere)
	bool bEmitterDelayUseRange = false;

	// true면 파티클이 컴포넌트 좌표계에서 시뮬레이션되어 컴포넌트를 따라 움직이고,
	// false(기본, UE와 같음)면 월드 좌표에서 시뮬레이션되어 이미 나온 파티클은 컴포넌트가 움직여도 남는다.
	UPROPERTY(EditAnywhere)
	bool bUseLocalSpace = false;

	// 컴포넌트 기준 이미터의 위치와 회전 (UE: EmitterOrigin, EmitterRotation). 생성 위치와 방향의 기준이 된다.
	UPROPERTY(EditAnywhere)
	FVector EmitterOrigin = FVector(0.0f, 0.0f, 0.0f);
	UPROPERTY(EditAnywhere)
	FRotator EmitterRotation = FRotator(0.0f, 0.0f, 0.0f);

	// 이 이미터를 그릴 머티리얼 (UE: Required.Material). 텍스처와 블렌드(Alpha/Additive)는 머티리얼이 정한다.
	// nullptr이면 엔진 기본 스프라이트 머티리얼(ResourcePaths::ParticleSpriteMaterial)로 그린다. UE의 DefaultSpriteMaterial과 같은 역할이다.
	// 파티클 PSO(ParticleSprite_*)가 아닌 머티리얼은 정점 형식이 맞지 않아 기본 머티리얼로 대신 그린다 (GetParticleRenderMaterial).
	//  - 에셋 머티리얼(경로 있음)은 공유한다. 이 모듈이 지우지 않는다.
	//  - 인스턴스(bIsInstance, UE의 Material Instance처럼 텍스처만 바꾼 복제본)는 이 모듈이 소유한다.
	// UPROPERTY가 아니다: 인스턴스는 경로가 없어서 에셋 경로로 저장하는 FObjectProperty로는 저장할 수 없다. Serialize가 직접 쓴다.
	UMaterial* Material = nullptr;

	~UParticleModuleRequired() override;

	// SetMaterial로 교체된 소유 인스턴스. 이번 프레임의 렌더 패킷이 아직 가리킬 수 있어 바로 지우지 않고 소멸자에서 지운다.
	TArray<UMaterial*> RetiredMaterials;

	// Material을 바꾼다. 지금 들고 있는 인스턴스는 이 모듈 소유라 지운다 (NewMaterial과 같으면 그대로).
	void SetMaterial(UMaterial* NewMaterial);
	// 텍스처만 바꾼 머티리얼 인스턴스로 바꾼다 (UE: Material Instance의 텍스처 파라미터).
	// 기준은 지금 머티리얼의 원본 에셋(없으면 DefaultMaterialKey 에셋)이라 블렌드는 그대로다. Texture가 nullptr이면 원본 에셋으로 돌아간다.
	void SetMaterialTexture(UTexture2D* Texture, const char* DefaultMaterialKey);

	using Super::Serialize;
	// UPROPERTY 값 + Material (UMaterial::SaveMaterial 형식).
	// 옛 에셋의 Texture / BlendMode 항목은 같은 모양의 머티리얼 인스턴스로 바꿔 읽는다.
	void Serialize(json& Handle, bool bIsLoading) override;
};

// 정해진 시각에 한꺼번에 만드는 파티클 묶음 (UE: FParticleBurst)
struct FParticleBurst
{
	// 만들 개수. CountLow가 -1이 아니면 [CountLow, Count] 중 난수로 정한다.
	int32 Count = 0;
	int32 CountLow = -1;
	// 이미터 시간(EmitterTime)이 이 값에 이르면 한 루프에 한 번 발사한다
	float Time = 0.0f;
};

// 생성 속도와 버스트를 정하는 모듈 (UE: ParticleModuleSpawn). LODLevel의 SpawnModule 슬롯에 둔다.
// UE와 같이 Spawn 함수를 구현하는 일반 모듈이 아니다: 이미터가 Tick마다 Rate와 버스트를 직접 읽는다.
// UE와 다름: BurstScale, ParticleBurstMethod, 전역/품질 Rate 배율, 모듈별 난수 시드(이미터의 RandomStream을 쓴다)는 없다.
// 초당 개수 = Rate(EmitterTime) * RateScale(EmitterTime)이고 0 미만은 0으로 본다.
UCLASS()
class UParticleModuleSpawn : public UParticleModule
{
	GENERATED_BODY()
public:
	using Super::Serialize;
	void Serialize(json& Handle, bool bIsLoading) override;
	// UObject 리플렉션이 TArray와 Distribution을 지원하지 않아 UPROPERTY가 없다
	FFloatDistribution Rate = FFloatDistribution::MakeConstant(20.0f);
	FFloatDistribution RateScale = FFloatDistribution::MakeConstant(1.0f);
	TArray<FParticleBurst> BurstList;
};

// 파티클의 수명을 정하는 모듈 (UE: ParticleModuleLifetime)
// Distribution을 EmitterTime으로 평가해 OneOverMaxLifetime을 정한다. 수명 모듈이 여러 개면 수명을 더한다.
// 수명이 0 이하이면 OneOverMaxLifetime = 0이라 파티클이 나이로는 죽지 않는다 (UE와 같다).
UCLASS()
class UParticleModuleLifetime : public UParticleModule
{
	GENERATED_BODY()
public:
	using Super::Serialize;
	void Serialize(json& Handle, bool bIsLoading) override;
	UParticleModuleLifetime();
	void Spawn(const FParticleSpawnContext& Context) override;

	FFloatDistribution Lifetime = FFloatDistribution::MakeConstant(1.0f);
};

// 아래 세 초기값 모듈은 Payload가 없고 FBaseParticle의 값에 "더한다" (UE: ParticleModules_Location/Velocity/Size.cpp).
// 더하기라서 같은 종류 모듈을 여러 개 두면 값이 합쳐진다. 모든 Distribution은 EmitterTime으로 평가한다.
// 위치와 속도는 이미터 로컬 값을 EmitterToSimulation으로 시뮬레이션 공간에 옮겨 쓴다 (컴포넌트가 없으면 단위 행렬이라 그대로다).
// UE와 다름: bInWorldSpace, bApplyOwnerScale, Size의 UV 뒤집기 보정은 없다. 컴포넌트 스케일은 이미터 변환에서 뺀다.

// 생성 위치 (UE: ParticleModuleLocation). UE의 DistributeOverNPoints/DistributeThreshold(격자점 분포)는 없다.
UCLASS()
class UParticleModuleLocation : public UParticleModule
{
	GENERATED_BODY()
public:
	using Super::Serialize;
	void Serialize(json& Handle, bool bIsLoading) override;
	UParticleModuleLocation();
	void Spawn(const FParticleSpawnContext& Context) override;

	FVectorDistribution StartLocation = FVectorDistribution::MakeConstant(FVector(0.0f, 0.0f, 0.0f));
};

// 생성 속도 (UE: ParticleModuleVelocity). StartVelocity에 더해 "원점에서 파티클 위치로 향하는 방향 × StartVelocityRadial"을 더한다.
// 원점은 이미터 로컬 원점(0)이다. 파티클이 원점에 있으면 방향이 0이라 방사 성분은 0이다 (UE의 GetSafeNormal과 같다).
// Velocity와 BaseVelocity 양쪽에 더한다: Reset이 BaseVelocity로 복원하므로 둘이 같아야 첫 프레임 이후에도 속도가 유지된다.
UCLASS()
class UParticleModuleVelocity : public UParticleModule
{
	GENERATED_BODY()
public:
	using Super::Serialize;
	void Serialize(json& Handle, bool bIsLoading) override;
	UParticleModuleVelocity();
	void Spawn(const FParticleSpawnContext& Context) override;

	FVectorDistribution StartVelocity = FVectorDistribution::MakeConstant(FVector(0.0f, 0.0f, 0.0f));
	FFloatDistribution StartVelocityRadial = FFloatDistribution::MakeConstant(0.0f);
};

// 생성 크기 (UE: ParticleModuleSize). Size와 BaseSize 양쪽에 더한다. 기본값 (1,1,1)은 UE 기본값을 확인하지 못해 임의로 정했다.
UCLASS()
class UParticleModuleSize : public UParticleModule
{
	GENERATED_BODY()
public:
	using Super::Serialize;
	void Serialize(json& Handle, bool bIsLoading) override;
	UParticleModuleSize();
	void Spawn(const FParticleSpawnContext& Context) override;

	FVectorDistribution StartSize = FVectorDistribution::MakeConstant(FVector(1.0f, 1.0f, 1.0f));
};

// 생성 색 (UE: ParticleModuleColor). 위 세 모듈과 달리 더하지 않고 대입한다 (UE: ParticleModules_Color.cpp:167-170).
// RGB는 StartColor, A는 StartAlpha이고 Color와 BaseColor에 같은 값을 넣는다 (Reset이 BaseColor로 복원하기 때문).
// 값은 클램프하지 않는다: Additive 블렌딩이 1을 넘는 값을 쓸 수 있다 (FBaseParticle::Color가 FLinearColor인 이유).
// 기본값 StartColor (1,1,1)은 UE의 SetToSensibleDefaults와 같고, StartAlpha 1은 UE 기본값을 확인하지 못해 임의로 정했다.
UCLASS()
class UParticleModuleColor : public UParticleModule
{
	GENERATED_BODY()
public:
	using Super::Serialize;
	void Serialize(json& Handle, bool bIsLoading) override;
	UParticleModuleColor();
	void Spawn(const FParticleSpawnContext& Context) override;

	FVectorDistribution StartColor = FVectorDistribution::MakeConstant(FVector(1.0f, 1.0f, 1.0f));
	FFloatDistribution StartAlpha = FFloatDistribution::MakeConstant(1.0f);
};

// 이미터 종류를 정하는 슬롯의 기반 클래스 (UE: TypeData/ParticleModuleTypeDataBase)
// LODLevel의 TypeDataModule이 nullptr이면 Sprite 이미터다. Mesh/Beam/Ribbon은 이 클래스를 상속한다 (Particles/TypeData/).
UCLASS()
class UParticleModuleTypeDataBase : public UParticleModule
{
	GENERATED_BODY()
public:
	// 이 종류의 이미터를 실행할 인스턴스를 만든다 (UE: CreateInstance). 컴포넌트가 소유하고 delete한다.
	// 기본은 Sprite와 같은 FParticleEmitterInstance다. 파생 TypeData는 자기 인스턴스 타입(FParticleBeamEmitterInstance 등)을 돌려준다.
	virtual FParticleEmitterInstance* CreateInstance() const;

	// 레이아웃을 계산하기 직전에 이미터의 다른 모듈 정보를 모은다 (UE: CacheModuleInfo). RequiredBytes보다 먼저 불린다.
	// 예) 빔은 Noise 모듈의 Frequency로 Payload 크기를 정한다. 기본 구현은 아무것도 하지 않는다.
	virtual void CacheModuleInfo(UParticleEmitter* Emitter) {}
};
