#include "ShaderPatch/ShaderPatchRuntime.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_set>

#include <windows.h>
#include <wrl/client.h>

#include "Globals.h"
#include "Hash/Hash.h"
#include "HookD3D12/HookD3D12.h"
#include "HookD3D12/D3D12/HookD3D12RuntimeState.h"
#include "HookD3D12/HookD3D12RetiredPSOTracker.h"
#include "IO/ShaderInjectorIO.h"
#include "RenderPass/RenderPassExpandedRootSignature.h"
#include "RenderPass/RenderPassReplacement.h"
#include "RenderPass/RenderPassRuntime.h"
#include "ShaderModelDetector.h"
#include "ShaderPatch/ShaderPatchCache.h"

namespace ShaderPatchRuntime
{
	namespace
	{
		struct Recipes
		{
			std::vector<std::shared_ptr<const ShaderPatch::Recipe>> entries;
			uint64_t generation = 0;
			bool resources = false;
		};

		//a patched pipeline; texture patches also carry the binding executed at render-pass boundaries.
		struct Result
		{
			Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline;
			std::shared_ptr<const RenderPassResourceRegistry::ExpandedRootSignature> root;
			std::shared_ptr<const RenderPassRuntime::ShaderPatchBinding> binding;
		};

		struct PatchedStage
		{
			ShaderTarget::ShaderType stage = ShaderTarget::Unknown;
			std::vector<uint8_t> bytecode;
		};


		std::mutex gMutex;
		std::condition_variable gWake;
		//the injected DLL normally lives until process exit; never run a joinable thread's destructor there.
		std::thread& gWorker = *new std::thread();
		bool gStopping = false;
		bool gReloadRequested = false;
		bool gProcessing = false;
		//results are published in batches so the render thread does not rebuild the override map per pipeline.
		constexpr ULONGLONG publishIntervalMilliseconds = 100;
		bool gPublishPending = false;
		ULONGLONG gLastPublishTick = 0;
		constexpr ULONGLONG publishLogIntervalMilliseconds = 2000;
		ULONGLONG gLastPublishLogTick = 0;
		std::shared_ptr<const Recipes> gRecipes;
		//captures are queued only while recipes exist; a reload re-queues the capture database itself.
		std::atomic<bool> gHasRecipes{false};
		//new pipelines are patched while the game precompiles them; bound pipelines jump ahead.
		//after a reload only pipelines bound again are re-patched, each once per recipe generation.
		std::deque<ID3D12PipelineState*> gJobs;
		std::deque<ID3D12PipelineState*> gBoundJobs;
		std::unordered_set<ID3D12PipelineState*> gBoundQueued;
		std::unordered_set<ID3D12PipelineState*> gProcessed;
		//lock-free filter in front of gQueued so repeated binds do not take the runtime mutex.
		constexpr size_t boundFilterSize = 1u << 16;
		std::array<std::atomic<ID3D12PipelineState*>, boundFilterSize> gBoundFilter{};
		std::unordered_map<ID3D12PipelineState*, std::shared_ptr<const Result>> gResults;
		//a replaced or dropped result whose pipeline recorded command lists may still use. the PSO goes
		//to the retired-PSO tracker for the release proof; the root signature and binding wait with it.
		struct RetiredResult
		{
			std::shared_ptr<const Result> result;
			ID3D12PipelineState* trackedPipelineState = nullptr;
		};

		std::vector<RetiredResult> gRetiredResults;
		std::unordered_map<std::string, PatchedShaderInfo> gReports;
		std::deque<std::string> gReportOrder;
		std::string gLastError;

		bool IsPatchableStage(ShaderTarget::ShaderType stage)
		{
			return stage == ShaderTarget::VertexShader || stage == ShaderTarget::PixelShader ||
				   stage == ShaderTarget::ComputeShader || stage == ShaderTarget::MeshShader;
		}

