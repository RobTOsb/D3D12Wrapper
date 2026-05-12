#pragma once

#include "pch.h"

#include "D3D12Exception.h"

enum class D3D12CommandListType : uint8_t
{
	Graphics,
	Bundle,
	Compute,
	Copy,
    Count
};

class D3D12CommandList;

class D3D12CommandAllocator
{
public:
    D3D12CommandAllocator(Microsoft::WRL::ComPtr<ID3D12Device> device, D3D12CommandListType type) : device_(device) , type_(type)
    {
        D3D12_COMMAND_LIST_TYPE commandListType;

        switch (type)
        {
            case D3D12CommandListType::Graphics:
                commandListType = D3D12_COMMAND_LIST_TYPE_DIRECT;
                break;
            case D3D12CommandListType::Bundle:
                commandListType = D3D12_COMMAND_LIST_TYPE_BUNDLE;
                break;
            case D3D12CommandListType::Compute:
                commandListType = D3D12_COMMAND_LIST_TYPE_COMPUTE;
                break;
            case D3D12CommandListType::Copy:
                commandListType = D3D12_COMMAND_LIST_TYPE_COPY;
                break;
            default:
                throw D3D12Exception("Invalid D3D12CommandListType specified.", E_INVALIDARG);
        }

        HRESULT hr = device->CreateCommandAllocator(commandListType, IID_PPV_ARGS(&commandAllocator_));
        if (FAILED(hr))
        {
            throw D3D12Exception("Failed to create D3D12 command allocator.", hr);
        }

    }
    ~D3D12CommandAllocator() = default;

    std::unique_ptr<D3D12CommandList> CreateCommandList()
    {
        return std::make_unique<D3D12CommandList>(device_, this, type_);
    }

    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> GetNativeCommandAllocator() const
    {
        return commandAllocator_;
    }

    void Reset()
    {
        HRESULT hr = commandAllocator_->Reset();
        if (FAILED(hr))
        {
            throw D3D12Exception("Failed to reset D3D12 command allocator.", hr);
        }
    }
    
private:
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator_;
    D3D12CommandListType type_;
};