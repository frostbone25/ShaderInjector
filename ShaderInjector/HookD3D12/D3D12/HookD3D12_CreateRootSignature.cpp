#include "../HookD3D12.h"

#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Hash/Hash.h"
#include "GUI/ShaderInjectorGUI.h"
#include "IO/ShaderInjectorIO.h"
#include "RenderPass/RenderPassResourceRegistry.h"
#include "RenderPass/RenderPassRuntime.h"

namespace HookD3D12
{
	std::unordered_map<ID3D12RootSignature*, RootSignatureInfo> gRootSignatureInfoByPointer;
	//root signatures are device children; the same sidecar cannot supply one COM object to two devices.
	std::unordered_map<IUnknown*, std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12RootSignature>>> gPersistedRootSignaturesByDevice;
	std::unordered_set<ID3D12RootSignature*> gRenderPassRegisteredRootSignatures;
	std::mutex gRootSignatureMutex;

	bool GetRootSignatureBlob(ID3D12RootSignature* rootSignature, std::vector<uint8_t>& blob, uint64_t& hash)
	{
		blob.clear();
		hash = 0;

		if (!rootSignature)
			return false;

		std::lock_guard<std::mutex> lock(gRootSignatureMutex);
		auto rootSignatureIt = gRootSignatureInfoByPointer.find(rootSignature);

		if (rootSignatureIt == gRootSignatureInfoByPointer.end() || rootSignatureIt->second.rootSignatureBlob.empty())
			return false;

		blob = rootSignatureIt->second.rootSignatureBlob;
		hash = rootSignatureIt->second.rootSignatureHash;
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

		if (rootSignatureIt == gRootSignatureInfoByPointer.end() || rootSignatureIt->second.rootSignatureBlob.empty())
			return;

		RenderPassResourceRegistry::RegisterRootSignature(
			rootSignature,
			rootSignatureIt->second.rootSignatureBlob.data(),
			rootSignatureIt->second.rootSignatureBlob.size());

		gRenderPassRegisteredRootSignatures.insert(rootSignature);
	}

	ID3D12RootSignature* GetOrCreatePersistedRootSignature(const ShaderTarget::ShaderTargetDisk& shaderTarget, ID3D12Device* device)
	{
		if (shaderTarget.rootSignatureBlobPath.empty() || !device)
			return nullptr;

		Microsoft::WRL::ComPtr<IUnknown> deviceIdentity;

		if (FAILED(device->QueryInterface(IID_PPV_ARGS(&deviceIdentity))))
			return nullptr;

		{
			std::lock_guard<std::mutex> rootLock(gRootSignatureMutex);
			auto& deviceRoots = gPersistedRootSignaturesByDevice[deviceIdentity.Get()];
			const auto existingRoot = deviceRoots.find(shaderTarget.rootSignatureBlobPath);

			if (existingRoot != deviceRoots.end())
				return existingRoot->second.Get();
		}

		std::vector<uint8_t> blob;

		if (!ShaderInjectorIO::LoadDXILBlobFromDisk(shaderTarget.rootSignatureBlobPath, blob))
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12RootSignature->GetOrCreatePersistedRootSignature: missing blob for " + shaderTarget.name);
			return nullptr;
		}

		ID3D12RootSignature* rootSignature = nullptr;

		HRESULT result = E_FAIL;

		if (Original_CreateRootSignature)
			result = Original_CreateRootSignature(device, 0, blob.data(), blob.size(), IID_PPV_ARGS(&rootSignature));
		else
			result = device->CreateRootSignature(0, blob.data(), blob.size(), IID_PPV_ARGS(&rootSignature));

		if (FAILED(result) || !rootSignature)
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12RootSignature->GetOrCreatePersistedRootSignature: failed hr=" + std::to_string(static_cast<unsigned int>(result)) + " replacement=" + shaderTarget.name);
			return nullptr;
		}

		{
			std::lock_guard<std::mutex> rootLock(gRootSignatureMutex);
			auto& storedRoot = gPersistedRootSignaturesByDevice[deviceIdentity.Get()][shaderTarget.rootSignatureBlobPath];

			if (storedRoot)
			{
				rootSignature->Release();
				return storedRoot.Get();
			}

			storedRoot.Attach(rootSignature);
			RootSignatureInfo& info = gRootSignatureInfoByPointer[rootSignature];
			info.retainedRootSignature = rootSignature;
			info.rootSignatureBlob = blob;
			info.rootSignatureHash = Hash::HashMemory(blob.data(), blob.size());
		}

		EnsureRenderPassRootSignatureRegistered(rootSignature);

		return rootSignature;
	}

	void ReleaseRootSignatureCache()
	{
		std::lock_guard<std::mutex> lock(gRootSignatureMutex);
		gPersistedRootSignaturesByDevice.clear();
		gRootSignatureInfoByPointer.clear();
		gRenderPassRegisteredRootSignatures.clear();
	}

	HRESULT STDMETHODCALLTYPE Hook_CreateRootSignature(ID3D12Device* device, UINT nodeMask, const void* blob, SIZE_T blobSize, REFIID interfaceId, void** rootSignature)
	{
		return Handle_CreateRootSignature(device, nodeMask, blob, blobSize, interfaceId, rootSignature);
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
				info.retainedRootSignature = rootSignatureObject;
				const uint8_t* bytes = static_cast<const uint8_t*>(blob);
				info.rootSignatureBlob.assign(bytes, bytes + blobSize);
				info.rootSignatureHash = Hash::HashMemory(blob, blobSize);

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
} //namespace HookD3D12
