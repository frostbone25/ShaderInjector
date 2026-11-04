#include "Performance/RenderPassGPUProfiler.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <limits>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Globals.h"
#include "HookD3D12/ScopedRenderPassInjection.h"
#include "IO/ShaderInjectorIO.h"
#include "Performance/GPUTimingCommandList.h"
#include "Performance/GPUTimingQueue.h"
#include "Performance/GPUTimingStatistics.h"
#include "StringHelper.h"

namespace RenderPassGPUProfiler
{
	constexpr UINT samplesPerPage = 128;
	constexpr uint64_t executionSampleInterval = 32;

	std::mutex gProfilerMutex;
	std::unordered_map<ID3D12GraphicsCommandList*, GPUTimingCommandList> gCommandLists;
	std::unordered_map<ID3D12CommandQueue*, GPUTimingQueue> gQueues;
	std::unordered_map<std::string, std::array<uint64_t, 2>> gExecutionCounts;
	std::unordered_map<std::string, GPUTimingStatistics> gStatistics;
	PerformanceSnapshot gPerformanceSnapshot;
	std::atomic<uint32_t> gRecordedCommandListCount = 0;
	thread_local ID3D12GraphicsCommandList* gSampledCommandList = nullptr;

	UINT64 CPUCounterFrequency()
	{
		static const UINT64 frequency = []
		{
			LARGE_INTEGER counterFrequency{};
			QueryPerformanceFrequency(&counterFrequency);

			return static_cast<UINT64>(counterFrequency.QuadPart);
		}();

		return frequency;
	}

	//|||||||||||||||||||||||||||||||||| QUERY STORAGE ||||||||||||||||||||||||||||||||||
	//|||||||||||||||||||||||||||||||||| QUERY STORAGE ||||||||||||||||||||||||||||||||||
	//|||||||||||||||||||||||||||||||||| QUERY STORAGE ||||||||||||||||||||||||||||||||||

	void CollectPage(GPUTimingPage& page)
	{
		if (page.collected || page.retirementBlocked || !page.retirementFence || !page.timestampFrequency)
			return;

		const UINT64 completedValue = page.retirementFence->GetCompletedValue();

		//UINT64_MAX means device removal, not successful completion of every query.
		if (completedValue == (std::numeric_limits<UINT64>::max)() || completedValue < page.retirementValue)
			return;

		void* mappedData = nullptr;
		const D3D12_RANGE readRange{0, page.samples.size() * 2 * sizeof(UINT64)};

		if (FAILED(page.readback->Map(0, &readRange, &mappedData)))
			return;

		const auto* timestamps = static_cast<const UINT64*>(mappedData);

		for (const GPUTimingSample& sample : page.samples)
		{
			if (!sample.completed)
				continue;

			const UINT64 startTimestamp = timestamps[sample.firstQuery];
			const UINT64 endTimestamp = timestamps[sample.firstQuery + 1];

			if (endTimestamp < startTimestamp)
				continue;

			const double milliseconds = static_cast<double>(endTimestamp - startTimestamp) * 1000.0 / static_cast<double>(page.timestampFrequency);
			GPUTimingStatistics& statistics = gStatistics[sample.name];
			statistics.samples++;
			statistics.totalMilliseconds += milliseconds;
			statistics.maximumMilliseconds = (std::max)(statistics.maximumMilliseconds, milliseconds);
			statistics.totalCPUMilliseconds += sample.cpuMilliseconds;
			statistics.maximumCPUMilliseconds = (std::max)(statistics.maximumCPUMilliseconds, sample.cpuMilliseconds);
		}

		const D3D12_RANGE noWrites{0, 0};
		page.readback->Unmap(0, &noWrites);
		page.collected = true;
	}

	bool PageIsReusable(GPUTimingPage& page)
	{
		if (page.recorded || page.retirementBlocked)
			return false;

		if (!page.retirementFence)
			return true;

		const UINT64 completedValue = page.retirementFence->GetCompletedValue();

		if (completedValue == (std::numeric_limits<UINT64>::max)() || completedValue < page.retirementValue)
			return false;

		CollectPage(page);

		return page.collected;
	}

