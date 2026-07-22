//PostProcessFog.hlsl

//library includes
//NOTE: this is where we have various useful shader functions
#include "LibraryMath.hlsl"

//||||||||||||||||||||||||||||||| CONFIGURATION - FOG |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| CONFIGURATION - FOG |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| CONFIGURATION - FOG |||||||||||||||||||||||||||||||

//#define DISABLE_NEAR_FOG

//DEFAULT: 1.0
#define FOG_DENSITY_NEAR_FIELD_MULTIPLIER 1.0

//#define DISABLE_FAR_FOG

//DEFAULT: 1.0
#define FOG_DENSITY_FAR_FIELD_MULTIPLIER 2.0

//||||||||||||||||||||||||||||||| CONFIGURATION - PROCEDUAL SKY (EXPERIMENTAL!!!) |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| CONFIGURATION - PROCEDUAL SKY (EXPERIMENTAL!!!) |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| CONFIGURATION - PROCEDUAL SKY (EXPERIMENTAL!!!) |||||||||||||||||||||||||||||||

//#define ATMOSPHERE
//#define ATMOSPHERE_ENABLE_GROUND  
#define ATMOSPHERE_BOTTOM_RADIUS_KM       6360.0
#define ATMOSPHERE_TOP_RADIUS_KM          6460.0
#define ATMOSPHERE_RAYLEIGH_SCALE_KM         4.0
#define ATMOSPHERE_MIE_SCALE_KM              2.2
#define ATMOSPHERE_VIEW_STEPS                 16
#define ATMOSPHERE_SUN_STEPS                   8
#define ATMOSPHERE_GROUND_HEIGHT_CM            10000.0
#define ATMOSPHERE_MIN_EYE_ALTITUDE_KM         0.002
#define ATMOSPHERE_MIE_G                       0.8
#define ATMOSPHERE_MULTISCATTER_STRENGTH       0.35
#define ATMOSPHERE_SUN_ANGULAR_RADIUS          0.004675
#define ATMOSPHERE_SUN_DISK_SCALE             20.0

#define ATMOSPHERE_APPROX_SUN_DIRECTION_SIGN 1.0
#define ATMOSPHERE_APPROX_RAYLEIGH_SCALE_KM 6.0
#define ATMOSPHERE_APPROX_MIE_SCALE_KM 6.2
#define ATMOSPHERE_APPROX_SKYLIGHT_STRENGTH 1.0

//|||||||||||||||||||||||||||||||||| RESOURCES ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| RESOURCES ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| RESOURCES ||||||||||||||||||||||||||||||||||
//resources passed in to the shader

//SamplerComparisonState View_SharedBilinearWrappedSampler : register(s0); // can't disambiguate
//SamplerComparisonState View_SharedBilinearClampedSampler : register(s1); // can't disambiguate

SamplerState View_SharedBilinearWrappedSampler : register(s0); // can't disambiguate
SamplerState View_SharedBilinearClampedSampler : register(s1); // can't disambiguate

Texture3D<float4> View_SpatiotemporalBlueNoiseVolumeTexture : register(t0, space0);
Texture3D<float4> FogStruct_IntegratedScatteringVolumeATexture : register(t3);
Texture3D<float4> FogStruct_IntegratedScatteringVolumeBTexture : register(t4);

Texture2D<float4> SceneTexturesStruct_SceneDepthTexture : register(t1);
Texture2D<float4> FogStruct_FarIntegratedScatteringTexture : register(t2);
Texture2D<float4> FogContextEnvironmentTextureArray[] : register(t0, space1);

//|||||||||||||||||||||||||||||||||| CONSTANT BUFFERS ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| CONSTANT BUFFERS ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| CONSTANT BUFFERS ||||||||||||||||||||||||||||||||||
//game data passed in to the shader

