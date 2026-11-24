#pragma once

#include "Math/Vector.h"
#include "Math/Matrix.h"
#include "Containers/Array.h"
#include "Math/EngineMath.h"

struct FRandomStream
{
public:
	FRandomStream()
		: InitialSeed(0)
		, Seed(0)
	{
	}

	FRandomStream(int32 InSeed)
	{
		Initialize(InSeed);
	}

	void Initialize(int32 InSeed)
	{
		InitialSeed = InSeed;
		Seed = uint32(InSeed);
	}

	void Reset() const
	{
		Seed = uint32(InitialSeed);
	}

	int32 GetInitialSeed() const
	{
		return InitialSeed;
	}

	void GenerateNewSeed()
	{
		Initialize(FMath::Rand());
	}

	float GetFraction() const
	{
		MutateSeed();

		float Result;

		*(uint32*)&Result = 0x3F800000U | (Seed >> 9);

		return Result - 1.0f;
	}

protected:
	void MutateSeed() const
	{
		Seed = (Seed * 196314165U) + 907633515U;
	}

private:
	int32 InitialSeed;
	mutable uint32 Seed;
};