#pragma once

#include "pch.h"

class D3D12CommandQueue;

class D3D12Fence
{
public:
	D3D12Fence(Microsoft::WRL::ComPtr<ID3D12Device> device, uint64_t initialValue = 0);
	~D3D12Fence() = default;

	Microsoft::WRL::ComPtr<ID3D12Fence> GetNativeFence() const
	{
		return fence_;
	}

	uint64_t GetCurrentValue() const
	{
		return currentValue_;
	}

	uint64_t GetCompletedValue() const;
	void Signal(uint64_t value);
	void Signal(D3D12CommandQueue *commandQueue, uint64_t value);
	void Wait(uint64_t value);

private:
	Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
	HANDLE fenceEvent_;
	uint64_t currentValue_;
};
