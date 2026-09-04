#include "HookD3D12HookHandlers.h"

#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Hash.h"
#include "GUI/ShaderInjectorGUI.h"
#include "IO/ShaderInjectorIO.h"
#include "RenderPass/RenderPassResourceRegistry.h"
#include "RenderPass/RenderPassRuntime.h"

namespace HookD3D12
{
	namespace
	{
		std::unordered_map<ID3D12RootSignature*, RootSignatureInfo> gRootSignatureInfoByPointer;
		std::unordered_map<std::string, ID3D12RootSignature*> gPersistedRootSignaturesByPath;
		std::unordered_set<ID3D12RootSignature*> gRenderPassRegisteredRootSignatures;
		std::mutex gRootSignatureMutex;
	}

	HRESULT STDMETHODCALLTYPE Hook_CreateRootSignature(ID3D12Device* device, UINT nodeMask, const void* blob, SIZE_T blobSize, REFIID interfaceId, void** rootSignature)
	{
		return Handle_CreateRootSignature(device, nodeMask, blob, blobSize, interfaceId, rootSignature);
	}

	bool GetRootSignatureBlob(ID3D12RootSignature* rootSignature, std::vector<uint8_t>& blob, uint64_t& hash)
	{
		blob.clear();
		hash = 0;

		if (!rootSignature)
			return false;

		std::lock_guard<std::mutex> lock(gRootSignatureMutex);
		auto rootSignatureIt = gRootSignatureInfoByPointer.find(rootSignature);
		if (rootSignatureIt == gRootSignatureInfoByPointer.end() || rootSignatureIt->second.blob.empty())
			return false;

		blob = rootSignatureIt->second.blob;
		hash = rootSignatureIt->second.hash;
		return hash != 0;
	}

	void EnsureRenderPassRootSignatureRegistered(ID3D12RootSignature* rootSignature)
	{
		if (!rootSignature || !RenderPassRuntime::HasEnabledRenderPasses())
			return;

		std::lock_guard<std::mutex> lock(gRootSignatureMutex);
		if (gRenderPassRegisteredRootSignatures.find(rootSignature) != gRenderPassRegisteredRootSignatures.end())
			return;

		const auto rootSignatureIt = gRootSignatureInfoByPointer.find(rootSignature);
		if (rootSignatureIt == gRootSignatureInfoByPointer.end() || rootSignatureIt->second.blob.empty())
			return;

		RenderPassResourceRegistry::RegisterRootSignature(
			rootSignature,
			rootSignatureIt->second.blob.data(),
			rootSignatureIt->second.blob.size());
		gRenderPassRegisteredRootSignatures.insert(rootSignature);
	}

	ID3D12RootSignature* GetOrCreatePersistedRootSignature(const ShaderTarget::ShaderTargetDisk& shaderTarget, ID3D12Device* device)
	{
		if (shaderTarget.rootSignatureBlobPath.empty() || !device)
			return nullptr;

		auto existingIt = gPersistedRootSignaturesByPath.find(shaderTarget.rootSignatureBlobPath);
		if (existingIt != gPersistedRootSignaturesByPath.end())
			return existingIt->second;

		std::vector<uint8_t> blob;
		if (!ShaderInjectorIO::LoadDXILBlobFromDisk(shaderTarget.rootSignatureBlobPath, blob))
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12RootSignature->GetOrCreatePersistedRootSignature: missing blob for " + shaderTarget.name);
			return nullptr;
		}

		ID3D12RootSignature* rootSignature = nullptr;
		HRESULT result = Original_CreateRootSignature
			? Original_CreateRootSignature(device, 0, blob.data(), blob.size(), IID_PPV_ARGS(&rootSignature))
			: device->CreateRootSignature(0, blob.data(), blob.size(), IID_PPV_ARGS(&rootSignature));

		if (FAILED(result) || !rootSignature)
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12RootSignature->GetOrCreatePersistedRootSignature: failed hr=" + std::to_string(static_cast<unsigned int>(result)) + " replacement=" + shaderTarget.name);
			return nullptr;
		}

		gPersistedRootSignaturesByPath[shaderTarget.rootSignatureBlobPath] = rootSignature;
		if (RenderPassRuntime::HasEnabledRenderPasses())
		{
			RenderPassResourceRegistry::RegisterRootSignature(rootSignature, blob.data(), blob.size());
			std::lock_guard<std::mutex> lock(gRootSignatureMutex);
			gRenderPassRegisteredRootSignatures.insert(rootSignature);
		}
		return rootSignature;
	}

	void ReleaseRootSignatureCache()
	{
		for (auto& persistedRootSignature : gPersistedRootSignaturesByPath)
		{
			if (persistedRootSignature.second)
				persistedRootSignature.second->Release();
		}

		gPersistedRootSignaturesByPath.clear();
		std::lock_guard<std::mutex> lock(gRootSignatureMutex);
		gRootSignatureInfoByPointer.clear();
		gRenderPassRegisteredRootSignatures.clear();
	}

	HRESULT STDMETHODCALLTYPE Handle_CreateRootSignature(ID3D12Device* device, UINT nodeMask, const void* blob, SIZE_T blobSize, REFIID interfaceId, void** rootSignature)
	{
		HRESULT result = Original_CreateRootSignature(device, nodeMask, blob, blobSize, interfaceId, rootSignature);

		if (SUCCEEDED(result) && blob && blobSize > 0 && rootSignature && *rootSignature)
		{
			ID3D12RootSignature* rootSignatureObject = nullptr;
			IUnknown* unknown = reinterpret_cast<IUnknown*>(*rootSignature);
			if (unknown && SUCCEEDED(unknown->QueryInterface(IID_PPV_ARGS(&rootSignatureObject))))
			{
				RootSignatureInfo info{};
				const uint8_t* bytes = static_cast<const uint8_t*>(blob);
				info.blob.assign(bytes, bytes + blobSize);
				info.hash = Hash::HashMemory(blob, blobSize);

				{
					std::lock_guard<std::mutex> lock(gRootSignatureMutex);
					gRootSignatureInfoByPointer[rootSignatureObject] = std::move(info);
				}

				if (RenderPassRuntime::HasEnabledRenderPasses())
				{
					RenderPassResourceRegistry::RegisterRootSignature(rootSignatureObject, blob, blobSize);
					std::lock_guard<std::mutex> lock(gRootSignatureMutex);
					gRenderPassRegisteredRootSignatures.insert(rootSignatureObject);
				}
				rootSignatureObject->Release();
			}
		}

		return result;
	}
}
