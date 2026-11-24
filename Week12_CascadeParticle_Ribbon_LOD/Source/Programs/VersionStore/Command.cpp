#include "Git.h"
#include "Store.h"
#include "Window.h"

#include <fstream>
#include <map>
#include <set>
#include <stdexcept>

namespace VersionStore
{
namespace
{
	std::wstring Option(const std::map<std::wstring, std::wstring>& Options, const wchar_t* Name)
	{
		const auto It = Options.find(Name);
		return It == Options.end() ? std::wstring{} : It->second;
	}
	void Print(const std::string& Text, const fs::path& Destination)
	{
		if (!Destination.empty())
		{
			std::ofstream File(Destination, std::ios::binary);
			File.exceptions(std::ios::badbit | std::ios::failbit);
			File << Text << '\n';
			return;
		}
		AttachConsole(ATTACH_PARENT_PROCESS);
		const auto Stdout = GetStdHandle(STD_OUTPUT_HANDLE);
		if (Stdout && Stdout != INVALID_HANDLE_VALUE)
		{
			DWORD Written{};
			if (GetFileType(Stdout) == FILE_TYPE_CHAR)
			{
				const auto Line = Wide(Text + "\n");
				WriteConsoleW(Stdout, Line.data(), static_cast<DWORD>(Line.size()), &Written, nullptr);
			}
			else
			{
				const auto Line = Text + "\n";
				WriteFile(Stdout, Line.data(), static_cast<DWORD>(Line.size()), &Written, nullptr);
			}
		}
	}
}

int ManagerCommand(const fs::path& Executable, const Args& Arguments)
{
	const auto& Action = Arguments.at(0);
	std::map<std::wstring, std::wstring> Options;
	const std::set<std::wstring> Flags{L"--current-branch", L"--allow-crash"};
	const std::set<std::wstring> Values{L"--repo", L"--store", L"--branch", L"--report",
		L"--artifacts", L"--id", L"--commit", L"--dump", L"--scope", L"--confirm-id", L"--confirm-plan", L"--parent",
		L"--issue-title", L"--issue-body", L"--storage-path", L"--share-name", L"--output"};
	fs::path Output;
	try
	{
		for (size_t Index = 1; Index < Arguments.size(); ++Index)
		{
			const auto Name = Arguments[Index];
			if (Flags.contains(Name)) Options[Name] = L"true";
			else if (Values.contains(Name) && Index + 1 < Arguments.size()) Options[Name] = Arguments[++Index];
			else throw std::runtime_error("Unknown or incomplete option: " + Utf8(Name));
		}
		Output = Option(Options, L"--output");
		const auto Text = [&](const wchar_t* Name) { return Utf8(Option(Options, Name)); };
		const auto ExplicitRepo = Option(Options, L"--repo");
		auto Repository = ExplicitRepo.empty() ? RepositoryFrom(Executable.parent_path()) : fs::absolute(fs::path(ExplicitRepo));
		if (Repository.empty()) Repository = RepositoryFrom(fs::current_path());
		if (Action == L"--help")
		{
			Print("HitoriVersionManager --release|--crash|--list|--delete-dump|--delete-deployment|--resolve|--issue|--initialize-share\n"
				"Options include --repo PATH, --store UNC, --output FILE, --report FILE and --branch NAME.", Output);
			return 0;
		}
		if (Action == L"--initialize-share")
		{
			auto Path = fs::path(Option(Options, L"--storage-path"));
			auto Name = Option(Options, L"--share-name");
			if (Path.empty()) Path = fs::path(Env(L"ProgramData")) / "Week12VersionStore";
			if (Name.empty()) Name = L"VersionStore";
			InitializeShare(Path, Name);
			Print(Utf8(L"\\\\" + Env(L"COMPUTERNAME") + L"\\" + Name), Output);
			return 0;
		}
		if (Action == L"--developer")
		{
			CheckDeveloper(Repository, ResolveDumpFile(Option(Options, L"--report")));
			Print("true", Output);
			return 0;
		}
		auto Root = fs::path(Option(Options, L"--store"));
		if (Root.empty()) Root = ConfiguredStore(Executable.parent_path(), Repository);
		Store Data(Root, Repository);
		Json Result;
		if (Action == L"--list") Result = Data.List(Text(L"--id"), Text(L"--commit"));
		else if (Action == L"--release")
			Result = Data.Release({Option(Options, L"--artifacts"), Text(L"--branch"), Options.contains(L"--current-branch")});
		else if (Action == L"--crash")
			Result = Data.Crash({ResolveDumpFile(Option(Options, L"--report")), Option(Options, L"--artifacts"), Text(L"--branch"),
				Text(L"--parent"), Text(L"--issue-title"), Text(L"--issue-body"), Options.contains(L"--allow-crash")});
		else if (Action == L"--issue") Result = Data.Issue(Text(L"--dump"), Text(L"--issue-title"), Text(L"--issue-body"));
		else if (Action == L"--delete-dump" || Action == L"--delete-deployment" || Action == L"--resolve")
			Result = Data.Remove({Action == L"--delete-dump" ? "DeleteDump" : Action == L"--resolve" ? "Resolve" : "DeleteDeployment",
				Text(L"--scope"), Text(L"--id"), Text(L"--commit"), Text(L"--dump"), Text(L"--confirm-id"), Text(L"--confirm-plan")});
		else throw std::runtime_error("Unknown command: " + Utf8(Action));
		Print(Result.dump(2), Output);
		return 0;
	}
	catch (const std::exception& Error)
	{
		Print(std::string("Error: ") + Error.what(), Output);
		return 1;
	}
}
}
