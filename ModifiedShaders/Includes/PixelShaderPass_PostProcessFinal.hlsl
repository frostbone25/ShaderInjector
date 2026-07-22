//PostProcessFinal.hlsl

//library includes
//NOTE: this is where we have various useful shader functions
#include "LibraryMath.hlsl"
#include "LibraryColor.hlsl"
#include "LibraryTonemaps.hlsl"

//only for debugging purposes
//#define DEBUG_COLOR_CHART

//|||||||||||||||||||||||||||||||||| CONFIGURATION - VIGNETTE ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| CONFIGURATION - VIGNETTE ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| CONFIGURATION - VIGNETTE ||||||||||||||||||||||||||||||||||

//disabled, I hate how strong the vignettes are by default
//#define VIGNETTE_ENABLED

//|||||||||||||||||||||||||||||||||| CONFIGURATION - BLOOM ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| CONFIGURATION - BLOOM ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| CONFIGURATION - BLOOM ||||||||||||||||||||||||||||||||||

//game has bloom by default
#define BLOOM_ENABLE

//NOTE: if you want the original game bloom just disable BLOOM_PHYSICAL and BLOOM_ADDITIVE

//this changes the bloom compositing to be more "physically" accurate
//#define BLOOM_PHYSICAL
#define BLOOM_PHYSICAL_INTENSITY 0.125

//this changes the bloom compositing to be more of a classic "additive"
//#define BLOOM_ADDITIVE
#define BLOOM_ADDITIVE_INTENSITY 2

//|||||||||||||||||||||||||||||||||| CONFIGURATION - AUTO EXPOSURE ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| CONFIGURATION - AUTO EXPOSURE ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| CONFIGURATION - AUTO EXPOSURE ||||||||||||||||||||||||||||||||||
//jank auto-expousre implementation, in the future I definetly wish to improve upon this because its way more expensive than it should be
//by leveraging mip generation and calculating average exposure based off of that... instead of doing AUTO_EXPOSURE_GRID_X x AUTO_EXPOSURE_GRID_Y samples for every pixel
//but for now... we are limited so just deal with it

//this is unusally expensive, but because we are kinda limited... we just gotta deal with it
//it's not perfect and will flicker occasionally, brightness changes are instantaneous also
#define AUTO_EXPOSURE

//more samples = more stable auto exposure (less flicker) but can be slower
//less samples = less stable auto exposure (more flicker) but faster
#define AUTO_EXPOSURE_GRID_X 16
#define AUTO_EXPOSURE_GRID_Y 16
#define AUTO_EXPOSURE_MIDDLE_GRAY      0.18
#define AUTO_EXPOSURE_STRENGTH         0.5
#define AUTO_EXPOSURE_MIN_EV          -6.0
#define AUTO_EXPOSURE_MAX_EV           1.0
#define AUTO_EXPOSURE_COMPENSATION_EV -0.35
#define AUTO_EXPOSURE_CENTER_FOCUS     0.5

//|||||||||||||||||||||||||||||||||| CONFIGURATION - COLOR / BRIGHTNESS ADJUSTMENTS ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| CONFIGURATION - COLOR / BRIGHTNESS ADJUSTMENTS ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| CONFIGURATION - COLOR / BRIGHTNESS ADJUSTMENTS ||||||||||||||||||||||||||||||||||
//these are simple artistic controls for being able to control the final image
//these get applied BEFORE the tonemaps

#define ADJUSTMENT_BRIGHTNESS_EV -0.35
#define ADJUSTMENT_CONTRAST 0.99
#define ADJUSTMENT_CONTRAST_PIVOT 0.18
#define ADJUSTMENT_SATURATION 1.0
#define ADJUSTMENT_VIBRANCE 0.0
#define ADJUSTMENT_TINT_COLOR float3(1.0, 1.0, 1.0)
#define ADJUSTMENT_TINT_FACTOR 0.0
#define ADJUSTMENT_GAMMA 1.1
#define ADJUSTMENT_LIFT float3(0.0, 0.0, 0.0)
#define ADJUSTMENT_GAIN float3(1.0, 1.0, 1.0)

//|||||||||||||||||||||||||||||||||| CONFIGURATION - TONEMAPPING ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| CONFIGURATION - TONEMAPPING ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| CONFIGURATION - TONEMAPPING ||||||||||||||||||||||||||||||||||

//NOTE: this only applies if a tonemap is enabled
//I would recomend leaving this on, but this very accurately preserves the original game color grade when applying a new tonemap
//because in the original game the tonemapping is baked into the LUTs that get used, good for performance but not for flexibility
#define TONEMAP_PRESERVE_COLOR_GRADE

//#define TONEMAP_NONE //raw untonemapped framebuffer to screen with srgb conversion
//#define TONEMAP_GRAN_TURISMO_7 //personal favorite, not 'filmic' but gives a much more natrual tonal and color response (ironically this matches more closely to the tonal response in the pre-rendered cutscenes)
//#define TONEMAP_AGX
//#define TONEMAP_UCHIMURA
//#define TONEMAP_REINHARD
//#define TONEMAP_REINHARD2
//#define TONEMAP_UNCHARTED2
//#define TONEMAP_ACES
//#define TONEMAP_ACES_FITTED //this is what the game is definetly using
//#define TONEMAP_FILMIC
//#define TONEMAP_UNREAL_3
//#define TONEMAP_KHRONOS_NEUTRAL
//#define TONEMAP_LOTTES
//#define TONEMAP_EXPONENTIAL
//#define TONEMAP_EXPONENTIAL_SQUARED
//#define TONEMAP_MGSV //(metal gear solid v)
//#define TONEMAP_TONY_MC_MAP_FACE

//|||||||||||||||||||||||||||||||||| RESOURCES ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| RESOURCES ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| RESOURCES ||||||||||||||||||||||||||||||||||
//resources passed in to the shader

//SamplerComparisonState View_SharedBilinearClampedSampler : register(s0); // can't disambiguate

SamplerState View_SharedBilinearClampedSampler : register(s0); // can't disambiguate

Texture3D<float4> View_SpatiotemporalBlueNoiseVolumeTexture : register(t0);

Texture2D<float4> ColorTexture : register(t1);
Texture2D<float4> GlareTexture : register(t2);
Texture2D<float4> CompositeSDRTexture : register(t3);

Texture3D<float4> BT709PQToBT2020PQLUT : register(t4);
Texture3D<float4> BT2020PQTosRGBLUT : register(t5);

//|||||||||||||||||||||||||||||||||| CONSTANT BUFFERS ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| CONSTANT BUFFERS ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| CONSTANT BUFFERS ||||||||||||||||||||||||||||||||||
//game data passed in to the shader

