#include "EnginePCH.h"
#include "Core/ResourcePaths.h"
#include "AssetManager.h"
#include "Render/Buffer.h"
#include "Render/Material.h"

#include "Render/Vertex.h"
#include "Render/Texture2D.h"
#include "Render/RenderCommand.h"

#include "Render/RenderResourceManager.h"

#include "Asset/ObjImporter/ObjImporter.h"
#include "Asset/FbxImporter/FbxAssetImporter.h"
#include "Asset/FbxImporter/FbxAssetLoader.h"
#include "Asset/FbxImporter/FbxAssetBake.h"
#include "Asset/FbxImporter/FbxPath.h"
#include "Asset/Skeleton.h"
#include "Asset/BlendSpace1D.h"
#include "Asset/BlendSpace2D.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSerialization.h"

#include "Render/GeometryGenerator.h"
#include "UObject/UObjectGlobals.h"

#include "Engine/Classes/Sound/SoundWave.h"

#include "Render/ImageLoader.h"
#include <algorithm>
#include <cctype>
#include <fstream>



namespace
{
	FString GetAssetExtension(const FString& Path) {
		FString Extension{ FbxPath::ToString(FbxPath::FromString(Path).extension()) };
		for (char& Character : Extension) {
			Character = static_cast<char>(std::tolower(static_cast<unsigned char>(Character)));
		}
		return Extension;
	}

	UStaticMesh* CreateStaticMesh(const FStaticMeshData* MeshData)
	{
		if (!MeshData || !MeshData->Vertices.Num() || !MeshData->Indices.Num())
			return nullptr;

		FStaticMeshData Optimized = *MeshData;

		// Mesh에 MaterialSlot 없을 경우 Default Material 할당
		if (Optimized.MaterialSlots.IsEmpty())
		{
			FStaticMaterialSlot DefaultSlot;
			DefaultSlot.Name = "Default";
			Optimized.MaterialSlots.Add(DefaultSlot);
		}

		// Mesh에 Section이 없을 경우 전체 메쉬을 섹션 하나로 세팅
		if (Optimized.Sections.IsEmpty())
		{
			FStaticMeshSection DefaultSection;
			DefaultSection.StartIndex = 0;
			DefaultSection.IndexCount = static_cast<uint32>(Optimized.Indices.Num());
			DefaultSection.MaterialSlotIndex = 0;

			Optimized.Sections.Add(DefaultSection);
		}

		Optimized.BuildTangentSpace();
		Optimized.OptimizeTriangleOrderForVertexCache();

		TUniquePtr<FVertexBuffer> VB = RenderCommand::CreateStaticVertexBuffer(Optimized.Vertices.GetData(), sizeof(FVertex) * static_cast<uint32>(Optimized.Vertices.size()), sizeof(FVertex));
		TUniquePtr<FIndexBuffer> IB = RenderCommand::CreateStaticIndexBuffer(Optimized.Indices.GetData(), static_cast<uint32>(Optimized.Indices.size()));
		if (VB == nullptr || IB == nullptr) return nullptr;

		UStaticMesh* Mesh = NewObject<UStaticMesh>();
		Mesh->MeshData = std::move(Optimized);

		// Vertex/Index GPU 업로드
		Mesh->VertexBuffer = std::move(VB);
		Mesh->IndexBuffer = std::move(IB);

		UMaterial* DefaultMaterial = UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
		if (!DefaultMaterial)
		{
			return nullptr;
		}

		// MaterialSlots를 실제 UMaterial로 변환
		for (const FStaticMaterialSlot& Slot : Mesh->MeshData.MaterialSlots)
		{
			UMaterial* Material = UMaterial::CreateInstance(DefaultMaterial);
			if (!Material)
			{
				return nullptr;
			}
			Material->BaseColor = Slot.BaseColor;
			Material->ApplyImportedProperties(Slot);
			if (Material->BaseColor.W < 1.0f)
			{
				Material->SetBlendState(EBlendState::AlphaBlend);
			}

			if (!Slot.DiffuseTexturePath.empty())
			{
				UTexture2D* Texture = UAssetManager::Get().LoadTexture(Slot.DiffuseTexturePath);

				if (Texture)
				{
					Material->Textures[0] = Texture;
				}
			}

			if (!Slot.NormalTexturePath.empty()) {
				if (UTexture2D* NormalTexture{ UAssetManager::Get().LoadTexture(Slot.NormalTexturePath) }) {
					Material->Textures[1] = NormalTexture;
				}
			}

			Mesh->Materials.Add(Material);
		}

		Mesh->MeshData.BuildTriangleBVH();
		Mesh->RebuildRenderData();

		return Mesh;
	}

	FString MakeAssetKey(const FString& Path)
	{
		return FbxPath::ToString(fs::relative(FbxPath::FromString(Path), "Assets"));
	}

	FString NormalizeAssetPath(const FString& Path)
	{
		if (Path.empty()) return {};
		return FbxPath::ToString(FbxPath::FromString(Path).lexically_normal());
	}

	FString CatalogKey(const FString& Path)
	{
		FString Key = NormalizeAssetPath(Path);
		if (Key.rfind("Assets/", 0) == 0) return Key.substr(7);
		if (FbxPath::FromString(Key).is_absolute())
		{
			FString Relative, Error;
			if (FbxPath::GetAssetKey(Key, Relative, Error, false)) return Relative;
			return {};
		}
		return Key;
	}

	FString AssetCacheKey(const FString& Path)
	{
		FString Key = CatalogKey(Path);
		return Key.empty() ? NormalizeAssetPath(Path) : Key;
	}

	EAssetType CatalogType(EFbxAssetType Type)
	{
		switch (Type)
		{
		case EFbxAssetType::StaticMesh:   return EAssetType::StaticMesh;
		case EFbxAssetType::SkeletalMesh: return EAssetType::SkeletalMesh;
		case EFbxAssetType::Skeleton:     return EAssetType::Skeleton;
		case EFbxAssetType::AnimSequence: return EAssetType::AnimSequence;
		case EFbxAssetType::Material:     return EAssetType::Material;
		case EFbxAssetType::Texture2D:    return EAssetType::Texture;
		default:                       return EAssetType::Unknown;
		}
	}

