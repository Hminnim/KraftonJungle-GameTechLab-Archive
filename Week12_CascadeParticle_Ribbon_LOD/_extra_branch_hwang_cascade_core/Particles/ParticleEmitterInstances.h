#pragma once

#include "Containers/Array.h"
#include "Math/Vector.h"
#include "Math/Vector2.h"
#include "Math/Matrix.h"
#include "Math/Box.h"
#include "Engine/Classes/Distributions/Distribution.h"

class UMaterialInterface;
class UParticleEmitter;
class UParticleLODLevel;
class UParticleModule;
class UParticleModuleRequired;
class UParticleModuleSpawn;
class IParticleEmitterInstanceOwner;
struct FBaseParticle;
struct FParticleEventInstancePayload;

struct FParticleEmitterBuildInfo
{
	UParticleModuleRequired* RequiredModule;
	UParticleModuleSpawn* SpawnModule;
	TArray<UParticleModule*> SpawnModules;

	FRawDistributionVector DynamicColor;
	FRawDistributionFloat DynamicAlpha;

	FRawDistributionVector DynamicColorScale;
	FRawDistributionFloat DynamicAlphaScale;

	FVector2 MaxSize;
	FVector2 SizeScaleBySpeed;
	FVector2 MaxSizeScaleBySpeed;

	// Collision
	bool bEnableCollision;
	uint8 CollisionResponse;
	uint8 CollisionMode;
	float CollisionRadiusScale;
	float CollisionRadiusBias;
	float CollisionRandomSpread;
	float CollisionRandomDistribution;
	float Friction;

	FVector ConstantAcceleration;

	float MaxLifetime;
	float MaxRotationRate;
	int32 EstimatedMaxActiveParticleCount;

	int32 ScreenAlignment;

	FVector2 PivotOffset;

	FParticleEmitterBuildInfo();
};

struct FParticleEmitterInstance
{
	UParticleEmitter* SpriteTemplate;
	IParticleEmitterInstanceOwner& Component;
	UParticleLODLevel* CurrentLODLevel;

	int32 CurrentLODLevelIndex;
	int32 TypeDataOffset;
	int32 TypeDataInstanceOffset;
	int32 SubUVDataOffset;
	int32 DynamicParameterDataOffset;
	int32 CameraPayloadOffset;
	int32 PayloadOffset;

	FVector Location;
	FMatrix EmitterToSimulation;
	FMatrix SimulationToWorld;

	bool bEnabled = true;
	bool bKillOnDeactivate = true;
	bool bKillOnCompleted = true;
	bool bHaltSpawning = false;
	bool bIgnoreComponentScale = false;
	bool bIsBeam = false;
	bool bEmitterIsDone = false;

	int32 SortMode;
	uint8* ParticleData;
	uint16* ParticleIndices;
	uint8* InstanceData;
	int32 InstancePayloadSize;

	int32 ParticleSize;
	int32 ParticleStride;
	int32 ActiveParticles;
	uint32 ParticleCounter;
	int32 MaxActiveParticles;
	float SpawnFraction;

	float SecondsSinceCreation;
	float EmitterTime;
	float LastDeltaTime;
	float LastTickDurationMs;

	FVector OldLocation;
	FBox ParticleBoundingBox;
	int32 LoopCount;
	int32 IsRenderDataDirty;
	float EmitterDuration;
	TArray<float> EmitterDurations;
	float CurrentDelay;

	int32	TrianglesToRender;
	int32 MaxVertexIndex;

	UMaterialInterface* CurrentMaterial;

	FParticleEmitterInstance(IParticleEmitterInstanceOwner& InComponent);
	virtual ~FParticleEmitterInstance();

	virtual void InitParameters(UParticleEmitter* InTemplate);
	virtual void Init();

	virtual bool Resize(int32 NewMaxActiveParticles, bool bSetMaxActiveCount = true);
	virtual void Tick(float DeltaTime, bool bSuppressSpawning);
	void CheckEmitterFinished();

	virtual float Tick_EmitterTimeSetup(float DeltaTime, UParticleLODLevel* CurrentLODLevel);
	virtual float Tick_SpawnParticles(float DeltaTime, UParticleLODLevel* CurrentLODLevel, bool bSuppressSpawning, bool bFirstTime);
	virtual void Tick_ModuleUpdate(float DeltaTime, UParticleLODLevel* CurrentLODLevel);
	virtual void Tick_ModulePostUpdate(float DeltaTime, UParticleLODLevel* CurrentLODLevel);
	virtual void Tick_ModuleFinalUpdate(float DeltaTime, UParticleLODLevel* CurrentLODLevel);

	virtual void SetCurrentLODIndex(int32 InLODIndex, bool bInFullyProcess);

	virtual void GetAllocatedSize(int32& OutNum, int32& OutMax);

	void SpawnParticles(int32 Count, float StartTime, float Increment, const FVector& InitialLocation, const FVector& InitialVelocity, struct FParticleEventInstancePayload* EventPayload);
	virtual void PreSpawn(FBaseParticle* Particle, const FVector& InitialLocation, const FVector& InitialVelocity);
	virtual void PostSpawn(FBaseParticle* Particle, float InterpolationPercentage, float SpawnTime);

	virtual void KillParticles();


	void SetupEmitterDuration();
};

struct FParticleSpriteEmitterInstance : public FParticleEmitterInstance
{
	FParticleSpriteEmitterInstance(IParticleEmitterInstanceOwner& InComponent);
	virtual ~FParticleSpriteEmitterInstance();

	virtual void GetAllocatedSize(int32& OutNum, int32& OutMax) override;
};