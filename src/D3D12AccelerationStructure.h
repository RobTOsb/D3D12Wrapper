#pragma once

#include "pch.h"

class D3D12Device;
class D3D12CommandList;
class D3D12Resource;
class D3D12Buffer;

struct BLASGeometryDesc
{
	D3D12_GPU_VIRTUAL_ADDRESS vertexBufferVA = 0;
	uint32_t vertexCount = 0;
	uint32_t vertexStride = 0; // typically sizeof(InterleavedVertex) - only the leading float3 position is read
	D3D12_GPU_VIRTUAL_ADDRESS indexBufferVA = 0;
	uint32_t indexCount = 0;
	DXGI_FORMAT indexFormat = DXGI_FORMAT_R32_UINT;
	bool isOpaque = true;
	bool noDuplicateAnyHit = false;
	D3D12_GPU_VIRTUAL_ADDRESS opacityMicromapArrayVA = 0;
	D3D12_GPU_VIRTUAL_ADDRESS opacityMicromapIndexBufferVA = 0;
};

// One BLAS per mesh. Rebuild-only (no refit/update) - appropriate for a static scene. Built with
// compaction allowed, so it can shrink to its compacted size after the build.
class D3D12BottomLevelAS
{
public:
	D3D12BottomLevelAS(D3D12Device *device, const BLASGeometryDesc &geometry);
	~D3D12BottomLevelAS();

	// geometryDesc_ points into the object itself when it has opacity micromaps.
	D3D12BottomLevelAS(const D3D12BottomLevelAS &) = delete;
	D3D12BottomLevelAS &operator=(const D3D12BottomLevelAS &) = delete;

	// Emits the actual BuildRaytracingAccelerationStructure call. Caller must have already
	// transitioned/barriered the source vertex/index buffers to a readable state. Allocates scratch
	// memory of its own on the first call and keeps it.
	void Build(D3D12CommandList *cmdList);
	// The same with GetScratchSize() bytes of scratch memory the caller owns (256-byte aligned), and
	// the compacted size written as a UINT64 to compactedSizeAddress (8-byte aligned, in a buffer in
	// UAV access) unless it is 0.
	void Build(D3D12CommandList *cmdList, D3D12_GPU_VIRTUAL_ADDRESS scratch, D3D12_GPU_VIRTUAL_ADDRESS compactedSizeAddress);

	// Copies the built BLAS into a new buffer of compactedSize bytes, which it uses from then on.
	// The old one stays alive until ReleaseUncompacted, to be called once the GPU is done copying.
	void Compact(D3D12CommandList *cmdList, uint64_t compactedSize);
	void ReleaseUncompacted();

	uint64_t GetScratchSize() const
	{
		return scratchSize_;
	}

	uint64_t GetResultSize() const
	{
		return resultSize_;
	}

	D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const;

private:
	D3D12Device *device_ = nullptr;
	std::unique_ptr<D3D12Resource> resultBuffer_;
	std::unique_ptr<D3D12Resource> uncompactedBuffer_;
	std::unique_ptr<D3D12Resource> scratchBuffer_;
	uint64_t scratchSize_ = 0;
	uint64_t resultSize_ = 0;
	D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc_ = {};
	D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC ommTriangles_ = {}; // OMM_TRIANGLES geometry only
	D3D12_RAYTRACING_GEOMETRY_OMM_LINKAGE_DESC ommLinkage_ = {};
};

class D3D12OpacityMicromapArray
{
public:
	// micromaps: one per OMM, its ByteOffset into data.
	D3D12OpacityMicromapArray(D3D12Device *device,
							  std::span<const uint8_t> data,
							  std::span<const D3D12_RAYTRACING_OPACITY_MICROMAP_DESC> micromaps);
	~D3D12OpacityMicromapArray();

	D3D12OpacityMicromapArray(const D3D12OpacityMicromapArray &) = delete;
	D3D12OpacityMicromapArray &operator=(const D3D12OpacityMicromapArray &) = delete;

	// See D3D12BottomLevelAS::Build.
	void Build(D3D12CommandList *cmdList, D3D12_GPU_VIRTUAL_ADDRESS scratch, D3D12_GPU_VIRTUAL_ADDRESS compactedSizeAddress);
	void Compact(D3D12CommandList *cmdList, uint64_t compactedSize);
	// The uncompacted array and the build inputs, once the GPU is done with them.
	void ReleaseBuildResources();

	uint64_t GetScratchSize() const
	{
		return scratchSize_;
	}

	uint64_t GetResultSize() const
	{
		return resultSize_;
	}

	D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const;

private:
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS Inputs() const;

	D3D12Device *device_ = nullptr;
	std::unique_ptr<D3D12Resource> resultBuffer_;
	std::unique_ptr<D3D12Resource> uncompactedBuffer_;
	std::unique_ptr<D3D12Buffer> dataBuffer_; // upload heap, the OC1 states
	std::unique_ptr<D3D12Buffer> descBuffer_; // upload heap, D3D12_RAYTRACING_OPACITY_MICROMAP_DESC per OMM
	std::vector<D3D12_RAYTRACING_OPACITY_MICROMAP_HISTOGRAM_ENTRY> histogram_;
	D3D12_RAYTRACING_OPACITY_MICROMAP_ARRAY_DESC arrayDesc_ = {};
	uint64_t scratchSize_ = 0;
	uint64_t resultSize_ = 0;
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
