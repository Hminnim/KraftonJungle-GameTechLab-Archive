#pragma once

#include "Core/Types.h"
#include "Math/EngineMath.h"
#include "Core/HAL/UnrealMemory.h"
#include <type_traits>

// Value를 Alignment의 배수로 올린다. Alignment는 2의 거듭제곱이어야 한다.
constexpr uint32 AlignUp(uint32 Value, uint32 Alignment)
{
	//Alignment = 16일 때
	// Alignment - 1 = 15 = 0000 1111
	// ~15 = 1111 0000 (하위 4비트를 지우는 마스크)
	// & ~15 는 하위 4비트를 0으로 한다 -> 16의 배수로 내림
	// 15를 먼저 더하고 16의 배수로 내리게 되니 항상 16의 배수를 유지하게 된다.
	return (Value + Alignment - 1) & ~(Alignment - 1);
}

// 파티클 상태 비트 (UE: ParticleHelper.h의 EParticleStates, 값도 같다)
// FBaseParticle::Flags의 상위 7비트가 상태이고 하위 25비트는 생성 순번(CounterMask)이다.
enum EParticleStates : uint32
{
	// 이번 프레임에 생성된 파티클
	STATE_Particle_JustSpawned = 0x02000000,
	// 갱신을 건너뛴다
	STATE_Particle_Freeze = 0x04000000,
	// 충돌 갱신을 건너뛴다
	STATE_Particle_IgnoreCollisions = 0x08000000,
	STATE_Particle_FreezeTranslation = 0x10000000,
	STATE_Particle_FreezeRotation = 0x20000000,
	// 위 '무시' 플래그를 한 번에 검사하기 위한 조합
	STATE_Particle_CollisionIgnoreCheck = STATE_Particle_Freeze | STATE_Particle_IgnoreCollisions | STATE_Particle_FreezeTranslation | STATE_Particle_FreezeRotation,
	STATE_Particle_DelayCollisions = 0x40000000,
	STATE_Particle_CollisionHasOccurred = 0x80000000,

	STATE_Mask = 0xFE000000,
	STATE_CounterMask = ~STATE_Mask,
};

// 모든 파티클이 항상 가지는 고정 영역 (UE: FBaseParticle)
// 파티클 메모리 = FBaseParticle + 모듈이 요구한 Payload 이므로, 이 구조체의 크기가 파티클 한 개의 최소 크기다.
//
// UE와 다름: UE는 위치가 double(FVector 24바이트)이라 160바이트이고 Placeholder 필드가 있다.
// 여기서는 FVector가 float 3개(12바이트)이므로 16바이트 한 줄에 'FVector + float' 한 쌍씩 넣어
// 모든 필드가 줄 경계를 넘지 않고 패딩 없이 128바이트가 된다.
//
// 'Base' 필드는 프레임 시작에 현재 값을 되돌릴 기준값이다. Update 모듈이 매 프레임
// '기준값 × 배율'로 현재 값을 다시 계산할 수 있어서, 모듈 실행 순서가 바뀌어도 값이 누적되지 않는다.
//
// 정렬: UE처럼 alignas를 두지 않는다. 구조체 자체는 4바이트 정렬이다.
// 대신 파티클 메모리의 시작 주소와 슬롯 간격(Stride)을 16의 배수로 맞춰서 모든 슬롯이 16바이트 경계에서 시작한다
// (FMemory 할당 정렬 + UParticleEmitter::ParticleStride). SIMD로 읽을 때는 비정렬 로드(Load)를 쓰면 정렬에 의존하지 않는다.
struct FBaseParticle
{
	// 줄 0 (offset 0)
	FVector Location;                 // 현재 위치
	// 나이를 수명으로 나눈 비율. 0 = 방금 생성, 1 = 수명 끝, 1 초과 = 사망
	// 초 단위 나이가 아니라 0~1 비율이라서, 색 변화 같은 over-life 모듈이 수명을 몰라도 그대로 곡선에 넣을 수 있다.
	// 갱신: 매 프레임 RelativeTime += OneOverMaxLifetime * DeltaTime. 1을 넘으면 다음 Kill 단계에서 제거된다.
	// 초 단위 나이가 필요하면 RelativeTime / OneOverMaxLifetime (= RelativeTime * 수명)으로 구한다.
	float RelativeTime;

