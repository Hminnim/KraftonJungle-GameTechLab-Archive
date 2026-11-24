#include "EnginePCH.h"
#include "Core/Misc/Paths.h"
#include "UI/ProgressBar.h"
#include "Editor/HitoriEd/ComponentReregisterContext.h"
#include "Editor/HitoriEd/EditorResourcePaths.h"
#include "Editor/EditorUI/EditorUIScale.h"
#include "Editor/EditorUI/EditorSort.h"
#include "Editor/Details/DetailsPanel.h"
#include "Editor/Details/PropertyEditor.h"

#include "imgui_internal.h"
#include "Editor/HitoriEd/EditorDragDrop.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/ParticleSystemComponent.h"
#include "Render/SkeletalMesh.h"
#include "Components/TextRenderComponent.h"
#include "Components/ParticleSubUVComponent.h"
#include "Components/SpotLightComponent.h"
#include "Editor/EditorUI/ImGuiRenderer.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SkyLightComponent.h"

#include "Components/RotatingMovementComponent.h"
#include "Components/ProjectileMovementComponent.h"
#include "Components/DecalComponent.h"
#include "Components/ScriptComponent.h"
#include "Asset/AssetManager.h"
#include "Asset/FbxImporter/FbxPath.h"
#include "Render/Material.h"
#include "Render/Texture2D.h"
#include "Text/Font.h"
#include "GameFramework/Actor.h"
#include "Core/EngineLog.h"

#include "Editor/HitoriEd/EditorFileUtils.h"
#include "Scripting/LuaScriptManager.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimGraphInstance.h"
#include "Asset/AnimGraphAsset.h"
#include "UObject/UObjectHash.h"


namespace
{
	bool EditLinearColor(const char* Label, float* Value, bool Alpha = false)
	{
		float Display[4] = { std::pow(std::max(Value[0], 0.0f), 1.0f / 2.2f),
			std::pow(std::max(Value[1], 0.0f), 1.0f / 2.2f), std::pow(std::max(Value[2], 0.0f), 1.0f / 2.2f), Alpha ? Value[3] : 1.0f };
		const float Previous[3] = { Display[0], Display[1], Display[2] };
		const auto Flags = ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float;
		if (!(Alpha ? ImGui::ColorEdit4(Label, Display, Flags) : ImGui::ColorEdit3(Label, Display, Flags))) return false;
		for (int I = 0; I < 3; ++I)
			if (Display[I] != Previous[I]) Value[I] = std::pow(std::max(Display[I], 0.0f), 2.2f);
		if (Alpha) Value[3] = Display[3];
		return true;
	}
	// Details 패널 드래그의 마우스 1픽셀당 변화량. 헤더의 Step 콤보로 바꾼다 (TEAM1).
	// 드래그 중 Shift는 10배, Alt는 1/100배(ImGui 기본)이고 Ctrl+클릭은 값을 직접 입력한다.
	float DragStep = 0.1f;
	constexpr float DragStepOptions[] = { 1.0f, 0.1f, 0.01f, 0.001f };
	constexpr const char* DragStepLabels[] = { "1", "0.1", "0.01", "0.001" };

	// 단계보다 한 자리 더 표시해 단계만큼 바뀐 값이 표시 반올림으로 사라지지 않게 한다.
	const char* GetDragFormat(const int32 MinDecimals = 2)
	{
		static constexpr const char* Formats[] = { "%.0f", "%.1f", "%.2f", "%.3f", "%.4f", "%.5f" };
		const int32 StepDecimals = static_cast<int32>(std::lround(-std::log10(DragStep)));
		const int32 Decimals = std::clamp(std::max(MinDecimals, StepDecimals + 1), 0, 5);
		return Formats[Decimals];
	}

	// 헤더 오른쪽의 드래그 단계 콤보
	void DrawDragStepCombo()
	{
		int32 Current = 1;
		for (int32 Index = 0; Index < IM_ARRAYSIZE(DragStepOptions); ++Index)
			if (std::abs(DragStepOptions[Index] - DragStep) < 1.0e-6f) Current = Index;
		ImGui::SetNextItemWidth(ImGui::CalcTextSize("0.001").x + ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.x * 2.0f);
		if (ImGui::Combo("Drag Step", &Current, DragStepLabels, IM_ARRAYSIZE(DragStepLabels)))
			DragStep = DragStepOptions[Current];
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Value change per pixel while dragging. Shift = x10, Alt = x0.01, Ctrl+Click = type a value");
	}
	// 슬래시(/) 경로의 UTF-8 문자열. 씬에 저장되는 ScriptPath 형식(MakeScriptPath)과 맞춘다.
	FString ToGenericUtf8(const std::filesystem::path& Path)
	{
		const std::u8string U8 = Path.generic_u8string();
		return FString(U8.begin(), U8.end());
	}

	// 파일 이름 또는 경로를 Assets/Scripts 아래 경로로 바꾼다. 밖의 파일은 받지 않는다 (TEAM5).
	// 폴더 없이 이름만 주면 Assets/Scripts 안에서 같은 이름을 찾고, 없으면 Assets/Scripts 바로 아래로 정한다.
	FString ResolveScriptPath(const FString& Input)
	{
		namespace fs = std::filesystem;
		if (Input.empty())
		{
			HTR_LOG(Warning, "[Lua] Script file name cannot be empty");
			return {};
		}

		const fs::path ScriptRoot = FLuaScriptManager::ToPath(ContentPaths::ScriptFolder);
		fs::path Requested = FLuaScriptManager::ToPath(Input);
		if (Requested.extension() != ".lua")
			Requested.replace_extension(".lua");
		Requested = Requested.lexically_normal();

		std::error_code Ec;
		if (!Requested.has_parent_path())
		{
			for (fs::recursive_directory_iterator It(ScriptRoot, Ec), End; !Ec && It != End; It.increment(Ec))
			{
				if (It->is_regular_file(Ec) && It->path().filename() == Requested)
					return ToGenericUtf8(It->path().lexically_normal());
			}
			return ToGenericUtf8((ScriptRoot / Requested));
		}

		const fs::path AbsoluteRoot = fs::weakly_canonical(ScriptRoot, Ec);
		const fs::path AbsoluteFile = fs::weakly_canonical(Requested.is_absolute() ? Requested : fs::absolute(Requested, Ec), Ec);
		const fs::path Relative = AbsoluteFile.lexically_relative(AbsoluteRoot);
		if (Ec || Relative.empty() || *Relative.begin() == "..")
		{
			HTR_LOG(Warning, "[Lua] Script must be inside {}: {}", ContentPaths::ScriptFolder, Input);
			return {};
		}
		return ToGenericUtf8((ScriptRoot / Relative));
	}

