#include "IO/ShaderInjectorIO.h"
#include "RenderPass/RenderPassConfigurationProperties.h"
#include "ShaderConfiguration/DatabaseShaderConfigurations.h"
#include "ShaderConfiguration/ShaderConfiguration.h"
#include "ShaderConfiguration/ShaderConfigurationCategories.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "Enum/ShaderResourceResolutionMode.h"

void SetConfigurationTestSaveFailure(bool shouldFail);

namespace
{
	class ExpansionChecks
	{
	public:
		int failureCount = 0;

		void Check(bool condition, const std::string& message)
		{
			if (condition)
				return;

			std::cerr << "FAIL: " << message << '\n';
			++failureCount;
		}
	};

	ShaderConfiguration::PropertyDisk MakeRenderPassProperty(
		const std::string& id,
		const std::string& passID,
		ShaderConfiguration::RenderPassPropertyType binding,
		const std::string& valueType,
		const std::string& value)
	{
		ShaderConfiguration::PropertyDisk property{};
		property.id = id;
		property.name = id;
		property.propertyType = ShaderConfiguration::PropertyType::RenderPass;
		property.renderPassID = passID;
		property.renderPassPropertyType = binding;
		property.valueType = valueType;
		property.defaultValue = value;
		property.value = value;
		return property;
	}

	ShaderConfiguration::PropertyDisk* FindProperty(const std::string& name)
	{
		for (ShaderConfiguration::PropertyDisk& property : DatabaseShaderConfigurations::GetEditableDocument().properties)
		{
			if (property.name == name)
				return &property;
		}

		return nullptr;
	}

	bool ContainsLine(const std::string& sourceText, const std::string& line)
	{
		return sourceText.find(line) != std::string::npos;
	}

	bool CheckCategorySource(ExpansionChecks& checks)
	{
		const std::string bom = "\xEF\xBB\xBF";
		const std::string sourceText = bom +
			"// general note\r\n"
			"// [CONFIG TYPE]: int\r\n"
			"// [CONFIG CATEGORY]: Lighting\r\n"
			"#define REPEATED_VALUE 2\r\n"
			"// second definition note\r\n"
			"#define REPEATED_VALUE 3\r\n";
		std::vector<ShaderConfiguration::PropertyDisk> properties;
		ShaderConfiguration::ParseShaderSource("Expansion.hlsl", sourceText, properties);
		checks.Check(properties.size() == 2, "category parsing retains repeated macro definitions");
		if (properties.size() != 2)
			return false;

		checks.Check(properties[0].category == "Lighting", "category tag populates property category");
		checks.Check(properties[0].description == "general note", "category tag is excluded from the description");
		checks.Check(properties[1].category == "Uncategorized", "missing category uses Uncategorized");

		std::string updatedSource;
		std::string error;
		checks.Check(ShaderConfiguration::UpdatePropertyCategorySource(sourceText, properties[1], "Post Processing", updatedSource, error), "category updater inserts a tag for the selected duplicate occurrence");
		checks.Check(updatedSource.rfind(bom, 0) == 0, "category insertion keeps the UTF-8 BOM at the beginning");
		checks.Check(updatedSource.find("\r\n") != std::string::npos && updatedSource.find("\n") == updatedSource.find("\r\n") + 1, "category insertion preserves CRLF line endings");
		checks.Check(ContainsLine(updatedSource, "#define REPEATED_VALUE 2\r\n") && ContainsLine(updatedSource, "#define REPEATED_VALUE 3\r\n"), "category insertion leaves both define lines unchanged");
		checks.Check(ContainsLine(updatedSource, "// [CONFIG CATEGORY]: Lighting\r\n") && ContainsLine(updatedSource, "// [CONFIG CATEGORY]: Post Processing\r\n"), "category insertion leaves comments attached to the other occurrence");

		std::string changedSource;
		checks.Check(ShaderConfiguration::UpdatePropertyCategorySource(updatedSource, properties[1], "Effects", changedSource, error), "category updater changes the exact existing occurrence");
		checks.Check(ContainsLine(changedSource, "// [CONFIG CATEGORY]: Effects\r\n") && !ContainsLine(changedSource, "// [CONFIG CATEGORY]: Post Processing\r\n"), "category update replaces only its matching tag");
		checks.Check(ContainsLine(changedSource, "// second definition note\r\n"), "category update keeps the neighboring description comment");
		return checks.failureCount == 0;
	}