	const UClass* AssetClass(EAssetType Type)
	{
		switch (Type)
		{
		case EAssetType::Texture:      return UTexture2D::StaticClass();
		case EAssetType::Font:         return UFont::StaticClass();
		case EAssetType::Material:     return UMaterial::StaticClass();
		case EAssetType::StaticMesh:   return UStaticMesh::StaticClass();
		case EAssetType::SkeletalMesh: return USkeletalMesh::StaticClass();
		case EAssetType::Skeleton:     return USkeleton::StaticClass();
		case EAssetType::AnimSequence: return UAnimSequence::StaticClass();
		case EAssetType::BlendSpace:   return UBlendSpace::StaticClass();
		case EAssetType::ParticleSystem: return UParticleSystem::StaticClass();
		default:                     return nullptr;
		}
	}

	EAssetType CatalogType(const FString& Path, FFbxAssetData& Header)
	{
		const FString Extension = GetAssetExtension(Path);
		if (Extension == ".obj") return EAssetType::StaticMesh;
		if (Extension == ".png" || Extension == ".dds") return EAssetType::Texture;
		if (Extension == ".blendspace") return EAssetType::BlendSpace;
		if (Extension == ".particle") return EAssetType::ParticleSystem;
		if (Extension == ".json" && FbxPath::FromString(Path).parent_path().filename() == "Fonts")
			return EAssetType::Font;
		if (Extension == ".fbxbin")
		{
			FString Error;
			if (FFbxAssetBake::ReadCatalogHeader(Path, Header, Error)) return CatalogType(Header.Type);
		}
		return EAssetType::Unknown;
	}

	FString FoldCase(FString Value)
	{
		for (char& Character : Value) Character = static_cast<char>(std::tolower(static_cast<unsigned char>(Character)));
		return Value;
	}
}

UAssetManager& UAssetManager::Get()
{
	static UAssetManager* Instance = NewObject<UAssetManager>();
	return *Instance;
}

void UAssetManager::Init(const FAssetLoadProgress& OnProgress)
{
	FGeometryGenerator::CreateDefaultMeshDatas();
	// 머티리얼이 참조하므로 반드시 먼저 만든다
	Get().CreateDefaultTextures();
	Get().CreateDefaultMaterial();
	Get().CreateDefaultTranslucentMaterial();
	Get().ScanAssets(ContentPaths::AssetRoot, OnProgress);
	// 엔진 기본 폰트는 스캔하지 않는 Resources에 있으므로 경로를 키로 직접 등록한다.
	if (UFont* DefaultFont = Get().LoadFontAtlas(ResourcePaths::DefaultFont, ResourcePaths::DefaultFontAtlas))
	{
		Get().RegisterAsset(ResourcePaths::DefaultFont, DefaultFont);
	}
	Get().CreateDefaultMeshes();
	Get().CreateBillboardMaterial();
	Get().CreateParticleMaterial();
	Get().CreateParticleSystemMaterials();
	Get().CreateEditorArrowMaterial();
	Get().CreateBoneWeightHeatmapMaterial();
}

void UAssetManager::RefreshAssetCatalog()
{
	Get().ScanAssets(ContentPaths::AssetRoot, nullptr);
}

void UAssetManager::ScanAssets(const fs::path& AssetRoot, const FAssetLoadProgress& OnProgress)
{
	if (!fs::exists(AssetRoot))
	{
		HTR_LOG(Error, "Asset root not found: {}", FbxPath::ToString(AssetRoot));
		return;
	}

	TArray<fs::path> Files;
	for (fs::recursive_directory_iterator It(AssetRoot), End; It != End; ++It)
	{
		const fs::directory_entry& Entry = *It;
		if (Entry.is_directory() && Entry.path().filename().native().starts_with(L"."))
		{ It.disable_recursion_pending(); continue; }
		if (Entry.is_regular_file() && Entry.path().extension() != ".bin")
		{
			Files.Add(Entry.path());
		}
	}

	AssetFiles.Empty();
	TArray<FString> FbxFiles;
	const int32 Total = Files.Num();
	for (int32 i = 0; i < Total; ++i)
	{
		// 키와 경로는 UTF-8이다. generic_string()은 ANSI 코드페이지로 바꾸므로 한글·이모지 이름에서 깨지거나 예외가 난다.
		FString Key = FbxPath::ToString(fs::relative(Files[i], AssetRoot));
		FString Path = FbxPath::ToString(Files[i]);
		if (GetAssetExtension(Path) == ".fbx")
		{
			FbxFiles.Add(Path);
		}
		else
		{
			FFbxAssetData Header;
			const EAssetType Type = CatalogType(Path, Header);
			if (Type != EAssetType::Unknown)
				AssetFiles.Add(Key, { Path, Type, Header.SkeletonKey });

			if (GetAssetExtension(Path) == ".fbxbin" &&
				(Type == EAssetType::StaticMesh || Type == EAssetType::SkeletalMesh))
			{
				MarkFbxImported(Header.SourcePath);
			}
			else if (Type == EAssetType::AnimSequence && !Header.SkeletonKey.empty())
			{
				// A sequence-only FBX is imported when it targets a skeleton from another source.
				FFbxAssetData SkeletonHeader;
				FString SkeletonPath, Error;
				if (FbxPath::ResolveKey(Header.SkeletonKey, SkeletonPath, Error) &&
					FFbxAssetBake::ReadHeader(SkeletonPath, SkeletonHeader, Error) &&
					SkeletonHeader.Type == EFbxAssetType::Skeleton &&
					FoldCase(SkeletonHeader.SourcePath) != FoldCase(Header.SourcePath))
					MarkFbxImported(Header.SourcePath);
			}
		}

		if (OnProgress)
		{
			OnProgress(i + 1, Total, Key);
		}
	}
	// 스캔 순서는 정해져 있지 않으므로 모든 바이너리를 본 뒤에 비교한다.
	FbxImportCandidates.Reset();
	for (const FString& Path : FbxFiles)
	{
		FString Key, Error;
		if (FbxPath::GetAssetKey(Path, Key, Error) && !IsFbxImported(Key))
			FbxImportCandidates.Add(Path);
	}
}

void UAssetManager::MarkFbxImported(const FString& SourceIdentity)
{
	ImportedFbxSources.insert(FoldCase(SourceIdentity));
}

