#pragma once

namespace VTableIndex
{
	//NOTE: these indexes for VTables SHOULD stable across windows versions and SDKs.

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| IDXGIFactory |||||||||||||||||||||||||||||||||||||||||||||||||||||

	//IDXGIFactory::CreateSwapChain
	constexpr size_t indexCreateSwapChain = 10;

	//IDXGIFactory2::CreateSwapChainForHwnd
	constexpr size_t indexCreateSwapChainForHwnd = 15;

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| IDXGISwapChain |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| IDXGISwapChain |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| IDXGISwapChain |||||||||||||||||||||||||||||||||||||||||||||||||||||

	//IDXGISwapChain::Present
	constexpr size_t indexPresent = 8;

	//IDXGISwapChain::ResizeBuffers
	constexpr size_t indexResizeBuffers = 13;

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| IDXGISwapChain1 |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| IDXGISwapChain1 |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| IDXGISwapChain1 |||||||||||||||||||||||||||||||||||||||||||||||||||||

	//IDXGISwapChain1::Present1
	constexpr size_t indexPresent1 = 22;

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| ID3D12CommandQueue |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| ID3D12CommandQueue |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| ID3D12CommandQueue |||||||||||||||||||||||||||||||||||||||||||||||||||||

	//ID3D12CommandQueue::ExecuteCommandLists
	constexpr size_t indexExecuteCommandLists = 10;

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| ID3D12Device |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| ID3D12Device |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| ID3D12Device |||||||||||||||||||||||||||||||||||||||||||||||||||||

	//ID3D12Device::CreateGraphicsPipelineState
	constexpr size_t indexCreateGraphicsPipelineState = 10;

	//ID3D12Device::CreateComputePipelineState
	constexpr size_t indexCreateComputePipelineState = 11;

	//ID3D12Device::CreateCommandList
	constexpr size_t indexCreateCommandList = 12;

	//ID3D12Device::CreateRootSignature
	constexpr size_t indexCreateRootSignature = 16;

	//ID3D12Device descriptor creation and copy methods
	constexpr size_t indexCreateConstantBufferView = 17;
	constexpr size_t indexCreateShaderResourceView = 18;
	constexpr size_t indexCreateUnorderedAccessView = 19;
	constexpr size_t indexCreateRenderTargetView = 20;
	constexpr size_t indexCreateDepthStencilView = 21;
	constexpr size_t indexCreateSampler = 22;
	constexpr size_t indexCopyDescriptors = 23;
	constexpr size_t indexCopyDescriptorsSimple = 24;

	//ID3D12Device resource creation methods
	constexpr size_t indexCreateCommittedResource = 27;
	constexpr size_t indexCreatePlacedResource = 29;
	constexpr size_t indexCreateReservedResource = 30;

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| ID3D12Device1 |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| ID3D12Device1 |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| ID3D12Device1 |||||||||||||||||||||||||||||||||||||||||||||||||||||

	//ID3D12Device1::CreatePipelineLibrary
	constexpr size_t indexCreatePipelineState = 47;

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| ID3D12Device2 |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| ID3D12Device2 |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| ID3D12Device2 |||||||||||||||||||||||||||||||||||||||||||||||||||||

	//ID3D12Device2::CreatePipelineState
	constexpr size_t indexCreatePipelineLibrary = 44;

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| ID3D12PipelineLibrary |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| ID3D12PipelineLibrary |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| ID3D12PipelineLibrary |||||||||||||||||||||||||||||||||||||||||||||||||||||

	//ID3D12PipelineLibrary::StorePipeline
	constexpr size_t indexStorePipeline = 8;

	//ID3D12PipelineLibrary::LoadGraphicsPipeline
	constexpr size_t indexLoadGraphicsPipeline = 9;

	//ID3D12PipelineLibrary::LoadComputePipeline
	constexpr size_t indexLoadComputePipeline = 10;

	//ID3D12PipelineLibrary::GetSerializedSize
	constexpr size_t indexGetSerializedSize = 11;

	//ID3D12PipelineLibrary::Serialize
	constexpr size_t indexSerialize = 12;

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| ID3D12PipelineLibrary1 |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| ID3D12PipelineLibrary1 |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| ID3D12PipelineLibrary1 |||||||||||||||||||||||||||||||||||||||||||||||||||||

	//ID3D12PipelineLibrary1::LoadPipeline
	constexpr size_t indexLoadPipeline = 13;

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| ID3D12GraphicsCommandList |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| ID3D12GraphicsCommandList |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| ID3D12GraphicsCommandList |||||||||||||||||||||||||||||||||||||||||||||||||||||

	//ID3D12GraphicsCommandList::Reset
	constexpr size_t indexResetGraphicsCommandList = 10;

	//ID3D12GraphicsCommandList::DrawInstanced
	constexpr size_t indexDrawInstanced = 12;

	//ID3D12GraphicsCommandList::DrawIndexedInstanced
	constexpr size_t indexDrawIndexedInstanced = 13;

	//ID3D12GraphicsCommandList::Dispatch
	constexpr size_t indexDispatch = 14;

	//ID3D12GraphicsCommandList::IASetPrimitiveTopology
	constexpr size_t indexIASetPrimitiveTopology = 20;

	//ID3D12GraphicsCommandList rasterizer state
	constexpr size_t indexRSSetViewports = 21;
	constexpr size_t indexRSSetScissorRects = 22;

	//ID3D12GraphicsCommandList::SetPipelineState
	constexpr size_t indexSetPipelineState = 25;

	//ID3D12GraphicsCommandList::SetDescriptorHeaps
	constexpr size_t indexSetDescriptorHeaps = 28;

	//ID3D12GraphicsCommandList::SetComputeRootSignature
	constexpr size_t indexSetComputeRootSignature = 29;

	//ID3D12GraphicsCommandList::SetGraphicsRootSignature
	constexpr size_t indexSetGraphicsRootSignature = 30;

	//ID3D12GraphicsCommandList root bindings
	constexpr size_t indexSetComputeRootDescriptorTable = 31;
	constexpr size_t indexSetGraphicsRootDescriptorTable = 32;
	constexpr size_t indexSetComputeRoot32BitConstant = 33;
	constexpr size_t indexSetGraphicsRoot32BitConstant = 34;
	constexpr size_t indexSetComputeRoot32BitConstants = 35;
	constexpr size_t indexSetGraphicsRoot32BitConstants = 36;
	constexpr size_t indexSetComputeRootConstantBufferView = 37;
	constexpr size_t indexSetGraphicsRootConstantBufferView = 38;
	constexpr size_t indexSetComputeRootShaderResourceView = 39;
	constexpr size_t indexSetGraphicsRootShaderResourceView = 40;
	constexpr size_t indexSetComputeRootUnorderedAccessView = 41;
	constexpr size_t indexSetGraphicsRootUnorderedAccessView = 42;

	//ID3D12GraphicsCommandList input assembler and output merger bindings
	constexpr size_t indexIASetIndexBuffer = 43;
	constexpr size_t indexIASetVertexBuffers = 44;
	constexpr size_t indexOMSetRenderTargets = 46;

	//ID3D12GraphicsCommandList::ExecuteIndirect
	constexpr size_t indexExecuteIndirect = 59;
}
