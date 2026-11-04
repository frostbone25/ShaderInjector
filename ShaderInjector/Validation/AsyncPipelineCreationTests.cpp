#include <condition_variable>
#include <algorithm>
#include <cstring>
#include <deque>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

#include "HookD3D12/AsyncPipelineCreation.h"
#include "HookD3D12/HookD3D12.h"
#include "ModifiedShader/DatabaseModifiedShaders.h"

namespace
{
	struct WorkerJob
	{
		std::function<void()> callback;
		bool urgent = false;
	};

	std::deque<WorkerJob> gWorkerQueue;
	std::mutex gWorkerQueueMutex;
	Microsoft::WRL::ComPtr<ID3D12Device> gWarpDevice;
	Microsoft::WRL::ComPtr<ID3D12Device2> gWarpDevice2;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> gRootSignature;

	void Check(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	WorkerJob PopWorkerJob()
	{
		std::lock_guard<std::mutex> lock(gWorkerQueueMutex);
		WorkerJob job = std::move(gWorkerQueue.front());
		gWorkerQueue.pop_front();
		return job;
	}

	void DrainWorkerQueue()
	{
		while (true)
		{
			WorkerJob job;
			{
				std::lock_guard<std::mutex> lock(gWorkerQueueMutex);
				if (gWorkerQueue.empty())
					return;
				job = std::move(gWorkerQueue.front());
				gWorkerQueue.pop_front();
			}

			std::thread worker([callback = std::move(job.callback)]() { callback(); });
			worker.join();
		}
	}

	std::vector<uint8_t> CompileShader(const char* sourceText, const char* targetProfile)
	{
		Microsoft::WRL::ComPtr<ID3DBlob> shaderBlob;
		Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
		const HRESULT result = D3DCompile(sourceText, strlen(sourceText), nullptr, nullptr, nullptr, "main", targetProfile,
			D3DCOMPILE_ENABLE_STRICTNESS, 0, &shaderBlob, &errorBlob);
		Check(SUCCEEDED(result) && shaderBlob, "WARP test shader should compile with the system HLSL compiler");
		const uint8_t* firstByte = static_cast<const uint8_t*>(shaderBlob->GetBufferPointer());
		return std::vector<uint8_t>(firstByte, firstByte + shaderBlob->GetBufferSize());
	}

	void CreateWarpDeviceAndRootSignature()
	{
		Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
		Check(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))), "WARP test should create a DXGI factory");
		Microsoft::WRL::ComPtr<IDXGIAdapter> warpAdapter;
		Check(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter))), "WARP adapter should be available");
		Check(SUCCEEDED(D3D12CreateDevice(warpAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&gWarpDevice))),
			"WARP should create a D3D12 device");
		Check(SUCCEEDED(gWarpDevice.As(&gWarpDevice2)), "WARP device should expose ID3D12Device2");

		D3D12_ROOT_SIGNATURE_DESC rootDescription{};
		rootDescription.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
		Microsoft::WRL::ComPtr<ID3DBlob> rootBlob;
		Microsoft::WRL::ComPtr<ID3DBlob> rootError;
		Check(SUCCEEDED(D3D12SerializeRootSignature(&rootDescription, D3D_ROOT_SIGNATURE_VERSION_1, &rootBlob, &rootError)),
			"WARP test root signature should serialize");
		Check(SUCCEEDED(gWarpDevice->CreateRootSignature(0, rootBlob->GetBufferPointer(), rootBlob->GetBufferSize(), IID_PPV_ARGS(&gRootSignature))),
			"WARP should create the test root signature");
	}

	void RunAndPollTicket(const std::string& ticketKey, Microsoft::WRL::ComPtr<ID3D12PipelineState>& pipelineState)
	{
		WorkerJob job = PopWorkerJob();
		std::thread worker([callback = std::move(job.callback)]() { callback(); });
		worker.join();
		HookD3D12::AsyncPipelineCreation::TicketResult result = HookD3D12::AsyncPipelineCreation::Poll(ticketKey);
		Check(result.status == HookD3D12::AsyncPipelineCreation::TicketStatus::Ready, "WARP native pipeline creation should complete successfully");
		pipelineState = std::move(result.pipelineState);
	}

	void TestOwnedSnapshotsCreateNativeWarpPipelines()
	{
		if (!gWarpDevice)
			CreateWarpDeviceAndRootSignature();

		std::vector<uint8_t> computeBytecode = CompileShader("[numthreads(1, 1, 1)] void main() {}", "cs_5_0");
		D3D12_COMPUTE_PIPELINE_STATE_DESC computeDescription{};
		computeDescription.pRootSignature = gRootSignature.Get();
		computeDescription.CS = {computeBytecode.data(), computeBytecode.size()};
		Microsoft::WRL::ComPtr<ID3D12PipelineState> originalComputePipeline;
		Check(SUCCEEDED(gWarpDevice->CreateComputePipelineState(&computeDescription, IID_PPV_ARGS(&originalComputePipeline))),
			"WARP should create the original compute PSO");
		auto ownedCompute = std::make_shared<HookD3D12::AsyncPipelineCreation::OwnedComputePipelineDesc>();
		Check(ownedCompute->Initialize(originalComputePipeline.Get(), computeDescription), "compute snapshot should own its bytecode");
		std::fill(computeBytecode.begin(), computeBytecode.end(), 0);
		const std::string computeTicket = "warp-owned-compute";
		Check(HookD3D12::AsyncPipelineCreation::Request(computeTicket,
			[ownedCompute](ID3D12PipelineState** output)
			{
				return ownedCompute->device->CreateComputePipelineState(&ownedCompute->description, IID_PPV_ARGS(output));
			}, true), "compute ticket should queue");
		Microsoft::WRL::ComPtr<ID3D12PipelineState> rebuiltComputePipeline;
		RunAndPollTicket(computeTicket, rebuiltComputePipeline);

		std::vector<uint8_t> vertexBytecode = CompileShader("float4 main(float3 position : POSITION) : SV_Position { return float4(position, 1); }", "vs_5_0");
		std::vector<uint8_t> pixelBytecode = CompileShader("float4 main() : SV_Target { return float4(1, 0, 0, 1); }", "ps_5_0");
		std::string semanticName = "POSITION";
		D3D12_INPUT_ELEMENT_DESC inputElement{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0};
		D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsDescription{};
		graphicsDescription.pRootSignature = gRootSignature.Get();
		graphicsDescription.VS = {vertexBytecode.data(), vertexBytecode.size()};
		graphicsDescription.PS = {pixelBytecode.data(), pixelBytecode.size()};
		graphicsDescription.InputLayout = {&inputElement, 1};
		inputElement.SemanticName = semanticName.c_str();
		graphicsDescription.SampleMask = UINT_MAX;
		graphicsDescription.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		graphicsDescription.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		graphicsDescription.RasterizerState.DepthClipEnable = TRUE;
		graphicsDescription.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		graphicsDescription.DepthStencilState.DepthEnable = FALSE;
		graphicsDescription.DepthStencilState.StencilEnable = FALSE;
		graphicsDescription.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		graphicsDescription.NumRenderTargets = 1;
		graphicsDescription.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		graphicsDescription.SampleDesc.Count = 1;
		Microsoft::WRL::ComPtr<ID3D12PipelineState> originalGraphicsPipeline;
		Check(SUCCEEDED(gWarpDevice->CreateGraphicsPipelineState(&graphicsDescription, IID_PPV_ARGS(&originalGraphicsPipeline))),
			"WARP should create the original graphics PSO");
		auto ownedGraphics = std::make_shared<HookD3D12::AsyncPipelineCreation::OwnedGraphicsPipelineDesc>();
		Check(ownedGraphics->Initialize(originalGraphicsPipeline.Get(), graphicsDescription), "graphics snapshot should own bytecode and semantic names");
		std::fill(vertexBytecode.begin(), vertexBytecode.end(), 0);
		std::fill(pixelBytecode.begin(), pixelBytecode.end(), 0);
		semanticName.assign("CHANGED");
		const std::string graphicsTicket = "warp-owned-graphics";
		Check(HookD3D12::AsyncPipelineCreation::Request(graphicsTicket,
			[ownedGraphics](ID3D12PipelineState** output)
			{
				return ownedGraphics->device->CreateGraphicsPipelineState(&ownedGraphics->description, IID_PPV_ARGS(output));
			}, true), "graphics ticket should queue");
		Microsoft::WRL::ComPtr<ID3D12PipelineState> rebuiltGraphicsPipeline;
		RunAndPollTicket(graphicsTicket, rebuiltGraphicsPipeline);
		Check(ownedGraphics->inputElements[0].SemanticName == ownedGraphics->inputElementSemanticNames[0].c_str(),
			"graphics semantic pointer should remain bound to owned storage");

		std::vector<uint8_t> streamComputeBytecode = CompileShader("[numthreads(1, 1, 1)] void main() {}", "cs_5_0");
		struct StreamRootSubobject { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type; ID3D12RootSignature* rootSignature; };
		struct StreamComputeSubobject { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type; D3D12_SHADER_BYTECODE bytecode; };
		struct ComputePipelineStream { StreamRootSubobject root; StreamComputeSubobject compute; };
		ComputePipelineStream computeStream{};
		computeStream.root.type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE;
		computeStream.root.rootSignature = gRootSignature.Get();
		computeStream.compute.type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS;
		computeStream.compute.bytecode = {streamComputeBytecode.data(), streamComputeBytecode.size()};
		std::vector<uint8_t> streamBytes(sizeof(computeStream));
		std::memcpy(streamBytes.data(), &computeStream, sizeof(computeStream));
		D3D12_PIPELINE_STATE_STREAM_DESC streamDescription{streamBytes.size(), streamBytes.data()};
		Microsoft::WRL::ComPtr<ID3D12PipelineState> originalStreamPipeline;
		Check(SUCCEEDED(gWarpDevice2->CreatePipelineState(&streamDescription, IID_PPV_ARGS(&originalStreamPipeline))),
			"WARP should create the original stream PSO");
		auto ownedStream = std::make_shared<HookD3D12::AsyncPipelineCreation::OwnedStreamPipelineDesc>();
		Check(ownedStream->Initialize(originalStreamPipeline.Get(), gRootSignature.Get(), streamBytes, streamComputeBytecode,
			D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS, {}, {}, {}, {}, {}, streamComputeBytecode, {}, {}, {}, {}, {}, {}, {}, {}),
			"stream snapshot should own its stream and stage bytecode");
		std::fill(streamComputeBytecode.begin(), streamComputeBytecode.end(), 0);
		const std::string streamTicket = "warp-owned-stream";
		Check(HookD3D12::AsyncPipelineCreation::Request(streamTicket,
			[ownedStream](ID3D12PipelineState** output)
			{
				return ownedStream->device->CreatePipelineState(&ownedStream->description, IID_PPV_ARGS(output));
			}, true), "stream ticket should queue");
		Microsoft::WRL::ComPtr<ID3D12PipelineState> rebuiltStreamPipeline;
		RunAndPollTicket(streamTicket, rebuiltStreamPipeline);
	}

	void TestNativeCreateRunsOnWorkerAndUrgentPromotionExecutesOnce()
	{
		const std::thread::id callerThread = std::this_thread::get_id();
		std::thread::id callbackThread;
		int callbackCount = 0;
		const std::string ticketKey = "async-pipeline-promotion-test";
		const auto createCallback = [&](ID3D12PipelineState** outputPipelineState)
		{
			++callbackCount;
			callbackThread = std::this_thread::get_id();
			*outputPipelineState = nullptr;
			return E_INVALIDARG;
		};

		Check(HookD3D12::AsyncPipelineCreation::Request(ticketKey, createCallback, false), "normal request should be accepted");
		Check(HookD3D12::AsyncPipelineCreation::Poll(ticketKey).status == HookD3D12::AsyncPipelineCreation::TicketStatus::Pending,
			"queued work should report pending before the worker runs");
		Check(HookD3D12::AsyncPipelineCreation::Request(ticketKey, createCallback, true), "urgent duplicate request should be accepted");
		Check(callbackCount == 0, "native callback must not run on the requesting thread");
		WorkerJob urgentJob = PopWorkerJob();
		Check(urgentJob.urgent, "urgent duplicate should be promoted ahead of normal work");
		std::thread urgentWorker([callback = std::move(urgentJob.callback)]() { callback(); });
		urgentWorker.join();
		DrainWorkerQueue();

		Check(callbackCount == 1, "promoted duplicate jobs should invoke native creation only once");
		Check(callbackThread != callerThread, "native callback should run away from the requesting thread");
		Check(HookD3D12::AsyncPipelineCreation::Poll(ticketKey).status == HookD3D12::AsyncPipelineCreation::TicketStatus::Failed,
			"only completed native failure should produce a failed ticket");
	}

	void TestTicketCapacityRejectsWithoutRunningNativeWork()
	{
		int callbackCount = 0;
		for (size_t index = 0; index < 64; ++index)
		{
			const std::string ticketKey = "async-capacity-" + std::to_string(index);
			Check(HookD3D12::AsyncPipelineCreation::Request(
				ticketKey,
				[&callbackCount](ID3D12PipelineState** outputPipelineState)
				{
					++callbackCount;
					*outputPipelineState = nullptr;
					return E_INVALIDARG;
				},
				false), "ticket should fit within the service cap");
		}

		Check(!HookD3D12::AsyncPipelineCreation::Request(
			"async-capacity-overflow",
			[&callbackCount](ID3D12PipelineState** outputPipelineState)
			{
				++callbackCount;
				*outputPipelineState = nullptr;
				return E_INVALIDARG;
			},
			false), "service should reject work when all ticket slots are occupied");
		Check(callbackCount == 0, "capacity rejection must not execute native work inline");

		DrainWorkerQueue();
		for (size_t index = 0; index < 64; ++index)
			Check(HookD3D12::AsyncPipelineCreation::Poll("async-capacity-" + std::to_string(index)).status == HookD3D12::AsyncPipelineCreation::TicketStatus::Failed,
				"each accepted ticket should finish on a worker");
	}

	void TestCanceledTicketsFreeCapacityForActiveWork()
	{
		for (size_t index = 0; index < 64; ++index)
		{
			const std::string ticketKey = "async-canceled-capacity-" + std::to_string(index);
			Check(HookD3D12::AsyncPipelineCreation::Request(
				ticketKey,
				[](ID3D12PipelineState** outputPipelineState)
				{
					*outputPipelineState = nullptr;
					return E_ABORT;
				},
				false), "canceled ticket should initially fit within the service cap");
		}

		DrainWorkerQueue();
		Check(HookD3D12::AsyncPipelineCreation::Request(
			"async-after-canceled-capacity",
			[](ID3D12PipelineState** outputPipelineState)
			{
				*outputPipelineState = nullptr;
				return E_INVALIDARG;
			},
			true), "worker cleanup should promptly free capacity for a new active ticket");
		DrainWorkerQueue();
	}

	void TestReleaseDuringNativeCreateKeepsCallbackAlive()
	{
		std::mutex callbackMutex;
		std::condition_variable callbackCondition;
		bool callbackStarted = false;
		bool allowCallbackToFinish = false;
		const std::string ticketKey = "async-release-in-flight";

		Check(HookD3D12::AsyncPipelineCreation::Request(
			ticketKey,
			[&](ID3D12PipelineState** outputPipelineState)
			{
				std::unique_lock<std::mutex> lock(callbackMutex);
				callbackStarted = true;
				callbackCondition.notify_all();
				callbackCondition.wait(lock, [&]() { return allowCallbackToFinish; });
				*outputPipelineState = nullptr;
				return E_INVALIDARG;
			},
			true), "in-flight request should be accepted");

		WorkerJob job = PopWorkerJob();
		std::thread worker([callback = std::move(job.callback)]() { callback(); });
		{
			std::unique_lock<std::mutex> lock(callbackMutex);
			callbackCondition.wait(lock, [&]() { return callbackStarted; });
		}

		HookD3D12::AsyncPipelineCreation::ReleaseResources();
		{
			std::lock_guard<std::mutex> lock(callbackMutex);
			allowCallbackToFinish = true;
		}
		callbackCondition.notify_all();
		worker.join();

		Check(HookD3D12::AsyncPipelineCreation::Poll(ticketKey).status == HookD3D12::AsyncPipelineCreation::TicketStatus::Unavailable,
			"released in-flight ticket should no longer be pollable");
		DrainWorkerQueue();
	}
}

namespace DatabaseModifiedShaders
{
	bool QueueBackgroundCompile(std::function<void()> compileTask, bool urgent, std::shared_ptr<ShaderConfiguration::ConfigurationProgress>)
	{
		std::lock_guard<std::mutex> lock(gWorkerQueueMutex);
		WorkerJob job{std::move(compileTask), urgent};
		if (urgent)
			gWorkerQueue.push_front(std::move(job));
		else
			gWorkerQueue.push_back(std::move(job));
		return true;
	}
}

namespace HookD3D12
{
	void QueueShaderTargetApplyWork() {}
}

int main()
{
	try
	{
		TestNativeCreateRunsOnWorkerAndUrgentPromotionExecutesOnce();
		TestTicketCapacityRejectsWithoutRunningNativeWork();
		TestCanceledTicketsFreeCapacityForActiveWork();
		TestReleaseDuringNativeCreateKeepsCallbackAlive();
		TestOwnedSnapshotsCreateNativeWarpPipelines();
		std::cout << "Async pipeline creation tests passed.\n";
		return 0;
	}
	catch (const std::exception& exception)
	{
		std::cerr << exception.what() << '\n';
		return 1;
	}
}
