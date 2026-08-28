//ShaderInjectorGUI.cpp
#include "ShaderInjectorGUI.h"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <string>
#include <mutex>
#include <unordered_set>
#include <vector>

//3RD Party
#include "imgui.h"

//custom
#include "HookD3D12.h"
#include "IO/ShaderInjectorIO.h"
#include "Hash.h"
#include "Globals.h"
#include "ModifiedShader/DatabaseModifiedShaders.h"
#include "RenderPass/DatabaseRenderPasses.h"
#include "RenderPass/RenderPassGraph.h"
#include "ShaderConfiguration/DatabaseShaderConfigurations.h"
#include "ModifiedShader/ModifiedShaderCreation.h"
#include "RenderDoc/RenderDocIntegration.h"
#include "RenderPass/RenderPassRuntime.h"
#include "ShaderResource/DatabaseShaderResources.h"
#include "ShaderResource/ShaderResourceCatalog.h"
#include "ShaderAutomaticDiscovery.h"
#include "StringHelper.h"
#include "GUI/ShaderInjectorGUITooltips.h"
#include "Keycodes.h"
#include "ShaderInjectorVersion.h"

namespace ShaderInjectorGUI
{
	static std::string gSelectedRenderPassId;
	static std::string gRenderPassNameBufferId;
	static char gRenderPassNameBuffer[256]{};
	static std::string gSelectedRenderPassResourceOwnerId;
	static size_t gSelectedRenderPassResourceIndex = 0;
	static std::string gSelectedRuntimeResourceOwnerId;
	static size_t gSelectedRuntimeResourceIndex = 0;
	static std::string gSelectedLogicalInputOwnerId;
	static size_t gSelectedLogicalInputIndex = 0;
	static std::string gSelectedLogicalOutputOwnerId;
	static size_t gSelectedLogicalOutputIndex = 0;

	struct MipSourceBindingOption
	{
		std::string name;
		uint32_t shaderRegister = 0;
		uint32_t registerSpace = 0;
	};

	struct GameTextureBindingOption
	{
		std::string name;
		RenderPass::GameResourceViewType viewType = RenderPass::GameResourceViewType::UnorderedAccess;
		uint32_t shaderRegister = 0;
		uint32_t registerSpace = 0;
	};

	const char* GameResourceViewTypeName(RenderPass::GameResourceViewType viewType)
	{
		return viewType == RenderPass::GameResourceViewType::UnorderedAccess
			? "Unordered Access (UAV)"
			: "Shader Resource (SRV)";
	}

	bool IsUnorderedAccessBinding(uint32_t inputType)
	{
		return inputType == D3D_SIT_UAV_RWTYPED ||
			inputType == D3D_SIT_UAV_RWSTRUCTURED ||
			inputType == D3D_SIT_UAV_RWBYTEADDRESS ||
			inputType == D3D_SIT_UAV_APPEND_STRUCTURED ||
			inputType == D3D_SIT_UAV_CONSUME_STRUCTURED ||
			inputType == D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER;
	}

	std::vector<GameTextureBindingOption> CollectGameTextureBindingOptions(
		const ModifiedShader::PackageDisk* modifiedShader)
	{
		std::vector<GameTextureBindingOption> options;
		if (!modifiedShader)
			return options;

		for (const ModifiedShader::TargetDisk& target : modifiedShader->targets)
		{
			for (const ShaderAnalysis::ResourceBindingDisk& resource : target.shaderAnalysis.resourceBindings)
			{
				const bool unorderedAccess = IsUnorderedAccessBinding(resource.type);
				if ((!unorderedAccess && resource.type != D3D_SIT_TEXTURE) ||
					resource.dimension == D3D_SRV_DIMENSION_UNKNOWN ||
					resource.dimension == D3D_SRV_DIMENSION_BUFFER || resource.bindCount == 0)
				{
					continue;
				}

				const uint32_t boundedCount = resource.bindCount == UINT_MAX
					? 1u
					: (std::min)(resource.bindCount, 64u);
				for (uint32_t bindingIndex = 0; bindingIndex < boundedCount; ++bindingIndex)
				{
					GameTextureBindingOption option{};
					option.name = resource.name.empty() ? "Texture" : resource.name;
					if (boundedCount > 1)
						option.name += "[" + std::to_string(bindingIndex) + "]";
					option.viewType = unorderedAccess
						? RenderPass::GameResourceViewType::UnorderedAccess
						: RenderPass::GameResourceViewType::ShaderResource;
					option.shaderRegister = resource.bindPoint + bindingIndex;
					option.registerSpace = resource.registerSpace;
					const bool duplicate = std::any_of(options.begin(), options.end(), [&](const auto& existing)
					{
						return existing.viewType == option.viewType &&
							existing.shaderRegister == option.shaderRegister &&
							existing.registerSpace == option.registerSpace;
					});
					if (!duplicate)
						options.push_back(std::move(option));
				}
			}
		}
		return options;
	}

	const char* ResourceDimensionName(uint32_t dimension)
	{
		switch (static_cast<D3D12_RESOURCE_DIMENSION>(dimension))
		{
			case D3D12_RESOURCE_DIMENSION_BUFFER: return "Buffer";
			case D3D12_RESOURCE_DIMENSION_TEXTURE1D: return "Texture1D";
			case D3D12_RESOURCE_DIMENSION_TEXTURE2D: return "Texture2D";
			case D3D12_RESOURCE_DIMENSION_TEXTURE3D: return "Texture3D";
			default: return "Unknown";
		}
	}

	std::vector<MipSourceBindingOption> CollectMipSourceBindingOptions(const ModifiedShader::PackageDisk* modifiedShader)
	{
		std::vector<MipSourceBindingOption> options;

		if (!modifiedShader)
			return options;

		for (const ModifiedShader::TargetDisk& target : modifiedShader->targets)
		{
			for (const ShaderAnalysis::ResourceBindingDisk& resource : target.shaderAnalysis.resourceBindings)
			{
				if (resource.type != D3D_SIT_TEXTURE || resource.dimension != D3D_SRV_DIMENSION_TEXTURE2D || resource.bindCount == 0)
				{
					continue;
				}

				const uint32_t boundedCount = resource.bindCount == UINT_MAX ? 1u : (std::min)(resource.bindCount, 64u);

				for (uint32_t bindingIndex = 0; bindingIndex < boundedCount; ++bindingIndex)
				{
					MipSourceBindingOption option{};
					option.name = resource.name.empty() ? "Texture2D" : resource.name;

					if (boundedCount > 1)
						option.name += "[" + std::to_string(bindingIndex) + "]";

					option.shaderRegister = resource.bindPoint + bindingIndex;
					option.registerSpace = resource.registerSpace;

					const bool duplicate = std::any_of(options.begin(), options.end(), [&](const auto& existing)
					{
						return existing.shaderRegister == option.shaderRegister && existing.registerSpace == option.registerSpace;
					});

					if (!duplicate)
						options.push_back(std::move(option));
				}
			}
		}

		return options;
	}

	void ApplyDefaultMipSourceBinding(RenderPass::RenderPassDisk& renderPass, const ModifiedShader::PackageDisk* modifiedShader)
	{
		const std::vector<MipSourceBindingOption> options = CollectMipSourceBindingOptions(modifiedShader);

		if (options.empty())
			return;

		renderPass.sourceTextureShaderRegister = options.front().shaderRegister;
		renderPass.sourceTextureRegisterSpace = options.front().registerSpace;
	}

	const char* ResolutionModeName(ShaderResource::ResolutionMode mode)
	{
		switch (mode)
		{
			case ShaderResource::ResolutionMode::DownscalePowerOfTwo: return "Power-of-two downscale";
			case ShaderResource::ResolutionMode::Explicit: return "Explicit";
			case ShaderResource::ResolutionMode::Inherit:
			default: return "Inherit target";
		}
	}

	const char* ResourceAccessName(RenderPass::ResourceAccess access)
	{
		switch (access)
		{
			case RenderPass::ResourceAccess::UnorderedAccess: return "Unordered Access";
			case RenderPass::ResourceAccess::RenderTarget: return "Render Target";
			case RenderPass::ResourceAccess::DepthStencil: return "Depth Stencil";
			case RenderPass::ResourceAccess::CopySource: return "Copy Source";
			case RenderPass::ResourceAccess::CopyDestination: return "Copy Destination";
			case RenderPass::ResourceAccess::ShaderResource:
			default: return "Shader Resource";
		}
	}

	const char* DispatchModeName(RenderPass::DispatchMode mode)
	{
		switch (mode)
		{
			case RenderPass::DispatchMode::ScaleByResolution: return "Scale by resolution";
			case RenderPass::DispatchMode::ExplicitThreadGroups: return "Explicit thread groups";
			case RenderPass::DispatchMode::InheritOriginal:
			default: return "Inherit original dispatch";
		}
	}

	std::vector<const RenderPass::RenderPassDisk*> BuildExecutionOrderedRenderPasses(
		const std::vector<RenderPass::RenderPassDisk>& renderPasses,
		RenderPassGraph::Compilation& outGraph)
	{
		outGraph = RenderPassGraph::Compile(renderPasses);
		const RenderPassGraph::Compilation& graph = outGraph;
		std::vector<const RenderPass::RenderPassDisk*> ordered;
		ordered.reserve(renderPasses.size());
		std::unordered_set<size_t> appendedIndices;
		std::unordered_set<std::string> appendedModifiedShaders;

		for (size_t renderPassIndex = 0; renderPassIndex < renderPasses.size(); ++renderPassIndex)
		{
			if (renderPassIndex >= graph.nodes.size() || !graph.nodes[renderPassIndex].valid)
				continue;
			const std::string& modifiedShaderId = graph.nodes[renderPassIndex].modifiedShaderId;
			if (!appendedModifiedShaders.insert(modifiedShaderId).second)
				continue;
			const auto planIt = graph.executionPlans.find(modifiedShaderId);
			if (planIt == graph.executionPlans.end())
				continue;
			for (size_t boundaryIndex = 0; boundaryIndex < 2; ++boundaryIndex)
			{
				for (size_t orderedIndex : planIt->second.executionOrders[boundaryIndex])
				{
					if (orderedIndex < renderPasses.size() && appendedIndices.insert(orderedIndex).second)
						ordered.push_back(&renderPasses[orderedIndex]);
				}
			}
		}

		// Keep disabled or temporarily invalid passes visible after the executable graph.
		for (size_t renderPassIndex = 0; renderPassIndex < renderPasses.size(); ++renderPassIndex)
		{
			if (appendedIndices.insert(renderPassIndex).second)
				ordered.push_back(&renderPasses[renderPassIndex]);
		}
		return ordered;
	}

	const RenderPassGraph::CompiledNode* FindCompiledRenderPassNode(
		const RenderPassGraph::Compilation& graph,
		const std::string& renderPassId)
	{
		const auto indexIt = graph.renderPassIndices.find(renderPassId);
		if (indexIt == graph.renderPassIndices.end() || indexIt->second >= graph.nodes.size())
			return nullptr;
		return &graph.nodes[indexIt->second];
	}

