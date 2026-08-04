//ShaderInjectorGUI.cpp
#include "ShaderInjectorGUI.h"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <mutex>
#include <vector>

//3RD Party
#include "imgui.h"

//custom
#include "HookD3D12.h"
#include "ShaderInjectorIO.h"
#include "Hash.h"
#include "Globals.h"
#include "DatabaseModifiedShaders.h"
#include "DatabaseRenderPasses.h"
#include "DatabaseShaderConfigurations.h"
#include "ModifiedShaderCreation.h"
#include "RenderDocIntegration.h"
#include "RenderPassRuntime.h"
#include "ShaderAutomaticDiscovery.h"
#include "StringHelper.h"
#include "ShaderInjectorGUITooltips.h"
#include "Keycodes.h"
#include "ShaderInjectorVersion.h"

namespace ShaderInjectorGUI
{
	static std::string gSelectedRenderPassId;
	static std::string gRenderPassNameBufferId;
	static char gRenderPassNameBuffer[256]{};

	struct MipSourceBindingOption
	{
		std::string name;
		uint32_t shaderRegister = 0;
		uint32_t registerSpace = 0;
	};

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

	void UI_RenderPasses()
	{
		if (!ImGui::CollapsingHeader("Render Passes"))
			return;

		ImGui::Indent(indentSpace);
		ImGui::Spacing();
		DatabaseRenderPasses::EnsureRenderPassesLoaded();
		DatabaseModifiedShaders::EnsureModifiedShadersLoaded();

		if (!HookD3D12::gLoadedShaderTargetsOnce)
			HookD3D12::RefreshLoadedShaderTargets();

		const std::vector<RenderPass::RenderPassDisk>& renderPasses = DatabaseRenderPasses::GetRenderPasses();
		ImGui::Text("Loaded: %zu", renderPasses.size());
		ImGui::SameLine();

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

		if (refreshedRenderPasses.empty())
		{
			ImGui::TextUnformatted("No Render Passes found.");
			ImGui::Spacing();
			ImGui::Unindent(indentSpace);
			return;
		}

		if (gSelectedRenderPassId.empty() || !DatabaseRenderPasses::FindRenderPassById(gSelectedRenderPassId))
			gSelectedRenderPassId = refreshedRenderPasses.front().id;

		if (ImGui::BeginChild("RenderPassList##RenderPasses", ImVec2(0, 180), ImGuiChildFlags_Borders))
		{
			for (const RenderPass::RenderPassDisk& renderPass : refreshedRenderPasses)
			{
				std::string label = renderPass.name.empty() ? renderPass.id : renderPass.name;

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
				else if (!eventChainActive)
					label += " (event unavailable)";
				else if (!modifiedShader)
					label += " (shader missing)";
				else if (!modifiedShader->enabled)
					label += " (shader disabled)";
				else if (!hasResolvedShaderTarget)
					label += " (waiting for target)";

				label += " [" + std::string(RenderPass::TypeName(renderPass.type)) + "]";
				label += "##RenderPass_" + renderPass.id;

				const bool selected = renderPass.id == gSelectedRenderPassId;
				const bool active = renderPass.enabled && eventChainActive && modifiedShader && modifiedShader->enabled && hasResolvedShaderTarget;

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

		ImGui::TextUnformatted("Name");
		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::InputText("##RenderPassName", gRenderPassNameBuffer, sizeof(gRenderPassNameBuffer));
		ImGui::TextUnformatted("Enabled");
		ImGui::Checkbox("##RenderPassEnabled", &renderPass->enabled);

		ImGui::TextUnformatted("Type");
		ImGui::SetNextItemWidth(-FLT_MIN);

		if (ImGui::BeginCombo("##RenderPassType", RenderPass::TypeName(renderPass->type)))
		{
			const RenderPass::RenderPassType typeOptions[] = {
				RenderPass::RenderPassType::Custom,
				RenderPass::RenderPassType::MipChain,
			};

			for (RenderPass::RenderPassType typeOption : typeOptions)
			{
				const bool selected = renderPass->type == typeOption;

				if (ImGui::Selectable(RenderPass::TypeName(typeOption), selected) && !selected)
				{
					renderPass->type = typeOption;
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
						ApplyDefaultMipSourceBinding(*renderPass, DatabaseRenderPasses::ResolveModifiedShader(*renderPass));
					}
				}
			}

			ImGui::EndCombo();
		}

		ImGui::TextUnformatted("Track Resource Bindings");
		ImGui::Checkbox("##RenderPassTrackResourceBindings", &renderPass->trackResourceBindings);

		if (renderPass->trackResourceBindings)
		{
			int maximumTrackedDescriptors = static_cast<int>(renderPass->maximumTrackedDescriptors);

			ImGui::TextUnformatted("Maximum Table Descriptors");
			ImGui::SetNextItemWidth(-FLT_MIN);

			if (ImGui::DragInt("##RenderPassMaximumTableDescriptors", &maximumTrackedDescriptors, 1.0f, 1, 1024))
			{
				renderPass->maximumTrackedDescriptors = static_cast<uint32_t>((std::max)(1, maximumTrackedDescriptors));
			}
		}

		ImGui::TextUnformatted("Event Type");
		ImGui::SetNextItemWidth(-FLT_MIN);

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
		ImGui::SetNextItemWidth(-FLT_MIN);
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
		const bool mipChainPass = renderPass->type == RenderPass::RenderPassType::MipChain;
		const bool directMipChainEvent = mipChainPass && renderPass->event.type == RenderPass::EventType::ModifiedShader;

		if (directMipChainEvent)
			renderPass->timing = RenderPass::timingBefore;

		ImGui::TextUnformatted("Timing");
		ImGui::BeginDisabled(directMipChainEvent);
		ImGui::SetNextItemWidth(-FLT_MIN);

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

		if (mipChainPass && resolvedRootTiming == RenderPass::timingAfter)
		{
			ImGui::TextWrapped("A MipChain event must resolve to a graph that executes before the Modified Shader draw.");
		}
		else if (!renderPass->event.id.empty() && !selectedModifiedShader)
		{
			ImGui::TextWrapped("The selected event chain does not currently resolve to an available Modified Shader.");
		}

		if (mipChainPass)
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
			ImGui::SetNextItemWidth(-FLT_MIN);

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
			ImGui::SetNextItemWidth(-FLT_MIN);

			if (ImGui::DragInt("##RenderPassSourceTextureRegister", &shaderRegister, 1.0f, 0, 4095))
				renderPass->sourceTextureShaderRegister = static_cast<uint32_t>((std::max)(0, shaderRegister));

			ImGui::TextUnformatted("Register Space");
			ImGui::SetNextItemWidth(-FLT_MIN);

			if (ImGui::DragInt("##RenderPassSourceTextureRegisterSpace", &registerSpace, 1.0f, 0, 4095))
				renderPass->sourceTextureRegisterSpace = static_cast<uint32_t>((std::max)(0, registerSpace));
		}

		ImGui::SeparatorText(mipChainPass ? "Mip Chain Shader" : "Fullscreen Fragment Shader");
		const bool hasShaderTemplate = RenderPass::HasShaderTemplate(*renderPass);
		const bool hasCompiledShaders = RenderPass::HasCompiledShaders(*renderPass);
		ImGui::Text("Source: %s", hasShaderTemplate ? "Ready" : "Not created");
		ImGui::Text("Compiled: %s", hasCompiledShaders ? "Ready" : "Not loaded");
		const bool canCreateShaderTemplate = selectedModifiedShader && selectedModifiedShader->shaderType == ShaderTarget::PixelShader;

		if (!hasShaderTemplate)
		{
			ImGui::BeginDisabled(!canCreateShaderTemplate);
			if (ImGui::Button(mipChainPass
				? "Create Mip Chain Shader Template"
				: "Create Render Pass Fragment Shader Template"))
			{
				std::string error;

				if (DatabaseRenderPasses::CreateFragmentShaderTemplate(renderPass->id, error))
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

			if (ImGui::Button("Open Fragment Shader") && !ShaderInjectorIO::OpenFile(renderPass->fragmentShaderSourcePath))
			{
				WriteToRuntimeLogError("Could not open Render Pass fragment shader: " + renderPass->fragmentShaderSourcePath);
			}
		}

		if (!canCreateShaderTemplate && !hasShaderTemplate)
			ImGui::TextUnformatted("Select an event that resolves to a pixel Modified Shader to create this template.");

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