	bool CheckRenderPassOverlay(ExpansionChecks& checks)
	{
		RenderPass::RenderPassDisk basePass{};
		basePass.id = "OverlayPass";
		basePass.enabled = true;
		basePass.resolution.mode = ShaderResource::ResolutionMode::Inherit;
		RenderPass::RuntimeResourceDefinitionDisk runtimeResource{};
		runtimeResource.id = "Output";
		runtimeResource.texture.resolution.mode = ShaderResource::ResolutionMode::Inherit;
		basePass.runtimeResources.push_back(runtimeResource);
		const std::vector<RenderPass::RenderPassDisk> basePasses = {basePass};
		std::vector<RenderPass::RenderPassDisk> effectivePasses = basePasses;

		ShaderConfiguration::DocumentDisk document{};
		document.properties.push_back(MakeRenderPassProperty("enabled", "OverlayPass", ShaderConfiguration::RenderPassPropertyType::Enabled, "bool", "false"));
		document.properties.push_back(MakeRenderPassProperty("downscale", "OverlayPass", ShaderConfiguration::RenderPassPropertyType::DownscalePowerOfTwo, "int", "4"));
		std::string error;
		checks.Check(RenderPass::ApplyConfigurationProperties(document, effectivePasses, error), "valid RenderPass properties apply");
		checks.Check(!effectivePasses[0].enabled, "enabled override changes the effective pass");
		checks.Check(effectivePasses[0].resolution.mode == ShaderResource::ResolutionMode::DownscalePowerOfTwo && effectivePasses[0].resolution.downscaleFactor == 4, "downscale override changes the effective pass resolution");
		checks.Check(effectivePasses[0].runtimeResources[0].texture.resolution.mode == ShaderResource::ResolutionMode::DownscalePowerOfTwo && effectivePasses[0].runtimeResources[0].texture.resolution.downscaleFactor == 4, "downscale override changes effective runtime texture policies");
		checks.Check(basePasses[0].enabled && basePasses[0].resolution.mode == ShaderResource::ResolutionMode::Inherit && basePasses[0].runtimeResources[0].texture.resolution.mode == ShaderResource::ResolutionMode::Inherit, "overlays leave the base pass vector unchanged");

		const std::vector<RenderPass::RenderPassDisk> beforeInvalid = effectivePasses;
		ShaderConfiguration::DocumentDisk invalidFactor = document;
		invalidFactor.properties[1].value = "3";
		checks.Check(!RenderPass::ApplyConfigurationProperties(invalidFactor, effectivePasses, error), "factor 3 is rejected");
		checks.Check(effectivePasses[0].resolution.downscaleFactor == beforeInvalid[0].resolution.downscaleFactor && effectivePasses[0].enabled == beforeInvalid[0].enabled, "invalid factor rejection leaves candidate passes unchanged");

		ShaderConfiguration::DocumentDisk missingPass = document;
		missingPass.properties[0].renderPassID = "MissingPass";
		checks.Check(!RenderPass::ValidateConfigurationProperties(missingPass, basePasses, error), "missing render pass ID is rejected");
		std::vector<RenderPass::RenderPassDisk> missingPassCandidate = basePasses;
		checks.Check(!RenderPass::ApplyConfigurationProperties(missingPass, missingPassCandidate, error) && missingPassCandidate[0].enabled, "missing render pass ID leaves the candidate unchanged");

		ShaderConfiguration::DocumentDisk duplicateBinding = document;
		duplicateBinding.properties.push_back(MakeRenderPassProperty("enabled-copy", "OverlayPass", ShaderConfiguration::RenderPassPropertyType::Enabled, "bool", "true"));
		checks.Check(!RenderPass::ValidateConfigurationProperties(duplicateBinding, basePasses, error), "duplicate render pass bindings are rejected");
		std::vector<RenderPass::RenderPassDisk> duplicateCandidate = basePasses;
		checks.Check(!RenderPass::ApplyConfigurationProperties(duplicateBinding, duplicateCandidate, error) && duplicateCandidate[0].enabled, "duplicate binding rejection leaves the candidate unchanged");

		RenderPass::RenderPassDisk secondPass = basePass;
		secondPass.id = "SecondPass";
		const std::vector<RenderPass::RenderPassDisk> groupedBasePasses = {basePass, secondPass};
		std::vector<RenderPass::RenderPassDisk> groupedPasses = groupedBasePasses;
		ShaderConfiguration::DocumentDisk groupedDocument{};
		ShaderConfiguration::PropertyDisk groupedEnabled = MakeRenderPassProperty("enabled-group", "OverlayPass", ShaderConfiguration::RenderPassPropertyType::EnabledMultiple, "bool", "false");
		groupedEnabled.renderPassIDs = {"OverlayPass", "SecondPass"};
		groupedDocument.properties.push_back(groupedEnabled);
		checks.Check(RenderPass::ApplyConfigurationProperties(groupedDocument, groupedPasses, error), "multiple enabled property applies");
		checks.Check(!groupedPasses[0].enabled && !groupedPasses[1].enabled, "multiple enabled property fans out to every target");

		ShaderConfiguration::PropertyDisk groupedDownscale = MakeRenderPassProperty("downscale-group", "OverlayPass", ShaderConfiguration::RenderPassPropertyType::DownscalePowerOfTwoMultiple, "int", "8");
		groupedDownscale.renderPassIDs = {"OverlayPass", "SecondPass"};
		groupedDocument.properties.push_back(groupedDownscale);
		groupedPasses = groupedBasePasses;
		checks.Check(RenderPass::ApplyConfigurationProperties(groupedDocument, groupedPasses, error), "multiple downscale property applies");
		checks.Check(groupedPasses[0].resolution.downscaleFactor == 8 && groupedPasses[1].resolution.downscaleFactor == 8, "multiple downscale property fans out to every pass");
		checks.Check(groupedPasses[0].runtimeResources[0].texture.resolution.downscaleFactor == 8 && groupedPasses[1].runtimeResources[0].texture.resolution.downscaleFactor == 8, "multiple downscale property updates every runtime texture");

		RenderPass::RenderPassDisk chainPass = basePass;
		chainPass.id = "ChainPass";
		chainPass.type = RenderPass::RenderPassType::DownsampleUpsample;
		chainPass.chainDownsampleFactor = 2;
		std::vector<RenderPass::RenderPassDisk> chainPasses = {chainPass};
		ShaderConfiguration::DocumentDisk chainDownscaleDocument{};
		chainDownscaleDocument.properties.push_back(MakeRenderPassProperty("chain-downscale", "ChainPass", ShaderConfiguration::RenderPassPropertyType::DownscalePowerOfTwo, "int", "8"));
		checks.Check(RenderPass::ApplyConfigurationProperties(chainDownscaleDocument, chainPasses, error), "downscale property applies to downsample upsample chain");
		checks.Check(chainPasses[0].chainDownsampleFactor == 8 && chainPasses[0].resolution.mode == ShaderResource::ResolutionMode::Inherit, "chain downscale property changes chain factor without changing final output resolution");
		chainDownscaleDocument.properties[0].value = "1";
		checks.Check(!RenderPass::ApplyConfigurationProperties(chainDownscaleDocument, chainPasses, error), "invalid unit factor is rejected for downsample upsample chain");
		checks.Check(chainPasses[0].chainDownsampleFactor == 8 && chainPasses[0].resolution.mode == ShaderResource::ResolutionMode::Inherit, "invalid chain factor leaves pass unchanged");

		//configuration chains support the same deep factors as the render-pass editor.
		for (int factor = 32; factor <= 1024; factor *= 2)
		{
			chainDownscaleDocument.properties[0].value = std::to_string(factor);
			checks.Check(RenderPass::ApplyConfigurationProperties(chainDownscaleDocument, chainPasses, error), "deep chain factor applies");
			checks.Check(chainPasses[0].chainDownsampleFactor == factor && chainPasses[0].resolution.mode == ShaderResource::ResolutionMode::Inherit, "deep chain preserves full output resolution");
		}

		chainDownscaleDocument.properties[0].value = "2048";
		checks.Check(!RenderPass::ApplyConfigurationProperties(chainDownscaleDocument, chainPasses, error), "chain rejects factors above 1024");

		RenderPass::RenderPassDisk ordinaryPass = basePass;
		ordinaryPass.id = "OrdinaryPass";
		chainPasses.push_back(ordinaryPass);
		chainDownscaleDocument.properties[0].renderPassIDs = {"ChainPass", "OrdinaryPass"};
		chainDownscaleDocument.properties[0].renderPassPropertyType = ShaderConfiguration::RenderPassPropertyType::DownscalePowerOfTwoMultiple;
		chainDownscaleDocument.properties[0].value = "64";
		checks.Check(!RenderPass::ApplyConfigurationProperties(chainDownscaleDocument, chainPasses, error), "mixed group rejects factors above the ordinary pass limit");
		checks.Check(chainPasses[0].chainDownsampleFactor == 1024, "rejected mixed group preserves the original chain factor");

		ShaderConfiguration::DocumentDisk invalidGroup = groupedDocument;
		invalidGroup.properties[0].renderPassIDs = {"OverlayPass", "MissingPass"};
		groupedPasses = groupedBasePasses;
		checks.Check(!RenderPass::ApplyConfigurationProperties(invalidGroup, groupedPasses, error), "invalid multiple target is rejected");
		checks.Check(groupedPasses[0].enabled && groupedPasses[1].enabled && groupedPasses[0].resolution.mode == ShaderResource::ResolutionMode::Inherit, "invalid multiple target leaves all effective passes unchanged");

		ShaderConfiguration::DocumentDisk overlappingGroups{};
		ShaderConfiguration::PropertyDisk overlappingSingle = MakeRenderPassProperty("enabled-single", "OverlayPass", ShaderConfiguration::RenderPassPropertyType::Enabled, "bool", "false");
		ShaderConfiguration::PropertyDisk overlappingMultiple = groupedEnabled;
		overlappingGroups.properties = {overlappingSingle, overlappingMultiple};
		groupedPasses = groupedBasePasses;
		checks.Check(!RenderPass::ApplyConfigurationProperties(overlappingGroups, groupedPasses, error), "overlapping single and multiple bindings are rejected");
		checks.Check(groupedPasses[0].enabled && groupedPasses[1].enabled, "overlapping binding rejection leaves all passes unchanged");

		const nlohmann::ordered_json groupedJson = groupedEnabled;
		const ShaderConfiguration::PropertyDisk roundTrippedGrouped = groupedJson.get<ShaderConfiguration::PropertyDisk>();
		checks.Check(roundTrippedGrouped.renderPassIDs == groupedEnabled.renderPassIDs && roundTrippedGrouped.renderPassID == "OverlayPass", "multiple target IDs survive property JSON round-trip with legacy scalar ID");
		return checks.failureCount == 0;
	}

