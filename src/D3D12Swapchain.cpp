#include "D3D12Swapchain.h"

#include "D3D12Exception.h"
#include "D3D12Resource.h"
#include "D3D12Texture.h"

D3D12Swapchain::D3D12Swapchain(Microsoft::WRL::ComPtr<IDXGIFactory6> dxgiFactory,
							   Microsoft::WRL::ComPtr<ID3D12Device> device,
							   Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue,
							   void *hwnd,
							   uint32_t width,
							   uint32_t height,
							   uint32_t bufferCount,
							   bool useHDR)
{
	width_ = width;
	height_ = height;
	bufferCount_ = bufferCount;

	// Create swapchain description
	DXGI_SWAP_CHAIN_DESC1 swapchainDesc = {};
	swapchainDesc.Width = width;
	swapchainDesc.Height = height;
	swapchainDesc.Format = useHDR ? DXGI_FORMAT_R10G10B10A2_UNORM  : DXGI_FORMAT_R8G8B8A8_UNORM;
	swapchainDesc.Stereo = FALSE;
	swapchainDesc.SampleDesc.Count = 1;
	swapchainDesc.SampleDesc.Quality = 0;
	swapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapchainDesc.BufferCount = bufferCount;
	swapchainDesc.Scaling = DXGI_SCALING_STRETCH;
	swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapchainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
	swapchainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

	backBufferFormat_ = swapchainDesc.Format;
	swapchainFlags_ = swapchainDesc.Flags;

	Microsoft::WRL::ComPtr<IDXGISwapChain1> swapchain1;
	HRESULT hr = dxgiFactory->CreateSwapChainForHwnd(commandQueue.Get(),
													 static_cast<HWND>(hwnd),
													 &swapchainDesc,
													 nullptr,
													 nullptr,
													 &swapchain1);
	if (FAILED(hr))
	{
		throw D3D12Exception("Failed to create DXGI swapchain.", hr);
	}

	hr = swapchain1.As(&swapchain_);

	if (FAILED(hr))
	{
		throw D3D12Exception("Failed to query IDXGISwapChain3 interface.", hr);
	}

	if (useHDR)
	{
		UINT colorSpaceSupport = 0;
		if (SUCCEEDED(swapchain_->CheckColorSpaceSupport(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020,
														 &colorSpaceSupport)) &&
			(colorSpaceSupport & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT))
		{
			hr = swapchain_->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
			if (FAILED(hr))
			{
				throw D3D12Exception("Failed to set HDR color space.", hr);
			}
		}
	}

	backBuffers_.resize(bufferCount);
	for (uint32_t i = 0; i < bufferCount; ++i)
	{
		backBuffers_[i] = std::make_unique<D3D12Texture>();
		Microsoft::WRL::ComPtr<ID3D12Resource> bufferResource;
		hr = swapchain_->GetBuffer(i, IID_PPV_ARGS(&bufferResource));
		if (FAILED(hr))
		{
			throw D3D12Exception("Failed to get swapchain back buffer.", hr);
		}
		backBuffers_[i]->SetResource(bufferResource.Get());
	}
}

void D3D12Swapchain::Present(uint32_t syncInterval, uint32_t flags)
{
	HRESULT hr = swapchain_->Present(syncInterval, flags);
	if (FAILED(hr))
	{
		throw D3D12Exception("Failed to present swapchain.", hr);
	}
}

void D3D12Swapchain::Resize(uint32_t width, uint32_t height)
{
	if (width == 0 || height == 0 || (width == width_ && height == height_))
	{
		return;
	}

	for (auto &backBuffer: backBuffers_)
	{
		backBuffer->SetResource(nullptr);
	}

	HRESULT hr = swapchain_->ResizeBuffers(bufferCount_, width, height, backBufferFormat_, swapchainFlags_);
	if (FAILED(hr))
	{
		throw D3D12Exception("Failed to resize DXGI swapchain.", hr);
	}

	width_ = width;
	height_ = height;

	for (uint32_t i = 0; i < bufferCount_; ++i)
	{
		Microsoft::WRL::ComPtr<ID3D12Resource> bufferResource;
		hr = swapchain_->GetBuffer(i, IID_PPV_ARGS(&bufferResource));
		if (FAILED(hr))
		{
			throw D3D12Exception("Failed to get swapchain back buffer after resize.", hr);
		}
		backBuffers_[i]->SetResource(bufferResource.Get());
	}
}
