#pragma once

#include "Asset/RenderAsset.h"
#include "ParticleSystem.generated.h"

class UParticleEmitter;

// 파티클 시스템이 LOD를 고르는 방식 (UE: ParticleSystemLODMethod)
UENUM()
enum class EParticleSystemLODMethod : uint8
{
	Automatic,			// LODDistanceCheckTime마다 카메라 거리로 자동 갱신 (UE: PARTICLESYSTEMLODMETHOD_Automatic)
	DirectSet,			// 컴포넌트의 SetLODLevel로 직접 지정한 값 유지 (UE: PARTICLESYSTEMLODMETHOD_DirectSet)
	ActivateAutomatic,	// 인스턴스가 처음 시작할 때 한 번만 카메라 거리로 결정 (UE: PARTICLESYSTEMLODMETHOD_ActivateAutomatic)
};

// 파티클 이펙트 에셋(Template). 설정만 가지며 여러 UParticleSystemComponent가 공유한다 (UE: Classes/Particles/ParticleSystem.h)
// URenderAsset을 상속하는 이유: 씬 저장이 UObject* 참조를 URenderAsset의 경로로만 기록하기 때문에,
// 컴포넌트의 Template 참조가 저장·복원되려면 이 계열이어야 한다.
UCLASS()
class UParticleSystem : public URenderAsset
{
	GENERATED_BODY()
public:
	// 소유 관계라 UPROPERTY가 아니다 (UParticleLODLevel의 설명 참고). UE: UPROPERTY(instanced)
	TArray<UParticleEmitter*> Emitters;

	// Automatic 방식에서 카메라 거리를 다시 재는 간격(초) (UE: LODDistanceCheckTime, 기본 0.25초)
	UPROPERTY(EditAnywhere, meta=(ClampMin=0))
	float LODDistanceCheckTime = 0.25f;

	// LOD 전환 방식 (UE: LODMethod)
	UPROPERTY(EditAnywhere)
	EParticleSystemLODMethod LODMethod = EParticleSystemLODMethod::Automatic;

	// 각 LOD 레벨로 전환되는 카메라 거리(m) (UE: LODDistances). 0번은 늘 0이고 이후 값은 단조 증가한다.
	// 리플렉션이 TArray를 지원하지 않아 Serialize가 직접 읽고 쓴다.
	TArray<float> LODDistances;

	using Super::Serialize;
	void Serialize(json& Handle, bool bIsLoading) override;

	// 소유한 이미터 트리(이미터, LOD, 모듈)를 모두 지운다. GC가 없어서 트리를 통째로 버리는 쪽이 직접 불러야 한다.
	// 소멸자가 하지 않는 이유: 엔진은 종료할 때 등록된 UObject를 하나씩 모두 delete한다(FEngineLoop::DestroyAllObjectsExceptEngine).
	// 소멸자가 자식을 또 지우면 이미 지워진 자식을 다시 지워서 종료 중에 죽는다.
	// 이 에셋을 Template으로 쓰는 FParticleEmitterInstance가 남아 있으면 안 된다 (컴포넌트가 먼저 인스턴스를 없앤다).
	void DeleteOwnedObjects();

	UParticleEmitter* AddEmitter();

	// 이미터를 목록에서 빼고 그 아래 트리(LOD, 모듈)까지 지운다.
	// 이 시스템을 쓰는 인스턴스를 먼저 없애야 한다 (UParticleSystemComponent::ResetSystem 등).
	bool RemoveEmitter(UParticleEmitter* Emitter);

	// 이미터를 깊게 복사해 바로 뒤에 넣는다. 이미터·LOD·모듈의 설정은 JSON 직렬화(Serialize)로 옮긴다.
	// 새 이미터의 레이아웃 캐시(CacheEmitterModuleInfo)까지 만든다. 실패하면 nullptr.
	UParticleEmitter* DuplicateEmitter(UParticleEmitter* Source);

	int32 GetNumEmitters() const { return Emitters.Num(); }
	UParticleEmitter* GetEmitter(int32 Index) const { return Emitters.IsValidIndex(Index) ? Emitters[Index] : nullptr; }

	// 시스템의 LOD 레벨 수 (첫 유효 이미터 기준, 이미터가 없으면 0)
	int32 GetNumLODLevels() const;
	float GetLODDistance(int32 LODIndex) const;
	void SetLODDistance(int32 LODIndex, float Distance);
	// LODDistances 배열 크기를 GetNumLODLevels()에 맞추고 단조 증가 규칙을 보정한다.
	void SyncLODDistances();
	// 카메라 거리(m)에 해당하는 LOD 번호를 돌려준다 (UE: GetLODLevelForLocation).
	int32 GetLODLevelForDistance(float Distance) const;

	// 모든 이미터의 InsertIndex 위치(INDEX_NONE이면 맨 뒤)에 새 LOD 레벨을 추가하고 추가된 LOD 번호를 돌려준다.
	int32 AddLODLevel(int32 InsertIndex = INDEX_NONE, float SpawnRateScale = 1.0f);
	// 모든 이미터에서 지정한 LOD 레벨을 지운다 (마지막 1개는 지울 수 없음).
	bool RemoveLODLevel(int32 LODIndex);
	// 모든 이미터의 TargetLODIndex를 SourceLODIndex로부터 다시 생성한다.
	bool RegenerateLODLevel(int32 TargetLODIndex, int32 SourceLODIndex, float SpawnRateScale = 0.5f);
};

// 시스템과 그 아래 트리를 모두 지운다. 코드로 만들었거나 읽다가 실패한 에셋을 버릴 때 쓴다. nullptr이면 아무것도 하지 않는다.
void DestroyParticleSystem(UParticleSystem* System);
