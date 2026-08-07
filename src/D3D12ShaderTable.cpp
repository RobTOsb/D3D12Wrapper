#include "D3D12ShaderTable.h"

#include <cstring>

#include "D3D12Buffer.h"
#include "D3D12Device.h"
#include "D3D12Exception.h"
#include "D3D12Pipeline.h"

namespace
{
	uint32_t AlignUp(uint32_t value, uint32_t alignment)
	{
		return (value + alignment - 1) & ~(alignment - 1);
	}
} // namespace

D3D12ShaderTable::D3D12ShaderTable(D3D12Device *device, D3D12RaytracingPipeline *pipeline) :
	device_(device), pipeline_(pipeline)
{
}

D3D12ShaderTable::~D3D12ShaderTable() = default;

uint32_t D3D12ShaderTable::AddRayGenRecord(const ShaderTableRecord &record)
{
	rayGenRecords_.push_back(PendingRecord{ record.exportName, record.localRootArguments });
	return static_cast<uint32_t>(rayGenRecords_.size() - 1);
}

uint32_t D3D12ShaderTable::AddMissRecord(const ShaderTableRecord &record)
{
	missRecords_.push_back(PendingRecord{ record.exportName, record.localRootArguments });
	return static_cast<uint32_t>(missRecords_.size() - 1);
}

uint32_t D3D12ShaderTable::AddHitGroupRecord(const ShaderTableRecord &record)
{
	hitGroupRecords_.push_back(PendingRecord{ record.exportName, record.localRootArguments });
	return static_cast<uint32_t>(hitGroupRecords_.size() - 1);
}

uint32_t D3D12ShaderTable::RecordSizeFor(const std::vector<PendingRecord> &records)
{
	uint32_t maxLocalArgs = 0;
	for (const auto &record: records)
	{
		maxLocalArgs = (std::max) (maxLocalArgs, static_cast<uint32_t>(record.localRootArguments.size()));
	}
	const uint32_t unaligned = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + maxLocalArgs;
	return AlignUp(unaligned, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
}

void D3D12ShaderTable::WriteRecords(uint8_t *dest,
									uint32_t recordStride,
									const std::vector<PendingRecord> &records,
									ID3D12StateObjectProperties *properties)
{
	for (size_t i = 0; i < records.size(); ++i)
	{
		const auto &record = records[i];
		void *identifier = properties->GetShaderIdentifier(record.exportName.c_str());
		if (!identifier)
		{
			throw D3D12Exception("Shader table export not found in raytracing state object", E_INVALIDARG);
		}

		uint8_t *dst = dest + i * recordStride;
		std::memcpy(dst, identifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
		if (!record.localRootArguments.empty())
		{
			std::memcpy(dst + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES,
						record.localRootArguments.data(),
						record.localRootArguments.size());
		}
	}
}

void D3D12ShaderTable::Build()
{
	if (rayGenRecords_.empty())
	{
		throw D3D12Exception("Shader table requires at least one raygen record", E_INVALIDARG);
	}

	const uint32_t rayGenStride = RecordSizeFor(rayGenRecords_);
	missStride_ = missRecords_.empty() ? 0 : RecordSizeFor(missRecords_);
	hitGroupStride_ = hitGroupRecords_.empty() ? 0 : RecordSizeFor(hitGroupRecords_);

	// Raygen sub-table holds exactly one record (DispatchRays takes no raygen count/stride).
	rayGenSizeInBytes_ = rayGenStride;
	missSizeInBytes_ = missStride_ * static_cast<uint32_t>(missRecords_.size());
	hitGroupSizeInBytes_ = hitGroupStride_ * static_cast<uint32_t>(hitGroupRecords_.size());

	const uint32_t rayGenTableSize = AlignUp(rayGenSizeInBytes_, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
	const uint32_t missTableOffset = rayGenTableSize;
	const uint32_t missTableSize = AlignUp(missSizeInBytes_, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
	const uint32_t hitGroupTableOffset = missTableOffset + missTableSize;
	const uint32_t hitGroupTableSize = AlignUp(hitGroupSizeInBytes_, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
	const uint32_t totalSize = hitGroupTableOffset + hitGroupTableSize;

	D3D12_RESOURCE_DESC1 desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = totalSize;
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.SampleDesc = { 1, 0 };
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	desc.Flags = D3D12_RESOURCE_FLAG_NONE;

	tableBuffer_ = device_->CreateBuffer(desc, D3D12_HEAP_TYPE_UPLOAD, D3D12_BARRIER_LAYOUT_UNDEFINED);
	tableBuffer_->SetName(L"ShaderTable");

	tableBuffer_->Map();
	uint8_t *base = static_cast<uint8_t *>(tableBuffer_->GetMappedData());
	std::memset(base, 0, totalSize);

	ID3D12StateObjectProperties *properties = pipeline_->GetStateObjectProperties().Get();
	WriteRecords(base, rayGenStride, rayGenRecords_, properties);
	WriteRecords(base + missTableOffset, missStride_, missRecords_, properties);
	WriteRecords(base + hitGroupTableOffset, hitGroupStride_, hitGroupRecords_, properties);
	tableBuffer_->Unmap();

	const D3D12_GPU_VIRTUAL_ADDRESS baseAddress = tableBuffer_->GetResource()->GetGPUVirtualAddress();
	rayGenAddress_ = baseAddress;
	missBaseAddress_ = baseAddress + missTableOffset;
	hitGroupBaseAddress_ = baseAddress + hitGroupTableOffset;
}

D3D12_GPU_VIRTUAL_ADDRESS_RANGE D3D12ShaderTable::GetRayGenRange() const
{
	return { rayGenAddress_, rayGenSizeInBytes_ };
}

D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE D3D12ShaderTable::GetMissRange() const
{
	return { missBaseAddress_, missSizeInBytes_, missStride_ };
}

D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE D3D12ShaderTable::GetHitGroupRange() const
{
	return { hitGroupBaseAddress_, hitGroupSizeInBytes_, hitGroupStride_ };
}