cbuffer View : register(b0) 
{
	float4x4 View_TranslatedWorldToClip : packoffset(c0.x);
	float4x4 View_WorldToOrthographicClip : packoffset(c4.x);
	float4x4 View_TranslatedWorldToOrthographicClip : packoffset(c8.x);
	row_major float4x4 View_WorldToClip : packoffset(c12.x);
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

cbuffer FogStruct : register(b1) 
{
	float4 FogStruct_DirectionalLightDirection : packoffset(c0.x);
	float4 FogStruct_DirectionalLightColor : packoffset(c1.x);
	float FogStruct_MediumIOR : packoffset(c2.x);
	int FogStruct_NumberOfFogContext : packoffset(c2.y);
	int PrePadding_FogStruct_40 : packoffset(c2.z);
	int PrePadding_FogStruct_44 : packoffset(c2.w);
	float4 FogStruct_HeightBasedContextAlbedo[4] : packoffset(c3.x);
	float4 FogStruct_HeightBasedContextDensity[4] : packoffset(c7.x);
	float4 FogStruct_TransmitContextAlbedo[4] : packoffset(c11.x);
	float4 FogStruct_TransmitContextDensity[4] : packoffset(c15.x);
	float4 FogStruct_RegionContext[4] : packoffset(c19.x);
	float4 FogStruct_LightContext[4] : packoffset(c23.x);
	float4 FogStruct_EnvironmentTextureContext[4] : packoffset(c27.x);
	float4 FogStruct_DistanceTextureContext[4] : packoffset(c31.x);
	float PrePadding_FogStruct_560 : packoffset(c35.x);
	float PrePadding_FogStruct_564 : packoffset(c35.y);
	float PrePadding_FogStruct_568 : packoffset(c35.z);
	float PrePadding_FogStruct_572 : packoffset(c35.w);
	float4 FogStruct_FarIntegratedScatteringTextureContext : packoffset(c36.x);
	float PrePadding_FogStruct_592 : packoffset(c37.x);
	float PrePadding_FogStruct_596 : packoffset(c37.y);
	float PrePadding_FogStruct_600 : packoffset(c37.z);
	float PrePadding_FogStruct_604 : packoffset(c37.w);
	float4 FogStruct_IntegratedScatteringVolumeTextureContext : packoffset(c38.x);
};

struct FApplyFogPSInput
{
    float2 UV       : TEXCOORD0;
    float3 ViewRay  : TEXCOORD1;
    float4 Position : SV_Position;
};

struct FApplyFogPSOutput
{
    float4 FogColor       : SV_Target0;
    float4 Transmittance  : SV_Target1;
};

float ConvertFromDeviceZ(float deviceZ)
{
    return View_InvDeviceZToWorldZTransform.x * deviceZ + View_InvDeviceZToWorldZTransform.y + rcp(View_InvDeviceZToWorldZTransform.z * deviceZ - View_InvDeviceZToWorldZTransform.w);
}

float3 SafeNormalizeExact(float3 v)
{
    return v * rsqrt(dot(v, v));
}

float SignedEqualAreaDecode(float encoded)
{
    float x = encoded * 2.0 - 1.0;
    float signX = (float)((int)(x > 0.0) - (int)(x < 0.0));
    return signX * (1.0 - sqrt(1.0 - abs(x)));
}

float2 DirectionToEnvironmentUV(float3 direction)
{
    float x = -direction.x;
    float y = -direction.y;
    float angle = atan(y / x);

    if (direction.x > -0.0 && direction.y <= -0.0)
        angle += 3.14159;
    if (direction.x > -0.0 && direction.y > -0.0)
        angle -= 3.14159;

    float u = angle * 0.159155 + 1.0;

    if (direction.x == -0.0 && direction.y > -0.0)
        u = 0.75;
    if (direction.x == -0.0 && direction.y <= -0.0)
        u = 1.25;

    return float2(frac(u), asin(-direction.z) * 0.318310 + 0.5);
}

float DistanceToContainingSphere(float3 direction, float4 sphere)
{
    float3 cameraToCenter = sphere.xyz - View_WorldCameraOrigin;
    float projectedCenter = dot(cameraToCenter, direction);
    float c = dot(cameraToCenter, cameraToCenter) - sphere.w * sphere.w;
    float discriminant = projectedCenter * projectedCenter - c;

    if (c < 0.0 && discriminant >= 0.0)
    {
        float root = sqrt(discriminant);
        return min(abs(projectedCenter - root), abs(projectedCenter + root));
    }

    return 0.0;
}

int SelectFogContext(float3 direction, float randomValue)
{
    int fallback = (View_NumberOfFogContext != 1) ? -1 : 0;

    if (View_NumberOfFogContext <= 1)
        return fallback;

    float distances[4] = { 0.0, 0.0, 0.0, 0.0 };

    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        if (i < View_NumberOfFogContext)
            distances[i] = DistanceToContainingSphere(direction, View_FogContextRegionContext[i]);
    }

    float threshold = randomValue * 0.9999 * dot(float4(distances[0], distances[1], distances[2], distances[3]), 1.0);
    float cumulative = distances[0];

    if (cumulative > threshold) 
        return 0;

    cumulative += distances[1];

    if (cumulative > threshold) 
        return 1;

    cumulative += distances[2];

    if (cumulative > threshold) 
        return 2;

    cumulative += distances[3];

    if (cumulative > threshold) 
        return 3;

    return fallback;
}

