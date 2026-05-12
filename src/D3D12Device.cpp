#include "D3D12Device.h"
#include "D3D12Buffer.h"
#include "D3D12Exception.h"
#include "D3D12Resource.h"
#include "D3D12Texture.h"

#include "mimalloc.h"

#include <fmtlog.h>
#include <stdexcept>

#include <string>

// extern "C"
// {
// 	__declspec(dllexport) extern const UINT D3D12SDKVersion = 717;
// }
//
// extern "C"
// {
// 	__declspec(dllexport) extern const char *D3D12SDKPath = ".\\D3D12\\";
// }

static void *AllocationCallback(size_t size, size_t alignment, void *pPrivateData)
{
	return mi_malloc_aligned(size, alignment);
}

static void FreeCallback(void *ptr, void *pPrivateData)
{
	mi_free(ptr);
}

static void PrintAdapterInfo(DXGI_ADAPTER_DESC1 &desc)
{
	int size = WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, nullptr, 0, nullptr, nullptr);
	std::string adapterName(size - 1, 0);
	WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, &adapterName[0], size, nullptr, nullptr);

	logi("Selected GPU Adapter: {}", adapterName);
	logi("  Dedicated Video Memory: {} MB", desc.DedicatedVideoMemory / (1024 * 1024));
	logi("  Dedicated System Memory: {} MB", desc.DedicatedSystemMemory / (1024 * 1024));
	logi("  Shared System Memory: {} MB", desc.SharedSystemMemory / (1024 * 1024));
}

static void GetAdapter(Microsoft::WRL::ComPtr<IDXGIFactory6> &factory, Microsoft::WRL::ComPtr<IDXGIAdapter1> &adapter)
{
	HRESULT hr;
	for (UINT adapterIndex = 0;; ++adapterIndex)
	{
		hr = factory->EnumAdapters1(adapterIndex, &adapter);
		if (hr == DXGI_ERROR_NOT_FOUND)
		{
			throw D3D12Exception("No suitable GPU adapter found.", hr);
		}

		DXGI_ADAPTER_DESC1 desc;
		adapter->GetDesc1(&desc);

		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
		{
			// Skip software adapters
			continue;
		}

		PrintAdapterInfo(desc);

		// Always prefer dedicated GPU's
		if (desc.DedicatedVideoMemory < 512 * 1024 * 1024)
		{
			// Skip adapters with less than 512 MB of dedicated video memory
			continue;
		}

		// Check if the adapter supports Direct3D 12
		hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_2, __uuidof(ID3D12Device), nullptr);

		if (SUCCEEDED(hr))
		{
			break; // Found a suitable adapter
		}
	}
}

D3D12Device::D3D12Device()
{
	HRESULT hr;

	UINT dxgiFactoryFlags = 0;
#ifdef _DEBUG
	{
		Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
		{
			debugController->EnableDebugLayer();

			// Enable GPU-based validation in debug builds.
			Microsoft::WRL::ComPtr<ID3D12Debug1> debugController1;
			// if (SUCCEEDED(debugController.As(&debugController1)))
			// 	debugController1->SetEnableGPUBasedValidation(TRUE);

			{
				Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> dredSettings;
				if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dredSettings))))
				{
					dredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
					dredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
					dredSettings->SetBreadcrumbContextEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
					logi("DRED: Auto-breadcrumbs, page fault tracking, and breadcrumb context enabled.");
				}
				else
				{
					logw("DRED: ID3D12DeviceRemovedExtendedDataSettings1 unavailable; hang diagnostics will be "
						 "limited.");
				}
			}

			// Enable additional debug layers.
			dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
		}
		else
		{
			logw("WARNING: D3D12 Debug Layer is not available.");
		}
	}
