#include "EnginePCH.h"
#include "Editor/EditorUI/OverflowToolbar.h"
#include "Editor/EditorUI/EditorUIScale.h"
#include <imgui.h>
#include <algorithm>

namespace
{
	float PadX() { return EditorUIScale::Px(8.0f); }
	float IconSize() { return EditorUIScale::Px(16.0f); }
	float IconGap() { return EditorUIScale::Px(6.0f); }
	float ItemGap() { return EditorUIScale::Px(2.0f); }
	float OptionsWidth() { return EditorUIScale::Px(14.0f); }
	float SeparatorWidth() { return EditorUIScale::Px(13.0f); }
	float OverflowWidth() { return EditorUIScale::Px(44.0f); }
	bool IsItemEnabled(const FToolbarItem& Item) { return !Item.IsEnabled || Item.IsEnabled(); }
	bool IsItemChecked(const FToolbarItem& Item) { return Item.IsChecked && Item.IsChecked(); }

	bool ShowsLabel(const FToolbarItem& Item) { return !Item.Label.empty() && !(Item.bIconOnly && Item.Icon); }

	float ButtonWidth(const FToolbarItem& Item)
	{
		const float Text = ShowsLabel(Item) ? ImGui::CalcTextSize(Item.Label.c_str()).x : 0.0f;
		const float Icon = Item.Icon ? IconSize() + (ShowsLabel(Item) ? IconGap() : 0.0f) : 0.0f;
		return PadX() * 2.0f + Icon + Text;
	}

	float ItemWidth(const FToolbarItem& Item)
	{
		if (Item.DrawCustom) return Item.CustomWidth ? Item.CustomWidth() : 0.0f;
		return ButtonWidth(Item) + (Item.DrawOptionsMenu ? OptionsWidth() : 0.0f);
	}

	void DrawSeparator()
	{
		const float Width = SeparatorWidth();
		const ImVec2 Pos = ImGui::GetCursorScreenPos();
		const float Height = EditorToolbar::GetHeight();
		const float X = Pos.x + Width * 0.5f;
		ImGui::GetWindowDrawList()->AddLine(ImVec2(X, Pos.y + Height * 0.15f), ImVec2(X, Pos.y + Height * 0.85f),
			ImGui::GetColorU32(ImGuiCol_Separator));
		ImGui::Dummy(ImVec2(Width, Height));
	}

