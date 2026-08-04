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
	static char gShaderConfigurationSearch[256]{};
	static bool gShaderConfigurationDirty = false;

	bool ShaderConfigurationPropertyMatchesSearch(const ShaderConfiguration::PropertyDisk& property, const std::string& lowercaseSearch)
	{
		if (lowercaseSearch.empty())
			return true;

		const std::string searchableText = StringHelper::LowercaseAscii(
			property.name + "\n" +
			property.sourceFile + "\n" +
			property.sourcePath + "\n" +
			property.comment);

		return searchableText.find(lowercaseSearch) != std::string::npos;
	}

	std::string ShaderConfigurationSourceDisplayName(const ShaderConfiguration::PropertyDisk& property)
	{
		std::string displayName = property.sourceFile;

		if (StringHelper::EndsWithIgnoreCase(displayName, ShaderInjectorIO::extensionHLSL))
		{
			displayName.resize(displayName.size() - ShaderInjectorIO::extensionHLSL.size());
		}

		return displayName;
	}

	bool UI_ShaderConfigurationProperty(ShaderConfiguration::PropertyDisk& property)
	{
		bool changed = false;
		ImGui::PushID(property.id.c_str());

		ImGui::TextUnformatted(property.name.c_str());
		ImGui::SameLine();
		ImGui::SmallButton("?");

		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
		{
			ImGui::BeginTooltip();
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);

			if (property.comment.empty())
				ImGui::TextUnformatted("No description was provided.");
			else
				ImGui::TextUnformatted(property.comment.c_str());

			ImGui::Separator();
			ImGui::Text("Type: %s", property.type.c_str());
			ImGui::Text("Default: %s", property.defaultValue.c_str());

			if (!property.range.empty())
				ImGui::Text("Range: %s", property.range.c_str());

			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}

		ImGui::TextDisabled("Shader: %s", property.sourceFile.c_str());

		if (ImGui::IsItemHovered() && property.sourcePath != property.sourceFile)
			ImGui::SetTooltip("%s", property.sourcePath.c_str());

		ImGui::SetNextItemWidth(-FLT_MIN);

		if (property.type == "bool")
		{
			bool value = false;

			if (!ShaderConfiguration::TryGetBoolean(property, value))
			{
				ShaderConfiguration::PropertyDisk defaultProperty = property;
				defaultProperty.value = property.defaultValue;
				ShaderConfiguration::TryGetBoolean(defaultProperty, value);
			}

			if (ImGui::Checkbox("##Value", &value))
			{
				ShaderConfiguration::SetBoolean(property, value);
				changed = true;
			}
		}
		else if (property.type == "int")
		{
			int value = 0;

			if (!ShaderConfiguration::TryGetInteger(property, value))
			{
				ShaderConfiguration::PropertyDisk defaultProperty = property;
				defaultProperty.value = property.defaultValue;
				ShaderConfiguration::TryGetInteger(defaultProperty, value);
			}

			float minimum = 0.0f;
			float maximum = 0.0f;
			const bool hasRange = ShaderConfiguration::TryGetRange(property, minimum, maximum);
			const bool valueChanged = hasRange
				? ImGui::SliderInt(
					"##Value",
					&value,
					static_cast<int>(minimum),
					static_cast<int>(maximum))
				: ImGui::DragInt("##Value", &value, 1.0f);

			if (valueChanged)
			{
				ShaderConfiguration::SetInteger(property, value);
				changed = true;
			}
		}
		else
		{
			const size_t componentCount = ShaderConfiguration::ComponentCount(property);
			std::vector<float> parsedValues;

			if (!ShaderConfiguration::TryGetFloatComponents(property, parsedValues))
			{
				ShaderConfiguration::PropertyDisk defaultProperty = property;
				defaultProperty.value = property.defaultValue;
				ShaderConfiguration::TryGetFloatComponents(defaultProperty, parsedValues);
			}

			float values[4]{};
			for (size_t index = 0; index < componentCount && index < parsedValues.size(); ++index)
			{
				values[index] = parsedValues[index];
			}

			float minimum = 0.0f;
			float maximum = 0.0f;
			const bool hasRange = ShaderConfiguration::TryGetRange(property, minimum, maximum);
			const float dragSpeed = hasRange ? (std::max)(0.001f, (maximum - minimum) / 200.0f) : 0.01f;
			bool valueChanged = false;

			switch (componentCount)
			{
				case 1:
					valueChanged = hasRange
						? ImGui::SliderFloat("##Value", values, minimum, maximum)
						: ImGui::DragFloat("##Value", values, dragSpeed);
					break;
				case 2:
					valueChanged = hasRange
						? ImGui::SliderFloat2("##Value", values, minimum, maximum)
						: ImGui::DragFloat2("##Value", values, dragSpeed);
					break;
				case 3:
					valueChanged = hasRange
						? ImGui::SliderFloat3("##Value", values, minimum, maximum)
						: ImGui::DragFloat3("##Value", values, dragSpeed);
					break;
				case 4:
					valueChanged = hasRange
						? ImGui::SliderFloat4("##Value", values, minimum, maximum)
						: ImGui::DragFloat4("##Value", values, dragSpeed);
					break;
				default:
					ImGui::TextDisabled("Unsupported configuration type: %s", property.type.c_str());
					break;
			}

			if (valueChanged)
			{
				ShaderConfiguration::SetFloatComponents(property, values, componentCount);

				changed = true;
			}
		}

		ImGui::Separator();
		ImGui::PopID();
		return changed;
	}

	void UI_ShaderConfiguration()
	{
		if (!ImGui::CollapsingHeader("Shader Configuration"))
			return;

		ImGui::Indent(indentSpace);
		ImGui::Spacing();

		DatabaseShaderConfigurations::EnsureLoaded();
		ShaderConfiguration::DocumentDisk& configurationDocument = DatabaseShaderConfigurations::GetEditableDocument();

		ImGui::Text("Properties: %zu", configurationDocument.properties.size());

		if (gShaderConfigurationDirty)
		{
			ImGui::SameLine();
			ImGui::TextDisabled("(unsaved changes)");
		}

		ImGui::BeginDisabled(configurationDocument.properties.empty());

		if (ImGui::Button("Apply Changes##ShaderConfiguration"))
		{
			const DatabaseShaderConfigurations::ApplyResult applyResult = DatabaseShaderConfigurations::ApplyChanges();

			if (!applyResult.succeeded)
			{
				WriteToRuntimeLogError("Failed to apply Shader Configuration: " + applyResult.errorMessage);
			}
			else
			{
				gShaderConfigurationDirty = false;
				WriteToRuntimeLogSuccess(
					"Applied Shader Configuration properties=" +
					std::to_string(applyResult.propertyCount) +
					" sourceFiles=" +
					std::to_string(applyResult.sourceFileCount));
				RecompileModifiedShaders(false);
			}
		}

		ImGui::EndDisabled();

		ImGui::SameLine();

		if (ImGui::Button("Reload Properties##ShaderConfiguration"))
		{
			if (DatabaseShaderConfigurations::ReloadProperties())
			{
				gShaderConfigurationDirty = false;
				WriteToRuntimeLogSuccess("Reloaded Shader Configuration properties.");
			}
			else
			{
				WriteToRuntimeLogError("Failed to reload Shader Configuration properties.");
			}
		}

		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::InputTextWithHint("##ShaderConfigurationSearch", "Search properties...", gShaderConfigurationSearch, sizeof(gShaderConfigurationSearch));

		const std::string lowercaseSearch = StringHelper::LowercaseAscii(StringHelper::TrimWhitespace(gShaderConfigurationSearch));
		size_t visiblePropertyCount = 0;

		if (ImGui::BeginChild("ShaderConfiguration##PropertyList", ImVec2(0, 320), ImGuiChildFlags_Borders))
		{
			size_t sourceGroupStart = 0;

			while (sourceGroupStart < configurationDocument.properties.size())
			{
				ShaderConfiguration::PropertyDisk& firstProperty = configurationDocument.properties[sourceGroupStart];
				size_t sourceGroupEnd = sourceGroupStart + 1;

				while (sourceGroupEnd < configurationDocument.properties.size() && configurationDocument.properties[sourceGroupEnd].sourcePath == firstProperty.sourcePath)
				{
					++sourceGroupEnd;
				}

				size_t matchingPropertyCount = 0;

				for (size_t propertyIndex = sourceGroupStart; propertyIndex < sourceGroupEnd; ++propertyIndex)
				{
					if (ShaderConfigurationPropertyMatchesSearch(configurationDocument.properties[propertyIndex], lowercaseSearch))
					{
						++matchingPropertyCount;
					}
				}

				if (matchingPropertyCount > 0)
				{
					const size_t totalPropertyCount = sourceGroupEnd - sourceGroupStart;

					std::string groupLabel = ShaderConfigurationSourceDisplayName(firstProperty) + ": " + std::to_string(totalPropertyCount);

					if (!lowercaseSearch.empty() && matchingPropertyCount != totalPropertyCount)
					{
						groupLabel += " (" + std::to_string(matchingPropertyCount) + " matching)";
					}

					ImGui::PushID(firstProperty.sourcePath.c_str());

					if (!lowercaseSearch.empty())
						ImGui::SetNextItemOpen(true, ImGuiCond_Always);

					const bool sourceGroupOpen = ImGui::TreeNodeEx("##ShaderConfigurationSource", ImGuiTreeNodeFlags_SpanAvailWidth, "%s", groupLabel.c_str());

					if (ImGui::IsItemHovered() && firstProperty.sourcePath != firstProperty.sourceFile)
					{
						ImGui::SetTooltip("%s", firstProperty.sourcePath.c_str());
					}

					if (sourceGroupOpen)
					{
						for (size_t propertyIndex = sourceGroupStart; propertyIndex < sourceGroupEnd; ++propertyIndex)
						{
							ShaderConfiguration::PropertyDisk& property = configurationDocument.properties[propertyIndex];

							if (!ShaderConfigurationPropertyMatchesSearch(property, lowercaseSearch))
							{
								continue;
							}

							++visiblePropertyCount;

							if (UI_ShaderConfigurationProperty(property))
								gShaderConfigurationDirty = true;
						}

						ImGui::TreePop();
					}
					else
					{
						visiblePropertyCount += matchingPropertyCount;
					}

					ImGui::PopID();
				}

				sourceGroupStart = sourceGroupEnd;
			}

			if (configurationDocument.properties.empty())
			{
				ImGui::TextWrapped("No configurable #define properties were found in ModifiedShaders.");
			}
			else if (visiblePropertyCount == 0)
			{
				ImGui::TextUnformatted("No properties match the current search.");
			}
		}

		ImGui::EndChild();

		ImGui::Spacing();
		ImGui::Unindent(indentSpace);
	}
}
