#include "EnginePCH.h"
#include "Particles/ParticleModule.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleEmitterInstances.h"
#include "Particles/ParticleHelper.h"
#include "Particles/ParticleSerialization.h"
#include "Render/Material.h"
#include "Asset/AssetManager.h"
#include "Core/ResourcePaths.h"

UParticleModuleLifetime::UParticleModuleLifetime()
{
	bSpawnModule = true;
}

void UParticleModuleLifetime::Spawn(const FParticleSpawnContext& Context)
{
	SPAWN_INIT
	(void)ActiveParticles;
	(void)ParticleStride;
	(void)CurrentOffset;

	// Distribution의 Time은 이미터 시간이다 (파티클의 RelativeTime이 아니다)
	const float MaxLifetime = Lifetime.GetValue(Context.Owner.EmitterTime, &Context.Owner.RandomStream);
	if (Particle.OneOverMaxLifetime > 0.0f)
	{
		// 다른 수명 모듈이 이미 정했다: 수명을 더한다
		Particle.OneOverMaxLifetime = 1.0f / (MaxLifetime + 1.0f / Particle.OneOverMaxLifetime);
	}
	else
	{
		// 첫 수명 모듈. 0 이하는 0(나이로 죽지 않음)으로 둔다.
		Particle.OneOverMaxLifetime = MaxLifetime > 0.0f ? 1.0f / MaxLifetime : 0.0f;
	}
	// 다른 모듈이 RelativeTime > 1로 "만들지 않음"을 표시했으면 건드리지 않는다
	Particle.RelativeTime = Particle.RelativeTime > 1.0f ? Particle.RelativeTime : Context.SpawnTime * Particle.OneOverMaxLifetime;
}

UParticleModuleLocation::UParticleModuleLocation()
{
	bSpawnModule = true;
}

void UParticleModuleLocation::Spawn(const FParticleSpawnContext& Context)
{
	SPAWN_INIT
	(void)ActiveParticles;
	(void)ParticleStride;
	(void)CurrentOffset;

	const FVector LocationOffset = StartLocation.GetValue(Context.Owner.EmitterTime, &Context.Owner.RandomStream);
	// 이미터 로컬 오프셋을 시뮬레이션 공간으로 돌려 더한다 (UE: EmitterToSimulation.TransformVector)
	Particle.Location += Context.Owner.EmitterToSimulation.TransformVector(LocationOffset);
}

UParticleModuleVelocity::UParticleModuleVelocity()
{
	bSpawnModule = true;
}

void UParticleModuleVelocity::Spawn(const FParticleSpawnContext& Context)
{
	SPAWN_INIT
	(void)ActiveParticles;
	(void)ParticleStride;
	(void)CurrentOffset;

	// 난수는 StartVelocity(Uniform이면 3개), StartVelocityRadial 순서로 쓴다 (UE와 같은 순서)
	FVector Vel = StartVelocity.GetValue(Context.Owner.EmitterTime, &Context.Owner.RandomStream);
	// 방사 방향은 시뮬레이션 공간의 이미터 원점에서 파티클로 향한다 (UE: Particle.Location - EmitterToSimulation.GetOrigin())
	const FVector Origin = Context.Owner.EmitterToSimulation.TransformPosition(FVector(0.0f, 0.0f, 0.0f));
	const FVector FromOrigin = (Particle.Location - Origin).GetSafeNormal();
	// 속도는 이미터 로컬 방향으로 정한 값이라 시뮬레이션 공간으로 돌린다 (UE: bInWorldSpace가 false인 경로)
	Vel = Context.Owner.EmitterToSimulation.TransformVector(Vel);
	Vel += FromOrigin * StartVelocityRadial.GetValue(Context.Owner.EmitterTime, &Context.Owner.RandomStream);
	Particle.Velocity += Vel;
	Particle.BaseVelocity += Vel;
}

UParticleModuleSize::UParticleModuleSize()
{
	bSpawnModule = true;
}

void UParticleModuleSize::Spawn(const FParticleSpawnContext& Context)
{
	SPAWN_INIT
	(void)ActiveParticles;
	(void)ParticleStride;
	(void)CurrentOffset;

	const FVector Size = StartSize.GetValue(Context.Owner.EmitterTime, &Context.Owner.RandomStream);
	Particle.Size += Size;
	Particle.BaseSize += Size;
}

UParticleModuleColor::UParticleModuleColor()
{
	bSpawnModule = true;
}

void UParticleModuleColor::Spawn(const FParticleSpawnContext& Context)
{
	SPAWN_INIT
	(void)ActiveParticles;
	(void)ParticleStride;
	(void)CurrentOffset;

	// 난수는 StartColor(Uniform이면 3개), StartAlpha(Uniform이면 1개) 순서
	const FVector ColorVec = StartColor.GetValue(Context.Owner.EmitterTime, &Context.Owner.RandomStream);
	const float Alpha = StartAlpha.GetValue(Context.Owner.EmitterTime, &Context.Owner.RandomStream);
	Particle.Color.R = ColorVec.X;
	Particle.Color.G = ColorVec.Y;
	Particle.Color.B = ColorVec.Z;
	Particle.Color.A = Alpha;
	Particle.BaseColor = Particle.Color;
}

