#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace HookD3D12::AsyncPipelineCreation
{
	enum class TicketStatus
	{
		Pending,
		Ready,
		Failed,
		Unavailable
	};

	struct TicketResult
	{
		TicketStatus status = TicketStatus::Pending;
		HRESULT result = E_PENDING;
		Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;
	};

	struct OwnedGraphicsPipelineDesc
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC description{};
		Microsoft::WRL::ComPtr<ID3D12PipelineState> originalPipelineState;
		Microsoft::WRL::ComPtr<ID3D12Device> device;
		Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
		std::vector<uint8_t> vertexShaderBytecode;
		std::vector<uint8_t> pixelShaderBytecode;
		std::vector<uint8_t> geometryShaderBytecode;
		std::vector<uint8_t> hullShaderBytecode;
		std::vector<uint8_t> domainShaderBytecode;
		std::vector<D3D12_INPUT_ELEMENT_DESC> inputElements;
		std::vector<std::string> inputElementSemanticNames;
		std::vector<D3D12_SO_DECLARATION_ENTRY> streamOutputDeclarations;
		std::vector<std::string> streamOutputSemanticNames;
		std::vector<UINT> streamOutputStrides;

		bool Initialize(
			ID3D12PipelineState* original,
			const D3D12_GRAPHICS_PIPELINE_STATE_DESC& sourceDescription);
	};

	struct OwnedComputePipelineDesc
	{
		D3D12_COMPUTE_PIPELINE_STATE_DESC description{};
		Microsoft::WRL::ComPtr<ID3D12PipelineState> originalPipelineState;
		Microsoft::WRL::ComPtr<ID3D12Device> device;
		Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
		std::vector<uint8_t> computeShaderBytecode;

		bool Initialize(
			ID3D12PipelineState* original,
			const D3D12_COMPUTE_PIPELINE_STATE_DESC& sourceDescription);
	};

	struct OwnedStreamPipelineDesc
	{
		D3D12_PIPELINE_STATE_STREAM_DESC description{};
		Microsoft::WRL::ComPtr<ID3D12PipelineState> originalPipelineState;
		Microsoft::WRL::ComPtr<ID3D12Device2> device;
		Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
		std::vector<uint8_t> streamBlob;
		std::vector<uint8_t> vertexShaderBytecode;
		std::vector<uint8_t> pixelShaderBytecode;
		std::vector<uint8_t> geometryShaderBytecode;
		std::vector<uint8_t> hullShaderBytecode;
		std::vector<uint8_t> domainShaderBytecode;
		std::vector<uint8_t> computeShaderBytecode;
		std::vector<uint8_t> amplificationShaderBytecode;
		std::vector<uint8_t> meshShaderBytecode;
		std::vector<D3D12_INPUT_ELEMENT_DESC> inputElements;
		std::vector<std::string> inputElementSemanticNames;
		std::vector<D3D12_SO_DECLARATION_ENTRY> streamOutputDeclarations;
		std::vector<std::string> streamOutputSemanticNames;
		std::vector<UINT> streamOutputStrides;
		std::vector<D3D12_VIEW_INSTANCE_LOCATION> viewInstanceLocations;

		bool Initialize(
			ID3D12PipelineState* original,
			ID3D12RootSignature* selectedRootSignature,
			const std::vector<uint8_t>& sourceStreamBlob,
			const std::vector<uint8_t>& replacementBytecode,
			D3D12_PIPELINE_STATE_SUBOBJECT_TYPE replacementType,
			const std::vector<uint8_t>& vertexBytecode,
			const std::vector<uint8_t>& pixelBytecode,
			const std::vector<uint8_t>& geometryBytecode,
			const std::vector<uint8_t>& hullBytecode,
			const std::vector<uint8_t>& domainBytecode,
			const std::vector<uint8_t>& computeBytecode,
			const std::vector<uint8_t>& amplificationBytecode,
			const std::vector<uint8_t>& meshBytecode,
			const std::vector<D3D12_INPUT_ELEMENT_DESC>& sourceInputElements,
			const std::vector<std::string>& sourceInputSemanticNames,
			const std::vector<D3D12_SO_DECLARATION_ENTRY>& sourceStreamOutputDeclarations,
			const std::vector<std::string>& sourceStreamOutputSemanticNames,
			const std::vector<UINT>& sourceStreamOutputStrides,
			const std::vector<D3D12_VIEW_INSTANCE_LOCATION>& sourceViewInstanceLocations);
	};

	using NativeCreateCallback = std::function<HRESULT(ID3D12PipelineState**)>;

	bool Request(
		const std::string& key,
		NativeCreateCallback createCallback,
		bool urgent,
		bool* outQueued = nullptr,
		bool queueHookApplyOnCompletion = false);
	bool Promote(const std::string& key);
	TicketResult Poll(const std::string& key);
	void QueueReleaseOnWorker(Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState);
	void ReleaseResources();
}
