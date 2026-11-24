#include "EnginePCH.h"
#include "Editor/ParticleEditor/ParticleSystemEditor.h"
#include "Editor/HitoriEd/EditorEngine.h"
#include "Editor/Details/PropertyEditor.h"
#include "UObject/Property.h"
#include "Editor/EditorUI/EditorUIScale.h"
#include "Asset/AssetManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Actor/SkyLightActor.h"
#include "GameFramework/Actor/DirectionalLightActor.h"
#include "Components/SkyLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ParticleSystemComponent.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/ParticleModule.h"
#include "Particles/ParticleEmitterInstances.h"
#include "Render/Texture2D.h"
#include "Collision/Ray.h"
#include "Editor/EditorUI/ToolbarIcons.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "Particles/ParticleSerialization.h"
#include "Particles/TypeData/ParticleModuleTypeDataBeam.h"
#include "Particles/TypeData/ParticleModuleTypeDataMesh.h"
#include "Particles/TypeData/ParticleModuleTypeDataRibbon.h"
#include "Particles/Collision/ParticleModuleCollision.h"
#include "Particles/Beam/ParticleModuleBeamNoise.h"
#include "Particles/Color/ParticleModuleColorOverLife.h"
#include "Particles/Size/ParticleModuleSizeMultiplyLife.h"
#include "Asset/FbxImporter/FbxPath.h"
#include "Render/Material.h"
#include "Core/ResourcePaths.h"
#include "Editor/HitoriEd/EditorDragDrop.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
	constexpr float SimSpeeds[] = { 1.0f, 0.5f, 0.25f, 0.1f, 0.01f };
	constexpr const char* SimSpeedLabels[] = { "100% Speed", "50% Speed", "25% Speed", "10% Speed", "1% Speed" };
	constexpr int32 SimSpeedCount = static_cast<int32>(sizeof(SimSpeeds) / sizeof(SimSpeeds[0]));

	FString MaterialAssetName(const FString& Path)
	{
		return FbxPath::ToString(FbxPath::FromString(Path).stem());
	}

	// 메시 이미터의 머티리얼 슬롯별 덮어쓰기. 슬롯마다 머티리얼 에셋을 고르거나 "Default"(메시 자체 머티리얼)로 되돌린다.
	// 값이 바뀌면 true를 돌려준다.
	bool DrawMeshMaterialOverrides(UParticleModuleTypeDataMesh* MeshData)
	{
		ImGui::Separator();
		ImGui::TextUnformatted("Material Overrides");
		if (!MeshData->Mesh)
		{
			ImGui::TextDisabled("Select a mesh to override its materials.");
			return false;
		}
		// 슬롯 수는 메시 섹션이 가리키는 슬롯 번호의 최댓값 + 1이다
		uint32 SlotCount = 0;
		for (const FStaticMeshSection& Section : MeshData->Mesh->GetMeshData(0).Sections)
		{
			SlotCount = std::max(SlotCount, Section.MaterialSlotIndex + 1);
		}
		bool bEdited = false;
		for (uint32 Slot = 0; Slot < SlotCount; ++Slot)
		{
			ImGui::PushID(static_cast<int>(Slot));
			UMaterial* Override = static_cast<int32>(Slot) < MeshData->MaterialOverrides.Num() ? MeshData->MaterialOverrides[Slot] : nullptr;
			const FString Label = std::format("Slot {}", Slot);
			FString Current = "Default";
			if (Override)
			{
				Current = Override->GetPath().empty() ? FString("(unnamed material)") : MaterialAssetName(Override->GetPath());
			}
			ImGui::TextUnformatted(Label.c_str());
			ImGui::SameLine();
			ImGui::SetNextItemWidth(-1.0f);
			EditorUIScale::LimitNextComboPopupWidth();
			if (ImGui::BeginCombo("##MaterialOverride", Current.c_str()))
			{
				auto SetOverride = [&](UMaterial* Material)
					{
						while (static_cast<int32>(Slot) >= MeshData->MaterialOverrides.Num())
							MeshData->MaterialOverrides.Add(nullptr);
						MeshData->MaterialOverrides[Slot] = Material;
						bEdited = true;
					};
				if (ImGui::Selectable("Default (mesh material)", Override == nullptr) && Override != nullptr)
				{
					SetOverride(nullptr);
				}
				TArray<FString> Paths = UAssetManager::GetAssetPaths(EAssetType::Material);
				std::sort(Paths.begin(), Paths.end(), [](const FString& A, const FString& B) { return MaterialAssetName(A) < MaterialAssetName(B); });
				for (const FString& Path : Paths)
				{
					const bool bSelected = Override && UAssetManager::FindLoadedAsset<UMaterial>(Path) == Override;
					ImGui::PushID(Path.c_str());
					if (ImGui::Selectable(MaterialAssetName(Path).c_str(), bSelected) && !bSelected)
					{
						if (UMaterial* Material = UAssetManager::GetAssetByPath<UMaterial>(Path))
							SetOverride(Material);
					}
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Path.c_str());
					ImGui::PopID();
				}
				ImGui::EndCombo();
			}
			ImGui::PopID();
		}
		return bEdited;
	}

	// Required.Material 편집 (UE: Required 모듈의 Material 칸). 바꿨으면 true.
	//  - Material: 엔진 파티클 머티리얼 에셋 중 하나, 또는 None(엔진 기본 스프라이트 머티리얼)
	//  - Texture: 고른 머티리얼의 텍스처만 바꾼 인스턴스를 만든다 (UE의 Material Instance 텍스처 파라미터). 블렌드는 원본 머티리얼을 따른다.
	// DefaultKey는 None일 때 실제로 그려지는 머티리얼이다 (엔진 기본 스프라이트 머티리얼).
	bool DrawParticleMaterial(UParticleModuleRequired* Required, const char* DefaultKey)
	{
		struct FEntry { const char* Key; const char* Label; };
		static const FEntry Entries[] = {
			{ ResourcePaths::ParticleSpriteMaterial, "Sprite (Alpha)" },
			{ ResourcePaths::ParticleSpriteAdditiveMaterial, "Sprite (Additive)" },
			{ ResourcePaths::ParticleBeamMaterial, "Beam (Additive)" },
		};
		bool bChanged = false;
		ImGui::PushID("ParticleMaterial");
		ImGui::SeparatorText("Material");

		UMaterial* Current = Required->Material;
		const UMaterial* Base = Current ? Current->GetBaseAsset() : nullptr;
		FString Preview = "None";
		for (const FEntry& Entry : Entries)
			if (Base && Base->GetPath() == Entry.Key) Preview = Entry.Label;
		if (Base && Preview == "None") Preview = Base->GetPath();
		if (Current && Current->bIsInstance) Preview += " (Instance)";

		if (ImGui::BeginCombo("Material", Preview.c_str()))
		{
			if (ImGui::Selectable("None", !Current)) { Required->SetMaterial(nullptr); bChanged = true; }
			ImGui::SetItemTooltip("Use the emitter type's default material (%s).", DefaultKey);
			for (const FEntry& Entry : Entries)
			{
				UMaterial* Asset = UAssetManager::GetAssetByPath<UMaterial>(Entry.Key);
				if (Asset && ImGui::Selectable(Entry.Label, Current == Asset))
				{
					Required->SetMaterial(Asset);
					bChanged = true;
				}
			}
			ImGui::EndCombo();
		}

		// 지금 그려지는 머티리얼의 텍스처 (None이면 기본 머티리얼의 텍스처)
		const UMaterial* Shown = Current ? Current : UAssetManager::GetAssetByPath<UMaterial>(DefaultKey);
		UTexture2D* Texture = (Shown && !Shown->Textures.IsEmpty()) ? Shown->Textures[0] : nullptr;
		const float Thumb = EditorUIScale::Px(48.0f);
		if (Texture && Texture->GetResource()) ImGui::Image((ImTextureID)Texture->GetResource()->GetSRV(), { Thumb, Thumb });
		else ImGui::Button("Empty", { Thumb, Thumb });
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(EditorDragDrop::Texture))
			{
				Required->SetMaterialTexture(*static_cast<UTexture2D**>(Payload->Data), DefaultKey);
				bChanged = true;
			}
			ImGui::EndDragDropTarget();
		}
		ImGui::SetItemTooltip("Drop a texture from the Content Drawer to make a material instance with it.");
		ImGui::SameLine();
		ImGui::BeginGroup();
		const FString TexturePreview = Texture ? FbxPath::ToString(FbxPath::FromString(Texture->GetPath()).filename()) : FString("None");
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::BeginCombo("##Texture", TexturePreview.c_str()))
		{
			if (ImGui::Selectable("Material Default", Current && !Current->bIsInstance))
			{
				Required->SetMaterialTexture(nullptr, DefaultKey);
				bChanged = true;
			}
			for (const FString& Path : UAssetManager::GetAssetPaths(EAssetType::Texture))
			{
				ImGui::PushID(Path.c_str());
				if (ImGui::Selectable(Path.c_str(), Texture && Texture->GetPath() == Path))
				{
					if (UTexture2D* Picked = UAssetManager::Get().LoadTexture(Path))
					{
						Required->SetMaterialTexture(Picked, DefaultKey);
						bChanged = true;
					}
				}
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
		ImGui::TextDisabled("Texture");
		ImGui::EndGroup();

		ImGui::PopID();
		return bChanged;
	}

	struct FDistributionEditResult
	{
		bool bChanged = false;	// 종류를 바꿨다 (키를 새로 만든다)
	};

	// 분포 값은 UPROPERTY가 아니므로 Details에서 직접 편집한다.
	// Curve는 종류만 고르고, 키는 Curve Editor 패널의 그래프에서 편집한다.
	template <typename T>
	FDistributionEditResult DrawDistribution(const char* Label, TParticleDistribution<T>& Distribution, float MinValue = -FLT_MAX)
	{
		FDistributionEditResult Result;
		ImGui::PushID(Label);
		ImGui::SeparatorText(Label);
		// MinValue가 있으면 그 아래로 내려가지 않게 자른다 (음수가 의미 없는 값: 수명, 크기, 속도 등)
		const bool bClamp = MinValue > -FLT_MAX;
		const auto DrawValue = [MinValue, bClamp](const char* Name, T& Value)
		{
			const float Lo = bClamp ? MinValue : 0.0f;
			const float Hi = bClamp ? FLT_MAX : 0.0f;
			const ImGuiSliderFlags Flags = bClamp ? ImGuiSliderFlags_AlwaysClamp : ImGuiSliderFlags_None;
			if constexpr (std::is_same_v<T, float>) ImGui::DragFloat(Name, &Value, 0.01f, Lo, Hi, "%.3f", Flags);
			else ImGui::DragFloat3(Name, &Value.X, 0.01f, Lo, Hi, "%.3f", Flags);
		};

		// 순서는 EDistributionKind와 같다
		int Kind = static_cast<int>(Distribution.Kind);
		const char* Kinds[] = { "Constant", "Uniform", "Curve" };
		if (ImGui::Combo("Distribution", &Kind, Kinds, IM_ARRAYSIZE(Kinds)))
		{
			// 바꾸기 전의 값(시간 0)을 새 종류로 이어 쓴다. Uniform은 Min이다.
			const T Current = Distribution.GetValue(0.0f, nullptr);
			const EDistributionKind NewKind = static_cast<EDistributionKind>(Kind);
			Distribution.Keys = TArray<TDistributionKey<T>>();
			if (NewKind == EDistributionKind::Constant) Distribution.Constant = Current;
			else if (NewKind == EDistributionKind::Uniform) Distribution.Min = Distribution.Max = Current;
			else
			{
				// 지금 값 그대로인 평평한 곡선에서 시작한다
				Distribution.AddKey(0.0f, Current);
				Distribution.AddKey(1.0f, Current);
			}
			Distribution.Kind = NewKind;
			Result.bChanged = true;
		}

		if (Distribution.Kind == EDistributionKind::Constant) DrawValue("Value", Distribution.Constant);
		else if (Distribution.Kind == EDistributionKind::Uniform)
		{
			DrawValue("Min", Distribution.Min);
			DrawValue("Max", Distribution.Max);
		}
		else
		{
			// 키는 Curve Editor에서만 편집한다 (모듈 줄의 커브 아이콘으로 보낸다)
			ImGui::TextDisabled("%d keys. Edit them in the Curve Editor (curve icon on the module).", static_cast<int>(Distribution.Keys.Num()));
		}
		ImGui::PopID();
		return Result;
	}

	// "UParticleModuleColorOverLife" → "ColorOverLife"
	FString StripModulePrefix(const UClass* Class)
	{
		FString ClassName = Class ? Class->Name : FString();
		for (const char* Prefix : { "UParticleModule", "ParticleModule" })
		{
			if (ClassName.rfind(Prefix, 0) == 0) { ClassName = ClassName.substr(std::strlen(Prefix)); break; }
		}
		return ClassName;
	}

	// Cascade 모듈 이름. 표에 없으면 클래스 이름에서 접두사를 뗀다.
	FString GetModuleClassDisplayName(const UClass* Class)
	{
		if (!Class) return "Module";
		const FString ClassName = StripModulePrefix(Class);
		static const std::pair<const char*, const char*> Names[] = {
			{ "Required", "Required" }, { "Spawn", "Spawn" }, { "Lifetime", "Lifetime" },
			{ "Size", "Initial Size" }, { "Velocity", "Initial Velocity" }, { "Location", "Initial Location" },
			{ "Color", "Initial Color" }, { "ColorOverLife", "Color Over Life" }, { "SizeMultiplyLife", "Size By Life" },
			{ "TypeDataBeam", "Beam Data" }, { "TypeDataMesh", "Mesh Data" }, { "TypeDataRibbon", "Ribbon Data" },
			{ "MeshRotation", "Mesh Rotation" }, { "MeshRotationRate", "Mesh Rotation Rate" },
			{ "BeamNoise", "Noise" },
		};
		for (const auto& [Key, Display] : Names)
			if (ClassName == Key) return Display;
		return ClassName.empty() ? FString("Module") : ClassName;
	}

	// UE Cascade 이미터 우클릭 메뉴의 모듈 카테고리 (UE와 같은 순서). 아직 모듈이 없는 카테고리는 흐리게 보인다.
	constexpr const char* ModuleCategories[] = {
		"Acceleration", "Attraction", "Beam", "Camera", "Collision", "Color", "Event", "Kill", "Lifetime", "Light", "Location",
		"Orbit", "Orientation", "Parameter", "Rotation", "Rotation Rate", "Size", "Spawn", "SubUV", "Velocity",
	};

	// 모듈 클래스가 들어갈 카테고리. 표에 없으면 클래스 이름이 카테고리 이름으로 시작하는지 본다
	// (예: AccelerationConstant → Acceleration, RotationRate → Rotation Rate). 어디에도 안 맞으면 "Other".
	FString GetModuleCategory(const UClass* Class)
	{
		const FString Key = StripModulePrefix(Class);
		static const std::pair<const char*, const char*> Table[] = {
			{ "ColorOverLife", "Color" }, { "SizeMultiplyLife", "Size" },
			{ "MeshRotation", "Rotation" }, { "MeshRotationRate", "Rotation Rate" },
		};
		for (const auto& [Name, Category] : Table)
			if (Key == Name) return Category;
		FString Best;
		for (const char* Category : ModuleCategories)
		{
			FString Compact = Category;
			Compact.erase(std::remove(Compact.begin(), Compact.end(), ' '), Compact.end());
			// 더 긴 이름이 우선 (RotationRate가 Rotation보다 먼저 맞아야 한다)
			if (Key.rfind(Compact, 0) == 0 && Compact.size() > Best.size()) Best = Category;
		}
		return Best.empty() ? FString("Other") : Best;
	}

	// UE의 TypeData 메뉴 항목과 그 클래스 이름. 클래스가 아직 없으면 항목을 흐리게 둔다 (클래스를 추가하면 바로 켜진다).
	// UE의 Anim Trail(애니메이션 소켓 궤적)과 GPU Sprites(GPU 시뮬레이션)는 이 엔진에서 다루지 않아 뺐다.
	struct FTypeDataEntry { const char* Label; const char* ClassName; };
	constexpr FTypeDataEntry TypeDataEntries[] = {
		{ "New Beam Data", "UParticleModuleTypeDataBeam" },
		{ "New Mesh Data", "UParticleModuleTypeDataMesh" },
		{ "New Ribbon Data", "UParticleModuleTypeDataRibbon" },
	};

	UClass* FindTypeDataClass(const FTypeDataEntry& Entry)
	{
		UClass* Class = FindClass(Entry.ClassName);
		return Class && Class->Constructor && Class->IsChildOf(UParticleModuleTypeDataBase::StaticClass()) ? Class : nullptr;
	}

	// 검색어가 이름에 들어 있는지 (대소문자 무시)
	bool MatchesSearch(const FString& Text, const char* Search)
	{
		if (!Search || !*Search) return true;
		auto Lower = [](FString Value)
		{
			std::transform(Value.begin(), Value.end(), Value.begin(), [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
			return Value;
		};
		return Lower(Text).find(Lower(Search)) != FString::npos;
	}

	FString GetModuleDisplayName(const UParticleModule* Module)
	{
		return Module ? GetModuleClassDisplayName(Module->GetClass()) : FString("Module");
	}

	// 모듈 추가 메뉴에 내놓을 클래스 (만들 수 있는 것만, 표시 이름순).
	// Required는 이미터마다 하나로 고정이고, TypeData는 이미터 종류를 바꾸는 슬롯이라 따로 다룬다.
	const TArray<UClass*>& GetAddableModuleClasses()
	{
		static TArray<UClass*> Classes;
		static bool bBuilt = false;
		if (bBuilt) return Classes;
		bBuilt = true;
		TArray<UClass*> Derived;
		GetDerivedClasses(UParticleModule::StaticClass(), Derived);
		for (UClass* Class : Derived)
		{
			if (!Class || !Class->Constructor || Class == UParticleModule::StaticClass()) continue;
			if (Class->IsChildOf(UParticleModuleRequired::StaticClass()) || Class->IsChildOf(UParticleModuleTypeDataBase::StaticClass())) continue;
			// 파티클 레이아웃 자체 테스트용 모듈은 내놓지 않는다
			if (Class->Name.find("Test") != FString::npos) continue;
			Classes.Add(Class);
		}
		std::sort(Classes.begin(), Classes.end(), [](const UClass* A, const UClass* B)
			{ return GetModuleClassDisplayName(A) < GetModuleClassDisplayName(B); });
		return Classes;
	}

	// UE Cascade와 같이 Required와 Spawn은 지울 수 없다.
	bool CanRemoveModule(const UParticleLODLevel* LOD, const UParticleModule* Module)
	{
		return LOD && Module && Module != LOD->RequiredModule && Module != LOD->SpawnModule;
	}

	// 모듈이 가진 분포. Details와 모듈 줄의 커브 버튼이 같은 목록을 쓴다. 분포 멤버를 추가하면 여기에 넣는다.
	struct FModuleDistribution
	{
		const char* Label = nullptr;
		FFloatDistribution* Float = nullptr;
		FVectorDistribution* Vector = nullptr;
		float MinValue = -FLT_MAX;	// 이 아래 값은 Details와 Curve Editor에서 막는다 (음수가 의미 없는 분포는 0)
		bool bLife = false;	// 가로축이 파티클 수명 비율(0 ~ 1)이다 (Over Life 모듈). 아니면 이미터 시간(초)
	};
	TArray<FModuleDistribution> GetModuleDistributions(UParticleModule* Module)
	{
		TArray<FModuleDistribution> Out;
		const auto Add = [&Out](const char* Label, auto& Distribution, float MinValue = -FLT_MAX, bool bLife = false)
		{
			FModuleDistribution Entry;
			Entry.Label = Label;
			Entry.MinValue = MinValue;
			Entry.bLife = bLife;
			if constexpr (std::is_same_v<std::decay_t<decltype(Distribution)>, FFloatDistribution>) Entry.Float = &Distribution;
			else Entry.Vector = &Distribution;
			Out.Add(Entry);
		};
		if (auto* Spawn = Cast<UParticleModuleSpawn>(Module))
		{
			Add("Spawn Rate", Spawn->Rate, 0.0f);
			Add("Rate Scale", Spawn->RateScale, 0.0f);
		}
		else if (auto* Lifetime = Cast<UParticleModuleLifetime>(Module)) Add("Lifetime", Lifetime->Lifetime, 0.0f);
		else if (auto* Size = Cast<UParticleModuleSize>(Module)) Add("Initial Size", Size->StartSize, 0.0f);
		else if (auto* Location = Cast<UParticleModuleLocation>(Module)) Add("Initial Location", Location->StartLocation);
		else if (auto* Velocity = Cast<UParticleModuleVelocity>(Module))
		{
			Add("Initial Velocity", Velocity->StartVelocity);
			Add("Radial Velocity", Velocity->StartVelocityRadial);
		}
		else if (auto* Color = Cast<UParticleModuleColor>(Module))
		{
			Add("Initial Color", Color->StartColor);
			Add("Initial Alpha", Color->StartAlpha, 0.0f);
		}
		// 수명에 따라 바뀌는 값: 가로축이 수명 0 ~ 1이다
		else if (auto* ColorLife = Cast<UParticleModuleColorOverLife>(Module))
		{
			Add("Color Over Life", ColorLife->ColorOverLife, 0.0f, true);
			Add("Alpha Over Life", ColorLife->AlphaOverLife, 0.0f, true);
		}
		else if (auto* SizeLife = Cast<UParticleModuleSizeMultiplyLife>(Module)) Add("Life Multiplier", SizeLife->LifeMultiplier, 0.0f, true);
		else if (auto* Noise = Cast<UParticleModuleBeamNoise>(Module))
		{
			Add("Noise Range", Noise->NoiseRange);
		}
		else if (auto* Mesh = Cast<UParticleModuleTypeDataMesh>(Module)) Add("Roll Pitch Yaw Range", Mesh->RollPitchYawRange);
		else if (auto* MeshRotation = Cast<UParticleModuleMeshRotation>(Module)) Add("Start Rotation", MeshRotation->StartRotation);
		else if (auto* MeshRotationRate = Cast<UParticleModuleMeshRotationRate>(Module)) Add("Start Rotation Rate", MeshRotationRate->StartRotationRate);
		else if (auto* Collision = Cast<UParticleModuleCollision>(Module))
		{
			// 0 아래 값은 의미가 없다 (감쇠가 음수면 속도 방향이 뒤집히고, 횟수와 지연은 음수가 불가능하다)
			Add("Damping Factor", Collision->DampingFactor, 0.0f);
			Add("Damping Factor Rotation", Collision->DampingFactorRotation, 0.0f);
			Add("Max Collisions", Collision->MaxCollisions, 0.0f);
			Add("Delay Amount", Collision->DelayAmount, 0.0f);
		}
		// TODO(Ribbon): 분포 멤버가 생기면 여기에 넣는다
		return Out;
	}

	// Details 카테고리 (UE의 UPROPERTY Category와 선언 순서). 항목은 UPROPERTY 이름 또는 GetModuleDistributions의 Label이다.
	// 표가 없는 모듈은 기존처럼 클래스별 속성 뒤에 분포를 그린다.
	struct FDetailsCategory
	{
		const char* Name;
		const char* Items[12];
		bool bAdvanced = false;	// 처음에 접어 둔다 (UE: AdvancedDisplay). 평소에 만질 일이 없는 설정
	};
	const FDetailsCategory* GetDetailsCategories(const UParticleModule* Module, int32& OutCount)
	{
		// UE: ParticleModuleTypeDataBeam2.h (Beam, Distance). TargetPoint는 UE에서 BeamTarget 모듈의 값이라 따로 둔다.
		static const FDetailsCategory Beam[] = {
			{ "Beam", { "BeamMethod", "TextureTile", "Sheets", "InterpolationPoints" } },
			{ "Distance", { "Distance" } },
			{ "Target", { "TargetPoint" } },
		};
		// UE: ParticleModuleBeamNoise.h (LowFreq)
		static const FDetailsCategory Noise[] = {
			// 흔들림에 직접 쓰는 것: 켜기, 굽이 수, 크기, 방향 바꾸는 간격, 부드럽게
			{ "LowFreq", { "bLowFreq_Enabled", "Frequency", "Noise Range", "NoiseLockTime", "bSmooth" } },
			// 기본값으로 두면 되는 것
			{ "Advanced", { "NoiseSpeed", "NoiseLockRadius", "NoiseTessellation", "NoiseTangentStrength", "bTargetNoise" }, true },
		};
		if (Cast<UParticleModuleTypeDataBeam>(Module)) { OutCount = IM_ARRAYSIZE(Beam); return Beam; }
		if (Cast<UParticleModuleBeamNoise>(Module)) { OutCount = IM_ARRAYSIZE(Noise); return Noise; }
		OutCount = 0;
		return nullptr;
	}

	// 클래스 계층에서 이름이 같은 편집 가능 UPROPERTY
	const FProperty* FindEditProperty(const UObject* Object, const char* Name)
	{
		for (UClass* Class = Object ? Object->GetClass() : nullptr; Class; Class = Class->Super)
			for (const FProperty* Property : Class->GetProperties())
				if (Property->HasAnyFlags(CPF_Edit) && Property->GetName() == Name) return Property;
		return nullptr;
	}

	// 모듈 한 줄. 오른쪽 체크박스는 bEnabled를 토글한다 (Required는 끌 수 없어 nullptr).
	// bHasCurves면 체크박스 왼쪽에 커브 버튼을 그린다 (UE Cascade: 분포가 있는 모듈의 그래프 아이콘). bCurvesShown이면 밝게 그린다.
	// 반환값: 0 = 아무 일 없음, 1 = 행 클릭, 2 = 체크박스 토글, 3 = 커브 버튼
	int32 DrawModuleRow(const char* Id, const FString& Label, ImU32 Background, bool bSelected, bool* bEnabled, float Width,
		bool bHasCurves = false, bool bCurvesShown = false)
	{
		const float Height = ImGui::GetFrameHeight();
		const ImVec2 Min = ImGui::GetCursorScreenPos();
		const ImVec2 Max(Min.x + Width, Min.y + Height);
		const bool bClicked = ImGui::InvisibleButton(Id, ImVec2(Width, Height));
		const bool bHovered = ImGui::IsItemHovered();

		ImDrawList* DL = ImGui::GetWindowDrawList();
		DL->AddRectFilled(Min, Max, Background);
		if (bHovered) DL->AddRectFilled(Min, Max, IM_COL32(255, 255, 255, 18));
		if (bSelected) DL->AddRect(Min, Max, IM_COL32(255, 170, 40, 255), 0.0f, 0, EditorUIScale::Px(2.0f));
		const float Pad = ImGui::GetStyle().FramePadding.x;
		const ImU32 TextColor = (bEnabled && !*bEnabled) ? ImGui::GetColorU32(ImGuiCol_TextDisabled) : ImGui::GetColorU32(ImGuiCol_Text);
		DL->AddText(ImVec2(Min.x + Pad, Min.y + (Height - ImGui::GetTextLineHeight()) * 0.5f), TextColor, Label.c_str());

		const float Box = Height * 0.6f;
		const ImVec2 Mouse = ImGui::GetIO().MousePos;
		// 커브 버튼: 체크박스가 있으면 그 왼쪽, 없으면 오른쪽 끝
		if (bHasCurves)
		{
			const float Right = bEnabled ? Max.x - Pad - Box - Pad : Max.x - Pad;
			const ImVec2 IconMin(Right - Box, Min.y + (Height - Box) * 0.5f);
			const ImVec2 IconMax(Right, IconMin.y + Box);
			const ImU32 IconColor = bCurvesShown ? IM_COL32(120, 230, 120, 255) : ImGui::GetColorU32(ImGuiCol_TextDisabled);
			DL->AddRect(IconMin, IconMax, IconColor, 0.0f, 0, 1.0f);
			const ImVec2 Points[] = {
				ImVec2(IconMin.x + Box * 0.15f, IconMin.y + Box * 0.8f), ImVec2(IconMin.x + Box * 0.4f, IconMin.y + Box * 0.35f),
				ImVec2(IconMin.x + Box * 0.6f, IconMin.y + Box * 0.6f), ImVec2(IconMin.x + Box * 0.85f, IconMin.y + Box * 0.2f),
			};
			DL->AddPolyline(Points, IM_ARRAYSIZE(Points), IconColor, ImDrawFlags_None, EditorUIScale::Px(1.5f));
			if (bClicked && Mouse.x >= IconMin.x - Pad * 0.5f && Mouse.x <= IconMax.x + Pad * 0.5f) return 3;
			if (bHovered && Mouse.x >= IconMin.x && Mouse.x <= IconMax.x && Mouse.y >= IconMin.y && Mouse.y <= IconMax.y)
				ImGui::SetTooltip("Send this module's curves to the Curve Editor");
		}

		if (!bEnabled) return bClicked ? 1 : 0;

		const ImVec2 BoxMin(Max.x - Pad - Box, Min.y + (Height - Box) * 0.5f);
		const ImVec2 BoxMax(BoxMin.x + Box, BoxMin.y + Box);
		DL->AddRect(BoxMin, BoxMax, ImGui::GetColorU32(ImGuiCol_Text), 0.0f, 0, 1.0f);
		if (*bEnabled)
		{
			const float T = EditorUIScale::Px(1.5f);
			DL->AddLine(ImVec2(BoxMin.x + Box * 0.2f, BoxMin.y + Box * 0.55f), ImVec2(BoxMin.x + Box * 0.42f, BoxMin.y + Box * 0.78f), ImGui::GetColorU32(ImGuiCol_CheckMark), T);
			DL->AddLine(ImVec2(BoxMin.x + Box * 0.42f, BoxMin.y + Box * 0.78f), ImVec2(BoxMin.x + Box * 0.82f, BoxMin.y + Box * 0.25f), ImGui::GetColorU32(ImGuiCol_CheckMark), T);
		}
		if (bClicked)
		{
			if (Mouse.x >= BoxMin.x - Pad && Mouse.x <= Max.x)
			{
				*bEnabled = !*bEnabled;
				return 2;
			}
			return 1;
		}
		return 0;
	}
}

FParticleSystemEditor::FParticleSystemEditor(const FString& InName, uint32 InId, UParticleSystem* InSystem, const FString& InAssetPath)
	: Name(InName), AssetPath(InAssetPath)
{
	const FString Key = "ParticleEditor/" + std::to_string(InId);
	WindowKey = Key;
	WindowName = Name + "###" + Key;
	DockSpaceId = ImHashStr((Key + "/Dock").c_str());
	PanelClassId = ImHashStr((Key + "/Panels").c_str());
	// ### 뒤 ID를 고정 키로 두어 표시 이름을 바꿔도 레이아웃(ini)이 유지되게 한다.
	static const char* Labels[] = { "Viewport", "Emitters", "Details", "Curve Editor" };
	static const char* Keys[] = { "Viewport", "Emitters", "Details", "CurveEditor" };
	for (int32 Index = 0; Index < PanelCount; ++Index)
		PanelNames[Index] = FString(Labels[Index]) + "###" + Key + "/" + Keys[Index];

	if (InSystem) System = InSystem;
	else BuildDefaultSystem();
	if (System) System->SyncLODDistances();
	// 작업 사본에도 경로를 붙여 둔다 (컴포넌트의 "경로 없는 Template" 경고 방지). 에셋 등록은 하지 않는다.
	if (System && !AssetPath.empty()) System->SetPath(AssetPath);
	CreatePreviewWorld();
	BuildToolbarItems();
}

void FParticleSystemEditor::SetDisplayName(const FString& InName)
{
	Name = InName;
	// ### 뒤 키는 그대로라 ImGui 창·도킹 상태는 유지된다
	WindowName = Name + "###" + WindowKey;
}

bool FParticleSystemEditor::Save()
{
	if (AssetPath.empty())
	{
		PrepareSaveAs();
		bOpenSaveAs = true;
		return false;
	}
	return SaveAs(AssetPath);
}

namespace
{
	// 새 파티클의 기본 저장 폴더 (Content Drawer의 Create Particle System과 같은 위치)
	constexpr const char* DefaultParticleFolder = "Assets/Particles";
}

void FParticleSystemEditor::PrepareSaveAs()
{
	FString Folder = DefaultParticleFolder;
	FString FileName = Name;
	if (!AssetPath.empty())
	{
		const fs::path Path = FbxPath::FromString(AssetPath);
		Folder = FbxPath::ToString(Path.parent_path());
		FileName = FbxPath::ToString(Path.stem());
	}
	std::replace(Folder.begin(), Folder.end(), '\\', '/');
	strncpy_s(SaveAsFolder, Folder.c_str(), _TRUNCATE);
	strncpy_s(SaveAsName, FileName.c_str(), _TRUNCATE);
	bFocusSaveAsName = true;
}

bool FParticleSystemEditor::SaveAs(const FString& InAssetPath)
{
	if (!System) return false;

	// 등록할 에셋은 작업 사본과 다른 객체로 만든다. 레벨 컴포넌트가 이 에셋을 쓰는 동안에도 에디터는 계속 사본을 고칠 수 있다.
	FString Error;
	UParticleSystem* Saved = ParticleAsset::Duplicate(*System, Error);
	if (!Saved)
	{
		HTR_LOG(Error, "[ParticleEditor] Cannot save '{}': {}", InAssetPath, Error);
		return false;
	}
	if (!UAssetManager::SaveParticleSystem(InAssetPath, *Saved))
	{
		DestroyParticleSystem(Saved);
		HTR_LOG(Error, "[ParticleEditor] Cannot save '{}'", InAssetPath);
		return false;
	}

	// 같은 경로의 이전 에셋은 매니저가 Retired로 표시했다. 그 에셋을 쓰던 레벨 컴포넌트를 새 에셋으로 옮긴다.
	// SetTemplate가 인스턴스를 없애고, 다음 Tick에서 새 구성으로 다시 만든다. 이전 객체는 다른 Retired 에셋처럼 남겨 둔다.
	const FString& SavedKey = Saved->GetPath();
	for (TObjectIterator<UParticleSystemComponent> It; It; ++It)
	{
		UParticleSystemComponent* Other = *It;
		UParticleSystem* Template = Other ? Other->GetTemplate() : nullptr;
		if (!Template || Template == Saved || Template == System || Other == Component) continue;
		if (Template->GetPath() == SavedKey) Other->SetTemplate(Saved);
	}

	AssetPath = InAssetPath;
	System->SetPath(SavedKey);
	SetDisplayName(FbxPath::ToString(FbxPath::FromString(InAssetPath).stem()));
	bDirty = false;
	HTR_LOG(Info, "[ParticleEditor] Saved '{}'", InAssetPath);
	return true;
}

FParticleSystemEditor::~FParticleSystemEditor()
{
	Client.SetWorld(nullptr);
	if (Component) Component->SetTemplate(nullptr);
	Component = nullptr;
	if (World)
	{
		World->ClearWorld();
		World->FlushPendingDestruction();
		if (GEngine) GEngine->DestroyWorld(World);
		World = nullptr;
	}
	DestroyParticleSystem(System);
	System = nullptr;
}

// 프리뷰 UI는 로컬 구성을 유지하고 에셋과 시뮬레이션은 development API를 사용한다.
void FParticleSystemEditor::BuildDefaultSystem()
{
	System = NewObject<UParticleSystem>();
	AddDefaultEmitter();
}

UParticleEmitter* FParticleSystemEditor::AddDefaultEmitter()
{
	UParticleEmitter* Emitter = System ? System->AddEmitter() : nullptr;
	if (!Emitter) return nullptr;
	UParticleLODLevel* LOD = Emitter->AddLODLevel();
	if (!LOD) return Emitter;
	auto* Required = Cast<UParticleModuleRequired>(LOD->AddModule(UParticleModuleRequired::StaticClass()));
	auto* Spawn = Cast<UParticleModuleSpawn>(LOD->AddModule(UParticleModuleSpawn::StaticClass()));
	auto* Lifetime = Cast<UParticleModuleLifetime>(LOD->AddModule(UParticleModuleLifetime::StaticClass()));
	auto* Size = Cast<UParticleModuleSize>(LOD->AddModule(UParticleModuleSize::StaticClass()));
	auto* Velocity = Cast<UParticleModuleVelocity>(LOD->AddModule(UParticleModuleVelocity::StaticClass()));
	auto* Color = Cast<UParticleModuleColor>(LOD->AddModule(UParticleModuleColor::StaticClass()));

	Required->EmitterDuration = 1.0f;
	Required->EmitterLoops = 0;
	Spawn->Rate = FFloatDistribution::MakeConstant(20.0f);
	Lifetime->Lifetime = FFloatDistribution::MakeConstant(1.0f);
	Size->StartSize = FVectorDistribution::MakeUniform(FVector(0.20f), FVector(0.30f));
	Velocity->StartVelocity = FVectorDistribution::MakeUniform(FVector(-0.5f, -0.5f, 0.5f), FVector(0.5f, 0.5f, 1.5f));
	Color->StartColor = FVectorDistribution::MakeConstant(FVector(1.0f));
	Color->StartAlpha = FFloatDistribution::MakeConstant(1.0f);

	const int32 TargetLODCount = System ? std::max(1, System->GetNumLODLevels()) : 1;
	while (Emitter->GetNumLODLevels() < TargetLODCount)
	{
		Emitter->InsertLODLevel(Emitter->GetNumLODLevels(), LOD, 1.0f);
	}
	Emitter->CacheEmitterModuleInfo();
	if (System) System->SyncLODDistances();
	return Emitter;
}

void FParticleSystemEditor::CreatePreviewWorld()
{
	World = UWorld::CreateWorld(EWorldType::Editor, false);
	if (!World) return;
	// 다른 뷰어(스켈레탈 메시 뷰어, 에셋 에디터)와 같은 라이트. 메시 파티클은 조명을 받으므로 라이트가 없으면 어둡게 보인다.
	if (ASkyLightActor* SkyLight = World->SpawnActor<ASkyLightActor>())
	{
		SkyLight->GetBillboardComponent()->SetVisibility(false);
		SkyLight->GetLightComponent()->SetIntensity(0.3f);
	}
	if (ADirectionalLightActor* DirLight = World->SpawnActor<ADirectionalLightActor>())
	{
		DirLight->GetBillboardComponent()->SetVisibility(false);
		DirLight->GetArrowComponent()->SetVisibility(false);
		DirLight->GetLightComponent()->SetIntensity(1.0f);
		DirLight->GetLightComponent()->SetRelativeRotation(FRotator(40.0f, 160.0f, 0.0f));
	}
	if (AActor* Actor = World->SpawnActor<AActor>())
	{
		Component = Cast<UParticleSystemComponent>(Actor->AddComponent(UParticleSystemComponent::StaticClass(), "ParticlePreview"));
		if (Component)
		{
			Actor->SetRootComponent(Component);
			Component->SetTemplate(System);
			SyncPreviewLOD();
			Component->Activate(true);
		}
	}
	Client.SetWorld(World);
	Client.SetViewportType(ELevelViewportType::Perspective);
	ViewSize = ImVec2(800.0f, 600.0f);
	Client.Resize(800, 600);
	ResetCamera();
}

void FParticleSystemEditor::SyncPreviewLOD()
{
	if (!Component) return;
	const int32 MaxLOD = (System && System->GetNumLODLevels() > 0) ? (System->GetNumLODLevels() - 1) : 0;
	CurrentLOD = std::clamp(CurrentLOD, 0, MaxLOD);
	Component->bOverrideLODMethod = true;
	Component->LODMethod = bPreviewLODByDistance ? EParticleSystemLODMethod::Automatic : EParticleSystemLODMethod::DirectSet;
	if (!bPreviewLODByDistance)
	{
		Component->SetLODLevel(CurrentLOD);
	}
}

void FParticleSystemEditor::SetCurrentLOD(int32 NewLOD)
{
	const int32 MaxLOD = (System && System->GetNumLODLevels() > 0) ? (System->GetNumLODLevels() - 1) : 0;
	const int32 ClampedLOD = std::clamp(NewLOD, 0, MaxLOD);

	// 모듈이 선택돼 있으면 바뀐 LOD의 같은 슬롯 모듈로 선택을 옮긴다
	if (UParticleModule* SelectedModule = Cast<UParticleModule>(Selected))
	{
		if (UParticleEmitter* Owner = FindModuleEmitter(SelectedModule))
		{
			UParticleLODLevel* DestLOD = Owner->GetLODLevel(ClampedLOD);
			for (UParticleLODLevel* SrcLOD : Owner->LODLevels)
			{
				if (!SrcLOD || !DestLOD) continue;
				if (SelectedModule == SrcLOD->RequiredModule) { Selected = DestLOD->RequiredModule; break; }
				if (SelectedModule == SrcLOD->SpawnModule) { Selected = DestLOD->SpawnModule; break; }
				if (SelectedModule == SrcLOD->TypeDataModule) { Selected = DestLOD->TypeDataModule; break; }
				const int32 ModIdx = SrcLOD->Modules.Find(SelectedModule);
				if (ModIdx != INDEX_NONE && DestLOD->Modules.IsValidIndex(ModIdx))
				{
					Selected = DestLOD->Modules[ModIdx];
					break;
				}
			}
		}
	}

	CurrentLOD = ClampedLOD;
	bPreviewLODByDistance = false;
	SyncPreviewLOD();
}

void FParticleSystemEditor::RestartSimulation()
{
	if (Component)
	{
		SyncPreviewLOD();
		Component->ResetSystem();
	}
}

void FParticleSystemEditor::ResetCamera()
{
	Client.SetViewRotation(FRotator(-15.0f, 180.0f, 0.0f));
	Client.SetViewLocation(FVector(0.0f, 0.0f, 0.5f) - Client.GetViewForward() * 4.0f);
}

void FParticleSystemEditor::FocusOnParticles()
{
	// 지금 살아 있는 파티클의 월드 경계가 화면에 들어오는 위치로 카메라를 옮긴다 (스켈레탈 메시 뷰어의 F와 같은 FocusOnBounds 사용)
	if (Component && Component->GetActiveParticleCount() > 0)
	{
		const FBox Bounds = Component->CalcBounds();
		const FVector Center = (Bounds.Min + Bounds.Max) * 0.5f;
		Client.FocusOnBounds(Center, std::max(0.25f, (Bounds.Max - Bounds.Min).Length() * 0.5f));
		return;
	}
	// 파티클이 아직 없으면 기본 카메라가 보는 원점 주변으로 간다
	Client.FocusOnBounds(FVector(0.0f, 0.0f, 0.5f), 1.0f);
}

void FParticleSystemEditor::Tick(float DeltaTime)
{
	if (!World) return;
	if (bPreviewLODByDistance && Component)
	{
		Component->bOverrideLODMethod = true;
		Component->LODMethod = EParticleSystemLODMethod::Automatic;
	}
	// development의 에디터 월드가 파티클을 갱신하므로 컴포넌트를 다시 Tick하지 않는다.
	World->Tick(bPaused ? 0.0f : DeltaTime * SimSpeeds[SpeedIndex]);
	if (bPreviewLODByDistance && Component)
	{
		CurrentLOD = Component->GetLODLevel();
	}
	if (ViewSize.x >= 1.0f && ViewSize.y >= 1.0f)
		Client.Resize(static_cast<uint32>(ViewSize.x), static_cast<uint32>(ViewSize.y));
}

void FParticleSystemEditor::RenderPreview(UEditorEngine* Engine)
{
	if (!Engine || !World || !IsOpen() || !bViewportVisible) return;
	Engine->RenderPreviewViewport(&Client, World, nullptr, nullptr, bShowGrid, bShowBounds ? Component : nullptr, bShowSkybox);
}

void FParticleSystemEditor::OnHidden()
{
	bViewportVisible = false;
}

// ---------------------------------------------------------------- 툴바

void FParticleSystemEditor::BuildToolbarItems()
{
	// 기능을 하나씩 늘릴 때 여기에 항목을 추가한다. 창이 좁으면 뒤쪽부터 ☰ 드롭다운으로 들어간다.
	// 아직 구현하지 않은 버튼은 흐리게(비활성) 두고, 기능을 붙일 때 OnClick을 채우고 IsEnabled를 지운다.
	ToolbarItems.Empty();
	const auto NotYet = [] { return false; };
	const auto Add = [this](const char* Label, const char* Tooltip, FToolbarIconFn Icon, std::function<void()> OnClick,
		std::function<bool()> IsEnabled = nullptr, bool bSeparatorBefore = false) -> FToolbarItem&
	{
		FToolbarItem Item;
		Item.Label = Label;
		Item.Tooltip = Tooltip;
		Item.Icon = std::move(Icon);
		Item.OnClick = std::move(OnClick);
		Item.IsEnabled = std::move(IsEnabled);
		Item.bSeparatorBefore = bSeparatorBefore;
		ToolbarItems.Add(std::move(Item));
		return ToolbarItems[ToolbarItems.Num() - 1];
	};

	// 에셋
	Add("Save", "Save this particle system (Ctrl+S).", ToolbarIcons::Save, [this] { Save(); }).bIconOnly = true;
	Add("Browse", "Browse to and select this asset in the Content Drawer, opening it if needed. (Ctrl+B)", ToolbarIcons::Browse,
		[this] { BrowseToAsset(); }, [this] { return CanBrowseToAsset(); }).bIconOnly = true;

	// 시뮬레이션
	Add("Restart Sim", "Restart the simulation of the particle system.", ToolbarIcons::RestartSim,
		[this] { RestartSimulation(); }, nullptr, true);
	Add("Restart Level", "Restart all instances of this particle system in the level. (Not implemented)", ToolbarIcons::RestartLevel,
		nullptr, NotYet);

	// 편집
	Add("Undo", "Undo the last edit. (Not implemented)", ToolbarIcons::Undo, nullptr, NotYet, true);
	Add("Redo", "Redo the last undone edit. (Not implemented)", ToolbarIcons::Redo, nullptr, NotYet);

	// 썸네일
	Add("Thumbnail", "Generate a thumbnail from the current viewport. (Not implemented)", ToolbarIcons::Thumbnail, nullptr, NotYet, true);

	// 뷰포트 표시
	FToolbarItem& Bounds = Add("Bounds", "Display the current bounds of the particle system.", ToolbarIcons::Bounds,
		[this] { bShowBounds = !bShowBounds; }, nullptr, true);
	Bounds.IsChecked = [this] { return bShowBounds; };
	Bounds.DrawOptionsMenu = [this]
	{
		ImGui::MenuItem("Show Bounds", nullptr, &bShowBounds);
		ImGui::MenuItem("Set Fixed Bounds", nullptr, false, false);
	};
	FToolbarItem& Axis = Add("Origin Axis", "Display the origin axis in the viewport.", ToolbarIcons::OriginAxis,
		[this] { bShowOriginAxis = !bShowOriginAxis; });
	Axis.IsChecked = [this] { return bShowOriginAxis; };
	Add("Background Color", "Change the viewport background color. (Not implemented)", ToolbarIcons::BackgroundColor, nullptr, NotYet);

	// LOD (UE Cascade 기준: Highest LOD = LOD 0(최고 품질), Lowest LOD = 마지막 인덱스(최저 품질))
	Add("Regen LOD", "Regenerate the lowest LOD from the highest LOD.", ToolbarIcons::RegenLODFromHigher,
		[this]
		{
			if (!System || System->GetNumLODLevels() <= 1) return;
			const int32 TargetIdx = System->GetNumLODLevels() - 1;
			if (UParticleModule* Mod = Cast<UParticleModule>(Selected))
				if (UParticleEmitter* Owner = FindModuleEmitter(Mod)) Selected = Owner;
			ContextModule = nullptr;
			RemoveLODCurveEntries(TargetIdx);
			if (Component) Component->ResetSystem();
			if (System->RegenerateLODLevel(TargetIdx, 0, 0.5f))
			{
				SetCurrentLOD(TargetIdx);
				MarkDirty();
				RestartSimulation();
			}
		},
		[this] { return System && System->GetNumLODLevels() > 1; }, true);
	Add("Regen LOD", "Regenerate this LOD from the next higher LOD.", ToolbarIcons::RegenLODFromLower,
		[this]
		{
			if (!System || CurrentLOD <= 0 || CurrentLOD >= System->GetNumLODLevels()) return;
			if (UParticleModule* Mod = Cast<UParticleModule>(Selected))
				if (UParticleEmitter* Owner = FindModuleEmitter(Mod)) Selected = Owner;
			ContextModule = nullptr;
			RemoveLODCurveEntries(CurrentLOD);
			if (Component) Component->ResetSystem();
			if (System->RegenerateLODLevel(CurrentLOD, CurrentLOD - 1, 0.5f))
			{
				SetCurrentLOD(CurrentLOD);
				MarkDirty();
				RestartSimulation();
			}
		},
		[this] { return System && CurrentLOD > 0 && CurrentLOD < System->GetNumLODLevels(); });
	Add("Lowest LOD", "Jump to the lowest LOD.", ToolbarIcons::LowestLOD,
		[this] { if (System) SetCurrentLOD(System->GetNumLODLevels() - 1); },
		[this] { return System && CurrentLOD + 1 < System->GetNumLODLevels(); });
	Add("Lower LOD", "Move to the next lower LOD.", ToolbarIcons::LowerLOD,
		[this] { SetCurrentLOD(CurrentLOD + 1); },
		[this] { return System && CurrentLOD + 1 < System->GetNumLODLevels(); });
	Add("Add LOD", "Add a LOD before the current LOD.", ToolbarIcons::AddLODBefore,
		[this]
		{
			if (!System) return;
			if (Component) Component->ResetSystem();
			const int32 Added = System->AddLODLevel(CurrentLOD, 1.0f);
			if (Added != INDEX_NONE)
			{
				SetCurrentLOD(Added);
				MarkDirty();
				RestartSimulation();
			}
		},
		[this] { return System && System->GetNumLODLevels() > 0 && System->GetNumLODLevels() < 8; });

	FToolbarItem LODField;
	LODField.Label = "LOD";
	LODField.CustomWidth = [] { return ImGui::CalcTextSize("LOD:").x + EditorUIScale::Px(40.0f); };
	LODField.DrawCustom = [this]
	{
		// 툴바에서는 "LOD: [0]", 드롭다운에서는 한 줄 글자로 그린다.
		const float Height = EditorToolbar::GetHeight();
		const int32 TotalLODs = System ? std::max(1, System->GetNumLODLevels()) : 1;
		if (ImGui::GetCurrentWindow()->Flags & ImGuiWindowFlags_Popup)
		{
			ImGui::TextDisabled("LOD: %d / %d", CurrentLOD, TotalLODs - 1);
			return;
		}
		const ImVec2 Min = ImGui::GetCursorScreenPos();
		ImDrawList* DL = ImGui::GetWindowDrawList();
		const ImVec2 LabelSize = ImGui::CalcTextSize("LOD:");
		DL->AddText(ImVec2(Min.x + EditorUIScale::Px(6.0f), Min.y + (Height - LabelSize.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), "LOD:");
		const float BoxX = Min.x + EditorUIScale::Px(10.0f) + LabelSize.x;
		const float BoxW = EditorUIScale::Px(24.0f);
		const float BoxH = Height - EditorUIScale::Px(6.0f);
		const ImVec2 BoxMin(BoxX, Min.y + (Height - BoxH) * 0.5f);
		DL->AddRectFilled(BoxMin, ImVec2(BoxMin.x + BoxW, BoxMin.y + BoxH), ImGui::GetColorU32(ImGuiCol_FrameBg), EditorUIScale::Px(3.0f));
		DL->AddRect(BoxMin, ImVec2(BoxMin.x + BoxW, BoxMin.y + BoxH), ImGui::GetColorU32(ImGuiCol_Border), EditorUIScale::Px(3.0f));
		const FString Value = std::to_string(CurrentLOD);
		const ImVec2 ValueSize = ImGui::CalcTextSize(Value.c_str());
		DL->AddText(ImVec2(BoxMin.x + (BoxW - ValueSize.x) * 0.5f, BoxMin.y + (BoxH - ValueSize.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), Value.c_str());
		ImGui::Dummy(ImVec2(ImGui::CalcTextSize("LOD:").x + EditorUIScale::Px(40.0f), Height));
		if (ImGui::IsItemHovered())
		{
			const float Dist = System ? System->GetLODDistance(CurrentLOD) : 0.0f;
			ImGui::SetTooltip("Current LOD: %d (of %d, Distance >= %.2fm)", CurrentLOD, TotalLODs, Dist);
		}
	};
	ToolbarItems.Add(LODField);

	Add("Add LOD", "Add a LOD after the current LOD.", ToolbarIcons::AddLODAfter,
		[this]
		{
			if (!System) return;
			if (Component) Component->ResetSystem();
			const int32 Added = System->AddLODLevel(CurrentLOD + 1, 1.0f);
			if (Added != INDEX_NONE)
			{
				SetCurrentLOD(Added);
				MarkDirty();
				RestartSimulation();
			}
		},
		[this] { return System && System->GetNumLODLevels() > 0 && System->GetNumLODLevels() < 8; });
	Add("Higher LOD", "Move to the next higher LOD.", ToolbarIcons::HigherLOD,
		[this] { SetCurrentLOD(CurrentLOD - 1); },
		[this] { return System && CurrentLOD > 0; });
	Add("Highest LOD", "Jump to the highest LOD.", ToolbarIcons::HighestLOD,
		[this] { SetCurrentLOD(0); },
		[this] { return System && CurrentLOD > 0; });
	Add("Delete LOD", "Delete the current LOD.", ToolbarIcons::DeleteLOD,
		[this]
		{
			if (!System || System->GetNumLODLevels() <= 1) return;
			if (UParticleModule* Mod = Cast<UParticleModule>(Selected))
				if (UParticleEmitter* Owner = FindModuleEmitter(Mod)) Selected = Owner;
			ContextModule = nullptr;
			RemoveLODCurveEntries(CurrentLOD);
			if (Component) Component->ResetSystem();
			if (System->RemoveLODLevel(CurrentLOD))
			{
				SetCurrentLOD(std::clamp(CurrentLOD, 0, System->GetNumLODLevels() - 1));
				MarkDirty();
				RestartSimulation();
			}
		},
		[this] { return System && System->GetNumLODLevels() > 1; });
}

// ---------------------------------------------------------------- 메인 메뉴바 (이 탭이 활성일 때 레벨 메뉴 대신)

void FParticleSystemEditor::OpenSaveAs()
{
	PrepareSaveAs();
	bOpenSaveAs = true;
}

void FParticleSystemEditor::BrowseToAsset()
{
	if (!CanBrowseToAsset()) return;
	// 저장하지 않은 문서는 저장될 기본 폴더를 연다 (폴더가 없으면 Content Drawer가 Assets를 연다)
	OnBrowseAsset(AssetPath.empty() ? FString(DefaultParticleFolder) : AssetPath);
}

int32 FParticleSystemEditor::GetMainMenuFocusFrame() const
{
	return IsOpen() && bTabVisible ? LastFocusFrame : -1;
}

void FParticleSystemEditor::DrawMainMenus()
{
	if (ImGui::BeginMenu("File"))
	{
		if (ImGui::MenuItem("Save", "Ctrl+S")) Save();
		if (ImGui::MenuItem("Save As...")) OpenSaveAs();
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Asset"))
	{
		if (ImGui::MenuItem("Find in Content Drawer", "Ctrl+B", false, CanBrowseToAsset())) BrowseToAsset();
		ImGui::EndMenu();
	}
}

void FParticleSystemEditor::DrawMainWindowMenuItems()
{
	ImGui::SeparatorText(Name.c_str());
	static const char* Labels[] = { "Viewport", "Emitters", "Details", "Curve Editor" };
	for (int32 Index = 0; Index < PanelCount; ++Index)
		ImGui::MenuItem(Labels[Index], nullptr, &PanelOpen[Index]);
	if (ImGui::MenuItem("Reset Layout"))
	{
		bResetLayout = true;
		for (bool& bOpen : PanelOpen) bOpen = true;
	}
}

// ---------------------------------------------------------------- 탭 / 도킹

void FParticleSystemEditor::OnRender()
{
	if (FocusFramesLeft > 0) ImGui::SetNextWindowFocus();
	ImGuiWindowFlags WindowFlags = ImGuiWindowFlags_None;
	// 탭에 저장하지 않은 변경 표시(점)를 띄운다
	if (bDirty) WindowFlags |= ImGuiWindowFlags_UnsavedDocument;
	const bool bActive = BeginPanel(WindowFlags);
	// 탭의 x를 눌렀는데 저장하지 않은 변경이 있으면 바로 닫지 않고 확인 창을 띄운다
	if (!IsOpen() && bDirty)
	{
		SetOpen(true);
		bCloseRequested = true;
	}
	// 탭 안에 도킹된 패널까지 포함해 이 문서에 포커스가 있는지
	bFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows | ImGuiFocusedFlags_DockHierarchy);
	bTabVisible = bActive;
	if (bFocused) LastFocusFrame = ImGui::GetFrameCount();
	const ImGuiIO& IO = ImGui::GetIO();
	if (bFocused && IO.KeyCtrl && !IO.KeyShift && !IO.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_S, false))
		Save();
	// UE의 Browse 단축키 (Content Browser에서 이 에셋 찾기)
	if (bFocused && IO.KeyCtrl && !IO.KeyShift && !IO.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_B, false))
		BrowseToAsset();
	if (FocusFramesLeft > 0)
	{
		ImGuiWindow* Window = ImGui::GetCurrentWindow();
		// 도킹되지 않은 창이거나 탭이 이미 선택되어 보이면 끝낸다
		if (!Window->DockIsActive || Window->DockTabIsVisible)
		{
			FocusFramesLeft = 0;
		}
		else
		{
			if (ImGuiTabBar* TabBar = Window->DockNode ? Window->DockNode->TabBar : nullptr)
				if (ImGuiTabItem* Tab = ImGui::TabBarFindTabByID(TabBar, Window->TabId))
					ImGui::TabBarQueueFocus(TabBar, Tab);
			--FocusFramesLeft;
		}
	}
	if (!IsOpen())
	{
		OnHidden();
		ImGui::End();
		return;
	}
	if (bActive)
	{
		ImGui::Dummy(ImVec2(0.0f, EditorUIScale::Px(2.0f)));
		EditorToolbar::Draw("ParticleToolbar", ToolbarItems);
		ImGui::Dummy(ImVec2(0.0f, EditorUIScale::Px(2.0f)));
	}

	if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable)
	{
		const ImVec2 Size = ImGui::GetContentRegionAvail();
		if (bActive && Size.x > 0.0f && Size.y > 0.0f &&
			(bResetLayout || !ImGui::DockBuilderGetNode(DockSpaceId)))
		{
			ImGui::DockBuilderRemoveNode(DockSpaceId);
			BuildDefaultLayout(Size);
			bResetLayout = false;
		}
		ImGuiWindowClass Class;
		Class.ClassId = PanelClassId;
		Class.DockingAllowUnclassed = false;
		if (bActive || ImGui::DockBuilderGetNode(DockSpaceId))
			ImGui::DockSpace(DockSpaceId, ImVec2(0, 0), bActive ? ImGuiDockNodeFlags_None : ImGuiDockNodeFlags_KeepAliveOnly, &Class);
	}
	ImGui::End();

	bViewportVisible = false;
	DrawDockedPanels(bActive);
	DrawSavePopups();
}

void FParticleSystemEditor::DrawSavePopups()
{
	// 탭마다 팝업 ID가 겹치지 않게 문서 키로 감싼다
	ImGui::PushID(WindowKey.c_str());
	if (bCloseRequested)
	{
		ImGui::OpenPopup("Save Changes");
		bCloseRequested = false;
	}
	if (bOpenSaveAs)
	{
		ImGui::OpenPopup("Save Particle System As");
		bOpenSaveAs = false;
	}

	if (ImGui::BeginPopupModal("Save Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("Save changes to \"%s\" before closing?", Name.c_str());
		ImGui::Spacing();
		if (ImGui::Button("Save"))
		{
			ImGui::CloseCurrentPopup();
			if (AssetPath.empty())
			{
				// 경로가 없으면 Save As 창으로 이어서, 저장에 성공하면 닫는다
				bCloseAfterSave = true;
				Save();
			}
			else if (Save())
			{
				SetOpen(false);
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Don't Save"))
		{
			ImGui::CloseCurrentPopup();
			bDirty = false;
			SetOpen(false);
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	if (ImGui::BeginPopupModal("Save Particle System As", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		// 이름만 입력하면 된다. 폴더는 Assets 아래 폴더 중에서 고르고, 확장자는 자동으로 붙는다 (UE의 Save Asset As처럼).
		const float FieldWidth = EditorUIScale::Px(320.0f);
		ImGui::SetNextItemWidth(FieldWidth);
		if (ImGui::BeginCombo("Folder", SaveAsFolder))
		{
			std::error_code ListError;
			const auto FolderItem = [&](const FString& Folder)
			{
				const bool bSelected = Folder == SaveAsFolder;
				if (ImGui::Selectable(Folder.c_str(), bSelected)) strncpy_s(SaveAsFolder, Folder.c_str(), _TRUNCATE);
				if (bSelected) ImGui::SetItemDefaultFocus();
			};
			FolderItem("Assets");
			// 기본 폴더가 아직 없어도 고를 수 있다 (저장할 때 만들어진다)
			if (!fs::is_directory(DefaultParticleFolder, ListError)) FolderItem(DefaultParticleFolder);
			for (fs::recursive_directory_iterator It("Assets", fs::directory_options::skip_permission_denied, ListError), End;
				!ListError && It != End; It.increment(ListError))
			{
				if (!It->is_directory(ListError)) continue;
				FString Folder = FbxPath::ToString(It->path());
				std::replace(Folder.begin(), Folder.end(), '\\', '/');
				FolderItem(Folder);
			}
			ImGui::EndCombo();
		}

		if (bFocusSaveAsName)
		{
			ImGui::SetKeyboardFocusHere();
			bFocusSaveAsName = false;
		}
		ImGui::SetNextItemWidth(FieldWidth);
		// Enter로 바로 저장한다
		const bool bEnter = ImGui::InputText("Name", SaveAsName, sizeof(SaveAsName), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);

		// 선택한 폴더의 기존 파티클 에셋. 누르면 그 이름이 입력되고, 저장하면 그 파일을 덮어쓴다 (UE Save Asset As의 에셋 목록)
		{
			FString ListFolder = SaveAsFolder;
			std::replace(ListFolder.begin(), ListFolder.end(), '\\', '/');
			std::error_code ListError;
			TArray<FString> Existing;
			for (fs::directory_iterator It(FbxPath::FromString(ListFolder), fs::directory_options::skip_permission_denied, ListError), End;
				!ListError && It != End; It.increment(ListError))
			{
				if (It->is_regular_file(ListError) && It->path().extension() == ".particle")
					Existing.Add(FbxPath::ToString(It->path().stem()));
			}
			if (Existing.Num() > 0)
			{
				ImGui::TextDisabled("Existing assets in this folder (click to replace):");
				const float ListHeight = std::min(static_cast<float>(Existing.Num()), 6.0f) * ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().WindowPadding.y * 2.0f;
				ImGui::BeginChild("##ExistingAssets", ImVec2(FieldWidth, ListHeight), ImGuiChildFlags_Borders);
				for (const FString& Asset : Existing)
				{
					if (ImGui::Selectable(Asset.c_str(), Asset == SaveAsName)) strncpy_s(SaveAsName, Asset.c_str(), _TRUNCATE);
				}
				ImGui::EndChild();
			}
		}

		FString FileName = SaveAsName;
		// 사용자가 확장자까지 입력했으면 떼고 다시 붙인다
		if (FileName.size() > 9 && FileName.compare(FileName.size() - 9, 9, ".particle") == 0) FileName.resize(FileName.size() - 9);
		while (!FileName.empty() && FileName.back() == ' ') FileName.pop_back();
		FString Folder = SaveAsFolder;
		std::replace(Folder.begin(), Folder.end(), '\\', '/');
		while (!Folder.empty() && Folder.back() == '/') Folder.pop_back();
		const FString Path = Folder + "/" + FileName + ".particle";

		const bool bInAssets = Folder == "Assets" || Folder.rfind("Assets/", 0) == 0;
		const bool bValidName = !FileName.empty() && FileName.find_first_of("\\/:*?\"<>|") == FString::npos;
		std::error_code Ec;
		const bool bExists = Path != AssetPath && fs::exists(FbxPath::FromString(Path), Ec);
		ImGui::TextDisabled("%s", Path.c_str());
		if (!bInAssets) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "The folder must be inside Assets.");
		else if (!bValidName) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Enter a valid asset name.");
		// 이미 있는 파일이면 막지 않고 덮어쓴다고 알린다 (버튼도 Replace로 바뀐다)
		else if (bExists) ImGui::TextColored(ImVec4(1, 0.75f, 0.3f, 1), "An asset with this name exists. Saving will replace it.");

		const bool bCanSave = bInAssets && bValidName;
		ImGui::BeginDisabled(!bCanSave);
		if (ImGui::Button(bExists ? "Replace" : "Save") || (bEnter && bCanSave))
		{
			if (SaveAs(Path))
			{
				ImGui::CloseCurrentPopup();
				if (bCloseAfterSave) SetOpen(false);
				bCloseAfterSave = false;
			}
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
		{
			bCloseAfterSave = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
	ImGui::PopID();
}

// Cascade 기본 배치: 왼쪽 위 Viewport, 왼쪽 아래 Details, 오른쪽 위 Emitters, 오른쪽 아래 Curve Editor
void FParticleSystemEditor::BuildDefaultLayout(const ImVec2& Size)
{
	ImGui::DockBuilderAddNode(DockSpaceId, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(DockSpaceId, Size);
	ImGuiID Right = DockSpaceId;
	ImGuiID Left = ImGui::DockBuilderSplitNode(Right, ImGuiDir_Left, 0.3f, nullptr, &Right);
	const ImGuiID LeftBottom = ImGui::DockBuilderSplitNode(Left, ImGuiDir_Down, 0.45f, nullptr, &Left);
	const ImGuiID RightBottom = ImGui::DockBuilderSplitNode(Right, ImGuiDir_Down, 0.35f, nullptr, &Right);
	ImGui::DockBuilderDockWindow(PanelNames[Viewport].c_str(), Left);
	ImGui::DockBuilderDockWindow(PanelNames[Details].c_str(), LeftBottom);
	ImGui::DockBuilderDockWindow(PanelNames[Emitters].c_str(), Right);
	ImGui::DockBuilderDockWindow(PanelNames[CurveEditor].c_str(), RightBottom);
	ImGui::DockBuilderFinish(DockSpaceId);
}

bool FParticleSystemEditor::IsPanelDetached(int32 Index) const
{
	const ImGuiWindow* Window = ImGui::FindWindowByName(PanelNames[Index].c_str());
	return Window && (!Window->DockNode || ImGui::DockNodeGetRootNode(Window->DockNode)->ID != DockSpaceId);
}

void FParticleSystemEditor::DrawDockedPanels(bool bActive)
{
	for (int32 Index = 0; Index < PanelCount; ++Index)
	{
		if (!PanelOpen[Index] || (!bActive && !IsPanelDetached(Index))) continue;
		ImGuiWindowClass Class;
		Class.ClassId = PanelClassId;
		Class.DockingAllowUnclassed = false;
		ImGui::SetNextWindowClass(&Class);
		ImGui::SetNextWindowSize(ImVec2(EditorUIScale::Px(480), EditorUIScale::Px(320)), ImGuiCond_FirstUseEver);
		const bool bNoPadding = Index == Viewport;
		if (bNoPadding) ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		const ImGuiWindowFlags Flags = Index == Viewport ? ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse : 0;
		const bool bVisible = ImGui::Begin(PanelNames[Index].c_str(), &PanelOpen[Index], Flags);
		if (bNoPadding) ImGui::PopStyleVar();
		if (bVisible)
		{
			switch (Index)
			{
			case Viewport: DrawViewport(); break;
			case Emitters: DrawEmitters(); break;
			case Details: DrawDetails(); break;
			case CurveEditor: DrawCurveEditor(); break;
			}
		}
		ImGui::End();
	}
}

// ---------------------------------------------------------------- 패널 내용

void FParticleSystemEditor::DrawViewport()
{
	const ImVec2 Size = ImGui::GetContentRegionAvail();
	if (Size.x < 1.0f || Size.y < 1.0f) return;
	bViewportVisible = true;
	// 실제 Resize는 다음 Tick에서 한다 (이번 프레임에 그린 SRV가 바뀌지 않게).
	ViewSize = Size;

	const ImVec2 Min = ImGui::GetCursorScreenPos();
	const ImVec2 Max(Min.x + Size.x, Min.y + Size.y);
	// 위에 "View / Time" 버튼을 겹쳐 올리므로 겹침을 허용한다.
	ImGui::SetNextItemAllowOverlap();
	ImGui::InvisibleButton("##ParticleViewport", Size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
	ImDrawList* DL = ImGui::GetWindowDrawList();
	if (FTexture2D* Color = Client.GetColorTarget())
		DL->AddImage(Color->GetSRV(), Min, Max);

	if (ImGui::IsItemHovered() || ImGui::IsItemActive() || Client.IsFraming())
	{
		const bool bCaptured = ImGui::IsMouseDown(ImGuiMouseButton_Right);
		float Wheel = ImGui::GetIO().MouseWheel;
		// 우클릭으로 카메라를 잡은 동안 휠은 이동 속도만 바꾸고 카메라를 앞뒤로 움직이지 않는다 (다른 메시 뷰어와 같다).
		if (bCaptured && Wheel != 0.0f)
		{
			CameraSpeed = std::clamp(CameraSpeed + Wheel * 0.5f, 0.1f, 20.0f);
			Wheel = 0.0f;
		}
		Client.TickInputFromImGui(ImGui::GetIO().DeltaTime, bCaptured, true,
			static_cast<int32>(Wheel * 120.0f), 2.5f * CameraSpeed, 0.2f);
	}

	// F: 파티클 전체가 보이게 카메라 이동. 우클릭으로 카메라를 잡은 동안과 글자 입력 중에는 받지 않는다.
	if ((ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) || ImGui::IsItemHovered()) &&
		!ImGui::GetIO().WantTextInput && !ImGui::IsMouseDown(ImGuiMouseButton_Right) &&
		ImGui::IsKeyPressed(ImGuiKey_F, false))
	{
		FocusOnParticles();
	}

	// 오른쪽 아래: 활성 파티클 수 / 최대 (언리얼 뷰포트의 "20 / 23" 표시). LOD가 여러 개면 현재 LOD와 거리도 함께 표시한다.
	int32 Active = 0, MaxCount = 0;
	if (Component)
		for (int32 Index = 0; Index < Component->GetNumEmitterInstances(); ++Index)
			if (const auto* Instance = Component->GetEmitterInstance(Index))
			{ Active += Instance->ActiveParticles; MaxCount += Instance->MaxActiveParticles; }
	const float CamDist = Component ? (Component->GetWorldLocation() - Client.GetViewLocation()).Length() : 0.0f;
	const FString Stat = (System && System->GetNumLODLevels() > 1)
		? std::format("LOD {} ({:.1f}m) | {} / {}", CurrentLOD, CamDist, Active, MaxCount)
		: std::format("{} / {}", Active, MaxCount);
	const ImVec2 TextSize = ImGui::CalcTextSize(Stat.c_str());
	const float Pad = EditorUIScale::Px(6.0f);
	DL->AddText(ImVec2(Max.x - TextSize.x - Pad, Max.y - TextSize.y - Pad), IM_COL32(90, 170, 255, 255), Stat.c_str());
	if (bShowOriginAxis) DrawOriginAxis(Min, Size);
	if (bPaused)
		DL->AddText(ImVec2(Min.x + Pad, Max.y - TextSize.y - Pad), IM_COL32(255, 200, 60, 255), "Paused");
	DrawViewportMenus(Min);
}

// 뷰포트 왼쪽 위의 "View" / "Time" 알약 버튼 (언리얼 Cascade 뷰포트와 같은 자리)
void FParticleSystemEditor::DrawViewportMenus(const ImVec2& ViewMin)
{
	const float Pad = EditorUIScale::Px(6.0f);
	ImGui::SetCursorScreenPos(ImVec2(ViewMin.x + Pad, ViewMin.y + Pad));
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, EditorUIScale::Px(10.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(EditorUIScale::Px(9.0f), EditorUIScale::Px(3.0f)));
	ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(60, 60, 60, 220));
	if (ImGui::Button("View")) ImGui::OpenPopup("##ViewMenu");
	ImGui::SameLine(0.0f, EditorUIScale::Px(4.0f));
	if (ImGui::Button("Time")) ImGui::OpenPopup("##TimeMenu");
	ImGui::PopStyleColor();
	ImGui::PopStyleVar(2);

	if (ImGui::BeginPopup("##ViewMenu"))
	{
		ImGui::MenuItem("Grid", nullptr, &bShowGrid);
		ImGui::MenuItem("Skybox", nullptr, &bShowSkybox);
		ImGui::MenuItem("Bounds", nullptr, &bShowBounds);
		ImGui::MenuItem("Origin Axis", nullptr, &bShowOriginAxis);
		if (ImGui::MenuItem("LOD Distance Check", nullptr, &bPreviewLODByDistance))
		{
			SyncPreviewLOD();
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Reset Camera")) ResetCamera();
		if (ImGui::MenuItem("Focus on Particles", "F")) FocusOnParticles();
		ImGui::EndPopup();
	}
	if (ImGui::BeginPopup("##TimeMenu"))
	{
		if (ImGui::MenuItem("Play", nullptr, !bPaused)) bPaused = false;
		if (ImGui::MenuItem("Pause", nullptr, bPaused)) bPaused = true;
		ImGui::Separator();
		for (int32 Index = 0; Index < SimSpeedCount; ++Index)
			if (ImGui::MenuItem(SimSpeedLabels[Index], nullptr, SpeedIndex == Index)) SpeedIndex = Index;
		ImGui::EndPopup();
	}
}

// 월드 원점에 X(빨강) / Y(초록) / Z(파랑) 축을 그린다.
void FParticleSystemEditor::DrawOriginAxis(const ImVec2& ViewMin, const ImVec2& InViewSize)
{
	const FMatrix ViewProj = Client.GetViewProjectionMatrix();
	const int W = static_cast<int>(InViewSize.x), H = static_cast<int>(InViewSize.y);
	const FVector2 Origin = WorldToScreen(FVector(0.0f, 0.0f, 0.0f), ViewProj, W, H);
	if (Origin.X == -FLT_MAX) return;
	ImDrawList* DL = ImGui::GetWindowDrawList();
	const struct { FVector Dir; ImU32 Color; } Axes[] = {
		{ FVector(1.0f, 0.0f, 0.0f), IM_COL32(230, 70, 70, 255) },
		{ FVector(0.0f, 1.0f, 0.0f), IM_COL32(90, 200, 90, 255) },
		{ FVector(0.0f, 0.0f, 1.0f), IM_COL32(80, 130, 240, 255) },
	};
	for (const auto& Axis : Axes)
	{
		const FVector2 End = WorldToScreen(Axis.Dir, ViewProj, W, H);
		if (End.X == -FLT_MAX) continue;
		DL->AddLine(ImVec2(ViewMin.x + Origin.X, ViewMin.y + Origin.Y), ImVec2(ViewMin.x + End.X, ViewMin.y + End.Y),
			Axis.Color, EditorUIScale::Px(2.0f));
	}
}

void FParticleSystemEditor::DrawEmitters()
{
	if (!System) return;
	const float ColumnWidth = EditorUIScale::Px(180.0f);
	const float Gap = EditorUIScale::Px(9.0f);
	ImDrawList* DL = ImGui::GetWindowDrawList();
	const float PanelBottom = ImGui::GetWindowPos().y + ImGui::GetWindowHeight();
	for (int32 Index = 0; Index < System->Emitters.Num(); ++Index)
	{
		if (Index > 0) ImGui::SameLine(0.0f, Gap);
		ImGui::PushID(Index);
		ImGui::BeginGroup();
		DrawEmitterColumn(Index, System->Emitters[Index], ColumnWidth);
		ImGui::EndGroup();
		ImGui::PopID();

		// 이미터 열 오른쪽 구분선 (UE Cascade처럼 열마다 세로선으로 나눈다)
		const float LineX = ImGui::GetItemRectMax().x + Gap * 0.5f;
		DL->AddLine(ImVec2(LineX, ImGui::GetItemRectMin().y), ImVec2(LineX, PanelBottom),
			ImGui::GetColorU32(ImGuiCol_Separator), EditorUIScale::Px(1.0f));
	}

	// 이미터 열 밖의 빈 곳: 좌클릭은 파티클 시스템 선택, 우클릭은 이미터 추가 메뉴
	if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered())
	{
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) Selected = nullptr;
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
		{
			Selected = nullptr;
			ImGui::OpenPopup("##SystemMenu");
		}
	}
	if (ImGui::BeginPopup("##SystemMenu"))
	{
		if (ImGui::MenuItem("New Particle Sprite Emitter"))
			PendingEdit = { FPendingEdit::AddEmitter };
		ImGui::EndPopup();
	}

	// Delete 키: 선택한 모듈이나 이미터 삭제 (입력 중인 칸이 없을 때만)
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsAnyItemActive() &&
		ImGui::IsKeyPressed(ImGuiKey_Delete, false))
	{
		if (UParticleModule* Module = Cast<UParticleModule>(Selected))
		{
			if (UParticleEmitter* Owner = FindModuleEmitter(Module))
			{
				const UParticleLODLevel* ActiveLOD = Owner->GetLODLevel(CurrentLOD) ? Owner->GetLODLevel(CurrentLOD) : Owner->GetLODLevel(0);
				if (CanRemoveModule(ActiveLOD, Module))
					PendingEdit = { FPendingEdit::RemoveModule, Owner, nullptr, Module };
			}
		}
		else if (UParticleEmitter* Emitter = Cast<UParticleEmitter>(Selected))
		{
			PendingEdit = { FPendingEdit::RemoveEmitter, Emitter };
		}
	}

	ApplyPendingEdit();
}

UParticleEmitter* FParticleSystemEditor::FindModuleEmitter(const UParticleModule* Module) const
{
	if (!System || !Module) return nullptr;
	for (UParticleEmitter* Emitter : System->Emitters)
	{
		if (!Emitter) continue;
		for (const UParticleLODLevel* LOD : Emitter->LODLevels)
		{
			if (!LOD) continue;
			if (Module == LOD->RequiredModule || Module == LOD->SpawnModule || Module == LOD->TypeDataModule ||
				LOD->Modules.Find(const_cast<UParticleModule*>(Module)) != INDEX_NONE)
				return Emitter;
		}
	}
	return nullptr;
}

// 목록을 그리는 도중에 바꾸지 않도록, 메뉴와 Delete 키는 요청만 남기고 이미터 패널을 다 그린 뒤 여기서 적용한다.
void FParticleSystemEditor::ApplyPendingEdit()
{
	const FPendingEdit Edit = PendingEdit;
	PendingEdit = {};
	if (Edit.Type == FPendingEdit::None || !System) return;
	// Refresh는 에셋 내용을 바꾸지 않는다
	if (Edit.Type != FPendingEdit::RefreshModule) MarkDirty();

	// 인스턴스가 이미터·모듈 포인터와 Payload 오프셋을 들고 있으므로 먼저 없앤다.
	// 다음 Tick에서 컴포넌트가 바뀐 구성으로 인스턴스를 다시 만든다.
	if (Component) Component->ResetSystem();

	UParticleLODLevel* LOD = Edit.Emitter
		? (Edit.Emitter->GetLODLevel(CurrentLOD) ? Edit.Emitter->GetLODLevel(CurrentLOD) : Edit.Emitter->GetLODLevel(0))
		: nullptr;
	switch (Edit.Type)
	{
	case FPendingEdit::AddModule:
		if (LOD && Edit.Emitter)
		{
			if (UParticleModule* Added = LOD->AddModule(Edit.ModuleClass))
			{
				Added->SetToSensibleDefaults(Edit.Emitter);
				Selected = Added;
				json CopyData = json::object();
				Added->Serialize(CopyData, false);
				for (UParticleLODLevel* OtherLOD : Edit.Emitter->LODLevels)
				{
					if (!OtherLOD || OtherLOD == LOD) continue;
					if (UParticleModule* OtherAdded = OtherLOD->AddModule(Edit.ModuleClass))
					{
						json CloneData = CopyData;
						OtherAdded->Serialize(CloneData, true);
					}
				}
			}
			Edit.Emitter->CacheEmitterModuleInfo();
		}
		break;
	case FPendingEdit::RemoveModule:
		if (LOD && Edit.Emitter && Edit.Module)
		{
			const bool bIsSpawnSlot = (Edit.Module == LOD->SpawnModule);
			const bool bIsTypeDataSlot = (Edit.Module == LOD->TypeDataModule);
			const int32 ModuleIndex = (!bIsSpawnSlot && !bIsTypeDataSlot) ? LOD->Modules.Find(Edit.Module) : INDEX_NONE;
			for (UParticleLODLevel* TargetLOD : Edit.Emitter->LODLevels)
			{
				if (!TargetLOD) continue;
				UParticleModule* ToRemove = nullptr;
				if (bIsSpawnSlot) ToRemove = TargetLOD->SpawnModule;
				else if (bIsTypeDataSlot) ToRemove = TargetLOD->TypeDataModule;
				else if (TargetLOD->Modules.IsValidIndex(ModuleIndex)) ToRemove = TargetLOD->Modules[ModuleIndex];
				if (ToRemove)
				{
					if (Selected == ToRemove) Selected = Edit.Emitter;
					if (ContextModule == ToRemove) ContextModule = nullptr;
					RemoveCurveEntries(nullptr, ToRemove);
					TargetLOD->RemoveModule(ToRemove);
				}
			}
			Edit.Emitter->CacheEmitterModuleInfo();
		}
		break;
	case FPendingEdit::SetTypeData:
		if (LOD && Edit.Emitter)
		{
			// 모든 LOD 레벨의 TypeData를 같은 종류로 맞춘다
			for (UParticleLODLevel* TargetLOD : Edit.Emitter->LODLevels)
			{
				if (!TargetLOD) continue;
				if (UParticleModule* Old = TargetLOD->TypeDataModule)
				{
					if (Selected == Old) Selected = Edit.Emitter;
					if (ContextModule == Old) ContextModule = nullptr;
					RemoveCurveEntries(nullptr, Old);
					TargetLOD->RemoveModule(Old);
				}
			}
			if (UParticleModule* Added = LOD->AddModule(Edit.ModuleClass))
			{
				// UE Cascade처럼 새로 붙인 모듈이 이미터를 자기에게 맞는 기본값으로 맞춘다
				Added->SetToSensibleDefaults(Edit.Emitter);
				Selected = Added;
				json CopyData = json::object();
				Added->Serialize(CopyData, false);
				for (UParticleLODLevel* OtherLOD : Edit.Emitter->LODLevels)
				{
					if (!OtherLOD || OtherLOD == LOD) continue;
					if (UParticleModule* OtherAdded = OtherLOD->AddModule(Edit.ModuleClass))
					{
						json CloneData = CopyData;
						OtherAdded->Serialize(CloneData, true);
					}
				}
			}
			Edit.Emitter->CacheEmitterModuleInfo();
		}
		break;
	case FPendingEdit::RefreshModule:
		if (Edit.Emitter) Edit.Emitter->CacheEmitterModuleInfo();
		break;
	case FPendingEdit::AddEmitter:
		Selected = AddDefaultEmitter();
		break;
	case FPendingEdit::DuplicateEmitter:
		if (UParticleEmitter* Copy = System->DuplicateEmitter(Edit.Emitter))
			Selected = Copy;
		break;
	case FPendingEdit::RemoveEmitter:
	{
		// 지울 이미터나 그 모듈을 가리키는 선택은 놓는다
		UParticleModule* SelectedModule = Cast<UParticleModule>(Selected);
		if (Selected == Edit.Emitter || (SelectedModule && FindModuleEmitter(SelectedModule) == Edit.Emitter))
			Selected = nullptr;
		if (ContextModule && FindModuleEmitter(ContextModule) == Edit.Emitter)
			ContextModule = nullptr;
		RemoveCurveEntries(Edit.Emitter, nullptr);
		System->RemoveEmitter(Edit.Emitter);
		break;
	}
	default:
		break;
	}
}

// UE Cascade 이미터 우클릭 메뉴: 맨 위 검색창, Emitter / Particle System / TypeData, 그 아래 모듈 카테고리.
// 검색어가 있으면 하위 메뉴 대신 이름이 맞는 모듈과 TypeData를 한 목록으로 보여준다.
void FParticleSystemEditor::DrawEmitterAddMenu(UParticleEmitter* Emitter, UParticleLODLevel* LOD)
{
	if (ImGui::IsWindowAppearing())
	{
		EmitterMenuSearch[0] = '\0';
		ImGui::SetKeyboardFocusHere();
	}
	ImGui::SetNextItemWidth(EditorUIScale::Px(200.0f));
	ImGui::InputTextWithHint("##Search", "Search...", EmitterMenuSearch, sizeof(EmitterMenuSearch));
	const bool bSearching = EmitterMenuSearch[0] != '\0';

	// Spawn은 이미터마다 하나라 이미 있으면 흐리게 둔다
	auto ModuleItem = [&](UClass* Class)
	{
		const bool bSlotTaken = Class->IsChildOf(UParticleModuleSpawn::StaticClass()) && LOD->SpawnModule;
		if (ImGui::MenuItem(GetModuleClassDisplayName(Class).c_str(), nullptr, false, !bSlotTaken))
			PendingEdit = { FPendingEdit::AddModule, Emitter, Class };
	};
	// TypeData는 이미터 종류라 하나만 둔다. 이미 있으면 새 것으로 바꾼다.
	auto TypeDataItem = [&](const FTypeDataEntry& Entry)
	{
		UClass* Class = FindTypeDataClass(Entry);
		if (ImGui::MenuItem(Entry.Label, nullptr, false, Class != nullptr))
			PendingEdit = { FPendingEdit::SetTypeData, Emitter, Class };
		if (!Class) ImGui::SetItemTooltip("Not implemented yet (%s)", Entry.ClassName);
	};

	if (bSearching)
	{
		ImGui::Separator();
		bool bAny = false;
		for (const FTypeDataEntry& Entry : TypeDataEntries)
		{
			if (!MatchesSearch(Entry.Label, EmitterMenuSearch)) continue;
			TypeDataItem(Entry);
			bAny = true;
		}
		for (UClass* Class : GetAddableModuleClasses())
		{
			if (!MatchesSearch(GetModuleClassDisplayName(Class), EmitterMenuSearch) &&
				!MatchesSearch(GetModuleCategory(Class), EmitterMenuSearch)) continue;
			ModuleItem(Class);
			bAny = true;
		}
		if (!bAny) ImGui::TextDisabled("No results");
		return;
	}

	DrawEmitterMenuItems(Emitter);
	if (ImGui::BeginMenu("TypeData"))
	{
		ImGui::SeparatorText("TypeData");
		for (const FTypeDataEntry& Entry : TypeDataEntries) TypeDataItem(Entry);
		ImGui::EndMenu();
	}

	auto DrawCategory = [&](const FString& Category)
	{
		TArray<UClass*> Classes;
		for (UClass* Class : GetAddableModuleClasses())
			if (GetModuleCategory(Class) == Category) Classes.Add(Class);
		if (ImGui::BeginMenu(Category.c_str(), !Classes.IsEmpty()))
		{
			ImGui::SeparatorText(Category.c_str());
			for (UClass* Class : Classes) ModuleItem(Class);
			ImGui::EndMenu();
		}
	};
	for (const char* Category : ModuleCategories) DrawCategory(Category);
	// UE 카테고리에 맞지 않는 모듈이 있을 때만
	for (UClass* Class : GetAddableModuleClasses())
	{
		if (GetModuleCategory(Class) != "Other") continue;
		DrawCategory("Other");
		break;
	}
}

// UE Cascade 이미터 우클릭 메뉴의 Emitter / Particle System 하위 메뉴
void FParticleSystemEditor::DrawEmitterMenuItems(UParticleEmitter* Emitter)
{
	if (ImGui::BeginMenu("Emitter"))
	{
		if (ImGui::MenuItem("Duplicate Emitter"))
			PendingEdit = { FPendingEdit::DuplicateEmitter, Emitter };
		if (ImGui::MenuItem("Delete Emitter", "Delete"))
			PendingEdit = { FPendingEdit::RemoveEmitter, Emitter };
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Particle System"))
	{
		if (ImGui::MenuItem("New Particle Sprite Emitter"))
			PendingEdit = { FPendingEdit::AddEmitter };
		ImGui::EndMenu();
	}
}

void FParticleSystemEditor::DrawEmitterColumn(int32 EmitterIndex, UParticleEmitter* Emitter, float Width)
{
	if (!Emitter) return;
	UParticleLODLevel* LOD = Emitter->GetLODLevel(CurrentLOD);
	if (!LOD) LOD = Emitter->GetLODLevel(0);
	if (!LOD) return;
	ImDrawList* DL = ImGui::GetWindowDrawList();

	// 헤더: 이미터 이름 + 현재 LOD에서의 활성 체크박스 + 활성 파티클 수
	const float HeaderHeight = ImGui::GetFrameHeight() * 2.0f;
	const ImVec2 Min = ImGui::GetCursorScreenPos();
	const ImVec2 Max(Min.x + Width, Min.y + HeaderHeight);
	const bool bHeaderClicked = ImGui::InvisibleButton("##EmitterHeader", ImVec2(Width, HeaderHeight));
	bool bOpenEmitterMenu = ImGui::IsItemClicked(ImGuiMouseButton_Right);
	if (bOpenEmitterMenu) Selected = Emitter;
	const ImU32 HeaderBg = LOD->bEnabled ? IM_COL32(200, 200, 200, 255) : IM_COL32(110, 110, 115, 255);
	DL->AddRectFilled(Min, Max, HeaderBg);
	DL->AddRectFilled(Min, ImVec2(Min.x + EditorUIScale::Px(4.0f), Max.y), IM_COL32(40, 190, 230, 255));
	if (Selected == Emitter) DL->AddRect(Min, Max, IM_COL32(255, 170, 40, 255), 0.0f, 0, EditorUIScale::Px(2.0f));
	const float Pad = ImGui::GetStyle().FramePadding.x + EditorUIScale::Px(4.0f);
	const float BoxSize = ImGui::GetFrameHeight() * 0.55f;
	const ImVec2 BoxMin(Max.x - Pad - BoxSize, Min.y + EditorUIScale::Px(3.0f));
	const ImVec2 BoxMax(BoxMin.x + BoxSize, BoxMin.y + BoxSize);
	const FString& EmitterName = Emitter->EmitterName.empty() ? FString("Particle Emitter") : Emitter->EmitterName;
	DL->PushClipRect(Min, ImVec2(BoxMin.x - EditorUIScale::Px(2.0f), Max.y), true);
	DL->AddText(ImVec2(Min.x + Pad, Min.y + EditorUIScale::Px(3.0f)), IM_COL32(20, 20, 20, 255), EmitterName.c_str());
	DL->PopClipRect();
	DL->AddRect(BoxMin, BoxMax, IM_COL32(20, 20, 20, 255), 0.0f, 0, 1.0f);
	if (LOD->bEnabled)
	{
		const float T = EditorUIScale::Px(1.5f);
		DL->AddLine(ImVec2(BoxMin.x + BoxSize * 0.2f, BoxMin.y + BoxSize * 0.55f), ImVec2(BoxMin.x + BoxSize * 0.42f, BoxMin.y + BoxSize * 0.78f), IM_COL32(20, 20, 20, 255), T);
		DL->AddLine(ImVec2(BoxMin.x + BoxSize * 0.42f, BoxMin.y + BoxSize * 0.78f), ImVec2(BoxMin.x + BoxSize * 0.82f, BoxMin.y + BoxSize * 0.25f), IM_COL32(20, 20, 20, 255), T);
	}
	if (bHeaderClicked)
	{
		const ImVec2 Mouse = ImGui::GetIO().MousePos;
		if (Mouse.x >= BoxMin.x - EditorUIScale::Px(2.0f) && Mouse.x <= BoxMax.x + EditorUIScale::Px(2.0f) &&
			Mouse.y >= BoxMin.y - EditorUIScale::Px(2.0f) && Mouse.y <= BoxMax.y + EditorUIScale::Px(2.0f))
		{
			LOD->bEnabled = !LOD->bEnabled;
			MarkDirty();
			RestartSimulation();
		}
		else
		{
			Selected = Emitter;
		}
	}
	int32 Active = 0;
	if (const auto* Instance = Component ? Component->GetEmitterInstance(EmitterIndex) : nullptr)
		Active = Instance->ActiveParticles;
	const FString Count = std::to_string(Active);
	const ImVec2 CountSize = ImGui::CalcTextSize(Count.c_str());
	DL->AddText(ImVec2(Max.x - CountSize.x - Pad, Max.y - CountSize.y - EditorUIScale::Px(3.0f)), IM_COL32(20, 20, 20, 255), Count.c_str());

	ImGui::Dummy(ImVec2(Width, EditorUIScale::Px(6.0f)));

	// 모듈 목록: TypeData(있을 때만, 맨 위), Required(노랑), Spawn(빨강), 나머지 (UE Cascade 순서)
	int32 Row = 0;
	bool bOpenModuleMenu = false;
	auto Draw = [&](UParticleModule* Module, ImU32 Background, bool bCanDisable)
	{
		if (!Module) return;
		ImGui::PushID(Row++);
		const TArray<FModuleDistribution> Curves = GetModuleDistributions(Module);
		bool bCurvesShown = false;
		for (const FCurveEntry& Entry : CurveEntries)
			bCurvesShown |= (Entry.Module == Module);
		const int32 Result = DrawModuleRow("##Module", GetModuleDisplayName(Module), Background, Selected == Module,
			bCanDisable ? &Module->bEnabled : nullptr, Width, Curves.Num() > 0, bCurvesShown);
		if (Result == 1) Selected = Module;
		// UE Cascade처럼 모듈의 분포를 모두 Curve Editor에 보낸다 (이미 있는 것은 건너뛴다)
		if (Result == 3)
		{
			Selected = Module;
			for (const FModuleDistribution& Entry : Curves)
				SendToCurveEditor(Module, Entry.Label, Entry.Float, Entry.Vector, Entry.MinValue, Entry.bLife);
		}
		if (Result == 2)
		{
			MarkDirty();
			RestartSimulation();
		}
		if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
		{
			Selected = Module;
			ContextModule = Module;
			bOpenModuleMenu = true;
		}
		ImGui::PopID();
	};
	Draw(LOD->TypeDataModule, IM_COL32(60, 60, 66, 255), false);
	Draw(LOD->RequiredModule, IM_COL32(150, 150, 60, 255), false);
	Draw(LOD->SpawnModule, IM_COL32(150, 60, 60, 255), true);
	for (UParticleModule* Module : LOD->Modules)
		Draw(Module, IM_COL32(45, 45, 48, 255), true);

	// 열 아래 빈 곳: 좌클릭은 선택 해제, 우클릭은 이미터 메뉴 (UE Cascade의 이미터 열 우클릭)
	const float FillHeight = std::max(ImGui::GetContentRegionAvail().y, ImGui::GetFrameHeight() * 2.0f);
	ImGui::InvisibleButton("##EmitterFill", ImVec2(Width, FillHeight), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
	if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) Selected = nullptr;
	if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
	{
		Selected = Emitter;
		bOpenEmitterMenu = true;
	}

	if (bOpenModuleMenu) ImGui::OpenPopup("##ModuleMenu");
	if (bOpenEmitterMenu) ImGui::OpenPopup("##EmitterMenu");

	// 모듈 우클릭: UE Cascade와 같이 세 항목만 둔다 (켜고 끄기는 줄의 체크박스)
	if (ImGui::BeginPopup("##ModuleMenu"))
	{
		if (UParticleModule* Module = ContextModule)
		{
			if (ImGui::MenuItem("Delete Module", "Delete", false, CanRemoveModule(LOD, Module)))
				PendingEdit = { FPendingEdit::RemoveModule, Emitter, nullptr, Module };
			// 모듈 목록과 Payload 레이아웃을 다시 만들고 시뮬레이션을 처음부터 돌린다
			if (ImGui::MenuItem("Refresh Module"))
				PendingEdit = { FPendingEdit::RefreshModule, Emitter, nullptr, Module };
			ImGui::MenuItem("Convert to Seeded", nullptr, false, false);
			ImGui::SetItemTooltip("Not implemented yet");
		}
		ImGui::EndPopup();
	}
	if (ImGui::BeginPopup("##EmitterMenu"))
	{
		DrawEmitterAddMenu(Emitter, LOD);
		ImGui::EndPopup();
	}
}

void FParticleSystemEditor::DrawDetails()
{
	if (!Selected)
	{
		ImGui::TextUnformatted("Particle System");
		ImGui::Separator();
		if (System)
		{
			System->SyncLODDistances();
			ImGui::BeginGroup();
			FPropertyEditor::DrawObjectProperties(System);
			ImGui::SeparatorText("LOD Distances (m)");
			const int32 NumLODs = System->GetNumLODLevels();
			for (int32 i = 0; i < NumLODs; ++i)
			{
				ImGui::PushID(i);
				const FString Label = std::format("LOD {}", i);
				float Dist = System->GetLODDistance(i);
				if (i == 0)
				{
					ImGui::BeginDisabled(true);
					ImGui::DragFloat(Label.c_str(), &Dist, 0.1f, 0.0f, 0.0f, "%.2f");
					ImGui::EndDisabled();
				}
				else
				{
					const float MinDist = System->GetLODDistance(i - 1);
					if (ImGui::DragFloat(Label.c_str(), &Dist, 0.1f, MinDist, FLT_MAX, "%.2f", ImGuiSliderFlags_AlwaysClamp))
					{
						System->SetLODDistance(i, Dist);
						MarkDirty();
					}
				}
				ImGui::PopID();
			}
			ImGui::EndGroup();
			if (ImGui::IsItemEdited() || ImGui::IsItemDeactivatedAfterEdit()) MarkDirty();
			ImGui::Separator();
		}
		ImGui::Text("Emitters: %d", System ? System->Emitters.Num() : 0);
		ImGui::Text("LOD Levels: %d (Editing LOD %d)", System ? System->GetNumLODLevels() : 0, CurrentLOD);
		ImGui::TextDisabled("Select an emitter or a module in the Emitters panel.");
		return;
	}
	if (UParticleEmitter* Emitter = Cast<UParticleEmitter>(Selected))
	{
		ImGui::TextUnformatted("Particle Emitter");
		ImGui::Separator();
		// EmitterName 등 이미터의 UPROPERTY(EditAnywhere)를 편집한다
		ImGui::BeginGroup();
		FPropertyEditor::DrawObjectProperties(Emitter);
		if (UParticleLODLevel* ActiveLOD = Emitter->GetLODLevel(CurrentLOD))
		{
			const FString EnableLabel = std::format("Enabled in LOD {}", CurrentLOD);
			if (ImGui::Checkbox(EnableLabel.c_str(), &ActiveLOD->bEnabled))
			{
				MarkDirty();
				RestartSimulation();
			}
		}
		ImGui::EndGroup();
		if (ImGui::IsItemEdited() || ImGui::IsItemDeactivatedAfterEdit()) MarkDirty();
		ImGui::Separator();
		ImGui::TextDisabled("LOD levels: %d (Editing LOD %d)", Emitter->LODLevels.Num(), CurrentLOD);
		ImGui::TextDisabled("Particle size: %d bytes", Emitter->ParticleSize);
		return;
	}

	ImGui::TextUnformatted(GetModuleDisplayName(Cast<UParticleModule>(Selected)).c_str());
	ImGui::Separator();
	// 값을 다 바꾸면(드래그를 놓거나 입력을 마치면) 시뮬레이션을 다시 시작해 바로 확인한다.
	ImGui::BeginGroup();
	UParticleModule* SelectedModule = Cast<UParticleModule>(Selected);
	// 키 추가/삭제처럼 IsItemEdited로 잡히지 않는 변경도 dirty와 재시작에 넣는다
	bool bKeysChanged = false;
	// 분포 멤버는 GetModuleDistributions가 모듈 종류별로 모은다 (모듈 줄의 커브 버튼과 같은 목록)
	const TArray<FModuleDistribution> Distributions = GetModuleDistributions(SelectedModule);
	const auto DrawDist = [&](const FModuleDistribution& Entry)
	{
		const FDistributionEditResult Result = Entry.Float
			? DrawDistribution(Entry.Label, *Entry.Float, Entry.MinValue)
			: DrawDistribution(Entry.Label, *Entry.Vector, Entry.MinValue);
		bKeysChanged |= Result.bChanged;
	};
	int32 CategoryCount = 0;
	if (const FDetailsCategory* Categories = GetDetailsCategories(SelectedModule, CategoryCount))
	{
		// UE처럼 카테고리별로 묶어 그린다. 표에 없는 편집 가능 속성은 맨 아래 Other에 둔다 (빠뜨려도 사라지지 않게).
		TArray<const FProperty*> Listed;
		for (int32 c = 0; c < CategoryCount; ++c)
		{
			ImGui::PushID(c);
			const bool bOpen = ImGui::CollapsingHeader(Categories[c].Name, Categories[c].bAdvanced ? ImGuiTreeNodeFlags_None : ImGuiTreeNodeFlags_DefaultOpen);
			for (const char* Item : Categories[c].Items)
			{
				if (!Item) break;
				if (const FProperty* Property = FindEditProperty(Selected, Item))
				{
					Listed.Add(Property);
					if (bOpen) FPropertyEditor::DrawProperty(Selected, *Property);
					continue;
				}
				for (const FModuleDistribution& Entry : Distributions)
					if (bOpen && std::strcmp(Entry.Label, Item) == 0) DrawDist(Entry);
			}
			ImGui::PopID();
		}
		bool bOtherHeader = false;
		bool bOtherOpen = false;
		for (UClass* Class = Selected->GetClass(); Class; Class = Class->Super)
		{
			for (const FProperty* Property : Class->GetProperties())
			{
				// bEnabled는 모듈 줄의 체크박스다 (UE도 Details에 두지 않는다)
				if (!Property->HasAnyFlags(CPF_Edit) || Listed.Find(Property) != INDEX_NONE || Property->GetName() == "bEnabled") continue;
				if (!bOtherHeader)
				{
					bOtherHeader = true;
					bOtherOpen = ImGui::CollapsingHeader("Other", ImGuiTreeNodeFlags_DefaultOpen);
				}
				if (bOtherOpen) FPropertyEditor::DrawProperty(Selected, *Property);
			}
		}
	}
	else
	{
		FPropertyEditor::DrawObjectProperties(Selected);
		if (auto* Required = Cast<UParticleModuleRequired>(Selected))
		{
			// None이면 이미터 종류와 관계없이 엔진 기본 스프라이트 머티리얼로 그린다 (UE의 DefaultSpriteMaterial)
			if (DrawParticleMaterial(Required, ResourcePaths::ParticleSpriteMaterial)) MarkDirty();
		}
		for (const FModuleDistribution& Entry : Distributions) DrawDist(Entry);
		// 메시 이미터의 MaterialOverrides는 UPROPERTY가 아니라서 DrawObjectProperties가 그리지 않는다. 직접 그리고 바뀌면 변경 표시를 한다.
		if (auto* Mesh = Cast<UParticleModuleTypeDataMesh>(Selected))
		{
			if (DrawMeshMaterialOverrides(Mesh)) MarkDirty();
		}
	}
	ImGui::EndGroup();
	const bool bEdited = ImGui::IsItemEdited() || ImGui::IsItemDeactivatedAfterEdit() || bKeysChanged;
	if (bEdited) MarkDirty();
	bRestartPending |= bEdited;
	if (bRestartPending && !ImGui::IsAnyItemActive())
	{
		RestartSimulation();
		bRestartPending = false;
	}
}

namespace
{
	// Float 커브마다 다른 색 (UE Cascade처럼 목록 순서대로 돌려 쓴다). 벡터는 채널마다 빨강/초록/파랑.
	constexpr ImU32 CurvePalette[] = {
		IM_COL32(255, 200, 60, 255), IM_COL32(90, 200, 255, 255), IM_COL32(255, 110, 200, 255),
		IM_COL32(150, 255, 120, 255), IM_COL32(255, 150, 80, 255), IM_COL32(190, 150, 255, 255),
	};
	constexpr ImU32 ChannelColors[3] = { IM_COL32(255, 80, 80, 255), IM_COL32(80, 230, 80, 255), IM_COL32(90, 140, 255, 255) };
	constexpr const char* ChannelNames[3] = { "X", "Y", "Z" };

	float GetChannel(float Value, int32) { return Value; }
	float GetChannel(const FVector& Value, int32 Channel) { return Channel == 0 ? Value.X : (Channel == 1 ? Value.Y : Value.Z); }
	void SetChannel(float& Value, int32, float NewValue) { Value = NewValue; }
	void SetChannel(FVector& Value, int32 Channel, float NewValue) { (Channel == 0 ? Value.X : (Channel == 1 ? Value.Y : Value.Z)) = NewValue; }

	// 그리드 간격: 화면에서 MinPixels 이상 떨어지는 1, 2, 5 × 10^n
	float NiceGridStep(float Range, float Pixels, float MinPixels)
	{
		if (Range <= 0.0f || Pixels <= 0.0f) return 1.0f;
		const float Raw = Range * MinPixels / Pixels;
		const float Pow10 = std::pow(10.0f, std::floor(std::log10(Raw)));
		for (const float Mul : { 1.0f, 2.0f, 5.0f })
			if (Pow10 * Mul >= Raw) return Pow10 * Mul;
		return Pow10 * 10.0f;
	}

	// 한 채널이 그래프에 차지하는 범위. Curve는 키 시간도 넣는다.
	template <typename T>
	void AccumulateBounds(const TParticleDistribution<T>& D, int32 Channel, float& MinX, float& MaxX, float& MinY, float& MaxY)
	{
		const auto AddY = [&](const T& Value)
		{
			const float Y = GetChannel(Value, Channel);
			MinY = std::min(MinY, Y);
			MaxY = std::max(MaxY, Y);
		};
		switch (D.Kind)
		{
		case EDistributionKind::Constant: AddY(D.Constant); break;
		case EDistributionKind::Uniform: AddY(D.Min); AddY(D.Max); break;
		case EDistributionKind::Curve:
			for (const auto& Key : D.Keys)
			{
				AddY(Key.Value);
				MinX = std::min(MinX, Key.Time);
				MaxX = std::max(MaxX, Key.Time);
			}
			break;
		}
	}
}

void FParticleSystemEditor::SendToCurveEditor(UParticleModule* Module, const char* Label, FFloatDistribution* Float, FVectorDistribution* Vector, float MinValue, bool bLife)
{
	if (!Module || (!Float && !Vector)) return;
	PanelOpen[CurveEditor] = true;
	for (const FCurveEntry& Entry : CurveEntries)
		if (Entry.Float == Float && Entry.Vector == Vector) return;
	FCurveEntry Entry;
	Entry.Module = Module;
	Entry.Label = GetModuleDisplayName(Module) + ": " + Label;
	Entry.Float = Float;
	Entry.Vector = Vector;
	Entry.MinValue = MinValue;
	Entry.bLife = bLife;
	Entry.Color = CurvePalette[CurveEntries.Num() % IM_ARRAYSIZE(CurvePalette)];
	CurveEntries.Add(Entry);
	bCurveFitPending = true;
	ActiveCurveEntry = static_cast<int32>(CurveEntries.Num()) - 1;
}

void FParticleSystemEditor::RemoveCurveEntries(const UParticleEmitter* Emitter, const UParticleModule* Module)
{
	for (int32 i = static_cast<int32>(CurveEntries.Num()) - 1; i >= 0; --i)
	{
		const UParticleModule* Owner = CurveEntries[i].Module;
		if (Owner == Module || (Emitter && FindModuleEmitter(Owner) == Emitter))
		{
			CurveEntries.RemoveAt(static_cast<uint32>(i));
			ClearCurveKeySelection();
			ActiveCurveEntry = -1;
		}
	}
}

void FParticleSystemEditor::RemoveLODCurveEntries(int32 LODIndex)
{
	if (!System) return;
	for (const UParticleEmitter* Emitter : System->Emitters)
	{
		const UParticleLODLevel* LOD = Emitter ? Emitter->GetLODLevel(LODIndex) : nullptr;
		if (!LOD) continue;
		RemoveCurveEntries(nullptr, LOD->RequiredModule);
		RemoveCurveEntries(nullptr, LOD->SpawnModule);
		RemoveCurveEntries(nullptr, LOD->TypeDataModule);
		for (const UParticleModule* Module : LOD->Modules)
		{
			RemoveCurveEntries(nullptr, Module);
		}
	}
}

// 보이는 커브가 모두 들어오게 범위를 맞춘다 (UE Cascade의 Fit).
// 시간축은 0~1을 기본으로 두고 키가 밖에 있으면 넓힌다 (수명 중 모듈과 빔 위치 값은 0~1을 쓴다).
void FParticleSystemEditor::FitCurveView()
{
	float MinX = 0.0f, MaxX = 1.0f;
	float MinY = FLT_MAX, MaxY = -FLT_MAX;
	for (const FCurveEntry& Entry : CurveEntries)
	{
		if (!Entry.bVisible) continue;
		if (Entry.Float) AccumulateBounds(*Entry.Float, 0, MinX, MaxX, MinY, MaxY);
		else
		{
			for (int32 Channel = 0; Channel < 3; ++Channel)
				if (Entry.bShowChannel[Channel]) AccumulateBounds(*Entry.Vector, Channel, MinX, MaxX, MinY, MaxY);
		}
	}
	if (MinY > MaxY) { MinY = 0.0f; MaxY = 1.0f; }
	// 값이 한 줄(Constant)이면 위아래로 여유를 둔다
	if (MaxY - MinY < 1e-4f)
	{
		const float Half = std::max(std::abs(MinY) * 0.5f, 0.5f);
		MinY -= Half;
		MaxY += Half;
	}
	// 수명 기준이면 가로축은 늘 0 ~ 1 (키 시간도 0 ~ 1로 묶여 있다)
	if (IsLifeCurveView()) { MinX = 0.0f; MaxX = 1.0f; }
	const float PadX = (MaxX - MinX) * 0.05f;
	const float PadY = (MaxY - MinY) * 0.1f;
	CurveViewMinX = MinX - PadX;
	CurveViewMaxX = MaxX + PadX;
	CurveViewMinY = MinY - PadY;
	CurveViewMaxY = MaxY + PadY;
}

bool FParticleSystemEditor::IsLifeCurveView() const
{
	bool bAny = false;
	for (const FCurveEntry& Entry : CurveEntries)
	{
		if (!Entry.bVisible) continue;
		if (!Entry.bLife) return false;
		bAny = true;
	}
	return bAny;
}

void FParticleSystemEditor::ApplyCurvePreset(int32 EntryIndex, ECurvePreset Preset)
{
	if (EntryIndex < 0 || EntryIndex >= static_cast<int32>(CurveEntries.Num())) return;
	FCurveEntry& Entry = CurveEntries[EntryIndex];
	// 프리셋 모양: (시간, 크기 비율). 크기 1 = 지금 커브의 가장 큰 값
	struct FPresetPoint { float Time; float Scale; };
	static const FPresetPoint Flat[] = { { 0.0f, 1.0f }, { 1.0f, 1.0f } };
	static const FPresetPoint FadeOut[] = { { 0.0f, 1.0f }, { 1.0f, 0.0f } };
	static const FPresetPoint FadeIn[] = { { 0.0f, 0.0f }, { 1.0f, 1.0f } };
	static const FPresetPoint Pulse[] = { { 0.0f, 0.0f }, { 0.5f, 1.0f }, { 1.0f, 0.0f } };
	const FPresetPoint* Points = Flat;
	int32 PointCount = IM_ARRAYSIZE(Flat);
	switch (Preset)
	{
	case ECurvePreset::Flat: break;
	case ECurvePreset::FadeOut: Points = FadeOut; PointCount = IM_ARRAYSIZE(FadeOut); break;
	case ECurvePreset::FadeIn: Points = FadeIn; PointCount = IM_ARRAYSIZE(FadeIn); break;
	case ECurvePreset::Pulse: Points = Pulse; PointCount = IM_ARRAYSIZE(Pulse); break;
	}

	const auto Apply = [&](auto& D)
	{
		using T = std::decay_t<decltype(D.Constant)>;
		const TParticleDistribution<T> Old = D;
		const int32 ChannelCount = Entry.Float ? 1 : 3;
		// 채널마다 크기: 지금 값 중 가장 큰 값 (0 이하면 1)
		float Peak[3] = { 1.0f, 1.0f, 1.0f };
		for (int32 Channel = 0; Channel < ChannelCount; ++Channel)
		{
			float MinX = 0.0f, MaxX = 1.0f, MinY = FLT_MAX, MaxY = -FLT_MAX;
			AccumulateBounds(Old, Channel, MinX, MaxX, MinY, MaxY);
			Peak[Channel] = MaxY > 1e-4f ? MaxY : 1.0f;
		}
		D.Keys = TArray<TDistributionKey<T>>();
		D.Kind = EDistributionKind::Curve;
		for (int32 p = 0; p < PointCount; ++p)
		{
			// 숨긴 채널은 지금 커브 값을 그대로 둔다
			T Value = Old.GetValue(Points[p].Time, nullptr);
			for (int32 Channel = 0; Channel < ChannelCount; ++Channel)
			{
				if (Entry.Vector && !Entry.bShowChannel[Channel]) continue;
				SetChannel(Value, Channel, std::max(Peak[Channel] * Points[p].Scale, Entry.MinValue));
			}
			D.AddKey(Points[p].Time, Value);
		}
	};
	if (Entry.Float) Apply(*Entry.Float);
	else Apply(*Entry.Vector);
	ClearCurveKeySelection();
	bCurveFitPending = true;
	OnCurveKeysChanged();
}

void FParticleSystemEditor::DrawCurveEditor()
{
	// 왼쪽: 보낸 값 목록 (UE Cascade 커브 에디터의 왼쪽 목록). 체크박스로 그래프에서 보이고 숨긴다.
	const float ListWidth = EditorUIScale::Px(260.0f);
	ImGui::BeginChild("CurveList", ImVec2(ListWidth, 0.0f), ImGuiChildFlags_Borders);
	if (CurveEntries.Num() == 0)
		ImGui::TextDisabled("Click the curve icon on a module\nin the Emitters panel to add\nits values here.");
	int32 RemoveIndex = -1;
	for (int32 i = 0; i < static_cast<int32>(CurveEntries.Num()); ++i)
	{
		FCurveEntry& Entry = CurveEntries[i];
		ImGui::PushID(i);
		// Float는 체크 표시를 선 색으로 칠해 범례로 쓴다
		if (Entry.Float) ImGui::PushStyleColor(ImGuiCol_CheckMark, Entry.Color);
		ImGui::Checkbox("##Visible", &Entry.bVisible);
		if (Entry.Float) ImGui::PopStyleColor();
		ImGui::SetItemTooltip("Show in the graph");
		ImGui::SameLine();
		const EDistributionKind Kind = Entry.Float ? Entry.Float->Kind : Entry.Vector->Kind;
		FString Text = Entry.Label;
		if (Kind == EDistributionKind::Constant) Text += " (Constant)";
		else if (Kind == EDistributionKind::Uniform) Text += " (Uniform)";
		const float RemoveWidth = ImGui::CalcTextSize("X").x + ImGui::GetStyle().FramePadding.x * 2.0f;
		const float LabelWidth = std::max(ImGui::GetContentRegionAvail().x - RemoveWidth - ImGui::GetStyle().ItemSpacing.x, 1.0f);
		// 누르면 그 모듈을 Details에 띄우고, 프리셋 버튼이 이 커브를 바꾼다
		if (ImGui::Selectable(Text.c_str(), ActiveCurveEntry == i, 0, ImVec2(LabelWidth, 0.0f)))
		{
			Selected = Entry.Module;
			ActiveCurveEntry = i;
		}
		ImGui::SetItemTooltip("%s", Text.c_str());
		ImGui::SameLine();
		if (ImGui::SmallButton("X")) RemoveIndex = i;
		ImGui::SetItemTooltip("Remove from the Curve Editor");
		// 벡터: X/Y/Z 채널을 따로 켜고 끈다 (체크 색 = 선 색)
		if (Entry.Vector)
		{
			const float IndentWidth = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
			ImGui::Indent(IndentWidth);
			for (int32 Channel = 0; Channel < 3; ++Channel)
			{
				if (Channel > 0) ImGui::SameLine();
				ImGui::PushStyleColor(ImGuiCol_CheckMark, ChannelColors[Channel]);
				ImGui::Checkbox(ChannelNames[Channel], &Entry.bShowChannel[Channel]);
				ImGui::PopStyleColor();
			}
			ImGui::Unindent(IndentWidth);
		}
		ImGui::PopID();
	}
	if (RemoveIndex >= 0)
	{
		CurveEntries.RemoveAt(static_cast<uint32>(RemoveIndex));
		ClearCurveKeySelection();
		ActiveCurveEntry = -1;
	}
	if (ActiveCurveEntry >= static_cast<int32>(CurveEntries.Num())) ActiveCurveEntry = -1;
	ImGui::EndChild();
	ImGui::SameLine();

	// 선택한 키가 아직 있는지 (Details에서 종류를 바꾸거나 키를 지웠을 수 있다)
	const auto WithDistribution = [](FCurveEntry& Entry, const auto& Func)
	{
		if (Entry.Float) Func(*Entry.Float);
		else Func(*Entry.Vector);
	};
	if (SelectedCurveEntry >= 0)
	{
		bool bValid = SelectedCurveEntry < static_cast<int32>(CurveEntries.Num());
		if (bValid)
		{
			WithDistribution(CurveEntries[SelectedCurveEntry], [&](auto& D)
			{
				bValid = D.Kind == EDistributionKind::Curve && SelectedCurveKey >= 0 && SelectedCurveKey < static_cast<int32>(D.Keys.Num());
			});
		}
		if (!bValid) ClearCurveKeySelection();
	}

	// 오른쪽: 그래프 (가로 = 시간 또는 수명 비율, 세로 = 값)
	const bool bLifeView = IsLifeCurveView();
	// 키 시간의 범위: 수명 기준 커브는 0 ~ 1 (태어날 때 ~ 죽을 때)
	const auto TimeLimits = [&](int32 EntryIndex, float& Lo, float& Hi)
	{
		if (CurveEntries[EntryIndex].bLife) { Lo = std::max(Lo, 0.0f); Hi = std::min(Hi, 1.0f); }
	};
	ImGui::BeginGroup();
	if (ImGui::Button("Fit")) bCurveFitPending = true;
	ImGui::SetItemTooltip("Fit all visible curves in the view");
	// 프리셋: 목록에서 고른 커브의 모양을 한 번에 바꾼다 (보이는 채널만, 크기는 지금 가장 큰 값)
	{
		const bool bHasActive = ActiveCurveEntry >= 0 && ActiveCurveEntry < static_cast<int32>(CurveEntries.Num());
		struct FPresetButton { const char* Label; ECurvePreset Preset; const char* Tip; };
		static const FPresetButton Buttons[] = {
			{ "Flat", ECurvePreset::Flat, "Same value for the whole life" },
			{ "Fade Out", ECurvePreset::FadeOut, "Start at the value, go down to 0" },
			{ "Fade In", ECurvePreset::FadeIn, "Start at 0, go up to the value" },
			{ "Pulse", ECurvePreset::Pulse, "0 -> value at half -> 0" },
		};
		ImGui::BeginDisabled(!bHasActive);
		for (const FPresetButton& Button : Buttons)
		{
			ImGui::SameLine();
			if (ImGui::Button(Button.Label)) ApplyCurvePreset(ActiveCurveEntry, Button.Preset);
			ImGui::SetItemTooltip("%s\nApplies to: %s", Button.Tip, bHasActive ? CurveEntries[ActiveCurveEntry].Label.c_str() : "(click a curve in the list)");
		}
		ImGui::EndDisabled();
	}
	// 선택한 키를 숫자로 고친다 (UE Cascade 커브 에디터 툴바의 Time / Value 칸)
	if (SelectedCurveEntry >= 0)
	{
		WithDistribution(CurveEntries[SelectedCurveEntry], [&](auto& D)
		{
			auto& Keys = D.Keys;
			const int32 k = SelectedCurveKey;
			float Lo = (k > 0) ? Keys[k - 1].Time : -FLT_MAX;
			float Hi = (k + 1 < static_cast<int32>(Keys.Num())) ? Keys[k + 1].Time : FLT_MAX;
			TimeLimits(SelectedCurveEntry, Lo, Hi);
			const float FieldWidth = EditorUIScale::Px(90.0f);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(FieldWidth);
			// 수명 기준이면 시간을 %로 보여준다 (0 = 태어날 때, 100 = 죽을 때)
			if (CurveEntries[SelectedCurveEntry].bLife)
			{
				float Percent = Keys[k].Time * 100.0f;
				if (ImGui::DragFloat("Life##Key", &Percent, 0.5f, Lo * 100.0f, Hi * 100.0f, "%.1f%%", ImGuiSliderFlags_AlwaysClamp))
					Keys[k].Time = Percent / 100.0f;
			}
			else ImGui::DragFloat("Time##Key", &Keys[k].Time, 0.01f, Lo, Hi, "%.3f", ImGuiSliderFlags_AlwaysClamp);
			const bool bTimeDone = ImGui::IsItemDeactivatedAfterEdit();
			ImGui::SameLine();
			float Value = GetChannel(Keys[k].Value, SelectedCurveChannel);
			ImGui::SetNextItemWidth(FieldWidth);
			if (ImGui::DragFloat("Value##Key", &Value, 0.01f, CurveEntries[SelectedCurveEntry].MinValue, FLT_MAX, "%.3f", ImGuiSliderFlags_AlwaysClamp)) SetChannel(Keys[k].Value, SelectedCurveChannel, Value);
			if (bTimeDone || ImGui::IsItemDeactivatedAfterEdit()) OnCurveKeysChanged();
		});
	}
	ImGui::SameLine();
	ImGui::TextDisabled("(?)");
	ImGui::SetItemTooltip(
		"Over Life curves (Color Over Life, Size By Life): left = birth (0%%), right = death (100%%)\n"
		"Presets: click a curve in the list, then Flat / Fade Out / Fade In / Pulse\n"
		"Left click a key: select\n"
		"Drag a key: move it (stays between its neighbors)\n"
		"Ctrl + left click: add a key (on the curve under the mouse,\n"
		"  else the selected key's curve, else the nearest Curve)\n"
		"Right click: Add Key / Delete Key menu\n"
		"Delete: remove the selected key\n"
		"Wheel: zoom (Ctrl: time only, Shift: value only)\n"
		"Right / middle drag: pan\n"
		"(Over Life only: the time axis stays 0%% ~ 100%%, zoom and pan move the value axis)");
	const ImVec2 Size = ImGui::GetContentRegionAvail();
	if (Size.x < 1.0f || Size.y < 1.0f)
	{
		ImGui::EndGroup();
		return;
	}
	if (bCurveFitPending)
	{
		FitCurveView();
		bCurveFitPending = false;
	}
	const ImVec2 Min = ImGui::GetCursorScreenPos();
	const ImVec2 Max(Min.x + Size.x, Min.y + Size.y);
	ImGui::InvisibleButton("##CurveGraph", Size,
		ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
	const bool bHovered = ImGui::IsItemHovered();
	ImGuiIO& IO = ImGui::GetIO();

	// 팬: 오른쪽/가운데 버튼 드래그. 화면 이동량을 값 단위로 바꾼다.
	if (ImGui::IsItemActive() && (ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Middle)))
	{
		const float DX = bLifeView ? 0.0f : -IO.MouseDelta.x * (CurveViewMaxX - CurveViewMinX) / Size.x;
		const float DY = IO.MouseDelta.y * (CurveViewMaxY - CurveViewMinY) / Size.y;
		CurveViewMinX += DX; CurveViewMaxX += DX;
		CurveViewMinY += DY; CurveViewMaxY += DY;
	}
	// 줌: 휠. 마우스 아래 점을 고정한 채 범위를 줄이고 늘린다.
	if (bHovered)
	{
		ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
		if (IO.MouseWheel != 0.0f)
		{
			const float Factor = std::pow(0.85f, IO.MouseWheel);
			const float MouseX = CurveViewMinX + (IO.MousePos.x - Min.x) / Size.x * (CurveViewMaxX - CurveViewMinX);
			const float MouseY = CurveViewMinY + (Max.y - IO.MousePos.y) / Size.y * (CurveViewMaxY - CurveViewMinY);
			if (!IO.KeyShift && !bLifeView)
			{
				CurveViewMinX = MouseX + (CurveViewMinX - MouseX) * Factor;
				CurveViewMaxX = MouseX + (CurveViewMaxX - MouseX) * Factor;
			}
			if (!IO.KeyCtrl)
			{
				CurveViewMinY = MouseY + (CurveViewMinY - MouseY) * Factor;
				CurveViewMaxY = MouseY + (CurveViewMaxY - MouseY) * Factor;
			}
		}
	}
	// 수명 기준이면 가로축을 0 ~ 100%로 고정한다 (양 끝 키를 잡을 수 있게 조금 여유)
	if (bLifeView)
	{
		CurveViewMinX = -0.05f;
		CurveViewMaxX = 1.05f;
	}
	// 범위가 0이 되면 좌표 변환이 깨지므로 최소 폭을 둔다
	if (CurveViewMaxX - CurveViewMinX < 1e-4f) CurveViewMaxX = CurveViewMinX + 1e-4f;
	if (CurveViewMaxY - CurveViewMinY < 1e-4f) CurveViewMaxY = CurveViewMinY + 1e-4f;
	const float RangeX = CurveViewMaxX - CurveViewMinX;
	const float RangeY = CurveViewMaxY - CurveViewMinY;
	const auto ToScreenX = [&](float Time) { return Min.x + (Time - CurveViewMinX) / RangeX * Size.x; };
	const auto ToScreenY = [&](float Value) { return Max.y - (Value - CurveViewMinY) / RangeY * Size.y; };

	ImDrawList* DL = ImGui::GetWindowDrawList();
	DL->PushClipRect(Min, Max, true);
	DL->AddRectFilled(Min, Max, IM_COL32(40, 40, 40, 255));

	// 그리드와 눈금 숫자. 0 선은 밝게 그린다.
	const ImU32 GridColor = IM_COL32(62, 62, 62, 255);
	const ImU32 AxisColor = IM_COL32(120, 120, 120, 255);
	const ImU32 LabelColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
	const float LineHeight = ImGui::GetTextLineHeight();
	// 수명 기준이면 25% 간격 (좁으면 50%)
	const float StepX = bLifeView ? (Size.x >= EditorUIScale::Px(240.0f) ? 0.25f : 0.5f) : NiceGridStep(RangeX, Size.x, EditorUIScale::Px(80.0f));
	const float StepY = NiceGridStep(RangeY, Size.y, EditorUIScale::Px(40.0f));
	char Buffer[32];
	for (int64 i = static_cast<int64>(std::ceil(CurveViewMinX / StepX)), End = static_cast<int64>(std::floor(CurveViewMaxX / StepX)); i <= End; ++i)
	{
		const float Time = static_cast<float>(i) * StepX;
		const float X = ToScreenX(Time);
		// 수명 기준이면 0%(태어날 때)와 100%(죽을 때)를 밝게 그린다
		const bool bAxisLine = i == 0 || (bLifeView && std::abs(Time - 1.0f) < 1e-4f);
		DL->AddLine(ImVec2(X, Min.y), ImVec2(X, Max.y), bAxisLine ? AxisColor : GridColor);
		if (bLifeView) std::snprintf(Buffer, sizeof(Buffer), "%g%%", Time * 100.0f);
		else std::snprintf(Buffer, sizeof(Buffer), "%g", Time);
		DL->AddText(ImVec2(X + EditorUIScale::Px(3.0f), Max.y - LineHeight - EditorUIScale::Px(2.0f)), LabelColor, Buffer);
	}
	for (int64 i = static_cast<int64>(std::ceil(CurveViewMinY / StepY)), End = static_cast<int64>(std::floor(CurveViewMaxY / StepY)); i <= End; ++i)
	{
		const float Value = static_cast<float>(i) * StepY;
		const float Y = ToScreenY(Value);
		DL->AddLine(ImVec2(Min.x, Y), ImVec2(Max.x, Y), i == 0 ? AxisColor : GridColor);
		std::snprintf(Buffer, sizeof(Buffer), "%g", Value);
		DL->AddText(ImVec2(Min.x + EditorUIScale::Px(3.0f), Y - LineHeight), LabelColor, Buffer);
	}

	// 커브: Constant는 가로선, Uniform은 Min/Max 두 선, Curve는 키를 잇는 선(범위 밖은 끝 키 값) + 키 점
	const float Thickness = EditorUIScale::Px(1.5f);
	const float KeyHalf = EditorUIScale::Px(3.5f);
	const float MouseTime = CurveViewMinX + (IO.MousePos.x - Min.x) / Size.x * RangeX;
	const float MouseValue = CurveViewMinY + (Max.y - IO.MousePos.y) / Size.y * RangeY;
	// 마우스 아래 키, 마우스 아래 Curve 선 (Ctrl+클릭으로 키를 넣을 곳)
	int32 HoverEntry = -1, HoverChannel = 0, HoverKey = -1;
	float HoverTime = 0.0f, HoverValue = 0.0f;
	// 마우스 시간에서 세로로 가장 가까운 Curve 채널. CurvePickPixels 안이면 "선 위"로 본다.
	int32 HoverCurveEntry = -1, HoverCurveChannel = 0;
	float NearestCurvePixels = FLT_MAX;
	const float CurvePickPixels = EditorUIScale::Px(8.0f);
	const auto DrawChannel = [&](int32 EntryIndex, const auto& D, int32 Channel, ImU32 Color)
	{
		const auto DrawFlat = [&](float Value)
		{
			const float Y = ToScreenY(Value);
			DL->AddLine(ImVec2(Min.x, Y), ImVec2(Max.x, Y), Color, Thickness);
		};
		switch (D.Kind)
		{
		case EDistributionKind::Constant: DrawFlat(GetChannel(D.Constant, Channel)); break;
		case EDistributionKind::Uniform:
			DrawFlat(GetChannel(D.Min, Channel));
			DrawFlat(GetChannel(D.Max, Channel));
			break;
		case EDistributionKind::Curve:
		{
			const int32 Count = static_cast<int32>(D.Keys.Num());
			if (Count == 0) break;
			std::vector<ImVec2> Points;
			Points.reserve(static_cast<size_t>(Count) + 2);
			for (int32 k = 0; k < Count; ++k)
				Points.push_back(ImVec2(ToScreenX(D.Keys[k].Time), ToScreenY(GetChannel(D.Keys[k].Value, Channel))));
			// 첫 키 앞과 마지막 키 뒤는 끝 키 값으로 고정된다 (GetValue와 같다)
			const ImVec2 First = Points.front();
			const ImVec2 Last = Points.back();
			Points.insert(Points.begin(), ImVec2(std::min(Min.x, First.x), First.y));
			Points.push_back(ImVec2(std::max(Max.x, Last.x), Last.y));
			DL->AddPolyline(Points.data(), static_cast<int>(Points.size()), Color, ImDrawFlags_None, Thickness);
			if (bHovered)
			{
				const float Pixels = std::abs(ToScreenY(GetChannel(D.GetValue(MouseTime, nullptr), Channel)) - IO.MousePos.y);
				if (Pixels < NearestCurvePixels)
				{
					NearestCurvePixels = Pixels;
					HoverCurveEntry = EntryIndex;
					HoverCurveChannel = Channel;
				}
			}
			for (int32 k = 0; k < Count; ++k)
			{
				const ImVec2& P = Points[static_cast<size_t>(k) + 1];
				const bool bKeyHovered = bHovered && std::abs(IO.MousePos.x - P.x) <= KeyHalf + 1.0f && std::abs(IO.MousePos.y - P.y) <= KeyHalf + 1.0f;
				const bool bKeySelected = EntryIndex == SelectedCurveEntry && Channel == SelectedCurveChannel && k == SelectedCurveKey;
				DL->AddRectFilled(ImVec2(P.x - KeyHalf, P.y - KeyHalf), ImVec2(P.x + KeyHalf, P.y + KeyHalf),
					(bKeyHovered || bKeySelected) ? IM_COL32(255, 255, 255, 255) : Color);
				if (bKeySelected)
				{
					const float Ring = KeyHalf + EditorUIScale::Px(2.5f);
					DL->AddRect(ImVec2(P.x - Ring, P.y - Ring), ImVec2(P.x + Ring, P.y + Ring), IM_COL32(255, 170, 40, 255), 0.0f, 0, EditorUIScale::Px(1.5f));
				}
				if (bKeyHovered)
				{
					HoverEntry = EntryIndex;
					HoverChannel = Channel;
					HoverKey = k;
					HoverTime = D.Keys[k].Time;
					HoverValue = GetChannel(D.Keys[k].Value, Channel);
				}
			}
			break;
		}
		}
	};
	for (int32 i = 0; i < static_cast<int32>(CurveEntries.Num()); ++i)
	{
		const FCurveEntry& Entry = CurveEntries[i];
		if (!Entry.bVisible) continue;
		if (Entry.Float) DrawChannel(i, *Entry.Float, 0, Entry.Color);
		else
		{
			for (int32 Channel = 0; Channel < 3; ++Channel)
				if (Entry.bShowChannel[Channel]) DrawChannel(i, *Entry.Vector, Channel, ChannelColors[Channel]);
		}
	}
	DL->PopClipRect();
	DL->AddRect(Min, Max, IM_COL32(80, 80, 80, 255));

	// 보이는 Curve 채널 수 (0이면 키를 넣을 곳이 없다는 안내를 띄운다)
	int32 VisibleCurveCount = 0;
	for (int32 i = 0; i < static_cast<int32>(CurveEntries.Num()); ++i)
	{
		FCurveEntry& Entry = CurveEntries[i];
		if (!Entry.bVisible) continue;
		WithDistribution(Entry, [&](auto& D)
		{
			if (D.Kind != EDistributionKind::Curve) return;
			for (int32 Channel = 0; Channel < (Entry.Float ? 1 : 3); ++Channel)
			{
				if (Entry.Vector && !Entry.bShowChannel[Channel]) continue;
				++VisibleCurveCount;
			}
		});
	}
	// 키를 넣을 커브: 선 근처(CurvePickPixels 안) → 선택한 키의 커브 → 마우스 시간에서 가장 가까운 Curve
	const bool bOnCurve = HoverCurveEntry >= 0 && NearestCurvePixels <= CurvePickPixels;
	int32 AddEntry = -1, AddChannel = 0;
	if (bOnCurve) { AddEntry = HoverCurveEntry; AddChannel = HoverCurveChannel; }
	else if (SelectedCurveEntry >= 0) { AddEntry = SelectedCurveEntry; AddChannel = SelectedCurveChannel; }
	else if (HoverCurveEntry >= 0) { AddEntry = HoverCurveEntry; AddChannel = HoverCurveChannel; }

	// 키 추가: 선 위면 그 시간의 곡선 값(모양 유지), 선 밖이면 그 채널만 마우스 높이 값으로 넣고 선택한다
	const auto AddKeyAt = [&](int32 EntryIndex, int32 Channel, float Time, float Value, bool bKeepShape)
	{
		float Lo = -FLT_MAX, Hi = FLT_MAX;
		TimeLimits(EntryIndex, Lo, Hi);
		Time = std::clamp(Time, Lo, Hi);
		ActiveCurveEntry = EntryIndex;
		WithDistribution(CurveEntries[EntryIndex], [&](auto& D)
		{
			auto NewValue = D.GetValue(Time, nullptr);
			if (!bKeepShape) SetChannel(NewValue, Channel, std::max(Value, CurveEntries[EntryIndex].MinValue));
			D.AddKey(Time, NewValue);
			int32 NewKey = 0;
			while (NewKey + 1 < static_cast<int32>(D.Keys.Num()) && D.Keys[NewKey].Time < Time) ++NewKey;
			SelectedCurveEntry = EntryIndex;
			SelectedCurveChannel = Channel;
			SelectedCurveKey = NewKey;
		});
		bDraggingCurveKey = false;
		OnCurveKeysChanged();
	};

	// 왼쪽 클릭: 키 선택과 드래그 시작, Ctrl이면 키 추가, 빈 곳이면 선택 해제
	if (bHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		if (HoverKey >= 0)
		{
			SelectedCurveEntry = HoverEntry;
			SelectedCurveChannel = HoverChannel;
			SelectedCurveKey = HoverKey;
			ActiveCurveEntry = HoverEntry;
			bDraggingCurveKey = true;
			bCurveKeyMoved = false;
		}
		else if (IO.KeyCtrl && AddEntry >= 0) AddKeyAt(AddEntry, AddChannel, MouseTime, MouseValue, bOnCurve);
		else ClearCurveKeySelection();
	}

	// 우클릭(드래그 없이 놓기): Add Key / Delete Key 메뉴. 우클릭 드래그는 팬이다.
	if (bHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) CurveMenuPressPos = IO.MousePos;
	if (bHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
		std::abs(IO.MousePos.x - CurveMenuPressPos.x) + std::abs(IO.MousePos.y - CurveMenuPressPos.y) < EditorUIScale::Px(4.0f))
	{
		CurveMenuTime = MouseTime;
		CurveMenuValue = MouseValue;
		CurveMenuEntry = AddEntry;
		CurveMenuChannel = AddChannel;
		bCurveMenuOnCurve = bOnCurve;
		CurveMenuKeyEntry = HoverEntry;
		CurveMenuKeyChannel = HoverChannel;
		CurveMenuKey = HoverKey;
		ImGui::OpenPopup("##CurveMenu");
	}
	if (ImGui::BeginPopup("##CurveMenu"))
	{
		const bool bCanAdd = CurveMenuEntry >= 0 && CurveMenuEntry < static_cast<int32>(CurveEntries.Num());
		if (ImGui::MenuItem("Add Key", nullptr, false, bCanAdd))
			AddKeyAt(CurveMenuEntry, CurveMenuChannel, CurveMenuTime, CurveMenuValue, bCurveMenuOnCurve);
		if (!bCanAdd) ImGui::SetItemTooltip("No Curve to add a key to. Set Distribution to Curve in Details (Constant / Uniform have no keys).");
		const bool bCanDelete = CurveMenuKey >= 0 && CurveMenuKeyEntry < static_cast<int32>(CurveEntries.Num());
		if (ImGui::MenuItem("Delete Key", "Delete", false, bCanDelete))
		{
			WithDistribution(CurveEntries[CurveMenuKeyEntry], [&](auto& D)
			{
				if (CurveMenuKey < static_cast<int32>(D.Keys.Num())) D.Keys.RemoveAt(static_cast<uint32>(CurveMenuKey));
			});
			ClearCurveKeySelection();
			OnCurveKeysChanged();
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Fit")) bCurveFitPending = true;
		ImGui::EndPopup();
	}

	// 키를 넣을 수 있는 커브가 없으면 이유를 보여준다 (Constant / Uniform은 키가 없다)
	if (VisibleCurveCount == 0)
	{
		const char* Hint = CurveEntries.Num() == 0
			? "Click the curve icon on a module to add its values here."
			: "Set Distribution to Curve in Details to add keys.";
		const ImVec2 HintSize = ImGui::CalcTextSize(Hint);
		DL->AddText(ImVec2(Min.x + (Size.x - HintSize.x) * 0.5f, Min.y + EditorUIScale::Px(8.0f)), ImGui::GetColorU32(ImGuiCol_TextDisabled), Hint);
	}

	// 드래그: 키를 마우스 위치로 옮긴다. 시간은 이웃 키 사이로 묶어 순서를 지킨다.
	if (bDraggingCurveKey)
	{
		if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && SelectedCurveEntry >= 0)
		{
			if (IO.MouseDelta.x != 0.0f || IO.MouseDelta.y != 0.0f)
			{
				WithDistribution(CurveEntries[SelectedCurveEntry], [&](auto& D)
				{
					auto& Keys = D.Keys;
					const int32 k = SelectedCurveKey;
					float Lo = (k > 0) ? Keys[k - 1].Time : -FLT_MAX;
					float Hi = (k + 1 < static_cast<int32>(Keys.Num())) ? Keys[k + 1].Time : FLT_MAX;
					TimeLimits(SelectedCurveEntry, Lo, Hi);
					Keys[k].Time = std::clamp(MouseTime, Lo, Hi);
					SetChannel(Keys[k].Value, SelectedCurveChannel, std::max(MouseValue, CurveEntries[SelectedCurveEntry].MinValue));
				});
				bCurveKeyMoved = true;
			}
		}
		else
		{
			// 놓을 때 한 번만 재시작한다 (드래그 중에는 지금 시뮬레이션이 바뀐 값을 바로 쓴다)
			if (bCurveKeyMoved) OnCurveKeysChanged();
			bDraggingCurveKey = false;
			bCurveKeyMoved = false;
		}
	}

	// Delete: 선택한 키 삭제 (이 패널에 포커스가 있고 입력 중인 칸이 없을 때)
	if (SelectedCurveEntry >= 0 && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsAnyItemActive() &&
		ImGui::IsKeyPressed(ImGuiKey_Delete, false))
	{
		WithDistribution(CurveEntries[SelectedCurveEntry], [&](auto& D) { D.Keys.RemoveAt(static_cast<uint32>(SelectedCurveKey)); });
		ClearCurveKeySelection();
		OnCurveKeysChanged();
	}

	// 키에 마우스를 올리면 시간과 값을 보여준다 (드래그 중에는 숨긴다)
	if (HoverKey >= 0 && !bDraggingCurveKey)
	{
		const FCurveEntry& Entry = CurveEntries[HoverEntry];
		if (Entry.bLife)
		{
			if (Entry.Vector) ImGui::SetTooltip("%s.%s\nLife: %.1f%%\nValue: %.3f", Entry.Label.c_str(), ChannelNames[HoverChannel], HoverTime * 100.0f, HoverValue);
			else ImGui::SetTooltip("%s\nLife: %.1f%%\nValue: %.3f", Entry.Label.c_str(), HoverTime * 100.0f, HoverValue);
		}
		else if (Entry.Vector) ImGui::SetTooltip("%s.%s\nTime: %.3f\nValue: %.3f", Entry.Label.c_str(), ChannelNames[HoverChannel], HoverTime, HoverValue);
		else ImGui::SetTooltip("%s\nTime: %.3f\nValue: %.3f", Entry.Label.c_str(), HoverTime, HoverValue);
	}
	ImGui::EndGroup();
}
