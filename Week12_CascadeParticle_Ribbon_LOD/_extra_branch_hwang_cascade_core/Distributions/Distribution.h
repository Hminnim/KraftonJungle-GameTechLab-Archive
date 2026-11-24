#pragma once

#include "UObject/Object.h"
#include "Containers/Array.h"
#include "Math/EngineMath.h"
#include "Math/RandomStream.h"
#include "Curves/CurveFloat.h"
#include "Distribution.generated.h"

enum ERawDistributionOperation
{
	RDO_Uninitialized,
	RDO_None,
	RDO_Random,
	RDO_Extreme,
};

struct FDistributionLookupTable
{
	float TimeScale;
	float TimeBias;
	TArray<float> Values;
	uint8 Op;
	uint8 EntryCount;
	uint8 EntryStride;
	uint8 SubEntryStride;
	uint8 LockFlag;

	FDistributionLookupTable()
		: TimeScale(0.0f)
		, TimeBias(0.0f)
		, Op(RDO_Uninitialized)
		, EntryCount(0)
		, EntryStride(0)
		, SubEntryStride(0)
		, LockFlag(0)
	{
	}
	
	void Empty()
	{
		Op = RDO_Uninitialized;
		EntryCount = 0;
		EntryStride = 0;
		SubEntryStride = 0;
		TimeScale = 0.0f;
		TimeBias = 0.0f;
		LockFlag = 0;
	}

	inline bool IsEmpty() const
	{
		return Values.Num() == 0 || EntryCount == 0;
	}

	inline float GetValuesPerEntry() const
	{
		return (float)(EntryStride - SubEntryStride);
	}

	inline float GetValueCount() const
	{
		return (float)(Values.Num());
	}

	inline void GetEntry(float Time, const float*& Entry1, const float*& Entry2, float& LerpAlpha) const
	{
		Time -= TimeBias;
		Time *= TimeScale;
		Time = FMath::FloatSelect(Time, Time, 0.0f);

		LerpAlpha = FMath::Fractional(Time);
		
		const uint32 Index = FMath::TruncToInt(Time);
		const uint32 Index1 = FMath::Min<uint32>(Index + 0, EntryCount - 1) * EntryStride;
		const uint32 Index2 = FMath::Min<uint32>(Index + 1, EntryCount - 1) * EntryStride;
		Entry1 = &Values[Index1];
		Entry2 = &Values[Index2];
	}

	void GetRange(float* OutMinValues, float* OutMaxValues)
	{
		if(EntryCount > 0)
		{
			const int32 ValuesPerEntry = (int32)GetValuesPerEntry();
			const float* Entry = Values.GetData();

			// Initialize to the first entry in the table.
			for(int32 ValueIndex = 0; ValueIndex < ValuesPerEntry; ++ValueIndex)
			{
				OutMinValues[ValueIndex] = Entry[ValueIndex];
				OutMaxValues[ValueIndex] = Entry[ValueIndex + SubEntryStride];
			}

			// Iterate over each entry updating the minimum and maximum values.
			for(int32 EntryIndex = 1; EntryIndex < EntryCount; ++EntryIndex)
			{
				Entry += EntryStride;
				for(int32 ValueIndex = 0; ValueIndex < ValuesPerEntry; ++ValueIndex)
				{
					OutMinValues[ValueIndex] = FMath::Min(OutMinValues[ValueIndex], Entry[ValueIndex]);
					OutMaxValues[ValueIndex] = FMath::Max(OutMaxValues[ValueIndex], Entry[ValueIndex + SubEntryStride]);
				}
			}
		}
	}
};

class FArchive;

struct FRawDistribution
{
	FRawDistribution()
	{
	}

	bool Serialize(FArchive& Ar);

	void GetValue(float Time, float* Value, int32 NumCoords, int32 Extreme, struct FRandomStream* InRandomStream) const;

