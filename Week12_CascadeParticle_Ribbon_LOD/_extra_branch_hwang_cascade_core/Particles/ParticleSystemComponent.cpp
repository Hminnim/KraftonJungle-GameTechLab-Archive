#include "EnginePCH.h"
#include "Engine/Classes/Particles/ParticleSystemComponent.h"

#include "Asset/AssetManager.h"
#include "Engine/PrimitiveSceneProxy.h"
#include "Engine/World.h"
#include "Engine/Classes/Distributions/Distribution.h"
#include "Engine/Classes/Particles/ParticleEmitter.h"
#include "Engine/Classes/Particles/ParticleEmitterInstances.h"
#include "Engine/Classes/Particles/ParticleHelper.h"
#include "Engine/Classes/Particles/ParticleLODLevel.h"
#include "Engine/Classes/Particles/ParticleModule.h"
#include "Engine/Classes/Particles/ParticleSystem.h"
#include "Render/Material.h"
#include "Render/RenderPacket.h"
#include "UObject/UObjectGlobals.h"

UParticleSystemComponent::UParticleSystemComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	QuadMesh = UAssetManager::GetAssetByPath<UStaticMesh>("ParticleQuad");
	DefaultMaterial = UAssetManager::GetAssetByPath<UMaterial>("SubUVMaterial");
	RandomStream.Initialize(FMath::Rand());
}

UParticleSystemComponent::~UParticleSystemComponent()
{
	ClearEmitterInstances();
}

void UParticleSystemComponent::OnRegister()
{
	Super::OnRegister();

	if (IsAutoActivate() && !IsActive())
	{
		Activate(true);
	}

	if (!bSystemInitialized && IsAutoActivate())
	{
		InitializeSystem();
	}
}

void UParticleSystemComponent::BeginPlay()
{
	Super::BeginPlay();

	if (IsAutoActivate() && !IsActive())
	{
		Activate(true);
	}

	if (!bSystemInitialized && IsAutoActivate())
	{
		InitializeSystem();
	}
}

void UParticleSystemComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);

	if (DeltaTime <= 0.0f)
	{
		return;
	}

	AccumulatedTimeSeconds += DeltaTime;
	OldPosition = CachedWorldTransform.Location;
	SyncCachedWorldTransform();

	if (!IsActive())
	{
		return;
	}

	if (!bSystemInitialized)
	{
		InitializeSystem();
	}

	if (EmitterInstances.IsEmpty())
	{
		return;
	}

	if (!bHasWarmedUp && Template && Template->WarmupTime > 0.0f)
	{
		const float WarmupStep = (Template->WarmupTickRate > 0.0f) ? Template->WarmupTickRate : (1.0f / 30.0f);
		float RemainingWarmup = Template->WarmupTime;
		while (RemainingWarmup > 0.0f)
		{
			const float Step = FMath::Min(WarmupStep, RemainingWarmup);
			for (FParticleEmitterInstance* Instance : EmitterInstances)
			{
				if (Instance)
				{
					Instance->Tick(Step, bSuppressSpawning);
				}
			}
			RemainingWarmup -= Step;
		}
		bHasWarmedUp = true;
	}

	bool bAnyActiveOrDirty = false;
	for (FParticleEmitterInstance* Instance : EmitterInstances)
	{
		if (!Instance)
		{
			continue;
		}

		Instance->Tick(DeltaTime, bSuppressSpawning);
		if (Instance->ActiveParticles > 0 || Instance->IsRenderDataDirty != 0)
		{
			bAnyActiveOrDirty = true;
		}
	}

	if (bAnyActiveOrDirty)
	{
		MarkTransformDirty();
	}
}

void UParticleSystemComponent::OnTransformDirty()
{
	SyncCachedWorldTransform();
	Super::OnTransformDirty();
}

