
RaytracingAccelerationStructure scene : register(t0);
RWTexture2D<float4> img : register(u0);

struct Payload
{
    float3 color;
};

[shader("raygeneration")]
void rgen()
{
    uint2 px = DispatchRaysIndex().xy;
    uint2 dim = DispatchRaysDimensions().xy;

    RayDesc ray;
    ray.Origin = float3(0, 0, 0);
    ray.Direction = float3((float2(px) / float2(dim)) * 2.0f - 1.0f, 1.0f);
    ray.TMin = 0.001f;
    ray.TMax = 1000.0f;

    Payload payload;
    payload.color = float3(0, 0, 0);
    TraceRay(scene, RAY_FLAG_NONE, 0xFF, 0, 1, 0, ray, payload);

    img[px] = float4(payload.color, 1.0f);
}

[shader("miss")]
void miss(inout Payload payload)
{
    payload.color = float3(0.1f, 0.2f, 0.4f);
}

[shader("closesthit")]
void chit(inout Payload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    payload.color = float3(attr.barycentrics, 1.0f - attr.barycentrics.x - attr.barycentrics.y);
}
