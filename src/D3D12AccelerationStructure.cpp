#include "D3D12AccelerationStructure.h"

#include <algorithm>
#include <cstring>

#include "D3D12Buffer.h"
#include "D3D12CommandList.h"
#include "D3D12Device.h"
#include "D3D12Exception.h"
#include "D3D12Resource.h"

namespace
{
	constexpr D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS kBLASBuildFlags =
			D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE |
			D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION;

	void WriteInstanceDescs(std::span<const TLASInstanceDesc> instances, D3D12_RAYTRACING_INSTANCE_DESC *dst)
	{
		for (size_t i = 0; i < instances.size(); ++i)
		{
			const TLASInstanceDesc &src = instances[i];
			D3D12_RAYTRACING_INSTANCE_DESC &out = dst[i];
			std::memcpy(out.Transform, src.transform, sizeof(out.Transform));
			out.InstanceID = src.instanceID;
			out.InstanceMask = src.instanceMask;
			out.InstanceContributionToHitGroupIndex = src.hitGroupIndex;
			out.Flags = src.flags;
			out.AccelerationStructure = src.blasAddress;
		}
	}
}

D3D12BottomLevelAS::D3D12BottomLevelAS(D3D12Device *device, const BLASGeometryDesc &geometry)
	: device_(device)
{
	geometryDesc_ = {};
	geometryDesc_.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
	geometryDesc_.Flags =
			geometry.isOpaque ? D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE : D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
	if (geometry.noDuplicateAnyHit)
	{
		geometryDesc_.Flags |= D3D12_RAYTRACING_GEOMETRY_FLAG_NO_DUPLICATE_ANYHIT_INVOCATION;
	}
	
	geometryDesc_.Triangles.Transform3x4 = 0;
	geometryDesc_.Triangles.IndexFormat = geometry.indexFormat;
	geometryDesc_.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
	geometryDesc_.Triangles.IndexCount = geometry.indexCount;
	geometryDesc_.Triangles.VertexCount = geometry.vertexCount;
	geometryDesc_.Triangles.IndexBuffer = geometry.indexBufferVA;
	geometryDesc_.Triangles.VertexBuffer.StartAddress = geometry.vertexBufferVA;
	geometryDesc_.Triangles.VertexBuffer.StrideInBytes = geometry.vertexStride;

	// The same triangles behind a pointer, next to their micromap indices. The opaque flag above only
	// matters where the micromaps are disabled.
	if (geometry.opacityMicromapIndexBufferVA != 0)
	{
		ommTriangles_ = geometryDesc_.Triangles;
		ommLinkage_.OpacityMicromapIndexBuffer.StartAddress = geometry.opacityMicromapIndexBufferVA;
		ommLinkage_.OpacityMicromapIndexBuffer.StrideInBytes = sizeof(uint32_t);
		ommLinkage_.OpacityMicromapIndexFormat = DXGI_FORMAT_R32_UINT;
		ommLinkage_.OpacityMicromapBaseLocation = 0;
		ommLinkage_.OpacityMicromapArray = geometry.opacityMicromapArrayVA;
		geometryDesc_.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_OMM_TRIANGLES;
		geometryDesc_.OmmTriangles.pTriangles = &ommTriangles_;
		geometryDesc_.OmmTriangles.pOmmLinkage = &ommLinkage_;
	}

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
	inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
	inputs.Flags = kBLASBuildFlags;
	inputs.NumDescs = 1;
	inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	inputs.pGeometryDescs = &geometryDesc_;

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo = {};
	device_->GetRaytracingAccelerationStructurePrebuildInfo(inputs, prebuildInfo);

	resultBuffer_ = device_->CreateAccelerationStructureBuffer(prebuildInfo.ResultDataMaxSizeInBytes, false);
	resultSize_ = prebuildInfo.ResultDataMaxSizeInBytes;
	scratchSize_ = prebuildInfo.ScratchDataSizeInBytes;
}

D3D12BottomLevelAS::~D3D12BottomLevelAS() = default;

void D3D12BottomLevelAS::Build(D3D12CommandList *cmdList)
{
	if (!scratchBuffer_)
	{
		scratchBuffer_ = device_->CreateAccelerationStructureBuffer(scratchSize_, true);
	}
	Build(cmdList, scratchBuffer_->GetResource()->GetGPUVirtualAddress(), 0);
}

void D3D12BottomLevelAS::Build(D3D12CommandList *cmdList,
							   D3D12_GPU_VIRTUAL_ADDRESS scratch,
							   D3D12_GPU_VIRTUAL_ADDRESS compactedSizeAddress)
{
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc = {};
	desc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
	desc.Inputs.Flags = kBLASBuildFlags;
	desc.Inputs.NumDescs = 1;
	desc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	desc.Inputs.pGeometryDescs = &geometryDesc_;
	desc.DestAccelerationStructureData = resultBuffer_->GetResource()->GetGPUVirtualAddress();
	desc.ScratchAccelerationStructureData = scratch;

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC postbuild = {};
	postbuild.DestBuffer = compactedSizeAddress;
	postbuild.InfoType = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_COMPACTED_SIZE;
	cmdList->BuildRaytracingAccelerationStructure(desc, compactedSizeAddress != 0 ? 1 : 0, &postbuild);
}

