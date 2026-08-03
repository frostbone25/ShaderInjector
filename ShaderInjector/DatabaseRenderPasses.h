#pragma once

#include <string>
#include <vector>

#include "ModifiedShader.h"
#include "RenderPass.h"

namespace DatabaseRenderPasses
{
	void RefreshRenderPasses();
	void EnsureRenderPassesLoaded();
	const std::vector<RenderPass::RenderPassDisk>& GetRenderPasses();
	RenderPass::RenderPassDisk* FindRenderPassById(const std::string& renderPassId);
	const RenderPass::RenderPassDisk* FindRenderPassByIdReadOnly(const std::string& renderPassId);
	std::string ResolveModifiedShaderId(const RenderPass::RenderPassDisk& renderPass);
	std::string ResolveRootTiming(const RenderPass::RenderPassDisk& renderPass);
	const ModifiedShader::PackageDisk* ResolveModifiedShader(const RenderPass::RenderPassDisk& renderPass);
	bool IsEventChainActive(const RenderPass::RenderPassDisk& renderPass);
	bool CanReferenceRenderPass(const std::string& renderPassId, const std::string& eventRenderPassId);
	bool CreateRenderPass(std::string& outRenderPassId);
	bool SaveRenderPass(const std::string& renderPassId);
	bool DeleteRenderPass(const std::string& renderPassId);
	bool CreateFragmentShaderTemplate(const std::string& renderPassId, std::string& outError);
	bool CompileRenderPassShaders(const std::string& renderPassId, std::string& outError);
}
