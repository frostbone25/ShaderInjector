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

namespace DatabaseModifiedShaders
{
	std::mutex Detail::gCompileMutex;

	//workers only read package snapshots; results are applied on the render thread.
	struct CompileJob
	{
		std::shared_ptr<const ModifiedShader::ModifiedShaderPackageDisk> package;
		std::vector<ModifiedShader::ShaderDefine> defines;
		//package id + define description; also the in-flight dedupe key while a worker executes this job.
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

	//recorded by lookups on the bind/render threads; drained by ProcessCompletedCompiles.
	struct DeferredVariantRequest
	{
		std::string packageId;
		std::vector<ModifiedShader::ShaderDefine> defines;
	};

	static std::condition_variable gCompileCondition;
	static std::deque<CompileJob> gCompileQueue;
	static std::vector<CompileResult> gCompileResults;
	static CompileProgress gCompileProgress;
	//keys of jobs a worker is executing, so a lookup never spawns an identical duplicate.
	static std::unordered_set<std::string> gCompileInFlightKeys;
	static uint64_t gNextCompileSerial = 1;
	//process exit destroys static storage without running Shutdown, and a joinable thread's
	//destructor calls std::terminate. leaked deliberately, matching ShaderPatchRuntime.
	static std::vector<std::thread>& gCompileWorkers = *new std::vector<std::thread>();
	//set by Shutdown; no worker starts and no job is queued once hook resources are released.
	static bool gCompileStopRequested = false;

	static std::vector<DeferredVariantRequest> gDeferredVariantRequests;
	//one entry per (package, define set) until the drain, so per-bind lookups stay cheap.
	static std::unordered_set<std::string> gDeferredVariantRequestKeys;

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
				//the dedupe set covers the working span so new requests cannot fork a second job.
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

		//DXC runs as a separate process per compile, so a few workers compile variants in parallel.
		const unsigned int workerCount = (std::clamp)(std::thread::hardware_concurrency() / 2, 1u, 4u);

		for (unsigned int workerIndex = 0; workerIndex < workerCount; ++workerIndex)
			gCompileWorkers.emplace_back(CompileWorker);
	}

	static std::shared_ptr<const ModifiedShader::ModifiedShaderPackageDisk> MakeCompileSnapshot(const ModifiedShader::ModifiedShaderPackageDisk& modifiedShader)
	{
		auto snapshot = std::make_shared<ModifiedShader::ModifiedShaderPackageDisk>(modifiedShader);
		snapshot->compiledBlob.clear();
		snapshot->compiledVariants.clear();
		snapshot->variantIndexByShaderHash.clear();
		return snapshot;
	}

	//an explicit request supersedes a queued job and retries a variant whose last compile failed.
	static bool VariantNeedsCompile(const ModifiedShader::ModifiedShaderCompiledVariant& variant, bool explicitRequest)
	{
		if (!explicitRequest && (variant.compileFailed || variant.pendingCompileSerial != 0))
			return false;

		return variant.compiledBlob.empty() || explicitRequest;
	}

	//urgent variants go to the front so shaders the game uses compile first. a requeue replaces the
	//waiting job; a lookup request for a variant whose job is already running does nothing, because
	//the running result arrives with a matching serial and is applied on its own.
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
			//a worker has popped this variant's job and is compiling it; dropping the request avoids
			//a duplicate DXC launch whose result the supersede check would discard anyway.
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
			//a requeued variant was already counted when it was first queued.
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

	//a miss only records the request; ProcessCompletedCompiles drains it on the render thread, where
	//copying a package snapshot is safe.
	void Detail::RequestVariantCompile(const std::string& packageId, const std::vector<ModifiedShader::ShaderDefine>& defines)
	{
		std::lock_guard<std::mutex> lock(Detail::gCompileMutex);

		if (gCompileStopRequested)
			return;

		if (!gDeferredVariantRequestKeys.insert(CompileJobKey(packageId, defines)).second)
			return;

		gDeferredVariantRequests.push_back({packageId, defines});
	}

	//one snapshot per package: a package's analysis data is far too large to copy once per request.
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

			if (!modifiedShader)
				continue;

			std::vector<ModifiedShader::ModifiedShaderCompiledVariant*> pendingVariants;

			for (const DeferredVariantRequest& packageRequest : requests)
			{
				if (packageRequest.packageId != request.packageId)
					continue;

				ModifiedShader::ModifiedShaderCompiledVariant* variant = modifiedShader->FindVariantForDefinesMutable(packageRequest.defines);

				if (variant)
					pendingVariants.push_back(variant);
			}

			if (pendingVariants.empty())
				continue;

			const std::shared_ptr<const ModifiedShader::ModifiedShaderPackageDisk> snapshot = MakeCompileSnapshot(*modifiedShader);
			std::lock_guard<std::mutex> lock(Detail::gCompileMutex);

