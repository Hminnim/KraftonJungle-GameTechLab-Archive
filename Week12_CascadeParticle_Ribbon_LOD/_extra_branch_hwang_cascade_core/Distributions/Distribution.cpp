#include "EnginePCH.h"
#include "Engine/Classes/Distributions/Distribution.h"
#include "Serialization/Archive.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	float GetRandomFraction(FRandomStream* InRandomStream)
	{
		if (InRandomStream != nullptr)
		{
			return InRandomStream->GetFraction();
		}

		thread_local FRandomStream FallbackRandomStream(FMath::Rand());
		return FallbackRandomStream.GetFraction();
	}

	void GetCurveScalarRange(const UCurveFloat* Curve, float& OutMin, float& OutMax)
	{
		if (!Curve)
		{
			OutMin = 0.0f;
			OutMax = 0.0f;
			return;
		}

		const TArray<FFloatCurveKey>& Keys = Curve->GetKeys();
		if (Keys.IsEmpty())
		{
			OutMin = Curve->DefaultValue;
			OutMax = Curve->DefaultValue;
			return;
		}

		OutMin = Keys[0].Value;
		OutMax = Keys[0].Value;
		for (int32 Index = 1; Index < Keys.Num(); ++Index)
		{
			OutMin = FMath::Min(OutMin, Keys[Index].Value);
			OutMax = FMath::Max(OutMax, Keys[Index].Value);
		}
	}

	void ComputeVectorScalarRange(const FVector& MinVec, const FVector& MaxVec, float& OutMin, float& OutMax)
	{
		OutMin = FMath::Min(MinVec.X, FMath::Min(MinVec.Y, MinVec.Z));
		OutMax = FMath::Max(MaxVec.X, FMath::Max(MaxVec.Y, MaxVec.Z));
	}
}

// ============================================================================
// FRawDistribution
// ============================================================================

bool FRawDistribution::Serialize(FArchive& Ar)
{
	// TODO: 2단계 구현
	return false;
}

void FRawDistribution::GetValue(float Time, float* Value, int32 NumCoords, int32 Extreme, FRandomStream* InRandomStream) const
{
	// TODO: 2단계 구현
}

void FRawDistribution::GetValue1(float Time, float* Value, int32 Extreme, FRandomStream* InRandomStream) const
{
	// TODO: 2단계 구현
}

void FRawDistribution::GetValue3(float Time, float* Value, int32 Extreme, FRandomStream* InRandomStream) const
{
	// TODO: 2단계 구현
}

void FRawDistribution::GetValue1Extreme(float Time, float* Value, int32 Extreme, FRandomStream* InRandomStream) const
{
	// TODO: 2단계 구현
}

void FRawDistribution::GetValue3Extreme(float Time, float* Value, int32 Extreme, FRandomStream* InRandomStream) const
{
	// TODO: 2단계 구현
}

void FRawDistribution::GetValue1Random(float Time, float* Value, FRandomStream* InRandomStream) const
{
	// TODO: 2단계 구현
}

void FRawDistribution::GetValue3Random(float Time, float* Value, FRandomStream* InRandomStream) const
{
	// TODO: 2단계 구현
}

// ============================================================================
// UDistribution
// ============================================================================

const float UDistribution::DefaultValue = 0.0f;

void UDistribution::GetInRange(float& MinIn, float& MaxIn) const
{
	MinIn = 0.0f;
	MaxIn = 0.0f;
}

void UDistribution::GetOutRange(float& MinOut, float& MaxOut) const
{
	MinOut = 0.0f;
	MaxOut = 0.0f;
}

// ============================================================================
// FRawDistributionFloat
// ============================================================================

bool FRawDistributionFloat::IsCreated()
{
	return Distribution != nullptr;
}

void FRawDistributionFloat::Initialize()
{
	// TODO: 2단계 LUT 베이크 연동 시 InitLookupTable() 호출
}

const FRawDistribution* FRawDistributionFloat::GetFastRawDistribution()
{
	return (!LookupTable.IsEmpty() && IsSimple()) ? this : nullptr;
}