cbuffer _Globals : register(b0) 
{
	float4 PostprocessInput0Size : packoffset(c0.x);
	float4 PostprocessInput1Size : packoffset(c1.x);
	float4 PostprocessInput2Size : packoffset(c2.x);
	float4 PostprocessInput3Size : packoffset(c3.x);
	float4 PostprocessInput4Size : packoffset(c4.x);
	float4 PostprocessInput5Size : packoffset(c5.x);
	float4 PostprocessInput6Size : packoffset(c6.x);
	float4 PostprocessInput7Size : packoffset(c7.x);
	float4 PostprocessInput8Size : packoffset(c8.x);
	float4 PostprocessInput9Size : packoffset(c9.x);
	float4 PostprocessInput10Size : packoffset(c10.x);
	float4 PostprocessInput0MinMax : packoffset(c11.x);
	float4 PostprocessInput1MinMax : packoffset(c12.x);
	float4 PostprocessInput2MinMax : packoffset(c13.x);
	float4 PostprocessInput3MinMax : packoffset(c14.x);
	float4 PostprocessInput4MinMax : packoffset(c15.x);
	float4 PostprocessInput5MinMax : packoffset(c16.x);
	float4 PostprocessInput6MinMax : packoffset(c17.x);
	float4 PostprocessInput7MinMax : packoffset(c18.x);
	float4 PostprocessInput8MinMax : packoffset(c19.x);
	float4 PostprocessInput9MinMax : packoffset(c20.x);
	float4 PostprocessInput10MinMax : packoffset(c21.x);
	float4 ViewportSize : packoffset(c22.x);
	int4 ViewportRect : packoffset(c23.x);
	float4 ScreenPosToPixel : packoffset(c24.x);
	float4 SceneColorBufferUVViewport : packoffset(c25.x);
	float3 MappingPolynomial : packoffset(c26.x);
	float2 ViewportColor_Extent : packoffset(c27.x);
	float2 ViewportColor_ExtentInverse : packoffset(c27.z);
	float2 ViewportColor_ScreenPosToViewportScale : packoffset(c28.x);
	float2 ViewportColor_ScreenPosToViewportBias : packoffset(c28.z);
	int2 ViewportColor_ViewportMin : packoffset(c29.x);
	int2 ViewportColor_ViewportMax : packoffset(c29.z);
	float2 ViewportColor_ViewportSize : packoffset(c30.x);
	float2 ViewportColor_ViewportSizeInverse : packoffset(c30.z);
	float2 ViewportColor_UVViewportMin : packoffset(c31.x);
	float2 ViewportColor_UVViewportMax : packoffset(c31.z);
	float2 ViewportColor_UVViewportSize : packoffset(c32.x);
	float2 ViewportColor_UVViewportSizeInverse : packoffset(c32.z);
	float2 ViewportColor_UVViewportBilinearMin : packoffset(c33.x);
	float2 ViewportColor_UVViewportBilinearMax : packoffset(c33.z);
	float2 ViewportGlare_Extent : packoffset(c34.x);
	float2 ViewportGlare_ExtentInverse : packoffset(c34.z);
	float2 ViewportGlare_ScreenPosToViewportScale : packoffset(c35.x);
	float2 ViewportGlare_ScreenPosToViewportBias : packoffset(c35.z);
	int2 ViewportGlare_ViewportMin : packoffset(c36.x);
	int2 ViewportGlare_ViewportMax : packoffset(c36.z);
	float2 ViewportGlare_ViewportSize : packoffset(c37.x);
	float2 ViewportGlare_ViewportSizeInverse : packoffset(c37.z);
	float2 ViewportGlare_UVViewportMin : packoffset(c38.x);
	float2 ViewportGlare_UVViewportMax : packoffset(c38.z);
	float2 ViewportGlare_UVViewportSize : packoffset(c39.x);
	float2 ViewportGlare_UVViewportSizeInverse : packoffset(c39.z);
	float2 ViewportGlare_UVViewportBilinearMin : packoffset(c40.x);
	float2 ViewportGlare_UVViewportBilinearMax : packoffset(c40.z);
	float2 ViewportDestination_Extent : packoffset(c41.x);
	float2 ViewportDestination_ExtentInverse : packoffset(c41.z);
	float2 ViewportDestination_ScreenPosToViewportScale : packoffset(c42.x);
	float2 ViewportDestination_ScreenPosToViewportBias : packoffset(c42.z);
	int2 ViewportDestination_ViewportMin : packoffset(c43.x);
	int2 ViewportDestination_ViewportMax : packoffset(c43.z);
	float2 ViewportDestination_ViewportSize : packoffset(c44.x);
	float2 ViewportDestination_ViewportSizeInverse : packoffset(c44.z);
	float2 ViewportDestination_UVViewportMin : packoffset(c45.x);
	float2 ViewportDestination_UVViewportMax : packoffset(c45.z);
	float2 ViewportDestination_UVViewportSize : packoffset(c46.x);
	float2 ViewportDestination_UVViewportSizeInverse : packoffset(c46.z);
	float2 ViewportDestination_UVViewportBilinearMin : packoffset(c47.x);
	float2 ViewportDestination_UVViewportBilinearMax : packoffset(c47.z);
	float4 VignetteContext : packoffset(c48.x);
	float4 GlareContext : packoffset(c49.x);
	float4 NoiseContext : packoffset(c50.x);
	float4 HDRCompositionContext : packoffset(c51.x);
	float4 HDRCompositionContextColor : packoffset(c52.x);
	float4 CompositeSurfaceContext : packoffset(c53.x);
	float4 DeviceCorrectorContext : packoffset(c54.x);

#if !defined(GAME_VERSION_1_0_0_3)
	float4 DevelopmentContext : packoffset(c55.x);
#endif

};

