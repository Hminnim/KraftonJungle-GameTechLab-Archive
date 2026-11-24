#pragma once

#include "Particles/ParticleModule.h"
#include "Particles/ParticleEmitterInstances.h"
#include "Particles/ParticleSpriteRender.h"
#include "Particles/ParticleDistribution.h"
#include "ParticleModuleTypeDataBeam.generated.h"

class FVertexBuffer;
class FIndexBuffer;

// 빔 이미터 (UE: TypeData/ParticleModuleTypeDataBeam2. UE의 2는 옛 빔 시스템을 대체한 2세대라는 뜻이라 여기서는 뗐다). 두 점(소스 → 타깃) 사이를 잇는 띠를 그린다. 레이저, 번개, 전기.
// 에디터에서 이미터 우클릭 → TypeData → New Beam Data로 붙인다. 이미터 칸의 Required 위에 "Beam Data"로 보인다.
//
// [틀만 있음] 지금은 Sprite 이미터와 똑같이 동작한다. 채울 곳:
//  1) [완료: BeamMethod, Distance, TargetPoint, TextureTile] 아래 UPROPERTY 설정값 (Details 패널에 자동으로 나온다. Distribution은 ParticleSystemEditor.cpp의 DrawDetails에 한 줄 추가)
//  2) [완료: 직선 소스/타깃 → FBeamPayload] FParticleBeamEmitterInstance::Tick : 빔마다 소스/타깃 위치와 중간 점(노이즈) 계산
//  3) [완료: 직선 띠 1장] FParticleBeamEmitterInstance::SubmitRender: 띠 정점을 만들어 RenderQueue에 넣고 true를 돌려준다
//  4) [완료] FParticleBeamEmitterInstance::ExpandWorldBounds: 시작점, 중간 점, 끝점을 모두 덮는 상자
//  5) Distribution 멤버를 추가하면 Serialize 오버라이드 (ParticleModuleLifetime 등 참고)
//  6) 저장: 에디터의 저장과 이미터 복제는 Serialize를 거치므로, Serialize가 없는 Distribution/TArray 멤버는 저장 때 사라진다.
//     멤버를 추가하면 ParticleLayoutSelfTest의 저장/로드 왕복 검사도 함께 만든다.
//  6-1) InterpolationPoints: [완료 A 설정값, B Payload 점 배열 + GetBeamPoints, C Tick에서 직선 위 균등 분할, D 구간별 사각형 렌더, E 누적 길이 UV, F 모든 점을 덮는 경계 상자]
//  6-2) Taper: 쓰지 않아 뺐다 (UE: TaperMethod, TaperFactor, TaperScale). 빔 두께는 Size.X 하나다.
//  6-3) Noise: [완료 A 모듈 틀(Beam/ParticleModuleBeamNoise), B Payload 노이즈 칸, C 스폰 때 오프셋, D NoiseLockTime 규칙, E 노이즈 중심선, F Tessellation 곡선]
//  Sheets: [완료 띠를 진행 방향 축으로 돌려 여러 장 그린다. 돌린 모서리도 같은 원 위라 경계 상자는 그대로다]
//       간단한 빔에 필요 없어 뺀 UE 설정: Noise의 Frequency_LowRange, NoiseRangeScale, NoiseTension
// UE 참고 설정: BeamMethod(Distance/Target), Distance, TessellationFactor, Sheets, MaxBeamCount, Speed,
//               InterpolationPoints, bAlwaysOn, TextureTile
// UE는 소스·타깃·노이즈를 별도 모듈(BeamSource/BeamTarget/BeamNoise)로 둔다. 필요하면 같은 방식으로 모듈을 추가한다.

// 빔 끝점을 정하는 방식 (UE: EBeam2Method. UE의 Branch는 아직 없다)
UENUM()
enum class EBeamMethod : uint8
{
	Distance,	// 이미터 X축 방향으로 Distance만큼 뻗는다
	Target,		// TargetPoint까지 잇는다
};

UCLASS()
class UParticleModuleTypeDataBeam : public UParticleModuleTypeDataBase
{
	GENERATED_BODY()
public:
	FParticleEmitterInstance* CreateInstance() const override;

	// 파티클마다 FBeamPayload를 붙인다. CacheEmitterModuleInfo가 Template->TypeDataOffset에 위치를 기록한다.
	uint32 RequiredBytes(UParticleModuleTypeDataBase* TypeData) override;

