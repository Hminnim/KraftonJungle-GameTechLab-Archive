#include "EnginePCH.h"
#include "Particles/TypeData/ParticleModuleTypeDataBeam.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/ParticleHelper.h"
#include "Particles/ParticleSerialization.h"
#include "Particles/Beam/ParticleModuleBeamNoise.h"
#include "UObject/Casts.h"
#include "Render/RenderPacket.h"
#include "Render/RenderCommand.h"
#include "Render/Material.h"
#include "Asset/AssetManager.h"
#include "Core/ResourcePaths.h"
#include <algorithm>
#include <cmath>

int32 UParticleModuleTypeDataBeam::GetClampedInterpolationPoints() const
{
	return std::clamp(InterpolationPoints, 0, MaxInterpolationPoints);
}

FParticleEmitterInstance* UParticleModuleTypeDataBeam::CreateInstance() const
{
	// 컴포넌트는 CacheEmitterModuleInfo(→ RequiredBytes) 직후에 이 함수를 부르므로 두 값이 같다
	FParticleBeamEmitterInstance* Instance = new FParticleBeamEmitterInstance();
	Instance->PointCount = GetClampedInterpolationPoints();
	Instance->NoiseMaxFrequency = CachedNoiseMaxFrequency;
	Instance->NoiseTessellation = CachedNoiseTessellation;
	Instance->bNoiseSmooth = bCachedNoiseSmooth && CachedNoiseMaxFrequency > 0;
	Instance->LineCapacity = GetLineCapacity();
	return Instance;
}

void UParticleModuleTypeDataBeam::CacheModuleInfo(UParticleEmitter* Emitter)
{
	CachedNoiseMaxFrequency = 0;
	CachedNoiseTessellation = 1;
	bCachedNoiseSmooth = false;
	if (!Emitter)
	{
		return;
	}
	// 모든 LOD와 꺼진 모듈(bEnabled == false)까지 확인해 가장 큰 노이즈 요구량을 잡는다.
	// 실행 중에 LOD가 바뀌거나 모듈을 껐다 켜도 파티클 메모리 배치가 바뀌지 않게 하기 위해서다.
	for (const UParticleLODLevel* LODLevel : Emitter->LODLevels)
	{
		if (!LODLevel)
		{
			continue;
		}
		for (UParticleModule* Module : LODLevel->Modules)
		{
			if (const UParticleModuleBeamNoise* Noise = Cast<UParticleModuleBeamNoise>(Module))
			{
				CachedNoiseMaxFrequency = std::max(CachedNoiseMaxFrequency, Noise->GetMaxFrequency());
				CachedNoiseTessellation = std::max(CachedNoiseTessellation, Noise->GetTessellationFactor());
				bCachedNoiseSmooth |= Noise->bSmooth;
			}
		}
	}
}

int32 UParticleModuleTypeDataBeam::GetLineCapacity() const
{
	const int32 InterpolationLine = GetClampedInterpolationPoints() + 2;
	const int32 NoiseLine = CachedNoiseMaxFrequency > 0 ? (CachedNoiseMaxFrequency + 1) * CachedNoiseTessellation + 1 : 0;
	return std::max(InterpolationLine, NoiseLine);
}

uint32 UParticleModuleTypeDataBeam::RequiredBytes(UParticleModuleTypeDataBase* TypeData)
{
	// UE처럼 Noise가 켜져 있을 때만 노이즈 자리를 더한다
	size_t Bytes = sizeof(FBeamPayload) + sizeof(FVector) * GetClampedInterpolationPoints();
	if (CachedNoiseMaxFrequency > 0)
	{
		Bytes += sizeof(FBeamNoisePayload) + sizeof(FVector) * (CachedNoiseMaxFrequency + 1);
		// bSmooth: 목표 오프셋 칸 (UE: NextNoisePoints)
		if (bCachedNoiseSmooth) Bytes += sizeof(FVector) * (CachedNoiseMaxFrequency + 1);
	}
	return static_cast<uint32>(Bytes);
}

