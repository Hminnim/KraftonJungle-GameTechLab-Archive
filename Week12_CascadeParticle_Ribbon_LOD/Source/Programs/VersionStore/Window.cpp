#include "Window.h"

#include <d3d11.h>
#include <wrl/client.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <stdexcept>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace VersionStore
{
namespace
{
	using Microsoft::WRL::ComPtr;
	LRESULT CALLBACK WindowProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
	{
		if (ImGui_ImplWin32_WndProcHandler(Window, Message, WParam, LParam)) return TRUE;
		if (Message == WM_DESTROY) { PostQuitMessage(0); return 0; }
		return DefWindowProcW(Window, Message, WParam, LParam);
	}
}

int Window(const wchar_t* Title, const std::function<void(HWND)>& Draw, int Width, int Height)
{
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	const auto Instance = GetModuleHandleW(nullptr);
	const wchar_t* ClassName = L"HitoriVersionStoreWindow";
	WNDCLASSEXW Class{sizeof(Class), CS_CLASSDC, WindowProc, 0, 0, Instance, nullptr, LoadCursor(nullptr, IDC_ARROW),
		nullptr, nullptr, ClassName, nullptr};
	if (!RegisterClassExW(&Class)) throw std::runtime_error("Cannot register the Hitori tool window.");
	const float Scale = GetDpiForSystem() / 96.0f;
	RECT Size{0, 0, static_cast<LONG>(Width * Scale), static_cast<LONG>(Height * Scale)};
	AdjustWindowRectExForDpi(&Size, WS_OVERLAPPEDWINDOW, FALSE, 0, GetDpiForSystem());
	const HWND Hwnd = CreateWindowExW(0, ClassName, Title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
		Size.right - Size.left, Size.bottom - Size.top, nullptr, nullptr, Instance, nullptr);
	if (!Hwnd) throw std::runtime_error("Cannot open the Hitori tool window.");
	struct Cleanup
	{
		HWND Hwnd;
		HINSTANCE Instance;
		const wchar_t* Class;
		bool Win32{}, DX11{}, Context{};
		~Cleanup()
		{
			if (DX11) ImGui_ImplDX11_Shutdown();
			if (Win32) ImGui_ImplWin32_Shutdown();
			if (Context) ImGui::DestroyContext();
			if (IsWindow(Hwnd)) DestroyWindow(Hwnd);
			UnregisterClassW(Class, Instance);
		}
	} Guard{Hwnd, Instance, ClassName};
	DXGI_SWAP_CHAIN_DESC Description{};
	Description.BufferCount = 2;
	Description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	Description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	Description.OutputWindow = Hwnd;
	Description.SampleDesc.Count = 1;
	Description.Windowed = TRUE;
	Description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	ComPtr<IDXGISwapChain> Swapchain;
	ComPtr<ID3D11Device> Device;
	ComPtr<ID3D11DeviceContext> Context;
	auto CreateDevice = [&](D3D_DRIVER_TYPE Type)
	{
		return D3D11CreateDeviceAndSwapChain(nullptr, Type, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &Description,
			Swapchain.ReleaseAndGetAddressOf(), Device.ReleaseAndGetAddressOf(), nullptr, Context.ReleaseAndGetAddressOf());
	};
	if (FAILED(CreateDevice(D3D_DRIVER_TYPE_HARDWARE)) && FAILED(CreateDevice(D3D_DRIVER_TYPE_WARP)))
		throw std::runtime_error("Cannot initialize DirectX for the Hitori tool window.");

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	Guard.Context = true;
	auto& Io = ImGui::GetIO();
	Io.IniFilename = nullptr;
	Io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	const auto Font = fs::path(Env(L"WINDIR")) / "Fonts/malgun.ttf";
	if (fs::is_regular_file(Font)) Io.Fonts->AddFontFromFileTTF(PathText(Font).c_str(), 17.0f * Scale, nullptr, Io.Fonts->GetGlyphRangesKorean());
	ImGui::StyleColorsDark();
	ImGui::GetStyle().ScaleAllSizes(Scale);
	Guard.Win32 = ImGui_ImplWin32_Init(Hwnd);
	Guard.DX11 = ImGui_ImplDX11_Init(Device.Get(), Context.Get());
	if (!Guard.Win32 || !Guard.DX11) throw std::runtime_error("Cannot initialize ImGui for the Hitori tool window.");
	ShowWindow(Hwnd, SW_SHOW);
	UpdateWindow(Hwnd);

	ComPtr<ID3D11RenderTargetView> Target;
	UINT PreviousWidth{}, PreviousHeight{};
	for (;;)
	{
		MSG Message{};
		while (PeekMessageW(&Message, nullptr, 0, 0, PM_REMOVE))
		{
			if (Message.message == WM_QUIT) return 0;
			TranslateMessage(&Message);
			DispatchMessageW(&Message);
		}
		RECT Rect{};
		GetClientRect(Hwnd, &Rect);
		const auto Width = static_cast<UINT>(Rect.right), Height = static_cast<UINT>(Rect.bottom);
		if (!Width || !Height || IsIconic(Hwnd)) { Sleep(30); continue; }
		if (Width != PreviousWidth || Height != PreviousHeight)
		{
			PreviousWidth = Width;
			PreviousHeight = Height;
			Context->OMSetRenderTargets(0, nullptr, nullptr);
			Target.Reset();
			if (FAILED(Swapchain->ResizeBuffers(0, Width, Height, DXGI_FORMAT_UNKNOWN, 0)))
				throw std::runtime_error("Cannot resize the Hitori tool window.");
			ComPtr<ID3D11Texture2D> Buffer;
			if (FAILED(Swapchain->GetBuffer(0, IID_PPV_ARGS(&Buffer))) ||
				FAILED(Device->CreateRenderTargetView(Buffer.Get(), nullptr, &Target)))
				throw std::runtime_error("Cannot create the Hitori render target.");
		}
		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
		ImGui::SetNextWindowPos({0, 0});
		ImGui::SetNextWindowSize(Io.DisplaySize);
		ImGui::Begin("Hitori##tool", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
		Draw(Hwnd);
		ImGui::End();
		ImGui::Render();
		const float Clear[4]{0.08f, 0.08f, 0.09f, 1.0f};
		auto* View = Target.Get();
		Context->OMSetRenderTargets(1, &View, nullptr);
		Context->ClearRenderTargetView(View, Clear);
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
		Swapchain->Present(1, 0);
	}
}
}
