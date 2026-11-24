#pragma once

#include "Core/Types.h"
#include <random>

// 시드를 지정할 수 있는 난수 열 (UE: Core/Math/RandomStream.h의 FRandomStream)
// 같은 시드에서 같은 수열이 나온다. 파티클 모듈이 값을 뽑을 때 이 스트림을 쓰면 시뮬레이션을 재현할 수 있다.
// UE와 다름: UE는 스트림이 없으면 전역 SRand를 쓴다. 여기서는 전역 상태를 두지 않는다.
struct FRandomStream
{
	explicit FRandomStream(uint32 InSeed = 0) { Initialize(InSeed); }

	void Initialize(uint32 InSeed)
	{
		Seed = InSeed;
		Generator.seed(InSeed);
	}

	// [0, 1) 구간의 실수. 32비트 정수의 상위 24비트를 float 가수 크기에 맞춰 나눈다.
	// std::uniform_real_distribution은 구현마다 결과가 달라질 수 있어서 쓰지 않는다 (값 재현성이 목적이다).
	float GetFraction()
	{
		return static_cast<float>(Generator() >> 8) * (1.0f / 16777216.0f);
	}

	// [Min, Max] 구간의 실수
	float GetFraction(float Min, float Max)
	{
		return Min + (Max - Min) * GetFraction();
	}

	// [Min, Max] 구간의 정수 (양끝 포함). Max < Min이면 Min이고 난수를 쓰지 않는다.
	// 난수를 하나 써서 구간 크기를 곱하고 내림한다. 구간이 2^24보다 훨씬 작을 때 쓰는 용도다 (파티클 개수 범위 등).
	int32 GetInt(int32 Min, int32 Max)
	{
		if (Max < Min)
		{
			return Min;
		}
		const int32 Range = Max - Min + 1;
		const int32 Value = Min + static_cast<int32>(GetFraction() * static_cast<float>(Range));
		return Value > Max ? Max : Value;
	}

	uint32 GetSeed() const { return Seed; }

private:
	uint32 Seed = 0;
	std::mt19937 Generator;
};