	// 배경 없는 납작한 버튼. 마우스를 올리거나 켜진 상태일 때만 배경을 칠한다.
	void DrawButton(const FToolbarItem& Item)
	{
		const bool bEnabled = IsItemEnabled(Item);
		const bool bChecked = IsItemChecked(Item);
		const float Height = EditorToolbar::GetHeight();
		const float Width = ButtonWidth(Item);

		ImGui::PushID(Item.Label.c_str());
		ImGui::BeginDisabled(!bEnabled);
		const ImVec2 Min = ImGui::GetCursorScreenPos();
		const bool bPressed = ImGui::InvisibleButton("##Button", ImVec2(Width, Height));
		const bool bHovered = ImGui::IsItemHovered();
		const bool bHeld = ImGui::IsItemActive();
		ImDrawList* DL = ImGui::GetWindowDrawList();
		const ImVec2 Max(Min.x + Width, Min.y + Height);
		const float Rounding = EditorUIScale::Px(3.0f);
		if (bChecked) DL->AddRectFilled(Min, Max, ImGui::GetColorU32(ImGuiCol_ButtonActive), Rounding);
		else if (bHeld) DL->AddRectFilled(Min, Max, ImGui::GetColorU32(ImGuiCol_ButtonActive), Rounding);
		else if (bHovered) DL->AddRectFilled(Min, Max, ImGui::GetColorU32(ImGuiCol_ButtonHovered), Rounding);

		float X = Min.x + PadX();
		if (Item.Icon)
		{
			Item.Icon(DL, ImVec2(X, Min.y + (Height - IconSize()) * 0.5f), IconSize());
			X += IconSize() + IconGap();
		}
		if (ShowsLabel(Item))
			DL->AddText(ImVec2(X, Min.y + (Height - ImGui::GetTextLineHeight()) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), Item.Label.c_str());
		ImGui::EndDisabled();

		if (bPressed && Item.OnClick) Item.OnClick();
		if (!Item.Tooltip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("%s", Item.Tooltip.c_str());

		// ⋮ 옵션 버튼
		if (Item.DrawOptionsMenu)
		{
			ImGui::SameLine(0.0f, 0.0f);
			const ImVec2 OMin = ImGui::GetCursorScreenPos();
			if (ImGui::InvisibleButton("##Options", ImVec2(OptionsWidth(), Height))) ImGui::OpenPopup("##OptionsMenu");
			if (ImGui::IsItemHovered())
				DL->AddRectFilled(OMin, ImVec2(OMin.x + OptionsWidth(), OMin.y + Height), ImGui::GetColorU32(ImGuiCol_ButtonHovered), Rounding);
			const float CX = OMin.x + OptionsWidth() * 0.5f;
			const float Dot = EditorUIScale::Px(1.6f);
			for (int32 Index = -1; Index <= 1; ++Index)
				DL->AddCircleFilled(ImVec2(CX, OMin.y + Height * 0.5f + Index * EditorUIScale::Px(4.5f)), Dot, ImGui::GetColorU32(ImGuiCol_Text));
			if (ImGui::BeginPopup("##OptionsMenu"))
			{
				Item.DrawOptionsMenu();
				ImGui::EndPopup();
			}
		}
		ImGui::PopID();
	}

	// ☰ + ⌄ 모양 버튼. 폰트에 기호가 없을 수 있어 직접 그린다.
	bool DrawOverflowButton()
	{
		const float Width = OverflowWidth();
		const float Height = EditorToolbar::GetHeight();
		const ImVec2 Min = ImGui::GetCursorScreenPos();
		const bool bPressed = ImGui::InvisibleButton("##Overflow", ImVec2(Width, Height));
		ImDrawList* DL = ImGui::GetWindowDrawList();
		const ImU32 Back = ImGui::GetColorU32(ImGui::IsItemHovered() || ImGui::IsPopupOpen("##OverflowMenu") ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
		DL->AddRectFilled(Min, ImVec2(Min.x + Width, Min.y + Height), Back, EditorUIScale::Px(3.0f));
		const ImU32 Color = ImGui::GetColorU32(ImGuiCol_Text);
		const float T = EditorUIScale::Px(1.8f);
		const float Left = Min.x + EditorUIScale::Px(9.0f);
		const float Right = Left + EditorUIScale::Px(13.0f);
		for (int32 Line = 0; Line < 3; ++Line)
		{
			const float Y = Min.y + Height * 0.5f + (Line - 1) * EditorUIScale::Px(4.5f);
			DL->AddLine(ImVec2(Left, Y), ImVec2(Right, Y), Color, T);
		}
		const float CX = Right + EditorUIScale::Px(9.0f);
		const float CY = Min.y + Height * 0.5f;
		const float C = EditorUIScale::Px(3.0f);
		DL->AddLine(ImVec2(CX - C, CY - C * 0.5f), ImVec2(CX, CY + C * 0.5f), Color, EditorUIScale::Px(1.4f));
		DL->AddLine(ImVec2(CX, CY + C * 0.5f), ImVec2(CX + C, CY - C * 0.5f), Color, EditorUIScale::Px(1.4f));
		return bPressed;
	}

	// 드롭다운 안의 항목: 왼쪽에 아이콘, 토글은 오른쪽 체크 표시.
	void DrawOverflowEntry(const FToolbarItem& Item)
	{
		if (Item.DrawCustom)
		{
			Item.DrawCustom();
			return;
		}
		// 글자 앞에 공백을 채워 아이콘 자리를 만들고, 그 자리에 아이콘을 덧그린다.
		const float Indent = IconSize() + IconGap();
		const float SpaceWidth = std::max(1.0f, ImGui::CalcTextSize(" ").x);
		const FString Label = FString(static_cast<size_t>(Indent / SpaceWidth) + 1, ' ') + Item.Label;
		ImGui::PushID(Item.Label.c_str());
		const ImVec2 Pos = ImGui::GetCursorScreenPos();
		const bool bEnabled = IsItemEnabled(Item);
		const bool bClicked = ImGui::MenuItem(Label.c_str(), nullptr, IsItemChecked(Item), bEnabled);
		if (Item.Icon)
		{
			ImGui::BeginDisabled(!bEnabled);
			const float Height = ImGui::GetItemRectSize().y;
			Item.Icon(ImGui::GetWindowDrawList(), ImVec2(Pos.x, Pos.y + (Height - IconSize()) * 0.5f), IconSize());
			ImGui::EndDisabled();
		}
		if (bClicked && Item.OnClick) Item.OnClick();
		if (!Item.Tooltip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", Item.Tooltip.c_str());
		ImGui::PopID();
	}
}

float EditorToolbar::GetHeight()
{
	return std::max(EditorUIScale::Px(26.0f), ImGui::GetTextLineHeight() + EditorUIScale::Px(8.0f));
}

void EditorToolbar::Draw(const char* Id, const TArray<FToolbarItem>& Items)
{
	ImGui::PushID(Id);
	const float Avail = ImGui::GetContentRegionAvail().x;
	const float Height = GetHeight();

	// 툴바 띠 배경
	{
		const ImVec2 Pos = ImGui::GetCursorScreenPos();
		const float BandPad = EditorUIScale::Px(3.0f);
		ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(Pos.x - BandPad, Pos.y - BandPad),
			ImVec2(Pos.x + Avail + BandPad, Pos.y + Height + BandPad), ImGui::GetColorU32(ImGuiCol_MenuBarBg));
	}

	auto NeededWidth = [&](int32 Index)
	{
		float Width = ItemWidth(Items[Index]);
		if (Index > 0) Width += ItemGap();
		if (Index > 0 && Items[Index].bSeparatorBefore) Width += SeparatorWidth() + ItemGap();
		return Width;
	};

	// 전부 들어가면 드롭다운 없이 그린다. 아니면 ☰ 자리를 남기고 들어가는 데까지만 그린다.
	float Total = 0.0f;
	for (int32 Index = 0; Index < Items.Num(); ++Index) Total += NeededWidth(Index);
	const bool bOverflow = Total > Avail;
	const float Budget = bOverflow ? Avail - OverflowWidth() - ItemGap() : Avail;

	int32 VisibleCount = 0;
	float Used = 0.0f;
	for (int32 Index = 0; Index < Items.Num(); ++Index)
	{
		const float Needed = NeededWidth(Index);
		if (Used + Needed > Budget) break;
		Used += Needed;
		++VisibleCount;
	}

	for (int32 Index = 0; Index < VisibleCount; ++Index)
	{
		const FToolbarItem& Item = Items[Index];
		if (Index > 0)
		{
			ImGui::SameLine(0.0f, ItemGap());
			if (Item.bSeparatorBefore)
			{
				DrawSeparator();
				ImGui::SameLine(0.0f, ItemGap());
			}
		}
		// 같은 이름의 버튼(예: LOD 재생성 두 개)이 있어도 ID가 겹치지 않게 순번으로 구분한다.
		ImGui::PushID(Index);
		if (Item.DrawCustom)
		{
			ImGui::BeginGroup();
			Item.DrawCustom();
			ImGui::EndGroup();
		}
		else DrawButton(Item);
		ImGui::PopID();
	}

	if (bOverflow)
	{
		// ☰는 항상 오른쪽 끝에 붙인다.
		if (VisibleCount > 0) ImGui::SameLine(0.0f, 0.0f);
		const float StartX = ImGui::GetCursorPosX();
		const float RightX = StartX + (Avail - Used) - OverflowWidth();
		ImGui::SetCursorPosX(std::max(StartX, RightX));
		if (DrawOverflowButton()) ImGui::OpenPopup("##OverflowMenu");
		if (ImGui::BeginPopup("##OverflowMenu"))
		{
			for (int32 Index = VisibleCount; Index < Items.Num(); ++Index)
			{
				if (Index > VisibleCount && Items[Index].bSeparatorBefore) ImGui::Separator();
				ImGui::PushID(Index);
				DrawOverflowEntry(Items[Index]);
				ImGui::PopID();
			}
			ImGui::EndPopup();
		}
	}
	else if (VisibleCount == 0)
	{
		ImGui::Dummy(ImVec2(0.0f, Height));
	}
	ImGui::PopID();
}