bool UAssetManager::IsFbxImported(const FString& SourceIdentity) const
{
	return ImportedFbxSources.contains(FoldCase(SourceIdentity));
}

const UAssetManager::FAssetFile* UAssetManager::FindOrDiscoverFile(const FString& Key)
{
	if (Key.empty()) return nullptr;
	if (const FAssetFile* File = AssetFiles.Find(Key)) return File;

	FString Path, Error;
	std::error_code Ec;
	if (!FbxPath::ResolveKey(Key, Path, Error) || !fs::is_regular_file(FbxPath::FromString(Path), Ec))
		return nullptr;

	FFbxAssetData Header;
	const EAssetType Type = CatalogType(Path, Header);
	if (Type == EAssetType::Unknown) return nullptr;

	AssetFiles.Add(Key, { Path, Type, Header.SkeletonKey });
	return AssetFiles.Find(Key);
}

URenderAsset* UAssetManager::FindLoaded(const FString& Path) const
{
	const auto* Asset = AssetMap.Find(AssetCacheKey(Path));
	if (Asset && *Asset && !(*Asset)->IsRetired()) return *Asset;
	return nullptr;
}

URenderAsset* UAssetManager::FindOrLoad(const FString& Path, const UClass* ExpectedClass)
{
	if (URenderAsset* Existing = FindLoaded(Path)) return Existing;
	const FString Key = CatalogKey(Path);
	const FAssetFile* File = FindOrDiscoverFile(Key);
	if (!File) return nullptr;

	const UClass* Class = AssetClass(File->Type);
	if (!Class || !(Class->IsChildOf(ExpectedClass) || ExpectedClass->IsChildOf(Class))) return nullptr;
	// Loading dependencies can expand the catalog and invalidate map entries.
	const FAssetFile FileToLoad = *File;
	return LoadAssetFile(Key, FileToLoad);
}

URenderAsset* UAssetManager::LoadAssetFile(const FString& Key, const FAssetFile& File)
{
	if (GetAssetExtension(File.Path) == ".fbxbin")
	{
		FString Error;
		URenderAsset* Asset = FFbxAssetLoader::LoadAsset(File.Path, Error);
		if (!Asset) HTR_LOG(Error, "[FBX] {}", Error);
		return Asset;
	}

	switch (File.Type)
	{
	case EAssetType::Texture: return LoadTexture(File.Path);
	case EAssetType::Font:
	{
		const FString TexturePath = FbxPath::ToString(FbxPath::FromString(File.Path).replace_extension(".png"));
		return LoadFontAtlas(File.Path, TexturePath);
	}
	case EAssetType::StaticMesh: return LoadObjStaticMesh(File.Path);
	case EAssetType::BlendSpace: return LoadBlendSpaceFile(Key, File.Path);
	case EAssetType::ParticleSystem: return LoadParticleSystemFile(Key, File.Path);
	default: return nullptr;
	}
}

EAssetType UAssetManager::GetAssetType(const FString& Path)
{
	if (URenderAsset* Asset = Get().FindLoaded(Path))
	{
		for (EAssetType Type : { EAssetType::Texture, EAssetType::Font, EAssetType::Material, EAssetType::StaticMesh,
			EAssetType::SkeletalMesh, EAssetType::Skeleton, EAssetType::AnimSequence, EAssetType::BlendSpace, EAssetType::ParticleSystem })
		{
			if (Asset->IsA(AssetClass(Type))) return Type;
		}
	}
	const FAssetFile* File = Get().FindOrDiscoverFile(CatalogKey(Path));
	return File ? File->Type : EAssetType::Unknown;
}

TArray<FString> UAssetManager::GetAssetPaths(EAssetType Type, const FString& SkeletonPath)
{
	TArray<FString> Result;
	const UClass* Class = AssetClass(Type);
	if (!Class) return Result;

	std::set<FString> Paths;
	const FString SkeletonKey = CatalogKey(SkeletonPath);
	for (const auto& [Key, File] : Get().AssetFiles)
	{
		if (File.Type == Type && !Get().FindLoaded(Key) &&
			(SkeletonPath.empty() || (!SkeletonKey.empty() && CatalogKey(File.SkeletonKey) == SkeletonKey)))
			Paths.insert(Key);
	}

	// Imports and runtime assets are already registered even when they were absent at startup.
	for (const auto& [Key, Asset] : Get().AssetMap)
	{
		if (!Asset || Asset->IsRetired() || !Asset->IsA(Class) || Asset->GetPath().empty()) continue;
		if (!SkeletonPath.empty() && GetAssetSkeletonPath(Asset->GetPath()) != SkeletonKey) continue;
		Paths.insert(Key);
	}

	for (const FString& Path : Paths) Result.Add(Path);
	return Result;
}

FString UAssetManager::GetAssetSkeletonPath(const FString& Path)
{
	if (URenderAsset* Asset = Get().FindLoaded(Path))
	{
		if (const auto* Mesh = Cast<USkeletalMesh>(Asset); Mesh && Mesh->Skeleton)
			return CatalogKey(Mesh->Skeleton->GetPath());
		if (const auto* Sequence = Cast<UAnimSequence>(Asset); Sequence && Sequence->Skeleton)
			return CatalogKey(Sequence->Skeleton->GetPath());
	}
	const FAssetFile* File = Get().FindOrDiscoverFile(CatalogKey(Path));
	return File ? CatalogKey(File->SkeletonKey) : FString();
}

TArray<FString> UAssetManager::GetAnimationSkeletonPaths()
{
	std::set<FString> Paths;
	for (const FString& Sequence : GetAssetPaths(EAssetType::AnimSequence))
	{
		FString Skeleton = GetAssetSkeletonPath(Sequence);
		if (!Skeleton.empty()) Paths.insert(std::move(Skeleton));
	}
	TArray<FString> Result;
	for (const FString& Path : Paths) Result.Add(Path);
	return Result;
}

USkeletalMesh* UAssetManager::LoadFirstSkeletalMeshForSkeleton(const USkeleton* Skeleton)
{
	if (!Skeleton) return nullptr;
	for (const FString& Path : GetAssetPaths(EAssetType::SkeletalMesh, Skeleton->GetPath()))
	{
		if (auto* Mesh = GetAssetByPath<USkeletalMesh>(Path); Mesh && Mesh->Skeleton == Skeleton)
			return Mesh;
	}
	return nullptr;
}

