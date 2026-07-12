#include "ShaderAutomaticDiscovery.h"

#include <deque>
#include <vector>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <wrl/client.h>

#include "DatabaseModifiedShaders.h"
#include "DatabaseShaderTargets.h"
#include "Globals.h"
#include "Hash.h"
#include "HookD3D12.h"
#include "HookD3D12PipelineUtils.h"
#include "ShaderAnalysis.h"
#include "ShaderDiscovery.h"
#include "ShaderInjectorGUI.h"
#include "StringHelper.h"

namespace ShaderAutomaticDiscovery
{
	namespace
	{
		constexpr size_t maximumQueuedShaders = 8192;
		constexpr size_t defaultMaximumPendingAnalyses = 64;
		constexpr size_t maximumPendingAnalysesLimit = 1024;

		size_t AutomaticAnalysisWorkerThreadCount()
		{
			const unsigned int detectedThreads = std::thread::hardware_concurrency();
			return (std::clamp<size_t>)(detectedThreads > 0 ? detectedThreads / 2 : 4, 2, 16);
		}

		size_t ConfiguredAnalysisWorkerThreadCount()
		{
			if (Globals::gShaderDiscoveryWorkerThreads > 0)
				return (std::clamp<size_t>)(static_cast<size_t>(Globals::gShaderDiscoveryWorkerThreads), 1, 32);

			return AutomaticAnalysisWorkerThreadCount();
		}

		size_t ConfiguredPendingAnalysisLimit()
		{
			if (Globals::gShaderDiscoveryPendingAnalysisLimit <= 0)
				return defaultMaximumPendingAnalyses;

			return (std::clamp<size_t>)(
				static_cast<size_t>(Globals::gShaderDiscoveryPendingAnalysisLimit),
				1,
				maximumPendingAnalysesLimit);
		}

		struct ShaderKey
		{
			uint64_t hash = 0;
			ShaderTarget::ShaderType type = ShaderTarget::Unknown;

			bool operator==(const ShaderKey& other) const
			{
				return hash == other.hash && type == other.type;
			}
		};

		struct ShaderKeyHasher
		{
			size_t operator()(const ShaderKey& key) const
			{
				return static_cast<size_t>(key.hash ^ (static_cast<uint64_t>(key.type) << 57));
			}
		};

		enum class PipelineSource
		{
			Graphics,
			Stream,
		};

		struct QueuedShader
		{
			ShaderKey key;
			PipelineSource source = PipelineSource::Stream;
			int pipelineIndex = -1;
			double priority = 0.0;
			Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;
			std::vector<uint8_t> shaderBytecode;
		};

		struct AnalysisJob
		{
			QueuedShader queuedShader;
			std::shared_ptr<const std::vector<ModifiedShader::PackageDisk>> modifiedShaders;
		};

		struct AnalysisResult
		{
			QueuedShader queuedShader;
			std::string modifiedShaderId;
			ShaderAnalysis::ShaderAnalysisDisk shaderAnalysis;
		};

		std::mutex gQueueMutex;
		std::deque<QueuedShader> gQueuedShaders;
		std::deque<AnalysisJob> gAnalysisJobs;
		std::deque<AnalysisResult> gAnalysisResults;
		std::unordered_set<ShaderKey, ShaderKeyHasher> gSubmittedShaders;
		std::unordered_map<ShaderKey, std::string, ShaderKeyHasher> gDirectMatchShaders;
		std::unordered_set<std::string> gModifiedShadersWithGeneratedTargets;
		std::shared_ptr<const std::vector<ModifiedShader::PackageDisk>> gModifiedShaderAnalysisSnapshot;
		bool gCompatibleShaderTypes[static_cast<size_t>(ShaderTarget::Unknown) + 1]{};
		bool gSeenShaderTypes[static_cast<size_t>(ShaderTarget::Unknown) + 1]{};
		std::vector<std::pair<size_t, size_t>> gPlausibleByteLengthRanges[static_cast<size_t>(ShaderTarget::Unknown) + 1];
		std::chrono::steady_clock::time_point gSessionStartTime{};
		bool gSessionStartTimeInitialized = false;
		bool gNeverSeenShaderTypeDiagnosticLogged = false;
		constexpr std::chrono::seconds neverSeenShaderTypeDiagnosticDelay{60};
		constexpr std::chrono::seconds progressLogInterval{5};
		constexpr uint64_t progressLogCompletedStep = 25;
		constexpr uint64_t progressLogQueuedStep = 100;
		std::condition_variable gAnalysisCondition;
		std::vector<std::thread> gAnalysisWorkers;
		bool gAnalysisWorkerRunning = false;
		size_t gRunningAnalysisWorkerCount = 0;
		bool gAnalysisStopRequested = false;
		bool gAcceptingWork = true;
		size_t gActiveAnalysisJobs = 0;
		uint64_t gQueuedShaderTotal = 0;
		uint64_t gAnalysisSubmittedTotal = 0;
		uint64_t gAnalysisCompletedTotal = 0;
		uint64_t gAnalysisMatchedTotal = 0;
		uint64_t gAnalysisFailedTotal = 0;
		uint64_t gDirectMatchProcessedTotal = 0;
		uint64_t gShaderTargetsCreatedTotal = 0;
		uint64_t gByteLengthRejectedTotal = 0;
		uint64_t gQueueDroppedTotal = 0;
		uint64_t gQueueEvictedTotal = 0;
		uint64_t gLastLoggedProcessedTotal = 0;
		uint64_t gLastLoggedQueuedTotal = 0;
		std::chrono::steady_clock::time_point gLastProgressLogTime{};
		bool gProgressWasActive = false;
		// Newer game patches tend to compile shaders slightly smaller than the version a mod
		// package was originally built against, so the lower bound gets more slack than the
		// upper bound.
		constexpr size_t byteLengthLowerTolerancePercent = 35;
		constexpr size_t byteLengthUpperTolerancePercent = 15;