float3 SampleSkyEnvironment(int context, float3 viewDirection)
{
    if (context == -1)
        return 0.0;

    float4 textureContext = FogStruct_EnvironmentTextureContext[context];

    if (textureContext.x == 0.0)
        return 0.0;

    float2 uv = DirectionToEnvironmentUV(viewDirection);
    return textureContext.y * FogContextEnvironmentTextureArray[NonUniformResourceIndex(context)].SampleLevel(View_SharedBilinearWrappedSampler, uv, 0.0).rgb;
}

float LimitSkyDistance(int context, float viewDirectionZ, float defaultDistance)
{
    if (context == -1)
        return defaultDistance;

    float2 distanceContext = FogStruct_DistanceTextureContext[context].zw;

    if (viewDirectionZ < -1.0e-7)
        return min(distanceContext.y / viewDirectionZ, distanceContext.x);

    return distanceContext.x;
}

float3 ComputeVolumeCoordinate(float3 translatedWorldPosition, float3 jitter)
{
    float clipX = dot(float4(translatedWorldPosition, 1.0),
                      float4(View_WorldToClip[0].x, View_WorldToClip[1].x,
                             View_WorldToClip[2].x, View_WorldToClip[3].x));
    float clipY = dot(float4(translatedWorldPosition, 1.0),
                      float4(View_WorldToClip[0].y, View_WorldToClip[1].y,
                             View_WorldToClip[2].y, View_WorldToClip[3].y));
    float clipW = dot(float4(translatedWorldPosition, 1.0),
                      float4(View_WorldToClip[0].w, View_WorldToClip[1].w,
                             View_WorldToClip[2].w, View_WorldToClip[3].w));

    float2 uv = float2(clipX / clipW * 0.5 + 0.5, 0.5 - clipY / clipW * 0.5);
    float gridZ = log2(View_VolumetricFogGridZParameter.x * clipW + View_VolumetricFogGridZParameter.y) * View_VolumetricFogGridZParameter.z;
    gridZ = max(0.0, min(gridZ, View_VolumetricFogGridSize.z - 1.0) + 0.5);

    return float3(uv + jitter.xy * View_VolumetricFogGridSizeReciprocal.xy, (gridZ + jitter.z) * View_VolumetricFogGridSizeReciprocal.z);
}

float2 ClampFarFogUV(float2 volumeUV)
{
    float2 uv = View_VolumetricFogGridCoordinateSolver.xy * volumeUV;

    return min(max(uv, View_VolumetricFogGridCoordinateMinimum.xy), View_VolumetricFogGridCoordinateMaximum.xy);
}

float3 ClampIntegratedFogUVW(float3 volumeUVW)
{
    float3 uvw = View_VolumetricFogGridCoordinateSolver * volumeUVW;
    return min(max(uvw, View_VolumetricFogGridCoordinateMinimum), View_VolumetricFogGridCoordinateMaximum);
}

struct FFarFogResult
{
    float3 Scattering;
    float3 Transmittance;
};

FFarFogResult EvaluateFarFog(int context, float3 rayVector, float3 rayDirection, float rayLength, float farFogStartDistance, float3 volumeCoordinate)
{
    FFarFogResult result;
    result.Scattering = 0.0;
    result.Transmittance = 1.0;

    if (context == -1)
        return result;

    float4 heightAlbedo = View_FogContextHeightBasedContextAlbedo[context];
    heightAlbedo.a = min(heightAlbedo.a, 0.99);

    float4 heightDensity = View_FogContextHeightBasedContextDensity[context];
    float3 transmitAlbedo = View_FogContextTransmitContextAlbedo[context].rgb;
    float transmitDensity = View_FogContextTransmitContextDensity[context].x;
    float lightIntensity = View_FogContextLightContext[context].x;

    float3 incidentLight = 0.0;

    if (FogStruct_FarIntegratedScatteringTextureContext.x > 0.0)
    {
        float2 farUV = ClampFarFogUV(volumeCoordinate.xy);
        incidentLight = View_OneOverPreExposure * FogStruct_FarIntegratedScatteringTexture.SampleLevel(View_SharedBilinearClampedSampler, farUV, 0.0).rgb;
    }

    float phaseCosine = dot(-View_FogContextDirectionalLightDirection.xyz, -rayDirection);
    float phase = (phaseCosine * phaseCosine + 1.0) * 0.0397887 * exp2(-1.5 * log2(max(0.0, phaseCosine + 1.25)));
    incidentLight += View_FogContextDirectionalLightColor.rgb * lightIntensity * phase;

    float nearFraction = farFogStartDistance / rayLength;
    float nearEndZ = nearFraction * rayVector.z;
    float farSegmentZ = (1.0 - nearFraction) * rayVector.z;
    float farSegmentLength = rayLength - farFogStartDistance;

    float heightIntegral = 0.0;

    if (heightDensity.x != 0.0)
    {
        float startExponent = (nearEndZ - heightDensity.z + View_WorldCameraOrigin.z) * heightDensity.y;
        float segmentExponent = farSegmentZ * heightDensity.y;
        float startDensity = exp2(-startExponent);

        if (abs(segmentExponent) > 0.01)
        {
            heightIntegral = heightDensity.x * (startDensity - exp2(-segmentExponent - startExponent)) / segmentExponent;
        }
        else
        {
            heightIntegral = heightDensity.x * 0.693147 * startDensity;
        }
    }

    float heightExtinction = heightIntegral * (1.0 - heightAlbedo.a);
    heightExtinction *= FOG_DENSITY_FAR_FIELD_MULTIPLIER;

    float3 transmitExtinction = (1.0 - transmitAlbedo) * transmitDensity;
    result.Transmittance = saturate(exp2(-farSegmentLength * (heightExtinction + transmitExtinction)));

    float3 transmitOnly = saturate(exp2(-farSegmentLength * transmitExtinction));
    float heightOnly = saturate(exp2(-farSegmentLength * heightExtinction));
    result.Scattering = heightAlbedo.a * heightAlbedo.rgb * incidentLight * transmitOnly * (1.0 - heightOnly);
    return result;
}

