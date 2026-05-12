#include "D3D12CommandQueue.h"
#include "D3D12Exception.h"
#include "D3D12CommandList.h"
#include "D3D12Fence.h"

D3D12CommandQueue::D3D12CommandQueue(Microsoft::WRL::ComPtr<ID3D12Device> device, D3D12QueueType type) : type_(type)
{
	D3D12_COMMAND_QUEUE_DESC desc = {};
	switch (type)
	{
		case D3D12QueueType::Graphics:
			desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
			break;
		case D3D12QueueType::Compute:
			desc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
			break;
		case D3D12QueueType::Copy:
			desc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
			break;
		default:
			throw D3D12Exception("Invalid D3D12QueueType specified.", E_INVALIDARG);
	}
	desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	desc.NodeMask = 0;

	HRESULT hr = device->CreateCommandQueue(&desc, IID_PPV_ARGS(&commandQueue_));
	if (FAILED(hr))
	{
		throw D3D12Exception("Failed to create D3D12 command queue.", hr);
	}
}

void D3D12CommandQueue::ExecuteCommandLists(D3D12CommandList **commandLists, uint32_t count)
{
	std::vector<ID3D12CommandList *> nativeCommandLists;
	nativeCommandLists.reserve(count);

	for (uint32_t i = 0; i < count; ++i)
	{
		D3D12CommandList *cmdList = commandLists[i];
		nativeCommandLists.push_back(cmdList->GetNativeCommandList().Get());
	}

	commandQueue_->ExecuteCommandLists(static_cast<UINT>(nativeCommandLists.size()), nativeCommandLists.data());
}

void D3D12CommandQueue::ExecuteCommandLists(D3D12CommandList **commandLists, uint32_t count, const SubmitSyncInfo &syncInfo)
{
	// Wait for any prerequisite GPU work if specified
	if (syncInfo.fence && syncInfo.waitValues)
	{
		syncInfo.fence->Wait(*syncInfo.waitValues);
	}

	std::vector<ID3D12CommandList *> nativeCommandLists;
	nativeCommandLists.reserve(count);

	for (uint32_t i = 0; i < count; ++i)
	{
		D3D12CommandList *cmdList = commandLists[i];
		nativeCommandLists.push_back(cmdList->GetNativeCommandList().Get());
	}

	commandQueue_->ExecuteCommandLists(static_cast<UINT>(nativeCommandLists.size()), nativeCommandLists.data());

	// Signal the fence with the specified value when GPU finishes these commands
	if (syncInfo.fence)
	{
		syncInfo.fence->Signal(this, syncInfo.signalValue);
	}
}

void D3D12CommandQueue::Wait(D3D12Fence *fence, uint64_t value)
{
	commandQueue_->Wait(fence->GetNativeFence().Get(), value);
}

void D3D12CommandQueue::Signal(D3D12Fence *fence, uint64_t value)
{
	commandQueue_->Signal(fence->GetNativeFence().Get(), value);
}