		bool ParseByteLength(const std::string& byteLengthText, size_t& outByteLength)
		{
			if (byteLengthText.empty())
				return false;

			char* parseEnd = nullptr;
			const unsigned long long parsedLength = std::strtoull(byteLengthText.c_str(), &parseEnd, 10);
			if (!parseEnd || *parseEnd != '\0' || parsedLength == 0)
				return false;

			outByteLength = static_cast<size_t>(parsedLength);
			return true;
		}

		void AddPlausibleByteLengthRange(ShaderTarget::ShaderType shaderType, size_t byteLength)
		{
			const size_t shaderTypeIndex = static_cast<size_t>(shaderType);
			if (shaderTypeIndex >= static_cast<size_t>(ShaderTarget::Unknown) || byteLength == 0)
				return;

			const size_t lowerTolerance = (std::max<size_t>)(1, (byteLength * byteLengthLowerTolerancePercent) / 100);
			const size_t upperTolerance = (std::max<size_t>)(1, (byteLength * byteLengthUpperTolerancePercent) / 100);
			const size_t minimumLength = byteLength > lowerTolerance ? byteLength - lowerTolerance : 1;
			const size_t maximumLength = byteLength + upperTolerance;
			gPlausibleByteLengthRanges[shaderTypeIndex].push_back({ minimumLength, maximumLength });
		}

		// Combines overlapping ranges into one sorted list per shader type, so lookups can use
		// binary search instead of checking every range one by one.
		void MergePlausibleByteLengthRanges()
		{
			for (auto& ranges : gPlausibleByteLengthRanges)
			{
				if (ranges.size() < 2)
					continue;

				std::sort(ranges.begin(), ranges.end());

				std::vector<std::pair<size_t, size_t>> merged;
				merged.reserve(ranges.size());
				for (const auto& range : ranges)
				{
					if (!merged.empty() && range.first <= merged.back().second)
						merged.back().second = (std::max)(merged.back().second, range.second);
					else
						merged.push_back(range);
				}
				ranges = std::move(merged);
			}
		}

		bool HasPlausibleByteLength(ShaderTarget::ShaderType shaderType, size_t byteLength)
		{
			const size_t shaderTypeIndex = static_cast<size_t>(shaderType);
			if (shaderTypeIndex >= static_cast<size_t>(ShaderTarget::Unknown))
				return false;

			const std::vector<std::pair<size_t, size_t>>& ranges = gPlausibleByteLengthRanges[shaderTypeIndex];
			if (ranges.empty())
				return true;

			// Find the last range that starts at or before byteLength, then check if it fits inside it.
			const auto it = std::upper_bound(ranges.begin(), ranges.end(), byteLength,
				[](size_t value, const std::pair<size_t, size_t>& range) { return value < range.first; });
			if (it == ranges.begin())
				return false;
			const auto& candidate = *std::prev(it);
			return byteLength >= candidate.first && byteLength <= candidate.second;
		}

		double ByteLengthSimilarity(size_t candidateLength, size_t referenceLength)
		{
			if (candidateLength == 0 || referenceLength == 0)
				return 0.0;

			const double largerLength = static_cast<double>((std::max)(candidateLength, referenceLength));
			const double difference = std::fabs(static_cast<double>(candidateLength) - static_cast<double>(referenceLength));
			return (std::max)(0.0, 1.0 - (difference / largerLength));
		}

		double ShaderTypeQueuePriorityBonus(ShaderTarget::ShaderType shaderType)
		{
			// Prioritize pixel shader candidates over compute shaders or other shader types for now.
			// Currently the mod uses far more pixel shaders and these make up the bulk of the rendering
			// replacement work.
			return shaderType == ShaderTarget::PixelShader ? 100.0 : 0.0;
		}

		double CalculateQueuePriorityLocked(
			ShaderTarget::ShaderType shaderType,
			size_t byteLength,
			bool force,
			bool directMatch)
		{
			if (force)
				return 1000.0;
			if (directMatch)
				return 900.0;

			double bestLengthSimilarity = 0.0;
			if (gModifiedShaderAnalysisSnapshot)
			{
				for (const ModifiedShader::PackageDisk& modifiedShader : *gModifiedShaderAnalysisSnapshot)
				{
					if (!modifiedShader.enabled || modifiedShader.shaderType != shaderType)
						continue;

					for (const ModifiedShader::TargetDisk& target : modifiedShader.targets)
					{
						size_t targetLength = 0;
						if (!ParseByteLength(target.originalShaderBytecodeLength, targetLength))
							continue;

						bestLengthSimilarity = (std::max)(
							bestLengthSimilarity,
							ByteLengthSimilarity(byteLength, targetLength));
					}
				}
			}

			// Most cross-version matches are still close in byte size. This is deliberately
			// only a queue ordering hint; the full bytecode analysis remains the authority.
			return ShaderTypeQueuePriorityBonus(shaderType) + bestLengthSimilarity;
		}

		std::deque<QueuedShader>::iterator FindLowestPriorityQueuedShaderLocked()
		{
			if (gQueuedShaders.empty())
				return gQueuedShaders.end();

			auto lowestPriorityIterator = gQueuedShaders.begin();
			for (auto queuedIterator = std::next(gQueuedShaders.begin()); queuedIterator != gQueuedShaders.end(); ++queuedIterator)
			{
				if (queuedIterator->priority < lowestPriorityIterator->priority)
					lowestPriorityIterator = queuedIterator;
			}
			return lowestPriorityIterator;
		}

