#include "GUI/ShaderInjectorGUI.h"

#include <string>

#include "imgui.h"

#include "IO/ShaderInjectorIO.h"
#include "ShaderResource/DatabaseShaderResources.h"

namespace ShaderInjectorGUI
{
	void UI_ShaderResources()
	{
		if (!ImGui::CollapsingHeader("Shader Resources"))
			return;

		ImGui::Indent(indentSpace);
		ImGui::Spacing();
		DatabaseShaderResources::EnsureShaderResourcesLoaded();

		const auto& resources = DatabaseShaderResources::GetShaderResources();
		ImGui::Text("Loaded: %zu", resources.size());
		ImGui::SameLine();
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
				const std::string label = resource.name + "##ShaderResource_" + resource.id;
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
		}
		else if (refreshedResources.empty())
		{
			ImGui::TextUnformatted("Drop .dds files into ShaderInjector/ShaderResources, then refresh.");
		}

		ImGui::Spacing();
		ImGui::Unindent(indentSpace);
	}
}
