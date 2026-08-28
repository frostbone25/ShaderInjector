#include "RenderPass/RenderPassGraph.h"

#include <algorithm>
#include <functional>
#include <unordered_set>

namespace RenderPassGraph
{
	namespace
	{
		enum class VisitState : uint8_t
		{
			Unvisited,
			Visiting,
			Complete,
		};

		void AddDependency(CompiledNode& node, size_t dependencyIndex)
		{
			if (dependencyIndex == invalidNodeIndex || dependencyIndex == node.renderPassIndex)
				return;
			if (std::find(node.dependencies.begin(), node.dependencies.end(), dependencyIndex) == node.dependencies.end())
				node.dependencies.push_back(dependencyIndex);
		}

		bool HasImplementedBackend(
			const RenderPass::RenderPassDisk& renderPass,
			const CompiledNode& node)
		{
			switch (node.operation)
			{
			case RenderPass::PassOperation::Custom:
				return renderPass.type == RenderPass::RenderPassType::Custom &&
					(node.executionMode == RenderPass::ExecutionMode::FullscreenPixel ||
						node.executionMode == RenderPass::ExecutionMode::Compute);
			case RenderPass::PassOperation::Downsample:
			case RenderPass::PassOperation::UpsampleChain:
				return renderPass.type == RenderPass::RenderPassType::Custom &&
					(node.executionMode == RenderPass::ExecutionMode::FullscreenPixel ||
						node.executionMode == RenderPass::ExecutionMode::Compute);
				case RenderPass::PassOperation::MipChain:
					return renderPass.type == RenderPass::RenderPassType::MipChain &&
						(node.executionMode == RenderPass::ExecutionMode::FullscreenPixel ||
							node.executionMode == RenderPass::ExecutionMode::Compute);
				case RenderPass::PassOperation::ReplaceOriginal:
					return (renderPass.type == RenderPass::RenderPassType::ReplacementPixelShader &&
							node.executionMode == RenderPass::ExecutionMode::FullscreenPixel) ||
						(renderPass.type == RenderPass::RenderPassType::ReplacementComputeShader &&
							node.executionMode == RenderPass::ExecutionMode::Compute);
				case RenderPass::PassOperation::Copy:
					return renderPass.type == RenderPass::RenderPassType::Custom;
				case RenderPass::PassOperation::Resolve:
				case RenderPass::PassOperation::Automatic:
				default:
					return false;
			}
		}

		std::vector<size_t> StableTopologicalSort(
			const std::vector<RenderPass::RenderPassDisk>& renderPasses,
			const std::vector<CompiledNode>& nodes,
			const std::vector<size_t>& candidates,
			std::vector<std::string>& diagnostics)
		{
			std::unordered_set<size_t> candidateSet(candidates.begin(), candidates.end());
			std::unordered_map<size_t, size_t> indegrees;
			std::unordered_map<size_t, std::vector<size_t>> dependents;
			for (size_t candidateIndex : candidates)
			{
				indegrees[candidateIndex] = 0;
				for (size_t dependencyIndex : nodes[candidateIndex].dependencies)
				{
					if (candidateSet.find(dependencyIndex) == candidateSet.end())
						continue;
					++indegrees[candidateIndex];
					dependents[dependencyIndex].push_back(candidateIndex);
				}
			}

			std::vector<size_t> order;
			order.reserve(candidates.size());
			std::unordered_set<size_t> appended;
			while (order.size() < candidates.size())
			{
				size_t selectedIndex = invalidNodeIndex;
				for (size_t candidateIndex : candidates)
				{
					if (appended.find(candidateIndex) == appended.end() && !indegrees[candidateIndex])
					{
						selectedIndex = candidateIndex;
						break;
					}
				}
				if (selectedIndex == invalidNodeIndex)
					break;

				appended.insert(selectedIndex);
				order.push_back(selectedIndex);
				for (size_t dependentIndex : dependents[selectedIndex])
				{
					if (indegrees[dependentIndex])
						--indegrees[dependentIndex];
				}
			}

			if (order.size() != candidates.size())
			{
				for (size_t candidateIndex : candidates)
				{
					if (appended.find(candidateIndex) == appended.end())
					{
						diagnostics.push_back(
							"Dependency cycle excludes render pass " + renderPasses[candidateIndex].name + ".");
					}
				}
			}
			return order;
		}
	}

