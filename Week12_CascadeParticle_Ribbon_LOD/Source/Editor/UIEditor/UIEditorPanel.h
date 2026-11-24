#pragma once

#include "Editor/EditorUI/EditorPanel.h"
#include "Editor/UIEditor/UIEditor.h"

// 파일별 편집 상태를 유지하고 각 위젯 에셋을 루트 에디터 탭으로 연다.
class FUIEditorPanel : public IEditorPanel
{
public:
	bool Init() override { return true; }
	void Tick(float DeltaTime) override {}
	void OnRender() override;
	void OnHidden() override;
	const char* GetPanelName() const override { return "UI Editor"; }
	bool IsRootPanel() const override { return true; }

	void NewAsset(const FString& Path);
	void OpenAsset(const FString& Path);
	void RenderPreview(FUIRenderer& Renderer);
	void Release();

private:
	FUIEditor* FindDocument(const FString& Path) const;
	TArray<TUniquePtr<FUIEditor>> Documents;
};