FBox UParticleSystemComponent::CalcLocalBounds() const
{
	if (Template && Template->bUseFixedRelativeBoundingBox)
	{
		return Template->FixedRelativeBoundingBox;
	}

	bool bHasBounds = false;
	FVector WorldMin(FLT_MAX, FLT_MAX, FLT_MAX);
	FVector WorldMax(-FLT_MAX, -FLT_MAX, -FLT_MAX);

	for (const FParticleEmitterInstance* Instance : EmitterInstances)
	{
		if (!Instance || Instance->ActiveParticles <= 0)
		{
			continue;
		}

		WorldMin.X = FMath::Min(WorldMin.X, Instance->ParticleBoundingBox.Min.X);
		WorldMin.Y = FMath::Min(WorldMin.Y, Instance->ParticleBoundingBox.Min.Y);
		WorldMin.Z = FMath::Min(WorldMin.Z, Instance->ParticleBoundingBox.Min.Z);
		WorldMax.X = FMath::Max(WorldMax.X, Instance->ParticleBoundingBox.Max.X);
		WorldMax.Y = FMath::Max(WorldMax.Y, Instance->ParticleBoundingBox.Max.Y);
		WorldMax.Z = FMath::Max(WorldMax.Z, Instance->ParticleBoundingBox.Max.Z);
		bHasBounds = true;
	}

	if (!bHasBounds)
	{
		return FBox{ FVector(-1.0f, -1.0f, -1.0f), FVector(1.0f, 1.0f, 1.0f) };
	}

	const FBox WorldBox{ WorldMin, WorldMax };
	return WorldBox.GetWorldAABB(GetWorldMatrix().Inverse());
}

FPrimitiveSceneProxy* UParticleSystemComponent::CreateSceneProxy()
{
	return new FParticleSystemSceneProxy(this);
}

void UParticleSystemComponent::SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext)
{
	if (!QuadMesh)
	{
		QuadMesh = UAssetManager::GetAssetByPath<UStaticMesh>("ParticleQuad");
	}
	if (!DefaultMaterial)
	{
		DefaultMaterial = UAssetManager::GetAssetByPath<UMaterial>("SubUVMaterial");
	}

	if (!QuadMesh || EmitterInstances.IsEmpty())
	{
		return;
	}

	int32 TotalActiveParticles = 0;
	for (const FParticleEmitterInstance* Instance : EmitterInstances)
	{
		if (Instance && Instance->bEnabled && Instance->ActiveParticles > 0)
		{
			TotalActiveParticles += Instance->ActiveParticles;
		}
	}

	if (TotalActiveParticles <= 0)
	{
		return;
	}

	ParticleConstants.Reset();
	ParticleConstants.Reserve(static_cast<uint32>(TotalActiveParticles));

	for (const FParticleEmitterInstance* Instance : EmitterInstances)
	{
		if (!Instance || !Instance->bEnabled || Instance->ActiveParticles <= 0 || !Instance->ParticleData || !Instance->ParticleIndices)
		{
			continue;
		}

		UMaterial* PacketMaterial = Cast<UMaterial>(Instance->CurrentMaterial);
		if (!PacketMaterial)
		{
			PacketMaterial = DefaultMaterial;
		}
		if (!PacketMaterial)
		{
			continue;
		}

		const UParticleLODLevel* LODLevel = Instance->CurrentLODLevel;
		const UParticleModuleRequired* RequiredModule = LODLevel ? LODLevel->RequiredModule : nullptr;
		const bool bUseLocalSpace = RequiredModule ? RequiredModule->bUseLocalSpace : false;

		const int32 SubH = (RequiredModule && RequiredModule->SubImages_Horizontal > 0) ? RequiredModule->SubImages_Horizontal : 1;
		const int32 SubV = (RequiredModule && RequiredModule->SubImages_Vertical > 0) ? RequiredModule->SubImages_Vertical : 1;
		const int32 TotalFrames = SubH * SubV;
		const bool bAnimateSubUV = RequiredModule && (RequiredModule->InterpolationMethod != EParticleSubUVInterpMethod::PSUVIM_None) && (TotalFrames > 1);

		const int32 MaxDraw = (RequiredModule && RequiredModule->MaxDrawCount > 0)
			? FMath::Min(Instance->ActiveParticles, RequiredModule->MaxDrawCount)
			: Instance->ActiveParticles;

		for (int32 i = 0; i < MaxDraw; ++i)
		{
			const uint16 ParticleIndex = Instance->ParticleIndices[i];
			const FBaseParticle& Particle = *reinterpret_cast<const FBaseParticle*>(
				Instance->ParticleData + static_cast<size_t>(ParticleIndex) * static_cast<size_t>(Instance->ParticleStride));

			const FVector WorldPos = bUseLocalSpace ? Instance->SimulationToWorld.TransformPosition(Particle.Location) : Particle.Location;
			const float Width = (Particle.Size.X > 0.0f) ? Particle.Size.X : 1.0f;
			const float Height = (Particle.Size.Y > 0.0f) ? Particle.Size.Y : Width;

			FSubUVConstants Consts{};
			if (bAnimateSubUV)
			{
				const float ClampedLife = FMath::Clamp(Particle.RelativeTime, 0.0f, 0.9999f);
				Consts.CurrentFrame = static_cast<float>(static_cast<int32>(ClampedLife * static_cast<float>(TotalFrames)));
			}
			else
			{
				Consts.CurrentFrame = 0.0f;
			}
			Consts.AtlasColSize = static_cast<float>(SubH);
			Consts.AtlasRowSize = static_cast<float>(SubV);
			Consts.Alpha = Particle.Color.A;
			Consts.Tint = FVector4(Particle.Color.R, Particle.Color.G, Particle.Color.B, 1.0f);
			Consts.DecodeSRGB = 0.0f;
			ParticleConstants.Add(Consts);

			const FMatrix BillboardMatrix = ViewContext.BuildBillboardMatrix(WorldPos, Width, Height);
			const FVector Delta = WorldPos - ViewContext.CameraPosition;

			FRenderPacket Packet;
			Packet.Model = RenderQueue.StoreWorldMatrix(BillboardMatrix);
			Packet.Mesh = QuadMesh;
			Packet.Material = PacketMaterial;
			Packet.CameraToParticleDistance = Delta.Dot(Delta);
			Packet.MaterialParamData = &ParticleConstants[ParticleConstants.Num() - 1];
			Packet.MaterialParamDataSize = sizeof(FSubUVConstants);
			RenderQueue.Add(Packet);
		}
	}
}