//||||||||||||||||||||||||||||||| PROCEDUAL SKY (EXPERIMENTAL!!!) |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| PROCEDUAL SKY (EXPERIMENTAL!!!) |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| PROCEDUAL SKY (EXPERIMENTAL!!!) |||||||||||||||||||||||||||||||

static const float3 ATMOSPHERE_BETA_R = float3(5.802, 13.558, 33.100) * 1.0e-3;
static const float3 ATMOSPHERE_BETA_M_SCATTER = float3(3.996, 3.996, 3.996) * 1.0e-3;
static const float3 ATMOSPHERE_BETA_M_EXTINCT = float3(4.440, 4.440, 4.440) * 1.0e-3;
static const float3 ATMOSPHERE_BETA_OZONE = float3(0.650, 1.881, 0.085) * 1.0e-3;

static const float3 AtmosphereApprox_BetaRayleigh = float3(5.802, 13.558, 33.100) * 1.0e-3;
static const float3 AtmosphereApprox_BetaMieScattering = float3(3.996, 3.996, 3.996) * 1.0e-3;
static const float3 AtmosphereApprox_BetaMieExtinction = float3(4.440, 4.440, 4.440) * 1.0e-3;
static const float3 AtmosphereApprox_BetaOzone = float3(0.650, 1.881, 0.085) * 1.0e-3;

struct FAtmosphereMedium
{
    float3 MolecularScattering;
    float3 AerosolScattering;
    float3 Extinction;
};

struct FRuntimeSky
{
    float3 Radiance;
    float3 Transmittance;
};

bool AtmosphereRaySphere(float3 origin, float3 direction, float radius, out float2 intersections)
{
    float b = dot(origin, direction);
    float c = dot(origin, origin) - radius * radius;
    float discriminant = b * b - c;

    if (discriminant < 0.0)
    {
        intersections = -1.0;
        return false;
    }

    float root = sqrt(discriminant);
    intersections = float2(-b - root, -b + root);
    return true;
}

FAtmosphereMedium GetAtmosphereMedium(float altitudeKm)
{
    FAtmosphereMedium medium;
    float altitude = max(0.0, altitudeKm);
    float molecularDensity = exp(-altitude / ATMOSPHERE_RAYLEIGH_SCALE_KM);
    float aerosolDensity = exp(-altitude / ATMOSPHERE_MIE_SCALE_KM);

    //hillaire's earth preset approximates the ozone layer with a triangular profile centered at 25 km and extending roughly from 10 to 40 km.
    float ozoneDensity = saturate(1.0 - abs(altitude - 25.0) / 15.0);

    medium.MolecularScattering = ATMOSPHERE_BETA_R * molecularDensity;
    medium.AerosolScattering = ATMOSPHERE_BETA_M_SCATTER * aerosolDensity;
    medium.Extinction = medium.MolecularScattering + ATMOSPHERE_BETA_M_EXTINCT * aerosolDensity + ATMOSPHERE_BETA_OZONE * ozoneDensity;
    return medium;
}

float MolecularPhase(float cosineTheta)
{
    return (3.0 / (16.0 * MATH_PI)) * (1.0 + cosineTheta * cosineTheta);
}

