#include "RenderPass/RenderPassExecutor.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>
#include <unordered_map>

#include "HookD3D12.h"
#include "HookD3D12RenderPass.h"
#include "RenderPass/RenderPassMipChain.h"
#include "RenderPass/RenderPassReplacement.h"
#include "RenderPass/RenderPassTexturePool.h"
#include "ShaderResource/ShaderResourceRuntime.h"
#include "Globals.h"
#include "RenderDoc/RenderDocIntegration.h"
#include "IO/ShaderInjectorIO.h"
#include "StringHelper.h"

namespace RenderPassExecutor
{
	namespace
	{
		std::mutex gPipelineCacheMutex;
		std::unordered_map<std::string, ID3D12PipelineState*> gPipelineCache;
		std::unordered_map<std::string, std::string> gPipelineCreationErrors;
		std::atomic<bool> gLoggedExecutionDuringActiveCapture = false;
		std::atomic<uint64_t> gLoggedCaptureRequestSequence = 0;

		struct RenderTargetState
		{
			UINT count = 0;
			DXGI_FORMAT formats[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
			UINT sampleCount = 1;
			UINT sampleQuality = 0;
		};

		struct ThreadPipelineLookup
		{
			const RenderPass::RenderPassDisk* renderPass = nullptr;
			ID3D12RootSignature* rootSignature = nullptr;
			RenderTargetState renderTargets;
			ID3D12PipelineState* pipelineState = nullptr;
		};

		thread_local std::array<ThreadPipelineLookup, 16> gThreadPipelineLookups;
		thread_local size_t gNextThreadPipelineLookup = 0;
		thread_local const RenderPass::RenderPassDisk* gEventNameRenderPass = nullptr;
		thread_local std::wstring gEventName;

		bool RenderTargetStatesEqual(const RenderTargetState& left, const RenderTargetState& right)
		{
			if (left.count != right.count || left.sampleCount != right.sampleCount ||
				left.sampleQuality != right.sampleQuality)
			{
				return false;
			}

			for (UINT renderTargetIndex = 0; renderTargetIndex < left.count; ++renderTargetIndex)
			{
				if (left.formats[renderTargetIndex] != right.formats[renderTargetIndex])
					return false;
			}
			return true;
		}

		void CacheThreadPipelineLookup(
			const RenderPass::RenderPassDisk& renderPass,
			ID3D12RootSignature* rootSignature,
			const RenderTargetState& renderTargets,
			ID3D12PipelineState* pipelineState)
		{
			for (ThreadPipelineLookup& lookup : gThreadPipelineLookups)
			{
				if (lookup.renderPass == &renderPass && lookup.rootSignature == rootSignature)
				{
					lookup = { &renderPass, rootSignature, renderTargets, pipelineState };
					return;
				}
			}
			gThreadPipelineLookups[gNextThreadPipelineLookup] = {
				&renderPass, rootSignature, renderTargets, pipelineState };
			gNextThreadPipelineLookup = (gNextThreadPipelineLookup + 1) % gThreadPipelineLookups.size();
		}

		const std::wstring& GetRenderPassEventName(const RenderPass::RenderPassDisk& renderPass)
		{
			if (gEventNameRenderPass != &renderPass)
			{
				gEventNameRenderPass = &renderPass;
				gEventName = StringHelper::Utf8ToWide("Shader Injector Render Pass: " + renderPass.name);
			}
			return gEventName;
		}

		bool BuildRenderTargetState(
			const std::vector<RenderPass::ResourceBindingDiagnostic>& outputBindings,
			RenderTargetState& outState)
		{
			outState = {};
			outState.sampleCount = 1;
			for (const RenderPass::ResourceBindingDiagnostic& binding : outputBindings)
			{
				if (binding.bindingType != "RTV" ||
					binding.descriptorIndex >= D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT ||
					binding.resourceFormat == DXGI_FORMAT_UNKNOWN)
				{
					continue;
				}

				outState.formats[binding.descriptorIndex] = static_cast<DXGI_FORMAT>(binding.resourceFormat);
				outState.count = (std::max)(outState.count, binding.descriptorIndex + 1);
				if (binding.resourceSampleCount)
				{
					outState.sampleCount = binding.resourceSampleCount;
					outState.sampleQuality = binding.resourceSampleQuality;
				}
			}
			return outState.count > 0 && outState.formats[0] != DXGI_FORMAT_UNKNOWN;
		}

