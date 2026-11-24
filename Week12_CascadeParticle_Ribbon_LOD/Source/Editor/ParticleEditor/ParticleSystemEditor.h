#pragma once

#include "Editor/EditorUI/EditorPanel.h"
#include "Editor/EditorUI/OverflowToolbar.h"
#include "Editor/Viewports/EditorViewportClient.h"
#include "Particles/ParticleDistribution.h"

class UWorld;
class UObject;
class UParticleSystem;
class UParticleEmitter;
class UParticleLODLevel;
class UClass;
class UParticleModule;
class UParticleSystemComponent;
class UEditorEngine;

// 파티클 시스템 하나를 편집하는 루트 탭 (언리얼 Cascade 구성).
// 탭 안에 자체 DockSpace를 두고 Viewport / Emitters / Details / Curve Editor 패널을 도킹한다.
// 등록된 에셋(UAssetManager가 가진 객체)은 레벨 컴포넌트가 같이 쓰므로 직접 고치지 않는다.
// 열 때 작업 사본을 만들어 편집하고, 저장할 때 사본을 하나 더 만들어 에셋으로 등록한 뒤 레벨 컴포넌트를 새 에셋으로 옮긴다.
class FParticleSystemEditor : public IEditorPanel
{
public:
	// InSystem이 nullptr이면 기본 이미터 하나로 새 시스템을 만든다. 넘기면 그 작업 사본을 소유한다.
	// InAssetPath가 비어 있으면 아직 파일이 없는 문서다 (처음 저장할 때 경로를 묻는다).
	FParticleSystemEditor(const FString& InName, uint32 InId, UParticleSystem* InSystem = nullptr, const FString& InAssetPath = {});
	~FParticleSystemEditor() override;

	const FString& GetAssetPath() const { return AssetPath; }
	bool IsDirty() const { return bDirty; }
	// 이 탭이나 탭 안 패널에 포커스가 있는지 (엔진이 Ctrl+S·Delete를 레벨로 보내지 않을 때 쓴다)
	bool HasFocusedWindow() const { return IsOpen() && bFocused; }
	// 경로가 있으면 바로 저장하고, 없으면 Save As 창을 연다. 저장에 성공하면 true.
	bool Save();
	bool SaveAs(const FString& InAssetPath);

	bool Init() override { return true; }
	void Tick(float DeltaTime) override;
	void OnRender() override;
	void OnHidden() override;
	const char* GetPanelName() const override { return WindowName.c_str(); }
	bool IsRootPanel() const override { return true; }
	// 이 탭이 활성이면 메인 메뉴바에 File / Asset 메뉴와 Window 항목을 그린다 (FParticleEditorPanel이 전달한다)
	int32 GetMainMenuFocusFrame() const override;
	void DrawMainMenus() override;
	void DrawMainWindowMenuItems() override;

	const FString& GetName() const { return Name; }
	void RequestFocus() { SetOpen(true); FocusFramesLeft = FocusRetryFrames; }
	// 툴바 Browse 버튼: Content Drawer에서 이 에셋을 찾아 보여준다. 인자는 "Assets/..." 경로.
	void SetBrowseAssetCallback(std::function<void(const FString&)> InCallback) { OnBrowseAsset = std::move(InCallback); }
	// ImGui 프레임 뒤 엔진이 부른다. 뷰포트 패널이 보일 때만 프리뷰 월드를 그린다.
	void RenderPreview(UEditorEngine* Engine);

private:
	enum EPanelIndex { Viewport, Emitters, Details, CurveEditor, PanelCount };

	void BuildDefaultSystem();
	// UE의 "New Particle Sprite Emitter"와 같은 기본 구성의 이미터를 시스템 끝에 추가한다.
	UParticleEmitter* AddDefaultEmitter();
	void CreatePreviewWorld();
	void RestartSimulation();
	void ResetCamera();
	// 활성 파티클 전체가 보이도록 카메라를 옮긴다 (뷰포트에서 F)
	void FocusOnParticles();

	void BuildToolbarItems();
	void OpenSaveAs();
	// 저장하지 않은 문서도 Browse할 수 있다 (저장될 폴더를 연다)
	bool CanBrowseToAsset() const { return static_cast<bool>(OnBrowseAsset); }
	// Save As 창의 폴더/이름을 채운다. 경로가 있으면 그 폴더와 파일 이름, 없으면 기본 폴더와 탭 이름
	void PrepareSaveAs();
	void BrowseToAsset();
	void BuildDefaultLayout(const ImVec2& Size);
	bool IsPanelDetached(int32 Index) const;
	void DrawDockedPanels(bool bActive);

