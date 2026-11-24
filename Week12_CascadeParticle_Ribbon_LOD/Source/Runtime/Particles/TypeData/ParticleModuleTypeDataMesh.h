#pragma once

#include "Particles/ParticleModule.h"
#include "Particles/ParticleEmitterInstances.h"
#include "Render/Mesh.h"
#include "Math/Color.h"
#include "ParticleModuleTypeDataMesh.generated.h"

class FStructuredBuffer;

// 메시 이미터 (UE: TypeData/ParticleModuleTypeDataMesh). 파티클마다 빌보드 대신 스태틱 메시를 그린다. 돌 파편, 낙엽, 탄피.
// 에디터에서 이미터 우클릭 → TypeData → New Mesh Data로 붙인다. 이미터 칸의 Required 위에 "Mesh Data"로 보인다.
//
// 시뮬레이션은 Sprite와 같다. 파티클은 데이터 레코드일 뿐이고 액터나 컴포넌트를 만들지 않는다.
// 그리기는 인스턴싱이다: 활성 파티클의 변환을 FMeshParticleInstance 배열로 StructuredBuffer에 올리고,
// 메시 섹션마다 DrawIndexedInstanced 한 번으로 모든 파티클을 그린다 (UE: FDynamicMeshEmitterData의 인스턴스 버퍼).
// 현재 지원 범위: Forward 렌더링의 불투명 머티리얼. Deferred(GBuffer), 반투명 머티리얼, 그림자, 데칼은 아직 그리지 않는다.
//
// 저장: 에디터의 저장과 이미터 복제는 모듈의 Serialize를 거쳐 값을 옮긴다. UPROPERTY 멤버는 자동으로 저장되지만
// Distribution이나 TArray 멤버는 Serialize 오버라이드가 없으면 저장하는 순간 값이 사라진다.
// 멤버를 추가하면 Serialize 오버라이드와 ParticleLayoutSelfTest의 저장/로드 왕복 검사를 함께 만든다.
// UE 참고 설정: MeshAlignment(None/XY/XZ/YZ), bOverrideMaterial, AxisLockOption, bCameraFacing, Pitch/Roll/Yaw
UCLASS()
class UParticleModuleTypeDataMesh : public UParticleModuleTypeDataBase
{
	GENERATED_BODY()
public:
	using Super::Serialize;
	void Serialize(json& Handle, bool bIsLoading) override;
	UParticleModuleTypeDataMesh();

	FParticleEmitterInstance* CreateInstance() const override;
	// 파티클마다 3D 회전 Payload(FMeshRotationPayload)를 쓴다. TypeData의 Payload라서 Emitter->TypeDataOffset에서 시작한다.
	uint32 RequiredBytes(UParticleModuleTypeDataBase* TypeData) override;

	// 파티클마다 그릴 메시. nullptr이면 이 이미터는 아무것도 그리지 않는다 (UE와 같다).
	UPROPERTY(EditAnywhere)
	UStaticMesh* Mesh = nullptr;

	// 메시 자체의 사전 회전(도): X = Roll, Y = Pitch, Z = Yaw. 파티클이 생길 때 한 번 뽑아 InitialOrientation에 둔다 (UE: RollPitchYawRange).
	FVectorDistribution RollPitchYawRange = FVectorDistribution::MakeConstant(FVector(0.0f, 0.0f, 0.0f));

	// 메시 머티리얼 슬롯별 덮어쓰기. 슬롯 번호로 찾고, 비었거나 nullptr이면 메시 자체의 머티리얼을 쓴다.
	// UE와 다름: UE는 bOverrideMaterial(이미터 머티리얼)과 MeshMaterial 모듈을 따로 두고 서로 배타적으로 쓴다. 여기서는 이 배열 하나로 합쳤다.
	// UPROPERTY가 아니라서 Details 패널에는 나오지 않는다 (편집 UI는 아직 없고, 저장/로드와 그리기는 지원한다).
	TArray<UMaterial*> MaterialOverrides;

	// 슬롯의 머티리얼: 덮어쓰기 → 메시의 머티리얼 → 기본 머티리얼 순서
	UMaterial* ResolveMaterial(uint32 SlotIndex) const;
};

// 메시 이미터의 파티클당 회전 Payload. 각은 모두 도(degree)이고 X = Roll, Y = Pitch, Z = Yaw다.
// UE: FMeshRotationPayloadData는 6개 필드(InitialOrientation, InitRotation, Rotation, CurContinuousRotation, RotationRate, RotationRateBase)다.
// UE와 다름: 수명 기반(Over Life) 회전 모듈이 아직 없어 Base/Init/Cur 구분이 필요 없다. 그 모듈을 만들 때 필드를 추가한다.
struct FMeshRotationPayload
{
	FVector InitialOrientation;	// 메시의 사전 회전. 생성 때 한 번 정해진다
	FVector Rotation;			// 현재 누적 회전. 매 Tick RotationRate만큼 더한다
	FVector RotationRate;		// 초당 회전(도)
};
static_assert(alignof(FMeshRotationPayload) <= 8, "Payload type must not require more than 8-byte alignment");