		void ReportShaderLocked(const std::string& key, PatchedShaderInfo info)
		{
			if (gReports.find(key) == gReports.end())
				gReportOrder.push_back(key);

			info.cacheKey = key;
			gReports[key] = std::move(info);
		}

		std::string RecipeIdentity(const ShaderPatch::RecipeInfo& info)
		{
			return ":" + info.name + ":" + std::to_string(info.version) + ":" + Hash::FormatHash(info.contentHash);
		}

		std::shared_ptr<Recipes> LoadRecipes(std::string& error)
		{
			auto result = std::make_shared<Recipes>();
			const ShaderTarget::ShaderType stages[] = {ShaderTarget::VertexShader, ShaderTarget::PixelShader, ShaderTarget::ComputeShader, ShaderTarget::MeshShader, ShaderTarget::Unknown};
			const char* directories[] = {"VS", "PS", "CS", "MS", ""};

			for (const bool shaderModel6 : {false, true})
			{
				for (size_t stage = 0; stage < std::size(stages); ++stage)
				{
					if (!shaderModel6 && stages[stage] == ShaderTarget::MeshShader)
						continue;

					const std::string modelDirectory = ShaderInjectorIO::JoinPath(GetRecipeDirectory(), shaderModel6 ? "SM6" : "SM5");
					const std::string directory = ShaderInjectorIO::JoinPath(modelDirectory, directories[stage]);
					std::vector<std::string> paths;
					ShaderInjectorIO::CollectFilesByExtension(directory, ".yaml", paths, false, true);
					ShaderInjectorIO::CollectFilesByExtension(directory, ".yml", paths, false, true);
					std::sort(paths.begin(), paths.end());

					for (const std::string& path : paths)
					{
						std::string text;
						if (!ShaderInjectorIO::ReadTextFile(path, text))
						{
							error = "Cannot read recipe: " + path;
							return {};
						}

						ShaderPatch::RecipeInfo info;
						info.path = path;
						info.stage = stages[stage];
						info.shaderModel6 = shaderModel6;
						info.name = std::string(shaderModel6 ? "SM6/" : "SM5/") + (stages[stage] == ShaderTarget::Unknown ? "" : std::string(directories[stage]) + "/") + ShaderInjectorIO::FileNameFromPath(path);
						info.contentHash = Hash::HashMemory(text.data(), text.size());
						auto parsed = ShaderPatch::Recipe::Load(info, text, error);
						if (!parsed)
							return {};

						result->resources |= parsed->GetInfo().requiresResourceBindings;
						result->entries.push_back(std::move(parsed));
					}
				}
			}

			return result;
		}

		//copy a captured pipeline's stage bytecode and root signature out of the capture database.
		bool CopyCapturedShaders(
			ID3D12PipelineState* original,
			std::vector<std::pair<ShaderTarget::ShaderType, std::vector<uint8_t>>>& shaders,
			ID3D12RootSignature*& rootSignature,
			bool& computePipeline)
		{
			const auto append = [&](ShaderTarget::ShaderType stage, const std::vector<uint8_t>& bytecode)
			{
				if (!bytecode.empty())
					shaders.emplace_back(stage, bytecode);
			};

			std::lock_guard<std::mutex> pipelineLock(HookD3D12::gPipelineMutex);

			for (const HookD3D12::GraphicsPipelineInfo& pipeline : HookD3D12::gGraphicsPipelines)
			{
				if (pipeline.pipelineState != original)
					continue;
				append(ShaderTarget::VertexShader, pipeline.vertexShaderBytecode);
				append(ShaderTarget::PixelShader, pipeline.pixelShaderBytecode);
				rootSignature = pipeline.originalDescription.pRootSignature;
				computePipeline = false;
				return true;
			}

			for (const HookD3D12::ComputePipelineInfo& pipeline : HookD3D12::gComputePipelines)
			{
				if (pipeline.pipelineState != original)
					continue;
				append(ShaderTarget::ComputeShader, pipeline.computeShaderBytecode);
				rootSignature = pipeline.originalDescription.pRootSignature;
				computePipeline = true;
				return true;
			}

			for (const HookD3D12::PipelineStateInfo& pipeline : HookD3D12::gPipelineStates)
			{
				if (pipeline.pipelineState != original)
					continue;
				append(ShaderTarget::VertexShader, pipeline.vertexShaderBytecode);
				append(ShaderTarget::PixelShader, pipeline.pixelShaderBytecode);
				append(ShaderTarget::ComputeShader, pipeline.computeShaderBytecode);
				append(ShaderTarget::MeshShader, pipeline.meshShaderBytecode);
				rootSignature = pipeline.rootSignature;
				computePipeline = pipeline.isCompute;
				return true;
			}

			return false;
		}