	// UE처럼 Beam Data를 붙여도 다른 모듈(Spawn, Lifetime, Size, Required.Material)은 건드리지 않는다.
	// 빔 두께는 Size 모듈의 Size.X이고, 새 이미터의 Size 기본값을 그대로 쓴다.

	// 빔 끝점을 정하는 방식
	UPROPERTY(EditAnywhere)
	EBeamMethod BeamMethod = EBeamMethod::Distance;

	// BeamMethod가 Distance일 때 빔 길이 (m). UE 기본값 25(cm)를 이 엔진 단위(m)로 옮긴 값이다.
	UPROPERTY(EditAnywhere, meta=(ClampMin=0))
	float Distance = 0.25f;

	// BeamMethod가 Target일 때 끝점 (컴포넌트 로컬 좌표, m). UE의 BeamTarget 모듈에 해당하며, Distance와 같은 길이로 둔다.
	UPROPERTY(EditAnywhere)
	FVector TargetPoint = FVector(0.25f, 0.0f, 0.0f);

	// 빔 길이 방향으로 텍스처가 반복되는 횟수
	UPROPERTY(EditAnywhere, meta=(ClampMin=0))
	float TextureTile = 1.0f;

	// 빔 중간에 두는 점 수 (UE: InterpolationPoints, 기본 0). 0이면 시작점과 끝점만으로 그린다(구간 1개).
	// N이면 중간 점 N개를 Payload에 두고 구간 N+1개로 나눈다. Noise, Tangent가 이 점마다 값을 다르게 준다.
	// 값을 바꾸면 파티클 메모리 크기가 바뀌므로 시뮬레이션을 재시작할 때 레이아웃을 다시 계산한다 (컴포넌트가 인스턴스를 만들 때 CacheEmitterModuleInfo).
	UPROPERTY(EditAnywhere, meta=(ClampMin=0, ClampMax=128))
	int32 InterpolationPoints = 0;
	// 파티클 하나의 Payload가 커지는 것을 막는 상한 (이 엔진에서 정한 값, UE에는 없다). 음수는 0으로 본다.
	static constexpr int32 MaxInterpolationPoints = 128;
	int32 GetClampedInterpolationPoints() const;

	// 이미터의 Noise 모듈 설정을 모은다 (UE: CacheModuleInfo가 LOD_BeamModule_Noise를 찾는다). RequiredBytes 직전에 불린다.
	void CacheModuleInfo(UParticleEmitter* Emitter) override;
	// CacheModuleInfo가 정한 값. 노이즈가 없으면 0이다. 레이아웃과 CreateInstance가 쓴다.
	int32 CachedNoiseMaxFrequency = 0;
	int32 CachedNoiseTessellation = 1;
	// Noise 모듈의 bSmooth. 켜져 있으면 목표 오프셋(Next) 칸을 더 둔다 (UE: NextNoisePoints)
	bool bCachedNoiseSmooth = false;
	// 빔 하나의 중심선 점 수의 최대값: 중간 점 경로(InterpolationPoints + 2)와 노이즈 경로((Frequency + 1) × Tessellation + 1) 중 큰 쪽
	int32 GetLineCapacity() const;

	// 빔 하나에 그리는 띠 수 (UE: Sheets, 기본 1). 0 이하는 1로 본다.
	// 첫 띠는 카메라를 향하고, i번째 띠는 폭 방향을 빔 진행 방향을 축으로 π / Sheets × i만큼 돌린다 (UE와 같다).
	// 옆에서 봐도 빔이 납작한 선으로 보이지 않게 한다. 띠 수만큼 사각형이 늘어난다.
	UPROPERTY(EditAnywhere, meta=(ClampMin=1))
	int32 Sheets = 1;
	int32 GetSheetCount() const { return Sheets > 0 ? Sheets : 1; }
};

