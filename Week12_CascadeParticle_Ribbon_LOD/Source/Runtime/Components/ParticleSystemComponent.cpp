#include "EnginePCH.h"
#include "ParticleSystemComponent.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/ParticleModule.h"
#include "Engine/PrimitiveSceneProxy.h"
#include "Engine/World.h"
#include "Camera/CameraActor.h"
#include "Render/Material.h"
#include "Render/RenderCommand.h"
#include "Asset/AssetManager.h"
#include "Core/ResourcePaths.h"
#include "Core/Stats/LightweightStats.h"
#include "Particles/ParticleStats.h"
#include <chrono>
#include <cmath>

// stat profile에 나타나는 구간
DECLARE_CYCLE_STAT("Particle Simulation", STAT_ParticleSimulation);
DECLARE_CYCLE_STAT("Particle Bounds", STAT_ParticleBounds);
DECLARE_CYCLE_STAT("Particle Submit", STAT_ParticleSubmit);

UParticleSystemComponent::UParticleSystemComponent()
{
	// 파티클 시뮬레이션을 매 프레임 진행한다
	PrimaryComponentTick.bCanEverTick = true;
	// 파티클은 그림자를 만들거나 받지 않는다
	bCastShadow = false;
	bReceiveShadow = false;
	bReceiveDecal = false;
	// 겹침 이벤트를 쓰지 않는다 (경계가 매 프레임 바뀌는 컴포넌트가 겹침 검사 대상에서 빠지게 한다)
	bGenerateOverlapEvents = false;
}

UParticleSystemComponent::~UParticleSystemComponent()
{
	DestroyInstances();
}

void UParticleSystemComponent::OnUnregister()
{
	// 월드에서 빠지면 시뮬레이션 상태를 버린다. 다시 등록되면 처음부터 시작한다.
	DestroyInstances();
	Super::OnUnregister();
}

void UParticleSystemComponent::SetTemplate(UParticleSystem* InTemplate)
{
	if (Template == InTemplate)
	{
		return;
	}
	// 인스턴스가 이전 에셋의 이미터를 가리키므로 먼저 없앤다
	DestroyInstances();
	Template = InTemplate;
	if (Template && Template->GetPath().empty())
	{
		HTR_LOG(Warning, "[Particle] Template has no asset path: it will not be restored when the scene is saved and loaded");
	}
}

void UParticleSystemComponent::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);
	// 에디터에서 에셋이나 시드를 바꾸면 인스턴스가 이전 값으로 만들어져 있으므로 다시 만든다
	if (Property.Name == "Template" || Property.Name == "RandomSeed")
	{
		DestroyInstances();
	}
}

void UParticleSystemComponent::ResetSystem()
{
	DestroyInstances();
	bEmitting = true;
}

void UParticleSystemComponent::StopEmitting()
{
	bEmitting = false;
	for (FParticleEmitterInstance* Instance : EmitterInstances)
	{
		Instance->bHaltSpawning = true;
	}
}

void UParticleSystemComponent::StartEmitting()
{
	bEmitting = true;
	for (FParticleEmitterInstance* Instance : EmitterInstances)
	{
		Instance->bHaltSpawning = false;
	}
}

bool UParticleSystemComponent::HasCompleted() const
{
	for (const FParticleEmitterInstance* Instance : EmitterInstances)
	{
		if (!Instance->HasCompleted())
		{
			return false;
		}
	}
	return true;
}

int32 UParticleSystemComponent::GetActiveParticleCount() const
{
	int32 Total = 0;
	for (const FParticleEmitterInstance* Instance : EmitterInstances)
	{
		Total += Instance->ActiveParticles;
	}
	return Total;
}

EParticleSystemLODMethod UParticleSystemComponent::GetEffectiveLODMethod() const
{
	return bOverrideLODMethod ? LODMethod : (Template ? Template->LODMethod : EParticleSystemLODMethod::Automatic);
}

