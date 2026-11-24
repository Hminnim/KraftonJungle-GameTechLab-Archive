// INSTANCING: 메시 파티클처럼 한 번의 드로우로 같은 메시를 여러 변환으로 그린다.
// 오브젝트 상수의 World/NormalMatrix 대신 SV_InstanceID로 MeshInstances를 읽는다 (View/Projection은 기존 상수를 그대로 쓴다).
// 인스턴스 World는 월드 공간 값이다 (파티클 쪽에서 시뮬레이션 공간 변환을 마쳐서 넘긴다).
// C++의 FMeshParticleInstance (ParticleModuleTypeDataMesh.h)와 레이아웃이 같아야 한다 (64바이트).
#ifndef MESH_INSTANCE_HLSLI
#define MESH_INSTANCE_HLSLI

#if INSTANCING
struct FMeshInstance
{
    // Row0~2: xyz = World의 0~2행, w = 이동의 x, y, z
    float4 Row0;
    float4 Row1;
    float4 Row2;
    float4 Color;
};
StructuredBuffer<FMeshInstance> MeshInstances : register(t17);

float4x4 LoadInstanceWorld(uint InstanceID)
{
    FMeshInstance Instance = MeshInstances[InstanceID];
    return float4x4(
        float4(Instance.Row0.xyz, 0.0f),
        float4(Instance.Row1.xyz, 0.0f),
        float4(Instance.Row2.xyz, 0.0f),
        float4(Instance.Row0.w, Instance.Row1.w, Instance.Row2.w, 1.0f));
}

// 법선 변환 행렬 = World 3x3의 역전치. 여인수 행렬은 (det * 역전치)라서 det의 부호만 곱하면 방향이 맞고, 길이는 어차피 정규화된다.
float4x4 LoadInstanceNormalMatrix(uint InstanceID)
{
    FMeshInstance Instance = MeshInstances[InstanceID];
    float3 R0 = Instance.Row0.xyz;
    float3 R1 = Instance.Row1.xyz;
    float3 R2 = Instance.Row2.xyz;
    float3 C0 = cross(R1, R2);
    float3 C1 = cross(R2, R0);
    float3 C2 = cross(R0, R1);
    float Sign = dot(R0, C0) < 0.0f ? -1.0f : 1.0f;
    return float4x4(
        float4(C0 * Sign, 0.0f),
        float4(C1 * Sign, 0.0f),
        float4(C2 * Sign, 0.0f),
        float4(0.0f, 0.0f, 0.0f, 1.0f));
}
#endif

#endif
