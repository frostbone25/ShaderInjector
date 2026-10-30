//PostProcessFinalHDR3LayerBackdrop.hlsl
//Game Shader Version: 1.0.0.5

//The HDR final pass variant with three UI composite layers.
//Same algorithm as the single layer variant (including the composition backdrop),
//but with the menu style background/main/foreground UI stack and the HDR->SDR LUT moved to t7.
//Original game shader: PixelShader AFD51D036C4730AD
//[NO CONFIG]
#define POSTPROCESS_FINAL_HDR_3LAYER_BACKDROP

//NOTE: shared with the SDR pass - every setting lives in one place so all variants stay in sync.
//You'll find this in ShaderInjector/ModifiedShaders/Includes
#include "PixelShaderPass_PostProcessFinal.hlsl"
