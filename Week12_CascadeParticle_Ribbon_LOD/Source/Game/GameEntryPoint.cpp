#include "EnginePCH.h"

#include "Launch/EntryPoint.h"
#include "Engine/GameEngine.h"
#include "Core/Misc/Paths.h"

UClass* GetEngineClass()
{
	return UGameEngine::StaticClass();
}

std::filesystem::path ResolveProjectFile(HINSTANCE, const std::filesystem::path& FromCommandLine)
{
	if (!FromCommandLine.empty()) return FromCommandLine;
	const auto BesideExe = FPaths::FindProjectFileIn(GetExecutablePath().parent_path());
	if (!BesideExe.empty()) return BesideExe;
	const auto DefaultProject = FPaths::FindProjectFileIn(FPaths::EngineDir());
	if (DefaultProject.empty())
		MessageBoxW(nullptr, L"No .hproject file found. Pass a project file on the command line.", L"Hitori", MB_OK | MB_ICONERROR);
	return DefaultProject;
}