void UParticleSystemComponent::SetTemplate(UParticleSystem* NewTemplate)
{
	if (Template == NewTemplate && bSystemInitialized)
	{
		return;
	}

	Template = NewTemplate;
	InitializeSystem();
}

void UParticleSystemComponent::InitializeSystem()
{
	ClearEmitterInstances();
	SyncCachedWorldTransform();
	OldPosition = CachedWorldTransform.Location;

	if (!QuadMesh)
	{
		QuadMesh = UAssetManager::GetAssetByPath<UStaticMesh>("ParticleQuad");
	}
	if (!DefaultMaterial)
	{
		DefaultMaterial = UAssetManager::GetAssetByPath<UMaterial>("SubUVMaterial");
	}

	if (!Template && bAutoCreateDefaultTemplate)
	{
		Template = CreateDefaultParticleSystem(this);
	}

	if (!Template)
	{
		bSystemInitialized = false;
		return;
	}

	Template->BuildEmitters();

	for (int32 EmitterIdx = 0; EmitterIdx < Template->Emitters.Num(); ++EmitterIdx)
	{
		UParticleEmitter* Emitter = Template->Emitters[EmitterIdx];
		if (!Emitter)
		{
			continue;
		}

		FParticleEmitterInstance* Instance = Emitter->CreateInstance(*this);
		if (Instance)
		{
			Instance->Init();
			EmitterInstances.Add(Instance);
		}
	}

	bSystemInitialized = true;
	bHasWarmedUp = false;
}

