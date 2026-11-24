#pragma once

#include <filesystem>

#include "Core/EngineString.h"
#include "Core/Types.h"

// UE의 FPaths처럼 엔진 폴더와 프로젝트 폴더를 나눠서 다룬다.
// 엔진 폴더: Resources(셰이더, 스플래시)와 Build/ShaderCache가 있는 곳.
// 프로젝트 폴더: .hproject 파일과 Assets, GameConfig, Settings, Saved가 있는 곳.
// 프로젝트를 정하면 작업 디렉터리를 프로젝트 폴더로 바꾸므로 "Assets/..." 같은 기존 상대 경로는 그대로 쓴다.
// 엔진 파일은 EngineFile()로 절대 경로를 만들어 연다.
class FPaths
{
public:
	static constexpr const wchar_t* ProjectExtension = L".hproject";

	// exe 위치에서 위로 올라가며 Resources/Shader가 있는 폴더를 찾아 엔진 폴더로 정한다.
	static bool InitEngineDir();

	static const std::filesystem::path& EngineDir();
	static const std::filesystem::path& ProjectDir();
	static const std::filesystem::path& ProjectFilePath();
	static const FString& ProjectName();
	static bool HasProject();

	// .hproject 파일을 읽어 현재 프로젝트로 정하고 작업 디렉터리를 프로젝트 폴더로 바꾼다.
	static bool SetProject(const std::filesystem::path& ProjectFile, FString* OutError = nullptr);

	// 엔진 폴더 기준 경로. 절대 경로는 그대로 돌려준다.
	static std::filesystem::path EngineFile(const std::filesystem::path& Relative);
	// Resources와 Build/ShaderCache는 엔진 기준으로 해석한다. 그 밖의 상대 경로는 프로젝트 기준이다.
	// 셰이더 키처럼 엔진 상대 경로가 데이터로 돌아다니는 곳에서 파일을 열 때 쓴다.
	static std::filesystem::path ResolveEngineRelative(const FString& Path);
	// 엔진 폴더 기준 경로의 UTF-8 문자열. FString을 받는 로더에 넘길 때 쓴다.
	static FString EngineFileUtf8(const std::filesystem::path& Relative);

	// 엔진 쪽 사용자 데이터(최근 프로젝트 목록 등)를 두는 폴더.
	static std::filesystem::path EngineSavedDir();

	static FString ToUtf8(const std::filesystem::path& Path);
	static std::filesystem::path FromUtf8(const FString& Utf8);

	// Dir 바로 아래에 있는 첫 .hproject 파일. 없으면 빈 경로.
	static std::filesystem::path FindProjectFileIn(const std::filesystem::path& Dir);
};

// .hproject 파일 내용. JSON 형식이다.
struct FProjectDescriptor
{
	static constexpr int32 CurrentFileVersion = 1;

	int32 FileVersion = CurrentFileVersion;
	FString Name;
	FString Description;

	bool Load(const std::filesystem::path& File, FString* OutError = nullptr);
	bool Save(const std::filesystem::path& File) const;
};
