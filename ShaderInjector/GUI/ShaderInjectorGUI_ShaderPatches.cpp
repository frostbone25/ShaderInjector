#include "ShaderInjectorGUI.h"
#include "Globals.h"
#include <algorithm>
#include <cfloat>
#include <cstring>
#include <string>
#include "imgui.h"
#include "ShaderPatch/ShaderPatchRuntime.h"
#include "Hash/Hash.h"
#include "IO/ShaderInjectorIO.h"
#include "StringHelper.h"
#include "GUI/ShaderInjectorGUITooltips.h"
#include "GUI/PropertyWidgets.h"

namespace ShaderInjectorGUI
{
	namespace
	{
		std::string selectedShader;
		std::string ShaderKey(const ShaderPatchRuntime::PatchedShaderInfo& shader)
		{
			return Hash::FormatHash(shader.originalHash) + ":" + std::to_string(shader.stage);
		}
		void Dump(const ShaderPatchRuntime::PatchedShaderInfo& shader, bool patched)
		{
			const auto result = ShaderPatchRuntime::DumpShader(shader, patched);
			const auto& path = result.bytecodePath;
			if (path.empty())
			{
				WriteToRuntimeLogError(std::string("No ") + (patched ? "patched" : "original") + " bytecode is available for shader " + Hash::FormatHash(shader.originalHash));
				return;
			}
			if (!result.bytecodeWritten)
			{
				WriteToRuntimeLogError("Could not dump shader bytecode: " + path);
				return;
			}
			WriteToRuntimeLogSuccess("Dumped shader bytecode: " + path);
			if (result.disassemblyAttempted)
			{
				if (result.disassemblyWritten)
					WriteToRuntimeLogSuccess("Dumped SM6 disassembly beside bytecode: " + path);
				else
					WriteToRuntimeLogWarning("Bytecode was dumped, but SM6 disassembly failed: " + path);
			}
		}
	} //namespace

	void DrawShaderPatches()
	{
		const auto recipes = ShaderPatchRuntime::GetRecipes();
		const ShaderPatchRuntime::PatchedShaderCounts patchedShaders = ShaderPatchRuntime::GetPatchedShaderCounts();
		const std::string shaderPatchesHeader = "Shader Patches (Recipes: " + std::to_string(recipes.size()) +
												", Patched Shaders: " + std::to_string(patchedShaders.total) + ")###ShaderPatches";

		if (!ImGui::CollapsingHeader(shaderPatchesHeader.c_str())) return;

		ImGui::Indent(indentSpace);
		ImGui::Spacing();

		ImGui::InputTextMultiline("##ShaderPatchesNote",
								  const_cast<char*>(noteShaderPatchesText),
								  strlen(noteShaderPatchesText) + 1,
								  ImVec2(-FLT_MIN, 0), //-FLT_MIN width = stretch to window edge, 0 height = auto
								  ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_WordWrap);

		bool enabled = Globals::gShaderPatchesEnabled.load();
		if (PropertyWidgets::Checkbox("Enable Shader Patches", "Enable Shader Patches", &enabled))
		{
			Globals::gShaderPatchesEnabled.store(enabled);
			ShaderPatchRuntime::NotifyEnabledChanged();
		}
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltipEnableShaderPatches);
		ImGui::SameLine();
		const bool reloadAvailable = ShaderPatchRuntime::IsReloadAvailable();
		ImGui::BeginDisabled(!reloadAvailable);
		if (ImGui::Button("Reload Recipes##ShaderPatches")) ShaderPatchRuntime::RequestReload();
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("%s", reloadAvailable ? tooltipReloadShaderPatchRecipes : tooltipReloadShaderPatchRecipesUnavailable);
		ImGui::SameLine();
		if (ImGui::Button("Open Folder##ShaderPatches") && !ShaderInjectorIO::OpenDirectory(ShaderPatchRuntime::GetRecipeDirectory()))
			WriteToRuntimeLogError("Could not open the Shader Patches folder: " + ShaderPatchRuntime::GetRecipeDirectory());

		if (ShaderPatchRuntime::IsBusy()) ImGui::TextUnformatted("Applying recipes to captured pipelines...");
		const std::string error = ShaderPatchRuntime::GetError();
		if (!error.empty()) ImGui::TextWrapped("Failed: %s", error.c_str());