			if (gCompileStopRequested)
				return;

			for (ModifiedShader::ModifiedShaderCompiledVariant* variant : pendingVariants)
				QueueVariantCompileLocked(snapshot, *variant, true, false);
		}
	}

	//queue every variant of this package; the ones loaded shader targets use go to the front.
	//current blobs stay in use until their replacement is ready.
	int Detail::QueuePackageCompiles(ModifiedShader::ModifiedShaderPackageDisk& modifiedShader)
	{
		if (modifiedShader.compiledVariants.empty())
			Detail::LoadPackageVariants(modifiedShader);

		//the shared loaded-target list can be replaced by the render thread, so read it under its own mutex first.
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

		//variants not used by a loaded shader target are queued first, so urgent pushes overtake them.
		const std::shared_ptr<const ModifiedShader::ModifiedShaderPackageDisk> snapshot = MakeCompileSnapshot(modifiedShader);
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

			//urgent pushes land at the deque head in call order, so the front-queued order is the
			//reverse of the argument: push from the back reversed so the first loaded variant ends up
			//at the front of the urgent block, ahead of every non-urgent one.
			for (auto loadedIterator = loadedVariants.rbegin(); loadedIterator != loadedVariants.rend(); ++loadedIterator)
			{
				if (QueueVariantCompileLocked(snapshot, **loadedIterator, true, true))
					++queuedCount;

				++loadedCount;
			}
		}

		//the log line keeps the user-facing action name, matching what the UI reports when a compile is queued.
		ShaderInjectorGUI::WriteToRuntimeLog(
			"DatabaseModifiedShaders->CompileModifiedShader: queued " + modifiedShader.id +
			" variants=" + std::to_string(queuedCount) +
			" of=" + std::to_string(modifiedShader.compiledVariants.size()) +
			" loadedFirst=" + std::to_string(loadedCount));

		return queuedCount;
	}

	void ProcessCompletedCompiles()
	{
		//lookup misses recorded on the bind threads become queued jobs here, on the render thread.
		DrainDeferredVariantRequests();

		std::vector<CompileResult> results;
		CompileProgress progress;
		bool reportBatch = false;

		{
			std::lock_guard<std::mutex> lock(Detail::gCompileMutex);
			results.swap(gCompileResults);

			//the batch is reported and reset only after the queue drained and every result was claimed,
			//so a batch that starts while results are still pending lands in the same report.
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
			//blob files are written before gPipelineMutex is taken, so disk latency never stalls the
			//bind hooks that wait on it.
			std::vector<CompileResult*> readyResults;

			for (CompileResult& result : results)
			{
				ModifiedShader::ModifiedShaderPackageDisk* modifiedShader = Detail::FindMutableModifiedShaderById(result.packageId);

				if (!modifiedShader)
					continue;

				ModifiedShader::ModifiedShaderCompiledVariant* variant = modifiedShader->FindVariantForDefinesMutable(result.defines);

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
					//disk keeps the previous blob, so the session state must match it.
					std::lock_guard<std::mutex> lock(Detail::gCompileMutex);
					variant->compileFailed = true;
					ShaderInjectorGUI::WriteToRuntimeLogError("DatabaseModifiedShaders->ProcessCompletedCompiles: could not write " + variant->compiledBlobPath);
					continue;
				}

				readyResults.push_back(&result);
			}

			//the whole install runs under gPipelineMutex so no PSO rebuild observes a half-updated variant.
			if (!readyResults.empty())
			{
				std::lock_guard<std::mutex> pipelineLock(HookD3D12::gPipelineMutex);
				//one hot-swap pass per shader target, no matter how many of its variants finished together.
				std::vector<std::string> updatedShaderTargetNames;

				for (CompileResult* readyResult : readyResults)
				{
					ModifiedShader::ModifiedShaderPackageDisk* modifiedShader = Detail::FindMutableModifiedShaderById(readyResult->packageId);

					if (!modifiedShader)
						continue;

					ModifiedShader::ModifiedShaderCompiledVariant* variant = modifiedShader->FindVariantForDefinesMutable(readyResult->defines);

					if (!variant)
						continue;

					{
						std::lock_guard<std::mutex> lock(Detail::gCompileMutex);
						variant->compiledBlob = std::move(readyResult->blob);
						variant->compiledShaderAnalysis = readyResult->analysis;
						variant->compiledShaderInterfaceCompatible = true;
						Detail::UpdatePackageCompiledState(*modifiedShader);
					}

					//a disabled package still records its new blob, but loaded targets keep the original game shader.
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

				//one apply-scan restart and one log line cover every target this pass updated.
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

			//refuse later requests and drop unfinished work before the resources it uses go away.
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