		bool PopHighestPriorityQueuedShaderLocked(QueuedShader& outQueuedShader)
		{
			if (gQueuedShaders.empty())
				return false;

			auto highestPriorityIterator = gQueuedShaders.begin();
			for (auto queuedIterator = std::next(gQueuedShaders.begin()); queuedIterator != gQueuedShaders.end(); ++queuedIterator)
			{
				if (queuedIterator->priority > highestPriorityIterator->priority)
					highestPriorityIterator = queuedIterator;
			}

			outQueuedShader = std::move(*highestPriorityIterator);
			gQueuedShaders.erase(highestPriorityIterator);
			return true;
		}

		bool TargetContainsHash(const ShaderTarget::ShaderTargetDisk& target, uint64_t shaderHash)
		{
			if (Hash::ParseHashText(target.originalShaderBytecodeHash) == shaderHash)
				return true;

			for (const std::string& aliasHash : target.shaderBytecodeHashAliases)
			{
				if (Hash::ParseHashText(aliasHash) == shaderHash)
					return true;
			}
			return false;
		}

		uint64_t ProcessedShaderTotalLocked()
		{
			return gAnalysisCompletedTotal + gDirectMatchProcessedTotal;
		}

		bool HasPendingDiscoveryWorkLocked()
		{
			return !gQueuedShaders.empty() ||
				!gAnalysisJobs.empty() ||
				!gAnalysisResults.empty() ||
				gActiveAnalysisJobs > 0;
		}

		std::string BuildDiscoveryProgressMessageLocked(const char* reason)
		{
			const uint64_t processedTotal = ProcessedShaderTotalLocked();
			const uint64_t resolvedTotal = processedTotal + gQueueEvictedTotal;
			const uint64_t rejectedTotal = gAnalysisCompletedTotal >= gAnalysisMatchedTotal ?
				gAnalysisCompletedTotal - gAnalysisMatchedTotal : 0;
			const double processedPercent = gQueuedShaderTotal > 0 ?
				(static_cast<double>(processedTotal) * 100.0) / static_cast<double>(gQueuedShaderTotal) : 100.0;
			const double resolvedPercent = gQueuedShaderTotal > 0 ?
				(static_cast<double>(resolvedTotal) * 100.0) / static_cast<double>(gQueuedShaderTotal) : 100.0;
			const double analysisPercent = gAnalysisSubmittedTotal > 0 ?
				(static_cast<double>(gAnalysisCompletedTotal) * 100.0) / static_cast<double>(gAnalysisSubmittedTotal) : 100.0;

			char processedPercentText[32]{};
			char resolvedPercentText[32]{};
			char analysisPercentText[32]{};
			sprintf_s(processedPercentText, "%.1f", processedPercent);
			sprintf_s(resolvedPercentText, "%.1f", resolvedPercent);
			sprintf_s(analysisPercentText, "%.1f", analysisPercent);

			return "ShaderAutomaticDiscovery->Progress: " + std::string(reason) +
				" processed=" + std::to_string(processedTotal) + "/" + std::to_string(gQueuedShaderTotal) +
				" (" + processedPercentText + "%)" +
				" resolved=" + std::to_string(resolvedTotal) + "/" + std::to_string(gQueuedShaderTotal) +
				" (" + resolvedPercentText + "%)" +
				" analyzed=" + std::to_string(gAnalysisCompletedTotal) + "/" + std::to_string(gAnalysisSubmittedTotal) +
				" (" + analysisPercentText + "%)" +
				" directMatches=" + std::to_string(gDirectMatchProcessedTotal) +
				" analysisMatches=" + std::to_string(gAnalysisMatchedTotal) +
				" analysisRejected=" + std::to_string(rejectedTotal) +
				" analysisFailed=" + std::to_string(gAnalysisFailedTotal) +
				" targetsCreated=" + std::to_string(gShaderTargetsCreatedTotal) +
				" byteLengthRejected=" + std::to_string(gByteLengthRejectedTotal) +
				" queueDropped=" + std::to_string(gQueueDroppedTotal) +
				" queueEvicted=" + std::to_string(gQueueEvictedTotal) +
				" queued=" + std::to_string(gQueuedShaders.size()) +
				" pendingAnalysis=" + std::to_string(gAnalysisJobs.size()) +
				" activeAnalysis=" + std::to_string(gActiveAnalysisJobs) +
				" pendingResults=" + std::to_string(gAnalysisResults.size()) +
				" workerThreads=" + std::to_string(ConfiguredAnalysisWorkerThreadCount()) +
				" frameJobBudget=" + std::to_string((std::clamp)(Globals::gShaderDiscoveryFrameJobBudget, 1, 8192)) +
				" pendingAnalysisLimit=" + std::to_string(ConfiguredPendingAnalysisLimit()) +
				" minimumSimilarityScore=" + std::to_string(Globals::gShaderDiscoveryMinimumSimilarityScore) +
				" similarityAmbiguityMargin=" + std::to_string(Globals::gShaderDiscoverySimilarityAmbiguityMargin);
		}

		void LogMissingModifiedShaderTargets();

		void MaybeLogDiscoveryProgress(const char* reason, bool force = false)
		{
			std::string progressMessage;
			bool logMissingTargets = false;
			{
				std::lock_guard<std::mutex> lock(gQueueMutex);
				const auto now = std::chrono::steady_clock::now();
				const bool active = HasPendingDiscoveryWorkLocked();
				const uint64_t processedTotal = ProcessedShaderTotalLocked();
				const bool completedEnoughWork = processedTotal >= gLastLoggedProcessedTotal + progressLogCompletedStep;
				const bool queuedEnoughWork = gQueuedShaderTotal >= gLastLoggedQueuedTotal + progressLogQueuedStep;
				const bool waitedLongEnough = gLastProgressLogTime.time_since_epoch().count() == 0 ||
					now - gLastProgressLogTime >= progressLogInterval;
				const bool justFinished = gProgressWasActive && !active;

				if (!force && !justFinished && (!active || (!completedEnoughWork && !queuedEnoughWork && !waitedLongEnough)))
					return;

				progressMessage = BuildDiscoveryProgressMessageLocked(justFinished ? "complete" : reason);
				gLastLoggedProcessedTotal = processedTotal;
				gLastLoggedQueuedTotal = gQueuedShaderTotal;
				gLastProgressLogTime = now;
				gProgressWasActive = active;
				logMissingTargets = justFinished;
			}

			ShaderInjectorGUI::WriteToRuntimeLog(progressMessage);
			if (logMissingTargets)
				LogMissingModifiedShaderTargets();
		}