float FRawDistributionFloat::GetValue(float F, UObject* Data, FRandomStream* InRandomStream)
{
	if (!LookupTable.IsEmpty())
	{
		float Value = 0.0f;
		GetValue1(F, &Value, 0, InRandomStream);
		return Value;
	}

	if (Distribution != nullptr)
	{
		return Distribution->GetValue(F, Data, InRandomStream);
	}

	return 0.0f;
}

void FRawDistributionFloat::GetOutRange(float& MinOut, float& MaxOut)
{
	if (!LookupTable.IsEmpty())
	{
		LookupTable.GetRange(&MinOut, &MaxOut);
		return;
	}

	if (Distribution != nullptr)
	{
		Distribution->GetOutRange(MinOut, MaxOut);
		return;
	}

	MinOut = 0.0f;
	MaxOut = 0.0f;
}

void FRawDistributionFloat::InitLookupTable()
{
	// TODO: 2단계 구현
}

// ============================================================================
// UDistributionFloat
// ============================================================================

float UDistributionFloat::GetFloatValue(float F)
{
	return GetValue(F);
}

uint32 UDistributionFloat::InitializeRawEntry(float Time, float* Values) const
{
	// TODO: 2단계 구현
	return 0;
}

float UDistributionFloat::GetValue(float F, UObject* Data, FRandomStream* InRandomStream) const
{
	return DefaultValue;
}

void UDistributionFloat::GetInRange(float& MinIn, float& MaxIn) const
{
	MinIn = 0.0f;
	MaxIn = 0.0f;
}

void UDistributionFloat::GetOutRange(float& MinOut, float& MaxOut) const
{
	MinOut = 0.0f;
	MaxOut = 0.0f;
}

// ============================================================================
// UDistributionFloatConstant
// ============================================================================

uint32 UDistributionFloatConstant::InitializeRawEntry(float Time, float* Values) const
{
	// TODO: 2단계 구현
	return 0;
}

float UDistributionFloatConstant::GetValue(float F, UObject* Data, FRandomStream* InRandomStream) const
{
	return Constant;
}

void UDistributionFloatConstant::GetInRange(float& MinIn, float& MaxIn) const
{
	MinIn = 0.0f;
	MaxIn = 0.0f;
}

void UDistributionFloatConstant::GetOutRange(float& MinOut, float& MaxOut) const
{
	MinOut = Constant;
	MaxOut = Constant;
}

// ============================================================================
// UDistributionFloatUniform
// ============================================================================

uint32 UDistributionFloatUniform::InitializeRawEntry(float Time, float* Values) const
{
	// TODO: 2단계 구현
	return 0;
}

float UDistributionFloatUniform::GetValue(float F, UObject* Data, FRandomStream* InRandomStream) const
{
	return FMath::Lerp(Min, Max, GetRandomFraction(InRandomStream));
}

void UDistributionFloatUniform::GetInRange(float& MinIn, float& MaxIn) const
{
	MinIn = 0.0f;
	MaxIn = 0.0f;
}

void UDistributionFloatUniform::GetOutRange(float& MinOut, float& MaxOut) const
{
	MinOut = FMath::Min(Min, Max);
	MaxOut = FMath::Max(Min, Max);
}

// ============================================================================
// UDistributionFloatConstantCurve
// ============================================================================

uint32 UDistributionFloatConstantCurve::InitializeRawEntry(float Time, float* Values) const
{
	// TODO: 2단계 구현
	return 0;
}

float UDistributionFloatConstantCurve::GetValue(float F, UObject* Data, FRandomStream* InRandomStream) const
{
	return ConstantCurve ? ConstantCurve->GetFloatValue(F) : DefaultValue;
}

void UDistributionFloatConstantCurve::GetInRange(float& MinIn, float& MaxIn) const
{
	if (!ConstantCurve || ConstantCurve->GetKeys().IsEmpty())
	{
		MinIn = 0.0f;
		MaxIn = 0.0f;
		return;
	}

	const TArray<FFloatCurveKey>& Keys = ConstantCurve->GetKeys();
	MinIn = Keys[0].Time;
	MaxIn = Keys.Last().Time;
}

void UDistributionFloatConstantCurve::GetOutRange(float& MinOut, float& MaxOut) const
{
	GetCurveScalarRange(ConstantCurve, MinOut, MaxOut);
}

