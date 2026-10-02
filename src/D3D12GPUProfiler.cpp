#include "D3D12GPUProfiler.h"

#include <numeric>

#include "D3D12Buffer.h"
#include "D3D12CommandList.h"
#include "D3D12CommandQueue.h"
#include "D3D12Device.h"
#include "D3D12Exception.h"

namespace
{
	constexpr uint32_t kNoScope = UINT32_MAX;
	constexpr uint32_t kNoQuery = UINT32_MAX;
} // namespace

D3D12GPUProfiler::D3D12GPUProfiler(D3D12Device *device,
								   D3D12CommandQueue *queue,
								   uint32_t framesInFlight,
								   uint32_t maxScopesPerFrame,
								   uint32_t historyLength) :
	maxQueriesPerFrame_(maxScopesPerFrame * 2), historyLength_((std::max) (historyLength, 1u)), frames_(framesInFlight)
{
	const uint32_t queryCount = maxQueriesPerFrame_ * framesInFlight;

	D3D12_QUERY_HEAP_DESC heapDesc = {};
	heapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
	heapDesc.Count = queryCount;
	HRESULT hr = device->GetNativeDevice()->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(&queryHeap_));
	if (FAILED(hr))
	{
		throw D3D12Exception("Failed to create the GPU profiler's timestamp query heap.", hr);
	}
	queryHeap_->SetName(L"GPUProfiler_Timestamps");

	readbackBuffer_ = device->CreateBuffer(CD3DX12_RESOURCE_DESC1::Buffer((uint64_t) queryCount * sizeof(uint64_t)),
										   D3D12_HEAP_TYPE_READBACK);
	readbackBuffer_->GetResource()->SetName(L"GPUProfiler_Readback");

	// Persistently mapped; each frame slot is only read after its fence has passed.
	const D3D12_RANGE wholeBuffer = { 0, (SIZE_T) queryCount * sizeof(uint64_t) };
	readbackBuffer_->Map(0, wholeBuffer);
	readbackData_ = static_cast<const uint64_t *>(readbackBuffer_->GetMappedData());

	const uint64_t frequency = queue->GetTimestampFrequency();
	ticksToMs_ = frequency ? 1000.0 / (double) frequency : 0.0;
}

D3D12GPUProfiler::~D3D12GPUProfiler()
{
	if (readbackBuffer_)
	{
		readbackBuffer_->Unmap();
	}
}

void D3D12GPUProfiler::BeginFrame(uint32_t frameIndex)
{
	currentFrame_ = frameIndex;
	FrameSlot &frame = frames_[currentFrame_];

	if (frame.resolved && frame.generation == generation_)
	{
		std::vector<bool> touched(scopes_.size(), false);
		for (const PendingScope &pending: frame.scopes)
		{
			if (pending.endQuery == kNoQuery)
			{
				continue;
			}

			const uint64_t begin = readbackData_[pending.beginQuery];
			const uint64_t end = readbackData_[pending.endQuery];
			const float ms = end > begin ? (float) ((double) (end - begin) * ticksToMs_) : 0.0f;
			AddSample(scopes_[pending.scopeIndex], ms);
			touched[pending.scopeIndex] = true;
		}

		for (size_t s = 0; s < scopes_.size(); ++s)
		{
			if (touched[s])
			{
				UpdateStats(scopes_[s]);
			}
		}
	}

	frame.scopes.clear();
	frame.queriesUsed = 0;
	frame.queriesReserved = 0;
	frame.generation = generation_;
	frame.resolved = false;
	openScopes_.clear();
	inFrame_ = true;
}

void D3D12GPUProfiler::EndFrame(D3D12CommandList *commandList)
{
	FrameSlot &frame = frames_[currentFrame_];

	if (inFrame_ && frame.queriesUsed > 0)
	{
		const uint32_t firstQuery = FirstQuery(currentFrame_);
		commandList->ResolveQueryData(queryHeap_.Get(),
									  D3D12_QUERY_TYPE_TIMESTAMP,
									  firstQuery,
									  frame.queriesUsed,
									  readbackBuffer_.get(),
									  (uint64_t) firstQuery * sizeof(uint64_t));
		frame.resolved = true;
	}

	// Anything still open has no end timestamp; BeginFrame skips it.
	openScopes_.clear();
	inFrame_ = false;
}

