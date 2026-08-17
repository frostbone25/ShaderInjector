//PostProcessFinalHDRGameplay3LayerFull.hlsl
//Game Shader Version: 1.0.0.5

//HDR gameplay pass with non-neutral calibration and a composition backdrop active. Three UI layers, all three LUTs.
//Original game shader: PixelShader AD971BD64DEB7F9E
//[NO CONFIG]
#define POSTPROCESS_FINAL_HDR_GAMEPLAY_3LAYER_FULL

//NOTE: shared with the SDR pass and the other HDR variants - every setting lives in one place so they all stay in sync.
//You'll find this in ShaderInjector/ModifiedShaders/Includes
#include "PixelShaderPass_PostProcessFinal.hlsl"
