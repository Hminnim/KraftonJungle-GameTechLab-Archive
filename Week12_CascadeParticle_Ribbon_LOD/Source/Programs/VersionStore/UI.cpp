#include "Window.h"
#include "Git.h"
#include "Store.h"

#include <imgui.h>
#include <imgui_stdlib.h>
#include <chrono>
#include <future>
#include <set>
#include <stdexcept>

namespace VersionStore
{
namespace
{
	struct Context
	{
		fs::path Executable, Repository, Address;
		std::string AddressInput, Error, Status;
		std::future<Json> Result;
		std::function<void(const Json&)> Complete;
		explicit Context(const fs::path& Executable) : Executable(Executable), Repository(RepositoryFrom(Executable.parent_path())),
			Address(ConfiguredStore(Executable.parent_path(), Repository)), AddressInput(PathText(Address)) {}
		void Start(std::function<Json()> Task, std::function<void(const Json&)> Finished)
		{
			Error.clear();
			Status.clear();
			Complete = std::move(Finished);
			Result = std::async(std::launch::async, std::move(Task));
		}
		void Poll()
		{
			if (!Result.valid() || Result.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
			auto Finished = std::move(Complete);
			try { Finished(Result.get()); } catch (const std::exception& Failure) { Error = Failure.what(); }
		}
		void Message() const
		{
			if (Result.valid()) ImGui::TextUnformatted("처리 중…");
			if (!Error.empty()) ImGui::TextWrapped("%s", Error.c_str());
			if (!Status.empty()) ImGui::TextWrapped("%s", Status.c_str());
		}
		template<class Action, class Finished> void Run(Action Task, Finished Done)
		{
			Start([Address = Address, Repository = Repository, Task = std::move(Task)]() mutable
			{
				Store Data(Address, Repository);
				return Task(Data);
			}, std::move(Done));
		}
		bool Settings()
		{
			ImGui::Text("공유 저장소  %s", Address.empty() ? "설정되지 않음" : PathText(Address).c_str());
			ImGui::SameLine();
			if (ImGui::Button("저장소 설정")) { AddressInput = PathText(Address); ImGui::OpenPopup("공유 저장소 설정"); }
			bool Changed = false;
			if (ImGui::BeginPopupModal("공유 저장소 설정", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
			{
				ImGui::SetNextItemWidth(480);
				ImGui::InputText("주소", &AddressInput);
				if (ImGui::Button("연결"))
				{
					try
					{
						const auto New = fs::path(Wide(Trim(AddressInput)));
						Store Check(New, Repository);
						SaveStore(New);
						Address = New;
						Changed = true;
						Error.clear();
						ImGui::CloseCurrentPopup();
					}
					catch (const std::exception& Failure) { Error = Failure.what(); }
				}
				ImGui::SameLine();
				if (ImGui::Button("취소")) ImGui::CloseCurrentPopup();
				ImGui::EndPopup();
			}
			return Changed;
		}
	};

	std::string SourceSha(const Json& Build)
	{
		return Build.contains("source") ? Field(Build.at("source"), "commitSha") : std::string{};
	}

	void OpenRecord(const Json& Record, bool Dump)
	{
		const auto File = fs::path(Wide(Field(Record, "recordPath")));
		OpenPath(Dump ? File.parent_path() / "crash.dmp" : File);
	}

	void RecordValue(const std::string& Label, const std::string& FullValue)
	{
		ImGui::TextUnformatted(Label.c_str());
		if (Label != FullValue && !FullValue.empty() && ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", FullValue.c_str());
	}
}

int ManagerUI(const fs::path& Executable)
{
	Context Ui(Executable);
	Json Listing, Selected, Build, Plan;
	ReleaseRequest Release;
	DeleteRequest Removal;
	std::string Search, Commit, SelectedKind, Branch, Confirmation, IssueTitle, IssueBody;
	std::vector<std::string> CleanupCandidates;
	bool Current{}, CanCurrent{}, ShowRelease{}, ShowRemoval{}, ShowIssue{};
	const auto Refresh = [&]
	{
		if (Ui.Address.empty()) return;
		Ui.Run([Search = Trim(Search), Commit = Trim(Commit)](Store& Data) { return Data.List(Search, Commit); },
			[&](const Json& Result) { Listing = Result; Selected = nullptr; SelectedKind.clear(); Ui.Status = "저장소에 연결됨"; });
	};
	const auto Preview = [&](DeleteRequest Request)
	{
		Ui.Run([Request](Store& Data) { return Data.Remove(Request); }, [&, Request](const Json& Result)
		{
			Plan = Result;
			Removal = Request;
			Confirmation.clear();
			ShowRemoval = true;
		});
	};
	Refresh();
	return Window(L"Hitori 배포 및 덤프 관리", [&](HWND Hwnd)
	{
		Ui.Poll();
		ImGui::BeginDisabled(Ui.Result.valid());
		if (Ui.Settings()) Refresh();
		ImGui::BeginDisabled(Listing.is_null() || Ui.Repository.empty());
		if (ImGui::Button("Release 게시"))
		{
			try
			{
				Release = {};
				Release.Directory = LatestArtifacts(Ui.Repository);
				Build = Read(Release.Directory / "build.json");
				if (Build.is_null()) throw std::runtime_error("The selected build has no build.json.");
				std::string Head;
				try { Head = Git(Ui.Repository, {L"symbolic-ref", L"--short", L"HEAD"}); } catch (const std::exception&) {}
				CanCurrent = !Head.empty() && Head == Field(Build, "workingBranch") && !Head.starts_with("crash/");
				Current = false;
				Branch.clear();
				ShowRelease = true;
			}
			catch (const std::exception& Failure) { Ui.Error = Failure.what(); }
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("로컬 덤프 제출"))
		{
			const auto Dump = ChooseDumpFile(Hwnd, ReportRoot());
			if (!Dump.empty())
			{
				try { StartDetached(Executable.parent_path() / "HitoriCrashReporter.exe", {L"--report", Dump.native()}); }
				catch (const std::exception& Failure) { Ui.Error = Failure.what(); }
			}
		}
		ImGui::SeparatorText("게시 기록 검색");
		ImGui::SetNextItemWidth(210);
		const auto BranchEnter = ImGui::InputText("브랜치 / 배포 ID", &Search, ImGuiInputTextFlags_EnterReturnsTrue);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(230);
		const auto CommitEnter = ImGui::InputText("커밋 SHA", &Commit, ImGuiInputTextFlags_EnterReturnsTrue);
		ImGui::SameLine();
		if (ImGui::Button("목록 조회") || BranchEnter || CommitEnter) Refresh();
		Ui.Message();
		if (!Listing.is_null())
		{
			ImGui::BeginChild("Records", {0, -(ImGui::GetFrameHeightWithSpacing() * 2)}, ImGuiChildFlags_Borders);
			const auto Row = [&](const Json& Record, const char* Kind, const char* Key, const std::string& Label, int Depth)
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				const auto Id = Field(Record, Key);
				const auto Title = std::string(Depth * 4, ' ') + Label + "###row";
				ImGui::PushID((std::string(Kind) + ":" + Id).c_str());
				if (ImGui::Selectable(Title.c_str(), SelectedKind == Kind && Field(Selected, Key) == Id,
					ImGuiSelectableFlags_SpanAllColumns))
				{
					Selected = Record;
					SelectedKind = Kind;
				}
				ImGui::PopID();
			};
			if (Listing["deployments"].empty() && Listing["branches"].empty() && Listing["resolved"].empty())
				ImGui::TextUnformatted(Search.empty() && Commit.empty() ? "아직 게시된 배포나 덤프가 없습니다." : "검색 결과가 없습니다.");
			else if (ImGui::BeginTable("RecordList", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV))
			{
				ImGui::TableSetupColumn("항목", ImGuiTableColumnFlags_WidthStretch, 2.4f);
				ImGui::TableSetupColumn("커밋 SHA", ImGuiTableColumnFlags_WidthStretch, 1.0f);
				ImGui::TableSetupColumn("배포 ID", ImGuiTableColumnFlags_WidthStretch, 1.0f);
				ImGui::TableSetupColumn("덤프 파일 시각 (UTC)", ImGuiTableColumnFlags_WidthStretch, 1.7f);
				ImGui::TableHeadersRow();

				const auto DrawDeployment = [&](const Json& Deployment, int Depth)
				{
					const auto Id = Field(Deployment, "deploymentId");
					const auto BranchName = Field(Deployment, "branch");
					std::string Label = Field(Deployment, "type") + " 배포";
					if (Depth == 0 && !BranchName.empty()) Label += "  " + BranchName;
					Row(Deployment, "deployment", "deploymentId", Label, Depth);
					const auto Sha = SourceSha(Deployment);
					ImGui::TableSetColumnIndex(1);
					RecordValue(Sha.empty() ? "확인된 소스 없음" : Sha.substr(0, 12), Sha);
					ImGui::TableSetColumnIndex(2);
					RecordValue(Id.substr(0, 12), Id);

					for (const auto& Dump : Listing["dumps"])
					{
						if (Field(Dump, "deploymentId") != Id) continue;
						Row(Dump, "dump", "dumpNumber", "덤프 #" + Field(Dump, "dumpNumber"), Depth + 1);
						ImGui::TableSetColumnIndex(3);
						const auto When = Field(Dump, "occurredAt");
						ImGui::TextUnformatted(When.c_str());
					}
				};

				std::set<std::string> ShownBranches;
				for (const auto& Record : Listing["branches"])
				{
					const auto Id = Field(Record, "branchId");
					ShownBranches.insert(Id);
					Row(Record, "branch", "branchId", "Crash 브랜치  " + Field(Record, "name"), 0);
					for (const auto& Deployment : Listing["deployments"])
						if (Field(Deployment, "branchId") == Id) DrawDeployment(Deployment, 1);
				}
				for (const auto& Deployment : Listing["deployments"])
					if (!ShownBranches.contains(Field(Deployment, "branchId"))) DrawDeployment(Deployment, 0);
				for (const auto& Record : Listing["resolved"])
					Row(Record, "resolved", "branchId", "조사 종료  " + Field(Record, "name"), 0);
				ImGui::EndTable();
			}
			ImGui::EndChild();
			const bool Dump = SelectedKind == "dump", Deployment = SelectedKind == "deployment", Active = SelectedKind == "branch";
			ImGui::BeginDisabled(!Dump && !Deployment && !Active);
			if (ImGui::Button("선택 항목 열기")) OpenRecord(Selected, Dump);
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::BeginDisabled(!Dump && !Deployment);
			if (ImGui::Button("선택 항목 삭제"))
				Preview({Dump ? "DeleteDump" : "DeleteDeployment", {}, Field(Selected, "deploymentId"), {}, Dump ? Field(Selected, "dumpNumber") : ""});
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::BeginDisabled(!Deployment && !Active);
			if (ImGui::Button("덤프 일괄 삭제"))
				Preview({"DeleteDump", Active ? "Branch" : "", Field(Selected, Active ? "branchId" : "deploymentId")});
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::BeginDisabled(!Deployment || Field(Selected, "branchId").empty() || SourceSha(Selected).empty());
			if (ImGui::Button("같은 커밋의 덤프 삭제"))
				Preview({"DeleteDump", "Commit", Field(Selected, "branchId"), SourceSha(Selected)});
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::BeginDisabled(!Active);
			if (ImGui::Button("조사 종료")) Preview({"Resolve", "Branch", Field(Selected, "branchId")});
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::BeginDisabled(!Dump);
			if (ImGui::Button("GitHub 이슈")) { IssueTitle.clear(); IssueBody.clear(); ShowIssue = true; }
			ImGui::EndDisabled();
		}
		ImGui::EndDisabled();

		if (ShowRelease) { ImGui::OpenPopup("Release 게시##confirm"); ShowRelease = false; }
		if (ImGui::BeginPopupModal("Release 게시##confirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("빌드  %s / %s", Field(Build, "target").c_str(), Field(Build, "configuration").c_str());
			ImGui::Text("빌드에 기록된 커밋  %s", Field(Build, "commitSha").c_str());
			if (ImGui::RadioButton("새 브랜치", !Current)) Current = false;
			ImGui::BeginDisabled(Current);
			ImGui::SetNextItemWidth(385);
			ImGui::InputText("브랜치명", &Branch);
			ImGui::EndDisabled();
			ImGui::BeginDisabled(!CanCurrent);
			if (ImGui::RadioButton(("현재 브랜치 (" + Field(Build, "workingBranch") + ")").c_str(), Current)) Current = true;
			ImGui::EndDisabled();
			ImGui::BeginDisabled(!Current && Trim(Branch).empty());
			if (ImGui::Button("게시"))
			{
				Release.CurrentBranch = Current;
				Release.Branch = Current ? "" : Trim(Branch);
				Ui.Run([Request = Release](Store& Data) { return Data.Release(Request); },
					[&](const Json&) { Search.clear(); Commit.clear(); Refresh(); });
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("취소")) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		if (ShowRemoval) { ImGui::OpenPopup("정리 확인"); ShowRemoval = false; }
		if (ImGui::BeginPopupModal("정리 확인", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			const auto Id = Field(Plan, "confirmation");
			ImGui::Text("대상 배포 %zu건, 덤프 %zu건, 브랜치 %zu건", Plan["deployments"].size(), Plan["dumps"].size(), Plan["branches"].size());
			if (const auto Count = Plan.value("publications", size_t{}); Count > 0)
				ImGui::Text("중단된 배포 작업 %zu건도 함께 정리합니다.", Count);
			ImGui::Text("확인하려면 %s 입력", Id.c_str());
			ImGui::InputText("확인", &Confirmation);
			ImGui::BeginDisabled(Confirmation != Id);
			if (ImGui::Button("정리"))
			{
				Removal.ConfirmId = Confirmation;
				Removal.ConfirmPlan = Field(Plan, "plan");
				Ui.Run([Request = Removal](Store& Data) { return Data.Remove(Request); }, [&](const Json& Result)
				{
					if (!Result.value("completed", false)) throw std::runtime_error("The records changed. Review the deletion again.");
					CleanupCandidates = Result["cleanupCandidates"].get<std::vector<std::string>>();
					Refresh();
				});
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("취소")) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		if (!Ui.Result.valid() && !CleanupCandidates.empty())
		{
			const auto Candidate = CleanupCandidates.back();
			CleanupCandidates.pop_back();
			const auto Question = L"연결된 덤프가 없습니다. 이 배포의 정리 범위를 확인할까요?\n" + Wide(Candidate);
			if (MessageBoxW(Hwnd, Question.c_str(), L"배포 정리", MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION) == IDYES)
				Preview({"DeleteDeployment", {}, Candidate});
		}
		if (ShowIssue) { ImGui::OpenPopup("GitHub 이슈 작성"); ShowIssue = false; }
		if (ImGui::BeginPopupModal("GitHub 이슈 작성", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::InputText("제목", &IssueTitle);
			ImGui::InputTextMultiline("내용", &IssueBody, {440, 110});
			ImGui::BeginDisabled(Trim(IssueTitle).empty());
			if (ImGui::Button("작성"))
			{
				Ui.Run([Number = Field(Selected, "dumpNumber"), Title = Trim(IssueTitle), Body = IssueBody](Store& Data)
					{ return Data.Issue(Number, Title, Body); }, [&](const Json&) { Refresh(); });
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("취소")) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
	});
}

int ReporterUI(const fs::path& Executable, fs::path Report)
{
	const auto Dump = Report.empty() ? ChooseDumpFile(nullptr, ReportRoot()) : ResolveDumpFile(Report);
	if (Dump.empty()) return 0;
	if (!fs::is_regular_file(Dump)) throw std::runtime_error("선택한 덤프 파일을 찾을 수 없습니다.");
	Context Ui(Executable);
	ReportRequest Request;
	Request.DumpFile = Dump;
	bool Developer{}, CreateIssue{};
	return Window(L"Hitori 크래시 리포트", [&](HWND)
	{
		Ui.Poll();
		ImGui::BeginDisabled(Ui.Result.valid());
		Ui.Settings();
		ImGui::TextWrapped("로컬 덤프  %s", PathText(Dump).c_str());
		if (ImGui::Button("폴더 열기")) OpenPath(Dump.parent_path());
		if (!Developer)
		{
			if (ImGui::Button("개발자 권한 확인"))
				Ui.Start([Repository = Ui.Repository, Dump] { CheckDeveloper(Repository, Dump); return Json{}; },
					[&](const Json&) { Developer = true; });
		}
		else
		{
			ImGui::Checkbox("기존 배포가 없으면 새 Crash 배포 만들기", &Request.AllowCrash);
			if (Request.AllowCrash)
				ImGui::TextDisabled("덤프와 일치하는 원래 빌드 파일을 자동으로 찾습니다.");
			ImGui::Checkbox("GitHub 이슈 작성", &CreateIssue);
			if (CreateIssue)
			{
				ImGui::InputText("제목", &Request.IssueTitle);
				ImGui::InputTextMultiline("내용", &Request.IssueBody, {0, 110});
			}
		}
		ImGui::BeginDisabled(Ui.Address.empty() || (CreateIssue && Trim(Request.IssueTitle).empty()));
		if (ImGui::Button("덤프 제출"))
		{
			auto Submission = Request;
			Submission.IssueTitle = CreateIssue ? Trim(Request.IssueTitle) : "";
			Ui.Run([Submission](Store& Data) { return Data.Crash(Submission); }, [&](const Json& Result)
			{
				Ui.Status = "덤프 " + Field(Result, "dumpNumber") + " 제출 완료. 배포 " + Field(Result, "deploymentId");
			});
		}
		ImGui::EndDisabled();
		ImGui::EndDisabled();
		Ui.Message();
	}, 770, 420);
}
}
