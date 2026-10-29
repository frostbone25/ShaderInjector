#include "DatabaseModifiedShaders.h"
#include "DatabaseModifiedShadersInternal.h"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#include "GUI/ShaderInjectorGUI.h"
#include "HookD3D12/HookD3D12.h"
#include "IO/ShaderInjectorIO.h"
#include "RenderPass/DatabaseRenderPasses.h"

namespace DatabaseModifiedShaders
{
	std::mutex Detail::gCompileMutex;

	struct CompileJob
	{
		std::shared_ptr<const ModifiedShader::ModifiedShaderPackageDisk> package;
		std::vector<ModifiedShader::ShaderDefine> defines;
		std::string key;
		std::string candidateBasePath;
		uint64_t serial = 0;
	};

	struct CompileResult
	{
		std::string packageId;
		std::vector<ModifiedShader::ShaderDefine> defines;
		uint64_t serial = 0;
		bool succeeded = false;
		std::vector<uint8_t> blob;
		ShaderAnalysis::ShaderAnalysisDisk analysis;
	};

	struct DeferredVariantRequest
	{
		std::string packageId;
		std::vector<ModifiedShader::ShaderDefine> defines;
	};

	static std::condition_variable gCompileCondition;
	static std::deque<CompileJob> gCompileQueue;
	static std::vector<CompileResult> gCompileResults;
	static CompileProgress gCompileProgress;
	static std::unordered_set<std::string> gCompileInFlightKeys;
	static uint64_t gNextCompileSerial = 1;
	//leaked: a joinable thread destroyed at process exit calls std::terminate.
	static std::vector<std::thread>& gCompileWorkers = *new std::vector<std::thread>();
	static bool gCompileStopRequested = false;

	static std::vector<DeferredVariantRequest> gDeferredVariantRequests;
	static std::unordered_set<std::string> gDeferredVariantRequestKeys;
	static bool gRenderPassPublishPending = false;

	static std::string CompileJobKey(const std::string& packageId, const std::vector<ModifiedShader::ShaderDefine>& defines)
	{
		return packageId + "|" + Detail::DescribeDefines(defines);
	}

	static void CompileWorker()
	{
		for (;;)
		{
			CompileJob job;

			{
				std::unique_lock<std::mutex> lock(Detail::gCompileMutex);
				gCompileCondition.wait(lock, []
				{
					return gCompileStopRequested || !gCompileQueue.empty();
				});

				if (gCompileStopRequested)
					return;

				job = std::move(gCompileQueue.front());
				gCompileQueue.pop_front();
				--gCompileProgress.queued;
				++gCompileProgress.running;
				gCompileInFlightKeys.insert(job.key);
			}

			CompileResult result;
			result.packageId = job.package->id;
			result.defines = job.defines;
			result.serial = job.serial;
			result.succeeded = Detail::CompileVariant(*job.package, job.defines, job.candidateBasePath, result.blob, result.analysis);

			{
				std::lock_guard<std::mutex> lock(Detail::gCompileMutex);
				--gCompileProgress.running;
				++gCompileProgress.completed;
				gCompileInFlightKeys.erase(job.key);

				if (!result.succeeded)
					++gCompileProgress.failed;

				gCompileResults.push_back(std::move(result));
			}
		}
	}

	static void StartCompileWorkersLocked()
	{
		if (gCompileStopRequested || !gCompileWorkers.empty())
			return;

		const unsigned int workerCount = (std::clamp)(std::thread::hardware_concurrency() / 2, 1u, 4u);

		for (unsigned int workerIndex = 0; workerIndex < workerCount; ++workerIndex)
			gCompileWorkers.emplace_back(CompileWorker);
	}

	static std::shared_ptr<const ModifiedShader::ModifiedShaderPackageDisk> MakeCompileSnapshot(
		const ModifiedShader::ModifiedShaderPackageDisk& modifiedShader,
		const std::string& ownerId,
		const std::string& sourcePath)
	{
		auto snapshot = std::make_shared<ModifiedShader::ModifiedShaderPackageDisk>(modifiedShader);
		snapshot->id = ownerId;
		snapshot->sourcePath = sourcePath;
		snapshot->compiledBlob.clear();
		snapshot->compiledVariants.clear();
		snapshot->variantIndexByShaderHash.clear();
		return snapshot;
	}

