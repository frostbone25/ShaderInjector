#include "IO/ShaderInjectorIO.h"
#include "ShaderConfiguration/CompileConfiguration.h"
#include "ShaderConfiguration/ShaderSourceDependencies.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace
{
	bool WriteSourceFixture(const std::filesystem::path& rootDirectory, const std::string& relativePath, const std::string& sourceText)
	{
		const std::filesystem::path sourcePath = rootDirectory / std::filesystem::u8path(relativePath);
		std::error_code error;
		std::filesystem::create_directories(sourcePath.parent_path(), error);

		if (error)
			return false;

		return ShaderInjectorIO::WriteTextFile(ShaderInjectorIO::PathToUTF8(sourcePath), sourceText);
	}

	void CheckDependency(bool condition, const std::string& message, int& failureCount)
	{
		if (condition)
			return;

		std::cerr << "FAIL: " << message << '\n';
		++failureCount;
	}

	ShaderConfiguration::DocumentDisk MakeDependencyDocument(const std::string& value)
	{
		ShaderConfiguration::DocumentDisk document;
		ShaderConfiguration::PropertyDisk property{};
		property.id = "dependency-property";
		property.name = "DEPENDENCY_PROPERTY";
		property.sourcePath = "Properties/Settings.hlsl";
		property.valueType = "int";
		property.defaultValue = "1";
		property.value = value;
		document.properties.push_back(property);
		return document;
	}
}

