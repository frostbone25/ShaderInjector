#include "HookD3D12.h"

#include <string>
#include <unordered_map>
#include <vector>

#include "Hash.h"
#include "ShaderInjectorGUI.h"
#include "ShaderInjectorIO.h"

namespace HookD3D12
{
	static std::unordered_map<ID3D12RootSignature*, RootSignatureInfo> gRootSignatureInfoByPointer;
	static std::unordered_map<std::string, ID3D12RootSignature*> gPersistedRootSignaturesByPath;

	bool GetRootSignatureBlob(ID3D12RootSignature* rootSignature, std::vector<uint8_t>& outBlob, uint64_t& outHash)
	{
		outBlob.clear();
		outHash = 0;

		if (!rootSignature)
			return false;

		auto it = gRootSignatureInfoByPointer.find(rootSignature);
		if (it == gRootSignatureInfoByPointer.end() || it->second.blob.empty())
			return false;

		outBlob = it->second.blob;
		outHash = it->second.hash;
		return outHash != 0;
	}

	ID3D12RootSignature* GetOrCreatePersistedRootSignature(const ShaderReplacement::ShaderReplacementDisk& replacement, ID3D12Device* device)
	{
		if (replacement.rootSignatureBlobPath.empty() || !device)
			return nullptr;

		auto existingIt = gPersistedRootSignaturesByPath.find(replacement.rootSignatureBlobPath);
		if (existingIt != gPersistedRootSignaturesByPath.end())
			return existingIt->second;

		std::vector<uint8_t> blob;
		if (!ShaderInjectorIO::LoadDXILBlobFromDisk(replacement.rootSignatureBlobPath, blob))
		{
			ShaderInjectorGUI::WriteToRuntimeLog("GetOrCreatePersistedRootSignature: missing blob for " + replacement.name);
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
			ShaderInjectorGUI::WriteToRuntimeLog("GetOrCreatePersistedRootSignature: failed hr=" + std::to_string((unsigned)hr) + " replacement=" + replacement.name);
			return nullptr;
		}

		gPersistedRootSignaturesByPath[replacement.rootSignatureBlobPath] = rootSignature;
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
		gRootSignatureInfoByPointer.clear();
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

				gRootSignatureInfoByPointer[rootSignatureObject] = info;
				rootSignatureObject->Release();
			}
		}

		return hr;
	}
}
