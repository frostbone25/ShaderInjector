#pragma once

namespace ShaderTarget
{
	enum ShaderType
	{
		VertexShader = 0,
		HullShader = 1,
		DomainShader = 2,
		GeometryShader = 3,
		PixelShader = 4,
		ComputeShader = 5,
		AmplificationShader = 6,
		MeshShader = 7,
		Unknown = 8,
	};
} //namespace ShaderTarget