	void SetRenderPassExecutionMode(
		RenderPass::RenderPassDisk& renderPass,
		RenderPass::ExecutionMode executionMode)
	{
		if (renderPass.executionMode == executionMode)
		{
			RenderPass::NormalizeExecutionResources(renderPass);
			return;
		}

		renderPass.executionMode = executionMode;
		renderPass.vertexShaderSourceFile.clear();
		renderPass.fragmentShaderSourceFile.clear();
		renderPass.vertexShaderCompiledBlobFile.clear();
		renderPass.fragmentShaderCompiledBlobFile.clear();
		renderPass.vertexShaderBlob.clear();
		renderPass.fragmentShaderBlob.clear();
		renderPass.vertexShaderBlobHash = 0;
		renderPass.fragmentShaderBlobHash = 0;
		RenderPass::NormalizeExecutionResources(renderPass);
		RenderPass::ResolveShaderPaths(renderPass);
	}

	void ApplyAutomaticExecutionMode(
		RenderPass::RenderPassDisk& renderPass,
		const ModifiedShader::PackageDisk* modifiedShader)
	{
		if (!modifiedShader || RenderPass::IsReplacementPass(renderPass.type))
		{
			return;
		}
		const RenderPass::PassOperation operation = RenderPass::ResolvePassOperation(renderPass);
		if (operation != RenderPass::PassOperation::Custom &&
			operation != RenderPass::PassOperation::Downsample &&
			operation != RenderPass::PassOperation::UpsampleChain &&
			operation != RenderPass::PassOperation::MipChain)
		{
			return;
		}
		const RenderPass::ExecutionMode executionMode =
			modifiedShader->shaderType == ShaderTarget::ComputeShader
				? RenderPass::ExecutionMode::Compute
				: RenderPass::ExecutionMode::FullscreenPixel;
		SetRenderPassExecutionMode(renderPass, executionMode);
	}

	void UI_ResolutionPolicy(const char* id, ShaderResource::ResolutionPolicyDisk& resolution)
	{
		ImGui::PushID(id);
		ImGui::TextUnformatted("Resolution");
		ImGui::SameLine();
		if (ImGui::BeginCombo("##Mode", ResolutionModeName(resolution.mode)))
		{
			const ShaderResource::ResolutionMode modes[] = {
				ShaderResource::ResolutionMode::Inherit,
				ShaderResource::ResolutionMode::DownscalePowerOfTwo,
				ShaderResource::ResolutionMode::Explicit,
			};
			for (ShaderResource::ResolutionMode mode : modes)
			{
				if (ImGui::Selectable(ResolutionModeName(mode), resolution.mode == mode))
					resolution.mode = mode;
			}
			ImGui::EndCombo();
		}

		if (resolution.mode == ShaderResource::ResolutionMode::DownscalePowerOfTwo)
		{
			ImGui::TextUnformatted("Downscale");
			ImGui::SameLine();
			const uint32_t factors[] = { 1, 2, 4, 8, 16 };
			const std::string preview = std::to_string(resolution.downscaleFactor) + "x";
			if (ImGui::BeginCombo("##Factor", preview.c_str()))
			{
				for (uint32_t factor : factors)
				{
					const std::string label = std::to_string(factor) + "x";
					if (ImGui::Selectable(label.c_str(), resolution.downscaleFactor == factor))
						resolution.downscaleFactor = factor;
				}
				ImGui::EndCombo();
			}
		}
		else if (resolution.mode == ShaderResource::ResolutionMode::Explicit)
		{
			int width = static_cast<int>(resolution.width);
			int height = static_cast<int>(resolution.height);
			ImGui::TextUnformatted("Width");
			ImGui::SameLine();
			if (ImGui::DragInt("##Width", &width, 1.0f, 1, 16384))
				resolution.width = static_cast<uint32_t>((std::max)(1, width));
			ImGui::TextUnformatted("Height");
			ImGui::SameLine();
			if (ImGui::DragInt("##Height", &height, 1.0f, 1, 16384))
				resolution.height = static_cast<uint32_t>((std::max)(1, height));
		}
		ImGui::PopID();
	}

	std::string NextRuntimeResourceId(const RenderPass::RenderPassDisk& renderPass)
	{
		for (uint32_t suffix = 1; suffix < UINT32_MAX; ++suffix)
		{
			const std::string candidate = renderPass.id + ":Texture" + std::to_string(suffix);
			if (std::none_of(renderPass.runtimeResources.begin(), renderPass.runtimeResources.end(), [&](const auto& resource)
			{
				return resource.id == candidate;
			}))
			{
				return candidate;
			}
		}
		return {};
	}

	bool IsExposedRuntimeResource(const std::string& resourceId)
	{
		if (resourceId.empty())
			return false;
		for (const RenderPass::RenderPassDisk& pass : DatabaseRenderPasses::GetRenderPasses())
		{
			if (RenderPass::FindMipChainRuntimeSource(pass) &&
				RenderPass::MipChainOutputResourceId(pass) == resourceId)
				return true;
			if (std::any_of(pass.outputs.begin(), pass.outputs.end(), [&](const auto& output)
			{
				return output.origin == ShaderResource::ResourceOrigin::Runtime &&
					output.resourceId == resourceId;
			}))
			{
				return true;
			}
		}
		return false;
	}

	void UI_RuntimeResources(RenderPass::RenderPassDisk& renderPass)
	{
		ImGui::SeparatorText("Runtime Textures");
		if (gSelectedRuntimeResourceOwnerId != renderPass.id)
		{
			gSelectedRuntimeResourceOwnerId = renderPass.id;
			gSelectedRuntimeResourceIndex = 0;
		}
		if (ImGui::Button("Add##RuntimeTexture"))
		{
			const bool computePass =
				RenderPass::ResolveExecutionMode(renderPass) == RenderPass::ExecutionMode::Compute;
			const bool copyPass =
				RenderPass::ResolvePassOperation(renderPass) == RenderPass::PassOperation::Copy;
			RenderPass::RuntimeResourceDefinitionDisk resource{};
			resource.id = NextRuntimeResourceId(renderPass);
			resource.name = "Runtime Texture " + std::to_string(renderPass.runtimeResources.size() + 1);
			resource.texture.dimension = ShaderResource::TextureDimension::Texture2D;
			resource.texture.format = computePass && !copyPass
				? static_cast<uint32_t>(DXGI_FORMAT_R16G16B16A16_FLOAT)
				: static_cast<uint32_t>(DXGI_FORMAT_UNKNOWN);
			resource.texture.resolution = renderPass.resolution;
			resource.texture.matchReferenceTexture = copyPass;
			resource.texture.allowRenderTarget = !computePass && !copyPass;
			resource.texture.allowUnorderedAccess = computePass && !copyPass;
			renderPass.runtimeResources.push_back(resource);
			gSelectedRuntimeResourceIndex = renderPass.runtimeResources.size() - 1;
			if (renderPass.outputs.empty())
			{
				RenderPass::LogicalResourceBindingDisk output{};
				output.resourceId = resource.id;
				output.hlslName = "OutputColor";
				output.origin = ShaderResource::ResourceOrigin::Runtime;
				output.access = copyPass
					? RenderPass::ResourceAccess::CopyDestination
					: computePass
					? RenderPass::ResourceAccess::UnorderedAccess
					: RenderPass::ResourceAccess::RenderTarget;
				renderPass.outputs.push_back(std::move(output));
			}
		}
		ImGui::SameLine();
		const bool canRemove = gSelectedRuntimeResourceIndex < renderPass.runtimeResources.size();
		ImGui::BeginDisabled(!canRemove);
		if (ImGui::Button("Remove##RuntimeTexture"))
		{
			const std::string removedId = renderPass.runtimeResources[gSelectedRuntimeResourceIndex].id;
			renderPass.runtimeResources.erase(renderPass.runtimeResources.begin() + gSelectedRuntimeResourceIndex);
			const auto removeReferences = [&](auto& bindings)
			{
				bindings.erase(std::remove_if(bindings.begin(), bindings.end(), [&](const auto& binding)
				{
					return binding.origin == ShaderResource::ResourceOrigin::Runtime && binding.resourceId == removedId;
				}), bindings.end());
			};
			removeReferences(renderPass.inputs);
			removeReferences(renderPass.outputs);
			if (gSelectedRuntimeResourceIndex && gSelectedRuntimeResourceIndex >= renderPass.runtimeResources.size())
				--gSelectedRuntimeResourceIndex;
		}
		ImGui::EndDisabled();

		if (ImGui::BeginChild("RuntimeTextureList", ImVec2(0, 120), ImGuiChildFlags_Borders))
		{
			for (size_t resourceIndex = 0; resourceIndex < renderPass.runtimeResources.size(); ++resourceIndex)
			{
				const auto& resource = renderPass.runtimeResources[resourceIndex];
				const std::string label = (resource.name.empty() ? resource.id : resource.name) +
					"##RuntimeTexture_" + std::to_string(resourceIndex);
				if (ImGui::Selectable(label.c_str(), resourceIndex == gSelectedRuntimeResourceIndex))
					gSelectedRuntimeResourceIndex = resourceIndex;
			}
		}
		ImGui::EndChild();

		if (gSelectedRuntimeResourceIndex >= renderPass.runtimeResources.size())
			return;
		auto& resource = renderPass.runtimeResources[gSelectedRuntimeResourceIndex];
		char name[128]{};
		strncpy_s(name, resource.name.c_str(), _TRUNCATE);
		ImGui::PushID("RuntimeTextureProperties");
		ImGui::TextUnformatted("Name");
		ImGui::SameLine();
		if (ImGui::InputText("##Name", name, sizeof(name)))
			resource.name = name;
		ImGui::Text("ID: %s", resource.id.c_str());

		ImGui::TextUnformatted("Dimension");
		ImGui::SameLine();
		if (ImGui::BeginCombo("##Dimension", ShaderResource::TextureDimensionName(resource.texture.dimension)))
		{
			const ShaderResource::TextureDimension dimensions[] = {
				ShaderResource::TextureDimension::Texture2D,
				ShaderResource::TextureDimension::Texture2DArray,
				ShaderResource::TextureDimension::Texture3D,
			};
			for (ShaderResource::TextureDimension dimension : dimensions)
			{
				if (ImGui::Selectable(ShaderResource::TextureDimensionName(dimension), resource.texture.dimension == dimension))
					resource.texture.dimension = dimension;
			}
			ImGui::EndCombo();
		}
		int format = static_cast<int>(resource.texture.format);
		ImGui::TextUnformatted("DXGI Format (0 = target)");
		ImGui::SameLine();
		if (ImGui::DragInt("##Format", &format, 1.0f, 0, 255))
			resource.texture.format = static_cast<uint32_t>((std::max)(0, format));
		UI_ResolutionPolicy("TextureResolution", resource.texture.resolution);

		int mipLevels = static_cast<int>(resource.texture.mipLevels);
		ImGui::TextUnformatted("Mip Levels (0 = full chain)");
		ImGui::SameLine();
		if (ImGui::DragInt("##MipLevels", &mipLevels, 1.0f, 0, 16))
			resource.texture.mipLevels = static_cast<uint32_t>((std::max)(0, mipLevels));
		if (resource.texture.dimension == ShaderResource::TextureDimension::Texture2DArray)
		{
			int arraySize = static_cast<int>(resource.texture.arraySize);
			ImGui::TextUnformatted("Array Size");
			ImGui::SameLine();
			if (ImGui::DragInt("##ArraySize", &arraySize, 1.0f, 1, 2048))
				resource.texture.arraySize = static_cast<uint32_t>((std::max)(1, arraySize));
		}
		else if (resource.texture.dimension == ShaderResource::TextureDimension::Texture3D)
		{
			int depth = static_cast<int>(resource.texture.depth);
			ImGui::TextUnformatted("Depth");
			ImGui::SameLine();
			if (ImGui::DragInt("##Depth", &depth, 1.0f, 1, 2048))
				resource.texture.depth = static_cast<uint32_t>((std::max)(1, depth));
		}

		ImGui::TextUnformatted("Lifetime");
		ImGui::SameLine();
		if (ImGui::BeginCombo("##Lifetime", ShaderResource::ResourceLifetimeName(resource.texture.lifetime)))
		{
			const ShaderResource::ResourceLifetime lifetimes[] = {
				ShaderResource::ResourceLifetime::Transient,
				ShaderResource::ResourceLifetime::Persistent,
				ShaderResource::ResourceLifetime::History,
			};
			for (ShaderResource::ResourceLifetime lifetime : lifetimes)
			{
				if (ImGui::Selectable(ShaderResource::ResourceLifetimeName(lifetime), resource.texture.lifetime == lifetime))
					resource.texture.lifetime = lifetime;
			}
			ImGui::EndCombo();
		}
		const bool requiresRenderTarget = std::any_of(
			renderPass.outputs.begin(),
			renderPass.outputs.end(),
			[&](const RenderPass::LogicalResourceBindingDisk& output)
			{
				return output.origin == ShaderResource::ResourceOrigin::Runtime &&
					output.resourceId == resource.id &&
					output.access == RenderPass::ResourceAccess::RenderTarget;
			});
		const bool requiresUnorderedAccess = std::any_of(
			renderPass.outputs.begin(),
			renderPass.outputs.end(),
			[&](const RenderPass::LogicalResourceBindingDisk& output)
			{
				return output.origin == ShaderResource::ResourceOrigin::Runtime &&
					output.resourceId == resource.id &&
					output.access == RenderPass::ResourceAccess::UnorderedAccess;
			});
		const bool requiresCopySourceMatch = std::any_of(
			renderPass.outputs.begin(),
			renderPass.outputs.end(),
			[&](const RenderPass::LogicalResourceBindingDisk& output)
			{
				return output.origin == ShaderResource::ResourceOrigin::Runtime &&
					output.resourceId == resource.id &&
					output.access == RenderPass::ResourceAccess::CopyDestination;
			});
		resource.texture.allowRenderTarget = resource.texture.allowRenderTarget || requiresRenderTarget;
		resource.texture.allowUnorderedAccess = resource.texture.allowUnorderedAccess || requiresUnorderedAccess;
		resource.texture.matchReferenceTexture = resource.texture.matchReferenceTexture || requiresCopySourceMatch;
		if (requiresCopySourceMatch)
		{
			resource.texture.allowRenderTarget = false;
			resource.texture.allowUnorderedAccess = false;
		}
		ImGui::BeginDisabled(requiresCopySourceMatch);
		ImGui::Checkbox("Match Copy Source", &resource.texture.matchReferenceTexture);
		ImGui::EndDisabled();
		ImGui::BeginDisabled(requiresRenderTarget);
		ImGui::Checkbox("Render Target", &resource.texture.allowRenderTarget);
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(requiresUnorderedAccess);
		ImGui::Checkbox("Unordered Access", &resource.texture.allowUnorderedAccess);
		ImGui::EndDisabled();
		if (requiresRenderTarget || requiresUnorderedAccess || requiresCopySourceMatch)
			ImGui::TextUnformatted("Output bindings lock the required resource access flags.");
		ImGui::PopID();
	}

