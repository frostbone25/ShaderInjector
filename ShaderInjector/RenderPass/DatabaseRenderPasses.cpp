#include "DatabaseRenderPasses.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

#include "ModifiedShader/DatabaseModifiedShaders.h"
#include "RenderPass/RenderPassRuntime.h"
#include "RenderPass/RenderPassShaders.h"
#include "IO/ShaderInjectorIO.h"

namespace DatabaseRenderPasses
{
	namespace
	{
		std::vector<RenderPass::RenderPassDisk> gRenderPasses;
		bool gRenderPassesLoaded = false;

		struct AvailableRenderPassIdentity
		{
			std::string id;
			std::string name;
		};

		AvailableRenderPassIdentity FindAvailableRenderPassIdentity()
		{
			const std::string renderPassDirectory = ShaderInjectorIO::GetRenderPassesDirectory();
			for (uint32_t suffix = 1; suffix < UINT32_MAX; ++suffix)
			{
				const std::string candidateId = suffix == 1
					? "RenderPass"
					: "RenderPass_" + std::to_string(suffix);
				const std::string candidateName = suffix == 1
					? "New Render Pass"
					: "New Render Pass " + std::to_string(suffix);
				const std::string candidateDirectory = ShaderInjectorIO::JoinPath(
					renderPassDirectory,
					ShaderInjectorIO::SanitizeFileStem(candidateName));
				const bool duplicateId = std::any_of(gRenderPasses.begin(), gRenderPasses.end(), [&](const auto& renderPass)
				{
					return renderPass.id == candidateId;
				});
				if (!duplicateId && !ShaderInjectorIO::PathExists(candidateDirectory))
					return { candidateId, candidateName };
			}

			return {};
		}

		bool RenderPassPackageUsesCurrentName(const RenderPass::RenderPassDisk& renderPass)
		{
			const std::string fileStem = ShaderInjectorIO::SanitizeFileStem(renderPass.name);
			if (fileStem.empty())
				return false;

			const std::string desiredPackageDirectory = ShaderInjectorIO::JoinPath(
				ShaderInjectorIO::GetRenderPassesDirectory(),
				fileStem);
			const std::string desiredJsonPath = ShaderInjectorIO::JoinPath(
				desiredPackageDirectory,
				fileStem + ShaderInjectorIO::extensionJSON);
			return ShaderInjectorIO::PathsEqual(renderPass.packageDirectory, desiredPackageDirectory) &&
				ShaderInjectorIO::PathsEqual(renderPass.jsonPath, desiredJsonPath);
		}

		bool MoveRenderPassPackageToCurrentName(RenderPass::RenderPassDisk& renderPass)
		{
			const std::string fileStem = ShaderInjectorIO::SanitizeFileStem(renderPass.name);
			if (fileStem.empty())
				return false;

			const std::string renderPassesDirectory = ShaderInjectorIO::GetRenderPassesDirectory();
			const std::string desiredPackageDirectory = ShaderInjectorIO::JoinPath(
				renderPassesDirectory,
				fileStem);
			const std::string desiredJsonPath = ShaderInjectorIO::JoinPath(
				desiredPackageDirectory,
				fileStem + ShaderInjectorIO::extensionJSON);

			std::string currentPackageDirectory = renderPass.packageDirectory.empty()
				? ShaderInjectorIO::DirectoryFromPath(renderPass.jsonPath)
				: renderPass.packageDirectory;
			std::string currentJsonPath = renderPass.jsonPath;
			const bool currentlyStoredAtRoot = ShaderInjectorIO::PathsEqual(
				currentPackageDirectory,
				renderPassesDirectory);

			if (!currentlyStoredAtRoot &&
				!ShaderInjectorIO::PathsEqual(currentPackageDirectory, desiredPackageDirectory))
			{
				if (ShaderInjectorIO::PathExists(desiredPackageDirectory) ||
					!ShaderInjectorIO::MovePath(currentPackageDirectory, desiredPackageDirectory))
				{
					return false;
				}

				currentJsonPath = ShaderInjectorIO::JoinPath(
					desiredPackageDirectory,
					ShaderInjectorIO::FileNameFromPath(currentJsonPath));
			}
			else if (currentlyStoredAtRoot)
			{
				if (ShaderInjectorIO::PathExists(desiredPackageDirectory))
					return false;
				ShaderInjectorIO::DirectoryCreate(desiredPackageDirectory);
				if (!ShaderInjectorIO::DirectoryExists(desiredPackageDirectory))
					return false;
			}

			if (!ShaderInjectorIO::PathsEqual(currentJsonPath, desiredJsonPath))
			{
				if (ShaderInjectorIO::PathExists(desiredJsonPath))
					return false;

				if (ShaderInjectorIO::FileExists(currentJsonPath) &&
					!ShaderInjectorIO::MovePath(currentJsonPath, desiredJsonPath))
				{
					return false;
				}
			}

			renderPass.packageDirectory = desiredPackageDirectory;
			renderPass.jsonPath = desiredJsonPath;
			RenderPass::ResolveShaderPaths(renderPass);
			return RenderPass::WriteJson(renderPass);
		}

