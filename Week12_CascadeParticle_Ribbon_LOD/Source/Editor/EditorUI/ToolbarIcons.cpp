#include "EnginePCH.h"
#include "Editor/EditorUI/ToolbarIcons.h"
#include <imgui.h>
#include <cmath>

namespace
{
	constexpr float Pi = 3.14159265f;

	// 비활성(BeginDisabled) 상태의 알파가 곱해지도록 항상 GetColorU32를 거친다.
	ImU32 Col(int R, int G, int B, int A = 255) { return ImGui::GetColorU32(IM_COL32(R, G, B, A)); }
	ImU32 White() { return Col(215, 215, 215); }
	ImU32 Blue() { return Col(120, 155, 215); }
	ImU32 Green() { return Col(110, 195, 110); }
	ImU32 Tan() { return Col(210, 180, 120); }
	ImU32 Red() { return Col(230, 90, 90); }
	ImU32 Lavender() { return Col(140, 155, 225); }

	struct FCanvas
	{
		ImDrawList* DL;
		ImVec2 Min;
		float S;
		ImVec2 P(float X, float Y) const { return ImVec2(Min.x + X * S, Min.y + Y * S); }
		float T(float Thickness) const { return std::max(1.0f, S * Thickness); }

		void Line(float X0, float Y0, float X1, float Y1, ImU32 C, float Th = 0.1f) const { DL->AddLine(P(X0, Y0), P(X1, Y1), C, T(Th)); }
		void Tri(float X0, float Y0, float X1, float Y1, float X2, float Y2, ImU32 C) const { DL->AddTriangleFilled(P(X0, Y0), P(X1, Y1), P(X2, Y2), C); }

		// 꺾쇠 (^ 또는 v). CY는 꼭짓점 높이.
		void Chevron(float CY, bool bUp, ImU32 C) const
		{
			const float H = bUp ? 0.18f : -0.18f;
			DL->AddLine(P(0.2f, CY + H), P(0.5f, CY), C, T(0.13f));
			DL->AddLine(P(0.5f, CY), P(0.8f, CY + H), C, T(0.13f));
		}

		void Plus(float CX, float CY, float R, ImU32 C) const
		{
			DL->AddLine(P(CX - R, CY), P(CX + R, CY), C, T(0.12f));
			DL->AddLine(P(CX, CY - R), P(CX, CY + R), C, T(0.12f));
		}

		// 마름모 층 (LOD 재생성 아이콘)
		void Layer(float CY, ImU32 C, bool bFilled) const
		{
			const ImVec2 Pts[4] = { P(0.5f, CY - 0.14f), P(0.88f, CY), P(0.5f, CY + 0.14f), P(0.12f, CY) };
			if (bFilled) DL->AddConvexPolyFilled(Pts, 4, C);
			else DL->AddPolyline(Pts, 4, C, ImDrawFlags_Closed, T(0.07f));
		}
	};
}

void ToolbarIcons::Save(ImDrawList* DL, const ImVec2& Min, float Size)
{
	const FCanvas C{ DL, Min, Size };
	// 몸체 (오른쪽 위 모서리를 깎은 사각형)
	const ImVec2 Body[5] = { C.P(0.12f, 0.12f), C.P(0.72f, 0.12f), C.P(0.88f, 0.28f), C.P(0.88f, 0.88f), C.P(0.12f, 0.88f) };
	DL->AddPolyline(Body, 5, Blue(), ImDrawFlags_Closed, C.T(0.09f));
	// 위쪽 셔터와 아래쪽 라벨
	DL->AddRectFilled(C.P(0.3f, 0.12f), C.P(0.64f, 0.36f), Blue());
	DL->AddRect(C.P(0.26f, 0.54f), C.P(0.74f, 0.88f), Blue(), 0.0f, 0, C.T(0.08f));
}

