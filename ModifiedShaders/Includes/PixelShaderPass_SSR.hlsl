//SSR.hlsl
// Define GAME_VERSION_1_0_0_3 before including this file for the 1.0.0.3 ABI.

#include "LibraryMath.hlsl"
#include "LibraryRandom.hlsl"
#include "LibraryGBuffer.hlsl"

//|||||||||||||||||||||||||||||||||| CONFIGURATION - SSR ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| CONFIGURATION - SSR ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| CONFIGURATION - SSR ||||||||||||||||||||||||||||||||||

//this completely disables the entire SSR pass and effectively makes it return no data
//if you want no SSR you can enable this
//#define DISABLE_SSR

//this changes the noise pattern every frame, which with Temporal Anti-Aliasing (or DLSS or anything related)
//you want to do this, that way samples change every frame and results get blended together over time for a better final apperance
#define RANDOM_ANIMATE_NOISE

//NOTE: if desired, noise can be disabled entirely by default
//that would lead to a performance speedup but worse looking reflections

//this is used by the game by default, recomended
//#define RANDOM_BLUE_NOISE

//this is a non-texture based, more random than blue noise but can look worse because of that
#define RANDOM_WHITE_NOISE

//factors in surface roughness for much better/higher quality reflections
//this does make the SSR a little more expensive (more randomness = less coherence)
#define ENABLE_SSR_ROUGHNESS

//number of independent reflection rays traced per pixel
#define SSR_RAY_COUNT 1

//disabled by default, but enabling this allows you to set the sample count in regards to the raymarch
//higher quality means less artifacts
//#define SSR_OVERRIDE_STEP_COUNT
#define SSR_STEP_COUNT 16

//disabled by default, but this allows you to control the thickness factor in regards to the raymarching
//smaller values meanse less artifacts, but reflections appear less "full"
//#define SSR_DEPTH_THICKNESS_MULTIPLIER
#define SSR_DEPTH_THICKNESS_MULTIPLIER_FACTOR 0.5

//|||||||||||||||||||||||||||||||||| RESOURCES ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| RESOURCES ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| RESOURCES ||||||||||||||||||||||||||||||||||
//resources passed in to the shader

//SamplerComparisonState View_SharedPointClampedSampler : register(s0); // can't disambiguate

SamplerState View_SharedPointClampedSampler : register(s0); // can't disambiguate

Texture3D<float4> View_SpatiotemporalBlueNoiseVolumeTexture : register(t0);

Texture2D<float4> GBufferATexture : register(t1);
Texture2D<float4> GBufferBTexture : register(t2);
Texture2D<float4> GBufferETexture : register(t3);
Texture2D<float4> GBufferVelocityTexture : register(t4);
Texture2D<float4> SceneDepthTexture : register(t5);
Texture2D<float4> SceneColor : register(t6);
Texture2D<float4> HZB : register(t7);

//|||||||||||||||||||||||||||||||||| CONSTANT BUFFERS ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| CONSTANT BUFFERS ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| CONSTANT BUFFERS ||||||||||||||||||||||||||||||||||
//game data passed in to the shader