const UParticleModuleTypeDataBeam* FParticleBeamEmitterInstance::GetBeamTypeData() const
{
	const UParticleLODLevel* LODLevel = GetCurrentLODLevel();
	return LODLevel ? Cast<UParticleModuleTypeDataBeam>(LODLevel->TypeDataModule) : nullptr;
}

FBeamPayload* FParticleBeamEmitterInstance::GetBeamPayload(int32 ActiveIndex) const
{
	if (!Template || Template->TypeDataOffset == INDEX_NONE || ActiveIndex < 0 || ActiveIndex >= ActiveParticles)
	{
		return nullptr;
	}
	uint8* ParticleBase = ParticleData + static_cast<size_t>(ParticleIndices[ActiveIndex]) * ParticleStride;
	return reinterpret_cast<FBeamPayload*>(ParticleBase + Template->TypeDataOffset);
}

FVector* FParticleBeamEmitterInstance::GetBeamPoints(int32 ActiveIndex) const
{
	FBeamPayload* Payload = PointCount > 0 ? GetBeamPayload(ActiveIndex) : nullptr;
	return Payload ? reinterpret_cast<FVector*>(reinterpret_cast<uint8*>(Payload) + sizeof(FBeamPayload)) : nullptr;
}

FBeamNoisePayload* FParticleBeamEmitterInstance::GetBeamNoiseFromParticle(FBaseParticle* Particle) const
{
	if (!Particle || NoiseMaxFrequency <= 0 || !Template || Template->TypeDataOffset == INDEX_NONE)
	{
		return nullptr;
	}
	uint8* Payload = reinterpret_cast<uint8*>(Particle) + Template->TypeDataOffset;
	return reinterpret_cast<FBeamNoisePayload*>(Payload + sizeof(FBeamPayload) + sizeof(FVector) * PointCount);
}

FVector* FParticleBeamEmitterInstance::GetNoiseOffsets(FBeamNoisePayload* Noise)
{
	return Noise ? reinterpret_cast<FVector*>(reinterpret_cast<uint8*>(Noise) + sizeof(FBeamNoisePayload)) : nullptr;
}

FVector* FParticleBeamEmitterInstance::GetNextNoiseOffsets(FBeamNoisePayload* Noise) const
{
	return (Noise && bNoiseSmooth) ? GetNoiseOffsets(Noise) + (NoiseMaxFrequency + 1) : nullptr;
}

const UParticleModuleBeamNoise* FParticleBeamEmitterInstance::GetBeamNoiseModule() const
{
	const UParticleLODLevel* LODLevel = GetCurrentLODLevel();
	if (!LODLevel)
	{
		return nullptr;
	}
	for (UParticleModule* Module : LODLevel->Modules)
	{
		if (const UParticleModuleBeamNoise* Noise = Cast<UParticleModuleBeamNoise>(Module))
		{
			return Noise->bEnabled ? Noise : nullptr;
		}
	}
	return nullptr;
}

FParticleBeamEmitterInstance* FParticleBeamEmitterInstance::FromInstance(FParticleEmitterInstance& Instance)
{
	// Beam Data를 붙인 직후 레이아웃을 다시 만들기 전에는 기본 인스턴스일 수 있어 실제 타입으로 확인한다
	return dynamic_cast<FParticleBeamEmitterInstance*>(&Instance);
}

int32 FParticleBeamEmitterInstance::GetBeamLineCount(FBaseParticle* Particle) const
{
	const FBeamNoisePayload* Noise = GetBeamNoiseFromParticle(Particle);
	if (Noise && Noise->Frequency > 0 && GetBeamNoiseModule())
	{
		const int32 Frequency = std::min(Noise->Frequency, NoiseMaxFrequency);
		return (Frequency + 1) * NoiseTessellation + 1;
	}
	return PointCount + 2;
}

