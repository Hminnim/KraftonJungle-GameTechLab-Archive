#pragma once
#include "Binary.h"

namespace VersionStore
{
struct ReleaseRequest { fs::path Directory; std::string Branch; bool CurrentBranch{}; };
struct ReportRequest
{
	fs::path DumpFile, Artifacts;
	std::string Branch, Parent, IssueTitle, IssueBody;
	bool AllowCrash{};
};
struct DeleteRequest
{
	std::string Action, Scope, Id, Commit, DumpNumber, ConfirmId, ConfirmPlan;
};
fs::path ConfiguredStore(const fs::path& ExecutableDirectory, const fs::path& Repository);
void SaveStore(const fs::path& Store);
fs::path ResolveDumpFile(const fs::path& Report);
void CheckDeveloper(const fs::path& Repository, const fs::path& DumpFile = {});

class Store
{
public:
	Store(const fs::path& Root, fs::path Repository);
	Json List(const std::string& Id = {}, const std::string& Commit = {}) const;
	Json Release(const ReleaseRequest& Request);
	Json Crash(const ReportRequest& Request);
	Json Issue(const std::string& Dump, const std::string& Title, const std::string& Body);
	Json Remove(const DeleteRequest& Request);
private:
	Json Records(const char* Filename) const;
	Json Active(const std::string& BuildId, const Module* Image = nullptr) const;
	Json Register(const std::string& Kind, const std::string& BuildId, const Json& Build, const fs::path& Artifacts,
		const std::string& Exe, const ReleaseRequest& Release, const ReportRequest& Report, const DumpInfo* Dump);
	Json RecordBranch(const std::string& Name, const std::string& Repo, const std::string& Sha,
		const std::string& Base, const std::string& Parent);
	std::string Next(const char* Counter);
	std::string AddSymbols(const Json& Modules, const std::string& Marker);
	void RemoveSymbols(const std::string& Transaction);
	void AddIssue(const Json& Report, const Json& Deployment, const std::string& Title, const std::string& Body);
	void RemoveDump(const Json& Dump);
	void RemoveDeployment(Json Deployment, const Json& Dumps);
	void RemoveEmptyParents(fs::path Directory) const;
	fs::path Data, Symbols, Repository;
};
}