		void MarkAnalysisCompleted(bool matched, bool failed)
		{
			std::lock_guard<std::mutex> lock(gQueueMutex);
			if (gActiveAnalysisJobs > 0)
				--gActiveAnalysisJobs;
			++gAnalysisCompletedTotal;
			if (matched)
				++gAnalysisMatchedTotal;
			if (failed)
				++gAnalysisFailedTotal;
		}

		void MarkShaderTargetCreated(const std::string& modifiedShaderId)
		{
			std::lock_guard<std::mutex> lock(gQueueMutex);
			++gShaderTargetsCreatedTotal;
			if (!modifiedShaderId.empty())
				gModifiedShadersWithGeneratedTargets.insert(modifiedShaderId);
		}

		void LogMissingModifiedShaderTargets()
		{
			std::unordered_set<std::string> modifiedShaderIdsWithTargets;
			{
				std::lock_guard<std::mutex> lock(gQueueMutex);
				modifiedShaderIdsWithTargets = gModifiedShadersWithGeneratedTargets;
			}

			if (HookD3D12::gLoadedShaderTargetsOnce)
			{
				for (const ShaderTarget::ShaderTargetDisk& shaderTarget : HookD3D12::gLoadedShaderTargets)
				{
					if (!shaderTarget.modifiedShaderId.empty())
						modifiedShaderIdsWithTargets.insert(shaderTarget.modifiedShaderId);
				}
			}

			size_t enabledModifiedShaderCount = 0;
			size_t modifiedShadersWithTargets = 0;
			for (const ModifiedShader::PackageDisk& modifiedShader : DatabaseModifiedShaders::GetModifiedShaders())
			{
				if (!modifiedShader.enabled || modifiedShader.shaderType == ShaderTarget::Unknown)
					continue;

				++enabledModifiedShaderCount;
				if (modifiedShaderIdsWithTargets.find(modifiedShader.id) != modifiedShaderIdsWithTargets.end())
				{
					++modifiedShadersWithTargets;
					continue;
				}

				ShaderInjectorGUI::WriteToRuntimeLogWarning(
					"ShaderAutomaticDiscovery->Summary: no ShaderTarget generated for ModifiedShader id=" +
					modifiedShader.id +
					" name=\"" + modifiedShader.name + "\"" +
					" shaderType=" + StringHelper::ShaderTypeToString(modifiedShader.shaderType) +
					" knownTargets=" + std::to_string(modifiedShader.targets.size()));
			}

			ShaderInjectorGUI::WriteToRuntimeLog(
				"ShaderAutomaticDiscovery->Summary: ModifiedShaders with ShaderTargets=" +
				std::to_string(modifiedShadersWithTargets) + "/" + std::to_string(enabledModifiedShaderCount));
		}

		void RebindGraphicsPipelinePointers(HookD3D12::GraphicsPipelineInfo& pipeline)
		{
			pipeline.originalDesc.VS = { pipeline.vsBytecode.empty() ? nullptr : pipeline.vsBytecode.data(), pipeline.vsBytecode.size() };
			pipeline.originalDesc.PS = { pipeline.psBytecode.empty() ? nullptr : pipeline.psBytecode.data(), pipeline.psBytecode.size() };
			pipeline.originalDesc.GS = { pipeline.gsBytecode.empty() ? nullptr : pipeline.gsBytecode.data(), pipeline.gsBytecode.size() };
			pipeline.originalDesc.HS = { pipeline.hsBytecode.empty() ? nullptr : pipeline.hsBytecode.data(), pipeline.hsBytecode.size() };
			pipeline.originalDesc.DS = { pipeline.dsBytecode.empty() ? nullptr : pipeline.dsBytecode.data(), pipeline.dsBytecode.size() };
			pipeline.originalDesc.InputLayout.pInputElementDescs = pipeline.inputElements.empty() ? nullptr : pipeline.inputElements.data();
			pipeline.originalDesc.InputLayout.NumElements = static_cast<UINT>(pipeline.inputElements.size());
			pipeline.originalDesc.StreamOutput.pSODeclaration = pipeline.soDeclarations.empty() ? nullptr : pipeline.soDeclarations.data();
			pipeline.originalDesc.StreamOutput.NumEntries = static_cast<UINT>(pipeline.soDeclarations.size());
			pipeline.originalDesc.StreamOutput.pBufferStrides = pipeline.soStrides.empty() ? nullptr : pipeline.soStrides.data();
			pipeline.originalDesc.StreamOutput.NumStrides = static_cast<UINT>(pipeline.soStrides.size());
		}

