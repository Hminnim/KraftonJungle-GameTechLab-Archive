#include "Store.h"
#include "Git.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>

namespace VersionStore
{
namespace
{
	fs::path RecordPath(const Json& Record) { return Wide(Field(Record, "recordPath")); }
	std::string SymbolRegistration(const fs::path& Symbols, const std::string& Marker)
	{
		const auto Admin = Symbols / "000Admin";
		std::ifstream Input(Admin / "history.txt");
		std::string Result;
		for (std::string Line; std::getline(Input, Line);)
		{
			const auto Comma = Line.find(',');
			if (Comma == Line.npos || !Line.substr(Comma).starts_with(",add,file,") || Line.find("\"" + Marker + "\"") == Line.npos) continue;
			const auto Number = Line.substr(0, Comma);
			if (!std::all_of(Number.begin(), Number.end(), [](unsigned char Ch) { return std::isdigit(Ch) != 0; }) ||
				!fs::is_regular_file(Admin / Number)) continue;
			if (!Result.empty()) throw std::runtime_error("Several SymStore transactions matched the operation.");
			Result = Number;
		}
		return Result;
	}
	void SaveRecord(const fs::path& Path, const Json& Record)
	{
		auto Value = Record;
		Value.erase("recordPath");
		Write(Path, Value);
	}
	bool NameEquals(const std::string& Left, const std::string& Right)
	{
		return _wcsicmp(Wide(Left).c_str(), Wide(Right).c_str()) == 0;
	}
	const Module* FindModule(const DumpInfo& Dump, const std::string& Name)
	{
		for (const auto& Image : Dump.Modules) if (NameEquals(Image.Name, Name)) return &Image;
		return nullptr;
	}
	bool MatchesDump(const Module& File, const Module& Expected)
	{
		return File.Timestamp == Expected.Timestamp && File.Size == Expected.Size &&
			(Expected.CodeView.empty() || File.CodeView == Expected.CodeView);
	}
	bool HasDumpImage(const fs::path& Directory, const Module& Expected)
	{
		if (Directory.empty()) return false;
		try
		{
			const auto File = Directory / Wide(Expected.Name);
			return fs::is_regular_file(File) && MatchesDump(ReadImage(File), Expected);
		}
		catch (const std::exception&) { return false; }
	}
	fs::path LocalImageDirectory(const fs::path& Image)
	{
		const auto Drive = Image.root_name().native();
		if (!Image.is_absolute() || Drive.size() != 2 || Drive[1] != L':') return {};
		const auto Kind = GetDriveTypeW(Image.root_path().c_str());
		return Kind == DRIVE_FIXED || Kind == DRIVE_REMOVABLE || Kind == DRIVE_RAMDISK ? Image.parent_path() : fs::path{};
	}
	fs::path FindCrashArtifacts(const ReportRequest& Request, const fs::path& Repository, const fs::path& Origin,
		const std::string& RecordedBuildId, const Module& Image)
	{
		if (!Request.Artifacts.empty())
		{
			if (!HasDumpImage(Request.Artifacts, Image))
				throw std::runtime_error("원래 빌드 폴더에 덤프와 일치하는 EXE가 없습니다: " + Image.Name);
			return Request.Artifacts;
		}
		if (!Repository.empty())
		{
			const auto Latest = Read(Repository / "Build/VersionStore/local-builds/latest.json");
			const fs::path Candidate(Wide(Field(Latest, "artifacts")));
			if ((RecordedBuildId.empty() || Field(Latest, "buildId") == RecordedBuildId) && HasDumpImage(Candidate, Image))
				return Candidate;
		}
		// The automatic reporter records the process path in origin.txt.
		if (Origin.is_absolute() && HasDumpImage(Origin.parent_path(), Image)) return Origin.parent_path();
		// A manually saved dump supplies the image path through its module list.
		const auto RecordedFolder = LocalImageDirectory(Image.ImagePath);
		if (HasDumpImage(RecordedFolder, Image)) return RecordedFolder;
		throw std::runtime_error("최신 빌드와 덤프에 기록된 위치에서 원래 EXE를 찾지 못했습니다: " + Image.Name);
	}
	bool Matches(const Json& Module, const struct Module& Image)
	{
		return NameEquals(Field(Module, "name"), Image.Name) && Module.value("timestamp", uint32_t{}) == Image.Timestamp &&
			Module.value("imageSize", uint32_t{}) == Image.Size && Field(Module, "codeView") == Image.CodeView;
	}
	Json Find(const Json& Records, const char* Key, const std::string& Value)
	{
		for (const auto& Record : Records) if (Field(Record, Key) == Value) return Record;
		return {};
	}
	Json BuildFromArtifacts(const fs::path& Directory, const Module& Image)
	{
		auto Build = ReadBuildManifest(Directory / Wide(Image.Name));
		return IsHex(Field(Build, "buildId"), 32) ? Build : Json{};
	}
	std::set<std::string> Descendants(const Json& Branches, const std::string& Id)
	{
		std::set<std::string> Result{Id};
		size_t Before{};
		do
		{
			Before = Result.size();
			for (const auto& Branch : Branches)
				if (Result.contains(Field(Branch, "parentBranchId"))) Result.insert(Field(Branch, "branchId"));
		} while (Result.size() != Before);
		return Result;
	}
	std::string Commit(const Json& Deployment)
	{
		return Deployment.contains("source") ? Field(Deployment.at("source"), "commitSha") : std::string{};
	}
	bool IsSafeName(const std::string& Value)
	{
		return !Value.empty() && Value != "." && Value != ".." && Value.find_first_of("/\\:") == Value.npos;
	}
	bool UsesReportFiles(const fs::path& DumpFile)
	{
		return NameEquals(PathText(DumpFile.filename()), "crash.dmp");
	}
	std::string ReadOrigin(const fs::path& Path)
	{
		if (!fs::exists(Path)) return {};
		const auto Bytes = ReadBytes(Path);
		if (!Bytes.starts_with("\xff\xfe")) return Trim(Bytes);
		if (Bytes.size() % 2) throw std::runtime_error("Invalid report executable path.");
		std::wstring Value(Bytes.size() / 2 - 1, L'\0');
		memcpy(Value.data(), Bytes.data() + 2, Bytes.size() - 2);
		return Utf8(Value);
	}
	std::string ReportRepo(const fs::path& Repository, const Json& Build)
	{
		const auto Name = Field(Build, "repository");
		return Name.empty() ? GitHubRepository(Repository) : Name;
	}
	Json ArtifactsFor(const fs::path& Artifacts, const fs::path& Symbols, const std::string& Exe,
		const Json& Build, const DumpInfo* Dump)
	{
		std::vector<std::string> Names{Exe};
		if (Build.contains("modules"))
		{
			for (const auto& Entry : Build["modules"])
			{
				const auto Name = Field(Entry, "name");
				if (Field(Entry, "extension") == ".dll" && (!Dump || FindModule(*Dump, Name))) Names.push_back(Name);
			}
		}
		else if (Dump && (NameEquals(Exe, "HitoriEditor.exe") || NameEquals(Exe, "HitoriGame.exe")) && FindModule(*Dump, "libfbxsdk.dll"))
			Names.push_back("libfbxsdk.dll");
		Json Result = Json::array();
		for (const auto& Name : Names)
		{
			if (!IsSafeName(Name)) throw std::runtime_error("Invalid artifact filename.");
			const auto Expected = Dump ? FindModule(*Dump, Name) : nullptr;
			auto File = Artifacts / Wide(Name);
			if (!fs::is_regular_file(File) && Name != Exe && Expected)
				File = Symbols / Wide(Name) / std::format("{:08X}{:X}", Expected->Timestamp, Expected->Size) / Wide(Name);
			if (!fs::is_regular_file(File)) throw std::runtime_error("Missing required build artifact: " + Name);
			const auto Image = ReadImage(File);
			if (Dump && (!Expected || !MatchesDump(Image, *Expected)))
				throw std::runtime_error(Name + " does not match the dump.");
			if (Build.contains("modules"))
			{
				const auto Entry = Find(Build["modules"], "name", Name);
				if (!Entry.is_null() && !NameEquals(Field(Entry, "sha256"), HashFile(File)))
					throw std::runtime_error(Name + " does not match its build manifest.");
			}
			Result.push_back({{"name", Name}, {"file", PathText(File)}, {"extension", PathText(File.extension())},
				{"timestamp", Image.Timestamp}, {"imageSize", Image.Size}, {"codeView", Image.CodeView}});
			auto Pdb = File;
			Pdb.replace_extension(".pdb");
			if (!Image.CodeView.empty() && fs::is_regular_file(Pdb))
			{
				std::string Identity;
				try { Identity = ReadPdb(Pdb); } catch (const std::exception&) {}
				if (Identity == Image.CodeView) Result.push_back({{"name", PathText(Pdb.filename())}, {"file", PathText(Pdb)},
					{"extension", ".pdb"}, {"timestamp", nullptr}, {"imageSize", nullptr}, {"codeView", Identity}});
			}
		}
		return Result;
	}
	fs::path UnavailablePdb(const fs::path& File, const fs::path& Stage, GitHub& Remote,
		const std::string& Repo, const std::string& Base, const std::string& Missing)
	{
		fs::create_directories(Stage);
		const auto Copy = Stage / File.filename();
		fs::copy_file(File, Copy, fs::copy_options::overwrite_existing);
		const auto PdbStr = DebugTool(L"pdbstr.exe", true);
		const auto Stream = Trim(Run(PdbStr, {L"-r", L"-p:" + Copy.native(), L"-s:sourcelink$1"}).Out);
		if (Stream.empty()) return Copy;
		auto Text = Stream;
		if (const auto Null = Text.find('\0'); Null != Text.npos) Text.resize(Null);
		auto Map = Json::parse(Text);
		const auto Prefix = "https://raw.githubusercontent.com/" + Repo + "/";
		auto Changed = false;
		for (auto& Value : Map.at("documents"))
		{
			const auto Url = Value.get<std::string>();
			if (!Url.starts_with(Prefix) || Url.size() <= Prefix.size() + 40 || Url[Prefix.size() + 40] != '/') continue;
			const auto Sha = Url.substr(Prefix.size(), 40);
			if (IsHex(Sha, 40) && (Sha == Missing || !Remote.HasCommit(Sha)))
			{
				Value = Prefix + Base + "/.week12-source-unavailable/*";
				Changed = true;
			}
		}
		if (Changed)
		{
			const auto Mapping = Stage / "sourcelink.json";
			Write(Mapping, Map);
			Check(PdbStr, {L"-w", L"-p:" + Copy.native(), L"-s:sourcelink$1", L"-i:" + Mapping.native()});
			RestorePdbIdentity(File, Copy);
			if (ReadPdb(File) != ReadPdb(Copy)) throw std::runtime_error("PDB identity changed.");
		}
		return Copy;
	}
}

fs::path ConfiguredStore(const fs::path& ExecutableDirectory, const fs::path& Repository)
{
	if (const auto Override = Env(L"HITORI_VERSION_STORE"); !Override.empty()) return Override;
	const auto User = ReportRoot() / "store.txt";
	if (fs::exists(User))
	{
		auto Value = Trim(ReadBytes(User));
		if (Value.starts_with("\xef\xbb\xbf")) Value.erase(0, 3);
		if (!Value.empty()) return Wide(Value);
	}
	for (const auto& Candidate : {ExecutableDirectory, Repository})
	{
		if (Candidate.empty()) continue;
		const auto Root = Field(Read(Candidate / "GameConfig/VersionStore.json"), "storageRoot");
		if (!Root.empty()) return Wide(Root);
	}
	return {};
}

void SaveStore(const fs::path& Root)
{
	if (!Root.native().starts_with(L"\\\\")) throw std::runtime_error("Enter a UNC share such as \\\\TeamPC\\VersionStore.");
	fs::create_directories(ReportRoot());
	const auto Temp = ReportRoot() / ("store-" + Id() + ".tmp");
	std::ofstream File(Temp, std::ios::binary);
	File.exceptions(std::ios::badbit | std::ios::failbit);
	File << PathText(Root);
	File.close();
	Replace(Temp, ReportRoot() / "store.txt");
}

fs::path ResolveDumpFile(const fs::path& Report)
{
	return fs::is_directory(Report) ? Report / "crash.dmp" : Report;
}

void CheckDeveloper(const fs::path& Repository, const fs::path& DumpFile)
{
	const auto Build = UsesReportFiles(DumpFile) ? Read(DumpFile.parent_path() / "build.json") : Json{};
	GitHub(Repository, ReportRepo(Repository, Build)).RequireDeveloper();
}

Store::Store(const fs::path& Root, fs::path Repository) : Data(Root / "VersionData"), Symbols(Root / "symbols"), Repository(std::move(Repository))
{
	if (Root.empty() || !fs::is_directory(Data) || !fs::is_directory(Symbols))
		throw std::runtime_error("Cannot open the version store. Check the share address and permissions.");
}

Json Store::Records(const char* Filename) const
{
	Json Result = Json::array();
	for (auto It = fs::recursive_directory_iterator(Data); It != fs::recursive_directory_iterator(); ++It)
	{
		if (It->path() == Data / "_operations") { It.disable_recursion_pending(); continue; }
		if (It->path().filename() != Filename || !It->is_regular_file()) continue;
		auto Record = Read(It->path());
		if (Record.is_null()) continue;
		Record["recordPath"] = PathText(It->path());
		Result.push_back(std::move(Record));
	}
	return Result;
}

Json Store::List(const std::string& Id, const std::string& Sha) const
{
	const auto Branches = Records("branch.json");
	std::set<std::string> Scope, Deployments;
	for (const auto& Branch : Branches)
		if (!Id.empty() && (Field(Branch, "branchId") == Id || NameEquals(Field(Branch, "name"), Id)))
			Scope.merge(Descendants(Branches, Field(Branch, "branchId")));
	Json Result{{"deployments", Json::array()}, {"branches", Json::array()}, {"resolved", Json::array()}, {"dumps", Json::array()}};
	for (const auto& Deployment : Records("deployment.json"))
	{
		const auto Key = Field(Deployment, "deploymentId");
		if (Field(Deployment, "status") == "deleted" || (!Sha.empty() && !NameEquals(Commit(Deployment), Sha))) continue;
		if (!Id.empty() && Id != Key && !NameEquals(Id, Field(Deployment, "branch")) && !Scope.contains(Field(Deployment, "branchId"))) continue;
		Deployments.insert(Key);
		Result["deployments"].push_back(Deployment);
	}
	for (const auto& Branch : Branches)
		if (Id.empty() || Scope.contains(Field(Branch, "branchId"))) Result["branches"].push_back(Branch);
	for (const auto& Dump : Records("dump.json"))
		if (Deployments.contains(Field(Dump, "deploymentId"))) Result["dumps"].push_back(Dump);
	const auto Resolved = Data / "Crash/_resolved";
	if (fs::is_directory(Resolved))
		for (const auto& File : fs::directory_iterator(Resolved))
		{
			if (!File.is_regular_file() || File.path().extension() != ".json") continue;
			auto Branch = Read(File.path());
			if (Id.empty() || Id == Field(Branch, "branchId") || NameEquals(Id, Field(Branch, "name")))
				Result["resolved"].push_back(std::move(Branch));
		}
	return Result;
}

std::string Store::Next(const char* Counter)
{
	const auto File = Data / "Crash/counters.json";
	auto Record = Read(File);
	if (Record.is_null()) Record = {{"nextCrashBranchNumber", "1"}, {"nextDumpNumber", "1"}};
	const auto Number = std::stoll(Field(Record, Counter));
	if (Number < 1 || Number == INT64_MAX) throw std::runtime_error("Version store counter is exhausted.");
	Record[Counter] = std::to_string(Number + 1);
	Write(File, Record);
	return std::to_string(Number);
}

Json Store::Active(const std::string& BuildId, const Module* Image) const
{
	Json Matched;
	bool Ambiguous = false;
	for (const auto& Deployment : Records("deployment.json"))
	{
		if (Field(Deployment, "status") != "published") continue;
		const auto& Modules = Deployment.at("modules");
		const auto SameImage = !Image || std::any_of(Modules.begin(), Modules.end(), [&](const Json& Record)
		{
			return Field(Record, "extension") != ".pdb" && Matches(Record, *Image);
		});
		if (Field(Deployment, "buildId") == BuildId)
		{
			if (!SameImage) throw std::runtime_error("The build ID and dump executable identify different builds.");
			return Deployment;
		}
		if (Image && SameImage && BuildId.starts_with("image-"))
		{
			Ambiguous = Ambiguous || !Matched.is_null();
			Matched = Deployment;
		}
	}
	return Ambiguous ? Json{} : Matched;
}

Json Store::RecordBranch(const std::string& Name, const std::string& Repo, const std::string& Sha,
	const std::string& Base, const std::string& Parent)
{
	const auto Branches = Records("branch.json");
	for (auto Branch : Branches)
	{
		if (Field(Branch, "repository") != Repo || Field(Branch, "name") != Name) continue;
		if (!Parent.empty() && Field(Branch, "parentBranchId") != Parent) throw std::runtime_error("This branch has another parent.");
		Branch["latestSha"] = Sha;
		SaveRecord(RecordPath(Branch), Branch);
		return Branch;
	}
	auto Folder = Data / "Crash";
	if (!Parent.empty())
	{
		const auto Record = Find(Branches, "branchId", Parent);
		if (Field(Record, "repository") != Repo) throw std::runtime_error("Parent branch is not active for this repository.");
		Folder = RecordPath(Record).parent_path() / "children";
	}
	const auto Key = Id();
	const auto File = Folder / Key / "branch.json";
	Json Record{{"branchId", Key}, {"name", Name}, {"repository", Repo}, {"parentBranchId", Parent}, {"baseSha", Base},
		{"firstSha", Sha}, {"latestSha", Sha}, {"status", "active"}, {"recordPath", PathText(File)}};
	SaveRecord(File, Record);
	return Record;
}

std::string Store::AddSymbols(const Json& Modules, const std::string& Marker)
{
	const auto Admin = Symbols / "000Admin";
	auto Number = SymbolRegistration(Symbols, Marker);
	if (Number.empty())
	{
		const auto List = fs::temp_directory_path() / ("Hitori-symbols-" + Id() + ".txt");
		struct Cleanup { fs::path Path; ~Cleanup() { std::error_code Error; fs::remove(Path, Error); } } Guard{List};
		std::ofstream Input(List, std::ios::binary);
		Input.exceptions(std::ios::badbit | std::ios::failbit);
		for (const auto& Module : Modules)
		{
			const auto Text = Wide(Field(Module, "file"));
			BOOL Substituted{};
			const auto Size = WideCharToMultiByte(CP_ACP, GetACP() == CP_UTF8 ? 0 : WC_NO_BEST_FIT_CHARS,
				Text.data(), static_cast<int>(Text.size()), nullptr, 0, nullptr, GetACP() == CP_UTF8 ? nullptr : &Substituted);
			if (!Size || Substituted) throw std::runtime_error("SymStore cannot encode an artifact path in the Windows code page.");
			std::string Line(Size, '\0');
			WideCharToMultiByte(CP_ACP, 0, Text.data(), static_cast<int>(Text.size()), Line.data(), Size, nullptr, nullptr);
			Input << Line << "\r\n";
		}
		Input.close();
		Check(DebugTool(L"symstore.exe"), {L"add", L"/f", L"@" + List.native(), L"/s", Symbols.native(), L"/t", L"Hitori", L"/c", Wide(Marker)});
		Number = SymbolRegistration(Symbols, Marker);
		if (Number.empty()) throw std::runtime_error("SymStore did not record the transaction.");
	}
	std::ifstream Registration(Admin / Number);
	size_t Count{};
	for (std::string Line; std::getline(Registration, Line);) if (!Trim(Line).empty()) ++Count;
	if (Count != Modules.size())
	{
		RemoveSymbols(Number);
		throw std::runtime_error("SymStore did not register every artifact.");
	}
	return Number;
}

void Store::RemoveSymbols(const std::string& Number)
{
	if (Number.empty() || !std::all_of(Number.begin(), Number.end(), [](unsigned char Ch) { return std::isdigit(Ch) != 0; }))
		throw std::runtime_error("Invalid SymStore transaction number.");
	const auto Record = Symbols / "000Admin" / Number;
	if (!fs::is_regular_file(Record)) return;
	Check(DebugTool(L"symstore.exe"), {L"del", L"/i", Wide(Number), L"/s", Symbols.native()});
	if (fs::is_regular_file(Record)) throw std::runtime_error("SymStore did not remove transaction " + Number);
}

Json Store::Register(const std::string& Kind, const std::string& BuildId, const Json& Build, const fs::path& Artifacts,
	const std::string& Exe, const ReleaseRequest& Release, const ReportRequest& Report, const DumpInfo* Dump)
{
	if (auto Existing = Active(BuildId, Dump ? FindModule(*Dump, Exe) : nullptr); !Existing.is_null()) return Existing;
	DebugTool(L"symstore.exe");
	const auto Repo = ReportRepo(Repository, Build);
	GitHub Remote(Repository, Repo);
	Remote.RequireDeveloper();
	auto Modules = ArtifactsFor(Artifacts, Symbols, Exe, Build, Dump);
	const auto Sha = Field(Build, "commitSha"), Base = Field(Build, "baseSha"), Working = Field(Build, "workingBranch");
	auto SourceAvailable = Remote.HasCommit(Sha);
	const bool HasSource = SourceAvailable || HasLocalCommit(Repository, Sha);
	if (Kind == "Crash" && !HasSource && std::any_of(Modules.begin(), Modules.end(), [](const Json& Module)
		{ return Field(Module, "extension") == ".pdb"; }))
		DebugTool(L"pdbstr.exe", true);
	const auto Key = Hash(Kind + "/" + BuildId);
	const auto Operation = "deploy-" + Key;
	const auto WorkPath = Data / "_operations" / (Operation + ".json");
	auto Work = Read(WorkPath);
	if (Work.is_null() || Field(Work, "status") == "closed")
		Work = {{"id", Id()}, {"branch", nullptr}, {"transaction", nullptr}};
	const auto DeploymentId = Field(Work, "id");
	if (!IsHex(DeploymentId, 32)) throw std::runtime_error("Invalid pending deployment ID.");
	Work["status"] = "pending";
	Work["lastError"] = nullptr;
	Work["repository"] = Repo;
	Write(WorkPath, Work);
	try
	{
		auto Branch = Field(Work, "branch");
		std::string BranchId;
		fs::path Destination;
		if (Kind == "Release")
		{
			if (!IsHex(Sha, 40)) throw std::runtime_error("Build a versioned snapshot before publishing a Release.");
			const auto Target = Release.CurrentBranch ? Git(Repository, {L"symbolic-ref", L"--short", L"HEAD"}) : Release.Branch;
			if (Target.empty() || Target.starts_with("crash/")) throw std::runtime_error("Choose a Release branch.");
			if (Release.CurrentBranch && Target != Working) throw std::runtime_error("The current branch differs from the branch used for the build.");
			if (!Branch.empty() && Branch != Target) throw std::runtime_error("This pending request already targets " + Branch);
			Branch = Target;
			Work["branch"] = Branch;
			Write(WorkPath, Work);
			if (!PushBranch(Repository, Branch, Sha, !Release.CurrentBranch)) throw std::runtime_error("The new branch already exists.");
			SourceAvailable = Remote.HasCommit(Sha);
			if (!SourceAvailable) throw std::runtime_error("GitHub cannot resolve the pushed commit yet.");
			Destination = Data / "Release" / DeploymentId / "deployment.json";
		}
		else if (HasSource)
		{
			const auto Branches = Records("branch.json");
			auto Parent = Report.Parent;
			if (!Parent.empty() && Field(Find(Branches, "branchId", Parent), "repository") != Repo)
				throw std::runtime_error("Select an active parent branch.");
			if (Parent.empty() && Report.Branch.empty())
				for (const auto& Record : Branches)
					if (Field(Record, "repository") == Repo && Field(Record, "name") == Working && Working != Branch)
						Parent = Field(Record, "branchId");
			if (!Report.Branch.empty())
			{
				bool Known = false;
				for (const auto& Record : Branches)
					Known = Known || (Field(Record, "repository") == Repo && Field(Record, "name") == Report.Branch);
				if ((!Known && Parent.empty()) || !Report.Branch.starts_with("crash/"))
					throw std::runtime_error("Select a Crash branch or its parent.");
				if (!Branch.empty() && Branch != Report.Branch) throw std::runtime_error("This pending request has another branch.");
				Branch = Report.Branch;
				Work["branch"] = Branch;
				Write(WorkPath, Work);
				if (!PushBranch(Repository, Branch, Sha, !Known)) throw std::runtime_error("The new Crash branch already exists.");
			}
			else
			{
				for (;;)
				{
					if (Branch.empty())
					{
						Branch = "crash/" + Next("nextCrashBranchNumber");
						Work["branch"] = Branch;
						Write(WorkPath, Work);
					}
					if (PushBranch(Repository, Branch, Sha, true)) break;
					Branch.clear();
				}
			}
			SourceAvailable = Remote.HasCommit(Sha);
			if (!SourceAvailable) throw std::runtime_error("GitHub cannot resolve the pushed commit yet.");
			const auto Record = RecordBranch(Branch, Repo, Sha, Base, Parent);
			BranchId = Field(Record, "branchId");
			Destination = RecordPath(Record).parent_path() / "commits" / Sha / DeploymentId / "deployment.json";
		}
		else
			Destination = Data / "Crash/_without-source" / DeploymentId / "deployment.json";
		const auto Stage = Data / "_operations" / Key;
		if (!SourceAvailable)
		{
			std::string Known;
			for (auto& Module : Modules)
			{
				if (Field(Module, "extension") != ".pdb") continue;
				if (Known.empty()) Known = Remote.HasCommit(Base) ? Base : Remote.DefaultCommit();
				Module["file"] = PathText(UnavailablePdb(Wide(Field(Module, "file")), Stage, Remote, Repo, Known, Sha));
			}
		}
		const auto Transaction = AddSymbols(Modules, "hitori-" + DeploymentId);
		Work["transaction"] = Transaction;
		Write(WorkPath, Work);
		for (auto& Module : Modules) Module.erase("file");
		Json Metadata{{"deploymentId", DeploymentId}, {"buildId", BuildId}, {"type", Kind}, {"repository", Repo}, {"status", "published"},
			{"branchId", BranchId.empty() ? Json(nullptr) : Json(BranchId)}, {"branch", Branch}, {"workingBranch", Working}, {"baseSha", Base},
			{"source", {{"status", SourceAvailable ? "available" : "unavailable"}, {"commitSha", SourceAvailable ? Json(Sha) : Json(nullptr)}}},
			{"target", Exe}, {"configuration", Field(Build, "configuration")}, {"architecture", Field(Build, "architecture")},
			{"modules", Modules}, {"symbolStore", "symbols"}, {"symbolTransactions", Json::array({Transaction})},
			{"publisher", Account()}, {"publishedAt", TimeStamp()}, {"operation", Operation}, {"recordPath", PathText(Destination)}};
		SaveRecord(Destination, Metadata);
		Work["status"] = "complete";
		Write(WorkPath, Work);
		std::error_code Error;
		fs::remove_all(Stage, Error);
		return Metadata;
	}
	catch (const std::exception& Error)
	{
		if (Field(Work, "status") != "complete")
		{
			Work["status"] = "failed";
			Work["lastError"] = Error.what();
			Write(WorkPath, Work);
		}
		throw;
	}
}

Json Store::Release(const ReleaseRequest& Request)
{
	if (Request.CurrentBranch && !Request.Branch.empty()) throw std::runtime_error("Choose one publication branch.");
	const auto Directory = Request.Directory.empty() ? LatestArtifacts(Repository) : Request.Directory;
	const auto Build = Read(Directory / "build.json");
	const auto BuildId = Field(Build, "buildId");
	if (!IsHex(BuildId, 32)) throw std::runtime_error("No valid build manifest was found.");
	Json Result;
	WithLock(Data / "store.lock", [&]
	{
		Result = Register("Release", BuildId, Build, Directory, Field(Build, "target") + ".exe", Request, {}, nullptr);
	});
	return Result;
}

void Store::AddIssue(const Json& Report, const Json& Deployment, const std::string& Title, const std::string& Body)
{
	GitHub Remote(Repository, Field(Deployment, "repository"));
	Remote.RequireDeveloper();
	const auto File = RecordPath(Report);
	const auto Marker = "<!-- hitori-dump:" + Field(Report, "submissionId") + " -->";
	const auto Description = Body + "\n\nDump " + Field(Report, "dumpNumber") + ", deployment " +
		Field(Deployment, "deploymentId") + ", source " + Commit(Deployment) + "\n" + PathText(File.parent_path() / "crash.dmp");
	Remote.Issue(Marker, Title, Description);
}

Json Store::Issue(const std::string& Number, const std::string& Title, const std::string& Body)
{
	if (Number.empty() || Trim(Title).empty()) throw std::runtime_error("Select a dump and enter an issue title.");
	Json Result;
	WithLock(Data / "store.lock", [&]
	{
		Result = Find(Records("dump.json"), "dumpNumber", Number);
		if (Result.is_null()) throw std::runtime_error("The dump was not found.");
		AddIssue(Result, Find(Records("deployment.json"), "deploymentId", Field(Result, "deploymentId")), Title, Body);
	});
	return Result;
}

Json Store::Crash(const ReportRequest& Request)
{
	const auto& File = Request.DumpFile;
	if (!fs::is_regular_file(File)) throw std::runtime_error("선택한 덤프 파일을 찾을 수 없습니다.");
	const bool HasReportFiles = UsesReportFiles(File);
	const auto ReportFolder = File.parent_path();
	auto Build = HasReportFiles ? Read(ReportFolder / "build.json") : Json{};
	const auto Origin = HasReportFiles ? fs::path(Wide(ReadOrigin(ReportFolder / "origin.txt"))) : fs::path{};
	const auto Dump = ReadDump(File);
	auto Exe = !Origin.empty() ? PathText(Origin.filename()) : Field(Build, "target");
	if (Origin.empty() && !Exe.empty()) Exe += ".exe";
	if (Exe.empty())
		for (const auto& Module : Dump.Modules)
			if (NameEquals(PathText(fs::path(Wide(Module.Name)).extension()), ".exe")) { Exe = Module.Name; break; }
	const auto* Image = FindModule(Dump, Exe);
	if (!Image) throw std::runtime_error("The report executable is missing from the module list.");
	const auto RecordedBuildId = Field(Build, "buildId");
	auto BuildId = RecordedBuildId;
	if (BuildId.empty()) BuildId = "image-" + Hash(std::format("{}/{}/{}/{}", Exe, Image->Timestamp, Image->Size, Image->CodeView));
	if (!IsHex(BuildId, 32) && !(BuildId.starts_with("image-") && IsHex(BuildId.substr(6), 64)))
		throw std::runtime_error("Invalid build identifier.");
	const auto Digest = HashFile(File);
	Json Result;
	WithLock(Data / "store.lock", [&]
	{
		auto Deployment = Active(BuildId, Image);
		if (Deployment.is_null())
		{
			if (!Request.AllowCrash) throw std::runtime_error("This build has no active deployment. Developer publication is required.");
			const auto Artifacts = FindCrashArtifacts(Request, Repository, Origin, RecordedBuildId, *Image);
			if (Build.is_null())
			{
				Build = BuildFromArtifacts(Artifacts, *Image);
				if (!Build.is_null()) BuildId = Field(Build, "buildId");
			}
			Deployment = Register("Crash", BuildId, Build, Artifacts, Exe, {}, Request, &Dump);
		}
		if (!Request.IssueTitle.empty()) GitHub(Repository, Field(Deployment, "repository")).RequireDeveloper();
		const auto Number = Next("nextDumpNumber");
		const auto Folder = RecordPath(Deployment).parent_path() / "dumps" / Number;
		const auto Destination = Folder / "crash.dmp";
		const auto Temp = Folder / "crash.dmp.tmp";
		const auto Record = Folder / "dump.json";
		if (!fs::create_directories(Folder)) throw std::runtime_error("This dump number is already in use.");
		try
		{
			fs::copy_file(File, Temp);
			if (HashFile(Temp) != Digest) throw std::runtime_error("Dump copy checksum mismatch.");
			Replace(Temp, Destination);
			Result = {{"dumpNumber", Number}, {"submissionId", Id()}, {"deploymentId", Field(Deployment, "deploymentId")},
				{"buildId", Field(Deployment, "buildId")}, {"file", "crash.dmp"}, {"sha256", Digest}, {"size", fs::file_size(Destination)},
				{"occurredAt", FileTimeStamp(File)}, {"submittedAt", TimeStamp()},
				{"dumpType", "MiniDumpNormal"}, {"exceptionCode", Dump.ExceptionCode}, {"recordPath", PathText(Record)}};
			SaveRecord(Record, Result);
		}
		catch (...)
		{
			std::error_code Error;
			if (!fs::exists(Record, Error) && !Error)
			{
				fs::remove(Temp, Error);
				fs::remove(Destination, Error);
				fs::remove(Folder, Error);
			}
			throw;
		}
		if (!Request.IssueTitle.empty()) AddIssue(Result, Deployment, Request.IssueTitle, Request.IssueBody);
	});
	return Result;
}

void Store::RemoveEmptyParents(fs::path Directory) const
{
	while (Directory != Data && fs::is_directory(Directory) && fs::is_empty(Directory))
	{
		fs::remove(Directory);
		Directory = Directory.parent_path();
	}
}

void Store::RemoveDump(const Json& Dump)
{
	const auto Submission = Field(Dump, "submissionId");
	if (!IsHex(Submission, 32)) throw std::runtime_error("Invalid submission ID.");
	// Older clients can still retry using a local submission ID.
	const auto WorkPath = Data / "_operations" / ("dump-" + Submission + ".json");
	auto Work = Read(WorkPath);
	if (!Work.is_null()) { Work["status"] = "deleted"; Write(WorkPath, Work); }
	const auto File = RecordPath(Dump), Folder = File.parent_path();
	fs::remove(Folder / "crash.dmp");
	fs::remove(File);
	RemoveEmptyParents(Folder);
}

void Store::RemoveDeployment(Json Deployment, const Json& Dumps)
{
	const auto File = RecordPath(Deployment);
	Deployment["status"] = "deleting";
	SaveRecord(File, Deployment);
	try
	{
		for (const auto& Dump : Dumps)
			if (Field(Dump, "deploymentId") == Field(Deployment, "deploymentId")) RemoveDump(Dump);
		for (const auto& Number : Deployment.at("symbolTransactions")) RemoveSymbols(Number.get<std::string>());
		const auto Operation = Field(Deployment, "operation");
		if (!IsSafeName(Operation)) throw std::runtime_error("Invalid deployment operation.");
		const auto WorkPath = Data / "_operations" / (Operation + ".json");
		auto Work = Read(WorkPath);
		if (!Work.is_null()) { Work["status"] = "closed"; Write(WorkPath, Work); }
		Deployment["status"] = "deleted";
		const auto Id = Field(Deployment, "deploymentId");
		if (!IsHex(Id, 32)) throw std::runtime_error("Invalid deployment ID.");
		SaveRecord(Data / "_operations" / ("retired-" + Id + ".json"), Deployment);
		fs::remove(File);
		RemoveEmptyParents(File.parent_path());
	}
	catch (...)
	{
		Deployment["status"] = "cleanup-failed";
		if (fs::exists(File)) SaveRecord(File, Deployment);
		throw;
	}
}

Json Store::Remove(const DeleteRequest& Request)
{
	if (Request.Action != "DeleteDump" && Request.Action != "DeleteDeployment" && Request.Action != "Resolve")
		throw std::runtime_error("Invalid deletion action.");
	Json Result;
	WithLock(Data / "store.lock", [&]
	{
		const auto Branches = Records("branch.json"), AllDeployments = Records("deployment.json"), AllDumps = Records("dump.json");
		const auto Resolve = Request.Action == "Resolve";
		const auto SingleDump = Request.Action == "DeleteDump" && !Request.DumpNumber.empty();
		std::set<std::string> BranchIds, DeploymentIds, DumpNumbers;
		const auto Key = SingleDump ? Request.DumpNumber : Request.Id;
		if (Key.empty()) throw std::runtime_error("Select a record to remove.");
		if (Resolve || Request.Scope == "Branch")
		{
			if (Find(Branches, "branchId", Request.Id).is_null()) throw std::runtime_error("Select an active branch.");
			BranchIds = Descendants(Branches, Request.Id);
		}
		if (Request.Scope == "Commit" && !IsHex(Request.Commit, 40)) throw std::runtime_error("Enter the full commit SHA.");
		Json Dumps = Json::array(), Deployments = Json::array(), Publications = Json::array();
		if (SingleDump)
			for (const auto& Dump : AllDumps)
				if (Field(Dump, "dumpNumber") == Key) { Dumps.push_back(Dump); DeploymentIds.insert(Field(Dump, "deploymentId")); }
		for (const auto& Deployment : AllDeployments)
		{
			if (Field(Deployment, "status") == "deleted") continue;
			const auto Id = Field(Deployment, "deploymentId"), Branch = Field(Deployment, "branchId");
			const auto Selected = SingleDump ? DeploymentIds.contains(Id) : !BranchIds.empty() ? BranchIds.contains(Branch)
				: Request.Scope == "Commit" ? Branch == Key && Commit(Deployment) == Request.Commit : Id == Key;
			if (Selected) { Deployments.push_back(Deployment); DeploymentIds.insert(Id); }
		}
		if (!SingleDump)
			for (const auto& Dump : AllDumps)
				if (DeploymentIds.contains(Field(Dump, "deploymentId"))) Dumps.push_back(Dump);
		for (const auto& Dump : Dumps) DumpNumbers.insert(Field(Dump, "dumpNumber"));
		if (Resolve)
		{
			if (!Dumps.empty()) throw std::runtime_error("Delete the remaining dumps before resolving this investigation.");
			std::set<std::pair<std::string, std::string>> Names;
			for (const auto& Branch : Branches)
				if (BranchIds.contains(Field(Branch, "branchId"))) Names.emplace(Field(Branch, "repository"), Field(Branch, "name"));
			const auto Operations = Data / "_operations";
			if (fs::is_directory(Operations))
				for (const auto& Entry : fs::directory_iterator(Operations))
				{
					if (!PathText(Entry.path().filename()).starts_with("deploy-") || Entry.path().extension() != ".json") continue;
					auto Work = Read(Entry.path());
					const auto Status = Field(Work, "status");
					const auto Repo = Field(Work, "repository"), Branch = Field(Work, "branch");
					const bool Selected = std::any_of(Names.begin(), Names.end(), [&](const auto& Name)
						{ return Name.second == Branch && (Repo.empty() || Name.first == Repo); });
					if ((Status != "pending" && Status != "failed") || !Selected) continue;
					const auto Key = PathText(Entry.path().stem()).substr(7);
					if (!IsHex(Key, 64) || !IsHex(Field(Work, "id"), 32))
						throw std::runtime_error("Invalid unfinished publication record.");
					if (Field(Work, "transaction").empty()) Work["transaction"] = SymbolRegistration(Symbols, "hitori-" + Field(Work, "id"));
					Work["recordPath"] = PathText(Entry.path());
					Publications.push_back(std::move(Work));
				}
		}
		const auto Plan = Hash(Json::array({Request.Action, Key, DeploymentIds, DumpNumbers, BranchIds, Publications}).dump());
		Result = {{"confirmation", Key}, {"plan", Plan}, {"deployments", DeploymentIds}, {"dumps", DumpNumbers},
			{"branches", BranchIds}, {"publications", Publications.size()}, {"cleanupCandidates", Json::array()}};
		if (Request.ConfirmId != Key || Request.ConfirmPlan != Plan) return;
		if (Request.Action != "DeleteDump")
		{
			std::set<std::string> Registered;
			for (const auto& Deployment : Deployments)
				for (const auto& Number : Deployment.at("symbolTransactions")) Registered.insert(Number.get<std::string>());
			for (const auto& Work : Publications)
				if (!Field(Work, "transaction").empty()) Registered.insert(Field(Work, "transaction"));
			for (const auto& Number : Registered)
			{
				if (Number.empty() || !std::all_of(Number.begin(), Number.end(), [](unsigned char Ch) { return std::isdigit(Ch) != 0; }))
					throw std::runtime_error("Invalid SymStore transaction number.");
				if (fs::is_regular_file(Symbols / "000Admin" / Number)) DebugTool(L"symstore.exe");
			}
		}
		for (auto& Work : Publications)
		{
			const auto Transaction = Field(Work, "transaction");
			const bool UsedByDeployment = std::any_of(AllDeployments.begin(), AllDeployments.end(), [&](const Json& Deployment)
			{
				return Deployment.contains("symbolTransactions") &&
					std::find(Deployment["symbolTransactions"].begin(), Deployment["symbolTransactions"].end(), Transaction) != Deployment["symbolTransactions"].end();
			});
			if (!Transaction.empty() && !UsedByDeployment) RemoveSymbols(Transaction);
			const auto Path = RecordPath(Work);
			fs::remove_all(Data / "_operations" / PathText(Path.stem()).substr(7));
			Work["status"] = "closed";
			Work["closedAt"] = TimeStamp();
			SaveRecord(Path, Work);
		}
		if (Request.Action == "DeleteDump")
		{
			for (const auto& Dump : Dumps) RemoveDump(Dump);
			std::set<std::string> Remaining;
			for (const auto& Dump : Records("dump.json")) Remaining.insert(Field(Dump, "deploymentId"));
			std::set<std::string> Candidates;
			for (const auto& Deployment : Deployments)
			{
				if (Field(Deployment, "type") != "Crash") continue;
				const auto Branch = Field(Deployment, "branchId"), Sha = Commit(Deployment), Id = Field(Deployment, "deploymentId");
				std::set<std::string> Group{Id};
				if (!Branch.empty() && !Sha.empty())
					for (const auto& Entry : AllDeployments)
						if (Field(Entry, "branchId") == Branch && Commit(Entry) == Sha) Group.insert(Field(Entry, "deploymentId"));
				if (std::none_of(Group.begin(), Group.end(), [&](const auto& Id) { return Remaining.contains(Id); })) Candidates.merge(Group);
			}
			Result["cleanupCandidates"] = Candidates;
		}
		else
			for (const auto& Deployment : Deployments) RemoveDeployment(Deployment, Dumps);
		if (Resolve)
		{
			std::vector<Json> Selected;
			for (const auto& Branch : Branches)
				if (BranchIds.contains(Field(Branch, "branchId"))) Selected.push_back(Branch);
			std::ranges::sort(Selected, std::greater{}, [](const Json& Branch) { return Field(Branch, "recordPath").size(); });
			for (auto& Branch : Selected)
			{
				const auto Path = RecordPath(Branch);
				Branch["resolvedAt"] = TimeStamp();
				Branch["resolvedBy"] = Account();
				Branch["status"] = "resolved";
				const auto Id = Field(Branch, "branchId");
				if (!IsHex(Id, 32)) throw std::runtime_error("Invalid branch ID.");
				SaveRecord(Data / "Crash/_resolved" / (Id + ".json"), Branch);
				fs::remove(Path);
				RemoveEmptyParents(Path.parent_path());
			}
		}
		Result["completed"] = true;
	});
	return Result;
}
}