int32 UParticleSystemComponent::DetermineLODLevelForLocation(const FVector& EffectLocation, const FVector& CameraLocation) const
{
	if (!Template)
	{
		return 0;
	}
	const FVector Delta = EffectLocation - CameraLocation;
	return Template->GetLODLevelForDistance(Delta.Length());
}

void UParticleSystemComponent::SetLODLevel(int32 InLODLevel)
{
	const int32 MaxLOD = (Template && Template->GetNumLODLevels() > 0) ? (Template->GetNumLODLevels() - 1) : 0;
	LODLevel = std::clamp(InLODLevel, 0, MaxLOD);
	for (FParticleEmitterInstance* Instance : EmitterInstances)
	{
		if (Instance)
		{
			Instance->SetCurrentLODIndex(LODLevel);
		}
	}
}

bool UParticleSystemComponent::TryGetCameraPosition(FVector& OutCameraPosition) const
{
	if (bHasLastCameraPosition)
	{
		OutCameraPosition = LastCameraPosition;
		return true;
	}
	if (UWorld* MyWorld = GetWorld())
	{
		if (ACameraActor* MainCam = MyWorld->GetMainCamera())
		{
			OutCameraPosition = MainCam->GetActorLocation();
			return true;
		}
	}
	return false;
}

void UParticleSystemComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);

	ComponentTimeSeconds += DeltaTime;

	// 지난 Tick의 충돌 이벤트는 이제 버린다 (그 전까지는 GetCollisionEvents로 읽을 수 있다)
	CollisionEvents.Reset();
	DroppedCollisionEvents = 0;

	EnsureInstances();
	if (EmitterInstances.IsEmpty())
	{
		return;
	}

	if (Template && Template->GetNumLODLevels() > 1 && GetEffectiveLODMethod() == EParticleSystemLODMethod::Automatic)
	{
		TimeSinceLastLODUpdate += DeltaTime;
		const float CheckTime = std::max(0.0f, Template->LODDistanceCheckTime);
		if (TimeSinceLastLODUpdate >= CheckTime)
		{
			TimeSinceLastLODUpdate = 0.0f;
			FVector CamPos;
			if (TryGetCameraPosition(CamPos))
			{
				SetLODLevel(DetermineLODLevelForLocation(GetWorldLocation(), CamPos));
			}
		}
	}

	using FClock = std::chrono::steady_clock;
	const auto SimStart = FClock::now();
	const FMatrix ComponentToWorld = GetComponentToWorldNoScale();
	const bool bCollectStats = FParticleStats::IsCollecting();
	FParticleFrameStats& FrameStats = FParticleStats::Pending();
	if (bCollectStats)
	{
		++FrameStats.Components;
		FrameStats.EmitterInstances += static_cast<uint32>(EmitterInstances.Num());
	}
	{
		SCOPE_CYCLE_COUNTER(STAT_ParticleSimulation);
		for (FParticleEmitterInstance* Instance : EmitterInstances)
		{
			Instance->SetComponentToWorld(ComponentToWorld);
			const int32 ActiveBefore = Instance->ActiveParticles;
			const uint32 CounterBefore = Instance->ParticleCounter;
			Instance->Tick(DeltaTime);
			if (bCollectStats)
			{
				// 생성 수는 ParticleCounter의 증가분이고, 죽은 수는 (이전 활성 + 생성 - 현재 활성)이다
				const uint32 Spawned = Instance->ParticleCounter - CounterBefore;
				const int32 Killed = ActiveBefore + static_cast<int32>(Spawned) - Instance->ActiveParticles;
				FrameStats.Spawned += Spawned;
				FrameStats.Killed += static_cast<uint32>(std::max(0, Killed));
				FrameStats.ActiveParticles += static_cast<uint64>(std::max(0, Instance->ActiveParticles));
			}
		}
	}
	const auto SimEnd = FClock::now();
	{
		SCOPE_CYCLE_COUNTER(STAT_ParticleBounds);
		UpdateParticleBounds();
	}
	const auto BoundsEnd = FClock::now();
	Timings.SimulationMicroseconds = std::chrono::duration<double, std::micro>(SimEnd - SimStart).count();
	Timings.BoundsMicroseconds = std::chrono::duration<double, std::micro>(BoundsEnd - SimEnd).count();

	// 충돌 이벤트 전달. 구독자가 큐를 바꾸거나 컴포넌트를 건드려도 순회가 깨지지 않게 복사본을 돈다.
	if (!CollisionEvents.IsEmpty() && OnParticleCollide.IsBound())
	{
		const TArray<FParticleCollisionEvent> Snapshot = CollisionEvents;
		for (const FParticleCollisionEvent& Event : Snapshot)
		{
			OnParticleCollide.Broadcast(this, Event);
		}
	}
}