void UAssetManager::CreateDefaultTextures() {
	// 텍스처가 지정되지 않은 머티리얼이 검게 나오지 않도록 하는 1x1 흰색 텍스처.
	// 셰이더에서 곱해도 결과가 변하지 않으므로 "텍스처 없음"의 기본값으로 쓴다.
	const uint32 WhitePixel{ 0xFFFFFFFF };

	FTextureDesc Desc{};
	Desc.Width = 1;
	Desc.Height = 1;
	Desc.Format = ETextureFormat::R8G8B8A8_UNorm;
	Desc.Usage = ETextureUsage::Immutable;
	Desc.BindFlags = ETextureBindFlags::ShaderResource;

	TUniquePtr<FTexture2D> Resource{ RenderCommand::CreateTexture2D(Desc, &WhitePixel) };
	if (!Resource) {
		return;
	}

	UTexture2D* WhiteTexture{ NewObject<UTexture2D>() };
	WhiteTexture->SetResource(std::move(Resource));
	WhiteTexture->SetPath("WhiteTexture");

	AssetMap["WhiteTexture"] = WhiteTexture;

	const FVector4 NormalPixel{ 0.5f, 0.5f, 1.0f, 1.0f };
	Desc.Format = ETextureFormat::R32G32B32A32_Float;
	TUniquePtr<FTexture2D> NormalResource{ RenderCommand::CreateTexture2D(Desc, &NormalPixel) };
	if (!NormalResource) {
		return;
	}

	UTexture2D* FlatNormalTexture{ NewObject<UTexture2D>() };
	FlatNormalTexture->SetResource(std::move(NormalResource));
	FlatNormalTexture->SetPath("FlatNormalTexture");
	AssetMap["FlatNormalTexture"] = FlatNormalTexture;
}

void UAssetManager::CreateDefaultMeshes()
{
	// 메시 데이터 업로드
	FStaticMeshData* CubeData = FGeometryGenerator::GetMeshData("Cube");
	RegisterAsset("Cube", CreateStaticMesh(CubeData));

	FStaticMeshData* ConeData = FGeometryGenerator::GetMeshData("Cone");
	RegisterAsset("Cone", CreateStaticMesh(ConeData));

	FStaticMeshData* SphereData = FGeometryGenerator::GetMeshData("Sphere");
	RegisterAsset("Sphere", CreateStaticMesh(SphereData));

	FStaticMeshData* CylinderData = FGeometryGenerator::GetMeshData("Cylinder");
	RegisterAsset("Cylinder", CreateStaticMesh(CylinderData));

	FStaticMeshData* PlaneData = FGeometryGenerator::GetMeshData("Plane");
	RegisterAsset("Plane", CreateStaticMesh(PlaneData));

	FStaticMeshData* ArrowMeshData = FGeometryGenerator::GetMeshData("Arrow");
	RegisterAsset("Arrow", CreateStaticMesh(ArrowMeshData));

	FStaticMeshData* RingMeshData = FGeometryGenerator::GetMeshData("Ring");
	RegisterAsset("Ring", CreateStaticMesh(RingMeshData));

	FStaticMeshData* ScaleBarData = FGeometryGenerator::GetMeshData("ScaleBar");
	RegisterAsset("ScaleBar", CreateStaticMesh(ScaleBarData));

	FStaticMeshData* GizmoSphereData = FGeometryGenerator::GetMeshData("GizmoSphere");
	RegisterAsset("GizmoSphere", CreateStaticMesh(GizmoSphereData));

	const FParticleVertex Vertices[] =
	{
		{ FVector(0.0f, -0.5f,  0.5f), FVector2(0.0f, 0.0f) },
		{ FVector(0.0f, 0.5f,  0.5f), FVector2(1.0f, 0.0f) },
		{ FVector(0.0f, 0.5f, -0.5f), FVector2(1.0f, 1.0f) },
		{ FVector(0.0f, -0.5f, -0.5f), FVector2(0.0f, 1.0f) }
	};

	const uint32 Indices[] =
	{
		0, 1, 2,
		0, 2, 3
	};

	UStaticMesh* Mesh = NewObject<UStaticMesh>();
	Mesh->VertexBuffer = RenderCommand::CreateStaticVertexBuffer(
		Vertices,
		sizeof(Vertices), sizeof(FParticleVertex));
	Mesh->IndexBuffer = RenderCommand::CreateStaticIndexBuffer(
		Indices,
		ARRAYSIZE(Indices));

	FVertex Vertex{};
	Vertex.Position = FVector(0.0f, -0.5f, 0.5f);
	Mesh->MeshData.Vertices.Add(Vertex);
	Vertex.Position = FVector(0.0f, 0.5f, 0.5f);
	Mesh->MeshData.Vertices.Add(Vertex);
	Vertex.Position = FVector(0.0f, 0.5f, -0.5f);
	Mesh->MeshData.Vertices.Add(Vertex);
	Vertex.Position = FVector(0.0f, -0.5f, -0.5f);
	Mesh->MeshData.Vertices.Add(Vertex);

	Mesh->MeshData.Indices = { 0, 1, 2, 0, 2, 3, 0,2,1, 0,3,2 };

	Mesh->MeshData.AABB.Min = FVector(-0.001f, -0.5, -0.5);
	Mesh->MeshData.AABB.Max = FVector(0.001f, 0.5, 0.5);
	Mesh->RebuildRenderData();

	RegisterAsset("ParticleQuad", Mesh);
}

void UAssetManager::CreateDefaultMaterial() {
	UMaterial* DefaultMat{ NewObject<UMaterial>() };
	DefaultMat->Shader = FRenderResourceManager::GetShaderProgram("Resources/Shader/UberLit.hlsl");
	DefaultMat->Textures.Add(GetAssetByPath<UTexture2D>("WhiteTexture"));
	DefaultMat->Textures.Add(GetAssetByPath<UTexture2D>("FlatNormalTexture"));
	DefaultMat->ParamLayout = EMaterialParamLayout::StaticMesh;
	DefaultMat->ParamBuffer = RenderCommand::CreateConstantBuffer(sizeof(FStaticMeshMaterialParams));
	RegisterAsset("DefaultMaterial", DefaultMat);
}

