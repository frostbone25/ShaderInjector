#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <d3d12shader.h>

#include "RenderPass/RenderPassGraph.h"
#include "RenderPass/DownsampleUpsampleStagePlan.h"
#include "RenderPass/RenderPassTexturePool.h"
#include "ShaderModelDetector.h"
#include "ShaderAnalysis/ShaderAnalysisDisk.h"
#include "ShaderTemplates.h"
#include "StringHelper.h"
#include "ModifiedShader/ModifiedShader.h"
#include "ModifiedShader/ModifiedShaderRecapture.h"
#include "ShaderConfiguration/CompileConfiguration.h"

namespace TestIO
{
	void SetRenderPassJson(std::string json);
	void SetModifiedShaderJson(std::string json);
	const std::string& GetModifiedShaderJson();
	int GetModifiedShaderWriteCount();
}

namespace DatabaseShaderConfigurations
{
	std::shared_ptr<const ShaderConfiguration::CompileConfiguration> GetAppliedConfiguration()
	{
		return {};
	}
}

namespace ShaderConfiguration
{
	std::string CompileConfiguration::GetSourceFingerprint(const std::string&) const
	{
		return {};
	}

	const std::string& CompileConfiguration::GetFingerprint() const
	{
		static const std::string emptyFingerprint;
		return emptyFingerprint;
	}
}

namespace
{
	void Check(bool condition, const char* description);
	RenderPass::LogicalResourceBindingDisk RuntimeBinding(const char* id, RenderPass::ResourceAccess access);

	void TestDownsampleUpsampleStagePlan()
	{
		std::vector<RenderPassRuntime::DownsampleUpsampleStage> stages;
		Check(RenderPassRuntime::BuildDownsampleUpsampleStagePlan(1919, 1079, 8, stages), "odd-sized chain plan is accepted");
		Check(stages.size() == 6, "factor eight produces three down and three up stages");
		Check(stages[0].width == 959 && stages[0].height == 539, "first downsample floors odd dimensions");
		Check(stages[2].width == 239 && stages[2].height == 134, "factor endpoint floors odd dimensions");
		Check(stages[3].width == 479 && stages[3].height == 269, "upsample reverses to the preceding exact extent");
		Check(stages.back().width == 1919 && stages.back().height == 1079 && stages.back().upsample,
			  "last upsample restores the full source extent");
		Check(!RenderPassRuntime::BuildDownsampleUpsampleStagePlan(1, 1, 8, stages), "one by one source is rejected");
		Check(!RenderPassRuntime::BuildDownsampleUpsampleStagePlan(1920, 1080, 3, stages), "non-power-of-two factor is rejected");
		Check(!RenderPassRuntime::BuildDownsampleUpsampleStagePlan(1920, 1080, 2048, stages), "factor above maximum is rejected");
	}

	void TestDownsampleUpsampleGraph()
	{
		RenderPass::RenderPassDisk source;
		source.id = "ChainSource";
		source.name = source.id;
		source.type = RenderPass::RenderPassType::Custom;
		source.operation = RenderPass::PassOperation::Custom;
		source.event = {RenderPass::EventType::ModifiedShader, "ChainAnchor"};
		source.outputs.push_back(RuntimeBinding("ChainSource:Texture", RenderPass::ResourceAccess::RenderTarget));
		RenderPass::RuntimeResourceDefinitionDisk sourceTexture;
		sourceTexture.id = "ChainSource:Texture";
		sourceTexture.texture.dimension = ShaderResource::TextureDimension::Texture2D;
		source.runtimeResources.push_back(sourceTexture);

		RenderPass::RenderPassDisk chain;
		chain.id = "Chain";
		chain.name = chain.id;
		chain.type = RenderPass::RenderPassType::DownsampleUpsample;
		chain.operation = RenderPass::PassOperation::DownsampleUpsample;
		chain.event = {RenderPass::EventType::RenderPass, "ChainSource"};
		chain.timing = RenderPass::timingAfter;
		chain.inputs.push_back(RuntimeBinding("ChainSource:Texture", RenderPass::ResourceAccess::ShaderResource));
		chain.outputs.push_back(RuntimeBinding("Chain:Output", RenderPass::ResourceAccess::RenderTarget));
		RenderPass::RuntimeResourceDefinitionDisk outputTexture;
		outputTexture.id = "Chain:Output";
		outputTexture.texture.dimension = ShaderResource::TextureDimension::Texture2D;
		outputTexture.texture.matchReferenceTexture = true;
		outputTexture.texture.allowRenderTarget = true;
		chain.runtimeResources.push_back(outputTexture);
		auto graph = RenderPassGraph::Compile({source, chain});
		Check(graph.nodes[1].valid, "downsample/upsample runtime graph is accepted");

		chain.chainDownsampleFactor = 3;
		graph = RenderPassGraph::Compile({source, chain});
		Check(!graph.nodes[1].valid, "downsample/upsample graph rejects non-power-of-two factors");
		chain.chainDownsampleFactor = 8;
		chain.outputs.front().resourceId = "ChainSource:Texture";
		graph = RenderPassGraph::Compile({source, chain});
		Check(!graph.nodes[1].valid, "downsample/upsample graph rejects input/output aliasing");
	}

