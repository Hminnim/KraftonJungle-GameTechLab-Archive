#include "EnginePCH.h"
#include "Editor/EditorUI/EditorUI.h"
#include "Editor/EditorUI/EditorUIScale.h"
#include "Asset/AssetManager.h"
#include "Asset/FbxImporter/FbxAssetImporter.h"
#include "Asset/FbxImporter/FbxAssetLoader.h"
#include "Asset/FbxImporter/FbxPath.h"
#include "Animation/Skeleton.h"
#include "Launch/CrashReporting.h"
#include <commdlg.h>
#include <cwchar>
#include <shobjidl.h>
#include <thread>
#include <vector>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "comdlg32.lib")
namespace
{
HRESULT ChooseFbxOutputFolder(HWND Owner, const fs::path& InitialDirectory, FString& Selected)
{
	HRESULT Result = E_FAIL;
	// 오디오 등이 메인 스레드를 MTA로 초기화할 수 있어 Shell UI는 별도의 STA에서 연다.
	std::thread Browser([&]
		{
		CrashReporting::ReserveExceptionStack();
		const HRESULT Init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
		if (FAILED(Init)) { Result = Init; return; }
		{
			try
			{
				ComPtr<IFileOpenDialog> Dialog;
				Result = CoCreateInstance(__uuidof(FileOpenDialog), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&Dialog));
				if (SUCCEEDED(Result))
				{
					FILEOPENDIALOGOPTIONS Flags{};
					Result = Dialog->GetOptions(&Flags);
					if (SUCCEEDED(Result)){ Result = Dialog->SetOptions(Flags | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR);
}
					Dialog->SetTitle(L"Select FBX output folder inside Assets");
					fs::path Initial = InitialDirectory;
					while (!Initial.empty() && !fs::is_directory(Initial))
					{
						const fs::path Parent = Initial.parent_path();
						if (Parent == Initial) { Initial.clear(); break; }
						Initial = Parent;
					}
					ComPtr<IShellItem> Start;
					if (!Initial.empty() && SUCCEEDED(SHCreateItemFromParsingName(Initial.c_str(), nullptr, IID_PPV_ARGS(&Start)))){
						Dialog->SetFolder(Start.Get());
}
					if (SUCCEEDED(Result)){ Result = Dialog->Show(Owner);
}
					if (SUCCEEDED(Result))
					{
						ComPtr<IShellItem> Item;
						Result = Dialog->GetResult(&Item);
						PWSTR Path = nullptr;
						if (SUCCEEDED(Result)){ Result = Item->GetDisplayName(SIGDN_FILESYSPATH, &Path);
}
						if (SUCCEEDED(Result)){ Selected = FbxPath::ToString(fs::path(Path));
}
						CoTaskMemFree(Path);
					}
				}
			}
			catch (...) { Result = E_FAIL; }
		}
		CoUninitialize(); });
	// 소유 창으로 오는 메시지도 처리해야 다른 스레드의 모달 대화상자가 정상 동작한다.
	const HANDLE Thread = Browser.native_handle();
	bool Quit = false;
	int QuitCode = 0;
	while (MsgWaitForMultipleObjects(1, &Thread, FALSE, INFINITE, QS_ALLINPUT) == WAIT_OBJECT_0 + 1)
	{
		MSG Message;
		while (PeekMessageW(&Message, nullptr, 0, 0, PM_REMOVE))
		{
			if (Message.message == WM_QUIT)
			{
				Quit = true;
				QuitCode = static_cast<int>(Message.wParam);
			}
			else
			{
				TranslateMessage(&Message);
				DispatchMessageW(&Message);
			}
		}
	}
	Browser.join();
	if (Quit)
	{
		PostQuitMessage(QuitCode);
	}
	return Result;
}
}
namespace
{
// 선택한 파일을 UTF-8 경로로 돌려준다. 취소하면 빈 배열이다.
TArray<FString> PickFbxFiles(HWND Owner, bool bMultiSelect, FString& Error)
{
	TArray<FString> Result;
	std::vector<wchar_t> Buffer(bMultiSelect ? 262144 : 32768, L'\0');
	const std::wstring InitialDirectory = fs::absolute("Assets").wstring();
	OPENFILENAMEW Dialog{};
	Dialog.lStructSize = sizeof(Dialog);
	Dialog.hwndOwner = Owner;
	Dialog.lpstrFilter = L"FBX files (*.fbx)\0*.fbx\0\0";
	Dialog.lpstrDefExt = L"fbx";
	Dialog.lpstrFile = Buffer.data();
	Dialog.nMaxFile = static_cast<DWORD>(Buffer.size());
	Dialog.lpstrInitialDir = InitialDirectory.c_str();
	Dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | (bMultiSelect ? OFN_ALLOWMULTISELECT : 0);
	if (!GetOpenFileNameW(&Dialog))
	{
		if (const DWORD Code = CommDlgExtendedError())
		{
			Error = "Could not open the file browser.";
			HTR_LOG(Error, "FBX file dialog failed: {}", Code);
		}
		return Result;
	}
	// 한 개면 전체 경로, 여러 개면 폴더 다음에 파일 이름이 널 문자로 이어진다.
	const fs::path First(Buffer.data());
	const wchar_t* Next = Buffer.data() + std::wcslen(Buffer.data()) + 1;
	if (!bMultiSelect || *Next == L'\0')
	{
		Result.Add(FbxPath::ToString(First));
		return Result;
	}
	for (; *Next != L'\0'; Next += std::wcslen(Next) + 1)
	{
		Result.Add(FbxPath::ToString(First / Next));
	}
	return Result;
}
}

