#pragma once

#include "pch.h"

class D3D12Resource;

class D3D12DescriptorHeap
{
public:
	D3D12DescriptorHeap(Microsoft::WRL::ComPtr<ID3D12Device> device,
						uint32_t numDescriptors,
						D3D12_DESCRIPTOR_HEAP_FLAGS flags);

	~D3D12DescriptorHeap() = default;

	CPUDescriptorHandle GetCPUDescriptorHandle(uint32_t index) const;
	GPUDescriptorHandle GetGPUDescriptorHandle(uint32_t index) const;
	CPUDescriptorHandle GetCPUDescriptorHandleForHeapStart() const;
	GPUDescriptorHandle GetGPUDescriptorHandleForHeapStart() const;
	DescriptorHandle AllocateDescriptor();
	DescriptorHandle CreateSRV(D3D12Resource *resource, D3D12_SHADER_RESOURCE_VIEW_DESC &srvDesc);
	DescriptorHandle CreateUAV(D3D12Resource *resource, D3D12_UNORDERED_ACCESS_VIEW_DESC &uavDesc);
	DescriptorHandle CreateCBV(const D3D12_CONSTANT_BUFFER_VIEW_DESC &cbvDesc);
	DescriptorHandle CreateSampler(const D3D12_SAMPLER_DESC &samplerDesc);
	// Creates an SRV for a TLAS. The resource pointer is null (valid only for AS descriptors).
	DescriptorHandle CreateAccelerationStructureSRV(D3D12_GPU_VIRTUAL_ADDRESS gpuVA);
	void FreeResource(DescriptorHandle handle);
	void FreeSampler(DescriptorHandle handle);

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
		return resourceDescriptorHeap_.Get();
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
	Microsoft::WRL::ComPtr<ID3D12Device> device_;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> resourceDescriptorHeap_;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> samplerDescriptorHeap_;
	uint32_t resourceDescriptorSize_;
	uint32_t samplerDescriptorSize_;
	uint32_t numDescriptors_;
	std::vector<uint32_t> resourceFreeList_;
	std::vector<uint32_t> samplerFreeList_;
};