void UParticleSystemComponent::ActivateSystem(bool bFlagAsJustAttached)
{
	Activate(bFlagAsJustAttached);
	bSuppressSpawning = false;
	if (!bSystemInitialized || bFlagAsJustAttached)
	{
		InitializeSystem();
	}
}

void UParticleSystemComponent::DeactivateSystem()
{
	bSuppressSpawning = true;
	Deactivate();
}

void UParticleSystemComponent::ResetParticles(bool bEmptyInstances)
{
	if (bEmptyInstances)
	{
		ClearEmitterInstances();
		bSystemInitialized = false;
		return;
	}

	for (FParticleEmitterInstance* Instance : EmitterInstances)
	{
		if (Instance)
		{
			Instance->Init();
		}
	}
	bHasWarmedUp = false;
}

UParticleSystem* UParticleSystemComponent::CreateDefaultParticleSystem(UObject* InOuter)
{
	UParticleSystem* System = NewObject<UParticleSystem>(InOuter);
	UParticleEmitter* Emitter = NewObject<UParticleEmitter>(System);
	Emitter->EmitterName = FName("DefaultSpriteEmitter");

	UParticleLODLevel* LOD0 = NewObject<UParticleLODLevel>(Emitter);
	LOD0->Level = 0;
	LOD0->bEnabled = true;

	UParticleModuleRequired* Required = NewObject<UParticleModuleRequired>(LOD0);
	Required->Material = UAssetManager::GetAssetByPath<UMaterial>("SubUVMaterial");
	Required->EmitterDuration = 1.0f;
	Required->EmitterLoops = 0;
	Required->SubImages_Horizontal = 8;
	Required->SubImages_Vertical = 8;
	Required->InterpolationMethod = EParticleSubUVInterpMethod::PSUVIM_Linear;

	UParticleModuleSpawn* Spawn = NewObject<UParticleModuleSpawn>(LOD0);
	UDistributionFloatConstant* RateDist = NewObject<UDistributionFloatConstant>(Spawn);
	RateDist->Constant = 12.0f;
	Spawn->Rate.Distribution = RateDist;

	UParticleModuleLifetime* Lifetime = NewObject<UParticleModuleLifetime>(LOD0);
	UDistributionFloatUniform* LifeDist = NewObject<UDistributionFloatUniform>(Lifetime);
	LifeDist->Min = 1.5f;
	LifeDist->Max = 2.5f;
	Lifetime->LifeTime.Distribution = LifeDist;

	UParticleModuleSize* Size = NewObject<UParticleModuleSize>(LOD0);
	UDistributionVectorUniform* SizeDist = NewObject<UDistributionVectorUniform>(Size);
	SizeDist->Min = FVector(0.6f, 0.6f, 0.6f);
	SizeDist->Max = FVector(1.2f, 1.2f, 1.2f);
	SizeDist->bLockAxes = true;
	Size->StartSize.Distribution = SizeDist;

	UParticleModuleVelocity* Velocity = NewObject<UParticleModuleVelocity>(LOD0);
	UDistributionVectorUniform* VelDist = NewObject<UDistributionVectorUniform>(Velocity);
	VelDist->Min = FVector(-0.5f, -0.5f, 1.5f);
	VelDist->Max = FVector(0.5f, 0.5f, 3.0f);
	Velocity->StartVelocity.Distribution = VelDist;

	UParticleModuleColor* Color = NewObject<UParticleModuleColor>(LOD0);
	UDistributionVectorConstant* ColorDist = NewObject<UDistributionVectorConstant>(Color);
	ColorDist->Constant = FVector(1.0f, 0.85f, 0.5f);
	Color->StartColor.Distribution = ColorDist;
	UDistributionFloatConstant* AlphaDist = NewObject<UDistributionFloatConstant>(Color);
	AlphaDist->Constant = 1.0f;
	Color->StartAlpha.Distribution = AlphaDist;

	LOD0->RequiredModule = Required;
	LOD0->SpawnModule = Spawn;
	LOD0->Modules.Add(Lifetime);
	LOD0->Modules.Add(Size);
	LOD0->Modules.Add(Velocity);
	LOD0->Modules.Add(Color);

	Emitter->LODLevels.Add(LOD0);
	System->Emitters.Add(Emitter);
	return System;
}