	void Check(bool condition, const char* description)
	{
		if (!condition)
		{
			std::cerr << "FAIL: " << description << '\n';
			std::exit(1);
		}
	}

	RenderPass::LogicalResourceBindingDisk RuntimeBinding(
		const char* id, RenderPass::ResourceAccess access)
	{
		RenderPass::LogicalResourceBindingDisk binding;
		binding.resourceId = id;
		binding.origin = ShaderResource::ResourceOrigin::Runtime;
		binding.access = access;
		return binding;
	}

	std::vector<RenderPass::RenderPassDisk> MakeCopyMipReplacement()
	{
		RenderPass::RenderPassDisk copy;
		copy.id = "Copy";
		copy.name = copy.id;
		copy.operation = RenderPass::PassOperation::Copy;
		copy.event = {RenderPass::EventType::RenderPass, "Mip"};
		RenderPass::LogicalResourceBindingDisk gameSource;
		gameSource.origin = ShaderResource::ResourceOrigin::Game;
		gameSource.access = RenderPass::ResourceAccess::CopySource;
		gameSource.gameResourceViewType = RenderPass::GameResourceViewType::UnorderedAccess;
		copy.inputs.push_back(gameSource);
		copy.outputs.push_back(RuntimeBinding("Copy:Texture", RenderPass::ResourceAccess::CopyDestination));
		RenderPass::RuntimeResourceDefinitionDisk texture;
		texture.id = "Copy:Texture";
		copy.runtimeResources.push_back(texture);

		RenderPass::RenderPassDisk mip;
		mip.id = "Mip";
		mip.name = mip.id;
		mip.type = RenderPass::RenderPassType::MipChain;
		mip.executionMode = RenderPass::ExecutionMode::Compute;
		mip.event = {RenderPass::EventType::RenderPass, "Replacement"};
		mip.inputs.push_back(RuntimeBinding("Copy:Texture", RenderPass::ResourceAccess::ShaderResource));
		mip.sourceTextureShaderRegister = 1;

		RenderPass::RenderPassDisk replacement;
		replacement.id = "Replacement";
		replacement.name = replacement.id;
		replacement.type = RenderPass::RenderPassType::ReplacementComputeShader;
		replacement.event = {RenderPass::EventType::ModifiedShader, "ReflectionEnvironment"};
		replacement.inputs.push_back(RuntimeBinding("Copy:Texture", RenderPass::ResourceAccess::ShaderResource));
		//Deliberately not in execution order, matching directory enumeration.
		return {replacement, mip, copy};
	}

	void CheckSequence(const RenderPassGraph::Compilation& graph, const std::vector<size_t>& expected,
					   size_t boundary = 0)
	{
		const auto plan = graph.executionPlans.find("ReflectionEnvironment");
		Check(plan != graph.executionPlans.end(), "execution plan exists");
		Check(plan->second.executionOrders[boundary] == expected, "copy, mip, consumer order is preserved");
		Check(plan->second.mipChainOrders[boundary].empty(), "runtime mip is never batched as a game-SRV mip");
	}

