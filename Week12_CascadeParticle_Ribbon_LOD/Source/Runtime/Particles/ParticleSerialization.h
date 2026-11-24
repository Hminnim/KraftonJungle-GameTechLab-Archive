#pragma once

#include "Particles/ParticleDistribution.h"
#include "Core/EngineLog.h"

class UParticleSystem;

// 파티클 에셋(.particle)의 JSON 입출력 (UE: 패키지 직렬화에 해당)
//
// 왜 UPROPERTY가 아니라 직접 쓰는가: 리플렉션이 TArray와 소유 UObject 배열(이미터, LOD, 모듈)을 지원하지 않는다.
// 에셋 파일을 nlohmann json으로 직접 읽고 쓰는 방식은 .blendspace(AssetManager.cpp)와 같다.
// 모듈 하나의 설정은 UObject::Serialize(json&, bool)의 UPROPERTY 처리에 맡기고, 리플렉션 밖의 멤버(Distribution, 버스트 목록)만
// 모듈 클래스가 같은 함수를 오버라이드해 추가한다.

namespace ParticleJson
{
	// ---- 값 ----
	inline json ValueToJson(float Value) { return Value; }
	inline json ValueToJson(const FVector& Value) { return json::array({ Value.X, Value.Y, Value.Z }); }

	inline bool ValueFromJson(const json& Source, float& Out)
	{
		if (!Source.is_number()) return false;
		Out = Source.get<float>();
		return true;
	}
	inline bool ValueFromJson(const json& Source, FVector& Out)
	{
		if (!Source.is_array() || Source.size() != 3) return false;
		for (const json& Component : Source)
		{
			if (!Component.is_number()) return false;
		}
		Out = FVector(Source[0].get<float>(), Source[1].get<float>(), Source[2].get<float>());
		return true;
	}

	inline const char* KindToString(EDistributionKind Kind)
	{
		switch (Kind)
		{
		case EDistributionKind::Uniform: return "Uniform";
		case EDistributionKind::Curve: return "Curve";
		default: return "Constant";
		}
	}
	inline bool KindFromString(const FString& Name, EDistributionKind& Out)
	{
		if (Name == "Constant") { Out = EDistributionKind::Constant; return true; }
		if (Name == "Uniform") { Out = EDistributionKind::Uniform; return true; }
		if (Name == "Curve") { Out = EDistributionKind::Curve; return true; }
		return false;
	}

	// ---- Distribution ----
	// 사용하지 않는 필드(예: Constant 종류의 Min/Max)도 같이 쓴다. 종류를 바꿔도 값을 잃지 않게 하기 위해서다.
	template <typename T>
	json DistributionToJson(const TParticleDistribution<T>& Distribution)
	{
		json Result = json::object();
		Result["Kind"] = KindToString(Distribution.Kind);
		Result["Constant"] = ValueToJson(Distribution.Constant);
		Result["Min"] = ValueToJson(Distribution.Min);
		Result["Max"] = ValueToJson(Distribution.Max);
		json Keys = json::array();
		for (const TDistributionKey<T>& Key : Distribution.Keys)
		{
			Keys.push_back({ { "Time", Key.Time }, { "Value", ValueToJson(Key.Value) } });
		}
		Result["Keys"] = Keys;
		return Result;
	}

	// 형식이 틀리면 false이고 Out은 바뀌지 않는다
	template <typename T>
	bool DistributionFromJson(const json& Source, TParticleDistribution<T>& Out)
	{
		constexpr size_t MaxKeys = 4096;
		if (!Source.is_object() || !Source.contains("Kind") || !Source["Kind"].is_string()) return false;

		TParticleDistribution<T> Result;
		if (!KindFromString(Source["Kind"].get<FString>(), Result.Kind)) return false;
		if (!Source.contains("Constant") || !ValueFromJson(Source["Constant"], Result.Constant)) return false;
		if (!Source.contains("Min") || !ValueFromJson(Source["Min"], Result.Min)) return false;
		if (!Source.contains("Max") || !ValueFromJson(Source["Max"], Result.Max)) return false;
		if (!Source.contains("Keys") || !Source["Keys"].is_array() || Source["Keys"].size() > MaxKeys) return false;

		for (const json& KeyJson : Source["Keys"])
		{
			T Value = T();
			if (!KeyJson.is_object() || !KeyJson.contains("Time") || !KeyJson["Time"].is_number() ||
				!KeyJson.contains("Value") || !ValueFromJson(KeyJson["Value"], Value))
			{
				return false;
			}
			// 순서가 뒤섞이거나 같은 Time이 있어도 AddKey가 정렬하고 덮어쓴다
			Result.AddKey(KeyJson["Time"].get<float>(), Value);
		}
		Out = Result;
		return true;
	}

	// 모듈의 Serialize(json&, bool)에서 쓰는 한 줄 도우미. 저장이면 쓰고, 불러오기면 있을 때만 읽는다.
	// 읽기에 실패하면 경고를 남기고 기본값을 유지한다 (필드 하나가 깨져도 에셋 전체를 버리지 않는다).
	template <typename T>
	void SerializeDistribution(json& Handle, const char* Name, TParticleDistribution<T>& Distribution, bool bIsLoading)
	{
		if (!bIsLoading)
		{
			Handle[Name] = DistributionToJson(Distribution);
			return;
		}
		if (!Handle.contains(Name))
		{
			return;
		}
		if (!DistributionFromJson(Handle[Name], Distribution))
		{
			HTR_LOG(Warning, "[Particle] Load: distribution '{}' is malformed, keeping default", Name);
		}
	}
}

// 파티클 에셋 전체(이미터 → LOD → 모듈)의 JSON 변환
namespace ParticleAsset
{
	// 에셋 파일의 한 항목. 모듈 설정을 담는 "Data"는 UObject::Serialize(json&, bool)가 만든다.
	// 형식은 파일 하나에 { Type, Version, Emitters[ { Properties, LODLevels[ { Properties, Required, Spawn, TypeData, Modules[] } ] } ] }이다.
	json ToJson(UParticleSystem& System);

	// 새 UParticleSystem을 만들어 돌려준다 (Outer 없음, 경로 없음). 형식이 틀리면 nullptr이고 Error에 이유를 쓴다.
	// 모듈은 클래스 이름으로 복원하고(FindClass + UParticleLODLevel::AddModule) 마지막에 레이아웃 캐시(CacheEmitterModuleInfo)를 다시 만든다.
	// 실패하면 만들던 트리를 모두 지운다.
	UParticleSystem* FromJson(const json& Source, FString& Error);

	// 시스템 전체를 깊게 복사한 새 UParticleSystem (Outer 없음, 경로 없음). 저장 파일과 같은 JSON 경로를 거친다.
	// 에디터가 등록된 에셋을 직접 고치지 않고 작업 사본을 편집하거나, 저장할 사본을 만들 때 쓴다.
	UParticleSystem* Duplicate(UParticleSystem& Source, FString& Error);

	constexpr int32 FormatVersion = 1;
}
