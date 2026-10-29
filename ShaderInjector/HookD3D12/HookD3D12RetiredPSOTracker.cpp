#include "HookD3D12RetiredPSOTracker.h"
#include "CommandListPipelineState.h"
#include "D3D12/HookD3D12RuntimeState.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Globals.h"
#include "IO/ShaderInjectorIO.h"

namespace HookD3D12
{
	struct RetireReference
	{
		ID3D12GraphicsCommandList* commandList = nullptr;
		uint64_t recordingGeneration = 0;
	};

	struct RetiredPSOEntry
	{
		ID3D12PipelineState* pipelineState = nullptr;
		uint64_t retireEpoch = 0;
		ULONGLONG retireTick = 0;
		std::vector<RetireReference> references;
		bool referencesCleared = false;
		std::vector<uint64_t> releaseFenceValues;
		bool releaseSignalIssued = false;
	};

	struct RetiredPSOStats
	{
		std::atomic<uint64_t> retiredTotal{0};
		std::atomic<uint64_t> releasedTotal{0};
		std::atomic<uint64_t> recordedReferences{0};
		std::atomic<uint64_t> releaseSignalsFailed{0};
	};

	namespace
	{
		std::mutex gRetiredMutex;
		std::deque<RetiredPSOEntry> gRetiredPipelineStates;
		std::unordered_set<ID3D12PipelineState*> gRetiredPipelineStateSet;
		std::atomic<uint64_t> gNextRetireEpoch{1};
		std::atomic<size_t> gPendingRetireCount{0};

		std::unordered_map<ID3D12PipelineState*, std::vector<RetireReference>> gRecordedReplacements;

		//kept alive so a Signal never reaches a destroyed queue.
		struct QueueTracker
		{
			Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
			Microsoft::WRL::ComPtr<ID3D12Fence> fence;
			uint64_t nextValue = 1;
		};

		std::vector<QueueTracker> gQueueTrackers;

		RetiredPSOStats gStats{};

		bool ReleaseFencesCompletedLocked(const RetiredPSOEntry& entry)
		{
			for (size_t trackerIndex = 0; trackerIndex < entry.releaseFenceValues.size(); ++trackerIndex)
			{
				if (gQueueTrackers[trackerIndex].fence->GetCompletedValue() < entry.releaseFenceValues[trackerIndex])
					return false;
			}

			return true;
		}

		void LogRetiredPSOStatsThrottled()
		{
			if (!Globals::gVerboseLog)
				return;

			static std::chrono::steady_clock::time_point gLastStatsLogTime{};
			static uint64_t gLastRetiredTotal = 0;
			static uint64_t gLastReleasedTotal = 0;
			constexpr std::chrono::milliseconds statsLogInterval{5000};
			const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
			const bool statsDue = gLastStatsLogTime.time_since_epoch().count() == 0 || now - gLastStatsLogTime >= statsLogInterval;

			if (!statsDue)
				return;

			const uint64_t retiredTotal = gStats.retiredTotal;
			const uint64_t releasedTotal = gStats.releasedTotal;
			const size_t pendingCount = gPendingRetireCount.load(std::memory_order_relaxed);

			if (pendingCount == 0 && retiredTotal == gLastRetiredTotal && releasedTotal == gLastReleasedTotal)
				return;

			gLastStatsLogTime = now;
			gLastRetiredTotal = retiredTotal;
			gLastReleasedTotal = releasedTotal;

			ShaderInjectorIO::WriteToLogFile(
				"HookD3D12RetiredPSOTracker->Stats: retired=" + std::to_string(retiredTotal) +
				" released=" + std::to_string(releasedTotal) +
				" pending=" + std::to_string(pendingCount) +
				" references=" + std::to_string(gStats.recordedReferences) +
				" signalsFailed=" + std::to_string(gStats.releaseSignalsFailed));
		}
	} //namespace