		void PublishRuntimeConfiguration()
		{
			RenderPassRuntime::PublishRenderPassConfigurations(gRenderPasses);
		}
	}

	void RefreshRenderPasses()
	{
		gRenderPasses.clear();
		gRenderPassesLoaded = true;

		const std::string renderPassDirectory = ShaderInjectorIO::GetRenderPassesDirectory();
		ShaderInjectorIO::DirectoryCreate(renderPassDirectory);
		std::vector<std::string> jsonPaths;
		ShaderInjectorIO::CollectFilesByExtension(
			renderPassDirectory,
			ShaderInjectorIO::extensionJSON,
			jsonPaths,
			true,
			true);
		std::sort(jsonPaths.begin(), jsonPaths.end());

		for (const std::string& jsonPath : jsonPaths)
		{
			const std::string containingDirectory = ShaderInjectorIO::DirectoryFromPath(jsonPath);
			const bool storedAtRenderPassRoot = ShaderInjectorIO::PathsEqual(
				containingDirectory,
				renderPassDirectory);
			const std::string expectedPackageJsonName =
				ShaderInjectorIO::FileNameFromPath(containingDirectory) + ShaderInjectorIO::extensionJSON;
			if (!storedAtRenderPassRoot &&
				ShaderInjectorIO::FileNameFromPath(jsonPath) != expectedPackageJsonName)
			{
				// Other JSON resources may live beside the primary package document.
				continue;
			}

			RenderPass::RenderPassDisk renderPass{};
			if (!RenderPass::LoadJson(jsonPath, renderPass))
			{
				ShaderInjectorIO::WriteToLogFileWarning(
					"DatabaseRenderPasses->RefreshRenderPasses: ignoring invalid render pass " + jsonPath);
				continue;
			}

			const bool duplicateId = std::any_of(gRenderPasses.begin(), gRenderPasses.end(), [&](const auto& existing)
			{
				return existing.id == renderPass.id;
			});
			if (duplicateId)
			{
				ShaderInjectorIO::WriteToLogFileWarning(
					"DatabaseRenderPasses->RefreshRenderPasses: duplicate id " + renderPass.id);
				continue;
			}

			const bool legacyRootDocument = ShaderInjectorIO::PathsEqual(
				renderPass.packageDirectory,
				renderPassDirectory);
			if (legacyRootDocument && !RenderPassPackageUsesCurrentName(renderPass) &&
				!MoveRenderPassPackageToCurrentName(renderPass))
			{
				ShaderInjectorIO::WriteToLogFileWarning(
					"DatabaseRenderPasses->RefreshRenderPasses: could not normalize package path for " + renderPass.name);
			}

			gRenderPasses.push_back(std::move(renderPass));
		}

		std::sort(gRenderPasses.begin(), gRenderPasses.end(), [](const auto& left, const auto& right)
		{
			return left.name < right.name;
		});
		PublishRuntimeConfiguration();
		ShaderInjectorIO::WriteToLogFile(
			"DatabaseRenderPasses->RefreshRenderPasses: loaded render passes=" + std::to_string(gRenderPasses.size()));
	}

