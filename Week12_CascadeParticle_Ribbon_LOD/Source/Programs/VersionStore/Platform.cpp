#include "Platform.h"

#include <bcrypt.h>
#include <lm.h>
#include <sddl.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <wrl/client.h>
#include <chrono>
#include <format>
#include <fstream>
#include <map>
#include <memory>
#include <stdexcept>
#include <thread>

namespace VersionStore
{
namespace
{
	struct LocalFreeDeleter { void operator()(void* Pointer) const { LocalFree(Pointer); } };
	std::wstring Quote(std::wstring_view Value)
	{
		std::wstring Result = L"\"";
		size_t Slashes = 0;
		for (auto Ch : Value)
		{
			if (Ch == L'\\') { ++Slashes; continue; }
			Result.append(Ch == L'"' ? Slashes * 2 + 1 : Slashes, L'\\');
			Result += Ch;
			Slashes = 0;
		}
		return Result + std::wstring(Slashes * 2, L'\\') + L'"';
	}

	std::wstring CommandLine(const fs::path& File, const Args& Arguments)
	{
		auto Line = Quote(File.native());
		for (const auto& Arg : Arguments) Line += L" " + Quote(Arg);
		return Line;
	}

	std::string ReadPipe(HANDLE Pipe)
	{
		std::string Text;
		char Buffer[16384];
		DWORD Bytes{};
		while (ReadFile(Pipe, Buffer, sizeof(Buffer), &Bytes, nullptr) && Bytes) Text.append(Buffer, Bytes);
		return Text;
	}

	void Require(DWORD Code, const char* Operation)
	{
		if (Code) throw std::runtime_error(std::format("{} failed (Windows error {}).", Operation, Code));
	}

	std::string TimestampText(const SYSTEMTIME& Utc)
	{
		return std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}Z", Utc.wYear, Utc.wMonth, Utc.wDay, Utc.wHour, Utc.wMinute, Utc.wSecond);
	}

	struct Sha256
	{
		BCRYPT_ALG_HANDLE Algorithm{};
		BCRYPT_HASH_HANDLE State{};
		Sha256()
		{
			Require(BCryptOpenAlgorithmProvider(&Algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0), "SHA-256");
			if (auto Status = BCryptCreateHash(Algorithm, &State, nullptr, 0, nullptr, 0, 0))
			{
				BCryptCloseAlgorithmProvider(Algorithm, 0);
				Require(Status, "SHA-256");
			}
		}
		~Sha256() { BCryptDestroyHash(State); BCryptCloseAlgorithmProvider(Algorithm, 0); }
		void Add(std::string_view Bytes)
		{
			Require(BCryptHashData(State, reinterpret_cast<PUCHAR>(const_cast<char*>(Bytes.data())),
				static_cast<ULONG>(Bytes.size()), 0), "SHA-256");
		}
		std::string Finish()
		{
			unsigned char Bytes[32];
			Require(BCryptFinishHash(State, Bytes, sizeof(Bytes), 0), "SHA-256");
			std::string Hex;
			for (auto Byte : Bytes) Hex += std::format("{:02x}", Byte);
			return Hex;
		}
	};
}

std::string Utf8(std::wstring_view Text)
{
	if (Text.empty()) return {};
	const auto Size = static_cast<int>(Text.size());
	const auto Length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, Text.data(), Size, nullptr, 0, nullptr, nullptr);
	if (!Length) Require(GetLastError(), "UTF-8 conversion");
	std::string Result(Length, '\0');
	WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, Text.data(), Size, Result.data(), Length, nullptr, nullptr);
	return Result;
}

std::wstring Wide(std::string_view Text)
{
	if (Text.empty()) return {};
	const auto Size = static_cast<int>(Text.size());
	const auto Length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Text.data(), Size, nullptr, 0);
	if (!Length) Require(GetLastError(), "Unicode conversion");
	std::wstring Result(Length, L'\0');
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Text.data(), Size, Result.data(), Length);
	return Result;
}

std::string PathText(const fs::path& Path) { return Utf8(Path.native()); }
std::wstring Env(const wchar_t* Name)
{
	const auto Size = GetEnvironmentVariableW(Name, nullptr, 0);
	if (!Size) return {};
	std::wstring Value(Size, L'\0');
	Value.resize(GetEnvironmentVariableW(Name, Value.data(), Size));
	return Value;
}

std::string Trim(std::string Text)
{
	const auto Begin = Text.find_first_not_of(" \r\n\t");
	if (Begin == std::string::npos) return {};
	return Text.substr(Begin, Text.find_last_not_of(" \r\n\t") - Begin + 1);
}