void FEditorUI::EnqueueFbxRequest(FFbxImportRequest Request)
{
	for (const FFbxImportRequest& Queued : FbxQueue)
	{
		if (Queued.Source == Request.Source && Queued.Output == Request.Output)
		{
			return;
		}
	}
	FbxQueue.Add(std::move(Request));
}

void FEditorUI::OpenFbxImportDialog()
{
	EnqueueFbxRequest({});
}

void FEditorUI::QueueFbxImports(const TArray<FString>& Sources, const FString& LockedOutput)
{
	for (const FString& Source : Sources)
	{
		FFbxImportRequest Request;
		Request.Source = Source;
		Request.Output = LockedOutput;
		Request.bLockOutput = !LockedOutput.empty();
		EnqueueFbxRequest(std::move(Request));
	}
}

void FEditorUI::QueueFbxReimport(const FString& Source, const FString& Output, const FFbxImportOptions& Options, const FString& Owner)
{
	FFbxImportRequest Request;
	Request.Source = Source;
	Request.Output = Output;
	Request.Owner = Owner;
	Request.Options = Options;
	Request.bLockOutput = true;
	Request.bReimport = true;
	EnqueueFbxRequest(std::move(Request));
}

void FEditorUI::BrowseFbxImportFiles(const FString& Output)
{
	FString Error;
	const TArray<FString> Files = PickFbxFiles(static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw), true, Error);
	if (!Error.empty())
	{
		HTR_LOG(Error, "[FBX] {}", Error);
	}
	QueueFbxImports(Files, Output);
}

void FEditorUI::BrowseFbxReimportFile(const FString& Output, const FFbxImportOptions& Options, const FString& Owner)
{
	FString Error;
	const TArray<FString> Files = PickFbxFiles(static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw), false, Error);
	if (!Error.empty())
	{
		HTR_LOG(Error, "[FBX] {}", Error);
	}
	if (!Files.IsEmpty())
	{
		QueueFbxReimport(Files[0], Output, Options, Owner);
	}
}

void FEditorUI::ApplyFbxRequest(const FFbxImportRequest& Request, bool bKeepOptions)
{
	FbxResult = {};
	if (Request.Source.size() >= sizeof(FbxSourcePath) || Request.Output.size() >= sizeof(FbxOutputDirectory))
	{
		FbxSourcePath[0] = FbxOutputDirectory[0] = '\0';
		FbxResult.Error = "The selected path is too long.";
		return;
	}
	strcpy_s(FbxSourcePath, Request.Source.c_str());
	strcpy_s(FbxOutputDirectory, Request.Output.c_str());
	bFbxOutputDirectoryEdited = !Request.Output.empty();
	if (!bKeepOptions)
	{
		FbxOptions = Request.Options;
	}
	UpdateFbxOutputDirectory();
}