int32 FParticleBeamEmitterInstance::BuildBeamLine(int32 ActiveIndex, FVector* Out) const
{
	const FBeamPayload* Payload = GetBeamPayload(ActiveIndex);
	if (!Payload || !Out)
	{
		return 0;
	}
	FBaseParticle* Particle = reinterpret_cast<FBaseParticle*>(ParticleData + static_cast<size_t>(ParticleIndices[ActiveIndex]) * ParticleStride);
	const FVector Source = Payload->SourcePoint;
	const FVector Target = Payload->TargetPoint;

	FBeamNoisePayload* Noise = GetBeamNoiseFromParticle(Particle);
	const UParticleModuleBeamNoise* NoiseModule = (Noise && Noise->Frequency > 0) ? GetBeamNoiseModule() : nullptr;
	if (!NoiseModule)
	{
		// 노이즈 없음: 시작점, 중간 점(Payload), 끝점
		const FVector* Points = GetBeamPoints(ActiveIndex);
		Out[0] = Source;
		for (int32 k = 0; k < PointCount; ++k)
		{
			Out[k + 1] = Points ? Points[k] : Source + (Target - Source) * (static_cast<float>(k + 1) / static_cast<float>(PointCount + 1));
		}
		Out[PointCount + 1] = Target;
		return PointCount + 2;
	}

	// 노이즈 중심선 (UE: FDynamicBeam2EmitterData의 노이즈 경로)
	// 노이즈 점 k(1..F) = 직선을 F + 1 구간으로 나눈 k번째 위치 + (컴포넌트 회전으로 돌린 오프셋[k - 1]).
	// 시작점은 흔들지 않고, 끝점은 bTargetNoise일 때만 오프셋[F]를 더한다.
	const int32 Frequency = std::min(Noise->Frequency, NoiseMaxFrequency);
	const int32 Tess = NoiseTessellation;
	const FVector* Offsets = GetNoiseOffsets(Noise);
	const FVector Delta = Target - Source;
	const float StepSize = 1.0f / static_cast<float>(Frequency + 1);
	const auto NoiseAt = [&](int32 Index) { return ComponentToWorld.TransformVector(Offsets[Index]); };

	// 노이즈 점(곡선의 제어점)은 Tess칸 간격으로 놓고, 사이는 아래에서 곡선으로 채운다
	Out[0] = Source;
	for (int32 k = 1; k <= Frequency; ++k)
	{
		Out[k * Tess] = Source + Delta * (StepSize * static_cast<float>(k)) + NoiseAt(k - 1);
	}
	Out[(Frequency + 1) * Tess] = NoiseModule->bTargetNoise ? Target + NoiseAt(Frequency) : Target;

	// Tessellation: 제어점 사이를 Hermite 곡선(UE: FMath::CubicInterp)으로 Tess등분한다. 접선은 모두 길이 NoiseTangentStrength로 맞춘다.
	//  - 시작점: 이미터 X축 방향 (UE: 소스 모듈이 없을 때)
	//  - 노이즈 점: 다음 점 - 이전 점의 방향 (UE는 (1 - NoiseTension) / 2를 곱한 뒤 정규화해서 결과가 같다. Tension은 뺐다)
	//  - 끝점: 마지막 노이즈 점 → 직선 위 끝점 방향 (UE: 타깃 모듈이 없을 때)
	// UE는 마지막 노이즈 점에서 타깃 모듈의 접선을 쓰는데, 타깃 모듈이 아직 없어 다른 노이즈 점과 같은 규칙을 쓴다.
	if (Tess > 1)
	{
		const float Strength = CurrentNoiseTangentStrength;
		const auto Control = [&](int32 k) -> const FVector& { return Out[k * Tess]; };
		const auto TangentAt = [&](int32 k) -> FVector
		{
			if (k == 0) return CurrentSourceTangentDirection * Strength;
			if (k == Frequency + 1) return (Target - Control(Frequency)).GetSafeNormal() * Strength;
			return (Control(k + 1) - Control(k - 1)).GetSafeNormal() * Strength;
		};
		for (int32 k = 0; k <= Frequency; ++k)
		{
			const FVector P0 = Control(k);
			const FVector P1 = Control(k + 1);
			const FVector T0 = TangentAt(k);
			const FVector T1 = TangentAt(k + 1);
			for (int32 i = 1; i < Tess; ++i)
			{
				const float A = static_cast<float>(i) / static_cast<float>(Tess);
				const float A2 = A * A;
				const float A3 = A2 * A;
				Out[k * Tess + i] = P0 * (2.0f * A3 - 3.0f * A2 + 1.0f) + T0 * (A3 - 2.0f * A2 + A)
					+ P1 * (-2.0f * A3 + 3.0f * A2) + T1 * (A3 - A2);
			}
		}
	}
	return (Frequency + 1) * Tess + 1;
}