	void TestGraph()
	{
		auto passes = MakeCopyMipReplacement();
		auto graph = RenderPassGraph::Compile(passes);
		Check(graph.diagnostics.empty(), "runtime copy/mip/replacement graph is valid");
		CheckSequence(graph, {2, 1, 0});
		Check(RenderPass::FindMipChainRuntimeSource(passes[1]) == &passes[1].inputs.front(),
			  "runtime source wins over configured game t1");
		Check(RenderPass::MipChainOutputResourceId(passes[1]) == "Mip:MipChain", "stable generated output id");

		passes[0].inputs.front().resourceId = "Mip:MipChain";
		graph = RenderPassGraph::Compile(passes);
		Check(graph.diagnostics.empty(), "consumer can explicitly bind the generated mip output");
		CheckSequence(graph, {2, 1, 0});

		passes[1].enabled = false;
		graph = RenderPassGraph::Compile(passes);
		Check(!graph.nodes[0].valid, "disabled mip producer cannot supply its generated output");

		passes = MakeCopyMipReplacement();
		passes[1].inputs.front().resourceId.clear();
		graph = RenderPassGraph::Compile(passes);
		Check(!graph.nodes[1].valid, "empty runtime mip input does not fall back to a game register");
		Check(!graph.nodes[0].valid && !graph.nodes[2].valid,
			  "invalid mip event excludes dependent copy and replacement instead of reading stale textures");

		passes = MakeCopyMipReplacement();
		passes[1].inputs.push_back(passes[1].inputs.front());
		graph = RenderPassGraph::Compile(passes);
		Check(!graph.nodes[1].valid, "ambiguous runtime mip inputs are rejected");

		passes = MakeCopyMipReplacement();
		passes[0].type = RenderPass::RenderPassType::Custom;
		passes[0].executionMode = RenderPass::ExecutionMode::Compute;
		passes[0].timing = RenderPass::timingAfter;
		graph = RenderPassGraph::Compile(passes);
		Check(graph.diagnostics.empty(), "runtime mip production is allowed after an anchor");
		CheckSequence(graph, {2, 1, 0}, 1);

		passes = MakeCopyMipReplacement();
		passes[0].type = RenderPass::RenderPassType::ReplacementPixelShader;
		passes[1].executionMode = RenderPass::ExecutionMode::FullscreenPixel;
		graph = RenderPassGraph::Compile(passes);
		Check(graph.diagnostics.empty(), "pixel runtime mip graph uses the same scheduling");
		CheckSequence(graph, {2, 1, 0});

		passes = MakeCopyMipReplacement();
		passes[0].inputs.front().resourceId = "Mip:MipChain";
		passes[0].inputs.front().temporalView = ShaderResource::TemporalView::Previous;
		graph = RenderPassGraph::Compile(passes);
		Check(!graph.nodes[0].valid, "execution-local mip output cannot be used as temporal history");

		RenderPass::RenderPassDisk nativeMip;
		nativeMip.id = "NativeMip";
		nativeMip.type = RenderPass::RenderPassType::MipChain;
		nativeMip.event = {RenderPass::EventType::ModifiedShader, "ReflectionEnvironment"};
		graph = RenderPassGraph::Compile({nativeMip});
		const auto& nativePlan = graph.executionPlans.at("ReflectionEnvironment");
		Check(nativePlan.mipChainOrders[0] == std::vector<size_t>{0}, "game-SRV mips retain native batching");
		Check(nativePlan.graphicsBoundaryMask == 1 && nativePlan.computeBoundaryMask == 0,
			  "graphics-only mips do not activate compute tracking");

		passes = MakeCopyMipReplacement();
		RenderPass::RenderPassDisk laterConsumer;
		laterConsumer.id = "LaterConsumer";
		laterConsumer.name = laterConsumer.id;
		laterConsumer.type = RenderPass::RenderPassType::Custom;
		laterConsumer.executionMode = RenderPass::ExecutionMode::Compute;
		laterConsumer.event = {RenderPass::EventType::ModifiedShader, "LaterLighting"};
		laterConsumer.inputs.push_back(RuntimeBinding("Mip:MipChain", RenderPass::ResourceAccess::ShaderResource));
		passes.push_back(laterConsumer);
		graph = RenderPassGraph::Compile(passes);
		Check(!graph.nodes.back().valid, "cross-anchor current mip input requires explicit opt-in");
		passes.back().inputs.front().allowCrossAnchor = true;
		graph = RenderPassGraph::Compile(passes);
		Check(graph.nodes.back().valid, "explicit cross-anchor generated mip input is accepted");
		passes.back().inputs.front().resourceId = "Copy:Texture";
		graph = RenderPassGraph::Compile(passes);
		Check(graph.nodes.back().valid, "explicit cross-anchor declared output is accepted with a same-frame runtime guard");
		passes.back().inputs.front().allowCrossAnchor = false;
		graph = RenderPassGraph::Compile(passes);
		Check(!graph.nodes.back().valid, "cross-anchor declared output still requires explicit opt-in");
	}

	void TestScopedInputOverrides()
	{
		using namespace RenderPassTexturePool;
		TextureView generated;
		generated.description.mipLevels = 11;
		TextureView resolved;
		{
			ScopedInputTextureOverrides scope;
			OverrideInputTexture("Copy:Texture", ShaderResource::TemporalView::Current, generated);
			GetInputTexture("Copy:Texture", ShaderResource::TemporalView::Current, resolved);
			Check(resolved.description.mipLevels == 11, "input resolves execution-local mip view");
			Check(!GetTexture("Copy:Texture", ShaderResource::TemporalView::Current, resolved),
				  "output allocation lookup never resolves the input override");
			{
				ScopedInputTextureOverrides nestedScope;
				OverrideInputTexture("Copy:Texture", ShaderResource::TemporalView::Current, {});
				Check(!GetInputTexture("Copy:Texture", ShaderResource::TemporalView::Current, resolved),
					  "failed generation blocks stale input resolution");
				Check(resolved.description.mipLevels != 11, "failed generation does not expose prior mips");
			}
			GetInputTexture("Copy:Texture", ShaderResource::TemporalView::Current, resolved);
			Check(resolved.description.mipLevels == 11, "nested execution restores outer override");
		}
		Check(!GetInputTexture("Copy:Texture", ShaderResource::TemporalView::Current, resolved),
			  "generated view does not leak into the next execution");
	}

	void TestInheritedBindingSerialization()
	{
		RenderPass::RenderPassDisk renderPass;
		renderPass.id = "InheritedBindings";
		nlohmann::ordered_json json = renderPass;
		Check(json["inheritedGameBindings"]["shaderResources"].get<bool>(),
			  "new render passes inherit game shader resources by default");
		Check(json["inheritedGameBindings"]["constantBuffers"].get<bool>(),
			  "new render passes inherit game constant buffers by default");

		json.erase("inheritedGameBindings");
		const RenderPass::RenderPassDisk legacyPass = json.get<RenderPass::RenderPassDisk>();
		Check(legacyPass.inheritedGameBindings.shaderResources &&
				  legacyPass.inheritedGameBindings.constantBuffers,
			  "existing render-pass JSON enables both inheritance categories");

		renderPass.inheritedGameBindings.shaderResources = false;
		renderPass.inheritedGameBindings.constantBuffers = false;
		const RenderPass::RenderPassDisk selfContainedPass =
			nlohmann::ordered_json(renderPass).get<RenderPass::RenderPassDisk>();
		Check(!selfContainedPass.inheritedGameBindings.shaderResources &&
				  !selfContainedPass.inheritedGameBindings.constantBuffers,
			  "disabled inheritance categories round-trip through JSON");
	}

