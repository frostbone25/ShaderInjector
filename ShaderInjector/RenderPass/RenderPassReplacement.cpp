#include "RenderPass/RenderPassReplacement.h"

#include <array>
#include <atomic>
#include <mutex>
#include <unordered_map>

#include "Hash/Hash.h"
#include "HookD3D12/HookD3D12.h"
#include "StringHelper.h"

namespace RenderPassReplacement
{
	namespace
	{
		std::mutex gCacheMutex;
		std::unordered_map<std::string, ID3D12PipelineState*> gPipelineCache;
		std::atomic<uint64_t> gPipelineAvailabilityGeneration{ 1 };

		struct PipelineFailure
		{
			uint64_t generation = 0;
			std::string error;
		};

		std::unordered_map<std::string, PipelineFailure> gPipelineFailures;

		struct ThreadPipelineLookup
		{
			const RenderPass::RenderPassDisk* renderPass = nullptr;
			ID3D12PipelineState* originalPipelineState = nullptr;
			uint64_t shaderBlobHash = 0;
			ID3D12PipelineState* replacementPipelineState = nullptr;
			std::string error;
		};

		thread_local std::unordered_map<const RenderPass::RenderPassDisk*, ThreadPipelineLookup> gThreadPipelineLookups;
		thread_local uint64_t gThreadPipelineGeneration = 0;

		const ThreadPipelineLookup* FindThreadPipeline(
			const RenderPass::RenderPassDisk& renderPass,
			ID3D12PipelineState* originalPipelineState)
		{
			const uint64_t generation = gPipelineAvailabilityGeneration.load(std::memory_order_acquire);

			if (generation != gThreadPipelineGeneration)
			{
				gThreadPipelineLookups.clear();
				gThreadPipelineGeneration = generation;
			}

			const auto cached = gThreadPipelineLookups.find(&renderPass);

			if (cached != gThreadPipelineLookups.end())
			{
				const ThreadPipelineLookup& lookup = cached->second;

				if (lookup.renderPass == &renderPass &&
					lookup.originalPipelineState == originalPipelineState &&
					lookup.shaderBlobHash == renderPass.fragmentShaderBlobHash)
				{
					return &lookup;
				}
			}

			return nullptr;
		}

		void CacheThreadPipeline(
			const RenderPass::RenderPassDisk& renderPass,
			ID3D12PipelineState* originalPipelineState,
			ID3D12PipelineState* replacementPipelineState,
			const std::string& error = {})
		{
			gThreadPipelineLookups[&renderPass] = {
				&renderPass,
				originalPipelineState,
				renderPass.fragmentShaderBlobHash,
				replacementPipelineState,
				error };
		}

		std::string CacheKey(const RenderPass::RenderPassDisk& renderPass, ID3D12PipelineState* original)
		{
			return StringHelper::PointerToString(original) + ':' + std::to_string(static_cast<int>(renderPass.type)) +
				':' + std::to_string(renderPass.fragmentShaderBlobHash);
		}