	void GetValue1(float Time, float* Value, int32 Extreme, struct FRandomStream* InRandomStream) const;
	void GetValue3(float Time, float* Value, int32 Extreme, struct FRandomStream* InRandomStream) const;
	inline void GetValue1None(float Time, float* InValue) const
	{
		float* Value = InValue;
		const float* Entry1;
		const float* Entry2;
		float LerpAlpha = 0.0f;
		LookupTable.GetEntry(Time, Entry1, Entry2, LerpAlpha);
		const float* NewEntry1 = Entry1;
		const float* NewEntry2 = Entry2;
		Value[0] = FMath::Lerp(NewEntry1[0], NewEntry2[0], LerpAlpha);
	}
	inline void GetValue3None(float Time, float* InValue) const
	{
		float* Value = InValue;
		const float* Entry1;
		const float* Entry2;
		float LerpAlpha = 0.0f;
		LookupTable.GetEntry(Time, Entry1, Entry2, LerpAlpha);
		const float* NewEntry1 = Entry1;
		const float* NewEntry2 = Entry2;
		float T0 = FMath::Lerp(NewEntry1[0], NewEntry2[0], LerpAlpha);
		float T1 = FMath::Lerp(NewEntry1[1], NewEntry2[1], LerpAlpha);
		float T2 = FMath::Lerp(NewEntry1[2], NewEntry2[2], LerpAlpha);
		Value[0] = T0;
		Value[1] = T1;
		Value[2] = T2;
	}
	void GetValue1Extreme(float Time, float* Value, int32 Extreme, struct FRandomStream* InRandomStream) const;
	void GetValue3Extreme(float Time, float* Value, int32 Extreme, struct FRandomStream* InRandomStream) const;
	void GetValue1Random(float Time, float* Value, struct FRandomStream* InRandomStream) const;
	void GetValue3Random(float Time, float* Value, struct FRandomStream* InRandomStream) const;

	inline bool IsSimple()
	{
		return LookupTable.Op == RDO_None;
	}

protected:
	FDistributionLookupTable LookupTable;
};

UCLASS()
class UDistribution : public UObject
{
	GENERATED_BODY()

public:
	static const float DefaultValue;
	virtual void GetInRange(float& MinIn, float& MaxIn) const;
	virtual void GetOutRange(float& MinOut, float& MaxOut) const;
};

class UDistributionFloat;

struct FRawDistributionFloat : public FRawDistribution
{
private:
	float MinValue;
	float MaxValue;

public:
	UDistributionFloat* Distribution;

	FRawDistributionFloat()
		: MinValue(0.0f)
		, MaxValue(0.0f)
		, Distribution(nullptr)
	{
	}

	bool IsCreated();
	void Initialize();

	const FRawDistribution* GetFastRawDistribution();
	float GetValue(float F = 0.0f, UObject* Data = nullptr, struct FRandomStream* InRandomStream = nullptr);
	void GetOutRange(float& MinOut, float& MaxOut);

	inline bool IsUniform() { return LookupTable.SubEntryStride != 0; }
	void InitLookupTable();
};

UCLASS()
class UDistributionFloat : public UDistribution
{
	GENERATED_BODY()

public:
	bool bCanBeBaked = true;
	bool bIsDirty = true;
	bool bBakedDataSuccesfully = false;

	virtual float GetFloatValue(float F = 0.0f);
	virtual ERawDistributionOperation GetOperation() const { return RDO_None; }
	virtual uint32 InitializeRawEntry(float Time, float* Values) const;
	virtual float GetValue(float F = 0.f, UObject* Data = nullptr, struct FRandomStream* InRandomStream = nullptr) const;
	virtual void GetInRange(float& MinIn, float& MaxIn) const override;
	virtual void GetOutRange(float& MinOut, float& MaxOut) const override;

	virtual bool CanBeBaked() const
	{
		return bCanBeBaked;
	}
	bool HasBakedSuccesfully() const
	{
		return bBakedDataSuccesfully;
	}
	int32 GetValueCount() const
	{
		return 1;
	}
};

UCLASS()
class UDistributionFloatConstant : public UDistributionFloat
{
	GENERATED_BODY()

public:
	float Constant = 0.0f;

	virtual ERawDistributionOperation GetOperation() const override { return RDO_None; }
	virtual uint32 InitializeRawEntry(float Time, float* Values) const override;
	virtual float GetValue(float F = 0.f, UObject* Data = nullptr, struct FRandomStream* InRandomStream = nullptr) const override;
	virtual void GetInRange(float& MinIn, float& MaxIn) const override;
	virtual void GetOutRange(float& MinOut, float& MaxOut) const override;
};

UCLASS()
class UDistributionFloatUniform : public UDistributionFloat
{
	GENERATED_BODY()

public:
	float Min = 0.0f;
	float Max = 0.0f;

	virtual ERawDistributionOperation GetOperation() const override { return RDO_Random; }
	virtual uint32 InitializeRawEntry(float Time, float* Values) const override;
	virtual float GetValue(float F = 0.f, UObject* Data = nullptr, struct FRandomStream* InRandomStream = nullptr) const override;
	virtual void GetInRange(float& MinIn, float& MaxIn) const override;
	virtual void GetOutRange(float& MinOut, float& MaxOut) const override;
};

