#include "RenderPass/RenderPassReplacement.h"
#include "RenderPass/PipelineFailure.h"
#include "RenderPass/ReplacementThreadPipelineLookup.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <wrl/client.h>

#include "Hash/Hash.h"
#include "HookD3D12/AsyncPipelineCreation.h"
#include "HookD3D12/HookD3D12.h"
#include "ModifiedShader/DatabaseModifiedShaders.h"
#include "StringHelper.h"

namespace RenderPassReplacement
{
	namespace
	{
		std::mutex gCacheMutex;
		std::unordered_map<std::string, ID3D12PipelineState*> gPipelineCache;
		std::unordered_map<std::string, ID3D12PipelineState*> gLastGoodPipelines;
		std::atomic<uint64_t> gPipelineAvailabilityGeneration{1};

		std::unordered_map<std::string, PipelineFailure> gPipelineFailures;

		thread_local std::unordered_map<const RenderPass::RenderPassDisk*, ThreadPipelineLookup> gThreadPipelineLookups;
		thread_local uint64_t gThreadPipelineGeneration = 0;

		//D3D12 expects null pointers for absent stream data even when vector::data is available.
		template <typename Value>
		Value* DataOrNull(std::vector<Value>& values)
		{
			if (values.empty())
				return nullptr;
			return values.data();
		}

