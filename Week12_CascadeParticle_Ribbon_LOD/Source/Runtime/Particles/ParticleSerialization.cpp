#include "EnginePCH.h"
#include "Particles/ParticleSerialization.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/ParticleModule.h"

namespace
{
	// 악의적이거나 깨진 파일이 메모리를 끝없이 쓰지 않도록 하는 상한
	constexpr size_t MaxEmitters = 64;
	constexpr size_t MaxLODLevels = 8;
	constexpr size_t MaxModules = 256;

	json ModuleToJson(UParticleModule* Module)
	{
		if (!Module)
		{
			return nullptr;
		}
		json Data = json::object();
		Module->Serialize(Data, false);
		return { { "Class", Module->GetClass()->Name }, { "Data", Data } };
	}

	// 슬롯 모듈(Required, Spawn, TypeData)과 일반 모듈의 공통 읽기. RequiredBase로 슬롯 종류를 검사한다 (nullptr이면 일반 모듈).
	// 항목이 null이면 nullptr을 돌려주고 OutOk는 true다 (그 슬롯이 비어 있다는 뜻).
	UParticleModule* ModuleFromJson(UParticleLODLevel& LOD, const json& Source, const UClass* SlotBase, bool& bOutOk, FString& Error)
	{
		bOutOk = true;
		if (Source.is_null())
		{
			return nullptr;
		}
		bOutOk = false;
		if (!Source.is_object() || !Source.contains("Class") || !Source["Class"].is_string() ||
			!Source.contains("Data") || !Source["Data"].is_object())
		{
			Error = "module entry is malformed";
			return nullptr;
		}
		const FString ClassName = Source["Class"].get<FString>();
		UClass* Class = FindClass(ClassName);
		if (!Class || !Class->IsChildOf(UParticleModule::StaticClass()))
		{
			Error = "unknown module class '" + ClassName + "'";
			return nullptr;
		}

		// 슬롯에 들어갈 모듈은 그 슬롯의 기반 클래스여야 하고, 일반 모듈 목록에는 슬롯 전용 클래스가 들어갈 수 없다
		const bool bIsSlotClass = Class->IsChildOf(UParticleModuleRequired::StaticClass()) ||
			Class->IsChildOf(UParticleModuleSpawn::StaticClass()) ||
			Class->IsChildOf(UParticleModuleTypeDataBase::StaticClass());
		if ((SlotBase && !Class->IsChildOf(SlotBase)) || (!SlotBase && bIsSlotClass))
		{
			Error = "module class '" + ClassName + "' is in the wrong place";
			return nullptr;
		}

		UParticleModule* Module = LOD.AddModule(Class);
		if (!Module)
		{
			Error = "cannot add module '" + ClassName + "' (abstract class or slot already used)";
			return nullptr;
		}
		json Data = Source["Data"];
		Module->Serialize(Data, true);
		bOutOk = true;
		return Module;
	}
}

namespace ParticleAsset
{
	json ToJson(UParticleSystem& System)
	{
		json Root = json::object();
		Root["Type"] = "ParticleSystem";
		Root["Version"] = FormatVersion;

		System.SyncLODDistances();
		json SystemData = json::object();
		System.Serialize(SystemData, false);
		Root["Properties"] = SystemData;

		json Emitters = json::array();
		for (UParticleEmitter* Emitter : System.Emitters)
		{
			if (!Emitter)
			{
				continue;
			}
			json EmitterJson = json::object();
			json EmitterData = json::object();
			Emitter->Serialize(EmitterData, false);
			EmitterJson["Properties"] = EmitterData;

			json LODs = json::array();
			for (UParticleLODLevel* LOD : Emitter->LODLevels)
			{
				if (!LOD)
				{
					continue;
				}
				json LODJson = json::object();
				json LODData = json::object();
				LOD->Serialize(LODData, false);
				LODJson["Properties"] = LODData;
				LODJson["Required"] = ModuleToJson(LOD->RequiredModule);
				LODJson["Spawn"] = ModuleToJson(LOD->SpawnModule);
				LODJson["TypeData"] = ModuleToJson(LOD->TypeDataModule);
				json Modules = json::array();
				for (UParticleModule* Module : LOD->Modules)
				{
					if (Module)
					{
						Modules.push_back(ModuleToJson(Module));
					}
				}
				LODJson["Modules"] = Modules;
				LODs.push_back(LODJson);
			}
			EmitterJson["LODLevels"] = LODs;
			Emitters.push_back(EmitterJson);
		}
		Root["Emitters"] = Emitters;
		return Root;
	}

