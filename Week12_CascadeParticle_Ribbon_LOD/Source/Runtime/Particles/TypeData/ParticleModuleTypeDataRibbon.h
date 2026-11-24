#pragma once

#include "Particles/ParticleModule.h"
#include "Particles/ParticleEmitterInstances.h"
#include "Particles/ParticleSpriteRender.h"
#include "ParticleModuleTypeDataRibbon.generated.h"

// 리본 이미터 (UE: TypeData/ParticleModuleTypeDataRibbon). 움직인 경로에 남긴 점들을 이어 띠(꼬리)를 그린다. 미사일 연기, 궤적.
// 에디터에서 이미터 우클릭 → TypeData → New Ribbon Data로 붙인다. 이미터 칸의 Required 위에 "Ribbon Data"로 보인다.
//
// [틀만 있음] 지금은 Sprite 이미터와 똑같이 동작한다. 채울 곳:
//  1) 아래 UPROPERTY 설정값 (Details 패널에 자동으로 나온다)
//  2) FParticleRibbonEmitterInstance::Tick       : 이미터(소스)가 움직인 만큼 꼬리 점을 추가하고 오래된 점을 정리
//  3) FParticleRibbonEmitterInstance::SubmitRender: 이웃한 점을 이어 카메라를 향하는 띠 정점을 만들고 true를 돌려준다
//  4) FParticleRibbonEmitterInstance::ExpandWorldBounds: 꼬리 점 전체를 덮는 상자
//  5) 저장: 에디터의 저장과 이미터 복제는 Serialize를 거치므로, Serialize가 없는 Distribution/TArray 멤버는 저장 때 사라진다.
//     멤버를 추가하면 ParticleLayoutSelfTest의 저장/로드 왕복 검사도 함께 만든다.
// UE 참고 설정: MaxTrailCount, MaxParticleInTrailCount, SheetsPerTrail, MaxTessellationBetweenParticles,
//               TilingDistance, bDeadTrailsOnDeactivate, TangentSpawningScalar
// UE는 꼬리를 무엇에서 뽑을지(이미터 원점 / 다른 이미터의 파티클 / 액터)를 TrailSource 모듈로 정한다.
UCLASS()
class UParticleModuleTypeDataRibbon : public UParticleModuleTypeDataBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, meta=(ClampMin=0))
	int32 MaxTessellationBetweenParticles = 25;

	UPROPERTY(EditAnywhere, meta=(ClampMin=1))
	int32 SheetsPerTrail = 1;

	UPROPERTY(EditAnywhere, meta=(ClampMin=1))
	int32 MaxTrailCount = 1;

	UPROPERTY(EditAnywhere, meta=(ClampMin=1))
	int32 MaxParticleInTrailCount = 500;

	UPROPERTY(EditAnywhere)
	bool bDeadTrailsOnDeactivate = true;

	UPROPERTY(EditAnywhere)
	bool bDeadTrailsOnSourceLoss = true;

	UPROPERTY(EditAnywhere)
	bool bClipSourceSegement = true;

	UPROPERTY(EditAnywhere)
	bool bEnablePreviousTangentRecalculation = true;

	UPROPERTY(EditAnywhere)
	bool bTangentRecalculationEveryFrame = true;

	UPROPERTY(EditAnywhere)
	bool bSpawnInitialParticle = false;

	UPROPERTY(EditAnywhere, meta=(ClampMin=0))
	float TangentSpawningScalar = 0.0f;

	UPROPERTY(EditAnywhere)
	bool bRenderGeometry = true;

	UPROPERTY(EditAnywhere)
	bool bRenderSpawnPoints = false;

	UPROPERTY(EditAnywhere)
	bool bRenderTangents = false;

	UPROPERTY(EditAnywhere)
	bool bRenderTessellation = false;

	UPROPERTY(EditAnywhere, meta=(ClampMin=0))
	float TilingDistance = 0.0f;

	UPROPERTY(EditAnywhere, meta=(ClampMin=0))
	float DistanceTessellationStepSize = 15.0f;

	UPROPERTY(EditAnywhere)
	bool bEnableTangentDiffInterpScale = false;

	UPROPERTY(EditAnywhere, meta=(ClampMin=0))
	float TangentTessellationScalar = 25.0f;

	virtual uint32 RequiredBytes(UParticleModuleTypeDataBase* TypeData) override;
	virtual FParticleEmitterInstance* CreateInstance() const override;
};