cbuffer _Globals : register(b0) 
{
	int bSceneLightingChannelsValid : packoffset(c0.x);
	float4 SSRParams : packoffset(c1.x);

#if defined(GAME_VERSION_1_0_0_3)
	// Present in the 1.0.0.3 layout even though the optimized original shader
	// does not consume it. Its insertion shifts every following SSR parameter.
	float PrevSceneColorPreExposureCorrection : packoffset(c2.x);
	float4 HZBUvFactorAndInvFactor : packoffset(c3.x);
	float4 PrevScreenPositionScaleBias : packoffset(c4.x);
	float4 PreviousViewRectangleContext : packoffset(c5.x);
#else
	float4 HZBUvFactorAndInvFactor : packoffset(c2.x);
	float4 PrevScreenPositionScaleBias : packoffset(c3.x);
	float4 PreviousViewRectangleContext : packoffset(c4.x);
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

struct InputStruct 
{
	float4 Position : SV_Position;
};

struct OutputStruct 
{
	float4 Target0 : SV_Target0;
};

struct SurfaceData
{
    float3 translatedWorldPosition;
    float3 translatedWorldPositionNoOffset;
    float3 normal;
    float  roughness;
    uint   shadingModelId;
    float  linearDepth;
};

struct ScreenRay
{
    float3 startNdc;
    float3 deltaNdc;
    float2 startHzbUv;
    float3 hzbStep;
    float  depthThickness;
    uint   stepCount;
};

struct TraceResult
{
    float2 hzbUv;
    float  deviceZ;
    float  confidence;
};

//||||||||||||||||||||||||||||||| RANDOM |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| RANDOM |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| RANDOM |||||||||||||||||||||||||||||||

//128x128x64 R8G8B8A8
float4 GetBlueNoise(float2 pixelPosition, uint rayIndex)
{
	int2 rayOffset = int2(47, 113) * (int)rayIndex;

	#if defined(RANDOM_ANIMATE_NOISE)
		int3 noiseCoordinate = int3(((int2)pixelPosition + rayOffset) & 127,
			(View_StateFrameIndex + (int)rayIndex * 17) & 63);
	#else
		int3 noiseCoordinate = int3(((int2)pixelPosition + rayOffset) & 127,
			((int)rayIndex * 17) & 63);
	#endif

    return View_SpatiotemporalBlueNoiseVolumeTexture.Load(int4(noiseCoordinate, 0));
}

float4 GetRandom(float2 pixelPosition, uint rayIndex)
{
	#if defined(RANDOM_BLUE_NOISE)
		return GetBlueNoise(pixelPosition, rayIndex);
	#elif defined(RANDOM_WHITE_NOISE)
		#if defined(RANDOM_ANIMATE_NOISE)
			int frameIndex = View_StateFrameIndex;
		#else
			int frameIndex = 0;
		#endif

		uint raySeed = rayIndex * 0x9e3779b9u;
		return float4(
				GenerateHashedRandomFloat(uint4(pixelPosition, frameIndex, 0x68bc21ebu ^ raySeed)),
				GenerateHashedRandomFloat(uint4(pixelPosition, frameIndex, 0x02e5be93u ^ raySeed)),
				GenerateHashedRandomFloat(uint4(pixelPosition, frameIndex, 0x03e56253u ^ raySeed)),
				GenerateHashedRandomFloat(uint4(pixelPosition, frameIndex, 0x01aabe43u ^ raySeed)));
	#else
		return float4(1, 1, 1, 1);
	#endif
}

//||||||||||||||||||||||||||||||| GGX SPECULAR |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| GGX SPECULAR |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| GGX SPECULAR |||||||||||||||||||||||||||||||

//simplified isotropic variant
//since the game does not have anisotropic specular or materials as far as I can tell
//roughness remains the same, and this collapses/simplifies some of the ALU cost
half3 ImportanceSampleGGX_VNDF(
    half2 random,
    half3 vector_normalDirectionWorld,
    half3 vector_viewDirectionWorld,
    half roughness,
    out bool valid)
{
    half3x3 localToWorld = GetLocalFrame(vector_normalDirectionWorld);
    half3 localView = mul(vector_viewDirectionWorld, transpose(localToWorld));

    //isotropic GGX means one alpha stretches both tangent axes equally.
    half3 stretchedView = normalize(half3(roughness * localView.x, roughness * localView.y, localView.z));
    half3 tangent = stretchedView.z < 0.9999h ? normalize(cross(half3(0.0h, 0.0h, 1.0h), stretchedView)) : half3(1.0h, 0.0h, 0.0h);
    half3 bitangent = cross(stretchedView, tangent);

    half radius = sqrt(random.x);
    half phi = MATH_PI_TWO * random.y;
    half diskX = radius * cos(phi);
    half diskY = radius * sin(phi);
    half projectionBlend = 0.5h * (1.0h + stretchedView.z);
    diskY = (1.0h - projectionBlend) * sqrt(1.0h - diskX * diskX) + projectionBlend * diskY;

    half3 localHalfVector = diskX * tangent + diskY * bitangent + sqrt(max(0.0h, 1.0h - diskX * diskX - diskY * diskY)) * stretchedView;

    //unstretch the sampled visible normal back onto the GGX ellipsoid.
    localHalfVector = normalize(half3(roughness * localHalfVector.x, roughness * localHalfVector.y, max(0.0h, localHalfVector.z)));

    half viewDotHalf = saturate(dot(localView, localHalfVector));
    half3 localReflection = 2.0h * viewDotHalf * localHalfVector - localView;
    half3 outgoingDirection = mul(localReflection, localToWorld);

    valid = dot(vector_normalDirectionWorld, outgoingDirection) >= 0.001h;
    return outgoingDirection;
}

//|||||||||||||||||||||||||||||||||| SPACE CONVERSIONS ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| SPACE CONVERSIONS ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| SPACE CONVERSIONS ||||||||||||||||||||||||||||||||||

float ConvertFromDeviceZ(float deviceZ)
{
    return deviceZ * View_InvDeviceZToWorldZTransform.x
         + View_InvDeviceZToWorldZTransform.y
         + rcp(deviceZ * View_InvDeviceZToWorldZTransform.z
             - View_InvDeviceZToWorldZTransform.w);
}

float3 ReconstructTranslatedWorldPosition(float2 pixelPosition, float linearDepth)
{
    float2 viewNdc;
    viewNdc.x = (pixelPosition.x - View_ViewRectMin.x) * View_ViewSizeAndInvSize.z * 2.0f - 1.0f;
    viewNdc.y = 1.0f - (pixelPosition.y - View_ViewRectMin.y) * View_ViewSizeAndInvSize.w * 2.0f;

    float3 screenPosition = linearDepth * float3(viewNdc, 1.0f);
    return mul(View_ScreenToTranslatedWorld, float4(screenPosition, 1.0f)).xyz;
}

float3 ReconstructTranslatedWorldVector(float2 ndc, float deviceZ)
{
    // w=0 deliberately omits the translation row. The DXIL compares two such
    // vectors, so the translation would cancel in the distance test anyway.
    float linearDepth = ConvertFromDeviceZ(deviceZ);
    return mul(View_ScreenToTranslatedWorld, float4(linearDepth * float3(ndc, 1.0f), 0.0f)).xyz;
}

//||||||||||||||||||||||||||||||| GBUFFER |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| GBUFFER |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| GBUFFER |||||||||||||||||||||||||||||||

uint DecodeShadingModelId(float packedGBufferAlpha)
{
    return ((uint)round(packedGBufferAlpha * 255.0f)) & 0x0fu;
}

SurfaceData DecodeSurface(float2 pixelPosition)
{
    SurfaceData surface;

    float2 bufferUv = pixelPosition * View_BufferSizeAndInvSize.zw;
    float4 gbufferA = GBufferATexture.SampleLevel(View_SharedPointClampedSampler, bufferUv, 0.0f);
    float4 gbufferB = GBufferBTexture.SampleLevel(View_SharedPointClampedSampler, bufferUv, 0.0f);
    float4 gbufferE = GBufferETexture.SampleLevel(View_SharedPointClampedSampler, bufferUv, 0.0f);
    float deviceZ = SceneDepthTexture.SampleLevel(View_SharedPointClampedSampler, bufferUv, 0.0f).r;

    surface.shadingModelId = DecodeShadingModelId(gbufferB.a);
    surface.normal = DecodeUnitVector(gbufferA.xyz);
    surface.linearDepth = ConvertFromDeviceZ(deviceZ);
    surface.translatedWorldPosition = ReconstructTranslatedWorldPosition(pixelPosition, surface.linearDepth);

    // DXIL decodes the low nibble of CustomData.a as a 0..1 blend. This path
    // is gated by the view's wetness intensity and bends the normal toward the
    // vector stored in GBufferE.xyz while reducing roughness.
    uint customBlendBits = ((uint)round(saturate(gbufferE.a) * 255.0f)) & 0x0fu;
    float customBlend = (customBlendBits / 15.0f) * (View_WetnessIntensity > 0.0f ? 1.0f : 0.0f);

    float rawRoughness = gbufferB.b;
    surface.roughness = sqrt(saturate(rawRoughness * rawRoughness * (1.0f - customBlend)));
    surface.normal = normalize(lerp(surface.normal, DecodeUnitVector(gbufferE.xyz), customBlend));

    if (surface.shadingModelId == SHADINGMODELID_PREINTEGRATED_SKIN)
        surface.roughness *= 0.93655f;
    else if (surface.shadingModelId == SHADINGMODELID_HAIR)
        surface.roughness = 0.48f;

    // Same position without the matrix translation, used by hit-distance logic.
    float2 viewNdc;
    viewNdc.x = (pixelPosition.x - View_ViewRectMin.x) * View_ViewSizeAndInvSize.z * 2.0f - 1.0f;
    viewNdc.y = 1.0f - (pixelPosition.y - View_ViewRectMin.y) * View_ViewSizeAndInvSize.w * 2.0f;
    surface.translatedWorldPositionNoOffset = mul(View_ScreenToTranslatedWorld, float4(surface.linearDepth * float3(viewNdc, 1.0f), 0.0f)).xyz;

    return surface;
}

//||||||||||||||||||||||||||||||| SSR TRACE |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| SSR TRACE |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| SSR TRACE |||||||||||||||||||||||||||||||

float2 HzbUvToNdc(float2 hzbUv)
{
    float2 screenUv = saturate(hzbUv * HZBUvFactorAndInvFactor.zw);
	return mad(screenUv, float2(2.0f, -2.0f), float2(-1.0f, 1.0f));
}

float3 ComputeReflectionTraceDirection(SurfaceData surface, float2 random, out bool valid)
{
    float3 viewDirection = normalize(View_TranslatedWorldCameraOrigin - surface.translatedWorldPosition);

	#if defined(ENABLE_SSR_ROUGHNESS)
		//GBuffer roughness looks to be perceptual roughness
		//GGX expects roughness to be "roughness", plus this better matches the cubemap specular convolution level
		float perceptualRoughness = max(saturate(surface.roughness), MATH_MIN_ROUGHNESS);

		//skip the ggx sampling for very low roughness values (which should appear pin sharp anyway)
		if (perceptualRoughness <= MATH_MIN_ROUGHNESS)
		{
			valid = true;
			return reflect(-viewDirection, surface.normal);
		}

		half roughness = perceptualRoughness * perceptualRoughness;

		half3 sampledDirection = ImportanceSampleGGX_VNDF(
			random,
			surface.normal,
			viewDirection,
			roughness,
			valid);

		float directionLengthSquared = dot(sampledDirection, sampledDirection);

		valid = valid && directionLengthSquared > 1.0e-6f;
		return valid ? sampledDirection : 0.0f;
	#else
		//no randomness from the GGX specular
		//so the reflection direction should always be valid here
		valid = true;

		//NOTE: this is the original game SSR direction vector, keeping it around just in case
		//but it is wierd as heck, it has a * 2 that skews the reflection for some reason? why??? what happened to just regular reflect calls?
		//they also try to do some approximate stretching and conforming of the reflection direction to get some semblance of "rough" reflections
		//but honestly... it doesn't look good
		//float normalDotView = dot(surface.normal, viewDirection);
		//float3 sharpDirection = normalize(2.0f * (normalDotView * surface.normal - viewDirection) + abs(normalDotView) * surface.normal);
		//float roughnessBias = surface.roughness * surface.roughness * surface.roughness;
		//return normalize(lerp(sharpDirection, surface.normal, roughnessBias));

		//much more simplified and correct reflection direction
		//the original SSR was pin-sharp anyway but did some funky stretching
		return normalize(reflect(-viewDirection, surface.normal));
	#endif
}

ScreenRay BuildScreenRay(SurfaceData surface, float3 traceDirection, float random)
{
    ScreenRay ray;

    float4 startClip = mul(View_TranslatedWorldToClip, float4(surface.translatedWorldPosition, 1.0f));
    float3 endWorld = surface.translatedWorldPosition + traceDirection * surface.linearDepth;
    float4 endClip = mul(View_TranslatedWorldToClip, float4(endWorld, 1.0f));

    ray.startNdc = startClip.xyz * rcp(startClip.w);
    ray.startNdc.z += 0.05f * rcp(startClip.w);
    float3 endNdc = endClip.xyz * rcp(endClip.w);

    float roughnessSquared = max(1.0e-5f, surface.roughness * surface.roughness);
    float roughnessLimitedDistance = 10.0f / roughnessSquared;

    // The compiled shader estimates a second end point directly from two rows
    // of ViewToClip. Keep the component-level form to avoid changing results.
    float roughEndW = startClip.w + roughnessLimitedDistance * View_ViewToClip[0].w;
    float2 roughEndNdc = (startClip.xy + roughnessLimitedDistance * View_ViewToClip[0].xy) * rcp(roughEndW);

    float depthEndW = startClip.w + surface.linearDepth * View_ViewToClip[2].w;
    float depthEndNdc = (startClip.z + surface.linearDepth * View_ViewToClip[2].z) * rcp(depthEndW);

    float3 unclippedDelta = endNdc - ray.startNdc;

    // Clip the projected segment to the screen using the algebra found in DXIL.
    float screenRadius = 0.5f * length(unclippedDelta.xy);
    float2 overshoot = max(abs(unclippedDelta.xy + screenRadius * ray.startNdc.xy) - screenRadius, 0.0f);
    float2 axisFraction = 1.0f - overshoot / abs(unclippedDelta.xy);
    float clipScale = min(axisFraction.x, axisFraction.y) / screenRadius;
    ray.deltaNdc = unclippedDelta * clipScale;
    ray.depthThickness = max(abs(ray.deltaNdc.z), depthEndNdc - ray.startNdc.z);

	#if defined(SSR_DEPTH_THICKNESS_MULTIPLIER)
		ray.depthThickness *= SSR_DEPTH_THICKNESS_MULTIPLIER_FACTOR;
	#endif

    float roughProjectedLength = length(roughEndNdc - ray.startNdc.xy);
    float traceProjectedLength = max(1.0e-4f, length(ray.deltaNdc.xy));
    float stepEstimate = saturate(roughProjectedLength / traceProjectedLength) * 8.0f + random;

	#if defined(SSR_OVERRIDE_STEP_COUNT)
		ray.stepCount = SSR_STEP_COUNT;
	#else
		ray.stepCount = clamp((uint)ceil(stepEstimate), 1u, 8u);
	#endif

    ray.startHzbUv = float2(ray.startNdc.x * 0.5f + 0.5f, 0.5f - ray.startNdc.y * 0.5f) * HZBUvFactorAndInvFactor.xy;
    ray.hzbStep = float3(ray.deltaNdc.x * HZBUvFactorAndInvFactor.x,
                        -ray.deltaNdc.y * HZBUvFactorAndInvFactor.y,
                         ray.deltaNdc.z) * float3(1.0f / 16.0f,
                                                  1.0f / 16.0f,
                                                  1.0f / 8.0f);

	ray.depthThickness *= 0.125f; //(1.0f / 8.0f)
    return ray;
}

TraceResult TraceHierarchicalZ(ScreenRay ray, float random)
{
    TraceResult result = (TraceResult)0;

    uint hzbWidth;
	uint hzbHeight;
    HZB.GetDimensions(hzbWidth, hzbHeight);

    float previousDepthDifference = 0.0f;

    [loop]
    for (uint stepIndex = 0u; stepIndex < ray.stepCount; ++stepIndex)
    {
        float sampleDistance = (float)stepIndex + random;
        float2 sampleUv = ray.startHzbUv + sampleDistance * ray.hzbStep.xy;
        float rayDeviceZ = ray.startNdc.z + sampleDistance * ray.hzbStep.z;

        int2 samplePixel = (int2)floor(sampleUv * float2(hzbWidth, hzbHeight));
        float hzbDeviceZ = HZB.Load(int3(samplePixel, 0)).r;
        float depthDifference = rayDeviceZ - hzbDeviceZ;

        bool crossesSurface = abs(-ray.depthThickness - depthDifference) < ray.depthThickness;
        if (crossesSurface && hzbDeviceZ != 0.0f)
        {
            float previous = (stepIndex == 0u) ? depthDifference - ray.hzbStep.z : previousDepthDifference;
            float refinement = saturate(previous / (previous - depthDifference));
            float hitDistance = abs(random - 1.0f + (float)stepIndex + refinement);

            result.hzbUv = ray.startHzbUv + hitDistance * ray.hzbStep.xy;
            result.deviceZ = ray.startNdc.z + hitDistance * ray.hzbStep.z;
            //float normalizedDistance = min(hitDistance / 8.0f, 1.0f);
			float normalizedDistance = min(hitDistance * 0.125f, 1.0f);
            result.confidence = 1.0f - normalizedDistance * normalizedDistance;
            return result;
        }

        previousDepthDifference = depthDifference;
    }

    return result;
}

//||||||||||||||||||||||||||||||| SSR HISTORY / TEMPORAL BLENDING |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| SSR HISTORY / TEMPORAL BLENDING |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| SSR HISTORY / TEMPORAL BLENDING |||||||||||||||||||||||||||||||

int2 GetVelocityPixel(float2 hitNdc)
{
    float2 clampedNdc = clamp(hitNdc, -1.0f, 1.0f);
    float2 bufferUv = clampedNdc * View_ScreenPositionScaleBias.xy + View_ScreenPositionScaleBias.wz;
    int2 pixel = (int2)(bufferUv * View_BufferSizeAndInvSize.xy);

    int2 viewMin = (int2)View_ViewRectMin.xy;
    int2 viewMax = (int2)(View_ViewRectMin.xy + View_ViewSizeAndInvSize.xy) - 1;
    return clamp(pixel, viewMin, viewMax);
}

float2 GetScreenMotion(float2 hitNdc, float hitDeviceZ)
{
    float2 encodedVelocity = GBufferVelocityTexture.Load(int3(GetVelocityPixel(hitNdc), 0)).xy;

    if (encodedVelocity.x != 0.0f)
    {
        return clamp((encodedVelocity - 0.500008f) * 4.00018f, -2.0f, 2.0f);
    }

    float4 previousClip = mul(View_ClipToPrevClip, float4(hitNdc, hitDeviceZ, 1.0f));
    float2 previousNdc = previousClip.xy * rcp(previousClip.w);
    return hitNdc - previousNdc;
}

float2 FoldNdcAtScreenEdges(float2 ndc)
{
    float2 folded = ndc - sign(ndc) * 2.0f * max(abs(ndc) - 1.0f, 0.0f);
    return clamp(folded, -1.0f, 1.0f);
}

float ComputePreviousScreenFade(float2 previousNdc)
{
    float2 clampDeltaUv = (clamp(previousNdc, -1.0f, 1.0f) - previousNdc) * 0.5f;
    float2 edgePenalty = saturate(abs(clampDeltaUv) * 5.0f - 4.0f);
    return max(0.0f, 1.0f - dot(edgePenalty, edgePenalty));
}

float3 LoadPreviousSceneColor(float2 previousNdc)
{
    float2 foldedNdc = FoldNdcAtScreenEdges(previousNdc);
    float2 previousUv = foldedNdc * PrevScreenPositionScaleBias.xy + PrevScreenPositionScaleBias.zw;

    uint sceneColorWidth, sceneColorHeight;
    SceneColor.GetDimensions(sceneColorWidth, sceneColorHeight);

    int2 rectMin = (int2)PreviousViewRectangleContext.xy;
    int2 rectMax = (int2)(PreviousViewRectangleContext.xy + PreviousViewRectangleContext.zw) - 1;
    int2 pixel = (int2)(previousUv * float2(sceneColorWidth, sceneColorHeight));
    pixel = clamp(pixel, rectMin, rectMax);

    float3 history = SceneColor.Load(int3(pixel, 0)).rgb;
    return min(history, 65504.0f) * View_PreviousOneOverPreExposure;
}

//||||||||||||||||||||||||||||||| MAIN |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| MAIN |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| MAIN |||||||||||||||||||||||||||||||

OutputStruct main(in InputStruct IN)
{
    OutputStruct OUT = (OutputStruct)0;

	#if defined(DISABLE_SSR)
		return OUT;
	#endif

    float2 pixelPosition = IN.Position.xy;
    SurfaceData surface = DecodeSurface(pixelPosition);

    if (surface.shadingModelId == SHADINGMODELID_UNLIT)
        return OUT;

	float3 weightedHistoryColor = 0.0f;
	float totalConfidence = 0.0f;

	[loop]
	for (uint rayIndex = 0u; rayIndex < SSR_RAY_COUNT; ++rayIndex)
	{
		float4 random = GetRandom(pixelPosition, rayIndex);

		bool validMicrofacetSample;
		float3 traceDirection = ComputeReflectionTraceDirection(surface, random.xy, validMicrofacetSample);

		if (!validMicrofacetSample)
			continue;

		ScreenRay ray = BuildScreenRay(surface, traceDirection, random.z);
		TraceResult trace = TraceHierarchicalZ(ray, random.w);

		if (trace.confidence <= 0.0f)
			continue;

		float2 hitNdc = HzbUvToNdc(trace.hzbUv);
		float3 hitPositionNoOffset = ReconstructTranslatedWorldVector(hitNdc, trace.deviceZ);
		float3 hitDelta = surface.translatedWorldPositionNoOffset - hitPositionNoOffset;
		float hitDistanceSquared = dot(hitDelta, hitDelta);

		//SSR temporal blending
		float2 motion = GetScreenMotion(hitNdc, trace.deviceZ);
		float2 previousNdc = hitNdc - motion;
		float previousScreenFade = ComputePreviousScreenFade(previousNdc);
		float3 historyColor = LoadPreviousSceneColor(previousNdc);

		float roughnessSquared = surface.roughness * surface.roughness;
		float distanceFade = saturate(rcp(max(1.0e-5f, roughnessSquared * roughnessSquared * hitDistanceSquared)));
		float confidence = previousScreenFade * trace.confidence * SSRParams.x * distanceFade;

		if (confidence > 0.0f)
		{
			weightedHistoryColor += historyColor * confidence;
			totalConfidence += confidence;
		}
	}

	if (totalConfidence <= 0.0f)
		return OUT;

	float3 resolvedHistoryColor = weightedHistoryColor * rcp(totalConfidence);
	float resolvedConfidence = totalConfidence / (float)SSR_RAY_COUNT;
	OUT.Target0 = float4(resolvedHistoryColor * View_PreExposure, resolvedConfidence);

    return OUT;
}
