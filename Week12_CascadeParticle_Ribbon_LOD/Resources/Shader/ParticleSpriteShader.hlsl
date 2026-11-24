#pragma pack_matrix(row_major)

// CPU 시뮬레이션 파티클(UParticleSystemComponent)을 그리는 단순 스프라이트 셰이더.
// 정점은 CPU가 카메라 방향에 맞춰 월드 좌표로 펼쳐서 넣는다 (Particles/ParticleSpriteRender.h의 FParticleSpriteVertex와 같은 레이아웃).
cbuffer constants : register(b0)
{
    matrix VP;
};

Texture2D ParticleTexture : register(t0);
SamplerState ParticleSampler : register(s0);

struct VS_INPUT
{
    float3 position : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

PS_INPUT mainVS(VS_INPUT input)
{
    PS_INPUT output;
    output.position = mul(float4(input.position, 1.0f), VP);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

float4 mainPS(PS_INPUT input) : SV_TARGET
{
    // 텍스처 색과 알파에 파티클 색(Color.rgb, Color.a)을 곱한다.
    // 블렌드(알파/Additive)는 파이프라인 상태가 정한다.
    float4 texel = ParticleTexture.Sample(ParticleSampler, input.uv);
    float4 result = texel * input.color;
#ifdef PARTICLE_ADDITIVE
    // Additive의 블렌드 계수는 One/One이라 알파가 색에 반영되지 않는다. 알파로 색을 줄여 페이드가 보이게 한다.
    result.rgb *= result.a;
#endif
    return result;
}