	// 클래스별 접이식 헤더 안에 표시하는 Lua 스크립트 연결 UI.
	void DrawScriptSection(UScriptComponent* Script)
	{
		static UScriptComponent* EditingScript = nullptr;
		static FString EditingPath;
		static char FileNameBuffer[256] = {};

		// 다른 스크립트 컴포넌트를 고르거나 경로가 바뀌었을 때만 입력칸을 적용된 파일 이름으로 맞춘다.
		if (EditingScript != Script || EditingPath != Script->GetScriptPath())
		{
			EditingScript = Script;
			EditingPath = Script->GetScriptPath();
			const FString FileName = FLuaScriptManager::FromPath(FLuaScriptManager::ToPath(Script->GetScriptPath()).filename());
			strncpy_s(FileNameBuffer, sizeof(FileNameBuffer), FileName.c_str(), _TRUNCATE);
		}

		const auto Assign = [Script](const FString& Input)
		{
			const FString Resolved = ResolveScriptPath(Input);
			if (!Resolved.empty())
				Script->SetScriptPath(Resolved);   // UPROPERTY라 씬 저장 때 같이 저장된다
		};

		if (!Script->GetScriptPath().empty())
			ImGui::TextWrapped("File: %s", Script->GetScriptPath().c_str());
		else
			ImGui::TextDisabled("No script assigned.");

		const float ButtonsWidth = EditorUIScale::ButtonWidth("Apply") + EditorUIScale::ButtonWidth("Browse...") + ImGui::GetStyle().ItemSpacing.x * 2.0f;
		ImGui::SetNextItemWidth(-ButtonsWidth);
		const bool bEnterPressed = ImGui::InputTextWithHint("##ScriptFileName", "script name", FileNameBuffer, sizeof(FileNameBuffer),
			ImGuiInputTextFlags_EnterReturnsTrue);

		// Content Drawer에서 끌어 온 .lua 파일을 받는다.
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(EditorDragDrop::ScriptFile))
				Assign(static_cast<const char*>(Payload->Data));
			ImGui::EndDragDropTarget();
		}
		if (ImGui::IsItemHovered() && !ImGui::IsItemActive())
			ImGui::SetTooltip("Type a file name, or drag a .lua file from the Content Drawer.");

		ImGui::SameLine();
		const bool bApplyClicked = ImGui::Button("Apply");
		ImGui::SameLine();
		if (ImGui::Button("Browse..."))
		{
			std::error_code Ec;
			const FString Initial = std::filesystem::absolute(ContentPaths::ScriptFolder, Ec).string();
			const FString Selected = FEditorFileUtils::OpenScriptDialog(Initial);
			// 대화상자는 ANSI 경로를 돌려주므로 path로 받아 UTF-8로 바꾼다.
			if (!Selected.empty())
				Assign(FLuaScriptManager::FromPath(std::filesystem::path(Selected)));
		}

		if (bEnterPressed || bApplyClicked)
			Assign(FileNameBuffer);

		// Edit Script(남은 폭 전부) | Detach
		const bool bAssigned = !Script->GetScriptPath().empty();
		const float EditWidth = bAssigned
			? -(EditorUIScale::ButtonWidth("Detach") + ImGui::GetStyle().ItemSpacing.x)
			: -FLT_MIN;
		if (ImGui::Button("Edit Script", ImVec2(EditWidth, 0.0f)))
		{
			// 아직 연결된 스크립트가 없으면 "씬이름_액터이름_컴포넌트이름.lua"로 정한다 (한 액터에 여러 개가 붙을 수 있어서).
			if (!bAssigned)
			{
				const FString& ScenePath = FEditorFileUtils::CurrentScenePath;
				if (ScenePath.empty())
				{
					HTR_LOG(Warning, "[Lua] Save the scene first. The scene name will be included in the script name.");
				}
				else
				{
					const FString SceneName = FLuaScriptManager::FromPath(FLuaScriptManager::ToPath(ScenePath).stem());
					AActor* Owner = Script->GetOwner();
					const FString OwnerName = Owner ? Owner->GetName() : FString("Actor");
					Script->SetScriptPath(FLuaScriptManager::MakeScriptPath(SceneName, OwnerName + "_" + Script->GetName()));
				}
			}
			// 파일이 없으면 template.lua로 만들고, 있으면 그대로 연다.
			if (!Script->GetScriptPath().empty() && FLuaScriptManager::Get().CreateScriptFromTemplate(Script->GetScriptPath()))
				FLuaScriptManager::Get().OpenScriptInEditor(Script->GetScriptPath());
		}
		if (ImGui::IsItemHovered() && !bAssigned)
			ImGui::SetTooltip("Creates <Scene>_<Actor>_<Component>.lua from the template and opens it.");

		if (bAssigned)
		{
			ImGui::SameLine();
			if (ImGui::Button("Detach"))
				Script->SetScriptPath(FString());   // 연결만 끊고 파일은 지우지 않는다
		}
	}

	bool DrawAxisControl(const FString& _label, float& _value, float _speed, float _minValue, float _maxValue, float _resetValue, FVector4 _color)
	{
		bool isValueChanged = false;

		// 배율이 적용된 프레임 높이를 써야 입력칸과 버튼 높이가 맞는다 (TEAM5).
		float lineHeight = ImGui::GetFrameHeight();
		ImVec4 color(_color.X, _color.Y, _color.Z, _color.W);
		ImVec2 buttonSize = { lineHeight + 2.0f , lineHeight };

		// 각 버튼의 색상 설정
		ImGui::PushStyleColor(ImGuiCol_Button, color);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ color.x + 0.1f, color.y + 0.1f, color.z + 0.1f, 1.0f });
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ color.x - 0.1f, color.y - 0.1f, color.z - 0.1f, 1.0f });

		// 굵은 폰트 적용
		if (ImGui::Button(_label.c_str(), buttonSize))
		{
			_value = _resetValue;
			isValueChanged = true;
		}
		ImGui::PopStyleColor(3);

		// 드래그 슬라이더
		ImGui::SameLine();
		std::string dragID = "##" + _label;
		isValueChanged |= ImGui::DragFloat(dragID.c_str(), &_value, _speed, _minValue, _maxValue, GetDragFormat());

		return isValueChanged;

	}

	bool DrawVector3Controller(const FString& _label, float* _values, float _resetValue)
	{
		ImGuiIO& io = ImGui::GetIO();
		auto boldFont = io.Fonts->Fonts[0];

		// 저장된 열 폭이 있으면 초기 폭을 무시하므로 저장하지 않는다 (TEAM5).
		ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings;

		bool isVectorChanged = false;

		ImGui::PushID(_label.c_str());
		if (ImGui::BeginTable(_label.c_str(), 2, flags)) // 고유 ID, 열 2개, 플래그
		{
			// ImGuiTableColumnFlags_WidthFixed: 초기 너비 고정
			// ImGuiTableColumnFlags_WidthStretch: 창 크기에 따라 너비 조절 (기본값)
			ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Location").x);
			ImGui::TableSetupColumn("Content", ImGuiTableColumnFlags_WidthStretch);

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(_label.c_str()); // 왼쪽 열: 레이블

			ImGui::TableSetColumnIndex(1);
			// [컨트롤러 UI 코드] // 오른쪽 열: 컨트롤러
			// 축 버튼 3개와 그 사이 간격(5칸)을 뺀 폭을 입력칸 3개가 나눠 쓴다 (TEAM5).
			const float AxisButtonWidth = ImGui::GetFrameHeight() + 2.0f;
			const float AxisSpacing = EditorUIScale::Px(2.0f);
			ImGui::PushMultiItemsWidths(3, ImGui::GetContentRegionAvail().x - AxisButtonWidth * 3.0f - AxisSpacing * 5.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ AxisSpacing, 0 });
			ImGui::PushFont(boldFont);

			isVectorChanged |= DrawAxisControl("X", _values[0], DragStep, 0.0f, 0.0f, _resetValue, { 0.8f, 0.1f, 0.1f, 1.0f });

			ImGui::PopItemWidth();

			ImGui::SameLine();
			isVectorChanged |= DrawAxisControl("Y", _values[1], DragStep, 0.0f, 0.0f, _resetValue, { 0.1f, 0.8f, 0.1f, 1.0f });

			ImGui::PopItemWidth();

			ImGui::SameLine();
			isVectorChanged |= DrawAxisControl("Z", _values[2], DragStep, 0.0f, 0.0f, _resetValue, { 0.1f, 0.1f, 0.8f, 1.0f });

			ImGui::PopItemWidth();

			ImGui::PopFont();
			ImGui::PopStyleVar();
			ImGui::EndTable();
		}

		ImGui::PopID();

		return isVectorChanged;
	}

	// FRotator의 V는 (Pitch, Yaw, Roll) 순서지만 엔진은 Roll=X, Pitch=Y, Yaw=Z축이므로
	// X/Y/Z 칸이 실제 회전축과 맞도록 재배열해 편집한다.
	bool DrawRotatorAsXYZ(const FString& _label, FRotator& _rotator)
	{
		float Euler[3] = { _rotator.Roll, _rotator.Pitch, _rotator.Yaw };
		const bool isChanged = DrawVector3Controller(_label, Euler, 0.0f);
		if (isChanged)
		{
			_rotator.Roll = Euler[0];
			_rotator.Pitch = Euler[1];
			_rotator.Yaw = Euler[2];
		}
		return isChanged;
	}

	void DrawTextureSlot(UMaterial** MaterialPtr, int32 SlotIndex)
	{
		const float ThumbnailSize = EditorUIScale::Px(64.0f);

		UMaterial* Material = *MaterialPtr;
		UTexture2D* Texture = nullptr;
		if (Material && SlotIndex < static_cast<int32>(Material->Textures.Num()))
		{
			Texture = Material->Textures[SlotIndex];
		}

		ImGui::PushID(SlotIndex);

		if (Texture && Texture->GetResource())
		{
			ImGui::Image(
				(ImTextureID)Texture->GetResource()->GetSRV(),
				{ ThumbnailSize, ThumbnailSize });
		}
		else
		{
			ImGui::Button("Empty", { ThumbnailSize, ThumbnailSize });
		}

		// 바로 위 위젯이 드롭을 받는다
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(EditorDragDrop::Texture))
			{
				UTexture2D* DroppedTexture = *static_cast<UTexture2D**>(Payload->Data);

				// 머티리얼이 없으면 기본 머티리얼에서 시작한다
				if (!Material)
				{
					Material = UMaterial::CreateInstance(UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial"));
					*MaterialPtr = Material;
				}
				// 공유 에셋이면 지금 복제한다.
				// 이 시점까지 미루면 손대지 않은 오브젝트는 계속 공유 에셋을 쓴다.
				else if (Material->bIsInstance == false)
				{
					Material = UMaterial::CreateInstance(Material);
					*MaterialPtr = Material;
				}

				if (Material)
				{
					// 슬롯이 배열 범위 밖이면 그 자리까지 늘려 준다
					while (static_cast<int32>(Material->Textures.Num()) <= SlotIndex)
					{
						Material->Textures.Add(nullptr);
					}

					Material->Textures[SlotIndex] = DroppedTexture;
					Texture = DroppedTexture;
				}
			}
			ImGui::EndDragDropTarget();
		}

		ImGui::SameLine();

		ImGui::BeginGroup();
		ImGui::Text("Slot %d", SlotIndex);

		if (Texture)
		{
			ImGui::TextWrapped("%s", Texture->GetPath().c_str());
		}
		else
		{
			ImGui::TextDisabled("(비어 있음)");
		}
		ImGui::EndGroup();

		ImGui::PopID();
	}

	void DrawDecalMaterial(UDecalComponent* Decal)
	{
		if (!Decal)
			return;

		ImGui::PushID(Decal);

		if (ImGui::CollapsingHeader(
			"Decal Material",
			ImGuiTreeNodeFlags_DefaultOpen))
		{
			UMaterial* Material = Decal->GetDecalMaterial();

			const UMaterial* Base =
				Material ? Material->GetBaseAsset() : nullptr;

			const FString Preview =
				Base ? Base->GetPath() :
				Material ? FString("(Instance)") :
				FString("(None)");

			EditorUIScale::LimitNextComboPopupWidth();
			if (ImGui::BeginCombo(
				"Material",
				Preview.c_str()))
			{
				if (ImGui::Selectable(
					"(None)",
					Material == nullptr))
				{
					Decal->SetDecalMaterial(nullptr);
				}

				for (const FString& Path : UAssetManager::GetAssetPaths(EAssetType::Material))
				{
					const bool bSelected = (Material && Path == Material->GetPath()) || (Base && Path == Base->GetPath());
					if (ImGui::Selectable(Path.c_str(), bSelected))
						if (auto* Candidate = UAssetManager::GetAssetByPath<UMaterial>(Path)) Decal->SetDecalMaterial(Candidate);
				}

				ImGui::EndCombo();
			}

			Material = Decal->GetDecalMaterial();

			// 기존 함수는 텍스처 변경 시 공유 에셋을 복제해
			// 컴포넌트 전용 머티리얼 인스턴스로 바꾼다.
			ImGui::TextUnformatted("Base Color");
			DrawTextureSlot(&Material, 0);

			ImGui::TextUnformatted("Normal Map");
			DrawTextureSlot(&Material, 1);

			Decal->SetDecalMaterial(Material);

			if (Material)
			{
				FVector4 Color = Material->BaseColor;

				if (ImGui::ColorEdit4(
					"Material BaseColor",
					&Color.X))
				{
					if (!Material->bIsInstance)
					{
						Material =
							UMaterial::CreateInstance(Material);
					}

					if (Material)
					{
						Material->BaseColor = Color;
						Decal->SetDecalMaterial(Material);
					}
				}

				if (ImGui::Button("Clear Material"))
				{
					Decal->SetDecalMaterial(nullptr);
				}
			}
		}

		ImGui::PopID();
	}

	FString GetAssetDisplayName(const FString& Path)
	{
		fs::path AssetPath = FbxPath::FromString(Path);
		if (AssetPath.stem() == "Atlas")
		{
			return FbxPath::ToString(AssetPath.parent_path().filename());
		}
		return FbxPath::ToString(AssetPath.stem());
	}

	TArray<FString> GetSortedAssetPaths(EAssetType Type)
	{
		TArray<FString> Paths = UAssetManager::GetAssetPaths(Type);
		EditorSort::SortByName(Paths, [](const FString& Path) { return GetAssetDisplayName(Path); });
		return Paths;
	}

	template <typename T, typename ApplyType>
	void DrawAssetChoices(EAssetType Type, T* Current, ApplyType Apply)
	{
		for (const FString& Path : GetSortedAssetPaths(Type))
		{
			const bool bSelected = Current && UAssetManager::FindLoadedAsset<T>(Path) == Current;
			ImGui::PushID(Path.c_str());
			if (ImGui::Selectable(GetAssetDisplayName(Path).c_str(), bSelected) && !bSelected)
				if (T* Asset = UAssetManager::GetAssetByPath<T>(Path)) Apply(Asset);
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Path.c_str());
			if (bSelected) ImGui::SetItemDefaultFocus();
			ImGui::PopID();
		}
	}

	template <typename T, typename ApplyType>
	void DrawAssetSlot(const char* Label, EAssetType Type, T* Current, ApplyType Apply)
	{
		const FString CurrentPath = Current ? Current->GetPath() : FString();
		const FString DisplayName = Current ? (CurrentPath.empty() ? Current->GetName() : GetAssetDisplayName(CurrentPath)) : "None";
		ImGui::SetNextItemWidth(-1.0f);
		EditorUIScale::LimitNextComboPopupWidth();
		const bool bOpen = ImGui::BeginCombo(Label, DisplayName.c_str());
		if (ImGui::IsItemHovered() && !CurrentPath.empty()) ImGui::SetTooltip("%s", CurrentPath.c_str());
		if (bOpen)
		{
			DrawAssetChoices(Type, Current, Apply);
			ImGui::EndCombo();
		}
	}

	UMaterial* EnsureMaterialOverride(UMeshComponent* MeshComponent, int32 Slot, UMaterial* Effective, UMaterial* Override)
	{
		// 이 컴포넌트 전용 인스턴스가 아니면 지금 적용 중인 머티리얼을 복제해서 덮어쓰기로 만든다.
		// 메시의 머티리얼은 같은 메시를 쓰는 모든 액터가 공유하므로 직접 고치면 안 된다.
		if (Override && Override->bIsInstance)	return Override;

		UMaterial* Source = Effective ? Effective : UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");

		Override = UMaterial::CreateInstance(Source);
		MeshComponent->SetMaterial(Slot, Override);

		return Override;
	}

	bool SetMaterialTexture(UMeshComponent* MeshComponent, int32 MaterialSlot, int32 TextureSlot, UTexture2D* Texture, UMaterial*& Effective, UMaterial*& Override) {
		UTexture2D* Current{ Effective && Effective->Textures.IsValidIndex(TextureSlot) ? Effective->Textures[TextureSlot] : nullptr };
		if (Current == Texture) {
			return false;
		}

		Override = EnsureMaterialOverride(MeshComponent, MaterialSlot, Effective, Override);
		if (!Override) {
			return false;
		}

		while (!Override->Textures.IsValidIndex(TextureSlot)) {
			Override->Textures.Add(nullptr);
		}
		Override->Textures[TextureSlot] = Texture;
		Effective = Override;
		return true;
	}

	void DrawMaterialTextureSlot(UMeshComponent* MeshComponent, int32 MaterialSlot, int32 TextureSlot, const char* Label, UMaterial*& Effective, UMaterial*& Override) {
		const float ThumbnailSize{ EditorUIScale::Px(64.0f) };
		const float ItemThumbnailSize{ EditorUIScale::Px(24.0f) };
		UTexture2D* Texture{ Effective && Effective->Textures.IsValidIndex(TextureSlot) ? Effective->Textures[TextureSlot] : nullptr };

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::TextUnformatted(Label);
		ImGui::TableSetColumnIndex(1);
		ImGui::PushID(TextureSlot);

		if (Texture && Texture->GetResource()) {
			ImGui::Image((ImTextureID)Texture->GetResource()->GetSRV(), { ThumbnailSize, ThumbnailSize });
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("%s", Texture->GetPath().c_str());
			}
		} else {
			ImGui::Button("Empty", { ThumbnailSize, ThumbnailSize });
		}

		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* Payload{ ImGui::AcceptDragDropPayload(EditorDragDrop::Texture) }) {
				UTexture2D* DroppedTexture{ *static_cast<UTexture2D**>(Payload->Data) };
				if (SetMaterialTexture(MeshComponent, MaterialSlot, TextureSlot, DroppedTexture, Effective, Override)) {
					Texture = DroppedTexture;
				}
			}
			ImGui::EndDragDropTarget();
		}

		const FString CurrentPath{ Texture ? Texture->GetPath() : "" };
		const FString CurrentFileName{ Texture ? GetAssetDisplayName(CurrentPath) : "None" };
		ImGui::SetNextItemWidth(-1.0f);
		EditorUIScale::LimitNextComboPopupWidth();
		const bool Open{ ImGui::BeginCombo("##Texture", CurrentFileName.c_str()) };
		if (ImGui::IsItemHovered() && Texture) {
			ImGui::SetTooltip("%s", CurrentPath.c_str());
		}

		if (Open) {
			if (ImGui::Selectable("None", Texture == nullptr)) {
				SetMaterialTexture(MeshComponent, MaterialSlot, TextureSlot, nullptr, Effective, Override);
			}

			for (const FString& ItemPath : GetSortedAssetPaths(EAssetType::Texture))
			{
				UTexture2D* Loaded = UAssetManager::FindLoadedAsset<UTexture2D>(ItemPath);
				const bool bSelected = Loaded && Loaded == Texture;
				ImGui::PushID(ItemPath.c_str());
				if (Loaded && Loaded->GetResource())
					ImGui::Image((ImTextureID)Loaded->GetResource()->GetSRV(), { ItemThumbnailSize, ItemThumbnailSize });
				else
					ImGui::Dummy({ ItemThumbnailSize, ItemThumbnailSize });
				ImGui::SameLine();
				if (ImGui::Selectable(GetAssetDisplayName(ItemPath).c_str(), bSelected, ImGuiSelectableFlags_None, { 0.0f, ItemThumbnailSize }))
					if (auto* Item = UAssetManager::GetAssetByPath<UTexture2D>(ItemPath))
						SetMaterialTexture(MeshComponent, MaterialSlot, TextureSlot, Item, Effective, Override);
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", ItemPath.c_str());
				if (bSelected) ImGui::SetItemDefaultFocus();
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}

		ImGui::PopID();
	}

	const char* GetComponentRenameHint(const UActorComponent* Component, bool bCanRename)
	{
		if (bCanRename)
			return "Rename: double-click the name, press F2, or use the context menu.";
		if (!Component->IsUserCreated() || Component->HasAnyFlags(EObjectFlags::RF_DefaultSubObject))
			return "Rename unavailable. The scene loader matches class-defined components by name.";
		if (const AActor* Owner = Component->GetOwner(); Owner && Owner->GetRootComponent() == Component)
			return "Rename unavailable for the current root component.";
		if (!Cast<USceneComponent>(Component))
			return "Rename unavailable for non-scene components in Details.";
		return "Rename unavailable for this component.";
	}

	void DrawComponentRowTooltip(const UActorComponent* Component, bool bCanRename)
	{
		if (ImGui::GetDragDropPayload() || !ImGui::BeginItemTooltip())
			return;

		const AActor* Owner = Component->GetOwner();
		const bool bDefaultSubobject = Component->HasAnyFlags(EObjectFlags::RF_DefaultSubObject);
		ImGui::TextUnformatted(bDefaultSubobject ? "Default subobject: Yes" : "Default subobject: No");
		ImGui::TextUnformatted(GetComponentRenameHint(Component, bCanRename));
		if (Owner && !Owner->CanDeleteComponentInEditor(Component))
			ImGui::TextUnformatted("This component cannot be deleted in the editor.");
		if (Owner && Owner->GetRootComponent() == Component)
		{
			if (Component->IsEditorPlaceholderRoot())
				ImGui::TextUnformatted("This fallback root is removed when another component is made the root.");
			else if (bDefaultSubobject || !Component->IsUserCreated())
				ImGui::TextUnformatted("This class-defined root cannot be replaced in the editor.");
		}
		ImGui::EndTooltip();
	}

	// 메시 컴포넌트의 머티리얼 슬롯을 한 줄씩 그린다.
	// 텍스처를 드롭하면 그 슬롯의 덮어쓰기에 반영하고, 메시 에셋의 머티리얼은 건드리지 않는다.
	void DrawMaterialSlots(UMeshComponent* MeshComponent)
	{
		const int32 NumSlots = MeshComponent->GetNumMaterials();
		if (NumSlots <= 0)
		{
			return;
		}

		if (!ImGui::CollapsingHeader("Materials", ImGuiTreeNodeFlags_DefaultOpen))
		{
			return;
		}

		// 메쉬 내의 매터리얼 별 Slot 순회
		for (int32 Slot = 0; Slot < NumSlots; ++Slot)
		{
			ImGui::PushID(Slot);

			FString SlotLabel = std::format("[{}] {}", Slot, MeshComponent->GetMaterialSlotName(Slot));
			bool bOpen = ImGui::TreeNode(SlotLabel.c_str());

			UMaterial* Override = MeshComponent->GetOverrideMaterial(Slot);
			UMaterial* Effective = MeshComponent->GetMaterial(Slot);

			ImGui::SameLine();

			if (Override)
			{
				ImGui::TextDisabled("Override");
				ImGui::SameLine();

				if (ImGui::SmallButton("Reset"))
				{
					MeshComponent->SetMaterial(Slot, nullptr);
					Override = nullptr;
					Effective = MeshComponent->GetMaterial(Slot);
				}
			}
			else
			{
				ImGui::TextDisabled("Default");
			}

			if (bOpen)
			{
				if (ImGui::BeginTable("MaterialProperties", 2))
				{
					ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Override Shading").x);
					ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

					DrawMaterialTextureSlot(MeshComponent, Slot, 0, "Texture", Effective, Override);
					DrawMaterialTextureSlot(MeshComponent, Slot, 1, "Normal Map", Effective, Override);

					ImGui::TableNextRow();

					// 3: Base Color
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("Base Color");

					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-1.0f);

					FVector4 TempColor = Effective ? Effective->BaseColor : FVector4(1.0f, 1.0f, 1.0f, 1.0f);
					if (EditLinearColor("##BaseColor", &TempColor.X, true))
					{
						Override = EnsureMaterialOverride(MeshComponent, Slot, Effective, Override);
						if (Override)
						{
							Override->BaseColor = TempColor;
							Effective = Override;
						}
					}

					ImGui::TableNextRow();
					
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("Shininess");
					
					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-1.0f);
					
					float TempShininess =Effective ? Effective->Shininess : 32.0f;
					if (ImGui::DragFloat("##Shininess", &TempShininess, 0.5f, 1.0f, 256.0f))
					{
						Override = EnsureMaterialOverride(MeshComponent, Slot, Effective, Override);
						if (Override)
						{
							Override->Shininess = TempShininess;
							Effective = Override;
						}
					}

					ImGui::TableNextRow();
					
					// 4: UV Scroll Speed
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("UV Scroll");

					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-1.0f);

					FVector2 TempUVScrollSpeed = Effective ? Effective->UVScrollSpeed : FVector2();
					if (ImGui::DragFloat2("##UVScrollSpeed", &TempUVScrollSpeed.X, 0.01f))
					{
						Override = EnsureMaterialOverride(MeshComponent, Slot, Effective, Override);
						if (Override)
						{
							Override->UVScrollSpeed = TempUVScrollSpeed;
							Effective = Override;
						}
					}

					ImGui::TableNextRow();

					// 5: Sampler State
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("Sampler");

					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-1.0f);

					const char* SamplerItems[] = { "Linear Clamp", "Linear Wrap" };
					int SamplerIndex = Effective ? static_cast<int>(Effective->SamplerState) : static_cast<int>(ESamplerState::LinearClamp);
					EditorUIScale::LimitNextComboPopupWidth();
					if (ImGui::BeginCombo("##SamplerState", SamplerItems[SamplerIndex]))
					{
						for (int i = 0; i < IM_ARRAYSIZE(SamplerItems); ++i)
						{
							if (ImGui::Selectable(SamplerItems[i], SamplerIndex == i))
							{
								Override = EnsureMaterialOverride(MeshComponent, Slot, Effective, Override);
								if (Override)
								{
									Override->SamplerState = static_cast<ESamplerState>(i);
									Effective = Override;
								}
							}
						}
						ImGui::EndCombo();
					}

					ImGui::TableNextRow();

					// 6: Blend State
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("Blend Mode");

					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-1.0f);

					const char* BlendItems[] = { "Opaque", "Alpha Blend" };
					int BlendIndex = Effective ? static_cast<int>(Effective->BlendState) : static_cast<int>(EBlendState::Opaque);
					EditorUIScale::LimitNextComboPopupWidth();
					if (ImGui::BeginCombo("##BlendState", BlendItems[BlendIndex]))
					{
						for (int i = 0; i < IM_ARRAYSIZE(BlendItems); ++i)
						{
							if (ImGui::Selectable(BlendItems[i], BlendIndex == i))
							{
								Override = EnsureMaterialOverride(MeshComponent, Slot, Effective, Override);
								if (Override)
								{
									Override->SetBlendState(static_cast<EBlendState>(i));
									Effective = Override;
								}
							}
						}
						ImGui::EndCombo();
					}

					// TEAM5 material controls share the same instance-on-edit policy.
					auto Row = [](const char* Label)
					{
						ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(Label);
						ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1.0f);
					};
					auto Commit = [&]() -> UMaterial*
					{
						Override = EnsureMaterialOverride(MeshComponent, Slot, Effective, Override);
						if (Override) Effective = Override;
						return Override;
					};
					auto Factor = [&](const char* Label, float UMaterial::* Member)
					{
						Row(Label); float Value = Effective ? Effective->*Member : 1.0f;
						if (ImGui::DragFloat(Label, &Value, 0.02f, 0.0f, 100.0f))
							if (UMaterial* M = Commit()) M->*Member = Value;
					};
					auto Color = [&](const char* Label, FVector UMaterial::* Member, const FVector& Default)
					{
						Row(Label); FVector Value = Effective ? Effective->*Member : Default;
						if (EditLinearColor(Label, &Value.X)) if (UMaterial* M = Commit()) M->*Member = Value;
					};
					Color("Specular Color", &UMaterial::SpecularColor, FVector(1,1,1));
					Factor("Specular Factor", &UMaterial::SpecularFactor);
					Color("Emissive Color", &UMaterial::EmissiveColor, FVector(0,0,0));
					Factor("Emissive Factor", &UMaterial::EmissiveFactor);
					Row("UV Tiling");
					FVector2 Tiling = Effective ? Effective->UVTiling : FVector2(1,1);
					if (ImGui::DragFloat2("##UVTiling", &Tiling.X, 0.05f, 0.01f, 1000.0f))
						if (UMaterial* M = Commit()) M->UVTiling = Tiling;
					Row("World Tile Size");
					FVector2 Tile = Effective ? Effective->WorldUVTileSize : FVector2(0,0);
					if (ImGui::DragFloat2("##WorldTileSize", &Tile.X, 0.05f, 0.0f, 10000.0f))
						if (UMaterial* M = Commit()) M->WorldUVTileSize = Tile;
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("X=0 disables world tiling. Y=0 uses X for both axes. Use Linear Wrap to repeat.");
					Row("Local Tile Axes");
					bool Local = Effective && Effective->bWorldUVLocalSpace;
					if (ImGui::Checkbox("##LocalTileAxes", &Local)) if (UMaterial* M = Commit()) M->bWorldUVLocalSpace = Local;
					Row("Override Shading");
					bool OverrideShading = Effective && Effective->bOverrideShadingModel;
					if (ImGui::Checkbox("##OverrideShading", &OverrideShading)) if (UMaterial* M = Commit()) M->bOverrideShadingModel = OverrideShading;
					Row("Shading Model");
					int Model = static_cast<int>(Effective ? Effective->ShadingModel : ELightingModel::Phong);
					const char* Models[] = { "Gouraud", "Lambert", "Blinn-Phong", "Unlit" };
					ImGui::BeginDisabled(!OverrideShading);
					if (ImGui::Combo("##MaterialShadingModel", &Model, Models, IM_ARRAYSIZE(Models)))
						if (UMaterial* M = Commit()) M->ShadingModel = static_cast<ELightingModel>(Model);
					ImGui::EndDisabled();
					ImGui::EndTable();
				}
				ImGui::TreePop();
			}
			ImGui::PopID();

			ImGui::Separator();
		}
	}

	// UClass에 등록된 프로퍼티를 타입에 맞는 위젯으로 그린다
	struct FDragSettings
	{
		float Speed;
		float Min;
		float Max;
	};

	FDragSettings GetDragSettings(const FPropertyMeta& Meta, float DefaultSpeed)
	{
		const float Min = (std::max)(Meta.UIMin, Meta.ClampMin);
		const float Max = (std::min)(Meta.UIMax, Meta.ClampMax);
		const bool bHasRange = Min > -FLT_MAX || Max < FLT_MAX;
		return { Meta.Delta > 0.0f ? Meta.Delta : DefaultSpeed, bHasRange ? Min : 0.0f, bHasRange ? Max : 0.0f };
	}

	int32 ToIntBound(float Value)
	{
		if (Value <= static_cast<float>(INT_MIN)) return INT_MIN;
		if (Value >= static_cast<float>(INT_MAX)) return INT_MAX;
		return static_cast<int32>(Value);
	}

	// 드래그 범위와 별개로, Ctrl+클릭으로 직접 입력한 값도 Clamp 범위로 자른다.
	void ClampToMeta(float* Values, int32 Count, const FPropertyMeta& Meta)
	{
		if (!Meta.HasClamp())
		{
			return;
		}
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Values[Index] = std::clamp(Values[Index], Meta.ClampMin, Meta.ClampMax);
		}
	}

	uint32 ToUIntBound(float Value)
	{
		if (Value <= 0.0f) return 0;
		if (Value >= static_cast<float>(UINT_MAX)) return UINT_MAX;
		return static_cast<uint32>(Value);
	}

	void DrawPropertyRow(UObject* Object, void* Container, const FProperty& Property, const FProperty& TopProperty, ImFont* CustomFont)
	{
		void* ValuePtr = Property.ContainerPtrToValuePtr(Container);
		const FString Label = "##" + Property.Name;

		const char* DisplayName = Property.Name.c_str();

		// 라벨 | 값 배치를 유지하고 긴 라벨은 라벨 영역에서 잘라낸다.
		const float StartX = ImGui::GetCursorPosX();
		const float AvailableWidth = ImGui::GetContentRegionAvail().x;
		// 체크박스는 오른쪽 끝에 맞추고 나머지 공간을 라벨에 배정한다.
		const float LabelWidth = Property.IsA<FBoolProperty>()
			? std::max(0.0f, AvailableWidth - ImGui::GetFrameHeight())
			: AvailableWidth * 0.45f;

		const bool bInline = !Property.IsA<FTransformProperty>() && !Property.IsA<FStructProperty>();

		ImGui::AlignTextToFramePadding();
		if (bInline)
		{
			const ImVec2 LabelStart = ImGui::GetCursorScreenPos();
			const float ClipWidth = std::max(0.0f, LabelWidth - ImGui::GetStyle().ItemSpacing.x);
			ImGui::PushClipRect(LabelStart, ImVec2(LabelStart.x + ClipWidth, LabelStart.y + ImGui::GetFrameHeight()), true);
			ImGui::PushTextWrapPos(-1.0f);
			ImGui::TextUnformatted(DisplayName);
			ImGui::PopTextWrapPos();
			ImGui::PopClipRect();
			ImGui::SameLine();
			ImGui::SetCursorPosX(StartX + LabelWidth);
		}
		else
		{
			ImGui::TextWrapped("%s", DisplayName);
		}

		ImGui::SetNextItemWidth(-1.0f);

		const auto Commit = [&](auto& Target, const auto& Value)
		{
			FComponentReregisterContext Reregister(Cast<UActorComponent>(Object));
			Target = Value;
			Object->PostEditChangeProperty(TopProperty);
		};
		const auto Edit = [&](auto& Target, const auto& Widget)
		{
			auto Value = Target;
			if (Widget(Value)) Commit(Target, Value);
		};

		if (const auto* FloatProperty = CastField<FFloatProperty>(&Property))
		{
			Edit(FloatProperty->GetPropertyValue(ValuePtr), [&](float& Value)
			{
				if (Container == Object && Cast<UProgressBar>(Object) && Property.Name == "Percent")
					return ImGui::SliderFloat(Label.c_str(), &Value, 0.0f, 1.0f, "%.2f");
				const FDragSettings Drag = GetDragSettings(FloatProperty->Meta, DragStep);
				const bool bChanged = ImGui::DragFloat(Label.c_str(), &Value, Drag.Speed, Drag.Min, Drag.Max, GetDragFormat());
				if (bChanged) ClampToMeta(&Value, 1, FloatProperty->Meta);
				return bChanged;
			});
		}
		else if (const auto* IntProperty = CastField<FIntProperty>(&Property))
		{
			Edit(IntProperty->GetPropertyValue(ValuePtr), [&](int32& Value)
			{
				if (Container == Object && Cast<UProgressBar>(Object) && Property.Name == "BarFillType")
				{
					static const char* Directions[] = { "Left to Right", "Right to Left", "From Center", "From Center Horizontal", "From Center Vertical", "Top to Bottom", "Bottom to Top" };
					return ImGui::Combo(Label.c_str(), &Value, Directions, IM_ARRAYSIZE(Directions));
				}
				const FPropertyMeta& Meta = IntProperty->Meta;
				const FDragSettings Drag = GetDragSettings(Meta, 1.0f);
				const bool bChanged = ImGui::DragInt(Label.c_str(), &Value, Drag.Speed, ToIntBound(Drag.Min), ToIntBound(Drag.Max));
				if (bChanged && Meta.HasClamp())
					Value = ToIntBound(std::clamp(static_cast<float>(Value), Meta.ClampMin, Meta.ClampMax));
				return bChanged;
			});
		}
		else if (const auto* UIntProperty = CastField<FUInt32Property>(&Property))
		{
			Edit(UIntProperty->GetPropertyValue(ValuePtr), [&](uint32& Value)
			{
				const FPropertyMeta& Meta = UIntProperty->Meta;
				const FDragSettings Drag = GetDragSettings(Meta, 1.0f);
				const uint32 Min = ToUIntBound(Drag.Min);
				const uint32 Max = Drag.Min < Drag.Max ? ToUIntBound(Drag.Max) : UINT_MAX;
				const bool bChanged = ImGui::DragScalar(Label.c_str(), ImGuiDataType_U32, &Value, Drag.Speed, &Min, &Max);
				if (bChanged && Meta.HasClamp())
					Value = ToUIntBound(std::clamp(static_cast<float>(Value), Meta.ClampMin, Meta.ClampMax));
				return bChanged;
			});
		}
		else if (const auto* VectorProperty = CastField<FVectorProperty>(&Property))
		{
			Edit(VectorProperty->GetPropertyValue(ValuePtr), [&](FVector& Value)
			{
				const FDragSettings Drag = GetDragSettings(VectorProperty->Meta, DragStep);
				const bool bChanged = ImGui::DragFloat3(Label.c_str(), Value.V, Drag.Speed, Drag.Min, Drag.Max, GetDragFormat());
				if (bChanged) ClampToMeta(Value.V, 3, VectorProperty->Meta);
				return bChanged;
			});
		}
		else if (const auto* RotatorProperty = CastField<FRotatorProperty>(&Property))
		{
			Edit(RotatorProperty->GetPropertyValue(ValuePtr), [&](FRotator& Value)
			{
				// Show the axes in the same order as the transform editor.
				float Euler[3] = { Value.Roll, Value.Pitch, Value.Yaw };
				const FDragSettings Drag = GetDragSettings(RotatorProperty->Meta, DragStep);
				if (!ImGui::DragFloat3(Label.c_str(), Euler, Drag.Speed, Drag.Min, Drag.Max))
					return false;
				ClampToMeta(Euler, 3, RotatorProperty->Meta);
				Value.Roll = Euler[0];
				Value.Pitch = Euler[1];
				Value.Yaw = Euler[2];
				return true;
			});
		}
		else if (const auto* Vector4Property = CastField<FVector4Property>(&Property))
		{
			Edit(Vector4Property->GetPropertyValue(ValuePtr), [&](FVector4& Value)
			{
				const FDragSettings Drag = GetDragSettings(Vector4Property->Meta, DragStep);
				const bool bChanged = ImGui::DragFloat4(Label.c_str(), &Value.X, Drag.Speed, Drag.Min, Drag.Max, GetDragFormat());
				if (bChanged) ClampToMeta(&Value.X, 4, Vector4Property->Meta);
				return bChanged;
			});
		}
		else if (const auto* BoolProperty = CastField<FBoolProperty>(&Property))
		{
			Edit(BoolProperty->GetPropertyValue(ValuePtr),
				[&](bool& Value) { return ImGui::Checkbox(Label.c_str(), &Value); });
		}
		else if (const auto* EnumProperty = CastField<FEnumProperty>(&Property))
		{
			const FEnumInfo* Info = EnumProperty->Enum;
			const int64 Current = EnumProperty->GetValue(ValuePtr);
			const FString Preview = Info->ToName(Current);
			if (ImGui::BeginCombo(Label.c_str(), Preview.c_str()))
			{
				for (const FEnumEntry& Entry : Info->Entries)
				{
					if (Entry.bHidden) continue;
					if (ImGui::Selectable(Entry.Name.c_str(), Entry.Value == Current) && Entry.Value != Current)
					{
						FComponentReregisterContext Reregister(Cast<UActorComponent>(Object));
						EnumProperty->SetValue(ValuePtr, Entry.Value);
						Object->PostEditChangeProperty(TopProperty);
					}
				}
				ImGui::EndCombo();
			}
		}
		else if (Property.IsA<FColorProperty>())
		{
			FColor* C = static_cast<FColor*>(ValuePtr);
			float V[4] = { C->R / 255.f, C->G / 255.f, C->B / 255.f, C->A / 255.f };
			if (ImGui::ColorEdit4(Label.c_str(), V))
			{
				Commit(*C, FColor(uint8(V[0] * 255.f + 0.5f), uint8(V[1] * 255.f + 0.5f),
					uint8(V[2] * 255.f + 0.5f), uint8(V[3] * 255.f + 0.5f)));
			}
		}

		else if (const auto* ColorProperty = CastField<FLinearColorProperty>(&Property))
		{
			FVector4 Channels = ColorProperty->GetChannels(ValuePtr);
			if (ImGui::ColorEdit4(Label.c_str(), &Channels.X))
			{
				FComponentReregisterContext Reregister(Cast<UActorComponent>(Object));
				ColorProperty->SetChannels(ValuePtr, Channels);
				Object->PostEditChangeProperty(TopProperty);
			}
		}
		else if (Property.IsA<FStrProperty>())
		{
			FString* Value = static_cast<FString*>(ValuePtr);
			if (&TopProperty == &Property && Property.Name == "AnimGraphPath" && Object->IsA(USkeletalMeshComponent::StaticClass()))
			{
				auto* Component = static_cast<USkeletalMeshComponent*>(Object);
				const FString& Current = Component->GetAnimGraphPath();
				if (ImGui::BeginCombo(Label.c_str(), Current.empty() ? "None" : Current.c_str()))
				{
					if (ImGui::Selectable("None", Current.empty())) Component->SetAnimGraphPath({});
					std::error_code Ec;
					for (fs::recursive_directory_iterator It("Assets", Ec), End; !Ec && It != End; It.increment(Ec))
					{
						if (It->path().extension() != ".animgraph") continue;
						const FString Candidate = FbxPath::ToString(It->path());
						FAnimGraphAsset Graph; FString Error;
						if (!FAnimGraphAsset::Load(Candidate, Graph, Error)) continue;
						USkeletalMesh* Mesh = Component->GetSkeletalMesh();
						if (Mesh && Mesh->Skeleton && !Graph.SkeletonPath.empty() &&
							Graph.SkeletonPath != Mesh->Skeleton->GetPath()) continue;
						if (ImGui::Selectable(Candidate.c_str(), Candidate == Current))
							Component->SetAnimGraphPath(Candidate);
					}
					ImGui::EndCombo();
				}
				return;
			}

			char Buffer[256] = {};
			strncpy_s(Buffer, Value->c_str(), sizeof(Buffer) - 1);
			if (CustomFont) ImGui::PushFont(CustomFont);
			if (ImGui::InputText(Label.c_str(), Buffer, sizeof(Buffer)))
			{
				Commit(*Value, FString(Buffer));
			}
			if (CustomFont) ImGui::PopFont();
		}
		else if (Property.IsA<FTransformProperty>())
		{
			auto& Value = *static_cast<FTransform*>(ValuePtr);
			FTransform Transform = Value;
			bool bTransformChanged = DrawVector3Controller("Location", Transform.Location.V, 0.0f);
			bTransformChanged |= DrawRotatorAsXYZ("Rotation", Transform.Rotation);
			bTransformChanged |= DrawVector3Controller("Scale", Transform.Scale.V, 1.0f);
			if (bTransformChanged)
			{
				if (auto* Scene = Cast<USceneComponent>(Object); &TopProperty == &Property && Scene)
				{
					FComponentReregisterContext Reregister(Scene);
					Scene->SetTransform(Transform);
					Object->PostEditChangeProperty(TopProperty);
				}
				else Commit(Value, Transform);
			}
		}
		else if (const auto* ObjectProperty = CastField<FObjectProperty>(&Property))
		{
			UObject** ObjPtr = static_cast<UObject**>(ValuePtr);

			if (&TopProperty == &Property && Property.Name == "AnimClass" && Object->IsA(USkeletalMeshComponent::StaticClass()))
			{
				USkeletalMeshComponent* Component = static_cast<USkeletalMeshComponent*>(Object);
				UClass* Current = Component->GetAnimInstanceClass();
				if (ImGui::BeginCombo(Label.c_str(), Current ? Current->Name.c_str() : "None"))
				{
					if (ImGui::Selectable("None", !Current)) { Component->SetAnimGraphPath({}); Component->SetAnimInstanceClass(nullptr); }
					TArray<UClass*> Classes;
					Classes.Add(UAnimInstance::StaticClass());
					GetDerivedClasses(UAnimInstance::StaticClass(), Classes);
					for (UClass* Class : Classes)
						if (Class && Class != UAnimGraphInstance::StaticClass() && Class->Constructor && ImGui::Selectable(Class->Name.c_str(), Current == Class))
						{
							Component->SetAnimGraphPath({});
							Component->SetAnimInstanceClass(Class);
						}
					ImGui::EndCombo();
				}
			}
			else if (&TopProperty == &Property && Property.Name == "AnimSequence" && Object->IsA(USkeletalMeshComponent::StaticClass()))
			{
				USkeletalMeshComponent* Component = static_cast<USkeletalMeshComponent*>(Object);
				UAnimSequence* Current = Component->GetAnimationSequence();
				// 테이크 이름(예: "Unreal Take")은 파일마다 같을 수 있어 애셋 이름(AS_...)을 보여 준다.
				const auto SequenceLabel = [](const UAnimSequence* Sequence) { return FbxPath::ToString(FbxPath::FromString(Sequence->GetPath()).stem()); };
				const FString Preview = Current ? SequenceLabel(Current) : FString("None");
				if (ImGui::BeginCombo(Label.c_str(), Preview.c_str()))
				{
					if (ImGui::Selectable("None", !Current)) Component->SetAnimationSequence(nullptr);
					USkeletalMesh* Mesh = Component->GetSkeletalMesh();
					if (Mesh && Mesh->Skeleton)
					{
						for (const FString& Path : UAssetManager::GetAssetPaths(EAssetType::AnimSequence, Mesh->Skeleton->GetPath()))
						{
							ImGui::PushID(Path.c_str());
							const FString Name = FbxPath::ToString(FbxPath::FromString(Path).stem());
							if (ImGui::Selectable(Name.c_str(), Current && Current->GetPath() == Path))
								if (auto* Sequence = UAssetManager::LoadAnimSequence(Path)) Component->SetAnimationSequence(Sequence);
							ImGui::PopID();
						}
					}
					ImGui::EndCombo();
				}
					if (Current && ImGui::TreeNode("Root Motion"))
					{
						const bool OldEnable = Current->bEnableRootMotion;
						const bool OldForce = Current->bForceRootLock;
						const bool OldScale = Current->bUseNormalizedRootMotionScale;
						const ERootMotionRootLock OldLock = Current->RootMotionRootLock;
						bool Changed = ImGui::Checkbox("Enable Root Motion", &Current->bEnableRootMotion);
						int Lock = static_cast<int>(Current->RootMotionRootLock);
						if (ImGui::Combo("Root Motion Root Lock", &Lock, "Ref Pose\0Anim First Frame\0Zero\0"))
						{
							Current->RootMotionRootLock = static_cast<ERootMotionRootLock>(Lock);
							Changed = true;
						}
						Changed |= ImGui::Checkbox("Force Root Lock", &Current->bForceRootLock);
						Changed |= ImGui::Checkbox("Use Normalized Root Motion Scale", &Current->bUseNormalizedRootMotionScale);
						if (Changed && !UAssetManager::SaveAnimSequence(*Current))
						{
							Current->bEnableRootMotion = OldEnable;
							Current->bForceRootLock = OldForce;
							Current->bUseNormalizedRootMotionScale = OldScale;
							Current->RootMotionRootLock = OldLock;
							HTR_LOG(Error, "Failed to save animation root motion settings: {}", Current->GetPath());
						}
						ImGui::TreePop();
					}
			}
			else if (ObjectProperty->PropertyClass == UMaterial::StaticClass())
			{
				DrawTextureSlot(reinterpret_cast<UMaterial**>(ObjPtr), 0);
			}
			else if (ObjectProperty->PropertyClass == UFont::StaticClass())
			{
				DrawAssetSlot(Label.c_str(), EAssetType::Font, Cast<UFont>(*ObjPtr), [&](UFont* Item)
				{
					if (auto* Text = Cast<UTextRenderComponent>(Object);
						Text && Property.GetOwnerStruct() == UTextRenderComponent::StaticClass())
						Text->SetFont(Item);
					else Commit(*ObjPtr, Item);
				});
			}
			else if (ObjectProperty->PropertyClass == UStaticMesh::StaticClass())
			{
				DrawAssetSlot(Label.c_str(), EAssetType::StaticMesh, Cast<UStaticMesh>(*ObjPtr), [&](auto* Item)
				{
					if (auto* Component = Cast<UStaticMeshComponent>(Object);
						Component && Property.GetOwnerStruct() == UStaticMeshComponent::StaticClass())
					{
						FComponentReregisterContext Reregister(Component);
						Component->SetStaticMesh(Item);
					}
					else Commit(*ObjPtr, Item);
				});
			}
			else if (ObjectProperty->PropertyClass == UParticleSystem::StaticClass())
			{
				DrawAssetSlot(Label.c_str(), EAssetType::ParticleSystem, Cast<UParticleSystem>(*ObjPtr), [&](auto* Item)
				{
					if (auto* Component = Cast<UParticleSystemComponent>(Object);
						Component && Property.GetOwnerStruct() == UParticleSystemComponent::StaticClass())
						Component->SetTemplate(Item);
					else Commit(*ObjPtr, Item);
				});
			}
			else if (ObjectProperty->PropertyClass == USkeletalMesh::StaticClass())
			{
				DrawAssetSlot(Label.c_str(), EAssetType::SkeletalMesh, Cast<USkeletalMesh>(*ObjPtr), [&](auto* Item)
				{
					if (auto* Component = Cast<USkinnedMeshComponent>(Object);
						Component && Property.GetOwnerStruct() == USkinnedMeshComponent::StaticClass())
					{
						FComponentReregisterContext Reregister(Component);
						Component->SetSkeletalMesh(Item);
					}
					else Commit(*ObjPtr, Item);
				});
			}
		}
		else if (const auto* StructProperty = CastField<FStructProperty>(&Property))
		{
			ImGui::Indent();
			ImGui::PushID(ValuePtr);
			TArray<const UStruct*> Chain;
			for (const UStruct* Struct = StructProperty->Struct; Struct; Struct = Struct->GetSuperStruct())
				Chain.Add(Struct);

			for (auto It = Chain.rbegin(); It != Chain.rend(); ++It)
			{
				for (const FProperty* Member : (*It)->GetProperties())
				{
					if (!Member->HasAnyFlags(CPF_Edit)) continue;
					const bool bReadOnly = Member->HasAnyFlags(CPF_EditConst);
					if (bReadOnly) ImGui::BeginDisabled();
					DrawPropertyRow(Object, ValuePtr, *Member, TopProperty, CustomFont);
					if (bReadOnly) ImGui::EndDisabled();
				}
			}
			ImGui::PopID();
			ImGui::Unindent();
		}
		else
		{
			ImGui::TextDisabled("(Unsupported %s)", Property.GetClass()->GetName());
		}
	}

	void DrawProperty(UObject* Object, const FProperty& Property, ImFont* CustomFont)
	{
		DrawPropertyRow(Object, Object, Property, Property, CustomFont);
	}

	// 클래스 계층을 따라 올라가며 각 단계의 프로퍼티를 표시
	void DrawProperties(UObject* Object, ImFont* CustomFont)
	{
		if (!Object)
		{
			return;
		}

		TArray<UClass*> ClassChain;
		for (UClass* Class = Object->GetClass(); Class; Class = Class->Super)
		{
			ClassChain.Add(Class);
		}

		// 기반 클래스부터 표시
		for (auto It = ClassChain.rbegin(); It != ClassChain.rend(); ++It)
		{
			UClass* Class = *It;

			// 스크립트는 경로 검증을 위한 전용 UI를 같은 클래스 헤더 안에 표시한다.
			UScriptComponent* ScriptComponent = Class == UScriptComponent::StaticClass()
				? Cast<UScriptComponent>(Object) : nullptr;

			// 노출할 프로퍼티나 전용 UI가 없으면 헤더도 그리지 않는다.
			bool bHasVisibleProperty = false;
			for (const FProperty* Property : Class->GetProperties())
			{
				if (Property->HasAnyFlags(CPF_Edit))
				{
					bHasVisibleProperty = true;
					break;
				}
			}
			if (!bHasVisibleProperty && !ScriptComponent)
			{
				continue;
			}

			ImGui::PushID(Class->Name.c_str());
			if (ImGui::CollapsingHeader(Class->Name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
			{
				for (const FProperty* Property : Class->GetProperties())
				{
					if (!Property->HasAnyFlags(CPF_Edit))
					{
						continue;
					}
					const bool bReadOnly = Property->HasAnyFlags(CPF_EditConst);
					if (bReadOnly) ImGui::BeginDisabled();
					DrawProperty(Object, *Property, CustomFont);
					if (bReadOnly) ImGui::EndDisabled();
				}
				if (ScriptComponent)
				{
					DrawScriptSection(ScriptComponent);
				}
			}
			ImGui::PopID();
		}
	}

	void DrawActorHeader(AActor* Actor, UActorComponent*& OutSelectedComponent)
	{
		if (!Actor) { return; }

		static char ComponentNameInputBuf[256] = {};

		ImGui::Text("Actor: %s", Actor->GetName().c_str());
		ImGui::Text("UUID: %u", Actor->GetUUID());
		DrawDragStepCombo();

		if (ImGui::Button("Add Component"))
		{
			ImGui::OpenPopup("AddComponentPopup");
		}

		if (ImGui::BeginPopup("AddComponentPopup"))
		{
			ImGui::Text("Component Name:");
			ImGui::SameLine();
			ImGui::InputText("##Component Name", ComponentNameInputBuf, sizeof(ComponentNameInputBuf));
			TArray<UClass*> Classes;
			GetDerivedClasses(UActorComponent::StaticClass(), Classes);
			// 이름순 정렬하면 메뉴가 안정적
			EditorSort::SortByName(Classes, [](const UClass* Class) { return Class->Name; });
			for (UClass* Class : Classes)
			{
				//if (!IsSpawnableComponent(Class)) continue;
				if (ImGui::MenuItem(Class->Name.c_str()))
				{
					OutSelectedComponent = Actor->AddComponent(Class, FString(ComponentNameInputBuf));
					if (OutSelectedComponent)
						OutSelectedComponent->MarkUserCreated();
					ComponentNameInputBuf[0] = '\0';
					ImGui::CloseCurrentPopup();
				}
			}
			ImGui::EndPopup();
		}
	}
}

// UI 에디터처럼 다른 패널도 같은 속성 편집 UI를 쓰도록 위 함수들을 공개한다.
// 보조 함수들이 이 파일의 익명 네임스페이스에 있어 구현은 옮기지 않고 넘겨 준다.
namespace FPropertyEditor
{
	void DrawProperty(UObject* Object, const FProperty& Property, ImFont* CustomFont)
	{
		::DrawProperty(Object, Property, CustomFont);
	}

	void DrawObjectProperties(UObject* Object, ImFont* CustomFont)
	{
		DrawProperties(Object, CustomFont);
	}
}

bool FDetailsPanel::Init()
{
	ImGuiIO& io = ImGui::GetIO();

	io.Fonts->AddFontDefault();

	const FString FontPath = FPaths::EngineFileUtf8(EditorResourcePaths::UIFont);
	const char* fontPath = FontPath.c_str();
	float fontSize = 15.0f;
	const ImWchar* koreanRanges = io.Fonts->GetGlyphRangesKorean();

	// 기본 폰트(영문 전용)에 한글 글리프만 합친다. PushFont 없이 그리는 툴팁·콤보 같은 곳에서도 한글이 나오게 한다 (TEAM5).
	// 기본 폰트에 이미 있는 영문은 그대로 쓰고, 없는 글자만 이 폰트에서 가져온다. 크기 0 = 기본 폰트 크기에 맞춤.
	ImFontConfig KoreanMergeConfig;
	KoreanMergeConfig.MergeMode = true;
	io.Fonts->AddFontFromFileTTF(fontPath, 0.0f, &KoreanMergeConfig, koreanRanges);

	CustomFont = io.Fonts->AddFontFromFileTTF(fontPath, fontSize, nullptr, koreanRanges);
	assert(CustomFont != nullptr);

	return true;
}

void FDetailsPanel::Tick(float DeltaTime)
{
}

const ULightComponent* FDetailsPanel::GetSelectedLight() const
{
	UActorComponent* Component = TargetComponent;
	while (Component && Component->IsHiddenInDetails())
	{
		USceneComponent* SceneComponent = Cast<USceneComponent>(Component);
		Component = SceneComponent ? SceneComponent->GetAttachParent() : nullptr;
	}
	return Cast<ULightComponent>(Component);
}

void FDetailsPanel::DrawShadowPreview()
{
	const ULightComponent* Light = GetSelectedLight();
	if (!Light || !QueryShadowPreview || !ImGuiRenderer) return;
	if (!ImGui::CollapsingHeader("Shadow Maps", ImGuiTreeNodeFlags_DefaultOpen)) return;
	FTexture2DArray* Atlas = nullptr;
	TArray<FLightShadowRenderData> Shadows;
	if (!QueryShadowPreview(Light, Atlas, Shadows) || !Atlas || !Atlas->GetWidth() || !Atlas->GetHeight())
	{
		ImGui::TextDisabled("No shadow map available for this light in the active view.");
		return;
	}
	ImGui::SliderFloat("Preview Contrast", &ShadowPreviewContrast, 1.0f, 256.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
	const float Width = float(Atlas->GetWidth());
	const float Height = float(Atlas->GetHeight());
	const float DisplayWidth = ImGui::GetContentRegionAvail().x;
	if (DisplayWidth <= 0) return;
	static const char* PointFaces[] = { "+X", "-X", "+Z", "-Z", "+Y", "-Y" };
	for (int32 i = 0; i < Shadows.Num(); ++i)
	{
		const auto& Shadow = Shadows[i];
		const FRect& Rect = Shadow.AtlasRect;
		if (Rect.Width <= 0 || Rect.Height <= 0) continue;
		ImGui::PushID(i);
		if (Light->GetLightType() == ELightType::Point)
			ImGui::Text("Face %s", PointFaces[i % 6]);
		else if (Light->GetLightType() == ELightType::Directional)
			ImGui::Text("Cascade %d", i + 1);
		ImGui::TextDisabled("Slice %u | %.0f x %.0f", Shadow.AtlasIndex, Rect.Width, Rect.Height);
		
		const ImVec2 UV0(Rect.X / Width, Rect.Y / Height);
		const ImVec2 UV1((Rect.X + Rect.Width) / Width, (Rect.Y + Rect.Height) / Height);
		
		if (!ImGuiRenderer->DrawShadowMap(Atlas->GetSRV(), Shadow.AtlasIndex,
			ImVec2(DisplayWidth, DisplayWidth * Rect.Height / Rect.Width), UV0, UV1, ShadowPreviewContrast))
			ImGui::TextDisabled("Shadow preview shader is unavailable.");
		ImGui::PopID();
	}
}


void FDetailsPanel::OnRender()
{
	ImGui::SetNextWindowSize(ImVec2(EditorUIScale::Px(400.0f), EditorUIScale::Px(200.0f)), ImGuiCond_FirstUseEver);

	if (!BeginPanel())
	{
		ImGui::End();
		return;
	}

	if (TargetActor)
	{
		// 빌보드처럼 디테일에 숨겨진 컴포넌트가 선택되면 노출되는 부모로 올린다.
		// (예: 라이트는 빌보드를 클릭해서 고르지만 수치는 SpotLight 쪽에 있다)
		while (TargetComponent && TargetComponent->IsHiddenInDetails())
		{
			USceneComponent* HiddenComponent = Cast<USceneComponent>(TargetComponent);
			TargetComponent = HiddenComponent ? HiddenComponent->GetAttachParent() : nullptr;
		}

		UActorComponent* SelectCandidateComponent = TargetComponent;

		DrawActorHeader(TargetActor, TargetComponent);
		DrawCompoenetList(TargetActor, TargetComponent);				
		ImGui::Separator();

		// 액터 -> 컴포넌트 순으로, 클래스별 프로퍼티 표시
		ImGui::PushID(TargetActor);
		DrawProperties(TargetActor, CustomFont);
		ImGui::PopID();

		if (TargetComponent)
		{
			ImGui::PushID(TargetComponent);
			ImGui::SeparatorText("Component Properties");
			DrawProperties(TargetComponent, CustomFont);
			DrawShadowPreview();

			if(SelectCandidateComponent != TargetComponent)
			{
				SelectComponent(TargetComponent);
			}
			if (UMeshComponent* MeshComponent = Cast<UMeshComponent>(TargetComponent))
			{
				DrawMaterialSlots(MeshComponent);
			}
			if (UDecalComponent* DecalComponent = Cast<UDecalComponent>(TargetComponent))
			{
				DrawDecalMaterial(DecalComponent);
			}
			ImGui::PopID();

			if (PendingDeleteComponent)
			{
				if (DeleteCallback)
				{
					DeleteCallback(PendingDeleteComponent);
				}

				PendingDeleteComponent = nullptr;
			}			
		}		
	}	
	ImGui::End();
}


void FDetailsPanel::SelectComponent(UActorComponent* Component)
{
	if (!Component)
	{
		TargetComponent = nullptr;
		EditingNameComponent = nullptr;
		bFocusNameEdit = false;

		if (Callback)
			Callback(nullptr);

		return;
	}

	TargetComponent = Component;

	if (Callback)
		Callback(TargetComponent);
}

void FDetailsPanel::DrawCompoenetList(AActor* SelectedActor, UActorComponent*& OutSelectedComponent)
{
	if (!SelectedActor) { return; }

	// 트리를 순회하는 도중에는 계층을 바꾸지 않는다.
	if (PendingRootComponent)
	{
		if (SelectedActor->ReplaceRootComponent(PendingRootComponent))
		{
			OutSelectedComponent = PendingRootComponent;
			SelectComponent(PendingRootComponent);
		}
		PendingRootComponent = nullptr;
	}

	if (PendingDragComponent && PendingDroppedComponent)
	{
		if (PendingDragComponent == SelectedActor->GetRootComponent() && SelectedActor->CanMakeRootInEditor(PendingDroppedComponent))
		{
			RootDropComponent = PendingDroppedComponent;
			ImGui::OpenPopup("Component Root Drop");
		}
		else TryReparent(PendingDroppedComponent, PendingDragComponent);
		PendingDragComponent = nullptr;
		PendingDroppedComponent = nullptr;
	}

	if (ImGui::BeginPopup("Component Root Drop"))
	{
		if (SelectedActor->GetComponents().Find(RootDropComponent) != INDEX_NONE && SelectedActor->CanMakeRootInEditor(RootDropComponent))
		{
			if (ImGui::MenuItem("Make Root")) PendingRootComponent = RootDropComponent;
			if (ImGui::MenuItem("Attach to Root")) TryReparent(RootDropComponent, SelectedActor->GetRootComponent());
		}
		ImGui::EndPopup();
	}

	USceneComponent* Root = SelectedActor->GetRootComponent();

	if (Root && ImGui::CollapsingHeader("Components", ImGuiTreeNodeFlags_DefaultOpen))
	{
		DrawSceneComponentNode(Root, Root, OutSelectedComponent);
	}

	// Draw ActorComponent
	bool bHasDrawnActorComponent = false;
	for (UActorComponent* ActorComp : SelectedActor->GetComponents())
	{
		if (!ActorComp || Cast<USceneComponent>(ActorComp))
		{
			continue;
		}

		if (!bHasDrawnActorComponent)
		{
			ImGui::Separator();
			bHasDrawnActorComponent = true;
		}

		ImGui::PushID(ActorComp);
		ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
			ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;
		if (ActorComp == OutSelectedComponent)
		{
			Flags |= ImGuiTreeNodeFlags_Selected;
		}
		
		FString Label = ActorComp->GetName();
		const ImVec2 RowStart = ImGui::GetCursorScreenPos();
		const float RowWidth = ImGui::GetContentRegionAvail().x;

		const float RowPadding = std::max(ImGui::GetStyle().FramePadding.y, EditorUIScale::Px(6.0f));
		ImGui::PushStyleVarY(ImGuiStyleVar_FramePadding, RowPadding);
		ImGui::TreeNodeEx("ActorComponentNode", Flags, "%s", Label.c_str());
		ImGui::PopStyleVar();
		DrawComponentRowTooltip(ActorComp, false);

		if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
		{
			OutSelectedComponent = ActorComp;
		}

		if (ImGui::BeginPopupContextItem("Component Context"))
		{
			if (ImGui::MenuItem("Delete", nullptr, false, SelectedActor->CanDeleteComponentInEditor(ActorComp)))
				PendingDeleteComponent = ActorComp;
			ImGui::EndPopup();
		}

		const FString ClassName = std::format("({})", ActorComp->GetClass()->Name);
		const float TextX = RowStart.x + RowWidth - ImGui::CalcTextSize(ClassName.c_str()).x;
		ImGui::GetWindowDrawList()->AddText(ImVec2(TextX, RowStart.y + RowPadding), IM_COL32(200, 200, 200, 255), ClassName.c_str());

		ImGui::PopID();
	}
}

void FDetailsPanel::DrawSceneComponentNode(USceneComponent* Component, USceneComponent* Root, UActorComponent*& OutSelectedComponent)
{
	// 빌보드 같은 에디터 표시용 컴포넌트는 트리에 노출하지 않는다
	if (!Component || Component->IsHiddenInDetails())
		return;

	// 숨겨진 자식만 있는 노드가 펼침 화살표를 갖지 않도록 노출되는 자식만 센다
	TArray<USceneComponent*> Children;
	for (USceneComponent* Child : Component->GetAttachChildren())
	{
		if (Child && !Child->IsHiddenInDetails())
		{
			Children.Add(Child);
		}
	}

	const bool bIsLeaf = Children.IsEmpty();
	bIsEditingName = (EditingNameComponent == Component);

	ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_FramePadding;
	if (Component == OutSelectedComponent)
	{
		Flags |= ImGuiTreeNodeFlags_Selected;
	}
	if (bIsLeaf)
	{
		Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	}

	FString Label = Component->GetName();
	if (Component == Root)
		Label += " (Root)";

	ImGui::PushID(Component);
	const ImVec2 RowStart = ImGui::GetCursorScreenPos();
	const float RowWidth = ImGui::GetContentRegionAvail().x;
	// 위아래 여백도 클릭/드롭 영역에 포함해 작은 컴포넌트 행을 고르기 쉽게 한다.
	const float RowPadding = std::max(ImGui::GetStyle().FramePadding.y, EditorUIScale::Px(6.0f));
	ImGui::PushStyleVarY(ImGuiStyleVar_FramePadding, RowPadding);
	const bool bOpen = ImGui::TreeNodeEx("ComponentNode", Flags, "%s", bIsEditingName ? "" : Label.c_str());
	ImGui::PopStyleVar();

	USceneComponent* DragComponent = Component;

	// source: Root 제외, 현재 Component를 payload에 담음
	if (Component != Root && Component->GetOwner()->CanDeleteComponentInEditor(Component) && ImGui::BeginDragDropSource())
	{
		ImGui::SetDragDropPayload(EditorDragDrop::Component, &Component, sizeof(USceneComponent*));
		ImGui::EndDragDropSource();
	}
	// target: 모든 행이 가능, payload를 받아 현재 Component를 부모로 사용
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(
			EditorDragDrop::Component, ImGuiDragDropFlags_AcceptBeforeDelivery))
		{
			auto* DroppedComponent = *static_cast<USceneComponent* const*>(Payload->Data);
			AActor* Owner = Component->GetOwner();
			if (Owner && Owner->GetComponents().Find(DroppedComponent) != INDEX_NONE)
			{
				if (Payload->IsDelivery())
				{
					PendingDroppedComponent = DroppedComponent;
					PendingDragComponent = DragComponent;
				}
				else if (Component == Root && Payload->IsPreview())
				{
					if (Owner->CanMakeRootInEditor(DroppedComponent))
						ImGui::SetTooltip("Drop to choose Make Root or Attach to Root.");
					else if (Root->HasAnyFlags(EObjectFlags::RF_DefaultSubObject) || !Root->IsUserCreated())
						ImGui::SetTooltip("The class-defined root cannot be replaced in the editor.");
				}
			}
		}
		ImGui::EndDragDropTarget();
	}

	if (!bIsEditingName)
	{
		const bool bCanRename = Component != Root && Component->GetOwner()->CanDeleteComponentInEditor(Component);
		DrawComponentRowTooltip(Component, bCanRename);
		if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
			OutSelectedComponent = Component;

		// 이름 변경은 사용자가 추가한 컴포넌트만 (TEAM5). 더블 클릭, F2, 우클릭 메뉴로 시작한다.
		const auto BeginRename = [&]()
		{
			EditingNameComponent = Component;
			strncpy_s(ComponentNameEditBuffer, sizeof(ComponentNameEditBuffer), Component->GetName().c_str(), _TRUNCATE);
			bFocusNameEdit = true;
		};
		if (bCanRename && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			BeginRename();
		if (bCanRename && Component == OutSelectedComponent && !ImGui::GetIO().WantTextInput &&
			ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_F2, false))
			BeginRename();

		if (ImGui::BeginPopupContextItem("Component Context"))
		{
			if (ImGui::MenuItem("Rename", "F2", false, bCanRename))
				BeginRename();
			if (!bCanRename && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				ImGui::SetTooltip("%s", GetComponentRenameHint(Component, bCanRename));
			if (ImGui::MenuItem("Make Root", nullptr, false, TargetActor && TargetActor->CanMakeRootInEditor(Component)))
				PendingRootComponent = Component;
			if (ImGui::MenuItem("Delete", nullptr, false, Component->GetOwner()->CanDeleteComponentInEditor(Component)))
				PendingDeleteComponent = Component;
			ImGui::EndPopup();
		}

		const FString ClassName = std::format("({})", Component->GetClass()->Name);
		const float TextX = RowStart.x + RowWidth - ImGui::CalcTextSize(ClassName.c_str()).x;
		ImGui::GetWindowDrawList()->AddText(ImVec2(TextX, RowStart.y + RowPadding), IM_COL32(200, 200, 200, 255), ClassName.c_str());
	}
	else
	{
		ImGui::SameLine();
		if (bFocusNameEdit)
		{
			ImGui::SetKeyboardFocusHere();
			bFocusNameEdit = false;
		}

		// 액터 이름 변경과 같이, 편집 없이 다른 곳을 눌러도(포커스 해제) 편집을 끝낸다.
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(EditorUIScale::Px(6.0f), EditorUIScale::Px(3.0f)));
		ImGui::SetNextItemWidth(-FLT_MIN);
		const bool bConfirmed = ImGui::InputText("##RenameComponent", ComponentNameEditBuffer,
			sizeof(ComponentNameEditBuffer), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
		const bool bDeactivated = ImGui::IsItemDeactivated();
		ImGui::PopStyleVar();
		const bool bCancelled = ImGui::IsKeyPressed(ImGuiKey_Escape);
		if (bCancelled || bConfirmed || bDeactivated)
		{
			const FName NewName(ComponentNameEditBuffer);
			if (!bCancelled && ComponentNameEditBuffer[0] != '\0' && NewName != NAME_None && NewName != Component->GetFName())
			{
				UObject* Existing = StaticFindObjectFast(nullptr, Component->GetOuter(), NewName);
				if (!Existing && Component->GetOwner()->CanDeleteComponentInEditor(Component))
				{
					Component->SetName(FName(ComponentNameEditBuffer));
				}
				else
				{
					HTR_LOG(Warning, "A component with this name already exists under the same Outer.");
				}
			}
			EditingNameComponent = nullptr;
		}
	}

	if (bOpen && !bIsLeaf)
	{
		for (USceneComponent* Child : Children)
		{
			DrawSceneComponentNode(Child, Root, OutSelectedComponent);
		}
		ImGui::TreePop();
	}

	ImGui::PopID();
}

void FDetailsPanel::TryReparent(USceneComponent* DroppedComponent, USceneComponent* DragComponent)
{
	if (IsValid(DroppedComponent) && IsValid(DragComponent)
		&& DroppedComponent->GetOwner() == DragComponent->GetOwner()
		&& DroppedComponent->GetOwner()->CanDeleteComponentInEditor(DroppedComponent)
		&& DroppedComponent->GetAttachParent() != DragComponent
		&& DroppedComponent != DragComponent)
	{
		DroppedComponent->SetupAttachment(DragComponent, EAttachmentRule::KeepWorld);
	}
}


FDetailsPanel::~FDetailsPanel()
{
	//delete transform;
}
