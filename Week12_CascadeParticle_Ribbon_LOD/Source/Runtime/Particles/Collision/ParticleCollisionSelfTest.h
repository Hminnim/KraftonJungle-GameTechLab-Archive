#pragma once

#include "Core/Types.h"

// 파티클 충돌 검증 (Debug 빌드 시작 시 실행). 실패한 검사 수를 돌려준다.

// 월드 선분 쿼리
//  1) 인스턴스의 CollisionTraceOverride 경로 (월드 없이)
//  2) 실제 UWorld에 정적 메시 액터를 두고 BVH + 삼각형 법선 경로
int32 RunParticleCollisionQuerySelfTest();

// UParticleModuleCollision (월드 없이, 가짜 z = 0 평면). 반사, 감쇠, 횟수, 완료 동작, 지연, 로컬 공간, 저장, Tick 전체
int32 RunParticleCollisionModuleSelfTest();

// 도형 충돌(박스, 회전한 박스, 구, 캡슐: 지점, 법선, Time)과 메시와의 가까운 쪽 선택, 걸러내기, 모듈을 통한 끝에서 끝까지,
// 렌더 조건 옵션(카메라 거리, 가시성), Pawn 규칙, 저장
int32 RunParticleCollisionShapeSelfTest();

// 충돌 이벤트: 값, 필터 3종, 끄기, Kill이어도 보고, 상한, 컴포넌트 Tick 전체(델리게이트 호출 수, 큐 비우기), 저장
int32 RunParticleCollisionEventSelfTest();

// 에디터 연동: 모듈 메뉴 등록(리플렉션), 예제 에셋(CollisionDemo.particle, 있을 때만)
int32 RunParticleCollisionEditorSelfTest();
