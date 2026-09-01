// Smoke-test shader exercising the graphics and compute paths of the Slang compiler

struct PushConstants
{
    float4 g_color;
    float2 g_uvScale;
    uint2 g_pad;
};

// Bound by name
[[vk::push_constant]]
ConstantBuffer<PushConstants> g_PushConstants;

Texture2D<float4> t : register(t0);
SamplerState s : register(s0);
RWStructuredBuffer<float4> outBuffer : register(u0);

struct VSOut
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VSOut vsMain(uint id : SV_VertexID)
{
    VSOut o;
    float2 p = float2((id << 1) & 2, id & 2);
    o.uv = p * g_PushConstants.g_uvScale;
    o.position = float4(p * 2.0f - 1.0f, 0.0f, 1.0f);
    return o;
}

[shader("fragment")]
float4 psMain(VSOut i) : SV_Target
{
    return t.Sample(s, i.uv) * g_PushConstants.g_color;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void csMain(uint3 tid : SV_DispatchThreadID)
{
    outBuffer[tid.x] = g_PushConstants.g_color;
}