cbuffer View : register(b1) 
{
	float4x4 View_TranslatedWorldToClip : packoffset(c0.x);
	float4x4 View_WorldToOrthographicClip : packoffset(c4.x);
	float4x4 View_TranslatedWorldToOrthographicClip : packoffset(c8.x);
	float4x4 View_WorldToClip : packoffset(c12.x);
	float4x4 View_ClipToWorld : packoffset(c16.x);
	float4x4 View_TranslatedWorldToView : packoffset(c20.x);
	float4x4 View_ViewToTranslatedWorld : packoffset(c24.x);
	float4x4 View_TranslatedWorldToCameraView : packoffset(c28.x);
	float4x4 View_CameraViewToTranslatedWorld : packoffset(c32.x);
	float4x4 View_ViewToClip : packoffset(c36.x);
	float4x4 View_ViewToClipNoAA : packoffset(c40.x);
	float4x4 View_ClipToView : packoffset(c44.x);
	float4x4 View_ClipToTranslatedWorld : packoffset(c48.x);
	float4x4 View_SVPositionToTranslatedWorld : packoffset(c52.x);
	float4x4 View_ScreenToWorld : packoffset(c56.x);
	float4x4 View_ScreenToTranslatedWorld : packoffset(c60.x);
	float4x4 View_MobileMultiviewShadowTransform : packoffset(c64.x);
	float3 View_ViewForward : packoffset(c68.x);
	float PrePadding_View_1100 : packoffset(c68.w);
	float3 View_ViewUp : packoffset(c69.x);
	float PrePadding_View_1116 : packoffset(c69.w);
	float3 View_ViewRight : packoffset(c70.x);
	float PrePadding_View_1132 : packoffset(c70.w);
	float3 View_HMDViewNoRollUp : packoffset(c71.x);
	float PrePadding_View_1148 : packoffset(c71.w);
	float3 View_HMDViewNoRollRight : packoffset(c72.x);
	float PrePadding_View_1164 : packoffset(c72.w);
	float4 View_InvDeviceZToWorldZTransform : packoffset(c73.x);
	float4 View_ScreenPositionScaleBias : packoffset(c74.x);
	float3 View_WorldCameraOrigin : packoffset(c75.x);
	float PrePadding_View_1212 : packoffset(c75.w);
	float3 View_TranslatedWorldCameraOrigin : packoffset(c76.x);
	float PrePadding_View_1228 : packoffset(c76.w);
	float3 View_WorldViewOrigin : packoffset(c77.x);
	float PrePadding_View_1244 : packoffset(c77.w);
	float3 View_PreViewTranslation : packoffset(c78.x);
	float PrePadding_View_1260 : packoffset(c78.w);
	float4x4 View_PrevProjection : packoffset(c79.x);
	float4x4 View_PrevViewProj : packoffset(c83.x);
	float4x4 View_PrevViewRotationProj : packoffset(c87.x);
	float4x4 View_PrevViewToClip : packoffset(c91.x);
	float4x4 View_PrevClipToView : packoffset(c95.x);
	float4x4 View_PrevTranslatedWorldToClip : packoffset(c99.x);
	float4x4 View_PrevWorldToOrthographicClip : packoffset(c103.x);
	float4x4 View_PrevTranslatedWorldToOrthographicClip : packoffset(c107.x);
	float4x4 View_PrevTranslatedWorldToView : packoffset(c111.x);
	float4x4 View_PrevViewToTranslatedWorld : packoffset(c115.x);
	float4x4 View_PrevTranslatedWorldToCameraView : packoffset(c119.x);
	float4x4 View_PrevCameraViewToTranslatedWorld : packoffset(c123.x);
	float3 View_PrevWorldCameraOrigin : packoffset(c127.x);
	float PrePadding_View_2044 : packoffset(c127.w);
	float3 View_PrevWorldViewOrigin : packoffset(c128.x);
	float PrePadding_View_2060 : packoffset(c128.w);
	float3 View_PrevPreViewTranslation : packoffset(c129.x);
	float PrePadding_View_2076 : packoffset(c129.w);
	float4x4 View_PrevInvViewProj : packoffset(c130.x);
	float4x4 View_PrevScreenToTranslatedWorld : packoffset(c134.x);
	float4x4 View_ClipToPrevClip : packoffset(c138.x);
	float4x4 View_ClipToPrevClipWithoutTranslation : packoffset(c142.x);
	float4x4 View_ProjectionToWorld : packoffset(c146.x);
	float4x4 View_WorldToProjection : packoffset(c150.x);
	float4 View_TemporalAAJitter : packoffset(c154.x);
	float4 View_TemporalSamplerBias : packoffset(c155.x);
	float4 View_GlobalClippingPlane : packoffset(c156.x);
	float2 View_FieldOfViewWideAngles : packoffset(c157.x);
	float2 View_PrevFieldOfViewWideAngles : packoffset(c157.z);
	float4 View_ViewRectMin : packoffset(c158.x);
	float4 View_ViewSizeAndInvSize : packoffset(c159.x);
	float4 View_LightProbeSizeRatioAndInvSizeRatio : packoffset(c160.x);
	float4 View_BufferSizeAndInvSize : packoffset(c161.x);
	float4 View_BufferBilinearUVMinMax : packoffset(c162.x);
	float4 View_ScreenToViewSpace : packoffset(c163.x);
	int View_NumSceneColorMSAASamples : packoffset(c164.x);
	float View_PreExposure : packoffset(c164.y);
	float View_OneOverPreExposure : packoffset(c164.z);
	float View_PreviousPreExposure : packoffset(c164.w);
	float View_PreviousOneOverPreExposure : packoffset(c165.x);
	float PrePadding_View_2644 : packoffset(c165.y);
	float PrePadding_View_2648 : packoffset(c165.z);
	float PrePadding_View_2652 : packoffset(c165.w);
	float4 View_DiffuseOverrideParameter : packoffset(c166.x);
	float4 View_SpecularOverrideParameter : packoffset(c167.x);
	float4 View_NormalOverrideParameter : packoffset(c168.x);
	float4 View_RoughnessOverrideParameter : packoffset(c169.x);
	float View_PrevFrameGameTime : packoffset(c170.x);
	float View_PrevFrameRealTime : packoffset(c170.y);
	float View_OutOfBoundsMask : packoffset(c170.z);
	float PrePadding_View_2732 : packoffset(c170.w);
	float3 View_WorldCameraMovementSinceLastFrame : packoffset(c171.x);
	float View_CullingSign : packoffset(c171.w);
	float View_NearPlane : packoffset(c172.x);
	float View_AdaptiveTessellationFactor : packoffset(c172.y);
	float View_GameTime : packoffset(c172.z);
	float View_RealTime : packoffset(c172.w);
	float View_DeltaTime : packoffset(c173.x);
	float View_EnvironmentTime : packoffset(c173.y);
	float View_PreviousEnvironmentTime : packoffset(c173.z);
	float View_MaterialTextureMipBias : packoffset(c173.w);
	float View_MaterialTextureDerivativeMultiply : packoffset(c174.x);
	int View_Random : packoffset(c174.y);
	int View_FrameNumber : packoffset(c174.z);
	int View_StateFrameIndexMod8 : packoffset(c174.w);
	int View_StateFrameIndex : packoffset(c175.x);
	int View_StateFrameDelayIndex : packoffset(c175.y);
	int View_DebugViewModeMask : packoffset(c175.z);
	float View_CameraCut : packoffset(c175.w);
	float View_UnlitViewmodeMask : packoffset(c176.x);
	float PrePadding_View_2820 : packoffset(c176.y);
	float PrePadding_View_2824 : packoffset(c176.z);
	float PrePadding_View_2828 : packoffset(c176.w);
	float4 View_DirectionalLightColor : packoffset(c177.x);
	float3 View_DirectionalLightDirection : packoffset(c178.x);
	float PrePadding_View_2860 : packoffset(c178.w);
	float4 View_TranslucencyLightingVolumeMin[2] : packoffset(c179.x);
	float4 View_TranslucencyLightingVolumeInvSize[2] : packoffset(c181.x);
	float4 View_TranslucencyLightingVolumeDistance[2] : packoffset(c183.x);
	float4 View_TemporalAAParams : packoffset(c185.x);
	float4 View_CircleDOFParams : packoffset(c186.x);
	int View_ForceDrawAllVelocities : packoffset(c187.x);
	float View_DepthOfFieldIntensity : packoffset(c187.y);
	float View_DepthOfFieldFocalDistance : packoffset(c187.z);
	float View_DepthOfFieldFstop : packoffset(c187.w);
	float View_LightModifierEnvironmentLight : packoffset(c188.x);
	float View_LightModifierDirectionalLight : packoffset(c188.y);
	float View_MotionBlurNormalizedToPixel : packoffset(c188.z);
	float View_bSubsurfacePostprocessEnabled : packoffset(c188.w);
	float4 View_GeneralPurposeTweak : packoffset(c189.x);
	float View_DemosaicVposOffset : packoffset(c190.x);
	float PrePadding_View_3044 : packoffset(c190.y);
	float PrePadding_View_3048 : packoffset(c190.z);
	float PrePadding_View_3052 : packoffset(c190.w);
	float3 View_IndirectLightingColorScale : packoffset(c191.x);
	float View_AtmosphericFogSunPower : packoffset(c191.w);
	float View_AtmosphericFogPower : packoffset(c192.x);
	float View_AtmosphericFogDensityScale : packoffset(c192.y);
	float View_AtmosphericFogDensityOffset : packoffset(c192.z);
	float View_AtmosphericFogGroundOffset : packoffset(c192.w);
	float View_AtmosphericFogDistanceScale : packoffset(c193.x);
	float View_AtmosphericFogAltitudeScale : packoffset(c193.y);
	float View_AtmosphericFogHeightScaleRayleigh : packoffset(c193.z);
	float View_AtmosphericFogStartDistance : packoffset(c193.w);
	float View_AtmosphericFogDistanceOffset : packoffset(c194.x);
	float View_AtmosphericFogSunDiscScale : packoffset(c194.y);
	float PrePadding_View_3112 : packoffset(c194.z);
	float PrePadding_View_3116 : packoffset(c194.w);
	float4 View_AtmosphereLightDirection[2] : packoffset(c195.x);
	float4 View_AtmosphereLightColor[2] : packoffset(c197.x);
	float4 View_AtmosphereLightColorGlobalPostTransmittance[2] : packoffset(c199.x);
	float4 View_AtmosphereLightDiscLuminance[2] : packoffset(c201.x);
	float4 View_AtmosphereLightDiscCosHalfApexAngle[2] : packoffset(c203.x);
	float4 View_SkyViewLutSizeAndInvSize : packoffset(c205.x);
	float3 View_SkyWorldCameraOrigin : packoffset(c206.x);
	float PrePadding_View_3308 : packoffset(c206.w);
	float4 View_SkyPlanetCenterAndViewHeight : packoffset(c207.x);
	float4x4 View_SkyViewLutReferential : packoffset(c208.x);
	float4 View_SkyAtmosphereSkyLuminanceFactor : packoffset(c212.x);
	float View_SkyAtmospherePresentInScene : packoffset(c213.x);
	float View_SkyAtmosphereHeightFogContribution : packoffset(c213.y);
	float View_SkyAtmosphereBottomRadiusKm : packoffset(c213.z);
	float View_SkyAtmosphereTopRadiusKm : packoffset(c213.w);
	float4 View_SkyAtmosphereCameraAerialPerspectiveVolumeSizeAndInvSize : packoffset(c214.x);
	float View_SkyAtmosphereAerialPerspectiveStartDepthKm : packoffset(c215.x);
	float View_SkyAtmosphereCameraAerialPerspectiveVolumeDepthResolution : packoffset(c215.y);
	float View_SkyAtmosphereCameraAerialPerspectiveVolumeDepthResolutionInv : packoffset(c215.z);
	float View_SkyAtmosphereCameraAerialPerspectiveVolumeDepthSliceLengthKm : packoffset(c215.w);
	float View_SkyAtmosphereCameraAerialPerspectiveVolumeDepthSliceLengthKmInv : packoffset(c216.x);
	float View_SkyAtmosphereApplyCameraAerialPerspectiveVolume : packoffset(c216.y);
	int View_AtmosphericFogRenderMask : packoffset(c216.z);
	int View_AtmosphericFogInscatterAltitudeSampleNum : packoffset(c216.w);
	float4 View_EnvironmentLightFallbackContext : packoffset(c217.x);
	float4 View_FogContextDirectionalLightDirection : packoffset(c218.x);
	float4 View_FogContextDirectionalLightColor : packoffset(c219.x);
	float View_FogContextMediumIOR : packoffset(c220.x);
	int View_NumberOfFogContext : packoffset(c220.y);
	int PrePadding_View_3528 : packoffset(c220.z);
	int PrePadding_View_3532 : packoffset(c220.w);
	float4 View_FogContextHeightBasedContextAlbedo[4] : packoffset(c221.x);
	float4 View_FogContextHeightBasedContextDensity[4] : packoffset(c225.x);
	float4 View_FogContextTransmitContextAlbedo[4] : packoffset(c229.x);
	float4 View_FogContextTransmitContextDensity[4] : packoffset(c233.x);
	float4 View_FogContextRegionContext[4] : packoffset(c237.x);
	float4 View_FogContextLightContext[4] : packoffset(c241.x);
	float3 View_NormalCurvatureToRoughnessScaleBias : packoffset(c245.x);
	float View_RenderingReflectionCaptureMask : packoffset(c245.w);
	float View_RealTimeReflectionCapture : packoffset(c246.x);
	float View_RealTimeReflectionCapturePreExposure : packoffset(c246.y);
	float PrePadding_View_3944 : packoffset(c246.z);
	float PrePadding_View_3948 : packoffset(c246.w);
	float4 View_AmbientCubemapTint : packoffset(c247.x);
	float View_AmbientCubemapIntensity : packoffset(c248.x);
	float View_WetnessIntensity : packoffset(c248.y);
	float View_SkyLightApplyPrecomputedBentNormalShadowingFlag : packoffset(c248.z);
	float View_SkyLightAffectReflectionFlag : packoffset(c248.w);
	float View_SkyLightAffectGlobalIlluminationFlag : packoffset(c249.x);
	float PrePadding_View_3988 : packoffset(c249.y);
	float PrePadding_View_3992 : packoffset(c249.z);
	float PrePadding_View_3996 : packoffset(c249.w);
	float4 View_SkyLightColor : packoffset(c250.x);
	float4 View_MobileSkyIrradianceEnvironmentMap[7] : packoffset(c251.x);
	float View_MobilePreviewMode : packoffset(c258.x);
	float View_HMDEyePaddingOffset : packoffset(c258.y);
	float View_ReflectionCubemapMaxMip : packoffset(c258.z);
	float View_ShowDecalsMask : packoffset(c258.w);
	int View_DistanceFieldAOSpecularOcclusionMode : packoffset(c259.x);
	float View_IndirectCapsuleSelfShadowingIntensity : packoffset(c259.y);
	float PrePadding_View_4152 : packoffset(c259.z);
	float PrePadding_View_4156 : packoffset(c259.w);
	float3 View_ReflectionEnvironmentRoughnessMixingScaleBiasAndLargestWeight : packoffset(c260.x);
	int View_StereoPassIndex : packoffset(c260.w);
	float4 View_GlobalVolumeCenterAndExtent[4] : packoffset(c261.x);
	float4 View_GlobalVolumeWorldToUVAddAndMul[4] : packoffset(c265.x);
	float View_GlobalVolumeDimension : packoffset(c269.x);
	float View_GlobalVolumeTexelSize : packoffset(c269.y);
	float View_MaxGlobalDistance : packoffset(c269.z);
	float PrePadding_View_4316 : packoffset(c269.w);
	int2 View_CursorPosition : packoffset(c270.x);
	float View_bCheckerboardSubsurfaceProfileRendering : packoffset(c270.z);
	float PrePadding_View_4332 : packoffset(c270.w);
	float3 View_PrecomputedLightVolumeGridZParams : packoffset(c271.x);
	float PrePadding_View_4348 : packoffset(c271.w);
	float3 View_PrecomputedLightVolumeGridSize : packoffset(c272.x);
	float PrePadding_View_4364 : packoffset(c272.w);
	int3 View_PrecomputedLightVolumeGridSizeInt : packoffset(c273.x);
	int PrePadding_View_4380 : packoffset(c273.w);
	float3 View_VolumetricFogGridSize : packoffset(c274.x);
	float PrePadding_View_4396 : packoffset(c274.w);
	float3 View_VolumetricFogGridSizeReciprocal : packoffset(c275.x);
	float PrePadding_View_4412 : packoffset(c275.w);
	float3 View_VolumetricFogGridZParameter : packoffset(c276.x);
	float PrePadding_View_4428 : packoffset(c276.w);
	float3 View_VolumetricFogGridZParameterReciprocal : packoffset(c277.x);
	float PrePadding_View_4444 : packoffset(c277.w);
	float3 View_VolumetricFogGridCoordinateSolver : packoffset(c278.x);
	float PrePadding_View_4460 : packoffset(c278.w);
	float3 View_VolumetricFogGridCoordinateMinimum : packoffset(c279.x);
	float PrePadding_View_4476 : packoffset(c279.w);
	float3 View_VolumetricFogGridCoordinateMaximum : packoffset(c280.x);
	float View_VolumetricFogMaxDistance : packoffset(c280.w);
	float3 View_VolumetricLightmapWorldToUVScale : packoffset(c281.x);
	float PrePadding_View_4508 : packoffset(c281.w);
	float3 View_VolumetricLightmapWorldToUVAdd : packoffset(c282.x);
	float PrePadding_View_4524 : packoffset(c282.w);
	float3 View_VolumetricLightmapIndirectionTextureSize : packoffset(c283.x);
	float View_VolumetricLightmapBrickSize : packoffset(c283.w);
	float3 View_VolumetricLightmapBrickTexelSize : packoffset(c284.x);
	float View_StereoIPD : packoffset(c284.w);
	float View_IndirectLightingCacheShowFlag : packoffset(c285.x);
	float View_EyeToPixelSpreadAngle : packoffset(c285.y);
	float PrePadding_View_4568 : packoffset(c285.z);
	float PrePadding_View_4572 : packoffset(c285.w);
	float4x4 View_WorldToVirtualTexture : packoffset(c286.x);
	float4 View_XRPassthroughCameraUVs[2] : packoffset(c290.x);
	int View_VirtualTextureFeedbackStride : packoffset(c292.x);
	int PrePadding_View_4676 : packoffset(c292.y);
	int PrePadding_View_4680 : packoffset(c292.z);
	int PrePadding_View_4684 : packoffset(c292.w);
	float4 View_RuntimeVirtualTextureMipLevel : packoffset(c293.x);
	float2 View_RuntimeVirtualTexturePackHeight : packoffset(c294.x);
	float PrePadding_View_4712 : packoffset(c294.z);
	float PrePadding_View_4716 : packoffset(c294.w);
	float4 View_RuntimeVirtualTextureDebugParams : packoffset(c295.x);
	int View_FarShadowStaticMeshLODBias : packoffset(c296.x);
	float View_MinRoughness : packoffset(c296.y);
	float PrePadding_View_4744 : packoffset(c296.z);
	float PrePadding_View_4748 : packoffset(c296.w);
	float4 View_HairRenderInfo : packoffset(c297.x);
	int View_EnableSkyLight : packoffset(c298.x);
	int View_HairRenderInfoBits : packoffset(c298.y);
	int View_HairComponents : packoffset(c298.z);
	int View_DebugContext : packoffset(c298.w);
	int View_DebugTweak : packoffset(c299.x);
};

