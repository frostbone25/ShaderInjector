#include "ShaderTemplates.h"

namespace ShaderTemplates
{
	const char* vertexShaderTemplate = R"(
struct VertexOutput
{
	float4 position : SV_Position;
};

VertexOutput main(uint vertexId : SV_VertexID)
{
	VertexOutput output;
	output.position = float4(0.0, 0.0, 0.0, 1.0);
	return output;
}
)";

	const char* hullShaderTemplate = R"(
struct ControlPoint { float4 position : SV_Position; };
struct PatchConstants { float edges[3] : SV_TessFactor; float inside : SV_InsideTessFactor; };

PatchConstants PatchConstantFunction(InputPatch<ControlPoint, 3> patch)
{
	PatchConstants output;
	output.edges[0] = output.edges[1] = output.edges[2] = 1.0;
	output.inside = 1.0;
	return output;
}

[domain("tri")]
[partitioning("integer")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("PatchConstantFunction")]
ControlPoint main(InputPatch<ControlPoint, 3> patch, uint pointId : SV_OutputControlPointID)
{
	return patch[pointId];
}
)";

	const char* domainShaderTemplate = R"(
struct ControlPoint { float4 position : SV_Position; };
struct PatchConstants { float edges[3] : SV_TessFactor; float inside : SV_InsideTessFactor; };

[domain("tri")]
ControlPoint main(PatchConstants constants, float3 coordinates : SV_DomainLocation, const OutputPatch<ControlPoint, 3> patch)
{
	ControlPoint output;
	output.position = patch[0].position * coordinates.x + patch[1].position * coordinates.y + patch[2].position * coordinates.z;
	return output;
}
)";

	const char* geometryShaderTemplate = R"(
struct Vertex { float4 position : SV_Position; };

[maxvertexcount(3)]
void main(triangle Vertex input[3], inout TriangleStream<Vertex> outputStream)
{
	outputStream.Append(input[0]);
	outputStream.Append(input[1]);
	outputStream.Append(input[2]);
}
)";

	const char* GetModifiedShaderSourceTemplate(ShaderTarget::ShaderType shaderType)
	{
		switch (shaderType)
		{
		case ShaderTarget::VertexShader:
			return vertexShaderTemplate;
		case ShaderTarget::HullShader:
			return hullShaderTemplate;
		case ShaderTarget::DomainShader:
			return domainShaderTemplate;
		case ShaderTarget::GeometryShader:
			return geometryShaderTemplate;
		case ShaderTarget::PixelShader:
			return internalGreenPixelShaderSourceCode;
		case ShaderTarget::ComputeShader:
			return internalMarkerComputeShaderSourceCode;
		default:
			return nullptr;
		}
	}
} //namespace ShaderTemplates
