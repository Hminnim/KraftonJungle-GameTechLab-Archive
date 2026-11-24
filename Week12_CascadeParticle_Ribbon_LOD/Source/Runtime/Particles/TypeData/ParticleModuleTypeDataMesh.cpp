#include "EnginePCH.h"
#include "Particles/TypeData/ParticleModuleTypeDataMesh.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/ParticleHelper.h"
#include "Particles/ParticleSerialization.h"
#include "Render/RenderPacket.h"
#include "Render/RenderCommand.h"
#include "Render/Buffer.h"
#include "Math/Box.h"
#include <algorithm>
#include <cmath>

namespace
{
	const FBaseParticle& ParticleAt(const FParticleEmitterInstance& Instance, int32 Position)
	{
		return *reinterpret_cast<const FBaseParticle*>(
			Instance.ParticleData + static_cast<size_t>(Instance.ParticleIndices[Position]) * Instance.ParticleStride);
	}
}

UParticleModuleTypeDataMesh::UParticleModuleTypeDataMesh() = default;

void UParticleModuleTypeDataMesh::Serialize(json& Handle, bool bIsLoading)
{
	// Mesh 같은 UPROPERTY는 리플렉션이 처리한다. Distribution과 머티리얼 배열은 직접 저장한다.
	Super::Serialize(Handle, bIsLoading);
	ParticleJson::SerializeDistribution(Handle, "RollPitchYawRange", RollPitchYawRange, bIsLoading);
	if (bIsLoading)
	{
		MaterialOverrides.Reset();
		if (Handle.contains("MaterialOverrides") && Handle["MaterialOverrides"].is_array())
		{
			for (const json& SlotJson : Handle["MaterialOverrides"])
			{
				MaterialOverrides.Add(SlotJson.is_null() ? nullptr : UMaterial::LoadMaterial(SlotJson));
			}
		}
	}
	else
	{
		json Slots = json::array();
		for (UMaterial* Override : MaterialOverrides)
		{
			Slots.push_back(Override ? UMaterial::SaveMaterial(Override) : json(nullptr));
		}
		Handle["MaterialOverrides"] = Slots;
	}
}

FParticleEmitterInstance* UParticleModuleTypeDataMesh::CreateInstance() const
{
	return new FParticleMeshEmitterInstance();
}

uint32 UParticleModuleTypeDataMesh::RequiredBytes(UParticleModuleTypeDataBase* TypeData)
{
	(void)TypeData;
	return sizeof(FMeshRotationPayload);
}

UMaterial* UParticleModuleTypeDataMesh::ResolveMaterial(uint32 SlotIndex) const
{
	if (Mesh == nullptr)
	{
		return nullptr;
	}
	if (static_cast<int32>(SlotIndex) < MaterialOverrides.Num() && MaterialOverrides[SlotIndex])
	{
		return MaterialOverrides[SlotIndex];
	}
	return Mesh->GetMaterial(SlotIndex);
}

namespace
{
	// 파티클의 회전 Payload. 이 이미터가 메시 TypeData를 쓰지 않으면 같은 오프셋이 다른 TypeData의 Payload일 수 있어 건드리지 않는다.
	FMeshRotationPayload* RotationPayloadOf(FParticleEmitterInstance& Owner, FBaseParticle* Particle)
	{
		UParticleEmitter* Template = Owner.Template;
		UParticleLODLevel* LODLevel = Template ? Template->GetLODLevel(0) : nullptr;
		if (!LODLevel || !LODLevel->TypeDataModule || !LODLevel->TypeDataModule->IsA(UParticleModuleTypeDataMesh::StaticClass()))
		{
			return nullptr;
		}
		const int32 Offset = Template->TypeDataOffset;
		if (Offset == INDEX_NONE)
		{
			return nullptr;
		}
		return reinterpret_cast<FMeshRotationPayload*>(reinterpret_cast<uint8*>(Particle) + Offset);
	}
}

UParticleModuleMeshRotation::UParticleModuleMeshRotation()
{
	bSpawnModule = true;
}

void UParticleModuleMeshRotation::Serialize(json& Handle, bool bIsLoading)
{
	Super::Serialize(Handle, bIsLoading);
	ParticleJson::SerializeDistribution(Handle, "StartRotation", StartRotation, bIsLoading);
}

void UParticleModuleMeshRotation::Spawn(const FParticleSpawnContext& Context)
{
	if (FMeshRotationPayload* Payload = RotationPayloadOf(Context.Owner, Context.ParticleBase))
	{
		Payload->Rotation += StartRotation.GetValue(Context.Owner.EmitterTime, &Context.Owner.RandomStream);
	}
}