	void TestImportedDdsMigration()
	{
		RenderPass::RenderPassDisk saved;
		saved.id = "ImportedTexturePass";
		saved.name = saved.id;
		RenderPass::LogicalResourceBindingDisk runtimeInput;
		runtimeInput.origin = ShaderResource::ResourceOrigin::Runtime;
		runtimeInput.resourceId = "EarlierPass:Color";
		runtimeInput.shaderRegister = 2;
		saved.inputs.push_back(runtimeInput);

		nlohmann::ordered_json legacy = saved;
		legacy["schemaVersion"] = 10;
		legacy["shaderResources"] = nlohmann::ordered_json::array({
			{{"resourceId", "sky.dds"}, {"hlslName", "SkyTexture"},
			 {"shaderRegister", 7}, {"registerSpace", 1}}
		});
		TestIO::SetRenderPassJson(legacy.dump());
		RenderPass::RenderPassDisk loaded;
		Check(RenderPass::LoadJson("test-render-pass.json", loaded), "legacy DDS render pass loads");
		Check(loaded.inputs.size() == 2 && loaded.inputs.front().resourceId == runtimeInput.resourceId,
			"legacy DDS input follows existing runtime inputs");
		const auto& imported = loaded.inputs.back();
		Check(imported.origin == ShaderResource::ResourceOrigin::Disk &&
			imported.access == RenderPass::ResourceAccess::ShaderResource &&
			imported.resourceId == "sky.dds" && imported.hlslName == "SkyTexture" &&
			imported.shaderRegister == 7 && imported.registerSpace == 1,
			"legacy DDS identity and binding are preserved");
		const nlohmann::ordered_json upgraded = loaded;
		Check(!upgraded.contains("shaderResources") && upgraded["inputs"].size() == 2 &&
			upgraded["schemaVersion"] == RenderPass::currentSchemaVersion,
			"saved render pass uses only unified inputs");

		legacy["inputs"].push_back(imported);
		TestIO::SetRenderPassJson(legacy.dump());
		Check(RenderPass::LoadJson("test-render-pass.json", loaded) && loaded.inputs.size() == 2,
			"mixed-format DDS input is not duplicated");
	}

	void TestSamplerSerialization()
	{
		RenderPass::RenderPassDisk renderPass;
		RenderPass::SamplerStateDisk sampler;
		sampler.hlslName = "HistorySampler";
		sampler.shaderRegister = 7;
		sampler.registerSpace = 2;
		sampler.filter = D3D12_FILTER_COMPARISON_ANISOTROPIC;
		sampler.addressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		sampler.maximumAnisotropy = 16;
		sampler.comparisonSampler = true;
		sampler.borderColor = {1.0f, 0.5f, 0.25f, 1.0f};
		renderPass.samplers.push_back(sampler);

		const RenderPass::RenderPassDisk restored =
			nlohmann::ordered_json(renderPass).get<RenderPass::RenderPassDisk>();
		Check(restored.samplers.size() == 1, "render-pass sampler collection round-trips through JSON");
		Check(restored.samplers.front().hlslName == "HistorySampler" &&
				  restored.samplers.front().shaderRegister == 7 &&
				  restored.samplers.front().registerSpace == 2,
			  "render-pass sampler binding round-trips through JSON");
		Check(restored.samplers.front().comparisonSampler &&
				  restored.samplers.front().maximumAnisotropy == 16,
			  "render-pass sampler state round-trips through JSON");
	}

	void TestTemporalHistoryGraph()
	{
		RenderPass::RenderPassDisk consumer;
		consumer.id = "HistoryConsumer";
		consumer.name = consumer.id;
		consumer.event = {RenderPass::EventType::ModifiedShader, "ReflectionEnvironment"};
		RenderPass::LogicalResourceBindingDisk previousHistory =
			RuntimeBinding("DepthHistory:History", RenderPass::ResourceAccess::ShaderResource);
		previousHistory.temporalView = ShaderResource::TemporalView::Previous;
		consumer.inputs.push_back(previousHistory);

		RenderPass::RenderPassDisk history;
		history.id = "DepthHistory";
		history.name = history.id;
		history.type = RenderPass::RenderPassType::TemporalHistory;
		history.event = {RenderPass::EventType::ModifiedShader, "ReflectionEnvironment"};
		RenderPass::ConfigureTemporalHistoryPass(history);

		Check(history.inputs.size() == 1 &&
				  history.inputs.front().origin == ShaderResource::ResourceOrigin::Game &&
				  history.inputs.front().access == RenderPass::ResourceAccess::CopySource,
			  "temporal history configures one game copy source");
		Check(history.outputs.size() == 1 &&
				  history.outputs.front().resourceId == "DepthHistory:History" &&
				  history.outputs.front().access == RenderPass::ResourceAccess::CopyDestination,
			  "temporal history configures one stable copy destination");
		Check(history.runtimeResources.size() == 1 &&
				  history.runtimeResources.front().texture.lifetime == ShaderResource::ResourceLifetime::History,
			  "temporal history owns a double-buffered History texture");

		auto passes = std::vector<RenderPass::RenderPassDisk>{consumer, history};
		auto graph = RenderPassGraph::Compile(passes);
		Check(graph.diagnostics.empty(), "previous-frame history graph is valid");
		Check(graph.executionPlans.at("ReflectionEnvironment").executionOrders[0] ==
				  std::vector<size_t>{0, 1},
			  "previous-frame consumer may execute before the current history update");

		passes.front().inputs.front().temporalView = ShaderResource::TemporalView::Current;
		graph = RenderPassGraph::Compile(passes);
		Check(graph.diagnostics.empty(), "current-frame history graph is valid");
		Check(graph.executionPlans.at("ReflectionEnvironment").executionOrders[0] ==
				  std::vector<size_t>{1, 0},
			  "current-frame consumer waits for the history producer");
	}

