#pragma once

#include "Particles/ParticleModule.h"
#include <vector>
#include "ParticleLayoutSelfTest.generated.h"

// CacheEmitterModuleInfo의 오프셋 계산을 검증하기 위한 테스트 전용 모듈들.
// 요구 바이트만 돌려주고 다른 동작은 없다. 실제 Payload를 쓰는 모듈이 생기면 이 파일은 지워도 된다.

// Payload 12바이트, InstanceData 16바이트
UCLASS()
class UParticleModuleLayoutTestA : public UParticleModule
{
	GENERATED_BODY()
public:
	uint32 RequiredBytes(UParticleModuleTypeDataBase* TypeData) override { return 12; }
	uint32 RequiredBytesPerInstance() override { return 16; }
};

// 아무것도 요구하지 않는다
UCLASS()
class UParticleModuleLayoutTestB : public UParticleModule
{
	GENERATED_BODY()
};

// Payload 8바이트, InstanceData 8바이트
UCLASS()
class UParticleModuleLayoutTestC : public UParticleModule
{
	GENERATED_BODY()
public:
	uint32 RequiredBytes(UParticleModuleTypeDataBase* TypeData) override { return 8; }
	uint32 RequiredBytesPerInstance() override { return 8; }
};

// Payload 1바이트, InstanceData 4바이트 (8의 배수로 올림되는지 확인)
UCLASS()
class UParticleModuleLayoutTestD : public UParticleModule
{
	GENERATED_BODY()
public:
	uint32 RequiredBytes(UParticleModuleTypeDataBase* TypeData) override { return 1; }
	uint32 RequiredBytesPerInstance() override { return 4; }
};

// Payload 4바이트
UCLASS()
class UParticleModuleTypeDataLayoutTest : public UParticleModuleTypeDataBase
{
	GENERATED_BODY()
public:
	uint32 RequiredBytes(UParticleModuleTypeDataBase* TypeData) override { return 4; }
};

// Tick 흐름 검증용 모듈. Spawn / Update가 불린 순서를 기록하고, 파티클의 수명과 속도를 정한다.
// Payload 8바이트: [0] = Spawn이 받은 SpawnTime, [1] = Update가 실행된 횟수
UCLASS()
class UParticleModuleTickTest : public UParticleModule
{
	GENERATED_BODY()
public:
	UParticleModuleTickTest();
	uint32 RequiredBytes(UParticleModuleTypeDataBase* TypeData) override { return 8; }
	void Spawn(const FParticleSpawnContext& Context) override;
	void Update(const FParticleUpdateContext& Context) override;
};

// 위 테스트 모듈의 설정과 기록. 모듈 객체는 에셋처럼 공유되므로 테스트마다 값을 바꿔 쓴다.
struct FTickTestProbe
{
	// 호출 기록: 'S' = Spawn, 'U' = Update (Update는 파티클이 0개여도 한 번 불린다)
	std::vector<char> Log;
	// 생성되는 파티클의 수명(초). 0 이하면 OneOverMaxLifetime = 0 (죽지 않음)
	float Lifetime = 0.0f;
	// 생성되는 파티클의 BaseVelocity
	FVector Velocity = FVector(0.0f, 0.0f, 0.0f);
	// Update가 Velocity에 곱하는 배율. Reset이 Base로 복원하는지 확인하는 데 쓴다.
	float UpdateVelocityScale = 1.0f;
};
extern FTickTestProbe GTickTestProbe;

// 실패한 검사의 수를 돌려준다. 모듈 조합별 오프셋은 로그로 남긴다.
int32 RunParticleLayoutSelfTest();

// 이미터 인스턴스의 메모리 할당(Init, Resize, Release)을 검증한다. 실패한 검사의 수를 돌려준다.
int32 RunParticleInstanceSelfTest();

// 파티클 순회·Payload 매크로(BEGIN_UPDATE_LOOP, PARTICLE_ELEMENT 등)를 검증한다. 실패한 검사의 수를 돌려준다.
int32 RunParticleMacroSelfTest();

// 이미터 Tick의 호출 순서, 생성 개수 계산(SpawnFraction), Kill, 위치 적분을 검증한다. 실패한 검사의 수를 돌려준다.
int32 RunParticleTickSelfTest();

// FRandomStream과 Distribution(Constant, Uniform, Curve)의 값과 난수 사용량을 검증한다. 실패한 검사의 수를 돌려준다.
int32 RunParticleDistributionSelfTest();

// ParticleIndices 정합 검사(ValidateParticleIndices)가 정상은 통과시키고 망가진 상태는 잡아내는지 검증한다. 실패한 검사의 수를 돌려준다.
int32 RunParticleIndexValiditySelfTest();

// 이미터 시간(EmitterTime), 루프, 지연, 완료 판정과 Required 모듈의 범위 난수를 검증한다. 실패한 검사의 수를 돌려준다.
int32 RunParticleEmitterTimeSelfTest();

// Spawn 모듈(Rate, RateScale, 버스트)과 Lifetime 모듈을 검증한다. 실패한 검사의 수를 돌려준다.
int32 RunParticleSpawnModuleSelfTest();

// Location/Velocity/Size 초기값 모듈(더하기, Base 값 동기화, 방사 속도, 난수 순서)을 검증한다. 실패한 검사의 수를 돌려준다.
int32 RunParticleInitialModulesSelfTest();

// Color 초기값 모듈(대입, 클램프 없음, 난수 순서)을 검증한다. 실패한 검사의 수를 돌려준다.
int32 RunParticleColorModuleSelfTest();

// 컴포넌트와 변환(월드/로컬 공간, 이미터 원점, 회전, 방사 방향), 시드, 생성 중단과 완료를 검증한다. 실패한 검사의 수를 돌려준다.
int32 RunParticleComponentSelfTest();

// .particle 직렬화(왕복, 깨진 파일 거절, 에셋 매니저 저장과 등록)를 검증한다. 실패한 검사의 수를 돌려준다.
int32 RunParticleAssetSelfTest();

// 스프라이트 렌더(정점 채우기, 정렬, 경계, 인덱스, 컴포넌트 제출)를 검증한다. 실패한 검사의 수를 돌려준다.
int32 RunParticleSpriteRenderSelfTest();