	// 줄 1 (offset 16)
	FVector OldLocation;              // 이전 프레임 위치 (충돌과 보간에 사용)
	// 수명(초)의 역수 = 1 / MaxLifetime. 파티클마다 수명이 달라도 이 값 하나만 저장하면 되고,
	// 나이 갱신이 나눗셈 없이 곱셈 하나가 된다. 수명 0을 나누는 일은 생성할 때 한 번만 처리하면 된다.
	// 예) 수명 2초 → 0.5. 프레임이 0.1초면 RelativeTime이 0.05씩 늘어 2초 뒤 1이 된다.
	// 0이면 RelativeTime이 늘지 않아 파티클이 죽지 않는다. 수명 0 이하를 어떻게 다룰지는 수명을 정하는 모듈이 정한다.
	float OneOverMaxLifetime;

	// 줄 2 (offset 32)
	FVector Velocity;                 // 현재 속도. 프레임 시작에 BaseVelocity로 복원
	float Rotation;                   // 라디안

	// 줄 3 (offset 48)
	FVector BaseVelocity;
	float RotationRate;               // 현재 회전 속도. 프레임 시작에 BaseRotationRate로 복원

	// 줄 4 (offset 64)
	FVector Size;                     // 현재 크기. 프레임 시작에 BaseSize로 복원
	uint32 Flags;                     // 상위 비트 = EParticleStates, 하위 비트 = 생성 순번

	// 줄 5 (offset 80)
	FVector BaseSize;
	float BaseRotationRate;

	// 줄 6, 7 (offset 96, 112)
	FLinearColor Color;               // 현재 색
	FLinearColor BaseColor;           // 기준 색. FLinearColor인 이유: Additive에서 1.0을 넘는 값을 쓰기 위해
};

// 레이아웃을 바꾸면 모듈의 Payload 오프셋과 렌더 정점 채우기가 모두 영향을 받으므로 컴파일 시점에 고정한다.
static_assert(sizeof(FBaseParticle) == 128, "FBaseParticle must stay 128 bytes");
static_assert(sizeof(FBaseParticle) % 16 == 0, "FBaseParticle size must be a multiple of 16");
// 모든 필드가 4바이트 정렬이라 오프셋과 크기에 패딩이 끼지 않는다. 슬롯 정렬은 구조체가 아니라 Stride와 할당이 맡는다.
static_assert(alignof(FBaseParticle) <= 4, "FBaseParticle must not require more than 4-byte alignment");

static_assert(offsetof(FBaseParticle, Location) == 0);
static_assert(offsetof(FBaseParticle, RelativeTime) == 12);
static_assert(offsetof(FBaseParticle, OldLocation) == 16);
static_assert(offsetof(FBaseParticle, OneOverMaxLifetime) == 28);
static_assert(offsetof(FBaseParticle, Velocity) == 32);
static_assert(offsetof(FBaseParticle, Rotation) == 44);
static_assert(offsetof(FBaseParticle, BaseVelocity) == 48);
static_assert(offsetof(FBaseParticle, RotationRate) == 60);
static_assert(offsetof(FBaseParticle, Size) == 64);
static_assert(offsetof(FBaseParticle, Flags) == 76);
static_assert(offsetof(FBaseParticle, BaseSize) == 80);
static_assert(offsetof(FBaseParticle, BaseRotationRate) == 92);
static_assert(offsetof(FBaseParticle, Color) == 96);
static_assert(offsetof(FBaseParticle, BaseColor) == 112);

// 파티클 메모리는 memset으로 지우고 memcpy로 복사하므로 그렇게 다뤄도 안전한 타입이어야 한다.
static_assert(std::is_trivially_copyable_v<FBaseParticle>, "FBaseParticle is memcpy'd into snapshots");
static_assert(std::is_standard_layout_v<FBaseParticle>, "FBaseParticle is addressed by byte offsets");

// ---------------------------------------------------------------------------
// 모듈이 파티클 메모리를 순회하고 Payload를 읽을 때 쓰는 매크로 (UE: ParticleHelper.h의 같은 이름 매크로)
// ---------------------------------------------------------------------------

struct FParticleEmitterInstance;