void FEditorUI::AdvanceFbxQueue(bool bKeepOptions)
{
	FbxQueue.RemoveAt(0);
	++FbxProcessedCount;
	if (FbxQueue.IsEmpty())
	{
		bFbxDialogActive = bFbxImporting = bFbxImportAll = false;
		FbxProcessedCount = 0;
		return;
	}
	ApplyFbxRequest(FbxQueue[0], bKeepOptions);
	if (!bKeepOptions)
	{
		bFbxImporting = false;
	}
}

// 메시가 없는 FBX를 Target Skeleton 없이 받으면 실패한다. 이때 로드된 Skeleton을 차례로 대상으로 시도한다.
// 같은 폴더의 Skeleton을 먼저 본다. 본 이름과 부모가 맞지 않으면 임포터가 거절하므로 엉뚱한 Skeleton에 붙지 않는다.
bool FEditorUI::TryFbxImportWithLoadedSkeleton()
{
	FString SourceKey, Ignored;
	const FString SourceFolder = FbxPath::GetAssetKey(FbxSourcePath, SourceKey, Ignored, false)
		? FbxPath::ToString(FbxPath::FromString(SourceKey).parent_path()) : FString();
	TArray<FString> Candidates = UAssetManager::GetAssetPaths(EAssetType::Skeleton);
	std::stable_sort(Candidates.begin(), Candidates.end(), [&](const FString& A, const FString& B)
	{
		const bool ASame = FbxPath::ToString(FbxPath::FromString(A).parent_path()) == SourceFolder;
		const bool BSame = FbxPath::ToString(FbxPath::FromString(B).parent_path()) == SourceFolder;
		return ASame != BSame ? ASame : A < B;
	});
	// FBX를 매번 다시 읽으므로 시도 횟수를 제한한다.
	constexpr int32 MaxAttempts = 8;
	for (int32 I = 0; I < Candidates.Num() && I < MaxAttempts; ++I)
	{
		FFbxImportOptions Options = FbxOptions;
		Options.TargetSkeletonKey = Candidates[I];
		Options.bImportStaticMeshes = Options.bImportSkeletalMeshes = false;
		// 이름이 몇 개만 같은 다른 캐릭터의 Skeleton에 붙지 않도록 본 대부분이 맞아야 받는다.
		Options.MinTargetBoneMatch = 0.9f;
		FFbxImportResult Result = FFbxAssetLoader::Import(FbxSourcePath, Options, FbxOutputDirectory, FbxQueue[0].Owner);
		if (Result.bSuccess)
		{
			HTR_LOG(Info, "[FBX] No mesh in {}; imported animations for target skeleton {}", FbxSourcePath, Options.TargetSkeletonKey);
			FbxResult = std::move(Result);
			return true;
		}
	}
	return false;
}

void FEditorUI::RunFbxImport()
{
	bFbxImportTextDrawn = false;
	const FString Source = FbxSourcePath;
	FbxResult = FFbxAssetLoader::Import(FbxSourcePath, FbxOptions, FbxOutputDirectory, FbxQueue[0].Owner);
	if (!FbxResult.bSuccess && FbxOptions.TargetSkeletonKey.empty() && FbxResult.Error == "No visible supported mesh geometry in FBX" &&
		!TryFbxImportWithLoadedSkeleton())
	{
		// 아직 대상 Skeleton을 만드는 메시 FBX가 큐 뒤에 있을 수 있다(새로 받은 저장소). 한 번만 맨 뒤로 미룬다.
		if (!FbxQueue[0].bDeferred && FbxQueue.Num() > 1)
		{
			FFbxImportRequest Deferred = FbxQueue[0];
			Deferred.bDeferred = true;
			Deferred.Options = FbxOptions;
			FbxQueue.Add(std::move(Deferred));
			--FbxProcessedCount;
			HTR_LOG(Info, "[FBX] No mesh and no matching skeleton yet; retried after the other files: {}", Source);
			FbxResult = {};
			AdvanceFbxQueue(bFbxImportAll);
			return;
		}
		FbxResult.Error += ". Import the FBX that owns the skeleton first, or choose a Target Skeleton.";
	}
	for (const auto& Warning : FbxResult.Warnings)
	{
		HTR_LOG(Warning, "FBX: {}", Warning);
	}
	if (FbxResult.bSuccess)
	{
		HTR_LOG(Info, "FBX import completed: {} ({} assets)", Source, FbxResult.AssetKeys.Num());
		if (OnFbxImported)
		{
			OnFbxImported();
		}
	}
	else
	{
		HTR_LOG(Error, "FBX import failed: {}: {}", Source, FbxResult.Error);
		// 한 개씩 임포트할 때는 창에 남겨 옵션을 바꿔 다시 시도할 수 있게 한다.
		if (!bFbxImportAll)
		{
			bFbxImporting = false;
			return;
		}
		FbxFailures.Add(Source + ": " + FbxResult.Error);
	}
	AdvanceFbxQueue(bFbxImportAll);
}

