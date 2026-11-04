#pragma once

#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <string>
#include <utility>

namespace ModifiedShader
{
	inline unsigned int GetBoundedCompileWorkerCount(unsigned int hardwareConcurrency)
	{
		if (hardwareConcurrency <= 2)
			return 1;

		return 2;
	}

	inline bool CanWorkerStartNormalCompile(bool priorityOnly, bool urgentWorkPending)
	{
		return !priorityOnly && !urgentWorkPending;
	}

	inline std::string BuildCompileJobIdentity(
		const std::string& ownerID,
		const std::string& defineKey,
		const std::string& sourceFingerprint,
		uint64_t operationID)
	{
		return ownerID + "|" + defineKey + "|" + sourceFingerprint + "|" + std::to_string(operationID);
	}

	struct VariantUseEntry
	{
		std::atomic<uint64_t> shaderHash{0};
		std::atomic<uint64_t> lastUseMilliseconds{0};
	};

	class VariantUseTracker
	{
		static constexpr size_t entryCount = 4096;
		static constexpr size_t probeCount = 8;
		static constexpr uint64_t activeWindowMilliseconds = 10000;
		std::array<VariantUseEntry, entryCount> entries;

		static uint64_t CurrentTimeMilliseconds()
		{
			return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count());
		}

		static void StoreLatestTime(VariantUseEntry& entry, uint64_t nowMilliseconds)
		{
			uint64_t latestUse = entry.lastUseMilliseconds.load(std::memory_order_relaxed);
			while (latestUse < nowMilliseconds && !entry.lastUseMilliseconds.compare_exchange_weak(
				latestUse, nowMilliseconds, std::memory_order_relaxed))
			{
			}
		}

	  public:
		void Record(uint64_t shaderHash)
		{
			RecordAt(shaderHash, CurrentTimeMilliseconds());
		}

		uint64_t GetStamp(uint64_t shaderHash) const
		{
			return GetStampAt(shaderHash, CurrentTimeMilliseconds());
		}

		void RecordAt(uint64_t shaderHash, uint64_t nowMilliseconds)
		{
			if (shaderHash == 0)
				return;

			const size_t firstSlot = static_cast<size_t>(shaderHash % entryCount);
			size_t oldestSlot = firstSlot;
			uint64_t oldestStamp = UINT64_MAX;

			for (size_t probeIndex = 0; probeIndex < probeCount; ++probeIndex)
			{
				VariantUseEntry& entry = entries[(firstSlot + probeIndex) % entryCount];
				const uint64_t existingHash = entry.shaderHash.load(std::memory_order_relaxed);

				if (existingHash == shaderHash)
				{
					StoreLatestTime(entry, nowMilliseconds);
					return;
				}

				if (existingHash == 0)
				{
					uint64_t emptyHash = 0;
					if (entry.shaderHash.compare_exchange_strong(emptyHash, shaderHash, std::memory_order_relaxed))
					{
						entry.lastUseMilliseconds.store(nowMilliseconds, std::memory_order_relaxed);
						return;
					}

					if (emptyHash == shaderHash)
					{
						StoreLatestTime(entry, nowMilliseconds);
						return;
					}
				}

				const uint64_t lastUseMilliseconds = entry.lastUseMilliseconds.load(std::memory_order_relaxed);
				if (lastUseMilliseconds < oldestStamp)
				{
					oldestStamp = lastUseMilliseconds;
					oldestSlot = (firstSlot + probeIndex) % entryCount;
				}
			}

			VariantUseEntry& replacementEntry = entries[oldestSlot];
			const uint64_t previousHash = replacementEntry.shaderHash.load(std::memory_order_relaxed);
			if (previousHash != 0 && nowMilliseconds >= replacementEntry.lastUseMilliseconds.load(std::memory_order_relaxed) &&
				nowMilliseconds - replacementEntry.lastUseMilliseconds.load(std::memory_order_relaxed) < activeWindowMilliseconds)
				return;

			replacementEntry.shaderHash.store(shaderHash, std::memory_order_relaxed);
			replacementEntry.lastUseMilliseconds.store(nowMilliseconds, std::memory_order_relaxed);
		}

		uint64_t GetStampAt(uint64_t shaderHash, uint64_t nowMilliseconds) const
		{
			if (shaderHash == 0)
				return 0;

			const size_t firstSlot = static_cast<size_t>(shaderHash % entryCount);
			for (size_t probeIndex = 0; probeIndex < probeCount; ++probeIndex)
			{
				const VariantUseEntry& entry = entries[(firstSlot + probeIndex) % entryCount];
				const uint64_t existingHash = entry.shaderHash.load(std::memory_order_relaxed);

				if (existingHash == shaderHash)
				{
					const uint64_t lastUseMilliseconds = entry.lastUseMilliseconds.load(std::memory_order_relaxed);
					if (nowMilliseconds < lastUseMilliseconds || nowMilliseconds - lastUseMilliseconds > activeWindowMilliseconds)
						return 0;

					return lastUseMilliseconds;
				}

				if (existingHash == 0)
					return 0;
			}

			return 0;
		}
	};

	template <typename CompileJob>
	void InsertCompileJobByPriority(std::deque<CompileJob>& compileQueue, CompileJob compileJob)
	{
		if (!compileJob.urgent)
		{
			compileQueue.push_back(std::move(compileJob));
			return;
		}

		const auto firstNormalJob = std::find_if(compileQueue.begin(), compileQueue.end(), [](const CompileJob& queuedJob)
		{
			return !queuedJob.urgent;
		});
		compileQueue.insert(firstNormalJob, std::move(compileJob));
	}
} //namespace ModifiedShader