		bool Enqueue(
			PipelineSource source,
			ID3D12PipelineState* pipelineState,
			int pipelineIndex,
			ShaderTarget::ShaderType shaderType,
			uint64_t shaderHash,
			const std::vector<uint8_t>& shaderBytecode,
			bool force)
		{
			if (!pipelineState || shaderHash == 0 || shaderBytecode.empty())
				return false;

			const ShaderKey key{ shaderHash, shaderType };
			std::lock_guard<std::mutex> lock(gQueueMutex);
			if (!gAcceptingWork)
				return false;

			const size_t shaderTypeIndex = static_cast<size_t>(shaderType);
			if (shaderTypeIndex >= static_cast<size_t>(ShaderTarget::Unknown) ||
				!gCompatibleShaderTypes[shaderTypeIndex])
			{
				return true;
			}
			gSeenShaderTypes[shaderTypeIndex] = true;
			const auto directMatchIterator = gDirectMatchShaders.find(key);
			const bool directMatch = directMatchIterator != gDirectMatchShaders.end() &&
				!directMatchIterator->second.empty();
			if (!force && !directMatch && !HasPlausibleByteLength(shaderType, shaderBytecode.size()))
			{
				++gByteLengthRejectedTotal;
				return true;
			}

			if (!force && !gSubmittedShaders.insert(key).second)
				return true;
			if (force)
			{
				gSubmittedShaders.insert(key);
				for (auto queuedIterator = gQueuedShaders.begin(); queuedIterator != gQueuedShaders.end();)
				{
					if (queuedIterator->key == key)
						queuedIterator = gQueuedShaders.erase(queuedIterator);
					else
						++queuedIterator;
				}
			}

			const double queuePriority = CalculateQueuePriorityLocked(shaderType, shaderBytecode.size(), force, directMatch);
			if (gQueuedShaders.size() >= maximumQueuedShaders)
			{
				auto lowestPriorityIterator = FindLowestPriorityQueuedShaderLocked();
				if (lowestPriorityIterator == gQueuedShaders.end())
				{
					gSubmittedShaders.erase(key);
					++gQueueDroppedTotal;
					return false;
				}

				if (!force && queuePriority <= lowestPriorityIterator->priority)
				{
					gSubmittedShaders.erase(key);
					++gQueueDroppedTotal;
					return false;
				}

				gSubmittedShaders.erase(lowestPriorityIterator->key);
				gQueuedShaders.erase(lowestPriorityIterator);
				++gQueueEvictedTotal;
			}

			QueuedShader queued{};
			queued.key = key;
			queued.source = source;
			queued.pipelineIndex = pipelineIndex;
			queued.priority = queuePriority;
			queued.pipelineState = pipelineState;
			queued.shaderBytecode = shaderBytecode;
			if (force || directMatch)
				gQueuedShaders.push_front(std::move(queued));
			else
				gQueuedShaders.push_back(std::move(queued));
			++gQueuedShaderTotal;
			return true;
		}

		bool CopyGraphicsPipeline(ID3D12PipelineState* pipelineState, HookD3D12::GraphicsPipelineInfo& outPipeline)
		{
			std::lock_guard<std::mutex> lock(HookD3D12::gPipelineMutex);
			for (const HookD3D12::GraphicsPipelineInfo& pipeline : HookD3D12::gGraphicsPipelines)
			{
				if (pipeline.pipelineState != pipelineState)
					continue;

				outPipeline = pipeline;
				if (outPipeline.originalDesc.pRootSignature)
					outPipeline.originalDesc.pRootSignature->AddRef();
				RebindGraphicsPipelinePointers(outPipeline);
				return true;
			}
			return false;
		}

		bool CopyStreamPipeline(ID3D12PipelineState* pipelineState, HookD3D12::PipelineStateInfo& outPipeline)
		{
			std::lock_guard<std::mutex> lock(HookD3D12::gPipelineMutex);
			for (const HookD3D12::PipelineStateInfo& pipeline : HookD3D12::gPipelineStates)
			{
				if (pipeline.pipelineState != pipelineState)
					continue;

				outPipeline = pipeline;
				if (outPipeline.rootSignature)
					outPipeline.rootSignature->AddRef();
				HookD3D12::RebindPipelineStateInfoPointerFields(outPipeline);
				return true;
			}
			return false;
		}

		template<typename PipelineType>
		bool CreateTargetForMatch(
			const char* sourceList,
			PipelineType& pipeline,
			const QueuedShader& queued,
			const std::string& modifiedShaderId,
			const ShaderAnalysis::ShaderAnalysisDisk* shaderAnalysis)
		{
			if (!HookD3D12::CreateShaderTargetForPipeline(
				sourceList,
				queued.pipelineIndex,
				queued.key.type,
				queued.key.hash,
				queued.shaderBytecode.size(),
				queued.shaderBytecode.data(),
				pipeline,
				modifiedShaderId,
				false,
				shaderAnalysis))
			{
				return false;
			}

			HookD3D12::MarkShaderTargetApplyDirty();
			MarkShaderTargetCreated(modifiedShaderId);
			ShaderInjectorGUI::WriteToRuntimeLogSuccess(
				"ShaderAutomaticDiscovery: created ShaderTarget for " + Hash::FormatHash(queued.key.hash) +
				" using " + modifiedShaderId);
			return true;
		}