#endif

	// Create DXGI Factory
	hr = CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory_));
	if (FAILED(hr))
	{
		throw D3D12Exception("Failed to create DXGI Factory.", hr);
	}

	GetAdapter(dxgiFactory_, adapter_);


	// Create D3D12 Device
	hr = D3D12CreateDevice(adapter_.Get(), D3D_FEATURE_LEVEL_12_2, IID_PPV_ARGS(&device_));

	if (FAILED(hr))
	{
		throw D3D12Exception("Failed to create D3D12 Device.", hr);
	}

	D3D12_FEATURE_DATA_D3D12_OPTIONS16 options16 = {};
	bool GPUUploadHeapSupported = false;
	if (SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS16, &options16, sizeof(options16))))
	{
		GPUUploadHeapSupported = options16.GPUUploadHeapSupported;
		logi("D3D12_FEATURE_D3D12_OPTIONS16 supported. GPUUploadHeapSupported = {}",
			 GPUUploadHeapSupported ? "TRUE" : "FALSE");
	}

	Microsoft::WRL::ComPtr<IDXGIAdapter3> spAdapter;
	hr = adapter_.As(&spAdapter);
	if (FAILED(hr))
	{
		throw D3D12Exception("Failed to query IDXGIAdapter3 interface.", hr);
	}

	DXGI_QUERY_VIDEO_MEMORY_INFO VMInfo;
	hr = spAdapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &VMInfo);
	if (FAILED(hr))
	{
		throw D3D12Exception("Failed to query video memory info.", hr);
	}

	logi("  Budget: {} MB", VMInfo.Budget / (1024 * 1024));
	logi("  Current Usage: {} MB", VMInfo.CurrentUsage / (1024 * 1024));
	logi("  Available For Reservation: {} MB", VMInfo.AvailableForReservation / (1024 * 1024));
	logi("  Current Reservation: {} MB", VMInfo.CurrentReservation / (1024 * 1024));

	constexpr D3D12MA::ALLOCATOR_FLAGS allocatorFlags = D3D12MA_RECOMMENDED_ALLOCATOR_FLAGS;

	static D3D12MA::ALLOCATION_CALLBACKS allocatorCallbacks = {};
	allocatorCallbacks.pAllocate = AllocationCallback;
	allocatorCallbacks.pFree = FreeCallback;
	allocatorCallbacks.pPrivateData = nullptr;

	D3D12MA::ALLOCATOR_DESC desc = {};
	desc.Flags = allocatorFlags;
	desc.pDevice = device_.Get();
	desc.pAdapter = adapter_.Get();
	desc.pAllocationCallbacks = &allocatorCallbacks;

	hr = D3D12MA::CreateAllocator(&desc, &allocator_);

	if (FAILED(hr))
	{
		throw D3D12Exception("Failed to create D3D12 Memory Allocator.", hr);
	}

	const D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
	hr = D3D11On12CreateDevice(device_.Get(),
							   0,
							   featureLevels,
							   1,
							   nullptr,
							   0,
							   0,
							   &d3d11Device_,
							   &d3d11DeviceContext_,
							   nullptr);
	if (FAILED(hr))
	{
		logw("WARNING: Failed to create D3D11on12 device (0x{:08X}). GPU texture compression will be unavailable.",
			 static_cast<unsigned>(hr));
	}

	fmtlog::poll();
}

void D3D12Device::CreateSampler(const D3D12_SAMPLER_DESC &samplerDesc, CPUDescriptorHandle &destDescriptor)
{
	device_->CreateSampler(&samplerDesc, destDescriptor);
}

void D3D12Device::CreateUAV(D3D12Resource *resource,
							D3D12_UNORDERED_ACCESS_VIEW_DESC &uavDesc,
							CPUDescriptorHandle &destDescriptor)
{
	device_->CreateUnorderedAccessView(resource->GetResource(), nullptr, &uavDesc, destDescriptor);
}

void D3D12Device::CreateSRV(D3D12Resource *resource,
							D3D12_SHADER_RESOURCE_VIEW_DESC &srvDesc,
							CPUDescriptorHandle &destDescriptor)
{
	device_->CreateShaderResourceView(resource->GetResource(), &srvDesc, destDescriptor);
}

void D3D12Device::CreateCBV(const D3D12_CONSTANT_BUFFER_VIEW_DESC &cbvDesc, CPUDescriptorHandle &destDescriptor)
{
	device_->CreateConstantBufferView(&cbvDesc, destDescriptor);
}

void D3D12Device::GetCopyableFootprints1(const D3D12_RESOURCE_DESC1 &desc,
										 UINT firstSubresource,
										 UINT numSubresources,
										 UINT64 baseOffset,
										 D3D12_PLACED_SUBRESOURCE_FOOTPRINT *layouts,
										 UINT *numRows,
										 UINT64 *rowSizesInBytes,
										 UINT64 *totalBytes)
{
	Microsoft::WRL::ComPtr<ID3D12Device8> device8;
	HRESULT hr = device_.As(&device8);
	if (FAILED(hr))
	{
		throw D3D12Exception("Failed to query ID3D12Device8 interface.", hr);
	}

	device8->GetCopyableFootprints1(&desc,
									firstSubresource,
									numSubresources,
									baseOffset,
									layouts,
									numRows,
									rowSizesInBytes,
									totalBytes);
}

std::unique_ptr<D3D12CommandQueue> D3D12Device::CreateCommandQueue(D3D12QueueType type)
{
	return std::make_unique<D3D12CommandQueue>(device_, type);
}

