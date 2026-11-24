#include "EnginePCH.h"
#include "Editor/UIEditor/UIEditorPanel.h"
#include "Core/Misc/Paths.h"

namespace
{
	FString NormalizeWidgetPath(const FString& Path)
	{
		std::error_code Error;
		auto Absolute = fs::absolute(FPaths::FromUtf8(Path), Error);
		if (Error) return {};
		Absolute = fs::weakly_canonical(Absolute, Error);
		if (Error) return {};
		// 캐시와 같은 프로젝트 상대 경로를 사용한다.
		return FPaths::ToUtf8(Absolute.lexically_relative(fs::current_path()));
	}
}

FUIEditor* FUIEditorPanel::FindDocument(const FString& Path) const
{
	for (const auto& Document : Documents)
		if (_stricmp(Document->GetAssetPath().c_str(), Path.c_str()) == 0) return Document.get();
	return nullptr;
}

void FUIEditorPanel::NewAsset(const FString& Path)
{
	const FString Key = NormalizeWidgetPath(Path);
	if (Key.empty()) { HTR_LOG(Error, "Cannot resolve widget path: {}", Path); return; }
	if (FUIEditor* Existing = FindDocument(Key))
	{
		Existing->RequestFocus();
		SetOpen(true);
		return;
	}
	// 기존 파일에 대한 생성 요청은 열기로 처리한다.
	std::error_code Error;
	if (fs::exists(FPaths::FromUtf8(Key), Error)) { OpenAsset(Key); return; }
	auto Document = MakeUnique<FUIEditor>();
	Document->NewAsset(Key);
	if (!Document->IsLoaded()) return;
	Documents.Add(std::move(Document));
	SetOpen(true);
}

void FUIEditorPanel::OpenAsset(const FString& Path)
{
	const FString Key = NormalizeWidgetPath(Path);
	if (Key.empty()) { HTR_LOG(Error, "Cannot resolve widget path: {}", Path); return; }
	if (FUIEditor* Existing = FindDocument(Key))
	{
		Existing->RequestFocus();
		SetOpen(true);
		return;
	}
	auto Document = MakeUnique<FUIEditor>();
	Document->OpenAsset(Key);
	if (!Document->IsLoaded()) return;
	Documents.Add(std::move(Document));
	SetOpen(true);
}

void FUIEditorPanel::OnRender()
{
	bool bAnyOpen = false;
	for (auto& Document : Documents)
	{
		if (Document->IsOpen())
		{
			Document->OnRender();
			bAnyOpen |= Document->IsOpen();
		}
	}
	if (bAnyOpen) return;

	if (BeginPanel())
	{
		ImGui::TextDisabled("Double-click a .widget file in the Content Drawer to open its editor tab.");
		// 닫은 탭의 미저장 상태는 세션 동안 유지하고 다시 열 때 복원한다.
		for (auto& Document : Documents)
			if (ImGui::Selectable(Document->GetAssetPath().c_str())) Document->RequestFocus();
	}
	ImGui::End();
}

void FUIEditorPanel::OnHidden()
{
	for (auto& Document : Documents) Document->OnHidden();
}

void FUIEditorPanel::RenderPreview(FUIRenderer& Renderer)
{
	if (!IsOpen()) return;
	for (auto& Document : Documents) Document->RenderPreview(Renderer);
}

void FUIEditorPanel::Release()
{
	Documents.Reset();
}
