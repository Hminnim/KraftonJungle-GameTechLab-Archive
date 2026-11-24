#pragma once

#include "Core/Types.h"
#include <functional>

struct ImDrawList;
struct ImVec2;

// 툴바 아이콘을 그리는 함수. Min은 아이콘 칸의 왼쪽 위, Size는 한 변 길이.
// 색은 ImGui::GetColorU32(...)로 감싸야 비활성 상태에서 흐려진다.
using FToolbarIconFn = std::function<void(ImDrawList*, const ImVec2& Min, float Size)>;

// 언리얼 에셋 에디터의 상단 툴바 항목 (아이콘 + 글자 버튼).
// 창이 좁아지면 들어가지 않는 항목이 뒤에서부터 오른쪽 ☰ 드롭다운으로 옮겨진다.
struct FToolbarItem
{
	FString Label;
	FString Tooltip;
	FToolbarIconFn Icon;
	std::function<void()> OnClick;
	// 설정하면 토글 버튼이 된다 (켜진 상태는 버튼 배경, 드롭다운에서는 체크 표시).
	std::function<bool()> IsChecked;
	// 설정하지 않으면 항상 활성. 아직 구현하지 않은 기능은 false를 돌려 흐리게 둔다.
	std::function<bool()> IsEnabled;
	// 설정하면 버튼 오른쪽에 ⋮ 버튼이 붙고, 누르면 이 함수로 팝업 메뉴를 그린다.
	std::function<void()> DrawOptionsMenu;
	// 버튼 대신 직접 그리는 항목 (예: "LOD: [0]"). 폭을 알아야 접을 위치를 정할 수 있다.
	std::function<float()> CustomWidth;
	std::function<void()> DrawCustom;
	// 이 항목 앞에 구분선을 넣는다 (그룹 경계).
	bool bSeparatorBefore = false;
	// 툴바에는 아이콘만 그린다 (Label은 ☰ 드롭다운과 ID에만 쓴다).
	bool bIconOnly = false;
};

namespace EditorToolbar
{
	void Draw(const char* Id, const TArray<FToolbarItem>& Items);
	// 툴바 버튼 한 칸의 높이. 직접 그리는 항목도 이 높이에 맞춘다.
	float GetHeight();
}
