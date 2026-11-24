#pragma once

#include <Windows.h>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>
#include <json.hpp>

namespace VersionStore
{
namespace fs = std::filesystem;
using Json = nlohmann::json;
using Args = std::vector<std::wstring>;

class Handle
{
public:
	explicit Handle(HANDLE Value = nullptr) : Value(Value) {}
	~Handle() { Reset(); }
	Handle(const Handle&) = delete;
	Handle& operator=(const Handle&) = delete;
	explicit operator bool() const { return Value && Value != INVALID_HANDLE_VALUE; }
	operator HANDLE() const { return Value; }
	void Reset(HANDLE NewValue = nullptr) { if (*this) CloseHandle(Value); Value = NewValue; }
private:
	HANDLE Value;
};

struct CommandResult { DWORD Code; std::string Out, Err; };
std::string Utf8(std::wstring_view Text);
std::wstring Wide(std::string_view Text);
std::string PathText(const fs::path& Path);
std::wstring Env(const wchar_t* Name);
std::string Trim(std::string Text);
std::string Field(const Json& Record, const char* Key);
std::string Id();
std::string TimeStamp();
std::string FileTimeStamp(const fs::path& Path);
std::string Account();
std::string ReadBytes(const fs::path& Path);
Json Read(const fs::path& Path);
void Write(const fs::path& Path, const Json& Value);
void Replace(const fs::path& Source, const fs::path& Destination);
std::string Hash(std::string_view Text);
std::string HashFile(const fs::path& Path);
void WithLock(const fs::path& Path, const std::function<void()>& Operation);
CommandResult Run(const fs::path& File, const Args& Arguments, std::string_view Input = {},
	const wchar_t* OverrideName = nullptr, const std::wstring& OverrideValue = {});
std::string Check(const fs::path& File, const Args& Arguments);
void StartDetached(const fs::path& File, const Args& Arguments);
fs::path DebugTool(const wchar_t* Name, bool SourceTool = false);
fs::path RepositoryFrom(fs::path Path);
fs::path ReportRoot();
fs::path ChooseDumpFile(HWND Parent, const fs::path& Initial);
void OpenPath(const fs::path& Path);
void InitializeShare(const fs::path& Storage, const std::wstring& Name);
}
