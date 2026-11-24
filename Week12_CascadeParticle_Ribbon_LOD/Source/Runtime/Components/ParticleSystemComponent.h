#pragma once

#include "PrimitiveComponent.h"
#include "Particles/ParticleEmitterInstances.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSpriteRender.h"
#include "Particles/ParticleModule.h"
#include "Particles/Collision/ParticleCollisionEvent.h"
#include "Math/Box.h"
#include "ParticleSystemComponent.generated.h"

class UParticleSystemComponent;
// 파티클 충돌 이벤트를 받는 델리게이트 (UE: FParticleCollisionSignature). 컴포넌트가 Tick을 마친 뒤 이벤트마다 한 번 부른다.
DECLARE_DELEGATE(FParticleCollideSignature, UParticleSystemComponent*, const FParticleCollisionEvent&);



// 파티클 이펙트 에셋(UParticleSystem)을 월드에서 실행하는 컴포넌트 (UE: UParticleSystemComponent)
// 이미터마다 FParticleEmitterInstance를 하나씩 만들어 소유하고 매 Tick 컴포넌트 행렬을 넘긴다.
// 에셋은 공유되고 컴포넌트마다 인스턴스(파티클 메모리, 시간, 난수 스트림)가 따로 있다.
//
// 
//
// 수명 규칙: 인스턴스는 Template의 이미터 트리를 가리키는 raw 포인터를 들고 있다.
// 그래서 Template을 바꾸거나 컴포넌트를 해제하기 전에 인스턴스를 먼저 없앤다 (SetTemplate, 소멸자, OnUnregister).
// 이 컴포넌트가 쓰는 동안 Template 에셋 자체를 delete하면 안 된다.
UCLASS()
class UParticleSystemComponent : public UPrimitiveComponent
{
	GENERATED_BODY()
public:
	UParticleSystemComponent();
	virtual ~UParticleSystemComponent() override;

	virtual void TickComponent(float DeltaTime) override;
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	// 이번 Tick까지의 활성 파티클을 모두 덮는 상자(컴포넌트 공간). 파티클이 없으면 원점의 빈 상자다. 컬링이 이 값으로 먼저 걸러내므로 파티클을 덮지 못하면 아무것도 그려지지 않는다.
	virtual FBox CalcLocalBounds() const override { return ParticleLocalBounds; }
	// 뷰마다 호출된다. 이미터마다 활성 파티클을 카메라에 맞춰 동적 버퍼에 채우고 패킷을 하나씩 낸다 (이미터당 드로우 한 번).
	virtual void SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext) override;
	virtual void PostEditChangeProperty(const FProperty& Property) override;

	// 실행할 에셋. 바꾸면 인스턴스를 모두 다시 만든다. 씬에는 에셋 경로로 저장되므로 저장된 에셋(경로가 있는 것)이어야 복원된다.
	void SetTemplate(UParticleSystem* InTemplate);
	UParticleSystem* GetTemplate() const { return Template; }

	// 모든 인스턴스를 없애고 처음부터 다시 시작한다. 파티클도 모두 사라진다.
	void ResetSystem();
	// 새 파티클 생성을 멈춘다. 이미 나온 파티클은 수명이 끝날 때까지 계속 시뮬레이션된다 (UE: Deactivate의 생성 중단).
	// 컴포넌트의 Activate/Deactivate(Tick 자체를 켜고 끔)와는 별개다.
	void StopEmitting();
	// 생성을 다시 허용한다
	void StartEmitting();

	// 모든 이미터가 끝났고(HasCompleted) 파티클이 없으면 true. 인스턴스가 없으면 true다.
	bool HasCompleted() const;
	int32 GetNumEmitterInstances() const { return EmitterInstances.Num(); }
	FParticleEmitterInstance* GetEmitterInstance(int32 Index) const { return EmitterInstances.IsValidIndex(Index) ? EmitterInstances[Index] : nullptr; }
	// 모든 이미터의 살아 있는 파티클 수 합계
	int32 GetActiveParticleCount() const;

	// 활성 LOD 레벨을 설정하고 모든 이미터 인스턴스에 반영한다 (UE: SetLODLevel).
	void SetLODLevel(int32 InLODLevel);
	int32 GetLODLevel() const { return LODLevel; }
	// 이펙트 위치와 카메라 위치 사이의 거리로 Template->LODDistances 기준 LOD 번호를 구한다.
	int32 DetermineLODLevelForLocation(const FVector& EffectLocation, const FVector& CameraLocation) const;
	EParticleSystemLODMethod GetEffectiveLODMethod() const;

	// 구간별 걸린 시간(마이크로초). 가장 최근 Tick과 가장 최근 제출(뷰 하나) 값이다. 렌더가 된 뒤 칸 증가 공식, 16바이트 정렬, prefetch 비교를 잴 때 쓴다.
	// SimulationMicroseconds: 이미터 Tick 전체, BoundsMicroseconds: 경계 계산, SubmitMicroseconds: 정점 채우기·정렬·업로드. GPU 그리기 시간은 포함하지 않는다.
	struct FTimings
	{
		double SimulationMicroseconds = 0.0;
		double BoundsMicroseconds = 0.0;
		double SubmitMicroseconds = 0.0;
	};
	const FTimings& GetTimings() const { return Timings; }

	// 이 시드가 0이 아니면 같은 설정에서 항상 같은 결과가 나온다. 0이면 컴포넌트마다 다른 시드를 쓴다 (UUID에서 만든다).
	UPROPERTY(EditAnywhere)
	uint32 RandomSeed = 0;

	// true면 Template->LODMethod 대신 이 컴포넌트의 LODMethod를 쓴다 (UE: bOverrideLODMethod)
	UPROPERTY(EditAnywhere)
	bool bOverrideLODMethod = false;

	UPROPERTY(EditAnywhere)
	EParticleSystemLODMethod LODMethod = EParticleSystemLODMethod::Automatic;

	// ---- 렌더 기록 (충돌 모듈의 bCollideOnlyIfVisible, MaxCollisionDistance가 쓴다) ----
	// 렌더에 제출된 것을 기록한다. SubmitToRenderQueue가 부르고 자체 테스트도 쓴다. 카메라 위치와 컴포넌트 시간을 저장한다.
	void NoteRendered(const FVector& CameraPosition);
	// 마지막으로 렌더에 제출된 뒤 컴포넌트 시간(Tick의 DeltaTime 누적)이 Tolerance초 이내인가. 한 번도 렌더되지 않았으면 false.
	// UE는 월드 시간과 GetLastRenderTime을 쓴다. 여기서는 월드 시간에 접근하지 않고 컴포넌트가 직접 센다.
	bool WasRecentlyRendered(float Tolerance = 0.1f) const;
	// 마지막으로 렌더에 제출된 카메라 위치. 없으면 월드의 메인 카메라. 둘 다 없으면 false.
	bool GetCameraPosition(FVector& OutCameraPosition) const { return TryGetCameraPosition(OutCameraPosition); }

	// ---- 충돌 이벤트 (충돌 모듈의 bGenerateCollisionEvents) ----
	// 이번 Tick에 쌓인 충돌 이벤트. 다음 TickComponent가 시작할 때 비워지므로 그 전까지 읽을 수 있다.
	const TArray<FParticleCollisionEvent>& GetCollisionEvents() const { return CollisionEvents; }
	// 이번 Tick에 상한(MaxCollisionEventsPerTick)을 넘어 버려진 이벤트 수
	int32 GetDroppedCollisionEventCount() const { return DroppedCollisionEvents; }
	// Tick당 보관하는 이벤트 상한 (이 엔진에서 정한 값, UE에는 없다). 한 프레임에 수천 개가 충돌해도 프레임이 무너지지 않게 한다.
	static constexpr int32 MaxCollisionEventsPerTick = 1024;
	// 충돌 모듈이 부른다. Source가 이 컴포넌트의 몇 번째 이미터인지 찾아 EmitterIndex를 채우고 큐에 넣는다. 상한을 넘으면 버리고 센다.
	void ReportCollisionEvent(const FParticleEmitterInstance& Source, FParticleCollisionEvent Event);
	// TickComponent가 끝날 때 이벤트마다 한 번 불린다.
	FParticleCollideSignature OnParticleCollide;