struct FRibbonTypeDataPayload;
class FVertexBuffer;
class FIndexBuffer;

// 리본 이미터 한 개의 실행 상태 (UE: FParticleRibbonEmitterInstance). 파티클 하나가 꼬리의 점 하나다.
// 점들의 순서(앞/뒤 이웃)는 RequiredBytes로 Payload에 두는 것이 UE 방식이다 (UE: FRibbonTypeDataPayload).
struct FParticleRibbonEmitterInstance : public FParticleEmitterInstance
{
	int32 VertexCount = 0;
	int32 TriangleCount = 0;
	int32 TrailCount = 0;
	int32 MaxTrailCount = 0;
	float RunningTime = 0.0f;
	float LastTickTime = 0.0f;
	bool bDeadTrailsOnDeactivate = false;

	TArray<float> TrailSpawnTimes;
	TArray<float> LastSpawnTime;
	TArray<float> SourceDistanceTraveled;
	TArray<float> TiledUDistanceTraveled;
	bool bFirstUpdate = true;

	UParticleModuleTypeDataRibbon* TrailTypeData = nullptr;
	
	TArray<FVector> CurrentSourcePosition;
	TArray<FQuat> CurrentSourceRotation;
	TArray<FVector> CurrentSourceUp;
	TArray<FVector> CurrentSourceTangent;
	TArray<float> CurrentSourceTangentStrength;
	TArray<FVector> LastSourcePosition;
	TArray<FQuat> LastSourceRotation;
	TArray<FVector> LastSourceUp;
	TArray<FVector> LastSourceTangent;
	TArray<float> LastSourceTangentStrength;

	TArray<FVector> SourceOffsets;
	FParticleEmitterInstance* SourceEmitter = nullptr;

	int32 LastSelectedParticleIndex = INDEX_NONE;

	TArray<int32> SourceIndices;
	TArray<float> SourceTimes;
	TArray<float> LastSourceTimes;
	TArray<float> CurrentLifetimes;
	TArray<float> CurrentSizes;

	int32 HeadOnlyParticles = 0;

	TUniquePtr<FVertexBuffer> RenderVertexBuffer;
	TUniquePtr<FIndexBuffer> RenderIndexBuffer;
	int32 VertexBufferCapacity = 0;
	int32 IndexBufferCapacity = 0;
	TArray<FParticleSpriteVertex> StagingVertices;
	TArray<uint32> StagingIndices;

	bool EnsureRenderResources(int32 NeededVertices, int32 NeededIndices);
	void DestroyRenderResources();

	virtual ~FParticleRibbonEmitterInstance() override; 

	virtual bool Init(UParticleEmitter* InTemplate, uint32 Seed = 0) override;
	virtual bool Resize(int32 NewMaxActiveParticles) override;
	virtual void Release() override;
	virtual void Tick(float DeltaTime) override;
	virtual void KillParticles() override;
	virtual void KillParticle(int32 KillIndex) override;
	virtual float Tick_SpawnParticles(float DeltaTime, bool bFirstTime) override;
	virtual float Spawn(float DeltaTime, float Rate, int32 BurstCount = 0) override;
	virtual void PreSpawn(FBaseParticle* Particle) override;
	virtual void PostSpawn(FBaseParticle* Particle, float SpawnTime) override;
	bool SubmitRender(FRenderQueue& RenderQueue, const FViewContext& ViewContext, float ComponentDistanceSquared) override;
	bool ExpandWorldBounds(FBox& InOutBox, bool& bInOutHasAny) const override;

	void UpdateSourceData(float DeltaTime, bool bFirstTime);
	bool ResolveSourcePoint(int32 InTrailIdx, FVector& OutPosition, FQuat& OutRotation, FVector& OutUp, FVector& OutTangent, float& OutTangentStrength);
	bool AddParticleHelper(int32 InTrailIdx, int32 StartParticleIndex, FRibbonTypeDataPayload* StartTrailData, int32 ParticleIndex, FRibbonTypeDataPayload* TrailData);
	void Tick_RecalculateTangents(float DeltaTime, UParticleLODLevel* CurrentLODLevel);
	void KillParticles(int32 InTrailIdx, int32 InKillCount);
	void DetermineVertexAndTriangleCount();
	void GetParticleLifetimeAndSize(int32 InTrailIdx, const FBaseParticle* InParticle, bool bInNoLivingParticles, float& OutOneOverMaxLifetime, float& OutSize);

