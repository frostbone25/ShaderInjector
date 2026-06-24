#pragma once

namespace VTableIndex
{
	// VTable indices derived from the official DirectX interface order.
	// These values are stable across Windows versions and SDKs.
	constexpr size_t indexPresent = 8;            // IDXGISwapChain::Present
	constexpr size_t indexPresent1 = 22;           // IDXGISwapChain1::Present1
	constexpr size_t indexResizeBuffers = 13;     // IDXGISwapChain::ResizeBuffers
	constexpr size_t indexExecuteCommandLists = 10; // ID3D12CommandQueue::ExecuteCommandLists
	constexpr size_t indexCreateGraphicsPipelineState = 10;
	constexpr size_t indexCreateComputePipelineState = 11;
	constexpr size_t indexCreateRootSignature = 16;
	constexpr size_t indexCreatePipelineState = 47;
	constexpr size_t indexCreatePipelineLibrary = 44;
	constexpr size_t indexStorePipeline = 8;
	constexpr size_t indexLoadGraphicsPipeline = 9;
	constexpr size_t indexLoadComputePipeline = 10;
	constexpr size_t indexGetSerializedSize = 11;
	constexpr size_t indexSerialize = 12;
	constexpr size_t indexLoadPipeline = 13;
	constexpr size_t indexResetGraphicsCommandList = 10;
	constexpr size_t indexSetPipelineState = 25;
	constexpr size_t indexSetComputeRootSignature = 29;
	constexpr size_t indexSetGraphicsRootSignature = 30;
}