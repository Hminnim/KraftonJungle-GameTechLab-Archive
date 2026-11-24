#include "Git.h"

#include <winhttp.h>
#include <algorithm>
#include <format>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace VersionStore
{
namespace
{
	Args GitArgs(const fs::path& Repo, const Args& Tail)
	{
		Args Result{L"-C", Repo.native()};
		Result.insert(Result.end(), Tail.begin(), Tail.end());
		return Result;
	}
	struct HttpHandle
	{
		HINTERNET Value{};
		~HttpHandle() { if (Value) WinHttpCloseHandle(Value); }
		operator HINTERNET() const { return Value; }
	};
	void HttpCheck(bool Success)
	{
		if (!Success) throw std::runtime_error(std::format("GitHub connection failed (Windows error {}).", GetLastError()));
	}

}

bool IsHex(std::string_view Text, size_t Size)
{
	return Text.size() == Size && std::all_of(Text.begin(), Text.end(), [](unsigned char Ch) { return std::isxdigit(Ch) != 0; });
}

std::string Git(const fs::path& Repository, const Args& Arguments)
{
	if (Repository.empty()) throw std::runtime_error("Open the tool from a Git checkout.");
	return Check(L"git.exe", GitArgs(Repository, Arguments));
}

bool HasLocalCommit(const fs::path& Repository, const std::string& Sha)
{
	if (Repository.empty() || !IsHex(Sha, 40)) return false;
	const auto Result = Run(L"git.exe", GitArgs(Repository, {L"rev-parse", L"--verify", L"--quiet", L"--end-of-options", Wide(Sha + "^{commit}")}));
	if (Result.Code == 0) return Trim(Result.Out) == Sha;
	if (Result.Code == 1 && Result.Err.empty()) return false;
	throw std::runtime_error("로컬 Git에서 빌드 커밋을 확인하지 못했습니다.\n" + Trim(Result.Err));
}

std::string GitHubRepository(const fs::path& Repository)
{
	const auto Origin = Git(Repository, {L"remote", L"get-url", L"origin"});
	static const std::regex Pattern(R"(^(?:https://github\.com/|git@github\.com:|ssh://git@github\.com/)([A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+?)(?:\.git)?/?$)");
	std::smatch Match;
	if (!std::regex_match(Origin, Match, Pattern)) throw std::runtime_error("Configure origin as a GitHub repository.");
	return Match[1];
}

bool PushBranch(const fs::path& Repository, const std::string& Name, const std::string& Sha, bool NewBranch)
{
	const auto Ref = Wide("refs/heads/" + Name);
	Git(Repository, {L"check-ref-format", Ref});
	const auto Remote = Git(Repository, {L"ls-remote", L"--heads", L"origin", Ref});
	if (!Remote.empty())
	{
		if (Remote.substr(0, Remote.find_first_of(" \t")) == Sha) return true;
		if (NewBranch) return false;
	}
	Args Arguments{L"push"};
	if (NewBranch) Arguments.push_back(L"--force-with-lease=" + Ref + L":");
	Arguments.insert(Arguments.end(), {L"origin", Wide(Sha) + L":" + Ref});
	Git(Repository, Arguments);
	return true;
}

Json GitHub::Request(const std::string& Endpoint, const Json& Body, bool MissingAllowed)
{
	static const std::regex RepoPattern(R"(^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$)");
	if (!std::regex_match(Name, RepoPattern)) throw std::runtime_error("Invalid GitHub repository name.");
	if (Token.empty())
	{
		Token = Utf8(Env(L"GH_TOKEN"));
		if (Token.empty()) Token = Utf8(Env(L"GITHUB_TOKEN"));
		if (Token.empty() && !Repository.empty())
		{
			const auto Result = Run(L"git.exe", GitArgs(Repository, {L"-c", L"credential.interactive=never", L"credential", L"fill"}),
				"protocol=https\nhost=github.com\npath=" + Name + ".git\n\n", L"GIT_TERMINAL_PROMPT", L"0");
			std::istringstream Lines(Result.Out);
			for (std::string Line; std::getline(Lines, Line);)
				if (Line.starts_with("password=")) Token = Trim(Line.substr(9));
		}
		if (Token.empty()) throw std::runtime_error("Sign in with Git Credential Manager or set GH_TOKEN in the process environment.");
	}
	if (Token.find_first_of("\r\n") != Token.npos) throw std::runtime_error("Invalid GitHub credential.");
	HttpHandle Session{WinHttpOpen(L"HitoriVersionStore/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0)};
	HttpCheck(Session.Value != nullptr);
	WinHttpSetTimeouts(Session, 10000, 15000, 15000, 30000);
	HttpHandle Connection{WinHttpConnect(Session, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0)};
	HttpCheck(Connection.Value != nullptr);
	const auto Uri = Wide("/repos/" + Name + (Endpoint.empty() ? "" : "/" + Endpoint));
	HttpHandle Request{WinHttpOpenRequest(Connection, Body.is_null() ? L"GET" : L"POST", Uri.c_str(), nullptr,
		WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
	HttpCheck(Request.Value != nullptr);
	const DWORD RedirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
	HttpCheck(WinHttpSetOption(Request, WINHTTP_OPTION_REDIRECT_POLICY, const_cast<DWORD*>(&RedirectPolicy), sizeof(RedirectPolicy)));
	const auto Headers = Wide("Authorization: Bearer " + Token + "\r\nAccept: application/vnd.github+json\r\nContent-Type: application/json\r\n");
	auto Data = Body.is_null() ? std::string{} : Body.dump();
	HttpCheck(WinHttpSendRequest(Request, Headers.c_str(), static_cast<DWORD>(Headers.size()), Data.empty() ? nullptr : Data.data(),
		static_cast<DWORD>(Data.size()), static_cast<DWORD>(Data.size()), 0));
	HttpCheck(WinHttpReceiveResponse(Request, nullptr));
	DWORD Status{}, StatusSize = sizeof(Status);
	HttpCheck(WinHttpQueryHeaders(Request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		nullptr, &Status, &StatusSize, nullptr));
	if (MissingAllowed && (Status == 404 || Status == 422)) return {};
	if (Status < 200 || Status >= 300) throw std::runtime_error(std::format("GitHub returned HTTP {} for {}.", Status, Name));
	std::string Response;
	char Buffer[16384];
	for (;;)
	{
		DWORD Size{};
		HttpCheck(WinHttpReadData(Request, Buffer, sizeof(Buffer), &Size));
		if (!Size) break;
		Response.append(Buffer, Size);
	}
	return Json::parse(Response);
}

void GitHub::RequireDeveloper()
{
	const auto Info = Request("");
	if (!Info.contains("permissions") || !Info["permissions"].value("push", false))
		throw std::runtime_error("GitHub write access to " + Name + " is required.");
}

bool GitHub::HasCommit(const std::string& Sha)
{
	return IsHex(Sha, 40) && Field(Request("commits/" + Sha, nullptr, true), "sha") == Sha;
}

std::string GitHub::DefaultCommit()
{
	const auto Commits = Request("commits?per_page=1");
	if (Commits.empty()) throw std::runtime_error("GitHub repository has no commits.");
	return Field(Commits.at(0), "sha");
}

std::string GitHub::Issue(const std::string& Marker, const std::string& Title, const std::string& Body)
{
	for (unsigned Page = 1;; ++Page)
	{
		const auto Issues = Request("issues?state=all&per_page=100&page=" + std::to_string(Page));
		for (const auto& Issue : Issues)
			if (Field(Issue, "body").find(Marker) != std::string::npos) return Field(Issue, "html_url");
		if (Issues.size() < 100) break;
	}
	return Field(Request("issues", {{"title", Title}, {"body", Body + "\n\n" + Marker}}), "html_url");
}

fs::path LatestArtifacts(const fs::path& Repository)
{
	const auto Latest = Field(Read(Repository / "Build/VersionStore/local-builds/latest.json"), "artifacts");
	if (Latest.empty()) throw std::runtime_error("Build a versioned snapshot first.");
	return Wide(Latest);
}

}