void ToolbarIcons::Browse(ImDrawList* DL, const ImVec2& Min, float Size)
{
	const FCanvas C{ DL, Min, Size };
	// 폴더 (왼쪽 위 탭이 튀어나온 외곽선). 오른쪽 아래는 돋보기 자리로 비워 둔다.
	const ImVec2 Folder[7] = { C.P(0.06f, 0.8f), C.P(0.06f, 0.18f), C.P(0.34f, 0.18f), C.P(0.42f, 0.28f),
		C.P(0.86f, 0.28f), C.P(0.86f, 0.46f), C.P(0.06f, 0.46f) };
	DL->AddPolyline(Folder, 7, White(), 0, C.T(0.08f));
	C.Line(0.06f, 0.8f, 0.46f, 0.8f, White(), 0.08f);
	// 돋보기
	DL->AddCircle(C.P(0.66f, 0.64f), Size * 0.16f, White(), 0, C.T(0.09f));
	C.Line(0.78f, 0.76f, 0.95f, 0.93f, White(), 0.12f);
}

void ToolbarIcons::RestartSim(ImDrawList* DL, const ImVec2& Min, float Size)
{
	const FCanvas C{ DL, Min, Size };
	const ImVec2 Center = C.P(0.5f, 0.5f);
	for (int32 Tooth = 0; Tooth < 8; ++Tooth)
	{
		const float A = Tooth * Pi / 4.0f;
		DL->AddLine(ImVec2(Center.x + std::cos(A) * Size * 0.28f, Center.y + std::sin(A) * Size * 0.28f),
			ImVec2(Center.x + std::cos(A) * Size * 0.46f, Center.y + std::sin(A) * Size * 0.46f), Blue(), C.T(0.14f));
	}
	DL->AddCircle(Center, Size * 0.28f, Blue(), 0, C.T(0.12f));
	DL->AddCircleFilled(Center, Size * 0.1f, Blue());
}

void ToolbarIcons::RestartLevel(ImDrawList* DL, const ImVec2& Min, float Size)
{
	const FCanvas C{ DL, Min, Size };
	const ImVec2 Center = C.P(0.5f, 0.52f);
	const float R = Size * 0.32f;
	const float Start = -0.35f * Pi, End = 1.25f * Pi;
	DL->PathArcTo(Center, R, Start, End, 20);
	DL->PathStroke(White(), 0, C.T(0.12f));
	// 화살촉: 원호의 시작점에서 진행 반대 방향을 가리킨다.
	const ImVec2 Tip(Center.x + std::cos(Start) * R, Center.y + std::sin(Start) * R);
	DL->AddTriangleFilled(ImVec2(Tip.x - Size * 0.2f, Tip.y - Size * 0.12f), ImVec2(Tip.x + Size * 0.08f, Tip.y - Size * 0.2f),
		ImVec2(Tip.x + Size * 0.02f, Tip.y + Size * 0.12f), White());
}

void ToolbarIcons::Undo(ImDrawList* DL, const ImVec2& Min, float Size)
{
	const FCanvas C{ DL, Min, Size };
	DL->AddBezierCubic(C.P(0.85f, 0.82f), C.P(0.9f, 0.3f), C.P(0.5f, 0.28f), C.P(0.3f, 0.4f), White(), C.T(0.12f));
	C.Tri(0.08f, 0.5f, 0.38f, 0.22f, 0.42f, 0.58f, White());
}

void ToolbarIcons::Redo(ImDrawList* DL, const ImVec2& Min, float Size)
{
	const FCanvas C{ DL, Min, Size };
	DL->AddBezierCubic(C.P(0.15f, 0.82f), C.P(0.1f, 0.3f), C.P(0.5f, 0.28f), C.P(0.7f, 0.4f), White(), C.T(0.12f));
	C.Tri(0.92f, 0.5f, 0.62f, 0.22f, 0.58f, 0.58f, White());
}

void ToolbarIcons::Thumbnail(ImDrawList* DL, const ImVec2& Min, float Size)
{
	const FCanvas C{ DL, Min, Size };
	DL->AddRect(C.P(0.1f, 0.16f), C.P(0.9f, 0.84f), White(), Size * 0.06f, 0, C.T(0.09f));
	C.Tri(0.2f, 0.74f, 0.42f, 0.42f, 0.62f, 0.74f, White());
	C.Tri(0.48f, 0.74f, 0.64f, 0.54f, 0.8f, 0.74f, White());
	DL->AddCircleFilled(C.P(0.7f, 0.34f), Size * 0.07f, White());
}

