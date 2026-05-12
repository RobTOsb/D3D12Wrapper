#pragma once

#include "pch.h"

class D3D12CommandList;
class D3D12Fence;

enum class D3D12QueueType : uint8_t
{
	Graphics,
	Compute,
	Copy,
	Count
};

struct SubmitSyncInfo
{
	D3D12Fence *fence;
	uint64_t signalValue;
	const uint64_t *waitValues = nullptr;
};

class D3D12CommandQueue
{
public:
	D3D12CommandQueue(Microsoft::WRL::ComPtr<ID3D12Device> device, D3D12QueueType type);
	~D3D12CommandQueue() = default;

	Microsoft::WRL::ComPtr<ID3D12CommandQueue> GetNativeCommandQueue() const
	{
		return commandQueue_;
	}

	void Wait(D3D12Fence *fence, uint64_t value);
	void Signal(D3D12Fence *fence, uint64_t value);

	void ExecuteCommandLists(D3D12CommandList **commandLists, uint32_t count);
	void ExecuteCommandLists(D3D12CommandList **commandLists, uint32_t count, const SubmitSyncInfo &syncInfo);

private:
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue_;
	D3D12QueueType type_;
};
