#pragma once

#include "D3D12Resource.h"

class D3D12Buffer : public D3D12Resource
{
public:
	D3D12Buffer() = default;

	D3D12Buffer(Microsoft::WRL::ComPtr<ID3D12Resource> resource) : D3D12Resource(resource) {}

	D3D12Buffer(Microsoft::WRL::ComPtr<D3D12MA::Allocation> allocation) : D3D12Resource(allocation) {}

	~D3D12Buffer() override = default;

	void Map(UINT subresource = 0, const D3D12_RANGE &readRange = { 0, 0 })
	{
		HRESULT hr = allocation_->GetResource()->Map(subresource, &readRange, &mappedData_);
		if (FAILED(hr))
		{
			throw std::runtime_error("Failed to map D3D12 buffer.");
		}
	}

	void Unmap(UINT subresource = 0, const D3D12_RANGE &writtenRange = { 0, 0 })
	{
		allocation_->GetResource()->Unmap(subresource, &writtenRange);
	}

	void *GetMappedData() const
	{
		return mappedData_;
	}

private:
	void *mappedData_ = nullptr;
};
