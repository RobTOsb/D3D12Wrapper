#pragma once

#include "pch.h"

#include "D3D12CommandList.h"
#include "D3D12CommandQueue.h"
#include "D3D12DescriptorHeap.h"
#include "D3D12Fence.h"
#include "D3D12Pipeline.h"
#include "D3D12Swapchain.h"

class D3D12Resource;
class D3D12Buffer;
class D3D12Texture;

class D3D12Device
{
public:
	explicit D3D12Device(bool useSoftwareAdapter = false);
	~D3D12Device() = default;

	std::unique_ptr<D3D12CommandQueue> CreateCommandQueue(D3D12QueueType type);

	std::unique_ptr<D3D12Swapchain> CreateSwapchain(D3D12CommandQueue *commandQueue,
													void *hwnd,
													uint32_t width,
													uint32_t height,
													uint32_t imageCount = 3,
													bool useHDR = false);

	std::unique_ptr<D3D12CommandAllocator> CreateCommandAllocator(D3D12CommandListType type);
	std::unique_ptr<D3D12CommandList> CreateCommandList(D3D12CommandListType type);

	std::unique_ptr<D3D12Fence> CreateFence(uint64_t initialValue = 0);

	std::unique_ptr<D3D12Buffer> CreateBuffer(const D3D12_RESOURCE_DESC1 &desc,
											  D3D12_HEAP_TYPE heapType,
											  D3D12_BARRIER_LAYOUT initialLayout = D3D12_BARRIER_LAYOUT_UNDEFINED);

	std::unique_ptr<D3D12Texture> CreateTexture(const D3D12_RESOURCE_DESC1 &desc,
												D3D12_HEAP_TYPE heapType,
												D3D12_BARRIER_LAYOUT initialLayout = D3D12_BARRIER_LAYOUT_UNDEFINED);

	void GetCopyableFootprints1(const D3D12_RESOURCE_DESC1 &desc,
								UINT firstSubresource,
								UINT numSubresources,
								UINT64 baseOffset,
								D3D12_PLACED_SUBRESOURCE_FOOTPRINT *layouts,
								UINT *numRows,
								UINT64 *rowSizesInBytes,
								UINT64 *totalBytes);

	std::unique_ptr<D3D12GraphicsPipeline> CreateGraphicsPipeline();
	std::unique_ptr<D3D12ComputePipeline> CreateComputePipeline();
	std::unique_ptr<D3D12RaytracingPipeline> CreateRaytracingPipeline();

	std::unique_ptr<D3D12DescriptorHeap> CreateDescriptorHeap(uint32_t numDescriptors,
															  D3D12_DESCRIPTOR_HEAP_FLAGS flags);

	void GetRaytracingAccelerationStructurePrebuildInfo(
			const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS &inputs,
			D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO &outInfo);
	std::unique_ptr<D3D12Resource> CreateAccelerationStructureBuffer(uint64_t size, bool isScratch = false);

	Microsoft::WRL::ComPtr<ID3D12CommandSignature> CreateDispatchIndirectCommandSignature(
			ID3D12RootSignature *rootSignature, uint32_t rootConstantsSizeInBytes);
	Microsoft::WRL::ComPtr<ID3D12CommandSignature> CreateDrawIndirectCommandSignature(
			ID3D12RootSignature *rootSignature, uint32_t rootConstantsSizeInBytes);
	Microsoft::WRL::ComPtr<ID3D12CommandSignature> CreateDispatchMeshIndirectCommandSignature(
			ID3D12RootSignature *rootSignature, uint32_t rootConstantsSizeInBytes);

	Microsoft::WRL::ComPtr<ID3D12Device> GetNativeDevice() const
	{
		return device_;
	}

	Microsoft::WRL::ComPtr<D3D12MA::Allocator> GetMemoryAllocator() const
	{
		return allocator_;
	}

	// DXR 1.2 shader execution reordering: lib_6_9 shaders can use HitObject and MaybeReorderThread.
	bool SupportsShaderExecutionReordering() const
	{
		return serSupported_;
	}

	// False where the driver implements MaybeReorderThread as a no-op.
	bool ShaderExecutionReorderingActuallyReorders() const
	{
		return serActuallyReorders_;
	}

private:
	void QueryShaderExecutionReordering();

	Microsoft::WRL::ComPtr<ID3D12Device> device_;
	Microsoft::WRL::ComPtr<IDXGIFactory6> dxgiFactory_;
	Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter_;
	Microsoft::WRL::ComPtr<D3D12MA::Allocator> allocator_;
	bool serSupported_ = false;
	bool serActuallyReorders_ = false;
};
