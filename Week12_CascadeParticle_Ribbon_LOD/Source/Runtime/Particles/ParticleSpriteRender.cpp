#include "EnginePCH.h"
#include "Particles/ParticleSpriteRender.h"
#include "Particles/ParticleEmitterInstances.h"
#include "Particles/ParticleHelper.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/ParticleModule.h"
#include "Render/Material.h"
#include "Asset/AssetManager.h"
#include <algorithm>
#include <cmath>

namespace
{
	const FBaseParticle& ParticleAt(const FParticleEmitterInstance& Instance, int32 Position)
	{
		return *reinterpret_cast<const FBaseParticle*>(
			Instance.ParticleData + static_cast<size_t>(Instance.ParticleIndices[Position]) * Instance.ParticleStride);
	}

	FVector WorldLocation(const FParticleEmitterInstance& Instance, const FBaseParticle& Particle, bool bLocalSpace)
	{
		return bLocalSpace ? Instance.SimulationToWorld.TransformPosition(Particle.Location) : Particle.Location;
	}
}

FParticleSpriteCamera FParticleSpriteCamera::FromView(const FVector& CameraPosition, const FVector& CameraForward)
{
	FParticleSpriteCamera Camera;
	Camera.Position = CameraPosition;
	FVector Forward = CameraForward.Normalized();
	if (Forward.Length() <= 1e-6f)
	{
		return Camera;	// 방향 정보가 없으면 기본 축(Y 오른쪽, Z 위쪽)을 쓴다
	}
	FVector Right = FVector::Cross(FVector(0.0f, 0.0f, 1.0f), Forward).Normalized();
	if (Right.Length() <= 1e-6f)
	{
		Right = FVector(0.0f, 1.0f, 0.0f);
	}
	Camera.Right = Right;
	Camera.Up = FVector::Cross(Forward, Right).Normalized();
	return Camera;
}

int32 FillSpriteVertices(const FParticleEmitterInstance& Instance, const FParticleSpriteCamera& Camera, bool bSortBackToFront,
	FParticleSpriteVertex* OutVertices, int32 Capacity, TArray<FParticleSortItem>& Scratch)
{
	if (!OutVertices || Capacity <= 0 || !Instance.ParticleData || !Instance.ParticleIndices || Instance.ActiveParticles <= 0)
	{
		return 0;
	}
	const bool bLocalSpace = Instance.SimulatesInLocalSpace();
	const int32 Count = Instance.ActiveParticles < Capacity ? Instance.ActiveParticles : Capacity;

	// 채울 순서: 정렬이 필요하면 먼 것부터, 아니면 ParticleIndices 순서
	Scratch.Reset();
	if (bSortBackToFront)
	{
		Scratch.Reserve(Count);
		for (int32 Position = 0; Position < Count; ++Position)
		{
			const FVector Delta = WorldLocation(Instance, ParticleAt(Instance, Position), bLocalSpace) - Camera.Position;
			Scratch.Add({ Delta.Dot(Delta), Position });
		}
		// 같은 거리는 위치 순서로 고정해서 프레임마다 깜빡이지 않게 한다
		std::sort(Scratch.begin(), Scratch.end(), [](const FParticleSortItem& A, const FParticleSortItem& B)
			{
				return A.DistanceSquared != B.DistanceSquared ? A.DistanceSquared > B.DistanceSquared : A.Position < B.Position;
			});
	}

	const FVector2 Corners[4] = { FVector2(-0.5f, -0.5f), FVector2(-0.5f, 0.5f), FVector2(0.5f, 0.5f), FVector2(0.5f, -0.5f) };
	const FVector2 UVs[4] = { FVector2(0.0f, 1.0f), FVector2(0.0f, 0.0f), FVector2(1.0f, 0.0f), FVector2(1.0f, 1.0f) };

	for (int32 Out = 0; Out < Count; ++Out)
	{
		const int32 Position = bSortBackToFront ? Scratch[Out].Position : Out;
		const FBaseParticle& Particle = ParticleAt(Instance, Position);
		const FVector Center = WorldLocation(Instance, Particle, bLocalSpace);
		const float Size = std::fabs(Particle.Size.X);
		const float Sin = std::sin(Particle.Rotation);
		const float Cos = std::cos(Particle.Rotation);

		FParticleSpriteVertex* Vertices = OutVertices + static_cast<size_t>(Out) * SpriteVerticesPerParticle;
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			// 화면 평면에서 회전한 뒤 카메라 오른쪽/위쪽 축으로 펼친다
			const float X = Corners[Corner].X * Cos - Corners[Corner].Y * Sin;
			const float Y = Corners[Corner].X * Sin + Corners[Corner].Y * Cos;
			Vertices[Corner].Position = Center + Camera.Right * (X * Size) + Camera.Up * (Y * Size);
			Vertices[Corner].UV = UVs[Corner];
			Vertices[Corner].Color = Particle.Color;
		}
	}
	return Count;
}