std::unique_ptr<D3D12Swapchain> D3D12Device::CreateSwapchain(
		D3D12CommandQueue *commandQueue, void *hwnd, uint32_t width, uint32_t height, uint32_t bufferCount, bool useHDR)
{
	return std::make_unique<D3D12Swapchain>(dxgiFactory_,
											device_,
											commandQueue->GetNativeCommandQueue(),
											hwnd,
											width,
											height,
											bufferCount,
											useHDR);
}

// std::unique_ptr<D3D12CommandList> D3D12Device::CreateCommandList — restored
std::unique_ptr<D3D12CommandList> D3D12Device::CreateCommandList(D3D12CommandListType type)
{
	return std::make_unique<D3D12CommandList>(device_, type);
}

std::unique_ptr<D3D12CommandAllocator> D3D12Device::CreateCommandAllocator(D3D12CommandListType type)
{
	return std::make_unique<D3D12CommandAllocator>(device_, type);
}

std::unique_ptr<D3D12Fence> D3D12Device::CreateFence(uint64_t initialValue)
{
	return std::make_unique<D3D12Fence>(device_, initialValue);
}

std::unique_ptr<D3D12GraphicsPipeline> D3D12Device::CreateGraphicsPipeline()
{
	return std::make_unique<D3D12GraphicsPipeline>(device_);
}

std::unique_ptr<D3D12ComputePipeline> D3D12Device::CreateComputePipeline()
{
	return std::make_unique<D3D12ComputePipeline>(device_);
}

std::unique_ptr<D3D12DescriptorHeap> D3D12Device::CreateDescriptorHeap(uint32_t numDescriptors,
																	   D3D12_DESCRIPTOR_HEAP_FLAGS flags)
{
	return std::make_unique<D3D12DescriptorHeap>(device_, numDescriptors, flags);
}

void D3D12Device::GetRaytracingAccelerationStructurePrebuildInfo(
		const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS &inputs,
		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO &outInfo)
{
	Microsoft::WRL::ComPtr<ID3D12Device5> device5;
	HRESULT hr = device_.As(&device5);
	if (FAILED(hr))
	{
		throw D3D12Exception("Failed to query ID3D12Device5 interface for ray tracing.", hr);
	}
	device5->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &outInfo);
}

std::unique_ptr<D3D12Resource> D3D12Device::CreateAccelerationStructureBuffer(uint64_t size, bool isScratch)
{
	D3D12_RESOURCE_DESC1 desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Alignment = 0;
	desc.Width = size;
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.SampleDesc = { 1, 0 };
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	desc.Flags = isScratch ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
						   : D3D12_RESOURCE_FLAG_RAYTRACING_ACCELERATION_STRUCTURE;

	return CreateResource3(desc, D3D12_HEAP_TYPE_DEFAULT, D3D12_BARRIER_LAYOUT_UNDEFINED);
}

Microsoft::WRL::ComPtr<D3D12MA::Allocation> D3D12Device::CreateResource(const D3D12_RESOURCE_DESC &desc,
																		D3D12_HEAP_TYPE heapType,
																		D3D12_RESOURCE_STATES initialLayout)
{
	D3D12MA::ALLOCATION_DESC allocDesc = {};
	allocDesc.HeapType = heapType;

	Microsoft::WRL::ComPtr<D3D12MA::Allocation> allocation = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> resource;

	HRESULT hr =
			allocator_->CreateResource(&allocDesc, &desc, initialLayout, nullptr, &allocation, IID_PPV_ARGS(&resource));

	if (FAILED(hr))
	{
		throw D3D12Exception("Failed to create D3D12 resource.", hr);
	}

	return allocation;
}

std::unique_ptr<D3D12Resource> D3D12Device::CreateResource3(const D3D12_RESOURCE_DESC1 &desc,
															D3D12_HEAP_TYPE heapType,
															D3D12_BARRIER_LAYOUT initialLayout)
{
	D3D12MA::ALLOCATION_DESC allocDesc = {};
	allocDesc.HeapType = heapType;

	Microsoft::WRL::ComPtr<D3D12MA::Allocation> allocation = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> resource;
	bool isDepth = desc.Format == DXGI_FORMAT_D32_FLOAT || DXGI_FORMAT_R32_TYPELESS == desc.Format ||
				   desc.Format == DXGI_FORMAT_D24_UNORM_S8_UINT || DXGI_FORMAT_R24G8_TYPELESS == desc.Format;
	bool isBuffer = desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;
	bool isRenderTarget = (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) != 0;

	D3D12_CLEAR_VALUE clearValue = {};
	D3D12_CLEAR_VALUE *optimizedClearValue = nullptr;
	if (isDepth)
	{

		if (desc.Format == DXGI_FORMAT_R32_TYPELESS)
		{
			clearValue.Format = DXGI_FORMAT_D32_FLOAT;
		}
		else if (desc.Format == DXGI_FORMAT_R24G8_TYPELESS)
		{
			clearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		}
		else
		{
			clearValue.Format = desc.Format;
		}

		clearValue.DepthStencil.Depth = 0.0f;
		clearValue.DepthStencil.Stencil = 0;
		optimizedClearValue = &clearValue;
	}
	else if (isBuffer)
	{

		optimizedClearValue = nullptr;
	}
	else if (isRenderTarget)
	{
		clearValue.Format = desc.Format;
		clearValue.Color[0] = 0.0f;
		clearValue.Color[1] = 0.0f;
		clearValue.Color[2] = 0.0f;
		clearValue.Color[3] = 1.0f;
		optimizedClearValue = &clearValue;
	}

	HRESULT hr = allocator_->CreateResource3(&allocDesc,
											 &desc,
											 initialLayout,
											 optimizedClearValue,
											 0,
											 nullptr,
											 &allocation,
											 IID_PPV_ARGS(&resource));

	if (FAILED(hr))
	{
		throw D3D12Exception("Failed to create D3D12 resource.", hr);
	}

	return std::make_unique<D3D12Resource>(allocation);
}