// 모듈의 Update가 받는 값. 매크로가 이 구조체의 멤버 이름(Owner, Offset, DeltaTime)을 그대로 쓴다.
// UE의 FParticleEmitterInstance::Tick 쪽 FUpdateContext에 해당하며 이름과 구성은 우리가 정했다.
struct FParticleUpdateContext
{
	FParticleEmitterInstance& Owner;
	// 이 모듈의 Payload가 파티클 안에서 시작하는 위치 (UParticleEmitter::GetModuleDataOffset)
	int32 Offset;
	float DeltaTime;
};

// 모듈의 Spawn이 받는 값. 새 파티클 하나를 가리킨다.
struct FParticleSpawnContext
{
	FParticleEmitterInstance& Owner;
	int32 Offset;
	// 이 파티클이 생성된 시점부터 이번 프레임 끝까지의 시간(초). 프레임 중간에 생성된 파티클의 이동량 보정에 쓴다.
	float SpawnTime;
	// 방금 확보한 파티클 슬롯의 시작 주소
	FBaseParticle* ParticleBase;
};

// 주소를 FBaseParticle로 해석한다. 주소는 슬롯의 시작이어야 한다.
#define DECLARE_PARTICLE(Name, Address)			FBaseParticle& Name = *((FBaseParticle*)(Address));
#define DECLARE_PARTICLE_CONST(Name, Address)	const FBaseParticle& Name = *((const FBaseParticle*)(Address));
#define DECLARE_PARTICLE_PTR(Name, Address)		FBaseParticle* Name = (FBaseParticle*)(Address);

// 캐시 줄 크기 (UE: PLATFORM_CACHE_LINE_SIZE)
constexpr uint32 ParticleCacheLineSize = 64;

// 역순 순회에서 미리 올릴 거리. 위치 i를 처리할 때 i - ParticlePrefetchDistance 번째 파티클을 올린다.
// 측정값(Release, i7-14700, 파티클 슬롯 160바이트): 8이 가장 안정적이었다. 16, 32는 이득이 늘지 않고 일부 조건에서 손해다.
constexpr int32 ParticlePrefetchDistance = 8;

// 1이면 BEGIN_UPDATE_LOOP가 다음에 방문할 파티클을 미리 캐시에 올린다. 0이면 하지 않는다 (UE: _PARTICLES_USE_PREFETCH_).
#ifndef PARTICLE_USE_PREFETCH
#define PARTICLE_USE_PREFETCH 1
#endif

// 역순 순회에서 위치 i를 처리할 때 미리 올릴 위치. 아직 방문하지 않은 i - Distance이고, 범위를 벗어나면 INDEX_NONE이다.
// ParticleIndices[-1]을 읽지 않게 하는 가드를 겸한다.
// UE와 다름: UE는 역순 루프에서도 ParticleIndices[i+1](직전에 방문한 파티클)을 올려서 다음 파티클을 올리지 못한다.
inline int32 GetParticlePrefetchIndex(int32 i, int32 Distance)
{
	return (i - Distance >= 0) ? (i - Distance) : INDEX_NONE;
}

// 슬롯의 시작 줄과 그 다음 줄을 올린다 (FBaseParticle 앞쪽 128바이트가 대부분 여기에 걸린다).
// 슬롯 전체의 줄을 올리거나 마지막 바이트까지 올리는 방식도 측정했으나 가벼운 모듈에서 손해라서 쓰지 않는다.
inline void PrefetchParticleSlot(const uint8* Slot)
{
	FMemory::Prefetch(Slot);
	FMemory::Prefetch(Slot + ParticleCacheLineSize);
}

#if PARTICLE_USE_PREFETCH
// 루프 전: 처음 방문할 ParticlePrefetchDistance개 파티클을 올린다 (Active-1, Active-2, ...)
#define PARTICLE_PREFETCH_BEGIN																			\
		for (int32 w = ActiveParticles - 1; w >= 0 && w >= ActiveParticles - ParticlePrefetchDistance; --w)	\
		{																								\
			PrefetchParticleSlot(ParticleData + ParticleIndices[w] * ParticleStride);					\
		}
// 루프 안: 다음에 방문할 파티클이 아니라 거리만큼 앞선 파티클을 올린다
#define PARTICLE_PREFETCH_STEP																			\
			{																							\
				const int32 PrefetchTarget = GetParticlePrefetchIndex(i, ParticlePrefetchDistance);		\
				if (PrefetchTarget != INDEX_NONE)														\
				{																						\
					PrefetchParticleSlot(ParticleData + ParticleIndices[PrefetchTarget] * ParticleStride);	\
				}																						\
			}