//|||||||||||||||||||||||||||||||||| STRUCTS ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| STRUCTS ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| STRUCTS ||||||||||||||||||||||||||||||||||

struct PixelInput
{
    float2 ScreenUV           : TEXCOORD0; //Present in the signature; unused here?
    float4 VignetteRayContext : TEXCOORD1;
    float4 Position           : SV_Position;
};

struct PixelOutput
{
    float4 Color : SV_Target0;
};

//||||||||||||||||||||||||||||||| UV |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| UV |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| UV |||||||||||||||||||||||||||||||

float2 MakeDestinationUV(float2 pixelPosition)
{
    float2 viewportPixel = pixelPosition - float2(ViewportDestination_ViewportMin);
    return saturate(viewportPixel * ViewportDestination_ViewportSizeInverse);
}

float2 ApplyDeviceCorrectorSamplingGrid(float2 destinationUV, float2 pixelPosition)
{
    // DeviceCorrectorContext.z selects the center of the containing 2x2 block.
    if (DeviceCorrectorContext.z != 0.0f)
    {
        float2 viewportPixel = pixelPosition - float2(ViewportDestination_ViewportMin);
        float2 blockCenter = round(viewportPixel * 0.5f) * 2.0f + 1.0f;
        return saturate(blockCenter * ViewportDestination_ViewportSizeInverse);
    }
    return destinationUV;
}

