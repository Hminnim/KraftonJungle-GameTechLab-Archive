#include "../VersionStore/Window.h"
#include <objbase.h>
#include <shellapi.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	int Count = 0;
	auto** Argv = CommandLineToArgvW(GetCommandLineW(), &Count);
	if (!Argv) return 1;
	const std::filesystem::path Executable(Argv[0]);
	const VersionStore::Args Arguments(Argv + 1, Argv + Count);
	LocalFree(Argv);
	if (!Arguments.empty()) return VersionStore::ManagerCommand(Executable, Arguments);
	const auto COM = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	int Result = 1;
	try { Result = VersionStore::ManagerUI(Executable); }
	catch (const std::exception& Error) { MessageBoxW(nullptr, VersionStore::Wide(Error.what()).c_str(), L"Hitori", MB_ICONERROR); }
	if (SUCCEEDED(COM)) CoUninitialize();
	return Result;
}
