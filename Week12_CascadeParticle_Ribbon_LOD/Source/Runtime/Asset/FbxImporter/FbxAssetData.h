#pragma once

#include "Animation/ReferenceSkeleton.h"
#include "Asset/AnimNotifyEvent.h"
#include "Asset/AnimDataModel.h"
#include "Render/SkeletalMesh.h"
#include "Render/StaticMeshData.h"
#include "Render/ImageLoader.h"
#include "Render/RenderEnums.h"
#include "Render/RenderStates.h"
#include <array>

struct FSkinWeightData
{
	TArray<std::array<uint16, 4>> InfluenceBones;
	TArray<std::array<float, 4>> InfluenceWeights;
	bool Validate(int32 VertexCount, int32 BoneCount, FString& Error) const;
};

enum class EFbxAssetType : uint32
{
	StaticMesh = 1,
	SkeletalMesh,
	Skeleton,
	Material,
	Texture2D,
	AnimSequence,

	// 새 타입은 위에 추가하고 Last를 옮긴다. .fbxbin 헤더 검사가 이 범위를 쓴다.
	First = StaticMesh,
	Last = AnimSequence,
};

enum class EFbxAnimationRange : uint8 { Timeline, AnimatedKeys, SetRange };
enum class EFbxFrameAlignment : uint8 { None, Floor, Closest, Ceiling };

struct FFbxImportOptions
{
	bool bBakeStaticMeshes = true;
	bool bCombineStaticMeshes = false;
	bool bBakeSkeletalMeshes = true;
	bool bCombineSkeletalMeshes = true;
	bool bImportStaticMeshes = true;
	bool bImportSkeletalMeshes = true;
	bool bImportAnimations = true;
	FString MeshSkeletonKey;
	FString TargetSkeletonKey;
	bool bAddMissingBones = false;
	bool bConvertMeshesInBoneHierarchy = true;
	bool bConvertMeshesWithAnimatedTransforms = true;
	bool bHumanoidRetarget = false;
	bool bBakeHipsToRoot = false;
	EFbxAnimationRange AnimationRange = EFbxAnimationRange::Timeline;
	EFbxFrameAlignment FrameAlignment = EFbxFrameAlignment::None;
	int32 FirstAnimationFrame = 0;
	int32 LastAnimationFrame = 0;
	int32 CustomSampleRate = 0;
	float MinTargetBoneMatch = 0.f;
	bool bForceFrontX = true;
	float UniformScale = 1.f;
	bool bKeepNotifies = true;
};

struct FFbxMaterialData
{
	FVector4 DiffuseOrFactor{ 1, 1, 1, 1 }; // 선형 RGB: Diffuse 맵이 없으면 Diffuse × DiffuseFactor, 있으면 DiffuseFactor를 각 채널에 저장한다. W는 불투명도.
	float Shininess = 32.f;
	FVector SpecularColor = FVector(1, 1, 1); // 선형 반사광 색상
	float SpecularFactor = 1.f; // 반사광 강도 계수
	FVector EmissiveColor = FVector(0, 0, 0); // 선형 자체 발광 색상
	float EmissiveFactor = 1.f; // 자체 발광 강도 계수
	ELightingModel ShadingModel = ELightingModel::Phong;
	ESamplerState SamplerState = ESamplerState::LinearClamp;
	EBlendState BlendState = EBlendState::Opaque;
	FString DiffuseTexture;
	FString NormalTexture;
};

// 본 하나의 키. 이름은 FName이 아니라 원래 문자열로 둔다. FName은 "_숫자" 접미사를 번호로 떼어 내 "spine_01"이 "spine_1"로 바뀌므로
// 파일에 저장하면 원래 본 이름과 맞지 않게 된다.
struct FFbxBoneTrack
{
	FString BoneName;
	FRawAnimSequenceTrack InternalTrack;
};

// AnimStack 하나를 프레임마다 샘플링한 결과. 로더가 UAnimSequence + UAnimDataModel로 옮긴다.
// 트랙은 본 이름으로 연결하고, 대상 Skeleton에 없는 본은 빠질 수 있다. 성분마다 키는 NumberOfKeys개이거나 상수면 1개다.
// 루트 모션 설정은 에디터에서 바꿔 저장하는 값이다. 재임포트할 때 기존 파일의 값을 유지한다.
struct FFbxAnimationData
{
	FString Name; // AnimStack 이름
	FAnimFrameRate FrameRate;
	int32 NumberOfKeys = 0;
	TArray<FFbxBoneTrack> Tracks;
	int32 SourceTakeIndex = 0;
	bool bEnableRootMotion = false;
	bool bForceRootLock = false;
	bool bUseNormalizedRootMotionScale = false;
	uint8 RootMotionRootLock = 0; // ERootMotionRootLock
	float RateScale = 1.f;
	// 에디터에서 추가한 Notify. 재임포트할 때 기존 파일의 값을 유지한다(FFbxImportOptions::bKeepNotifies).
	TArray<FAnimNotifyRecord> Notifies;
};

struct FFbxMeshLODData
{
	FStaticMeshData MeshData;
	FSkinWeightData SkinWeights;
};

// Cook 결과다. SDK 포인터/GPU 리소스는 포함하지 않는다. 한 항목이 한 .fbxbin이다.
struct FFbxAssetData
{
	EFbxAssetType Type = EFbxAssetType::StaticMesh;
	FString Key;
	FString SourcePath; // 소유 FBX: Assets 상대 경로 또는 외부 원본의 절대 경로. 로딩 의존성이 아니다.
	FString SourceObject;
	FFbxImportOptions Options;
	bool bHasImportRecipe = true;
	bool bHasSharedBoneExtensions = false;
	// 실제로 읽은 파일. Reimport From File을 쓰면 소유 FBX와 달라질 수 있다.
	FString ActualSourcePath;
	uint64 SourceHash = 0; // 임포트 시점 ActualSourcePath의 내용 해시
	TArray<FFbxMeshLODData> LODs;
	TArray<FString> MaterialKeys;
	std::array<float, 3> ScreenThresholds{ .30f, .10f, .03f };
	FString SkeletonKey; // SkeletalMesh와 AnimSequence가 쓰는 Skeleton
	FReferenceSkeleton RefSkeleton;
	FFbxAnimationData Animation;
	FFbxMaterialData Material;
	FImageData Texture;

	bool Validate(FString& Error) const;
};

struct FFbxImportResult
{
	bool bSuccess = false;
	FString Error;
	TArray<FString> Warnings;
	TArray<FString> AssetKeys;
};
