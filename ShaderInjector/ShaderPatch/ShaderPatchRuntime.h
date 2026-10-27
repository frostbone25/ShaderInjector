#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <d3d12.h>

#include "Enum/ShaderModel.h"
#include "Enum/ShaderType.h"
#include "ShaderPatch/Recipe.h"

//Shader patches apply DXP recipes to captured game shaders. Patched pipelines are published
//through the same substitution paths as Shader Targets: bytecode-only patches through the
//pipeline override map, texture patches through render-pass execution boundaries.
namespace ShaderPatchRuntime
{
	struct PatchedShaderInfo
	{
		uint64_t originalHash = 0;
		ShaderTarget::ShaderType stage = ShaderTarget::Unknown;
		Globals::ShaderModel shaderModel = Globals::ShaderModel::ShaderModel5_0;
		std::string recipeName;
		std::string status;
		size_t originalSize = 0;
		//0 when the recipe failed on this shader.
		size_t patchedSize = 0;
		//the shader's patch-cache key; dumps read bytecode from the cache and the capture database.
		std::string cacheKey;
	};

	//shaders patched with the current recipes, in total and per recipe.
	struct PatchedShaderCounts
	{
		size_t total = 0;
		std::unordered_map<std::string, size_t> byRecipe;
	};

	struct DumpResult
	{
		std::string bytecodePath;
		bool bytecodeWritten = false;
		bool disassemblyAttempted = false;
		bool disassemblyWritten = false;
	};

	//load recipes synchronously at device creation and start the worker when there are any.
	void Initialize();
	void Shutdown();
	//recipes reload on the worker, which runs only when recipes were loaded at startup.
	bool IsReloadAvailable();
	void RequestReload();
	//republish pipeline overrides after the enable toggle changes.
	void NotifyEnabledChanged();

	//queue a newly created pipeline so its patch is built while the game precompiles pipelines.
	void NotifyPipelineCreated(ID3D12PipelineState* pipelineState);
	//queue a bound pipeline ahead of the creation backlog; after a reload this re-patches it.
	void NotifyPipelineBound(ID3D12PipelineState* pipelineState);
	//add patched pipelines without a Shader Target override; called while the override map is rebuilt.
	void AppendPipelineOverrides(std::unordered_map<ID3D12PipelineState*, ID3D12PipelineState*>& overrides);
	//render thread, once per frame: frees retired results whose pipeline the tracker released.
	void ReleaseRetiredResults();
	//true when a loaded recipe adds textures, so new shader-visible heaps reserve room for their tables.
	bool HasResourceRecipes();

	bool IsBusy();
	std::string GetError();
	std::string GetRecipeDirectory();
	std::vector<ShaderPatch::RecipeInfo> GetRecipes();
	std::vector<PatchedShaderInfo> GetShaders();
	PatchedShaderCounts GetPatchedShaderCounts();
	DumpResult DumpShader(const PatchedShaderInfo& shader, bool patched);
} //namespace ShaderPatchRuntime