	enum EGetTrailDirection
	{
		GET_Prev,
		GET_Next
	};
	enum EGetTrailParticleOption
	{
		GET_Any,			// Grab the prev/next particle
		GET_Spawned,		// Grab the first prev/next particle that was true spawned
		GET_Interpolated,	// Grab the first prev/next particle that was interpolation spawned
		GET_Start,			// Grab the start particle for the trail the particle is in
		GET_End,			// Grab the end particle for the trail the particle is in
	};

	bool GetParticleInTrail(bool bSkipStartingParticle, FBaseParticle* InStartingFromParticle, FRibbonTypeDataPayload* InStartingTrailData, EGetTrailDirection InGetDirection, EGetTrailParticleOption InGetOption, FBaseParticle*& OutParticle, FRibbonTypeDataPayload*& OutTrailData);
};

struct FRibbonTypeDataPayload
{
	int32 Flags;
	int32 TrailIndex;
	int32 TriangleCount;
	float SpawnTime;
	float SpawnDelta;
	float TiledU;
	int32 SpawnedTessellationPoints;
	int32 RenderingInterpCount;
	float PinchScaleFactor;
	bool bInterpolatedSpawn;
	bool bMovementSpawned;

	FVector Tangent;
	FVector Up;
	int32 SourceIndex;
};

#define TRAIL_EMITTER_FLAG_FORCEKILL	0x00000000
//
//  Trail emitter flags and macros
//
// ForceKill: Indicates all the particles in the trail should be killed in the next KillParticles call.
#define TRAIL_EMITTER_FLAG_FORCEKILL	0x00000000
// DeadTrail: indicates that the particle is the start of a trail than should no longer spawn.
//			  It should just fade out as the particles die...
#define TRAIL_EMITTER_FLAG_DEADTRAIL	0x10000000
// Middle: indicates the particle is in the middle of a trail.
#define TRAIL_EMITTER_FLAG_MIDDLE       0x20000000
// Start: indicates the particle is the start of a trail.
#define TRAIL_EMITTER_FLAG_START        0x40000000
// End: indicates the particle is the end of a trail.
#define TRAIL_EMITTER_FLAG_END          0x80000000

//#define TRAIL_EMITTER_FLAG_ONLY	        (TRAIL_EMITTER_FLAG_START | TRAIL_EMITTER_FLAG_END)
#define TRAIL_EMITTER_FLAG_MASK         0xf0000000
#define TRAIL_EMITTER_PREV_MASK         0x0fffc000
#define TRAIL_EMITTER_PREV_SHIFT        14
#define TRAIL_EMITTER_NEXT_MASK         0x00003fff
#define TRAIL_EMITTER_NEXT_SHIFT        0

#define TRAIL_EMITTER_NULL_PREV			(TRAIL_EMITTER_PREV_MASK >> TRAIL_EMITTER_PREV_SHIFT)
#define TRAIL_EMITTER_NULL_NEXT			(TRAIL_EMITTER_NEXT_MASK >> TRAIL_EMITTER_NEXT_SHIFT)

// Helper macros
#define TRAIL_EMITTER_CHECK_FLAG(val, mask, flag)				((val & mask) == flag)
#define TRAIL_EMITTER_SET_FLAG(val, mask, flag)					((val & ~mask) | flag)
#define TRAIL_EMITTER_GET_PREVNEXT(val, mask, shift)			((val & mask) >> shift)
#define TRAIL_EMITTER_SET_PREVNEXT(val, mask, shift, setval)	((val & ~mask) | ((setval << shift) & mask))

// Start/end accessor macros
#define TRAIL_EMITTER_IS_START(index)       TRAIL_EMITTER_CHECK_FLAG(index, TRAIL_EMITTER_FLAG_MASK, TRAIL_EMITTER_FLAG_START)
#define TRAIL_EMITTER_SET_START(index)      TRAIL_EMITTER_SET_FLAG(index, TRAIL_EMITTER_FLAG_MASK, TRAIL_EMITTER_FLAG_START)

