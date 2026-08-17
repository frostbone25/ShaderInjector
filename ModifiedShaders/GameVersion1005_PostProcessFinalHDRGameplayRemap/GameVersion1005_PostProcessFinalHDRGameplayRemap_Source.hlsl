//PostProcessFinalHDRGameplayRemap.hlsl
//Game Shader Version: 1.0.0.5

//The HDR gameplay pass the game uses when the player's HDR calibration is not neutral - HDR brightness set below
//maximum and/or the brightness slider off default. Found via RenderDoc; in practice this is the common case, the
//non-remap variants only run at exactly neutral settings. One UI composite layer, BT709PQToBT2020PQLUT plus the
//BT2020PQ1000ToBT2020PQ250LUT peak brightness remap, driven by DeviceCorrectorContext.xy. No HDRCompositionContext use.
//Original game shader: PixelShader 75C16A8ECF232D62 (27236 bytes)
//[NO CONFIG]
#define POSTPROCESS_FINAL_HDR_GAMEPLAY_REMAP

//NOTE: shared with the SDR pass and the other HDR variants - every setting lives in one place so they all stay in sync.
//You'll find this in ShaderInjector/ModifiedShaders/Includes
#include "PixelShaderPass_PostProcessFinal.hlsl"