	void RecordReplacementBinding(ID3D12PipelineState* pipelineState, ID3D12GraphicsCommandList* commandList)
	{
		if (!pipelineState || !commandList)
			return;

		//resolved first: the registry lock must never nest inside gRetiredMutex.
		CommandListPipelineState* listState = FindCommandListPipelineState(commandList);

		if (!listState)
			return;

		std::lock_guard<std::mutex> lock(gRetiredMutex);
		const uint64_t recordingGeneration = listState->recordingGeneration.load(std::memory_order_acquire);

		//a bind can race the retire, so the pending entry takes the reference and its proof restarts.
		if (gRetiredPipelineStateSet.find(pipelineState) != gRetiredPipelineStateSet.end())
		{
			for (RetiredPSOEntry& entry : gRetiredPipelineStates)
			{
				if (entry.pipelineState != pipelineState)
					continue;

				const auto existing = std::find_if(entry.references.begin(), entry.references.end(), [&](const RetireReference& reference)
				{
					return reference.commandList == commandList;
				});

				if (existing != entry.references.end())
					existing->recordingGeneration = recordingGeneration;
				else
					entry.references.push_back({commandList, recordingGeneration});

				entry.referencesCleared = false;
				entry.releaseSignalIssued = false;
				entry.releaseFenceValues.clear();
				return;
			}

			return;
		}

		std::vector<RetireReference>& references = gRecordedReplacements[pipelineState];

		//one reference per (list, PSO), refreshed to the newest recording.
		for (RetireReference& reference : references)
		{
			if (reference.commandList != commandList)
				continue;

			if (reference.recordingGeneration != recordingGeneration)
				reference.recordingGeneration = recordingGeneration;

			return;
		}

		references.push_back({commandList, recordingGeneration});
		++gStats.recordedReferences;
	}

	void TrackRetiredPipelineStateReference(ID3D12PipelineState* pipelineState)
	{
		if (!pipelineState)
			return;

		pipelineState->AddRef();

		ID3D12PipelineState* ownedReference = pipelineState;
		TrackRetiredPipelineState(ownedReference);
	}

	void TrackRetiredPipelineState(ID3D12PipelineState*& pipelineState)
	{
		if (!pipelineState)
			return;

		std::lock_guard<std::mutex> lock(gRetiredMutex);

		//already pending: that entry keeps the PSO alive, so drop this holder's reference.
		if (!gRetiredPipelineStateSet.insert(pipelineState).second)
		{
			pipelineState->Release();
			pipelineState = nullptr;
			return;
		}

		RetiredPSOEntry entry{};
		entry.pipelineState = pipelineState;
		entry.retireEpoch = gNextRetireEpoch.fetch_add(1, std::memory_order_relaxed);
		entry.retireTick = GetTickCount64();

		const auto recordedIt = gRecordedReplacements.find(pipelineState);

		if (recordedIt != gRecordedReplacements.end())
		{
			entry.references = std::move(recordedIt->second);
			gRecordedReplacements.erase(recordedIt);
		}

		entry.referencesCleared = entry.references.empty();

		gRetiredPipelineStates.push_back(std::move(entry));
		gPendingRetireCount.store(gRetiredPipelineStates.size(), std::memory_order_relaxed);
		++gStats.retiredTotal;

		pipelineState = nullptr;
	}

