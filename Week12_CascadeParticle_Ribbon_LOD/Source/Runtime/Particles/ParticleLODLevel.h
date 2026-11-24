#pragma once

#include "UObject/Object.h"
#include "ParticleLODLevel.generated.h"

class UParticleModule;
class UParticleModuleRequired;
class UParticleModuleSpawn;
class UParticleModuleTypeDataBase;

// 이미터의 모듈 한 세트 (UE: Classes/Particles/ParticleLODLevel.h)
// 이미터의 LODLevels 배열이 각 레벨(0 = 가장 상세한 레벨)을 순서대로 소유한다.
UCLASS()
class UParticleLODLevel : public UObject
{
	GENERATED_BODY()
public:
	// 소유한 모듈(Required, Spawn, TypeData, Modules)을 모두 지운다 (UParticleSystem::DeleteOwnedObjects 참고)
	void DeleteOwnedObjects();

	// 다른 LOD 레벨의 설정과 모듈 구성을 이 LOD 레벨로 깊게 복사한다 (Level 번호는 유지).
	// SpawnRateScale이 1이 아니면 복사된 SpawnModule의 RateScale에 곱한다 (LOD 재생성 시 생성량 축소용).
	void CopyFrom(const UParticleLODLevel& Source, float SpawnRateScale = 1.0f);

	UPROPERTY(VisibleAnywhere)
	int32 Level = 0;

	UPROPERTY(EditAnywhere)
	bool bEnabled = true;

	// 아래 슬롯은 UObject 소유 관계라 UPROPERTY로 선언할 수 없다(리플렉션이 TArray와 소유 포인터를 지원하지 않음).
	// UE와 다름: UE는 UPROPERTY(instanced)로 자동 저장한다.
	UParticleModuleRequired* RequiredModule = nullptr;
	// 기본 생성률을 읽는 슬롯 (UE: SpawnModule, Tick_SpawnParticles에서 Rate를 읽음)
	UParticleModuleSpawn* SpawnModule = nullptr;
	TArray<UParticleModule*> Modules;
	// nullptr이면 Sprite 이미터
	UParticleModuleTypeDataBase* TypeDataModule = nullptr;

	// Modules에서 Spawn / Update를 구현하는 모듈만 골라 같은 순서로 모은 목록 (UE: SpawnModules, UpdateModules)
	// 소유하지 않는 참조이고 UpdateModuleLists()가 다시 만든다. 꺼진 모듈(bEnabled == false)도 들어 있고 실행할 때 걸러낸다.
	TArray<UParticleModule*> SpawnModules;
	TArray<UParticleModule*> UpdateModules;

	// Modules를 훑어 SpawnModules / UpdateModules를 다시 만든다.
	// 모듈을 추가·삭제·이동한 뒤에는 UParticleEmitter::CacheEmitterModuleInfo()가 이 함수를 부른다.
	void UpdateModuleLists();

	// 모듈 객체를 만들어 알맞은 슬롯에 넣는다. 이미터 편집(모듈 추가)의 공용 경로로 쓰이며,
	// 호출한 쪽이 이후 CacheEmitterModuleInfo()와 인스턴스 재생성을 책임진다.
	// UE와 다름: UE는 TypeData도 Modules 배열에 두고 UpdateModuleLists()에서 꺼낸다. 여기서는 추가할 때 슬롯으로 보낸다.
	UParticleModule* AddModule(UClass* ModuleClass);

	// 모듈을 슬롯이나 Modules에서 빼고 지운다. Required는 이미터에 꼭 있어야 하므로 지우지 않는다.
	// 호출하기 전에 이 이미터를 쓰는 인스턴스를 없애야 하고(인스턴스가 모듈을 가리킨다),
	// 호출한 뒤 CacheEmitterModuleInfo()를 다시 불러야 한다. AddModule과 같은 규칙이다.
	bool RemoveModule(UParticleModule* Module);
};
