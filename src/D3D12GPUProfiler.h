#pragma once

#include "pch.h"

#include <string>

class D3D12Buffer;
class D3D12CommandList;
class D3D12CommandQueue;
class D3D12Device;

struct GPUProfileScopeStats
{
	std::string name;
	float lastMs = 0.0f;
	float averageMs = 0.0f;
	float medianMs = 0.0f;
	float minMs = 0.0f;
	float maxMs = 0.0f;
	uint32_t sampleCount = 0;
};

// Timestamp profiler for one queue. Named scopes write timestamps into a per-frame-slot range of a query heap, which
// is resolved into a readback buffer at the end of the frame and read back the next time that frame slot begins (once
// its fence has been waited on), so reading never stalls the GPU. Each scope keeps a rolling window of samples.
//
// Per frame: BeginFrame(slot) after waiting on the slot's fence, Begin/EndScope while recording, EndFrame before
// closing the command list.
class D3D12GPUProfiler
{
public:
	D3D12GPUProfiler(D3D12Device *device,
					 D3D12CommandQueue *queue,
					 uint32_t framesInFlight,
					 uint32_t maxScopesPerFrame = 64,
					 uint32_t historyLength = 256);
	~D3D12GPUProfiler();

	D3D12GPUProfiler(const D3D12GPUProfiler &) = delete;
	D3D12GPUProfiler &operator=(const D3D12GPUProfiler &) = delete;

	void BeginFrame(uint32_t frameIndex);
	void EndFrame(D3D12CommandList *commandList);

	// Scopes may nest. Scopes beyond maxScopesPerFrame in one frame are dropped.
	void BeginScope(D3D12CommandList *commandList, const std::string &name);
	void EndScope(D3D12CommandList *commandList);

	// Null until the scope has at least one sample.
	const GPUProfileScopeStats *FindStats(const std::string &name) const;
	std::vector<GPUProfileScopeStats> GetAllStats() const;

	// Drops every sample collected so far, including frames still in flight.
	void Reset();

private:
	struct Scope
	{
		GPUProfileScopeStats stats;
		std::vector<float> samples; // ring buffer, ms
		uint32_t nextSample = 0;
	};

	struct PendingScope
	{
		uint32_t scopeIndex;
		uint32_t beginQuery;
		uint32_t endQuery;
	};

	struct FrameSlot
	{
		std::vector<PendingScope> scopes;
		uint32_t queriesUsed = 0;
		uint32_t queriesReserved = 0; // two per begun scope, so a scope that begins can always end
		uint64_t generation = 0; // frames recorded before the last Reset() are discarded
		bool resolved = false;
	};

	uint32_t FirstQuery(uint32_t frameIndex) const
	{
		return frameIndex * maxQueriesPerFrame_;
	}

	uint32_t FindOrAddScope(const std::string &name);
	void AddSample(Scope &scope, float ms);
	static void UpdateStats(Scope &scope);

	Microsoft::WRL::ComPtr<ID3D12QueryHeap> queryHeap_;
	std::unique_ptr<D3D12Buffer> readbackBuffer_;
	const uint64_t *readbackData_ = nullptr; // persistently mapped
	double ticksToMs_ = 0.0;
	uint32_t maxQueriesPerFrame_ = 0;
	uint32_t historyLength_ = 0;

	std::vector<Scope> scopes_;
	std::unordered_map<std::string, uint32_t> scopeLookup_;
	std::vector<FrameSlot> frames_;
	std::vector<uint32_t> openScopes_; // indices into the current frame's PendingScope list
	uint32_t currentFrame_ = 0;
	uint64_t generation_ = 0;
	bool inFrame_ = false;
};
