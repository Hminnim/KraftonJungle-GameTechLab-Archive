#include "EnginePCH.h"

#include "Launch/EntryPoint.h"

#include "Editor/HitoriEd/EditorEngine.h"
#include "Editor/ProjectBrowser/ProjectBrowser.h"
#include "Core/Misc/Paths.h"

UClass* GetEngineClass()
{
	return UEditorEngine::StaticClass();
}

std::filesystem::path ResolveProjectFile(HINSTANCE hInstance, const std::filesystem::path& FromCommandLine)
{
	const auto ProjectFile = FromCommandLine.empty() ? ProjectBrowser::Run(hInstance) : FromCommandLine;
	FProjectDescriptor Descriptor;
	if (!ProjectFile.empty() && Descriptor.Load(ProjectFile)) RecentProjects::Add(ProjectFile);
	return ProjectFile;
}
