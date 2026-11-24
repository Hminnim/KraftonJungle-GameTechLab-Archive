#pragma once

#include "Editor/EditorUI/EditorPanel.h"
#include "Editor/ParticleEditor/ParticleSystemEditor.h"

class UEditorEngine;

// 파티클 시스템 에디터 탭들을 관리한다. 탭이 하나도 없으면 "New Particle System" 버튼만 있는 탭을 띄운다.
// 메인 메뉴의 패널 목록에서 "Particle Editor"를 켜면 열린다.
class FParticleEditorPanel : public IEditorPanel
{
public:
	bool Init() override { return true; }
	void Tick(float DeltaTime) override;
	void OnRender() override;
	void OnHidden() override;
	const char* GetPanelName() const override { return "Particle Editor"; }
	bool IsRootPanel() const override { return true; }
	// 메인 메뉴바: 문서 탭들은 EditorUI의 패널 목록에 없으므로, 가장 최근에 포커스를 받은 문서에 전달한다
	int32 GetMainMenuFocusFrame() const override;
	void DrawMainMenus() override;
	void DrawMainWindowMenuItems() override;

	// 파일 없는 새 문서 ("New Particle System" 버튼)
	void NewParticleSystem();
	// Content Drawer: .particle 더블클릭. 이미 열린 탭이면 그 탭으로 간다.
	void OpenAsset(const FString& Path);
	// Content Drawer: 우클릭 → Create Particle System. 기본 이미터로 만들어 바로 파일로 저장하고 연다.
	void NewAsset(const FString& Path);
	// 파티클 탭에 포커스가 있으면 엔진이 Ctrl+S·Delete를 레벨로 보내지 않는다
	bool HasFocusedWindow() const;
	// 각 탭 툴바의 Browse 버튼이 부른다 (엔진이 Content Drawer로 연결한다)
	// 이미 열린 탭에도 넘겨서, 콜백보다 먼저 만들어진 탭의 Browse가 꺼진 채로 남지 않게 한다
	void SetBrowseAssetCallback(std::function<void(const FString&)> InCallback)
	{
		OnBrowseAsset = std::move(InCallback);
		for (const auto& Document : Documents)
			if (Document) Document->SetBrowseAssetCallback(OnBrowseAsset);
	}
	void RenderPreview(UEditorEngine* Engine);
	void Release();

private:
	FParticleSystemEditor* FindDocument(const FString& Path) const;
	FString MakeUntitledName() const;
	FParticleSystemEditor* FindMainMenuDocument() const;
	TUniquePtr<FParticleSystemEditor> MakeDocument(const FString& Name, UParticleSystem* System = nullptr, const FString& Path = {});

	std::function<void(const FString&)> OnBrowseAsset;

	TArray<TUniquePtr<FParticleSystemEditor>> Documents;
	uint32 NextId = 0;
};