std::unique_ptr<D3D12Buffer> D3D12Device::CreateBuffer(const D3D12_RESOURCE_DESC1 &desc,
													   D3D12_HEAP_TYPE heapType,
													   D3D12_BARRIER_LAYOUT initialLayout)
{
	D3D12MA::ALLOCATION_DESC allocDesc = {};
	allocDesc.HeapType = heapType;

	Microsoft::WRL::ComPtr<D3D12MA::Allocation> allocation = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> resource;

	if (device_ == nullptr)
	{
		throw std::runtime_error("D3D12Device is not initialized.");
	}

	if (allocator_ == nullptr)
	{
		throw std::runtime_error("D3D12 Memory Allocator is not initialized.");
	}

	HRESULT hr = allocator_->CreateResource3(&allocDesc,
											 &desc,
											 initialLayout,
											 nullptr,
											 0,
											 nullptr,
											 &allocation,
											 IID_PPV_ARGS(&resource));

	if (FAILED(hr))
	{
		throw D3D12Exception("Failed to create D3D12 buffer.", hr);
	}

	return std::make_unique<D3D12Buffer>(allocation);
}

std::unique_ptr<D3D12Texture> D3D12Device::CreateTexture(const D3D12_RESOURCE_DESC1 &desc,
														 D3D12_HEAP_TYPE heapType,
														 D3D12_BARRIER_LAYOUT initialLayout)
{
	D3D12MA::ALLOCATION_DESC allocDesc = {};
	allocDesc.HeapType = heapType;

	Microsoft::WRL::ComPtr<D3D12MA::Allocation> allocation = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> resource;
	bool isDepth = desc.Format == DXGI_FORMAT_D32_FLOAT || DXGI_FORMAT_R32_TYPELESS == desc.Format ||
				   desc.Format == DXGI_FORMAT_D24_UNORM_S8_UINT || DXGI_FORMAT_R24G8_TYPELESS == desc.Format;
	bool isRenderTarget = (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) != 0;

	D3D12_CLEAR_VALUE clearValue = {};
	D3D12_CLEAR_VALUE *optimizedClearValue = nullptr;
	if (isDepth)
	{
		if (desc.Format == DXGI_FORMAT_R32_TYPELESS)
		{
			clearValue.Format = DXGI_FORMAT_D32_FLOAT;
		}
		else if (desc.Format == DXGI_FORMAT_R24G8_TYPELESS)
		{
			clearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		}
		else
		{
			clearValue.Format = desc.Format;
		}

		clearValue.DepthStencil.Depth = 0.0f;
		clearValue.DepthStencil.Stencil = 0;
		optimizedClearValue = &clearValue;
	}
	else if (isRenderTarget)
	{
		clearValue.Format = desc.Format;
		clearValue.Color[0] = 0.0f;
		clearValue.Color[1] = 0.0f;
		clearValue.Color[2] = 0.0f;
		clearValue.Color[3] = 1.0f;
		optimizedClearValue = &clearValue;
	}

	HRESULT hr = allocator_->CreateResource3(&allocDesc,
											 &desc,
											 initialLayout,
											 optimizedClearValue,
											 0,
											 nullptr,
											 &allocation,
											 IID_PPV_ARGS(&resource));

	if (FAILED(hr))
	{
		throw D3D12Exception("Failed to create D3D12 texture.", hr);
	}

	return std::make_unique<D3D12Texture>(allocation);
}

// static_assert(IsDevice<D3D12Device>, "D3D12Device does not satisfy IsDevice concept.");
