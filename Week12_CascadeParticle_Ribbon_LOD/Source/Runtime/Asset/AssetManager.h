#pragma once

#include "UObject/Object.h"
#include "UObject/Property.h"
#include "Render/Renderer.h"
#include "Asset/LOD/StaticMeshLODGenerator.h"
#include "Asset/AnimSequence.h"
#include "Asset/BlendSpace.h"

#include <filesystem>
#include <functional>
#include <set>

#include "AssetManager.generated.h"

enum class EAssetType
{
	Unknown,
	Texture,
	Font,
	Material,
	StaticMesh,
	SkeletalMesh,
	Skeleton,
	AnimSequence,
	BlendSpace,
	ParticleSystem,
};

class UStaticMesh;
class UParticleSystem;
class USkeletalMesh;
class USkeleton;
class UBlendSpace1D;
class UBlendSpace2D;
class USoundWave;

namespace fs = std::filesystem;

UCLASS()
class UAssetManager : public UObject
{
	GENERATED_BODY()
private:
	UAssetManager() = default;

	UAssetManager(const UAssetManager& src) =  delete;
	UAssetManager& operator= (const UAssetManager& src) = delete;

	using FAssetLoadProgress = std::function<void(int32 Loaded, int32 Total, const FString& Path)>;
public:
	static UAssetManager& Get();
	static void Init(const FAssetLoadProgress& OnProgress = nullptr);
	static void Shutdown();
	static void RefreshAssetCatalog();



	void CreateDefaultTextures();
	void CreateDefaultMeshes();
	void CreateDefaultMaterial();
	void CreateDefaultTranslucentMaterial();
	void CreateBillboardMaterial();
	void CreateParticleMaterial();
	// Cascade 파티클 시스템 이미터용 기본 머티리얼 (ResourcePaths::ParticleSpriteMaterial 등)
	void CreateParticleSystemMaterials();
	void CreateEditorArrowMaterial();
	void CreateBoneWeightHeatmapMaterial();

	// FindLoadedAsset is for cache checks. GetAssetByPath resolves the file and loads it on demand.
	template <typename T>
	static T* FindLoadedAsset(const FString& Path)
	{
		return Cast<T>(Get().FindLoaded(Path));
	}

	template <typename T>
	static T* GetAssetByPath(const FString& Path)
	{
		return Cast<T>(Get().FindOrLoad(Path, T::StaticClass()));
	}

	// Catalog queries return paths without loading the assets. Skeleton filters compare asset keys.
	static EAssetType GetAssetType(const FString& Path);
	static TArray<FString> GetAssetPaths(EAssetType Type, const FString& SkeletonPath = {});
	static FString GetAssetSkeletonPath(const FString& Path);
	static TArray<FString> GetAnimationSkeletonPaths();
	static USkeletalMesh* LoadFirstSkeletalMeshForSkeleton(const USkeleton* Skeleton);
	void RegisterAsset(const FString& Key, URenderAsset* Asset);
	void PrepareAssetRegistration(const TMap<FString, URenderAsset*>& Replacements, TMap<FString, URenderAsset*>& Prepared) const;
	void CommitAssetRegistration(TMap<FString, URenderAsset*>& Prepared) noexcept;

	//UStaticMesh* GetMesh(FString InName);
	//UStaticMesh* GetMesh(EPrimitiveType Type); // 오버로드(수정 중)