void UParticleSystemComponent::NoteRendered(const FVector& CameraPosition)
{
	LastCameraPosition = CameraPosition;
	bHasLastCameraPosition = true;
	LastRenderComponentTime = ComponentTimeSeconds;
	bHasBeenRendered = true;
}

bool UParticleSystemComponent::WasRecentlyRendered(float Tolerance) const
{
	return bHasBeenRendered && (ComponentTimeSeconds - LastRenderComponentTime) <= static_cast<double>(Tolerance);
}

void UParticleSystemComponent::ReportCollisionEvent(const FParticleEmitterInstance& Source, FParticleCollisionEvent Event)
{
	if (CollisionEvents.Num() >= MaxCollisionEventsPerTick)
	{
		++DroppedCollisionEvents;
		if (FParticleStats::IsCollecting()) ++FParticleStats::Pending().CollisionEventsDropped;
		return;
	}
	Event.EmitterIndex = -1;
	for (int32 i = 0; i < EmitterInstances.Num(); ++i)
	{
		if (EmitterInstances[i] == &Source)
		{
			Event.EmitterIndex = i;
			break;
		}
	}
	CollisionEvents.Add(Event);
	if (FParticleStats::IsCollecting()) ++FParticleStats::Pending().CollisionEvents;
}

void UParticleSystemComponent::EnsureInstances()
{
	if (!EmitterInstances.IsEmpty() || !Template)
	{
		return;
	}

	Template->SyncLODDistances();
	const EParticleSystemLODMethod Method = GetEffectiveLODMethod();
	if (Method == EParticleSystemLODMethod::Automatic || Method == EParticleSystemLODMethod::ActivateAutomatic)
	{
		FVector CamPos;
		if (TryGetCameraPosition(CamPos))
		{
			LODLevel = DetermineLODLevelForLocation(GetWorldLocation(), CamPos);
		}
		else
		{
			LODLevel = 0;
		}
	}
	else
	{
		const int32 MaxLOD = Template->GetNumLODLevels() > 0 ? (Template->GetNumLODLevels() - 1) : 0;
		LODLevel = std::clamp(LODLevel, 0, MaxLOD);
	}
	TimeSinceLastLODUpdate = 0.0f;

	for (int32 EmitterIndex = 0; EmitterIndex < Template->GetNumEmitters(); ++EmitterIndex)
	{
		UParticleEmitter* Emitter = Template->GetEmitter(EmitterIndex);
		if (!Emitter || !Emitter->GetLODLevel(0))
		{
			continue;
		}
		// 모듈 구성이 바뀌었을 수 있으므로 레이아웃 캐시를 다시 계산한다 (싸고 멱등하다)
		Emitter->CacheEmitterModuleInfo();

		// TypeData가 있으면 그 종류의 인스턴스(Beam, Mesh, Ribbon)를, 없으면 Sprite 인스턴스를 만든다 (UE: TypeDataModule->CreateInstance)
		UParticleModuleTypeDataBase* TypeData = Emitter->GetLODLevel(0)->TypeDataModule;
		FParticleEmitterInstance* Instance = TypeData ? TypeData->CreateInstance() : new FParticleEmitterInstance();
		if (!Instance)
		{
			continue;
		}
		if (!Instance->Init(Emitter, GetInstanceSeed(EmitterIndex)))
		{
			delete Instance;
			continue;
		}
		Instance->OwnerComponent = this;
		Instance->SetCurrentLODIndex(LODLevel);
		Instance->bHaltSpawning = !bEmitting;
		EmitterInstances.Add(Instance);
	}
}