		void ProcessMatchedShader(QueuedShader& queued, const std::string& modifiedShaderId, const ShaderAnalysis::ShaderAnalysisDisk* shaderAnalysis = nullptr)
		{
			if (!HookD3D12::gLoadedShaderTargetsOnce)
				HookD3D12::RefreshLoadedShaderTargets();

			for (int targetIndex = 0; targetIndex < static_cast<int>(HookD3D12::gLoadedShaderTargets.size()); ++targetIndex)
			{
				const ShaderTarget::ShaderTargetDisk& existingTarget = HookD3D12::gLoadedShaderTargets[targetIndex];
				if (existingTarget.shaderType != queued.key.type || !TargetContainsHash(existingTarget, queued.key.hash))
					continue;

				if (existingTarget.modifiedShaderId != modifiedShaderId)
				{
					ShaderInjectorGUI::WriteToRuntimeLogError(
						"ShaderAutomaticDiscovery: shader " + Hash::FormatHash(queued.key.hash) +
						" already belongs to a target using a different ModifiedShader");
					return;
				}
				if (!existingTarget.enabled)
					return;

				const ModifiedShader::PackageDisk* existingPackage =
					DatabaseModifiedShaders::FindModifiedShaderById(modifiedShaderId);
				if (existingPackage && existingPackage->compiledBlob.empty())
				{
					if (!DatabaseModifiedShaders::CompileModifiedShader(modifiedShaderId) ||
						!HookD3D12::ReloadShaderTarget(targetIndex))
					{
						return;
					}
				}
				HookD3D12::MarkShaderTargetApplyDirty();
				return;
			}

			const ModifiedShader::PackageDisk* selectedPackage =
				DatabaseModifiedShaders::FindModifiedShaderById(modifiedShaderId);
			if (!selectedPackage)
				return;
			if (selectedPackage->compiledBlob.empty() &&
				!DatabaseModifiedShaders::CompileModifiedShader(modifiedShaderId))
			{
				ShaderInjectorGUI::WriteToRuntimeLogError("ShaderAutomaticDiscovery: failed to compile " + modifiedShaderId);
				return;
			}

			if (queued.source == PipelineSource::Graphics)
			{
				HookD3D12::GraphicsPipelineInfo pipeline{};
				if (CopyGraphicsPipeline(queued.pipelineState.Get(), pipeline))
				{
					CreateTargetForMatch("Graphics", pipeline, queued, modifiedShaderId, shaderAnalysis);
					if (pipeline.originalDesc.pRootSignature)
						pipeline.originalDesc.pRootSignature->Release();
				}
			}
			else
			{
				HookD3D12::PipelineStateInfo pipeline{};
				if (CopyStreamPipeline(queued.pipelineState.Get(), pipeline))
				{
					CreateTargetForMatch("Stream", pipeline, queued, modifiedShaderId, shaderAnalysis);
					if (pipeline.rootSignature)
						pipeline.rootSignature->Release();
				}
			}
		}

		void AnalysisWorkerMain()
		{
			SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
			const HRESULT comInitializationResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

			for (;;)
			{
				AnalysisJob job{};
				{
					std::unique_lock<std::mutex> lock(gQueueMutex);
					gAnalysisCondition.wait(lock, []
					{
						return gAnalysisStopRequested || !gAnalysisJobs.empty();
					});

					if (gAnalysisStopRequested && gAnalysisJobs.empty())
						break;

					job = std::move(gAnalysisJobs.front());
					gAnalysisJobs.pop_front();
					++gActiveAnalysisJobs;
				}

				try
				{
					ShaderAnalysis::ShaderAnalysisDisk candidateAnalysis{};
					const int modifiedShaderIndex = ShaderDiscovery::DiscoverEnabledModifiedShader(
						job.queuedShader.key.hash,
						job.queuedShader.key.type,
						job.queuedShader.shaderBytecode,
						*job.modifiedShaders,
						&candidateAnalysis);
					if (modifiedShaderIndex < 0 || modifiedShaderIndex >= static_cast<int>(job.modifiedShaders->size()))
					{
						MarkAnalysisCompleted(false, false);
						continue;
					}

					AnalysisResult result{};
					result.queuedShader = std::move(job.queuedShader);
					result.modifiedShaderId = (*job.modifiedShaders)[modifiedShaderIndex].id;
					result.shaderAnalysis = std::move(candidateAnalysis);
					{
						std::lock_guard<std::mutex> lock(gQueueMutex);
						gAnalysisResults.push_back(std::move(result));
					}
					MarkAnalysisCompleted(true, false);
				}
				catch (...)
				{
					MarkAnalysisCompleted(false, true);
					ShaderInjectorGUI::WriteToRuntimeLogError(
						"ShaderAutomaticDiscovery: background shader analysis failed unexpectedly");
				}
			}

			if (SUCCEEDED(comInitializationResult))
				CoUninitialize();

			// Last thread exiting must not clear gAnalysisWorkerRunning while others are still alive.
			std::lock_guard<std::mutex> lock(gQueueMutex);
			if (gRunningAnalysisWorkerCount > 0)
				--gRunningAnalysisWorkerCount;
			gAnalysisWorkerRunning = gRunningAnalysisWorkerCount > 0;
		}

		void EnsureAnalysisWorkerStartedLocked()
		{
			if (gAnalysisWorkerRunning)
				return;

			gAnalysisStopRequested = false;
			const size_t analysisWorkerThreadCount = ConfiguredAnalysisWorkerThreadCount();
			gAnalysisWorkers.reserve(analysisWorkerThreadCount);
			for (size_t workerIndex = 0; workerIndex < analysisWorkerThreadCount; ++workerIndex)
			{
				++gRunningAnalysisWorkerCount;
				gAnalysisWorkers.emplace_back(AnalysisWorkerMain);
			}
			gAnalysisWorkerRunning = true;
		}

		bool TrySubmitAnalysis(QueuedShader& queued)
		{
			std::lock_guard<std::mutex> lock(gQueueMutex);
			if (gAnalysisJobs.size() >= ConfiguredPendingAnalysisLimit() || !gModifiedShaderAnalysisSnapshot)
				return false;

			EnsureAnalysisWorkerStartedLocked();
			AnalysisJob job{};
			job.queuedShader = std::move(queued);
			job.modifiedShaders = gModifiedShaderAnalysisSnapshot;
			gAnalysisJobs.push_back(std::move(job));
			++gAnalysisSubmittedTotal;
			gAnalysisCondition.notify_one();
			return true;
		}