	void EnsureRenderPassesLoaded()
	{
		if (!gRenderPassesLoaded)
			RefreshRenderPasses();
	}

	const std::vector<RenderPass::RenderPassDisk>& GetRenderPasses()
	{
		EnsureRenderPassesLoaded();
		return gRenderPasses;
	}

	RenderPass::RenderPassDisk* FindRenderPassById(const std::string& renderPassId)
	{
		EnsureRenderPassesLoaded();
		const auto renderPassIt = std::find_if(gRenderPasses.begin(), gRenderPasses.end(), [&](const auto& renderPass)
		{
			return renderPass.id == renderPassId;
		});
		return renderPassIt != gRenderPasses.end() ? &*renderPassIt : nullptr;
	}

	const RenderPass::RenderPassDisk* FindRenderPassByIdReadOnly(const std::string& renderPassId)
	{
		return FindRenderPassById(renderPassId);
	}

	std::string ResolveModifiedShaderId(const RenderPass::RenderPassDisk& renderPass)
	{
		EnsureRenderPassesLoaded();
		const RenderPass::RenderPassDisk* currentRenderPass = &renderPass;
		std::unordered_set<std::string> visitedRenderPassIds;
		while (currentRenderPass)
		{
			if (!visitedRenderPassIds.insert(currentRenderPass->id).second)
				return {};

			if (currentRenderPass->event.type == RenderPass::EventType::ModifiedShader)
				return currentRenderPass->event.id;

			if (currentRenderPass->event.id.empty())
				return {};
			currentRenderPass = FindRenderPassByIdReadOnly(currentRenderPass->event.id);
		}

		return {};
	}

	std::string ResolveRootTiming(const RenderPass::RenderPassDisk& renderPass)
	{
		EnsureRenderPassesLoaded();
		const RenderPass::RenderPassDisk* currentRenderPass = &renderPass;
		std::unordered_set<std::string> visitedRenderPassIds;
		while (currentRenderPass)
		{
			if (!visitedRenderPassIds.insert(currentRenderPass->id).second)
				return {};
			if (currentRenderPass->event.type == RenderPass::EventType::ModifiedShader)
			{
				return currentRenderPass->type == RenderPass::RenderPassType::MipChain &&
					!RenderPass::FindMipChainRuntimeSource(*currentRenderPass)
					? RenderPass::timingBefore
					: currentRenderPass->timing;
			}
			if (currentRenderPass->event.id.empty())
				return {};
			currentRenderPass = FindRenderPassByIdReadOnly(currentRenderPass->event.id);
		}
		return {};
	}

	const ModifiedShader::PackageDisk* ResolveModifiedShader(const RenderPass::RenderPassDisk& renderPass)
	{
		const std::string modifiedShaderId = ResolveModifiedShaderId(renderPass);
		return modifiedShaderId.empty()
			? nullptr
			: DatabaseModifiedShaders::FindModifiedShaderById(modifiedShaderId);
	}

	bool IsEventChainActive(const RenderPass::RenderPassDisk& renderPass)
	{
		EnsureRenderPassesLoaded();
		const bool mipChainPass = renderPass.type == RenderPass::RenderPassType::MipChain &&
			!RenderPass::FindMipChainRuntimeSource(renderPass);
		const RenderPass::RenderPassDisk* currentRenderPass = &renderPass;
		std::unordered_set<std::string> visitedRenderPassIds;
		while (currentRenderPass)
		{
			if (!currentRenderPass->enabled ||
				!visitedRenderPassIds.insert(currentRenderPass->id).second ||
				currentRenderPass->event.id.empty())
			{
				return false;
			}

			if (currentRenderPass->event.type == RenderPass::EventType::ModifiedShader)
			{
				const bool rootExecutesAfter = (currentRenderPass->type != RenderPass::RenderPassType::MipChain ||
					RenderPass::FindMipChainRuntimeSource(*currentRenderPass)) &&
					currentRenderPass->timing == RenderPass::timingAfter;
				return (!mipChainPass || !rootExecutesAfter) &&
					DatabaseModifiedShaders::FindModifiedShaderById(currentRenderPass->event.id) != nullptr;
			}
			currentRenderPass = FindRenderPassByIdReadOnly(currentRenderPass->event.id);
		}

		return false;
	}