		//DXP resolves dxcompiler.dll and dxil.dll by name for SM6 work; prefer the injector's Tools copies
		//over whatever the game directory provides. Modules that are already loaded are kept.
		void EnsureDXILRuntimeLoaded()
		{
			static std::once_flag loaded;
			std::call_once(loaded, []
						   {
				const std::string tools = ShaderInjectorIO::GetToolsDirectory();
				for (const char* name : {"dxil.dll", "dxcompiler.dll"})
				{
					const std::wstring wideName = ShaderInjectorIO::PathFromUTF8(name).wstring();
					if (GetModuleHandleW(wideName.c_str()))
						continue;
					const std::wstring path = ShaderInjectorIO::PathFromUTF8(ShaderInjectorIO::JoinPath(tools, name)).wstring();
					if (!LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH))
						ShaderInjectorIO::WriteToLogFileWarning(std::string("ShaderPatchRuntime: could not load ") + name + " from " + tools);
				} });
		}

		//stage-specific recipes first, then model-wide ones; the first recipe that modifies the shader wins.
		bool ExecuteRecipes(
			const Recipes& recipes,
			ShaderTarget::ShaderType stage,
			bool shaderModel6,
			const std::vector<uint8_t>& bytecode,
			ShaderPatch::PatchResult& result,
			std::string& failure)
		{
			for (const ShaderTarget::ShaderType scope : {stage, ShaderTarget::Unknown})
			{
				for (const auto& recipe : recipes.entries)
				{
					const ShaderPatch::RecipeInfo& info = recipe->GetInfo();
					if (info.shaderModel6 != shaderModel6 || info.stage != scope)
						continue;

					ShaderPatch::PatchResult candidate;
					if (recipe->Execute(bytecode, candidate))
					{
						result = std::move(candidate);
						return true;
					}

					if (!candidate.error.empty())
					{
						failure = info.name + ": " + candidate.error;
						return false;
					}
				}
			}

			return true;
		}

		void ReportFailure(const Recipes& recipes, const std::string& key, PatchedShaderInfo info, const std::string& failure)
		{
			ShaderInjectorIO::WriteToLogFileWarning("ShaderPatchRuntime: " + failure);
			std::lock_guard<std::mutex> lock(gMutex);
			if (!gRecipes || gRecipes->generation != recipes.generation || gStopping)
				return;

			gLastError = failure;
			info.status = "Failed: " + failure;
			ReportShaderLocked(key, std::move(info));
		}