	static bool VariantNeedsCompile(const ModifiedShader::ModifiedShaderCompiledVariant& variant, bool explicitRequest)
	{
		if (!explicitRequest && (variant.compileFailed || variant.pendingCompileSerial != 0))
			return false;

		return variant.compiledBlob.empty() || explicitRequest;
	}

	static bool QueueVariantCompileLocked(
		const std::shared_ptr<const ModifiedShader::ModifiedShaderPackageDisk>& snapshot,
		ModifiedShader::ModifiedShaderCompiledVariant& variant,
		bool urgent,
		bool explicitRequest)
	{
		if (!VariantNeedsCompile(variant, explicitRequest))
			return false;

		const std::string key = CompileJobKey(snapshot->id, variant.defines);
		const auto findQueuedJob = [&](CompileJob& queuedJob)
		{
			return queuedJob.key == key;
		};

		const auto existing = std::find_if(gCompileQueue.begin(), gCompileQueue.end(), findQueuedJob);

		if (existing == gCompileQueue.end() && !explicitRequest && gCompileInFlightKeys.count(key) != 0)
		{
			return false;
		}

		variant.pendingCompileSerial = gNextCompileSerial++;
		variant.compileFailed = false;

		CompileJob job;
		job.package = snapshot;
		job.defines = variant.defines;
		job.key = key;
		job.candidateBasePath = variant.compiledBlobPath + "." + std::to_string(variant.pendingCompileSerial);
		job.serial = variant.pendingCompileSerial;

		if (existing != gCompileQueue.end())
			gCompileQueue.erase(existing);
		else
		{
			++gCompileProgress.queued;
			++gCompileProgress.total;
		}

		if (urgent)
			gCompileQueue.push_front(std::move(job));
		else
			gCompileQueue.push_back(std::move(job));

		StartCompileWorkersLocked();
		gCompileCondition.notify_one();
		return true;
	}

	void RequestVariantCompile(const std::string& ownerId, const std::vector<ModifiedShader::ShaderDefine>& defines)
	{
		std::lock_guard<std::mutex> lock(Detail::gCompileMutex);

		if (gCompileStopRequested)
			return;

		if (!gDeferredVariantRequestKeys.insert(CompileJobKey(ownerId, defines)).second)
			return;

		gDeferredVariantRequests.push_back({ownerId, defines});
	}

	static ModifiedShader::ModifiedShaderCompiledVariant* FindOwnerVariant(
		const std::string& ownerId,
		const std::vector<ModifiedShader::ShaderDefine>& defines,
		ModifiedShader::ModifiedShaderPackageDisk*& outModifiedShader)
	{
		outModifiedShader = Detail::FindMutableModifiedShaderById(ownerId);

		if (outModifiedShader)
			return outModifiedShader->FindVariantForDefinesMutable(defines);

		RenderPass::RenderPassDisk* renderPass = DatabaseRenderPasses::FindRenderPassById(ownerId);

		if (!renderPass)
			return nullptr;

		const auto variant = std::find_if(renderPass->fragmentShaderVariants.begin(), renderPass->fragmentShaderVariants.end(), [&](const ModifiedShader::ModifiedShaderCompiledVariant& candidate)
		{
			return candidate.defines == defines;
		});

		if (variant == renderPass->fragmentShaderVariants.end())
			return nullptr;

		return &*variant;
	}