void FillSpriteIndices(uint32* OutIndices, int32 Capacity)
{
	if (!OutIndices || Capacity <= 0)
	{
		return;
	}
	for (int32 Particle = 0; Particle < Capacity; ++Particle)
	{
		const uint32 Base = static_cast<uint32>(Particle) * SpriteVerticesPerParticle;
		uint32* Out = OutIndices + static_cast<size_t>(Particle) * SpriteIndicesPerParticle;
		Out[0] = Base + 0; Out[1] = Base + 1; Out[2] = Base + 2;
		Out[3] = Base + 0; Out[4] = Base + 2; Out[5] = Base + 3;
	}
}

bool ExpandSpriteWorldBounds(const FParticleEmitterInstance& Instance, FBox& InOutBox, bool& bInOutHasAny)
{
	if (!Instance.ParticleData || !Instance.ParticleIndices || Instance.ActiveParticles <= 0)
	{
		return false;
	}
	const bool bLocalSpace = Instance.SimulatesInLocalSpace();
	for (int32 Position = 0; Position < Instance.ActiveParticles; ++Position)
	{
		const FBaseParticle& Particle = ParticleAt(Instance, Position);
		const FVector Center = WorldLocation(Instance, Particle, bLocalSpace);
		// 회전해도 가로세로 반지름이 Size * sqrt(2)/2를 넘지 않지만, 여유를 두어 Size만큼 부풀린다
		const float Radius = std::fabs(Particle.Size.X);
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

bool IsParticleSpriteMaterial(const UMaterial* Material)
{
	return Material && (Material->PSOType == EPSOType::ParticleSprite_AlphaBlend || Material->PSOType == EPSOType::ParticleSprite_Additive);
}

bool NeedsBackToFrontSort(const UMaterial* Material)
{
	return Material && Material->BlendState == EBlendState::AlphaBlend;
}

UMaterial* GetParticleRenderMaterial(const FParticleEmitterInstance& Instance, const char* DefaultMaterialKey)
{
	const UParticleLODLevel* LOD = Instance.GetCurrentLODLevel();
	UMaterial* Material = (LOD && LOD->RequiredModule) ? LOD->RequiredModule->Material : nullptr;
	if (!Material && Instance.Template && Instance.Template->GetLODLevel(0) && Instance.Template->GetLODLevel(0)->RequiredModule)
	{
		Material = Instance.Template->GetLODLevel(0)->RequiredModule->Material;
	}
	if (IsParticleSpriteMaterial(Material))
	{
		return Material;
	}
	if (Material)
	{
		// 메시용 머티리얼은 셰이더 입력이 달라 그릴 수 없다. 매 프레임 불리므로 경고는 한 번만 남긴다.
		static bool bWarned = false;
		if (!bWarned)
		{
			HTR_LOG(Warning, "[Particle] Required.Material '{}' is not a particle material, using the default", Material->GetPath());
			bWarned = true;
		}
	}
	return UAssetManager::GetAssetByPath<UMaterial>(DefaultMaterialKey);
}