	UParticleSystem* FromJson(const json& Source, FString& Error)
	{
		if (!Source.is_object() || !Source.contains("Type") || !Source["Type"].is_string() ||
			Source["Type"].get<FString>() != "ParticleSystem")
		{
			Error = "not a ParticleSystem file";
			return nullptr;
		}
		if (!Source.contains("Version") || !Source["Version"].is_number_integer() || Source["Version"].get<int>() != FormatVersion)
		{
			Error = "unsupported ParticleSystem version";
			return nullptr;
		}
		if (!Source.contains("Emitters") || !Source["Emitters"].is_array() || Source["Emitters"].size() > MaxEmitters)
		{
			Error = "invalid Emitters";
			return nullptr;
		}

		// 만들던 트리는 실패 시 시스템의 소멸자가 통째로 지운다
		UParticleSystem* System = NewObject<UParticleSystem>();
		if (Source.contains("Properties") && Source["Properties"].is_object())
		{
			json SystemData = Source["Properties"];
			System->Serialize(SystemData, true);
		}
		for (const json& EmitterJson : Source["Emitters"])
		{
			if (!EmitterJson.is_object() || !EmitterJson.contains("Properties") || !EmitterJson["Properties"].is_object() ||
				!EmitterJson.contains("LODLevels") || !EmitterJson["LODLevels"].is_array() ||
				EmitterJson["LODLevels"].empty() || EmitterJson["LODLevels"].size() > MaxLODLevels)
			{
				Error = "emitter entry is malformed";
				DestroyParticleSystem(System);
				return nullptr;
			}
			UParticleEmitter* Emitter = System->AddEmitter();
			json EmitterData = EmitterJson["Properties"];
			Emitter->Serialize(EmitterData, true);

			for (const json& LODJson : EmitterJson["LODLevels"])
			{
				if (!LODJson.is_object() || !LODJson.contains("Properties") || !LODJson["Properties"].is_object() ||
					!LODJson.contains("Required") || !LODJson.contains("Spawn") || !LODJson.contains("TypeData") ||
					!LODJson.contains("Modules") || !LODJson["Modules"].is_array() || LODJson["Modules"].size() > MaxModules)
				{
					Error = "LOD entry is malformed";
					DestroyParticleSystem(System);
					return nullptr;
				}
				UParticleLODLevel* LOD = Emitter->AddLODLevel();
				const int32 LevelIndex = LOD->Level;
				json LODData = LODJson["Properties"];
				LOD->Serialize(LODData, true);
				LOD->Level = LevelIndex;	// 파일에 적힌 값이 아니라 배열 위치가 LOD 번호다

				bool bOk = true;
				ModuleFromJson(*LOD, LODJson["Required"], UParticleModuleRequired::StaticClass(), bOk, Error);
				if (bOk) ModuleFromJson(*LOD, LODJson["Spawn"], UParticleModuleSpawn::StaticClass(), bOk, Error);
				if (bOk) ModuleFromJson(*LOD, LODJson["TypeData"], UParticleModuleTypeDataBase::StaticClass(), bOk, Error);
				if (bOk)
				{
					for (const json& ModuleJson : LODJson["Modules"])
					{
						// null 항목은 형식 오류다 (일반 모듈 목록에는 빈 칸이 없다)
						if (ModuleJson.is_null())
						{
							Error = "module list contains null";
							bOk = false;
							break;
						}
						ModuleFromJson(*LOD, ModuleJson, nullptr, bOk, Error);
						if (!bOk) break;
					}
				}
				if (!bOk)
				{
					DestroyParticleSystem(System);
					return nullptr;
				}
			}
			// 모듈 목록이 완성된 뒤에 목록과 메모리 레이아웃을 한 번에 만든다
			Emitter->CacheEmitterModuleInfo();
		}
		System->SyncLODDistances();
		return System;
	}

	UParticleSystem* Duplicate(UParticleSystem& Source, FString& Error)
	{
		return FromJson(ToJson(Source), Error);
	}
}