		ID3D12PipelineState* BuildGraphicsPipeline(
			const RenderPass::RenderPassDisk& renderPass,
			ID3D12PipelineState* original,
			std::string& outError)
		{
			for (HookD3D12::GraphicsPipelineInfo& pipeline : HookD3D12::gGraphicsPipelines)
			{
				if (pipeline.pipelineState != original)
					continue;

				D3D12_GRAPHICS_PIPELINE_STATE_DESC description = pipeline.originalDescription;
				description.VS = { pipeline.vertexShaderBytecode.empty() ? nullptr : pipeline.vertexShaderBytecode.data(), pipeline.vertexShaderBytecode.size() };
				description.PS = { renderPass.fragmentShaderBlob.data(), renderPass.fragmentShaderBlob.size() };
				description.GS = { pipeline.geometryShaderBytecode.empty() ? nullptr : pipeline.geometryShaderBytecode.data(), pipeline.geometryShaderBytecode.size() };
				description.HS = { pipeline.hullShaderBytecode.empty() ? nullptr : pipeline.hullShaderBytecode.data(), pipeline.hullShaderBytecode.size() };
				description.DS = { pipeline.domainShaderBytecode.empty() ? nullptr : pipeline.domainShaderBytecode.data(), pipeline.domainShaderBytecode.size() };
				description.InputLayout = { pipeline.inputElements.empty() ? nullptr : pipeline.inputElements.data(), static_cast<UINT>(pipeline.inputElements.size()) };
				description.StreamOutput.pSODeclaration = pipeline.streamOutputDeclarations.empty() ? nullptr : pipeline.streamOutputDeclarations.data();
				description.StreamOutput.NumEntries = static_cast<UINT>(pipeline.streamOutputDeclarations.size());
				description.StreamOutput.pBufferStrides = pipeline.streamOutputStrides.empty() ? nullptr : pipeline.streamOutputStrides.data();
				description.StreamOutput.NumStrides = static_cast<UINT>(pipeline.streamOutputStrides.size());
				description.CachedPSO = {};
				ID3D12PipelineState* replacement = nullptr;
				HookD3D12::ScopedRenderPassInjection injectionScope;
				const HRESULT result = HookD3D12::Original_CreateGraphicsPipelineState
					? HookD3D12::Original_CreateGraphicsPipelineState(HookD3D12::GetCapturedDevice(), &description, IID_PPV_ARGS(&replacement))
					: E_NOINTERFACE;

				if (FAILED(result) || !replacement)
					outError = "Replacement pixel PSO creation failed with " + StringHelper::FormatHRESULT(result);

				return replacement;
			}

			return nullptr;
		}

