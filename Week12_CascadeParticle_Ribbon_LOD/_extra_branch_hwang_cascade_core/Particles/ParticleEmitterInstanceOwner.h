#pragma once

#include "Containers/Array.h"

class UObject;
class UWorld;
class FPrimitiveSceneProxy;
class UParticleSystemComponent;
struct FTransform;
struct FRotator;
struct FBoxSphereBounds;
struct FVector;
struct FRandomStream;

class IParticleEmitterInstanceOwner
{
public:
	virtual const FTransform& GetAsyncComponentToWorld() const = 0;

	virtual UObject* GetDistributionData() const = 0;
	virtual const FTransform& GetComponentTransform() const = 0;
	virtual FRotator GetComponentRotation() const = 0;
	virtual const FTransform& GetComponentToWorld() const = 0;
	virtual const FBoxSphereBounds& GetBounds() const = 0;
	virtual UWorld* GetWeakWorld() const = 0;
	virtual bool HasWorld() const = 0;
	virtual bool HasWorldSettings() const = 0;
	virtual bool IsGameWorld() const = 0;
	virtual float GetWorldTimeSeconds() const = 0;
	virtual float GetWorldEffectiveTimeDilation() const = 0;
	virtual FVector GetWorldOriginLocation() const = 0;	
	virtual bool IsActive() const = 0;
	virtual bool IsValidLowLevel() const = 0;
	virtual int32 GetCurrentDetailMode() const = 0;
	virtual int32 GetCurrentLODIndex() const = 0;
	virtual const FVector& GetPartSysVelocity() const = 0;
	virtual const FVector& GetOldPosition() const = 0;
	virtual FPrimitiveSceneProxy* GetSceneProxy() const = 0;
	virtual float GetEmitterDelay() const = 0;
	virtual FRandomStream& GetRandomStream() = 0;

	virtual void SetComponentToWorld(const FTransform& NewComponentToWorld) = 0;	

	virtual UParticleSystemComponent* AsComponent() const = 0;

protected:
	virtual ~IParticleEmitterInstanceOwner() = default;
};