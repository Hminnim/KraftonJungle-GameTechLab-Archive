#include "Binary.h"

#include <cstring>
#include <format>
#include <fstream>
#include <stdexcept>

namespace VersionStore
{
namespace
{
	using Bytes = std::string_view;
	Bytes Slice(Bytes Data, size_t Offset, size_t Length)
	{
		if (Offset > Data.size() || Length > Data.size() - Offset) throw std::runtime_error("Truncated binary data.");
		return Data.substr(Offset, Length);
	}
	template<class T> T At(Bytes Data, size_t Offset)
	{
		T Value{};
		std::memcpy(&Value, Slice(Data, Offset, sizeof(T)).data(), sizeof(T));
		return Value;
	}
	uint32_t U32(Bytes Data, size_t Offset) { return At<uint32_t>(Data, Offset); }
	std::string GuidAge(Bytes Data, size_t Guid, size_t Age)
	{
		const auto Value = At<GUID>(Data, Guid);
		return std::format("{:08x}{:04x}{:04x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}:{}",
			Value.Data1, Value.Data2, Value.Data3, Value.Data4[0], Value.Data4[1], Value.Data4[2], Value.Data4[3],
			Value.Data4[4], Value.Data4[5], Value.Data4[6], Value.Data4[7], U32(Data, Age));
	}
	std::string CodeView(Bytes Data, size_t Offset, size_t Size)
	{
		if (Size < 24 || Slice(Data, Offset, 4) != "RSDS") return {};
		return GuidAge(Data, Offset + 4, Offset + 20);
	}
	std::wstring Utf16(Bytes Data)
	{
		if (Data.size() % 2) throw std::runtime_error("Invalid UTF-16 path.");
		std::wstring Text(Data.size() / 2, L'\0');
		std::memcpy(Text.data(), Data.data(), Data.size());
		return Text;
	}
	size_t PdbStream(Bytes Data, size_t Stream, uint32_t Required)
	{
		if (!Data.starts_with("Microsoft C/C++ MSF 7.00")) throw std::runtime_error("Expected an MSF 7 PDB.");
		const auto Block = U32(Data, 32), Size = U32(Data, 44), Map = U32(Data, 52);
		if (Block < 512 || Block > 65536 || Size < 12 || Size > Data.size()) throw std::runtime_error("Invalid PDB block directory.");
		std::string Directory;
		for (size_t Written = 0; Written < Size; Written += Block)
		{
			const auto Number = U32(Data, size_t(Map) * Block + Written / Block * 4);
			Directory += Slice(Data, size_t(Number) * Block, std::min<size_t>(Block, Size - Written));
		}
		const auto Streams = U32(Directory, 0);
		if (Streams <= Stream) throw std::runtime_error("Required PDB stream is missing.");
		size_t Ignored = 0;
		for (size_t Index = 0; Index < Stream; ++Index)
		{
			const auto Length = U32(Directory, 4 + Index * 4);
			if (Length != UINT32_MAX) Ignored += (size_t(Length) + Block - 1) / Block;
		}
		const auto StreamLength = U32(Directory, 4 + Stream * 4);
		if (StreamLength == UINT32_MAX || StreamLength < Required) throw std::runtime_error("Truncated PDB stream.");
		return size_t(Block) * U32(Directory, 4 + 4 * (Streams + Ignored));
	}
}

DumpInfo ReadDump(const fs::path& Path)
{
	const auto Data = ReadBytes(Path);
	if (Slice(Data, 0, 4) != "MDMP") throw std::runtime_error("Expected a Windows minidump.");
	const auto Count = U32(Data, 8), Directory = U32(Data, 12);
	Slice(Data, Directory, size_t(Count) * 12);
	DumpInfo Result;
	for (size_t Index = 0; Index < Count; ++Index)
	{
		const size_t Entry = Directory + Index * 12;
		const auto Type = U32(Data, Entry), Size = U32(Data, Entry + 4), Offset = U32(Data, Entry + 8);
		if (Type == 6 && Size >= 12) Result.ExceptionCode = std::format("0x{:08X}", U32(Data, size_t(Offset) + 8));
		if (Type != 4) continue;
		const auto List = Slice(Data, Offset, Size);
		const auto Modules = U32(List, 0);
		if (Modules > (List.size() - 4) / 108) throw std::runtime_error("Invalid dump module list.");
		for (size_t Position = 4; Position < 4 + size_t(Modules) * 108; Position += 108)
		{
			const size_t Name = U32(List, Position + 20);
			const fs::path ImagePath(Utf16(Slice(Data, Name + 4, U32(Data, Name))));
			Result.Modules.push_back({PathText(ImagePath.filename()), CodeView(Data, U32(List, Position + 80), U32(List, Position + 76)),
				U32(List, Position + 16), U32(List, Position + 8), ImagePath});
		}
	}
	if (Result.Modules.empty()) throw std::runtime_error("The dump has no module list.");
	return Result;
}

Module ReadImage(const fs::path& Path)
{
	const auto Data = ReadBytes(Path);
	const size_t Pe = U32(Data, 60);
	if (U32(Data, Pe) != 0x4550) throw std::runtime_error("Expected a PE image.");
	const size_t Optional = Pe + 24;
	const auto Count = At<uint16_t>(Data, Pe + 6), OptionalSize = At<uint16_t>(Data, Pe + 20);
	Module Result{PathText(Path.filename()), {}, U32(Data, Pe + 8), U32(Data, Optional + 56)};
	const size_t DataDirectory = At<uint16_t>(Data, Optional) == 0x20B ? 112 : 96;
	if (OptionalSize < DataDirectory + 56) return Result;
	const auto Rva = U32(Data, Optional + DataDirectory + 48), Size = U32(Data, Optional + DataDirectory + 52);
	for (size_t Index = 0; Index < Count; ++Index)
	{
		const size_t Section = Optional + OptionalSize + Index * 40;
		const auto Base = U32(Data, Section + 12), RawSize = U32(Data, Section + 16);
		if (Rva < Base || Rva - Base >= RawSize) continue;
		const size_t Offset = size_t(U32(Data, Section + 20)) + Rva - Base;
		for (size_t Debug = 0; Debug + 28 <= Size; Debug += 28)
		{
			const size_t Entry = Offset + Debug;
			if (U32(Data, Entry + 12) != 2) continue;
			Result.CodeView = CodeView(Data, U32(Data, Entry + 24), U32(Data, Entry + 16));
			if (!Result.CodeView.empty()) break;
		}
		break;
	}
	return Result;
}

std::string ReadPdb(const fs::path& Path)
{
	const auto Data = ReadBytes(Path);
	const auto Info = PdbStream(Data, 1, 28);
	return GuidAge(Data, Info + 12, Info + 8);
}

Json ReadBuildManifest(const fs::path& Image)
{
	auto Named = Image;
	Named.replace_extension(".build.json");
	auto Build = Read(fs::exists(Named) ? Named : Image.parent_path() / "build.json");
	if (_wcsicmp(Wide(Field(Build, "target")).c_str(), Image.stem().c_str()) != 0 || !Build.contains("modules")) return {};
	for (const auto& Module : Build["modules"])
	{
		if (_wcsicmp(Wide(Field(Module, "name")).c_str(), Image.filename().c_str()) != 0) continue;
		const auto Expected = Wide(Field(Module, "sha256"));
		if (!Expected.empty() && _wcsicmp(Expected.c_str(), Wide(HashFile(Image)).c_str()) == 0) return Build;
		break;
	}
	return {};
}

void RestorePdbIdentity(const fs::path& Original, const fs::path& Modified)
{
	const auto Source = ReadBytes(Original), Target = ReadBytes(Modified);
	const auto Info = Slice(Source, PdbStream(Source, 1, 28) + 4, 24);
	const auto Dbi = Slice(Source, PdbStream(Source, 3, 12) + 8, 4);
	std::fstream File(Modified, std::ios::binary | std::ios::in | std::ios::out);
	File.exceptions(std::ios::badbit | std::ios::failbit);
	File.seekp(PdbStream(Target, 1, 28) + 4);
	File.write(Info.data(), Info.size());
	File.seekp(PdbStream(Target, 3, 12) + 8);
	File.write(Dbi.data(), Dbi.size());
}
}