void UParticleSystemComponent::DestroyInstances()
{
	// 렌더 자원은 인스턴스와 같은 인덱스로 짝지어져 있으므로 함께 지운다
	DestroyRenderData();
	// 인스턴스가 없으면 덮을 파티클도 없다. 프록시는 다음 갱신에서 빈 상자로 돌아온다.
	ParticleLocalBounds = FBox{ FVector(0.0f, 0.0f, 0.0f), FVector(0.0f, 0.0f, 0.0f) };
	for (FParticleEmitterInstance* Instance : EmitterInstances)
	{
		delete Instance;
	}
	EmitterInstances.Empty();
}

FMatrix UParticleSystemComponent::GetComponentToWorldNoScale() const
{
	FMatrix Matrix = GetWorldMatrix();
	// 행 벡터 규약이라 처음 세 행이 축이다. 각 축을 단위 길이로 만들어 스케일을 뺀다.
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const float Length = std::sqrt(Matrix.M[Axis][0] * Matrix.M[Axis][0] + Matrix.M[Axis][1] * Matrix.M[Axis][1] + Matrix.M[Axis][2] * Matrix.M[Axis][2]);
		if (Length > 1e-8f)
		{
			Matrix.M[Axis][0] /= Length;
			Matrix.M[Axis][1] /= Length;
			Matrix.M[Axis][2] /= Length;
		}
	}
	return Matrix;
}

uint32 UParticleSystemComponent::GetInstanceSeed(int32 EmitterIndex) const
{
	// 이미터마다 다른 값이 나오도록 인덱스를 섞는다. 0이면 컴포넌트마다 다른 UUID를 기반으로 한다.
	const uint32 Base = RandomSeed != 0 ? RandomSeed : (GetUUID() * 2654435761u + 1u);
	return Base ^ (static_cast<uint32>(EmitterIndex) * 0x9E3779B1u);
}

// ---------------------------------------------------------------------------
// 렌더
// ---------------------------------------------------------------------------

namespace
{
	// 컬링과 BVH가 쓰는 프록시. 그릴 메시가 없어서 정적 메시 경로에 들어가지 않고 컴포넌트의 SubmitToRenderQueue로만 그려진다.
	class FParticleSystemSceneProxy : public FPrimitiveSceneProxy
	{
	public:
		explicit FParticleSystemSceneProxy(UParticleSystemComponent* InComponent) : FPrimitiveSceneProxy(InComponent) {}

		void UpdateRenderState() override
		{
			RenderState.reset();
			bVisible = Component->IsShown(false);
			bHiddenInGame = !Component->IsShown(true);
			bCastsShadow = false;
			bReceiveShadow = false;
			bReceiveDecal = false;
		}
		FVertexBuffer* GetVertexBuffer(uint8) const override { return nullptr; }
		FIndexBuffer* GetIndexBuffer(uint8) const override { return nullptr; }
	};
}

FPrimitiveSceneProxy* UParticleSystemComponent::CreateSceneProxy()
{
	return new FParticleSystemSceneProxy(this);
}

void UParticleSystemComponent::DestroyRenderData()
{
	for (FEmitterRenderData* Data : EmitterRenderData)
	{
		delete Data;
	}
	EmitterRenderData.Empty();
}

