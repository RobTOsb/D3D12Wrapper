#pragma once

#include "pch.h"

#include "D3D12CommandAllocator.h"

#include "HelperClasses/RenderTargetHelper.h"

class D3D12DescriptorHeap;
class D3D12GraphicsPipeline;
class D3D12ComputePipeline;
class D3D12Resource;

class D3D12CommandList
{
public:
	D3D12CommandList(Microsoft::WRL::ComPtr<ID3D12Device> device,
					 D3D12CommandAllocator *allocator,
					 D3D12CommandListType type);
	D3D12CommandList(Microsoft::WRL::ComPtr<ID3D12Device> device, D3D12CommandListType type);
	~D3D12CommandList() = default;

	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> GetNativeCommandList() const
	{
		return commandList_;
	}

	void SetName(const char *name);
	void BeginCommandList();
	void EndCommandList();
	void Reset();
	void Close();
	void SetDescriptorHeaps(const D3D12DescriptorHeap *heaps);
	void TransitionBarrier(uint32_t numBarriers, const ResourceBarrier *barriers);
	void SetBarriers(uint32_t numBarriers, const D3D12_BARRIER_GROUP *barrierGroups);
	void SetTextureBarrier(D3D12Resource *resource,
						   uint32_t mipLevel,
						   D3D12_BARRIER_LAYOUT layoutBefore,
						   D3D12_BARRIER_SYNC syncBefore,
						   D3D12_BARRIER_ACCESS accessBefore,
						   D3D12_BARRIER_LAYOUT layoutAfter,
						   D3D12_BARRIER_SYNC syncAfter,
						   D3D12_BARRIER_ACCESS accessAfter);

	void SetBufferBarrier(D3D12Resource *resource,
						  D3D12_BARRIER_SYNC syncBefore,
						  D3D12_BARRIER_ACCESS accessBefore,
						  D3D12_BARRIER_SYNC syncAfter,
						  D3D12_BARRIER_ACCESS accessAfter);

	void SetGlobalBarrier(D3D12_BARRIER_SYNC syncBefore,
						  D3D12_BARRIER_ACCESS accessBefore,
						  D3D12_BARRIER_SYNC syncAfter,
						  D3D12_BARRIER_ACCESS accessAfter);

	void SetRenderTargets(uint32_t numRTVs,
						  D3D12Resource *const *rtvResources,
						  D3D12Resource *dsvResource,
						  bool readOnlyDSV = false);
	void CopyResource(D3D12Resource *dstResource, D3D12Resource *srcResource);
	void CopyBufferRegion(D3D12Resource *dstResource,
						  uint64_t dstOffset,
						  D3D12Resource *srcResource,
						  uint64_t srcOffset,
						  uint64_t numBytes);
	void CopyTextureRegion(D3D12Resource *dstResource,
						   uint32_t dstX,
						   uint32_t dstY,
						   uint32_t dstZ,
						   D3D12Resource *srcResource,
						   const D3D12Rect *srcRect = nullptr);
	void CopyBufferToTextureRegion(D3D12Resource *dstTexture,
								   uint32_t dstX,
								   uint32_t dstY,
								   uint32_t dstZ,
								   D3D12Resource *srcBuffer,
								   const D3D12_BOX *srcBox,
								   uint32_t srcRowPitch);
	void ResolveSubresource(D3D12Resource *dstResource,
							uint32_t dstSubresource,
							D3D12Resource *srcResource,
							uint32_t srcSubresource,
							DXGI_FORMAT format);

	void ClearRenderTarget(D3D12Resource *rtvResource,
						   const float clearColour[4],
						   uint32_t numRects = 0,
						   const D3D12Rect *rects = nullptr);

	void ClearDepthStencil(D3D12Resource *dsvResource,
						   D3D12_CLEAR_FLAGS clearFlags,
						   float depth,
						   uint8_t stencil,
						   uint32_t numRects = 0,
						   const D3D12Rect *rects = nullptr);
	void ClearUnorderedAccessViewFloat(const D3D12DescriptorHeap *gpuDescriptorHeap,
								   DescriptorHandle gpuUavHandle,
								   const D3D12DescriptorHeap *cpuDescriptorHeap,
								   DescriptorHandle cpuUavHandle,
								   D3D12Resource *uavResource,
								   const float values[4]);

	void ClearUnorderedAccessViewUint(const D3D12DescriptorHeap *gpuDescriptorHeap,
								  DescriptorHandle gpuUavHandle,
								  const D3D12DescriptorHeap *cpuDescriptorHeap,
								  DescriptorHandle cpuUavHandle,
								  D3D12Resource *uavResource,
								  const uint32_t values[4]);
								  
	void SetGraphicsPipeline(D3D12GraphicsPipeline *pipeline);
	void SetComputePipeline(D3D12ComputePipeline *pipeline);
	void SetGraphicsDescriptorTable(uint32_t rootParameterIndex, GPUDescriptorHandle gpuHandle);
	void SetGraphics32BitConstants(uint32_t rootParameterIndex,
								   uint32_t numConstants,
								   const void *pConstants,
								   uint32_t destOffsetIn32BitValues);

	void SetComputeDescriptorTable(uint32_t rootParameterIndex, GPUDescriptorHandle gpuHandle);
	void SetCompute32BitConstants(uint32_t rootParameterIndex,
								  uint32_t numConstants,
								  const void *pConstants,
								  uint32_t destOffsetIn32BitValues);
	void SetViewport(const D3D12ViewPort &viewport);
	void SetScissorRect(const D3D12Rect &rect);
	void SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY topology);
	void DispatchMesh(uint32_t threadGroupCountX, uint32_t threadGroupCountY, uint32_t threadGroupCountZ);
	void Dispatch(uint32_t threadGroupCountX, uint32_t threadGroupCountY, uint32_t threadGroupCountZ);
	void DrawInstanced(uint32_t vertexCountPerInstance,
					   uint32_t instanceCount,
					   uint32_t startVertexLocation,
					   uint32_t startInstanceLocation);
	void ExecuteIndirect(ID3D12CommandSignature *commandSignature,
						 UINT maxDrawCount,
						 ID3D12Resource *argumentBuffer,
						 UINT argumentBufferOffset = 0,
						 ID3D12Resource *countBuffer = nullptr,
						 UINT countBufferOffset = 0);
	void BuildRaytracingAccelerationStructure(const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC &desc);
	void EmitUAVBarrier(D3D12Resource *resource = nullptr);

	void WriteBreadcrumb(uint64_t destGpuVA,
						 uint32_t value,
						 D3D12_WRITEBUFFERIMMEDIATE_MODE mode = D3D12_WRITEBUFFERIMMEDIATE_MODE_MARKER_IN);

private:
	std::unique_ptr<D3D12CommandAllocator> ownedAllocator_; // non-null only for self-contained lists
	D3D12CommandAllocator *commandAllocator_;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_;
	RenderTargetHelper renderTargetHelper_;
	D3D12CommandListType type_;
};