bool RunShaderSourceDependenciesTests(const std::string& testDirectory)
{
	int failureCount = 0;
	const std::filesystem::path modifiedShadersDirectory = std::filesystem::u8path(testDirectory);
	const std::filesystem::path sharedIncludesDirectory = modifiedShadersDirectory / "Includes";

	WriteSourceFixture(modifiedShadersDirectory, "Properties/Settings.hlsl", "#define DEPENDENCY_PROPERTY 1\n");
	WriteSourceFixture(modifiedShadersDirectory, "Roots/Main.hlsl", "#include \"../Wrappers/First.hlsl\"\n");
	WriteSourceFixture(modifiedShadersDirectory, "Roots/Unrelated.hlsl", "float4 main() : SV_Target { return 0; }\n");
	WriteSourceFixture(modifiedShadersDirectory, "Roots/Conditional.hlsl", "#if 0\n#include \"../Properties/Settings.hlsl\"\n#endif\n");
	WriteSourceFixture(modifiedShadersDirectory, "Roots/Compact.hlsl", "#include/* comment */\"../Properties/Settings.hlsl\"\r\n");
	WriteSourceFixture(modifiedShadersDirectory, "Roots/Spliced.hlsl", "#inc\\\nlude \"../Properties/Settings.hlsl\"\r\n");
	WriteSourceFixture(modifiedShadersDirectory, "Wrappers/First.hlsl", "/* root wrapper */ #include \\\n\t\"Second.hlsl\" // wrapped include\n");
	WriteSourceFixture(modifiedShadersDirectory, "Wrappers/Second.hlsl", "#include <../Properties/Settings.hlsl>\n");
	WriteSourceFixture(modifiedShadersDirectory, "Cycles/A.hlsl", "#include \"B.hlsl\"\n");
	WriteSourceFixture(modifiedShadersDirectory, "Cycles/B.hlsl", "#include \"A.hlsl\"\n#include \"../Properties/Settings.hlsl\"\n");
	WriteSourceFixture(modifiedShadersDirectory, "Roots/Missing.hlsl", "#include \"DoesNotExist.hlsl\"\n");
	WriteSourceFixture(modifiedShadersDirectory, "Roots/Dynamic.hlsl", "#include DEPENDENCY_INCLUDE\n");
	WriteSourceFixture(sharedIncludesDirectory, "SharedWrapper.hlsl", "#include \"../Properties/Settings.hlsl\"\n");
	WriteSourceFixture(modifiedShadersDirectory, "Roots/Shared.hlsl", "#include \"SharedWrapper.hlsl\"\n");
	WriteSourceFixture(modifiedShadersDirectory, "Roots/SharedAgain.hlsl", "#include \"SharedWrapper.hlsl\"\n");

	ShaderConfiguration::CompileConfiguration initialConfiguration(MakeDependencyDocument("1"));
	ShaderConfiguration::CompileConfiguration startupConfiguration(MakeDependencyDocument("1"));
	const std::string initialGlobalFingerprint = initialConfiguration.GetFingerprint();
	CheckDependency(initialConfiguration.GetSourceFingerprint("Roots/Main.hlsl") == initialGlobalFingerprint, "unprepared root lookup immediately returns the global fingerprint", failureCount);
	CheckDependency(initialConfiguration.GetSourceFingerprint("Roots/Unknown.hlsl") == initialGlobalFingerprint, "unknown root lookup falls back without scanning", failureCount);

	const std::vector<std::string> shaderSourcePaths = {
		"Roots/Main.hlsl",
		"Roots/Unrelated.hlsl",
		"Roots/Conditional.hlsl",
		"Roots/Compact.hlsl",
		"Roots/Spliced.hlsl",
		"Cycles/A.hlsl",
		"Roots/Shared.hlsl",
		"Roots/SharedAgain.hlsl",
		"Roots/Missing.hlsl",
		"Roots/Dynamic.hlsl"};
	initialConfiguration.PrepareSourceFingerprints(shaderSourcePaths);

	const std::string mainFingerprint = initialConfiguration.GetSourceFingerprint("Roots/Main.hlsl");
	const std::string unrelatedFingerprint = initialConfiguration.GetSourceFingerprint("Roots/Unrelated.hlsl");
	CheckDependency(!mainFingerprint.empty(), "transitive wrapper chain includes configured property", failureCount);
	CheckDependency(unrelatedFingerprint.empty(), "unrelated root keeps the legacy empty fingerprint", failureCount);
	CheckDependency(!initialConfiguration.GetSourceFingerprint("Roots/Conditional.hlsl").empty(), "disabled literal include branch is included conservatively", failureCount);
	CheckDependency(!initialConfiguration.GetSourceFingerprint("Roots/Compact.hlsl").empty(), "comments between directive and literal include are handled", failureCount);
	CheckDependency(!initialConfiguration.GetSourceFingerprint("Roots/Spliced.hlsl").empty(), "line splicing before directive parsing is handled", failureCount);
	CheckDependency(!initialConfiguration.GetSourceFingerprint("Cycles/A.hlsl").empty(), "include cycles terminate and retain reachable properties", failureCount);
	CheckDependency(!initialConfiguration.GetSourceFingerprint("Roots/Shared.hlsl").empty(), "shared include search root resolves transitive property source", failureCount);
	CheckDependency(initialConfiguration.GetSourceFingerprint("ROOTS/MAIN.HLSL") == mainFingerprint, "source lookup normalizes case and slash conventions", failureCount);
	CheckDependency(initialConfiguration.GetSourceFingerprint("Roots/Unknown.hlsl") == initialGlobalFingerprint, "unprepared root retains conservative global fallback", failureCount);

	WriteSourceFixture(modifiedShadersDirectory, "Wrappers/First.hlsl", "#include DYNAMIC_INCLUDE\n");
	CheckDependency(initialConfiguration.GetSourceFingerprint("Roots/Main.hlsl") == mainFingerprint, "prepared lookup stays filesystem-free after source files change", failureCount);
	WriteSourceFixture(modifiedShadersDirectory, "Wrappers/First.hlsl", "/* root wrapper */ #include \\\n\t\"Second.hlsl\" // wrapped include\n");

	ShaderConfiguration::DocumentDisk changedDocument = MakeDependencyDocument("2");
	ShaderConfiguration::CompileConfiguration changedConfiguration(changedDocument);
	changedConfiguration.PrepareSourceFingerprints(shaderSourcePaths, std::make_shared<const ShaderConfiguration::CompileConfiguration>(startupConfiguration));
	CheckDependency(changedConfiguration.GetSourceFingerprint("Roots/Main.hlsl") != mainFingerprint, "changed included property fans out to its consumer", failureCount);
	CheckDependency(changedConfiguration.GetSourceFingerprint("Roots/Unrelated.hlsl") == startupConfiguration.GetFingerprint(), "unrelated root preserves prior startup global cache identity", failureCount);
	CheckDependency(startupConfiguration.GetSourceFingerprint("Roots/Unrelated.hlsl") == startupConfiguration.GetFingerprint(), "preparation does not mutate the old configuration cache", failureCount);
	ShaderConfiguration::DocumentDisk metadataOnlyDocument = MakeDependencyDocument("1");
	metadataOnlyDocument.properties.front().description = "metadata edit";
	metadataOnlyDocument.properties.front().category = "Different category";
	ShaderConfiguration::CompileConfiguration metadataOnlyConfiguration(metadataOnlyDocument);
	WriteSourceFixture(modifiedShadersDirectory, "Wrappers/First.hlsl", "#include DYNAMIC_INCLUDE\n");
	metadataOnlyConfiguration.PrepareSourceFingerprints(shaderSourcePaths, std::make_shared<const ShaderConfiguration::CompileConfiguration>(initialConfiguration));
	CheckDependency(metadataOnlyConfiguration.GetSourceFingerprint("Roots/Main.hlsl") == mainFingerprint, "metadata-only edits do not change source fingerprint", failureCount);
	WriteSourceFixture(modifiedShadersDirectory, "Wrappers/First.hlsl", "/* root wrapper */ #include \\\n\t\"Second.hlsl\" // wrapped include\n");

	ShaderConfiguration::CompileConfiguration changedWithPreparedPrevious(changedDocument);
	changedWithPreparedPrevious.PrepareSourceFingerprints(shaderSourcePaths, std::make_shared<const ShaderConfiguration::CompileConfiguration>(initialConfiguration));
	CheckDependency(changedWithPreparedPrevious.GetSourceFingerprint("Roots/Unrelated.hlsl").empty(), "unchanged semantic roots retain their prepared prior fingerprint", failureCount);
	CheckDependency(initialConfiguration.GetSourceFingerprint("Roots/Unrelated.hlsl").empty(), "preparing a new configuration does not mutate its published predecessor", failureCount);
	CheckDependency(changedConfiguration.GetSourceFingerprint("Roots/Missing.hlsl") == changedConfiguration.GetFingerprint(), "unresolved literal include falls back to global fingerprint", failureCount);
	CheckDependency(changedConfiguration.GetSourceFingerprint("Roots/Dynamic.hlsl") == changedConfiguration.GetFingerprint(), "dynamic include falls back to global fingerprint", failureCount);

	ShaderConfiguration::DocumentDisk removedPropertyDocument;
	ShaderConfiguration::CompileConfiguration removedPropertyConfiguration(removedPropertyDocument);
	removedPropertyConfiguration.PrepareSourceFingerprints(shaderSourcePaths);
	CheckDependency(removedPropertyConfiguration.GetSourceFingerprint("Roots/Main.hlsl").empty(), "removed property leaves no source-specific fingerprint", failureCount);

	ShaderConfiguration::ShaderSourceDependencyScanStats scanStats;
	const std::vector<ShaderConfiguration::ShaderSourceDependencyResult> sharedResults = ShaderConfiguration::CollectShaderSourceDependencyBatch(
		{"Roots/Shared.hlsl", "Roots/SharedAgain.hlsl"},
		ShaderInjectorIO::GetModifiedShadersDirectory(),
		ShaderInjectorIO::GetModifiedShadersIncludesDirectory(),
		&scanStats);
	CheckDependency(sharedResults.size() == 2 && sharedResults[0].resolved && sharedResults[1].resolved, "shared include roots resolve as a batch", failureCount);
	CheckDependency(scanStats.sourceFilesRead == 4, "batch preparation parses a shared include source only once", failureCount);

	const std::filesystem::path externalRootDirectory = modifiedShadersDirectory.parent_path() / "ExternalShaderRoot";
	WriteSourceFixture(externalRootDirectory, "Outside.hlsl", "#include \"LocalProperty.hlsl\"\n");
	WriteSourceFixture(externalRootDirectory, "LocalProperty.hlsl", "#define DEPENDENCY_PROPERTY 1\n");
	ShaderConfiguration::DocumentDisk externalRootDocument = MakeDependencyDocument("3");
	externalRootDocument.properties.front().sourcePath = ShaderInjectorIO::PathToUTF8(externalRootDirectory / "LocalProperty.hlsl");
	ShaderConfiguration::CompileConfiguration externalRootConfiguration(externalRootDocument);
	externalRootConfiguration.PrepareSourceFingerprints({ShaderInjectorIO::PathToUTF8(externalRootDirectory / "Outside.hlsl")});
	CheckDependency(!externalRootConfiguration.GetSourceFingerprint(ShaderInjectorIO::PathToUTF8(externalRootDirectory / "Outside.hlsl")).empty(), "absolute root outside ModifiedShaders resolves parent-relative includes", failureCount);

	return failureCount == 0;
}
