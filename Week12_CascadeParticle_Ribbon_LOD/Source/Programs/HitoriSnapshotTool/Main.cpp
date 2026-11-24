#include "../VersionStore/Binary.h"
#include "../VersionStore/Git.h"

#include <cstdio>
#include <stdexcept>

namespace
{
	using namespace VersionStore;

	struct Snapshot
	{
		fs::path Repository, Output, Intermediate, Cache;
		std::string Target, Configuration;

		std::wstring PendingRef() const { return Wide("refs/crash-snapshot-pending/" + Target + "/" + Configuration); }
		fs::path PendingFile() const { return Intermediate / "HitoriSnapshot.json"; }

		std::string WorkingTree(const std::string& Parent) const
		{
			const auto Index = Intermediate / ("HitoriIndex-" + Id());
			struct Cleanup { fs::path Index; ~Cleanup() { std::error_code Error; fs::remove(Index, Error); } } Guard{Index};
			const auto WithIndex = [&](const Args& Tail)
			{
				Args Command{L"-C", Repository.native()};
				Command.insert(Command.end(), Tail.begin(), Tail.end());
				const auto Result = Run(L"git.exe", Command, {}, L"GIT_INDEX_FILE", Index.native());
				if (Result.Code) throw std::runtime_error("Git snapshot failed.\n" + Trim(Result.Err));
				return Trim(Result.Out);
			};
			WithIndex({L"read-tree", Wide(Parent)});
			WithIndex({L"add", L"-u", L"--", L"."});
			WithIndex({L"add", L"-A", L"--", L"Source", L"Setup", L"Assets", L"Resources", L"GameConfig", L"Hitori.hproject"});
			return WithIndex({L"write-tree"});
		}

		void Prepare() const
		{
			fs::create_directories(Intermediate);
			const auto Name = GitHubRepository(Repository), BuildId = Id();
			const auto Parent = Git(Repository, {L"rev-parse", L"HEAD"});
			const auto Branch = Git(Repository, {L"rev-parse", L"--abbrev-ref", L"HEAD"});
			const auto Tree = WorkingTree(Parent);
			const auto Sha = Tree == Git(Repository, {L"rev-parse", Wide(Parent + "^{tree}")}) ? Parent :
				Git(Repository, {L"-c", L"user.name=Hitori Build Snapshot", L"-c", L"user.email=snapshot@hitori.invalid",
					L"commit-tree", Wide(Tree), L"-p", Wide(Parent), L"-m", Wide("Hitori build snapshot " + BuildId)});
			Git(Repository, {L"update-ref", PendingRef(), Wide(Sha)});
			const auto Url = "https://raw.githubusercontent.com/" + Name + "/" + Sha + "/Source/*";
			Write(Intermediate / "HitoriSourceLink.json", {{"documents", {
				{PathText(Repository / L"Source\\*"), Url}, {Utf8((Repository / "Source/*").generic_wstring()), Url}
			}}});
			Write(PendingFile(), {{"buildId", BuildId}, {"repository", Name}, {"commitSha", Sha}, {"baseSha", Parent},
				{"workingBranch", Branch}, {"tree", Tree}, {"target", Target}, {"configuration", Configuration}, {"architecture", "x64"}});
			std::printf("Hitori source snapshot: %s\n", Sha.c_str());
		}