void FParticleBeamEmitterInstance::PostSpawn(FBaseParticle* Particle, float SpawnTime)
{
	// UE와 같은 순서: TypeData의 Spawn → 기반 PostSpawn. TypeData는 Modules 배열에 없어서 기반 SpawnParticles가 부르지 않는다.
	UParticleLODLevel* LODLevel = GetCurrentLODLevel();
	UParticleModuleTypeDataBase* TypeData = LODLevel ? LODLevel->TypeDataModule : nullptr;
	if (TypeData && TypeData->bEnabled && Template->TypeDataOffset != INDEX_NONE)
	{
		const FParticleSpawnContext Context{ *this, Template->TypeDataOffset, SpawnTime, Particle };
		TypeData->Spawn(Context);
	}
	FParticleEmitterInstance::PostSpawn(Particle, SpawnTime);
}

void FParticleBeamEmitterInstance::Tick(float DeltaTime)
{
	// 파티클 생성·수명·모듈 실행은 기반 Tick이 한다. 기반 Tick이 시작할 때 UpdateTransforms를 하므로 아래 행렬은 이번 프레임 값이다.
	FParticleEmitterInstance::Tick(DeltaTime);

	const UParticleModuleTypeDataBeam* BeamData = GetBeamTypeData();
	if (!BeamData || ActiveParticles == 0)
	{
		return;
	}

	// 이미터 로컬 → 월드. EmitterToSimulation은 이미터 로컬 → 시뮬레이션 공간, SimulationToWorld는 시뮬레이션 공간 → 월드다 (행 벡터라 왼쪽이 먼저).
	const FMatrix EmitterToWorld = EmitterToSimulation * SimulationToWorld;
	const FVector SourceSim = EmitterToSimulation.TransformPosition(FVector(0.0f, 0.0f, 0.0f));
	const FVector SourceWorld = EmitterToWorld.TransformPosition(FVector(0.0f, 0.0f, 0.0f));

	FVector TargetWorld;
	if (BeamData->BeamMethod == EBeamMethod::Target)
	{
		// TargetPoint는 컴포넌트 로컬 좌표다
		TargetWorld = ComponentToWorld.TransformPosition(BeamData->TargetPoint);
	}
	else
	{
		// 이미터 X축 방향으로 Distance만큼
		const FVector Direction = EmitterToWorld.TransformVector(FVector(1.0f, 0.0f, 0.0f)).GetSafeNormal(SMALL_NUMBER, FVector(1.0f, 0.0f, 0.0f));
		TargetWorld = SourceWorld + Direction * BeamData->Distance;
	}

	// 노이즈 곡선에 쓰는 이번 프레임 값 (접선 길이는 Noise 모듈의 상수다)
	CurrentSourceTangentDirection = EmitterToWorld.TransformVector(FVector(1.0f, 0.0f, 0.0f)).GetSafeNormal(SMALL_NUMBER, FVector(1.0f, 0.0f, 0.0f));
	if (const UParticleModuleBeamNoise* NoiseModule = GetBeamNoiseModule())
	{
		CurrentNoiseTangentStrength = NoiseModule->NoiseTangentStrength;
	}

	for (int32 i = 0; i < ActiveParticles; ++i)
	{
		FBeamPayload* Payload = GetBeamPayload(i);
		if (!Payload)
		{
			return;
		}
		Payload->SourcePoint = SourceWorld;
		Payload->TargetPoint = TargetWorld;

		// 중간 점: 시작점과 끝점 사이를 PointCount + 1 구간으로 균등하게 나눈 위치 (k번 점 = (k + 1) / (PointCount + 1) 지점).
		// 지금은 직선 위의 점이다. Noise(6-3)는 이 점을 흔들고, Tangent(6-4)는 곡선 위의 위치로 바꾼다.
		if (FVector* Points = GetBeamPoints(i))
		{
			const float Step = 1.0f / static_cast<float>(PointCount + 1);
			const FVector Delta = TargetWorld - SourceWorld;
			for (int32 k = 0; k < PointCount; ++k)
			{
				Points[k] = SourceWorld + Delta * (Step * static_cast<float>(k + 1));
			}
		}

		// UE처럼 파티클 위치를 빔 시작점에 둔다. 속도 모듈이 있어도 빔은 이미터에서 시작한다.
		// SubmitRender가 아직 false라 Sprite로 그려지는 동안은 이 위치(시작점)에 스프라이트가 보인다.
		FBaseParticle* Particle = reinterpret_cast<FBaseParticle*>(ParticleData + static_cast<size_t>(ParticleIndices[i]) * ParticleStride);
		Particle->Location = SourceSim;
		Particle->OldLocation = SourceSim;
	}
}

