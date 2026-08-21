//PostProcessFinalHDRGameplayFrameGen.hlsl
//Game Shader Version: 1.0.0.5

//Frame generation variant of HDRGameplay: gameplay with a composition backdrop active: one UI layer.
//
//With DLSS frame generation enabled the game selects a parallel set of final pass shaders that
//write THREE render targets instead of one, so the interpolator receives the scene and the UI
//separately: target 0 is the composited image as before, target 1 the same scene before UI
//compositing, and target 2 the UI recovered as max(0, target0 - target1 * transmittance) with
//alpha 1 - transmittance.
//
//The resource layout is identical to 6ACF39BD7FB286B8 register for register - only the output
//signature differs.
//Original game shader: PixelShader 71A1B0AF1952B96D
//[NO CONFIG]
#define POSTPROCESS_FINAL_HDR_GAMEPLAY
#define POSTPROCESS_FINAL_HDR_FRAMEGEN

//NOTE: shared with the SDR pass - every setting lives in one place so all variants stay in sync.
//You'll find this in ShaderInjector/ModifiedShaders/Includes
#include "PixelShaderPass_PostProcessFinal.hlsl"