	void TestRuntimeTextureReuse()
	{
		RenderPass::RenderPassDisk first;
		first.id = "First";
		first.name = first.id;
		first.executionMode = RenderPass::ExecutionMode::Compute;
		first.event = {RenderPass::EventType::ModifiedShader, "ReflectionEnvironment"};
		first.runtimeResources.push_back({"First:Texture", "First Texture"});
		first.outputs.push_back(RuntimeBinding("First:Texture", RenderPass::ResourceAccess::UnorderedAccess));

		RenderPass::RenderPassDisk second;
		second.id = "Second";
		second.name = second.id;
		second.executionMode = RenderPass::ExecutionMode::Compute;
		second.event = {RenderPass::EventType::RenderPass, first.id};
		second.timing = RenderPass::timingAfter;
		second.inputs.push_back(RuntimeBinding("First:Texture", RenderPass::ResourceAccess::ShaderResource));
		second.runtimeResources.push_back({"Second:Texture", "Second Texture"});
		second.outputs.push_back(RuntimeBinding("Second:Texture", RenderPass::ResourceAccess::UnorderedAccess));

		RenderPass::RenderPassDisk third;
		third.id = "Third";
		third.name = third.id;
		third.executionMode = RenderPass::ExecutionMode::Compute;
		third.event = {RenderPass::EventType::RenderPass, second.id};
		third.timing = RenderPass::timingAfter;
		third.inputs.push_back(RuntimeBinding("Second:Texture", RenderPass::ResourceAccess::ShaderResource));
		RenderPass::RuntimeResourceDefinitionDisk reused;
		reused.id = "Third:Texture";
		reused.name = "Reused First Texture";
		reused.reuseFromResourceId = "First:Texture";
		third.runtimeResources.push_back(reused);
		third.outputs.push_back(RuntimeBinding(reused.id.c_str(), RenderPass::ResourceAccess::UnorderedAccess));

		RenderPass::RenderPassDisk fourth;
		fourth.id = "Fourth";
		fourth.name = fourth.id;
		fourth.executionMode = RenderPass::ExecutionMode::Compute;
		fourth.event = {RenderPass::EventType::RenderPass, third.id};
		fourth.timing = RenderPass::timingAfter;
		fourth.inputs.push_back(RuntimeBinding("Third:Texture", RenderPass::ResourceAccess::ShaderResource));
		reused.id = "Fourth:Texture";
		reused.reuseFromResourceId = "Second:Texture";
		fourth.runtimeResources.push_back(reused);
		fourth.outputs.push_back(RuntimeBinding(reused.id.c_str(), RenderPass::ResourceAccess::UnorderedAccess));

		auto passes = std::vector<RenderPass::RenderPassDisk>{fourth, third, first, second};
		auto graph = RenderPassGraph::Compile(passes);
		Check(graph.diagnostics.empty(), "ping-pong reuse graph is valid");
		Check(graph.executionPlans.at("ReflectionEnvironment").executionOrders[0] ==
				  std::vector<size_t>({2, 3, 1, 0}),
			  "reused texture writes follow their earlier reads");

		const RenderPass::RenderPassDisk restored =
			nlohmann::ordered_json(third).get<RenderPass::RenderPassDisk>();
		Check(restored.runtimeResources.front().reuseFromResourceId == "First:Texture",
			  "texture reuse survives JSON serialization");

		passes[1].inputs.push_back(RuntimeBinding("First:Texture", RenderPass::ResourceAccess::ShaderResource));
		graph = RenderPassGraph::Compile(passes);
		Check(!graph.nodes[1].valid, "a pass cannot read the texture it reuses for its output");

		passes[1].inputs.pop_back();
		passes[2].runtimeResources.front().texture.lifetime = ShaderResource::ResourceLifetime::History;
		graph = RenderPassGraph::Compile(passes);
		Check(!graph.nodes[1].valid, "history textures cannot be aliased by a transient output");
	}