UParticleSystemComponent::FEmitterRenderData* UParticleSystemComponent::EnsureRenderData(int32 EmitterIndex, const FParticleEmitterInstance& Instance)
{
	while (EmitterRenderData.Num() <= EmitterIndex)
	{
		EmitterRenderData.Add(nullptr);
	}
	if (!EmitterRenderData[EmitterIndex])
	{
		EmitterRenderData[EmitterIndex] = new FEmitterRenderData();
	}
	FEmitterRenderData& Data = *EmitterRenderData[EmitterIndex];

	// 파티클 칸이 늘면 버퍼도 키운다. 인덱스 버퍼는 파티클 수와 무관한 고정 패턴이라 크기가 바뀔 때만 다시 채운다.
	const int32 Needed = Instance.MaxActiveParticles;
	if (Needed > Data.Capacity)
	{
		int32 NewCapacity = Data.Capacity > 0 ? Data.Capacity : 64;
		while (NewCapacity < Needed) NewCapacity *= 2;
		if (NewCapacity > FParticleEmitterInstance::MaxParticleLimit) NewCapacity = FParticleEmitterInstance::MaxParticleLimit;

		Data.VertexBuffer = RenderCommand::CreateDynamicVertexBuffer(
			sizeof(FParticleSpriteVertex) * SpriteVerticesPerParticle * NewCapacity, sizeof(FParticleSpriteVertex));
		Data.IndexBuffer = RenderCommand::CreateDynamicIndexBuffer(SpriteIndicesPerParticle * NewCapacity);
		if (!Data.VertexBuffer || !Data.IndexBuffer)
		{
			Data.VertexBuffer.reset();
			Data.IndexBuffer.reset();
			Data.Capacity = 0;
			return nullptr;
		}
		StagingIndices.SetNum(SpriteIndicesPerParticle * NewCapacity);
		FillSpriteIndices(StagingIndices.GetData(), NewCapacity);
		RenderCommand::UpdateBufferData(Data.IndexBuffer.get(), StagingIndices.GetData(), sizeof(uint32) * StagingIndices.Num());
		Data.Capacity = NewCapacity;
	}
	return &Data;
}

void UParticleSystemComponent::SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext)
{
	SCOPE_CYCLE_COUNTER(STAT_ParticleSubmit);
	// Show 메뉴의 Particles가 꺼진 뷰: 그리지 않는다. 렌더 기록(NoteRendered)도 남기지 않으므로 bCollideOnlyIfVisible 이미터는 충돌을 멈춘다.
	if (!EmitterInstances.IsEmpty() && !ViewContext.bShowParticles && FParticleStats::IsCollecting())
	{
		++FParticleStats::Pending().HiddenSubmits;
	}
	if (EmitterInstances.IsEmpty() || !ViewContext.bShowParticles)
	{
		return;
	}
	const auto SubmitStart = std::chrono::steady_clock::now();
	const bool bFirstCamera = !bHasLastCameraPosition;
	NoteRendered(ViewContext.CameraPosition);
	const bool bCollectStats = FParticleStats::IsCollecting();
	FParticleFrameStats& FrameStats = FParticleStats::Pending();
	if (bCollectStats)
	{
		++FrameStats.SubmitCalls;
	}
	if (bFirstCamera && Template && Template->GetNumLODLevels() > 1)
	{
		const EParticleSystemLODMethod Method = GetEffectiveLODMethod();
		if (Method == EParticleSystemLODMethod::Automatic || Method == EParticleSystemLODMethod::ActivateAutomatic)
		{
			SetLODLevel(DetermineLODLevelForLocation(GetWorldLocation(), LastCameraPosition));
		}
	}
	const FParticleSpriteCamera Camera = FParticleSpriteCamera::FromView(ViewContext.CameraPosition, ViewContext.CameraForward);
	const FVector ToComponent = GetWorldLocation() - ViewContext.CameraPosition;
	const float ComponentDistanceSquared = ToComponent.Dot(ToComponent);

	for (int32 EmitterIndex = 0; EmitterIndex < EmitterInstances.Num(); ++EmitterIndex)
	{
		FParticleEmitterInstance& Instance = *EmitterInstances[EmitterIndex];
		if (Instance.ActiveParticles <= 0)
		{
			continue;
		}
		// Beam, Mesh, Ribbon 같은 TypeData 인스턴스는 스스로 그린다. false면 아래 Sprite 경로로 그린다.
		if (Instance.SubmitRender(RenderQueue, ViewContext, ComponentDistanceSquared))
		{
			if (bCollectStats) ++FrameStats.SelfDrawnEmitters;
			continue;
		}
		FEmitterRenderData* Data = EnsureRenderData(EmitterIndex, Instance);
		// Required.Material (UE와 같다). 비어 있거나 파티클용이 아니면 엔진 기본 스프라이트 머티리얼
		UMaterial* Material = GetParticleRenderMaterial(Instance, ResourcePaths::ParticleSpriteMaterial);
		if (!Data || !Material)
		{
			continue;
		}

		StagingVertices.SetNum(SpriteVerticesPerParticle * Data->Capacity);
		const bool bSort = NeedsBackToFrontSort(Material);
		const int32 Count = FillSpriteVertices(Instance, Camera, bSort, StagingVertices.GetData(), Data->Capacity, SortScratch);
		if (Count <= 0)
		{
			continue;
		}
		RenderCommand::UpdateBufferData(Data->VertexBuffer.get(), StagingVertices.GetData(),
			static_cast<uint32>(sizeof(FParticleSpriteVertex) * SpriteVerticesPerParticle * Count));

		FRenderPacket Packet;
		Packet.Model = RenderQueue.StoreWorldMatrix(FMatrix::Identity);
		Packet.DynamicVertexBuffer = Data->VertexBuffer.get();
		Packet.DynamicIndexBuffer = Data->IndexBuffer.get();
		Packet.Material = Material;
		Packet.IndexCount = static_cast<uint32>(SpriteIndicesPerParticle * Count);
		// 이미터 사이의 순서는 컴포넌트 거리로 정한다 (이미터 안의 순서는 정점을 채울 때 정렬한다)
		Packet.CameraToParticleDistance = ComponentDistanceSquared;
		RenderQueue.Add(Packet);
		if (bCollectStats)
		{
			++FrameStats.SpritePackets;
			FrameStats.SpriteParticles += static_cast<uint64>(Count);
			FrameStats.SpriteVertices += static_cast<uint64>(SpriteVerticesPerParticle) * static_cast<uint64>(Count);
			FrameStats.UploadBytes += sizeof(FParticleSpriteVertex) * SpriteVerticesPerParticle * static_cast<uint64>(Count);
		}
	}
	Timings.SubmitMicroseconds = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - SubmitStart).count();
}