	GPUTimingPage* AcquirePage(ID3D12GraphicsCommandList* commandList, GPUTimingCommandList& recording)
	{
		if (recording.allocationFailed)
			return nullptr;

		if (recording.currentPage && recording.currentPage->samples.size() < samplesPerPage)
			return recording.currentPage;

		GPUTimingPage* selectedPage = nullptr;
		bool alreadyRecorded = false;

		for (const auto& page : recording.pages)
		{
			alreadyRecorded = alreadyRecorded || page->recorded;

			if (!selectedPage && PageIsReusable(*page))
				selectedPage = page.get();
		}

		if (!selectedPage)
		{
			if (!recording.device && FAILED(commandList->GetDevice(IID_PPV_ARGS(&recording.device))))
			{
				recording.allocationFailed = true;

				return nullptr;
			}

			auto page = std::make_unique<GPUTimingPage>();
			D3D12_QUERY_HEAP_DESC queryDescription{};
			queryDescription.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
			queryDescription.Count = samplesPerPage * 2;

			D3D12_RESOURCE_DESC bufferDescription{};
			bufferDescription.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			bufferDescription.Width = queryDescription.Count * sizeof(UINT64);
			bufferDescription.Height = 1;
			bufferDescription.DepthOrArraySize = 1;
			bufferDescription.MipLevels = 1;
			bufferDescription.SampleDesc.Count = 1;
			bufferDescription.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			const D3D12_HEAP_PROPERTIES readbackHeap{D3D12_HEAP_TYPE_READBACK};

			if (FAILED(recording.device->CreateQueryHeap(&queryDescription, IID_PPV_ARGS(&page->queryHeap))) || FAILED(recording.device->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &bufferDescription, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&page->readback))))
			{
				recording.allocationFailed = true;
				ShaderInjectorIO::WriteToLogFileWarning("RenderPassGPUProfiler: timestamp resources unavailable; GPU profiling skipped for this command list.");

				return nullptr;
			}

