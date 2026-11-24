#pragma once
#include "Platform.h"

namespace VersionStore
{
struct Module
{
	std::string Name, CodeView;
	uint32_t Timestamp{}, Size{};
	fs::path ImagePath;
};
struct DumpInfo
{
	std::vector<Module> Modules;
	std::string ExceptionCode;
};
DumpInfo ReadDump(const fs::path& Path);
Module ReadImage(const fs::path& Path);
Json ReadBuildManifest(const fs::path& Image);
std::string ReadPdb(const fs::path& Path);
void RestorePdbIdentity(const fs::path& Original, const fs::path& Modified);
}