	Compilation Compile(const std::vector<RenderPass::RenderPassDisk>& renderPasses)
	{
		Compilation compilation{};
		compilation.nodes.resize(renderPasses.size());
		std::unordered_set<std::string> duplicateRenderPassIds;
		for (size_t renderPassIndex = 0; renderPassIndex < renderPasses.size(); ++renderPassIndex)
		{
			CompiledNode& node = compilation.nodes[renderPassIndex];
			node.renderPassIndex = renderPassIndex;
			node.executionMode = RenderPass::ResolveExecutionMode(renderPasses[renderPassIndex]);
			node.operation = RenderPass::ResolvePassOperation(renderPasses[renderPassIndex]);
			const auto inserted = compilation.renderPassIndices.emplace(renderPasses[renderPassIndex].id, renderPassIndex);
			if (!inserted.second)
			{
				duplicateRenderPassIds.insert(renderPasses[renderPassIndex].id);
				compilation.diagnostics.push_back("Duplicate render-pass id: " + renderPasses[renderPassIndex].id + ".");
			}
		}

		std::vector<VisitState> visitStates(renderPasses.size(), VisitState::Unvisited);
		std::function<bool(size_t)> resolveNode = [&](size_t renderPassIndex)
		{
			CompiledNode& node = compilation.nodes[renderPassIndex];
			const RenderPass::RenderPassDisk& renderPass = renderPasses[renderPassIndex];
			if (visitStates[renderPassIndex] == VisitState::Complete)
				return node.valid;
			if (visitStates[renderPassIndex] == VisitState::Visiting)
			{
				node.error = "Render-pass event dependency cycle.";
				compilation.diagnostics.push_back(node.error + " Pass=" + renderPass.name + ".");
				return false;
			}

			visitStates[renderPassIndex] = VisitState::Visiting;
			if (!renderPass.enabled)
				node.error = "Render pass is disabled.";
			else if (renderPass.id.empty() || duplicateRenderPassIds.find(renderPass.id) != duplicateRenderPassIds.end())
				node.error = "Render pass has an empty or duplicate id.";
			else if (!HasImplementedBackend(renderPass, node))
				node.error = "The selected operation and execution mode do not have a compatible runtime backend.";
			else if (renderPass.event.id.empty())
				node.error = "Render pass has no event anchor.";
			else if (renderPass.event.type == RenderPass::EventType::ModifiedShader)
			{
				node.valid = true;
				node.modifiedShaderId = renderPass.event.id;
				node.rootBoundary = (node.operation == RenderPass::PassOperation::MipChain &&
					!RenderPass::FindMipChainRuntimeSource(renderPass)) ||
					renderPass.timing != RenderPass::timingAfter
					? Boundary::Before
					: Boundary::After;
			}
			else
			{
				const auto parentIt = compilation.renderPassIndices.find(renderPass.event.id);
				if (parentIt == compilation.renderPassIndices.end() || parentIt->second == renderPassIndex)
					node.error = "Referenced render pass was not found or references itself.";
				else if (resolveNode(parentIt->second))
				{
					const CompiledNode& parent = compilation.nodes[parentIt->second];
					node.valid = true;
					node.parentRenderPassIndex = parentIt->second;
					node.modifiedShaderId = parent.modifiedShaderId;
					node.rootBoundary = parent.rootBoundary;
					if (renderPass.timing == RenderPass::timingBefore)
						AddDependency(compilation.nodes[parentIt->second], renderPassIndex);
					else
						AddDependency(node, parentIt->second);
				}
				else
					node.error = "Referenced render-pass event chain is invalid.";
			}

			if (node.valid && node.operation == RenderPass::PassOperation::MipChain &&
				!RenderPass::FindMipChainRuntimeSource(renderPass) &&
				node.rootBoundary == Boundary::After)
			{
				node.valid = false;
				node.error = "Mip-chain passes cannot execute after the anchor draw.";
			}
			visitStates[renderPassIndex] = VisitState::Complete;
			if (!node.valid && renderPass.enabled && !node.error.empty())
				compilation.diagnostics.push_back(node.error + " Pass=" + renderPass.name + ".");
			return node.valid;
		};

		for (size_t renderPassIndex = 0; renderPassIndex < renderPasses.size(); ++renderPassIndex)
			resolveNode(renderPassIndex);

		std::unordered_map<std::string, size_t> runtimeResourceOwners;
		for (size_t renderPassIndex = 0; renderPassIndex < renderPasses.size(); ++renderPassIndex)
		{
			for (const RenderPass::RuntimeResourceDefinitionDisk& definition : renderPasses[renderPassIndex].runtimeResources)
			{
				if (definition.id.empty())
					continue;
				const auto inserted = runtimeResourceOwners.emplace(definition.id, renderPassIndex);
				if (!inserted.second && inserted.first->second != renderPassIndex)
					compilation.diagnostics.push_back("Duplicate runtime-resource id: " + definition.id + ".");
			}
		}

		for (size_t renderPassIndex = 0; renderPassIndex < renderPasses.size(); ++renderPassIndex)
		{
			CompiledNode& node = compilation.nodes[renderPassIndex];
			if (!node.valid)
				continue;
			const RenderPass::RenderPassDisk& renderPass = renderPasses[renderPassIndex];
			if (const auto* mipSource = RenderPass::FindMipChainRuntimeSource(renderPass))
			{
				const size_t sourceCount = static_cast<size_t>(std::count_if(
					renderPass.inputs.begin(), renderPass.inputs.end(), [](const auto& input)
					{
						return input.origin == ShaderResource::ResourceOrigin::Runtime &&
							input.access == RenderPass::ResourceAccess::ShaderResource;
					}));
				if (sourceCount != 1 || mipSource->resourceId.empty())
				{
					node.valid = false;
					node.error = "A runtime mip chain requires exactly one selected runtime texture input.";
					compilation.diagnostics.push_back(node.error + " Pass=" + renderPass.name + ".");
					continue;
				}
			}
			const bool hasShaderInput = std::any_of(renderPass.inputs.begin(), renderPass.inputs.end(), [](const auto& input)
			{
				return input.origin == ShaderResource::ResourceOrigin::Runtime &&
					input.access == RenderPass::ResourceAccess::ShaderResource &&
					!input.resourceId.empty();
			});
			const bool hasCopyInput = hasShaderInput || std::any_of(
				renderPass.inputs.begin(), renderPass.inputs.end(), [](const auto& input)
				{
					return input.access == RenderPass::ResourceAccess::CopySource &&
						((input.origin == ShaderResource::ResourceOrigin::Runtime && !input.resourceId.empty()) ||
							input.origin == ShaderResource::ResourceOrigin::Game);
				});
			const bool hasRenderTargetOutput = std::any_of(renderPass.outputs.begin(), renderPass.outputs.end(), [](const auto& output)
			{
				return output.origin == ShaderResource::ResourceOrigin::Runtime &&
					output.access == RenderPass::ResourceAccess::RenderTarget &&
					!output.resourceId.empty();
			});
			const bool hasUnorderedAccessOutput = std::any_of(
				renderPass.outputs.begin(), renderPass.outputs.end(), [](const auto& output)
				{
					return output.origin == ShaderResource::ResourceOrigin::Runtime &&
						output.access == RenderPass::ResourceAccess::UnorderedAccess &&
						!output.resourceId.empty();
				});
			const bool hasCopyOutput = hasRenderTargetOutput || std::any_of(
				renderPass.outputs.begin(), renderPass.outputs.end(), [](const auto& output)
				{
					return output.origin == ShaderResource::ResourceOrigin::Runtime &&
						output.access == RenderPass::ResourceAccess::CopyDestination &&
						!output.resourceId.empty();
				});
			const bool requiresShaderInput = node.operation == RenderPass::PassOperation::Downsample ||
				node.operation == RenderPass::PassOperation::UpsampleChain;
			const bool requiresCopyInput = node.operation == RenderPass::PassOperation::Copy;
			const bool hasShaderOutput = node.executionMode == RenderPass::ExecutionMode::Compute
				? hasUnorderedAccessOutput
				: hasRenderTargetOutput;
			if (((requiresShaderInput && !hasShaderInput) ||
				(requiresCopyInput && !hasCopyInput) ||
				(requiresShaderInput && !hasShaderOutput) ||
				(requiresCopyInput && !hasCopyOutput)))
			{
				node.valid = false;
				node.error = "The operation requires a texture input and runtime texture output.";
				compilation.diagnostics.push_back(node.error + " Pass=" + renderPass.name + ".");
				continue;
			}
			const size_t runtimeRenderTargetCount = static_cast<size_t>(std::count_if(
				renderPass.outputs.begin(), renderPass.outputs.end(), [](const auto& output)
				{
					return output.origin == ShaderResource::ResourceOrigin::Runtime &&
						output.access == RenderPass::ResourceAccess::RenderTarget;
				}));
			if (runtimeRenderTargetCount > 1)
			{
				node.valid = false;
				node.error = "The current fullscreen backend supports one runtime render target per pass.";
				compilation.diagnostics.push_back(node.error + " Pass=" + renderPass.name + ".");
				continue;
			}
			for (const RenderPass::LogicalResourceBindingDisk& output : renderPass.outputs)
			{
				if (output.origin != ShaderResource::ResourceOrigin::Runtime || output.resourceId.empty())
					continue;
				const auto ownerIt = runtimeResourceOwners.find(output.resourceId);
				if (ownerIt == runtimeResourceOwners.end() || ownerIt->second != renderPassIndex)
				{
					node.valid = false;
					node.error = "A runtime output must reference a texture owned by the producing pass.";
					compilation.diagnostics.push_back(node.error + " Pass=" + renderPass.name + ".");
					break;
				}
			}
		}

		std::unordered_map<std::string, size_t> runtimeResourceProducers;
		for (size_t renderPassIndex = 0; renderPassIndex < renderPasses.size(); ++renderPassIndex)
		{
			if (!compilation.nodes[renderPassIndex].valid)
				continue;
			if (RenderPass::FindMipChainRuntimeSource(renderPasses[renderPassIndex]))
			{
				const std::string outputId = RenderPass::MipChainOutputResourceId(renderPasses[renderPassIndex]);
				const auto inserted = runtimeResourceProducers.emplace(outputId, renderPassIndex);
				if (!inserted.second && inserted.first->second != renderPassIndex)
					compilation.diagnostics.push_back("Runtime resource has multiple producers: " + outputId + ".");
			}
			for (const RenderPass::LogicalResourceBindingDisk& output : renderPasses[renderPassIndex].outputs)
			{
				if (output.origin != ShaderResource::ResourceOrigin::Runtime || output.resourceId.empty())
					continue;
				const auto inserted = runtimeResourceProducers.emplace(output.resourceId, renderPassIndex);
				if (!inserted.second && inserted.first->second != renderPassIndex)
					compilation.diagnostics.push_back("Runtime resource has multiple producers: " + output.resourceId + ".");
			}
		}

		for (size_t renderPassIndex = 0; renderPassIndex < renderPasses.size(); ++renderPassIndex)
		{
			CompiledNode& node = compilation.nodes[renderPassIndex];
			if (!node.valid)
				continue;
			for (const RenderPass::LogicalResourceBindingDisk& input : renderPasses[renderPassIndex].inputs)
			{
				if (input.origin != ShaderResource::ResourceOrigin::Runtime || input.resourceId.empty())
					continue;
				const auto producerIt = runtimeResourceProducers.find(input.resourceId);
				if (producerIt == runtimeResourceProducers.end())
				{
					if (!input.optional)
					{
						node.valid = false;
						node.error = "Runtime resource has no producer: " + input.resourceId + ".";
						compilation.diagnostics.push_back(node.error +
							" Consumer=" + renderPasses[renderPassIndex].name + ".");
					}
					continue;
				}

				const CompiledNode& producer = compilation.nodes[producerIt->second];
				const RenderPass::RenderPassDisk& producerPass = renderPasses[producerIt->second];
				if (RenderPass::FindMipChainRuntimeSource(producerPass) &&
					input.resourceId == RenderPass::MipChainOutputResourceId(producerPass) &&
					input.temporalView != ShaderResource::TemporalView::Current)
				{
					node.valid = false;
					node.error = "Generated mip outputs are only available in the current execution: " + input.resourceId + ".";
					compilation.diagnostics.push_back(node.error);
					continue;
				}
				if (producerIt->second == renderPassIndex &&
					input.temporalView != ShaderResource::TemporalView::Previous)
				{
					node.valid = false;
					node.error = "A pass cannot sample its current runtime output: " + input.resourceId + ".";
					compilation.diagnostics.push_back(node.error);
					continue;
				}
				if (producer.modifiedShaderId != node.modifiedShaderId || producer.rootBoundary != node.rootBoundary)
				{
					node.valid = false;
					node.error = "Runtime resource crosses incompatible execution anchors: " + input.resourceId + ".";
					compilation.diagnostics.push_back(node.error);
					continue;
				}
				AddDependency(node, producerIt->second);
			}
		}

		// Resource validation happens after event resolution. Propagate failures
		// through both kinds of dependency before building an executable order.
		bool invalidatedDependency = false;
		do
		{
			invalidatedDependency = false;
			for (CompiledNode& node : compilation.nodes)
			{
				if (!node.valid)
					continue;
				const bool invalidParent = node.parentRenderPassIndex != invalidNodeIndex &&
					!compilation.nodes[node.parentRenderPassIndex].valid;
				const bool invalidInput = std::any_of(node.dependencies.begin(), node.dependencies.end(),
					[&](size_t dependencyIndex) { return !compilation.nodes[dependencyIndex].valid; });
				if (invalidParent || invalidInput)
				{
					node.valid = false;
					node.error = "A required render-pass dependency is invalid.";
					compilation.diagnostics.push_back(node.error + " Pass=" +
						renderPasses[node.renderPassIndex].name + ".");
					invalidatedDependency = true;
				}
			}
		} while (invalidatedDependency);

		// Seed each plan with the legacy recursive order. The topological pass below
		// then preserves that stable order unless an explicit resource edge requires
		// a producer to move before its consumer.
		std::vector<uint8_t> appendedRenderPasses(renderPasses.size(), 0);
		std::function<void(size_t, std::vector<size_t>&)> appendRenderPassSubtree =
			[&](size_t renderPassIndex, std::vector<size_t>& executionOrder)
		{
			if (renderPassIndex >= renderPasses.size() || appendedRenderPasses[renderPassIndex] ||
				!compilation.nodes[renderPassIndex].valid)
			{
				return;
			}
			appendedRenderPasses[renderPassIndex] = 1;

			const auto appendChildren = [&](const char* timing)
			{
				for (size_t childIndex = 0; childIndex < renderPasses.size(); ++childIndex)
				{
					const RenderPass::RenderPassDisk& child = renderPasses[childIndex];
					if (compilation.nodes[childIndex].valid &&
						child.event.type == RenderPass::EventType::RenderPass &&
						child.event.id == renderPasses[renderPassIndex].id && child.timing == timing)
					{
						appendRenderPassSubtree(childIndex, executionOrder);
					}
				}
			};

			appendChildren(RenderPass::timingBefore);
			executionOrder.push_back(renderPassIndex);
			appendChildren(RenderPass::timingAfter);
		};

		for (size_t renderPassIndex = 0; renderPassIndex < renderPasses.size(); ++renderPassIndex)
		{
			const CompiledNode& node = compilation.nodes[renderPassIndex];
			if (!node.valid || renderPasses[renderPassIndex].event.type != RenderPass::EventType::ModifiedShader)
				continue;
			ExecutionPlan& plan = compilation.executionPlans[node.modifiedShaderId];
			const size_t boundaryIndex = node.rootBoundary == Boundary::After ? 1u : 0u;
			appendRenderPassSubtree(renderPassIndex, plan.executionOrders[boundaryIndex]);
		}

		for (auto& planEntry : compilation.executionPlans)
		{
			for (size_t boundaryIndex = 0; boundaryIndex < 2; ++boundaryIndex)
			{
				std::vector<size_t>& candidates = planEntry.second.executionOrders[boundaryIndex];
				for (size_t replacementIndex : candidates)
				{
					if (compilation.nodes[replacementIndex].operation != RenderPass::PassOperation::ReplaceOriginal)
						continue;
					for (size_t candidateIndex : candidates)
					{
						if (compilation.nodes[candidateIndex].operation != RenderPass::PassOperation::ReplaceOriginal)
							AddDependency(compilation.nodes[replacementIndex], candidateIndex);
					}
				}

				candidates = StableTopologicalSort(renderPasses, compilation.nodes, candidates, compilation.diagnostics);
				for (size_t candidateIndex : candidates)
				{
					if (compilation.nodes[candidateIndex].operation == RenderPass::PassOperation::MipChain &&
						!RenderPass::FindMipChainRuntimeSource(renderPasses[candidateIndex]))
						planEntry.second.mipChainOrders[boundaryIndex].push_back(candidateIndex);
				}
				if (!candidates.empty())
				{
					const uint32_t boundaryMask = boundaryIndex ? 2u : 1u;
					if (std::any_of(candidates.begin(), candidates.end(), [&](size_t candidateIndex)
					{
						return compilation.nodes[candidateIndex].operation == RenderPass::PassOperation::Copy ||
							compilation.nodes[candidateIndex].executionMode == RenderPass::ExecutionMode::FullscreenPixel;
					}))
					{
						planEntry.second.graphicsBoundaryMask |= boundaryMask;
					}
					if (std::any_of(candidates.begin(), candidates.end(), [&](size_t candidateIndex)
					{
						return compilation.nodes[candidateIndex].operation == RenderPass::PassOperation::Copy ||
							compilation.nodes[candidateIndex].executionMode == RenderPass::ExecutionMode::Compute;
					}))
					{
						planEntry.second.computeBoundaryMask |= boundaryMask;
					}
				}
			}
		}

		return compilation;
	}
}
