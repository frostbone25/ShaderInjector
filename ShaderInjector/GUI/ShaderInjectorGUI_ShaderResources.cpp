#include "GUI/ShaderInjectorGUI.h"

#include <algorithm>
#include <string>
#include <vector>

#include "imgui.h"

#include "IO/ShaderInjectorIO.h"
#include "ShaderResource/DatabaseShaderResources.h"
#include "ShaderResource/Runtime/ShaderResourceRuntime.h"
#include "ShaderResource/ShaderResourceCatalog.h"

namespace ShaderInjectorGUI
{
	void DrawShaderResources()
	{
		DatabaseShaderResources::EnsureShaderResourcesLoaded();
		std::vector<ShaderResource::CatalogEntry> resources = ShaderResourceCatalog::GetSnapshot();
		const std::string shaderResourcesHeader = "Shader Resources: " + std::to_string(resources.size()) + "###ShaderResources";

		if (!ImGui::CollapsingHeader(shaderResourcesHeader.c_str()))
			return;

		ImGui::Indent(indentSpace);
		ImGui::Spacing();

		if (ImGui::Button("Refresh##ShaderResources"))
		{
			ShaderResourceRuntime::ReloadTextures();
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
											 { return catalogKey(resource) == selectedResourceKey; });
		if (selectedIt == resources.end())
		{
			selectedResourceKey.clear();

			if (!resources.empty())
				selectedResourceKey = catalogKey(resources.front());
		}

		//the column weights below require an explicit stretch sizing policy.
		const ImGuiTableFlags tableFlags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortMulti | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_ScrollY;

		if (ImGui::BeginTable("ShaderResourceList", 3, tableFlags, ImVec2(0, 15 * ImGui::GetFontSize())))
		{
			ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_DefaultSort, 2.0f);
			ImGui::TableSetupColumn("Texture Type", ImGuiTableColumnFlags_None, 1.0f);
			ImGui::TableSetupColumn("Origin Type", ImGuiTableColumnFlags_None, 1.0f);
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableHeadersRow();

			//the catalog is a fresh snapshot each frame, so apply the active column ordering every time.
			const ImGuiTableSortSpecs* sortSpecifications = ImGui::TableGetSortSpecs();

			std::stable_sort(resources.begin(), resources.end(), [&](const ShaderResource::CatalogEntry& left, const ShaderResource::CatalogEntry& right)
			{
				if (sortSpecifications)
				{
					for (int sortIndex = 0; sortIndex < sortSpecifications->SpecsCount; ++sortIndex)
					{
						const ImGuiTableColumnSortSpecs& specification = sortSpecifications->Specs[sortIndex];
						std::string leftValue = left.name;
						std::string rightValue = right.name;

						if (specification.ColumnIndex == 1)
						{
							leftValue = ShaderResource::TextureDimensionName(left.dimension);
							rightValue = ShaderResource::TextureDimensionName(right.dimension);
						}
						else if (specification.ColumnIndex == 2)
						{
							leftValue = ShaderResource::ResourceOriginName(left.origin);
							rightValue = ShaderResource::ResourceOriginName(right.origin);
						}

						const int order = leftValue.compare(rightValue);

						if (order == 0)
							continue;

						if (specification.SortDirection == ImGuiSortDirection_Descending)
							return order > 0;

						return order < 0;
					}
				}

				//stable identities keep equal-valued rows from jumping around during catalog updates.
				return catalogKey(left) < catalogKey(right);
			});

			for (const auto& resource : resources)
			{
				std::string label = resource.name;

				if (resource.resident)
					label += " (resident)";

				const std::string resourceKey = catalogKey(resource);
				ImGui::PushID(resourceKey.c_str());
				label += "###ShaderResource";
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);

				if (ImGui::Selectable(label.c_str(), selectedResourceKey == resourceKey, ImGuiSelectableFlags_SpanAllColumns))
					selectedResourceKey = resourceKey;

				ImGui::TableSetColumnIndex(1);
				ImGui::TextUnformatted(ShaderResource::TextureDimensionName(resource.dimension));
				ImGui::TableSetColumnIndex(2);
				ImGui::TextUnformatted(ShaderResource::ResourceOriginName(resource.origin));
				ImGui::PopID();
			}

			ImGui::EndTable();
		}

		const auto refreshedSelectedIt = std::find_if(resources.begin(), resources.end(), [&](const auto& resource)
													  { return catalogKey(resource) == selectedResourceKey; });

		if (refreshedSelectedIt != resources.end())
		{
			const ShaderResource::CatalogEntry& resource = *refreshedSelectedIt;

			ImGui::SeparatorText(resource.name.c_str());
			PropertyWidgets::ReadOnlyText("Resource ID", "##ResourceID", resource.id);
			PropertyWidgets::ReadOnlyText("Origin", "##ResourceOrigin", ShaderResource::ResourceOriginName(resource.origin));
			PropertyWidgets::ReadOnlyText("Lifetime", "##ResourceLifetime", ShaderResource::ResourceLifetimeName(resource.lifetime));

			if (!resource.ownerRenderPassId.empty())
				PropertyWidgets::ReadOnlyText("Owner", "##ResourceOwner", resource.ownerRenderPassId);

			PropertyWidgets::ReadOnlyText("Dimension", "##ResourceDimension", ShaderResource::TextureDimensionName(resource.dimension));

			std::string extent = std::to_string(resource.width) + "x" + std::to_string(resource.height);

			if (resource.dimension == ShaderResource::TextureDimension::Texture3D)
				extent += "x" + std::to_string(resource.depth);

			PropertyWidgets::ReadOnlyText("Extent", "##ResourceExtent", extent);

			if (resource.dimension != ShaderResource::TextureDimension::Texture3D)
				PropertyWidgets::ReadOnlyText("Array Slices", "##ResourceArraySlices", std::to_string(resource.arraySize));

			PropertyWidgets::ReadOnlyText("Mip Levels", "##ResourceMipLevels", std::to_string(resource.mipLevels));
			PropertyWidgets::ReadOnlyText("DXGI Format", "##ResourceFormat", std::to_string(resource.format));

			if (!resource.status.empty())
				PropertyWidgets::ReadOnlyText("Status", "##ResourceStatus", resource.status);
		}
		else if (resources.empty())
		{
			ImGui::TextUnformatted("Drop .dds files into ShaderInjector/ShaderResources, then refresh.");
		}

		ImGui::Spacing();
		ImGui::Unindent(indentSpace);
	}
} //namespace ShaderInjectorGUI