	void DrawViewport();
	void DrawViewportMenus(const ImVec2& ViewMin);
	void DrawOriginAxis(const ImVec2& ViewMin, const ImVec2& ViewSize);
	void DrawEmitters();
	void DrawEmitterColumn(int32 EmitterIndex, UParticleEmitter* Emitter, float Width);
	void DrawEmitterAddMenu(UParticleEmitter* Emitter, UParticleLODLevel* LOD);
	void DrawEmitterMenuItems(UParticleEmitter* Emitter);
	UParticleEmitter* FindModuleEmitter(const UParticleModule* Module) const;
	void ApplyPendingEdit();
	void DrawDetails();
	void DrawCurveEditor();
	// 이미터 패널 모듈 줄의 커브 버튼 (UE Cascade: 모듈의 그래프 아이콘). 이미 있으면 넣지 않는다.
	void SendToCurveEditor(UParticleModule* Module, const char* Label, FFloatDistribution* Float, FVectorDistribution* Vector, float MinValue, bool bLife);
	// 모듈(또는 이미터의 모든 모듈)을 지우기 전에 부른다. 목록이 지워진 분포를 가리키지 않게 한다.
	void RemoveCurveEntries(const UParticleEmitter* Emitter, const UParticleModule* Module);
	void RemoveLODCurveEntries(int32 LODIndex);
	void SetCurrentLOD(int32 NewLOD);
	void SyncPreviewLOD();

	void MarkDirty() { bDirty = true; }
	void SetDisplayName(const FString& InName);
	// 탭을 닫으려 할 때 저장 확인, 경로가 없을 때 Save As 입력 창
	void DrawSavePopups();

	FString Name;
	FString WindowKey;
	FString WindowName;
	// "Assets/..." 형식. 비어 있으면 저장한 적 없는 문서
	FString AssetPath;
	bool bDirty = false;
	bool bFocused = false;
	// 메인 메뉴바 주인 고르기: 탭이 보이는지, 마지막으로 포커스를 받은 프레임
	bool bTabVisible = false;
	int32 LastFocusFrame = -1;
	// 닫기를 눌렀는데 저장하지 않은 변경이 있을 때 확인 창을 띄운다
	bool bCloseRequested = false;
	bool bOpenSaveAs = false;
	// Save As가 닫기 확인에서 시작됐으면 저장 후 탭을 닫는다
	bool bCloseAfterSave = false;
	// Save As 입력: 폴더("Assets/..." 형식)와 확장자 없는 이름. 저장 경로 = 폴더/이름.particle
	char SaveAsFolder[260] = {};
	char SaveAsName[128] = {};
	// 창이 열린 첫 프레임에 이름 칸에 커서를 둔다
	bool bFocusSaveAsName = false;
	FString PanelNames[PanelCount];
	bool PanelOpen[PanelCount] = { true, true, true, true };
	ImGuiID DockSpaceId = 0;
	ImGuiID PanelClassId = 0;
	bool bResetLayout = false;
	// 새 탭은 도킹 탭바에 들어가는 프레임에 포커스 요청이 사라질 수 있어(빈 Particle Editor 탭이 같은 때 사라짐),
	// 탭이 실제로 보일 때까지 몇 프레임 동안 포커스와 탭 선택을 다시 요청한다.
	static constexpr int32 FocusRetryFrames = 10;
	int32 FocusFramesLeft = FocusRetryFrames;
	TArray<FToolbarItem> ToolbarItems;
	std::function<void(const FString&)> OnBrowseAsset;

