#include <Windows.h>
#include <DbgHelp.h>
#include <objbase.h>
#include <shellapi.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "../../Runtime/Launch/CrashCaptureRequest.h"
#include "../VersionStore/Window.h"
#include "../VersionStore/Binary.h"

namespace
{
	namespace fs = std::filesystem;

	bool Capture(HANDLE Target, DWORD Tid, EXCEPTION_POINTERS* Context, const fs::path& Directory)
	{
		const DWORD Pid = GetProcessId(Target);
		if (!Pid) return false;
		wchar_t Image[32768];
		DWORD Length = static_cast<DWORD>(std::size(Image));
		bool Written = false;
		if (QueryFullProcessImageNameW(Target, 0, Image, &Length))
		{
			fs::create_directories(Directory);
			const auto DumpPath = Directory / L"crash.dmp";
			const HANDLE Dump = CreateFileW(DumpPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
				FILE_ATTRIBUTE_NORMAL, nullptr);
			if (Dump != INVALID_HANDLE_VALUE)
			{
				MINIDUMP_EXCEPTION_INFORMATION Exception{};
				Exception.ThreadId = Tid;
				Exception.ExceptionPointers = Context;
				Exception.ClientPointers = TRUE;
				Written = MiniDumpWriteDump(Target, Pid, Dump, MiniDumpNormal, &Exception, nullptr, nullptr) != FALSE;
				CloseHandle(Dump);
				if (!Written) DeleteFileW(DumpPath.c_str());
			}

			if (Written)
			{
				// Keep the original binary path and a copy of its small build manifest for later posting.
				std::ofstream Origin(Directory / L"origin.txt", std::ios::binary);
				const char16_t Bom = 0xFEFF;
				Origin.write(reinterpret_cast<const char*>(&Bom), sizeof(Bom));
				Origin.write(reinterpret_cast<const char*>(Image), Length * sizeof(wchar_t));
				try
				{
					const auto Build = VersionStore::ReadBuildManifest(fs::path(Image));
					if (!Build.is_null()) VersionStore::Write(Directory / L"build.json", Build);
				}
				catch (const std::exception&) {}
			}
		}
		return Written;
	}

	int CaptureAndOpenReport(const fs::path& Self, HANDLE Target, DWORD Tid, EXCEPTION_POINTERS* Exception, const wchar_t* Directory)
	{
		if (!Capture(Target, Tid, Exception, Directory)) return 1;
		if (!GetEnvironmentVariableW(L"HITORI_CRASH_NO_UI", nullptr, 0))
			VersionStore::StartDetached(Self, {L"--report", Directory});
		return 0;
	}

	HANDLE InheritedHandle(const wchar_t* Value)
	{
		return reinterpret_cast<HANDLE>(static_cast<uintptr_t>(std::stoull(Value, nullptr, 16)));
	}

	int Watch(const fs::path& Self, wchar_t** Args)
	{
		using CrashReporting::CrashCaptureRequest;
		const VersionStore::Handle Target(InheritedHandle(Args[2]));
		const VersionStore::Handle CrashEvent(InheritedHandle(Args[3]));
		const VersionStore::Handle Mapping(InheritedHandle(Args[4]));
		const VersionStore::Handle Ready(InheritedHandle(Args[5]));
		const std::unique_ptr<const CrashCaptureRequest, decltype(&UnmapViewOfFile)> Request(
			static_cast<const CrashCaptureRequest*>(MapViewOfFile(Mapping, FILE_MAP_READ, 0, 0, sizeof(CrashCaptureRequest))),
			UnmapViewOfFile);
		if (!Request || !SetEvent(Ready)) return 1;

		// Give process exit priority if the crash arrived too late to read the target.
		const HANDLE Signals[] = {Target, CrashEvent};
		const DWORD Signal = WaitForMultipleObjects(2, Signals, FALSE, INFINITE);
		if (Signal == WAIT_OBJECT_0) return 0;
		if (Signal != WAIT_OBJECT_0 + 1 || !Request->ThreadId || !Request->Exception) return 1;
		return CaptureAndOpenReport(Self, Target, Request->ThreadId, Request->Exception, Args[6]);
	}

	int Run(int Count, wchar_t** Args)
	{
		const fs::path Self = Args[0];
		if (Count == 7 && std::wstring_view(Args[1]) == L"--watch") return Watch(Self, Args);
		// Keep capture working for older editor and game binaries using this reporter.
		if (Count == 6 && std::wstring_view(Args[1]) == L"--capture")
		{
			const auto Pid = std::stoul(Args[2]);
			const auto Tid = std::stoul(Args[3]);
			const auto Context = reinterpret_cast<EXCEPTION_POINTERS*>(static_cast<uintptr_t>(std::stoull(Args[4])));
			const VersionStore::Handle Target(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, Pid));
			return Target ? CaptureAndOpenReport(Self, Target, Tid, Context, Args[5]) : 1;
		}

		if (Count == 3 && std::wstring_view(Args[1]) == L"--report")
			return VersionStore::ReporterUI(Self, Args[2]);
		if (Count != 1) return 2;
		return VersionStore::ReporterUI(Self, {});
	}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	int Count = 0;
	wchar_t** Args = CommandLineToArgvW(GetCommandLineW(), &Count);
	if (!Args) return 1;
	const bool Capturing = (Count == 7 && std::wstring_view(Args[1]) == L"--watch") ||
		(Count == 6 && std::wstring_view(Args[1]) == L"--capture");
	const auto COM = Capturing ? E_FAIL : CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	int Result = 1;
	try { Result = Run(Count, Args); }
	catch (const std::exception& Error)
	{
		if (!Capturing) MessageBoxW(nullptr, VersionStore::Wide(Error.what()).c_str(), L"Hitori", MB_ICONERROR);
	}
	if (SUCCEEDED(COM)) CoUninitialize();
	LocalFree(Args);
	return Result;
}