// 파티클이 생길 때 메시 회전(도)을 더한다 (UE: ParticleModuleMeshRotation). 같은 종류 모듈을 여러 개 두면 합쳐진다.
// 메시 이미터(TypeData가 Mesh)가 아니면 아무것도 하지 않는다.
// UE와 다름: UE는 값을 회전 수(1 = 360도)로 받는다. 여기서는 도다. bInheritParent는 없다.
UCLASS()
class UParticleModuleMeshRotation : public UParticleModule
{
	GENERATED_BODY()
public:
	using Super::Serialize;
	void Serialize(json& Handle, bool bIsLoading) override;
	UParticleModuleMeshRotation();
	void Spawn(const FParticleSpawnContext& Context) override;

	FVectorDistribution StartRotation = FVectorDistribution::MakeConstant(FVector(0.0f, 0.0f, 0.0f));
};

// 파티클이 생길 때 초당 회전(도)을 더한다 (UE: ParticleModuleMeshRotationRate). 메시 이미터가 아니면 아무것도 하지 않는다.
UCLASS()
class UParticleModuleMeshRotationRate : public UParticleModule
{
	GENERATED_BODY()
public:
	using Super::Serialize;
	void Serialize(json& Handle, bool bIsLoading) override;
	UParticleModuleMeshRotationRate();
	void Spawn(const FParticleSpawnContext& Context) override;

	FVectorDistribution StartRotationRate = FVectorDistribution::MakeConstant(FVector(0.0f, 0.0f, 0.0f));
};

// 인스턴스 하나의 GPU 데이터 (64바이트). Resources/Shader/UberLit.hlsl의 FMeshInstance와 같아야 한다.
// 변환은 월드 공간이다: Row0~2.xyz = World 행렬의 0~2행(회전과 크기), w = 이동의 x, y, z.
struct FMeshParticleInstance
{
	FVector4 Row0;
	FVector4 Row1;
	FVector4 Row2;
	FLinearColor Color;
};
static_assert(sizeof(FMeshParticleInstance) == 64, "FMeshParticleInstance must match FMeshInstance in UberLit.hlsl");

// 메시 이미터 한 개의 실행 상태 (UE: FParticleMeshEmitterInstance). 시뮬레이션은 Sprite와 같고 그리기만 다르다.
struct FParticleMeshEmitterInstance : public FParticleEmitterInstance
{
	FParticleMeshEmitterInstance();
	~FParticleMeshEmitterInstance() override;

	void Tick(float DeltaTime) override;
	void PostSpawn(FBaseParticle* Particle, float SpawnTime) override;
	bool SubmitRender(FRenderQueue& RenderQueue, const FViewContext& ViewContext, float ComponentDistanceSquared) override;
	bool ExpandWorldBounds(FBox& InOutBox, bool& bInOutHasAny) const override;

private:
	UParticleModuleTypeDataMesh* GetMeshTypeData() const;
	// 파티클 안에서 회전 Payload가 시작하는 위치. 이 이미터가 메시 TypeData가 아니거나 레이아웃이 없으면 INDEX_NONE.
	// 파티클마다 부르지 말고 루프 밖에서 한 번 구해 둔다 (LOD와 클래스를 조회한다).
	int32 GetRotationPayloadOffset() const;
	// 활성 파티클의 변환을 Staging에 채우고 개수를 돌려준다.
	// SortFromCamera가 있으면 그 위치에서 먼 파티클부터 놓는다 (반투명 머티리얼은 뒤에서 앞으로 그려야 해서, 인스턴스 순서가 곧 그리는 순서다).
	int32 FillInstances(const FVector* SortFromCamera);
	// 인스턴스 개수만큼 들어가는 GPU 버퍼를 보장한다. 늘릴 때 이전 버퍼는 같은 프레임의 다른 뷰 패킷이 아직 가리킬 수 있어
	// 다음 Tick까지 보관한다.
	bool EnsureInstanceBuffer(uint32 InstanceCount);

	TUniquePtr<FStructuredBuffer> InstanceBuffer;
	uint32 InstanceCapacity = 0;
	TArray<TUniquePtr<FStructuredBuffer>> RetiredBuffers;
	TArray<FMeshParticleInstance> Staging;
};
