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

	//ID3D12Device::CreateRootSignature
	constexpr size_t indexCreateRootSignature = 16;

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

	//ID3D12GraphicsCommandList::SetPipelineState
	constexpr size_t indexSetPipelineState = 25;

	//ID3D12GraphicsCommandList::SetComputeRootSignature
	constexpr size_t indexSetComputeRootSignature = 29;

	//ID3D12GraphicsCommandList::SetGraphicsRootSignature
	constexpr size_t indexSetGraphicsRootSignature = 30;
}
