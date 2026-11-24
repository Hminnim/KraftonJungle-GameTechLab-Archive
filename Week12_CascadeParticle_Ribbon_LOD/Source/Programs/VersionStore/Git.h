#pragma once
#include "Platform.h"

namespace VersionStore
{
std::string Git(const fs::path& Repository, const Args& Arguments);
bool HasLocalCommit(const fs::path& Repository, const std::string& Sha);
std::string GitHubRepository(const fs::path& Repository);
bool PushBranch(const fs::path& Repository, const std::string& Name, const std::string& Sha, bool NewBranch);
bool IsHex(std::string_view Text, size_t Size);
fs::path LatestArtifacts(const fs::path& Repository);

class GitHub
{
public:
	GitHub(fs::path Repository, std::string Name) : Repository(std::move(Repository)), Name(std::move(Name)) {}
	void RequireDeveloper();
	bool HasCommit(const std::string& Sha);
	std::string DefaultCommit();
	std::string Issue(const std::string& Marker, const std::string& Title, const std::string& Body);
private:
	Json Request(const std::string& Endpoint, const Json& Body = {}, bool MissingAllowed = false);
	fs::path Repository;
	std::string Name, Token;
};
}