protected:
	virtual void OnUnregister() override;

private:
	TArray<FParticleCollisionEvent> CollisionEvents;
	int32 DroppedCollisionEvents = 0;

	// 인스턴스가 없으면 Template에서 만든다. 컴포넌트가 등록·BeginPlay 순서와 무관하게 첫 Tick에 시작하게 한다.
	void EnsureInstances();
	void DestroyInstances();
	// 컴포넌트 월드 행렬에서 스케일을 뺀 것 (UE: GetComponentToWorld().ToMatrixNoScale())
	FMatrix GetComponentToWorldNoScale() const;
	uint32 GetInstanceSeed(int32 EmitterIndex) const;

	UPROPERTY(EditAnywhere)
	UParticleSystem* Template = nullptr;

	// 소유한다. UPROPERTY가 아니며 직렬화하지 않는다 (Template에서 다시 만든다).
	TArray<FParticleEmitterInstance*> EmitterInstances;
	bool bEmitting = true;
	int32 LODLevel = 0;
	float TimeSinceLastLODUpdate = 0.0f;
	FVector LastCameraPosition{ 0.0f, 0.0f, 0.0f };
	bool bHasLastCameraPosition = false;
	// TickComponent의 DeltaTime을 누적한 컴포넌트 시간(초)과 마지막으로 렌더에 제출된 시각
	double ComponentTimeSeconds = 0.0;
	double LastRenderComponentTime = 0.0;
	bool bHasBeenRendered = false;
	bool TryGetCameraPosition(FVector& OutCameraPosition) const;

	// 이미터 인스턴스마다 하나씩 (EmitterInstances와 같은 인덱스). 인스턴스가 사라지면 같이 지운다.
	struct FEmitterRenderData
	{
		TUniquePtr<FVertexBuffer> VertexBuffer;
		TUniquePtr<FIndexBuffer> IndexBuffer;
		int32 Capacity = 0;
		// 머티리얼은 여기 두지 않는다: Required.Material(또는 엔진 기본 머티리얼)을 그대로 쓴다 (GetParticleRenderMaterial)
	};
	TArray<FEmitterRenderData*> EmitterRenderData;
	FEmitterRenderData* EnsureRenderData(int32 EmitterIndex, const FParticleEmitterInstance& Instance);
	void DestroyRenderData();
	void UpdateParticleBounds();

	FTimings Timings;
	FBox ParticleLocalBounds{ FVector(0.0f, 0.0f, 0.0f), FVector(0.0f, 0.0f, 0.0f) };
	TArray<FParticleSpriteVertex> StagingVertices;
	TArray<uint32> StagingIndices;
	TArray<FParticleSortItem> SortScratch;
};