		//loaded recipes first, with how many shaders each one patched.
		if (ImGui::BeginTable("ShaderPatchRecipesTable", 4, ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter))
		{
			ImGui::TableSetupColumn("Recipe");
			ImGui::TableSetupColumn("Version");
			ImGui::TableSetupColumn("Adds Textures");
			ImGui::TableSetupColumn("Patched Shaders");
			ImGui::TableHeadersRow();
			for (const ShaderPatch::RecipeInfo& recipe : recipes)
			{
				const auto recipeShaders = patchedShaders.byRecipe.find(recipe.name);
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(recipe.name.c_str());
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%u", recipe.version);
				ImGui::TableSetColumnIndex(2);
				ImGui::TextUnformatted(recipe.requiresResourceBindings ? "Yes" : "No");
				ImGui::TableSetColumnIndex(3);
				ImGui::Text("%zu", recipeShaders == patchedShaders.byRecipe.end() ? size_t{0} : recipeShaders->second);
			}
			ImGui::EndTable();
		}
		ImGui::Spacing();

		auto shaders = ShaderPatchRuntime::GetShaders();
		if (ImGui::BeginTable("ShaderPatchesTable", 7, ImGuiTableFlags_Sortable | ImGuiTableFlags_SortMulti | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_ScrollY, ImVec2(0, 15 * ImGui::GetFontSize())))
		{
			ImGui::TableSetupColumn("Hash", ImGuiTableColumnFlags_DefaultSort);
			ImGui::TableSetupColumn("Recipe");
			ImGui::TableSetupColumn("Stage");
			ImGui::TableSetupColumn("Model");
			ImGui::TableSetupColumn("Original bytes");
			ImGui::TableSetupColumn("Patched bytes");
			ImGui::TableSetupColumn("Status");
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableHeadersRow();
			const ImGuiTableSortSpecs* sort = ImGui::TableGetSortSpecs();
			std::stable_sort(shaders.begin(), shaders.end(), [&](const auto& left, const auto& right)
							 {
				if (sort) for (int index = 0; index < sort->SpecsCount; ++index)
				{
					const auto& specification = sort->Specs[index];
					int order = 0;
					const auto compare = [](const auto& a, const auto& b) { return a < b ? -1 : a > b ? 1 : 0; };
					switch (specification.ColumnIndex)
					{
					case 0: order = compare(left.originalHash, right.originalHash); break;
					case 1: order = compare(left.recipeName, right.recipeName); break;
					case 2: order = compare(left.stage, right.stage); break;
					case 3: order = compare(left.shaderModel, right.shaderModel); break;
					case 4: order = compare(left.originalSize, right.originalSize); break;
					case 5: order = compare(left.patchedSize, right.patchedSize); break;
					case 6: order = compare(left.status, right.status); break;
					}
					if (order) return specification.SortDirection == ImGuiSortDirection_Ascending ? order < 0 : order > 0;
				}
				return ShaderKey(left) < ShaderKey(right); });
			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(shaders.size()));
			while (clipper.Step())
				for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
				{
					const auto& shader = shaders[index];
					const std::string key = ShaderKey(shader);
					ImGui::PushID(key.c_str());
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					if (ImGui::Selectable(Hash::FormatHash(shader.originalHash).c_str(), selectedShader == key, ImGuiSelectableFlags_SpanAllColumns)) selectedShader = key;
					ImGui::TableSetColumnIndex(1);
					ImGui::TextUnformatted(shader.recipeName.c_str());
					ImGui::TableSetColumnIndex(2);
					ImGui::TextUnformatted(StringHelper::ShaderTypeToString(shader.stage).c_str());
					ImGui::TableSetColumnIndex(3);
					ImGui::Text("SM%u.%u", static_cast<UINT>(shader.shaderModel) / 10, static_cast<UINT>(shader.shaderModel) % 10);
					ImGui::TableSetColumnIndex(4);
					ImGui::Text("%zu", shader.originalSize);
					ImGui::TableSetColumnIndex(5);
					ImGui::Text("%zu", shader.patchedSize);
					ImGui::TableSetColumnIndex(6);
					ImGui::TextUnformatted(shader.status.c_str());
					ImGui::PopID();
				}
			ImGui::EndTable();
		}
		const auto selected = std::find_if(shaders.begin(), shaders.end(), [&](const auto& shader)
										   { return ShaderKey(shader) == selectedShader; });
		ImGui::BeginDisabled(selected == shaders.end());
		if (ImGui::Button("Dump Original##Patch") && selected != shaders.end()) Dump(*selected, false);
		ImGui::SameLine();
		if (ImGui::Button("Dump Patched##Patch") && selected != shaders.end()) Dump(*selected, true);
		ImGui::SameLine();
		if (ImGui::Button("Dump Both##Patch") && selected != shaders.end())
		{
			Dump(*selected, false);
			Dump(*selected, true);
		}
		ImGui::EndDisabled();
		ImGui::Unindent(indentSpace);
	}
} //namespace ShaderInjectorGUI