			page->samples.reserve(samplesPerPage);
			selectedPage = page.get();
			recording.pages.push_back(std::move(page));
		}

		selectedPage->samples.clear();
		selectedPage->retirementFence.Reset();
		selectedPage->timestampFrequency = 0;
		selectedPage->collected = true;
		selectedPage->recorded = true;
		recording.currentPage = selectedPage;

		if (!alreadyRecorded)
			gRecordedCommandListCount.fetch_add(1, std::memory_order_release);

		return selectedPage;
	}

	GPUTimingToken BeginTiming(ID3D12GraphicsCommandList* commandList, const std::string& name)
	{
		if (!commandList || gSampledCommandList != commandList)
			return {};

		const D3D12_COMMAND_LIST_TYPE commandListType = commandList->GetType();

		if (commandListType != D3D12_COMMAND_LIST_TYPE_DIRECT && commandListType != D3D12_COMMAND_LIST_TYPE_COMPUTE)
			return {};

		LARGE_INTEGER startCounter{};
		QueryPerformanceCounter(&startCounter);

		std::lock_guard<std::mutex> lock(gProfilerMutex);
		//diagnostic buffers belong to the injector, not the game's captured resource registry.
		HookD3D12::ScopedRenderPassInjection injectionScope;
		GPUTimingPage* page = AcquirePage(commandList, gCommandLists[commandList]);

		if (!page)
			return {};

		const UINT sampleIndex = static_cast<UINT>(page->samples.size());
		const UINT queryIndex = sampleIndex * 2;
		page->samples.push_back({name, queryIndex, static_cast<UINT64>(startCounter.QuadPart), 0, false});
		commandList->EndQuery(page->queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, queryIndex);

		return {page, sampleIndex};
	}

	void EndTiming(ID3D12GraphicsCommandList* commandList, GPUTimingToken token)
	{
		if (!token.page)
			return;

		std::lock_guard<std::mutex> lock(gProfilerMutex);
		GPUTimingSample& sample = token.page->samples[token.sampleIndex];
		HookD3D12::ScopedRenderPassInjection injectionScope;
		commandList->EndQuery(token.page->queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, sample.firstQuery + 1);

		//resolve only this completed pair; unused or abandoned queries must never be resolved.
		commandList->ResolveQueryData(token.page->queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, sample.firstQuery, 2, token.page->readback.Get(), static_cast<UINT64>(sample.firstQuery) * sizeof(UINT64));

		LARGE_INTEGER endCounter{};
		QueryPerformanceCounter(&endCounter);
		sample.cpuMilliseconds = static_cast<double>(static_cast<UINT64>(endCounter.QuadPart) - sample.cpuStartTicks) * 1000.0 / static_cast<double>(CPUCounterFrequency());
		sample.completed = true;
	}

	//|||||||||||||||||||||||||||||||||| RECORDING SCOPES ||||||||||||||||||||||||||||||||||
	//|||||||||||||||||||||||||||||||||| RECORDING SCOPES ||||||||||||||||||||||||||||||||||
	//|||||||||||||||||||||||||||||||||| RECORDING SCOPES ||||||||||||||||||||||||||||||||||

	ScopedExecutionSample::ScopedExecutionSample(ID3D12GraphicsCommandList* commandList, const std::string& modifiedShaderID, bool afterBoundary)
		: previousCommandList(gSampledCommandList)
	{
		gSampledCommandList = nullptr;

		if (!Globals::gPerformanceTelemetryEnabled)
			return;

		std::lock_guard<std::mutex> lock(gProfilerMutex);
		size_t boundaryIndex = 0;

		if (afterBoundary)
			boundaryIndex = 1;

		//sample before/after independently, otherwise a paired boundary can starve all after-pass measurements.
		uint64_t& executionCount = gExecutionCounts[modifiedShaderID][boundaryIndex];

		//timestamps are bottom-of-pipe operations, so sample chains instead of serializing every frame.
		if (executionCount++ % executionSampleInterval == 0)
			gSampledCommandList = commandList;
	}

	ScopedExecutionSample::~ScopedExecutionSample()
	{
		gSampledCommandList = previousCommandList;
	}

	ScopedPassTiming::ScopedPassTiming(ID3D12GraphicsCommandList* commandList, const std::string& passName)
		: commandList(commandList), token(BeginTiming(commandList, passName))
	{
	}

	ScopedPassTiming::~ScopedPassTiming()
	{
		EndTiming(commandList, token);
	}

	void BeginOriginalDraw(ID3D12GraphicsCommandList* commandList, const std::string& modifiedShaderID)
	{
		if (gSampledCommandList != commandList)
			return;

		const GPUTimingToken token = BeginTiming(commandList, "OriginalDraw:" + modifiedShaderID);

		if (!token.page)
			return;

		std::lock_guard<std::mutex> lock(gProfilerMutex);
		gCommandLists.at(commandList).originalDraw = token;
	}

	void EndOriginalDraw(ID3D12GraphicsCommandList* commandList)
	{
		if (!Globals::gPerformanceTelemetryEnabled && !gRecordedCommandListCount.load(std::memory_order_relaxed))
			return;

		GPUTimingToken token;
		{
			std::lock_guard<std::mutex> lock(gProfilerMutex);
			const auto recording = gCommandLists.find(commandList);

			if (recording == gCommandLists.end())
				return;

			token = recording->second.originalDraw;
			recording->second.originalDraw = {};
		}

		EndTiming(commandList, token);
	}

	//|||||||||||||||||||||||||||||||||| SUBMISSION AND REPORTING ||||||||||||||||||||||||||||||||||
	//|||||||||||||||||||||||||||||||||| SUBMISSION AND REPORTING ||||||||||||||||||||||||||||||||||
	//|||||||||||||||||||||||||||||||||| SUBMISSION AND REPORTING ||||||||||||||||||||||||||||||||||

	bool HasRecordedCommandListWork()
	{
		return gRecordedCommandListCount.load(std::memory_order_acquire) != 0;
	}

	void ResetCommandList(ID3D12GraphicsCommandList* commandList)
	{
		if (!HasRecordedCommandListWork())
			return;

		std::lock_guard<std::mutex> lock(gProfilerMutex);
		const auto recording = gCommandLists.find(commandList);

		if (recording == gCommandLists.end())
			return;

		bool wasRecorded = false;

		for (const auto& page : recording->second.pages)
		{
			wasRecorded = wasRecorded || page->recorded;
			page->recorded = false;
		}

		if (wasRecorded)
			gRecordedCommandListCount.fetch_sub(1, std::memory_order_release);

		recording->second.currentPage = nullptr;
		recording->second.originalDraw = {};
	}

	void NotifyCommandListsSubmitted(ID3D12CommandQueue* commandQueue, UINT commandListCount, ID3D12CommandList* const* commandLists)
	{
		if (!HasRecordedCommandListWork() || !commandQueue || !commandLists)
			return;

		std::lock_guard<std::mutex> lock(gProfilerMutex);
		thread_local std::vector<GPUTimingPage*> submittedPages;
		submittedPages.clear();

		for (UINT commandListIndex = 0; commandListIndex < commandListCount; ++commandListIndex)
		{
			const auto recording = gCommandLists.find(reinterpret_cast<ID3D12GraphicsCommandList*>(commandLists[commandListIndex]));

			if (recording == gCommandLists.end())
				continue;

			for (const auto& page : recording->second.pages)
			{
				if (page->recorded)
					submittedPages.push_back(page.get());
			}
		}

		if (submittedPages.empty())
			return;

		GPUTimingQueue& queue = gQueues[commandQueue];

		if (!queue.fence && !queue.unavailable)
		{
			Microsoft::WRL::ComPtr<ID3D12Device> device;
			queue.unavailable = FAILED(commandQueue->GetTimestampFrequency(&queue.timestampFrequency)) || !queue.timestampFrequency || FAILED(commandQueue->GetDevice(IID_PPV_ARGS(&device))) || FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&queue.fence)));
		}

		const UINT64 fenceValue = ++queue.nextFenceValue;
		const bool signaled = !queue.unavailable && SUCCEEDED(commandQueue->Signal(queue.fence.Get(), fenceValue));

		if (!signaled)
		{
			//keep potentially in-flight pages alive, but stop allocating diagnostic pages on this failure.
			queue.unavailable = true;

			for (UINT commandListIndex = 0; commandListIndex < commandListCount; ++commandListIndex)
			{
				const auto recording = gCommandLists.find(reinterpret_cast<ID3D12GraphicsCommandList*>(commandLists[commandListIndex]));

				if (recording != gCommandLists.end())
					recording->second.allocationFailed = true;
			}
		}

		for (GPUTimingPage* page : submittedPages)
		{
			page->retirementBlocked = !signaled;
			page->collected = false;

			if (signaled)
			{
				page->retirementFence = queue.fence;
				page->retirementValue = fenceValue;
				page->timestampFrequency = queue.timestampFrequency;
			}
		}
	}

	void LogPerformanceStatistics()
	{
		std::vector<std::pair<std::string, GPUTimingStatistics>> report;
		{
			std::lock_guard<std::mutex> lock(gProfilerMutex);

			for (auto& recording : gCommandLists)
				for (const auto& page : recording.second.pages)
					CollectPage(*page);

			for (const auto& statistics : gStatistics)
				if (statistics.second.samples)
					report.push_back(statistics);

			gStatistics.clear();
		}

		std::sort(report.begin(), report.end(), [](const auto& left, const auto& right) { return left.second.totalMilliseconds > right.second.totalMilliseconds; });
		{
			std::lock_guard<std::mutex> lock(gProfilerMutex);
			gPerformanceSnapshot.available = true;
			gPerformanceSnapshot.executionSampleInterval = executionSampleInterval;
			gPerformanceSnapshot.passes.clear();
			gPerformanceSnapshot.passes.reserve(report.size());

			for (const auto& entry : report)
				gPerformanceSnapshot.passes.push_back({entry.first, entry.second});
		}

		for (const auto& entry : report)
		{
			const GPUTimingStatistics& statistics = entry.second;
			ShaderInjectorIO::WriteToLogFile(StringHelper::Format("RenderPassGPUProfiler->Performance: pass=%s gpuSamples=%llu sampledTotalMs=%.3f averageMs=%.3f maximumMs=%.3f averageCPURecordMs=%.3f maximumCPURecordMs=%.3f sampleInterval=%llu", entry.first.c_str(), static_cast<unsigned long long>(statistics.samples), statistics.totalMilliseconds, statistics.totalMilliseconds / static_cast<double>(statistics.samples), statistics.maximumMilliseconds, statistics.totalCPUMilliseconds / static_cast<double>(statistics.samples), statistics.maximumCPUMilliseconds, static_cast<unsigned long long>(executionSampleInterval)));
		}
	}

	PerformanceSnapshot GetSnapshot()
	{
		std::lock_guard<std::mutex> lock(gProfilerMutex);
		return gPerformanceSnapshot;
	}

	void ReleaseResources()
	{
		std::lock_guard<std::mutex> lock(gProfilerMutex);
		gCommandLists.clear();
		gQueues.clear();
		gExecutionCounts.clear();
		gStatistics.clear();
		gPerformanceSnapshot = {};
		gRecordedCommandListCount.store(0, std::memory_order_release);
		gSampledCommandList = nullptr;
	}
}