bool FParticleBeamEmitterInstance::EnsureRenderResources(int32 SheetCount)
{
	// 빔 수 × 구간 수 × 띠 수만큼 사각형이 필요하다. 인덱스는 Sprite와 같은 고정 패턴(사각형마다 0,1,2, 0,2,3)이라 크기가 바뀔 때만 채운다.
	const int32 Needed = MaxActiveParticles * (LineCapacity - 1) * SheetCount;
	if (Needed > Capacity)
	{
		int32 NewCapacity = Capacity > 0 ? Capacity : 16;
		while (NewCapacity < Needed) NewCapacity *= 2;
		if (NewCapacity > MaxParticleLimit) NewCapacity = MaxParticleLimit;

		VertexBuffer = RenderCommand::CreateDynamicVertexBuffer(
			sizeof(FParticleSpriteVertex) * SpriteVerticesPerParticle * NewCapacity, sizeof(FParticleSpriteVertex));
		IndexBuffer = RenderCommand::CreateDynamicIndexBuffer(SpriteIndicesPerParticle * NewCapacity);
		if (!VertexBuffer || !IndexBuffer)
		{
			VertexBuffer.reset();
			IndexBuffer.reset();
			Capacity = 0;
			return false;
		}
		TArray<uint32> Indices;
		Indices.SetNum(SpriteIndicesPerParticle * NewCapacity);
		FillSpriteIndices(Indices.GetData(), NewCapacity);
		RenderCommand::UpdateBufferData(IndexBuffer.get(), Indices.GetData(), sizeof(uint32) * Indices.Num());
		Capacity = NewCapacity;
	}
	return true;
}