void D3D12BottomLevelAS::Compact(D3D12CommandList *cmdList, uint64_t compactedSize)
{
	std::unique_ptr<D3D12Resource> compacted = device_->CreateAccelerationStructureBuffer(compactedSize, false);
	cmdList->CopyRaytracingAccelerationStructure(compacted->GetResource()->GetGPUVirtualAddress(),
												 resultBuffer_->GetResource()->GetGPUVirtualAddress(),
												 D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_COMPACT);
	uncompactedBuffer_ = std::move(resultBuffer_);
	resultBuffer_ = std::move(compacted);
	resultSize_ = compactedSize;
}

void D3D12BottomLevelAS::ReleaseUncompacted()
{
	uncompactedBuffer_.reset();
}

D3D12_GPU_VIRTUAL_ADDRESS D3D12BottomLevelAS::GetGPUVirtualAddress() const
{
	return resultBuffer_->GetResource()->GetGPUVirtualAddress();
}

D3D12OpacityMicromapArray::D3D12OpacityMicromapArray(D3D12Device *device,
													 std::span<const uint8_t> data,
													 std::span<const D3D12_RAYTRACING_OPACITY_MICROMAP_DESC> micromaps)
	: device_(device)
{
	// Buffers start 64 KB aligned, beyond the 128 bytes the input data needs.
	auto upload = [&](const void *source, uint64_t size, const wchar_t *name)
	{
		D3D12_RESOURCE_DESC1 desc = {};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		desc.Width = std::max<uint64_t>(size, 4);
		desc.Height = 1;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.Format = DXGI_FORMAT_UNKNOWN;
		desc.SampleDesc = { 1, 0 };
		desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		std::unique_ptr<D3D12Buffer> buffer =
				device_->CreateBuffer(desc, D3D12_HEAP_TYPE_UPLOAD, D3D12_BARRIER_LAYOUT_UNDEFINED);
		buffer->SetName(name);
		buffer->Map();
		std::memcpy(buffer->GetMappedData(), source, size);
		buffer->Unmap();
		return buffer;
	};
	dataBuffer_ = upload(data.data(), data.size(), L"OpacityMicromapData");
	descBuffer_ = upload(micromaps.data(), micromaps.size_bytes(), L"OpacityMicromapDescs");

	// How many OMMs there are of each level and format, from which the driver sizes the array.
	for (const D3D12_RAYTRACING_OPACITY_MICROMAP_DESC &micromap: micromaps)
	{
		auto entry = std::find_if(histogram_.begin(),
								  histogram_.end(),
								  [&](const D3D12_RAYTRACING_OPACITY_MICROMAP_HISTOGRAM_ENTRY &e) {
									  return e.SubdivisionLevel == micromap.SubdivisionLevel && e.Format == micromap.Format;
								  });
		if (entry == histogram_.end())
		{
			histogram_.push_back({ 0, micromap.SubdivisionLevel, micromap.Format });
			entry = histogram_.end() - 1;
		}
		++entry->Count;
	}

	arrayDesc_.NumOmmHistogramEntries = static_cast<UINT>(histogram_.size());
	arrayDesc_.pOmmHistogram = histogram_.data();
	arrayDesc_.InputBuffer = dataBuffer_->GetResource()->GetGPUVirtualAddress();
	arrayDesc_.PerOmmDescs.StartAddress = descBuffer_->GetResource()->GetGPUVirtualAddress();
	arrayDesc_.PerOmmDescs.StrideInBytes = sizeof(D3D12_RAYTRACING_OPACITY_MICROMAP_DESC);

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo = {};
	device_->GetRaytracingAccelerationStructurePrebuildInfo(Inputs(), prebuildInfo);

	resultBuffer_ = device_->CreateAccelerationStructureBuffer(prebuildInfo.ResultDataMaxSizeInBytes, false);
	resultSize_ = prebuildInfo.ResultDataMaxSizeInBytes;
	scratchSize_ = prebuildInfo.ScratchDataSizeInBytes;
}

D3D12OpacityMicromapArray::~D3D12OpacityMicromapArray() = default;

D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS D3D12OpacityMicromapArray::Inputs() const
{
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
	inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_OPACITY_MICROMAP_ARRAY;
	inputs.Flags = kBLASBuildFlags;
	inputs.NumDescs = 1;
	inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	inputs.pOpacityMicromapArrayDesc = &arrayDesc_;
	return inputs;
}

void D3D12OpacityMicromapArray::Build(D3D12CommandList *cmdList,
									  D3D12_GPU_VIRTUAL_ADDRESS scratch,
									  D3D12_GPU_VIRTUAL_ADDRESS compactedSizeAddress)
{
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc = {};
	desc.Inputs = Inputs();
	desc.DestAccelerationStructureData = resultBuffer_->GetResource()->GetGPUVirtualAddress();
	desc.ScratchAccelerationStructureData = scratch;

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC postbuild = {};
	postbuild.DestBuffer = compactedSizeAddress;
	postbuild.InfoType = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_COMPACTED_SIZE;
	cmdList->BuildRaytracingAccelerationStructure(desc, compactedSizeAddress != 0 ? 1 : 0, &postbuild);
}

