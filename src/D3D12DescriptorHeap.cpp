#include "D3D12DescriptorHeap.h"
#include "D3D12Exception.h"
#include "D3D12Resource.h"

D3D12DescriptorHeap::D3D12DescriptorHeap(Microsoft::WRL::ComPtr<ID3D12Device> device,
										 uint32_t numDescriptors,
										 D3D12_DESCRIPTOR_HEAP_FLAGS flags,
										 uint32_t framesInFlight) : device_(device), numDescriptors_(numDescriptors)
{
	const D3D12_DESCRIPTOR_HEAP_FLAGS resourceFlags = framesInFlight > 0 ? D3D12_DESCRIPTOR_HEAP_FLAG_NONE : flags;
	const D3D12_DESCRIPTOR_HEAP_FLAGS samplerFlags = framesInFlight > 0 ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : flags;

	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.NumDescriptors = numDescriptors;
	heapDesc.Flags = resourceFlags;
	heapDesc.NodeMask = 0;

	HRESULT hr = device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&resourceDescriptorHeap_));
	if (FAILED(hr))
	{
		throw D3D12Exception("Failed to create D3D12 descriptor heap.", hr);
	}

	resourceDescriptorSize_ = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
	heapDesc.NumDescriptors = 2048;
	heapDesc.Flags = samplerFlags;
	hr = device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&samplerDescriptorHeap_));
	if (FAILED(hr))
	{
		throw D3D12Exception("Failed to create D3D12 sampler descriptor heap.", hr);
	}

	samplerDescriptorSize_ = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

	resourceFreeList_.reserve(numDescriptors);
	samplerFreeList_.reserve(2048);

	for (uint32_t i = numDescriptors; i > 0; --i)
	{
		resourceFreeList_.push_back(i - 1);
	}

	for (uint32_t i = 2048; i > 0; --i)
	{
		samplerFreeList_.push_back(i - 1);
	}

	frameHeaps_.resize(framesInFlight);
	for (FrameHeaps &frame: frameHeaps_)
	{
		D3D12_DESCRIPTOR_HEAP_DESC frameDesc = {};
		frameDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		frameDesc.NumDescriptors = numDescriptors;
		frameDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		frameDesc.NodeMask = 0;

		hr = device->CreateDescriptorHeap(&frameDesc, IID_PPV_ARGS(&frame.resourceHeap));
		if (FAILED(hr))
		{
			throw D3D12Exception("Failed to create per-frame D3D12 descriptor heap.", hr);
		}
	}
}

DescriptorHandle D3D12DescriptorHeap::AllocateDescriptor()
{
	if (resourceFreeList_.empty())
	{
		throw std::runtime_error("No free descriptors available in the resource heap.");
	}
	uint32_t index = resourceFreeList_.back();
	resourceFreeList_.pop_back();
	return DescriptorHandle{ index };
}

uint32_t D3D12DescriptorHeap::AllocateContiguousDescriptors(uint32_t count)
{
	// Bumps down from the top of the heap; AllocateDescriptor's free list is seeded low-to-high
	// (pop from the back yields index 0 first), so the two allocators consume the heap from
	// opposite ends and stay disjoint as long as neither exhausts the full range.
	if (contiguousBumpNext_ == 0)
	{
		contiguousBumpNext_ = numDescriptors_;
	}
	if (count > contiguousBumpNext_)
	{
		throw std::runtime_error("No contiguous descriptor range available in the resource heap.");
	}
	contiguousBumpNext_ -= count;
	return contiguousBumpNext_;
}

CPUDescriptorHandle D3D12DescriptorHeap::GetCPUDescriptorHandle(uint32_t index) const
{
	if (index >= numDescriptors_)
	{
		throw std::out_of_range("Descriptor index out of range.");
	}

	D3D12_CPU_DESCRIPTOR_HANDLE handle = resourceDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
	CPUDescriptorHandle cpuHandle(handle);
	cpuHandle.Offset(static_cast<INT>(index), resourceDescriptorSize_);
	return cpuHandle;
}

GPUDescriptorHandle D3D12DescriptorHeap::GetGPUDescriptorHandle(uint32_t index) const
{
	if (index >= numDescriptors_)
	{
		throw std::out_of_range("Descriptor index out of range.");
	}

	D3D12_GPU_DESCRIPTOR_HANDLE handle = GetResourceDescriptorHeap()->GetGPUDescriptorHandleForHeapStart();
	GPUDescriptorHandle gpuHandle(handle);
	gpuHandle.Offset(static_cast<INT>(index), resourceDescriptorSize_);
	return gpuHandle;
}

CPUDescriptorHandle D3D12DescriptorHeap::GetCPUDescriptorHandleForHeapStart() const
{
	D3D12_CPU_DESCRIPTOR_HANDLE handle = resourceDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
	return CPUDescriptorHandle(handle);
}

GPUDescriptorHandle D3D12DescriptorHeap::GetGPUDescriptorHandleForHeapStart() const
{
	D3D12_GPU_DESCRIPTOR_HANDLE handle = GetResourceDescriptorHeap()->GetGPUDescriptorHandleForHeapStart();
	return GPUDescriptorHandle(handle);
}

DescriptorHandle D3D12DescriptorHeap::CreateSRV(D3D12Resource *resource, D3D12_SHADER_RESOURCE_VIEW_DESC &srvDesc)
{
	if (resourceFreeList_.empty())
	{
		throw std::runtime_error("No free descriptors available in the resource heap.");
	}

	uint32_t descriptorIndex = resourceFreeList_.back();
	resourceFreeList_.pop_back();

	CPUDescriptorHandle destHandle = GetCPUDescriptorHandle(descriptorIndex);

	device_->CreateShaderResourceView(resource->GetResource(), &srvDesc, destHandle);
	MarkDirty();

	return DescriptorHandle{ descriptorIndex };
}

