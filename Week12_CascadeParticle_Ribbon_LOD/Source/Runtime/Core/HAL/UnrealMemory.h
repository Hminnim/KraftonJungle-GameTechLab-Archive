// Core/Memory.h (위치는 프로젝트 규칙에 맞게)
#pragma once

#include <cstring>
#include <xmmintrin.h>
#include "Core/Types.h"

// UE의 FMemory 축소판. 복사/비교/채우기는 표준 함수를 감싸고, 할당은 정렬 할당(_aligned_*)을 쓴다.
// 나중에 할당 추적이 필요하면 이 층에서 바꾼다.
struct FMemory
{
	static void* Memcpy(void* Dest, const void* Src, uint64 Count) { return std::memcpy(Dest, Src, static_cast<size_t>(Count)); }
	static void* Memmove(void* Dest, const void* Src, uint64 Count) { return std::memmove(Dest, Src, static_cast<size_t>(Count)); }
	static int32 Memcmp(const void* A, const void* B, uint64 Count) { return std::memcmp(A, B, static_cast<size_t>(Count)); }
	static void* Memset(void* Dest, uint8 Value, uint64 Count) { return std::memset(Dest, Value, static_cast<size_t>(Count)); }
	static void* Memzero(void* Dest, uint64 Count) { return std::memset(Dest, 0, static_cast<size_t>(Count)); }

	// Ptr이 속한 캐시 줄을 L1까지 미리 올리라는 힌트 (UE: FPlatformMisc::Prefetch). 값과 동작은 바뀌지 않고, 잘못된 주소여도 폴트가 나지 않는다.
	static void Prefetch(const void* Ptr) { _mm_prefetch(static_cast<const char*>(Ptr), _MM_HINT_T0); }

	static uint64 QuantizeSize(uint64 Count, uint32 Alignment = 16);

	// 기본 정렬 (alignas(16) 타입과 SSE 16바이트 접근을 위해 항상 16 이상으로 할당한다)
	static constexpr uint32 DefaultAlignment = 16;

	// Alignment는 2의 거듭제곱이어야 한다. 아니면 nullptr을 돌려준다.
	// 할당은 항상 정렬 할당(_aligned_*)으로 하므로 Free까지 반드시 FMemory로 해야 한다. std::free로 해제하면 안 된다.
	static void* Malloc(uint64 Count, uint32 Alignment = DefaultAlignment);
	// 처음 할당할 때와 같은 Alignment로 호출해야 한다. 실패하면 nullptr을 돌려주고 Original은 그대로 유효하다.
	static void* Realloc(void* Original, uint64 Count, uint32 Alignment = DefaultAlignment);
	// nullptr은 무시한다.
	static void Free(void* Original);
};

// 정렬 할당과 FLargeMemoryData의 성장 경로를 확인한다. 실패 개수를 돌려준다.
int32 RunMemorySelfTest();