UParticleModuleMeshRotationRate::UParticleModuleMeshRotationRate()
{
	bSpawnModule = true;
}

void UParticleModuleMeshRotationRate::Serialize(json& Handle, bool bIsLoading)
{
	Super::Serialize(Handle, bIsLoading);
	ParticleJson::SerializeDistribution(Handle, "StartRotationRate", StartRotationRate, bIsLoading);
}

void UParticleModuleMeshRotationRate::Spawn(const FParticleSpawnContext& Context)
{
	if (FMeshRotationPayload* Payload = RotationPayloadOf(Context.Owner, Context.ParticleBase))
	{
		Payload->RotationRate += StartRotationRate.GetValue(Context.Owner.EmitterTime, &Context.Owner.RandomStream);
	}
}

int32 FParticleMeshEmitterInstance::GetRotationPayloadOffset() const
{
	return GetMeshTypeData() && Template ? Template->TypeDataOffset : INDEX_NONE;
}

void FParticleMeshEmitterInstance::PostSpawn(FBaseParticle* Particle, float SpawnTime)
{
	FParticleEmitterInstance::PostSpawn(Particle, SpawnTime);
	// 모듈의 Spawn(회전 더하기)이 끝난 뒤라 Rotation은 건드리지 않고 사전 회전만 정한다 (UE: PostSpawn의 InitialOrientation)
	UParticleModuleTypeDataMesh* TypeData = GetMeshTypeData();
	const int32 Offset = GetRotationPayloadOffset();
	if (TypeData && Offset != INDEX_NONE)
	{
		FMeshRotationPayload* Payload = reinterpret_cast<FMeshRotationPayload*>(reinterpret_cast<uint8*>(Particle) + Offset);
		Payload->InitialOrientation = TypeData->RollPitchYawRange.GetValue(EmitterTime, &RandomStream);
	}
}

FParticleMeshEmitterInstance::FParticleMeshEmitterInstance() = default;
FParticleMeshEmitterInstance::~FParticleMeshEmitterInstance() = default;

UParticleModuleTypeDataMesh* FParticleMeshEmitterInstance::GetMeshTypeData() const
{
	UParticleLODLevel* LODLevel = Template ? Template->GetLODLevel(0) : nullptr;
	return LODLevel ? Cast<UParticleModuleTypeDataMesh>(LODLevel->TypeDataModule) : nullptr;
}

void FParticleMeshEmitterInstance::Tick(float DeltaTime)
{
	// 지난 프레임에 늘리면서 남겨 둔 이전 버퍼는 그 프레임의 그리기가 끝났으므로 이제 해제해도 된다
	RetiredBuffers.Reset();
	FParticleEmitterInstance::Tick(DeltaTime);

	// 회전 적분: Rotation += RotationRate * dt. 이번 프레임에 생긴 파티클도 같이 더한다 (생성 시점부터가 아니라 한 프레임 분량).
	const int32 Offset = GetRotationPayloadOffset();
	if (Offset != INDEX_NONE && ParticleData && ParticleIndices)
	{
		for (int32 Position = 0; Position < ActiveParticles; ++Position)
		{
			uint8* Particle = ParticleData + static_cast<size_t>(ParticleIndices[Position]) * ParticleStride;
			FMeshRotationPayload* Payload = reinterpret_cast<FMeshRotationPayload*>(Particle + Offset);
			Payload->Rotation += Payload->RotationRate * DeltaTime;
		}
	}
}