float AerosolPhase(float cosineTheta)
{
    float g = ATMOSPHERE_MIE_G;
    float g2 = g * g;
    float denominator = max(1.0 + g2 - 2.0 * g * cosineTheta, 1.0e-4);

    return (3.0 / (8.0 * MATH_PI)) * ((1.0 - g2) * (1.0 + cosineTheta * cosineTheta)) / ((2.0 + g2) * denominator * sqrt(denominator));
}

float3 RuntimeTransmittanceToSun(float3 samplePosition, float3 sunDirection)
{
    float2 atmosphereHit;

    if (!AtmosphereRaySphere(samplePosition, sunDirection, ATMOSPHERE_TOP_RADIUS_KM, atmosphereHit))
        return 1.0;

    float distanceToTop = atmosphereHit.y;
    float2 groundHit;

    if (AtmosphereRaySphere(samplePosition, sunDirection, ATMOSPHERE_BOTTOM_RADIUS_KM, groundHit))
    {
        float distanceToGround = groundHit.x > 1.0e-4 ? groundHit.x : groundHit.y;

        if (distanceToGround > 1.0e-4 && distanceToGround < distanceToTop)
            return 0.0;
    }

    float stepLength = distanceToTop / (float)ATMOSPHERE_SUN_STEPS;
    float3 opticalDepth = 0.0;

    [unroll]
    for (int i = 0; i < ATMOSPHERE_SUN_STEPS; ++i)
    {
        float t = ((float)i + 0.5) * stepLength;
        float3 p = samplePosition + sunDirection * t;
        float altitude = length(p) - ATMOSPHERE_BOTTOM_RADIUS_KM;
        opticalDepth += GetAtmosphereMedium(altitude).Extinction * stepLength;
    }

    return exp(-opticalDepth);
}

bool PrepareAtmosphereViewRay(float3 viewDirection, out float3 marchOrigin, out float marchDistance, out bool hitsGround)
{
    float eyeAltitudeKm = max((View_WorldCameraOrigin.z - ATMOSPHERE_GROUND_HEIGHT_CM) * 1.0e-5, ATMOSPHERE_MIN_EYE_ALTITUDE_KM);
    float3 eye = float3(0.0, 0.0, ATMOSPHERE_BOTTOM_RADIUS_KM + eyeAltitudeKm);

    float2 atmosphereHit;

    if (!AtmosphereRaySphere(eye, viewDirection, ATMOSPHERE_TOP_RADIUS_KM, atmosphereHit) || atmosphereHit.y <= 0.0)
    {
        marchOrigin = eye;
        marchDistance = 0.0;
        hitsGround = false;
        return false;
    }

    float startDistance = max(atmosphereHit.x, 0.0);
    float endDistance = atmosphereHit.y;
    hitsGround = false;

#if defined(ATMOSPHERE_ENABLE_GROUND)
    float2 groundHit;

    if (AtmosphereRaySphere(eye, viewDirection, ATMOSPHERE_BOTTOM_RADIUS_KM, groundHit))
    {
        float groundDistance = groundHit.x > startDistance + 1.0e-4 ? groundHit.x : groundHit.y;

        if (groundDistance > startDistance + 1.0e-4 && groundDistance < endDistance)
        {
            endDistance = groundDistance;
            hitsGround = true;
        }
    }
#endif

    marchOrigin = eye + viewDirection * startDistance;
    marchDistance = max(endDistance - startDistance, 0.0);
    return marchDistance > 0.0;
}

float3 GetAtmosphereSunDirection()
{
    return SafeNormalizeExact(-View_DirectionalLightDirection);
}