		void CollectNeverSeenCompatibleShaderTypesForDiagnostic(std::vector<ShaderTarget::ShaderType>& outNeverSeenShaderTypes)
		{
			if (gNeverSeenShaderTypeDiagnosticLogged)
				return;

			if (!gSessionStartTimeInitialized)
			{
				gSessionStartTime = std::chrono::steady_clock::now();
				gSessionStartTimeInitialized = true;
				return;
			}

			if (std::chrono::steady_clock::now() - gSessionStartTime < neverSeenShaderTypeDiagnosticDelay)
				return;

			gNeverSeenShaderTypeDiagnosticLogged = true;

			for (size_t shaderTypeIndex = 0; shaderTypeIndex < static_cast<size_t>(ShaderTarget::Unknown); ++shaderTypeIndex)
			{
				if (!gCompatibleShaderTypes[shaderTypeIndex] || gSeenShaderTypes[shaderTypeIndex])
					continue;

				outNeverSeenShaderTypes.push_back(static_cast<ShaderTarget::ShaderType>(shaderTypeIndex));
			}
		}

		void LogNeverSeenCompatibleShaderTypes(const std::vector<ShaderTarget::ShaderType>& neverSeenShaderTypes)
		{
			if (neverSeenShaderTypes.empty())
				return;

			size_t enabledPackageCounts[static_cast<size_t>(ShaderTarget::Unknown) + 1]{};
			size_t enabledReplacementCounts[static_cast<size_t>(ShaderTarget::Unknown) + 1]{};
			for (const ModifiedShader::PackageDisk& modifiedShader : DatabaseModifiedShaders::GetModifiedShaders())
			{
				if (!modifiedShader.enabled || modifiedShader.shaderType == ShaderTarget::Unknown)
					continue;

				const size_t shaderTypeIndex = static_cast<size_t>(modifiedShader.shaderType);
				if (shaderTypeIndex < static_cast<size_t>(ShaderTarget::Unknown))
					++enabledPackageCounts[shaderTypeIndex];
			}

			if (HookD3D12::gLoadedShaderTargetsOnce)
			{
				for (const ShaderTarget::ShaderTargetDisk& replacement : HookD3D12::gLoadedShaderTargets)
				{
					if (!replacement.enabled || replacement.shaderType == ShaderTarget::Unknown)
						continue;

					const size_t shaderTypeIndex = static_cast<size_t>(replacement.shaderType);
					if (shaderTypeIndex < static_cast<size_t>(ShaderTarget::Unknown))
						++enabledReplacementCounts[shaderTypeIndex];
				}
			}

			for (ShaderTarget::ShaderType shaderType : neverSeenShaderTypes)
			{
				const size_t shaderTypeIndex = static_cast<size_t>(shaderType);
				ShaderInjectorGUI::WriteToRuntimeLogError(
					"ShaderAutomaticDiscovery->MaybeLogNeverSeenCompatibleShaderTypes: no capture attempt for " +
					StringHelper::ShaderTypeToString(shaderType) +
					" enabledPackages=" + std::to_string(enabledPackageCounts[shaderTypeIndex]) +
					" enabledReplacements=" + std::to_string(enabledReplacementCounts[shaderTypeIndex]));
			}
		}
	}

	void RefreshModifiedShaderIndex(const std::vector<ModifiedShader::PackageDisk>& modifiedShaders)
	{
		auto analysisSnapshot = std::make_shared<std::vector<ModifiedShader::PackageDisk>>(modifiedShaders);
		for (ModifiedShader::PackageDisk& modifiedShader : *analysisSnapshot)
			modifiedShader.compiledBlob.clear();

		std::lock_guard<std::mutex> lock(gQueueMutex);
		gModifiedShaderAnalysisSnapshot = std::move(analysisSnapshot);
		gDirectMatchShaders.clear();
		gModifiedShadersWithGeneratedTargets.clear();
		for (bool& compatibleShaderType : gCompatibleShaderTypes)
			compatibleShaderType = false;
		for (bool& seenShaderType : gSeenShaderTypes)
			seenShaderType = false;
		for (auto& ranges : gPlausibleByteLengthRanges)
			ranges.clear();

		for (const ModifiedShader::PackageDisk& modifiedShader : modifiedShaders)
		{
			if (!modifiedShader.enabled || modifiedShader.shaderType == ShaderTarget::Unknown)
				continue;

			const size_t shaderTypeIndex = static_cast<size_t>(modifiedShader.shaderType);
			if (shaderTypeIndex >= static_cast<size_t>(ShaderTarget::Unknown))
				continue;
			gCompatibleShaderTypes[shaderTypeIndex] = true;

			for (const ModifiedShader::TargetDisk& target : modifiedShader.targets)
			{
				size_t targetByteLength = 0;
				if (ParseByteLength(target.originalShaderBytecodeLength, targetByteLength))
					AddPlausibleByteLengthRange(modifiedShader.shaderType, targetByteLength);

				for (const std::string& knownHash : target.knownShaderBytecodeHashes)
				{
					const uint64_t parsedHash = Hash::ParseHashText(knownHash);
					if (parsedHash != 0)
					{
						const ShaderKey key{ parsedHash, modifiedShader.shaderType };
						auto [directMatch, inserted] = gDirectMatchShaders.emplace(key, modifiedShader.id);
						if (!inserted && directMatch->second != modifiedShader.id)
							directMatch->second.clear();
					}
				}
			}
		}

		MergePlausibleByteLengthRanges();
	}