	void TestShaderModelProfiles()
	{
		const Globals::ShaderModel previousPixelModel = Globals::gPixelShaderModel;
		const Globals::ShaderModel previousComputeModel = Globals::gComputeShaderModel;
		const Globals::ShaderModel previousAmplificationModel = Globals::gAmplificationShaderModel;
		const Globals::ShaderModel previousMeshModel = Globals::gMeshShaderModel;
		Globals::gPixelShaderModel = Globals::ShaderModel::ShaderModel5_0;
		Globals::gComputeShaderModel = Globals::ShaderModel::ShaderModel6_3;
		Globals::gAmplificationShaderModel = Globals::ShaderModel::ShaderModel5_0;
		Globals::gMeshShaderModel = Globals::ShaderModel::ShaderModel6_6;

		Check(StringHelper::ShaderProfileForType(ShaderTarget::PixelShader) == "ps_5_0",
			  "pixel profile uses the configured Shader Model");
		Check(StringHelper::ShaderProfileForType(ShaderTarget::ComputeShader) == "cs_6_3",
			  "compute profile uses its independent Shader Model");
		Check(StringHelper::ShaderProfileForType(ShaderTarget::AmplificationShader) == "as_6_5",
			  "amplification profiles cannot fall below Shader Model 6.5");
		Check(StringHelper::ShaderProfileForType(ShaderTarget::MeshShader) == "ms_6_6",
			  "mesh profile uses its independent Shader Model");
		Check(StringHelper::ShaderModelFromValue(999, Globals::ShaderModel::ShaderModel6_6) ==
				  Globals::ShaderModel::ShaderModel6_6,
			  "invalid serialized Shader Models use their fallback");

		Globals::gPixelShaderModel = previousPixelModel;
		Globals::gComputeShaderModel = previousComputeModel;
		Globals::gAmplificationShaderModel = previousAmplificationModel;
		Globals::gMeshShaderModel = previousMeshModel;
	}

	void TestShaderModelDetection()
	{
		std::vector<uint8_t> bytecode(48, 0);
		auto writeUInt32 = [&bytecode](size_t offset, uint32_t value)
		{
			for (size_t byteIndex = 0; byteIndex < sizeof(value); ++byteIndex)
				bytecode[offset + byteIndex] = static_cast<uint8_t>(value >> (byteIndex * 8));
		};

		writeUInt32(0, 0x43425844); //DXBC
		writeUInt32(24, static_cast<uint32_t>(bytecode.size()));
		writeUInt32(28, 1);
		writeUInt32(32, 36);
		writeUInt32(36, 0x4c495844); //DXIL
		writeUInt32(40, 4);
		writeUInt32(44, 0x00000051); //Pixel Shader 5.1

		ShaderModelDetector::ObserveShaderBytecode(ShaderTarget::PixelShader, bytecode.data(), bytecode.size());
		Globals::ShaderModel detectedModel = Globals::ShaderModel::ShaderModel6_6;
		Check(ShaderModelDetector::TryGetDetectedShaderModel(ShaderTarget::PixelShader, detectedModel) &&
				  detectedModel == Globals::ShaderModel::ShaderModel5_1,
			  "DXBC container shader model is detected from its program token");
		Check(ShaderModelDetector::GetEffectiveShaderModel(
				  ShaderTarget::PixelShader, Globals::ShaderModel::ShaderModel6_6) == Globals::ShaderModel::ShaderModel5_1,
			  "automatic shader model selection uses the detected model");

		writeUInt32(44, (static_cast<uint32_t>(D3D12_SHVER_MESH_SHADER) << 16) | 0x65);
		ShaderModelDetector::ObserveShaderBytecode(ShaderTarget::MeshShader, bytecode.data(), bytecode.size());
		Check(ShaderModelDetector::TryGetDetectedShaderModel(ShaderTarget::MeshShader, detectedModel) &&
				  detectedModel == Globals::ShaderModel::ShaderModel6_5,
			  "mesh shader model is detected from its DXIL program token");

		writeUInt32(44, (static_cast<uint32_t>(D3D12_SHVER_AMPLIFICATION_SHADER) << 16) | 0x66);
		ShaderModelDetector::ObserveShaderBytecode(ShaderTarget::AmplificationShader, bytecode.data(), bytecode.size());
		Check(ShaderModelDetector::TryGetDetectedShaderModel(ShaderTarget::AmplificationShader, detectedModel) &&
				  detectedModel == Globals::ShaderModel::ShaderModel6_6,
			  "amplification shader model is detected from its DXIL program token");
	}

	void TestMeshShaderTemplate()
	{
		ShaderAnalysis::ShaderAnalysisDisk original{};
		original.succeeded = true;
		original.shaderStage = D3D12_SHVER_MESH_SHADER;
		original.executionProperties.geometryOutputTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		original.executionProperties.threadGroupSizeX = 32;

		ShaderAnalysis::SignatureParameterDisk position{};
		position.semanticName = "SV_Position";
		position.systemValueType = D3D_NAME_POSITION;
		position.componentType = D3D_REGISTER_COMPONENT_FLOAT32;
		position.mask = 15;
		original.outputParameters.push_back(position);

		ShaderAnalysis::SignatureParameterDisk texCoord{};
		texCoord.semanticName = "TEXCOORD";
		texCoord.componentType = D3D_REGISTER_COMPONENT_FLOAT32;
		texCoord.mask = 3;
		texCoord.registerIndex = 1;
		original.outputParameters.push_back(texCoord);

		ShaderAnalysis::SignatureParameterDisk primitiveId{};
		primitiveId.semanticName = "SV_PrimitiveID";
		primitiveId.systemValueType = D3D_NAME_PRIMITIVE_ID;
		primitiveId.componentType = D3D_REGISTER_COMPONENT_UINT32;
		primitiveId.mask = 1;
		original.patchConstantParameters.push_back(primitiveId);

		const std::string source = ShaderTemplates::BuildMeshShaderSourceTemplate(original);
		Check(source.find("float2 value1 : TEXCOORD0") != std::string::npos, "mesh template preserves vertex output semantics");
		Check(source.find("uint value0 : SV_PrimitiveID0") != std::string::npos, "mesh template preserves primitive output semantics");
		Check(source.find("[numthreads(32, 1, 1)]") != std::string::npos, "mesh template preserves the captured thread group size");
	}