void FEditorUI::UpdateFbxOutputDirectory()
{
	if (!bFbxOutputDirectoryEdited)
	{
		const FString Default = FbxPath::DefaultOutputDirectory(FbxSourcePath);
		strcpy_s(FbxOutputDirectory, Default.c_str());
	}
}

void FEditorUI::BrowseFbxSource()
{
	FString Error;
	const TArray<FString> Files = PickFbxFiles(static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw), false, Error);
	if (!Error.empty())
	{
		FbxResult.Error = Error;
		return;
	}
	if (Files.IsEmpty())
	{
		return;
	}
	if (Files[0].size() >= sizeof(FbxSourcePath))
	{
		FbxResult.Error = "The selected file path is too long.";
		return;
	}
	strcpy_s(FbxSourcePath, Files[0].c_str());
	UpdateFbxOutputDirectory();
	FbxResult = {};
}

void FEditorUI::BrowseFbxOutputDirectory()
{
	FString Selected;
	fs::path Initial;
	FString Ignored;
	if (!FbxPath::GetOutputDirectory(FbxOutputDirectory, Initial, Ignored))
	{
		Initial = fs::absolute("Assets");
	}
	const HRESULT Result = ChooseFbxOutputFolder(static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw), Initial, Selected);
	if (Result == HRESULT_FROM_WIN32(ERROR_CANCELLED))
	{
		return;
	}
	if (FAILED(Result))
	{
		FbxResult.Error = "Could not open the output folder browser.";
		HTR_LOG(Error, "FBX output folder dialog failed: {}", static_cast<uint32>(Result));
		return;
	}
	fs::path Directory;
	FString Error;
	if (!FbxPath::GetOutputDirectory(Selected, Directory, Error))
	{
		FbxResult.Error = Error;
		return;
	}
	const FString Path = FbxPath::ToString(Directory);
	if (Path.size() >= sizeof(FbxOutputDirectory))
	{
		FbxResult.Error = "The selected folder path is too long.";
		return;
	}
	strcpy_s(FbxOutputDirectory, Path.c_str());
	bFbxOutputDirectoryEdited = true;
	FbxResult = {};
}