	// 편집 대상. development의 DestroyParticleSystem으로 소유 트리를 정리한다.
	UParticleSystem* System = nullptr;
	// Details에 띄울 대상. 모듈 또는 이미터, nullptr이면 파티클 시스템.
	UObject* Selected = nullptr;
	// 우클릭 메뉴를 연 모듈
	UParticleModule* ContextModule = nullptr;
	// Curve Editor에 보낸 분포. Float/Vector 중 하나만 있다. 분포는 Module 안의 멤버를 가리킨다.
	struct FCurveEntry
	{
		UParticleModule* Module = nullptr;
		FString Label;
		FFloatDistribution* Float = nullptr;
		FVectorDistribution* Vector = nullptr;
		float MinValue = -FLT_MAX;	// 키 값이 이 아래로 가지 않게 막는다
		bool bLife = false;	// 가로축이 파티클 수명 비율(0 ~ 1)이다. 키 시간을 0 ~ 1로 묶는다
		// 그래프 표시: Float 선 색, 보이기, 벡터의 X/Y/Z 채널 보이기
		ImU32 Color = 0;
		bool bVisible = true;
		bool bShowChannel[3] = { true, true, true };
	};
	TArray<FCurveEntry> CurveEntries;
	// Curve Editor 그래프가 보여주는 범위 (가로 = 시간, 세로 = 값)
	float CurveViewMinX = 0.0f, CurveViewMaxX = 1.0f;
	float CurveViewMinY = 0.0f, CurveViewMaxY = 1.0f;
	// 목록에 값이 추가되면 다음 그리기에서 모든 커브가 보이게 맞춘다
	bool bCurveFitPending = false;
	void FitCurveView();
	// 보이는 커브가 모두 수명 기준(bLife)이면 가로축을 수명 0 ~ 100%로 고정한다 (팬·줌은 세로만)
	bool IsLifeCurveView() const;
	// 프리셋 버튼이 바꿀 커브 (목록에서 누른 것, 키를 고른 것, 마지막으로 보낸 것). 없으면 -1
	int32 ActiveCurveEntry = -1;
	// 프리셋: 커브 모양을 한 번에 넣는다. 보이는 채널만 바꾸고 크기는 지금 커브의 가장 큰 값을 쓴다.
	enum class ECurvePreset { Flat, FadeOut, FadeIn, Pulse };
	void ApplyCurvePreset(int32 EntryIndex, ECurvePreset Preset);
	// 그래프에서 선택한 키 (목록 번호, 벡터 채널, 키 번호). 목록에서 값이 빠지면 놓는다.
	int32 SelectedCurveEntry = -1;
	int32 SelectedCurveChannel = 0;
	int32 SelectedCurveKey = -1;
	bool bDraggingCurveKey = false;
	bool bCurveKeyMoved = false;
	// 그래프 우클릭 메뉴: 연 위치의 시간/값, 키를 넣을 커브, 마우스 아래 있던 키
	ImVec2 CurveMenuPressPos{ 0.0f, 0.0f };
	float CurveMenuTime = 0.0f, CurveMenuValue = 0.0f;
	int32 CurveMenuEntry = -1, CurveMenuChannel = 0;
	bool bCurveMenuOnCurve = false;
	int32 CurveMenuKeyEntry = -1, CurveMenuKeyChannel = 0, CurveMenuKey = -1;
	void ClearCurveKeySelection() { SelectedCurveEntry = -1; SelectedCurveKey = -1; bDraggingCurveKey = false; bCurveKeyMoved = false; }
	// 키를 넣고, 지우고, 옮긴 뒤 부른다 (Details 편집과 같이 저장 표시 + 시뮬레이션 재시작)
	void OnCurveKeysChanged() { MarkDirty(); RestartSimulation(); }
	// 이미터·모듈 추가/삭제/복제 요청. 이미터 패널을 다 그린 뒤 ApplyPendingEdit()가 적용한다.
	struct FPendingEdit
	{
		enum EType { None, AddModule, RemoveModule, SetTypeData, RefreshModule, AddEmitter, RemoveEmitter, DuplicateEmitter };
		EType Type = None;
		UParticleEmitter* Emitter = nullptr;
		UClass* ModuleClass = nullptr;
		UParticleModule* Module = nullptr;
	};
	FPendingEdit PendingEdit;
	// 이미터 우클릭 메뉴 맨 위 검색창
	char EmitterMenuSearch[64] = {};

	// 프리뷰
	UWorld* World = nullptr;
	UParticleSystemComponent* Component = nullptr;
	FEditorViewportClient Client;
	ImVec2 ViewSize{ 0.0f, 0.0f };
	bool bViewportVisible = false;
	float CameraSpeed = 1.0f;

	// 툴바 상태
	bool bPaused = false;
	bool bRestartPending = false;
	int32 SpeedIndex = 0;
	bool bShowBounds = false;
	// UE Cascade처럼 기본은 검은 배경에 파티클만 보인다. 그리드는 X/Y 축 선을 함께 그린다.
	bool bShowGrid = false;
	bool bShowSkybox = false;
	bool bShowOriginAxis = false;
	bool bPreviewLODByDistance = false;
	int32 CurrentLOD = 0;
};