	void ProcessQueuedWork(size_t maximumJobs)
	{
		std::vector<ShaderTarget::ShaderType> neverSeenShaderTypes;
		{
			std::lock_guard<std::mutex> lock(gQueueMutex);
			if (!gAcceptingWork)
				return;
			CollectNeverSeenCompatibleShaderTypesForDiagnostic(neverSeenShaderTypes);
		}
		LogNeverSeenCompatibleShaderTypes(neverSeenShaderTypes);

		// D3D12-facing completion work remains on the render thread.
		for (size_t processedJobs = 0; processedJobs < maximumJobs; ++processedJobs)
		{
			AnalysisResult result{};
			{
				std::lock_guard<std::mutex> lock(gQueueMutex);
				if (gAnalysisResults.empty())
					break;
				result = std::move(gAnalysisResults.front());
				gAnalysisResults.pop_front();
			}
			ProcessMatchedShader(result.queuedShader, result.modifiedShaderId, &result.shaderAnalysis);
		}

		// Submit a bounded amount of new work each frame. Background workers process jobs concurrently.
		for (size_t submittedJobs = 0; submittedJobs < maximumJobs; ++submittedJobs)
		{
			QueuedShader queued{};
			std::string directModifiedShaderId;
			{
				std::lock_guard<std::mutex> lock(gQueueMutex);
				if (!PopHighestPriorityQueuedShaderLocked(queued))
					break;
				const auto directMatch = gDirectMatchShaders.find(queued.key);
				if (directMatch != gDirectMatchShaders.end())
					directModifiedShaderId = directMatch->second;
			}

			if (!directModifiedShaderId.empty())
			{
				ProcessMatchedShader(queued, directModifiedShaderId);
				{
					std::lock_guard<std::mutex> lock(gQueueMutex);
					++gDirectMatchProcessedTotal;
				}
				continue;
			}

			if (!TrySubmitAnalysis(queued))
			{
				std::lock_guard<std::mutex> lock(gQueueMutex);
				gQueuedShaders.push_front(std::move(queued));
				break;
			}
		}

		MaybeLogDiscoveryProgress("active");
	}

	void Shutdown()
	{
		std::vector<std::thread> workers;
		{
			std::lock_guard<std::mutex> lock(gQueueMutex);
			gAcceptingWork = false;
			gQueuedShaders.clear();
			gAnalysisJobs.clear();
			gAnalysisResults.clear();
			gSubmittedShaders.clear();
			gDirectMatchShaders.clear();
			gModifiedShadersWithGeneratedTargets.clear();
			gModifiedShaderAnalysisSnapshot.reset();

			if (gAnalysisWorkers.empty())
				return;

			gAnalysisStopRequested = true;
			workers = std::move(gAnalysisWorkers);
			gAnalysisCondition.notify_all();
		}

		for (std::thread& worker : workers)
		{
			if (worker.joinable())
				worker.join();
		}

		{
			std::lock_guard<std::mutex> lock(gQueueMutex);
			gAnalysisWorkerRunning = false;
			gRunningAnalysisWorkerCount = 0;
		}
	}

	void ProcessCapturedGraphicsPipeline(const HookD3D12::GraphicsPipelineInfo& pipeline)
	{
		Enqueue(PipelineSource::Graphics, pipeline.pipelineState, -1, ShaderTarget::VertexShader, pipeline.vsHash, pipeline.vsBytecode, false);
		Enqueue(PipelineSource::Graphics, pipeline.pipelineState, -1, ShaderTarget::PixelShader, pipeline.psHash, pipeline.psBytecode, false);
		Enqueue(PipelineSource::Graphics, pipeline.pipelineState, -1, ShaderTarget::GeometryShader, pipeline.gsHash, pipeline.gsBytecode, false);
		Enqueue(PipelineSource::Graphics, pipeline.pipelineState, -1, ShaderTarget::HullShader, pipeline.hsHash, pipeline.hsBytecode, false);
		Enqueue(PipelineSource::Graphics, pipeline.pipelineState, -1, ShaderTarget::DomainShader, pipeline.dsHash, pipeline.dsBytecode, false);
	}

	void ProcessCapturedStreamPipeline(const HookD3D12::PipelineStateInfo& pipeline)
	{
		Enqueue(PipelineSource::Stream, pipeline.pipelineState, -1, ShaderTarget::VertexShader, pipeline.vsHash, pipeline.vsBytecode, false);
		Enqueue(PipelineSource::Stream, pipeline.pipelineState, -1, ShaderTarget::PixelShader, pipeline.psHash, pipeline.psBytecode, false);
		Enqueue(PipelineSource::Stream, pipeline.pipelineState, -1, ShaderTarget::ComputeShader, pipeline.csHash, pipeline.csBytecode, false);
		Enqueue(PipelineSource::Stream, pipeline.pipelineState, -1, ShaderTarget::GeometryShader, pipeline.gsHash, pipeline.gsBytecode, false);
		Enqueue(PipelineSource::Stream, pipeline.pipelineState, -1, ShaderTarget::HullShader, pipeline.hsHash, pipeline.hsBytecode, false);
		Enqueue(PipelineSource::Stream, pipeline.pipelineState, -1, ShaderTarget::DomainShader, pipeline.dsHash, pipeline.dsBytecode, false);
	}

	bool ProcessCapturedShader(
		const HookD3D12::GraphicsPipelineInfo& pipeline,
		int pipelineIndex,
		ShaderTarget::ShaderType shaderType,
		uint64_t shaderHash,
		const std::vector<uint8_t>& shaderBytecode)
	{
		return Enqueue(PipelineSource::Graphics, pipeline.pipelineState, pipelineIndex, shaderType, shaderHash, shaderBytecode, true);
	}

	bool ProcessCapturedShader(
		const HookD3D12::PipelineStateInfo& pipeline,
		int pipelineIndex,
		ShaderTarget::ShaderType shaderType,
		uint64_t shaderHash,
		const std::vector<uint8_t>& shaderBytecode)
	{
		return Enqueue(PipelineSource::Stream, pipeline.pipelineState, pipelineIndex, shaderType, shaderHash, shaderBytecode, true);
	}
}