FRuntimeSky ComputeRuntimeAtmosphere(float3 viewDirection, float sampleJitter)
{
    FRuntimeSky sky;
    sky.Radiance = 0.0;
    sky.Transmittance = 1.0;

    float3 rayOrigin;
    float rayDistance;
    bool hitsGround;

    if (!PrepareAtmosphereViewRay(viewDirection, rayOrigin, rayDistance, hitsGround))
        return sky;

    float3 sunDirection = GetAtmosphereSunDirection();
	sunDirection = normalize(-sunDirection);

    //float3 sunIrradiance = max(View_DirectionalLightColor.rgb, 0.0);
	//float3 sunIrradiance = length(View_DirectionalLightColor) * RuntimeTransmittanceToSun(rayOrigin, sunDirection);
	float3 sunIrradiance = length(View_DirectionalLightColor) * length(RuntimeTransmittanceToSun(rayOrigin, sunDirection));
    float phaseCosine = dot(viewDirection, sunDirection);
    float molecularPhase = MolecularPhase(phaseCosine);
    float aerosolPhase = AerosolPhase(phaseCosine);
    float stepLength = rayDistance / (float)ATMOSPHERE_VIEW_STEPS;
    float3 viewTransmittance = 1.0;
    float3 inscattering = 0.0;

    // Blue-noise shifts all midpoint samples by less than one step. This keeps
    // the estimator unbiased enough for this fixed-step use and hides banding.
    float offset = frac(sampleJitter) - 0.5;
    [unroll]
    for (int i = 0; i < ATMOSPHERE_VIEW_STEPS; ++i)
    {
        float t = ((float)i + 0.5 + offset) * stepLength;
        t = clamp(t, 0.0, rayDistance);
        float3 samplePosition = rayOrigin + viewDirection * t;
        float altitude = length(samplePosition) - ATMOSPHERE_BOTTOM_RADIUS_KM;
        FAtmosphereMedium medium = GetAtmosphereMedium(altitude);
        float3 transmittanceToSun = RuntimeTransmittanceToSun(samplePosition, sunDirection);

        float3 singleScattering = medium.MolecularScattering * (molecularPhase * transmittanceToSun) + medium.AerosolScattering * (aerosolPhase * transmittanceToSun);

        // Hillaire's LUT version evaluates higher-order scattering as nearly
        // isotropic. With no LUT, use an energy-bounded local approximation.
        float3 lostSunEnergy = saturate(1.0 - transmittanceToSun);
        float3 multipleScattering = (medium.MolecularScattering + medium.AerosolScattering) * lostSunEnergy * (ATMOSPHERE_MULTISCATTER_STRENGTH / (4.0 * MATH_PI));

        float3 source = sunIrradiance * (singleScattering + multipleScattering);

        float3 stepTransmittance = exp(-stepLength * medium.Extinction);
        float3 integratedSource = source * (1.0 - stepTransmittance) / max(medium.Extinction, 1.0e-7);
        inscattering += viewTransmittance * integratedSource;
        viewTransmittance *= stepTransmittance;
    }

    // The solar disk is attenuated by the same view-path transmittance. It is
    // omitted when the ray terminates on the planet.
    if (!hitsGround)
    {
        float innerCos = cos(ATMOSPHERE_SUN_ANGULAR_RADIUS);
        float outerCos = cos(ATMOSPHERE_SUN_ANGULAR_RADIUS * 1.5);
        float sunDisk = smoothstep(outerCos, innerCos, phaseCosine);
        inscattering += sunIrradiance * viewTransmittance * (sunDisk * ATMOSPHERE_SUN_DISK_SCALE);
    }

    sky.Radiance = max(inscattering, 0.0);
    sky.Transmittance = saturate(viewTransmittance);
    return sky;
}

float HenyeyGreenstein(float g, float costh)
{
    return (1.0 - g * g) / (4.0 * MATH_PI * pow(1.0 + g * g - 2.0 * g * costh, 3.0/2.0));
}

float3 AtmosphereApprox_GetSunDirection(float3 directionalLightDirection)
{
    return normalize(directionalLightDirection * ATMOSPHERE_APPROX_SUN_DIRECTION_SIGN);
}

float AtmosphereApprox_RelativeAirMass(float sunZenithCosine)
{
    // Kasten-Young relative optical air mass. Unlike 1/cos(theta), this remains
    // finite at the horizon and closely tracks a curved atmosphere there.
    float cosineZenith = saturate(sunZenithCosine);
    float zenithDegrees = acos(cosineZenith) * 57.2957795131;
    float horizonTerm = 0.50572 * pow(max(96.07995 - zenithDegrees, 1.0e-3), -1.6364);
    return rcp(cosineZenith + horizonTerm);
}

float3 AtmosphereApprox_VerticalExtinctionDepth()
{
    // Integrals of the exponential molecular/aerosol profiles. The triangular
    // ozone profile in the runtime sky has an integrated thickness of 15 km.
    float3 rayleigh = AtmosphereApprox_BetaRayleigh * ATMOSPHERE_APPROX_RAYLEIGH_SCALE_KM;
    float3 mie = AtmosphereApprox_BetaMieExtinction * ATMOSPHERE_APPROX_MIE_SCALE_KM;
    float3 ozone = AtmosphereApprox_BetaOzone * 15.0;
    return rayleigh + mie + ozone;
}

float3 AtmosphereApprox_SunTransmittance(float3 sunDirection)
{
    float sunHeight = sunDirection.z;
    float airMass = AtmosphereApprox_RelativeAirMass(sunHeight);
    float3 transmittance = exp(-AtmosphereApprox_VerticalExtinctionDepth() * airMass);

    // Resolve the finite solar disk as it crosses the geometric horizon.
    const float solarAngularRadius = 0.004675;
    float horizonVisibility = smoothstep(-solarAngularRadius, solarAngularRadius, sunHeight);
    return transmittance * horizonVisibility;
}