	bool CanReferenceRenderPass(const std::string& renderPassId, const std::string& eventRenderPassId)
	{
		EnsureRenderPassesLoaded();
		if (renderPassId.empty() || eventRenderPassId.empty() || renderPassId == eventRenderPassId)
			return false;

		const RenderPass::RenderPassDisk* currentRenderPass = FindRenderPassByIdReadOnly(eventRenderPassId);
		std::unordered_set<std::string> visitedRenderPassIds;
		while (currentRenderPass)
		{
			if (currentRenderPass->id == renderPassId ||
				!visitedRenderPassIds.insert(currentRenderPass->id).second)
			{
				return false;
			}

			if (currentRenderPass->event.type != RenderPass::EventType::RenderPass ||
				currentRenderPass->event.id.empty())
			{
				return true;
			}
			currentRenderPass = FindRenderPassByIdReadOnly(currentRenderPass->event.id);
		}

		return true;
	}

	bool CreateRenderPass(std::string& outRenderPassId)
	{
		EnsureRenderPassesLoaded();
		outRenderPassId.clear();
		const AvailableRenderPassIdentity identity = FindAvailableRenderPassIdentity();
		if (identity.id.empty() || identity.name.empty())
			return false;

		RenderPass::RenderPassDisk renderPass{};
		renderPass.id = identity.id;
		renderPass.name = identity.name;
		const std::string fileStem = ShaderInjectorIO::SanitizeFileStem(renderPass.name);
		renderPass.packageDirectory = ShaderInjectorIO::JoinPath(
			ShaderInjectorIO::GetRenderPassesDirectory(),
			fileStem);
		renderPass.jsonPath = ShaderInjectorIO::JoinPath(
			renderPass.packageDirectory,
			fileStem + ShaderInjectorIO::extensionJSON);
		ShaderInjectorIO::DirectoryCreate(renderPass.packageDirectory);
		if (!ShaderInjectorIO::DirectoryExists(renderPass.packageDirectory))
			return false;
		if (!RenderPass::WriteJson(renderPass))
		{
			ShaderInjectorIO::DeleteDirectoryRecursively(renderPass.packageDirectory);
			return false;
		}

		gRenderPasses.push_back(renderPass);
		outRenderPassId = identity.id;
		PublishRuntimeConfiguration();
		return true;
	}

	bool SetRenderPassEnabled(const std::string& renderPassId, bool enabled)
	{
		RenderPass::RenderPassDisk* renderPass = FindRenderPassById(renderPassId);
		if (!renderPass)
			return false;
		if (renderPass->enabled == enabled)
			return true;

		const bool previousEnabled = renderPass->enabled;
		renderPass->enabled = enabled;
		if (!RenderPass::WriteJson(*renderPass))
		{
			renderPass->enabled = previousEnabled;
			return false;
		}

		// Runtime plans are immutable snapshots. Publish immediately so disabling a
		// pass takes effect without requiring the separate Save action.
		PublishRuntimeConfiguration();
		return true;
	}

