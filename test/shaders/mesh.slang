// Smoke-test mesh + amplification shader pipeline: the amplification shader dispatches one
// mesh group per surviving payload entry, the mesh shader emits a single triangle.

struct PushConstants
{
    float4 g_tint;
    uint g_groupCount;
    uint3 g_pad;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> g_PushConstants;

struct Payload
{
    uint baseId;
};

groupshared Payload s_payload;

[shader("amplification")]
[numthreads(1, 1, 1)]
void asMain(uint3 dtid : SV_DispatchThreadID)
{
    s_payload.baseId = dtid.x;
    DispatchMesh(g_PushConstants.g_groupCount, 1, 1, s_payload);
}

struct VertexOut
{
    float4 position : SV_Position;
    float4 color : COLOR0;
};

[shader("mesh")]
[numthreads(3, 1, 1)]
[outputtopology("triangle")]
void msMain(uint tid : SV_GroupThreadID,
            uint gid : SV_GroupID,
            in payload Payload payload,
            out vertices VertexOut verts[3],
            out indices uint3 tris[1])
{
    SetMeshOutputCounts(3, 1);

    if (tid == 0)
    {
        tris[0] = uint3(0, 1, 2);
    }

    float2 offsets[3] = { float2(-0.5, -0.5), float2(0.5, -0.5), float2(0.0, 0.5) };
    float base = float(payload.baseId + gid) * 0.01;
    verts[tid].position = float4(offsets[tid] + base, 0.0, 1.0);
    verts[tid].color = g_PushConstants.g_tint;
}

[shader("fragment")]
float4 psMain(VertexOut i) : SV_Target
{
    return i.color;
}
