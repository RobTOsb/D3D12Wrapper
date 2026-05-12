#include "D3D12Fence.h"
#include "D3D12CommandQueue.h"
#include "D3D12Exception.h"

D3D12Fence::D3D12Fence(Microsoft::WRL::ComPtr<ID3D12Device> device, uint64_t initialValue) : currentValue_(initialValue)
{
    HRESULT hr = device->CreateFence(initialValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
    if (FAILED(hr))
    {
        throw D3D12Exception("Failed to create D3D12 fence.", hr);
    }

    fenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (fenceEvent_ == nullptr)
    {
        throw D3D12Exception("Failed to create fence event for D3D12 fence.", HRESULT_FROM_WIN32(GetLastError()));
    }
}

uint64_t D3D12Fence::GetCompletedValue() const
{
    return fence_->GetCompletedValue();
}

void D3D12Fence::Signal(D3D12CommandQueue *commandQueue, uint64_t value)
{

    HRESULT hr = commandQueue->GetNativeCommandQueue()->Signal(fence_.Get(), value);
    if (FAILED(hr))
    {
        throw D3D12Exception("Failed to signal D3D12 fence.", hr);
    }
    currentValue_ = value;
}

void D3D12Fence::Signal(uint64_t value)
{
    HRESULT hr = fence_->Signal(value);
    if (FAILED(hr))
    {
        throw D3D12Exception("Failed to signal D3D12 fence.", hr);
    }
    currentValue_ = value;
}

void D3D12Fence::Wait(uint64_t value)
{
    if (fence_->GetCompletedValue() < value)
    {
        HRESULT hr = fence_->SetEventOnCompletion(value, fenceEvent_);
        if (FAILED(hr))
        {
            throw D3D12Exception("Failed to set event on completion for D3D12 fence.", hr);
        }
        WaitForSingleObject(fenceEvent_, INFINITE);
    }
}