std::string Field(const Json& Record, const char* Key)
{
	const auto It = Record.find(Key);
	return It != Record.end() && It->is_string() ? It->get<std::string>() : std::string{};
}

std::string Id()
{
	GUID Value{};
	Require(CoCreateGuid(&Value), "CoCreateGuid");
	wchar_t Text[40];
	StringFromGUID2(Value, Text, 40);
	std::string Result;
	for (auto Ch : Utf8(Text)) if (std::isxdigit(static_cast<unsigned char>(Ch))) Result += static_cast<char>(std::tolower(Ch));
	return Result;
}

std::string TimeStamp()
{
	SYSTEMTIME Utc{};
	GetSystemTime(&Utc);
	return TimestampText(Utc);
}

std::string FileTimeStamp(const fs::path& Path)
{
	WIN32_FILE_ATTRIBUTE_DATA Data{};
	if (!GetFileAttributesExW(Path.c_str(), GetFileExInfoStandard, &Data)) Require(GetLastError(), "Reading the file timestamp");
	SYSTEMTIME Utc{};
	if (!FileTimeToSystemTime(&Data.ftLastWriteTime, &Utc)) Require(GetLastError(), "Converting the file timestamp");
	return TimestampText(Utc);
}

std::string Account() { return Utf8(Env(L"USERDOMAIN") + L"\\" + Env(L"USERNAME")); }

std::string ReadBytes(const fs::path& Path)
{
	std::ifstream Stream(Path, std::ios::binary);
	if (!Stream) throw std::runtime_error("Cannot read " + PathText(Path));
	return {std::istreambuf_iterator<char>(Stream), std::istreambuf_iterator<char>()};
}

Json Read(const fs::path& Path)
{
	if (!fs::exists(Path)) return {};
	return Json::parse(ReadBytes(Path));
}

