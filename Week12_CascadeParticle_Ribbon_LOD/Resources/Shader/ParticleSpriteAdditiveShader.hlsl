// Additive 블렌드(원본 색 + 대상 색)는 알파를 쓰지 않으므로 셰이더가 색에 알파를 미리 곱한다.
// 알파 블렌드용 ParticleSpriteShader.hlsl과 같은 코드에 정의 하나만 더한 변형이다.
#define PARTICLE_ADDITIVE 1
#include "ParticleSpriteShader.hlsl"