#else
#define PARTICLE_PREFETCH_BEGIN
#define PARTICLE_PREFETCH_STEP
#endif

// Update 모듈의 순회 틀. Context(FParticleUpdateContext)가 같은 이름으로 있어야 한다.
// 살아 있는 파티클을 뒤에서 앞으로 돈다 (도중에 KILL_CURRENT_PARTICLE로 현재 칸이 바뀌어도 아직 안 본 칸이 영향받지 않는다).
// 블록 안에서 쓸 수 있는 이름: ActiveParticles(참조), DeltaTime, i, CurrentIndex, ParticleBase, Particle, CurrentOffset, ParticleIndices
// Freeze 상태인 파티클은 본문을 건너뛴다.
#define BEGIN_UPDATE_LOOP																				\
	{																									\
		int32&			ActiveParticles	= Context.Owner.ActiveParticles;								\
		int32			Offset			= Context.Offset;												\
		uint32			CurrentOffset	= Offset;														\
		float			DeltaTime		= Context.DeltaTime;											\
		const uint8*	ParticleData	= Context.Owner.ParticleData;									\
		const uint32	ParticleStride	= Context.Owner.ParticleStride;									\
		uint16*			ParticleIndices	= Context.Owner.ParticleIndices;								\
		PARTICLE_PREFETCH_BEGIN																			\
		for (int32 i = ActiveParticles - 1; i >= 0; i--)												\
		{																								\
			const int32	CurrentIndex	= ParticleIndices[i];											\
			const uint8* ParticleBase	= ParticleData + CurrentIndex * ParticleStride;					\
			PARTICLE_PREFETCH_STEP																		\
			FBaseParticle& Particle		= *((FBaseParticle*)ParticleBase);								\
			if ((Particle.Flags & STATE_Particle_Freeze) == 0)											\
			{

#define END_UPDATE_LOOP																					\
			}																							\
			CurrentOffset = Offset;																		\
		}																								\
	}

// 본문 중간에서 다음 파티클로 넘어간다. continue만 쓰면 CurrentOffset이 되돌아가지 않아 다음 파티클의 Payload 위치가 어긋난다.
#define CONTINUE_UPDATE_LOOP																			\
	CurrentOffset = Offset;																				\
	continue;

// Spawn 모듈의 시작. Context(FParticleSpawnContext)가 같은 이름으로 있어야 한다.
#define SPAWN_INIT																						\
	const int32		ActiveParticles	= Context.Owner.ActiveParticles;									\
	const uint32	ParticleStride	= Context.Owner.ParticleStride;										\
	uint32			CurrentOffset	= Context.Offset;													\
	FBaseParticle*	ParticleBase	= Context.ParticleBase;												\
	FBaseParticle&	Particle		= *(ParticleBase);

// 파티클 안의 Payload 한 칸을 Name으로 받고 CurrentOffset을 그 크기만큼 옮긴다. 모듈이 RequiredBytes에서 요구한 순서와 같아야 한다.
// UE와 다름: Payload 오프셋은 8의 배수이므로(ParticleEmitter.cpp의 SanitizeRequiredBytes) 정렬이 8을 넘는 타입은 컴파일 오류로 막는다.
#define PARTICLE_ELEMENT(Type, Name)																	\
	static_assert(alignof(Type) <= 8, "Payload type must not require more than 8-byte alignment");		\
	Type& Name = *((Type*)((uint8*)ParticleBase + CurrentOffset));										\
	CurrentOffset += sizeof(Type);

// 현재 파티클(Index i)을 죽인다. 살아 있는 구간의 마지막 칸 번호와 자리를 바꿔 뒤로 보내고 ActiveParticles를 줄인다.
// 슬롯 자체는 이동하지 않고 번호만 옮긴다. BEGIN_UPDATE_LOOP 안에서만 쓴다.
#define KILL_CURRENT_PARTICLE																			\
	{																									\
		ParticleIndices[i]					= ParticleIndices[ActiveParticles - 1];						\
		ParticleIndices[ActiveParticles - 1]	= CurrentIndex;											\
		ActiveParticles--;																				\
	}