float2 DestinationUVToColorTextureUV(float2 destinationUV)
{
    float2 viewportPixel = destinationUV * ViewportColor_ViewportSize + float2(ViewportColor_ViewportMin);
    float2 textureUV = viewportPixel * ViewportColor_ExtentInverse;
    return clamp(textureUV, ViewportColor_UVViewportBilinearMin, ViewportColor_UVViewportBilinearMax);
}

float2 DestinationUVToGlareTextureUV(float2 destinationUV)
{
    float2 viewportPixel = destinationUV * ViewportGlare_ViewportSize + float2(ViewportGlare_ViewportMin);
    float2 textureUV = viewportPixel * ViewportGlare_ExtentInverse;
    return clamp(textureUV, ViewportGlare_UVViewportBilinearMin, ViewportGlare_UVViewportBilinearMax);
}

float2 MakeCenteredCompositeUV(float2 destinationUV)
{
    // Fit a 16:9 surface inside the destination while retaining a centered UV.
    float2 fitScale;
    fitScale.x = min(ViewportDestination_ViewportSize.x * (9.0f / 16.0f) * ViewportDestination_ViewportSizeInverse.y, 1.0f);
    fitScale.y = min(ViewportDestination_ViewportSize.y * (16.0f / 9.0f) * ViewportDestination_ViewportSizeInverse.x, 1.0f);
    return (destinationUV - 0.5f) * fitScale + 0.5f;
}

//||||||||||||||||||||||||||||||| BLOOM |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| BLOOM |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| BLOOM |||||||||||||||||||||||||||||||
//NOTE: for the most part this is just the final composition stage of the bloom
//there are a number of draw passes prior where bloom is being calculated via downsample/upsample blurring

float3 ApplyGlare(float3 sceneColor, float2 correctedDestinationUV)
{
    float2 glareUV = DestinationUVToGlareTextureUV(correctedDestinationUV);
    float4 glareSample = min(GlareTexture.SampleLevel(View_SharedBilinearClampedSampler, glareUV, 0.0f), 64512.0f.xxxx);
    float3 glareColor = glareSample.rgb;

	//quick debug checking terms
	//return sceneColor; 
	//return glareColor * 15;

	//bloom is broken on earlier game versions?
	//it seems like the render target looks like it is stale and we see an older non-cleared version of the scene right before an apply that sticks to the screen
	//EDIT: this seemignly only happened one time... wtf? I can't re-create it
	//#if defined(GAME_VERSION_1_0_0_3)
		//return glareColor;
	//#endif

	#if defined(BLOOM_PHYSICAL)
		//more physically-based style bloom, energy conserving and non-additive
		//NOTE: the 15 is arbitrary, boosting the glare to be roughly the same brightness as the original exposure
		float3 bloomedSceneColor = lerp(sceneColor, glareColor * 15, BLOOM_PHYSICAL_INTENSITY);
		return bloomedSceneColor;
	#elif defined(BLOOM_ADDITIVE)
		//additive bloom, simple
		float3 bloomedSceneColor = sceneColor + glareColor * BLOOM_ADDITIVE_INTENSITY;
		return bloomedSceneColor;
	#else
		//original game
		#if defined(GAME_VERSION_1_0_0_3)
			// Matches the 1.0.0.3 DXIL: the last term is GlareTexture.a,
			// not GlareContext.y.
			return sceneColor + GlareContext.x * (glareColor - sceneColor + glareSample.a * sceneColor);
		#else
			return sceneColor + GlareContext.x * (glareColor - sceneColor + GlareContext.y * sceneColor);
		#endif
	#endif
}

//||||||||||||||||||||||||||||||| VIGNETTE |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| VIGNETTE |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| VIGNETTE |||||||||||||||||||||||||||||||

float ComputeVignette(float3 vignetteRayContext)
{
    float normalizedDepth = vignetteRayContext.z / max(0.001f, VignetteContext.y);
    float depthSquared = normalizedDepth * normalizedDepth;
    float rayLengthSquared = max(1.0e-5f, dot(float3(vignetteRayContext.xy, normalizedDepth), float3(vignetteRayContext.xy, normalizedDepth)));
    float radialFactor = depthSquared / rayLengthSquared;
    float radialFourthPower = radialFactor * radialFactor;
    return 1.0f + VignetteContext.x * (radialFourthPower - 1.0f);
}

//|||||||||||||||||||||||||||||||||||||||||||| AUTO EXPOSURE ||||||||||||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||||||||||||| AUTO EXPOSURE ||||||||||||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||||||||||||| AUTO EXPOSURE ||||||||||||||||||||||||||||||||||||||||||||

float2 ApplyAutoExposureCenterFocus(float2 uv)
{
    float focus = saturate(AUTO_EXPOSURE_CENTER_FOCUS);
    return lerp(uv, float2(0.5f, 0.5f), focus);
}