	static void DrainDeferredVariantRequests()
	{
		std::vector<DeferredVariantRequest> requests;

		{
			std::lock_guard<std::mutex> lock(Detail::gCompileMutex);
			requests.swap(gDeferredVariantRequests);
			gDeferredVariantRequestKeys.clear();
		}

		std::unordered_set<std::string> drainedPackageIds;

		for (const DeferredVariantRequest& request : requests)
		{
			if (!drainedPackageIds.insert(request.packageId).second)
				continue;

			ModifiedShader::ModifiedShaderPackageDisk* modifiedShader = Detail::FindMutableModifiedShaderById(request.packageId);
			const RenderPass::RenderPassDisk* renderPass = modifiedShader ? nullptr : DatabaseRenderPasses::FindRenderPassById(request.packageId);
			const ModifiedShader::ModifiedShaderPackageDisk* snapshotPackage = renderPass ? DatabaseRenderPasses::ResolveModifiedShader(*renderPass) : modifiedShader;

			if (!snapshotPackage)
				continue;

			std::vector<ModifiedShader::ModifiedShaderCompiledVariant*> pendingVariants;

			for (const DeferredVariantRequest& packageRequest : requests)
			{
				if (packageRequest.packageId != request.packageId)
					continue;

				ModifiedShader::ModifiedShaderPackageDisk* ownerPackage = nullptr;
				ModifiedShader::ModifiedShaderCompiledVariant* variant = FindOwnerVariant(packageRequest.packageId, packageRequest.defines, ownerPackage);

				if (variant)
					pendingVariants.push_back(variant);
			}

			if (pendingVariants.empty())
				continue;

			const std::shared_ptr<const ModifiedShader::ModifiedShaderPackageDisk> snapshot = renderPass
				? MakeCompileSnapshot(*snapshotPackage, renderPass->id, renderPass->fragmentShaderSourcePath)
				: MakeCompileSnapshot(*snapshotPackage, snapshotPackage->id, snapshotPackage->sourcePath);
			std::lock_guard<std::mutex> lock(Detail::gCompileMutex);

			if (gCompileStopRequested)
				return;

			for (ModifiedShader::ModifiedShaderCompiledVariant* variant : pendingVariants)
				QueueVariantCompileLocked(snapshot, *variant, true, false);
		}
	}

	int Detail::QueuePackageCompiles(ModifiedShader::ModifiedShaderPackageDisk& modifiedShader)
	{
		if (modifiedShader.compiledVariants.empty())
			Detail::LoadPackageVariants(modifiedShader);

		std::vector<ModifiedShader::ModifiedShaderCompiledVariant*> loadedVariants;

		{
			std::lock_guard<std::mutex> pipelineLock(HookD3D12::gPipelineMutex);

			for (ModifiedShader::ModifiedShaderCompiledVariant& variant : modifiedShader.compiledVariants)
			{
				for (const ShaderTarget::ShaderTargetDisk& shaderTarget : HookD3D12::gLoadedShaderTargets)
				{
					if (shaderTarget.modifiedShaderId == modifiedShader.id &&
						modifiedShader.FindVariantForShader(shaderTarget.originalShaderBytecodeHash, shaderTarget.shaderBytecodeHashAliases) == &variant)
					{
						loadedVariants.push_back(&variant);
						break;
					}
				}
			}
		}

		const std::shared_ptr<const ModifiedShader::ModifiedShaderPackageDisk> snapshot = MakeCompileSnapshot(modifiedShader, modifiedShader.id, modifiedShader.sourcePath);
		size_t loadedCount = 0;
		int queuedCount = 0;

		{
			std::lock_guard<std::mutex> lock(Detail::gCompileMutex);

			if (gCompileStopRequested)
				return 0;

			for (ModifiedShader::ModifiedShaderCompiledVariant& variant : modifiedShader.compiledVariants)
			{
				if (std::find(loadedVariants.begin(), loadedVariants.end(), &variant) != loadedVariants.end())
					continue;

				if (QueueVariantCompileLocked(snapshot, variant, false, true))
					++queuedCount;
			}

			//urgent pushes land at the front in call order, so push in reverse.
			for (auto loadedIterator = loadedVariants.rbegin(); loadedIterator != loadedVariants.rend(); ++loadedIterator)
			{
				if (QueueVariantCompileLocked(snapshot, **loadedIterator, true, true))
					++queuedCount;

				++loadedCount;
			}
		}

		ShaderInjectorGUI::WriteToRuntimeLog(
			"DatabaseModifiedShaders->CompileModifiedShader: queued " + modifiedShader.id +
			" variants=" + std::to_string(queuedCount) +
			" of=" + std::to_string(modifiedShader.compiledVariants.size()) +
			" loadedFirst=" + std::to_string(loadedCount));

		for (const RenderPass::RenderPassDisk& renderPass : DatabaseRenderPasses::GetRenderPasses())
		{
			if (renderPass.fragmentShaderVariants.empty() || DatabaseRenderPasses::ResolveModifiedShaderId(renderPass) != modifiedShader.id)
				continue;

			queuedCount += QueueSourceCompiles(modifiedShader, renderPass.id, renderPass.fragmentShaderSourcePath, DatabaseRenderPasses::FindRenderPassById(renderPass.id)->fragmentShaderVariants);
		}

		return queuedCount;
	}