		bool BuildRenderTargetState(
			const RenderPassTexturePool::TextureView& runtimeOutput,
			RenderTargetState& outState)
		{
			outState = {};
			if (!runtimeOutput.resource || !runtimeOutput.renderTargetView.ptr ||
				runtimeOutput.description.format == DXGI_FORMAT_UNKNOWN)
			{
				return false;
			}
			outState.count = 1;
			outState.formats[0] = runtimeOutput.description.format;
			outState.sampleCount = runtimeOutput.description.sampleCount;
			return true;
		}

		void RestoreGameOutputState(
			ID3D12GraphicsCommandList* commandList,
			const RenderPassMipChain::GraphicsStateSnapshot& gameState)
		{
			const D3D12_CPU_DESCRIPTOR_HANDLE* depthStencil = gameState.depthStencil.ptr
				? &gameState.depthStencil
				: nullptr;
			commandList->OMSetRenderTargets(
				static_cast<UINT>(gameState.renderTargets.size()),
				gameState.renderTargets.empty() ? nullptr : gameState.renderTargets.data(),
				FALSE,
				depthStencil);
			if (!gameState.viewports.empty())
				commandList->RSSetViewports(static_cast<UINT>(gameState.viewports.size()), gameState.viewports.data());
			if (!gameState.scissorRectangles.empty())
				commandList->RSSetScissorRects(
					static_cast<UINT>(gameState.scissorRectangles.size()),
					gameState.scissorRectangles.data());
		}

		std::string BuildPipelineCacheKey(
			const RenderPass::RenderPassDisk& renderPass,
			ID3D12RootSignature* rootSignature,
			const RenderTargetState& renderTargets)
		{
			std::string key = renderPass.id + ':' + StringHelper::PointerToString(rootSignature) + ':' +
				std::to_string(renderPass.vertexShaderBlobHash) + ':' +
				std::to_string(renderPass.fragmentShaderBlobHash) + ':' +
				std::to_string(renderTargets.sampleCount) + ':' +
				std::to_string(renderTargets.sampleQuality);
			for (UINT renderTargetIndex = 0; renderTargetIndex < renderTargets.count; ++renderTargetIndex)
				key += ':' + std::to_string(static_cast<UINT>(renderTargets.formats[renderTargetIndex]));
			return key;
		}

		D3D12_BLEND_DESC BuildBlendState(UINT renderTargetCount)
		{
			D3D12_BLEND_DESC blendState{};
			for (UINT renderTargetIndex = 0;
				renderTargetIndex < (std::min)(renderTargetCount, static_cast<UINT>(D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT));
				++renderTargetIndex)
			{
				D3D12_RENDER_TARGET_BLEND_DESC& target = blendState.RenderTarget[renderTargetIndex];
				target.BlendEnable = FALSE;
				target.LogicOpEnable = FALSE;
				target.SrcBlend = D3D12_BLEND_ONE;
				target.DestBlend = D3D12_BLEND_ZERO;
				target.BlendOp = D3D12_BLEND_OP_ADD;
				target.SrcBlendAlpha = D3D12_BLEND_ONE;
				target.DestBlendAlpha = D3D12_BLEND_ZERO;
				target.BlendOpAlpha = D3D12_BLEND_OP_ADD;
				target.LogicOp = D3D12_LOGIC_OP_NOOP;
				target.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
			}
			return blendState;
		}

		D3D12_RASTERIZER_DESC BuildRasterizerState(UINT sampleCount)
		{
			D3D12_RASTERIZER_DESC rasterizer{};
			rasterizer.FillMode = D3D12_FILL_MODE_SOLID;
			rasterizer.CullMode = D3D12_CULL_MODE_NONE;
			rasterizer.FrontCounterClockwise = FALSE;
			rasterizer.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
			rasterizer.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
			rasterizer.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
			rasterizer.DepthClipEnable = TRUE;
			rasterizer.MultisampleEnable = sampleCount > 1;
			rasterizer.AntialiasedLineEnable = FALSE;
			rasterizer.ForcedSampleCount = 0;
			rasterizer.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
			return rasterizer;
		}