void FEditorUI::DrawFbxImportBody()
{
	const FFbxImportRequest& Request = FbxQueue[0];
	const int32 Total = FbxProcessedCount + FbxQueue.Num();
	const auto Tip = [](const char* Text)
	{
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", Text);
	};

	if (bFbxImporting)
	{
		ImGui::Text("Importing %s  (%d / %d)", FbxPath::ToString(FbxPath::FromString(FbxSourcePath).filename()).c_str(), FbxProcessedCount + 1, Total);
		ImGui::ProgressBar(static_cast<float>(FbxProcessedCount) / static_cast<float>(Total), ImVec2(EditorUIScale::Px(580.0f), 0));
		bFbxImportTextDrawn = true;
		return;
	}

	if (Total > 1)
	{
		ImGui::Text("File %d / %d", FbxProcessedCount + 1, Total);
	}

	ImGui::TextUnformatted(Request.bReimport ? "Reimport from" : "Source FBX");
	const float PathWidth = ImGui::GetContentRegionAvail().x - EditorUIScale::ButtonWidth("Browse...") - ImGui::GetStyle().ItemSpacing.x;
	ImGui::SetNextItemWidth(PathWidth);
	// 대기열로 들어온 파일은 정해져 있다. 비어 있을 때만 (File 메뉴) 직접 고른다.
	ImGui::BeginDisabled(!Request.Source.empty());
	if (ImGui::InputText("##FbxSource", FbxSourcePath, sizeof(FbxSourcePath)))
	{
		UpdateFbxOutputDirectory();
		FbxResult = {};
	}
	Tip("The FBX file to read.");
	ImGui::SameLine();
	if (ImGui::Button("Browse..."))
	{
		BrowseFbxSource();
	}
	Tip("Choose the source FBX file.");
	ImGui::EndDisabled();

	ImGui::TextUnformatted("Output folder");
	ImGui::SetNextItemWidth(PathWidth);
	ImGui::BeginDisabled(Request.bLockOutput);
	if (ImGui::InputText("##FbxOutput", FbxOutputDirectory, sizeof(FbxOutputDirectory)))
	{
		bFbxOutputDirectoryEdited = FbxOutputDirectory[0] != '\0';
		FbxResult = {};
	}
	Tip("Folder under Assets where new assets are written.");
	ImGui::SameLine();
	if (ImGui::Button("Browse...##FbxOutput"))
	{
		BrowseFbxOutputDirectory();
	}
	Tip("Choose the output folder.");
	ImGui::EndDisabled();


	auto Check = [&](const char* Label, bool& Value, const char* Help)
	{
		if (ImGui::Checkbox(Label, &Value)) FbxResult = {};
		Tip(Help);
	};
	struct FChoice { const char* Label; const char* Help; };
	auto Choice = [&](const char* Label, int Value, std::initializer_list<FChoice> Choices, const char* Help)
	{
		const FChoice* Items = Choices.begin();
		const int Count = static_cast<int>(Choices.size());
		if (Value < 0 || Value >= Count) Value = 0;
		ImGui::SetNextItemWidth(EditorUIScale::Px(190.f));
		const bool Open = ImGui::BeginCombo(Label, Items[Value].Label);
		Tip(Help);
		if (Open)
		{
			for (int I = 0; I < Count; ++I)
			{
				if (ImGui::Selectable(Items[I].Label, I == Value))
				{
					Value = I;
					FbxResult = {};
				}
				Tip(Items[I].Help);
			}
			ImGui::EndCombo();
		}
		return Value;
	};
	auto SkeletonPicker = [&](const char* Label, FString& Key, const char* Default, const char* Help)
	{
		const auto Name = [](const FString& Path) { return FbxPath::ToString(FbxPath::FromString(Path).stem()); };
		const FString Current = Key.empty() ? FString(Default) : Name(Key);
		ImGui::SetNextItemWidth(EditorUIScale::Px(250.f));
		EditorUIScale::LimitNextComboPopupWidth();
		const bool Open = ImGui::BeginCombo(Label, Current.c_str());
		const FString Description = Key.empty() ? FString(Help) : FString(Help) + "\n" + Key;
		Tip(Description.c_str());
		if (Open)
		{
			if (ImGui::Selectable(Default, Key.empty())) { Key.clear(); FbxResult = {}; }
			Tip(Help);
			TArray<FString> Skeletons = UAssetManager::GetAssetPaths(EAssetType::Skeleton);
			std::stable_sort(Skeletons.begin(), Skeletons.end(), [&](const FString& A, const FString& B) { return Name(A) < Name(B); });
			for (const FString& Path : Skeletons)
			{
				ImGui::PushID(Path.c_str());
				if (ImGui::Selectable(Name(Path).c_str(), Path == Key)) { Key = Path; FbxResult = {}; }
				const FString EntryHelp = "Use this Skeleton asset.\n" + Path;
				Tip(EntryHelp.c_str());
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
	};

	ImGui::SeparatorText("Outputs");
	ImGui::BeginDisabled(Request.bReimport);
	Check("Static Meshes", FbxOptions.bImportStaticMeshes, "Create static mesh assets for geometry assigned to the static import path.");
	ImGui::SameLine();
	Check("Skeletal Meshes", FbxOptions.bImportSkeletalMeshes, "Create skeletal mesh assets and bind them to a new or existing Skeleton.");
	ImGui::SameLine();
	Check("Animations", FbxOptions.bImportAnimations, "Create AnimSequence assets from animation takes that move the selected rig.");
	if (ImGui::Button("Animations Only"))
	{
		FbxOptions.bImportStaticMeshes = FbxOptions.bImportSkeletalMeshes = false;
		FbxOptions.bImportAnimations = true;
		FbxResult = {};
	}
	Tip("Enable only animation output. Select an existing Animation Skeleton.");
	ImGui::EndDisabled();

	if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::SetNextItemWidth(EditorUIScale::Px(160.f));
		if (ImGui::DragFloat("Uniform Scale", &FbxOptions.UniformScale, .01f, .0001f, 100000.f, "%.4f", ImGuiSliderFlags_AlwaysClamp)) FbxResult = {};
		Tip("Multiply imported vertices and bone translations. Direct imports to an existing Skeleton use its import space.");
		Check("Force Front X Axis", FbxOptions.bForceFrontX, "Convert source forward to engine +X. Direct imports to an existing Skeleton use its import space.");
	}

	if (FbxOptions.bImportStaticMeshes && ImGui::CollapsingHeader("Static Meshes##ImportSection", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::PushID("Static");
		Check("Bake Meshes", FbxOptions.bBakeStaticMeshes, "Bake node and parent transforms into static geometry.");
		Check("Combine Meshes", FbxOptions.bCombineStaticMeshes, "Put the static geometry in one mesh asset. Enable Bake Meshes to preserve relative placement.");
		ImGui::PopID();
	}
	if (FbxOptions.bImportSkeletalMeshes && ImGui::CollapsingHeader("Skeletal Meshes##ImportSection", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::PushID("Skeletal");
		Check("Bake Meshes", FbxOptions.bBakeSkeletalMeshes, "Include placement outside the rig in the mesh and bind transforms. An existing Skeleton supplies its import space.");
		Check("Combine Meshes", FbxOptions.bCombineSkeletalMeshes, "Combine meshes that share a source rig. A single LOD group stays together.");
		SkeletonPicker("Mesh Skeleton", FbxOptions.MeshSkeletonKey, "Create New", "Create a Skeleton per imported rig, or select an existing Skeleton for mesh binding.");
		if (!FbxOptions.MeshSkeletonKey.empty())
			Check("Add Missing Bones", FbxOptions.bAddMissingBones, "Append missing source bones to the shared Skeleton. Existing bones, poses, and indices are preserved.");
		Check("Convert Meshes in Bone Hierarchy", FbxOptions.bConvertMeshesInBoneHierarchy, "Bind unskinned geometry beneath a rig rigidly to a bone.");
		Check("Convert Meshes with Animated Transforms", FbxOptions.bConvertMeshesWithAnimatedTransforms, "Import unskinned geometry moved by transform animation as skeletal, including its LODs.");
		ImGui::PopID();
	}
	if (FbxOptions.bImportAnimations && ImGui::CollapsingHeader("Animations##ImportSection", ImGuiTreeNodeFlags_DefaultOpen))
	{
		SkeletonPicker("Animation Skeleton", FbxOptions.TargetSkeletonKey, "Use Mesh Skeleton", "Use the imported mesh's Skeleton or select an existing animation target independently.");
		FbxOptions.bHumanoidRetarget = Choice("Retarget Mode", FbxOptions.bHumanoidRetarget, {
			{"Direct", "Match bone names and parent hierarchy and import local animation tracks."},
			{"Humanoid Retarget", "Transfer motion between supported humanoid rigs using their reference poses."}
		}, "How source motion is transferred to the animation Skeleton.") == 1;
		if (FbxOptions.bHumanoidRetarget)
			Check("Bake Hips Translation to Root", FbxOptions.bBakeHipsToRoot, "Move horizontal pelvis travel into a separate target root bone for root motion.");
		FbxOptions.AnimationRange = static_cast<EFbxAnimationRange>(Choice("Animation Range", static_cast<int>(FbxOptions.AnimationRange), {
			{"Timeline", "Use the take's timeline range."},
			{"Animated Keys", "Use the first and last source transform key in the rig."},
			{"Set Range", "Sample an explicit range expressed in frames at the selected sample rate."}
		}, "Choose the portion of each animation take to sample."));
		if (FbxOptions.AnimationRange == EFbxAnimationRange::SetRange)
		{
			ImGui::SetNextItemWidth(EditorUIScale::Px(160.f));
			if (ImGui::InputInt("Start Frame", &FbxOptions.FirstAnimationFrame)) FbxResult = {};
			Tip("Inclusive start frame at the selected sample rate.");
			ImGui::SetNextItemWidth(EditorUIScale::Px(160.f));
			if (ImGui::InputInt("End Frame", &FbxOptions.LastAnimationFrame)) FbxResult = {};
			Tip("Inclusive end frame at the selected sample rate.");
		}
		const bool Custom = Choice("Sample Rate", FbxOptions.CustomSampleRate > 0, {
			{"Source", "Sample at the FBX scene frame rate."},
			{"Custom", "Resample at the specified frames per second."}
		}, "Choose how frequently the source animation is sampled.") == 1;
		FbxOptions.CustomSampleRate = Custom ? std::max(1, FbxOptions.CustomSampleRate ? FbxOptions.CustomSampleRate : 30) : 0;
		if (Custom)
		{
			ImGui::SetNextItemWidth(EditorUIScale::Px(160.f));
			if (ImGui::InputInt("FPS", &FbxOptions.CustomSampleRate)) FbxResult = {};
			FbxOptions.CustomSampleRate = std::clamp(FbxOptions.CustomSampleRate, 1, 1000);
			Tip("Custom sample rate in frames per second, from 1 to 1000.");
		}
		FbxOptions.FrameAlignment = static_cast<EFbxFrameAlignment>(Choice("Frame Alignment", static_cast<int>(FbxOptions.FrameAlignment), {
			{"None", "Keep the source endpoints; sample count is rounded to the nearest frame."},
			{"Floor", "Round both endpoints down to the sample grid."},
			{"Closest", "Round both endpoints to the nearest sample frame."},
			{"Ceiling", "Round both endpoints up to the sample grid."}
		}, "Choose how the selected time range is aligned to the sampling grid."));
		if (Request.bReimport)
			Check("Keep Animation Notifies", FbxOptions.bKeepNotifies, "Copy existing Notifies into the replacement AnimSequence.");
	}

	const char* InvalidOptions = nullptr;
	if (!FbxOptions.bImportStaticMeshes && !FbxOptions.bImportSkeletalMeshes && !FbxOptions.bImportAnimations)
		InvalidOptions = "Enable at least one output.";
	else if (FbxOptions.bImportAnimations && FbxOptions.TargetSkeletonKey.empty() &&
		(!FbxOptions.bImportSkeletalMeshes || FbxOptions.bHumanoidRetarget))
		InvalidOptions = "Select an existing Animation Skeleton for animation-only import or humanoid retargeting.";
	else if (FbxOptions.bImportAnimations && FbxOptions.AnimationRange == EFbxAnimationRange::SetRange &&
		FbxOptions.FirstAnimationFrame > FbxOptions.LastAnimationFrame)
		InvalidOptions = "End Frame must be at or after Start Frame.";
	if (InvalidOptions) ImGui::TextWrapped("%s", InvalidOptions);

	if (!FFbxAssetImporter::IsAvailable())
	{
		ImGui::TextWrapped("Run Setup/SlnGenerator.bat and rebuild the editor to enable FBX import.");
	}
	if (!FbxResult.Error.empty())
	{
		ImGui::Separator();
		ImGui::TextWrapped("Import failed: %s", FbxResult.Error.c_str());
	}

	ImGui::Separator();
	const ImVec2 ButtonSize(EditorUIScale::Px(100.0f), 0);
	const bool bBlocked = IsFbxImportBlocked && IsFbxImportBlocked();
	const bool bHasOthers = FbxQueue.Num() > 1;
	ImGui::BeginDisabled(bBlocked || !FFbxAssetImporter::IsAvailable() || FbxSourcePath[0] == '\0' || FbxOutputDirectory[0] == '\0' ||
		InvalidOptions != nullptr);
	if (ImGui::Button(Request.bReimport ? "Reimport" : "Import", ButtonSize))
	{
		bFbxImporting = true;
		bFbxImportAll = false;
	}
	Tip("Run the import with the displayed options.");
	ImGui::SameLine();
	ImGui::BeginDisabled(!bHasOthers || Request.bReimport);
	if (ImGui::Button("Import All", ButtonSize))
	{
		bFbxImporting = true;
		bFbxImportAll = true;
	}
	ImGui::EndDisabled();
	Tip("Imports this and the remaining files with these options.");
	ImGui::EndDisabled();
	ImGui::SameLine();
	bool bAdvance = false;
	if (ImGui::Button("Cancel", ButtonSize))
	{
		bAdvance = true;
	}
	Tip("Skips this file and moves to the next one.\nFiles without a binary are asked again on the next start.");
	ImGui::SameLine();
	bool bCancelAll = false;
	ImGui::BeginDisabled(!bHasOthers);
	if (ImGui::Button("Cancel All", ButtonSize))
	{
		bCancelAll = true;
	}
	ImGui::EndDisabled();
	Tip("Stops asking for now.\nFiles without a binary are asked again on the next start.");

	// Request가 가리키는 대기열을 바꾸므로 그리기가 끝난 뒤에 처리한다.
	if (bCancelAll)
	{
		FbxQueue.Reset();
		FbxProcessedCount = 0;
		bFbxDialogActive = false;
	}
	else if (bAdvance)
	{
		AdvanceFbxQueue(false);
	}
}

void FEditorUI::DrawFbxFailures()
{
	const char* PopupName = "FBX Import Failures";
	if (FbxFailures.IsEmpty() || bFbxDialogActive)
	{
		return;
	}
	if (!ImGui::IsPopupOpen(PopupName))
	{
		ImGui::OpenPopup(PopupName);
	}
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(.5f, .5f));
	if (ImGui::BeginPopupModal(PopupName, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
	{
		ImGui::TextUnformatted("These files could not be imported:");
		for (const FString& Failure : FbxFailures)
		{
			ImGui::BulletText("%s", Failure.c_str());
		}
		ImGui::TextDisabled("See the Output Log for details.");
		ImGui::Separator();
		if (ImGui::Button("OK", ImVec2(EditorUIScale::Px(100.0f), 0)))
		{
			FbxFailures.Reset();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

void FEditorUI::DrawFbxImportDialog()
{
	// 앞 프레임에 진행 표시를 그렸으므로 이번 프레임에 실행한다. 임포트 동안 화면은 그 표시로 멈춰 있다.
	if (bFbxImporting && bFbxImportTextDrawn)
	{
		RunFbxImport();
	}

	const char* PopupName = "Import FBX";
	if (!bFbxDialogActive && !FbxQueue.IsEmpty() && !(IsFbxImportBlocked && IsFbxImportBlocked()))
	{
		bFbxDialogActive = true;
		ApplyFbxRequest(FbxQueue[0], false);
	}
	const bool bPopupOpen = ImGui::IsPopupOpen(PopupName);
	if (bFbxDialogActive && !bPopupOpen)
	{
		ImGui::OpenPopup(PopupName);
	}
	if (bFbxDialogActive || bPopupOpen)
	{
		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(.5f, .5f));
		ImGui::SetNextWindowSize(ImVec2(EditorUIScale::Px(600.0f), 0), ImGuiCond_Appearing);
		ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, ImGui::GetMainViewport()->WorkSize.y * .9f));
		bFbxPopupOpen = true;
		if (ImGui::BeginPopupModal(PopupName, bFbxImporting ? nullptr : &bFbxPopupOpen, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
		{
			if (bFbxDialogActive)
			{
				DrawFbxImportBody();
			}
			if (!bFbxDialogActive)
			{
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
		// X 버튼은 Cancel과 같다.
		if (!bFbxPopupOpen && bFbxDialogActive)
		{
			AdvanceFbxQueue(false);
		}
	}
	DrawFbxFailures();
}
