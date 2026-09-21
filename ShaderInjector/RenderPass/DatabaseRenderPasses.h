#pragma once

#include <string>
#include <vector>

#include "ModifiedShader/ModifiedShader.h"
#include "RenderPass/RenderPass.h"

namespace DatabaseRenderPasses
{
	struct RenderPassShaderBatchCompileResult
	{
		size_t compiledRenderPassCount = 0;
		size_t failedRenderPassCount = 0;
		size_t skippedRenderPassCount = 0;
		std::vector<std::string> errors;
	};

	void RefreshRenderPasses();
	void EnsureRenderPassesLoaded();
	const std::vector<RenderPass::RenderPassDisk>& GetRenderPasses();
	RenderPass::RenderPassDisk* FindRenderPassById(const std::string& renderPassId);
	const RenderPass::RenderPassDisk* FindRenderPassByIdReadOnly(const std::string& renderPassId);
	std::string ResolveModifiedShaderId(const RenderPass::RenderPassDisk& renderPass);
	std::string ResolveRootTiming(const RenderPass::RenderPassDisk& renderPass);
	const ModifiedShader::ModifiedShaderPackageDisk* ResolveModifiedShader(const RenderPass::RenderPassDisk& renderPass);
	bool IsEventChainActive(const RenderPass::RenderPassDisk& renderPass);
	bool CanReferenceRenderPass(const std::string& renderPassId, const std::string& eventRenderPassId);
	bool CreateRenderPass(std::string& outRenderPassId);
	bool SetRenderPassEnabled(const std::string& renderPassId, bool enabled);
	bool SaveRenderPass(const std::string& renderPassId);
	bool DeleteRenderPass(const std::string& renderPassId);
	bool CreateShaderTemplate(const std::string& renderPassId, std::string& outError);
	bool CompileRenderPassShaders(const std::string& renderPassId, std::string& outError);
	RenderPassShaderBatchCompileResult CompileAllRenderPassShaders();
}