		void Complete() const
		{
			auto Build = Read(PendingFile());
			const auto BuildId = Field(Build, "buildId"), Sha = Field(Build, "commitSha"), Parent = Field(Build, "baseSha");
			if (!IsHex(BuildId, 32) || !IsHex(Sha, 40) || !IsHex(Parent, 40) ||
				Field(Build, "target") != Target || Field(Build, "configuration") != Configuration)
				throw std::runtime_error("The build has no matching source snapshot. Rebuild the project.");
			if (Git(Repository, {L"rev-parse", L"HEAD"}) != Parent ||
				Git(Repository, {L"rev-parse", L"--abbrev-ref", L"HEAD"}) != Field(Build, "workingBranch") ||
				Git(Repository, {L"rev-parse", PendingRef()}) != Sha ||
				WorkingTree(Parent) != Field(Build, "tree"))
				throw std::runtime_error("Source files or the Git checkout changed during compilation. Rebuild before publishing.");

			const auto Exe = Output / (Target + ".exe");
			const auto Image = ReadImage(Exe);
			std::vector<fs::path> Modules{Exe};
			for (const auto& Entry : fs::directory_iterator(Output))
				if (Entry.is_regular_file() && _wcsicmp(Entry.path().extension().c_str(), L".dll") == 0) Modules.push_back(Entry.path());
			for (size_t Index = 0, Count = Modules.size(); Index < Count; ++Index)
			{
				auto Pdb = Modules[Index];
				Pdb.replace_extension(".pdb");
				if (!fs::is_regular_file(Pdb)) continue;
				const auto Module = Modules[Index] == Exe ? Image : ReadImage(Modules[Index]);
				if (Module.CodeView.empty()) continue;
				try { if (ReadPdb(Pdb) == Module.CodeView) Modules.push_back(Pdb); }
				catch (const std::exception&) {}
			}
			Build.erase("tree");
			Build["modules"] = Json::array();
			const auto Slot = Cache / BuildId, Artifacts = Slot / "artifacts";
			const auto Latest = Cache / "latest.json";
			const auto Manifest = Output / (Target + ".build.json");
			const auto Ref = Wide("refs/crash-snapshots/" + BuildId);
			fs::create_directories(Cache);
			WithLock(Cache / "build.lock", [&]
			{
				const auto Previous = Field(Read(Latest), "buildId");
				const bool Reusing = Previous == BuildId;
				bool Pinned = false, Installed = false;
				try
				{
					fs::create_directories(Artifacts);
					for (const auto& File : Modules)
					{
						const auto Copy = Artifacts / File.filename();
						fs::copy_file(File, Copy, fs::copy_options::overwrite_existing);
						Build["modules"].push_back({{"name", PathText(File.filename())}, {"extension", PathText(File.extension())},
							{"sha256", HashFile(Copy)}});
					}
					for (const char* Name : {"HitoriCrashReporter.exe", "HitoriVersionManager.exe"})
						if (fs::is_regular_file(Output / Name)) fs::copy_file(Output / Name, Artifacts / Name, fs::copy_options::overwrite_existing);
					Write(Artifacts / "build.json", Build);
					Git(Repository, {L"update-ref", Ref, Wide(Sha)});
					Pinned = true;
					Write(Manifest, Build);
					Installed = true;
					Write(Latest, {{"buildId", BuildId}, {"artifacts", PathText(Artifacts)}});
				}
				catch (...)
				{
					if (!Reusing)
					{
						if (Pinned) { try { Git(Repository, {L"update-ref", L"-d", Ref, Wide(Sha)}); } catch (...) {} }
						std::error_code Error;
						if (Installed) fs::remove(Manifest, Error);
						fs::remove_all(Slot, Error);
					}
					throw;
				}
				Git(Repository, {L"update-ref", L"-d", PendingRef(), Wide(Sha)});
				fs::remove(PendingFile());
				if (IsHex(Previous, 32) && Previous != BuildId)
				{
					Git(Repository, {L"update-ref", L"-d", Wide("refs/crash-snapshots/" + Previous)});
					std::error_code Error;
					fs::remove_all(Cache / Previous, Error);
				}
				std::printf("Hitori build manifest: %s\n", PathText(Manifest).c_str());
			});
		}
	};
}

int wmain(int Count, wchar_t** Args)
{
	try
	{
		if (Count != 7) throw std::runtime_error("Invalid invocation from the Hitori Visual Studio project.");
		const auto Target = VersionStore::Utf8(Args[3]), Configuration = VersionStore::Utf8(Args[4]);
		if ((Target != "HitoriEditor" && Target != "HitoriGame") ||
			(Configuration != "Debug" && Configuration != "Release" && Configuration != "Shipping"))
			throw std::runtime_error("Invalid Hitori build target or configuration.");
		const auto Repository = VersionStore::fs::absolute(Args[2]).lexically_normal();
		const Snapshot Build{Repository, VersionStore::fs::absolute(Args[5]).lexically_normal(),
			VersionStore::fs::absolute(Args[6]).lexically_normal(), Repository / "Build/VersionStore/local-builds", Target, Configuration};
		if (std::wstring_view(Args[1]) == L"prepare") Build.Prepare();
		else if (std::wstring_view(Args[1]) == L"complete") Build.Complete();
		else throw std::runtime_error("Invalid Hitori build step.");
		return 0;
	}
	catch (const std::exception& Error)
	{
		std::fprintf(stderr, "Hitori snapshot: %s\n", Error.what());
		return 1;
	}
}
