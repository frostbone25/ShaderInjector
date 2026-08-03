#include "RenderPassExecutor.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <unordered_map>

#include "HookD3D12.h"
#include "HookD3D12RenderPass.h"
#include "RenderPassMipChain.h"
#include "Globals.h"
#include "RenderDocIntegration.h"
#include "ShaderInjectorIO.h"
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
			const std::string cacheKey = BuildPipelineCacheKey(renderPass, rootSignature, renderTargets);
			std::lock_guard<std::mutex> cacheLock(gPipelineCacheMutex);
			const auto cachedPipelineIt = gPipelineCache.find(cacheKey);
			if (cachedPipelineIt != gPipelineCache.end())
				return cachedPipelineIt->second;
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

		RenderTargetState renderTargets{};
		if (!BuildRenderTargetState(outputBindings, renderTargets))
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

		const std::wstring eventName = StringHelper::Utf8ToWide(
			"Shader Injector Render Pass: " + renderPass.name);
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
			commandList->DrawInstanced(3, 1, 0, 0);
			commandList->IASetPrimitiveTopology(primitiveTopologyToRestore);
			commandList->SetPipelineState(pipelineStateToRestore);
			commandList->EndEvent();
		}
		return true;
	}

	void ReleaseResources()
	{
		RenderPassMipChain::ReleaseResources();
		std::lock_guard<std::mutex> cacheLock(gPipelineCacheMutex);
		for (auto& cachedPipeline : gPipelineCache)
		{
			if (cachedPipeline.second)
				cachedPipeline.second->Release();
		}
		gPipelineCache.clear();
		gPipelineCreationErrors.clear();
	}
}