void D3D12OpacityMicromapArray::Compact(D3D12CommandList *cmdList, uint64_t compactedSize)
{
	std::unique_ptr<D3D12Resource> compacted = device_->CreateAccelerationStructureBuffer(compactedSize, false);
	cmdList->CopyRaytracingAccelerationStructure(compacted->GetResource()->GetGPUVirtualAddress(),
												 resultBuffer_->GetResource()->GetGPUVirtualAddress(),
												 D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_COMPACT);
	uncompactedBuffer_ = std::move(resultBuffer_);
	resultBuffer_ = std::move(compacted);
	resultSize_ = compactedSize;
}

void D3D12OpacityMicromapArray::ReleaseBuildResources()
{
	uncompactedBuffer_.reset();
	dataBuffer_.reset();
	descBuffer_.reset();
}

D3D12_GPU_VIRTUAL_ADDRESS D3D12OpacityMicromapArray::GetGPUVirtualAddress() const
{
	return resultBuffer_->GetResource()->GetGPUVirtualAddress();
}

D3D12TopLevelAS::D3D12TopLevelAS(D3D12Device *device, std::span<const TLASInstanceDesc> instances)
	: device_(device)
	, instanceCount_(static_cast<uint32_t>(instances.size()))
{
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
	inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
	inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	inputs.NumDescs = instanceCount_;
	inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo = {};
	device_->GetRaytracingAccelerationStructurePrebuildInfo(inputs, prebuildInfo);

	resultBuffer_ = device_->CreateAccelerationStructureBuffer(prebuildInfo.ResultDataMaxSizeInBytes, false);
	scratchBuffer_ = device_->CreateAccelerationStructureBuffer(prebuildInfo.ScratchDataSizeInBytes, true);

	instanceBuffer_ = CreateInstanceBuffer(instances);
	buildInstances_ = instanceBuffer_.get();
}

std::unique_ptr<D3D12Buffer> D3D12TopLevelAS::CreateInstanceBuffer(std::span<const TLASInstanceDesc> instances) const
{
	const uint64_t instanceBufferSize = sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * instanceCount_;

	D3D12_RESOURCE_DESC1 instanceDesc = {};
	instanceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	instanceDesc.Width = instanceBufferSize;
	instanceDesc.Height = 1;
	instanceDesc.DepthOrArraySize = 1;
	instanceDesc.MipLevels = 1;
	instanceDesc.Format = DXGI_FORMAT_UNKNOWN;
	instanceDesc.SampleDesc = { 1, 0 };
	instanceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	instanceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	std::unique_ptr<D3D12Buffer> instanceUpload =
			device_->CreateBuffer(instanceDesc, D3D12_HEAP_TYPE_UPLOAD, D3D12_BARRIER_LAYOUT_UNDEFINED);
	instanceUpload->SetName(L"TLASInstanceBuffer");

	// Kept mapped, so SetInstances can rewrite it in place.
	instanceUpload->Map();
	WriteInstanceDescs(instances, static_cast<D3D12_RAYTRACING_INSTANCE_DESC *>(instanceUpload->GetMappedData()));
	return instanceUpload;
}

void D3D12TopLevelAS::SetInstances(std::span<const TLASInstanceDesc> instances, uint32_t frameIndex, uint32_t frameCount)
{
	if (instances.size() != instanceCount_)
	{
		throw std::runtime_error("D3D12TopLevelAS::SetInstances: the instance count differs from the one at creation.");
	}
	frameInstanceBuffers_.resize(std::max<size_t>(frameInstanceBuffers_.size(), frameCount));
	std::unique_ptr<D3D12Buffer> &buffer = frameInstanceBuffers_[frameIndex];
	if (buffer == nullptr)
	{
		buffer = CreateInstanceBuffer(instances);
	}
	else
	{
		WriteInstanceDescs(instances, static_cast<D3D12_RAYTRACING_INSTANCE_DESC *>(buffer->GetMappedData()));
	}
	buildInstances_ = buffer.get();
}

D3D12TopLevelAS::~D3D12TopLevelAS() = default;

void D3D12TopLevelAS::Build(D3D12CommandList *cmdList)
{
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc = {};
	desc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
	desc.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	desc.Inputs.NumDescs = instanceCount_;
	desc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	desc.Inputs.InstanceDescs = buildInstances_->GetResource()->GetGPUVirtualAddress();
	desc.DestAccelerationStructureData = resultBuffer_->GetResource()->GetGPUVirtualAddress();
	desc.ScratchAccelerationStructureData = scratchBuffer_->GetResource()->GetGPUVirtualAddress();

	cmdList->BuildRaytracingAccelerationStructure(desc);
}

D3D12_GPU_VIRTUAL_ADDRESS D3D12TopLevelAS::GetGPUVirtualAddress() const
{
	return resultBuffer_->GetResource()->GetGPUVirtualAddress();
}
