#pragma once

#include "ShaderInjectorIO.h"
#include "ShaderReplacement.h"

namespace ShaderTemplates
{
	//NOTE: These are source code shader templates that the shader injector will auto-generate for various tasks.
	//Now normally I would think it'd be wise to actually have these already serialized to the disk rather than holding them in memory.
	//But... I do not trust users... so for sanity sake these will remain in memory.
	//That way if anything goes wrong, we can just rebuild some of these shaders and it should still work...

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| PIXEL SHADER |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| PIXEL SHADER |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| PIXEL SHADER |||||||||||||||||||||||||||||||||||||||||||||||||||||

	//NOTE TO SELF: watch those spaces!
	static const char* internalMarkerPixelShaderSourceCode = R"(
float4 main() : SV_Target0
{
	return float4(0, 0, 1, 1); //blue for marking
}
)";

	//NOTE TO SELF: watch those spaces!
	static const char* internalNullPixelShaderSourceCode = R"(
float4 main() : SV_Target0
{
	return float4(1, 0, 0, 1); //red for error
}
)";

	//NOTE TO SELF: watch those spaces!
	static const char* internalGreenPixelShaderSourceCode = R"(
float4 main() : SV_Target0
{
	return float4(0, 1, 0, 1); //red for error
}
)";

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| COMPUTE SHADER |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| COMPUTE SHADER |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| COMPUTE SHADER |||||||||||||||||||||||||||||||||||||||||||||||||||||

	//NOTE TO SELF: watch those spaces!
	static const char* internalMarkerComputeShaderSourceCode = R"(
struct InputStruct 
{
	uint3 DispatchThreadID : SV_DispatchThreadID;
};

[numthreads(8, 8, 1)]
void main(in InputStruct IN)
{

}
)";

}
