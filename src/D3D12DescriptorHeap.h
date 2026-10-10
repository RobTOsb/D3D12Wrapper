#pragma once

#include "pch.h"

class D3D12Resource;

class D3D12DescriptorHeap
{
public:
	D3D12DescriptorHeap(Microsoft::WRL::ComPtr<ID3D12Device> device,
						uint32_t numDescriptors,
						D3D12_DESCRIPTOR_HEAP_FLAGS flags,
						uint32_t framesInFlight = 0);

	~D3D12DescriptorHeap() = default;

	CPUDescriptorHandle GetCPUDescriptorHandle(uint32_t index) const;
	GPUDescriptorHandle GetGPUDescriptorHandle(uint32_t index) const;
	CPUDescriptorHandle GetCPUDescriptorHandleForHeapStart() const;
	GPUDescriptorHandle GetGPUDescriptorHandleForHeapStart() const;
	DescriptorHandle AllocateDescriptor();
	uint32_t AllocateContiguousDescriptors(uint32_t count);
	DescriptorHandle CreateSRV(D3D12Resource *resource, D3D12_SHADER_RESOURCE_VIEW_DESC &srvDesc);
	DescriptorHandle CreateUAV(D3D12Resource *resource, D3D12_UNORDERED_ACCESS_VIEW_DESC &uavDesc);
	// Creates a view at an already-allocated index instead of allocating a fresh one from the free list.
	void CreateSRVAt(uint32_t index, D3D12Resource *resource, D3D12_SHADER_RESOURCE_VIEW_DESC &srvDesc);
	void CreateUAVAt(uint32_t index, D3D12Resource *resource, D3D12_UNORDERED_ACCESS_VIEW_DESC &uavDesc);
	DescriptorHandle CreateCBV(const D3D12_CONSTANT_BUFFER_VIEW_DESC &cbvDesc);
	DescriptorHandle CreateSampler(const D3D12_SAMPLER_DESC &samplerDesc);
	// Creates an SRV for a TLAS. The resource pointer is null (valid only for AS descriptors).
	DescriptorHandle CreateAccelerationStructureSRV(D3D12_GPU_VIRTUAL_ADDRESS gpuVA);
	void FreeResource(DescriptorHandle handle);
	void FreeSampler(DescriptorHandle handle);

	void BeginFrame(uint32_t frameIndex);

	void Flush();

	void MarkDirty();

	uint32_t GetResourceDescriptorSize() const
	{
		return resourceDescriptorSize_;
	}

	uint32_t GetSamplerDescriptorSize() const
	{
		return samplerDescriptorSize_;
	}

	ID3D12DescriptorHeap *GetResourceDescriptorHeap() const
	{
		return frameHeaps_.empty() ? resourceDescriptorHeap_.Get() : frameHeaps_[currentFrameIndex_].resourceHeap.Get();
	}

	ID3D12DescriptorHeap *GetSamplerDescriptorHeap() const
	{
		return samplerDescriptorHeap_.Get();
	}

	size_t GetFreeListSize() const
	{
		return resourceFreeList_.size();
	}

private:
	struct FrameHeaps
	{
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> resourceHeap;
		bool dirty = true;
	};

	Microsoft::WRL::ComPtr<ID3D12Device> device_;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> resourceDescriptorHeap_;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> samplerDescriptorHeap_;
	uint32_t resourceDescriptorSize_;
	uint32_t samplerDescriptorSize_;
	uint32_t numDescriptors_;
	std::vector<uint32_t> resourceFreeList_;
	std::vector<uint32_t> samplerFreeList_;
	uint32_t contiguousBumpNext_ = 0; // grows downward from numDescriptors_, disjoint from resourceFreeList_

	std::vector<FrameHeaps> frameHeaps_;
	uint32_t currentFrameIndex_ = 0;
};
