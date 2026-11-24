#pragma once
#include "Platform.h"

namespace VersionStore
{
int Window(const wchar_t* Title, const std::function<void(HWND)>& Draw, int Width = 950, int Height = 620);
int ManagerUI(const fs::path& Executable);
int ReporterUI(const fs::path& Executable, fs::path Report);
int ManagerCommand(const fs::path& Executable, const Args& Arguments);
}