	bool CheckPresetsAndCategories(ExpansionChecks& checks, const std::string& testDirectory)
	{
		ShaderConfiguration::PropertyDisk* configuredValue = FindProperty("CONFIGURED_VALUE");
		checks.Check(configuredValue != nullptr, "configured shader property remains available for preset checks");
		if (!configuredValue)
			return false;

		configuredValue->value = "31";
		std::string error;
		checks.Check(DatabaseShaderConfigurations::SavePreset(error), "save the default preset values");
		const std::string defaultPresetID = DatabaseShaderConfigurations::GetDocument().presetID;
		const auto appliedSnapshot = DatabaseShaderConfigurations::GetAppliedConfiguration();

		checks.Check(DatabaseShaderConfigurations::CreatePreset("Performance", error), "create a Performance preset");
		configuredValue = FindProperty("CONFIGURED_VALUE");
		if (configuredValue)
			configuredValue->value = "17";
		checks.Check(DatabaseShaderConfigurations::SavePreset(error), "save Performance preset values");
		const std::string performancePresetID = DatabaseShaderConfigurations::GetDocument().presetID;

		checks.Check(DatabaseShaderConfigurations::SelectPreset(defaultPresetID, error), "select default preset");
		configuredValue = FindProperty("CONFIGURED_VALUE");
		checks.Check(configuredValue && configuredValue->value == "31", "default preset restores value 31");
		checks.Check(DatabaseShaderConfigurations::SelectPreset(performancePresetID, error), "select Performance preset");
		configuredValue = FindProperty("CONFIGURED_VALUE");
		checks.Check(configuredValue && configuredValue->value == "17", "Performance preset restores value 17");
		checks.Check(DatabaseShaderConfigurations::CreatePreset("Performance Copy", error), "create a preset using the selected values");
		const std::string createdPresetID = DatabaseShaderConfigurations::GetDocument().presetID;
		checks.Check(!createdPresetID.empty() && createdPresetID != performancePresetID, "created preset receives an independent stable ID");
		checks.Check(DatabaseShaderConfigurations::RenamePreset(createdPresetID, "Performance Copy Renamed", error), "rename created preset without changing its ID");
		checks.Check(DatabaseShaderConfigurations::GetDocument().presetID == createdPresetID, "renaming a preset retains its stable ID");
		checks.Check(DatabaseShaderConfigurations::GetAppliedConfiguration() == appliedSnapshot, "preset editing does not publish a new applied snapshot");

		const std::string configurationDirectory = ShaderInjectorIO::GetShaderConfigurationDirectory();
		const std::string catalogPath = ShaderInjectorIO::GetShaderConfigurationsPath();
		std::string catalogText;
		checks.Check(ShaderInjectorIO::ReadTextFile(catalogPath, catalogText), "read saved configuration catalog");
		checks.Check(catalogText.find("\"value\"") == std::string::npos, "catalog properties do not persist editable values");
		const std::string presetDirectory = ShaderInjectorIO::GetShaderConfigurationPresetsDirectory();
		std::string presetPath = ShaderInjectorIO::JoinPath(presetDirectory, ShaderConfiguration::GetPresetFileName(performancePresetID));
		ShaderConfiguration::PresetDisk performancePreset;
		checks.Check(ShaderConfiguration::LoadPresetJson(presetPath, performancePreset), "load persisted Performance preset");
		checks.Check(performancePreset.values[configuredValue ? configuredValue->id : std::string()] == "17", "preset file stores the actual configured value");
		checks.Check(ShaderInjectorIO::DirectoryExists(configurationDirectory), "configuration preset storage directory exists");

		const std::string mainPath = ShaderInjectorIO::JoinPath(testDirectory, "Main.hlsl");
		std::string sourceBeforeCategory;
		checks.Check(ShaderInjectorIO::ReadTextFile(mainPath, sourceBeforeCategory), "read shader before category authoring");
		checks.Check(sourceBeforeCategory.find("[CONFIG CATEGORY]") == std::string::npos, "fixture starts without authored category metadata");
		checks.Check(DatabaseShaderConfigurations::AddCategory("Expansion Group", error), "add source category");
		configuredValue = FindProperty("CONFIGURED_VALUE");
		checks.Check(configuredValue && DatabaseShaderConfigurations::SetPropertyCategory(configuredValue->id, "Expansion Group", error), "author category metadata on a shader property");
		std::string sourceWithCategory;
		checks.Check(ShaderInjectorIO::ReadTextFile(mainPath, sourceWithCategory), "read source after category authoring");
		if (configuredValue)
		{
			checks.Check(ContainsLine(sourceWithCategory, "#define CONFIGURED_VALUE 1"), "category authoring leaves the configured define line unchanged");
			checks.Check(sourceWithCategory.find("[CONFIG CATEGORY]: Expansion Group") != std::string::npos, "category authoring writes the source metadata tag");
			std::string sourceWithoutCategory = sourceWithCategory;
			const size_t categoryStart = sourceWithoutCategory.rfind("// [CONFIG CATEGORY]: Expansion Group");
			if (categoryStart != std::string::npos)
			{
				const size_t categoryEnd = sourceWithoutCategory.find('\n', categoryStart);
				if (categoryEnd != std::string::npos)
					sourceWithoutCategory.erase(categoryStart, categoryEnd - categoryStart + 1);
			}
			checks.Check(sourceWithoutCategory == sourceBeforeCategory, "category authoring changes only the metadata comment bytes");
			checks.Check(DatabaseShaderConfigurations::RenameCategory("Expansion Group", "Renamed Group", error), "rename category metadata");
			checks.Check(DatabaseShaderConfigurations::RemoveCategory("Renamed Group", error), "remove category and restore Uncategorized grouping");
		}

		ShaderConfiguration::PropertyDisk renderPassProperty{};
		checks.Check(DatabaseShaderConfigurations::AddRenderPassProperty(
			"Overlay enabled",
			"ExpansionTestPass",
			ShaderConfiguration::RenderPassPropertyType::Enabled,
			"false",
			"test property",
			"Uncategorized",
			error), "add a custom render pass enabled property");
		checks.Check(DatabaseShaderConfigurations::AddRenderPassProperty(
			"Overlay downscale",
			"ExpansionTestPass",
			ShaderConfiguration::RenderPassPropertyType::DownscalePowerOfTwo,
			"4",
			"test property",
			"Uncategorized",
			error), "add a custom render pass downscale property");
		checks.Check(DatabaseShaderConfigurations::AddRenderPassProperty(
			"Grouped overlay enabled",
			std::vector<std::string>{"ExpansionSecondPass", "ExpansionThirdPass"},
			ShaderConfiguration::RenderPassPropertyType::EnabledMultiple,
			"true",
			"test grouped property",
			"Uncategorized",
			error), "add a custom multiple render pass property");
		for (const ShaderConfiguration::PropertyDisk& property : DatabaseShaderConfigurations::GetEditableDocument().properties)
		{
			if (property.propertyType == ShaderConfiguration::PropertyType::RenderPass && property.renderPassPropertyType == ShaderConfiguration::RenderPassPropertyType::Enabled)
				renderPassProperty = property;
		}
		checks.Check(!renderPassProperty.id.empty(), "custom render pass binding has a stable property ID");
		checks.Check(DatabaseShaderConfigurations::ApplyChanges().succeeded, "apply custom render pass definitions");
		checks.Check(DatabaseShaderConfigurations::ReloadProperties(), "reload configuration with custom render pass properties");
		const auto& reloadedProperties = DatabaseShaderConfigurations::GetEditableDocument().properties;
		checks.Check(std::any_of(reloadedProperties.begin(), reloadedProperties.end(), [](const ShaderConfiguration::PropertyDisk& property)
			{ return property.propertyType == ShaderConfiguration::PropertyType::RenderPass && property.renderPassID == "ExpansionTestPass"; }), "custom render pass definitions survive metadata reload");
		checks.Check(std::any_of(reloadedProperties.begin(), reloadedProperties.end(), [](const ShaderConfiguration::PropertyDisk& property)
			{ return property.propertyType == ShaderConfiguration::PropertyType::RenderPass && property.renderPassPropertyType == ShaderConfiguration::RenderPassPropertyType::EnabledMultiple && property.renderPassIDs == std::vector<std::string>{"ExpansionSecondPass", "ExpansionThirdPass"}; }), "multiple target metadata survives catalog reload");
		checks.Check(DatabaseShaderConfigurations::SelectPreset(performancePresetID, error), "select saved preset after metadata reload");
		const auto& selectedProperties = DatabaseShaderConfigurations::GetEditableDocument().properties;
		checks.Check(std::any_of(selectedProperties.begin(), selectedProperties.end(), [](const ShaderConfiguration::PropertyDisk& property)
			{ return property.propertyType == ShaderConfiguration::PropertyType::RenderPass && property.renderPassID == "ExpansionTestPass" && property.value == "false"; }), "preset selection retains the custom enabled definition and value");
		checks.Check(std::any_of(selectedProperties.begin(), selectedProperties.end(), [](const ShaderConfiguration::PropertyDisk& property)
			{ return property.propertyType == ShaderConfiguration::PropertyType::RenderPass && property.renderPassID == "ExpansionTestPass" && property.value == "4"; }), "preset selection retains the custom downscale definition and value");
		checks.Check(std::any_of(selectedProperties.begin(), selectedProperties.end(), [](const ShaderConfiguration::PropertyDisk& property)
			{ return property.propertyType == ShaderConfiguration::PropertyType::RenderPass && property.renderPassPropertyType == ShaderConfiguration::RenderPassPropertyType::EnabledMultiple && property.renderPassIDs.size() == 2 && property.value == "true"; }), "preset selection retains the grouped property targets and value");
		return checks.failureCount == 0;
	}
}

