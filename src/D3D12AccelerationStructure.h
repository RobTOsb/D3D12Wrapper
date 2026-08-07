#pragma once

#include "pch.h"

class D3D12Device;
class D3D12CommandList;
class D3D12Resource;

struct BLASGeometryDesc
{
	D3D12_GPU_VIRTUAL_ADDRESS vertexBufferVA = 0;
	uint32_t vertexCount = 0;
	uint32_t vertexStride = 0; // typically sizeof(InterleavedVertex) - only the leading float3 position is read
	D3D12_GPU_VIRTUAL_ADDRESS indexBufferVA = 0;
	uint32_t indexCount = 0;
	DXGI_FORMAT indexFormat = DXGI_FORMAT_R32_UINT;
	bool isOpaque = true;
};

// One BLAS per mesh. Rebuild-only (no refit/update) - appropriate for a static scene.
class D3D12BottomLevelAS
{
public:
	D3D12BottomLevelAS(D3D12Device *device, const BLASGeometryDesc &geometry);
	~D3D12BottomLevelAS();

	// Emits the actual BuildRaytracingAccelerationStructure call. Caller must have already
	// transitioned/barriered the source vertex/index buffers to a readable state.
	void Build(D3D12CommandList *cmdList);

	D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const;

private:
	D3D12Device *device_ = nullptr;
	std::unique_ptr<D3D12Resource> resultBuffer_;
	std::unique_ptr<D3D12Resource> scratchBuffer_;
	D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc_ = {};
};

struct TLASInstanceDesc
{
	D3D12_GPU_VIRTUAL_ADDRESS blasAddress = 0;
	float transform[3][4] = {}; // row-major 3x4, matches D3D12_RAYTRACING_INSTANCE_DESC::Transform
	uint32_t instanceID = 0; // becomes InstanceID() in HLSL
	uint32_t instanceMask = 0xFF;
	uint32_t hitGroupIndex = 0; // becomes InstanceContributionToHitGroupIndex
};

// One TLAS for the whole scene. Rebuild-only (no refit/update).
class D3D12TopLevelAS
{
public:
	D3D12TopLevelAS(D3D12Device *device, std::span<const TLASInstanceDesc> instances);
	~D3D12TopLevelAS();

	void Build(D3D12CommandList *cmdList); // uploads the instance buffer and issues the build

	D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const;
	D3D12Resource *GetResource() const
	{
		return resultBuffer_.get();
	}

private:
	D3D12Device *device_ = nullptr;
	std::unique_ptr<D3D12Resource> resultBuffer_;
	std::unique_ptr<D3D12Resource> scratchBuffer_;
	std::unique_ptr<D3D12Resource> instanceBuffer_; // upload-heap D3D12_RAYTRACING_INSTANCE_DESC array
	uint32_t instanceCount_ = 0;
};
