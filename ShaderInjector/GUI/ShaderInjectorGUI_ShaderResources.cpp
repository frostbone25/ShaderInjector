#include "GUI/ShaderInjectorGUI.h"

#include <algorithm>
#include <string>
#include <vector>

#include "imgui.h"

#include "IO/ShaderInjectorIO.h"
#include "ShaderResource/DatabaseShaderResources.h"
#include "ShaderResource/ShaderResourceCatalog.h"

namespace ShaderInjectorGUI
{
	void UI_ShaderResources()
	{
		DatabaseShaderResources::EnsureShaderResourcesLoaded();
		std::vector<ShaderResource::CatalogEntry> resources = ShaderResourceCatalog::GetSnapshot();
		const std::string shaderResourcesHeader =
			"Shader Resources: " + std::to_string(resources.size()) + "###ShaderResources";

		if (!ImGui::CollapsingHeader(shaderResourcesHeader.c_str()))
			return;

		ImGui::Indent(indentSpace);
		ImGui::Spacing();
		if (ImGui::Button("Refresh##ShaderResources"))
		{
			DatabaseShaderResources::RefreshShaderResources();
			resources = ShaderResourceCatalog::GetSnapshot();
		}
		ImGui::SameLine();
		if (ImGui::Button("Open Folder##ShaderResources") &&
			!ShaderInjectorIO::OpenDirectory(ShaderInjectorIO::GetShaderResourcesDirectory()))
		{
			WriteToRuntimeLogError("Could not open the Shader Resources folder.");
		}

		const auto catalogKey = [](const ShaderResource::CatalogEntry& resource)
		{
			return std::to_string(static_cast<unsigned int>(resource.origin)) + ":" + resource.id;
		};
		static std::string selectedResourceKey;
		const auto selectedIt = std::find_if(resources.begin(), resources.end(), [&](const auto& resource)
		{
			return catalogKey(resource) == selectedResourceKey;
		});
		if (selectedIt == resources.end())
			selectedResourceKey = resources.empty() ? std::string() : catalogKey(resources.front());

		if (ImGui::BeginChild("ShaderResourceList", ImVec2(0, 180), ImGuiChildFlags_Borders))
		{
			for (const auto& resource : resources)
			{
				std::string label = resource.name + " [" +
					ShaderResource::TextureDimensionName(resource.dimension) + "] - " +
					ShaderResource::ResourceOriginName(resource.origin);
				if (resource.resident)
					label += " (resident)";
				const std::string resourceKey = catalogKey(resource);
				label += "##ShaderResource_" + resourceKey;
				if (ImGui::Selectable(label.c_str(), selectedResourceKey == resourceKey))
					selectedResourceKey = resourceKey;
			}
		}
		ImGui::EndChild();

		const auto refreshedSelectedIt = std::find_if(resources.begin(), resources.end(), [&](const auto& resource)
		{
			return catalogKey(resource) == selectedResourceKey;
		});
		if (refreshedSelectedIt != resources.end())
		{
			const ShaderResource::CatalogEntry& resource = *refreshedSelectedIt;
			ImGui::SeparatorText(resource.name.c_str());
			ImGui::TextWrapped("Resource ID: %s", resource.id.c_str());
			ImGui::Text("Origin: %s", ShaderResource::ResourceOriginName(resource.origin));
			ImGui::Text("Lifetime: %s", ShaderResource::ResourceLifetimeName(resource.lifetime));
			if (!resource.ownerRenderPassId.empty())
				ImGui::TextWrapped("Owner: %s", resource.ownerRenderPassId.c_str());
			ImGui::Text("Dimension: %s", ShaderResource::TextureDimensionName(resource.dimension));
			if (resource.dimension == ShaderResource::TextureDimension::Texture3D)
				ImGui::Text("Extent: %ux%ux%u", resource.width, resource.height, resource.depth);
			else
				ImGui::Text("Extent: %ux%u, Array Slices: %u", resource.width, resource.height, resource.arraySize);
			ImGui::Text("Mip Levels: %u", resource.mipLevels);
			ImGui::Text("DXGI Format: %u", resource.format);
			if (!resource.status.empty())
				ImGui::TextWrapped("Status: %s", resource.status.c_str());
		}
		else if (resources.empty())
		{
			ImGui::TextUnformatted("Drop .dds files into ShaderInjector/ShaderResources, then refresh.");
		}

		ImGui::Spacing();
		ImGui::Unindent(indentSpace);
	}
}
