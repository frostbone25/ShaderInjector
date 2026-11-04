//PostProcessFinalHDR3LayerBackdropFrameGen.hlsl
//Game Shader Version: 1.0.0.5

//Frame generation variant of PostProcessFinalHDR3LayerBackdrop: three UI composite layers with a composition backdrop active.
//
//With DLSS frame generation enabled the game selects a parallel set of final pass shaders that
//write THREE render targets instead of one, so the interpolator receives the scene and the UI
//separately: target 0 is the composited image as before, target 1 the same scene before UI
//compositing, and target 2 the UI recovered as max(0, target0 - target1 * transmittance) with
//alpha 1 - transmittance.
//
//The resource layout is identical to AFD51D036C4730AD register for register - only the output
//signature differs.
//Original game shader: PixelShader A72A6F60AD6C4053
//[NO CONFIG]
#define POSTPROCESS_FINAL_HDR_3LAYER_BACKDROP
#define POSTPROCESS_FINAL_HDR_FRAMEGEN

//NOTE: shared with the SDR pass - every setting lives in one place so all variants stay in sync.
//You'll find this in ShaderInjector/ModifiedShaders/Includes
#include "PixelShaderPass_PostProcessFinal.hlsl"
