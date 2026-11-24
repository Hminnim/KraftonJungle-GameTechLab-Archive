#pragma once

#include "Core/Types.h"
#include "Math/EngineMath.h"
#include "Math/Box.h"
#include "Math/Color.h"

struct FParticleEmitterInstance;

// CPU가 파티클마다 4개씩 채우는 정점 (UE: FParticleSpriteVertex는 파티클당 64바이트 레코드 하나이고 모서리 펼치기는 GPU가 한다)
// UE와 다름: 이 엔진은 인스턴싱이 없고 배치 경로가 동적 버텍스 버퍼 + 인덱스 드로우만 지원해서, 빌보드 펼치기를 CPU가 한다.
// 레이아웃은 Resources/Shader/ParticleSpriteShader.hlsl의 VS_INPUT과 같아야 한다 (POSITION, TEXCOORD0, COLOR0).
struct FParticleSpriteVertex
{
	FVector Position;	// 월드 좌표
	FVector2 UV;
	FLinearColor Color;
};
static_assert(sizeof(FParticleSpriteVertex) == 36, "FParticleSpriteVertex must match the shader input layout");

constexpr uint32 SpriteVerticesPerParticle = 4;
constexpr uint32 SpriteIndicesPerParticle = 6;

// 빌보드를 펼칠 카메라 기준축 (월드 공간, 단위 벡터)
struct FParticleSpriteCamera
{
	FVector Position = FVector(0.0f, 0.0f, 0.0f);
	FVector Right = FVector(0.0f, 1.0f, 0.0f);
	FVector Up = FVector(0.0f, 0.0f, 1.0f);

	// 카메라 위치와 앞 방향에서 만든다. 축 규약은 FViewContext::BuildBillboardMatrix와 같다
	// (왼손 좌표계: X 앞, Y 오른쪽, Z 위. 위쪽이 수직에 가까워 오른쪽이 정해지지 않으면 Y축을 쓴다).
	static FParticleSpriteCamera FromView(const FVector& CameraPosition, const FVector& CameraForward);
};

// 정렬에 쓰는 항목 (파티클의 ParticleIndices 위치와 카메라까지 거리 제곱)
struct FParticleSortItem
{
	float DistanceSquared = 0.0f;
	int32 Position = 0;
};

// 이미터 인스턴스의 활성 파티클을 정점 버퍼용 배열에 채우고 채운 파티클 수를 돌려준다.
//  - 크기는 Size.X를 가로세로 모두에 쓴다 (UE의 PSA_Square와 같다: 정사각형 스프라이트). 회전은 카메라 앞 방향 축 기준 라디안.
//  - 색은 Color 그대로, UV는 (0,0)이 왼쪽 위.
//  - bSortBackToFront이면 카메라에서 먼 파티클부터 채운다 (알파 블렌드용). false면 ParticleIndices 순서다.
//  - 로컬 공간 이미터는 SimulationToWorld로 월드 위치를 만든다.
//  - Capacity(파티클 수)를 넘기면 거기까지만 채운다.
// Scratch는 정렬용 임시 배열이며 호출 사이에 재사용해서 매번 할당하지 않게 한다.
int32 FillSpriteVertices(const FParticleEmitterInstance& Instance, const FParticleSpriteCamera& Camera, bool bSortBackToFront,
	FParticleSpriteVertex* OutVertices, int32 Capacity, TArray<FParticleSortItem>& Scratch);

// 파티클 Capacity개 분의 인덱스(0,1,2, 0,2,3 + 4k)를 채운다. 파티클 수가 달라도 같은 버퍼의 앞부분을 그대로 쓴다.
void FillSpriteIndices(uint32* OutIndices, int32 Capacity);

// 활성 파티클이 월드에서 차지하는 상자를 Box에 합친다 (각 파티클은 크기만큼 부풀린다). 합쳤으면 true.
// bHasAny가 false인 Box는 첫 파티클로 초기화된다.
bool ExpandSpriteWorldBounds(const FParticleEmitterInstance& Instance, FBox& InOutBox, bool& bInOutHasAny);

class UMaterial;

// 이미터를 그릴 머티리얼 (UE: Required.Material, 없으면 엔진 기본 머티리얼).
// Required.Material이 파티클 PSO(ParticleSprite_*)면 그것을, 비어 있거나 다른 정점 형식용(메시 머티리얼 등)이면 DefaultMaterialKey 에셋을 돌려준다.
// DefaultMaterialKey는 Required.Material이 비어 있을 때 쓸 머티리얼이다 (보통 ResourcePaths::ParticleSpriteMaterial).
UMaterial* GetParticleRenderMaterial(const FParticleEmitterInstance& Instance, const char* DefaultMaterialKey);

// FParticleSpriteVertex를 그릴 수 있는 파티클 머티리얼인지 (ParticleSprite_AlphaBlend / ParticleSprite_Additive PSO)
bool IsParticleSpriteMaterial(const UMaterial* Material);

// 알파 블렌드라 먼 것부터 그려야 하는 머티리얼인지 (Additive는 순서와 무관하다)
bool NeedsBackToFrontSort(const UMaterial* Material);
