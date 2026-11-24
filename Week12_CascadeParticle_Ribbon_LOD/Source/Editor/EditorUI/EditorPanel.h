#pragma once

#include "Editor/EditorUI/EditorDocking.h"
#include <cstring>

class IEditorPanel
{
public:
	virtual ~IEditorPanel() = default;
	virtual bool Init() = 0;
	virtual void Tick(float DeltaTime) = 0;
	virtual void OnRender() = 0;

	bool IsOpen() const { return bIsOpen; }
	void SetOpen(bool bOpen)
	{
		if (bIsOpen == bOpen) return;
		bIsOpen = bOpen;
		if (ImGui::GetCurrentContext() && ImGui::GetCurrentContext()->SettingsLoaded)
			ImGui::MarkIniSettingsDirty();
	}
	virtual const char* GetPanelName() const = 0;
	virtual const char* GetOpenStateSettingsName() const { return GetPanelName(); }
	virtual bool IsRootPanel() const { return false; }
	virtual void OnHidden() {}

	// ---- 메인 메뉴바 (UE: 메이저 탭마다 자기 메뉴바가 있고, 활성 탭의 메뉴만 보인다) ----
	// 메인 메뉴바는 가장 최근에 포커스를 받은 메이저 탭(레벨 에디터 포함)의 메뉴를 그린다.
	// 자기 메뉴를 가진 메이저 탭은 탭이 보이는 동안 마지막으로 포커스를 받은 ImGui 프레임 번호를 돌려준다 (없으면 -1).
	virtual int32 GetMainMenuFocusFrame() const { return -1; }
	// 레벨의 File 메뉴 자리에 그릴 메뉴들 (BeginMenu ... EndMenu). Build 같은 전역 메뉴는 EditorUI가 이어서 그린다.
	virtual void DrawMainMenus() {}
	// 전역 Window 메뉴 맨 위에 넣을 이 탭의 항목 (패널 켜고 끄기, 레이아웃 초기화)
	virtual void DrawMainWindowMenuItems() {}

protected:
	bool BeginPanel(ImGuiWindowFlags Flags = 0)
	{
		return BeginPanel(GetPanelName(), Flags);
	}

	bool BeginPanel(const char* Title, ImGuiWindowFlags Flags = 0)
	{
		PreparePanelWindow(Title);
		bool bOpen = IsOpen();
		const bool bVisible = ImGui::Begin(Title, &bOpen, Flags);
		SetOpen(bOpen);
		return bVisible && bOpen;
	}

	void PreparePanelWindow(const char* Title) const
	{
		EditorDocking::PrepareWindow(Title, IsRootPanel());
	}

	// 첫 NewFrame 전에 등록한다. TypeName은 ini 섹션 이름이며 정적 문자열이어야 한다.
	void RegisterOpenStateSettings(const char* TypeName)
	{
		ImGuiSettingsHandler Handler;
		Handler.TypeName = TypeName;
		Handler.TypeHash = ImHashStr(TypeName);
		Handler.UserData = this;
		Handler.ReadOpenFn = [](ImGuiContext*, ImGuiSettingsHandler* Settings, const char* Name) -> void*
		{
			return std::strcmp(Name, "Panel") == 0 ? Settings->UserData : nullptr;
		};
		Handler.ReadLineFn = [](ImGuiContext*, ImGuiSettingsHandler*, void* Entry, const char* Line)
		{
			auto* Panel = static_cast<IEditorPanel*>(Entry);
			if (std::strcmp(Line, "Open=1") == 0) Panel->bIsOpen = true;
			else if (std::strcmp(Line, "Open=0") == 0) Panel->bIsOpen = false;
		};
		Handler.WriteAllFn = [](ImGuiContext*, ImGuiSettingsHandler* Settings, ImGuiTextBuffer* Buffer)
		{
			const auto* Panel = static_cast<const IEditorPanel*>(Settings->UserData);
			Buffer->appendf("[%s][Panel]\nOpen=%d\n\n", Settings->TypeName, Panel->IsOpen() ? 1 : 0);
		};
		ImGui::AddSettingsHandler(&Handler);
	}

private:
	friend class FEditorUI;
	bool bIsOpen = true;
};
