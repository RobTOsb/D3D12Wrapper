#include "D3D12CommandList.h"
#include <string>
#include "D3D12DescriptorHeap.h"
#include "D3D12Exception.h"
#include "D3D12Pipeline.h"
#include "D3D12Resource.h"


D3D12CommandList::D3D12CommandList(Microsoft::WRL::ComPtr<ID3D12Device> device,
								   D3D12CommandAllocator *allocator,
								   D3D12CommandListType type) : commandAllocator_(allocator), type_(type)
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

	HRESULT hr = device->CreateCommandList(0,
										   commandListType,
										   commandAllocator_->GetNativeCommandAllocator().Get(),
										   nullptr,
										   IID_PPV_ARGS(&commandList_));
	if (FAILED(hr))
	{
		throw D3D12Exception("Failed to create D3D12 command list.", hr);
	}

	// Close the command list initially. It needs to be reset before recording commands.
	hr = commandList_->Close();
	if (FAILED(hr))
	{
		throw D3D12Exception("Failed to close D3D12 command list after creation.", hr);
	}

	hr = renderTargetHelper_.Init(device.Get());
	if (FAILED(hr))
	{
		throw D3D12Exception("Failed to initialize RenderTargetHelper.", hr);
	}
}

D3D12CommandList::D3D12CommandList(Microsoft::WRL::ComPtr<ID3D12Device> device, D3D12CommandListType type) :
	commandAllocator_(nullptr), type_(type)
{
	ownedAllocator_ = std::make_unique<D3D12CommandAllocator>(device, type);
	commandAllocator_ = ownedAllocator_.get();

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

	HRESULT hr = device->CreateCommandList(0,
										   commandListType,
										   commandAllocator_->GetNativeCommandAllocator().Get(),
										   nullptr,
										   IID_PPV_ARGS(&commandList_));
	if (FAILED(hr))
	{
		throw D3D12Exception("Failed to create D3D12 command list.", hr);
	}

	hr = commandList_->Close();
	if (FAILED(hr))
	{
		throw D3D12Exception("Failed to close D3D12 command list after creation.", hr);
	}

	hr = renderTargetHelper_.Init(device.Get());
	if (FAILED(hr))
	{
		throw D3D12Exception("Failed to initialize RenderTargetHelper.", hr);
	}
}

void D3D12CommandList::SetName(const char *name)
{
	const std::wstring wName(name, name + strlen(name));
	commandList_->SetName(wName.c_str());
}

void D3D12CommandList::CopyResource(D3D12Resource *dstResource, D3D12Resource *srcResource)
{
	commandList_->CopyResource(dstResource->GetResource(), srcResource->GetResource());
}

void D3D12CommandList::CopyBufferRegion(D3D12Resource *dstResource,
										uint64_t dstOffset,
										D3D12Resource *srcResource,
										uint64_t srcOffset,
										uint64_t numBytes)
{
	commandList_->CopyBufferRegion(dstResource->GetResource(),
								   dstOffset,
								   srcResource->GetResource(),
								   srcOffset,
								   numBytes);
}

void D3D12CommandList::CopyTextureRegion(D3D12Resource *dstResource,
										 uint32_t dstX,
										 uint32_t dstY,
										 uint32_t dstZ,
										 D3D12Resource *srcResource,
										 const D3D12Rect *srcRect)
{
	D3D12_TEXTURE_COPY_LOCATION dstLocation = {};
	dstLocation.pResource = dstResource->GetResource();
	dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	dstLocation.SubresourceIndex = 0;

	D3D12_TEXTURE_COPY_LOCATION srcLocation = {};
	srcLocation.pResource = srcResource->GetResource();
	srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	srcLocation.SubresourceIndex = 0;

	D3D12_BOX srcBox = {};
	if (srcRect)
	{
		srcBox.left = srcRect->left;
		srcBox.top = srcRect->top;
		srcBox.front = 0;
		srcBox.right = srcRect->right;
		srcBox.bottom = srcRect->bottom;
		srcBox.back = 1;
	}
	else
	{
		// If no source rect is provided, copy the entire resource
		D3D12_RESOURCE_DESC desc = srcResource->GetResource()->GetDesc();
		srcBox.left = 0;
		srcBox.top = 0;
		srcBox.front = 0;
		srcBox.right = static_cast<UINT>(desc.Width);
		srcBox.bottom = desc.Height;
		srcBox.back = 1;
	}

	D3D12_BOX *pSrcBox = srcRect ? &srcBox : nullptr;

	commandList_->CopyTextureRegion(&dstLocation, dstX, dstY, dstZ, &srcLocation, pSrcBox);
}

