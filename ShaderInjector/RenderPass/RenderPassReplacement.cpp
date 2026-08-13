#include "RenderPass/RenderPassReplacement.h"

#include <array>
#include <mutex>
#include <unordered_map>

#include "Hash.h"
#include "HookD3D12/HookD3D12.h"
#include "HookD3D12/HookD3D12PipelineUtils.h"
#include "HookD3D12/HookD3D12RenderPass.h"
#include "StringHelper.h"

namespace RenderPassReplacement
{
	namespace
	{
		std::mutex gCacheMutex;
		std::unordered_map<std::string, ID3D12PipelineState*> gPipelineCache;

		struct ThreadPipelineLookup
		{
			const RenderPass::RenderPassDisk* renderPass = nullptr;
			ID3D12PipelineState* originalPipelineState = nullptr;
			uint64_t shaderBlobHash = 0;
			ID3D12PipelineState* replacementPipelineState = nullptr;
		};

		thread_local std::array<ThreadPipelineLookup, 16> gThreadPipelineLookups;
		thread_local size_t gNextThreadPipelineLookup = 0;

		ID3D12PipelineState* FindThreadPipeline(
			const RenderPass::RenderPassDisk& renderPass,
			ID3D12PipelineState* originalPipelineState)
		{
			for (const ThreadPipelineLookup& lookup : gThreadPipelineLookups)
			{
				if (lookup.renderPass == &renderPass &&
					lookup.originalPipelineState == originalPipelineState &&
					lookup.shaderBlobHash == renderPass.fragmentShaderBlobHash)
				{
					return lookup.replacementPipelineState;
				}
			}
			return nullptr;
		}

		void CacheThreadPipeline(
			const RenderPass::RenderPassDisk& renderPass,
			ID3D12PipelineState* originalPipelineState,
			ID3D12PipelineState* replacementPipelineState)
		{
			gThreadPipelineLookups[gNextThreadPipelineLookup] = {
				&renderPass,
				originalPipelineState,
				renderPass.fragmentShaderBlobHash,
				replacementPipelineState };
			gNextThreadPipelineLookup = (gNextThreadPipelineLookup + 1) % gThreadPipelineLookups.size();
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
				D3D12_GRAPHICS_PIPELINE_STATE_DESC description = pipeline.originalDesc;
				description.VS = { pipeline.vsBytecode.empty() ? nullptr : pipeline.vsBytecode.data(), pipeline.vsBytecode.size() };
				description.PS = { renderPass.fragmentShaderBlob.data(), renderPass.fragmentShaderBlob.size() };
				description.GS = { pipeline.gsBytecode.empty() ? nullptr : pipeline.gsBytecode.data(), pipeline.gsBytecode.size() };
				description.HS = { pipeline.hsBytecode.empty() ? nullptr : pipeline.hsBytecode.data(), pipeline.hsBytecode.size() };
				description.DS = { pipeline.dsBytecode.empty() ? nullptr : pipeline.dsBytecode.data(), pipeline.dsBytecode.size() };
				description.InputLayout = { pipeline.inputElements.empty() ? nullptr : pipeline.inputElements.data(), static_cast<UINT>(pipeline.inputElements.size()) };
				description.StreamOutput.pSODeclaration = pipeline.soDeclarations.empty() ? nullptr : pipeline.soDeclarations.data();
				description.StreamOutput.NumEntries = static_cast<UINT>(pipeline.soDeclarations.size());
				description.StreamOutput.pBufferStrides = pipeline.soStrides.empty() ? nullptr : pipeline.soStrides.data();
				description.StreamOutput.NumStrides = static_cast<UINT>(pipeline.soStrides.size());
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
			for (HookD3D12::PipelineStateInfo& pipeline : HookD3D12::gPipelineStates)
			{
				if (pipeline.pipelineState != original || pipeline.streamBlob.empty())
					continue;
				const D3D12_PIPELINE_STATE_SUBOBJECT_TYPE targetType =
					renderPass.type == RenderPass::RenderPassType::ReplacementComputeShader
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
						case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS: return &pipeline.vsBytecode;
						case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS: return &pipeline.psBytecode;
						case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS: return &pipeline.csBytecode;
						case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS: return &pipeline.gsBytecode;
						case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS: return &pipeline.hsBytecode;
						case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS: return &pipeline.dsBytecode;
						case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS: return &pipeline.asBytecode;
						case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS: return &pipeline.msBytecode;
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
						output->pSODeclaration = pipeline.soDeclarations.empty() ? nullptr : pipeline.soDeclarations.data();
						output->NumEntries = static_cast<UINT>(pipeline.soDeclarations.size());
						output->pBufferStrides = pipeline.soStrides.empty() ? nullptr : pipeline.soStrides.data();
						output->NumStrides = static_cast<UINT>(pipeline.soStrides.size());
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
				D3D12_COMPUTE_PIPELINE_STATE_DESC description = pipeline.originalDesc;
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
			outError = "Replacement pass is missing its target PSO or compiled shader.";
			return nullptr;
		}
		if (ID3D12PipelineState* cachedPipeline = FindThreadPipeline(renderPass, originalPipelineState))
			return cachedPipeline;
		const std::string key = CacheKey(renderPass, originalPipelineState);
		std::lock_guard<std::mutex> cacheLock(gCacheMutex);
		const auto cachedIt = gPipelineCache.find(key);
		if (cachedIt != gPipelineCache.end())
		{
			CacheThreadPipeline(renderPass, originalPipelineState, cachedIt->second);
			return cachedIt->second;
		}

		std::lock_guard<std::mutex> pipelineLock(HookD3D12::gPipelineMutex);
		ID3D12PipelineState* pipeline = renderPass.type == RenderPass::RenderPassType::ReplacementPixelShader
			? BuildGraphicsPipeline(renderPass, originalPipelineState, outError)
			: BuildComputePipeline(renderPass, originalPipelineState, outError);
		if (!pipeline)
			pipeline = BuildStreamPipeline(renderPass, originalPipelineState, outError);
		if (!pipeline && outError.empty())
			outError = "The target PSO has no captured rebuild template for this replacement pass.";
		if (pipeline)
		{
			gPipelineCache.emplace(key, pipeline);
			CacheThreadPipeline(renderPass, originalPipelineState, pipeline);
		}
		return pipeline;
	}

	void ReleaseResources()
	{
		std::lock_guard<std::mutex> lock(gCacheMutex);
		for (auto& pipeline : gPipelineCache)
			if (pipeline.second) pipeline.second->Release();
		gPipelineCache.clear();
		gThreadPipelineLookups = {};
		gNextThreadPipelineLookup = 0;
	}
}
