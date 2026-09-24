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
		Unknown = 6,
	};
} //namespace ShaderTarget