void Replace(const fs::path& Source, const fs::path& Destination)
{
	if (!MoveFileExW(Source.c_str(), Destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		Require(GetLastError(), "Replacing a file");
}

void Write(const fs::path& Path, const Json& Value)
{
	fs::create_directories(Path.parent_path());
	const auto Temporary = Path.native() + Wide("." + Id() + ".tmp");
	try
	{
		std::ofstream Stream(Temporary, std::ios::binary);
		Stream.exceptions(std::ios::badbit | std::ios::failbit);
		Stream << Value.dump(2) << '\n';
		Stream.close();
		Replace(Temporary, Path);
	}
	catch (...) { std::error_code Error; fs::remove(Temporary, Error); throw; }
}

std::string Hash(std::string_view Text) { Sha256 State; State.Add(Text); return State.Finish(); }
std::string HashFile(const fs::path& Path)
{
	std::ifstream Stream(Path, std::ios::binary);
	if (!Stream) throw std::runtime_error("Cannot hash " + PathText(Path));
	Sha256 State;
	char Buffer[65536];
	while (Stream) { Stream.read(Buffer, sizeof(Buffer)); State.Add({Buffer, static_cast<size_t>(Stream.gcount())}); }
	if (!Stream.eof()) throw std::runtime_error("Could not finish reading " + PathText(Path));
	return State.Finish();
}

void WithLock(const fs::path& Path, const std::function<void()>& Operation)
{
	for (unsigned Attempt = 0;; ++Attempt)
	{
		Handle Lock(CreateFileW(Path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
		if (Lock) { Operation(); return; }
		const auto Error = GetLastError();
		if (Error != ERROR_SHARING_VIOLATION || Attempt == 400) Require(Error, "Acquiring the version store lock");
		std::this_thread::sleep_for(std::chrono::milliseconds(150));
	}
}

CommandResult Run(const fs::path& File, const Args& Arguments, std::string_view Input,
	const wchar_t* OverrideName, const std::wstring& OverrideValue)
{
	SECURITY_ATTRIBUTES Inherit{sizeof(Inherit), nullptr, TRUE};
	const auto Pipe = [&](Handle& Read, Handle& Write)
	{
		HANDLE RawRead{}, RawWrite{};
		if (!CreatePipe(&RawRead, &RawWrite, &Inherit, 0)) Require(GetLastError(), "Creating a command pipe");
		Read.Reset(RawRead);
		Write.Reset(RawWrite);
	};
	Handle OutRead, OutHandle, ErrRead, ErrHandle, InHandle, InWrite;
	Pipe(OutRead, OutHandle);
	Pipe(ErrRead, ErrHandle);
	Pipe(InHandle, InWrite);

	std::vector<wchar_t> Environment;
	if (OverrideName)
	{
		struct Less { bool operator()(const std::wstring& Left, const std::wstring& Right) const { return _wcsicmp(Left.c_str(), Right.c_str()) < 0; } };
		std::map<std::wstring, std::wstring, Less> Vars;
		auto Block = GetEnvironmentStringsW();
		if (!Block) Require(GetLastError(), "GetEnvironmentStringsW");
		for (auto Ptr = Block; *Ptr; Ptr += wcslen(Ptr) + 1)
		{
			std::wstring_view Entry(Ptr);
			const auto Separator = Entry.find(L'=', 1);
			if (Separator != Entry.npos) Vars.emplace(Entry.substr(0, Separator), Entry.substr(Separator + 1));
		}
		FreeEnvironmentStringsW(Block);
		Vars[OverrideName] = OverrideValue;
		for (const auto& [Name, Value] : Vars)
		{
			const auto Entry = Name + L"=" + Value;
			Environment.insert(Environment.end(), Entry.begin(), Entry.end());
			Environment.push_back(L'\0');
		}
		Environment.push_back(L'\0');
	}
	STARTUPINFOEXW Startup{};
	Startup.StartupInfo.cb = sizeof(Startup);
	Startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
	Startup.StartupInfo.hStdInput = InHandle;
	Startup.StartupInfo.hStdOutput = OutHandle;
	Startup.StartupInfo.hStdError = ErrHandle;
	SIZE_T AttributeSize{};
	InitializeProcThreadAttributeList(nullptr, 1, 0, &AttributeSize);
	std::vector<std::byte> Attributes(AttributeSize);
	Startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(Attributes.data());
	if (!InitializeProcThreadAttributeList(Startup.lpAttributeList, 1, 0, &AttributeSize))
		Require(GetLastError(), "InitializeProcThreadAttributeList");
	struct AttributesCleanup { LPPROC_THREAD_ATTRIBUTE_LIST List; ~AttributesCleanup() { DeleteProcThreadAttributeList(List); } } Guard{Startup.lpAttributeList};
	HANDLE Handles[]{InHandle, OutHandle, ErrHandle};
	if (!UpdateProcThreadAttribute(Startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, Handles, sizeof(Handles), nullptr, nullptr))
		Require(GetLastError(), "UpdateProcThreadAttribute");
	PROCESS_INFORMATION Child{};
	auto Line = CommandLine(File, Arguments);
	if (!CreateProcessW(nullptr, Line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT |
		EXTENDED_STARTUPINFO_PRESENT, Environment.empty() ? nullptr : Environment.data(), nullptr, &Startup.StartupInfo, &Child))
		Require(GetLastError(), "Starting the command");
	Handle Process(Child.hProcess), Thread(Child.hThread);
	OutHandle.Reset();
	ErrHandle.Reset();
	InHandle.Reset();
	CommandResult Result{};
	std::jthread OutReader([&] { Result.Out = ReadPipe(OutRead); });
	std::jthread ErrReader([&] { Result.Err = ReadPipe(ErrRead); });
	if (!Input.empty())
	{
		DWORD Written{};
		WriteFile(InWrite, Input.data(), static_cast<DWORD>(Input.size()), &Written, nullptr);
	}
	InWrite.Reset();
	WaitForSingleObject(Process, INFINITE);
	if (!GetExitCodeProcess(Process, &Result.Code)) Require(GetLastError(), "GetExitCodeProcess");
	OutReader.join();
	ErrReader.join();
	return Result;
}

std::string Check(const fs::path& File, const Args& Arguments)
{
	const auto Result = Run(File, Arguments);
	if (Result.Code) throw std::runtime_error(std::format("{} exited with code {}.\n{}", PathText(File.filename()),
		Result.Code, Trim(Result.Err.empty() ? Result.Out : Result.Err)));
	return Trim(Result.Out);
}

void StartDetached(const fs::path& File, const Args& Arguments)
{
	STARTUPINFOW Startup{};
	Startup.cb = sizeof(Startup);
	PROCESS_INFORMATION Child{};
	auto Line = CommandLine(File, Arguments);
	if (!CreateProcessW(File.c_str(), Line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &Startup, &Child))
		Require(GetLastError(), "Starting the reporter");
	CloseHandle(Child.hThread);
	CloseHandle(Child.hProcess);
}

fs::path DebugTool(const wchar_t* Name, bool SourceTool)
{
	auto Path = fs::path(Env(L"ProgramFiles(x86)")) / L"Windows Kits/10/Debuggers/x64";
	if (SourceTool) Path /= L"srcsrv";
	Path /= Name;
	if (fs::is_regular_file(Path)) return Path;
	wchar_t Located[32768];
	const auto Length = SearchPathW(nullptr, Name, nullptr, static_cast<DWORD>(std::size(Located)), Located, nullptr);
	if (Length > 0 && Length < std::size(Located) && fs::is_regular_file(Located)) return Located;
	throw std::runtime_error(Utf8(Name) + "를 찾을 수 없습니다. Windows SDK 설치 프로그램에서 Debugging Tools for Windows를 설치하세요.");
}

fs::path RepositoryFrom(fs::path Path)
{
	while (!Path.empty())
	{
		if (fs::exists(Path / ".git")) return Path;
		const auto Parent = Path.parent_path();
		if (Parent == Path) break;
		Path = Parent;
	}
	return {};
}

fs::path ReportRoot() { return fs::path(Env(L"LOCALAPPDATA")) / "HitoriCrashReports"; }

fs::path ChooseDumpFile(HWND Parent, const fs::path& Initial)
{
	using Microsoft::WRL::ComPtr;
	ComPtr<IFileOpenDialog> Dialog;
	if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&Dialog)))) return {};
	Dialog->SetOptions(FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);
	const COMDLG_FILTERSPEC Filter{L"덤프 파일 (*.dmp)", L"*.dmp"};
	Dialog->SetFileTypes(1, &Filter);
	Dialog->SetTitle(L"제출할 덤프 파일 선택");
	ComPtr<IShellItem> Folder;
	if (SUCCEEDED(SHCreateItemFromParsingName(Initial.c_str(), nullptr, IID_PPV_ARGS(&Folder)))) Dialog->SetFolder(Folder.Get());
	ComPtr<IShellItem> File;
	if (FAILED(Dialog->Show(Parent)) || FAILED(Dialog->GetResult(&File))) return {};
	PWSTR Name{};
	if (FAILED(File->GetDisplayName(SIGDN_FILESYSPATH, &Name))) return {};
	fs::path Result(Name);
	CoTaskMemFree(Name);
	return Result;
}