void D3D12CommandList::CopyBufferToTextureRegion(D3D12Resource *dstTexture,
												 uint32_t dstX,
												 uint32_t dstY,
												 uint32_t dstZ,
												 D3D12Resource *srcBuffer,
												 const D3D12_BOX *srcBox,
												 uint32_t srcRowPitch)
{
	D3D12_TEXTURE_COPY_LOCATION dstLocation = {};
	dstLocation.pResource = dstTexture->GetResource();
	dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	dstLocation.SubresourceIndex = 0;

	D3D12_TEXTURE_COPY_LOCATION srcLocation = {};
	srcLocation.pResource = srcBuffer->GetResource();
	srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	srcLocation.PlacedFootprint.Offset = 0;
	srcLocation.PlacedFootprint.Footprint.Format = dstTexture->GetResource()->GetDesc().Format;
	srcLocation.PlacedFootprint.Footprint.Width =
			(srcBox) ? (srcBox->right - srcBox->left) : static_cast<UINT>(dstTexture->GetResource()->GetDesc().Width);
	srcLocation.PlacedFootprint.Footprint.Height =
			(srcBox) ? (srcBox->bottom - srcBox->top) : dstTexture->GetResource()->GetDesc().Height;
	srcLocation.PlacedFootprint.Footprint.Depth = 1;
	srcLocation.PlacedFootprint.Footprint.RowPitch = srcRowPitch;

	commandList_->CopyTextureRegion(&dstLocation, dstX, dstY, dstZ, &srcLocation, srcBox);
}

void D3D12CommandList::ResolveSubresource(D3D12Resource *dstResource,
										  uint32_t dstSubresource,
										  D3D12Resource *srcResource,
										  uint32_t srcSubresource,
										  DXGI_FORMAT format)
{
	commandList_->ResolveSubresource(dstResource->GetResource(),
									 dstSubresource,
									 srcResource->GetResource(),
									 srcSubresource,
									 format);
}

void D3D12CommandList::SetDescriptorHeaps(const D3D12DescriptorHeap *heaps)
{
	ID3D12DescriptorHeap *nativeHeaps[2] = { heaps->GetResourceDescriptorHeap(), heaps->GetSamplerDescriptorHeap() };
	commandList_->SetDescriptorHeaps(2, nativeHeaps);
}

void D3D12CommandList::SetGraphicsPipeline(D3D12GraphicsPipeline *pipeline)
{
	commandList_->SetPipelineState(pipeline->GetNativePipelineState().Get());
	commandList_->SetGraphicsRootSignature(pipeline->GetNativeRootSignature().Get());
}

void D3D12CommandList::SetComputePipeline(D3D12ComputePipeline *pipeline)
{
	commandList_->SetPipelineState(pipeline->GetNativePipelineState().Get());
	commandList_->SetComputeRootSignature(pipeline->GetNativeRootSignature().Get());
}

void D3D12CommandList::Reset()
{
	HRESULT hr = commandList_->Reset(commandAllocator_->GetNativeCommandAllocator().Get(), nullptr);
	if (FAILED(hr))
	{
		throw D3D12Exception("Failed to reset D3D12 command list.", hr);
	}
}

void D3D12CommandList::Close()
{
	HRESULT hr = commandList_->Close();
	if (FAILED(hr))
	{
		throw D3D12Exception("Failed to close D3D12 command list.", hr);
	}
}