		ID3D12PipelineState* GetOrCreatePipelineState(
			const RenderPass::RenderPassDisk& renderPass,
			ID3D12GraphicsCommandList* commandList,
			ID3D12RootSignature* rootSignature,
			const RenderTargetState& renderTargets,
			std::string& outError)
		{
			for (const ThreadPipelineLookup& lookup : gThreadPipelineLookups)
			{
				if (lookup.renderPass == &renderPass &&
					lookup.rootSignature == rootSignature &&
					lookup.pipelineState &&
					RenderTargetStatesEqual(lookup.renderTargets, renderTargets))
				{
					return lookup.pipelineState;
				}
			}

			const std::string cacheKey = BuildPipelineCacheKey(renderPass, rootSignature, renderTargets);
			std::lock_guard<std::mutex> cacheLock(gPipelineCacheMutex);
			const auto cachedPipelineIt = gPipelineCache.find(cacheKey);
			if (cachedPipelineIt != gPipelineCache.end())
			{
				CacheThreadPipelineLookup(renderPass, rootSignature, renderTargets, cachedPipelineIt->second);
				return cachedPipelineIt->second;
			}
			const auto cachedErrorIt = gPipelineCreationErrors.find(cacheKey);
			if (cachedErrorIt != gPipelineCreationErrors.end())
			{
				outError = cachedErrorIt->second;
				return nullptr;
			}

			ID3D12Device* device = nullptr;
			if (FAILED(commandList->GetDevice(IID_PPV_ARGS(&device))) || !device)
			{
				outError = "Could not query the D3D12 device from the command list.";
				return nullptr;
			}

			D3D12_GRAPHICS_PIPELINE_STATE_DESC description{};
			description.pRootSignature = rootSignature;
			description.VS = { renderPass.vertexShaderBlob.data(), renderPass.vertexShaderBlob.size() };
			description.PS = { renderPass.fragmentShaderBlob.data(), renderPass.fragmentShaderBlob.size() };
			description.BlendState = BuildBlendState(renderTargets.count);
			description.SampleMask = UINT_MAX;
			description.RasterizerState = BuildRasterizerState(renderTargets.sampleCount);
			description.DepthStencilState.DepthEnable = FALSE;
			description.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
			description.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
			description.DepthStencilState.StencilEnable = FALSE;
			description.InputLayout = { nullptr, 0 };
			description.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
			description.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			description.NumRenderTargets = renderTargets.count;
			for (UINT renderTargetIndex = 0; renderTargetIndex < renderTargets.count; ++renderTargetIndex)
				description.RTVFormats[renderTargetIndex] = renderTargets.formats[renderTargetIndex];
			description.DSVFormat = DXGI_FORMAT_UNKNOWN;
			description.SampleDesc = { renderTargets.sampleCount, renderTargets.sampleQuality };

			ID3D12PipelineState* pipelineState = nullptr;
			HRESULT result = E_FAIL;
			{
				HookD3D12::ScopedRenderPassInjection injectionScope;
				result = device->CreateGraphicsPipelineState(
					&description,
					IID_PPV_ARGS(&pipelineState));
			}
			device->Release();
			if (FAILED(result) || !pipelineState)
			{
				outError = "Fullscreen pipeline creation failed with " + StringHelper::FormatHRESULT(result);
				gPipelineCreationErrors[cacheKey] = outError;
				ShaderInjectorIO::WriteToLogFileError(
					"RenderPassExecutor->GetOrCreatePipelineState: " + renderPass.name + ": " + outError);
				return nullptr;
			}

			const std::wstring pipelineName = StringHelper::Utf8ToWide(
				"Shader Injector Render Pass: " + renderPass.name);
			pipelineState->SetName(pipelineName.c_str());
			gPipelineCache.emplace(cacheKey, pipelineState);
			CacheThreadPipelineLookup(renderPass, rootSignature, renderTargets, pipelineState);
			ShaderInjectorIO::WriteToLogFile(
				"RenderPassExecutor->GetOrCreatePipelineState: created fullscreen pipeline for " + renderPass.name);
			return pipelineState;
		}
	}