// 기본 머티리얼의 반투명 버전. 알파 블렌드 + 깊이 읽기 전용(SetBlendState가 PSOType과 깊이 상태를 함께 바꾼다).
void UAssetManager::CreateDefaultTranslucentMaterial()
{
	UMaterial* TranslucentMat{ UMaterial::CreateInstance(GetAssetByPath<UMaterial>("DefaultMaterial")) };
	if (!TranslucentMat) return;
	TranslucentMat->SetBlendState(EBlendState::AlphaBlend);
	RegisterAsset("DefaultTranslucentMaterial", TranslucentMat);
}

void UAssetManager::CreateBillboardMaterial()
{
	UMaterial* BillboardMat = NewObject<UMaterial>();
	BillboardMat->PSOType = EPSOType::Billboard;
	BillboardMat->Textures.Add(GetAssetByPath<UTexture2D>("WhiteTexture"));
	BillboardMat->BlendState = EBlendState::AlphaBlend;
	BillboardMat->DepthStencilState = EDepthStencilState::ReadOnly;
	BillboardMat->SamplerState = ESamplerState::LinearClamp;
	BillboardMat->ParamLayout = EMaterialParamLayout::ParticleSubUV;
	RegisterAsset("BillboardMaterial", BillboardMat);
}

void UAssetManager::CreateParticleMaterial()
{
	UMaterial* ParticleMat = NewObject<UMaterial>();
	ParticleMat->PSOType = EPSOType::Particle_AlphaBlend;
	ParticleMat->Shader = FRenderResourceManager::GetShaderProgram("Resources/Shader/ParticleSubUVShader.hlsl");
	ParticleMat->Textures.Add(LoadTexture(ResourcePaths::DefaultParticleTexture));
	ParticleMat->BlendState = EBlendState::AlphaBlend;
	ParticleMat->DepthStencilState = EDepthStencilState::ReadOnly;
	ParticleMat->ParamLayout = EMaterialParamLayout::ParticleSubUV;
	ParticleMat->ParamBuffer = RenderCommand::CreateConstantBuffer(256);
	RegisterAsset("SubUVMaterial", ParticleMat);
}

void UAssetManager::CreateParticleSystemMaterials()
{
	// UParticleSystemComponent의 정점(FParticleSpriteVertex)을 ParticleSpriteShader로 그리는 머티리얼. 셰이더는 PSOType이 고른다.
	const auto Make = [this](const char* Key, bool bAdditive, const char* TexturePath, ESamplerState Sampler)
	{
		UMaterial* Material = NewObject<UMaterial>();
		Material->PSOType = bAdditive ? EPSOType::ParticleSprite_Additive : EPSOType::ParticleSprite_AlphaBlend;
		Material->BlendState = bAdditive ? EBlendState::Additive : EBlendState::AlphaBlend;
		Material->DepthStencilState = EDepthStencilState::ReadOnly;
		Material->SamplerState = Sampler;
		Material->Textures.Add(LoadTexture(TexturePath));
		RegisterAsset(Key, Material);
	};
	Make(ResourcePaths::ParticleSpriteMaterial, false, ResourcePaths::DefaultParticleSpriteTexture, ESamplerState::LinearClamp);
	Make(ResourcePaths::ParticleSpriteAdditiveMaterial, true, ResourcePaths::DefaultParticleSpriteTexture, ESamplerState::LinearClamp);
	// 빔은 길이 방향으로 텍스처가 반복되므로(TextureTile) Wrap
	Make(ResourcePaths::ParticleBeamMaterial, true, ResourcePaths::DefaultParticleBeamTexture, ESamplerState::LinearWrap);
}

void UAssetManager::CreateEditorArrowMaterial() {
	UMaterial* BaseMaterial{ GetAssetByPath<UMaterial>("DefaultMaterial") };
	UMaterial* ArrowMaterial{ UMaterial::CreateInstance(BaseMaterial) };
	ArrowMaterial->Shader = FRenderResourceManager::GetShaderProgram("Resources/Shader/EditorArrowShader.hlsl");
	ArrowMaterial->BaseColor = FVector4{ 1.0f, 1.0f, 1.0f, 1.0f };
	RegisterAsset("EditorArrowMaterial", ArrowMaterial);
}

// Bone Weight Heatmap용. 색 텍스처가 없으면 UberLit이 정점 색을 표면 색으로 쓰고(SurfaceParams.z = 0),
// BaseColor가 흰색이라 히트 색이 그대로 나온다. 조명은 Viewer가 Unlit 뷰 모드로 끈다.
void UAssetManager::CreateBoneWeightHeatmapMaterial()
{
	UMaterial* HeatmapMaterial{ UMaterial::CreateInstance(GetAssetByPath<UMaterial>("DefaultMaterial")) };
	if (!HeatmapMaterial) return;
	HeatmapMaterial->Textures.Empty();
	HeatmapMaterial->BaseColor = FVector4{ 1.0f, 1.0f, 1.0f, 1.0f };
	HeatmapMaterial->EmissiveColor = FVector{ 0.0f, 0.0f, 0.0f };
	RegisterAsset("BoneWeightHeatmapMaterial", HeatmapMaterial);
}

void UAssetManager::Shutdown()
{
	Get().AssetFiles.Empty();
	Get().AssetMap.Empty();
	Get().SoundMap.Empty();
}


void UAssetManager::RegisterAsset(const FString& Key, URenderAsset* Asset)
{
	Asset->SetPath(Key);
	AssetMap[AssetCacheKey(Key)] = Asset;
}

void UAssetManager::PrepareAssetRegistration(const TMap<FString, URenderAsset*>& Replacements, TMap<FString, URenderAsset*>& Prepared) const
{
	Prepared = AssetMap;
	for (const auto& [Key, Asset] : Replacements) Prepared[AssetCacheKey(Key)] = Asset;
}

void UAssetManager::CommitAssetRegistration(TMap<FString, URenderAsset*>& Prepared) noexcept
{
	for (const auto& [Key, Previous] : AssetMap)
	{
		URenderAsset** Next = Prepared.Find(Key);
		if (Previous && Next && *Next != Previous) Previous->SetRetired(true);
	}
	AssetMap.Swap(Prepared);
}