namespace
{
	void CheckCategoryPersistenceFailures(ExpansionChecks& checks, const std::string& testDirectory)
	{
		std::string error;
		checks.Check(DatabaseShaderConfigurations::AddCategory("Rollback Group", error), "create category for persistence failure checks");
		const auto property = FindProperty("CONFIGURED_VALUE");

		if (!property)
		{
			checks.Check(false, "find property for category persistence checks");
			return;
		}

		const std::string propertyID = property->id;
		checks.Check(DatabaseShaderConfigurations::SetPropertyCategory(propertyID, "Rollback Group", error), "assign category before failure checks");
		std::string originalSource;
		const std::string sourcePath = ShaderInjectorIO::JoinPath(testDirectory, "Main.hlsl");
		checks.Check(ShaderInjectorIO::ReadTextFile(sourcePath, originalSource), "capture category source before failed writes");
		const auto originalDocument = DatabaseShaderConfigurations::GetEditableDocument();

		SetConfigurationTestSaveFailure(true);
		checks.Check(!DatabaseShaderConfigurations::AddCategory("Must Not Persist", error), "category creation reports failed catalog save");
		checks.Check(!DatabaseShaderConfigurations::SetPropertyCategory(propertyID, "Uncategorized", error), "property category reports failed catalog save");
		checks.Check(!DatabaseShaderConfigurations::RenameCategory("Rollback Group", "Failed Rename", error), "category rename reports failed catalog save");
		checks.Check(!DatabaseShaderConfigurations::RemoveCategory("Rollback Group", error), "category removal reports failed catalog save");
		SetConfigurationTestSaveFailure(false);

		const nlohmann::ordered_json currentDocument = DatabaseShaderConfigurations::GetEditableDocument();
		checks.Check(currentDocument == nlohmann::ordered_json(originalDocument), "failed category operations restore catalog metadata");
		std::string restoredSource;
		checks.Check(ShaderInjectorIO::ReadTextFile(sourcePath, restoredSource) && restoredSource == originalSource, "failed category operations restore HLSL metadata bytes");
		checks.Check(!DatabaseShaderConfigurations::AddRenderPassProperty("Invalid binding", "ExpansionTestPass", static_cast<ShaderConfiguration::RenderPassPropertyType>(42), "1", "", "Uncategorized", error), "unsupported render pass binding is rejected");
	}
}

bool RunShaderConfigurationExpansionTests(const std::string& testDirectory)
{
	ExpansionChecks checks;
	CheckCategorySource(checks);
	CheckRenderPassOverlay(checks);
	CheckPresetsAndCategories(checks, testDirectory);
	CheckCategoryPersistenceFailures(checks, testDirectory);
	return checks.failureCount == 0;
}