		std::shared_ptr<RenderPassRuntime::ShaderPatchBinding> CreateTextureBinding(
			ID3D12PipelineState* original,
			ID3D12RootSignature* originalRoot,
			const std::vector<ShaderPatch::TextureBinding>& textures,
			uint32_t textureStages,
			bool computePipeline,
			std::shared_ptr<const RenderPassResourceRegistry::ExpandedRootSignature>& outRoot,
			std::string& error)
		{
			std::vector<uint8_t> rootBlob;
			uint64_t rootHash = 0;
			if (!originalRoot || !HookD3D12::GetRootSignatureBlob(originalRoot, rootBlob, rootHash))
			{
				error = "Texture patches require a captured root signature object.";
				return {};
			}

			Microsoft::WRL::ComPtr<ID3D12Device> device;
			if (FAILED(original->GetDevice(IID_PPV_ARGS(&device))))
			{
				error = "Cannot query the pipeline device.";
				return {};
			}

			std::vector<RenderPassResourceRegistry::ResourceRegister> registers;
			for (const ShaderPatch::TextureBinding& texture : textures)
				registers.push_back({texture.shaderRegister, texture.registerSpace});

			outRoot = RenderPassResourceRegistry::GetOrCreateExpandedRootSignature(device.Get(), rootBlob, std::move(registers), textureStages, error);
			if (!outRoot)
				return {};

			auto binding = std::make_shared<RenderPassRuntime::ShaderPatchBinding>();
			binding->rootSignature = outRoot->root.Get();
			binding->appendedTableIndex = outRoot->appendedTableIndex;
			binding->computePipeline = computePipeline;

			//the appended table holds one descriptor per register, in the expanded root's order.
			for (const RenderPassResourceRegistry::ResourceRegister& resourceRegister : outRoot->bindings)
			{
				const ShaderPatch::TextureBinding* named = nullptr;
				for (const ShaderPatch::TextureBinding& texture : textures)
				{
					if (texture.shaderRegister != resourceRegister.shaderRegister || texture.registerSpace != resourceRegister.registerSpace)
						continue;

					if (named && named->resourceName != texture.resourceName)
					{
						error = "Stages bind different textures to t" + std::to_string(texture.shaderRegister) + ", space" + std::to_string(texture.registerSpace) + ".";
						return {};
					}

					named = &texture;
				}

				binding->textures.push_back(named->resourceName);
			}

			binding->device = device.Get();
			return binding;
		}

		//queue a result whose pipeline recorded command lists may still use.
		void RetireResultLocked(std::shared_ptr<const Result> result)
		{
			if (!result)
				return;

			RetiredResult retired{};
			retired.trackedPipelineState = result->pipeline.Get();

			if (retired.trackedPipelineState)
			{
				//the tracker takes its own reference; the result keeps the ComPtr until the proof ends.
				HookD3D12::TrackRetiredPipelineStateReference(retired.trackedPipelineState);
			}

			retired.result = std::move(result);
			gRetiredResults.push_back(std::move(retired));
		}

		//a pipeline the current recipes no longer patch stops using the previous generation's result.
		void DropResult(ID3D12PipelineState* original, const Recipes& recipes)
		{
			std::lock_guard<std::mutex> lock(gMutex);
			if (!gRecipes || gRecipes->generation != recipes.generation || gStopping)
				return;

			const auto existing = gResults.find(original);
			if (existing == gResults.end())
				return;

			RetireResultLocked(std::move(existing->second));
			gResults.erase(existing);
			gPublishPending = true;
		}

