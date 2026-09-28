#include "ShaderTemplates.h"

#include <algorithm>
#include <sstream>
#include <vector>

#include <d3d12shader.h>

#include "ShaderAnalysis/ShaderAnalysisDisk.h"

namespace ShaderTemplates
{
	extern const char* meshShaderTemplate;

	namespace
	{
		std::string HlslType(const ShaderAnalysis::SignatureParameterDisk& parameter)
		{
			const char* scalarType = "float";
			if (parameter.componentType == D3D_REGISTER_COMPONENT_SINT32)
				scalarType = "int";
			else if (parameter.componentType == D3D_REGISTER_COMPONENT_UINT32)
				scalarType = "uint";

			const uint32_t mask = parameter.mask & 0x0fu;
			const unsigned componentCount = (std::max)(1u, (mask & 1u) + ((mask >> 1) & 1u) + ((mask >> 2) & 1u) + ((mask >> 3) & 1u));
			if (componentCount == 1)
				return scalarType;
			return std::string(scalarType) + std::to_string(componentCount);
		}

		std::vector<ShaderAnalysis::SignatureParameterDisk> OrderedSignature(const std::vector<ShaderAnalysis::SignatureParameterDisk>& parameters)
		{
			std::vector<ShaderAnalysis::SignatureParameterDisk> ordered = parameters;
			std::stable_sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right)
			{
				return left.registerIndex < right.registerIndex;
			});
			return ordered;
		}

		void AppendOutputStruct(std::ostringstream& source, const char* name, const std::vector<ShaderAnalysis::SignatureParameterDisk>& parameters)
		{
			source << "struct " << name << "\n{\n";
			for (size_t index = 0; index < parameters.size(); ++index)
			{
				const auto& parameter = parameters[index];
				source << "    " << HlslType(parameter) << " value" << index << " : " << parameter.semanticName << parameter.semanticIndex << ";\n";
			}
			source << "};\n\n";
		}
	}

	std::string BuildMeshShaderSourceTemplate(const ShaderAnalysis::ShaderAnalysisDisk& originalShader)
	{
		if (!originalShader.succeeded || originalShader.shaderStage != D3D12_SHVER_MESH_SHADER || originalShader.outputParameters.empty())
			return meshShaderTemplate;

		const auto vertexOutputs = OrderedSignature(originalShader.outputParameters);
		const auto primitiveOutputs = OrderedSignature(originalShader.patchConstantParameters);
		const uint32_t topology = originalShader.executionProperties.geometryOutputTopology;
		const bool points = topology == D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
		const bool lines = topology == D3D_PRIMITIVE_TOPOLOGY_LINELIST;
		const unsigned vertexCount = points ? 1u : (lines ? 2u : 3u);
		const char* topologyName = points ? "point" : (lines ? "line" : "triangle");
		const char* indexType = points ? "uint" : (lines ? "uint2" : "uint3");
		const char* indices = points ? "0" : (lines ? "uint2(0, 1)" : "uint3(0, 1, 2)");
		const uint32_t groupSizeX = (std::max)(1u, originalShader.executionProperties.threadGroupSizeX);
		const uint32_t groupSizeY = (std::max)(1u, originalShader.executionProperties.threadGroupSizeY);
		const uint32_t groupSizeZ = (std::max)(1u, originalShader.executionProperties.threadGroupSizeZ);

		std::ostringstream source;
		source << "// replace the placeholder output values with the original mesh shader's data.\n";
		AppendOutputStruct(source, "VertexOutput", vertexOutputs);
		if (!primitiveOutputs.empty())
			AppendOutputStruct(source, "PrimitiveOutput", primitiveOutputs);
		source << "[outputtopology(\"" << topologyName << "\")]\n";
		source << "[numthreads(" << groupSizeX << ", " << groupSizeY << ", " << groupSizeZ << ")]\n";
		source << "void main(out vertices VertexOutput vertices[" << vertexCount << "], out indices " << indexType << " primitiveIndices[1]";
		if (!primitiveOutputs.empty())
			source << ", out primitives PrimitiveOutput primitives[1]";
		source << ", uint groupThreadIndex : SV_GroupIndex)\n{\n";
		source << "    SetMeshOutputCounts(" << vertexCount << ", 1);\n";
		source << "    if (groupThreadIndex == 0)\n    {\n";
		for (unsigned vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex)
		{
			for (size_t parameterIndex = 0; parameterIndex < vertexOutputs.size(); ++parameterIndex)
			{
				const auto& parameter = vertexOutputs[parameterIndex];
				const bool isPosition = parameter.systemValueType == D3D_NAME_POSITION;
				source << "        vertices[" << vertexIndex << "].value" << parameterIndex << " = ";
				if (isPosition && vertexCount == 3)
				{
					const char* position[] = {"float4(-1, -1, 0, 1)", "float4(-1, 3, 0, 1)", "float4(3, -1, 0, 1)"};
					source << position[vertexIndex];
				}
				else
				{
					source << "0";
				}
				source << ";\n";
			}
		}
		for (size_t parameterIndex = 0; parameterIndex < primitiveOutputs.size(); ++parameterIndex)
			source << "        primitives[0].value" << parameterIndex << " = 0;\n";
		source << "        primitiveIndices[0] = " << indices << ";\n";
		source << "    }\n}\n";
		return source.str();
	}
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

	const char* meshShaderTemplate = R"(
struct VertexOutput
{
	float4 position : SV_Position;
};

[outputtopology("triangle")]
[numthreads(1, 1, 1)]
void main(out vertices VertexOutput vertices[3], out indices uint3 triangles[1])
{
	SetMeshOutputCounts(3, 1);
	vertices[0].position = float4(-1.0, -1.0, 0.0, 1.0);
	vertices[1].position = float4(-1.0, 3.0, 0.0, 1.0);
	vertices[2].position = float4(3.0, -1.0, 0.0, 1.0);
	triangles[0] = uint3(0, 1, 2);
}
)";

	const char* amplificationShaderTemplate = R"(
struct MeshPayload
{
	uint meshletIndex;
};

[numthreads(1, 1, 1)]
void main(uint3 groupId : SV_GroupID)
{
	MeshPayload payload;
	payload.meshletIndex = groupId.x;
	DispatchMesh(1, 1, 1, payload);
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
		case ShaderTarget::AmplificationShader:
			return amplificationShaderTemplate;
		case ShaderTarget::MeshShader:
			return meshShaderTemplate;
		default:
			return nullptr;
		}
	}
} //namespace ShaderTemplates