bool FParticleBeamEmitterInstance::SubmitRender(FRenderQueue& RenderQueue, const FViewContext& ViewContext, float ComponentDistanceSquared)
{
	const UParticleModuleTypeDataBeam* BeamData = GetBeamTypeData();
	if (!BeamData || Template->TypeDataOffset == INDEX_NONE)
	{
		return false;	// Payload가 없으면(레이아웃 갱신 전) Sprite로 그린다
	}
	// 여기부터는 빔 이미터가 직접 그린다. 그릴 것이 없어도 true를 돌려 Sprite 경로로 넘어가지 않게 한다.
	const int32 SheetCount = BeamData->GetSheetCount();
	if (ActiveParticles <= 0 || !EnsureRenderResources(SheetCount))
	{
		return true;
	}
	// Required.Material (UE와 같다). 비어 있거나 파티클용이 아니면 Sprite와 같은 엔진 기본 머티리얼
	UMaterial* Material = GetParticleRenderMaterial(*this, ResourcePaths::ParticleSpriteMaterial);
	if (!Material)
	{
		return true;
	}

	const FParticleSpriteCamera Camera = FParticleSpriteCamera::FromView(ViewContext.CameraPosition, ViewContext.CameraForward);
	// 빔 하나 = 구간(사각형) 최대 MaxSegments개 × 띠 SheetCount장. 버퍼(Capacity, 사각형 수)에 들어가는 빔까지만 그린다.
	const int32 MaxSegments = LineCapacity - 1;
	const int32 QuadsPerBeam = MaxSegments * SheetCount;
	const int32 MaxBeams = QuadsPerBeam > 0 ? Capacity / QuadsPerBeam : 0;
	const int32 BeamCount = ActiveParticles < MaxBeams ? ActiveParticles : MaxBeams;

	// 알파 블렌드는 먼 빔부터 그린다 (빔 중점과 카메라 거리 기준). Additive는 순서와 무관하다.
	SortScratch.SetNum(BeamCount);
	for (int32 i = 0; i < BeamCount; ++i)
	{
		const FBeamPayload* Payload = GetBeamPayload(i);
		const FVector Mid = (Payload->SourcePoint + Payload->TargetPoint) * 0.5f;
		const FVector ToCamera = Mid - Camera.Position;
		SortScratch[i].DistanceSquared = ToCamera.Dot(ToCamera);
		SortScratch[i].Position = i;
	}
	if (NeedsBackToFrontSort(Material))
	{
		std::sort(SortScratch.begin(), SortScratch.end(),
			[](const FParticleSortItem& A, const FParticleSortItem& B) { return A.DistanceSquared > B.DistanceSquared; });
	}

	StagingVertices.SetNum(SpriteVerticesPerParticle * BeamCount * QuadsPerBeam);
	LineScratch.SetNum(LineCapacity);
	SideScratch.SetNum(LineCapacity);
	DirectionScratch.SetNum(LineCapacity);
	SheetSideScratch.SetNum(LineCapacity);
	UScratch.SetNum(LineCapacity);
	const float TextureTile = BeamData->TextureTile;
	int32 Count = 0;	// 채운 사각형 수
	for (int32 Out = 0; Out < BeamCount; ++Out)
	{
		const int32 ActiveIndex = SortScratch[Out].Position;
		const FBeamPayload* Payload = GetBeamPayload(ActiveIndex);
		const FBaseParticle* Particle = reinterpret_cast<const FBaseParticle*>(
			ParticleData + static_cast<size_t>(ParticleIndices[ActiveIndex]) * ParticleStride);

		const FVector Source = Payload->SourcePoint;
		const FVector Target = Payload->TargetPoint;
		const FVector BeamDirection = (Target - Source).GetSafeNormal();
		if (BeamDirection.Dot(BeamDirection) <= 0.0f)
		{
			continue;	// 길이가 0인 빔은 그리지 않는다
		}
		// 빔 두께는 Sprite 크기와 같은 규약으로 Size.X 전체를 쓴다
		const float HalfWidth = std::fabs(Particle->Size.X) * 0.5f;

		// 중심선: 시작점, 중간 점(Payload), 끝점. 노이즈가 있으면 노이즈 점과 그 사이 곡선.
		FVector* Line = LineScratch.GetData();
		const int32 Segments = BuildBeamLine(ActiveIndex, Line) - 1;
		if (Segments <= 0)
		{
			continue;
		}

		// 점마다 폭 방향을 하나씩 정한다. 이웃한 두 사각형이 같은 점에서 같은 모서리를 쓰므로 이음매에 틈이 생기지 않는다.
		// 방향은 그 점에서의 진행 방향(앞뒤 점을 잇는 방향, 끝점은 붙은 구간 방향)이고, 폭 방향 = 진행 방향 × 카메라를 보는 방향이라
		// 띠가 늘 카메라를 향한다. 외적이 0이면(빔이 카메라를 정면으로 향함) 빔 전체 방향, 그래도 0이면 카메라 오른쪽 축을 쓴다.
		FVector* Sides = SideScratch.GetData();
		FVector* Directions = DirectionScratch.GetData();
		for (int32 p = 0; p <= Segments; ++p)
		{
			const FVector& Prev = Line[p > 0 ? p - 1 : p];
			const FVector& Next = Line[p < Segments ? p + 1 : p];
			FVector Direction = (Next - Prev).GetSafeNormal();
			if (Direction.Dot(Direction) <= 0.0f) Direction = BeamDirection;
			Directions[p] = Direction;
			FVector Axis = FVector::Cross(Direction, Camera.Position - Line[p]).GetSafeNormal();
			if (Axis.Dot(Axis) <= 0.0f) Axis = Camera.Right;
			Sides[p] = Axis * HalfWidth;
		}

		// 점마다 U: 시작점부터의 누적 길이 / 전체 길이 × TextureTile. 점 간격이 고르지 않거나 빔이 휘어도 텍스처가 길이 방향으로 고르게 늘어난다.
		// 전체 길이가 0이면(모든 점이 겹침) 구간 번호로 나눈다.
		float* Us = UScratch.GetData();
		Us[0] = 0.0f;
		for (int32 p = 1; p <= Segments; ++p)
		{
			const FVector Step = Line[p] - Line[p - 1];
			Us[p] = Us[p - 1] + std::sqrt(Step.Dot(Step));
		}
		const float TotalLength = Us[Segments];
		for (int32 p = 0; p <= Segments; ++p)
		{
			Us[p] = TextureTile * (TotalLength > 0.0f ? Us[p] / TotalLength : static_cast<float>(p) / static_cast<float>(Segments));
		}

		// 띠마다(Sheets) 폭 방향을 진행 방향 축으로 π / SheetCount × Sheet만큼 돌린다 (UE: FQuat(Right, Angle).RotateVector(Up)).
		// 폭 방향은 진행 방향에 수직이라 로드리게스 회전이 Side × cos + (Direction × Side) × sin으로 줄어든다. 첫 띠(0)는 돌리지 않는다.
		for (int32 Sheet = 0; Sheet < SheetCount; ++Sheet)
		{
			const FVector* SheetSides = Sides;
			if (Sheet > 0)
			{
				const float Angle = (3.14159265358979f / static_cast<float>(SheetCount)) * static_cast<float>(Sheet);
				const float Cos = std::cos(Angle);
				const float Sin = std::sin(Angle);
				FVector* Rotated = SheetSideScratch.GetData();
				for (int32 p = 0; p <= Segments; ++p)
				{
					Rotated[p] = Sides[p] * Cos + FVector::Cross(Directions[p], Sides[p]) * Sin;
				}
				SheetSides = Rotated;
			}

			// 구간마다 사각형 하나. 정점 순서는 FillSpriteIndices(0,1,2, 0,2,3)에 맞춘 둘레 순서다. U = 빔 길이 방향, V = 폭 방향.
			for (int32 s = 0; s < Segments; ++s)
			{
				const float U0 = Us[s];
				const float U1 = Us[s + 1];
				FParticleSpriteVertex* V = StagingVertices.GetData() + static_cast<size_t>(Count) * SpriteVerticesPerParticle;
				V[0].Position = Line[s] - SheetSides[s];			V[0].UV = FVector2(U0, 1.0f);
				V[1].Position = Line[s] + SheetSides[s];			V[1].UV = FVector2(U0, 0.0f);
				V[2].Position = Line[s + 1] + SheetSides[s + 1];	V[2].UV = FVector2(U1, 0.0f);
				V[3].Position = Line[s + 1] - SheetSides[s + 1];	V[3].UV = FVector2(U1, 1.0f);
				for (int32 Corner = 0; Corner < 4; ++Corner)
				{
					V[Corner].Color = Particle->Color;
				}
				++Count;
			}
		}
	}
	if (Count <= 0)
	{
		return true;
	}

	RenderCommand::UpdateBufferData(VertexBuffer.get(), StagingVertices.GetData(),
		static_cast<uint32>(sizeof(FParticleSpriteVertex) * SpriteVerticesPerParticle * Count));

	FRenderPacket Packet;
	Packet.Model = RenderQueue.StoreWorldMatrix(FMatrix::Identity);
	Packet.DynamicVertexBuffer = VertexBuffer.get();
	Packet.DynamicIndexBuffer = IndexBuffer.get();
	Packet.Material = Material;
	Packet.IndexCount = static_cast<uint32>(SpriteIndicesPerParticle * Count);
	Packet.CameraToParticleDistance = ComponentDistanceSquared;
	RenderQueue.Add(Packet);
	return true;
}