	bool ExecuteFullscreenTriangle(
		const RenderPass::RenderPassDisk& renderPass,
		ID3D12GraphicsCommandList* commandList,
		ID3D12RootSignature* graphicsRootSignature,
		ID3D12PipelineState* pipelineStateToRestore,
		D3D12_PRIMITIVE_TOPOLOGY primitiveTopologyToRestore,
		const std::vector<RenderPass::ResourceBindingDiagnostic>& outputBindings,
		const RenderPassTexturePool::TextureView* runtimeOutput,
		const RenderPassMipChain::GraphicsStateSnapshot* gameStateToRestore,
		std::string& outError)
	{
		outError.clear();
		if (!commandList || !graphicsRootSignature || !pipelineStateToRestore)
		{
			outError = "The target draw does not have complete graphics state.";
			return false;
		}
		if (primitiveTopologyToRestore == D3D_PRIMITIVE_TOPOLOGY_UNDEFINED)
		{
			outError = "The target draw's primitive topology has not been observed yet.";
			return false;
		}
		if (!RenderPass::HasCompiledShaders(renderPass))
		{
			outError = "Render Pass shaders have not been compiled and loaded.";
			return false;
		}
		if (!HookD3D12::Original_SetPipelineState ||
			!HookD3D12::Original_IASetPrimitiveTopology ||
			!HookD3D12::Original_DrawInstanced)
		{
			outError = "One or more original D3D12 draw functions are unavailable.";
			return false;
		}
		if (runtimeOutput && (!gameStateToRestore ||
			gameStateToRestore->renderTargets.empty() ||
			gameStateToRestore->viewports.empty() ||
			gameStateToRestore->scissorRectangles.empty()))
		{
			outError = "Runtime output requires captured render-target, viewport, and scissor state.";
			return false;
		}

		RenderTargetState renderTargets{};
		const bool renderTargetStateAvailable = runtimeOutput
			? BuildRenderTargetState(*runtimeOutput, renderTargets)
			: BuildRenderTargetState(outputBindings, renderTargets);
		if (!renderTargetStateAvailable)
		{
			outError = "The target draw's render-target formats are not available yet.";
			return false;
		}

		ID3D12PipelineState* fullscreenPipelineState = GetOrCreatePipelineState(
			renderPass,
			commandList,
			graphicsRootSignature,
			renderTargets,
			outError);
		if (!fullscreenPipelineState)
			return false;

		const std::wstring& eventName = GetRenderPassEventName(renderPass);
		const bool renderDocCaptureActive =
			Globals::gRenderDocIntegrationEnabled && RenderDocIntegration::IsFrameCapturing();
		const uint64_t captureRequestSequence = RenderDocIntegration::GetCaptureRequestSequence();
		uint64_t loggedCaptureRequestSequence = gLoggedCaptureRequestSequence.load(std::memory_order_relaxed);
		if (captureRequestSequence > loggedCaptureRequestSequence &&
			gLoggedCaptureRequestSequence.compare_exchange_strong(
				loggedCaptureRequestSequence,
				captureRequestSequence,
				std::memory_order_relaxed))
		{
			ShaderInjectorIO::WriteToLogFile(StringHelper::Format(
				"RenderPassExecutor->ExecuteFullscreenTriangle: first pass execution after capture request sequence=%llu captureActive=%u pass=%s commandList=%p",
				static_cast<unsigned long long>(captureRequestSequence),
				renderDocCaptureActive ? 1u : 0u,
				renderPass.name.c_str(),
				commandList));
		}
		if (renderDocCaptureActive)
		{
			if (!gLoggedExecutionDuringActiveCapture.exchange(true, std::memory_order_relaxed))
			{
				ShaderInjectorIO::WriteToLogFileSuccess(
					"RenderPassExecutor->ExecuteFullscreenTriangle: pass executed during active RenderDoc capture: " +
					renderPass.name);
			}
		}
		else
		{
			gLoggedExecutionDuringActiveCapture.store(false, std::memory_order_relaxed);
		}

		{
			HookD3D12::ScopedRenderPassInjection injectionScope;
			commandList->BeginEvent(
				0,
				eventName.c_str(),
				static_cast<UINT>((eventName.size() + 1) * sizeof(wchar_t)));
			commandList->SetPipelineState(fullscreenPipelineState);
			commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			if (runtimeOutput)
			{
				D3D12_RESOURCE_BARRIER barrier{};
				barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				barrier.Transition.pResource = runtimeOutput->resource.Get();
				barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
				barrier.Transition.StateBefore = runtimeOutput->initialState;
				barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
				commandList->ResourceBarrier(1, &barrier);
				commandList->OMSetRenderTargets(1, &runtimeOutput->renderTargetView, FALSE, nullptr);
				const D3D12_VIEWPORT viewport{
					0.0f,
					0.0f,
					static_cast<float>(runtimeOutput->description.width),
					static_cast<float>(runtimeOutput->description.height),
					0.0f,
					1.0f };
				const D3D12_RECT scissor{
					0,
					0,
					static_cast<LONG>(runtimeOutput->description.width),
					static_cast<LONG>(runtimeOutput->description.height) };
				commandList->RSSetViewports(1, &viewport);
				commandList->RSSetScissorRects(1, &scissor);
			}
			commandList->DrawInstanced(3, 1, 0, 0);
			if (runtimeOutput)
			{
				D3D12_RESOURCE_BARRIER barrier{};
				barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				barrier.Transition.pResource = runtimeOutput->resource.Get();
				barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
				barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
				barrier.Transition.StateAfter = runtimeOutput->initialState;
				commandList->ResourceBarrier(1, &barrier);
				if (gameStateToRestore)
					RestoreGameOutputState(commandList, *gameStateToRestore);
			}
			commandList->IASetPrimitiveTopology(primitiveTopologyToRestore);
			commandList->SetPipelineState(pipelineStateToRestore);
			commandList->EndEvent();
		}
		return true;
	}