void UParticleSystemComponent::ClearEmitterInstances()
{
	for (FParticleEmitterInstance* Instance : EmitterInstances)
	{
		delete Instance;
	}
	EmitterInstances.Empty();
}

void UParticleSystemComponent::SyncCachedWorldTransform() const
{
	CachedWorldTransform.Location = GetWorldLocation();
	CachedWorldTransform.Rotation = GetWorldRotation();
	CachedWorldTransform.Scale = GetWorldScale3D();
}

const FTransform& UParticleSystemComponent::GetAsyncComponentToWorld() const
{
	SyncCachedWorldTransform();
	return CachedWorldTransform;
}

UObject* UParticleSystemComponent::GetDistributionData() const
{
	return const_cast<UParticleSystemComponent*>(this);
}

const FTransform& UParticleSystemComponent::GetComponentTransform() const
{
	return GetTransform();
}

FRotator UParticleSystemComponent::GetComponentRotation() const
{
	return GetWorldRotation();
}

const FTransform& UParticleSystemComponent::GetComponentToWorld() const
{
	SyncCachedWorldTransform();
	return CachedWorldTransform;
}

const FBoxSphereBounds& UParticleSystemComponent::GetBounds() const
{
	const FBox WorldBox = CalcBounds();
	CachedBounds.Origin = (WorldBox.Min + WorldBox.Max) * 0.5f;
	CachedBounds.BoxExtent = (WorldBox.Max - WorldBox.Min) * 0.5f;
	CachedBounds.SphereRadius = CachedBounds.BoxExtent.Length();
	return CachedBounds;
}

UWorld* UParticleSystemComponent::GetWeakWorld() const
{
	return GetWorld();
}

bool UParticleSystemComponent::HasWorld() const
{
	return GetWorld() != nullptr;
}

bool UParticleSystemComponent::HasWorldSettings() const
{
	return GetWorld() != nullptr;
}

bool UParticleSystemComponent::IsGameWorld() const
{
	const UWorld* World = GetWorld();
	return World ? World->IsGameWorld() : false;
}

float UParticleSystemComponent::GetWorldTimeSeconds() const
{
	return AccumulatedTimeSeconds;
}

float UParticleSystemComponent::GetWorldEffectiveTimeDilation() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetEffectiveTimeDilation() : 1.0f;
}

FVector UParticleSystemComponent::GetWorldOriginLocation() const
{
	return FVector::ZeroVector;
}

bool UParticleSystemComponent::IsActive() const
{
	return UActorComponent::IsActive();
}

bool UParticleSystemComponent::IsValidLowLevel() const
{
	return true;
}

int32 UParticleSystemComponent::GetCurrentDetailMode() const
{
	return 0;
}

int32 UParticleSystemComponent::GetCurrentLODIndex() const
{
	return LODLevelIndex;
}

const FVector& UParticleSystemComponent::GetPartSysVelocity() const
{
	return ComponentVelocity;
}

const FVector& UParticleSystemComponent::GetOldPosition() const
{
	return OldPosition;
}

FPrimitiveSceneProxy* UParticleSystemComponent::GetSceneProxy() const
{
	return UPrimitiveComponent::GetSceneProxy();
}

float UParticleSystemComponent::GetEmitterDelay() const
{
	return 0.0f;
}

FRandomStream& UParticleSystemComponent::GetRandomStream()
{
	return RandomStream;
}

void UParticleSystemComponent::SetComponentToWorld(const FTransform& NewComponentToWorld)
{
	SetWorldLocationAndRotation(NewComponentToWorld.Location, NewComponentToWorld.Rotation, false);
	SetWorldScale3D(NewComponentToWorld.Scale);
	SyncCachedWorldTransform();
}

UParticleSystemComponent* UParticleSystemComponent::AsComponent() const
{
	return const_cast<UParticleSystemComponent*>(this);
}
