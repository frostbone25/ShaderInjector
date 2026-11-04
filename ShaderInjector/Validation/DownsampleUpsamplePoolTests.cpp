#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <dxgi1_6.h>
#include <wrl/client.h>

#include "IO/ShaderInjectorIO.h"
#include "RenderPass/RenderPassTexturePool.h"
#include "ShaderModelDetector.h"

namespace StringHelper
{
	std::string ShaderModelToString(Globals::ShaderModel)
	{
		throw std::logic_error("texture allocation validation must not choose shader profiles");
	}
}

namespace ShaderModelDetector
{
	Globals::ShaderModel GetEffectiveShaderModel(ShaderTarget::ShaderType, Globals::ShaderModel)
	{
		throw std::logic_error("texture allocation validation must not inspect shader models");
	}
}

namespace
{
	using Microsoft::WRL::ComPtr;
	constexpr UINT sourceWidth = 63;
	constexpr UINT sourceHeight = 35;
	constexpr DXGI_FORMAT testFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
	constexpr D3D12_RESOURCE_STATES shaderReadState =
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

	void Check(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	void CheckResult(HRESULT result)
	{
		Check(SUCCEEDED(result), "D3D12 WARP setup failed.");
	}

	struct TestTexture
	{
		RenderPassTexturePool::TextureView view;
	};

	TestTexture CreateTexture(ID3D12Device* device, UINT width, UINT height)
	{
		TestTexture texture;
		D3D12_HEAP_PROPERTIES heapProperties{};
		heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
		D3D12_RESOURCE_DESC resourceDescription{};
		resourceDescription.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		resourceDescription.Width = width;
		resourceDescription.Height = height;
		resourceDescription.DepthOrArraySize = 1;
		resourceDescription.MipLevels = 1;
		resourceDescription.Format = testFormat;
		resourceDescription.SampleDesc.Count = 1;
		resourceDescription.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		resourceDescription.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		CheckResult(device->CreateCommittedResource(
			&heapProperties,
			D3D12_HEAP_FLAG_NONE,
			&resourceDescription,
			shaderReadState,
			nullptr,
			IID_PPV_ARGS(&texture.view.resource)));

		D3D12_DESCRIPTOR_HEAP_DESC heapDescription{};
		heapDescription.NumDescriptors = 3;
		heapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		CheckResult(device->CreateDescriptorHeap(&heapDescription, IID_PPV_ARGS(&texture.view.shaderViewHeap)));
		const UINT descriptorIncrement = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		texture.view.shaderResourceView = texture.view.shaderViewHeap->GetCPUDescriptorHandleForHeapStart();
		texture.view.unorderedAccessView = texture.view.shaderResourceView;
		texture.view.unorderedAccessView.ptr += descriptorIncrement;
		D3D12_CPU_DESCRIPTOR_HANDLE renderTargetHandle = texture.view.shaderResourceView;
		renderTargetHandle.ptr += descriptorIncrement * 2;
		D3D12_SHADER_RESOURCE_VIEW_DESC shaderViewDescription{};
		shaderViewDescription.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		shaderViewDescription.Format = testFormat;
		shaderViewDescription.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		shaderViewDescription.Texture2D.MipLevels = 1;
		device->CreateShaderResourceView(texture.view.resource.Get(), &shaderViewDescription, texture.view.shaderResourceView);
		D3D12_UNORDERED_ACCESS_VIEW_DESC unorderedAccessDescription{};
		unorderedAccessDescription.Format = testFormat;
		unorderedAccessDescription.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		device->CreateUnorderedAccessView(texture.view.resource.Get(), nullptr, &unorderedAccessDescription, texture.view.unorderedAccessView);
		D3D12_DESCRIPTOR_HEAP_DESC renderTargetHeapDescription{};
		renderTargetHeapDescription.NumDescriptors = 1;
		renderTargetHeapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		CheckResult(device->CreateDescriptorHeap(&renderTargetHeapDescription, IID_PPV_ARGS(&texture.view.renderTargetViewHeap)));
		texture.view.renderTargetView = texture.view.renderTargetViewHeap->GetCPUDescriptorHandleForHeapStart();
		device->CreateRenderTargetView(texture.view.resource.Get(), nullptr, texture.view.renderTargetView);
		texture.view.description.dimension = ShaderResource::TextureDimension::Texture2D;
		texture.view.description.format = testFormat;
		texture.view.description.shaderViewFormat = testFormat;
		texture.view.description.width = width;
		texture.view.description.height = height;
		texture.view.description.depth = 1;
		texture.view.description.arraySize = 1;
		texture.view.description.mipLevels = 1;
		texture.view.description.sampleCount = 1;
		texture.view.description.flags = resourceDescription.Flags;
		texture.view.initialState = shaderReadState;
		return texture;
	}

	void CheckStages(
		const std::vector<RenderPassTexturePool::TextureView>& stages,
		const RenderPassTexturePool::TextureView& source,
		const RenderPassTexturePool::TextureView& destination,
		bool computePipeline)
	{
		const std::array<std::array<UINT, 2>, 6> expectedSizes = {{{31, 17}, {15, 8}, {7, 4}, {15, 8}, {31, 17}, {63, 35}}};
		Check(stages.size() == expectedSizes.size(), "Factor-eight chain did not allocate six stages.");
		for (size_t index = 0; index < stages.size(); ++index)
		{
			const RenderPassTexturePool::TextureView& stage = stages[index];
			Check(stage.resource.Get() != nullptr, "A chain stage has no resource.");
			Check(stage.description.width == expectedSizes[index][0] && stage.description.height == expectedSizes[index][1], "A chain stage has the wrong extent.");
			Check(stage.shaderResourceView.ptr != 0 && stage.shaderViewHeap.Get() != nullptr, "A chain stage has no SRV.");
			Check(stage.initialState == shaderReadState, "A chain stage does not begin in shader-read state.");
			if (stage.resource.Get() != destination.resource.Get())
			{
				Check(stage.resource.Get() != source.resource.Get(), "An intermediate chain stage aliases the source.");
				if (computePipeline)
					Check(stage.unorderedAccessView.ptr != 0, "A compute stage has no UAV.");
				else
					Check(stage.renderTargetView.ptr != 0 && stage.renderTargetViewHeap.Get() != nullptr, "A graphics stage has no RTV.");
			}
			if (index > 0)
				Check(stages[index - 1].resource.Get() != stage.resource.Get(), "Consecutive chain stages alias the same resource.");
		}
		Check(stages.back().resource.Get() == destination.resource.Get(), "The last chain stage is not the requested destination.");
	}

	void RunMode(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE commandListType, bool computePipeline, const char* passID)
	{
		ComPtr<ID3D12CommandAllocator> allocator;
		CheckResult(device->CreateCommandAllocator(commandListType, IID_PPV_ARGS(&allocator)));
		ComPtr<ID3D12GraphicsCommandList> commandList;
		CheckResult(device->CreateCommandList(0, commandListType, allocator.Get(), nullptr, IID_PPV_ARGS(&commandList)));
		TestTexture source = CreateTexture(device, sourceWidth, sourceHeight);
		TestTexture destination = CreateTexture(device, sourceWidth, sourceHeight);
		RenderPass::RenderPassDisk renderPass{};
		renderPass.id = passID;
		renderPass.name = passID;
		renderPass.type = RenderPass::RenderPassType::DownsampleUpsample;
		renderPass.chainDownsampleFactor = 8;
		std::vector<RenderPassTexturePool::TextureView> stages;
		std::string error;
		Check(RenderPassTexturePool::EnsureDownsampleUpsampleStages(renderPass, commandList.Get(), source.view, destination.view, computePipeline, stages, error), "WARP stage allocation failed.");
		CheckStages(stages, source.view, destination.view, computePipeline);
		Check(RenderPassTexturePool::HasRecordedCommandListWork(), "Stage allocation was not tracked for command-list retirement.");
		RenderPassTexturePool::ResetCommandListRecording(commandList.Get());
		Check(!RenderPassTexturePool::HasRecordedCommandListWork(), "Reset did not release recorded stage tracking.");
		const uint64_t retiredGeneration = stages.front().generation;
		stages.clear();
		RenderPassTexturePool::ReleaseResources();
		Check(RenderPassTexturePool::EnsureDownsampleUpsampleStages(renderPass, commandList.Get(), source.view, destination.view, computePipeline, stages, error), "WARP stage reallocation failed after release.");
		Check(stages.front().generation != retiredGeneration, "Released stages were not assigned a new allocation generation.");
		RenderPassTexturePool::ResetCommandListRecording(commandList.Get());
		stages.clear();
		RenderPassTexturePool::ReleaseResources();
	}

	void RunInvalidCases(ID3D12Device* device, ID3D12GraphicsCommandList* commandList)
	{
		TestTexture source = CreateTexture(device, sourceWidth, sourceHeight);
		TestTexture destination = CreateTexture(device, sourceWidth, sourceHeight);
		RenderPass::RenderPassDisk renderPass{};
		renderPass.id = "InvalidChain";
		renderPass.name = renderPass.id;
		renderPass.type = RenderPass::RenderPassType::DownsampleUpsample;
		renderPass.chainDownsampleFactor = 8;
		std::vector<RenderPassTexturePool::TextureView> stages;
		std::string error;
		Check(!RenderPassTexturePool::EnsureDownsampleUpsampleStages(renderPass, commandList, source.view, source.view, false, stages, error), "Same-resource chain was accepted.");
		Check(stages.empty(), "Rejected same-resource chain left stage outputs.");
		destination.view.description.width -= 1;
		Check(!RenderPassTexturePool::EnsureDownsampleUpsampleStages(renderPass, commandList, source.view, destination.view, false, stages, error), "Mismatched extents were accepted.");
		Check(stages.empty(), "Rejected mismatched chain left stage outputs.");
		destination.view.description.width += 1;
		source.view.description.arraySize = 2;
		Check(!RenderPassTexturePool::EnsureDownsampleUpsampleStages(renderPass, commandList, source.view, destination.view, false, stages, error), "Unsupported array texture shape was accepted.");
		Check(stages.empty(), "Rejected unsupported shape left stage outputs.");
	}
}

namespace ShaderInjectorIO
{
	void WriteToLogFile(const std::string&) {}
	void WriteToLogFileError(const std::string&) {}
	void WriteToLogFileWarning(const std::string&) {}
}

namespace ShaderResourceCatalog
{
	void Upsert(const ShaderResource::CatalogEntry&) {}
}

namespace RenderPass
{
	bool IsValidDownsampleUpsampleFactor(uint32_t factor)
	{
		return factor >= 2 && factor <= 1024 && (factor & (factor - 1)) == 0;
	}
}

int main()
{
	try
	{
		ComPtr<IDXGIFactory6> factory;
		CheckResult(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
		ComPtr<IDXGIAdapter> warpAdapter;
		CheckResult(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));
		ComPtr<ID3D12Device> device;
		CheckResult(D3D12CreateDevice(warpAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
		RunMode(device.Get(), D3D12_COMMAND_LIST_TYPE_DIRECT, false, "WarpGraphicsChain");
		RunMode(device.Get(), D3D12_COMMAND_LIST_TYPE_COMPUTE, true, "WarpComputeChain");
		ComPtr<ID3D12CommandAllocator> allocator;
		CheckResult(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
		ComPtr<ID3D12GraphicsCommandList> commandList;
		CheckResult(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&commandList)));
		RunInvalidCases(device.Get(), commandList.Get());
		RenderPassTexturePool::ReleaseResources();
		std::cout << "DownsampleUpsamplePoolTests passed.\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		RenderPassTexturePool::ReleaseResources();
		std::cerr << "DownsampleUpsamplePoolTests failed: " << error.what() << '\n';
		return 1;
	}
}