// 빔 하나(= 파티클 하나)의 끝점. 파티클 슬롯의 FBaseParticle 바로 뒤에 붙는다 (UE: FBeam2TypeDataPayload).
// 렌더와 경계 상자가 바로 쓰도록 월드 좌표로 저장한다.
// 메모리 배치 (UE도 Beam2 Payload 뒤에 보간 점을 이어 붙인다):
//   [ FBeamPayload | FVector Points[InterpolationPoints]
//     | FBeamNoisePayload | FVector NoiseOffsets[MaxFrequency + 1] (Noise 모듈이 켜져 있을 때만)
//     | FVector NextNoiseOffsets[MaxFrequency + 1] (Noise 모듈의 bSmooth가 켜져 있을 때만) ]
//   Points는 시작점과 끝점 사이의 중간 점(월드)이고, 시작점 쪽부터 순서대로 놓인다. 정점이 아니라 중심선 위의 점이다
//   (정점은 카메라마다 달라 SubmitRender에서 만든다).
struct FBeamPayload
{
	FVector SourcePoint;	// 빔 시작점 (월드)
	FVector TargetPoint;	// 빔 끝점 (월드)
};

// 빔 하나의 노이즈 상태 (UE: Lock_Max_NumNoisePoints의 점 수 부분, NoiseRate). 바로 뒤에 오프셋 MaxFrequency + 1개가 온다.
// 오프셋은 컴포넌트 로컬 축 기준 벡터(m)이고, 마지막 칸은 끝점용(bTargetNoise)이다.
struct FBeamNoisePayload
{
	int32 Frequency;	// 이 빔의 노이즈 점 수. 0이면 노이즈 없이 그린다 (Noise 모듈이 꺼져 있거나 Spawn 전)
	float NoiseRate;	// NoiseLockTime이 양수일 때 마지막으로 뽑은 뒤 흐른 시간
};

class UParticleModuleBeamNoise;

// 빔 이미터 한 개의 실행 상태 (UE: FParticleBeam2EmitterInstance). 컴포넌트가 이미터마다 하나씩 만든다.
// 파티클 하나가 빔 하나다. 수명, 두께(Size.X), 색은 기존 Required/Lifetime/Size/Color 모듈 값을 그대로 쓴다.
struct FParticleBeamEmitterInstance : public FParticleEmitterInstance
{
	// 이 이미터의 Beam TypeData. 없으면 nullptr
	const UParticleModuleTypeDataBeam* GetBeamTypeData() const;
	// ParticleIndices[ActiveIndex]번 파티클의 Payload. ActiveIndex는 [0, ActiveParticles) 범위, Payload가 없으면 nullptr
	FBeamPayload* GetBeamPayload(int32 ActiveIndex) const;
	// 같은 파티클의 중간 점 배열 (PointCount개, FBeamPayload 바로 뒤). PointCount가 0이거나 Payload가 없으면 nullptr
	FVector* GetBeamPoints(int32 ActiveIndex) const;
	// 같은 파티클의 노이즈 상태 (중간 점 바로 뒤). 레이아웃에 노이즈 칸이 없으면 nullptr
	FBeamNoisePayload* GetBeamNoiseFromParticle(FBaseParticle* Particle) const;
	// 노이즈 상태 바로 뒤의 오프셋 배열 (NoiseMaxFrequency + 1칸)
	static FVector* GetNoiseOffsets(FBeamNoisePayload* Noise);
	// 오프셋 배열 바로 뒤의 목표 오프셋 배열 (NoiseMaxFrequency + 1칸). bNoiseSmooth가 아니면 nullptr
	FVector* GetNextNoiseOffsets(FBeamNoisePayload* Noise) const;
	// 빔 하나의 중심선 점 수 (스폰 때 정한 노이즈 점 수에 따라 다르다). 노이즈가 없으면 PointCount + 2
	int32 GetBeamLineCount(FBaseParticle* Particle) const;

	// Instance가 빔 이미터면 빔 인스턴스로, 아니면 nullptr (Noise처럼 빔 전용 모듈이 쓴다)
	static FParticleBeamEmitterInstance* FromInstance(FParticleEmitterInstance& Instance);
	// 이미터의 Noise 모듈 (LOD 0의 Modules에서 찾는다). 없으면 nullptr
	const UParticleModuleBeamNoise* GetBeamNoiseModule() const;

	// 빔 하나의 중심선(월드)을 Out에 채우고 점 수를 돌려준다. Out은 LineCapacity칸 이상이어야 한다. 렌더와 경계 상자가 같이 쓴다.
	// 노이즈가 있으면 노이즈 점(+ Tessellation 곡선)으로, 없으면 시작점, 중간 점, 끝점으로 만든다.
	int32 BuildBeamLine(int32 ActiveIndex, FVector* Out) const;

