#include "EnginePCH.h"
#include "Launch/CrashCaptureRequest.h"
#include "Launch/CrashReporting.h"

#include <Windows.h>
#include <objbase.h>
#include <memory>
#include <new>

namespace
{
	using ScopedHandle = std::unique_ptr<void, decltype(&CloseHandle)>;
	CrashReporting::CrashCaptureRequest* CaptureRequest = nullptr;
	HANDLE CrashEvent = nullptr;
	HANDLE Collector = nullptr;
	volatile LONG HandlingCrash = 0;

	LONG WINAPI ReportUnhandledException(EXCEPTION_POINTERS* Exception)
	{
		if (InterlockedCompareExchange(&HandlingCrash, 1, 0) == 0)
		{
			CaptureRequest->ThreadId = GetCurrentThreadId();
			CaptureRequest->Exception = Exception;
			if (!SetEvent(CrashEvent)) return EXCEPTION_EXECUTE_HANDLER;
		}
		// Keep the target and its exception context alive until the collector exits.
		WaitForSingleObject(Collector, 30000);
		return EXCEPTION_EXECUTE_HANDLER;
	}

	HANDLE StartCollector(const wchar_t* Reporter, const wchar_t* ReportDirectory,
		HANDLE Target, HANDLE Signal, HANDLE Mapping, HANDLE Ready)
	{
		wchar_t Command[MAX_PATH * 2 + 128];
		if (swprintf_s(Command, L"\"%s\" --watch %p %p %p %p \"%s\"",
			Reporter, Target, Signal, Mapping, Ready, ReportDirectory) < 0)
			return nullptr;

		SIZE_T Size = 0;
		InitializeProcThreadAttributeList(nullptr, 1, 0, &Size);
		const std::unique_ptr<std::byte[]> Buffer(new (std::nothrow) std::byte[Size]);
		if (!Buffer) return nullptr;
		STARTUPINFOEXW Startup{};
		Startup.StartupInfo.cb = sizeof(Startup);
		Startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(Buffer.get());
		if (!InitializeProcThreadAttributeList(Startup.lpAttributeList, 1, 0, &Size)) return nullptr;
		HANDLE Handles[] = {Target, Signal, Mapping, Ready};
		PROCESS_INFORMATION Child{};
		const bool Started = UpdateProcThreadAttribute(Startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
			Handles, sizeof(Handles), nullptr, nullptr) &&
			CreateProcessW(Reporter, Command, nullptr, nullptr, TRUE, EXTENDED_STARTUPINFO_PRESENT,
				nullptr, nullptr, &Startup.StartupInfo, &Child);
		DeleteProcThreadAttributeList(Startup.lpAttributeList);
		if (!Started) return nullptr;
		CloseHandle(Child.hThread);
		const HANDLE WaitHandles[] = {Child.hProcess, Ready};
		if (WaitForMultipleObjects(2, WaitHandles, FALSE, 5000) != WAIT_OBJECT_0 + 1)
		{
			TerminateProcess(Child.hProcess, 1);
			CloseHandle(Child.hProcess);
			return nullptr;
		}
		return Child.hProcess;
	}
}

void CrashReporting::Initialize()
{
	ReserveExceptionStack();
	wchar_t Executable[MAX_PATH];
	const DWORD Length = GetModuleFileNameW(nullptr, Executable, MAX_PATH);
	if (Length == 0 || Length >= MAX_PATH) return;
	wchar_t* Filename = wcsrchr(Executable, L'\\');
	if (!Filename) return;
	Filename[1] = L'\0';
	wchar_t Reporter[MAX_PATH];
	if (swprintf_s(Reporter, L"%sHitoriCrashReporter.exe", Executable) < 0) return;

	wchar_t LocalData[MAX_PATH];
	const DWORD LocalLength = GetEnvironmentVariableW(L"LOCALAPPDATA", LocalData, MAX_PATH);
	if (LocalLength == 0 || LocalLength >= MAX_PATH) return;
	wchar_t Root[MAX_PATH];
	if (swprintf_s(Root, L"%s\\HitoriCrashReports", LocalData) < 0) return;
	if (!CreateDirectoryW(Root, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return;

	GUID Id{};
	wchar_t IdText[40];
	if (FAILED(CoCreateGuid(&Id)) || !StringFromGUID2(Id, IdText, 40)) return;
	wchar_t ReportDirectory[MAX_PATH];
	if (swprintf_s(ReportDirectory, L"%s\\%s", Root, IdText) < 0) return;

	SECURITY_ATTRIBUTES Inheritable{sizeof(Inheritable), nullptr, TRUE};
	const ScopedHandle Target(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE,
		TRUE, GetCurrentProcessId()), CloseHandle);
	const ScopedHandle Mapping(CreateFileMappingW(INVALID_HANDLE_VALUE, &Inheritable, PAGE_READWRITE,
		0, sizeof(CrashCaptureRequest), nullptr), CloseHandle);
	ScopedHandle Signal(CreateEventW(&Inheritable, TRUE, FALSE, nullptr), CloseHandle);
	const ScopedHandle Ready(CreateEventW(&Inheritable, TRUE, FALSE, nullptr), CloseHandle);
	if (!Target || !Mapping || !Signal || !Ready) return;
	std::unique_ptr<CrashCaptureRequest, decltype(&UnmapViewOfFile)> Request(
		static_cast<CrashCaptureRequest*>(MapViewOfFile(Mapping.get(), FILE_MAP_WRITE, 0, 0, sizeof(CrashCaptureRequest))),
		UnmapViewOfFile);
	if (!Request) return;
	Collector = StartCollector(Reporter, ReportDirectory, Target.get(), Signal.get(), Mapping.get(), Ready.get());
	if (!Collector) return;
	// These objects stay valid for the exception filter until the process exits.
	SetHandleInformation(Signal.get(), HANDLE_FLAG_INHERIT, 0);
	CrashEvent = Signal.release();
	CaptureRequest = Request.release();
	SetUnhandledExceptionFilter(ReportUnhandledException);
}

void CrashReporting::ReserveExceptionStack()
{
	ULONG Bytes = 64 * 1024;
	SetThreadStackGuarantee(&Bytes);
}

[[noreturn]] void CrashReporting::CauseCrash()
{
	RaiseException(0xefff'ffff, EXCEPTION_NONCONTINUABLE, 0, nullptr);
	TerminateProcess(GetCurrentProcess(), 0xefff'ffff);
	std::abort();
}