bool FParticleBeamEmitterInstance::ExpandWorldBounds(FBox& InOutBox, bool& bInOutHasAny) const
{
	// Payload가 없으면(레이아웃 갱신 전) Sprite로 그려지므로 Sprite 상자를 쓴다
	if (!GetBeamTypeData() || Template->TypeDataOffset == INDEX_NONE)
	{
		return FParticleEmitterInstance::ExpandWorldBounds(InOutBox, bInOutHasAny);
	}
	if (!ParticleData || !ParticleIndices || ActiveParticles <= 0)
	{
		return false;
	}

	for (int32 i = 0; i < ActiveParticles; ++i)
	{
		const FBeamPayload* Payload = GetBeamPayload(i);
		const FBaseParticle* Particle = reinterpret_cast<const FBaseParticle*>(
			ParticleData + static_cast<size_t>(ParticleIndices[i]) * ParticleStride);

		// 띠의 모서리는 중심선의 각 점(시작점, 중간 점, 끝점)에서 폭 방향으로 HalfWidth만큼 떨어져 있다.
		// 폭 방향은 카메라마다 다르지만 늘 그 점의 진행 방향 D에 수직이므로(SubmitRender와 같은 D), 모서리는
		// "D에 수직인 반지름 HalfWidth의 원" 위에 있다. 이 원이 축 i로 퍼지는 길이는 HalfWidth * sqrt(1 - D_i^2)이다.
		// 그래서 빔이 X축으로 뻗으면 X 방향으로는 상자가 끝점보다 더 나가지 않고, Y/Z로만 HalfWidth만큼 넓어진다.
		// 중간 점까지 넣어야 Noise나 Tangent로 휜 빔이 시작점~끝점 상자 밖으로 나가도 컬링되지 않는다.
		const float HalfWidth = std::fabs(Particle->Size.X) * 0.5f;
		// SubmitRender와 같은 중심선 (노이즈가 있으면 노이즈 점과 곡선)
		BoundsLineScratch.SetNum(LineCapacity);
		const FVector* Line = BoundsLineScratch.GetData();
		const int32 Segments = BuildBeamLine(i, BoundsLineScratch.GetData()) - 1;
		if (Segments < 0)
		{
			continue;
		}
		const auto LinePoint = [&](int32 p) -> const FVector& { return Line[std::clamp(p, 0, Segments)]; };
		const FVector BeamDirection = (Payload->TargetPoint - Payload->SourcePoint).GetSafeNormal();

		FVector Min = Payload->SourcePoint;
		FVector Max = Payload->SourcePoint;
		for (int32 p = 0; p <= Segments; ++p)
		{
			const FVector Point = LinePoint(p);
			FVector Direction = (LinePoint(p + 1) - LinePoint(p - 1)).GetSafeNormal();
			if (Direction.Dot(Direction) <= 0.0f) Direction = BeamDirection;
			// 방향을 모르면(길이 0인 빔) 모든 축으로 반폭만큼 부풀린다
			const auto Spread = [&](float D) { return HalfWidth * std::sqrt(std::fmax(0.0f, 1.0f - D * D)); };
			const FVector Extent = Direction.Dot(Direction) > 0.0f
				? FVector(Spread(Direction.X), Spread(Direction.Y), Spread(Direction.Z))
				: FVector(HalfWidth, HalfWidth, HalfWidth);
			const FVector Lo = Point - Extent;
			const FVector Hi = Point + Extent;
			Min = FVector(std::fmin(Min.X, Lo.X), std::fmin(Min.Y, Lo.Y), std::fmin(Min.Z, Lo.Z));
			Max = FVector(std::fmax(Max.X, Hi.X), std::fmax(Max.Y, Hi.Y), std::fmax(Max.Z, Hi.Z));
		}
		const FBox BeamBox{ Min, Max };
		if (!bInOutHasAny)
		{
			InOutBox = BeamBox;
			bInOutHasAny = true;
		}
		else
		{
			InOutBox.Expand(BeamBox);
		}
	}
	return true;
}
