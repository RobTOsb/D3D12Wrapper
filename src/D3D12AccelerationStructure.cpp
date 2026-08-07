#include "D3D12AccelerationStructure.h"

#include <cstring>

#include "D3D12Buffer.h"
#include "D3D12CommandList.h"
#include "D3D12Device.h"
#include "D3D12Exception.h"
#include "D3D12Resource.h"

D3D12BottomLevelAS::D3D12BottomLevelAS(D3D12Device *device, const BLASGeometryDesc &geometry)
	: device_(device)
{
	geometryDesc_ = {};
	geometryDesc_.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
	geometryDesc_.Flags =
			geometry.isOpaque ? D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE : D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
	geometryDesc_.Triangles.Transform3x4 = 0;
	geometryDesc_.Triangles.IndexFormat = geometry.indexFormat;
	geometryDesc_.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
	geometryDesc_.Triangles.IndexCount = geometry.indexCount;
	geometryDesc_.Triangles.VertexCount = geometry.vertexCount;
	geometryDesc_.Triangles.IndexBuffer = geometry.indexBufferVA;
	geometryDesc_.Triangles.VertexBuffer.StartAddress = geometry.vertexBufferVA;
	geometryDesc_.Triangles.VertexBuffer.StrideInBytes = geometry.vertexStride;

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
	inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
	inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	inputs.NumDescs = 1;
	inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	inputs.pGeometryDescs = &geometryDesc_;

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo = {};
	device_->GetRaytracingAccelerationStructurePrebuildInfo(inputs, prebuildInfo);

	resultBuffer_ = device_->CreateAccelerationStructureBuffer(prebuildInfo.ResultDataMaxSizeInBytes, false);
	scratchBuffer_ = device_->CreateAccelerationStructureBuffer(prebuildInfo.ScratchDataSizeInBytes, true);
}

D3D12BottomLevelAS::~D3D12BottomLevelAS() = default;

void D3D12BottomLevelAS::Build(D3D12CommandList *cmdList)
{
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc = {};
	desc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
	desc.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	desc.Inputs.NumDescs = 1;
	desc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	desc.Inputs.pGeometryDescs = &geometryDesc_;
	desc.DestAccelerationStructureData = resultBuffer_->GetResource()->GetGPUVirtualAddress();
	desc.ScratchAccelerationStructureData = scratchBuffer_->GetResource()->GetGPUVirtualAddress();

	cmdList->BuildRaytracingAccelerationStructure(desc);
}

D3D12_GPU_VIRTUAL_ADDRESS D3D12BottomLevelAS::GetGPUVirtualAddress() const
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

	instanceUpload->Map();
	auto *dst = static_cast<D3D12_RAYTRACING_INSTANCE_DESC *>(instanceUpload->GetMappedData());
	for (uint32_t i = 0; i < instanceCount_; ++i)
	{
		const TLASInstanceDesc &src = instances[i];
		D3D12_RAYTRACING_INSTANCE_DESC &out = dst[i];
		std::memcpy(out.Transform, src.transform, sizeof(out.Transform));
		out.InstanceID = src.instanceID;
		out.InstanceMask = src.instanceMask;
		out.InstanceContributionToHitGroupIndex = src.hitGroupIndex;
		out.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
		out.AccelerationStructure = src.blasAddress;
	}
	instanceUpload->Unmap();

	instanceBuffer_ = std::move(instanceUpload);
}

D3D12TopLevelAS::~D3D12TopLevelAS() = default;

void D3D12TopLevelAS::Build(D3D12CommandList *cmdList)
{
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc = {};
	desc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
	desc.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	desc.Inputs.NumDescs = instanceCount_;
	desc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	desc.Inputs.InstanceDescs = instanceBuffer_->GetResource()->GetGPUVirtualAddress();
	desc.DestAccelerationStructureData = resultBuffer_->GetResource()->GetGPUVirtualAddress();
	desc.ScratchAccelerationStructureData = scratchBuffer_->GetResource()->GetGPUVirtualAddress();

	cmdList->BuildRaytracingAccelerationStructure(desc);
}

D3D12_GPU_VIRTUAL_ADDRESS D3D12TopLevelAS::GetGPUVirtualAddress() const
{
	return resultBuffer_->GetResource()->GetGPUVirtualAddress();
}