//this is a scuffed way of calculating exposure...
//ideally just before this we could generate mips of the raw framebuffer
//this would allow us to very efficently sample the lowest mip level once, natrually the mips are averages
//and we can use that average to calculate an exposure factor
float CalculateAutoExposure()
{
    float accumulatedLogLuminance = 0.0f;

    [unroll]
    for (uint y = 0; y < AUTO_EXPOSURE_GRID_Y; ++y)
    {
        [unroll]
        for (uint x = 0; x < AUTO_EXPOSURE_GRID_X; ++x)
        {
            //Sample the center of each grid cell
			//IMPORTANT NOTE: this has to be a specific points, not relative to the current pixel
			//So that way every pixel is sampling the same set of points
			float2 destinationUV = (float2(x, y) + 0.5f) / float2(AUTO_EXPOSURE_GRID_X, AUTO_EXPOSURE_GRID_Y);
			destinationUV = ApplyAutoExposureCenterFocus(destinationUV);

            float2 colorUV = DestinationUVToColorTextureUV(destinationUV);

            float3 sampleColor = ColorTexture.SampleLevel(View_SharedBilinearClampedSampler, colorUV, 0.0f).rgb;

            sampleColor = max(sampleColor, 0.0f.xxx);

			//float luminance = max(Luminance(sampleColor), 1.0e-5f);
            float luminance = max(LuminanceRec709(sampleColor), 1.0e-5f);

            //log averaging prevents a few very bright pixels from dominating the exposure calculation.
            accumulatedLogLuminance += log2(luminance);
        }
    }

    const float sampleCount = AUTO_EXPOSURE_GRID_X * AUTO_EXPOSURE_GRID_Y;

    float averageLuminance = exp2(accumulatedLogLuminance / sampleCount);
    float meteredEV  = log2(AUTO_EXPOSURE_MIDDLE_GRAY / averageLuminance);
	float exposureEV = meteredEV * AUTO_EXPOSURE_STRENGTH + AUTO_EXPOSURE_COMPENSATION_EV;

    exposureEV = clamp(exposureEV, AUTO_EXPOSURE_MIN_EV, AUTO_EXPOSURE_MAX_EV);

    return exp2(exposureEV);
}

//||||||||||||||||||||||||||||||| IMAGE ADJUSTMENTS |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| IMAGE ADJUSTMENTS |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| IMAGE ADJUSTMENTS |||||||||||||||||||||||||||||||
//not in the original game, but just granting more control to users to tune their image

float3 AdjustImage(float3 inputColor)
{
    float3 color = inputColor;

    //Lift adjusts the black level.
    color += ADJUSTMENT_LIFT;

    //Brightness expressed as exposure stops.
    color *= exp2(ADJUSTMENT_BRIGHTNESS_EV);

    //Per-channel gain / white balance.
    color *= ADJUSTMENT_GAIN;

    //Contrast around a configurable linear-light pivot.
    color = (color - ADJUSTMENT_CONTRAST_PIVOT.xxx) * ADJUSTMENT_CONTRAST + ADJUSTMENT_CONTRAST_PIVOT.xxx;

    //Prevent negative values from creating invalid gamma results.
    color = max(color, 0.0f.xxx);

    //Standard saturation.
    //float luminance = Luminance(color);
	float luminance = LuminanceRec709(color);
    color = lerp(luminance.xxx, color, ADJUSTMENT_SATURATION);

    //Vibrance affects less-saturated colors more strongly.
    //luminance = Luminance(color);
	luminance = LuminanceRec709(color);

    float channelMax = max(color.r, max(color.g, color.b));
    float channelMin = min(color.r, min(color.g, color.b));
    float chroma = channelMax - channelMin;

    float vibranceAmount = ADJUSTMENT_VIBRANCE * (1.0f - saturate(chroma));

    color = lerp(luminance.xxx, color, max(0.0f, 1.0f + vibranceAmount));

    //Multiplicative tinting preserves black.
    float3 tintMultiplier = lerp(1.0f.xxx, max(ADJUSTMENT_TINT_COLOR, 0.0f.xxx), saturate(ADJUSTMENT_TINT_FACTOR));

    color *= tintMultiplier;

    // Artistic gamma. Gamma > 1 brightens the midtones.
    color = pow(max(color, 0.0f.xxx), rcp(max(ADJUSTMENT_GAMMA, 1.0e-4f)));

    return color;
}

//||||||||||||||||||||||||||||||| TONEMAPPING / COLOR GRADING (ORIGINAL GAME) |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| TONEMAPPING / COLOR GRADING (ORIGINAL GAME) |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| TONEMAPPING / COLOR GRADING (ORIGINAL GAME) |||||||||||||||||||||||||||||||

float3 Linear100NitsToPQ(float3 linearColor)
{
    const float m1 = 0.1593017578125f;
    const float m2 = 78.84375f;
    const float c1 = 0.8359375f;
    const float c2 = 18.8515625f;
    const float c3 = 18.6875f;
    float3 normalizedLuminance = saturate(linearColor * 0.01f);
    float3 luminancePower = pow(normalizedLuminance, m1);
    return saturate(pow(max((c1 + c2 * luminancePower) / (1.0f + c3 * luminancePower), 0.0f), m2));
}

//NOTE: it appears that the game's tonemapping is baked into the LUTs
//NOTE 2: this was checked later but it looks like the game is using ACES 1... pretty standard for UE
float3 SampleColorConversionLUTs(float3 linearSceneColor)
{
    // Both LUTs are 32^3; map [0,1] to texel centers [0.5/32,31.5/32].
    const float lutScale = 31.0f / 32.0f;
    const float lutBias  = 0.5f / 32.0f;
    float3 bt709PQ = Linear100NitsToPQ(linearSceneColor);
    float3 bt2020PQ = BT709PQToBT2020PQLUT.SampleLevel(View_SharedBilinearClampedSampler, bt709PQ * lutScale + lutBias, 0.0f).rgb;
    return BT2020PQTosRGBLUT.SampleLevel(View_SharedBilinearClampedSampler, bt2020PQ * lutScale + lutBias, 0.0f).rgb;
}

//||||||||||||||||||||||||||||||| PRESERVING ORIGINAL COLOR GRADE |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| PRESERVING ORIGINAL COLOR GRADE |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| PRESERVING ORIGINAL COLOR GRADE |||||||||||||||||||||||||||||||

float3 PQToLinear100Nits(float3 pq)
{
    const float m1 = 0.1593017578125f;
    const float m2 = 78.84375f;
    const float c1 = 0.8359375f;
    const float c2 = 18.8515625f;
    const float c3 = 18.6875f;

    pq = saturate(pq);

    float3 p = pow(pq, 1.0f / m2);
    float3 n = pow(max((p - c1) / max(c2 - c3 * p, 1e-6f), 0.0f), 1.0f / m1);

    return n * 100.0f;
}

static const float3x3 BT2020ToBT709Mat =
{
    { 1.660491f, -0.587641f, -0.072850f},
    {-0.124550f,  1.132900f, -0.008349f},
    {-0.018151f, -0.100579f,  1.118730f}
};

static const float3x3 ACESInputMatInv =
{
    { 1.76474097f, -0.67577768f, -0.08896329f},
    {-0.14702785f,  1.16025151f, -0.01322366f},
    {-0.03633683f, -0.16243644f,  1.19877327f}
};

static const float3x3 ACESOutputMatInv =
{
    {0.64303825f, 0.31118675f, 0.04577546f},
    {0.05926869f, 0.93143649f, 0.00929492f},
    {0.00596190f, 0.06392902f, 0.93011838f}
};

float3 RRTAndODTFit_Inv(float3 y)
{
    y = clamp(y, 0.0f, 0.9999f);

    float3 A = 0.983729f * y - 1.0f;
    float3 B = 0.4329510f * y - 0.0245786f;
    float3 C = 0.238081f * y + 0.000090537f;

    float3 disc = max(B * B - 4.0f * A * C, 0.0f);
    return max((-B - sqrt(disc)) / (2.0f * A), 0.0f);
}

