#pragma once

#include "ParticleHelper.h"
#include "UObject/Object.h"
#include "Render/MaterialInterface.h"
#include "Engine/Classes/Distributions/Distribution.h"
#include "ParticleModule.generated.h"

class UParticleModuleTypeDataBase;
class UParticleEmitter;
class IParticleEmitterInstanceOwner;
struct FParticleEmitterInstance;
struct FRandomStream;

enum EModuleType : int
{
	EPMT_General,
	EPMT_TypeData,
	EPMT_Beam,
	EPMT_Trail,
	EPMT_Spawn,
	EPMT_Required,
	EPMT_Event,
	EPMT_Light,
	EPMT_SubUV,
	EPMT_MAX
};

UENUM()
enum class EParticleScreenAlignment : uint8
{
	PSA_FacingCameraPosition,
	PSA_Square,
	PSA_Rectangle,
	PSA_Velocity,
	PSA_TypeSpecific,
};

UENUM()
enum class EParticleSortMode : uint8
{
	PSORTMODE_None,
	PSORTMODE_ViewProjDepth,
	PSORTMODE_DistanceToView,
	PSORTMODE_Age_OldestFirst,
	PSORTMODE_Age_NewestFirst,
};

UENUM()
enum class EParticleSubUVInterpMethod : uint8
{
	PSUVIM_None,
	PSUVIM_Linear,
	PSUVIM_Linear_Blend,
	PSUVIM_Random,
	PSUVIM_Random_Blend,
};

UCLASS()
class UParticleModule : public UObject
{
	GENERATED_BODY()

public:
	bool bSpawnModule = false;
	bool bUpdateModule = false;
	bool bFinalUpdateModule = false;

	UPROPERTY(EditAnywhere)
	bool bEnabled = true;

	virtual void CompileModule(struct FParticleEmitterBuildInfo& EmitterInfo);

	struct FContext
	{
		FParticleEmitterInstance& Owner;
		FContext(FParticleEmitterInstance& Ow) : Owner(Ow) {}
	};

	struct FSpawnContext : FContext
	{
		int32 Offset;
		float SpawnTime;
		FBaseParticle* ParticleBase;
		FSpawnContext(FParticleEmitterInstance& Ow, int32 Of, float St, FBaseParticle* Pb) : FContext(Ow), Offset(Of), SpawnTime(St), ParticleBase(Pb) {}
	};

	virtual void Spawn(const FSpawnContext& Context);

	struct FUpdateContext : FContext
	{
		int32 Offset;
		float DeltaTime;
		FUpdateContext(FParticleEmitterInstance& Ow, int32 Of, float Dt) : FContext(Ow), Offset(Of), DeltaTime(Dt) {}
	};

	virtual void Update(const FUpdateContext& Context);
	virtual void FinalUpdate(const FUpdateContext& Context);
	virtual uint32 RequiredBytes(UParticleModuleTypeDataBase* TypeData);
	virtual uint32 RequiredBytesPerInstance();
	virtual EModuleType GetModuleType() const { return EModuleType::EPMT_General; }
};

UCLASS()
class UParticleModuleRequired : public UParticleModule
{
	GENERATED_BODY()

public:
	UParticleModuleRequired();

	UPROPERTY(EditAnywhere)
	UMaterialInterface* Material = nullptr;

	UPROPERTY(EditAnywhere)
	FVector EmitterOrigin = FVector::ZeroVector;

	UPROPERTY(EditAnywhere)
	FRotator EmitterRotation = FRotator(0.0f, 0.0f, 0.0f);

	UPROPERTY(EditAnywhere)
	EParticleScreenAlignment ScreenAlignment = EParticleScreenAlignment::PSA_FacingCameraPosition;

	UPROPERTY(EditAnywhere)
	EParticleSortMode SortMode = EParticleSortMode::PSORTMODE_None;

	UPROPERTY(EditAnywhere)
	bool bUseLocalSpace = false;

	UPROPERTY(EditAnywhere)
	float EmitterDuration = 1.0f;

	UPROPERTY(EditAnywhere)
	float EmitterDelay = 0.0f;

	UPROPERTY(EditAnywhere)
	int32 EmitterLoops = 0;