	void UI_LogicalBindings(
		RenderPass::RenderPassDisk& renderPass,
		bool outputs,
		std::vector<RenderPass::LogicalResourceBindingDisk>& bindings,
		std::string& selectedOwnerId,
		size_t& selectedIndex)
	{
		const char* sectionName = outputs ? "Outputs" : "Inputs";
		ImGui::SeparatorText(sectionName);
		if (selectedOwnerId != renderPass.id)
		{
			selectedOwnerId = renderPass.id;
			selectedIndex = 0;
		}
		if (ImGui::Button(outputs ? "Add##LogicalOutput" : "Add##LogicalInput"))
		{
			RenderPass::LogicalResourceBindingDisk binding{};
			const RenderPass::PassOperation passOperation = RenderPass::ResolvePassOperation(renderPass);
			binding.origin = !outputs && passOperation == RenderPass::PassOperation::Copy
				? ShaderResource::ResourceOrigin::Game
				: ShaderResource::ResourceOrigin::Runtime;
			binding.access = outputs
				? (passOperation == RenderPass::PassOperation::Copy
					? RenderPass::ResourceAccess::CopyDestination
					: (RenderPass::ResolveExecutionMode(renderPass) == RenderPass::ExecutionMode::Compute
					? RenderPass::ResourceAccess::UnorderedAccess
					: RenderPass::ResourceAccess::RenderTarget))
				: (passOperation == RenderPass::PassOperation::Copy
					? RenderPass::ResourceAccess::CopySource
					: RenderPass::ResourceAccess::ShaderResource);
			binding.hlslName = outputs
				? "OutputColor"
				: "RuntimeInput" + std::to_string(bindings.size());
			if (outputs && !renderPass.runtimeResources.empty())
				binding.resourceId = renderPass.runtimeResources.front().id;
			else if (!outputs && binding.origin == ShaderResource::ResourceOrigin::Runtime)
			{
				const std::vector<ShaderResource::CatalogEntry> catalog = ShaderResourceCatalog::GetSnapshot();
				const auto resourceIt = std::find_if(catalog.begin(), catalog.end(), [&](const auto& resource)
				{
					return resource.origin == ShaderResource::ResourceOrigin::Runtime &&
						resource.ownerRenderPassId != renderPass.id &&
						IsExposedRuntimeResource(resource.id);
				});
				if (resourceIt != catalog.end())
					binding.resourceId = resourceIt->id;
			}
			uint32_t nextRegister = 0;
			for (const auto& existing : bindings)
				nextRegister = (std::max)(nextRegister, existing.shaderRegister + 1);
			binding.shaderRegister = nextRegister;
			bindings.push_back(std::move(binding));
			selectedIndex = bindings.size() - 1;
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(selectedIndex >= bindings.size());
		if (ImGui::Button(outputs ? "Remove##LogicalOutput" : "Remove##LogicalInput"))
		{
			bindings.erase(bindings.begin() + selectedIndex);
			if (selectedIndex && selectedIndex >= bindings.size())
				--selectedIndex;
		}
		ImGui::EndDisabled();

		if (ImGui::BeginChild(outputs ? "LogicalOutputList" : "LogicalInputList", ImVec2(0, 105), ImGuiChildFlags_Borders))
		{
			for (size_t bindingIndex = 0; bindingIndex < bindings.size(); ++bindingIndex)
			{
				const auto& binding = bindings[bindingIndex];
				const char registerPrefix = binding.gameResourceViewType == RenderPass::GameResourceViewType::UnorderedAccess
					? 'u'
					: 't';
				const std::string target = binding.origin == ShaderResource::ResourceOrigin::Game
					? std::string(1, registerPrefix) + std::to_string(binding.shaderRegister) +
						", space" + std::to_string(binding.registerSpace)
					: (binding.resourceId.empty() ? "(none)" : binding.resourceId);
				const std::string label = std::string(ShaderResource::ResourceOriginName(binding.origin)) + " | " +
					ResourceAccessName(binding.access) + ": " + target +
					"##LogicalBinding_" + std::to_string(bindingIndex);
				if (ImGui::Selectable(label.c_str(), bindingIndex == selectedIndex))
					selectedIndex = bindingIndex;
			}
		}
		ImGui::EndChild();
		if (selectedIndex >= bindings.size())
			return;

		auto& binding = bindings[selectedIndex];
		ImGui::PushID(outputs ? "LogicalOutputProperties" : "LogicalInputProperties");
		const std::vector<ShaderResource::CatalogEntry> catalog = ShaderResourceCatalog::GetSnapshot();
		if (!outputs && RenderPass::ResolvePassOperation(renderPass) == RenderPass::PassOperation::Copy)
		{
			ImGui::TextUnformatted("Origin");
			ImGui::SameLine();
			if (ImGui::BeginCombo("##Origin", ShaderResource::ResourceOriginName(binding.origin)))
			{
				const ShaderResource::ResourceOrigin origins[] = {
					ShaderResource::ResourceOrigin::Game,
					ShaderResource::ResourceOrigin::Runtime,
				};
				for (ShaderResource::ResourceOrigin origin : origins)
				{
					if (ImGui::Selectable(ShaderResource::ResourceOriginName(origin), binding.origin == origin))
					{
						binding.origin = origin;
						binding.access = RenderPass::ResourceAccess::CopySource;
						if (origin == ShaderResource::ResourceOrigin::Game)
							binding.resourceId.clear();
					}
				}
				ImGui::EndCombo();
			}
		}

		if (binding.origin == ShaderResource::ResourceOrigin::Runtime)
		{
			ImGui::TextUnformatted("Runtime Texture");
			ImGui::SameLine();
			const std::string preview = binding.resourceId.empty() ? "(none)" : binding.resourceId;
			if (ImGui::BeginCombo("##Resource", preview.c_str()))
			{
				std::unordered_set<std::string> shownIds;
				for (const auto& resource : renderPass.runtimeResources)
				{
					shownIds.insert(resource.id);
					if (ImGui::Selectable(resource.id.c_str(), binding.resourceId == resource.id))
						binding.resourceId = resource.id;
				}
				if (!outputs)
				{
					for (const auto& resource : catalog)
					{
						if (resource.origin != ShaderResource::ResourceOrigin::Runtime ||
							!IsExposedRuntimeResource(resource.id) ||
							!shownIds.insert(resource.id).second)
						{
							continue;
						}
						const std::string label = resource.name + "##" + resource.id;
						if (ImGui::Selectable(label.c_str(), binding.resourceId == resource.id))
							binding.resourceId = resource.id;
					}
				}
				ImGui::EndCombo();
			}
		}
		else if (binding.origin == ShaderResource::ResourceOrigin::Game)
		{
			const std::vector<GameTextureBindingOption> gameBindings =
				CollectGameTextureBindingOptions(DatabaseRenderPasses::ResolveModifiedShader(renderPass));
			const char registerPrefix = binding.gameResourceViewType == RenderPass::GameResourceViewType::UnorderedAccess
				? 'u'
				: 't';
			const std::string preview = std::string(1, registerPrefix) +
				std::to_string(binding.shaderRegister) + ", space" + std::to_string(binding.registerSpace);
			ImGui::TextUnformatted("Game Texture Binding");
			ImGui::SameLine();
			if (ImGui::BeginCombo("##GameTextureBinding", preview.c_str()))
			{
				for (size_t optionIndex = 0; optionIndex < gameBindings.size(); ++optionIndex)
				{
					const GameTextureBindingOption& option = gameBindings[optionIndex];
					const char optionPrefix = option.viewType == RenderPass::GameResourceViewType::UnorderedAccess
						? 'u'
						: 't';
					const std::string label = option.name + " (" + optionPrefix +
						std::to_string(option.shaderRegister) + ", space" +
						std::to_string(option.registerSpace) + ")##GameBinding_" +
						std::to_string(optionIndex);
					const bool selected = binding.gameResourceViewType == option.viewType &&
						binding.shaderRegister == option.shaderRegister &&
						binding.registerSpace == option.registerSpace;
					if (ImGui::Selectable(label.c_str(), selected))
					{
						binding.hlslName = option.name;
						binding.gameResourceViewType = option.viewType;
						binding.shaderRegister = option.shaderRegister;
						binding.registerSpace = option.registerSpace;
					}
				}
				ImGui::EndCombo();
			}

			ImGui::TextUnformatted("Game View Type");
			ImGui::SameLine();
			if (ImGui::BeginCombo("##GameViewType", GameResourceViewTypeName(binding.gameResourceViewType)))
			{
				const RenderPass::GameResourceViewType viewTypes[] = {
					RenderPass::GameResourceViewType::UnorderedAccess,
					RenderPass::GameResourceViewType::ShaderResource,
				};
				for (RenderPass::GameResourceViewType viewType : viewTypes)
				{
					if (ImGui::Selectable(GameResourceViewTypeName(viewType), binding.gameResourceViewType == viewType))
						binding.gameResourceViewType = viewType;
				}
				ImGui::EndCombo();
			}
			int shaderRegister = static_cast<int>(binding.shaderRegister);
			int registerSpace = static_cast<int>(binding.registerSpace);
			ImGui::TextUnformatted(binding.gameResourceViewType == RenderPass::GameResourceViewType::UnorderedAccess
				? "UAV Register"
				: "Texture Register");
			ImGui::SameLine();
			if (ImGui::DragInt("##GameRegister", &shaderRegister, 1.0f, 0, 4095))
				binding.shaderRegister = static_cast<uint32_t>((std::max)(0, shaderRegister));
			ImGui::TextUnformatted("Register Space");
			ImGui::SameLine();
			if (ImGui::DragInt("##GameSpace", &registerSpace, 1.0f, 0, 4095))
				binding.registerSpace = static_cast<uint32_t>((std::max)(0, registerSpace));
			if (gameBindings.empty())
				ImGui::TextUnformatted("No reflected texture bindings are available; configure the register manually.");
		}

		ImGui::TextUnformatted("Access");
		ImGui::SameLine();
		if (ImGui::BeginCombo("##Access", ResourceAccessName(binding.access)))
		{
			const RenderPass::ResourceAccess inputAccesses[] = {
				RenderPass::ResourceAccess::ShaderResource,
				RenderPass::ResourceAccess::CopySource,
			};
			const RenderPass::ResourceAccess fullscreenOutputAccesses[] = {
				RenderPass::ResourceAccess::RenderTarget,
				RenderPass::ResourceAccess::CopyDestination,
			};
			const RenderPass::ResourceAccess computeOutputAccesses[] = {
				RenderPass::ResourceAccess::UnorderedAccess,
				RenderPass::ResourceAccess::CopyDestination,
			};
			const RenderPass::ResourceAccess copyOutputAccesses[] = {
				RenderPass::ResourceAccess::CopyDestination,
			};
			const RenderPass::ResourceAccess gameInputAccesses[] = {
				RenderPass::ResourceAccess::CopySource,
			};
			const RenderPass::ResourceAccess* choices = binding.origin == ShaderResource::ResourceOrigin::Game
				? gameInputAccesses
				: inputAccesses;
			size_t choiceCount = binding.origin == ShaderResource::ResourceOrigin::Game
				? std::size(gameInputAccesses)
				: std::size(inputAccesses);
			if (outputs)
			{
				if (RenderPass::ResolvePassOperation(renderPass) == RenderPass::PassOperation::Copy)
				{
					choices = copyOutputAccesses;
					choiceCount = std::size(copyOutputAccesses);
				}
				else if (RenderPass::ResolveExecutionMode(renderPass) == RenderPass::ExecutionMode::Compute)
				{
					choices = computeOutputAccesses;
					choiceCount = std::size(computeOutputAccesses);
				}
				else
				{
					choices = fullscreenOutputAccesses;
					choiceCount = std::size(fullscreenOutputAccesses);
				}
			}
			for (size_t choiceIndex = 0; choiceIndex < choiceCount; ++choiceIndex)
			{
				if (ImGui::Selectable(ResourceAccessName(choices[choiceIndex]), binding.access == choices[choiceIndex]))
				{
					binding.access = choices[choiceIndex];
					RenderPass::NormalizeExecutionResources(renderPass);
				}
			}
			ImGui::EndCombo();
		}

		const bool shaderBinding = binding.origin == ShaderResource::ResourceOrigin::Runtime &&
			((!outputs && binding.access == RenderPass::ResourceAccess::ShaderResource) ||
				(outputs && binding.access == RenderPass::ResourceAccess::UnorderedAccess));
		if (shaderBinding)
		{
			char hlslName[128]{};
			strncpy_s(hlslName, binding.hlslName.c_str(), _TRUNCATE);
			ImGui::TextUnformatted("HLSL Name");
			ImGui::SameLine();
			if (ImGui::InputText("##HlslName", hlslName, sizeof(hlslName)))
				binding.hlslName = hlslName;
			int shaderRegister = static_cast<int>(binding.shaderRegister);
			int registerSpace = static_cast<int>(binding.registerSpace);
			ImGui::TextUnformatted(outputs ? "UAV Register" : "Texture Register");
			ImGui::SameLine();
			if (ImGui::DragInt("##Register", &shaderRegister, 1.0f, 0, 4095))
				binding.shaderRegister = static_cast<uint32_t>((std::max)(0, shaderRegister));
			ImGui::TextUnformatted("Register Space");
			ImGui::SameLine();
			if (ImGui::DragInt("##Space", &registerSpace, 1.0f, 0, 4095))
				binding.registerSpace = static_cast<uint32_t>((std::max)(0, registerSpace));
			if (!outputs)
			{
				ImGui::TextUnformatted("Temporal View");
				ImGui::SameLine();
				const char* temporalName = binding.temporalView == ShaderResource::TemporalView::Previous
					? "Previous"
					: "Current";
				if (ImGui::BeginCombo("##Temporal", temporalName))
				{
					if (ImGui::Selectable("Current", binding.temporalView == ShaderResource::TemporalView::Current))
						binding.temporalView = ShaderResource::TemporalView::Current;
					if (ImGui::Selectable("Previous", binding.temporalView == ShaderResource::TemporalView::Previous))
						binding.temporalView = ShaderResource::TemporalView::Previous;
					ImGui::EndCombo();
				}
			}
			ImGui::Checkbox("Optional", &binding.optional);
		}
		ImGui::PopID();
	}

	void UI_RenderPasses()
	{
		DatabaseRenderPasses::EnsureRenderPassesLoaded();
		DatabaseModifiedShaders::EnsureModifiedShadersLoaded();
		DatabaseShaderResources::EnsureShaderResourcesLoaded();
		if (!HookD3D12::gLoadedShaderTargetsOnce)
			HookD3D12::RefreshLoadedShaderTargets();
		const std::vector<RenderPass::RenderPassDisk>& renderPasses = DatabaseRenderPasses::GetRenderPasses();
		const std::string renderPassesHeader =
			"Render Passes: " + std::to_string(renderPasses.size()) + "###RenderPasses";

		if (!ImGui::CollapsingHeader(renderPassesHeader.c_str()))
			return;

		ImGui::Indent(indentSpace);
		ImGui::Spacing();

		if (ImGui::Button("Refresh##RenderPasses"))
		{
			DatabaseModifiedShaders::RefreshModifiedShaders();
			HookD3D12::RefreshLoadedShaderTargets();
			DatabaseRenderPasses::RefreshRenderPasses();

			if (!gSelectedRenderPassId.empty() && !DatabaseRenderPasses::FindRenderPassById(gSelectedRenderPassId))
				gSelectedRenderPassId.clear();
		}

		ImGui::SameLine();

		if (ImGui::Button("Open Folder##RenderPasses") && !ShaderInjectorIO::OpenDirectory(ShaderInjectorIO::GetRenderPassesDirectory()))
		{
			WriteToRuntimeLogError("Could not open the Render Passes folder.");
		}

		ImGui::SameLine();

		if (ImGui::Button("Create Render Pass"))
		{
			std::string newRenderPassId;

			if (DatabaseRenderPasses::CreateRenderPass(newRenderPassId))
			{
				gSelectedRenderPassId = newRenderPassId;
				gRenderPassNameBufferId.clear();
				WriteToRuntimeLogSuccess("Created Render Pass: " + newRenderPassId);
			}
			else
			{
				WriteToRuntimeLogError("Failed to create Render Pass.");
			}
		}

		const std::vector<RenderPass::RenderPassDisk>& refreshedRenderPasses = DatabaseRenderPasses::GetRenderPasses();
		RenderPassGraph::Compilation renderPassGraph;
		const std::vector<const RenderPass::RenderPassDisk*> executionOrderedRenderPasses =
			BuildExecutionOrderedRenderPasses(refreshedRenderPasses, renderPassGraph);

		if (refreshedRenderPasses.empty())
		{
			ImGui::TextUnformatted("No Render Passes found.");
			ImGui::Spacing();
			ImGui::Unindent(indentSpace);
			return;
		}

		if (gSelectedRenderPassId.empty() || !DatabaseRenderPasses::FindRenderPassById(gSelectedRenderPassId))
			gSelectedRenderPassId = executionOrderedRenderPasses.front()->id;

		if (ImGui::BeginChild("RenderPassList##RenderPasses", ImVec2(0, 180), ImGuiChildFlags_Borders))
		{
			for (size_t executionIndex = 0; executionIndex < executionOrderedRenderPasses.size(); ++executionIndex)
			{
				const RenderPass::RenderPassDisk& renderPass = *executionOrderedRenderPasses[executionIndex];
				std::string label = renderPass.name.empty() ? renderPass.id : renderPass.name;
				const RenderPassGraph::CompiledNode* graphNode =
					FindCompiledRenderPassNode(renderPassGraph, renderPass.id);
				const bool graphValid = graphNode && graphNode->valid;

				const ModifiedShader::PackageDisk* modifiedShader = DatabaseRenderPasses::ResolveModifiedShader(renderPass);
				const bool eventChainActive = DatabaseRenderPasses::IsEventChainActive(renderPass);
				bool hasResolvedShaderTarget = false;

				if (modifiedShader)
				{
					for (const ShaderTarget::ShaderTargetDisk& shaderTarget : HookD3D12::gLoadedShaderTargets)
					{
						if (shaderTarget.modifiedShaderId == modifiedShader->id && HookD3D12::IsShaderTargetEffectivelyEnabled(shaderTarget))
						{
							hasResolvedShaderTarget = true;
							break;
						}
					}
				}

				if (!renderPass.enabled)
					label += " (disabled)";
				else if (renderPass.event.id.empty())
					label += " (event not set)";
				else if (!graphValid)
					label += " (invalid graph)";
				else if (!eventChainActive)
					label += " (event unavailable)";
				else if (!modifiedShader)
					label += " (shader missing)";
				else if (!modifiedShader->enabled)
					label += " (shader disabled)";
				else if (!hasResolvedShaderTarget)
					label += " (waiting for target)";

				label = std::to_string(executionIndex + 1) + ". " + label;
				label += " [" + std::string(RenderPass::PassOperationName(RenderPass::ResolvePassOperation(renderPass))) + "]";
				label += "##RenderPass_" + renderPass.id;

				const bool selected = renderPass.id == gSelectedRenderPassId;
				const bool active = renderPass.enabled && graphValid && eventChainActive && modifiedShader &&
					modifiedShader->enabled && hasResolvedShaderTarget;

				if (active)
				{
					ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.12f, 0.38f, 0.16f, 1.0f));
					ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.16f, 0.50f, 0.22f, 1.0f));
					ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.20f, 0.62f, 0.28f, 1.0f));
				}

				if (ImGui::Selectable(label.c_str(), selected || active))
				{
					gSelectedRenderPassId = renderPass.id;
					gRenderPassNameBufferId.clear();
				}

				if (active)
					ImGui::PopStyleColor(3);
			}
		}

		ImGui::EndChild();

		RenderPass::RenderPassDisk* renderPass = DatabaseRenderPasses::FindRenderPassById(gSelectedRenderPassId);

		if (!renderPass)
		{
			ImGui::Unindent(indentSpace);
			return;
		}

		if (gRenderPassNameBufferId != renderPass->id)
		{
			strncpy_s(gRenderPassNameBuffer, renderPass->name.c_str(), _TRUNCATE);
			gRenderPassNameBufferId = renderPass->id;
		}

		ImGui::SeparatorText(renderPass->name.empty() ? renderPass->id.c_str() : renderPass->name.c_str());
		ImGui::Indent(indentSpace);
		const RenderPassGraph::CompiledNode* selectedGraphNode =
			FindCompiledRenderPassNode(renderPassGraph, renderPass->id);
		if (renderPass->enabled && selectedGraphNode && !selectedGraphNode->valid && !selectedGraphNode->error.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
			ImGui::TextWrapped("Graph Error: %s", selectedGraphNode->error.c_str());
			ImGui::PopStyleColor();
		}

		bool renderPassEnabled = renderPass->enabled;
		if (ImGui::Checkbox("##RenderPassEnabled", &renderPassEnabled) && !DatabaseRenderPasses::SetRenderPassEnabled(renderPass->id, renderPassEnabled))
			WriteToRuntimeLogError("Could not update Render Pass enabled state: " + renderPass->name);

		ImGui::SameLine();
		ImGui::InputText("##RenderPassName", gRenderPassNameBuffer, sizeof(gRenderPassNameBuffer));

		ImGui::TextUnformatted("Type");
		ImGui::SameLine();

		if (ImGui::BeginCombo("##RenderPassType", RenderPass::TypeName(renderPass->type)))
		{
			const RenderPass::RenderPassType typeOptions[] = {
				RenderPass::RenderPassType::Custom,
				RenderPass::RenderPassType::MipChain,
				RenderPass::RenderPassType::ReplacementPixelShader,
				RenderPass::RenderPassType::ReplacementComputeShader,
			};

			for (RenderPass::RenderPassType typeOption : typeOptions)
			{
				const bool selected = renderPass->type == typeOption;

				if (ImGui::Selectable(RenderPass::TypeName(typeOption), selected) && !selected)
				{
					renderPass->type = typeOption;
					renderPass->operation = RenderPass::PassOperation::Automatic;
					renderPass->executionMode = RenderPass::ExecutionMode::Automatic;
					renderPass->timing = RenderPass::timingBefore;
					renderPass->vertexShaderSourceFile.clear();
					renderPass->fragmentShaderSourceFile.clear();
					renderPass->vertexShaderCompiledBlobFile.clear();
					renderPass->fragmentShaderCompiledBlobFile.clear();
					renderPass->vertexShaderBlob.clear();
					renderPass->fragmentShaderBlob.clear();
					renderPass->vertexShaderBlobHash = 0;
					renderPass->fragmentShaderBlobHash = 0;
					RenderPass::ResolveShaderPaths(*renderPass);

					if (typeOption == RenderPass::RenderPassType::MipChain)
					{
						const ModifiedShader::PackageDisk* modifiedShader =
							DatabaseRenderPasses::ResolveModifiedShader(*renderPass);
						ApplyDefaultMipSourceBinding(*renderPass, modifiedShader);
						ApplyAutomaticExecutionMode(*renderPass, modifiedShader);
					}
				}
			}

			ImGui::EndCombo();
		}

		const bool customPassType = renderPass->type == RenderPass::RenderPassType::Custom;
		if (!customPassType)
			renderPass->operation = RenderPass::PassOperation::Automatic;
		ImGui::TextUnformatted("Operation");
		ImGui::SameLine();
		ImGui::BeginDisabled(!customPassType);
		if (ImGui::BeginCombo("##RenderPassOperation", RenderPass::PassOperationName(renderPass->operation)))
		{
			const RenderPass::PassOperation operations[] = {
				RenderPass::PassOperation::Automatic,
				RenderPass::PassOperation::Custom,
				RenderPass::PassOperation::Downsample,
				RenderPass::PassOperation::UpsampleChain,
				RenderPass::PassOperation::Copy,
			};
			for (RenderPass::PassOperation operation : operations)
			{
				if (ImGui::Selectable(RenderPass::PassOperationName(operation), renderPass->operation == operation) &&
					renderPass->operation != operation)
				{
					renderPass->operation = operation;
					renderPass->vertexShaderSourceFile.clear();
					renderPass->fragmentShaderSourceFile.clear();
					renderPass->vertexShaderCompiledBlobFile.clear();
					renderPass->fragmentShaderCompiledBlobFile.clear();
					renderPass->vertexShaderBlob.clear();
					renderPass->fragmentShaderBlob.clear();
					renderPass->vertexShaderBlobHash = 0;
					renderPass->fragmentShaderBlobHash = 0;
					RenderPass::ResolveShaderPaths(*renderPass);
				}
			}
			ImGui::EndCombo();
		}
		ImGui::EndDisabled();

		const RenderPass::PassOperation configuredOperation = RenderPass::ResolvePassOperation(*renderPass);
		const bool configurableExecutionMode =
			(renderPass->type == RenderPass::RenderPassType::MipChain) ||
			(customPassType &&
				(configuredOperation == RenderPass::PassOperation::Custom ||
					configuredOperation == RenderPass::PassOperation::Downsample ||
					configuredOperation == RenderPass::PassOperation::UpsampleChain));
		ImGui::TextUnformatted("Execution Mode");
		ImGui::SameLine();
		ImGui::BeginDisabled(!configurableExecutionMode);
		if (ImGui::BeginCombo(
			"##RenderPassExecutionMode",
			RenderPass::ExecutionModeName(RenderPass::ResolveExecutionMode(*renderPass))))
		{
			const RenderPass::ExecutionMode modes[] = {
				RenderPass::ExecutionMode::FullscreenPixel,
				RenderPass::ExecutionMode::Compute,
			};
		for (RenderPass::ExecutionMode mode : modes)
			{
				if (ImGui::Selectable(RenderPass::ExecutionModeName(mode), renderPass->executionMode == mode) &&
					renderPass->executionMode != mode)
				{
					SetRenderPassExecutionMode(*renderPass, mode);
				}
			}
			ImGui::EndCombo();
		}
		ImGui::EndDisabled();
		RenderPass::NormalizeExecutionResources(*renderPass);

		if (RenderPass::ResolveExecutionMode(*renderPass) == RenderPass::ExecutionMode::Compute)
		{
			ImGui::TextUnformatted("Dispatch Mode");
			ImGui::SameLine();
			if (ImGui::BeginCombo("##RenderPassDispatchMode", DispatchModeName(renderPass->dispatch.mode)))
			{
				const RenderPass::DispatchMode modes[] = {
					RenderPass::DispatchMode::InheritOriginal,
					RenderPass::DispatchMode::ScaleByResolution,
					RenderPass::DispatchMode::ExplicitThreadGroups,
				};
				for (RenderPass::DispatchMode mode : modes)
				{
					if (ImGui::Selectable(DispatchModeName(mode), renderPass->dispatch.mode == mode))
						renderPass->dispatch.mode = mode;
				}
				ImGui::EndCombo();
			}

			int groupSize[3] = {
				static_cast<int>(renderPass->dispatch.threadGroupSizeX),
				static_cast<int>(renderPass->dispatch.threadGroupSizeY),
				static_cast<int>(renderPass->dispatch.threadGroupSizeZ) };
			ImGui::TextUnformatted("Thread Group Size");
			ImGui::SameLine();
			if (ImGui::DragInt3("##RenderPassThreadGroupSize", groupSize, 1.0f, 1, 1024))
			{
				renderPass->dispatch.threadGroupSizeX = static_cast<uint32_t>((std::max)(1, groupSize[0]));
				renderPass->dispatch.threadGroupSizeY = static_cast<uint32_t>((std::max)(1, groupSize[1]));
				renderPass->dispatch.threadGroupSizeZ = static_cast<uint32_t>((std::max)(1, groupSize[2]));
			}

			if (renderPass->dispatch.mode == RenderPass::DispatchMode::ExplicitThreadGroups)
			{
				int groupCount[3] = {
					static_cast<int>(renderPass->dispatch.explicitGroupCountX),
					static_cast<int>(renderPass->dispatch.explicitGroupCountY),
					static_cast<int>(renderPass->dispatch.explicitGroupCountZ) };
				ImGui::TextUnformatted("Thread Group Count");
				ImGui::SameLine();
				if (ImGui::DragInt3("##RenderPassThreadGroupCount", groupCount, 1.0f, 1, 65535))
				{
					renderPass->dispatch.explicitGroupCountX = static_cast<uint32_t>((std::max)(1, groupCount[0]));
					renderPass->dispatch.explicitGroupCountY = static_cast<uint32_t>((std::max)(1, groupCount[1]));
					renderPass->dispatch.explicitGroupCountZ = static_cast<uint32_t>((std::max)(1, groupCount[2]));
				}
			}
		}

		UI_ResolutionPolicy("PassResolution", renderPass->resolution);
		if (ImGui::Button("Apply to Runtime Textures"))
		{
			for (auto& resource : renderPass->runtimeResources)
				resource.texture.resolution = renderPass->resolution;
		}

		UI_RuntimeResources(*renderPass);
		UI_LogicalBindings(
			*renderPass,
			false,
			renderPass->inputs,
			gSelectedLogicalInputOwnerId,
			gSelectedLogicalInputIndex);
		UI_LogicalBindings(
			*renderPass,
			true,
			renderPass->outputs,
			gSelectedLogicalOutputOwnerId,
			gSelectedLogicalOutputIndex);

		ImGui::SeparatorText("Disk Shader Resources");

		if (ImGui::Button("Refresh DDS Resources"))
			DatabaseShaderResources::RefreshShaderResources();

		const std::vector<ShaderResource::TextureDisk>& shaderResources = DatabaseShaderResources::GetShaderResources();
		ImGui::SameLine();
		ImGui::BeginDisabled(shaderResources.empty());
		if (ImGui::Button("Add"))
		{
			const auto availableIt = std::find_if(shaderResources.begin(), shaderResources.end(), [&](const auto& resource)
			{
				return std::none_of(renderPass->shaderResources.begin(), renderPass->shaderResources.end(), [&](const auto& reference)
				{
					return reference.resourceId == resource.id;
				});
			});
			if (availableIt != shaderResources.end())
			{
				RenderPass::ShaderResourceReferenceDisk reference{};
				reference.resourceId = availableIt->id;
				reference.hlslName = availableIt->name;
				uint32_t nextRegister = 0;

				for (const auto& existing : renderPass->shaderResources)
					nextRegister = (std::max)(nextRegister, existing.shaderRegister + 1);

				reference.shaderRegister = nextRegister;
				renderPass->shaderResources.push_back(std::move(reference));
				gSelectedRenderPassResourceOwnerId = renderPass->id;
				gSelectedRenderPassResourceIndex = renderPass->shaderResources.size() - 1;
			}
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		const bool hasSelectedResource = !renderPass->shaderResources.empty() && gSelectedRenderPassResourceOwnerId == renderPass->id && gSelectedRenderPassResourceIndex < renderPass->shaderResources.size();
		ImGui::BeginDisabled(!hasSelectedResource);
		if (ImGui::Button("Remove"))
		{
			renderPass->shaderResources.erase(renderPass->shaderResources.begin() + gSelectedRenderPassResourceIndex);

			if (gSelectedRenderPassResourceIndex >= renderPass->shaderResources.size() && gSelectedRenderPassResourceIndex > 0)
			{
				--gSelectedRenderPassResourceIndex;
			}
		}
		ImGui::EndDisabled();

		if (gSelectedRenderPassResourceOwnerId != renderPass->id)
		{
			gSelectedRenderPassResourceOwnerId = renderPass->id;
			gSelectedRenderPassResourceIndex = 0;
		}

		if (ImGui::BeginChild("RenderPassShaderResourceList", ImVec2(0, 130), ImGuiChildFlags_Borders))
		{
			for (size_t resourceIndex = 0; resourceIndex < renderPass->shaderResources.size(); ++resourceIndex)
			{
				const auto& reference = renderPass->shaderResources[resourceIndex];
				const ShaderResource::TextureDisk* resource = DatabaseShaderResources::FindShaderResourceById(reference.resourceId);
				std::string label = "t" + std::to_string(reference.shaderRegister) +
					", space" + std::to_string(reference.registerSpace) + ": " +
					(reference.hlslName.empty() ? "Texture" : reference.hlslName) + " -> " +
					(resource ? resource->id + " [" + ShaderResource::TextureDimensionName(resource->dimension) + "]" :
						reference.resourceId + " (missing)") +
					"##RenderPassResource" + std::to_string(resourceIndex);
				if (ImGui::Selectable(label.c_str(), gSelectedRenderPassResourceIndex == resourceIndex))
					gSelectedRenderPassResourceIndex = resourceIndex;
			}
		}
		ImGui::EndChild();

		if (gSelectedRenderPassResourceIndex < renderPass->shaderResources.size())
		{
			RenderPass::ShaderResourceReferenceDisk& reference = renderPass->shaderResources[gSelectedRenderPassResourceIndex];
			const ShaderResource::TextureDisk* selectedResource = DatabaseShaderResources::FindShaderResourceById(reference.resourceId);
			const std::string preview = selectedResource ? selectedResource->id + " [" + ShaderResource::TextureDimensionName(selectedResource->dimension) + "]" : reference.resourceId + " (missing)";

			ImGui::PushID("SelectedRenderPassShaderResource");
			ImGui::TextUnformatted("DDS Texture");
			ImGui::SameLine();

			if (ImGui::BeginCombo("##DDSResource", preview.c_str()))
			{
				for (const ShaderResource::TextureDisk& resource : shaderResources)
				{
					const bool selected = resource.id == reference.resourceId;
					std::string resourceLabel = resource.id + " [" + ShaderResource::TextureDimensionName(resource.dimension) + "]";

					if (!resource.validationError.empty())
						resourceLabel += " (invalid)";

					if (ImGui::Selectable(resourceLabel.c_str(), selected))
					{
						reference.resourceId = resource.id;

						if (reference.hlslName.empty())
							reference.hlslName = resource.name;
					}
				}
				ImGui::EndCombo();
			}
			if (selectedResource)
			{
				if (!selectedResource->validationError.empty())
					ImGui::TextWrapped("DDS Error: %s", selectedResource->validationError.c_str());
			}

			char hlslName[128]{};
			strncpy_s(hlslName, reference.hlslName.c_str(), _TRUNCATE);
			ImGui::TextUnformatted("HLSL Texture Name");
			ImGui::SameLine();

			if (ImGui::InputText("##HLSLName", hlslName, sizeof(hlslName)))
				reference.hlslName = hlslName;

			int shaderRegister = static_cast<int>(reference.shaderRegister);
			int registerSpace = static_cast<int>(reference.registerSpace);

			ImGui::TextUnformatted("Texture Register");
			ImGui::SameLine();

			if (ImGui::DragInt("##TextureRegister", &shaderRegister, 1.0f, 0, 4095))
				reference.shaderRegister = static_cast<uint32_t>((std::max)(0, shaderRegister));

			ImGui::TextUnformatted("Register Space");
			ImGui::SameLine();

			if (ImGui::DragInt("##TextureRegisterSpace", &registerSpace, 1.0f, 0, 4095))
				reference.registerSpace = static_cast<uint32_t>((std::max)(0, registerSpace));

			ImGui::PopID();
		}

		if (shaderResources.empty())
			ImGui::TextUnformatted("Drop .dds files into ShaderInjector/ShaderResources, then refresh.");

		ImGui::TextUnformatted("Track Resource Bindings");
		ImGui::SameLine();
		ImGui::Checkbox("##RenderPassTrackResourceBindings", &renderPass->trackResourceBindings);

		if (renderPass->trackResourceBindings)
		{
			int maximumTrackedDescriptors = static_cast<int>(renderPass->maximumTrackedDescriptors);

			ImGui::TextUnformatted("Maximum Table Descriptors");
			ImGui::SameLine();

			if (ImGui::DragInt("##RenderPassMaximumTableDescriptors", &maximumTrackedDescriptors, 1.0f, 1, 1024))
			{
				renderPass->maximumTrackedDescriptors = static_cast<uint32_t>((std::max)(1, maximumTrackedDescriptors));
			}
		}

		ImGui::TextUnformatted("Event Type");
		ImGui::SameLine();

		if (ImGui::BeginCombo("##RenderPassEventType", RenderPass::EventTypeName(renderPass->event.type)))
		{
			const RenderPass::EventType eventTypes[] = {
				RenderPass::EventType::ModifiedShader,
				RenderPass::EventType::RenderPass,
			};

			for (RenderPass::EventType eventType : eventTypes)
			{
				const bool selected = renderPass->event.type == eventType;

				if (ImGui::Selectable(RenderPass::EventTypeName(eventType), selected) && !selected)
				{
					renderPass->event.type = eventType;
					renderPass->event.id.clear();
				}
			}

			ImGui::EndCombo();
		}

		std::string eventPreview = "(none)";

		if (!renderPass->event.id.empty())
		{
			if (renderPass->event.type == RenderPass::EventType::ModifiedShader)
			{
				const ModifiedShader::PackageDisk* eventModifiedShader = DatabaseModifiedShaders::FindModifiedShaderById(renderPass->event.id);

				eventPreview = eventModifiedShader ? DatabaseModifiedShaders::DisplayName(*eventModifiedShader) : renderPass->event.id + " (missing)";
			}
			else
			{
				const RenderPass::RenderPassDisk* eventRenderPass = DatabaseRenderPasses::FindRenderPassByIdReadOnly(renderPass->event.id);

				eventPreview = eventRenderPass ? eventRenderPass->name : renderPass->event.id + " (missing)";
			}
		}

		ImGui::TextUnformatted("Event");
		ImGui::SameLine();

		if (ImGui::BeginCombo("##RenderPassEvent", eventPreview.c_str()))
		{
			if (ImGui::Selectable("(none)", renderPass->event.id.empty()))
				renderPass->event.id.clear();

			if (renderPass->event.type == RenderPass::EventType::ModifiedShader)
			{
				const std::vector<ModifiedShader::PackageDisk>& modifiedShaders = DatabaseModifiedShaders::GetModifiedShaders();

				for (size_t modifiedShaderIndex = 0; modifiedShaderIndex < modifiedShaders.size(); ++modifiedShaderIndex)
				{
					const ModifiedShader::PackageDisk& modifiedShader = modifiedShaders[modifiedShaderIndex];
					const bool selected = modifiedShader.id == renderPass->event.id;

					std::string label = DatabaseModifiedShaders::DisplayName(modifiedShader);

					if (!modifiedShader.enabled)
						label += " (disabled)";

					label += "##RenderPassEventModifiedShader_" + std::to_string(modifiedShaderIndex);

					if (ImGui::Selectable(label.c_str(), selected))
					{
						renderPass->event.id = modifiedShader.id;
						ApplyAutomaticExecutionMode(*renderPass, &modifiedShader);

						if (renderPass->type == RenderPass::RenderPassType::MipChain)
							ApplyDefaultMipSourceBinding(*renderPass, &modifiedShader);
					}
				}
			}
			else
			{
				for (size_t eventRenderPassIndex = 0; eventRenderPassIndex < refreshedRenderPasses.size(); ++eventRenderPassIndex)
				{
					const RenderPass::RenderPassDisk& eventRenderPass = refreshedRenderPasses[eventRenderPassIndex];

					if (!DatabaseRenderPasses::CanReferenceRenderPass(renderPass->id, eventRenderPass.id))
						continue;

					const bool selected = eventRenderPass.id == renderPass->event.id;

					std::string label = eventRenderPass.name + " [" + RenderPass::TypeName(eventRenderPass.type) + "]";

					if (!eventRenderPass.enabled)
						label += " (disabled)";

					label += "##RenderPassEventRenderPass_" + std::to_string(eventRenderPassIndex);

					if (ImGui::Selectable(label.c_str(), selected))
					{
						renderPass->event.id = eventRenderPass.id;
						ApplyAutomaticExecutionMode(
							*renderPass,
							DatabaseRenderPasses::ResolveModifiedShader(eventRenderPass));

						if (renderPass->type == RenderPass::RenderPassType::MipChain)
						{
							ApplyDefaultMipSourceBinding(*renderPass, DatabaseRenderPasses::ResolveModifiedShader(eventRenderPass));
						}
					}
				}
			}
			ImGui::EndCombo();
		}

		const ModifiedShader::PackageDisk* selectedModifiedShader = DatabaseRenderPasses::ResolveModifiedShader(*renderPass);
		if (renderPass->executionMode == RenderPass::ExecutionMode::Automatic)
			ApplyAutomaticExecutionMode(*renderPass, selectedModifiedShader);
		const bool mipChainPass = renderPass->type == RenderPass::RenderPassType::MipChain;
		const RenderPass::LogicalResourceBindingDisk* mipRuntimeSource = RenderPass::FindMipChainRuntimeSource(*renderPass);
		const bool replacementPass = RenderPass::IsReplacementPass(renderPass->type);
		const bool directMipChainEvent = mipChainPass && !mipRuntimeSource &&
			renderPass->event.type == RenderPass::EventType::ModifiedShader;

		if (directMipChainEvent || replacementPass)
			renderPass->timing = RenderPass::timingBefore;

		ImGui::TextUnformatted("Timing");
		ImGui::SameLine();
		ImGui::BeginDisabled(directMipChainEvent || replacementPass);

		if (ImGui::BeginCombo("##RenderPassTiming", renderPass->timing.c_str()))
		{
			const char* timingOptions[] = { RenderPass::timingBefore, RenderPass::timingAfter };

			for (const char* timingOption : timingOptions)
			{
				const bool selected = renderPass->timing == timingOption;

				if (ImGui::Selectable(timingOption, selected))
					renderPass->timing = timingOption;
			}

			ImGui::EndCombo();
		}

		ImGui::EndDisabled();

		const std::string resolvedRootTiming = DatabaseRenderPasses::ResolveRootTiming(*renderPass);

		if (mipChainPass && !mipRuntimeSource && resolvedRootTiming == RenderPass::timingAfter)
		{
			ImGui::TextWrapped("A MipChain event must resolve to a graph that executes before the Modified Shader draw.");
		}
		else if (!renderPass->event.id.empty() && !selectedModifiedShader)
		{
			ImGui::TextWrapped("The selected event chain does not currently resolve to an available Modified Shader.");
		}

		if (mipRuntimeSource)
		{
			ImGui::TextWrapped("Mip Source: %s", mipRuntimeSource->resourceId.c_str());
			ImGui::TextWrapped("Mip Output: %s", RenderPass::MipChainOutputResourceId(*renderPass).c_str());
		}
		else if (mipChainPass)
		{
			const std::vector<MipSourceBindingOption> sourceOptions = CollectMipSourceBindingOptions(selectedModifiedShader);
			const auto selectedSourceIt = std::find_if(sourceOptions.begin(), sourceOptions.end(), [&](const auto& option)
			{
				return option.shaderRegister == renderPass->sourceTextureShaderRegister && option.registerSpace == renderPass->sourceTextureRegisterSpace;
			});

			const std::string sourcePreview = selectedSourceIt != sourceOptions.end()
				? selectedSourceIt->name + " (t" + std::to_string(selectedSourceIt->shaderRegister) +
				", space" + std::to_string(selectedSourceIt->registerSpace) + ")"
				: "Manual binding (t" + std::to_string(renderPass->sourceTextureShaderRegister) +
				", space" + std::to_string(renderPass->sourceTextureRegisterSpace) + ")";

			ImGui::TextUnformatted("Source Texture");
			ImGui::SameLine();

			if (ImGui::BeginCombo("##RenderPassMipSourceTexture", sourcePreview.c_str()))
			{
				for (size_t optionIndex = 0; optionIndex < sourceOptions.size(); ++optionIndex)
				{
					const MipSourceBindingOption& option = sourceOptions[optionIndex];
					const bool selected = option.shaderRegister == renderPass->sourceTextureShaderRegister && option.registerSpace == renderPass->sourceTextureRegisterSpace;
					const std::string optionLabel = option.name + " (t" +
						std::to_string(option.shaderRegister) + ", space" +
						std::to_string(option.registerSpace) + ")##MipSource_" +
						std::to_string(optionIndex);

					if (ImGui::Selectable(optionLabel.c_str(), selected))
					{
						renderPass->sourceTextureShaderRegister = option.shaderRegister;
						renderPass->sourceTextureRegisterSpace = option.registerSpace;
					}
				}

				ImGui::EndCombo();
			}

			if (sourceOptions.empty())
				ImGui::TextWrapped("No reflected Texture2D bindings are available for the selected Modified Shader.");

			else if (selectedSourceIt == sourceOptions.end())
			{
				const auto sameRegisterIt = std::find_if(sourceOptions.begin(), sourceOptions.end(), [&](const auto& option)
				{
					return option.shaderRegister == renderPass->sourceTextureShaderRegister;
				});

				if (sameRegisterIt != sourceOptions.end())
				{
					ImGui::TextWrapped(
						"The configured register space does not match the reflected %s binding (t%u, space%u). Select it above and save the Render Pass.",
						sameRegisterIt->name.c_str(),
						sameRegisterIt->shaderRegister,
						sameRegisterIt->registerSpace);
				}
				else
				{
					ImGui::TextWrapped("The manual binding is not present in the selected Modified Shader's reflected Texture2D resources.");
				}
			}

			int shaderRegister = static_cast<int>(renderPass->sourceTextureShaderRegister);
			int registerSpace = static_cast<int>(renderPass->sourceTextureRegisterSpace);

			ImGui::TextUnformatted("Source Texture Register (t)");
			ImGui::SameLine();

			if (ImGui::DragInt("##RenderPassSourceTextureRegister", &shaderRegister, 1.0f, 0, 4095))
				renderPass->sourceTextureShaderRegister = static_cast<uint32_t>((std::max)(0, shaderRegister));

			ImGui::TextUnformatted("Register Space");
			ImGui::SameLine();

			if (ImGui::DragInt("##RenderPassSourceTextureRegisterSpace", &registerSpace, 1.0f, 0, 4095))
				renderPass->sourceTextureRegisterSpace = static_cast<uint32_t>((std::max)(0, registerSpace));
		}

		const bool replacementPixelPass = renderPass->type == RenderPass::RenderPassType::ReplacementPixelShader;
		const bool replacementComputePass = renderPass->type == RenderPass::RenderPassType::ReplacementComputeShader;
		const bool computeShaderPass =
			RenderPass::ResolveExecutionMode(*renderPass) == RenderPass::ExecutionMode::Compute;
		const bool customComputePass = computeShaderPass && !replacementComputePass;
		const RenderPass::PassOperation resolvedOperation = RenderPass::ResolvePassOperation(*renderPass);
		const bool copyPass = resolvedOperation == RenderPass::PassOperation::Copy;
		if (!copyPass)
		{
		const char* shaderSectionLabel = "Fullscreen Fragment Shader";
		if (replacementComputePass)
			shaderSectionLabel = "Replacement Compute Shader";
		else if (computeShaderPass && mipChainPass)
			shaderSectionLabel = "Mip Chain Compute Shader";
		else if (computeShaderPass && resolvedOperation == RenderPass::PassOperation::Downsample)
			shaderSectionLabel = "Downsample Compute Shader";
		else if (computeShaderPass && resolvedOperation == RenderPass::PassOperation::UpsampleChain)
			shaderSectionLabel = "Upsample Chain Compute Shader";
		else if (computeShaderPass)
			shaderSectionLabel = "Compute Shader";
		else if (replacementPixelPass)
			shaderSectionLabel = "Replacement Pixel Shader";
		else if (mipChainPass)
			shaderSectionLabel = "Mip Chain Shader";
		else if (resolvedOperation == RenderPass::PassOperation::Downsample)
			shaderSectionLabel = "Downsample Shader";
		else if (resolvedOperation == RenderPass::PassOperation::UpsampleChain)
			shaderSectionLabel = "Upsample Chain Shader";
		ImGui::SeparatorText(shaderSectionLabel);
		const bool hasShaderTemplate = RenderPass::HasShaderTemplate(*renderPass);
		const bool hasCompiledShaders = RenderPass::HasCompiledShaders(*renderPass);
		ImGui::Text("Source: %s", hasShaderTemplate ? "Ready" : "Not created");
		ImGui::Text("Compiled: %s", hasCompiledShaders ? "Ready" : "Not loaded");
		const bool canCreateShaderTemplate = selectedModifiedShader &&
			(computeShaderPass
				? selectedModifiedShader->shaderType == ShaderTarget::ComputeShader
				: selectedModifiedShader->shaderType == ShaderTarget::PixelShader);

		if (!hasShaderTemplate)
		{
			ImGui::BeginDisabled(!canCreateShaderTemplate);
			const char* createTemplateLabel = replacementComputePass
				? "Create Replacement Compute Shader Template"
				: (computeShaderPass
					? (mipChainPass ? "Create Mip Chain Compute Shader Template" :
						(resolvedOperation == RenderPass::PassOperation::Downsample
							? "Create Downsample Compute Shader Template"
							: (resolvedOperation == RenderPass::PassOperation::UpsampleChain
								? "Create Upsample Chain Compute Shader Template"
								: "Create Render Pass Compute Shader Template")))
				: (replacementPixelPass
					? "Create Replacement Pixel Shader Template"
					: (mipChainPass ? "Create Mip Chain Shader Template" :
					(resolvedOperation == RenderPass::PassOperation::Downsample ? "Create Downsample Shader Template" :
					(resolvedOperation == RenderPass::PassOperation::UpsampleChain ? "Create Upsample Chain Shader Template" :
					"Create Render Pass Fragment Shader Template")))));
			if (ImGui::Button(createTemplateLabel))
			{
				std::string error;

				if (DatabaseRenderPasses::CreateShaderTemplate(renderPass->id, error))
					WriteToRuntimeLogSuccess("Created and compiled Render Pass shader template: " + renderPass->name);
				else
					WriteToRuntimeLogError("Could not create Render Pass shader template: " + error);
			}

			ImGui::EndDisabled();
		}
		else
		{
			if (ImGui::Button("Recompile Render Pass Shaders"))
			{
				std::string error;

				if (DatabaseRenderPasses::CompileRenderPassShaders(renderPass->id, error))
					WriteToRuntimeLogSuccess("Recompiled Render Pass shaders: " + renderPass->name);
				else
					WriteToRuntimeLogError("Could not compile Render Pass shaders: " + error);
			}

			ImGui::SameLine();

			if (ImGui::Button(computeShaderPass ? "Open Compute Shader" : "Open Fragment Shader") &&
				!ShaderInjectorIO::OpenFile(renderPass->fragmentShaderSourcePath))
			{
				WriteToRuntimeLogError("Could not open Render Pass shader: " + renderPass->fragmentShaderSourcePath);
			}
		}

		if (!canCreateShaderTemplate && !hasShaderTemplate)
			ImGui::TextUnformatted(computeShaderPass
				? "Select an event that resolves to a compute Modified Shader to create this template."
				: "Select an event that resolves to a pixel Modified Shader to create this template.");
		}

		if (ImGui::Button("Save##RenderPass"))
		{
			renderPass->name = StringHelper::TrimWhitespace(gRenderPassNameBuffer);

			if (renderPass->name.empty())
			{
				WriteToRuntimeLogError("Render Pass name cannot be empty.");
			}
			else if (DatabaseRenderPasses::SaveRenderPass(renderPass->id))
			{
				WriteToRuntimeLogSuccess("Saved Render Pass: " + renderPass->name);
			}
			else
			{
				WriteToRuntimeLogError("Failed to save Render Pass: " + renderPass->id);
			}
		}

		ImGui::SameLine();

		if (ImGui::Button("Delete##RenderPass"))
		{
			const std::string deletedName = renderPass->name;

			if (DatabaseRenderPasses::DeleteRenderPass(renderPass->id))
			{
				gSelectedRenderPassId.clear();
				gRenderPassNameBufferId.clear();
				WriteToRuntimeLogSuccess("Deleted Render Pass: " + deletedName);
			}
			else
			{
				WriteToRuntimeLogError("Failed to delete Render Pass: " + deletedName);
			}

			ImGui::Unindent(indentSpace);
			ImGui::Unindent(indentSpace);
			return;
		}

		ImGui::SameLine();

		if (ImGui::Button("Open Folder##SelectedRenderPass") && !ShaderInjectorIO::OpenDirectory(renderPass->packageDirectory))
		{
			WriteToRuntimeLogError("Could not open Render Pass package folder: " + renderPass->packageDirectory);
		}

		if (ImGui::TreeNodeEx("Info##RenderPassInfo"))
		{
			const std::string resolvedModifiedShaderId = DatabaseRenderPasses::ResolveModifiedShaderId(*renderPass);

			ImGui::Text("ID: %s", renderPass->id.c_str());
			ImGui::Text("Type: %s", RenderPass::TypeName(renderPass->type));
			ImGui::Text("Package: %s", renderPass->packageDirectory.c_str());
			ImGui::Text("JSON: %s", renderPass->jsonPath.c_str());
			ImGui::Text("Event Type: %s", RenderPass::EventTypeName(renderPass->event.type));
			ImGui::Text("Event ID: %s", renderPass->event.id.empty() ? "(none)" : renderPass->event.id.c_str());
			ImGui::Text("Resolved Modified Shader ID: %s", resolvedModifiedShaderId.empty() ? "(none)" : resolvedModifiedShaderId.c_str());
			ImGui::Text("Resolved Root Boundary: %s", resolvedRootTiming.empty() ? "(none)" : resolvedRootTiming.c_str());
			ImGui::Text("Maximum Table Descriptors: %u", renderPass->maximumTrackedDescriptors);

			if (mipChainPass)
			{
				ImGui::Text("Mip Source: t%u, space%u", renderPass->sourceTextureShaderRegister, renderPass->sourceTextureRegisterSpace);
			}

			ImGui::Text("Vertex Source: %s", renderPass->vertexShaderSourcePath.empty() ? "(none)" : renderPass->vertexShaderSourcePath.c_str());
			ImGui::Text("Fragment Source: %s", renderPass->fragmentShaderSourcePath.empty() ? "(none)" : renderPass->fragmentShaderSourcePath.c_str());
			ImGui::Text("Vertex Blob: %s", renderPass->vertexShaderCompiledBlobPath.empty() ? "(none)" : renderPass->vertexShaderCompiledBlobPath.c_str());
			ImGui::Text("Fragment Blob: %s", renderPass->fragmentShaderCompiledBlobPath.empty() ? "(none)" : renderPass->fragmentShaderCompiledBlobPath.c_str());

			if (selectedModifiedShader)
				ImGui::Text("Shader Type: %s", StringHelper::ShaderTypeToString(selectedModifiedShader->shaderType).c_str());

			int linkedShaderTargetCount = 0;

			if (!resolvedModifiedShaderId.empty())
			{
				for (const ShaderTarget::ShaderTargetDisk& shaderTarget : HookD3D12::gLoadedShaderTargets)
				{
					if (shaderTarget.modifiedShaderId == resolvedModifiedShaderId)
						++linkedShaderTargetCount;
				}
			}

			ImGui::Text("Resolved Shader Targets: %d", linkedShaderTargetCount);

			if (!resolvedModifiedShaderId.empty())
			{
				for (const ShaderTarget::ShaderTargetDisk& shaderTarget : HookD3D12::gLoadedShaderTargets)
				{
					if (shaderTarget.modifiedShaderId != resolvedModifiedShaderId)
						continue;

					ImGui::BulletText("%s [%s]", shaderTarget.name.c_str(), shaderTarget.originalShaderBytecodeHash.c_str());
				}
			}

			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Runtime Diagnostics##RenderPassDiagnostics"))
		{
			const RenderPass::RuntimeDiagnostics diagnostics = RenderPassRuntime::GetDiagnostics(renderPass->id);
			ImGui::Text("Triggers: %llu", static_cast<unsigned long long>(diagnostics.triggerCount));
			ImGui::Text("Executions: %llu", static_cast<unsigned long long>(diagnostics.executionCount));
			ImGui::Text("Execution Failures: %llu", static_cast<unsigned long long>(diagnostics.executionFailureCount));
			ImGui::Text("Last Event: %s:%s",
				diagnostics.lastEventType.empty() ? "(none)" : diagnostics.lastEventType.c_str(),
				diagnostics.lastEventId.empty() ? "(none)" : diagnostics.lastEventId.c_str());
			ImGui::Text("Last Event Timing: %s", diagnostics.lastTiming.empty() ? "(none)" : diagnostics.lastTiming.c_str());
			ImGui::Text("Last Command: %s", diagnostics.lastOperation.empty() ? "(none)" : diagnostics.lastOperation.c_str());
			ImGui::Text("Modified Shader: %s", diagnostics.lastModifiedShaderId.empty() ? "(none)" : diagnostics.lastModifiedShaderId.c_str());
			ImGui::Text("Last Target: %s", diagnostics.lastShaderTargetName.empty() ? "(none)" : diagnostics.lastShaderTargetName.c_str());
			ImGui::Text("Last Hash: %s", diagnostics.lastShaderTargetHash.empty() ? "(none)" : diagnostics.lastShaderTargetHash.c_str());
			if (!diagnostics.lastExecutionError.empty())
				ImGui::TextWrapped("Execution Error: %s", diagnostics.lastExecutionError.c_str());

			if (ImGui::Button("Clear##RenderPassDiagnostics"))
				RenderPassRuntime::ClearDiagnostics(renderPass->id);

			if (ImGui::BeginChild("ResourceBindings##RenderPassDiagnostics", ImVec2(0, 220), ImGuiChildFlags_Borders))
			{
				if (diagnostics.resourceBindings.empty())
				{
					ImGui::TextUnformatted("No resource bindings captured.");
				}
				else
				{
					for (size_t bindingIndex = 0; bindingIndex < diagnostics.resourceBindings.size(); ++bindingIndex)
					{
						const RenderPass::ResourceBindingDiagnostic& binding = diagnostics.resourceBindings[bindingIndex];
						ImGui::PushID(static_cast<int>(bindingIndex));
						if (ImGui::TreeNodeEx("Binding", ImGuiTreeNodeFlags_SpanAvailWidth, "%s: %s", binding.pipeline.c_str(), binding.bindingType.c_str()))
						{
							if (binding.rootParameterIndex != UINT32_MAX)
								ImGui::Text("Root Parameter: %u", binding.rootParameterIndex);
							if (binding.gpuAddress)
								ImGui::Text("GPU Address: 0x%016llX", static_cast<unsigned long long>(binding.gpuAddress));
							if (binding.gpuDescriptorHandle)
								ImGui::Text("GPU Descriptor: 0x%016llX", static_cast<unsigned long long>(binding.gpuDescriptorHandle));
							if (binding.cpuDescriptorHandle)
								ImGui::Text("CPU Descriptor: 0x%016llX", static_cast<unsigned long long>(binding.cpuDescriptorHandle));
							if (binding.descriptorHeapType != UINT32_MAX)
								ImGui::Text("Heap Type: %u", binding.descriptorHeapType);
							if (binding.descriptorIndex != UINT32_MAX)
								ImGui::Text("Descriptor Index: %u", binding.descriptorIndex);
							if (binding.descriptorCount)
								ImGui::Text("Descriptor Count: %u", binding.descriptorCount);
							if (binding.shaderRegister != UINT32_MAX)
							{
								ImGui::Text(
									"Shader Register: %u, Space: %u",
									binding.shaderRegister,
									binding.registerSpace == UINT32_MAX ? 0 : binding.registerSpace);
							}
							if (binding.resourcePointer)
								ImGui::Text("Resource: 0x%016llX", static_cast<unsigned long long>(binding.resourcePointer));
							if (!binding.resourceName.empty())
								ImGui::Text("Resource Name: %s", binding.resourceName.c_str());
							if (binding.resourceDimension != UINT32_MAX)
							{
								ImGui::Text("Dimension: %s", ResourceDimensionName(binding.resourceDimension));
								if (binding.resourceDimension == D3D12_RESOURCE_DIMENSION_BUFFER)
								{
									ImGui::Text("Buffer Offset: %llu", static_cast<unsigned long long>(binding.bufferOffset));
									ImGui::Text("Buffer Size: %llu", static_cast<unsigned long long>(binding.bufferSize));
								}
								else
								{
									ImGui::Text(
										"Extent: %llux%ux%u",
										static_cast<unsigned long long>(binding.resourceWidth),
										binding.resourceHeight,
										binding.resourceDepthOrArraySize);
									ImGui::Text("Mip Levels: %u", binding.resourceMipLevels);
									ImGui::Text("Sample Count: %u", binding.resourceSampleCount);
								}
								ImGui::Text("DXGI Format: %u", binding.resourceFormat);
							}
							if (binding.elementCount)
							{
								ImGui::Text("First Element: %llu", static_cast<unsigned long long>(binding.firstElement));
								ImGui::Text("Element Count: %u", binding.elementCount);
								ImGui::Text("Structure Stride: %u", binding.structureByteStride);
							}
							if (!binding.rootConstants.empty())
							{
								std::string constantText = "Values:";
								for (uint32_t value : binding.rootConstants)
									constantText += StringHelper::Format(" 0x%08X", value);
								ImGui::TextWrapped("%s", constantText.c_str());
								ImGui::Text("Destination Offset: %u", binding.destinationOffset);
							}
							ImGui::TreePop();
						}
						ImGui::PopID();
					}
				}
			}
			ImGui::EndChild();
			ImGui::TreePop();
		}

		ImGui::Unindent(indentSpace);
		ImGui::Spacing();
		ImGui::Unindent(indentSpace);
	}
}