int32 FParticleMeshEmitterInstance::FillInstances(const FVector* SortFromCamera)
{
	if (!ParticleData || !ParticleIndices || ActiveParticles <= 0)
	{
		return 0;
	}
	const bool bLocalSpace = SimulatesInLocalSpace();
	const int32 PayloadOffset = GetRotationPayloadOffset();
	Staging.SetNum(ActiveParticles);
	for (int32 Position = 0; Position < ActiveParticles; ++Position)
	{
		const FBaseParticle& Particle = ParticleAt(*this, Position);
		// 시뮬레이션 공간에서의 축(행)과 이동. 행 i = 크기 i × (사전 회전 후 회전을 적용한 메시 로컬 축 i)다.
		// 크기는 메시 로컬 축에 먼저 곱하므로 회전해도 비균등 크기가 찌그러지지 않는다.
		FVector AxisX(1.0f, 0.0f, 0.0f);
		FVector AxisY(0.0f, 1.0f, 0.0f);
		FVector AxisZ(0.0f, 0.0f, 1.0f);
		if (PayloadOffset != INDEX_NONE)
		{
			const FMeshRotationPayload* Payload = reinterpret_cast<const FMeshRotationPayload*>(reinterpret_cast<const uint8*>(&Particle) + PayloadOffset);
			const FQuat Initial = FQuat::MakeFromEuler(FMath::DegreesToRadians(Payload->InitialOrientation.X),
				FMath::DegreesToRadians(Payload->InitialOrientation.Y), FMath::DegreesToRadians(Payload->InitialOrientation.Z));
			const FQuat Current = FQuat::MakeFromEuler(FMath::DegreesToRadians(Payload->Rotation.X),
				FMath::DegreesToRadians(Payload->Rotation.Y), FMath::DegreesToRadians(Payload->Rotation.Z));
			AxisX = Current.RotateVector(Initial.RotateVector(AxisX));
			AxisY = Current.RotateVector(Initial.RotateVector(AxisY));
			AxisZ = Current.RotateVector(Initial.RotateVector(AxisZ));
		}
		AxisX = AxisX * Particle.Size.X;
		AxisY = AxisY * Particle.Size.Y;
		AxisZ = AxisZ * Particle.Size.Z;
		FVector Location = Particle.Location;
		if (bLocalSpace)
		{
			// 로컬 공간 이미터는 컴포넌트 공간 값을 월드로 옮긴다 (행은 방향이라 TransformVector, 이동은 TransformPosition)
			AxisX = SimulationToWorld.TransformVector(AxisX);
			AxisY = SimulationToWorld.TransformVector(AxisY);
			AxisZ = SimulationToWorld.TransformVector(AxisZ);
			Location = SimulationToWorld.TransformPosition(Location);
		}
		FMeshParticleInstance& Out = Staging[Position];
		Out.Row0 = FVector4(AxisX.X, AxisX.Y, AxisX.Z, Location.X);
		Out.Row1 = FVector4(AxisY.X, AxisY.Y, AxisY.Z, Location.Y);
		Out.Row2 = FVector4(AxisZ.X, AxisZ.Y, AxisZ.Z, Location.Z);
		Out.Color = Particle.Color;
	}
	if (SortFromCamera)
	{
		const FVector Camera = *SortFromCamera;
		auto DistanceSquared = [&Camera](const FMeshParticleInstance& Instance)
			{
				const float X = Instance.Row0.W - Camera.X;
				const float Y = Instance.Row1.W - Camera.Y;
				const float Z = Instance.Row2.W - Camera.Z;
				return X * X + Y * Y + Z * Z;
			};
		// 같은 거리는 이전 순서를 유지해서 프레임마다 깜빡이지 않게 한다
		std::stable_sort(Staging.begin(), Staging.end(), [&DistanceSquared](const FMeshParticleInstance& A, const FMeshParticleInstance& B)
			{
				return DistanceSquared(A) > DistanceSquared(B);
			});
	}
	return ActiveParticles;
}

bool FParticleMeshEmitterInstance::EnsureInstanceBuffer(uint32 InstanceCount)
{
	if (InstanceBuffer && InstanceCount <= InstanceCapacity)
	{
		return true;
	}
	// 2배씩 늘려서 파티클 수가 조금씩 늘 때마다 다시 만들지 않는다
	uint32 NewCapacity = std::max<uint32>(InstanceCapacity * 2, 64);
	while (NewCapacity < InstanceCount)
	{
		NewCapacity *= 2;
	}
	TUniquePtr<FStructuredBuffer> NewBuffer = RenderCommand::CreateDynamicStructuredBuffer(NewCapacity, sizeof(FMeshParticleInstance));
	if (!NewBuffer)
	{
		return InstanceBuffer != nullptr && InstanceCount <= InstanceCapacity;
	}
	if (InstanceBuffer)
	{
		RetiredBuffers.Add(std::move(InstanceBuffer));
	}
	InstanceBuffer = std::move(NewBuffer);
	InstanceCapacity = NewCapacity;
	return true;
}