float3 ApplyTonemap_AcesFitted_Inv(float3 color)
{
    color = mul(ACESOutputMatInv, color);
    color = RRTAndODTFit_Inv(color);
    color = mul(ACESInputMatInv, color);

    return max(color, 0.0f);
}

#define ORIGINAL_GAME_ACES_EXPOSURE 2.0f

float3 SampleGameFinalLinear(float3 linearSceneColor)
{
    float3 encodedSRGB = SampleColorConversionLUTs(linearSceneColor);
	return SRGBToLinear(encodedSRGB);
}

float3 SampleGradedNoTonemapNoSRGB(float3 linearSceneColor)
{
    float3 gameFinalLinear = SampleGameFinalLinear(linearSceneColor);

    // Invert the ACES fitted curve from the game's final display-linear result.
    float3 untonemapped = ApplyTonemap_AcesFitted_Inv(gameFinalLinear);

    // Your ACES fitted match used sceneColor *= 2.0, so inverse gives us that
    // exposed domain. Divide it back down to raw scene scale.
    return untonemapped / ORIGINAL_GAME_ACES_EXPOSURE;
}

//||||||||||||||||||||||||||||||| DITHER |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| DITHER |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| DITHER |||||||||||||||||||||||||||||||

float TriangularBlueNoise(float uniformNoise)
{
    float signedNoise = uniformNoise * 2.0f - 1.0f;
    return sign(signedNoise) * (1.0f - sqrt(1.0f - abs(signedNoise)));
}

float3 ApplyOutputDither(float3 encodedColor, float uniformNoise)
{
    float dither = TriangularBlueNoise(uniformNoise) * (1.0f / 255.0f);
	bool safelyInsideRangeR = abs(encodedColor.r * 2.0f - 1.0f) < (253.0f / 255.0f);
	bool safelyInsideRangeG = abs(encodedColor.g * 2.0f - 1.0f) < (253.0f / 255.0f);
	bool safelyInsideRangeB = abs(encodedColor.b * 2.0f - 1.0f) < (253.0f / 255.0f);

	float3 finalDither = float3(0, 0, 0);

	if(safelyInsideRangeR)
		finalDither.r = dither;

	if(safelyInsideRangeG)
		finalDither.g = dither;

	if(safelyInsideRangeB)
		finalDither.b = dither;

	return saturate(encodedColor + finalDither);
}

//||||||||||||||||||||||||||||||| DEBUG |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| DEBUG |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| DEBUG |||||||||||||||||||||||||||||||

float3 HslToRgb(float3 hsl) 
{
	float H = hsl.x; // Hue [0,1]
	float S = hsl.y; // Saturation [0,1]
	float L = hsl.z; // Lightness [0,1]
	float C = (1.0 - abs(2.0 * L - 1.0)) * S;  // Chroma
	float X = C * (1.0 - abs(fmod(H * 6.0, 2.0) - 1.0));  
	float m = L - C * 0.5;  
	float3 rgb = float3(0, 0, 0);

	if (H < 1.0 / 6.0) 
		rgb = float3(C, X, 0.0);
	else if (H < 2.0 / 6.0) 
		rgb = float3(X, C, 0.0);
	else if (H < 3.0 / 6.0) 
		rgb = float3(0.0, C, X);
	else if (H < 4.0 / 6.0) 
		rgb = float3(0.0, X, C);
	else if (H < 5.0 / 6.0) 
		rgb = float3(X, 0.0, C);
	else 
		rgb = float3(C, 0.0, X);

	return rgb + m;
}

float3 GrangerRainbow(float2 uv) 
{
	float3 hsl = float3(uv.x, min((1.0 - uv.y) * 2.0, 1.0), min((uv.y), 0.5));
	float3 rainbow = HslToRgb(hsl) * (1.0f + (uv.y)) * 2;
	return rainbow;
}

//|||||||||||||||||||||||||||||||||||||||||||| MAIN ||||||||||||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||||||||||||| MAIN ||||||||||||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||||||||||||| MAIN ||||||||||||||||||||||||||||||||||||||||||||