void D3D12CommandList::TransitionBarrier(uint32_t numBarriers, const ResourceBarrier *barriers)
{
	commandList_->ResourceBarrier(numBarriers, barriers);
}

void D3D12CommandList::SetBarriers(uint32_t numBarriers, const D3D12_BARRIER_GROUP *barrierGroups)
{
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList7> commandList7_;
	HRESULT hr = commandList_.As(&commandList7_);
	if (FAILED(hr))
	{
		throw D3D12Exception("Failed to query ID3D12GraphicsCommandList7 interface.", hr);
	}
	commandList7_->Barrier(numBarriers, barrierGroups);
}

void D3D12CommandList::SetTextureBarrier(D3D12Resource *resource,
										 uint32_t mipLevel,
										 D3D12_BARRIER_LAYOUT layoutBefore,
										 D3D12_BARRIER_SYNC syncBefore,
										 D3D12_BARRIER_ACCESS accessBefore,
										 D3D12_BARRIER_LAYOUT layoutAfter,
										 D3D12_BARRIER_SYNC syncAfter,
										 D3D12_BARRIER_ACCESS accessAfter)
{
	D3D12_TEXTURE_BARRIER b = {};
	b.SyncBefore = syncBefore;
	b.SyncAfter = syncAfter;
	b.AccessBefore = accessBefore;
	b.AccessAfter = accessAfter;
	b.LayoutBefore = layoutBefore;
	b.LayoutAfter = layoutAfter;
	b.pResource = resource->GetResource();
	b.Subresources = CD3DX12_BARRIER_SUBRESOURCE_RANGE(mipLevel, 1, 0, 1, 0, 1);
	b.Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE;
	D3D12_BARRIER_GROUP g = {};
	g.Type = D3D12_BARRIER_TYPE_TEXTURE;
	g.NumBarriers = 1;
	g.pTextureBarriers = &b;
	SetBarriers(1, &g);
}

void D3D12CommandList::SetBufferBarrier(D3D12Resource *resource,
										D3D12_BARRIER_SYNC syncBefore,
										D3D12_BARRIER_ACCESS accessBefore,
										D3D12_BARRIER_SYNC syncAfter,
										D3D12_BARRIER_ACCESS accessAfter)
{
	D3D12_BUFFER_BARRIER b = {};
	b.SyncBefore = syncBefore;
	b.SyncAfter = syncAfter;
	b.AccessBefore = accessBefore;
	b.AccessAfter = accessAfter;
	b.pResource = resource->GetResource();
	b.Offset = 0;
	b.Size = UINT64_MAX;
	D3D12_BARRIER_GROUP g = {};
	g.Type = D3D12_BARRIER_TYPE_BUFFER;
	g.NumBarriers = 1;
	g.pBufferBarriers = &b;
	SetBarriers(1, &g);
}

void D3D12CommandList::SetGlobalBarrier(D3D12_BARRIER_SYNC syncBefore,
										D3D12_BARRIER_ACCESS accessBefore,
										D3D12_BARRIER_SYNC syncAfter,
										D3D12_BARRIER_ACCESS accessAfter)
{
	D3D12_GLOBAL_BARRIER b = {};
	b.SyncBefore = syncBefore;
	b.SyncAfter = syncAfter;
	b.AccessBefore = accessBefore;
	b.AccessAfter = accessAfter;
	D3D12_BARRIER_GROUP g = {};
	g.Type = D3D12_BARRIER_TYPE_GLOBAL;
	g.NumBarriers = 1;
	g.pGlobalBarriers = &b;
	SetBarriers(1, &g);
}

void D3D12CommandList::SetRenderTargets(uint32_t numRTVs,
										D3D12Resource *const *rtvResources,
										D3D12Resource *dsvResource,
										bool readOnlyDSV)
{
	ID3D12Resource *nativeRTVResources[8] = {};
	for (uint32_t i = 0; i < numRTVs; ++i)
	{
		if (rtvResources[i] == nullptr)
		{
			break;
		}
		nativeRTVResources[i] = rtvResources[i]->GetResource();
	}

	renderTargetHelper_.OMSetRenderTargets(commandList_.Get(),
										   numRTVs,
										   nativeRTVResources,
										   nullptr,
										   dsvResource ? dsvResource->GetResource() : nullptr,
										   nullptr,
										   readOnlyDSV);
}

