#pragma once

#include "UObject/Object.h"
#include "Components/SceneComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/BillboardComponent.h"
#include "Render/MaterialInterface.h"
#include "ParticleSystem.h"
#include "ParticleEmitter.h"
#include "ParticleEmitterInstanceOwner.h"
#include "Math/BoxSphereBounds.h"
#include "Math/RandomStream.h"
#include "ParticleSystemComponent.generated.h"

class UMaterial;
class UParticleSystem;
class UStaticMesh;
struct FParticleEmitterInstance;

UCLASS()
class UParticleSystemComponent : public UPrimitiveComponent, public IParticleEmitterInstanceOwner
{
	GENERATED_BODY()

public:
	UParticleSystemComponent();
	virtual ~UParticleSystemComponent() override;

	UPROPERTY(EditAnywhere)
	UParticleSystem* Template = nullptr;

	TArray<FParticleEmitterInstance*> EmitterInstances;
	FRandomStream RandomStream;
	UStaticMesh* QuadMesh = nullptr;

	UPROPERTY(EditAnywhere)
	bool bSuppressSpawning = false;

	UPROPERTY(EditAnywhere)
	bool bAutoCreateDefaultTemplate = true;

	UPROPERTY(EditAnywhere)
	int32 LODLevelIndex = 0;

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime) override;
	virtual void OnTransformDirty() override;

	virtual FBox CalcLocalBounds() const override;
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual void SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext) override;
	virtual UStaticMesh* GetRenderMesh() const override { return nullptr; }

	UStaticMesh* GetQuadMesh() const { return QuadMesh; }

	void SetTemplate(UParticleSystem* NewTemplate);
	UParticleSystem* GetTemplate() const { return Template; }

	void InitializeSystem();
	bool IsSystemInitialized() const { return bSystemInitialized; }
	void ActivateSystem(bool bFlagAsJustAttached = false);
	void DeactivateSystem();
	void ResetParticles(bool bEmptyInstances = false);

	static UParticleSystem* CreateDefaultParticleSystem(UObject* InOuter);

//
// IParticleEmitterInstanceOwner Interfaces
//
public:
	virtual const FTransform& GetAsyncComponentToWorld() const override;

	virtual UObject* GetDistributionData() const override;
	virtual const FTransform& GetComponentTransform() const override;
	virtual FRotator GetComponentRotation() const override;
	virtual const FTransform& GetComponentToWorld() const override;
	virtual const FBoxSphereBounds& GetBounds() const override;
	virtual UWorld* GetWeakWorld() const override;
	virtual bool HasWorld() const override;
	virtual bool HasWorldSettings() const override;
	virtual bool IsGameWorld() const override;
	virtual float GetWorldTimeSeconds() const override;
	virtual float GetWorldEffectiveTimeDilation() const override;
	virtual FVector GetWorldOriginLocation() const override;
	virtual bool IsActive() const override;
	virtual bool IsValidLowLevel() const override;
	virtual int32 GetCurrentDetailMode() const override;
	virtual int32 GetCurrentLODIndex() const override;
	virtual const FVector& GetPartSysVelocity() const override;
	virtual const FVector& GetOldPosition() const override;
	virtual FPrimitiveSceneProxy* GetSceneProxy() const override;
	virtual float GetEmitterDelay() const override;
	virtual FRandomStream& GetRandomStream() override;

	virtual void SetComponentToWorld(const FTransform& NewComponentToWorld) override;

	virtual UParticleSystemComponent* AsComponent() const override;

protected:
	virtual void OnRegister() override;

private:
	void ClearEmitterInstances();
	void SyncCachedWorldTransform() const;

	UMaterial* DefaultMaterial = nullptr;
	TArray<FSubUVConstants> ParticleConstants;

	mutable FTransform CachedWorldTransform;
	mutable FBoxSphereBounds CachedBounds{ FVector::ZeroVector, FVector(1.0f, 1.0f, 1.0f), 1.73205f };
	FVector OldPosition = FVector::ZeroVector;
	float AccumulatedTimeSeconds = 0.0f;
	bool bSystemInitialized = false;
	bool bHasWarmedUp = false;
};
