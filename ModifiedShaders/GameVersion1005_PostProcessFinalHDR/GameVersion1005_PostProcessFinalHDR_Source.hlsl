//PostProcessFinalHDR.hlsl
//Game Shader Version: 1.0.0.5

//The HDR variant of the final pass. The game runs a completely separate shader when Windows HDR is on:
//it composites three UI layers instead of one, drops the BT2020PQ->sRGB LUT (that LUT is the HDR->SDR display map),
//and writes BT.2020 PQ to a 10 bit swapchain instead of sRGB to an 8 bit one.
//Original game shader: PixelShader 3966BB6523888928
//[NO CONFIG]
#define POSTPROCESS_FINAL_HDR

//NOTE: shared with the SDR pass - every setting lives in one place so both variants stay in sync.
//You'll find this in ShaderInjector/ModifiedShaders/Includes
#include "PixelShaderPass_PostProcessFinal.hlsl"
