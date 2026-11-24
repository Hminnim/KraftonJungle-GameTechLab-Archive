#include "EnginePCH.h"
#include "Editor/ParticleEditor/ParticleEditorPanel.h"
#include "Asset/AssetManager.h"
#include "Asset/FbxImporter/FbxPath.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSerialization.h"

namespace
{
	// "Assets/Particles/A.particle", "Particles\\A.particle" 등을 같은 키로 맞춰 비교한다
	FString ParticleKey(const FString& Path)
	{
		FString Key = FbxPath::ToString(FbxPath::FromString(Path).lexically_normal());
		std::replace(Key.begin(), Key.end(), '\\', '/');
		if (Key.rfind("Assets/", 0) == 0) Key = Key.substr(7);
		return Key;
	}

	FString AssetName(const FString& Path)
	{
		return FbxPath::ToString(FbxPath::FromString(Path).stem());
	}
}

FString FParticleEditorPanel::MakeUntitledName() const
{
	// 표시 이름은 지금 열린 탭과 겹치지 않는 가장 작은 번호를 쓴다 (닫으면 그 이름을 다시 쓸 수 있다).
	const auto IsNameUsed = [this](const FString& Candidate)
	{
		for (const auto& Document : Documents)
			if (Document->IsOpen() && Document->GetName() == Candidate) return true;
		return false;
	};
	FString Name = "NewParticleSystem";
	for (int32 Suffix = 1; IsNameUsed(Name); ++Suffix)
		Name = "NewParticleSystem_" + std::to_string(Suffix);
	return Name;
}

FParticleSystemEditor* FParticleEditorPanel::FindDocument(const FString& Path) const
{
	const FString Key = ParticleKey(Path);
	for (const auto& Document : Documents)
		if (Document->IsOpen() && !Document->GetAssetPath().empty() && ParticleKey(Document->GetAssetPath()) == Key)
			return Document.get();
	return nullptr;
}

TUniquePtr<FParticleSystemEditor> FParticleEditorPanel::MakeDocument(const FString& Name, UParticleSystem* System, const FString& Path)
{
	// 창 ID(NextId)는 계속 늘린다. 닫은 탭의 ImGui 창·도킹 상태가 새 탭에 섞이지 않게 하기 위해서다.
	auto Document = MakeUnique<FParticleSystemEditor>(Name, NextId++, System, Path);
	Document->SetBrowseAssetCallback(OnBrowseAsset);
	return Document;
}

FParticleSystemEditor* FParticleEditorPanel::FindMainMenuDocument() const
{
	FParticleSystemEditor* Best = nullptr;
	int32 BestFrame = -1;
	for (const auto& Document : Documents)
	{
		const int32 Frame = Document->GetMainMenuFocusFrame();
		if (Frame > BestFrame)
		{
			BestFrame = Frame;
			Best = Document.get();
		}
	}
	return Best;
}

int32 FParticleEditorPanel::GetMainMenuFocusFrame() const
{
	const FParticleSystemEditor* Document = FindMainMenuDocument();
	return Document ? Document->GetMainMenuFocusFrame() : -1;
}

void FParticleEditorPanel::DrawMainMenus()
{
	if (FParticleSystemEditor* Document = FindMainMenuDocument()) Document->DrawMainMenus();
}

void FParticleEditorPanel::DrawMainWindowMenuItems()
{
	if (FParticleSystemEditor* Document = FindMainMenuDocument()) Document->DrawMainWindowMenuItems();
}

void FParticleEditorPanel::NewParticleSystem()
{
	Documents.Add(MakeDocument(MakeUntitledName()));
	SetOpen(true);
}

void FParticleEditorPanel::OpenAsset(const FString& Path)
{
	if (FParticleSystemEditor* Existing = FindDocument(Path))
	{
		Existing->RequestFocus();
		SetOpen(true);
		return;
	}

	// 등록된 에셋은 레벨 컴포넌트와 같이 쓰므로 작업 사본을 만들어 편집한다
	UParticleSystem* Asset = UAssetManager::LoadParticleSystem(Path);
	if (!Asset)
	{
		HTR_LOG(Error, "[ParticleEditor] Cannot open '{}'", Path);
		return;
	}
	FString Error;
	UParticleSystem* Copy = ParticleAsset::Duplicate(*Asset, Error);
	if (!Copy)
	{
		HTR_LOG(Error, "[ParticleEditor] Cannot copy '{}': {}", Path, Error);
		return;
	}
	Documents.Add(MakeDocument(AssetName(Path), Copy, Path));
	SetOpen(true);
}

void FParticleEditorPanel::NewAsset(const FString& Path)
{
	// 이미 있는 파일이면 새로 만들지 않고 연다
	std::error_code Ec;
	if (fs::exists(FbxPath::FromString(Path), Ec) || FindDocument(Path))
	{
		OpenAsset(Path);
		return;
	}
	auto Document = MakeDocument(AssetName(Path), nullptr, Path);
	// 다른 에셋 생성과 같이 만들자마자 파일을 쓴다 (Content Drawer에 바로 보인다)
	if (!Document->SaveAs(Path)) return;
	Documents.Add(std::move(Document));
	SetOpen(true);
}

bool FParticleEditorPanel::HasFocusedWindow() const
{
	if (!IsOpen()) return false;
	for (const auto& Document : Documents)
		if (Document->HasFocusedWindow()) return true;
	return false;
}

void FParticleEditorPanel::Tick(float DeltaTime)
{
	for (auto& Document : Documents)
		if (Document->IsOpen()) Document->Tick(DeltaTime);
}

void FParticleEditorPanel::OnRender()
{
	// x로 닫은 탭(저장 확인을 마친 탭)은 프리뷰 월드와 작업 사본까지 정리한다.
	const bool bHadDocuments = !Documents.IsEmpty();
	for (int32 Index = Documents.Num() - 1; Index >= 0; --Index)
		if (!Documents[Index]->IsOpen()) Documents.RemoveAt(Index);

	// 마지막 탭을 닫으면 에디터도 닫는다. 그대로 두면 빈 "Particle Editor" 안내 창이 메인 창에 도킹되어 나타난다
	// (분리한 창을 닫았는데 창이 사라지지 않고 도킹되는 것처럼 보인다). 메뉴에서 직접 연 경우에만 안내 창을 보인다.
	if (bHadDocuments && Documents.IsEmpty())
	{
		SetOpen(false);
		return;
	}

	for (auto& Document : Documents)
		Document->OnRender();
	if (!Documents.IsEmpty()) return;

	if (BeginPanel())
	{
		ImGui::TextDisabled("No particle system is open.");
		ImGui::TextDisabled("Double-click a .particle asset in the Content Drawer, or create a new one.");
		if (ImGui::Button("New Particle System")) NewParticleSystem();
	}
	ImGui::End();
}

void FParticleEditorPanel::OnHidden()
{
	for (auto& Document : Documents) Document->OnHidden();
}

void FParticleEditorPanel::RenderPreview(UEditorEngine* Engine)
{
	if (!IsOpen()) return;
	for (auto& Document : Documents) Document->RenderPreview(Engine);
}

void FParticleEditorPanel::Release()
{
	Documents.Reset();
}
