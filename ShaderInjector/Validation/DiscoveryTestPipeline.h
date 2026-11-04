#pragma once

#include <atomic>
#include <d3d12.h>

//a tiny COM object lets queue tests check ownership without requiring a GPU or a driver cache.
class DiscoveryTestPipeline final : public ID3D12PipelineState
{
private:
	std::atomic<ULONG> referenceCount{1};

public:
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override { return E_NOINTERFACE; }

	ULONG STDMETHODCALLTYPE AddRef() override { return ++referenceCount; }

	ULONG STDMETHODCALLTYPE Release() override
	{
		const ULONG remainingReferences = --referenceCount;

		if (remainingReferences == 0)
			delete this;

		return remainingReferences;
	}

	HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, UINT*, void*) override { return E_NOTIMPL; }

	HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, UINT, const void*) override { return E_NOTIMPL; }

	HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID, const IUnknown*) override { return E_NOTIMPL; }

	HRESULT STDMETHODCALLTYPE SetName(LPCWSTR) override { return S_OK; }

	HRESULT STDMETHODCALLTYPE GetDevice(REFIID, void**) override { return E_NOTIMPL; }

	HRESULT STDMETHODCALLTYPE GetCachedBlob(ID3DBlob**) override { return E_NOTIMPL; }
};