UTexture2D* UAssetManager::LoadTexture(const FString& InPath, bool GenerateMips) {
	if (GetAssetExtension(InPath) == ".fbxbin") return GetAssetByPath<UTexture2D>(InPath);
	if (UTexture2D* Cached = FindLoadedAsset<UTexture2D>(InPath)) return Cached;

	const FImageData Data = ImageLoader::LoadAuto(InPath);
	if (!Data.IsValid())
	{
		return nullptr;
	}

	const bool bUseDDSLevels = Data.MipRowPitches.Num() > 0;
	// BC 압축 DDS는 렌더 타깃으로 바인딩할 수 없으므로 GPU mip 생성 대상에서 제외한다.
	const bool bGenerateGPUmips = GenerateMips && !bUseDDSLevels && Data.BytesPerPixel != 0;

	FTextureDesc Desc{};
	Desc.Width = Data.Width;
	Desc.Height = Data.Height;
	Desc.MipLevels = bGenerateGPUmips ? 0 : Data.MipLevels;
	Desc.Usage = bGenerateGPUmips ? ETextureUsage::Default : ETextureUsage::Immutable;
	Desc.BindFlags = ETextureBindFlags::ShaderResource |
		(bGenerateGPUmips ? ETextureBindFlags::RenderTarget : ETextureBindFlags::None);
	Desc.bGenerateMips = bGenerateGPUmips;

	TUniquePtr<FTexture2D> Resource = RenderCommand::CreateTexture2D(Desc, Data);

	if (!Resource || !Resource->GetSRV()) {
		return nullptr;
	}

	UTexture2D* Asset{ NewObject<UTexture2D>() };
	Asset->SetResource(std::move(Resource));
	RegisterAsset(InPath, Asset);

	return Asset;
}

UFont* UAssetManager::LoadFontAtlas(const FString& JsonPath, const FString& AtlasTexturePath)
{
	UFont* Font = NewObject<UFont>();

	if (!Font->LoadFontAtlasJson(JsonPath))
	{
		return nullptr;
	}

	Font->AtlasTexture = LoadTexture(AtlasTexturePath, false);
	if (!Font->AtlasTexture)
	{
		return nullptr;
	}

	RegisterAsset(JsonPath, Font);
	return Font;
}

USoundWave* UAssetManager::LoadSound(const FString& Path)
{
	if (USoundWave** Found = SoundMap.Find(Path)) return *Found;

	USoundWave* Wave = NewObject<USoundWave>();
	if (!Wave->LoadFromFile(Path))
	{
		// 실패한 객체는 텍스처 로드 실패 때와 같은 방식으로 정리
		return nullptr;
	}
	SoundMap[Path] = Wave;
	return Wave;
}

UStaticMesh* UAssetManager::LoadObjStaticMesh(const FString& Path)
{
	if (GetAssetExtension(Path) == ".fbxbin") return GetAssetByPath<UStaticMesh>(Path);
	const FString Key = MakeAssetKey(Path);
	if (UStaticMesh* Cached = FindLoadedAsset<UStaticMesh>(Key))
	{
		return Cached;
	}

	TUniquePtr<FStaticMeshData> Data = FObjImporter::LoadStaticMeshData(Path);
	UStaticMesh* Mesh = CreateStaticMesh(Data.get());   // Data가 nullptr이면 nullptr 반환
	if (!Mesh)
	{
		return nullptr;
	}

	Get().RegisterAsset(Key, Mesh);
	return Mesh;
}

USkeleton* UAssetManager::LoadSkeleton(const FString& AssetPath)
{
	return GetAssetByPath<USkeleton>(AssetPath);
}

UAnimSequence* UAssetManager::LoadAnimSequence(const FString& AssetPath)
{
	FString Key = CatalogKey(AssetPath);
	if (Key.ends_with(".animseq"))
	{
		const fs::path Legacy = FbxPath::FromString(Key);
		const FString Migrated = FbxPath::ToString(Legacy.parent_path() / (L"AS_" + Legacy.stem().native() + L".fbxbin"));
		HTR_LOG(Warning, "[Anim] Legacy sequence key {} is read as {}", Key, Migrated);
		Key = Migrated;
	}
	return GetAssetByPath<UAnimSequence>(Key);
}

UBlendSpace* UAssetManager::LoadBlendSpace(const FString& AssetPath)
{
	return GetAssetByPath<UBlendSpace>(AssetPath);
}

UBlendSpace* UAssetManager::LoadBlendSpaceFile(const FString& Key, const FString& Path)
{
	std::ifstream In(FbxPath::FromString(Path));
	if (!In) return nullptr;
	const json Data = json::parse(In, nullptr, false);
	if (!Data.is_object() || !Data.contains("Type") || !Data["Type"].is_string() ||
		!Data.contains("Skeleton") || !Data["Skeleton"].is_string() ||
		!Data.contains("Samples") || !Data["Samples"].is_array() ||
		Data["Samples"].empty() || Data["Samples"].size() > 256) return nullptr;
	const FString Type = Data["Type"].get<FString>();
	if (Type != "BlendSpace1D" && Type != "BlendSpace2D") return nullptr;
	USkeleton* Skeleton = LoadSkeleton(Data["Skeleton"].get<FString>());
	if (!Skeleton) return nullptr;
	UBlendSpace* BlendSpace = Type == "BlendSpace1D"
		? static_cast<UBlendSpace*>(NewObject<UBlendSpace1D>())
		: static_cast<UBlendSpace*>(NewObject<UBlendSpace2D>());
	BlendSpace->Skeleton = Skeleton;
	for (const json& Sample : Data["Samples"])
	{
		if (!Sample.is_object() || !Sample.contains("X") || !Sample["X"].is_number() ||
			(Type == "BlendSpace2D" && (!Sample.contains("Y") || !Sample["Y"].is_number())) ||
			!Sample.contains("Sequence") || !Sample["Sequence"].is_string())
		{
			delete BlendSpace;
			return nullptr;
		}
		const float X = Sample["X"].get<float>();
		const float Y = Type == "BlendSpace2D" ? Sample["Y"].get<float>() : 0.0f;
		UAnimSequence* Sequence = LoadAnimSequence(Sample["Sequence"].get<FString>());
		const bool Added = Type == "BlendSpace1D"
			? static_cast<UBlendSpace1D*>(BlendSpace)->AddSample(X, Sequence)
			: static_cast<UBlendSpace2D*>(BlendSpace)->AddSample(X, Y, Sequence);
		if (!Added)
		{
			delete BlendSpace;
			return nullptr;
		}
	}
	Get().RegisterAsset(Key, BlendSpace);
	return BlendSpace;
}