UCLASS()
class UDistributionFloatConstantCurve : public UDistributionFloat
{
	GENERATED_BODY()

public:
	UCurveFloat* ConstantCurve = nullptr;

	virtual ERawDistributionOperation GetOperation() const override { return RDO_None; }
	virtual uint32 InitializeRawEntry(float Time, float* Values) const override;
	virtual float GetValue(float F = 0.f, UObject* Data = nullptr, struct FRandomStream* InRandomStream = nullptr) const override;
	virtual void GetInRange(float& MinIn, float& MaxIn) const override;
	virtual void GetOutRange(float& MinOut, float& MaxOut) const override;
};

class UDistributionVector;

struct FRawDistributionVector : public FRawDistribution 
{
private:
	float MinValue;
	float MaxValue;
	FVector MinValueVec;
	FVector MaxValueVec;

public:
	UDistributionVector* Distribution;

	bool IsCreated();
	void Initialize();

	const FRawDistribution* GetFastRawDistribution();

	FVector GetValue(float F = 0.0f, UObject* Data = nullptr, struct FRandomStream* InRandomStream = nullptr);
	void GetOutRange(float& MinOut, float& MaxOut);
	void GetRange(FVector& MinOut, FVector& MaxOut);

	inline bool IsUniform() { return LookupTable.SubEntryStride != 0; }
	void InitLookupTable();
};

UCLASS()
class UDistributionVector : public UDistribution
{
	GENERATED_BODY()

public:
	bool bCanBeBaked = true;
	bool bIsDirty = true;
	bool bBakedDataSuccesfully = false;

	virtual float GetFloatValue(float F = 0.0f);
	virtual ERawDistributionOperation GetOperation() const { return RDO_None; }
	virtual uint32 InitializeRawEntry(float Time, float* Values) const;

	virtual FVector	GetValue(float F = 0.f, UObject* Data = nullptr, int32 LastExtreme = 0, struct FRandomStream* InRandomStream = nullptr) const;
	virtual void GetInRange(float& MinIn, float& MaxIn) const override;
	virtual void GetOutRange(float& MinOut, float& MaxOut) const override;
	virtual	void GetRange(FVector& OutMin, FVector& OutMax) const;

	virtual bool CanBeBaked() const
	{
		return bCanBeBaked;
	}
	bool HasBakedSuccesfully() const
	{
		return bBakedDataSuccesfully;
	}
	int32 GetValueCount() const
	{
		return 3;
	}
};

UCLASS()
class UDistributionVectorConstant : public UDistributionVector
{
	GENERATED_BODY()

public:
	FVector Constant;

	bool bLockAxes;

	virtual FVector	GetValue(float F = 0.f, UObject* Data = nullptr, int32 LastExtreme = 0, struct FRandomStream* InRandomStream = nullptr) const override;
	virtual void GetInRange(float& MinIn, float& MaxIn) const override;
	virtual void GetOutRange(float& MinOut, float& MaxOut) const override;
	virtual	void GetRange(FVector& OutMin, FVector& OutMax) const override;
};

UCLASS()
class UDistributionVectorUniform : public UDistributionVector
{
	GENERATED_BODY()

public:
	FVector Max;
	FVector Min;

	bool bLockAxes;

	virtual FVector	GetValue(float F = 0.f, UObject* Data = nullptr, int32 LastExtreme = 0, struct FRandomStream* InRandomStream = nullptr) const override;
	virtual void GetInRange(float& MinIn, float& MaxIn) const override;
	virtual void GetOutRange(float& MinOut, float& MaxOut) const override;
	virtual	void GetRange(FVector& OutMin, FVector& OutMax) const override;
};

UCLASS()
class UDistributionVectorConstantCurve : public UDistributionVector
{
	GENERATED_BODY()

public:
	UCurveFloat* ConstantCurves[3] = { nullptr, nullptr, nullptr };

	virtual FVector	GetValue(float F = 0.f, UObject* Data = nullptr, int32 LastExtreme = 0, struct FRandomStream* InRandomStream = nullptr) const override;
	virtual void GetInRange(float& MinIn, float& MaxIn) const override;
	virtual void GetOutRange(float& MinOut, float& MaxOut) const override;
	virtual	void GetRange(FVector& OutMin, FVector& OutMax) const override;
};