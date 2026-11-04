#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "RenderPass/RenderPass.h"

namespace RenderPassRuntime
{
	struct DownsampleUpsampleStage
	{
		uint32_t width = 0;
		uint32_t height = 0;
		bool upsample = false;
	};

	//describe every texture extent the chain visits, including the final full-size output.
	inline bool BuildDownsampleUpsampleStagePlan(
		uint32_t sourceWidth,
		uint32_t sourceHeight,
		uint32_t factor,
		std::vector<DownsampleUpsampleStage>& outStages)
	{
		outStages.clear();
		if (!sourceWidth || !sourceHeight ||
			!RenderPass::IsValidDownsampleUpsampleFactor(factor) ||
			(sourceWidth <= 1 && sourceHeight <= 1))
			return false;

		std::vector<DownsampleUpsampleStage> downsampleStages;
		uint32_t stageWidth = sourceWidth;
		uint32_t stageHeight = sourceHeight;
		for (uint32_t divisor = 2; divisor <= factor; divisor *= 2)
		{
			stageWidth = (std::max)(1u, stageWidth / 2);
			stageHeight = (std::max)(1u, stageHeight / 2);
			downsampleStages.push_back({stageWidth, stageHeight, false});
		}

		outStages = downsampleStages;
		//the upsample sizes mirror the exact prior downsample sizes in reverse.
		for (size_t index = 0; index < downsampleStages.size(); ++index)
		{
			if (index + 1 < downsampleStages.size())
			{
				const size_t sourceIndex = downsampleStages.size() - index - 2;
				outStages.push_back({downsampleStages[sourceIndex].width, downsampleStages[sourceIndex].height, true});
			}
			else
				outStages.push_back({sourceWidth, sourceHeight, true});
		}
		return true;
	}
} //namespace RenderPassRuntime