// ============================================================================
// FRawDistributionVector
// ============================================================================

bool FRawDistributionVector::IsCreated()
{
	return Distribution != nullptr;
}

void FRawDistributionVector::Initialize()
{
	// TODO: 2단계 LUT 베이크 연동 시 InitLookupTable() 호출
}

const FRawDistribution* FRawDistributionVector::GetFastRawDistribution()
{
	return (!LookupTable.IsEmpty() && IsSimple()) ? this : nullptr;
}

FVector FRawDistributionVector::GetValue(float F, UObject* Data, FRandomStream* InRandomStream)
{
	if (!LookupTable.IsEmpty())
	{
		FVector Value = FVector::ZeroVector;
		GetValue3(F, &Value.X, 0, InRandomStream);
		return Value;
	}

	if (Distribution != nullptr)
	{
		return Distribution->GetValue(F, Data, 0, InRandomStream);
	}

	return FVector::ZeroVector;
}

void FRawDistributionVector::GetOutRange(float& MinOut, float& MaxOut)
{
	if (!LookupTable.IsEmpty())
	{
		FVector MinVec = FVector::ZeroVector;
		FVector MaxVec = FVector::ZeroVector;
		LookupTable.GetRange(&MinVec.X, &MaxVec.X);
		ComputeVectorScalarRange(MinVec, MaxVec, MinOut, MaxOut);
		return;
	}

	if (Distribution != nullptr)
	{
		Distribution->GetOutRange(MinOut, MaxOut);
		return;
	}

	MinOut = 0.0f;
	MaxOut = 0.0f;
}

void FRawDistributionVector::GetRange(FVector& MinOut, FVector& MaxOut)
{
	if (!LookupTable.IsEmpty())
	{
		LookupTable.GetRange(&MinOut.X, &MaxOut.X);
		return;
	}

	if (Distribution != nullptr)
	{
		Distribution->GetRange(MinOut, MaxOut);
		return;
	}

	MinOut = FVector::ZeroVector;
	MaxOut = FVector::ZeroVector;
}

void FRawDistributionVector::InitLookupTable()
{
	// TODO: 2단계 구현
}

// ============================================================================
// UDistributionVector
// ============================================================================

float UDistributionVector::GetFloatValue(float F)
{
	return GetValue(F).X;
}

uint32 UDistributionVector::InitializeRawEntry(float Time, float* Values) const
{
	// TODO: 2단계 구현
	return 0;
}

FVector UDistributionVector::GetValue(float F, UObject* Data, int32 LastExtreme, FRandomStream* InRandomStream) const
{
	return FVector::ZeroVector;
}

void UDistributionVector::GetInRange(float& MinIn, float& MaxIn) const
{
	MinIn = 0.0f;
	MaxIn = 0.0f;
}

void UDistributionVector::GetOutRange(float& MinOut, float& MaxOut) const
{
	FVector MinVec = FVector::ZeroVector;
	FVector MaxVec = FVector::ZeroVector;
	GetRange(MinVec, MaxVec);
	ComputeVectorScalarRange(MinVec, MaxVec, MinOut, MaxOut);
}

void UDistributionVector::GetRange(FVector& OutMin, FVector& OutMax) const
{
	OutMin = FVector::ZeroVector;
	OutMax = FVector::ZeroVector;
}

// ============================================================================
// UDistributionVectorConstant
// ============================================================================

FVector UDistributionVectorConstant::GetValue(float F, UObject* Data, int32 LastExtreme, FRandomStream* InRandomStream) const
{
	return bLockAxes ? FVector(Constant.X, Constant.X, Constant.X) : Constant;
}

void UDistributionVectorConstant::GetInRange(float& MinIn, float& MaxIn) const
{
	MinIn = 0.0f;
	MaxIn = 0.0f;
}

void UDistributionVectorConstant::GetOutRange(float& MinOut, float& MaxOut) const
{
	FVector MinVec = FVector::ZeroVector;
	FVector MaxVec = FVector::ZeroVector;
	GetRange(MinVec, MaxVec);
	ComputeVectorScalarRange(MinVec, MaxVec, MinOut, MaxOut);
}

