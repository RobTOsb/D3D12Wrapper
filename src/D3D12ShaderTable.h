#pragma once

#include "pch.h"

class D3D12Device;
class D3D12RaytracingPipeline;
class D3D12Buffer;

struct ShaderTableRecord
{
	std::wstring exportName; // must match an export/hit-group name from RaytracingPipelineCreateInfo
	std::vector<uint8_t> localRootArguments; // empty if the export has no local root signature
};

// Packs raygen/miss/hit-group shader identifiers (+ optional local root arguments) into one
// GPU-visible upload buffer, respecting D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT (32) for
// each record and D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT (64) for each sub-table's start.
// Call AddRayGenRecord/AddMissRecord/AddHitGroupRecord as needed, then Build() once, then read
// back the GPU virtual address ranges to fill a D3D12_DISPATCH_RAYS_DESC.
class D3D12ShaderTable
{
public:
	D3D12ShaderTable(D3D12Device *device, D3D12RaytracingPipeline *pipeline);
	~D3D12ShaderTable();

	uint32_t AddRayGenRecord(const ShaderTableRecord &record);
	uint32_t AddMissRecord(const ShaderTableRecord &record);
	uint32_t AddHitGroupRecord(const ShaderTableRecord &record);

	// Uploads all pending records into one GPU buffer with correct per-table alignment. Must be
	// called once after all Add*Record calls and before reading the ranges below.
	void Build();

	D3D12_GPU_VIRTUAL_ADDRESS_RANGE GetRayGenRange() const;
	D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE GetMissRange() const;
	D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE GetHitGroupRange() const;

private:
	struct PendingRecord
	{
		std::wstring exportName;
		std::vector<uint8_t> localRootArguments;
	};

	static uint32_t RecordSizeFor(const std::vector<PendingRecord> &records);
	static void WriteRecords(uint8_t *dest,
							 uint32_t recordStride,
							 const std::vector<PendingRecord> &records,
							 ID3D12StateObjectProperties *properties);

	D3D12Device *device_ = nullptr;
	D3D12RaytracingPipeline *pipeline_ = nullptr;

	std::vector<PendingRecord> rayGenRecords_;
	std::vector<PendingRecord> missRecords_;
	std::vector<PendingRecord> hitGroupRecords_;

	std::unique_ptr<D3D12Buffer> tableBuffer_;

	D3D12_GPU_VIRTUAL_ADDRESS rayGenAddress_ = 0;
	D3D12_GPU_VIRTUAL_ADDRESS missBaseAddress_ = 0;
	D3D12_GPU_VIRTUAL_ADDRESS hitGroupBaseAddress_ = 0;
	uint32_t rayGenSizeInBytes_ = 0;
	uint32_t missStride_ = 0;
	uint32_t missSizeInBytes_ = 0;
	uint32_t hitGroupStride_ = 0;
	uint32_t hitGroupSizeInBytes_ = 0;
};