void UParticleSystemComponent::UpdateParticleBounds()
{
	FBox WorldBox{ FVector(0.0f, 0.0f, 0.0f), FVector(0.0f, 0.0f, 0.0f) };
	bool bHasAny = false;
	for (const FParticleEmitterInstance* Instance : EmitterInstances)
	{
		Instance->ExpandWorldBounds(WorldBox, bHasAny);
	}

	FBox NewLocalBounds{ FVector(0.0f, 0.0f, 0.0f), FVector(0.0f, 0.0f, 0.0f) };
	if (bHasAny)
	{
		// 월드 상자의 여덟 꼭짓점을 컴포넌트 공간으로 옮겨 그 상자를 구한다 (프록시가 다시 월드 상자로 바꾸므로 보수적이다)
		const FMatrix WorldToLocal = GetWorldMatrix().Inverse();
		bool bFirst = true;
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FVector Point((Corner & 1) ? WorldBox.Max.X : WorldBox.Min.X,
				(Corner & 2) ? WorldBox.Max.Y : WorldBox.Min.Y,
				(Corner & 4) ? WorldBox.Max.Z : WorldBox.Min.Z);
			const FVector Local = WorldToLocal.TransformPosition(Point);
			const FBox PointBox{ Local, Local };
			if (bFirst) { NewLocalBounds = PointBox; bFirst = false; }
			else { NewLocalBounds.Expand(PointBox); }
		}
	}

	const bool bChanged = NewLocalBounds.Min != ParticleLocalBounds.Min || NewLocalBounds.Max != ParticleLocalBounds.Max;
	ParticleLocalBounds = NewLocalBounds;
	if (bChanged)
	{
		// 프록시와 BVH가 새 경계로 다시 계산하게 한다
		OnTransformDirty();
	}
}
