#include "GUI/ShaderInjectorGUI.h"

#include <string>

#include "imgui.h"

#include "IO/ShaderInjectorIO.h"
#include "ShaderResource/DatabaseShaderResources.h"

namespace ShaderInjectorGUI
{
	void UI_ShaderResources()
	{
		DatabaseShaderResources::EnsureShaderResourcesLoaded();
		const auto& resources = DatabaseShaderResources::GetShaderResources();
		const std::string shaderResourcesHeader =
			"Shader Resources: " + std::to_string(resources.size()) + "###ShaderResources";

		if (!ImGui::CollapsingHeader(shaderResourcesHeader.c_str()))
			return;

		ImGui::Indent(indentSpace);
		ImGui::Spacing();
		if (ImGui::Button("Refresh##ShaderResources"))
			DatabaseShaderResources::RefreshShaderResources();
		ImGui::SameLine();
		if (ImGui::Button("Open Folder##ShaderResources") &&
			!ShaderInjectorIO::OpenDirectory(ShaderInjectorIO::GetShaderResourcesDirectory()))
		{
			WriteToRuntimeLogError("Could not open the Shader Resources folder.");
		}

		static std::string selectedResourceId;
		const auto& refreshedResources = DatabaseShaderResources::GetShaderResources();
		if (!selectedResourceId.empty() && !DatabaseShaderResources::FindShaderResourceById(selectedResourceId))
			selectedResourceId.clear();
		if (selectedResourceId.empty() && !refreshedResources.empty())
			selectedResourceId = refreshedResources.front().id;

		if (ImGui::BeginChild("ShaderResourceList", ImVec2(0, 180), ImGuiChildFlags_Borders))
		{
			for (const auto& resource : refreshedResources)
			{
				std::string label = resource.name + " [" +
					ShaderResource::TextureDimensionName(resource.dimension) + "]";
				if (!resource.validationError.empty())
					label += " (invalid)";
				label += "##ShaderResource_" + resource.id;
				if (ImGui::Selectable(label.c_str(), selectedResourceId == resource.id))
					selectedResourceId = resource.id;
			}
		}
		ImGui::EndChild();

		if (const auto* resource = DatabaseShaderResources::FindShaderResourceById(selectedResourceId))
		{
			ImGui::SeparatorText(resource->name.c_str());
			ImGui::TextWrapped("File: %s", resource->fileName.c_str());
			ImGui::TextWrapped("Resource ID: %s", resource->id.c_str());
			if (!resource->validationError.empty())
			{
				ImGui::TextWrapped("DDS Error: %s", resource->validationError.c_str());
			}
			else
			{
				ImGui::Text("Dimension: %s", ShaderResource::TextureDimensionName(resource->dimension));
				if (resource->dimension == ShaderResource::TextureDimension::Texture3D)
					ImGui::Text("Extent: %ux%ux%u", resource->width, resource->height, resource->depth);
				else
					ImGui::Text("Extent: %ux%u, Array Slices: %u", resource->width, resource->height, resource->arraySize);
				ImGui::Text("Mip Levels: %u", resource->mipLevels);
				ImGui::Text("DXGI Format: %u", resource->format);
			}
		}
		else if (refreshedResources.empty())
		{
			ImGui::TextUnformatted("Drop .dds files into ShaderInjector/ShaderResources, then refresh.");
		}

		ImGui::Spacing();
		ImGui::Unindent(indentSpace);
	}
}