	UPROPERTY(EditAnywhere)
	EParticleSubUVInterpMethod InterpolationMethod = EParticleSubUVInterpMethod::PSUVIM_None;

	UPROPERTY(EditAnywhere)
	int32 SubImages_Horizontal = 1;

	UPROPERTY(EditAnywhere)
	int32 SubImages_Vertical = 1;

	UPROPERTY(EditAnywhere)
	int32 MaxDrawCount = 500;

	virtual EModuleType GetModuleType() const override { return EModuleType::EPMT_Required; }
};

UCLASS()
class UParticleModuleSpawn : public UParticleModule
{
	GENERATED_BODY()

public:
	UParticleModuleSpawn();

	FRawDistributionFloat Rate;
	FRawDistributionFloat RateScale;
	TArray<FParticleBurst> BurstList;

	UPROPERTY(EditAnywhere)
	float DefaultSpawnRate = 0.0f;

	UPROPERTY(EditAnywhere)
	bool bApplyGlobalSpawnRateScale = true;

	virtual EModuleType GetModuleType() const override { return EModuleType::EPMT_Spawn; }

	void GetSpawnAmount(float EmitterTime, float DeltaTime, float& InOutSpawnFraction, int32& OutSpawnCount, UObject* Data = nullptr, FRandomStream* InRandomStream = nullptr);

	int32 GetBurstCount(float OldEmitterTime, float NewEmitterTime, FRandomStream* InRandomStream = nullptr) const;

	float GetMaximumSpawnRate();
};

UCLASS()
class UParticleModuleTypeDataBase : public UParticleModule
{
	GENERATED_BODY()

public:
	virtual EModuleType GetModuleType() const override { return EModuleType::EPMT_TypeData; }
	virtual FParticleEmitterInstance* CreateInstance(UParticleEmitter* InEmitterParent, IParticleEmitterInstanceOwner& InComponent) { return nullptr; }

	virtual bool RequiresBuild() const { return false; }
	virtual void Build(struct FParticleEmitterBuildInfo& EmitterBuildInfo) {}
	virtual void CacheModuleInfo(UParticleEmitter* Emitter) {}
};

UCLASS()
class UParticleModuleLifetimeBase : public UParticleModule
{
	GENERATED_BODY()

public:
	virtual float GetMaxLifeTime()
	{
		return 0.0f;
	}

	virtual float GetLifetimeValue(const FContext& Context, float InTime, UObject* Data = nullptr) = 0;
};

UCLASS()
class UParticleModuleLifetime : public UParticleModuleLifetimeBase
{
	GENERATED_BODY()

public:
	UParticleModuleLifetime();

	FRawDistributionFloat LifeTime;

	virtual float GetLifetimeValue(const FContext& Context, float InTime, UObject* Data = nullptr) override;

	virtual void CompileModule(struct FParticleEmitterBuildInfo& EmitterInfo) override;
	virtual void Spawn(const FSpawnContext& Context) override;
};

UCLASS()
class UParticleModuleSize : public UParticleModule
{
	GENERATED_BODY()

public:
	UParticleModuleSize();

	FRawDistributionVector StartSize;

	virtual void CompileModule(struct FParticleEmitterBuildInfo& EmitterInfo) override;
	virtual void Spawn(const FSpawnContext& Context) override;
};

UCLASS()
class UParticleModuleVelocityBase : public UParticleModule
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere)
	bool bInWorldSpace = false;

	UPROPERTY(EditAnywhere)
	bool bApplyOwnerScale = true;
};

UCLASS()
class UParticleModuleVelocity : public UParticleModuleVelocityBase
{
	GENERATED_BODY()

public:
	UParticleModuleVelocity();

	FRawDistributionVector StartVelocity;
	FRawDistributionFloat StartVelocityRadial;

	virtual void Spawn(const FSpawnContext& Context) override;
};

UCLASS()
class UParticleModuleColor : public UParticleModule
{
	GENERATED_BODY()

public:
	UParticleModuleColor();

	FRawDistributionVector StartColor;
	FRawDistributionFloat StartAlpha;

	UPROPERTY(EditAnywhere)
	bool bClampAlpha = true;

	virtual void Spawn(const FSpawnContext& Context) override;
	virtual void CompileModule(struct FParticleEmitterBuildInfo& EmitterInfo) override;
};