	int QueueSourceCompiles(
		const ModifiedShader::ModifiedShaderPackageDisk& modifiedShader,
		const std::string& ownerId,
		const std::string& sourcePath,
		std::vector<ModifiedShader::ModifiedShaderCompiledVariant>& variants)
	{
		const std::shared_ptr<const ModifiedShader::ModifiedShaderPackageDisk> snapshot = MakeCompileSnapshot(modifiedShader, ownerId, sourcePath);
		int queuedCount = 0;

		std::lock_guard<std::mutex> lock(Detail::gCompileMutex);

		if (gCompileStopRequested)
			return 0;

		for (ModifiedShader::ModifiedShaderCompiledVariant& variant : variants)
		{
			if (QueueVariantCompileLocked(snapshot, variant, false, true))
				++queuedCount;
		}

		return queuedCount;
	}

	void ProcessCompletedCompiles()
	{
		DrainDeferredVariantRequests();

		std::vector<CompileResult> results;
		CompileProgress progress;
		bool reportBatch = false;

		{
			std::lock_guard<std::mutex> lock(Detail::gCompileMutex);
			results.swap(gCompileResults);

			if (gCompileProgress.total > 0 && gCompileProgress.completed >= gCompileProgress.total &&
				gCompileQueue.empty() && gCompileProgress.running == 0)
			{
				reportBatch = true;
				progress = gCompileProgress;
				gCompileProgress = {};
			}
		}

		if (!results.empty())
		{
			//written before gPipelineMutex so disk latency never stalls the bind hooks.
			std::vector<CompileResult*> readyResults;

			for (CompileResult& result : results)
			{
				ModifiedShader::ModifiedShaderPackageDisk* modifiedShader = nullptr;
				ModifiedShader::ModifiedShaderCompiledVariant* variant = FindOwnerVariant(result.packageId, result.defines, modifiedShader);

				if (!variant)
					continue;

				{
					//a newer request for this variant supersedes this result.
					std::lock_guard<std::mutex> lock(Detail::gCompileMutex);

					if (variant->pendingCompileSerial != result.serial)
						continue;

					variant->pendingCompileSerial = 0;
					variant->compileFailed = !result.succeeded;
				}

				if (!result.succeeded)
					continue;

				if (!ShaderInjectorIO::WriteBinaryFile(variant->compiledBlobPath, result.blob.data(), result.blob.size()))
				{
					std::lock_guard<std::mutex> lock(Detail::gCompileMutex);
					variant->compileFailed = true;
					ShaderInjectorGUI::WriteToRuntimeLogError("DatabaseModifiedShaders->ProcessCompletedCompiles: could not write " + variant->compiledBlobPath);
					continue;
				}

				readyResults.push_back(&result);
			}

			if (!readyResults.empty())
			{
				std::lock_guard<std::mutex> pipelineLock(HookD3D12::gPipelineMutex);
				std::vector<std::string> updatedShaderTargetNames;

				for (CompileResult* readyResult : readyResults)
				{
					ModifiedShader::ModifiedShaderPackageDisk* modifiedShader = nullptr;
					ModifiedShader::ModifiedShaderCompiledVariant* variant = FindOwnerVariant(readyResult->packageId, readyResult->defines, modifiedShader);

					if (!variant)
						continue;

					{
						std::lock_guard<std::mutex> lock(Detail::gCompileMutex);
						variant->compiledBlob = std::move(readyResult->blob);
						variant->compiledShaderAnalysis = readyResult->analysis;
						variant->compiledShaderInterfaceCompatible = true;

						if (modifiedShader)
							Detail::UpdatePackageCompiledState(*modifiedShader);
					}

					if (!modifiedShader)
					{
						gRenderPassPublishPending = true;
						continue;
					}

					if (!modifiedShader->enabled)
						continue;

					for (int shaderTargetIndex = 0; shaderTargetIndex < static_cast<int>(HookD3D12::gLoadedShaderTargets.size()); ++shaderTargetIndex)
					{
						const ShaderTarget::ShaderTargetDisk& shaderTarget = HookD3D12::gLoadedShaderTargets[shaderTargetIndex];

						if (!HookD3D12::IsShaderTargetEffectivelyEnabled(shaderTarget))
							continue;

						if (shaderTarget.modifiedShaderId != modifiedShader->id ||
							modifiedShader->FindVariantForShader(shaderTarget.originalShaderBytecodeHash, shaderTarget.shaderBytecodeHashAliases) != variant)
							continue;

						HookD3D12::UpdateShaderTargetBlobLocked(shaderTargetIndex, variant->compiledBlob);

						if (std::find(updatedShaderTargetNames.begin(), updatedShaderTargetNames.end(), shaderTarget.name) == updatedShaderTargetNames.end())
							updatedShaderTargetNames.push_back(shaderTarget.name);
					}
				}

				if (!updatedShaderTargetNames.empty())
				{
					HookD3D12::RestartShaderTargetApplyScan();

					std::string updatedTargets;

					for (const std::string& shaderTargetName : updatedShaderTargetNames)
					{
						if (!updatedTargets.empty())
							updatedTargets += ",";

						updatedTargets += shaderTargetName;
					}

					ShaderInjectorGUI::WriteToRuntimeLog(
						"DatabaseModifiedShaders->ProcessCompletedCompiles: updated shader targets=" + std::to_string(updatedShaderTargetNames.size()) +
						" names=" + updatedTargets);
				}
			}

		}

		if (reportBatch && gRenderPassPublishPending)
		{
			gRenderPassPublishPending = false;
			DatabaseRenderPasses::PublishRuntimeConfiguration();
		}

		if (!reportBatch)
			return;

		const std::string summary =
			"DatabaseModifiedShaders->ProcessCompletedCompiles: background compile finished. compiled=" + std::to_string(progress.completed - progress.failed) +
			"/" + std::to_string(progress.total) + " failed=" + std::to_string(progress.failed);

		if (progress.failed == 0)
			ShaderInjectorGUI::WriteToRuntimeLogSuccess(summary);
		else
			ShaderInjectorGUI::WriteToRuntimeLogError(summary);
	}

	CompileProgress GetCompileProgress()
	{
		std::lock_guard<std::mutex> lock(Detail::gCompileMutex);
		return gCompileProgress;
	}

	void Shutdown()
	{
		std::vector<std::thread> workers;

		{
			std::lock_guard<std::mutex> lock(Detail::gCompileMutex);

			gCompileStopRequested = true;
			gCompileQueue.clear();
			gCompileResults.clear();
			gCompileInFlightKeys.clear();
			gDeferredVariantRequests.clear();
			gDeferredVariantRequestKeys.clear();
			gCompileProgress = {};
			workers = std::move(gCompileWorkers);
			gCompileCondition.notify_all();
		}

		for (std::thread& worker : workers)
		{
			if (worker.joinable())
				worker.join();
		}
	}
} //namespace DatabaseModifiedShaders