	bool ExecuteCompute(
		const RenderPass::RenderPassDisk& renderPass,
		ID3D12GraphicsCommandList* commandList,
		ID3D12PipelineState* computePipelineState,
		ID3D12PipelineState* pipelineStateToRestore,
		UINT threadGroupCountX,
		UINT threadGroupCountY,
		UINT threadGroupCountZ,
		const std::vector<RenderPassTexturePool::TextureView>& unorderedAccessOutputs,
		std::string& outError)
	{
		outError.clear();
		if (!commandList || !computePipelineState || !pipelineStateToRestore)
		{
			outError = "The target dispatch does not have complete compute state.";
			return false;
		}
		if (!threadGroupCountX || !threadGroupCountY || !threadGroupCountZ)
		{
			outError = "The injected compute dispatch resolved to zero thread groups.";
			return false;
		}
		if (!HookD3D12::Original_SetPipelineState || !HookD3D12::Original_Dispatch)
		{
			outError = "One or more original D3D12 compute functions are unavailable.";
			return false;
		}

		std::vector<D3D12_RESOURCE_BARRIER> transitions;
		transitions.reserve(unorderedAccessOutputs.size());
		for (const RenderPassTexturePool::TextureView& output : unorderedAccessOutputs)
		{
			if (!output.resource || !output.unorderedAccessView.ptr)
			{
				outError = "A compute output is missing its texture or UAV descriptor.";
				return false;
			}
			D3D12_RESOURCE_BARRIER barrier{};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition.pResource = output.resource.Get();
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			barrier.Transition.StateBefore = output.initialState;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
			transitions.push_back(barrier);
		}

		const std::wstring& eventName = GetRenderPassEventName(renderPass);
		{
			HookD3D12::ScopedRenderPassInjection injectionScope;
			commandList->BeginEvent(
				0,
				eventName.c_str(),
				static_cast<UINT>((eventName.size() + 1) * sizeof(wchar_t)));
			if (!transitions.empty())
				commandList->ResourceBarrier(static_cast<UINT>(transitions.size()), transitions.data());
			commandList->SetPipelineState(computePipelineState);
			HookD3D12::Original_Dispatch(
				commandList,
				threadGroupCountX,
				threadGroupCountY,
				threadGroupCountZ);

			if (!transitions.empty())
			{
				std::vector<D3D12_RESOURCE_BARRIER> completionBarriers;
				completionBarriers.reserve(transitions.size() * 2);
				for (D3D12_RESOURCE_BARRIER& transition : transitions)
				{
					D3D12_RESOURCE_BARRIER unorderedAccessBarrier{};
					unorderedAccessBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
					unorderedAccessBarrier.UAV.pResource = transition.Transition.pResource;
					completionBarriers.push_back(unorderedAccessBarrier);
					std::swap(transition.Transition.StateBefore, transition.Transition.StateAfter);
					completionBarriers.push_back(transition);
				}
				commandList->ResourceBarrier(
					static_cast<UINT>(completionBarriers.size()),
					completionBarriers.data());
			}
			commandList->SetPipelineState(pipelineStateToRestore);
			commandList->EndEvent();
		}
		return true;
	}

