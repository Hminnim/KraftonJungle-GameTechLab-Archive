#include "EnginePCH.h"
#include "UnrealMemory.h"
#include "Serialization/LargeMemoryData.h"
#include <malloc.h>
#include <vector>

namespace
{
	bool IsValidAlignment(uint32 Alignment)
	{
		return Alignment != 0 && (Alignment & (Alignment - 1)) == 0;
	}
}

uint64 FMemory::QuantizeSize(uint64 Count, uint32 Alignment)
{
	return (Count + Alignment - 1) & ~static_cast<uint64>(Alignment - 1);
}

void* FMemory::Malloc(uint64 Count, uint32 Alignment)
{
	if (!IsValidAlignment(Alignment))
	{
		return nullptr;
	}
	return _aligned_malloc(static_cast<size_t>(Count), Alignment);
}

void* FMemory::Realloc(void* Original, uint64 Count, uint32 Alignment)
{
	if (!IsValidAlignment(Alignment))
	{
		return nullptr;
	}
	return _aligned_realloc(Original, static_cast<size_t>(Count), Alignment);
}

void FMemory::Free(void* Original)
{
	_aligned_free(Original);
}

int32 RunMemorySelfTest()
{
	int32 Failed = 0;
	auto Check = [&Failed](bool bCondition, const char* What)
	{
		if (!bCondition)
		{
			++Failed;
			HTR_LOG(Error, "[Memory] self-test FAILED: {}", What);
		}
	};
	auto FillPattern = [](uint8* P, uint64 Count)
	{
		for (uint64 i = 0; i < Count; ++i) { P[i] = static_cast<uint8>(i * 31 + 7); }
	};
	auto PatternOk = [](const uint8* P, uint64 Count)
	{
		for (uint64 i = 0; i < Count; ++i) { if (P[i] != static_cast<uint8>(i * 31 + 7)) { return false; } }
		return true;
	};

	// 16보다 큰 정렬이 지켜지고, Realloc으로 커져도 정렬과 내용이 유지된다
	uint8* Block = static_cast<uint8*>(FMemory::Malloc(100, 64));
	Check(Block != nullptr && reinterpret_cast<uintptr_t>(Block) % 64 == 0, "Malloc alignment 64");
	if (Block)
	{
		FillPattern(Block, 100);
		uint8* Grown = static_cast<uint8*>(FMemory::Realloc(Block, 100000, 64));
		Check(Grown != nullptr && reinterpret_cast<uintptr_t>(Grown) % 64 == 0, "Realloc alignment 64 after growth");
		if (Grown)
		{
			Block = Grown;
			Check(PatternOk(Block, 100), "Realloc keeps contents");
		}
		// 잘못된 정렬은 거부하고 원래 블록은 그대로 유효하다
		Check(FMemory::Realloc(Block, 200000, 48) == nullptr, "Realloc rejects non power-of-two alignment");
		Check(PatternOk(Block, 100), "block is intact after rejected Realloc");
		FMemory::Free(Block);
	}
	Check(FMemory::Malloc(64, 48) == nullptr, "Malloc rejects non power-of-two alignment");
	Check(FMemory::Malloc(64, 0) == nullptr, "Malloc rejects alignment 0");
	FMemory::Free(nullptr);

	// 기본 정렬
	void* DefaultBlock = FMemory::Malloc(33);
	Check(DefaultBlock != nullptr && reinterpret_cast<uintptr_t>(DefaultBlock) % FMemory::DefaultAlignment == 0, "default alignment");
	FMemory::Free(DefaultBlock);

	// FLargeMemoryData: 64KB를 넘겨 여러 번 성장시킨 뒤 그대로 읽힌다
	{
		const uint64 Total = 300000;
		std::vector<uint8> Source(Total);
		FillPattern(Source.data(), Total);

		FLargeMemoryData Large;
		for (uint64 Offset = 0; Offset < Total; Offset += 1000)
		{
			Large.Append(Source.data() + Offset, 1000);
		}
		Check(Large.GetSize() == static_cast<int64>(Total), "FLargeMemoryData size");

		std::vector<uint8> Readback(Total);
		Check(Large.Read(Readback.data(), 0, static_cast<int64>(Total)), "FLargeMemoryData read");
		Check(FMemory::Memcmp(Source.data(), Readback.data(), Total) == 0, "FLargeMemoryData contents after growth");
	}

	if (Failed == 0)
	{
		HTR_LOG(Info, "[Memory] self-test: all passed");
	}
	return Failed;
}