// ---------------------------------------------------------------------------
// 에셋 파일(.particle) 직렬화: UPROPERTY가 아닌 멤버(Distribution, 버스트 목록)만 추가로 쓴다
// ---------------------------------------------------------------------------

namespace
{
	// 이 모듈이 소유한 머티리얼 인스턴스만 지운다. 에셋은 공유라 두고, 엔진 종료 때 먼저 지워졌으면 건드리지 않는다.
	void DeleteOwnedMaterial(UMaterial* Material)
	{
		if (Material && Material->bIsInstance && GUObjectArray.Find(Material) != INDEX_NONE)
		{
			delete Material;
		}
	}

	UTexture2D* FindOrLoadParticleTexture(const FString& Path)
	{
		if (UTexture2D* Texture = UAssetManager::GetAssetByPath<UTexture2D>(Path))
		{
			return Texture;
		}
		return UAssetManager::Get().LoadTexture(Path);
	}

	// Base 에셋의 첫 텍스처만 바꾼 인스턴스
	UMaterial* MakeTextureInstance(const UMaterial* Base, UTexture2D* Texture)
	{
		UMaterial* Instance = UMaterial::CreateInstance(Base);
		if (Instance)
		{
			if (Instance->Textures.IsEmpty()) Instance->Textures.Add(Texture);
			else Instance->Textures[0] = Texture;
		}
		return Instance;
	}
}

UParticleModuleRequired::~UParticleModuleRequired()
{
	for (UMaterial* Retired : RetiredMaterials)
	{
		DeleteOwnedMaterial(Retired);
	}
	RetiredMaterials.Empty();
	DeleteOwnedMaterial(Material);
	Material = nullptr;
}

void UParticleModuleRequired::SetMaterial(UMaterial* NewMaterial)
{
	if (NewMaterial == Material)
	{
		return;
	}
	if (Material && Material->bIsInstance)
	{
		RetiredMaterials.Add(Material);
	}
	Material = NewMaterial;
}

void UParticleModuleRequired::SetMaterialTexture(UTexture2D* Texture, const char* DefaultMaterialKey)
{
	const UMaterial* Base = Material ? Material->GetBaseAsset() : nullptr;
	if (!Base)
	{
		Base = UAssetManager::GetAssetByPath<UMaterial>(DefaultMaterialKey);
	}
	if (!Base)
	{
		return;
	}
	if (!Texture || (!Base->Textures.IsEmpty() && Base->Textures[0] == Texture))
	{
		// 원본과 같으면 인스턴스가 필요 없다
		SetMaterial(const_cast<UMaterial*>(Base));
		return;
	}
	SetMaterial(MakeTextureInstance(Base, Texture));
}

void UParticleModuleRequired::Serialize(json& Handle, bool bIsLoading)
{
	Super::Serialize(Handle, bIsLoading);

	// 형식: null | { "Asset": 경로 } | { "Base": 원본 에셋 경로, "Texture": 텍스처 경로 }
	// UMaterial::SaveMaterial은 메시용이라 Additive 블렌드를 저장하지 못해서 파티클은 이 형식을 쓴다.
	if (!bIsLoading)
	{
		if (!Material)
		{
			Handle["Material"] = nullptr;
		}
		else if (!Material->GetPath().empty())
		{
			json Out = json::object();
			Out["Asset"] = Material->GetPath();
			Handle["Material"] = Out;
		}
		else
		{
			const UMaterial* Base = Material->GetBaseAsset();
			UTexture2D* Texture = Material->Textures.IsEmpty() ? nullptr : Material->Textures[0];
			json Out = json::object();
			Out["Base"] = Base ? Base->GetPath() : FString(ResourcePaths::ParticleSpriteMaterial);
			Out["Texture"] = (Texture && !Texture->GetPath().empty()) ? json(Texture->GetPath()) : json(nullptr);
			Handle["Material"] = Out;
		}
		return;
	}

	UMaterial* Loaded = nullptr;
	if (Handle.contains("Material"))
	{
		const json& In = Handle["Material"];
		if (In.is_object() && In.contains("Asset") && In["Asset"].is_string())
		{
			Loaded = UAssetManager::GetAssetByPath<UMaterial>(In["Asset"].get<FString>());
		}
		else if (In.is_object() && In.contains("Base") && In["Base"].is_string())
		{
			const UMaterial* Base = UAssetManager::GetAssetByPath<UMaterial>(In["Base"].get<FString>());
			if (!Base) Base = UAssetManager::GetAssetByPath<UMaterial>(ResourcePaths::ParticleSpriteMaterial);
			UTexture2D* Texture = (In.contains("Texture") && In["Texture"].is_string())
				? FindOrLoadParticleTexture(In["Texture"].get<FString>()) : nullptr;
			Loaded = (Base && Texture) ? MakeTextureInstance(Base, Texture) : const_cast<UMaterial*>(Base);
		}
	}
	else
	{
		// 옛 에셋: Required가 Texture(경로)와 BlendMode("Alpha"/"Additive")를 직접 가졌다. 같은 모양의 머티리얼로 바꾼다.
		const bool bAdditive = Handle.contains("BlendMode") && Handle["BlendMode"].is_string() && Handle["BlendMode"].get<FString>() == "Additive";
		UTexture2D* Texture = (Handle.contains("Texture") && Handle["Texture"].is_string())
			? FindOrLoadParticleTexture(Handle["Texture"].get<FString>()) : nullptr;
		if (bAdditive || Texture)
		{
			const UMaterial* Base = UAssetManager::GetAssetByPath<UMaterial>(
				bAdditive ? ResourcePaths::ParticleSpriteAdditiveMaterial : ResourcePaths::ParticleSpriteMaterial);
			Loaded = (Base && Texture) ? MakeTextureInstance(Base, Texture) : const_cast<UMaterial*>(Base);
		}
	}
	SetMaterial(Loaded);
}

