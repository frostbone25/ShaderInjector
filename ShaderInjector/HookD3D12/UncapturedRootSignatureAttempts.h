#pragma once

#include <algorithm>
#include <d3d12.h>
#include <vector>
#include <wrl/client.h>

namespace HookD3D12
{
	struct UncapturedRootSignatureAttempts
	{
		std::vector<Microsoft::WRL::ComPtr<ID3D12RootSignature>> graphicsRoots;
		std::vector<Microsoft::WRL::ComPtr<ID3D12RootSignature>> computeRoots;

		bool Contains(ID3D12RootSignature* rootSignature, bool compute) const
		{
			const auto* roots = &graphicsRoots;

			if (compute)
				roots = &computeRoots;

			return std::any_of(roots->begin(), roots->end(), [rootSignature](const auto& root) { return root.Get() == rootSignature; });
		}

		void Remember(ID3D12RootSignature* rootSignature, bool compute)
		{
			if (!rootSignature || Contains(rootSignature, compute))
				return;

			auto* roots = &graphicsRoots;

			if (compute)
				roots = &computeRoots;

			//retain identities so a destroyed root's address cannot turn a new object into an old attempt.
			roots->emplace_back(rootSignature);
		}

		void Clear()
		{
			graphicsRoots.clear();
			computeRoots.clear();
		}
	};
}