		void Process(ID3D12PipelineState* original, const std::shared_ptr<const Recipes>& recipes)
		{
			std::vector<std::pair<ShaderTarget::ShaderType, std::vector<uint8_t>>> shaders;
			ID3D12RootSignature* originalRoot = nullptr;
			bool computePipeline = false;
			if (!CopyCapturedShaders(original, shaders, originalRoot, computePipeline))
				return;

			std::vector<PatchedStage> patchedStages;
			std::vector<ShaderPatch::TextureBinding> textures;
			std::vector<std::pair<std::string, PatchedShaderInfo>> reports;
			uint32_t textureStages = 0;

			for (const auto& shader : shaders)
			{
				const ShaderTarget::ShaderType stage = shader.first;
				const std::vector<uint8_t>& bytecode = shader.second;
				Globals::ShaderModel model;
				if (!IsPatchableStage(stage) || !ShaderModelDetector::TryReadShaderModel(stage, bytecode.data(), bytecode.size(), model))
					continue;

				const bool shaderModel6 = model >= Globals::ShaderModel::ShaderModel6_0;
				std::string applicable = ShaderPatch::GetBuildIdentity();
				bool hasApplicableRecipe = false;
				for (const auto& recipe : recipes->entries)
				{
					const ShaderPatch::RecipeInfo& info = recipe->GetInfo();
					if (info.shaderModel6 == shaderModel6 && (info.stage == stage || info.stage == ShaderTarget::Unknown))
					{
						applicable += RecipeIdentity(info);
						hasApplicableRecipe = true;
					}
				}

				if (!hasApplicableRecipe)
					continue;

				const uint64_t hash = Hash::HashMemory(bytecode.data(), bytecode.size());
				const std::string key = Hash::FormatHash(hash) + "-" + std::to_string(stage) + "-" + Hash::FormatHash(Hash::HashMemory(applicable.data(), applicable.size()));
				PatchedShaderInfo info;
				info.originalHash = hash;
				info.stage = stage;
				info.shaderModel = model;
				info.originalSize = bytecode.size();

				const auto matchesModel = [&](const std::vector<uint8_t>& output)
				{
					Globals::ShaderModel outputModel;
					return ShaderModelDetector::TryReadShaderModel(stage, output.data(), output.size(), outputModel) && outputModel == model;
				};

				ShaderPatch::PatchResult result;
				if (!ShaderPatchCache::Find(key, result) || (!result.bytecode.empty() && !matchesModel(result.bytecode)))
				{
					result = {};
					std::string failure;
					if (shaderModel6)
						EnsureDXILRuntimeLoaded();
					if (!ExecuteRecipes(*recipes, stage, shaderModel6, bytecode, result, failure))
					{
						ReportFailure(*recipes, key, std::move(info), failure);
						DropResult(original, *recipes);
						return;
					}

					if (!result.bytecode.empty() && !matchesModel(result.bytecode))
					{
						ReportFailure(*recipes, key, std::move(info), "Patched shader stage/model validation failed.");
						DropResult(original, *recipes);
						return;
					}

					ShaderPatchCache::Store(key, result);
				}

				if (result.bytecode.empty())
					continue;

				info.recipeName = result.recipeName;
				info.patchedSize = result.bytecode.size();
				info.status = "Ready";
				reports.emplace_back(key, std::move(info));

				if (!result.textures.empty())
					textureStages |= 1u << stage;
				textures.insert(textures.end(), result.textures.begin(), result.textures.end());
				patchedStages.push_back({stage, std::move(result.bytecode)});
			}

			if (patchedStages.empty())
			{
				DropResult(original, *recipes);
				return;
			}

			auto patched = std::make_shared<Result>();
			std::shared_ptr<RenderPassRuntime::ShaderPatchBinding> binding;
			std::string error;

			if (!textures.empty())
			{
				binding = CreateTextureBinding(original, originalRoot, textures, textureStages, computePipeline, patched->root, error);
				if (!binding)
				{
					for (auto& report : reports)
						ReportFailure(*recipes, report.first, std::move(report.second), error);
					DropResult(original, *recipes);
					return;
				}
			}

			RenderPassReplacement::StageOverrides overrides;
			for (const PatchedStage& stage : patchedStages)
				overrides.stages[stage.stage] = &stage.bytecode;

			ID3D12RootSignature* rootOverride = patched->root ? patched->root->root.Get() : nullptr;
			patched->pipeline.Attach(RenderPassReplacement::BuildPipeline(original, overrides, rootOverride, computePipeline, error));
			if (!patched->pipeline)
			{
				for (auto& report : reports)
					ReportFailure(*recipes, report.first, std::move(report.second), error.empty() ? "Patched PSO creation failed." : error);
				DropResult(original, *recipes);
				return;
			}

			if (binding)
			{
				binding->pipelineState = patched->pipeline.Get();
				patched->binding = std::move(binding);
			}

			{
				std::lock_guard<std::mutex> lock(gMutex);
				if (!gRecipes || gRecipes->generation != recipes->generation || gStopping)
					return;

				//recorded command lists may still use the result this one replaces.
				std::shared_ptr<const Result>& current = gResults[original];
				if (current)
					RetireResultLocked(std::move(current));
				current = std::move(patched);
				gPublishPending = true;
				for (auto& report : reports)
					ReportShaderLocked(report.first, std::move(report.second));
			}
		}