	UTexture2D* LoadTexture(const FString& InPath, bool bGenerateMips = true);
	UFont* LoadFontAtlas(const FString& JsonPath, const FString& AtlasTexturePath);
	USoundWave* LoadSound(const FString& Path);
	static UStaticMesh* LoadObjStaticMesh(const FString& Path);
	static USkeleton* LoadSkeleton(const FString& AssetPath);
	static UAnimSequence* LoadAnimSequence(const FString& AssetPath);
	static bool SaveAnimSequence(const UAnimSequence& Sequence);
	static UBlendSpace* LoadBlendSpace(const FString& AssetPath);
	// .particle 파일을 읽는다. 이미 읽은 에셋이면 같은 포인터를 돌려준다.
	static UParticleSystem* LoadParticleSystem(const FString& AssetPath);
	// 에셋을 Assets/ 아래 .particle 파일로 쓰고 AssetPath로 등록한다. 이미 읽혀 있던 에셋은 같은 포인터를 유지하므로
	// 이 에셋을 쓰는 컴포넌트의 인스턴스는 안전하다 (지우고 다시 읽지 않는다). 확장자가 .particle이 아니면 false.
	static bool SaveParticleSystem(const FString& AssetPath, UParticleSystem& System);
	// 등록만 지운다. 에셋 객체와 파일은 그대로다. 에셋 객체를 delete하기 전에 불러서 매니저가 지운 객체를 가리키지 않게 한다.
	static void UnregisterAsset(const FString& AssetPath);
	// 등록 없이 파일 하나만 쓴다 (SaveParticleSystem이 쓰는 부분). 부모 폴더를 만든다. 자체 테스트가 임시 폴더에 쓸 때 쓴다.
	static bool WriteParticleSystemFile(const fs::path& FilePath, UParticleSystem& System);
	static bool SaveBlendSpace1D(const FString& AssetPath, UBlendSpace1D& BlendSpace);
	static bool SaveBlendSpace2D(const FString& AssetPath, UBlendSpace2D& BlendSpace);
	static USkeletalMesh* LoadFbxSkeletalMesh(const FString& Path);
	// 시작 스캔에서 바이너리를 찾지 못한 FBX 경로. 에디터가 임포트 창으로 넘긴다.
	const TArray<FString>& GetFbxImportCandidates() const { return FbxImportCandidates; }
	void MarkFbxImported(const FString& SourceIdentity);
	bool IsFbxImported(const FString& SourceIdentity) const;

	FLODGenerateResult GenerateStaticMeshLODs(UStaticMesh& Mesh,const FLODGenerateRequest& Request);

	// 원본 좌표축이 엔진과 다른 메시를 로드 후 돌린다 (정점·법선·AABB·삼각형 BVH·GPU 정점 버퍼를 다시 만든다).
	// LOD는 LOD0에서 만들어지므로 LOD를 만들기 전에만 부를 수 있다. Rotate는 회전(길이·손잡이 보존)이어야 한다.
	static bool ReorientStaticMesh(UStaticMesh& Mesh, const std::function<FVector(const FVector&)>& Rotate);

	// 로드된 OBJ 스태틱 메시(키가 .obj로 끝나는 것)마다 부른다. 기본 도형·기즈모 메시는 제외된다.
	static void ForEachObjStaticMesh(const std::function<void(const FString& Key, UStaticMesh& Mesh)>& Func);

private:
	struct FAssetFile
	{
		FString Path;
		EAssetType Type = EAssetType::Unknown;
		FString SkeletonKey;
	};

	void ScanAssets(const fs::path& AssetRoot, const FAssetLoadProgress& OnProgress);
	const FAssetFile* FindOrDiscoverFile(const FString& Key);
	URenderAsset* FindLoaded(const FString& Path) const;
	URenderAsset* FindOrLoad(const FString& Path, const UClass* ExpectedClass);
	URenderAsset* LoadAssetFile(const FString& Key, const FAssetFile& File);
	UBlendSpace* LoadBlendSpaceFile(const FString& Key, const FString& Path);
	UParticleSystem* LoadParticleSystemFile(const FString& Key, const FString& Path);

	TMap<FString, FAssetFile> AssetFiles;
	TMap<FString, URenderAsset*> AssetMap;
	TMap<FString, USoundWave*> SoundMap;
	TArray<FString> FbxImportCandidates;
	std::set<FString> ImportedFbxSources; // 소유 FBX 식별자, 대소문자 무시

	//TMap<FString, UStaticMesh*> MeshMap;	// KEY: FILE NAME OR 쉐입 첫글자 대문자
	TMap<FString, TUniquePtr<FShader>> ShaderMap;	// KEY: FILE NAME

	//TMap<FString, UTexture2D*> TextureMap;
};
