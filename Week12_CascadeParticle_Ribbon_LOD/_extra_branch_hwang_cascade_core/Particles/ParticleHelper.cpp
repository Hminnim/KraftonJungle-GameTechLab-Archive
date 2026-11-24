#include "EnginePCH.h"
#include "ParticleHelper.h"
#include "Core/HAL/UnrealMemory.h"
#include <assert.h>

void FParticleDataContainer::Alloc(int32 InParticleDataNumBytes, int32 InParticleIndicesNumShorts)
{
	Free();

	if (InParticleDataNumBytes <= 0 || InParticleIndicesNumShorts < 0)
	{
		return;
	}

	const uint64 AlignedParticleDataBytes = FMemory::QuantizeSize(static_cast<uint64>(InParticleDataNumBytes), alignof(uint16));
	const uint64 IndicesBytes = static_cast<uint64>(InParticleIndicesNumShorts) * sizeof(uint16);
	const uint64 TotalBytes = AlignedParticleDataBytes + IndicesBytes;
	if (TotalBytes == 0 || TotalBytes > static_cast<uint64>(INT32_MAX))
	{
		return;
	}

	ParticleDataNumBytes = static_cast<int32>(AlignedParticleDataBytes);
	ParticleIndicesNumShorts = InParticleIndicesNumShorts;
	MemBlockSize = static_cast<int32>(TotalBytes);

	ParticleData = static_cast<uint8*>(FMemory::Malloc(TotalBytes));
	if (ParticleData == nullptr)
	{
		Free();
		return;
	}

	assert((reinterpret_cast<uintptr_t>(ParticleData) & (alignof(FBaseParticle) - 1)) == 0);
	FMemory::Memzero(ParticleData, TotalBytes);

	ParticleIndices = (ParticleIndicesNumShorts > 0)
		? reinterpret_cast<uint16*>(ParticleData + ParticleDataNumBytes)
		: nullptr;
}

void FParticleDataContainer::Free()
{
	if(ParticleData)
	{		
		FMemory::Free(ParticleData);
	}
	MemBlockSize = 0;
	ParticleDataNumBytes = 0;
	ParticleIndicesNumShorts = 0;
	ParticleData = nullptr;
	ParticleIndices = nullptr;
}