		//release retired results once the tracker finished proving their pipeline.
		void DrainRetiredResults()
		{
			std::vector<RetiredResult> stillPending;
			std::vector<RetiredResult> released;
			{
				std::lock_guard<std::mutex> lock(gMutex);

				if (gRetiredResults.empty())
					return;

				for (RetiredResult& retired : gRetiredResults)
				{
					if (HookD3D12::IsRetirePending(retired.trackedPipelineState))
						stillPending.push_back(std::move(retired));
					else
						released.push_back(std::move(retired));
				}

				gRetiredResults = std::move(stillPending);
			}

			//released outside gMutex: destroying a Result drops a PSO ComPtr and possibly the last
			//reference to an expanded root signature, which must not happen under the runtime lock.
			released.clear();
		}

		void PublishPendingResults()
		{
			{
				std::lock_guard<std::mutex> lock(gMutex);
				const ULONGLONG now = GetTickCount64();
				if (!gPublishPending || ((!gJobs.empty() || !gBoundJobs.empty()) && now - gLastPublishTick < publishIntervalMilliseconds))
					return;
				gPublishPending = false;
				gLastPublishTick = now;

				if (now - gLastPublishLogTick >= publishLogIntervalMilliseconds || (gJobs.empty() && gBoundJobs.empty()))
				{
					gLastPublishLogTick = now;
					const size_t textures = static_cast<size_t>(std::count_if(gResults.begin(), gResults.end(), [](const auto& result)
																			  { return result.second->binding != nullptr; }));
					ShaderInjectorIO::WriteToLogFile("ShaderPatchRuntime->Publish: patched=" + std::to_string(gResults.size()) + " textures=" + std::to_string(textures) + " queued=" + std::to_string(gJobs.size() + gBoundJobs.size()));
				}
			}

			HookD3D12::gPipelineStateOverridesDirty.store(true, std::memory_order_release);
		}

		void ApplyReload()
		{
			std::string error;
			std::shared_ptr<Recipes> snapshot = LoadRecipes(error);
			if (!error.empty())
				ShaderInjectorIO::WriteToLogFileError("ShaderPatchRuntime->Reload: " + error);

			//a failed parse keeps the previous recipe set and its published pipelines.
			if (!snapshot)
			{
				std::lock_guard<std::mutex> lock(gMutex);
				gLastError = error;
				return;
			}

			std::unique_lock<std::mutex> lock(gMutex);
			gLastError = error;

			snapshot->generation = gRecipes ? gRecipes->generation + 1 : 1;
			gRecipes = snapshot;
			//nothing is re-queued: pipelines are re-patched when the game next binds them.
			gJobs.clear();
			gBoundJobs.clear();
			gBoundQueued.clear();
			gProcessed.clear();
			for (auto& entry : gBoundFilter)
				entry.store(nullptr, std::memory_order_relaxed);
			gReports.clear();
			gReportOrder.clear();

			//existing results stay published until their pipeline is reprocessed with the new recipes.
			if (snapshot->entries.empty())
			{
				for (auto& result : gResults)
					RetireResultLocked(std::move(result.second));
				gResults.clear();
			}

			gHasRecipes.store(!snapshot->entries.empty(), std::memory_order_release);

			ShaderInjectorIO::WriteToLogFile("ShaderPatchRuntime->Reload: recipes=" + std::to_string(snapshot->entries.size()) + " patched=" + std::to_string(gResults.size()));
			lock.unlock();

			RenderPassRuntime::SetShaderPatchTrackingRequired(snapshot->resources);
			HookD3D12::gPipelineStateOverridesDirty.store(true, std::memory_order_release);
		}