void UParticleModuleSpawn::Serialize(json& Handle, bool bIsLoading)
{
	Super::Serialize(Handle, bIsLoading);
	ParticleJson::SerializeDistribution(Handle, "Rate", Rate, bIsLoading);
	ParticleJson::SerializeDistribution(Handle, "RateScale", RateScale, bIsLoading);

	constexpr size_t MaxBursts = 256;
	if (!bIsLoading)
	{
		json Bursts = json::array();
		for (const FParticleBurst& Burst : BurstList)
		{
			Bursts.push_back({ { "Count", Burst.Count }, { "CountLow", Burst.CountLow }, { "Time", Burst.Time } });
		}
		Handle["BurstList"] = Bursts;
		return;
	}
	if (!Handle.contains("BurstList"))
	{
		return;
	}
	const json& Source = Handle["BurstList"];
	if (!Source.is_array() || Source.size() > MaxBursts)
	{
		HTR_LOG(Warning, "[Particle] Load: BurstList is malformed, keeping default");
		return;
	}
	// 항목 하나라도 틀리면 목록 전체를 유지한다 (반쯤 읽은 목록을 쓰지 않는다)
	TArray<FParticleBurst> Loaded;
	for (const json& Item : Source)
	{
		if (!Item.is_object() || !Item.contains("Count") || !Item["Count"].is_number_integer() ||
			!Item.contains("CountLow") || !Item["CountLow"].is_number_integer() ||
			!Item.contains("Time") || !Item["Time"].is_number())
		{
			HTR_LOG(Warning, "[Particle] Load: a burst entry is malformed, keeping the default burst list");
			return;
		}
		FParticleBurst Burst;
		Burst.Count = Item["Count"].get<int32>();
		Burst.CountLow = Item["CountLow"].get<int32>();
		Burst.Time = Item["Time"].get<float>();
		Loaded.Add(Burst);
	}
	BurstList = Loaded;
}

void UParticleModuleLifetime::Serialize(json& Handle, bool bIsLoading)
{
	Super::Serialize(Handle, bIsLoading);
	ParticleJson::SerializeDistribution(Handle, "Lifetime", Lifetime, bIsLoading);
}

void UParticleModuleLocation::Serialize(json& Handle, bool bIsLoading)
{
	Super::Serialize(Handle, bIsLoading);
	ParticleJson::SerializeDistribution(Handle, "StartLocation", StartLocation, bIsLoading);
}

void UParticleModuleVelocity::Serialize(json& Handle, bool bIsLoading)
{
	Super::Serialize(Handle, bIsLoading);
	ParticleJson::SerializeDistribution(Handle, "StartVelocity", StartVelocity, bIsLoading);
	ParticleJson::SerializeDistribution(Handle, "StartVelocityRadial", StartVelocityRadial, bIsLoading);
}

void UParticleModuleSize::Serialize(json& Handle, bool bIsLoading)
{
	Super::Serialize(Handle, bIsLoading);
	ParticleJson::SerializeDistribution(Handle, "StartSize", StartSize, bIsLoading);
}

void UParticleModuleColor::Serialize(json& Handle, bool bIsLoading)
{
	Super::Serialize(Handle, bIsLoading);
	ParticleJson::SerializeDistribution(Handle, "StartColor", StartColor, bIsLoading);
	ParticleJson::SerializeDistribution(Handle, "StartAlpha", StartAlpha, bIsLoading);
}

FParticleEmitterInstance* UParticleModuleTypeDataBase::CreateInstance() const
{
	return new FParticleEmitterInstance();
}