void D3D12GPUProfiler::BeginScope(D3D12CommandList *commandList, const std::string &name)
{
	FrameSlot &frame = frames_[currentFrame_];

	if (!inFrame_ || frame.queriesReserved + 2 > maxQueriesPerFrame_)
	{
		openScopes_.push_back(kNoScope);
		return;
	}

	frame.queriesReserved += 2;
	const uint32_t query = FirstQuery(currentFrame_) + frame.queriesUsed++;
	commandList->EndQuery(queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, query);

	frame.scopes.push_back({ FindOrAddScope(name), query, kNoQuery });
	openScopes_.push_back((uint32_t) frame.scopes.size() - 1);
}

void D3D12GPUProfiler::EndScope(D3D12CommandList *commandList)
{
	if (openScopes_.empty())
	{
		return;
	}

	const uint32_t pendingIndex = openScopes_.back();
	openScopes_.pop_back();
	if (pendingIndex == kNoScope)
	{
		return;
	}

	FrameSlot &frame = frames_[currentFrame_];
	const uint32_t query = FirstQuery(currentFrame_) + frame.queriesUsed++;
	commandList->EndQuery(queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, query);
	frame.scopes[pendingIndex].endQuery = query;
}

const GPUProfileScopeStats *D3D12GPUProfiler::FindStats(const std::string &name) const
{
	const auto it = scopeLookup_.find(name);
	if (it == scopeLookup_.end() || scopes_[it->second].samples.empty())
	{
		return nullptr;
	}
	return &scopes_[it->second].stats;
}

std::vector<GPUProfileScopeStats> D3D12GPUProfiler::GetAllStats() const
{
	std::vector<GPUProfileScopeStats> result;
	for (const Scope &scope: scopes_)
	{
		if (!scope.samples.empty())
		{
			result.push_back(scope.stats);
		}
	}
	return result;
}

void D3D12GPUProfiler::Reset()
{
	generation_++;
	for (Scope &scope: scopes_)
	{
		scope.samples.clear();
		scope.nextSample = 0;
		scope.stats = { scope.stats.name };
	}
}

uint32_t D3D12GPUProfiler::FindOrAddScope(const std::string &name)
{
	const auto it = scopeLookup_.find(name);
	if (it != scopeLookup_.end())
	{
		return it->second;
	}

	Scope scope;
	scope.stats.name = name;
	scope.samples.reserve(historyLength_);
	scopes_.push_back(std::move(scope));
	scopeLookup_.emplace(name, (uint32_t) scopes_.size() - 1);
	return (uint32_t) scopes_.size() - 1;
}

void D3D12GPUProfiler::AddSample(Scope &scope, float ms)
{
	if (scope.samples.size() < historyLength_)
	{
		scope.samples.push_back(ms);
	}
	else
	{
		scope.samples[scope.nextSample] = ms;
	}
	scope.nextSample = (scope.nextSample + 1) % historyLength_;
	scope.stats.lastMs = ms;
}

void D3D12GPUProfiler::UpdateStats(Scope &scope)
{
	std::vector<float> sorted = scope.samples;
	std::sort(sorted.begin(), sorted.end());

	GPUProfileScopeStats &stats = scope.stats;
	stats.sampleCount = (uint32_t) sorted.size();
	stats.minMs = sorted.front();
	stats.maxMs = sorted.back();
	stats.averageMs = std::accumulate(sorted.begin(), sorted.end(), 0.0f) / (float) sorted.size();
	const size_t middle = sorted.size() / 2;
	stats.medianMs = sorted.size() % 2 ? sorted[middle] : 0.5f * (sorted[middle - 1] + sorted[middle]);
}
