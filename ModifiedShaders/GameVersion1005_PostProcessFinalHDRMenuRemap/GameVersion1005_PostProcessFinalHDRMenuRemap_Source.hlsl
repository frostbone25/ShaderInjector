//PostProcessFinalHDRMenuRemap.hlsl
//Game Shader Version: 1.0.0.5

//The HDR menu pass the game uses when the player's HDR calibration is not neutral - HDR brightness set below
//maximum and/or the brightness slider off default. Three UI composite layers, BT709PQToBT2020PQLUT plus the
//BT2020PQ1000ToBT2020PQ250LUT peak brightness remap, driven by DeviceCorrectorContext.xy. Its scene maths are
//identical to the single layer remap pass (75C16A8ECF232D62), only the UI composite differs. No HDRCompositionContext use.
//Original game shader: PixelShader EB2D0BCAD9327257 (27588 bytes)
//[NO CONFIG]
#define POSTPROCESS_FINAL_HDR_MENU_REMAP

//NOTE: shared with the SDR pass and the other HDR variants - every setting lives in one place so they all stay in sync.
//You'll find this in ShaderInjector/ModifiedShaders/Includes
#include "PixelShaderPass_PostProcessFinal.hlsl"
