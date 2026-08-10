//PostProcessFinalHDRGameplaySimple.hlsl
//Game Shader Version: 1.0.0.5

//The HDR final pass that actually draws normal gameplay. Identified from a RenderDoc capture of a gameplay frame -
//it is the only PostProcessFinal permutation present in that frame, and none of the other three ever bound during play.
//One UI composite layer, only BT709PQToBT2020PQLUT, and no HDRCompositionContext use at all: the simplest of the family.
//Original game shader: PixelShader CBA9C01BD1B69ABF (26120 bytes)
//[NO CONFIG]
#define POSTPROCESS_FINAL_HDR_GAMEPLAY_SIMPLE

//NOTE: shared with the SDR pass and the other HDR variants - every setting lives in one place so they all stay in sync.
//You'll find this in ShaderInjector/ModifiedShaders/Includes
#include "PixelShaderPass_PostProcessFinal.hlsl"