		void Run()
		{
			SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

			for (;;)
			{
				ID3D12PipelineState* job = nullptr;
				bool reload = false;
				std::shared_ptr<const Recipes> recipes;
				{
					std::unique_lock<std::mutex> lock(gMutex);
					gProcessing = false;
					gWake.wait(lock, []
							   { return gStopping || gReloadRequested || !gJobs.empty() || !gBoundJobs.empty(); });

					if (gStopping)
						return;

					gProcessing = true;
					reload = gReloadRequested;
					gReloadRequested = false;

					if (!reload)
					{
						std::deque<ID3D12PipelineState*>& queue = gBoundJobs.empty() ? gJobs : gBoundJobs;
						job = queue.front();
						queue.pop_front();
						if (gProcessed.insert(job).second)
							recipes = gRecipes;
					}
				}

				try
				{
					if (reload)
						ApplyReload();
					else if (recipes)
						Process(job, recipes);
					DrainRetiredResults();
					PublishPendingResults();
				}
				catch (const std::exception& exception)
				{
					ShaderInjectorIO::WriteToLogFileError(std::string("ShaderPatchRuntime->Run: ") + exception.what());
					std::lock_guard<std::mutex> lock(gMutex);
					gLastError = exception.what();
				}
			}
		}

	} //namespace

	void Initialize()
	{
		static std::once_flag initialized;
		std::call_once(initialized, []
					   {
			//game heaps get their texture-patch reserve when created, so the first recipe set loads here,
			//before the game creates any; reloads run on the worker.
			try
			{
				ShaderPatchCache::Load();
				ApplyReload();
			}
			catch (const std::exception& exception)
			{
				ShaderInjectorIO::WriteToLogFileError(std::string("ShaderPatchRuntime->Initialize: ") + exception.what());
			}

			//patches are built while the game creates its pipelines, so the worker exists only when recipes do.
			if (gHasRecipes.load(std::memory_order_acquire))
			{
				std::lock_guard<std::mutex> lock(gMutex);
				gWorker = std::thread(Run);
			} });
	}

	void ReleaseRetiredResults()
	{
		DrainRetiredResults();
	}

	void Shutdown()
	{
		{
			std::lock_guard<std::mutex> lock(gMutex);
			gStopping = true;
			gWake.notify_all();
		}

		if (gWorker.joinable())
			gWorker.join();
		ShaderPatchCache::Save();

		//unpublish patched pipelines and texture bindings before releasing the objects they point to.
		std::unordered_map<ID3D12PipelineState*, std::shared_ptr<const Result>> results;
		std::vector<RetiredResult> retiredResults;
		{
			std::lock_guard<std::mutex> lock(gMutex);
			results.swap(gResults);
			retiredResults.swap(gRetiredResults);
		}
		{
			std::lock_guard<std::mutex> pipelineLock(HookD3D12::gPipelineMutex);
			HookD3D12::RebuildPipelineStateOverrideMap();
		}

		std::lock_guard<std::mutex> lock(gMutex);
		gHasRecipes.store(false, std::memory_order_release);
		gJobs.clear();
		gBoundJobs.clear();
		gBoundQueued.clear();
		gProcessed.clear();
		gReports.clear();
		gReportOrder.clear();
		gRecipes.reset();
	}

	bool IsReloadAvailable()
	{
		std::lock_guard<std::mutex> lock(gMutex);
		return gWorker.joinable() && !gStopping;
	}

	void RequestReload()
	{
		std::lock_guard<std::mutex> lock(gMutex);
		if (!gWorker.joinable())
			return;
		gReloadRequested = true;
		gWake.notify_one();
	}

	void NotifyEnabledChanged()
	{
		HookD3D12::gPipelineStateOverridesDirty.store(true, std::memory_order_release);
	}

	void NotifyPipelineCreated(ID3D12PipelineState* pipelineState)
	{
		//pipelines built by the injector, including patched ones, are never patch sources.
		if (!pipelineState || !gHasRecipes.load(std::memory_order_acquire) || HookD3D12::IsInsideRenderPassInjection())
			return;

		std::lock_guard<std::mutex> lock(gMutex);
		if (gStopping)
			return;

		gJobs.push_back(pipelineState);
		gWake.notify_one();
	}

	void NotifyPipelineBound(ID3D12PipelineState* pipelineState)
	{
		if (!pipelineState || !gHasRecipes.load(std::memory_order_acquire) || HookD3D12::IsInsideRenderPassInjection())
			return;

		std::atomic<ID3D12PipelineState*>& filter = gBoundFilter[(reinterpret_cast<uintptr_t>(pipelineState) >> 4) % boundFilterSize];
		if (filter.load(std::memory_order_relaxed) == pipelineState)
			return;

		std::lock_guard<std::mutex> lock(gMutex);
		filter.store(pipelineState, std::memory_order_relaxed);
		if (gStopping || gProcessed.count(pipelineState) || !gBoundQueued.insert(pipelineState).second)
			return;

		gBoundJobs.push_back(pipelineState);
		gWake.notify_one();
	}

	void AppendPipelineOverrides(std::unordered_map<ID3D12PipelineState*, ID3D12PipelineState*>& overrides)
	{
		if (!Globals::gShaderPatchesEnabled.load(std::memory_order_acquire))
			return;

		std::lock_guard<std::mutex> lock(gMutex);
		for (const auto& result : gResults)
		{
			//Shader Target replacements and disabled-stage markers take priority over patches.
			if (overrides.find(result.first) != overrides.end())
				continue;

			if (result.second->binding)
				RenderPassRuntime::AddShaderPatchBinding(result.first, result.second->binding);
			else
				overrides.emplace(result.first, result.second->pipeline.Get());
		}
	}

	bool HasResourceRecipes()
	{
		std::lock_guard<std::mutex> lock(gMutex);
		return gRecipes && gRecipes->resources;
	}

	bool IsBusy()
	{
		std::lock_guard<std::mutex> lock(gMutex);
		return gProcessing || gReloadRequested || !gJobs.empty() || !gBoundJobs.empty();
	}

	std::string GetError()
	{
		std::lock_guard<std::mutex> lock(gMutex);
		return gLastError;
	}

	std::string GetRecipeDirectory()
	{
		return ShaderInjectorIO::GetShaderPatchesDirectory();
	}

	std::vector<ShaderPatch::RecipeInfo> GetRecipes()
	{
		std::lock_guard<std::mutex> lock(gMutex);
		std::vector<ShaderPatch::RecipeInfo> result;
		if (gRecipes)
			for (const auto& recipe : gRecipes->entries)
				result.push_back(recipe->GetInfo());
		return result;
	}

	std::vector<PatchedShaderInfo> GetShaders()
	{
		std::lock_guard<std::mutex> lock(gMutex);
		std::vector<PatchedShaderInfo> result;
		result.reserve(gReportOrder.size());
		for (const std::string& key : gReportOrder)
			result.push_back(gReports[key]);
		return result;
	}

	PatchedShaderCounts GetPatchedShaderCounts()
	{
		std::lock_guard<std::mutex> lock(gMutex);
		PatchedShaderCounts counts;
		for (const auto& report : gReports)
		{
			if (!report.second.patchedSize)
				continue;
			++counts.total;
			++counts.byRecipe[report.second.recipeName];
		}
		return counts;
	}
} //namespace ShaderPatchRuntime