float3 GetSunColor(float3 directionalLightColor, float3 directionalLightDirection)
{
    float3 sunDirection = AtmosphereApprox_GetSunDirection(directionalLightDirection);
    return max(directionalLightColor, 0.0) * AtmosphereApprox_SunTransmittance(sunDirection);
}

float3 GetSunColor()
{
    return GetSunColor(View_DirectionalLightColor.rgb, View_DirectionalLightDirection);
}

float3 AtmosphereApprox_Skylight(float3 sunDirection)
{
    float sunHeight = sunDirection.z;
    float airMass = AtmosphereApprox_RelativeAirMass(sunHeight);

    // Evaluate sunlight at a representative 2 km scattering layer. Molecular
    // and aerosol columns above that layer follow their exponential profiles;
    // nearly the full ozone column remains above it.
    const float representativeAltitudeKm = 2.0;
    float rayleighAbove = exp(-representativeAltitudeKm / ATMOSPHERE_APPROX_RAYLEIGH_SCALE_KM);
    float mieAbove = exp(-representativeAltitudeKm / ATMOSPHERE_APPROX_MIE_SCALE_KM);
    float3 opticalDepthAbove = AtmosphereApprox_BetaRayleigh * ATMOSPHERE_APPROX_RAYLEIGH_SCALE_KM * rayleighAbove + AtmosphereApprox_BetaMieExtinction * ATMOSPHERE_APPROX_MIE_SCALE_KM * mieAbove + AtmosphereApprox_BetaOzone * 15.0 * 0.95;
    float3 transmittanceToLayer = exp(-opticalDepthAbove * airMass);

    // Approximate attenuation from the representative layer back to an observer
    // near ground level, averaged over useful upper-hemisphere directions.
    float3 opticalDepthBelow = AtmosphereApprox_BetaRayleigh * ATMOSPHERE_APPROX_RAYLEIGH_SCALE_KM * (1.0 - rayleighAbove) + AtmosphereApprox_BetaMieExtinction * ATMOSPHERE_APPROX_MIE_SCALE_KM * (1.0 - mieAbove);
    float3 layerToObserver = exp(-opticalDepthBelow * 1.5);

    float3 scatteringDepth = AtmosphereApprox_BetaRayleigh * ATMOSPHERE_APPROX_RAYLEIGH_SCALE_KM + AtmosphereApprox_BetaMieScattering * ATMOSPHERE_APPROX_MIE_SCALE_KM;

    // More of the long horizon path contributes near sunset than at noon.
    float horizonAmount = 1.0 - saturate(sunHeight * 4.0);
    float singleScatterScale = lerp(2.5, 6.0, horizonAmount);
    float3 singleScattering = transmittanceToLayer * layerToObserver * scatteringDepth * singleScatterScale;

    // Cheap isotropic higher-order term. The fractional transmittance power
    // keeps twilight warm instead of allowing a gray/blue energy floor.
    float boundedAirMass = min(airMass, 12.0);
    float3 scatteredFraction = 1.0 - exp(-scatteringDepth * boundedAirMass);
    float3 multipleScattering = scatteredFraction * pow(saturate(transmittanceToLayer), 0.35) * 0.35;

    // Direct sun disappears at the horizon, while atmospheric twilight lingers
    // until the sun is roughly 8.5 degrees below it.
    float twilightVisibility = smoothstep(-0.15, 0.02, sunHeight);
    return max(singleScattering + multipleScattering, 0.0) * twilightVisibility * ATMOSPHERE_APPROX_SKYLIGHT_STRENGTH;
}

float3 GetSkylightColor(float3 directionalLightColor, float3 directionalLightDirection)
{
    float3 sunDirection = AtmosphereApprox_GetSunDirection(directionalLightDirection);
    return max(directionalLightColor, 0.0) * AtmosphereApprox_Skylight(sunDirection);
}

float3 GetSkylightColor()
{
    return GetSkylightColor(View_DirectionalLightColor.rgb, View_DirectionalLightDirection);
}

//|||||||||||||||||||||||||||||||||||||||||||| MAIN ||||||||||||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||||||||||||| MAIN ||||||||||||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||||||||||||| MAIN ||||||||||||||||||||||||||||||||||||||||||||

