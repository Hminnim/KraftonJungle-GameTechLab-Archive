#pragma once

struct ImDrawList;
struct ImVec2;

// 툴바용 벡터 아이콘. 이미지 파일 없이 ImDrawList로 그린다.
// 모두 FToolbarIconFn 시그니처 (DrawList, 왼쪽 위, 한 변 길이)를 따른다.
namespace ToolbarIcons
{
	void Save(ImDrawList* DL, const ImVec2& Min, float Size);           // 디스켓
	void Browse(ImDrawList* DL, const ImVec2& Min, float Size);         // 폴더와 돋보기
	void RestartSim(ImDrawList* DL, const ImVec2& Min, float Size);     // 톱니바퀴
	void RestartLevel(ImDrawList* DL, const ImVec2& Min, float Size);   // 원형 화살표
	void Undo(ImDrawList* DL, const ImVec2& Min, float Size);
	void Redo(ImDrawList* DL, const ImVec2& Min, float Size);
	void Thumbnail(ImDrawList* DL, const ImVec2& Min, float Size);      // 액자
	void Bounds(ImDrawList* DL, const ImVec2& Min, float Size);         // 와이어 구
	void OriginAxis(ImDrawList* DL, const ImVec2& Min, float Size);     // XYZ 축
	void BackgroundColor(ImDrawList* DL, const ImVec2& Min, float Size);// 붓
	void RegenLODFromHigher(ImDrawList* DL, const ImVec2& Min, float Size);
	void RegenLODFromLower(ImDrawList* DL, const ImVec2& Min, float Size);
	void LowestLOD(ImDrawList* DL, const ImVec2& Min, float Size);      // 아래 겹화살
	void LowerLOD(ImDrawList* DL, const ImVec2& Min, float Size);       // 아래 화살
	void AddLODBefore(ImDrawList* DL, const ImVec2& Min, float Size);   // + 와 아래 화살
	void AddLODAfter(ImDrawList* DL, const ImVec2& Min, float Size);    // + 와 위 화살
	void HigherLOD(ImDrawList* DL, const ImVec2& Min, float Size);      // 위 화살
	void HighestLOD(ImDrawList* DL, const ImVec2& Min, float Size);     // 위 겹화살
	void DeleteLOD(ImDrawList* DL, const ImVec2& Min, float Size);      // 빨간 X
}
