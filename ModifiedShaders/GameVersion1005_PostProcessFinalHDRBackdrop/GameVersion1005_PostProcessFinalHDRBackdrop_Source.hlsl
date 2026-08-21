//PostProcessFinalHDRBackdrop.hlsl
//Game Shader Version: 1.0.0.5

//The HDR gameplay variant of the final pass with a single UI composite layer.
//Same resource layout as the SDR pass (both LUTs, one UI layer), but it writes BT.2020 PQ to a 10 bit
//swapchain and also reads HDRCompositionContext/HDRCompositionContextColor for the composition backdrop.
//Original game shader: PixelShader 6ACF39BD7FB286B8
//[NO CONFIG]
#define POSTPROCESS_FINAL_HDR_BACKDROP

//NOTE: shared with the SDR pass - every setting lives in one place so all variants stay in sync.
//You'll find this in ShaderInjector/ModifiedShaders/Includes
#include "PixelShaderPass_PostProcessFinal.hlsl"