FApplyFogPSOutput main(FApplyFogPSInput input)
{
    FApplyFogPSOutput output;

    uint2 depthPixel = (uint2)(View_BufferSizeAndInvSize.xy * input.UV);
    float deviceZ = SceneTexturesStruct_SceneDepthTexture.Load(int3(depthPixel, 0)).r;
    float sceneDistance = ConvertFromDeviceZ(deviceZ);

    int3 noiseCoord = int3((int)input.Position.x & 127, (int)input.Position.y & 127, View_StateFrameIndex & 7);
    float3 noise = View_SpatiotemporalBlueNoiseVolumeTexture.Load(int4(noiseCoord, 0)).rgb;
    float3 viewDirection = SafeNormalizeExact(input.ViewRay);

    FFarFogResult farFog;
    farFog.Scattering = 0.0;
    farFog.Transmittance = 1.0;

    float3 skyEnvironment = 0.0;
    if (deviceZ == 0.0)
    {
        int skyContext = SelectFogContext(viewDirection, noise.x);
        sceneDistance = LimitSkyDistance(skyContext, viewDirection.z, sceneDistance);
        skyEnvironment = SampleSkyEnvironment(skyContext, viewDirection);

        #if defined(ATMOSPHERE)
	        FRuntimeSky sky = ComputeRuntimeAtmosphere(viewDirection, 1.0f);
		    skyEnvironment = sky.Radiance * View_OneOverPreExposure * 0.0003f;
		    //farFog.Transmittance = sky.Transmittance;
        #endif
    }

    sceneDistance = max(1.0e-4, sceneDistance);
    float3 rayVector = sceneDistance * input.ViewRay;
    float3 translatedWorldPosition = rayVector - View_PreViewTranslation;
    float3 volumeJitter = float3(SignedEqualAreaDecode(noise.x), SignedEqualAreaDecode(noise.y), SignedEqualAreaDecode(noise.z));
    float3 volumeCoordinate = ComputeVolumeCoordinate(translatedWorldPosition, volumeJitter);

    float rayLength = length(rayVector);
    float projectedLength = max(1.0e-4, dot(rayVector, View_ViewForward));
    float farFogStartDistance = max(0.0, (rayLength / projectedLength)
                                        * View_VolumetricFogMaxDistance);

    if (rayLength >= farFogStartDistance)
    {
        float3 rayDirection = SafeNormalizeExact(rayVector);
        int farContext = SelectFogContext(rayDirection, noise.x);
        farFog = EvaluateFarFog(farContext, rayVector, rayDirection, rayLength, farFogStartDistance, volumeCoordinate);
    }

    float3 volumeScattering = 0.0;
    float3 volumeTransmittance = 1.0;
    if (FogStruct_IntegratedScatteringVolumeTextureContext.x > 0.0)
    {
        float3 uvw = ClampIntegratedFogUVW(volumeCoordinate);
        volumeScattering = View_OneOverPreExposure * FogStruct_IntegratedScatteringVolumeATexture.SampleLevel(View_SharedBilinearClampedSampler, uvw, 0.0).rgb * FOG_DENSITY_NEAR_FIELD_MULTIPLIER;
        volumeTransmittance = FogStruct_IntegratedScatteringVolumeBTexture.SampleLevel(View_SharedBilinearClampedSampler, uvw, 0.0).rgb;
    }

    #if defined(DISABLE_NEAR_FOG)
        volumeScattering = 0.0f;
        volumeTransmittance = 1.0f;
    #endif

    #if defined(DISABLE_FAR_FOG)
        farFog.Scattering = 0.0f;
        farFog.Transmittance = 1.0f;
    #endif

    float3 combinedScattering = volumeTransmittance * farFog.Scattering + volumeScattering;
    float3 combinedTransmittance = volumeTransmittance * farFog.Transmittance;
    float3 fogAndSky = combinedScattering + combinedTransmittance * skyEnvironment;

    output.FogColor = float4(View_PreExposure * fogAndSky, 0.0);
    output.Transmittance = float4(combinedTransmittance, 1.0);

	//output.FogColor = float4(View_PreExposure * skyEnvironment, 1.0);

	//output.Transmittance = float4(1, 1, 1, 1);

    if (deviceZ > 0.0)
	{
        //float distantFog = 1.0f - saturate(sceneDistance / 500000);
        //output.FogColor = 1.0f - saturate(sceneDistance / 500000);
        //output.FogColor += float4(View_PreExposure * skyEnvironment, 0.0);
        //output.FogColor.rgb += (1.0f - distantFog) * GetSkylightColor() * View_PreExposure;
        //output.FogColor.rgb += (1.0f - distantFog) * GetSunColor() * View_PreExposure * HenyeyGreenstein(0.8, saturate(dot(viewDirection, View_DirectionalLightDirection)));


        //output.Transmittance = float4(1, 1, 1, 1);
        //output.Transmittance = float4(distantFog, distantFog, distantFog, 1);
	}

    //output.FogColor = float4(0.0, 0.0, 0.0, 0.0);
    //output.Transmittance = float4(1, 1, 1, 1);

    return output;
}