	bool ExecuteTextureCopy(
		ID3D12GraphicsCommandList* commandList,
		const RenderPassTexturePool::TextureView& source,
		const RenderPassTexturePool::TextureView& destination,
		std::string& outError)
	{
		outError.clear();
		if (!commandList || !source.resource || !destination.resource)
		{
			outError = "Copy pass requires valid source and destination textures.";
			return false;
		}
		if (source.resource.Get() == destination.resource.Get())
		{
			outError = "Copy pass source and destination refer to the same texture.";
			return false;
		}
		const auto& left = source.description;
		const auto& right = destination.description;
		if (left.dimension != right.dimension || left.format != right.format ||
			left.width != right.width || left.height != right.height ||
			left.depth != right.depth || left.arraySize != right.arraySize ||
			left.mipLevels != right.mipLevels || left.sampleCount != right.sampleCount)
		{
			outError = "Copy pass textures must have matching dimensions, format, mip count, and sample count.";
			return false;
		}

		std::array<D3D12_RESOURCE_BARRIER, 2> barriers{};
		barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barriers[0].Transition.pResource = source.resource.Get();
		barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barriers[0].Transition.StateBefore = source.initialState;
		barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
		barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barriers[1].Transition.pResource = destination.resource.Get();
		barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barriers[1].Transition.StateBefore = destination.initialState;
		barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
		{
			HookD3D12::ScopedRenderPassInjection injectionScope;
			if (source.initialState == D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
			{
				D3D12_RESOURCE_BARRIER unorderedAccessBarrier{};
				unorderedAccessBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
				unorderedAccessBarrier.UAV.pResource = source.resource.Get();
				commandList->ResourceBarrier(1, &unorderedAccessBarrier);
			}
			commandList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
			commandList->CopyResource(destination.resource.Get(), source.resource.Get());
			std::swap(barriers[0].Transition.StateBefore, barriers[0].Transition.StateAfter);
			std::swap(barriers[1].Transition.StateBefore, barriers[1].Transition.StateAfter);
			commandList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
		}
		return true;
	}

	void ReleaseResources()
	{
		RenderPassMipChain::ReleaseResources();
		RenderPassReplacement::ReleaseResources();
		RenderPassTexturePool::ReleaseResources();
		ShaderResourceRuntime::ReleaseResources();
		std::lock_guard<std::mutex> cacheLock(gPipelineCacheMutex);
		for (auto& cachedPipeline : gPipelineCache)
		{
			if (cachedPipeline.second)
				cachedPipeline.second->Release();
		}
		gPipelineCache.clear();
		gPipelineCreationErrors.clear();
		gThreadPipelineLookups = {};
		gNextThreadPipelineLookup = 0;
		gEventNameRenderPass = nullptr;
		gEventName.clear();
	}
}