	void TestModifiedShaderRecapture()
	{
		ModifiedShader::ModifiedShaderPackageDisk package;
		package.id = "directional-light";
		package.name = "My Directional Light";
		package.enabled = false;
		package.shaderType = ShaderTarget::PixelShader;
		package.shaderProfile = "ps_6_6";
		package.shaderEntryPoint = "CustomMain";
		package.sourceFile = "test-source.hlsl";
		package.compiledBlobFile = "test-compiled.blob";

		ModifiedShader::ModifiedShaderTargetDisk firstVersion;
		firstVersion.name = "Version 1.005";
		firstVersion.targetApplication = "game.exe";
		firstVersion.gameVersion = "1.005";
		firstVersion.knownShaderBytecodeHashes = {"ABCD", "1234"};
		firstVersion.originalShaderBytecodeLength = "40";
		firstVersion.shaderAnalysis.succeeded = true;
		firstVersion.shaderAnalysis.interfaceSignatureHash = "old-interface";
		package.targets.push_back(firstVersion);
		auto secondVersion = firstVersion;
		secondVersion.name = "Version 1.004";
		secondVersion.knownShaderBytecodeHashes = {"9876"};
		package.targets.push_back(secondVersion);

		nlohmann::ordered_json legacyJson = package;
		legacyJson["retiredPackageField"] = "remove on reserialize";
		legacyJson["targets"][0]["shaderAnalysis"]["retiredAnalysisField"] = 99;
		TestIO::SetModifiedShaderJson(legacyJson.dump());
		Check(ModifiedShader::LoadJson("test-modified-shader.json", package), "load existing fingerprint for recapture");
		package.compiledBlob = {7, 8, 9};
		const auto preservedBlob = package.compiledBlob;
		const auto preservedSourcePath = package.sourcePath;
		const auto preservedCompiledPath = package.compiledBlobPath;

		ShaderAnalysis::ShaderAnalysisDisk currentAnalysis;
		currentAnalysis.succeeded = true;
		currentAnalysis.interfaceSignatureHash = "fresh-interface";
		Check(ModifiedShaderRecapture::UpdateFingerprint(package, 0xABCD, 120, currentAnalysis), "recapture known shader hash");
		Check(package.targets.size() == 2 && package.targets[0].shaderAnalysis.interfaceSignatureHash == "fresh-interface" && package.targets[0].originalShaderBytecodeLength == "120", "refresh original analysis without duplicating an existing fingerprint");
		Check(package.targets[0].name == firstVersion.name && package.targets[0].gameVersion == firstVersion.gameVersion && package.targets[0].targetApplication == firstVersion.targetApplication && package.targets[0].knownShaderBytecodeHashes == firstVersion.knownShaderBytecodeHashes, "preserve version metadata and known hash aliases");
		Check(package.targets[1].shaderAnalysis.interfaceSignatureHash == "old-interface", "retain fingerprints for other game versions");
		Check(ModifiedShader::WriteJson(package), "rewrite updated fingerprint JSON");
		const auto rewrittenJson = nlohmann::ordered_json::parse(TestIO::GetModifiedShaderJson());
		Check(!rewrittenJson.contains("retiredPackageField") && !rewrittenJson["targets"][0]["shaderAnalysis"].contains("retiredAnalysisField"), "reserialize with the current schema and remove retired fields");
		Check(TestIO::GetModifiedShaderWriteCount() == 1, "recapture only writes the fingerprint JSON, not HLSL or compiled bytecode");
		Check(package.name == "My Directional Light" && !package.enabled && package.shaderEntryPoint == "CustomMain" && package.sourcePath == preservedSourcePath && package.compiledBlobPath == preservedCompiledPath && package.compiledBlob == preservedBlob, "preserve user settings, source paths, and compiled replacement memory");

		Check(ModifiedShaderRecapture::UpdateFingerprint(package, 0x4321, 80, currentAnalysis) && package.targets.size() == 3, "append a manually selected new-version shader while retaining old fingerprints");
		const nlohmann::ordered_json beforeFailure = package;
		currentAnalysis.succeeded = false;
		Check(!ModifiedShaderRecapture::UpdateFingerprint(package, 0xABCD, 120, currentAnalysis) && nlohmann::ordered_json(package) == beforeFailure && package.compiledBlob == preservedBlob, "failed analysis does not damage the previous fingerprint or compiled bytecode");
		currentAnalysis.succeeded = true;
		currentAnalysis.shaderStage = D3D12_SHVER_COMPUTE_SHADER;
		Check(!ModifiedShaderRecapture::UpdateFingerprint(package, 0xABCD, 120, currentAnalysis) && nlohmann::ordered_json(package) == beforeFailure, "recapture rejects original bytecode from the wrong shader stage");

		ShaderTarget::ShaderTargetDisk savedPipeline;
		savedPipeline.psHash = "ABCD";
		savedPipeline.rootSignatureHash = "root";
		savedPipeline.rasterizerStateHash = "outside-light-volume";
		savedPipeline.renderTargetFormats = "10";
		auto capturedPipeline = savedPipeline;
		Check(ModifiedShaderRecapture::MatchesGraphicsPipelineState(savedPipeline, capturedPipeline), "recapture recognizes the same graphics pipeline variant");
		capturedPipeline.rasterizerStateHash = "inside-light-volume";
		Check(!ModifiedShaderRecapture::MatchesGraphicsPipelineState(savedPipeline, capturedPipeline), "shared shader bytecode does not make different cull states interchangeable");
		capturedPipeline = savedPipeline;
		capturedPipeline.renderTargetFormats = "2";
		Check(!ModifiedShaderRecapture::MatchesGraphicsPipelineState(savedPipeline, capturedPipeline), "recapture rejects a different render-target format");
		capturedPipeline = savedPipeline;
		capturedPipeline.rootSignatureHash = "other-root";
		Check(!ModifiedShaderRecapture::MatchesGraphicsPipelineState(savedPipeline, capturedPipeline), "recapture rejects a different root signature");
	}

