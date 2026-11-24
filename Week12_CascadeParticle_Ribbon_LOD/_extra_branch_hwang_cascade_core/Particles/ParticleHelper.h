#pragma once

#include "Math/Vector.h"
#include "Math/Quat.h"
#include "Math/Color.h"
#include "Core/NameTypes.h"

struct FBaseParticle;

#define DECLARE_PARTICLE(Name,Address)		\
	FBaseParticle& Name = *((FBaseParticle*) (Address));

#define DECLARE_PARTICLE_CONST(Name,Address)		\
	const FBaseParticle& Name = *((const FBaseParticle*) (Address));

#define DECLARE_PARTICLE_PTR(Name,Address)		\
	FBaseParticle* Name = (FBaseParticle*) (Address);

#define BEGIN_UPDATE_LOOP																								\
	{																													\
		int32&			ActiveParticles = Context.Owner.ActiveParticles;												\
		int32			Offset	= Context.Offset;																		\
		uint32			CurrentOffset	= Offset;																		\
		float			DeltaTime = Context.DeltaTime;																	\
		const uint8*	ParticleData	= Context.Owner.ParticleData;													\
		const uint32	ParticleStride	= Context.Owner.ParticleStride;													\
		uint16*			ParticleIndices	= Context.Owner.ParticleIndices;												\
		for(int32 i=ActiveParticles-1; i>=0; i--)																		\
		{																												\
			const int32	CurrentIndex	= ParticleIndices[i];															\
			const uint8* ParticleBase	= ParticleData + CurrentIndex * ParticleStride;									\
			FBaseParticle& Particle		= *((FBaseParticle*) ParticleBase);												\
			if ((Particle.Flags & STATE_Particle_Freeze) == 0)															\
			{																											\

#define END_UPDATE_LOOP																									\
			}																											\
			CurrentOffset				= Offset;																		\
		}																												\
	}

#define CONTINUE_UPDATE_LOOP																							\
		CurrentOffset = Offset;																							\
		continue;

#define SPAWN_INIT																										\
	const int32		ActiveParticles	= Context.Owner.ActiveParticles;													\
	const uint32	ParticleStride	= Context.Owner.ParticleStride;														\
	uint32			CurrentOffset	= Context.Offset;																	\
	FBaseParticle*	ParticleBase	= Context.ParticleBase;																\
	FBaseParticle&	Particle		= *(ParticleBase);

#define PARTICLE_ELEMENT(Type,Name)																						\
	Type& Name = *((Type*)((uint8*)ParticleBase + CurrentOffset));																\
	CurrentOffset += sizeof(Type);

#define KILL_CURRENT_PARTICLE																							\
	{																													\
		ParticleIndices[i]					= ParticleIndices[ActiveParticles-1];										\
		ParticleIndices[ActiveParticles-1]	= CurrentIndex;																\
		ActiveParticles--;																								\
	}

enum EParticleStates : int32
{
	STATE_Particle_JustSpawned			= 0x00000001,
	STATE_Particle_Freeze				= 0x00000002,
	STATE_Particle_IgnoreCollisions		= 0x00000004,
	STATE_Particle_FreezeTranslation	= 0x00000008,
	STATE_Particle_FreezeRotation		= 0x00000010,
};

struct FParticleBurst
{
	int32 Count = 0;
	int32 CountLow = -1;
	float Time = 0.0f;

	FParticleBurst() = default;

	FParticleBurst(int32 InCount, int32 InCountLow, float InTime)
		: Count(InCount)
		, CountLow(InCountLow)
		, Time(InTime)
	{
	}
};

struct alignas(16) FBaseParticle
{
	FVector OldLocation; // last frame location
	float Placeholder0;

	FVector Location;	// current frame location
	float Placeholder1;

	FVector BaseVelocity;
	float Rotation;	// radian

	FVector Velocity;	// Current velocity
	float BaseRotationRate;

	FVector BaseSize;
	float RotationRate;

	FVector Size;
	int32 Flags;

	FLinearColor Color;
	FLinearColor BaseColor;

	float RelativeTime;
	float OneOverMaxLifeTime;
};

struct FParticleDataContainer
{
	int32 MemBlockSize;
	int32 ParticleDataNumBytes;
	int32 ParticleIndicesNumShorts;
	uint8* ParticleData;
	uint16* ParticleIndices;

	FParticleDataContainer()
		: MemBlockSize(0)
		, ParticleDataNumBytes(0)
		, ParticleIndicesNumShorts(0)
		, ParticleData(nullptr)
		, ParticleIndices(nullptr)
	{
	}
	~FParticleDataContainer()
	{
		Free();
	}
	FParticleDataContainer(const FParticleDataContainer&) = delete;
	FParticleDataContainer& operator=(const FParticleDataContainer&) = delete;

	void Alloc(int32 InParticleDataNumBytes, int32 InParticleIndicesNumShorts);
	void Free();
};