	// 이 인스턴스의 중간 점 수. 인스턴스를 만들 때(CreateInstance) 레이아웃과 같은 값으로 정해지고 바뀌지 않는다.
	// 에디터에서 InterpolationPoints를 드래그하는 동안에도 메모리 배치와 어긋나지 않게 TypeData 값 대신 이 값을 쓴다.
	int32 PointCount = 0;
	// 레이아웃의 노이즈 점 수 최대값과 Tessellation (노이즈 칸이 없으면 0, 1). PointCount와 같이 CreateInstance에서 정해진다.
	int32 NoiseMaxFrequency = 0;
	int32 NoiseTessellation = 1;
	// 레이아웃에 목표 오프셋 칸이 있는지 (Noise 모듈의 bSmooth). PointCount와 같이 CreateInstance에서 정해진다.
	bool bNoiseSmooth = false;
	// 빔 하나의 중심선 점 수 최대값 (렌더 버퍼, 임시 배열 크기)
	int32 LineCapacity = 2;

	// 이번 프레임의 노이즈 접선 길이 (UE처럼 이미터 시간으로 Tick마다 한 번 평가한다)
	float CurrentNoiseTangentStrength = 0.0f;
	// 이번 프레임의 이미터 X축(월드). 노이즈 곡선의 시작점 접선 방향이다.
	FVector CurrentSourceTangentDirection = FVector(1.0f, 0.0f, 0.0f);

	// 기반 Tick 뒤에 살아 있는 빔마다 SourcePoint / TargetPoint를 계산한다
	void Tick(float DeltaTime) override;
	// UE의 FParticleBeam2EmitterInstance::PostSpawn처럼 Spawn 모듈 다음에 TypeData의 Spawn을 부른다 (지금 빔 TypeData는 Spawn에서 할 일이 없다)
	void PostSpawn(FBaseParticle* Particle, float SpawnTime) override;
	// 빔마다 중심선(시작점 → 중간 점 → 끝점)의 구간마다 사각형 하나(정점 4개)를 카메라를 향하게 펼쳐 그린다.
	// 정점 형식, 셰이더, 인덱스 패턴은 Sprite와 같다 (FParticleSpriteVertex, ParticleSpriteShader, FillSpriteIndices).
	bool SubmitRender(FRenderQueue& RenderQueue, const FViewContext& ViewContext, float ComponentDistanceSquared) override;
	bool ExpandWorldBounds(FBox& InOutBox, bool& bInOutHasAny) const override;

private:
	// 버퍼 용량을 MaxActiveParticles 이상으로 맞춘다 (UParticleSystemComponent::EnsureRenderData와 같은 일을 이 인스턴스 소유의 버퍼로). 실패하면 false.
	// 머티리얼은 Required.Material을 쓰고, 비어 있으면 Sprite와 같은 ResourcePaths::ParticleSpriteMaterial을 쓴다 (UE의 DefaultSpriteMaterial처럼).
	bool EnsureRenderResources(int32 SheetCount);

	TUniquePtr<FVertexBuffer> VertexBuffer;
	TUniquePtr<FIndexBuffer> IndexBuffer;
	int32 Capacity = 0;	// 버퍼에 들어가는 사각형 수 (빔 하나 = 구간 최대 LineCapacity - 1개 × Sheets)
	// ExpandWorldBounds(const)가 중심선을 만들 때 쓰는 임시 배열
	mutable TArray<FVector> BoundsLineScratch;

	TArray<FParticleSpriteVertex> StagingVertices;
	// SubmitRender가 빔 하나를 그릴 때 쓰는 임시 배열 (중심선 점, 점마다 폭 방향). 매번 할당하지 않으려고 둔다.
	TArray<FVector> LineScratch;
	TArray<FVector> SideScratch;
	TArray<FVector> DirectionScratch;	// 점마다 진행 방향 (Sheets가 폭 방향을 돌리는 축)
	TArray<FVector> SheetSideScratch;	// 이번 띠의 점마다 폭 방향 (SideScratch를 돌린 것)
	TArray<float> UScratch;	// 점마다 U (누적 길이 비율 × TextureTile)
	TArray<FParticleSortItem> SortScratch;
};
