#include "PerformanceMetrics.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <vector>

#include <Windows.h>

#include "IO/ShaderInjectorIO.h"

namespace PerformanceMetrics
{
	namespace
	{
		constexpr size_t CounterCount = static_cast<size_t>(Counter::Count);
		constexpr size_t TimingCount = static_cast<size_t>(Timing::Count);
		constexpr double ReportIntervalSeconds = 5.0;

		struct TimingAccumulator
		{
			std::atomic<uint64_t> invocations = 0;
			std::atomic<uint64_t> samples = 0;
			std::atomic<uint64_t> totalTicks = 0;
			std::atomic<uint64_t> maximumTicks = 0;
		};

		struct ThreadMetrics
		{
			std::array<std::atomic<uint64_t>, CounterCount> counters{};
			std::array<TimingAccumulator, TimingCount> timings{};
		};

		struct TimingSnapshot
		{
			uint64_t invocations = 0;
			uint64_t samples = 0;
			uint64_t totalTicks = 0;
			uint64_t maximumTicks = 0;
		};

		std::mutex& ThreadRegistryMutex()
		{
			static auto* mutex = new std::mutex();
			return *mutex;
		}

		std::vector<std::unique_ptr<ThreadMetrics>>& ThreadRegistry()
		{
			static auto* registry = new std::vector<std::unique_ptr<ThreadMetrics>>();
			return *registry;
		}

		ThreadMetrics& CurrentThreadMetrics()
		{
			thread_local ThreadMetrics* metrics = nullptr;
			if (metrics)
				return *metrics;

			auto ownedMetrics = std::make_unique<ThreadMetrics>();
			metrics = ownedMetrics.get();
			std::lock_guard<std::mutex> lock(ThreadRegistryMutex());
			ThreadRegistry().push_back(std::move(ownedMetrics));
			return *metrics;
		}

		uint64_t QueryCounter()
		{
			LARGE_INTEGER counter{};
			QueryPerformanceCounter(&counter);
			return static_cast<uint64_t>(counter.QuadPart);
		}

		uint64_t QueryFrequency()
		{
			static const uint64_t frequency = []
			{
				LARGE_INTEGER value{};
				QueryPerformanceFrequency(&value);
				return static_cast<uint64_t>(value.QuadPart);
			}();
			return frequency;
		}

		const char* CounterName(Counter counter)
		{
			static constexpr const char* Names[] =
			{
				"present",
				"drawInstanced",
				"drawIndexedInstanced",
				"dispatch",
				"executeIndirect",
				"boundaryCandidate",
				"boundaryRecorded",
				"descriptorCopy",
				"descriptorCopySimple",
				"descriptorCopyRegistryHit",
				"mipPassAttempted",
				"mipPassSucceeded",
				"mipPassFailed",
				"mipLevelDraw",
				"mipTexels",
				"mipResourceCreated",
				"mipResourceReused",
				"customPassAttempted",
				"customPassSucceeded",
				"replacementPassAttempted",
				"replacementPassSucceeded",
				"shaderResourceBindAttempted",
				"shaderResourceBindSucceeded",
			};
			return Names[static_cast<size_t>(counter)];
		}

		const char* TimingName(Timing timing)
		{
			static constexpr const char* Names[] =
			{
				"descriptorCopyPropagation",
				"descriptorCopySimplePropagation",
				"descriptorRegistryTrackedPropagation",
				"registerShaderResourceView",
				"setPipelineStateHook",
				"executionHookLookup",
				"presentShaderMaintenance",
				"trackPipelineState",
				"trackDescriptorHeaps",
				"trackRootDescriptorTable",
				"trackGraphicsState",
				"recordExecutionBoundary",
				"buildMipGraphicsState",
				"prepareMipChains",
				"resolveMipPass",
				"ensureMipResources",
				"recordMipGeneration",
				"bindMipDescriptors",
				"restoreMipState",
				"bindShaderResources",
				"restoreShaderResources",
				"executeCustomPass",
				"resolveReplacementPipeline",
				"retireMipSubmissions",
			};
			return Names[static_cast<size_t>(timing)];
		}

		void UpdateMaximum(std::atomic<uint64_t>& maximum, uint64_t value)
		{
			uint64_t current = maximum.load(std::memory_order_relaxed);
			while (current < value && !maximum.compare_exchange_weak(
				current,
				value,
				std::memory_order_relaxed,
				std::memory_order_relaxed))
			{
			}
		}

