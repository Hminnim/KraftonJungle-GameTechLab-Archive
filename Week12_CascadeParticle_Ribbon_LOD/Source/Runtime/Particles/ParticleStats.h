#pragma once

#include "Core/StatOverlay.h"

#include <algorithm>

// stat particle: 이번 프레임의 파티클 규모, 변동, 렌더 제출량, 충돌 비용.
// 시간(Simulation, Bounds, Submit)은 이미 있는 STAT_Particle* 사이클 통계를 오버레이가 같이 보여 준다.
//
// 수집 위치와 의미:
//  - 규모, 변동: 컴포넌트 TickComponent (월드에 있는 모든 파티클 컴포넌트의 합)
//  - 렌더: SubmitToRenderQueue (뷰마다 호출되므로 뷰가 여럿이면 뷰 수만큼 합쳐진다)
//  - 충돌: 충돌 모듈 Update와 컴포넌트의 이벤트 보고
struct FParticleFrameStats
{
	// 규모
	uint32 Components = 0;          // 이번 프레임에 Tick한 (이미터 인스턴스가 있는) 컴포넌트 수
	uint32 EmitterInstances = 0;
	uint64 ActiveParticles = 0;     // Tick이 끝난 뒤 살아 있는 파티클 합
	// 변동 (인스턴스의 ParticleCounter 증가분과 활성 수 변화로 계산한 값)
	uint32 Spawned = 0;
	uint32 Killed = 0;
	// 렌더
	uint32 SubmitCalls = 0;         // 그린 (컴포넌트, 뷰) 쌍
	uint32 HiddenSubmits = 0;       // Show 메뉴의 Particles가 꺼져 건너뛴 쌍
	uint32 SpritePackets = 0;
	uint32 SelfDrawnEmitters = 0;   // Beam, Mesh, Ribbon처럼 인스턴스가 스스로 그린 이미터 수
	uint64 SpriteParticles = 0;     // 스프라이트로 제출한 파티클 수
	uint64 SpriteVertices = 0;
	uint64 UploadBytes = 0;         // 스프라이트 정점 버퍼 업로드량
	// 충돌
	uint32 CollisionTraces = 0;     // 파티클 선분 쿼리 수
	uint32 CollisionHits = 0;
	uint32 CollisionShapes = 0;     // Update마다 모은 도형 수의 합
	uint32 CollisionEvents = 0;     // 컴포넌트에 보고된 이벤트
	uint32 CollisionEventsDropped = 0;   // 상한 때문에 버린 이벤트
};

class FParticleStats
{
public:
	static bool IsCollecting() { return FStatOverlay::IsEnabled(EStatFlags::Particle); }

	static FParticleFrameStats& Pending() { return PendingFrame; }
	static const FParticleFrameStats& Last() { return LastFrame; }
	// 마지막 초기화 이후 한 프레임의 최대 활성 파티클 수
	static uint64 PeakActiveParticles() { return PeakActive; }

	// 프레임 끝: 이번 프레임 값을 표시용으로 옮기고 새 프레임을 시작한다. 꺼져 있으면 비운다.
	static void EndFrame()
	{
		if (!IsCollecting())
		{
			PendingFrame = {};
			return;
		}
		PeakActive = std::max(PeakActive, PendingFrame.ActiveParticles);
		LastFrame = PendingFrame;
		PendingFrame = {};
	}

	static void ResetHistory() { PeakActive = 0; }

private:
	inline static FParticleFrameStats PendingFrame{};
	inline static FParticleFrameStats LastFrame{};
	inline static uint64 PeakActive = 0;
};