void OpenPath(const fs::path& Path) { ShellExecuteW(nullptr, L"open", Path.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }

void InitializeShare(const fs::path& Storage, const std::wstring& Name)
{
	auto Path = fs::absolute(Storage).lexically_normal().wstring();
	PSHARE_INFO_502 Existing{};
	const auto Result = NetShareGetInfo(nullptr, const_cast<wchar_t*>(Name.c_str()), 502, reinterpret_cast<LPBYTE*>(&Existing));
	if (Result == NERR_Success)
	{
		const auto Match = !_wcsicmp(Path.c_str(), Existing->shi502_path);
		NetApiBufferFree(Existing);
		if (!Match) throw std::runtime_error("The share name already points to a different folder.");
	}
	else if (Result != NERR_NetNameNotFound) Require(Result, "Reading SMB share (administrator privileges required)");
	for (const auto* Folder : {"symbols", "VersionData/Release", "VersionData/Crash/_resolved", "VersionData/Crash/_without-source"})
		fs::create_directories(fs::path(Path) / Folder);
	if (Result == NERR_Success) return;
	HANDLE RawToken{};
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &RawToken)) Require(GetLastError(), "Opening the account token");
	Handle Token(RawToken);
	DWORD Size{};
	GetTokenInformation(Token, TokenUser, nullptr, 0, &Size);
	std::vector<std::byte> User(Size);
	if (!GetTokenInformation(Token, TokenUser, User.data(), Size, &Size)) Require(GetLastError(), "Reading the account SID");
	LPWSTR RawSid{};
	if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(User.data())->User.Sid, &RawSid))
		Require(GetLastError(), "Converting the account SID");
	std::unique_ptr<wchar_t, LocalFreeDeleter> Sid(RawSid);
	const auto Sddl = std::wstring(L"D:(A;;FA;;;") + Sid.get() + L")";
	PSECURITY_DESCRIPTOR Descriptor{};
	if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(Sddl.c_str(), SDDL_REVISION_1, &Descriptor, nullptr))
		Require(GetLastError(), "Creating share permissions");
	std::unique_ptr<void, LocalFreeDeleter> DescriptorGuard(Descriptor);
	SHARE_INFO_502 Share{};
	Share.shi502_netname = const_cast<wchar_t*>(Name.c_str());
	Share.shi502_type = STYPE_DISKTREE;
	Share.shi502_max_uses = static_cast<DWORD>(-1);
	Share.shi502_path = Path.data();
	Share.shi502_security_descriptor = Descriptor;
	Require(NetShareAdd(nullptr, 502, reinterpret_cast<LPBYTE>(&Share), nullptr), "Creating SMB share");
}
}