	bool SaveRenderPass(const std::string& renderPassId)
	{
		RenderPass::RenderPassDisk* renderPass = FindRenderPassById(renderPassId);
		if (!renderPass || renderPass->name.empty() || !RenderPass::IsTimingValid(renderPass->timing))
			return false;
		if (renderPass->event.type == RenderPass::EventType::RenderPass &&
			!renderPass->event.id.empty() &&
			!CanReferenceRenderPass(renderPass->id, renderPass->event.id))
		{
			return false;
		}
		if (renderPass->type == RenderPass::RenderPassType::MipChain &&
			!RenderPass::FindMipChainRuntimeSource(*renderPass) &&
			ResolveRootTiming(*renderPass) == RenderPass::timingAfter)
		{
			return false;
		}
		if (RenderPass::IsReplacementPass(renderPass->type))
			renderPass->timing = RenderPass::timingBefore;
		RenderPass::NormalizeExecutionResources(*renderPass);
		std::unordered_set<std::string> resourceBindings;
		for (const RenderPass::ShaderResourceReferenceDisk& resource : renderPass->shaderResources)
		{
			if (resource.resourceId.empty() || resource.hlslName.empty())
				return false;
			const std::string bindingKey = std::to_string(resource.shaderRegister) + ':' +
				std::to_string(resource.registerSpace);
			if (!resourceBindings.insert(bindingKey).second)
				return false;
		}
		std::unordered_set<std::string> samplerBindings;
		for (const RenderPass::SamplerStateDisk& sampler : renderPass->samplers)
		{
			if (sampler.hlslName.empty())
				return false;
			const std::string bindingKey = std::to_string(sampler.shaderRegister) + ':' +
				std::to_string(sampler.registerSpace);
			if (!samplerBindings.insert(bindingKey).second)
				return false;
		}

		if (!MoveRenderPassPackageToCurrentName(*renderPass))
			return false;

		PublishRuntimeConfiguration();
		return true;
	}

	bool DeleteRenderPass(const std::string& renderPassId)
	{
		EnsureRenderPassesLoaded();
		const auto renderPassIt = std::find_if(gRenderPasses.begin(), gRenderPasses.end(), [&](const auto& renderPass)
		{
			return renderPass.id == renderPassId;
		});
		if (renderPassIt == gRenderPasses.end())
			return false;

		const std::string renderPassesDirectory = ShaderInjectorIO::GetRenderPassesDirectory();
		const std::string packageDirectory = renderPassIt->packageDirectory.empty()
			? ShaderInjectorIO::DirectoryFromPath(renderPassIt->jsonPath)
			: renderPassIt->packageDirectory;
		if (!ShaderInjectorIO::PathsEqual(packageDirectory, renderPassesDirectory))
		{
			if (!ShaderInjectorIO::DeleteDirectoryRecursively(packageDirectory))
				return false;
		}
		else
		{
			ShaderInjectorIO::DeleteFileIfExists(renderPassIt->jsonPath);
			if (ShaderInjectorIO::FileExists(renderPassIt->jsonPath))
				return false;
		}

		RenderPassRuntime::ClearDiagnostics(renderPassId);
		gRenderPasses.erase(renderPassIt);
		PublishRuntimeConfiguration();
		return true;
	}

	bool CreateShaderTemplate(const std::string& renderPassId, std::string& outError)
	{
		RenderPass::RenderPassDisk* renderPass = FindRenderPassById(renderPassId);
		if (!renderPass)
		{
			outError = "Render Pass was not found.";
			return false;
		}

		const ModifiedShader::PackageDisk* modifiedShader = ResolveModifiedShader(*renderPass);
		if (!modifiedShader)
		{
			outError = "Select an event that resolves to an available Modified Shader first.";
			return false;
		}

		if (!RenderPassShaders::CreateShaderTemplate(*renderPass, *modifiedShader, outError) ||
			!RenderPass::WriteJson(*renderPass))
		{
			if (outError.empty())
				outError = "Could not save the Render Pass shader configuration.";
			return false;
		}

		PublishRuntimeConfiguration();
		return true;
	}

	bool CompileRenderPassShaders(const std::string& renderPassId, std::string& outError)
	{
		RenderPass::RenderPassDisk* renderPass = FindRenderPassById(renderPassId);
		if (!renderPass)
		{
			outError = "Render Pass was not found.";
			return false;
		}

		if (!RenderPassShaders::CompileShaders(*renderPass, outError))
			return false;

		PublishRuntimeConfiguration();
		return true;
	}
}
