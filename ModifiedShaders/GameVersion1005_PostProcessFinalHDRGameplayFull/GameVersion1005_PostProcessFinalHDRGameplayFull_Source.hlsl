//PostProcessFinalHDRGameplayFull.hlsl
//Game Shader Version: 1.0.0.5

//HDR gameplay pass with non-neutral calibration and a composition backdrop active. One UI layer, all three LUTs.
//Original game shader: PixelShader 10D1F04978261DDC
//[NO CONFIG]
#define POSTPROCESS_FINAL_HDR_GAMEPLAY_FULL

//NOTE: shared with the SDR pass and the other HDR variants - every setting lives in one place so they all stay in sync.
//You'll find this in ShaderInjector/ModifiedShaders/Includes
#include "PixelShaderPass_PostProcessFinal.hlsl"
