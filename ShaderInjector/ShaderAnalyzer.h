#pragma once

#include <unordered_set>

#include "ShaderAnalysis.h"

namespace ShaderAnalyzer
{
	//acceptablePortableReflectionHashes is an optional pre-filter: if provided, the expensive
	//full-disassembly step (which produces semanticInstructionSetHash/crossVersionIdentityHash)
	//is skipped whenever the shader's cheap portableReflectionIdentityHash - available from
	//structured reflection data alone - isn't in the set. Everything else (reflection, resource
	//bindings, signatures, etc.) is still filled in either way, and succeeded is still true;
	//only the disassembly-derived fields are left empty for skipped candidates. Pass nullptr to
	//always run the full analysis (existing callers that need complete results, e.g. building
	//replacement templates, are unaffected).
	bool Analyze(
		const void* bytecode,
		size_t bytecodeLength,
		ShaderAnalysis::ShaderAnalysisDisk& outAnalysis,
		const std::unordered_set<std::string>* acceptablePortableReflectionHashes = nullptr);
} //namespace ShaderAnalyzer
