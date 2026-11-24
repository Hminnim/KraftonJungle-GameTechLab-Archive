#pragma once

#include "EnginePCH.h"
#include <Windows.h>
#include "LaunchEngineLoop.h"
#include "Launch/CrashReporting.h"

// 실행 파일이 어떤 엔진 모드(UEngine 서브클래스)로 돌지 정한다. UE의 ini GameEngine= 설정 역할.
extern UClass* GetEngineClass();
// 에디터는 브라우저에서 선택하고 게임은 exe 옆의 프로젝트를 연다.
extern std::filesystem::path ResolveProjectFile(HINSTANCE hInstance, const std::filesystem::path& FromCommandLine);

// 실행 파일의 전체 경로. 긴 경로(longPathAware)에서도 잘리지 않도록 버퍼를 늘려 가며 다시 읽는다.
inline std::filesystem::path GetExecutablePath()
{
	std::wstring Buffer(MAX_PATH, L'\0');
	for (;;)
	{
		const DWORD Length = GetModuleFileNameW(nullptr, Buffer.data(), static_cast<DWORD>(Buffer.size()));
		if (Length == 0) return {};
		if (Length < Buffer.size()) { Buffer.resize(Length); return Buffer; }
		if (Buffer.size() >= 32768) return {};
		Buffer.resize(Buffer.size() * 2);
	}
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	CrashReporting::Initialize();
	if (_stricmp(lpCmdLine, "-causecrash") == 0) CrashReporting::CauseCrash();
	{
		FEngineLoop EngineLoop;
		if (!EngineLoop.PreInit(hInstance, GetEngineClass())) return -1;

		while (EngineLoop.Tick())
		{
		}

		EngineLoop.Exit();
	}

	if (FEngineStatics::TotalAllocationCount > 0)
		OutputDebugStringA("leaked UObjects\n");

	return 0;
}