PixelOutput main(PixelInput input)
{
    PixelOutput output;

    float2 destinationUV = MakeDestinationUV(input.Position.xy);
    float2 correctedDestinationUV = ApplyDeviceCorrectorSamplingGrid(destinationUV, input.Position.xy);

    float2 colorUV = DestinationUVToColorTextureUV(correctedDestinationUV);
    float2 compositeUV = MakeCenteredCompositeUV(destinationUV);

    float3 sceneColor = min(ColorTexture.SampleLevel(View_SharedBilinearClampedSampler, colorUV, 0.0f).rgb, 64512.0f.xxx);
    
	#if defined(VIGNETTE_ENABLED)
		sceneColor *= ComputeVignette(input.VignetteRayContext.xyz);
	#endif

	#if defined(BLOOM_ENABLE)
		sceneColor = ApplyGlare(sceneColor, correctedDestinationUV);
	#endif

	#if defined(AUTO_EXPOSURE)
		float autoExposure = CalculateAutoExposure();
		sceneColor *= autoExposure;
	#endif

	//apply any custom artistic adjustments before we tonemap
	sceneColor = AdjustImage(sceneColor);

	#if defined(DEBUG_COLOR_CHART)
		sceneColor = GrangerRainbow(destinationUV);
	#endif

	//|||||||||||||||||||||||||||||||||||||||||||| APPLY TONEMAPPING ||||||||||||||||||||||||||||||||||||||||||||
	//|||||||||||||||||||||||||||||||||||||||||||| APPLY TONEMAPPING ||||||||||||||||||||||||||||||||||||||||||||
	//|||||||||||||||||||||||||||||||||||||||||||| APPLY TONEMAPPING ||||||||||||||||||||||||||||||||||||||||||||
	//tonemapping! this is about taking our wide color/brightness range image and compressing it down into SDR/HDR for display

	#if defined(TONEMAP_NONE) 
		#if defined(TONEMAP_PRESERVE_COLOR_GRADE)
			float3 tonemapOutput = SampleGradedNoTonemapNoSRGB(sceneColor);
		#else
			float3 tonemapOutput = sceneColor;
		#endif
	#elif defined(TONEMAP_GRAN_TURISMO_7)
		#if defined(TONEMAP_PRESERVE_COLOR_GRADE)
			float3 tonemapOutput = SampleGradedNoTonemapNoSRGB(sceneColor);
			tonemapOutput = ApplyTonemap_GranTurismo7(tonemapOutput);
		#else
			float3 tonemapOutput = ApplyTonemap_GranTurismo7(sceneColor);
		#endif
	#elif defined(TONEMAP_AGX)
		//NOTE TO SELF: this * 4 is arbitrary, but I added it so the final tonemapped output
		//will match the original final game exposure almost 1:1
		#if defined(TONEMAP_PRESERVE_COLOR_GRADE)
			float3 tonemapOutput = SampleGradedNoTonemapNoSRGB(sceneColor);
			tonemapOutput = ApplyTonemap_Agx(tonemapOutput * 4.0f);
		#else
			float3 tonemapOutput = ApplyTonemap_Agx(sceneColor * 4.0f);
		#endif
	#elif defined(TONEMAP_UCHIMURA)
		#if defined(TONEMAP_PRESERVE_COLOR_GRADE)
			float3 tonemapOutput = SampleGradedNoTonemapNoSRGB(sceneColor);
			tonemapOutput = ApplyTonemap_Uchimura(tonemapOutput);
		#else
			float3 tonemapOutput = ApplyTonemap_Uchimura(sceneColor);
		#endif
	#elif defined(TONEMAP_REINHARD)
		#if defined(TONEMAP_PRESERVE_COLOR_GRADE)
			float3 tonemapOutput = SampleGradedNoTonemapNoSRGB(sceneColor);
			tonemapOutput = ApplyTonemap_Reinhard(tonemapOutput);
		#else
			float3 tonemapOutput = ApplyTonemap_Reinhard(sceneColor);
		#endif
	#elif defined(TONEMAP_REINHARD2)
		#if defined(TONEMAP_PRESERVE_COLOR_GRADE)
			float3 tonemapOutput = SampleGradedNoTonemapNoSRGB(sceneColor);
			tonemapOutput = ApplyTonemap_Reinhard2(tonemapOutput);
		#else
			float3 tonemapOutput = ApplyTonemap_Reinhard2(sceneColor);
		#endif
	#elif defined(TONEMAP_UNCHARTED2)
		#if defined(TONEMAP_PRESERVE_COLOR_GRADE)
			float3 tonemapOutput = SampleGradedNoTonemapNoSRGB(sceneColor);
			tonemapOutput = ApplyTonemap_Uncharted2(tonemapOutput);
		#else
			float3 tonemapOutput = ApplyTonemap_Uncharted2(sceneColor);
		#endif
	#elif defined(TONEMAP_ACES)
		#if defined(TONEMAP_PRESERVE_COLOR_GRADE)
			float3 tonemapOutput = SampleGradedNoTonemapNoSRGB(sceneColor);
			tonemapOutput = ApplyTonemap_Aces2015(tonemapOutput);
		#else
			float3 tonemapOutput = ApplyTonemap_Aces2015(sceneColor);
		#endif
	#elif defined(TONEMAP_ACES_FITTED) //NOTE: this appears to be what the game is effectively using
		//NOTE TO SELF: this * 2 is arbitrary, but I added it so the final tonemapped output
		//will match the original final game exposure almost 1:1
		#if defined(TONEMAP_PRESERVE_COLOR_GRADE)
			float3 tonemapOutput = SampleGradedNoTonemapNoSRGB(sceneColor);
			tonemapOutput = ApplyTonemap_AcesFitted(tonemapOutput * 2.0f);
		#else
			float3 tonemapOutput = ApplyTonemap_AcesFitted(sceneColor * 2.0f);
		#endif
	#elif defined(TONEMAP_FILMIC)
		#if defined(TONEMAP_PRESERVE_COLOR_GRADE)
			float3 tonemapOutput = SampleGradedNoTonemapNoSRGB(sceneColor);
			tonemapOutput = ApplyTonemap_Filmic(tonemapOutput);
		#else
			float3 tonemapOutput = ApplyTonemap_Filmic(sceneColor);
		#endif
	#elif defined(TONEMAP_UNREAL_3)
		#if defined(TONEMAP_PRESERVE_COLOR_GRADE)
			float3 tonemapOutput = SampleGradedNoTonemapNoSRGB(sceneColor);
			tonemapOutput = ApplyTonemap_Unreal3(tonemapOutput);
		#else
			float3 tonemapOutput = ApplyTonemap_Unreal3(sceneColor);
		#endif
	#elif defined(TONEMAP_KHRONOS_NEUTRAL)
		#if defined(TONEMAP_PRESERVE_COLOR_GRADE)
			float3 tonemapOutput = SampleGradedNoTonemapNoSRGB(sceneColor);
			tonemapOutput = ApplyTonemap_KhronosNeutral(tonemapOutput);
		#else
			float3 tonemapOutput = ApplyTonemap_KhronosNeutral(sceneColor);
		#endif
	#elif defined(TONEMAP_LOTTES)
		#if defined(TONEMAP_PRESERVE_COLOR_GRADE)
			float3 tonemapOutput = SampleGradedNoTonemapNoSRGB(sceneColor);
			tonemapOutput = ApplyTonemap_Lottes(tonemapOutput);
		#else
			float3 tonemapOutput = ApplyTonemap_Lottes(sceneColor);
		#endif
	#elif defined(TONEMAP_EXPONENTIAL)
		#if defined(TONEMAP_PRESERVE_COLOR_GRADE)
			float3 tonemapOutput = SampleGradedNoTonemapNoSRGB(sceneColor);
			tonemapOutput = ApplyTonemap_Exponential(tonemapOutput);
		#else
			float3 tonemapOutput = ApplyTonemap_Exponential(sceneColor);
		#endif
	#elif defined(TONEMAP_EXPONENTIAL_SQUARED)
		#if defined(TONEMAP_PRESERVE_COLOR_GRADE)
			float3 tonemapOutput = SampleGradedNoTonemapNoSRGB(sceneColor);
			tonemapOutput = ApplyTonemap_ExponentialSquared(tonemapOutput);
		#else
			float3 tonemapOutput = ApplyTonemap_ExponentialSquared(sceneColor);
		#endif
	#elif defined(TONEMAP_MGSV)
		#if defined(TONEMAP_PRESERVE_COLOR_GRADE)
			float3 tonemapOutput = SampleGradedNoTonemapNoSRGB(sceneColor);
			tonemapOutput = ApplyTonemap_MGSV(tonemapOutput);
		#else
			float3 tonemapOutput = ApplyTonemap_MGSV(sceneColor);
		#endif
	#elif defined(TONEMAP_TONY_MC_MAP_FACE)
		#if defined(TONEMAP_PRESERVE_COLOR_GRADE)
			float3 tonemapOutput = SampleGradedNoTonemapNoSRGB(sceneColor);
			tonemapOutput = ApplyTonemap_TonyMcMapFaceNeuralNetwork(tonemapOutput);
		#else
			float3 tonemapOutput = ApplyTonemap_TonyMcMapFaceNeuralNetwork(sceneColor);
		#endif
	#else //ORIGINAL GAME TONEMAPPING/COLOR HANDLING
		float3 tonemapOutput = SampleColorConversionLUTs(sceneColor); //aces fitted baked into LUTs
		tonemapOutput = SRGBToLinear(tonemapOutput);
	#endif

	/*
	//combine UI
	//NOTE: some of these tonemappers factor in SRGB mapping already
	#if defined(TONEMAP_AGX) || defined(TONEMAP_UNREAL_3)
		float4 uiColor = CompositeSDRTexture.SampleLevel(View_SharedBilinearClampedSampler, compositeUV, 0.0f);
		uiColor.rgb = LinearToSRGB(uiColor.rgb);
		//uiColor.a = LinearToSRGB(uiColor.a);
		uiColor.a = SRGBToLinear(uiColor.a);

		tonemapOutput = lerp(uiColor.rgb + uiColor.a, tonemapOutput, uiColor.a);

		float3 compositedLinear = saturate(tonemapOutput);
		float3 encodedOutput = compositedLinear;

		//output.Color = uiColor;
	#else
		float4 uiColor = CompositeSDRTexture.SampleLevel(View_SharedBilinearClampedSampler, compositeUV, 0.0f);
		float3 compositedLinear = saturate(tonemapOutput * uiColor.a + uiColor.rgb);
		float3 encodedOutput = LinearToSRGB(compositedLinear);

		//output.Color = uiColor;
	#endif
	*/

	//uI compositing has to happen in linear
	#if defined(TONEMAP_AGX) || defined(TONEMAP_UNREAL_3)
		tonemapOutput = SRGBToLinear(tonemapOutput);
	#endif
	
	float4 uiColor = CompositeSDRTexture.SampleLevel(View_SharedBilinearClampedSampler, compositeUV, 0.0f);
	float3 compositedLinear = saturate(tonemapOutput * uiColor.a + uiColor.rgb);
	float3 encodedOutput = LinearToSRGB(compositedLinear);

	//game applies a dither to the final image
    uint3 noiseAddress = uint3((uint2)input.Position.xy & 127u, (uint)View_StateFrameIndex & 63u);
    float blueNoise = View_SpatiotemporalBlueNoiseVolumeTexture.Load(int4(noiseAddress, 0)).r;
    output.Color = float4(ApplyOutputDither(encodedOutput, blueNoise), 0.0f);

    return output;
}