		template <typename Value>
		const Value* DataOrNull(const std::vector<Value>& values)
		{
			if (values.empty())
				return nullptr;
			return values.data();
		}

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
				error};
		}

		std::string CacheKey(const RenderPass::RenderPassDisk& renderPass, ID3D12PipelineState* original, uint64_t fragmentShaderBlobHash)
		{
			return renderPass.id + ':' + StringHelper::PointerToString(original) + ':' + std::to_string(static_cast<int>(renderPass.type)) +
				   ':' + std::to_string(fragmentShaderBlobHash);
		}

		//use the override for a stage when there is one, otherwise the captured bytecode.
		D3D12_SHADER_BYTECODE SelectStage(
			const StageOverrides& overrides,
			ShaderTarget::ShaderType stage,
			const std::vector<uint8_t>& original,
			size_t& appliedOverrides)
		{
			if (const std::vector<uint8_t>* replacement = overrides.stages[stage])
			{
				++appliedOverrides;
				return {replacement->data(), replacement->size()};
			}
			return {DataOrNull(original), original.size()};
		}

		size_t CountOverrides(const StageOverrides& overrides)
		{
			return static_cast<size_t>(std::count_if(overrides.stages.begin(), overrides.stages.end(), [](const auto* stage)
													 { return stage != nullptr; }));
		}

		//the captured descriptors keep the game's semantic-name pointers, which may be freed by the time a
		//pipeline is rebuilt; point them at the copies the capture owns instead.
		template <typename Pipeline>
		void CopyOwnedSemanticNames(
			const Pipeline& pipeline,
			std::vector<D3D12_INPUT_ELEMENT_DESC>& inputElements,
			std::vector<D3D12_SO_DECLARATION_ENTRY>& streamOutputDeclarations)
		{
			inputElements = pipeline.inputElements;
			streamOutputDeclarations = pipeline.streamOutputDeclarations;
			if (pipeline.inputElementSemanticNames.size() == inputElements.size())
				for (size_t index = 0; index < inputElements.size(); ++index)
					inputElements[index].SemanticName = pipeline.inputElementSemanticNames[index].c_str();
			if (pipeline.streamOutputSemanticNames.size() == streamOutputDeclarations.size())
				for (size_t index = 0; index < streamOutputDeclarations.size(); ++index)
					streamOutputDeclarations[index].SemanticName = pipeline.streamOutputSemanticNames[index].c_str();
		}

		ID3D12PipelineState* BuildGraphicsPipeline(
			const StageOverrides& overrides,
			ID3D12RootSignature* rootSignature,
			ID3D12PipelineState* original,
			std::string& outError,
			HookD3D12::AsyncPipelineCreation::NativeCreateCallback* outCreateCallback = nullptr)
		{
			for (HookD3D12::GraphicsPipelineInfo& pipeline : HookD3D12::gGraphicsPipelines)
			{
				if (pipeline.pipelineState != original)
					continue;

				std::vector<D3D12_INPUT_ELEMENT_DESC> inputElements;
				std::vector<D3D12_SO_DECLARATION_ENTRY> streamOutputDeclarations;
				CopyOwnedSemanticNames(pipeline, inputElements, streamOutputDeclarations);
				size_t appliedOverrides = 0;
				D3D12_GRAPHICS_PIPELINE_STATE_DESC description = pipeline.originalDescription;
				description.VS = SelectStage(overrides, ShaderTarget::VertexShader, pipeline.vertexShaderBytecode, appliedOverrides);
				description.PS = SelectStage(overrides, ShaderTarget::PixelShader, pipeline.pixelShaderBytecode, appliedOverrides);
				description.GS = SelectStage(overrides, ShaderTarget::GeometryShader, pipeline.geometryShaderBytecode, appliedOverrides);
				description.HS = SelectStage(overrides, ShaderTarget::HullShader, pipeline.hullShaderBytecode, appliedOverrides);
				description.DS = SelectStage(overrides, ShaderTarget::DomainShader, pipeline.domainShaderBytecode, appliedOverrides);
				description.InputLayout = {DataOrNull(inputElements), static_cast<UINT>(inputElements.size())};
				description.StreamOutput.pSODeclaration = DataOrNull(streamOutputDeclarations);
				description.StreamOutput.NumEntries = static_cast<UINT>(streamOutputDeclarations.size());
				description.StreamOutput.pBufferStrides = DataOrNull(pipeline.streamOutputStrides);
				description.StreamOutput.NumStrides = static_cast<UINT>(pipeline.streamOutputStrides.size());
				description.CachedPSO = {};
				if (rootSignature)
					description.pRootSignature = rootSignature;

				//a stage this pipeline does not have (e.g. mesh) belongs to the stream builder.
				if (appliedOverrides != CountOverrides(overrides))
					return nullptr;

				if (outCreateCallback)
				{
					auto ownedDescription = std::make_shared<HookD3D12::AsyncPipelineCreation::OwnedGraphicsPipelineDesc>();
					if (!ownedDescription->Initialize(original, description) || !HookD3D12::Original_CreateGraphicsPipelineState)
						return nullptr;

					const auto createGraphicsPipelineState = HookD3D12::Original_CreateGraphicsPipelineState;
					*outCreateCallback = [ownedDescription, createGraphicsPipelineState](ID3D12PipelineState** outputPipelineState)
					{
						HookD3D12::ScopedRenderPassInjection injectionScope;
						return createGraphicsPipelineState(ownedDescription->device.Get(), &ownedDescription->description, IID_PPV_ARGS(outputPipelineState));
					};
					return nullptr;
				}

				ID3D12PipelineState* replacement = nullptr;
				HookD3D12::ScopedRenderPassInjection injectionScope;
				HRESULT result = E_NOINTERFACE;
				Microsoft::WRL::ComPtr<ID3D12Device> pipelineDevice;
				if (SUCCEEDED(original->GetDevice(IID_PPV_ARGS(&pipelineDevice))) && HookD3D12::Original_CreateGraphicsPipelineState)
					result = HookD3D12::Original_CreateGraphicsPipelineState(pipelineDevice.Get(), &description, IID_PPV_ARGS(&replacement));
				if (FAILED(result) || !replacement)
					outError = "Replacement pixel PSO creation failed with " + StringHelper::FormatHRESULT(result);
				return replacement;
			}

			return nullptr;
		}

		ID3D12PipelineState* BuildStreamPipeline(
			const StageOverrides& overrides,
			ID3D12RootSignature* rootSignature,
			ID3D12PipelineState* original,
			std::string& outError,
			HookD3D12::AsyncPipelineCreation::NativeCreateCallback* outCreateCallback = nullptr)
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
				std::vector<D3D12_INPUT_ELEMENT_DESC> inputElements;
				std::vector<D3D12_SO_DECLARATION_ENTRY> streamOutputDeclarations;
				CopyOwnedSemanticNames(pipeline, inputElements, streamOutputDeclarations);
				std::vector<uint8_t> stream = pipeline.streamBlob;
				uint8_t* cursor = stream.data();
				uint8_t* end = cursor + stream.size();
				size_t appliedOverrides = 0;
				bool missingViewInstancingState = false;

				const auto originalShader = [&](D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type, ShaderTarget::ShaderType& stage) -> const std::vector<uint8_t>*
				{
					switch (type)
					{
					case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS:
						stage = ShaderTarget::VertexShader;
						return &pipeline.vertexShaderBytecode;
					case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS:
						stage = ShaderTarget::PixelShader;
						return &pipeline.pixelShaderBytecode;
					case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS:
						stage = ShaderTarget::ComputeShader;
						return &pipeline.computeShaderBytecode;
					case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS:
						stage = ShaderTarget::GeometryShader;
						return &pipeline.geometryShaderBytecode;
					case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS:
						stage = ShaderTarget::HullShader;
						return &pipeline.hullShaderBytecode;
					case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS:
						stage = ShaderTarget::DomainShader;
						return &pipeline.domainShaderBytecode;
					case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS:
						stage = ShaderTarget::AmplificationShader;
						return &pipeline.amplificationShaderBytecode;
					case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS:
						stage = ShaderTarget::MeshShader;
						return &pipeline.meshShaderBytecode;
					default:
						return nullptr;
					}
				};

				while (cursor < end)
				{
					if (cursor + sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE) > end)
						break;

					const auto type = *reinterpret_cast<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE*>(cursor);
					const UINT typeIndex = static_cast<UINT>(type);

					if (typeIndex >= ARRAYSIZE(HookD3D12::subobjectSizes) || !HookD3D12::subobjectSizes[typeIndex])
						break;

					const size_t subobjectSize = HookD3D12::subobjectSizes[typeIndex];

					if (cursor + subobjectSize > end)
						break;

					void* payload = cursor + sizeof(void*);

					ShaderTarget::ShaderType stage = ShaderTarget::Unknown;

					if (const std::vector<uint8_t>* originalBytecode = originalShader(type, stage))
						*static_cast<D3D12_SHADER_BYTECODE*>(payload) = SelectStage(overrides, stage, *originalBytecode, appliedOverrides);
					else if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE)
					{
						if (rootSignature)
							*static_cast<ID3D12RootSignature**>(payload) = rootSignature;
						else if (pipeline.rootSignature)
							*static_cast<ID3D12RootSignature**>(payload) = pipeline.rootSignature;
					}
					else if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO)
						*static_cast<D3D12_CACHED_PIPELINE_STATE*>(payload) = {};
					else if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT)
						*static_cast<D3D12_INPUT_LAYOUT_DESC*>(payload) = {DataOrNull(inputElements), static_cast<UINT>(inputElements.size())};
					else if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT)
					{
						auto* output = static_cast<D3D12_STREAM_OUTPUT_DESC*>(payload);
						output->pSODeclaration = DataOrNull(streamOutputDeclarations);
						output->NumEntries = static_cast<UINT>(streamOutputDeclarations.size());
						output->pBufferStrides = DataOrNull(pipeline.streamOutputStrides);
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
							viewInstancing->pViewInstanceLocations = DataOrNull(pipeline.viewInstanceLocations);
							viewInstancing->Flags = pipeline.viewInstancingFlags;
						}
					}

					cursor += subobjectSize;
				}

				if (appliedOverrides != CountOverrides(overrides) || missingViewInstancingState)
				{
					if (missingViewInstancingState)
						outError = "The stream PSO is missing durable view-instancing state.";

					return nullptr;
				}

				if (outCreateCallback)
				{
				ID3D12RootSignature* selectedRootSignature = rootSignature;
				if (!selectedRootSignature)
					selectedRootSignature = pipeline.rootSignature;

					std::vector<uint8_t> vertexBytecode = pipeline.vertexShaderBytecode;
					std::vector<uint8_t> pixelBytecode = pipeline.pixelShaderBytecode;
					std::vector<uint8_t> geometryBytecode = pipeline.geometryShaderBytecode;
					std::vector<uint8_t> hullBytecode = pipeline.hullShaderBytecode;
					std::vector<uint8_t> domainBytecode = pipeline.domainShaderBytecode;
					std::vector<uint8_t> computeBytecode = pipeline.computeShaderBytecode;
					std::vector<uint8_t> amplificationBytecode = pipeline.amplificationShaderBytecode;
					std::vector<uint8_t> meshBytecode = pipeline.meshShaderBytecode;
					const auto copyOverride = [&](ShaderTarget::ShaderType stage, std::vector<uint8_t>& destination)
					{
						if (overrides.stages[stage])
							destination = *overrides.stages[stage];
					};
					copyOverride(ShaderTarget::VertexShader, vertexBytecode);
					copyOverride(ShaderTarget::PixelShader, pixelBytecode);
					copyOverride(ShaderTarget::GeometryShader, geometryBytecode);
					copyOverride(ShaderTarget::HullShader, hullBytecode);
					copyOverride(ShaderTarget::DomainShader, domainBytecode);
					copyOverride(ShaderTarget::ComputeShader, computeBytecode);
					copyOverride(ShaderTarget::AmplificationShader, amplificationBytecode);
					copyOverride(ShaderTarget::MeshShader, meshBytecode);
					std::shared_ptr<HookD3D12::AsyncPipelineCreation::OwnedStreamPipelineDesc> ownedDescription =
						std::make_shared<HookD3D12::AsyncPipelineCreation::OwnedStreamPipelineDesc>();
					if (!ownedDescription->Initialize(
						original, selectedRootSignature, pipeline.streamBlob, {},
						D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MAX_VALID, vertexBytecode, pixelBytecode,
						geometryBytecode, hullBytecode, domainBytecode, computeBytecode, amplificationBytecode, meshBytecode,
						inputElements, pipeline.inputElementSemanticNames, streamOutputDeclarations,
						pipeline.streamOutputSemanticNames, pipeline.streamOutputStrides, pipeline.viewInstanceLocations))
						return nullptr;

					*outCreateCallback = [ownedDescription](ID3D12PipelineState** outputPipelineState)
					{
						HookD3D12::ScopedRenderPassInjection injectionScope;
						return HookD3D12::CreatePipelineStateInternal(ownedDescription->device.Get(), &ownedDescription->description, IID_PPV_ARGS(outputPipelineState));
					};
					return nullptr;
				}

				ID3D12Device2* device = nullptr;
				if (FAILED(original->GetDevice(IID_PPV_ARGS(&device))) || !device)
					return nullptr;
				D3D12_PIPELINE_STATE_STREAM_DESC description{stream.size(), stream.data()};
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
			const StageOverrides& overrides,
			ID3D12RootSignature* rootSignature,
			ID3D12PipelineState* original,
			std::string& outError,
			HookD3D12::AsyncPipelineCreation::NativeCreateCallback* outCreateCallback = nullptr)
		{
			const std::vector<uint8_t>* computeShader = overrides.stages[ShaderTarget::ComputeShader];

			if (!computeShader || CountOverrides(overrides) != 1)
				return nullptr;

			for (HookD3D12::ComputePipelineInfo& pipeline : HookD3D12::gComputePipelines)
			{
				if (pipeline.pipelineState != original)
					continue;

				D3D12_COMPUTE_PIPELINE_STATE_DESC description = pipeline.originalDescription;
				description.CS = {computeShader->data(), computeShader->size()};
				description.CachedPSO = {};
				if (rootSignature)
					description.pRootSignature = rootSignature;
				if (outCreateCallback)
				{
					auto ownedDescription = std::make_shared<HookD3D12::AsyncPipelineCreation::OwnedComputePipelineDesc>();
					if (!ownedDescription->Initialize(original, description) || !HookD3D12::Original_CreateComputePipelineState)
						return nullptr;
					const auto createComputePipelineState = HookD3D12::Original_CreateComputePipelineState;
					*outCreateCallback = [ownedDescription, createComputePipelineState](ID3D12PipelineState** outputPipelineState)
					{
						HookD3D12::ScopedRenderPassInjection injectionScope;
						return createComputePipelineState(ownedDescription->device.Get(), &ownedDescription->description, IID_PPV_ARGS(outputPipelineState));
					};
					return nullptr;
				}

				ID3D12PipelineState* replacement = nullptr;
				HookD3D12::ScopedRenderPassInjection injectionScope;
				HRESULT result = E_NOINTERFACE;
				Microsoft::WRL::ComPtr<ID3D12Device> pipelineDevice;

				if (SUCCEEDED(original->GetDevice(IID_PPV_ARGS(&pipelineDevice))) && HookD3D12::Original_CreateComputePipelineState)
					result = HookD3D12::Original_CreateComputePipelineState(pipelineDevice.Get(), &description, IID_PPV_ARGS(&replacement));

				if (FAILED(result) || !replacement)
					outError = "Replacement compute PSO creation failed with " + StringHelper::FormatHRESULT(result);

				return replacement;
			}

			return nullptr;
		}

		ID3D12PipelineState* BuildPipelineLocked(
			const StageOverrides& overrides,
			ID3D12RootSignature* rootSignature,
			bool computePipeline,
			ID3D12PipelineState* original,
			std::string& outError,
			HookD3D12::AsyncPipelineCreation::NativeCreateCallback* outCreateCallback = nullptr)
		{
			ID3D12PipelineState* pipeline = nullptr;
			if (computePipeline)
				pipeline = BuildComputePipeline(overrides, rootSignature, original, outError, outCreateCallback);
			else
				pipeline = BuildGraphicsPipeline(overrides, rootSignature, original, outError, outCreateCallback);

			if (!pipeline && (!outCreateCallback || !*outCreateCallback))
				pipeline = BuildStreamPipeline(overrides, rootSignature, original, outError, outCreateCallback);

			if (!pipeline && (!outCreateCallback || !*outCreateCallback) && outError.empty())
				outError = "The target PSO has no captured rebuild template for this replacement pass.";

			return pipeline;
		}
	} //namespace

	ID3D12PipelineState* GetOrCreatePipeline(
		const RenderPass::RenderPassDisk& renderPass,
		ID3D12PipelineState* originalPipelineState,
		const RenderPassRuntime::ShaderTargetBinding& shaderTarget,
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

		const std::vector<uint8_t>* fragmentShaderBlob = &renderPass.fragmentShaderBlob;
		uint64_t fragmentShaderBlobHash = renderPass.fragmentShaderBlobHash;

		if (!renderPass.fragmentShaderVariants.empty())
		{
			const ModifiedShader::ModifiedShaderPackageDisk* modifiedShader = DatabaseModifiedShaders::FindModifiedShaderById(shaderTarget.modifiedShaderId);
			std::vector<ModifiedShader::ShaderDefine> defines;

			if (!modifiedShader || !DatabaseModifiedShaders::TryGetVariantDefinesForShader(*modifiedShader, Hash::FormatHash(shaderTarget.hash), {}, defines))
			{
				outError = "Render Pass has no Modified Shader variant for " + shaderTarget.name + ".";
				return nullptr;
			}

			const auto variant = std::find_if(renderPass.fragmentShaderVariants.begin(), renderPass.fragmentShaderVariants.end(), [&](const ModifiedShader::ModifiedShaderCompiledVariant& candidate)
			{
				return candidate.defines == defines;
			});

			if (variant == renderPass.fragmentShaderVariants.end() || variant->compiledBlob.empty())
			{
				if (variant != renderPass.fragmentShaderVariants.end())
					DatabaseModifiedShaders::RequestVariantCompile(renderPass.id, defines);

				outError = "Render Pass shader is not compiled for " + shaderTarget.name + ".";
				return nullptr;
			}

			fragmentShaderBlob = &variant->compiledBlob;
			fragmentShaderBlobHash = Hash::HashMemory(fragmentShaderBlob->data(), fragmentShaderBlob->size());
		}

		const std::string key = CacheKey(renderPass, originalPipelineState, fragmentShaderBlobHash);
		const uint64_t generation = gPipelineAvailabilityGeneration.load(std::memory_order_acquire);
		{
			std::lock_guard<std::mutex> cacheLock(gCacheMutex);
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
		}

		const bool computePipeline = RenderPass::ResolveExecutionMode(renderPass) == RenderPass::ExecutionMode::Compute;
		StageOverrides overrides;
		if (computePipeline)
			overrides.stages[ShaderTarget::ComputeShader] = fragmentShaderBlob;
		else
			overrides.stages[ShaderTarget::PixelShader] = fragmentShaderBlob;
		const std::string requestKey = "renderpass:" + key;
		HookD3D12::AsyncPipelineCreation::TicketResult ticketResult = HookD3D12::AsyncPipelineCreation::Poll(requestKey);
		HookD3D12::AsyncPipelineCreation::NativeCreateCallback createCallback;
		if (ticketResult.status == HookD3D12::AsyncPipelineCreation::TicketStatus::Unavailable)
		{
			ID3D12PipelineState* preparedPipeline = nullptr;
			{
				std::lock_guard<std::mutex> pipelineLock(HookD3D12::gPipelineMutex);
				preparedPipeline = BuildPipelineLocked(overrides, nullptr, computePipeline, originalPipelineState, outError, &createCallback);
			}

			if (!createCallback)
			{
				std::lock_guard<std::mutex> cacheLock(gCacheMutex);
				const auto cachedPipeline = gPipelineCache.find(key);
				if (cachedPipeline != gPipelineCache.end())
				{
					CacheThreadPipeline(renderPass, originalPipelineState, cachedPipeline->second);
					return cachedPipeline->second;
				}
				gPipelineFailures[key] = {generation, outError};
				CacheThreadPipeline(renderPass, originalPipelineState, nullptr, outError);
				return nullptr;
			}
			static_cast<void>(preparedPipeline);

			bool requestQueued = false;
			const bool requestAccepted = HookD3D12::AsyncPipelineCreation::Request(requestKey, std::move(createCallback), true, &requestQueued);
			if (!requestAccepted)
				ticketResult.status = HookD3D12::AsyncPipelineCreation::TicketStatus::Pending;
			else
				ticketResult = HookD3D12::AsyncPipelineCreation::Poll(requestKey);
		}

		if (ticketResult.status == HookD3D12::AsyncPipelineCreation::TicketStatus::Ready && ticketResult.pipelineState)
		{
			ID3D12PipelineState* pipeline = ticketResult.pipelineState.Detach();
			Microsoft::WRL::ComPtr<ID3D12PipelineState> duplicatePipeline;
			{
				std::lock_guard<std::mutex> cacheLock(gCacheMutex);
				const auto insertedPipeline = gPipelineCache.emplace(key, pipeline);
				if (insertedPipeline.second)
					gLastGoodPipelines[renderPass.id + ':' + StringHelper::PointerToString(originalPipelineState) + ':' + std::to_string(static_cast<int>(renderPass.type))] = pipeline;
				else
				{
					duplicatePipeline.Attach(pipeline);
					pipeline = insertedPipeline.first->second;
				}
				gPipelineFailures.erase(key);
				CacheThreadPipeline(renderPass, originalPipelineState, pipeline);
			}
			if (duplicatePipeline)
				HookD3D12::AsyncPipelineCreation::QueueReleaseOnWorker(std::move(duplicatePipeline));
			return pipeline;
		}

		if (ticketResult.status == HookD3D12::AsyncPipelineCreation::TicketStatus::Failed)
		{
			outError = "Replacement PSO creation failed with " + StringHelper::FormatHRESULT(ticketResult.result);
			std::lock_guard<std::mutex> cacheLock(gCacheMutex);
			const auto cachedPipeline = gPipelineCache.find(key);
			if (cachedPipeline != gPipelineCache.end())
			{
				CacheThreadPipeline(renderPass, originalPipelineState, cachedPipeline->second);
				return cachedPipeline->second;
			}
			gPipelineFailures[key] = {generation, outError};
			CacheThreadPipeline(renderPass, originalPipelineState, nullptr, outError);
			return nullptr;
		}

		outError = "Replacement PSO is compiling asynchronously.";
		if (ticketResult.status == HookD3D12::AsyncPipelineCreation::TicketStatus::Pending)
			HookD3D12::AsyncPipelineCreation::Promote(requestKey);

		const std::string lastGoodKey = renderPass.id + ':' + StringHelper::PointerToString(originalPipelineState) + ':' + std::to_string(static_cast<int>(renderPass.type));
		std::lock_guard<std::mutex> cacheLock(gCacheMutex);
		const auto lastGood = gLastGoodPipelines.find(lastGoodKey);
		if (lastGood != gLastGoodPipelines.end())
			return lastGood->second;
		return nullptr;
	}

	ID3D12PipelineState* BuildPipeline(
		ID3D12PipelineState* original,
		const StageOverrides& overrides,
		ID3D12RootSignature* rootSignature,
		bool computePipeline,
		std::string& outError)
	{
		outError.clear();
		if (!original || !CountOverrides(overrides))
		{
			outError = "Replacement is missing its original PSO or shader bytecode.";
			return nullptr;
		}

		std::lock_guard<std::mutex> pipelineLock(HookD3D12::gPipelineMutex);
		return BuildPipelineLocked(overrides, rootSignature, computePipeline, original, outError);
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
		gLastGoodPipelines.clear();
		gPipelineFailures.clear();
		InvalidateFailedPipelines();
		gThreadPipelineLookups.clear();
	}
} //namespace RenderPassReplacement
