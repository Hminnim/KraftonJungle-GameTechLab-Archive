#pragma once

#include "Core/Types.h"
#include "Containers/Array.h"
#include "Math/Vector.h"
#include "Math/RandomStream.h"

// 모듈이 쓰는 값의 출처를 데이터로 분리한 것 (UE: Classes/Distributions/DistributionFloat.h, DistributionVector.h)
// 상수, 균일 난수 범위, 시간에 따른 곡선 세 종류를 한 구조체가 가진다.
//
// UE와 다름:
//  - UE는 UObject 분포(UDistributionFloatConstant 등)를 에셋으로 두고, 평가는 128개 표본의 룩업 테이블(FRawDistribution)로 한다.
//    여기서는 값 타입 구조체이고 테이블 없이 직접 평가한다. 직렬화가 단순하고 에디터 쪽 dirty 갱신이 필요 없다.
//    직접 평가가 테이블보다 느린지는 측정하지 않았다. 렌더 이후 측정 항목으로 남겨 둔다.
//  - UE의 UniformCurve, ParticleParameter는 아직 없다. 같은 GetValue 시그니처로 나중에 추가할 수 있다.

enum class EDistributionKind : uint8
{
	Constant,	// 항상 Constant
	Uniform,	// [Min, Max]에서 균일 난수
	Curve,		// Keys를 시간 순으로 선형 보간. 범위 밖은 끝 키 값으로 고정
};

namespace ParticleDistributionDetail
{
	inline float Lerp(float A, float B, float T) { return A + (B - A) * T; }
	inline FVector Lerp(const FVector& A, const FVector& B, float T) { return A + (B - A) * T; }

	// Min과 Max가 같으면 난수를 쓰지 않는다 (UE: Op == RDO_None, Distributions.cpp:2038-2046)
	inline bool IsDegenerate(float Min, float Max) { return Min == Max; }
	inline bool IsDegenerate(const FVector& Min, const FVector& Max) { return Min == Max; }

	// 스트림에서 값을 뽑는다. 벡터는 X, Y, Z 순서로 난수 3개를 쓴다 (축 잠금은 아직 없다).
	inline float SampleUniform(float Min, float Max, FRandomStream& Stream)
	{
		return Min + (Max - Min) * Stream.GetFraction();
	}
	inline FVector SampleUniform(const FVector& Min, const FVector& Max, FRandomStream& Stream)
	{
		const float RX = Stream.GetFraction();
		const float RY = Stream.GetFraction();
		const float RZ = Stream.GetFraction();
		return FVector(Min.X + (Max.X - Min.X) * RX, Min.Y + (Max.Y - Min.Y) * RY, Min.Z + (Max.Z - Min.Z) * RZ);
	}
}

template <typename T>
struct TDistributionKey
{
	float Time = 0.0f;
	T Value = T();
};

// Float와 Vector가 같은 코드를 쓰도록 값 타입만 템플릿으로 뺀다.
template <typename T>
struct TParticleDistribution
{
	EDistributionKind Kind = EDistributionKind::Constant;
	T Constant = T();
	T Min = T();
	T Max = T();
	// Time 오름차순. AddKey로 넣으면 순서가 유지된다.
	TArray<TDistributionKey<T>> Keys;

	static TParticleDistribution MakeConstant(const T& InValue)
	{
		TParticleDistribution D;
		D.Kind = EDistributionKind::Constant;
		D.Constant = InValue;
		return D;
	}

	static TParticleDistribution MakeUniform(const T& InMin, const T& InMax)
	{
		TParticleDistribution D;
		D.Kind = EDistributionKind::Uniform;
		D.Min = InMin;
		D.Max = InMax;
		return D;
	}

	static TParticleDistribution MakeCurve()
	{
		TParticleDistribution D;
		D.Kind = EDistributionKind::Curve;
		return D;
	}

	// Time 순서를 지켜 키를 넣는다. 같은 Time이 있으면 값을 덮어쓴다.
	void AddKey(float InTime, const T& InValue)
	{
		int32 Pos = 0;
		const int32 Count = static_cast<int32>(Keys.Num());
		while (Pos < Count && Keys[Pos].Time < InTime) { ++Pos; }
		if (Pos < Count && Keys[Pos].Time == InTime)
		{
			Keys[Pos].Value = InValue;
			return;
		}
		TDistributionKey<T> NewKey;
		NewKey.Time = InTime;
		NewKey.Value = InValue;
		Keys.Insert(NewKey, static_cast<uint32>(Pos));
	}

	// 난수가 필요한 종류인지. Min == Max인 Uniform은 난수를 쓰지 않는다.
	bool NeedsRandom() const
	{
		return Kind == EDistributionKind::Uniform && !ParticleDistributionDetail::IsDegenerate(Min, Max);
	}

	// Time: Spawn 모듈은 이미터 시간, 수명 중 모듈은 파티클의 RelativeTime (UE와 같은 규칙)
	// Stream: Uniform에서만 쓴다. nullptr이면 Min을 돌려준다 (UE는 전역 SRand로 대신한다).
	T GetValue(float Time, FRandomStream* Stream) const
	{
		switch (Kind)
		{
		case EDistributionKind::Constant:
			return Constant;

		case EDistributionKind::Uniform:
			if (!NeedsRandom() || Stream == nullptr)
			{
				return Min;
			}
			return ParticleDistributionDetail::SampleUniform(Min, Max, *Stream);

		case EDistributionKind::Curve:
		{
			const int32 Count = static_cast<int32>(Keys.Num());
			if (Count == 0)
			{
				return T();
			}
			if (Time <= Keys[0].Time)
			{
				return Keys[0].Value;
			}
			if (Time >= Keys[Count - 1].Time)
			{
				return Keys[Count - 1].Value;
			}
			// 키 수가 적으므로 선형 탐색한다
			int32 Next = 1;
			while (Keys[Next].Time < Time) { ++Next; }
			const TDistributionKey<T>& A = Keys[Next - 1];
			const TDistributionKey<T>& B = Keys[Next];
			const float Span = B.Time - A.Time;
			const float Alpha = (Span > 0.0f) ? (Time - A.Time) / Span : 0.0f;
			return ParticleDistributionDetail::Lerp(A.Value, B.Value, Alpha);
		}
		}
		return T();
	}
};

using FFloatDistribution = TParticleDistribution<float>;
using FVectorDistribution = TParticleDistribution<FVector>;