	void ProcessRetiredPSOReleases()
	{
		LogRetiredPSOStatsThrottled();

		if (gPendingRetireCount.load(std::memory_order_relaxed) == 0)
			return;

		std::vector<ID3D12GraphicsCommandList*> referencedCommandLists;
		{
			std::lock_guard<std::mutex> lock(gRetiredMutex);

			for (const RetiredPSOEntry& entry : gRetiredPipelineStates)
			{
				for (const RetireReference& reference : entry.references)
				{
					if (std::find(referencedCommandLists.begin(), referencedCommandLists.end(), reference.commandList) == referencedCommandLists.end())
						referencedCommandLists.push_back(reference.commandList);
				}
			}
		}

		//generation 0 marks a list that no longer exists.
		std::unordered_map<ID3D12GraphicsCommandList*, uint64_t> currentGenerations;

		for (ID3D12GraphicsCommandList* commandList : referencedCommandLists)
		{
			CommandListPipelineState* listState = FindCommandListPipelineState(commandList);
			currentGenerations[commandList] = listState
				? listState->recordingGeneration.load(std::memory_order_acquire)
				: 0;
		}

		std::lock_guard<std::mutex> lock(gRetiredMutex);

		std::deque<RetiredPSOEntry> remainingEntries;

		for (RetiredPSOEntry& entry : gRetiredPipelineStates)
		{
			if (!entry.referencesCleared)
			{
				bool stillRecorded = false;

				for (const RetireReference& reference : entry.references)
				{
					const auto generationIt = currentGenerations.find(reference.commandList);

					if (generationIt == currentGenerations.end() || generationIt->second == 0 ||
						generationIt->second != reference.recordingGeneration)
					{
						continue;
					}

					stillRecorded = true;
				}

				if (stillRecorded)
				{
					remainingEntries.push_back(std::move(entry));
					continue;
				}

				entry.referencesCleared = true;
			}

			if (!entry.releaseSignalIssued)
			{
				//a queue first seen later cannot carry a submission recorded before this point.
				bool signalled = true;
				entry.releaseFenceValues.clear();

				for (QueueTracker& tracker : gQueueTrackers)
				{
					if (!tracker.fence || FAILED(tracker.queue->Signal(tracker.fence.Get(), tracker.nextValue)))
					{
						signalled = false;
						break;
					}

					entry.releaseFenceValues.push_back(tracker.nextValue++);
				}

				if (!signalled)
				{
					++gStats.releaseSignalsFailed;
					remainingEntries.push_back(std::move(entry));
					continue;
				}

				entry.releaseSignalIssued = true;
			}

			if (!ReleaseFencesCompletedLocked(entry))
			{
				remainingEntries.push_back(std::move(entry));
				continue;
			}

			entry.pipelineState->Release();
			gRetiredPipelineStateSet.erase(entry.pipelineState);
			entry.pipelineState = nullptr;
			++gStats.releasedTotal;
		}

		gRetiredPipelineStates = std::move(remainingEntries);
		gPendingRetireCount.store(gRetiredPipelineStates.size(), std::memory_order_relaxed);
	}

	void ObserveSubmissionQueue(ID3D12CommandQueue* commandQueue)
	{
		static thread_local ID3D12CommandQueue* gLastObservedQueue = nullptr;

		if (!commandQueue || commandQueue == gLastObservedQueue)
			return;

		std::lock_guard<std::mutex> lock(gRetiredMutex);

		for (const QueueTracker& tracker : gQueueTrackers)
		{
			if (tracker.queue.Get() == commandQueue)
			{
				gLastObservedQueue = commandQueue;
				return;
			}
		}

		Microsoft::WRL::ComPtr<ID3D12Device> device;
		Microsoft::WRL::ComPtr<ID3D12Fence> fence;

		if (FAILED(commandQueue->GetDevice(IID_PPV_ARGS(&device))) ||
			FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))))
		{
			ShaderInjectorIO::WriteToLogFileError("HookD3D12RetiredPSOTracker->ObserveSubmissionQueue: fence creation failed; retired PSOs will not be released.");
			gQueueTrackers.push_back({commandQueue, nullptr, 1});
			gLastObservedQueue = commandQueue;
			return;
		}

		gQueueTrackers.push_back({commandQueue, fence, 1});
		gLastObservedQueue = commandQueue;
	}

	RetiredPSODiagnostics GetRetiredPSODiagnostics()
	{
		std::lock_guard<std::mutex> lock(gRetiredMutex);

		RetiredPSODiagnostics diagnostics{};
		diagnostics.pendingCount = gRetiredPipelineStates.size();
		diagnostics.retiredTotal = gStats.retiredTotal;
		diagnostics.releasedTotal = gStats.releasedTotal;

		if (!gRetiredPipelineStates.empty())
		{
			const ULONGLONG now = GetTickCount64();
			diagnostics.oldestPendingAgeMilliseconds = now - gRetiredPipelineStates.front().retireTick;
		}

		return diagnostics;
	}

	bool IsRetirePending(ID3D12PipelineState* pipelineState)
	{
		if (!pipelineState)
			return false;

		std::lock_guard<std::mutex> lock(gRetiredMutex);

		return gRetiredPipelineStateSet.find(pipelineState) != gRetiredPipelineStateSet.end();
	}

	void ShutdownRetiredPSOTracker()
	{
		std::lock_guard<std::mutex> lock(gRetiredMutex);

		for (RetiredPSOEntry& entry : gRetiredPipelineStates)
		{
			if (entry.pipelineState)
				entry.pipelineState->Release();
		}

		gRetiredPipelineStates.clear();
		gRetiredPipelineStateSet.clear();
		gRecordedReplacements.clear();
		gQueueTrackers.clear();
		gPendingRetireCount.store(0, std::memory_order_relaxed);
	}
} //namespace HookD3D12