		void LogSnapshot(uint64_t elapsedTicks)
		{
			std::array<uint64_t, CounterCount> counters{};
			std::array<TimingSnapshot, TimingCount> timings{};
			size_t metricThreadCount = 0;
			{
				std::lock_guard<std::mutex> lock(ThreadRegistryMutex());
				metricThreadCount = ThreadRegistry().size();
				for (const auto& thread : ThreadRegistry())
				{
					for (size_t counterIndex = 0; counterIndex < CounterCount; ++counterIndex)
					{
						counters[counterIndex] += thread->counters[counterIndex].exchange(
							0,
							std::memory_order_relaxed);
					}
					for (size_t timingIndex = 0; timingIndex < TimingCount; ++timingIndex)
					{
						TimingAccumulator& source = thread->timings[timingIndex];
						TimingSnapshot& destination = timings[timingIndex];
						destination.invocations += source.invocations.exchange(0, std::memory_order_relaxed);
						destination.samples += source.samples.exchange(0, std::memory_order_relaxed);
						destination.totalTicks += source.totalTicks.exchange(0, std::memory_order_relaxed);
						destination.maximumTicks = (std::max)(
							destination.maximumTicks,
							source.maximumTicks.exchange(0, std::memory_order_relaxed));
					}
				}
			}

			const double frequency = static_cast<double>(QueryFrequency());
			const double elapsedSeconds = static_cast<double>(elapsedTicks) / frequency;
			const uint64_t presents = counters[static_cast<size_t>(Counter::Present)];
			const double framesPerSecond = elapsedSeconds > 0.0
				? static_cast<double>(presents) / elapsedSeconds
				: 0.0;

			std::ostringstream summary;
			summary << std::fixed << std::setprecision(2)
				<< "PerformanceMetrics->Summary: intervalMs=" << elapsedSeconds * 1000.0
				<< " presents=" << presents
				<< " observedFPS=" << framesPerSecond
				<< " metricThreads=" << metricThreadCount;
			ShaderInjectorIO::WriteToLogFile(summary.str());

			std::ostringstream counterLine;
			counterLine << "PerformanceMetrics->Counters:";
			for (size_t counterIndex = 0; counterIndex < CounterCount; ++counterIndex)
			{
				if (!counters[counterIndex] || counterIndex == static_cast<size_t>(Counter::Present))
					continue;
				counterLine << ' ' << CounterName(static_cast<Counter>(counterIndex))
					<< '=' << counters[counterIndex];
			}
			ShaderInjectorIO::WriteToLogFile(counterLine.str());

			for (size_t timingIndex = 0; timingIndex < TimingCount; ++timingIndex)
			{
				const TimingSnapshot& timing = timings[timingIndex];
				if (!timing.invocations)
					continue;

				const double averageTicks = timing.samples
					? static_cast<double>(timing.totalTicks) / timing.samples
					: 0.0;
				const double averageMicroseconds = averageTicks * 1000000.0 / frequency;
				const double maximumMicroseconds = static_cast<double>(timing.maximumTicks) * 1000000.0 / frequency;
				const double estimatedTotalMilliseconds = averageTicks * timing.invocations * 1000.0 / frequency;
				const double oneThreadPercent = elapsedSeconds > 0.0
					? estimatedTotalMilliseconds / (elapsedSeconds * 1000.0) * 100.0
					: 0.0;

				std::ostringstream timingLine;
				timingLine << std::fixed << std::setprecision(2)
					<< "PerformanceMetrics->Timing: name=" << TimingName(static_cast<Timing>(timingIndex))
					<< " calls=" << timing.invocations
					<< " samples=" << timing.samples
					<< " estimatedTotalMs=" << estimatedTotalMilliseconds
					<< " averageUs=" << averageMicroseconds
					<< " maximumUs=" << maximumMicroseconds
					<< " oneThreadPercent=" << oneThreadPercent;
				ShaderInjectorIO::WriteToLogFile(timingLine.str());
			}
		}
	}

	void Increment(Counter counter, uint64_t amount)
	{
		CurrentThreadMetrics().counters[static_cast<size_t>(counter)].fetch_add(
			amount,
			std::memory_order_relaxed);
	}

	ScopedTimer::ScopedTimer(Timing timing, uint32_t sampleEvery)
	{
		TimingAccumulator& accumulator =
			CurrentThreadMetrics().timings[static_cast<size_t>(timing)];
		accumulator_ = &accumulator;
		const uint64_t sequence = accumulator.invocations.fetch_add(1, std::memory_order_relaxed);
		sampled_ = sampleEvery <= 1 || sequence % sampleEvery == 0;
		if (sampled_)
			startTicks_ = QueryCounter();
	}

	ScopedTimer::~ScopedTimer()
	{
		if (!sampled_ || !accumulator_)
			return;

		const uint64_t elapsedTicks = QueryCounter() - startTicks_;
		auto& accumulator = *static_cast<TimingAccumulator*>(accumulator_);
		accumulator.samples.fetch_add(1, std::memory_order_relaxed);
		accumulator.totalTicks.fetch_add(elapsedTicks, std::memory_order_relaxed);
		UpdateMaximum(accumulator.maximumTicks, elapsedTicks);
	}

	bool RecordPresentAndMaybeLog()
	{
		Increment(Counter::Present);
		static std::atomic<uint64_t> lastReportTicks = 0;
		static std::mutex reportMutex;

		const uint64_t now = QueryCounter();
		uint64_t last = lastReportTicks.load(std::memory_order_relaxed);
		if (!last)
		{
			lastReportTicks.compare_exchange_strong(
				last,
				now,
				std::memory_order_relaxed,
				std::memory_order_relaxed);
			return false;
		}

		const uint64_t intervalTicks = static_cast<uint64_t>(
			ReportIntervalSeconds * static_cast<double>(QueryFrequency()));
		if (now - last < intervalTicks || !reportMutex.try_lock())
			return false;

		std::lock_guard<std::mutex> reportLock(reportMutex, std::adopt_lock);
		last = lastReportTicks.load(std::memory_order_relaxed);
		if (now - last < intervalTicks)
			return false;
		lastReportTicks.store(now, std::memory_order_relaxed);
		LogSnapshot(now - last);
		return true;
	}
}