void UDistributionVectorConstant::GetRange(FVector& OutMin, FVector& OutMax) const
{
	const FVector Value = GetValue();
	OutMin = Value;
	OutMax = Value;
}

// ============================================================================
// UDistributionVectorUniform
// ============================================================================

FVector UDistributionVectorUniform::GetValue(float F, UObject* Data, int32 LastExtreme, FRandomStream* InRandomStream) const
{
	if (bLockAxes)
	{
		const float Alpha = GetRandomFraction(InRandomStream);
		return FVector(
			FMath::Lerp(Min.X, Max.X, Alpha),
			FMath::Lerp(Min.Y, Max.Y, Alpha),
			FMath::Lerp(Min.Z, Max.Z, Alpha)
		);
	}

	return FVector(
		FMath::Lerp(Min.X, Max.X, GetRandomFraction(InRandomStream)),
		FMath::Lerp(Min.Y, Max.Y, GetRandomFraction(InRandomStream)),
		FMath::Lerp(Min.Z, Max.Z, GetRandomFraction(InRandomStream))
	);
}

void UDistributionVectorUniform::GetInRange(float& MinIn, float& MaxIn) const
{
	MinIn = 0.0f;
	MaxIn = 0.0f;
}

void UDistributionVectorUniform::GetOutRange(float& MinOut, float& MaxOut) const
{
	FVector MinVec = FVector::ZeroVector;
	FVector MaxVec = FVector::ZeroVector;
	GetRange(MinVec, MaxVec);
	ComputeVectorScalarRange(MinVec, MaxVec, MinOut, MaxOut);
}

void UDistributionVectorUniform::GetRange(FVector& OutMin, FVector& OutMax) const
{
	OutMin = FVector(
		FMath::Min(Min.X, Max.X),
		FMath::Min(Min.Y, Max.Y),
		FMath::Min(Min.Z, Max.Z)
	);
	OutMax = FVector(
		FMath::Max(Min.X, Max.X),
		FMath::Max(Min.Y, Max.Y),
		FMath::Max(Min.Z, Max.Z)
	);
}

// ============================================================================
// UDistributionVectorConstantCurve
// ============================================================================

FVector UDistributionVectorConstantCurve::GetValue(float F, UObject* Data, int32 LastExtreme, FRandomStream* InRandomStream) const
{
	const float X = ConstantCurves[0] ? ConstantCurves[0]->GetFloatValue(F) : DefaultValue;
	const float Y = ConstantCurves[1] ? ConstantCurves[1]->GetFloatValue(F) : DefaultValue;
	const float Z = ConstantCurves[2] ? ConstantCurves[2]->GetFloatValue(F) : DefaultValue;
	return FVector(X, Y, Z);
}

void UDistributionVectorConstantCurve::GetInRange(float& MinIn, float& MaxIn) const
{
	bool bHasValidCurve = false;
	float CombinedMin = 0.0f;
	float CombinedMax = 0.0f;

	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const UCurveFloat* Curve = ConstantCurves[Axis];
		if (!Curve || Curve->GetKeys().IsEmpty())
		{
			continue;
		}

		const TArray<FFloatCurveKey>& Keys = Curve->GetKeys();
		if (!bHasValidCurve)
		{
			CombinedMin = Keys[0].Time;
			CombinedMax = Keys.Last().Time;
			bHasValidCurve = true;
		}
		else
		{
			CombinedMin = FMath::Min(CombinedMin, Keys[0].Time);
			CombinedMax = FMath::Max(CombinedMax, Keys.Last().Time);
		}
	}

	MinIn = CombinedMin;
	MaxIn = CombinedMax;
}

void UDistributionVectorConstantCurve::GetOutRange(float& MinOut, float& MaxOut) const
{
	FVector MinVec = FVector::ZeroVector;
	FVector MaxVec = FVector::ZeroVector;
	GetRange(MinVec, MaxVec);
	ComputeVectorScalarRange(MinVec, MaxVec, MinOut, MaxOut);
}

void UDistributionVectorConstantCurve::GetRange(FVector& OutMin, FVector& OutMax) const
{
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		GetCurveScalarRange(ConstantCurves[Axis], OutMin[Axis], OutMax[Axis]);
	}
}