DescriptorHandle D3D12DescriptorHeap::CreateUAV(D3D12Resource *resource, D3D12_UNORDERED_ACCESS_VIEW_DESC &uavDesc)
{
	if (resourceFreeList_.empty())
	{
		throw std::runtime_error("No free descriptors available in the resource heap.");
	}

	uint32_t descriptorIndex = resourceFreeList_.back();
	resourceFreeList_.pop_back();

	CPUDescriptorHandle destHandle = GetCPUDescriptorHandle(descriptorIndex);

	device_->CreateUnorderedAccessView(resource->GetResource(), nullptr, &uavDesc, destHandle);
	MarkDirty();

	return DescriptorHandle{ descriptorIndex };
}

void D3D12DescriptorHeap::CreateSRVAt(uint32_t index, D3D12Resource *resource, D3D12_SHADER_RESOURCE_VIEW_DESC &srvDesc)
{
	CPUDescriptorHandle destHandle = GetCPUDescriptorHandle(index);
	device_->CreateShaderResourceView(resource->GetResource(), &srvDesc, destHandle);
	MarkDirty();
}

void D3D12DescriptorHeap::CreateUAVAt(uint32_t index,
									  D3D12Resource *resource,
									  D3D12_UNORDERED_ACCESS_VIEW_DESC &uavDesc)
{
	CPUDescriptorHandle destHandle = GetCPUDescriptorHandle(index);
	device_->CreateUnorderedAccessView(resource->GetResource(), nullptr, &uavDesc, destHandle);
	MarkDirty();
}

DescriptorHandle D3D12DescriptorHeap::CreateCBV(const D3D12_CONSTANT_BUFFER_VIEW_DESC &cbvDesc)
{
	if (resourceFreeList_.empty())
	{
		throw std::runtime_error("No free descriptors available in the resource heap.");
	}

	uint32_t descriptorIndex = resourceFreeList_.back();
	resourceFreeList_.pop_back();

	CPUDescriptorHandle destHandle = GetCPUDescriptorHandle(descriptorIndex);

	device_->CreateConstantBufferView(&cbvDesc, destHandle);
	MarkDirty();

	return DescriptorHandle{ descriptorIndex };
}

DescriptorHandle D3D12DescriptorHeap::CreateSampler(const D3D12_SAMPLER_DESC &samplerDesc)
{
	if (samplerFreeList_.empty())
	{
		throw std::runtime_error("No free descriptors available in the sampler heap.");
	}

	uint32_t descriptorIndex = samplerFreeList_.back();
	samplerFreeList_.pop_back();

	D3D12_CPU_DESCRIPTOR_HANDLE destHandle = samplerDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
	CD3DX12_CPU_DESCRIPTOR_HANDLE offsetHandle(destHandle);
	offsetHandle.Offset(static_cast<INT>(descriptorIndex), samplerDescriptorSize_);

	device_->CreateSampler(&samplerDesc, offsetHandle);

	return DescriptorHandle{ descriptorIndex };
}

DescriptorHandle D3D12DescriptorHeap::CreateAccelerationStructureSRV(D3D12_GPU_VIRTUAL_ADDRESS gpuVA)
{
	if (resourceFreeList_.empty())
	{
		throw std::runtime_error("No free descriptors available in the resource heap.");
	}

	uint32_t descriptorIndex = resourceFreeList_.back();
	resourceFreeList_.pop_back();

	CPUDescriptorHandle destHandle = GetCPUDescriptorHandle(descriptorIndex);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.RaytracingAccelerationStructure.Location = gpuVA;

	// Resource must be nullptr for acceleration structure SRVs.
	device_->CreateShaderResourceView(nullptr, &srvDesc, destHandle);
	MarkDirty();

	return DescriptorHandle{ descriptorIndex };
}

void D3D12DescriptorHeap::FreeResource(DescriptorHandle handle)
{
	if (handle == InvalidDescriptorHandle)
		return;
	resourceFreeList_.push_back(static_cast<uint32_t>(handle));
}

void D3D12DescriptorHeap::FreeSampler(DescriptorHandle handle)
{
	if (handle == InvalidDescriptorHandle)
		return;
	samplerFreeList_.push_back(static_cast<uint32_t>(handle));
}

void D3D12DescriptorHeap::BeginFrame(uint32_t frameIndex)
{
	if (frameIndex >= frameHeaps_.size())
	{
		throw std::out_of_range("Frame index out of range for descriptor heap.");
	}
	currentFrameIndex_ = frameIndex;
}

void D3D12DescriptorHeap::Flush()
{
	if (frameHeaps_.empty())
	{
		return;
	}

	FrameHeaps &frame = frameHeaps_[currentFrameIndex_];
	if (!frame.dirty)
	{
		return;
	}

	device_->CopyDescriptorsSimple(numDescriptors_,
								   frame.resourceHeap->GetCPUDescriptorHandleForHeapStart(),
								   resourceDescriptorHeap_->GetCPUDescriptorHandleForHeapStart(),
								   D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	frame.dirty = false;
}

void D3D12DescriptorHeap::MarkDirty()
{
	for (FrameHeaps &frame: frameHeaps_)
	{
		frame.dirty = true;
	}
}