bool UAssetManager::SaveBlendSpace2D(const FString& AssetPath, UBlendSpace2D& BlendSpace)
{
	const FString Key = AssetPath.rfind("Assets/", 0) == 0 ? AssetPath.substr(7) : AssetPath;
	if (FbxPath::FromString(Key).extension() != ".blendspace" || !BlendSpace.Skeleton ||
		BlendSpace.Skeleton->GetPath().empty() || BlendSpace.GetSamples().IsEmpty()) return false;
	json Data = { { "Type", "BlendSpace2D" }, { "Skeleton", BlendSpace.Skeleton->GetPath() },
		{ "Samples", json::array() } };
	for (const UBlendSpace2D::FSample& Sample : BlendSpace.GetSamples())
	{
		if (!Sample.Sequence || Sample.Sequence->Skeleton != BlendSpace.Skeleton ||
			Sample.Sequence->GetPath().empty()) return false;
		Data["Samples"].push_back({ { "X", Sample.X }, { "Y", Sample.Y }, { "Sequence", Sample.Sequence->GetPath() } });
	}
	std::ofstream Out(ContentPaths::AssetRoot / FbxPath::FromString(Key));
	if (!Out) return false;
	Out << Data.dump(2);
	if (!Out) return false;
	Get().RegisterAsset(Key, &BlendSpace);
	return true;
}

bool UAssetManager::SaveBlendSpace1D(const FString& AssetPath, UBlendSpace1D& BlendSpace)
{
	const FString Key = AssetPath.rfind("Assets/", 0) == 0 ? AssetPath.substr(7) : AssetPath;
	if (FbxPath::FromString(Key).extension() != ".blendspace" || !BlendSpace.Skeleton ||
		BlendSpace.Skeleton->GetPath().empty() || BlendSpace.GetSamples().IsEmpty()) return false;
	json Data = { { "Type", "BlendSpace1D" }, { "Skeleton", BlendSpace.Skeleton->GetPath() },
		{ "Samples", json::array() } };
	for (const UBlendSpace1D::FSample& Sample : BlendSpace.GetSamples())
	{
		if (!Sample.Sequence || Sample.Sequence->Skeleton != BlendSpace.Skeleton ||
			Sample.Sequence->GetPath().empty()) return false;
		Data["Samples"].push_back({ { "X", Sample.X }, { "Sequence", Sample.Sequence->GetPath() } });
	}
	std::ofstream Out(ContentPaths::AssetRoot / FbxPath::FromString(Key));
	if (!Out) return false;
	Out << Data.dump(2);
	if (!Out) return false;
	Get().RegisterAsset(Key, &BlendSpace);
	return true;
}

bool UAssetManager::SaveAnimSequence(const UAnimSequence& Sequence)
{
	// 키 데이터는 FBX에서 다시 만들 수 있으므로 트랙은 그대로 두고 에디터에서 바꾸는 재생 설정만 고쳐 쓴다.
	const FString& Key = Sequence.GetPath();
	FString Path, Error;
	if (Key.empty() || GetAssetExtension(Key) != ".fbxbin" || GetAssetByPath<UAnimSequence>(Key) != &Sequence ||
		!FbxPath::ResolveKey(Key, Path, Error)) return false;
	FFbxAssetData Data;
	if (!FFbxAssetBake::ReadBaked(Path, Data, Error) || Data.Type != EFbxAssetType::AnimSequence)
	{
		HTR_LOG(Warning, "[Anim] Cannot save {}: {}", Key, Error);
		return false;
	}
	Data.Animation.bEnableRootMotion = Sequence.bEnableRootMotion;
	Data.Animation.bForceRootLock = Sequence.bForceRootLock;
	Data.Animation.bUseNormalizedRootMotionScale = Sequence.bUseNormalizedRootMotionScale;
	Data.Animation.RootMotionRootLock = static_cast<uint8>(Sequence.RootMotionRootLock);
	Data.Animation.RateScale = Sequence.RateScale;
	Sequence.ExportNotifies(Data.Animation.Notifies);
	// 쓰는 도중 실패해도 원본이 남도록 임시 파일에 쓰고 바꾼다.
	const fs::path Target = FbxPath::FromString(Path);
	fs::path Staged = Target; Staged += L".saving";
	if (!FFbxAssetBake::WriteBaked(FbxPath::ToString(Staged), Data, Error))
	{
		std::error_code Ignored; fs::remove(Staged, Ignored);
		HTR_LOG(Warning, "[Anim] Cannot save {}: {}", Key, Error);
		return false;
	}
	std::error_code Ec;
	fs::rename(Staged, Target, Ec);
	if (Ec) { fs::remove(Staged, Ec); return false; }
	return true;
}

USkeletalMesh* UAssetManager::LoadFbxSkeletalMesh(const FString& Path)
{
	if (GetAssetExtension(Path) == ".fbxbin") return GetAssetByPath<USkeletalMesh>(Path);
	const FString Key = MakeAssetKey(Path);
	if (USkeletalMesh* Cached = GetAssetByPath<USkeletalMesh>(Key))
		return Cached;
	if (GetAssetExtension(Path) == ".fbx" && fs::exists(FbxPath::FromString(Path)))
	{
		FFbxImportResult Imported = FFbxAssetLoader::Import(Path, FFbxImportOptions{});
		for (const FString& ImportedKey : Imported.AssetKeys)
		{
			if (USkeletalMesh* Mesh = GetAssetByPath<USkeletalMesh>(ImportedKey))
				return Mesh;
		}
	}
	return nullptr;
}