void D3D12CommandList::ClearRenderTarget(D3D12Resource *rtvResource,
										 const float clearColour[4],
										 uint32_t numRects,
										 const D3D12Rect *rects)
{
	renderTargetHelper_.ClearRenderTargetView(commandList_.Get(),
											  rtvResource->GetResource(),
											  nullptr,
											  clearColour,
											  numRects,
											  rects);
}

void D3D12CommandList::ClearDepthStencil(D3D12Resource *dsvResource,
										 D3D12_CLEAR_FLAGS clearFlags,
										 float depth,
										 uint8_t stencil,
										 uint32_t numRects,
										 const D3D12Rect *rects)
{
	renderTargetHelper_.ClearDepthStencilView(commandList_.Get(),
											  dsvResource->GetResource(),
											  nullptr,
											  clearFlags,
											  depth,
											  stencil,
											  numRects,
											  rects);
}

void D3D12CommandList::ClearUnorderedAccessViewFloat(const D3D12DescriptorHeap *gpuDescriptorHeap,
													  DescriptorHandle gpuUavHandle,
													  const D3D12DescriptorHeap *cpuDescriptorHeap,
													  DescriptorHandle cpuUavHandle,
													  D3D12Resource *uavResource,
													  const float values[4])
{
	D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = gpuDescriptorHeap->GetGPUDescriptorHandle(gpuUavHandle);
	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = cpuDescriptorHeap->GetCPUDescriptorHandle(cpuUavHandle);
	commandList_->ClearUnorderedAccessViewFloat(gpuHandle, cpuHandle, uavResource->GetResource(), values, 0, nullptr);
}

void D3D12CommandList::ClearUnorderedAccessViewUint(const D3D12DescriptorHeap *gpuDescriptorHeap,
													 DescriptorHandle gpuUavHandle,
													 const D3D12DescriptorHeap *cpuDescriptorHeap,
													 DescriptorHandle cpuUavHandle,
													 D3D12Resource *uavResource,
													 const uint32_t values[4])
{
	D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = gpuDescriptorHeap->GetGPUDescriptorHandle(gpuUavHandle);
	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = cpuDescriptorHeap->GetCPUDescriptorHandle(cpuUavHandle);
	commandList_->ClearUnorderedAccessViewUint(gpuHandle, cpuHandle, uavResource->GetResource(), values, 0, nullptr);
}

void D3D12CommandList::DispatchMesh(uint32_t threadGroupCountX, uint32_t threadGroupCountY, uint32_t threadGroupCountZ)
{
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList6> commandList6_;
	HRESULT hr = commandList_.As(&commandList6_);
	if (FAILED(hr))
	{
		throw D3D12Exception("Failed to query ID3D12GraphicsCommandList6 interface.", hr);
	}
	commandList6_->DispatchMesh(threadGroupCountX, threadGroupCountY, threadGroupCountZ);
}

void D3D12CommandList::Dispatch(uint32_t threadGroupCountX, uint32_t threadGroupCountY, uint32_t threadGroupCountZ)
{
	commandList_->Dispatch(threadGroupCountX, threadGroupCountY, threadGroupCountZ);
}

void D3D12CommandList::DrawInstanced(uint32_t vertexCountPerInstance,
									 uint32_t instanceCount,
									 uint32_t startVertexLocation,
									 uint32_t startInstanceLocation)
{
	commandList_->DrawInstanced(vertexCountPerInstance, instanceCount, startVertexLocation, startInstanceLocation);
}