void ToolbarIcons::Bounds(ImDrawList* DL, const ImVec2& Min, float Size)
{
	const FCanvas C{ DL, Min, Size };
	const ImVec2 Center = C.P(0.5f, 0.5f);
	const float R = Size * 0.4f;
	DL->AddCircle(Center, R, White(), 0, C.T(0.08f));
	DL->AddEllipse(Center, ImVec2(R, R * 0.38f), White(), 0.0f, 0, C.T(0.07f));
	DL->AddEllipse(Center, ImVec2(R * 0.38f, R), White(), 0.0f, 0, C.T(0.07f));
}

void ToolbarIcons::OriginAxis(ImDrawList* DL, const ImVec2& Min, float Size)
{
	const FCanvas C{ DL, Min, Size };
	C.Line(0.35f, 0.62f, 0.92f, 0.82f, Red(), 0.11f);       // X
	C.Line(0.35f, 0.62f, 0.82f, 0.25f, Green(), 0.11f);     // Y
	C.Line(0.35f, 0.62f, 0.35f, 0.06f, Blue(), 0.11f);      // Z
	C.Line(0.35f, 0.62f, 0.08f, 0.92f, White(), 0.11f);
}

void ToolbarIcons::BackgroundColor(ImDrawList* DL, const ImVec2& Min, float Size)
{
	const FCanvas C{ DL, Min, Size };
	C.Line(0.9f, 0.1f, 0.5f, 0.5f, White(), 0.12f);
	const ImVec2 Pts[4] = { C.P(0.44f, 0.44f), C.P(0.58f, 0.58f), C.P(0.28f, 0.9f), C.P(0.08f, 0.92f) };
	DL->AddConvexPolyFilled(Pts, 4, Col(90, 190, 210));
}

void ToolbarIcons::RegenLODFromHigher(ImDrawList* DL, const ImVec2& Min, float Size)
{
	const FCanvas C{ DL, Min, Size };
	C.Layer(0.72f, Tan(), false);
	C.Layer(0.5f, Tan(), false);
	C.Layer(0.28f, Tan(), true);
}

void ToolbarIcons::RegenLODFromLower(ImDrawList* DL, const ImVec2& Min, float Size)
{
	const FCanvas C{ DL, Min, Size };
	C.Layer(0.72f, Tan(), true);
	C.Layer(0.5f, Tan(), false);
	C.Layer(0.28f, Tan(), false);
}

void ToolbarIcons::LowestLOD(ImDrawList* DL, const ImVec2& Min, float Size)
{
	const FCanvas C{ DL, Min, Size };
	C.Chevron(0.5f, false, Lavender());
	C.Chevron(0.78f, false, Lavender());
}

void ToolbarIcons::LowerLOD(ImDrawList* DL, const ImVec2& Min, float Size)
{
	const FCanvas C{ DL, Min, Size };
	C.Chevron(0.68f, false, Lavender());
}

void ToolbarIcons::AddLODBefore(ImDrawList* DL, const ImVec2& Min, float Size)
{
	const FCanvas C{ DL, Min, Size };
	C.Plus(0.5f, 0.3f, 0.2f, Green());
	C.Chevron(0.9f, false, Green());
}

void ToolbarIcons::AddLODAfter(ImDrawList* DL, const ImVec2& Min, float Size)
{
	const FCanvas C{ DL, Min, Size };
	C.Plus(0.5f, 0.7f, 0.2f, Green());
	C.Chevron(0.1f, true, Green());
}

void ToolbarIcons::HigherLOD(ImDrawList* DL, const ImVec2& Min, float Size)
{
	const FCanvas C{ DL, Min, Size };
	C.Chevron(0.32f, true, Lavender());
}

void ToolbarIcons::HighestLOD(ImDrawList* DL, const ImVec2& Min, float Size)
{
	const FCanvas C{ DL, Min, Size };
	C.Chevron(0.22f, true, Lavender());
	C.Chevron(0.5f, true, Lavender());
}

void ToolbarIcons::DeleteLOD(ImDrawList* DL, const ImVec2& Min, float Size)
{
	const FCanvas C{ DL, Min, Size };
	C.Line(0.2f, 0.2f, 0.8f, 0.8f, Red(), 0.15f);
	C.Line(0.8f, 0.2f, 0.2f, 0.8f, Red(), 0.15f);
}
