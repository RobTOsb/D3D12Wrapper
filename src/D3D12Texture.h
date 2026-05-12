#pragma once

#include "D3D12Resource.h"

class D3D12Texture : public D3D12Resource
{
public:
	D3D12Texture() = default;

	D3D12Texture(Microsoft::WRL::ComPtr<ID3D12Resource> resource) : D3D12Resource(resource) {}

	D3D12Texture(Microsoft::WRL::ComPtr<D3D12MA::Allocation> allocation) : D3D12Resource(allocation) {}

	~D3D12Texture() override = default;

	void WriteToSubresource(
			UINT dstSubresource, const D3D12_BOX *dstBox, const void *srcData, UINT srcRowPitch, UINT srcDepthPitch)
	{
		HRESULT hr = allocation_->GetResource()->WriteToSubresource(dstSubresource,
																	dstBox,
																	srcData,
																	srcRowPitch,
																	srcDepthPitch);
		if (FAILED(hr))
		{
			throw std::runtime_error("Failed to write to D3D12 texture subresource.");
		}
	}
};