bool UAssetManager::ReorientStaticMesh(UStaticMesh& Mesh, const std::function<FVector(const FVector&)>& Rotate)
{
	if (Mesh.GetLODCount() > 1 || Mesh.MeshData.Vertices.IsEmpty())
		return false;   // LOD가 이미 옛 방향으로 만들어졌으면 서로 어긋난다

	FStaticMeshData& Data = Mesh.MeshData;
	FBox Bounds{ FVector(FLT_MAX, FLT_MAX, FLT_MAX), FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX) };
	for (FVertex& Vertex : Data.Vertices)
	{
		Vertex.Position = Rotate(Vertex.Position);
		Vertex.Normal = Rotate(Vertex.Normal);
		Bounds.Min = FVector(std::min(Bounds.Min.X, Vertex.Position.X), std::min(Bounds.Min.Y, Vertex.Position.Y), std::min(Bounds.Min.Z, Vertex.Position.Z));
		Bounds.Max = FVector(std::max(Bounds.Max.X, Vertex.Position.X), std::max(Bounds.Max.Y, Vertex.Position.Y), std::max(Bounds.Max.Z, Vertex.Position.Z));
	}
	Data.AABB = Bounds;
	Data.BuildTangentSpace();

	// 인덱스는 그대로다 (회전은 삼각형 감기 방향을 바꾸지 않는다). 정점 버퍼만 다시 올린다.
	TUniquePtr<FVertexBuffer> VB = RenderCommand::CreateStaticVertexBuffer(Data.Vertices.GetData(),
		sizeof(FVertex) * static_cast<uint32>(Data.Vertices.Num()), sizeof(FVertex));
	if (!VB)
		return false;
	Mesh.VertexBuffer = std::move(VB);

	Data.BuildTriangleBVH();   // 피킹용 삼각형 BVH도 새 좌표로
	return true;
}

void UAssetManager::ForEachObjStaticMesh(const std::function<void(const FString& Key, UStaticMesh& Mesh)>& Func)
{
	for (auto& [Key, Asset] : Get().AssetMap)
	{
		if (!Asset || !Asset->IsA<UStaticMesh>() || !Key.ends_with(".obj"))
			continue;
		Func(Key, *Cast<UStaticMesh>(Asset));
	}
}

FLODGenerateResult UAssetManager::GenerateStaticMeshLODs(UStaticMesh& Mesh, const FLODGenerateRequest& Request)
{
	TArray<FStaticMeshData> Generated;
	FLODGenerateResult Result = FStaticMeshLODGenerator::Generate(Mesh.GetMeshData(), Generated);

	if (!Result.bSuccess) return Result;

	if (!(Request.ScreenThresholds[0] >
		Request.ScreenThresholds[1] &&
		Request.ScreenThresholds[1] >
		Request.ScreenThresholds[2] &&
		Request.ScreenThresholds[2] > 0.0f))
	{
		Result.bSuccess = false;
		Result.FailureReason = "Screen thresholds must descend";
		return Result;
	}

	TArray<FStaticMeshLODResource> NewResources;

	for (FStaticMeshData& Data : Generated)
	{
		FStaticMeshLODResource Resource;
		Resource.Data = std::move(Data);

		Resource.Data.BuildTangentSpace();
		Resource.Data.OptimizeTriangleOrderForVertexCache();

		Resource.VertexBuffer = RenderCommand::CreateStaticVertexBuffer(
			Resource.Data.Vertices.GetData(),
			sizeof(FVertex) *
			static_cast<uint32>(Resource.Data.Vertices.Num()),
			sizeof(FVertex));

		Resource.IndexBuffer = RenderCommand::CreateStaticIndexBuffer(
			Resource.Data.Indices.GetData(),
			static_cast<uint32>(Resource.Data.Indices.Num()));

		if (!Resource.VertexBuffer || !Resource.IndexBuffer)
		{
			Result.bSuccess = false;
			Result.FailureReason = "LOD GPU buffer creation failed";
			return Result;
		}

		NewResources.Add(std::move(Resource));
	}

	// bSaveToAsset이면 이 지점에서 파일 저장.
	// 저장 실패 시 기존 Mesh.AdditionalLODs는 그대로 둔다.

	Mesh.AdditionalLODs = std::move(NewResources);
	Mesh.ScreenThresholds = Request.ScreenThresholds;
	Mesh.RebuildRenderData();
	return Result;
}

UParticleSystem* UAssetManager::LoadParticleSystem(const FString& AssetPath)
{
	return GetAssetByPath<UParticleSystem>(AssetPath);
}

UParticleSystem* UAssetManager::LoadParticleSystemFile(const FString& Key, const FString& Path)
{
	std::ifstream In(FbxPath::FromString(Path));
	if (!In) return nullptr;
	const json Data = json::parse(In, nullptr, false);
	FString Error;
	UParticleSystem* System = ParticleAsset::FromJson(Data, Error);
	if (!System)
	{
		HTR_LOG(Error, "[Particle] Failed to load '{}': {}", Path, Error);
		return nullptr;
	}
	Get().RegisterAsset(Key, System);
	return System;
}

bool UAssetManager::SaveParticleSystem(const FString& AssetPath, UParticleSystem& System)
{
	const FString Key = AssetPath.rfind("Assets/", 0) == 0 ? AssetPath.substr(7) : AssetPath;
	if (Key.empty() || FbxPath::FromString(Key).extension() != ".particle")
	{
		HTR_LOG(Error, "[Particle] Cannot save '{}': the extension must be .particle", AssetPath);
		return false;
	}

	if (!WriteParticleSystemFile(ContentPaths::AssetRoot / FbxPath::FromString(Key), System)) return false;

	// 같은 키에 다른 에셋이 읽혀 있었으면 폐기 표시를 한다 (그 에셋을 쓰는 쪽이 참조를 바꿀 때까지 포인터는 살아 있다)
	if (URenderAsset* Previous = Get().FindLoaded(Key); Previous && Previous != &System)
	{
		Previous->SetRetired(true);
	}
	Get().RegisterAsset(Key, &System);
	return true;
}

void UAssetManager::UnregisterAsset(const FString& AssetPath)
{
	Get().AssetMap.Remove(AssetCacheKey(AssetPath));
	Get().AssetFiles.Remove(CatalogKey(AssetPath));
}

bool UAssetManager::WriteParticleSystemFile(const fs::path& FilePath, UParticleSystem& System)
{
	std::error_code Ec;
	fs::create_directories(FilePath.parent_path(), Ec);
	std::ofstream Out(FilePath);
	if (!Out) return false;
	Out << ParticleAsset::ToJson(System).dump(2);
	Out.flush();
	return static_cast<bool>(Out);
}