		ID3D12PipelineState* BuildStreamPipeline(
			const RenderPass::RenderPassDisk& renderPass,
			ID3D12PipelineState* original,
			std::string& outError)
		{
			const HookD3D12::PipelineStateInfo* rebuildTemplate = nullptr;

			for (const auto& pipeline : HookD3D12::gPipelineStates)
			{
				if (pipeline.pipelineState == original && !pipeline.streamBlob.empty())
				{
					rebuildTemplate = &pipeline;
					break;
				}
			}

			if (!rebuildTemplate)
				rebuildTemplate = HookD3D12::FindUncapturedRebuildTemplateLocked(original);

			if (rebuildTemplate && !rebuildTemplate->streamBlob.empty())
			{
				const auto& pipeline = *rebuildTemplate;
				const D3D12_PIPELINE_STATE_SUBOBJECT_TYPE targetType =
					RenderPass::ResolveExecutionMode(renderPass) == RenderPass::ExecutionMode::Compute
					? D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS
					: D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS;
				std::vector<uint8_t> stream = pipeline.streamBlob;
				uint8_t* cursor = stream.data();
				uint8_t* end = cursor + stream.size();
				bool targetPatched = false;
				bool missingViewInstancingState = false;

				const auto originalShader = [&](D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type) -> const std::vector<uint8_t>*
				{
					switch (type)
					{
						case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS: return &pipeline.vertexShaderBytecode;
						case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS: return &pipeline.pixelShaderBytecode;
						case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS: return &pipeline.computeShaderBytecode;
						case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS: return &pipeline.geometryShaderBytecode;
						case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS: return &pipeline.hullShaderBytecode;
						case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS: return &pipeline.domainShaderBytecode;
						case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS: return &pipeline.amplificationShaderBytecode;
						case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS: return &pipeline.meshShaderBytecode;
						default: return nullptr;
					}
				};

				while (cursor < end)
				{
					if (cursor + sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE) > end)
						break;

					const auto type = *reinterpret_cast<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE*>(cursor);
					const UINT typeIndex = static_cast<UINT>(type);

					if (typeIndex >= ARRAYSIZE(HookD3D12::kSubobjectSizes) || !HookD3D12::kSubobjectSizes[typeIndex])
						break;

					const size_t subobjectSize = HookD3D12::kSubobjectSizes[typeIndex];

					if (cursor + subobjectSize > end)
						break;

					void* payload = cursor + sizeof(void*);

					if (const std::vector<uint8_t>* originalBytecode = originalShader(type))
					{
						auto* bytecode = static_cast<D3D12_SHADER_BYTECODE*>(payload);

						if (type == targetType)
						{
							*bytecode = { renderPass.fragmentShaderBlob.data(), renderPass.fragmentShaderBlob.size() };
							targetPatched = true;
						}
						else
							*bytecode = { originalBytecode->empty() ? nullptr : originalBytecode->data(), originalBytecode->size() };
					}
					else if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE)
					{
						if (pipeline.rootSignature)
							*static_cast<ID3D12RootSignature**>(payload) = pipeline.rootSignature;
					}
					else if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO)
						*static_cast<D3D12_CACHED_PIPELINE_STATE*>(payload) = {};
					else if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT)
						*static_cast<D3D12_INPUT_LAYOUT_DESC*>(payload) = { pipeline.inputElements.empty() ? nullptr : pipeline.inputElements.data(), static_cast<UINT>(pipeline.inputElements.size()) };
					else if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT)
					{
						auto* output = static_cast<D3D12_STREAM_OUTPUT_DESC*>(payload);
						output->pSODeclaration = pipeline.streamOutputDeclarations.empty() ? nullptr : pipeline.streamOutputDeclarations.data();
						output->NumEntries = static_cast<UINT>(pipeline.streamOutputDeclarations.size());
						output->pBufferStrides = pipeline.streamOutputStrides.empty() ? nullptr : pipeline.streamOutputStrides.data();
						output->NumStrides = static_cast<UINT>(pipeline.streamOutputStrides.size());
					}
					else if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING)
					{
						auto* viewInstancing = static_cast<D3D12_VIEW_INSTANCING_DESC*>(payload);

						if (!pipeline.hasViewInstancing && viewInstancing->ViewInstanceCount)
							missingViewInstancingState = true;
						else if (pipeline.hasViewInstancing)
						{
							viewInstancing->ViewInstanceCount = static_cast<UINT>(pipeline.viewInstanceLocations.size());
							viewInstancing->pViewInstanceLocations = pipeline.viewInstanceLocations.empty() ? nullptr : pipeline.viewInstanceLocations.data();
							viewInstancing->Flags = pipeline.viewInstancingFlags;
						}
					}

					cursor += subobjectSize;
				}

				if (!targetPatched || missingViewInstancingState)
				{
					if (missingViewInstancingState)
						outError = "The stream PSO is missing durable view-instancing state.";

					return nullptr;
				}

				ID3D12Device2* device = nullptr;

				if (FAILED(HookD3D12::GetCapturedDevice()->QueryInterface(IID_PPV_ARGS(&device))) || !device)
					return nullptr;

				D3D12_PIPELINE_STATE_STREAM_DESC description{ stream.size(), stream.data() };
				ID3D12PipelineState* replacement = nullptr;
				HookD3D12::ScopedRenderPassInjection injectionScope;
				const HRESULT result = HookD3D12::CreatePipelineStateInternal(device, &description, IID_PPV_ARGS(&replacement));
				device->Release();

				if (FAILED(result) || !replacement)
					outError = "Replacement stream PSO creation failed with " + StringHelper::FormatHRESULT(result);

				return replacement;
			}

			return nullptr;
		}

		ID3D12PipelineState* BuildComputePipeline(
			const RenderPass::RenderPassDisk& renderPass,
			ID3D12PipelineState* original,
			std::string& outError)
		{
			for (HookD3D12::ComputePipelineInfo& pipeline : HookD3D12::gComputePipelines)
			{
				if (pipeline.pipelineState != original)
					continue;

				D3D12_COMPUTE_PIPELINE_STATE_DESC description = pipeline.originalDescription;
				description.CS = { renderPass.fragmentShaderBlob.data(), renderPass.fragmentShaderBlob.size() };
				description.CachedPSO = {};
				ID3D12PipelineState* replacement = nullptr;
				HookD3D12::ScopedRenderPassInjection injectionScope;
				const HRESULT result = HookD3D12::Original_CreateComputePipelineState
					? HookD3D12::Original_CreateComputePipelineState(HookD3D12::GetCapturedDevice(), &description, IID_PPV_ARGS(&replacement))
					: E_NOINTERFACE;

				if (FAILED(result) || !replacement)
					outError = "Replacement compute PSO creation failed with " + StringHelper::FormatHRESULT(result);

				return replacement;
			}

			return nullptr;
		}
	}

	ID3D12PipelineState* GetOrCreatePipeline(
		const RenderPass::RenderPassDisk& renderPass,
		ID3D12PipelineState* originalPipelineState,
		std::string& outError)
	{
		outError.clear();

		if (!originalPipelineState || renderPass.fragmentShaderBlob.empty())
		{
			outError = "Render Pass is missing its target PSO or compiled shader.";
			return nullptr;
		}

		if (const auto* cachedPipeline = FindThreadPipeline(renderPass, originalPipelineState))
		{
			outError = cachedPipeline->error;
			return cachedPipeline->replacementPipelineState;
		}

		const std::string key = CacheKey(renderPass, originalPipelineState);
		std::lock_guard<std::mutex> cacheLock(gCacheMutex);
		const uint64_t generation = gPipelineAvailabilityGeneration.load(std::memory_order_acquire);
		const auto cachedIt = gPipelineCache.find(key);

		if (cachedIt != gPipelineCache.end())
		{
			CacheThreadPipeline(renderPass, originalPipelineState, cachedIt->second);
			return cachedIt->second;
		}

		const auto failed = gPipelineFailures.find(key);

		if (failed != gPipelineFailures.end() && failed->second.generation == generation)
		{
			outError = failed->second.error;
			CacheThreadPipeline(renderPass, originalPipelineState, nullptr, outError);
			return nullptr;
		}

		std::lock_guard<std::mutex> pipelineLock(HookD3D12::gPipelineMutex);
		ID3D12PipelineState* pipeline =
			RenderPass::ResolveExecutionMode(renderPass) == RenderPass::ExecutionMode::Compute
				? BuildComputePipeline(renderPass, originalPipelineState, outError)
				: BuildGraphicsPipeline(renderPass, originalPipelineState, outError);

		if (!pipeline)
			pipeline = BuildStreamPipeline(renderPass, originalPipelineState, outError);

		if (!pipeline && outError.empty())
			outError = "The target PSO has no captured rebuild template for this replacement pass.";

		if (pipeline)
		{
			gPipelineCache.emplace(key, pipeline);
			CacheThreadPipeline(renderPass, originalPipelineState, pipeline);
		}
		else
		{
			//repeated invalid-argument or missing-template failures are not new work.
			//a package edit or newly published target invalidates this negative cache.
			gPipelineFailures[key] = { generation, outError };
			CacheThreadPipeline(renderPass, originalPipelineState, nullptr, outError);
		}

		return pipeline;
	}

	void InvalidateFailedPipelines()
	{
		gPipelineAvailabilityGeneration.fetch_add(1, std::memory_order_release);
	}

	void ReleaseResources()
	{
		std::lock_guard<std::mutex> lock(gCacheMutex);

		for (auto& pipeline : gPipelineCache)
			if (pipeline.second) pipeline.second->Release();

		gPipelineCache.clear();
		gPipelineFailures.clear();
		InvalidateFailedPipelines();
		gThreadPipelineLookups.clear();
	}
}
