#pragma once

#include "pch.h"

class D3D12Resource;
class D3D12Texture;

class D3D12Swapchain
{
public:
	D3D12Swapchain(Microsoft::WRL::ComPtr<IDXGIFactory6> dxgiFactory,
				   Microsoft::WRL::ComPtr<ID3D12Device> device,
				   Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue,
				   void *hwnd,
				   uint32_t width,
				   uint32_t height,
				   uint32_t bufferCount,
				   bool useHDR);
	~D3D12Swapchain() = default;

	Microsoft::WRL::ComPtr<IDXGISwapChain3> GetNativeSwapchain() const
	{
		return swapchain_;
	}

	D3D12Texture *GetCurrentBackBuffer() const
	{
		return backBuffers_[swapchain_->GetCurrentBackBufferIndex()].get();
	}

	uint32_t GetCurrentBackBufferIndex() const
	{
		return swapchain_->GetCurrentBackBufferIndex();
	}

	DXGI_FORMAT GetBackBufferFormat() const
	{
		return backBufferFormat_;
	}

	std::vector<std::unique_ptr<D3D12Texture>> &GetBackBuffers()
	{
		return backBuffers_;
	}

	uint32_t GetWidth() const
	{
		return width_;
	}

	uint32_t GetHeight() const
	{
		return height_;
	}

	void Present(uint32_t syncInterval = 0, uint32_t flags = 0);

private:
	Microsoft::WRL::ComPtr<IDXGISwapChain3> swapchain_;
	std::vector<std::unique_ptr<D3D12Texture>> backBuffers_;
	DXGI_FORMAT backBufferFormat_;
	UINT width_;
	UINT height_;
	uint32_t bufferCount_;
};