bool FParticleMeshEmitterInstance::SubmitRender(FRenderQueue& RenderQueue, const FViewContext& ViewContext, float ComponentDistanceSquared)
{
	UParticleModuleTypeDataMesh* TypeData = GetMeshTypeData();
	UStaticMesh* Mesh = TypeData ? TypeData->Mesh : nullptr;
	// 메시가 없거나 렌더 리소스가 없으면 아무것도 그리지 않는다. false를 돌려주면 Sprite로 그려져 의도와 다른 모양이 나온다.
	if (!Mesh || !Mesh->GetVertexBuffer(0) || !Mesh->GetIndexBuffer(0))
	{
		return true;
	}
	// 반투명 머티리얼이 하나라도 있으면 먼 파티클부터 놓는다 (모든 섹션이 같은 인스턴스 버퍼를 읽으므로 한 번만 정렬한다)
	bool bSortBackToFront = false;
	for (const FStaticMeshSection& Section : Mesh->GetMeshData(0).Sections)
	{
		const UMaterial* Material = TypeData->ResolveMaterial(Section.MaterialSlotIndex);
		bSortBackToFront |= Material && Material->PSOType == EPSOType::StaticMesh_Translucent;
	}
	const FVector CameraPosition = ViewContext.CameraPosition;
	const int32 Count = FillInstances(bSortBackToFront ? &CameraPosition : nullptr);
	if (Count <= 0 || !EnsureInstanceBuffer(static_cast<uint32>(Count)))
	{
		return true;
	}
	RenderCommand::UpdateBufferData(InstanceBuffer.get(), Staging.GetData(), static_cast<uint32>(sizeof(FMeshParticleInstance)) * Count);

	// 섹션(머티리얼 구간)마다 패킷 하나. 모든 섹션이 같은 인스턴스 버퍼를 읽는다.
	for (const FStaticMeshSection& Section : Mesh->GetMeshData(0).Sections)
	{
		UMaterial* Material = TypeData->ResolveMaterial(Section.MaterialSlotIndex);
		if (!Material || Section.IndexCount == 0)
		{
			continue;
		}
		FRenderPacket Packet;
		// 인스턴스 값이 이미 월드 공간이라 오브젝트 행렬은 단위 행렬이다 (View/Projection만 오브젝트 상수에서 쓴다)
		Packet.Model = RenderQueue.StoreWorldMatrix(FMatrix::Identity);
		Packet.Mesh = Mesh;
		Packet.Material = Material;
		Packet.StartIndex = Section.StartIndex;
		Packet.IndexCount = Section.IndexCount;
		Packet.LODIndex = 0;
		Packet.InstanceBuffer = InstanceBuffer.get();
		Packet.InstanceCount = static_cast<uint32>(Count);
		Packet.CameraToParticleDistance = ComponentDistanceSquared;
		RenderQueue.Add(Packet);
	}
	return true;
}

bool FParticleMeshEmitterInstance::ExpandWorldBounds(FBox& InOutBox, bool& bInOutHasAny) const
{
	UParticleModuleTypeDataMesh* TypeData = GetMeshTypeData();
	UStaticMesh* Mesh = TypeData ? TypeData->Mesh : nullptr;
	if (!Mesh || !ParticleData || !ParticleIndices || ActiveParticles <= 0)
	{
		return false;
	}
	// 메시 로컬 상자에서 원점으로부터 가장 먼 축별 거리. 파티클의 회전은 원점 기준이라 이 값으로 만든 구는 어떤 회전에서도 메시를 덮는다.
	const FBox& Local = Mesh->GetRenderBounds();
	const float ExtentX = std::max(std::fabs(Local.Min.X), std::fabs(Local.Max.X));
	const float ExtentY = std::max(std::fabs(Local.Min.Y), std::fabs(Local.Max.Y));
	const float ExtentZ = std::max(std::fabs(Local.Min.Z), std::fabs(Local.Max.Z));

	const bool bLocalSpace = SimulatesInLocalSpace();
	for (int32 Position = 0; Position < ActiveParticles; ++Position)
	{
		const FBaseParticle& Particle = ParticleAt(*this, Position);
		const FVector Center = bLocalSpace ? SimulationToWorld.TransformPosition(Particle.Location) : Particle.Location;
		const float X = ExtentX * std::fabs(Particle.Size.X);
		const float Y = ExtentY * std::fabs(Particle.Size.Y);
		const float Z = ExtentZ * std::fabs(Particle.Size.Z);
		const float Radius = std::sqrt(X * X + Y * Y + Z * Z);
		const FBox ParticleBox{ Center - FVector(Radius, Radius, Radius), Center + FVector(Radius, Radius, Radius) };
		if (!bInOutHasAny)
		{
			InOutBox = ParticleBox;
			bInOutHasAny = true;
		}
		else
		{
			InOutBox.Expand(ParticleBox);
		}
	}
	return true;
}