#define TRAIL_EMITTER_IS_END(index)			TRAIL_EMITTER_CHECK_FLAG(index, TRAIL_EMITTER_FLAG_MASK, TRAIL_EMITTER_FLAG_END)
#define TRAIL_EMITTER_SET_END(index)		TRAIL_EMITTER_SET_FLAG(index, TRAIL_EMITTER_FLAG_MASK, TRAIL_EMITTER_FLAG_END)

#define TRAIL_EMITTER_IS_MIDDLE(index)		TRAIL_EMITTER_CHECK_FLAG(index, TRAIL_EMITTER_FLAG_MASK, TRAIL_EMITTER_FLAG_MIDDLE)
#define TRAIL_EMITTER_SET_MIDDLE(index)		TRAIL_EMITTER_SET_FLAG(index, TRAIL_EMITTER_FLAG_MASK, TRAIL_EMITTER_FLAG_MIDDLE)

// Only is used for the first emission from the emitter
#define TRAIL_EMITTER_IS_ONLY(index)		(TRAIL_EMITTER_CHECK_FLAG(index, TRAIL_EMITTER_FLAG_MASK, TRAIL_EMITTER_FLAG_START)	&& \
											(TRAIL_EMITTER_GET_NEXT(index) == TRAIL_EMITTER_NULL_NEXT))
#define TRAIL_EMITTER_SET_ONLY(index)		TRAIL_EMITTER_SET_FLAG(index, TRAIL_EMITTER_FLAG_MASK, TRAIL_EMITTER_FLAG_START)

#define TRAIL_EMITTER_IS_FORCEKILL(index)	TRAIL_EMITTER_CHECK_FLAG(index, TRAIL_EMITTER_FLAG_MASK, TRAIL_EMITTER_FLAG_FORCEKILL)
#define TRAIL_EMITTER_SET_FORCEKILL(index)	TRAIL_EMITTER_SET_FLAG(index, TRAIL_EMITTER_FLAG_MASK, TRAIL_EMITTER_FLAG_FORCEKILL)

#define TRAIL_EMITTER_IS_DEADTRAIL(index)	TRAIL_EMITTER_CHECK_FLAG(index, TRAIL_EMITTER_FLAG_MASK, TRAIL_EMITTER_FLAG_DEADTRAIL)
#define TRAIL_EMITTER_SET_DEADTRAIL(index)	TRAIL_EMITTER_SET_FLAG(index, TRAIL_EMITTER_FLAG_MASK, TRAIL_EMITTER_FLAG_DEADTRAIL)

#define TRAIL_EMITTER_IS_HEAD(index)		(TRAIL_EMITTER_IS_START(index) || TRAIL_EMITTER_IS_DEADTRAIL(index))
#define TRAIL_EMITTER_IS_HEADONLY(index)	((TRAIL_EMITTER_IS_START(index) || TRAIL_EMITTER_IS_DEADTRAIL(index)) && \
											(TRAIL_EMITTER_GET_NEXT(index) == TRAIL_EMITTER_NULL_NEXT))

// Prev/Next accessor macros
#define TRAIL_EMITTER_GET_PREV(index)       TRAIL_EMITTER_GET_PREVNEXT(index, TRAIL_EMITTER_PREV_MASK, TRAIL_EMITTER_PREV_SHIFT)
#define TRAIL_EMITTER_SET_PREV(index, prev) TRAIL_EMITTER_SET_PREVNEXT(index, TRAIL_EMITTER_PREV_MASK, TRAIL_EMITTER_PREV_SHIFT, prev)
#define TRAIL_EMITTER_GET_NEXT(index)       TRAIL_EMITTER_GET_PREVNEXT(index, TRAIL_EMITTER_NEXT_MASK, TRAIL_EMITTER_NEXT_SHIFT)
#define TRAIL_EMITTER_SET_NEXT(index, next) TRAIL_EMITTER_SET_PREVNEXT(index, TRAIL_EMITTER_NEXT_MASK, TRAIL_EMITTER_NEXT_SHIFT, next)


#define DECLARE_PARTICLE_PTR(Name,Address)		\
	FBaseParticle* Name = (FBaseParticle*) (Address);