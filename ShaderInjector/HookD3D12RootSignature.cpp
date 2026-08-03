//HookD3D12RootSignature.cpp
#include "HookD3D12.h"

#include <string>
#include <mutex>
#include <unordered_map>
#include <vector>

//custom
#include "Hash.h"
#include "ShaderInjectorGUI.h"
#include "ShaderInjectorIO.h"
#include "RenderPassResourceRegistry.h"
#include "RenderPassRuntime.h"

namespace HookD3D12
{
	static std::unordered_map<ID3D12RootSignature*, RootSignatureInfo> gRootSignatureInfoByPointer;
	static std::unordered_map<std::string, ID3D12RootSignature*> gPersistedRootSignaturesByPath;
	static std::mutex gRootSignatureMutex;

	bool GetRootSignatureBlob(ID3D12RootSignature* rootSignature, std::vector<uint8_t>& outBlob, uint64_t& outHash)
	{
		outBlob.clear();
		outHash = 0;

		if (!rootSignature)
			return false;

		std::lock_guard<std::mutex> lock(gRootSignatureMutex);
		auto it = gRootSignatureInfoByPointer.find(rootSignature);
		if (it == gRootSignatureInfoByPointer.end() || it->second.blob.empty())
			return false;

		outBlob = it->second.blob;
		outHash = it->second.hash;
		return outHash != 0;
	}

	void EnsureRenderPassRootSignatureRegistered(ID3D12RootSignature* rootSignature)
	{
		if (!rootSignature || !RenderPassRuntime::HasEnabledRenderPasses())
			return;

		std::vector<uint8_t> blob;
		uint64_t hash = 0;
		if (GetRootSignatureBlob(rootSignature, blob, hash))
			RenderPassResourceRegistry::RegisterRootSignature(rootSignature, blob.data(), blob.size());
	}

	ID3D12RootSignature* GetOrCreatePersistedRootSignature(const ShaderTarget::ShaderTargetDisk& replacement, ID3D12Device* device)
	{
		if (replacement.rootSignatureBlobPath.empty() || !device)
			return nullptr;

		auto existingIt = gPersistedRootSignaturesByPath.find(replacement.rootSignatureBlobPath);
		if (existingIt != gPersistedRootSignaturesByPath.end())
			return existingIt->second;

		std::vector<uint8_t> blob;

		if (!ShaderInjectorIO::LoadDXILBlobFromDisk(replacement.rootSignatureBlobPath, blob))
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12RootSignature->GetOrCreatePersistedRootSignature: missing blob for " + replacement.name);
			return nullptr;
		}

		ID3D12RootSignature* rootSignature = nullptr;
		HRESULT hr = E_FAIL;
		if (Original_CreateRootSignature)
			hr = Original_CreateRootSignature(device, 0, blob.data(), blob.size(), IID_PPV_ARGS(&rootSignature));
		else
			hr = device->CreateRootSignature(0, blob.data(), blob.size(), IID_PPV_ARGS(&rootSignature));

		if (FAILED(hr) || !rootSignature)
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12RootSignature->GetOrCreatePersistedRootSignature: failed hr=" + std::to_string((unsigned)hr) + " replacement=" + replacement.name);
			return nullptr;
		}

		gPersistedRootSignaturesByPath[replacement.rootSignatureBlobPath] = rootSignature;
		if (RenderPassRuntime::HasEnabledRenderPasses())
		{
			RenderPassResourceRegistry::RegisterRootSignature(
				rootSignature,
				blob.data(),
				blob.size());
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
		{
			std::lock_guard<std::mutex> lock(gRootSignatureMutex);
			gRootSignatureInfoByPointer.clear();
		}
	}

	HRESULT STDMETHODCALLTYPE Hook_CreateRootSignature(ID3D12Device* device, UINT nodeMask, const void* blobWithRootSignature, SIZE_T blobLengthInBytes, REFIID riid, void** rootSignature)
	{
		HRESULT hr = Original_CreateRootSignature(device, nodeMask, blobWithRootSignature, blobLengthInBytes, riid, rootSignature);

		if (SUCCEEDED(hr) && blobWithRootSignature && blobLengthInBytes > 0 && rootSignature && *rootSignature)
		{
			ID3D12RootSignature* rootSignatureObject = nullptr;
			IUnknown* unknown = reinterpret_cast<IUnknown*>(*rootSignature);
			if (unknown && SUCCEEDED(unknown->QueryInterface(IID_PPV_ARGS(&rootSignatureObject))))
			{
				RootSignatureInfo info{};
				const uint8_t* bytes = static_cast<const uint8_t*>(blobWithRootSignature);
				info.blob.assign(bytes, bytes + blobLengthInBytes);
				info.hash = Hash::HashMemory(blobWithRootSignature, blobLengthInBytes);

				{
					std::lock_guard<std::mutex> lock(gRootSignatureMutex);
					gRootSignatureInfoByPointer[rootSignatureObject] = info;
				}
				if (RenderPassRuntime::HasEnabledRenderPasses())
				{
					RenderPassResourceRegistry::RegisterRootSignature(
						rootSignatureObject,
						blobWithRootSignature,
						blobLengthInBytes);
				}
				rootSignatureObject->Release();
			}
		}

		return hr;
	}
}