void D3D12CommandList::SetGraphicsDescriptorTable(uint32_t rootParameterIndex, GPUDescriptorHandle gpuHandle)
{
	D3D12_GPU_DESCRIPTOR_HANDLE handle;
	handle.ptr = gpuHandle.ptr;
	commandList_->SetGraphicsRootDescriptorTable(rootParameterIndex, handle);
}

void D3D12CommandList::SetComputeDescriptorTable(uint32_t rootParameterIndex, GPUDescriptorHandle gpuHandle)
{
	D3D12_GPU_DESCRIPTOR_HANDLE handle;
	handle.ptr = gpuHandle.ptr;
	commandList_->SetComputeRootDescriptorTable(rootParameterIndex, handle);
}

void D3D12CommandList::SetCompute32BitConstants(uint32_t rootParameterIndex,
												uint32_t numConstants,
												const void *pConstants,
												uint32_t destOffsetIn32BitValues)
{
	commandList_->SetComputeRoot32BitConstants(rootParameterIndex, numConstants, pConstants, destOffsetIn32BitValues);
}

void D3D12CommandList::SetGraphics32BitConstants(uint32_t rootParameterIndex,
												 uint32_t numConstants,
												 const void *pConstants,
												 uint32_t destOffsetIn32BitValues)
{
	commandList_->SetGraphicsRoot32BitConstants(rootParameterIndex, numConstants, pConstants, destOffsetIn32BitValues);
}

// Just aliases for clarity
void D3D12CommandList::BeginCommandList()
{
	Reset();
}

void D3D12CommandList::EndCommandList()
{
	Close();
}

void D3D12CommandList::SetViewport(const D3D12ViewPort &viewport)
{
	commandList_->RSSetViewports(1, &viewport);
}

void D3D12CommandList::SetScissorRect(const D3D12Rect &rect)
{
	commandList_->RSSetScissorRects(1, &rect);
}

void D3D12CommandList::SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY topology)
{
	commandList_->IASetPrimitiveTopology(topology);
}

void D3D12CommandList::ExecuteIndirect(ID3D12CommandSignature *commandSignature,
									   UINT maxDrawCount,
									   ID3D12Resource *argumentBuffer,
									   UINT argumentBufferOffset,
									   ID3D12Resource *countBuffer,
									   UINT countBufferOffset)
{
	commandList_->ExecuteIndirect(commandSignature,
								  maxDrawCount,
								  argumentBuffer,
								  argumentBufferOffset,
								  countBuffer,
								  countBufferOffset);
}

void D3D12CommandList::BuildRaytracingAccelerationStructure(
	const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC &desc)
{
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> commandList4;
	HRESULT hr = commandList_.As(&commandList4);
	if (FAILED(hr))
	{
		throw D3D12Exception("Failed to query ID3D12GraphicsCommandList4 interface.", hr);
	}
	commandList4->BuildRaytracingAccelerationStructure(&desc, 0, nullptr);
}

void D3D12CommandList::EmitUAVBarrier(D3D12Resource * /*resource*/)
{
	D3D12_GLOBAL_BARRIER globalBarrier = {};
	globalBarrier.SyncBefore   = D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE;
	globalBarrier.SyncAfter    = D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE
	                             | D3D12_BARRIER_SYNC_ALL_SHADING;
	globalBarrier.AccessBefore = D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_WRITE;
	globalBarrier.AccessAfter  = D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_READ;

	D3D12_BARRIER_GROUP group = {};
	group.Type            = D3D12_BARRIER_TYPE_GLOBAL;
	group.NumBarriers     = 1;
	group.pGlobalBarriers = &globalBarrier;

	SetBarriers(1, &group);
}

void D3D12CommandList::WriteBreadcrumb(uint64_t destGpuVA, uint32_t value, D3D12_WRITEBUFFERIMMEDIATE_MODE mode)
{
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> cmdList2;
	if (SUCCEEDED(commandList_.As(&cmdList2)))
	{
		D3D12_WRITEBUFFERIMMEDIATE_PARAMETER param = { destGpuVA, value };
		cmdList2->WriteBufferImmediate(1, &param, &mode);
	}
}