	void TestInstalledRenderPasses(const std::filesystem::path& directory)
	{
		std::vector<RenderPass::RenderPassDisk> passes;
		size_t importedTextureCount = 0;
		for (const auto& entry : std::filesystem::recursive_directory_iterator(directory))
		{
			if (!entry.is_regular_file() || entry.path().extension() != ".json")
				continue;
			std::ifstream input(entry.path());
			Check(input.good(), "installed render-pass JSON can be opened");
			nlohmann::ordered_json json;
			input >> json;
			passes.push_back(json.get<RenderPass::RenderPassDisk>());
			if (json.contains("shaderResources") && json["shaderResources"].is_array())
			{
				TestIO::SetRenderPassJson(json.dump());
				RenderPass::RenderPassDisk loaded;
				Check(RenderPass::LoadJson("test-render-pass.json", loaded), "installed DDS pass loads");
				for (const auto& oldResource : json["shaderResources"])
				{
					const auto matching = std::find_if(loaded.inputs.begin(), loaded.inputs.end(), [&](const auto& candidate)
					{
						return candidate.origin == ShaderResource::ResourceOrigin::Disk &&
							candidate.resourceId == oldResource.value("resourceId", std::string()) &&
							candidate.hlslName == oldResource.value("hlslName", std::string()) &&
							candidate.shaderRegister == oldResource.value("shaderRegister", 0u) &&
							candidate.registerSpace == oldResource.value("registerSpace", 0u);
					});
					Check(matching != loaded.inputs.end(), "installed DDS input preserves its binding");
					++importedTextureCount;
				}
			}
		}
		Check(!passes.empty(), "installed render passes were found");
		const auto graph = RenderPassGraph::Compile(passes);
		size_t reusedCount = 0;
		size_t ssgiCount = 0;
		for (size_t index = 0; index < passes.size(); ++index)
		{
			if (passes[index].name.rfind("SSGI_", 0) == 0 ||
				passes[index].name.rfind("Custom_PrePass_DepthNormals", 0) == 0)
			{
				if (!graph.nodes[index].valid)
					std::cerr << passes[index].name << ": " << graph.nodes[index].error << '\n';
				Check(graph.nodes[index].valid, "installed SSGI and shared prepass are valid");
				++ssgiCount;
			}
			if (passes[index].enabled && passes[index].name.rfind("GameVersion1005_SSR_", 0) == 0)
			{
				if (!graph.nodes[index].valid)
					std::cerr << passes[index].name << ": " << graph.nodes[index].error << '\n';
				Check(graph.nodes[index].valid, "installed active SSR render pass is valid");
			}
			for (const auto& resource : passes[index].runtimeResources)
				if (!resource.reuseFromResourceId.empty())
					++reusedCount;
		}
		Check(ssgiCount == 21, "expected SSGI render passes and shared prepass are loaded");
		Check(reusedCount >= 22, "expected SSGI texture reuse links are loaded");
		std::cout << "Installed SSGI graph valid: " << ssgiCount
				  << " passes, " << reusedCount << " reused textures, "
				  << importedTextureCount << " imported DDS bindings.\n";
	}
} //namespace

int main(int argc, char** argv)
{
	TestDownsampleUpsampleStagePlan();
	TestDownsampleUpsampleGraph();
	TestGraph();
	TestScopedInputOverrides();
	TestInheritedBindingSerialization();
	TestImportedDdsMigration();
	TestSamplerSerialization();
	TestTemporalHistoryGraph();
	TestRuntimeTextureReuse();
	TestShaderModelProfiles();
	TestShaderModelDetection();
	TestMeshShaderTemplate();
	TestModifiedShaderRecapture();
	if (argc > 1)
		TestInstalledRenderPasses(argv[1]);
	std::cout << "Render-pass graph, scoped-input, and shader-profile tests passed.\n";
}
