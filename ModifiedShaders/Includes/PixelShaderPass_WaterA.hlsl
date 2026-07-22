// WaterA.hlsl

#define WATERA_WAVE_NORMAL_STRENGTH             0.5f
#define WATERA_REFRACTION_STRENGTH              0.15f
#define WATERA_REFRACTION_NOISE_STRENGTH        0.0075f
#define WATERA_REFRACTION_NOISE_DEPTH_SCALE     0.0015f
#define WATERA_DEPTH_BLUR_START_CM              1000.0f
#define WATERA_DEPTH_BLUR_FULL_CM               4000.0f
#define WATERA_DEPTH_BLUR_MAX_RADIUS_PIXELS     16.0f
#define ENABLE_REFRACTION_CHROMATIC_DISPERSION
#define REFRACTION_DISPERSION_STRENGTH           0.075f
#define WATERA_REFRACTION_WATER_PLANE_Z_BIAS    0.0f

#define CAUSTICS_WORLD_SCALE       0.04f // one primary cell per ~80 cm
#define CAUSTICS_ANIMATION_SPEED   5.70f
#define CAUSTICS_EDGE_WIDTH        0.095f
#define CAUSTICS_INTENSITY         5.5f
#define CAUSTICS_SHALLOW_FADE      0.020f  // reaches full strength by ~50 cm
#define CAUSTICS_DEPTH_FALLOFF     0.0008f // half strength after ~12.5 m

SamplerState View_SharedPointClampedSampler : register(s0, space0);
SamplerState View_SharedBilinearWrappedSampler : register(s1, space0);
SamplerState View_SharedBilinearClampedSampler : register(s2, space0);
SamplerState View_SharedTrilinearClampedSampler : register(s3, space0);
SamplerState View_SharedAnisotropic4XWrappedSampler : register(s4, space0);
SamplerState View_SharedAnisotropic4XClampedSampler : register(s5, space0);

StructuredBuffer<float4> View_PrimitiveSceneData : register(t0, space0);
Texture3D<float4> View_SpatiotemporalBlueNoiseVolumeTexture : register(t1, space0);

#if defined(GAME_VERSION_1_0_0_4)
    Texture2D<float4> TranslucentBasePass_Shared_Forward_DirectionalLightShadowmapAtlas : register(t2, space0);
    Buffer<int> TranslucentBasePass_Shared_Forward_DirectionalLightStaticShadowDepthTileIndices : register(t3, space0);
    Buffer<float2> TranslucentBasePass_Shared_Forward_DirectionalLightStaticShadowDepthRanges : register(t4, space0);
    Buffer<uint> TranslucentBasePass_Shared_Forward_DirectionalLightStaticShadowLoadedTileData : register(t5, space0);
    Buffer<uint> TranslucentBasePass_Shared_Forward_DirectionalLightStaticShadowDepthTileData : register(t6, space0);
    Buffer<float4> TranslucentBasePass_Shared_Forward_ForwardLocalLightBuffer : register(t7, space0);
    Buffer<uint> TranslucentBasePass_Shared_Forward_NumCulledLightsGrid : register(t8, space0);
    Buffer<uint> TranslucentBasePass_Shared_Forward_CulledLightDataGrid : register(t9, space0);
    Texture2D<float4> TranslucentBasePass_SceneTextures_SceneDepthTexture : register(t10, space0);
    Texture2D<float4> TranslucentBasePass_HZBTexture : register(t11, space0);
    Texture3D<float4> TranslucentBasePass_TranslucencyLightingVolumeEnvironmentA : register(t12, space0);
    Texture3D<float4> TranslucentBasePass_TranslucencyLightingVolumeEnvironmentB : register(t13, space0);
    Texture3D<float4> TranslucentBasePass_TranslucencyLightingVolumeEnvironmentC : register(t14, space0);
    Texture2D<float4> TranslucentBasePass_SceneColorCopyTexture : register(t15, space0);
    Texture2D<float4> Material_Texture2D_0 : register(t16, space0);
    Texture2D<float4> Material_Texture2D_1 : register(t17, space0);
    Texture2D<float4> Material_Texture2D_2 : register(t18, space0);
    Texture2D<float4> Material_Texture2D_3 : register(t19, space0);
    Texture2D<float4> Material_Texture2D_4 : register(t20, space0);
    Texture2D<float4> Material_Texture2D_7 : register(t21, space0);
    Texture2D<float4> Material_Texture2D_8 : register(t22, space0);
    Texture2D<float4> Material_Texture2D_15 : register(t23, space0);
    Texture2D<float4> Material_Texture2D_16 : register(t24, space0);
    Texture2D<float4> Material_Texture2D_17 : register(t25, space0);
    Texture2D<float4> Material_Texture2D_18 : register(t26, space0);
    Texture2D<float4> Material_Texture2D_20 : register(t27, space0);
    Texture2D<float4> Material_Texture2D_21 : register(t28, space0);
    Texture2D<float4> Material_Texture2D_23 : register(t29, space0);
    Texture2D<float4> Material_Texture2D_25 : register(t30, space0);
    Texture2D<float4> Material_Texture2D_26 : register(t31, space0);
    Texture2D<float4> Material_Texture2D_27 : register(t32, space0);
    Texture2D<float4> Material_Texture2D_28 : register(t33, space0);
    Texture2D<float4> Material_Texture2D_29 : register(t34, space0);
    Texture2D<float4> Material_Texture2D_30 : register(t35, space0);
#else
    Buffer<int> TranslucentBasePass_Shared_Forward_DirectionalLightStaticShadowDepthTileIndices : register(t2, space0);
    Buffer<float2> TranslucentBasePass_Shared_Forward_DirectionalLightStaticShadowDepthRanges : register(t3, space0);
    Buffer<uint> TranslucentBasePass_Shared_Forward_DirectionalLightStaticShadowLoadedTileData : register(t4, space0);
    Buffer<uint> TranslucentBasePass_Shared_Forward_DirectionalLightStaticShadowDepthTileData : register(t5, space0);
    Buffer<float4> TranslucentBasePass_Shared_Forward_ForwardLocalLightBuffer : register(t6, space0);
    Buffer<uint> TranslucentBasePass_Shared_Forward_NumCulledLightsGrid : register(t7, space0);
    Buffer<uint> TranslucentBasePass_Shared_Forward_CulledLightDataGrid : register(t8, space0);
    Texture2D<float4> TranslucentBasePass_SceneTextures_SceneDepthTexture : register(t9, space0);
    Texture2D<float4> TranslucentBasePass_HZBTexture : register(t10, space0);
    Texture3D<float4> TranslucentBasePass_TranslucencyLightingVolumeEnvironmentA : register(t11, space0);
    Texture3D<float4> TranslucentBasePass_TranslucencyLightingVolumeEnvironmentB : register(t12, space0);
    Texture3D<float4> TranslucentBasePass_TranslucencyLightingVolumeEnvironmentC : register(t13, space0);
    Texture2D<float4> TranslucentBasePass_SceneColorCopyTexture : register(t14, space0);
    Texture2D<float4> Material_Texture2D_0 : register(t15, space0);
    Texture2D<float4> Material_Texture2D_1 : register(t16, space0);
    Texture2D<float4> Material_Texture2D_2 : register(t17, space0);
    Texture2D<float4> Material_Texture2D_3 : register(t18, space0);
    Texture2D<float4> Material_Texture2D_4 : register(t19, space0);
    Texture2D<float4> Material_Texture2D_7 : register(t20, space0);
    Texture2D<float4> Material_Texture2D_8 : register(t21, space0);
    Texture2D<float4> Material_Texture2D_15 : register(t22, space0);
    Texture2D<float4> Material_Texture2D_16 : register(t23, space0);
    Texture2D<float4> Material_Texture2D_17 : register(t24, space0);
    Texture2D<float4> Material_Texture2D_18 : register(t25, space0);
    Texture2D<float4> Material_Texture2D_20 : register(t26, space0);
    Texture2D<float4> Material_Texture2D_21 : register(t27, space0);
    Texture2D<float4> Material_Texture2D_23 : register(t28, space0);
    Texture2D<float4> Material_Texture2D_25 : register(t29, space0);
    Texture2D<float4> Material_Texture2D_26 : register(t30, space0);
    Texture2D<float4> Material_Texture2D_27 : register(t31, space0);
    Texture2D<float4> Material_Texture2D_28 : register(t32, space0);
    Texture2D<float4> Material_Texture2D_29 : register(t33, space0);
    Texture2D<float4> Material_Texture2D_30 : register(t34, space0);
#endif

Texture2D<float4> ForwardLightProfileTextures[] : register(t0, space13);

#if !defined(GAME_VERSION_1_0_0_4)
    Texture2D<float4> ShadowMaps[] : register(t0, space21);
#endif

TextureCube<float4> ReflectionCubemaps[] : register(t0, space1);

cbuffer View : register(b0, space0) 
{
	row_major float4x4 View_TranslatedWorldToClip : packoffset(c0.x);
	row_major float4x4 View_WorldToOrthographicClip : packoffset(c4.x);
	row_major float4x4 View_TranslatedWorldToOrthographicClip : packoffset(c8.x);
	row_major float4x4 View_WorldToClip : packoffset(c12.x);
	row_major float4x4 View_ClipToWorld : packoffset(c16.x);
	row_major float4x4 View_TranslatedWorldToView : packoffset(c20.x);
	row_major float4x4 View_ViewToTranslatedWorld : packoffset(c24.x);
	row_major float4x4 View_TranslatedWorldToCameraView : packoffset(c28.x);
	row_major float4x4 View_CameraViewToTranslatedWorld : packoffset(c32.x);
	row_major float4x4 View_ViewToClip : packoffset(c36.x);
	row_major float4x4 View_ViewToClipNoAA : packoffset(c40.x);
	row_major float4x4 View_ClipToView : packoffset(c44.x);
	row_major float4x4 View_ClipToTranslatedWorld : packoffset(c48.x);
	row_major float4x4 View_SVPositionToTranslatedWorld : packoffset(c52.x);
	row_major float4x4 View_ScreenToWorld : packoffset(c56.x);
	row_major float4x4 View_ScreenToTranslatedWorld : packoffset(c60.x);
	row_major float4x4 View_MobileMultiviewShadowTransform : packoffset(c64.x);
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
	row_major float4x4 View_PrevProjection : packoffset(c79.x);
	row_major float4x4 View_PrevViewProj : packoffset(c83.x);
	row_major float4x4 View_PrevViewRotationProj : packoffset(c87.x);
	row_major float4x4 View_PrevViewToClip : packoffset(c91.x);
	row_major float4x4 View_PrevClipToView : packoffset(c95.x);
	row_major float4x4 View_PrevTranslatedWorldToClip : packoffset(c99.x);
	row_major float4x4 View_PrevWorldToOrthographicClip : packoffset(c103.x);
	row_major float4x4 View_PrevTranslatedWorldToOrthographicClip : packoffset(c107.x);
	row_major float4x4 View_PrevTranslatedWorldToView : packoffset(c111.x);
	row_major float4x4 View_PrevViewToTranslatedWorld : packoffset(c115.x);
	row_major float4x4 View_PrevTranslatedWorldToCameraView : packoffset(c119.x);
	row_major float4x4 View_PrevCameraViewToTranslatedWorld : packoffset(c123.x);
	float3 View_PrevWorldCameraOrigin : packoffset(c127.x);
	float PrePadding_View_2044 : packoffset(c127.w);
	float3 View_PrevWorldViewOrigin : packoffset(c128.x);
	float PrePadding_View_2060 : packoffset(c128.w);
	float3 View_PrevPreViewTranslation : packoffset(c129.x);
	float PrePadding_View_2076 : packoffset(c129.w);
	row_major float4x4 View_PrevInvViewProj : packoffset(c130.x);
	row_major float4x4 View_PrevScreenToTranslatedWorld : packoffset(c134.x);
	row_major float4x4 View_ClipToPrevClip : packoffset(c138.x);
	row_major float4x4 View_ClipToPrevClipWithoutTranslation : packoffset(c142.x);
	row_major float4x4 View_ProjectionToWorld : packoffset(c146.x);
	row_major float4x4 View_WorldToProjection : packoffset(c150.x);
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
	row_major float4x4 View_SkyViewLutReferential : packoffset(c208.x);
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
	row_major float4x4 View_WorldToVirtualTexture : packoffset(c286.x);
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

cbuffer TranslucentBasePass : register(b1, space0) 
{
	int TranslucentBasePass_Shared_Forward_NumLocalLights : packoffset(c0.x);
	int TranslucentBasePass_Shared_Forward_NumReflectionCaptures : packoffset(c0.y);
	int TranslucentBasePass_Shared_Forward_HasDirectionalLight : packoffset(c0.z);
	int TranslucentBasePass_Shared_Forward_NumGridCells : packoffset(c0.w);
	int3 TranslucentBasePass_Shared_Forward_CulledGridSize : packoffset(c1.x);
	int TranslucentBasePass_Shared_Forward_MaxCulledLightsPerCell : packoffset(c1.w);
	int TranslucentBasePass_Shared_Forward_LightGridPixelSizeShift : packoffset(c2.x);
	int PrePadding_TranslucentBasePass_Shared_Forward_36 : packoffset(c2.y);
	int PrePadding_TranslucentBasePass_Shared_Forward_40 : packoffset(c2.z);
	int PrePadding_TranslucentBasePass_Shared_Forward_44 : packoffset(c2.w);
	float3 TranslucentBasePass_Shared_Forward_LightGridZParams : packoffset(c3.x);
	float PrePadding_TranslucentBasePass_Shared_Forward_60 : packoffset(c3.w);
	float3 TranslucentBasePass_Shared_Forward_DirectionalLightColor : packoffset(c4.x);
	float PrePadding_TranslucentBasePass_Shared_Forward_76 : packoffset(c4.w);
	float3 TranslucentBasePass_Shared_Forward_DirectionalLightDirection : packoffset(c5.x);
	float PrePadding_TranslucentBasePass_Shared_Forward_92 : packoffset(c5.w);
	float3 TranslucentBasePass_Shared_Forward_DirectionalLightTangent : packoffset(c6.x);
	int TranslucentBasePass_Shared_Forward_DirectionalLightLightContextBitField : packoffset(c6.w);
	float2 TranslucentBasePass_Shared_Forward_DirectionalLightDistanceFadeMAD : packoffset(c7.x);
	int TranslucentBasePass_Shared_Forward_NumDirectionalLightCascades : packoffset(c7.z);
	int PrePadding_TranslucentBasePass_Shared_Forward_124 : packoffset(c7.w);
	float4 TranslucentBasePass_Shared_Forward_CascadeEndDepths : packoffset(c8.x);
	row_major float4x4 TranslucentBasePass_Shared_Forward_DirectionalLightWorldToShadowMatrix[4] : packoffset(c9.x);
	float4 TranslucentBasePass_Shared_Forward_DirectionalLightShadowmapMinMax[4] : packoffset(c25.x);
	float4 TranslucentBasePass_Shared_Forward_DirectionalLightShadowmapAtlasBufferSize : packoffset(c29.x);
	float TranslucentBasePass_Shared_Forward_DirectionalLightDepthBias : packoffset(c30.x);
	int TranslucentBasePass_Shared_Forward_DirectionalLightUseStaticShadowing : packoffset(c30.y);
	int TranslucentBasePass_Shared_Forward_SimpleLightsEndIndex : packoffset(c30.z);
	int TranslucentBasePass_Shared_Forward_ClusteredDeferredSupportedEndIndex : packoffset(c30.w);
	int4 TranslucentBasePass_Shared_Forward_DirectionalLightStaticShadowDepthTileSize : packoffset(c31.x);
	int4 TranslucentBasePass_Shared_Forward_DirectionalLightStaticShadowDepthTileRange : packoffset(c32.x);
	row_major float4x4 TranslucentBasePass_Shared_Forward_DirectionalLightWorldToStaticShadow : packoffset(c33.x);
	float PrePadding_TranslucentBasePass_Shared_ForwardISR_592 : packoffset(c37.x);
	float PrePadding_TranslucentBasePass_Shared_ForwardISR_596 : packoffset(c37.y);
	float PrePadding_TranslucentBasePass_Shared_ForwardISR_600 : packoffset(c37.z);
	float PrePadding_TranslucentBasePass_Shared_ForwardISR_604 : packoffset(c37.w);
	float PrePadding_TranslucentBasePass_Shared_ForwardISR_608 : packoffset(c38.x);
	float PrePadding_TranslucentBasePass_Shared_ForwardISR_612 : packoffset(c38.y);
	float PrePadding_TranslucentBasePass_Shared_ForwardISR_616 : packoffset(c38.z);
	float PrePadding_TranslucentBasePass_Shared_ForwardISR_620 : packoffset(c38.w);
	float PrePadding_TranslucentBasePass_Shared_ForwardISR_624 : packoffset(c39.x);
	float PrePadding_TranslucentBasePass_Shared_ForwardISR_628 : packoffset(c39.y);
	float PrePadding_TranslucentBasePass_Shared_ForwardISR_632 : packoffset(c39.z);
	float PrePadding_TranslucentBasePass_Shared_ForwardISR_636 : packoffset(c39.w);
	float PrePadding_TranslucentBasePass_Shared_ForwardISR_640 : packoffset(c40.x);
	float PrePadding_TranslucentBasePass_Shared_ForwardISR_644 : packoffset(c40.y);
	float PrePadding_TranslucentBasePass_Shared_ForwardISR_648 : packoffset(c40.z);
	float PrePadding_TranslucentBasePass_Shared_ForwardISR_652 : packoffset(c40.w);
	float PrePadding_TranslucentBasePass_Shared_ForwardISR_656 : packoffset(c41.x);
	float PrePadding_TranslucentBasePass_Shared_ForwardISR_660 : packoffset(c41.y);
	float PrePadding_TranslucentBasePass_Shared_ForwardISR_664 : packoffset(c41.z);
	float PrePadding_TranslucentBasePass_Shared_ForwardISR_668 : packoffset(c41.w);
	int TranslucentBasePass_Shared_ForwardISR_NumLocalLights : packoffset(c42.x);
	int TranslucentBasePass_Shared_ForwardISR_NumReflectionCaptures : packoffset(c42.y);
	int TranslucentBasePass_Shared_ForwardISR_HasDirectionalLight : packoffset(c42.z);
	int TranslucentBasePass_Shared_ForwardISR_NumGridCells : packoffset(c42.w);
	int3 TranslucentBasePass_Shared_ForwardISR_CulledGridSize : packoffset(c43.x);
	int TranslucentBasePass_Shared_ForwardISR_MaxCulledLightsPerCell : packoffset(c43.w);
	int TranslucentBasePass_Shared_ForwardISR_LightGridPixelSizeShift : packoffset(c44.x);
	int PrePadding_TranslucentBasePass_Shared_ForwardISR_708 : packoffset(c44.y);
	int PrePadding_TranslucentBasePass_Shared_ForwardISR_712 : packoffset(c44.z);
	int PrePadding_TranslucentBasePass_Shared_ForwardISR_716 : packoffset(c44.w);
	float3 TranslucentBasePass_Shared_ForwardISR_LightGridZParams : packoffset(c45.x);
	float PrePadding_TranslucentBasePass_Shared_ForwardISR_732 : packoffset(c45.w);
	float3 TranslucentBasePass_Shared_ForwardISR_DirectionalLightColor : packoffset(c46.x);
	float PrePadding_TranslucentBasePass_Shared_ForwardISR_748 : packoffset(c46.w);
	float3 TranslucentBasePass_Shared_ForwardISR_DirectionalLightDirection : packoffset(c47.x);
	float PrePadding_TranslucentBasePass_Shared_ForwardISR_764 : packoffset(c47.w);
	float3 TranslucentBasePass_Shared_ForwardISR_DirectionalLightTangent : packoffset(c48.x);
	int TranslucentBasePass_Shared_ForwardISR_DirectionalLightLightContextBitField : packoffset(c48.w);
	float2 TranslucentBasePass_Shared_ForwardISR_DirectionalLightDistanceFadeMAD : packoffset(c49.x);
	int TranslucentBasePass_Shared_ForwardISR_NumDirectionalLightCascades : packoffset(c49.z);
	int PrePadding_TranslucentBasePass_Shared_ForwardISR_796 : packoffset(c49.w);
	float4 TranslucentBasePass_Shared_ForwardISR_CascadeEndDepths : packoffset(c50.x);
	row_major float4x4 TranslucentBasePass_Shared_ForwardISR_DirectionalLightWorldToShadowMatrix[4] : packoffset(c51.x);
	float4 TranslucentBasePass_Shared_ForwardISR_DirectionalLightShadowmapMinMax[4] : packoffset(c67.x);
	float4 TranslucentBasePass_Shared_ForwardISR_DirectionalLightShadowmapAtlasBufferSize : packoffset(c71.x);
	float TranslucentBasePass_Shared_ForwardISR_DirectionalLightDepthBias : packoffset(c72.x);
	int TranslucentBasePass_Shared_ForwardISR_DirectionalLightUseStaticShadowing : packoffset(c72.y);
	int TranslucentBasePass_Shared_ForwardISR_SimpleLightsEndIndex : packoffset(c72.z);
	int TranslucentBasePass_Shared_ForwardISR_ClusteredDeferredSupportedEndIndex : packoffset(c72.w);
	int4 TranslucentBasePass_Shared_ForwardISR_DirectionalLightStaticShadowDepthTileSize : packoffset(c73.x);
	int4 TranslucentBasePass_Shared_ForwardISR_DirectionalLightStaticShadowDepthTileRange : packoffset(c74.x);
	row_major float4x4 TranslucentBasePass_Shared_ForwardISR_DirectionalLightWorldToStaticShadow : packoffset(c75.x);
	float PrePadding_TranslucentBasePass_Shared_Reflection_1264 : packoffset(c79.x);
	float PrePadding_TranslucentBasePass_Shared_Reflection_1268 : packoffset(c79.y);
	float PrePadding_TranslucentBasePass_Shared_Reflection_1272 : packoffset(c79.z);
	float PrePadding_TranslucentBasePass_Shared_Reflection_1276 : packoffset(c79.w);
	float PrePadding_TranslucentBasePass_Shared_Reflection_1280 : packoffset(c80.x);
	float PrePadding_TranslucentBasePass_Shared_Reflection_1284 : packoffset(c80.y);
	float PrePadding_TranslucentBasePass_Shared_Reflection_1288 : packoffset(c80.z);
	float PrePadding_TranslucentBasePass_Shared_Reflection_1292 : packoffset(c80.w);
	float PrePadding_TranslucentBasePass_Shared_Reflection_1296 : packoffset(c81.x);
	float PrePadding_TranslucentBasePass_Shared_Reflection_1300 : packoffset(c81.y);
	float PrePadding_TranslucentBasePass_Shared_Reflection_1304 : packoffset(c81.z);
	float PrePadding_TranslucentBasePass_Shared_Reflection_1308 : packoffset(c81.w);
	float PrePadding_TranslucentBasePass_Shared_Reflection_1312 : packoffset(c82.x);
	float PrePadding_TranslucentBasePass_Shared_Reflection_1316 : packoffset(c82.y);
	float PrePadding_TranslucentBasePass_Shared_Reflection_1320 : packoffset(c82.z);
	float PrePadding_TranslucentBasePass_Shared_Reflection_1324 : packoffset(c82.w);
	float PrePadding_TranslucentBasePass_Shared_Reflection_1328 : packoffset(c83.x);
	float PrePadding_TranslucentBasePass_Shared_Reflection_1332 : packoffset(c83.y);
	float PrePadding_TranslucentBasePass_Shared_Reflection_1336 : packoffset(c83.z);
	float PrePadding_TranslucentBasePass_Shared_Reflection_1340 : packoffset(c83.w);
	float4 TranslucentBasePass_Shared_Reflection_SkyLightParameters : packoffset(c84.x);
	float TranslucentBasePass_Shared_Reflection_SkyLightCubemapBrightness : packoffset(c85.x);
	float PrePadding_TranslucentBasePass_Shared_PlanarReflection_1364 : packoffset(c85.y);
	float PrePadding_TranslucentBasePass_Shared_PlanarReflection_1368 : packoffset(c85.z);
	float PrePadding_TranslucentBasePass_Shared_PlanarReflection_1372 : packoffset(c85.w);
	float PrePadding_TranslucentBasePass_Shared_PlanarReflection_1376 : packoffset(c86.x);
	float PrePadding_TranslucentBasePass_Shared_PlanarReflection_1380 : packoffset(c86.y);
	float PrePadding_TranslucentBasePass_Shared_PlanarReflection_1384 : packoffset(c86.z);
	float PrePadding_TranslucentBasePass_Shared_PlanarReflection_1388 : packoffset(c86.w);
	float PrePadding_TranslucentBasePass_Shared_PlanarReflection_1392 : packoffset(c87.x);
	float PrePadding_TranslucentBasePass_Shared_PlanarReflection_1396 : packoffset(c87.y);
	float PrePadding_TranslucentBasePass_Shared_PlanarReflection_1400 : packoffset(c87.z);
	float PrePadding_TranslucentBasePass_Shared_PlanarReflection_1404 : packoffset(c87.w);
	float PrePadding_TranslucentBasePass_Shared_PlanarReflection_1408 : packoffset(c88.x);
	float PrePadding_TranslucentBasePass_Shared_PlanarReflection_1412 : packoffset(c88.y);
	float PrePadding_TranslucentBasePass_Shared_PlanarReflection_1416 : packoffset(c88.z);
	float PrePadding_TranslucentBasePass_Shared_PlanarReflection_1420 : packoffset(c88.w);
	float4 TranslucentBasePass_Shared_PlanarReflection_ReflectionPlane : packoffset(c89.x);
	float4 TranslucentBasePass_Shared_PlanarReflection_PlanarReflectionOrigin : packoffset(c90.x);
	float4 TranslucentBasePass_Shared_PlanarReflection_PlanarReflectionXAxis : packoffset(c91.x);
	float4 TranslucentBasePass_Shared_PlanarReflection_PlanarReflectionYAxis : packoffset(c92.x);
	row_major float3x4 TranslucentBasePass_Shared_PlanarReflection_InverseTransposeMirrorMatrix : packoffset(c93.x);
	float3 TranslucentBasePass_Shared_PlanarReflection_PlanarReflectionParameters : packoffset(c96.x);
	float PrePadding_TranslucentBasePass_Shared_PlanarReflection_1548 : packoffset(c96.w);
	float2 TranslucentBasePass_Shared_PlanarReflection_PlanarReflectionParameters2 : packoffset(c97.x);
	float PrePadding_TranslucentBasePass_Shared_PlanarReflection_1560 : packoffset(c97.z);
	float PrePadding_TranslucentBasePass_Shared_PlanarReflection_1564 : packoffset(c97.w);
	row_major float4x4 TranslucentBasePass_Shared_PlanarReflection_ProjectionWithExtraFOV[2] : packoffset(c98.x);
	float4 TranslucentBasePass_Shared_PlanarReflection_PlanarReflectionScreenScaleBias[2] : packoffset(c106.x);
	float2 TranslucentBasePass_Shared_PlanarReflection_PlanarReflectionScreenBound : packoffset(c108.x);
	int TranslucentBasePass_Shared_PlanarReflection_bIsStereo : packoffset(c108.z);
	float PrePadding_TranslucentBasePass_Shared_Fog_1740 : packoffset(c108.w);
	float PrePadding_TranslucentBasePass_Shared_Fog_1744 : packoffset(c109.x);
	float PrePadding_TranslucentBasePass_Shared_Fog_1748 : packoffset(c109.y);
	float PrePadding_TranslucentBasePass_Shared_Fog_1752 : packoffset(c109.z);
	float PrePadding_TranslucentBasePass_Shared_Fog_1756 : packoffset(c109.w);
	float4 TranslucentBasePass_Shared_Fog_DirectionalLightDirection : packoffset(c110.x);
	float4 TranslucentBasePass_Shared_Fog_DirectionalLightColor : packoffset(c111.x);
	float TranslucentBasePass_Shared_Fog_MediumIOR : packoffset(c112.x);
	int TranslucentBasePass_Shared_Fog_NumberOfFogContext : packoffset(c112.y);
	int PrePadding_TranslucentBasePass_Shared_Fog_1800 : packoffset(c112.z);
	int PrePadding_TranslucentBasePass_Shared_Fog_1804 : packoffset(c112.w);
	float4 TranslucentBasePass_Shared_Fog_HeightBasedContextAlbedo[4] : packoffset(c113.x);
	float4 TranslucentBasePass_Shared_Fog_HeightBasedContextDensity[4] : packoffset(c117.x);
	float4 TranslucentBasePass_Shared_Fog_TransmitContextAlbedo[4] : packoffset(c121.x);
	float4 TranslucentBasePass_Shared_Fog_TransmitContextDensity[4] : packoffset(c125.x);
	float4 TranslucentBasePass_Shared_Fog_RegionContext[4] : packoffset(c129.x);
	float4 TranslucentBasePass_Shared_Fog_LightContext[4] : packoffset(c133.x);
	float4 TranslucentBasePass_Shared_Fog_EnvironmentTextureContext[4] : packoffset(c137.x);
	float4 TranslucentBasePass_Shared_Fog_DistanceTextureContext[4] : packoffset(c141.x);
	float PrePadding_TranslucentBasePass_Shared_Fog_2320 : packoffset(c145.x);
	float PrePadding_TranslucentBasePass_Shared_Fog_2324 : packoffset(c145.y);
	float PrePadding_TranslucentBasePass_Shared_Fog_2328 : packoffset(c145.z);
	float PrePadding_TranslucentBasePass_Shared_Fog_2332 : packoffset(c145.w);
	float4 TranslucentBasePass_Shared_Fog_FarIntegratedScatteringTextureContext : packoffset(c146.x);
	float PrePadding_TranslucentBasePass_Shared_Fog_2352 : packoffset(c147.x);
	float PrePadding_TranslucentBasePass_Shared_Fog_2356 : packoffset(c147.y);
	float PrePadding_TranslucentBasePass_Shared_Fog_2360 : packoffset(c147.z);
	float PrePadding_TranslucentBasePass_Shared_Fog_2364 : packoffset(c147.w);
	float4 TranslucentBasePass_Shared_Fog_IntegratedScatteringVolumeTextureContext : packoffset(c148.x);
	float4 TranslucentBasePass_Shared_FogISR_DirectionalLightDirection : packoffset(c149.x);
	float4 TranslucentBasePass_Shared_FogISR_DirectionalLightColor : packoffset(c150.x);
	float TranslucentBasePass_Shared_FogISR_MediumIOR : packoffset(c151.x);
	int TranslucentBasePass_Shared_FogISR_NumberOfFogContext : packoffset(c151.y);
	int PrePadding_TranslucentBasePass_Shared_FogISR_2424 : packoffset(c151.z);
	int PrePadding_TranslucentBasePass_Shared_FogISR_2428 : packoffset(c151.w);
	float4 TranslucentBasePass_Shared_FogISR_HeightBasedContextAlbedo[4] : packoffset(c152.x);
	float4 TranslucentBasePass_Shared_FogISR_HeightBasedContextDensity[4] : packoffset(c156.x);
	float4 TranslucentBasePass_Shared_FogISR_TransmitContextAlbedo[4] : packoffset(c160.x);
	float4 TranslucentBasePass_Shared_FogISR_TransmitContextDensity[4] : packoffset(c164.x);
	float4 TranslucentBasePass_Shared_FogISR_RegionContext[4] : packoffset(c168.x);
	float4 TranslucentBasePass_Shared_FogISR_LightContext[4] : packoffset(c172.x);
	float4 TranslucentBasePass_Shared_FogISR_EnvironmentTextureContext[4] : packoffset(c176.x);
	float4 TranslucentBasePass_Shared_FogISR_DistanceTextureContext[4] : packoffset(c180.x);
	float PrePadding_TranslucentBasePass_Shared_FogISR_2944 : packoffset(c184.x);
	float PrePadding_TranslucentBasePass_Shared_FogISR_2948 : packoffset(c184.y);
	float PrePadding_TranslucentBasePass_Shared_FogISR_2952 : packoffset(c184.z);
	float PrePadding_TranslucentBasePass_Shared_FogISR_2956 : packoffset(c184.w);
	float4 TranslucentBasePass_Shared_FogISR_FarIntegratedScatteringTextureContext : packoffset(c185.x);
	float PrePadding_TranslucentBasePass_Shared_FogISR_2976 : packoffset(c186.x);
	float PrePadding_TranslucentBasePass_Shared_FogISR_2980 : packoffset(c186.y);
	float PrePadding_TranslucentBasePass_Shared_FogISR_2984 : packoffset(c186.z);
	float PrePadding_TranslucentBasePass_Shared_FogISR_2988 : packoffset(c186.w);
	float4 TranslucentBasePass_Shared_FogISR_IntegratedScatteringVolumeTextureContext : packoffset(c187.x);
	float PrePadding_TranslucentBasePass_3008 : packoffset(c188.x);
	float PrePadding_TranslucentBasePass_3012 : packoffset(c188.y);
	float PrePadding_TranslucentBasePass_3016 : packoffset(c188.z);
	float PrePadding_TranslucentBasePass_3020 : packoffset(c188.w);
	float PrePadding_TranslucentBasePass_3024 : packoffset(c189.x);
	float PrePadding_TranslucentBasePass_3028 : packoffset(c189.y);
	float PrePadding_TranslucentBasePass_3032 : packoffset(c189.z);
	float PrePadding_TranslucentBasePass_3036 : packoffset(c189.w);
	float PrePadding_TranslucentBasePass_3040 : packoffset(c190.x);
	float PrePadding_TranslucentBasePass_3044 : packoffset(c190.y);
	float PrePadding_TranslucentBasePass_3048 : packoffset(c190.z);
	float PrePadding_TranslucentBasePass_3052 : packoffset(c190.w);
	float PrePadding_TranslucentBasePass_3056 : packoffset(c191.x);
	float PrePadding_TranslucentBasePass_3060 : packoffset(c191.y);
	float PrePadding_TranslucentBasePass_3064 : packoffset(c191.z);
	float PrePadding_TranslucentBasePass_3068 : packoffset(c191.w);
	float PrePadding_TranslucentBasePass_3072 : packoffset(c192.x);
	float PrePadding_TranslucentBasePass_3076 : packoffset(c192.y);
	float PrePadding_TranslucentBasePass_3080 : packoffset(c192.z);
	float PrePadding_TranslucentBasePass_3084 : packoffset(c192.w);
	float PrePadding_TranslucentBasePass_3088 : packoffset(c193.x);
	float PrePadding_TranslucentBasePass_3092 : packoffset(c193.y);
	float PrePadding_TranslucentBasePass_3096 : packoffset(c193.z);
	float PrePadding_TranslucentBasePass_3100 : packoffset(c193.w);
	float PrePadding_TranslucentBasePass_3104 : packoffset(c194.x);
	float PrePadding_TranslucentBasePass_3108 : packoffset(c194.y);
	float PrePadding_TranslucentBasePass_3112 : packoffset(c194.z);
	float PrePadding_TranslucentBasePass_3116 : packoffset(c194.w);
	float PrePadding_TranslucentBasePass_3120 : packoffset(c195.x);
	float PrePadding_TranslucentBasePass_3124 : packoffset(c195.y);
	float PrePadding_TranslucentBasePass_3128 : packoffset(c195.z);
	float PrePadding_TranslucentBasePass_3132 : packoffset(c195.w);
	float PrePadding_TranslucentBasePass_3136 : packoffset(c196.x);
	float PrePadding_TranslucentBasePass_3140 : packoffset(c196.y);
	float PrePadding_TranslucentBasePass_3144 : packoffset(c196.z);
	float PrePadding_TranslucentBasePass_3148 : packoffset(c196.w);
	float PrePadding_TranslucentBasePass_3152 : packoffset(c197.x);
	float PrePadding_TranslucentBasePass_3156 : packoffset(c197.y);
	float PrePadding_TranslucentBasePass_3160 : packoffset(c197.z);
	float PrePadding_TranslucentBasePass_3164 : packoffset(c197.w);
	float4 TranslucentBasePass_HZBUvFactorAndInvFactor : packoffset(c198.x);
	float4 TranslucentBasePass_PrevScreenPositionScaleBias : packoffset(c199.x);
	float4 TranslucentBasePass_PreviousViewRectangleContext : packoffset(c200.x);
	float TranslucentBasePass_PrevSceneColorPreExposureInv : packoffset(c201.x);
	float PrePadding_TranslucentBasePass_3220 : packoffset(c201.y);
	float PrePadding_TranslucentBasePass_3224 : packoffset(c201.z);
	float PrePadding_TranslucentBasePass_3228 : packoffset(c201.w);
	float PrePadding_TranslucentBasePass_3232 : packoffset(c202.x);
	float PrePadding_TranslucentBasePass_3236 : packoffset(c202.y);
	float PrePadding_TranslucentBasePass_3240 : packoffset(c202.z);
	float PrePadding_TranslucentBasePass_3244 : packoffset(c202.w);
	float PrePadding_TranslucentBasePass_3248 : packoffset(c203.x);
	float PrePadding_TranslucentBasePass_3252 : packoffset(c203.y);
	float PrePadding_TranslucentBasePass_3256 : packoffset(c203.z);
	float PrePadding_TranslucentBasePass_3260 : packoffset(c203.w);
	float4 TranslucentBasePass_HZBCoordinateContext : packoffset(c204.x);
};

cbuffer ReflectionCapture : register(b2, space0) 
{
	float4 ReflectionCapture_PositionAndRadius[341] : packoffset(c0.x);
	float4 ReflectionCapture_CaptureProperties[341] : packoffset(c341.x);
	float4 ReflectionCapture_CaptureOffsetAndAverageBrightness[341] : packoffset(c682.x);
	row_major float4x4 ReflectionCapture_BoxTransform[341] : packoffset(c1023.x);
	float4 ReflectionCapture_BoxScales[341] : packoffset(c2387.x);
};

cbuffer Material : register(b3, space0) 
{
	float4 Material_VectorExpressions[20] : packoffset(c0.x);
	float4 Material_ScalarExpressions[6] : packoffset(c20.x);
};

struct InputStruct
{
    float4 TEXCOORD10 : TEXCOORD10;
    float4 TEXCOORD11 : TEXCOORD11;

#if defined(GAME_VERSION_1_0_0_4)
    float2 TEXCOORD[4] : TEXCOORD0;
    nointerpolation uint PRIMITIVE_ID : PRIMITIVE_ID;
    nointerpolation uint COVERAGE_CONTEXT : COVERAGE_CONTEXT;
#else
    float4 TEXCOORD0 : TEXCOORD0;
    float4 TEXCOORD1 : TEXCOORD1;
    nointerpolation float3 CONSTANT_CONTEXT : CONSTANT_CONTEXT;
#endif

    linear noperspective float4 SV_Position : SV_Position;
    nointerpolation uint SV_IsFrontFace : SV_IsFrontFace;
};

struct OutputStruct
{
    float4 SV_Target0 : SV_Target0;
    float4 SV_Target1 : SV_Target1;
};

// -----------------------------------------------------------------------------
// Reconstructed source-level helpers
// -----------------------------------------------------------------------------

float4 GatherWaterShadowMapRed(int shadowMapIndex, float2 uv, int2 offset)
{
#if defined(GAME_VERSION_1_0_0_4)
    return TranslucentBasePass_Shared_Forward_DirectionalLightShadowmapAtlas.GatherRed(View_SharedPointClampedSampler, uv, offset);
#else
    return ShadowMaps[NonUniformResourceIndex(shadowMapIndex)].GatherRed(View_SharedPointClampedSampler, uv, offset);
#endif
}

void GetWaterShadowMapDimensions(int shadowMapIndex, out uint width, out uint height, out uint mipCount)
{
#if defined(GAME_VERSION_1_0_0_4)
    TranslucentBasePass_Shared_Forward_DirectionalLightShadowmapAtlas.GetDimensions(0, width, height, mipCount);
#else
    ShadowMaps[NonUniformResourceIndex(shadowMapIndex)].GetDimensions(0, width, height, mipCount);
#endif
}

float3 SafeNormalize(float3 value)
{
    return value * rsqrt(max(dot(value, value), 1.0e-12));
}

float DeviceZToWorldDepth(float deviceZ)
{
    float numerator = mad(View_InvDeviceZToWorldZTransform.x, deviceZ, View_InvDeviceZToWorldZTransform.y);
    float denominator = mad(View_InvDeviceZToWorldZTransform.z, deviceZ, -View_InvDeviceZToWorldZTransform.w);
    return numerator + rcp(denominator);
}

float3 ReconstructTranslatedWorldPosition(float2 screenPosition, float deviceZ)
{
    float4 worldH = mul(float4(screenPosition, deviceZ, 1.0), View_SVPositionToTranslatedWorld);
    return worldH.xyz / worldH.w;
}

float3 ReconstructTranslatedWorldFromNdc(float2 ndc, float worldDepth)
{
    float3 position =
        ndc.x * View_ScreenToTranslatedWorld[0].xyz +
        ndc.y * View_ScreenToTranslatedWorld[1].xyz +
        worldDepth * View_ScreenToTranslatedWorld[2].xyz +
        View_ScreenToTranslatedWorld[3].xyz;
    return position;
}

float4 TransformHomogeneous(float3 position, row_major float4x4 transform)
{
    return mul(float4(position, 1.0), transform);
}

struct HZBTraceResult
{
    float2 HZBUv;
    float DeviceZ;
    float Confidence;
};

struct SurfaceState
{
    float2 MaterialUV;
    float3 TranslatedWorldPosition;
    float3 WorldPosition;
    float3 CameraToPixelDirection;
    float3 Normal;
    float FilteredRoughness;
    float RoughnessSquared;
    float RoughnessCubed;
    float3 ReflectionVector;
    float3 ReflectionDirection;
    float3 TransmissionDirection;
};

HZBTraceResult TraceHierarchicalZ(
    float3 rayStartNdc,
    float3 rayEndNdc,
    float2 projectedLimitNdc,
    float projectedLimitDepth,
    float2 hzbUvScale,
    float randomOffset)
{
    HZBTraceResult result = (HZBTraceResult)0;

    float3 rayDelta = rayEndNdc - rayStartNdc;
    float2 projectedDelta = projectedLimitNdc - rayStartNdc.xy;
    float halfScreenLength = 0.5 * length(rayDelta.xy);

    float2 centeredRay = rayDelta.xy + halfScreenLength * rayStartNdc.xy;
    float2 outsideDistance = max(abs(centeredRay) - halfScreenLength, 0.0);
    float2 remainingFraction = 1.0 - outsideDistance / abs(rayDelta.xy);
    float clipScale = min(remainingFraction.x, remainingFraction.y) / halfScreenLength;
    float3 traceStep = clipScale * rayDelta;
    float thickness = max(abs(traceStep.z), rayStartNdc.z - projectedLimitDepth);

    uint hzbWidth, hzbHeight, hzbMipCount;
    TranslucentBasePass_HZBTexture.GetDimensions(0, hzbWidth, hzbHeight, hzbMipCount);

    float projectedLength = length(projectedDelta);
    float stepLength = max(length(traceStep.xy), 1.0e-4);
    uint traceStepCount = clamp((uint)ceil(saturate(projectedLength / stepLength) * 8.0 + randomOffset), 1u, 8u);

    float2 traceUv = float2(rayStartNdc.x * 0.5 + 0.5, 0.5 - rayStartNdc.y * 0.5) * hzbUvScale;
    float2 uvStep = float2(hzbUvScale.x * 0.0625 * traceStep.x, hzbUvScale.y * -0.0625 * traceStep.y);
    float depthStep = traceStep.z * 0.125;
    thickness *= 0.125;

    float previousDepthDelta = 0.0;

    [loop]
    for (uint stepIndex = 0u; stepIndex < traceStepCount; ++stepIndex)
    {
        float sampleTime = (float)stepIndex + randomOffset;
        float2 sampleUv = mad(sampleTime, uvStep, traceUv);
        float rayDepth = mad(sampleTime, depthStep, rayStartNdc.z);
        int2 texel = int2(floor(sampleUv * float2(hzbWidth, hzbHeight)));
        float sceneDepth = TranslucentBasePass_HZBTexture.Load(int3(texel, 0)).x;
        float depthDelta = rayDepth - sceneDepth;
        bool crossedSurface = abs(-thickness - depthDelta) < thickness;

        if (crossedSurface && sceneDepth != 0.0)
        {
            float previous = stepIndex == 0u ? depthDelta - depthStep : previousDepthDelta;
            float interpolation = saturate(previous / (previous - depthDelta));
            float hitTime = abs(randomOffset - 1.0 + (float)stepIndex + interpolation);
            result.HZBUv = mad(hitTime, uvStep, traceUv);
            result.DeviceZ = mad(hitTime, depthStep, rayStartNdc.z);
            float edgeFade = min(hitTime * 0.125, 1.0);
            result.Confidence = 1.0 - edgeFade * edgeFade;
            return result;
        }

        previousDepthDelta = depthDelta;
    }

    return result;
}

float HashWrappedGridPoint(int2 gridPoint)
{
    float2 wrappedPoint = float2(gridPoint & 255);
    return frac(sin(dot(wrappedPoint, float2(127.1, 311.7))) * 43758.5);
}

float WrappedValueNoise2D(float2 position)
{
    ///*
    position += View_RealTime * 2.0;
    int2 cell = int2(floor(position));
    float2 blend = saturate(frac(position));
    blend = blend * blend * (3.0 - 2.0 * blend); // cubic Hermite curve

    float bottom = lerp(HashWrappedGridPoint(cell), HashWrappedGridPoint(cell + int2(1, 0)), blend.x);
    float top = lerp(HashWrappedGridPoint(cell + int2(0, 1)), HashWrappedGridPoint(cell + int2(1, 1)), blend.x);
    return lerp(bottom, top, blend.y);
    //*/

    //return View_PerlinNoiseVolumeTexture.SampleLevel(View_SharedBilinearWrappedSampler, float3(position * 0.025f, 0), 0).r;
    //return View_SpatiotemporalBlueNoiseVolumeTexture.SampleLevel(View_SharedBilinearWrappedSampler, float3(position * 0.1f, 0), 0).r;
}

// Cheap, deterministic 2-D hash used by the procedural Worley cells. This
// avoids a texture dependency (and avoids the relatively expensive sin hash).
float2 HashWorleyCell(int2 cell)
{
    float3 p = frac(float3((float)cell.x, (float)cell.y, (float)cell.x) * float3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yzx + 33.33);
    return frac((p.xx + p.yz) * p.zy);
}

// Returns distance to the nearest Voronoi boundary (F2 - F1). Keeping the
// distance rather than thresholding here lets the caller use fwidth for stable,
// antialiased caustic lines.
float WorleyEdgeDistance(float2 position, float animationTime)
{
    int2 baseCell = int2(floor(position));
    float2 localPosition = frac(position);
    float nearestDistanceSq = 1.0e10;
    float secondDistanceSq = 1.0e10;

    [unroll]
    for (int cellY = -1; cellY <= 1; ++cellY)
    {
        [unroll]
        for (int cellX = -1; cellX <= 1; ++cellX)
        {
            int2 neighborOffset = int2(cellX, cellY);
            int2 neighborCell = baseCell + neighborOffset;
            float2 randomPhase = HashWorleyCell(neighborCell) * 6.28318530718;

            float2 featurePoint = 0.5 + 0.42 * sin(randomPhase + animationTime * float2(1.0, 1.173));
            float2 delta = float2(neighborOffset) + featurePoint - localPosition;
            float distanceSq = dot(delta, delta);

            if (distanceSq < nearestDistanceSq)
            {
                secondDistanceSq = nearestDistanceSq;
                nearestDistanceSq = distanceSq;
            }
            else
            {
                secondDistanceSq = min(secondDistanceSq, distanceSq);
            }
        }
    }

    return sqrt(secondDistanceSq) - sqrt(nearestDistanceSq);
}

float2 RotateDensityOctave(float2 value)
{
    return float2(mad(value.y,  0.809017, value.x * -0.587785), mad(value.y, -0.587785, value.x * -0.809017));
}

float Luminance(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

float EvaluateAmbientDice(float3 direction, float3 positiveAxisWeights, float3 negativeAxisWeights)
{
    float3 positiveLobes = saturate(direction);
    float3 negativeLobes = saturate(-direction);
    return dot(positiveAxisWeights, positiveLobes * positiveLobes) + dot(negativeAxisWeights, negativeLobes * negativeLobes);
}

struct TranslucencyAmbientDice
{
    float3 BaseColor;
    float3 PositiveAxisWeights;
    float3 NegativeAxisWeights;
};

TranslucencyAmbientDice SampleTranslucencyAmbientDice(float3 worldPosition)
{
    float3 volumeUv = (worldPosition - View_TranslucencyLightingVolumeMin[1].xyz) * View_TranslucencyLightingVolumeInvSize[1].xyz;

    TranslucencyAmbientDice ambient;
    ambient.BaseColor = TranslucentBasePass_TranslucencyLightingVolumeEnvironmentA.SampleLevel(View_SharedBilinearClampedSampler, volumeUv, 0.0).rgb;
    ambient.PositiveAxisWeights = TranslucentBasePass_TranslucencyLightingVolumeEnvironmentB.SampleLevel(View_SharedBilinearClampedSampler, volumeUv, 0.0).rgb;
    ambient.NegativeAxisWeights = TranslucentBasePass_TranslucencyLightingVolumeEnvironmentC.SampleLevel(View_SharedBilinearClampedSampler, volumeUv, 0.0).rgb;
    return ambient;
}

// -----------------------------------------------------------------------------
// Directional-light shadowing
// -----------------------------------------------------------------------------

float SignedUnit(float value)
{
    return (float)((int)(value > 0.0f) - (int)(value < 0.0f));
}

float2 SafeNormalizeShadowDirection(float2 value)
{
    float lengthSquared = dot(value, value);
    return lengthSquared < 1.0e-24f ? 0.0f : value * rsqrt(lengthSquared);
}

// Treat (shadow U, shadow V, receiver depth) as a screen-space plane. The
// original shader uses this normal to orient its stochastic PCF footprint.
float3 ComputeShadowReceiverPlaneNormal(float2 shadowUv, float receiverDepth)
{
    float3 shadowDx = float3(ddx_fine(shadowUv), ddx_fine(receiverDepth));
    float3 shadowDy = float3(ddy_fine(shadowUv), ddy_fine(receiverDepth));
    return cross(shadowDx, shadowDy);
}

float4 CompareGatheredShadowDepths(float4 shadowDepths, float receiverDepth)
{
    const float depthStep = 1.0f / 65535.0f;
    float4 visibility = saturate((shadowDepths - receiverDepth + depthStep) * 65535.0f + 1.0f);

    // Depths near one represent unwritten texels in this shadow-map format.
    return float4(
        shadowDepths.x > 0.99f ? 1.0f : visibility.x,
        shadowDepths.y > 0.99f ? 1.0f : visibility.y,
        shadowDepths.z > 0.99f ? 1.0f : visibility.z,
        shadowDepths.w > 0.99f ? 1.0f : visibility.w);
}

// Unreal's gathered 3x3 PCF reconstruction. Four GatherRed calls provide the
// overlapping 4x4 footprint, and the edge samples are weighted by the
// fractional location inside the current shadow texel.
float FilterGatheredShadow3x3(
    float2 texelFraction,
    float4 upperLeft,
    float4 upperRight,
    float4 lowerLeft,
    float4 lowerRight)
{
    float4 horizontalRows;
    horizontalRows.x = upperLeft.w * (1.0f - texelFraction.x) + upperLeft.z + upperRight.w + upperRight.z * texelFraction.x;
    horizontalRows.y = upperLeft.x * (1.0f - texelFraction.x) + upperLeft.y + upperRight.x + upperRight.y * texelFraction.x;
    horizontalRows.z = lowerLeft.w * (1.0f - texelFraction.x) + lowerLeft.z + lowerRight.w + lowerRight.z * texelFraction.x;
    horizontalRows.w = lowerLeft.x * (1.0f - texelFraction.x) + lowerLeft.y + lowerRight.x + lowerRight.y * texelFraction.x;

    return dot(horizontalRows, float4(1.0f - texelFraction.y, 1.0f, 1.0f, texelFraction.y)) * (1.0f / 9.0f);
}

float EvaluateDynamicDirectionalShadow(
    int cascadeIndex,
    float2 shadowUv,
    float receiverDepth,
    float4 blueNoise)
{
    float4 atlasSize = TranslucentBasePass_Shared_Forward_DirectionalLightShadowmapAtlasBufferSize;
    float3 receiverPlaneNormal = ComputeShadowReceiverPlaneNormal(shadowUv, receiverDepth);
    float2 filterDirection = SafeNormalizeShadowDirection(-receiverPlaneNormal.xy);

    float centeredNoiseY = blueNoise.y - 0.5f;
    float shiftedNoiseX = blueNoise.x + 0.5f;
    float2 jitteredTexel = shadowUv * atlasSize.xy + float2(
        filterDirection.y * centeredNoiseY + filterDirection.x * shiftedNoiseX,
        filterDirection.x * centeredNoiseY + filterDirection.y * shiftedNoiseX);

    float2 continuousTexel = jitteredTexel - 0.5f;
    float2 baseTexel = trunc(continuousTexel);
    float2 texelFraction = continuousTexel - baseTexel;
    float2 gatherUv = baseTexel * atlasSize.zw;

    float4 upperLeft = CompareGatheredShadowDepths(GatherWaterShadowMapRed(cascadeIndex, gatherUv, int2(0, 0)), receiverDepth);
    float4 upperRight = CompareGatheredShadowDepths(GatherWaterShadowMapRed(cascadeIndex, gatherUv, int2(2, 0)), receiverDepth);
    float4 lowerLeft = CompareGatheredShadowDepths(GatherWaterShadowMapRed(cascadeIndex, gatherUv, int2(0, 2)), receiverDepth);
    float4 lowerRight = CompareGatheredShadowDepths(GatherWaterShadowMapRed(cascadeIndex, gatherUv, int2(2, 2)), receiverDepth);

    return FilterGatheredShadow3x3(texelFraction, upperLeft, upperRight, lowerLeft, lowerRight);
}

float EvaluateStaticDirectionalShadow(float3 worldPosition, float4 blueNoise)
{
    if (TranslucentBasePass_Shared_Forward_DirectionalLightUseStaticShadowing == 0)
        return 1.0f;

    float4 shadowPosition = TransformHomogeneous(worldPosition, TranslucentBasePass_Shared_Forward_DirectionalLightWorldToStaticShadow);
    float2 shadowUv = shadowPosition.xy / shadowPosition.w;
    float receiverDepth = shadowPosition.z;

    int2 baseTile = int2((int)floor(shadowUv.x), (int)-floor(shadowUv.y));
    int4 tileRange = TranslucentBasePass_Shared_Forward_DirectionalLightStaticShadowDepthTileRange;
    bool baseTileInRange = all(baseTile >= tileRange.xy) && all(baseTile < tileRange.zw);

    if (!baseTileInRange)
        return 1.0f;

    float3 receiverPlaneNormal = ComputeShadowReceiverPlaneNormal(shadowUv, receiverDepth);
    float inverseJacobian = rcp(max(abs(receiverPlaneNormal.z), length(receiverPlaneNormal) * 0.1f));
    float planeOrientation = SignedUnit(receiverPlaneNormal.z);
    float2 filterDirection = SafeNormalizeShadowDirection(-receiverPlaneNormal.xy);

    float centeredNoiseY = blueNoise.y - 0.5f;
    float2 stochasticOffset = float2(
        (filterDirection.y * centeredNoiseY + filterDirection.x * blueNoise.x) * 2.0f + filterDirection.x * 0.5f,
        (filterDirection.y * blueNoise.x + filterDirection.x * centeredNoiseY) * 2.0f + filterDirection.y * 0.5f);

    float2 subTilePosition = stochasticOffset + frac(shadowUv) * 256.0f;
    float2 subTileCenterDelta = floor(subTilePosition) + 0.5f - subTilePosition;
    float2 receiverDepthPerSubTexel = receiverPlaneNormal.xy * (256.0f * inverseJacobian * planeOrientation);
    float receiverPlaneOffset = dot(subTileCenterDelta, receiverDepthPerSubTexel);

    float2 samplePosition = shadowUv + stochasticOffset * (1.0f / 256.0f);
    int2 sampleTile = int2((int)floor(samplePosition.x), (int)-floor(samplePosition.y));
    float2 sampleFraction = frac(samplePosition);

    int tileGridWidth = tileRange.z - tileRange.x;
    uint relativeTileX = (uint)sampleTile.x - (uint)tileRange.x;
    uint relativeTileY = (uint)sampleTile.y - (uint)tileRange.y;
    uint linearTileIndex = relativeTileY * (uint)tileGridWidth + relativeTileX;

    uint loadedWord = TranslucentBasePass_Shared_Forward_DirectionalLightStaticShadowLoadedTileData.Load(linearTileIndex >> 5);
    uint loadedBit = 1u << (linearTileIndex & 31u);

    if ((loadedWord & loadedBit) == 0u)
        return 1.0f;

    int physicalTileIndex = TranslucentBasePass_Shared_Forward_DirectionalLightStaticShadowDepthTileIndices.Load(linearTileIndex);

    if (physicalTileIndex < 0)
        return 1.0f;

    uint texelX = (uint)floor(sampleFraction.x * 256.0f);
    uint texelY = (uint)floor(sampleFraction.y * 256.0f);
    uint packedTexelAddress = ((uint)physicalTileIndex << 16) + (texelY << 8) + texelX;
    uint packedDepthPair = TranslucentBasePass_Shared_Forward_DirectionalLightStaticShadowDepthTileData.Load(packedTexelAddress >> 1);
    uint encodedDepth = ((packedTexelAddress & 1u) == 0u ? packedDepthPair : (packedDepthPair >> 16)) & 65535u;
    float normalizedDepth = (float)encodedDepth * (1.0f / 65535.0f);

    if (normalizedDepth == 1.0f)
        return 1.0f;

    float2 depthRange = TranslucentBasePass_Shared_Forward_DirectionalLightStaticShadowDepthRanges.Load(physicalTileIndex);
    float decodedShadowDepth = depthRange.x + depthRange.y * normalizedDepth;
    float inverseDepthRange = rcp(depthRange.y);
    float receiverPlaneCorrection = min(abs(receiverPlaneOffset), inverseDepthRange * 10.0f) * SignedUnit(receiverPlaneOffset);

    const float depthStep = 1.0f / 65535.0f;
    return saturate((decodedShadowDepth - receiverDepth + depthStep + receiverPlaneCorrection) * 65535.0f + 1.0f);
}

float EvaluateDirectionalLightVisibility(
    SurfaceState surface,
    float sceneDepth,
    float4 blueNoise)
{
    int cascadeCount = TranslucentBasePass_Shared_Forward_NumDirectionalLightCascades;
    int cascadeIndex = cascadeCount;

    [loop]
    for (int candidateCascade = 0; candidateCascade < cascadeCount; ++candidateCascade)
    {
        if (sceneDepth < TranslucentBasePass_Shared_Forward_CascadeEndDepths[candidateCascade])
        {
            cascadeIndex = candidateCascade;
            break;
        }
    }

    if (cascadeIndex < cascadeCount)
    {
        float4 shadowPosition = TransformHomogeneous(surface.WorldPosition, TranslucentBasePass_Shared_Forward_DirectionalLightWorldToShadowMatrix[cascadeIndex]);
        float2 shadowUv = shadowPosition.xy / shadowPosition.w;
        float4 uvBounds = TranslucentBasePass_Shared_Forward_DirectionalLightShadowmapMinMax[cascadeIndex];
        bool insideCascade = all(shadowUv >= uvBounds.xy) && all(shadowUv <= uvBounds.zw);

        if (insideCascade)
            return EvaluateDynamicDirectionalShadow(cascadeIndex, shadowUv, shadowPosition.z, blueNoise);
    }

    return EvaluateStaticDirectionalShadow(surface.WorldPosition, blueNoise);
}

// -----------------------------------------------------------------------------
// Forward lighting
// -----------------------------------------------------------------------------

struct LightingComponents
{
    float3 Diffuse;
    float3 Transmission;
    float3 Subsurface;
    float3 Specular;
};

struct DirectLightingResult
{
    LightingComponents Primary;
    float3 SecondaryDiffuse;
    float3 SecondarySpecular;
};

DirectLightingResult EvaluateClusteredLocalLighting(
    SurfaceState surface,
    int clusterCellIndex,
    float normalViewDot,
    float absNormalViewDot,
    float clampedNormalViewDot,
    float minimumRoughness,
    float dielectricF0,
    float3 reflectionBasis,
    float4 blueNoiseSample,
    LightingComponents directionalLighting)
{
    int surfaceIndex22 = clusterCellIndex;
    float surfaceAbsoluteValue = absNormalViewDot;
    float surfaceTerm19 = clampedNormalViewDot;
    float surfaceTerm85 = minimumRoughness;
    float3 directionalDiffuse = directionalLighting.Diffuse;
    float3 directionalTransmission = directionalLighting.Transmission;
    float3 directionalSubsurface = directionalLighting.Subsurface;
    float3 directionalSpecular = directionalLighting.Specular;

    bool evaluateDetailedLocalLight = false;
    float3 localDiffuse;
    float3 localTransmission;
    float3 localSubsurface;
    float3 localSpecular;
    float3 localDiffuseSecondary;
    float3 localSpecularSecondary;
    int localLightLoopIndex;
    uint4 lightTerm0;
    float lightTerm1;
    float lightTerm2;
    float localShadowVisibility;
    float3 lightVector;
    float lightTerm3;
    float3 shadowedLightColor;
    float lightTerm4;
    float lightTerm5;
    float lightTerm6;
    float lightTerm7;
    float lightTerm8;
    float lightTerm9;
    float lightTerm10;
    float lightTerm11;
    float lightTerm12;
    float lightTerm13;
    float lightTerm14;
    float lightTerm15;
    float lightTerm16;
    float lightTerm17;
    float3 normalizedLightVector;
    float3 evaluatedDiffuse;
    float3 evaluatedTransmission;
    float3 evaluatedSubsurface;
    float3 evaluatedSpecular;
    float3 simpleLightColor;
    float3 simpleLightVector;
    float3 simpleDiffuse;
    float3 simpleTransmission;
    float3 simpleSpecular;
    float lightTerm18;
    float lightTerm19;
    float lightTerm20;
    float3 nextLocalDiffuse;
    float3 nextLocalSpecular;
    float3 totalDiffuse;
    float3 totalTransmission;
    float3 totalSubsurface;
    float3 totalSpecular;
    float3 totalDiffuseSecondary;
    float3 totalSpecularSecondary;
    float3 directDiffuseLighting;
    float3 directTransmissionLighting;
    float3 directSubsurfaceLighting;
    float3 directSpecularLighting;
    float3 secondaryDiffuseLighting;
    float3 secondarySpecularLighting;

    int lightIndex0 = surfaceIndex22 << 1;
    int4 lightTerm21 = float4(TranslucentBasePass_Shared_Forward_NumLocalLights, TranslucentBasePass_Shared_Forward_NumReflectionCaptures, TranslucentBasePass_Shared_Forward_HasDirectionalLight, TranslucentBasePass_Shared_Forward_NumGridCells);
    int4 lightSample0 = TranslucentBasePass_Shared_Forward_NumCulledLightsGrid.Load(lightIndex0);
    int lightTerm22 = (int)min((uint)(lightSample0.x), (uint)(lightTerm21.x));
    int lightIndex1 = lightIndex0 | 1;
    int4 lightSample1 = TranslucentBasePass_Shared_Forward_NumCulledLightsGrid.Load(lightIndex1);
    int4 lightTerm23 = float4(TranslucentBasePass_Shared_Forward_NumLocalLights, TranslucentBasePass_Shared_Forward_NumReflectionCaptures, TranslucentBasePass_Shared_Forward_HasDirectionalLight, TranslucentBasePass_Shared_Forward_NumGridCells);
    int lightTerm24 = (int)min((uint)(lightTerm22), (uint)(lightTerm23.x));
    bool lightTest0 = (lightTerm24 == 0);

    if (lightTest0)
    {
        directDiffuseLighting = directionalDiffuse;
        directTransmissionLighting = directionalTransmission;
        directSubsurfaceLighting = directionalSubsurface;
        directSpecularLighting = directionalSpecular;
        secondaryDiffuseLighting = float3(0.0, 0.0, 0.0);
        secondarySpecularLighting = float3(0.0, 0.0, 0.0);
    }
    else
    {
        localDiffuse = directionalDiffuse;
        localTransmission = directionalTransmission;
        localSubsurface = directionalSubsurface;
        localSpecular = directionalSpecular;
        localDiffuseSecondary = float3(0.0, 0.0, 0.0);
        localSpecularSecondary = float3(0.0, 0.0, 0.0);
        localLightLoopIndex = 0;

        // Clustered forward local-light accumulation
        while (true)
        {
            int lightIndex2 = localLightLoopIndex + lightSample1.x;
            int4 localLightCulledLightDataGridLoadResult = TranslucentBasePass_Shared_Forward_CulledLightDataGrid.Load(lightIndex2);
            int lightIndex3 = localLightCulledLightDataGridLoadResult.x * 12;
            float4 lightSample2 = TranslucentBasePass_Shared_Forward_ForwardLocalLightBuffer.Load(lightIndex3);
            int lightIndex4 = lightIndex3 | 1;
            float4 lightSample3 = TranslucentBasePass_Shared_Forward_ForwardLocalLightBuffer.Load(lightIndex4);
            int lightIndex5 = lightIndex3 | 2;
            float4 lightSample4 = TranslucentBasePass_Shared_Forward_ForwardLocalLightBuffer.Load(lightIndex5);
            int lightIndex6 = asint((lightSample4[3]));
            int lightIndex7 = lightIndex3 | 3;
            float4 lightSample5 = TranslucentBasePass_Shared_Forward_ForwardLocalLightBuffer.Load(lightIndex7);
            int lightIndex8 = lightIndex3 + 4;
            float4 lightSample6 = TranslucentBasePass_Shared_Forward_ForwardLocalLightBuffer.Load(lightIndex8);
            int lightIndex9 = lightIndex3 + 5;
            float4 lightSample7 = TranslucentBasePass_Shared_Forward_ForwardLocalLightBuffer.Load(lightIndex9);
            int lightIndex10 = asint((lightSample7[2]));
            int lightIndex11 = lightIndex3 + 6;
            float4 lightSample8 = TranslucentBasePass_Shared_Forward_ForwardLocalLightBuffer.Load(lightIndex11);
            int lightIndex12 = lightIndex3 + 7;
            float4 lightSample9 = TranslucentBasePass_Shared_Forward_ForwardLocalLightBuffer.Load(lightIndex12);
            int lightIndex13 = asint((lightSample9[0]));
            int lightIndex14 = lightIndex3 + 8;
            float4 lightSample10 = TranslucentBasePass_Shared_Forward_ForwardLocalLightBuffer.Load(lightIndex14);
            int lightIndex15 = lightIndex3 + 9;
            float4 lightSample11 = TranslucentBasePass_Shared_Forward_ForwardLocalLightBuffer.Load(lightIndex15);
            int lightIndex16 = lightIndex3 + 10;
            float4 lightSample12 = TranslucentBasePass_Shared_Forward_ForwardLocalLightBuffer.Load(lightIndex16);
            int lightIndex17 = lightIndex3 + 11;
            float4 lightSample13 = TranslucentBasePass_Shared_Forward_ForwardLocalLightBuffer.Load(lightIndex17);
            int lightIndex18 = lightIndex6 & 131072;
            bool lightTest1 = (lightIndex18 != 0);
            bool lightTest2 = (lightSample5.x > -2.00000);
            int lightIndex19 = lightIndex6 & 16384;
            bool lightTest3 = (lightIndex19 == 0);

            if (lightTest3)
            {
                if (lightTest1)
                {
                    int lightIndex20 = lightIndex13 + 0;
                    GetWaterShadowMapDimensions(lightIndex20, lightTerm0.x, lightTerm0.y, lightTerm0.z);
                    float lightTerm25 = (float)((int)(lightTerm0.x));
                    float lightTerm26 = (float)((int)(lightTerm0.y));
                    float lightTerm27 = 1.00000 / lightTerm25;
                    float lightTerm28 = 1.00000 / lightTerm26;
                    float lightTerm29 = (mad(surface.WorldPosition.z, lightSample12.z, (mad(surface.WorldPosition.y, lightSample11.z, (lightSample10.z * surface.WorldPosition.x))))) + lightSample13.z;
                    float lightTerm30 = (mad(surface.WorldPosition.z, lightSample12.w, (mad(surface.WorldPosition.y, lightSample11.w, (lightSample10.w * surface.WorldPosition.x))))) + lightSample13.w;
                    float lightTerm31 = ((mad(surface.WorldPosition.z, lightSample12.x, (mad(surface.WorldPosition.y, lightSample11.x, (lightSample10.x * surface.WorldPosition.x))))) + lightSample13.x) / lightTerm30;
                    float lightTerm32 = ((mad(surface.WorldPosition.z, lightSample12.y, (mad(surface.WorldPosition.y, lightSample11.y, (lightSample10.y * surface.WorldPosition.x))))) + lightSample13.y) / lightTerm30;
                    bool lightTest4 = (lightTerm31 >= lightSample8.x);
                    bool lightTest5 = (lightTerm32 >= lightSample8.y);
                    bool lightTest6 = (lightTerm31 <= lightSample8.z);
                    bool lightTest7 = (lightTerm32 <= lightSample8.w);
                    bool lightTest8 = lightTest4 & lightTest5;
                    bool lightTest9 = lightTest6 & lightTest8;
                    bool lightTest10 = lightTest7 & lightTest9;

                    if (lightTest10)
                    {
                        float lightTerm33 = ddy_fine(lightTerm31);
                        float lightTerm34 = ddy_fine(lightTerm32);
                        float lightTerm35 = ddy_fine(lightTerm29);
                        float lightTerm36 = ddx_fine(lightTerm31);
                        float lightTerm37 = ddx_fine(lightTerm32);
                        float lightTerm38 = ddx_fine(lightTerm29);
                        float lightTerm39 = (lightTerm37 * lightTerm35) - (lightTerm38 * lightTerm34);
                        float lightTerm40 = (lightTerm38 * lightTerm33) - (lightTerm36 * lightTerm35);
                        float lightTerm41 = (lightTerm36 * lightTerm34) - (lightTerm37 * lightTerm33);
                        float lightTerm42 = 1.00000 / (max((abs(lightTerm41)), ((sqrt((((lightTerm40 * lightTerm40) + (lightTerm41 * lightTerm41)) + (lightTerm39 * lightTerm39)))) * 0.100000)));
                        bool lightTest11 = (lightTerm41 > 0.000000);
                        bool lightTest12 = (lightTerm41 < 0.000000);
                        int lightIndex21 = (int)(lightTest11);
                        int lightIndex22 = (int)(lightTest12);
                        int lightIndex23 = lightIndex21 - lightIndex22;
                        float lightTerm43 = (float)(lightIndex23);
                        float lightTerm44 = lightTerm39 * lightTerm27;
                        float lightTerm45 = (lightTerm44 * lightTerm42) * lightTerm43;
                        float lightTerm46 = lightTerm40 * lightTerm28;
                        float lightTerm47 = (lightTerm46 * lightTerm42) * lightTerm43;
                        float lightTerm48 = dot(float2(lightTerm39, lightTerm40), float2(lightTerm39, lightTerm40));
                        bool lightTest13 = (lightTerm48 < 1.00000e-24);

                        if (lightTest13)
                        {
                            lightTerm1 = 0.000000;
                            lightTerm2 = 0.000000;
                        }
                        else
                        {
                            float lightTerm49 = -0.000000 - lightTerm39;
                            float lightTerm50 = -0.000000 - lightTerm40;
                            float lightTerm51 = dot(float2(lightTerm49, lightTerm50), float2(lightTerm49, lightTerm50));
                            float lightTerm52 = rsqrt(lightTerm51);
                            lightTerm1 = (lightTerm52 * lightTerm49);
                            lightTerm2 = (lightTerm52 * lightTerm50);
                        }

                        float lightTerm53 = blueNoiseSample.y + -0.500000;
                        float lightTerm54 = blueNoiseSample.x + 0.500000;
                        float lightTerm55 = ((lightTerm2 * lightTerm53) + (lightTerm31 * lightTerm25)) + (lightTerm1 * lightTerm54);
                        float lightTerm56 = ((lightTerm1 * lightTerm53) + (lightTerm32 * lightTerm26)) + (lightTerm2 * lightTerm54);
                        float lightTerm57 = lightTerm55 + -0.500000;
                        float lightTerm58 = lightTerm56 + -0.500000;
                        float lightTerm59 = trunc(lightTerm57);
                        float lightTerm60 = trunc(lightTerm58);
                        float lightTerm61 = lightTerm57 - lightTerm59;
                        float lightTerm62 = lightTerm58 - lightTerm60;
                        float lightTerm63 = lightTerm59 * lightTerm27;
                        float lightTerm64 = lightTerm60 * lightTerm28;
                        float lightTerm65 = (lightTerm59 + -0.500000) - lightTerm55;
                        float lightTerm66 = (lightTerm60 + -0.500000) - lightTerm56;
                        float lightTerm67 = dot(float2(lightTerm65, lightTerm66), float2(lightTerm45, lightTerm47));
                        float lightTerm68 = lightTerm67 + lightTerm47;
                        float lightTerm69 = lightTerm43 * (lightTerm42 * (lightTerm46 + lightTerm44));
                        float lightTerm70 = lightTerm69 + lightTerm67;
                        float lightTerm71 = lightTerm67 + lightTerm45;
                        float4 lightSample14 = GatherWaterShadowMapRed(lightIndex20, float2(lightTerm63, lightTerm64), int2(0, 0));
                        float lightTerm72 = abs(lightTerm68);
                        float lightTerm73 = abs(lightTerm70);
                        float lightTerm74 = abs(lightTerm71);
                        float lightTerm75 = abs(lightTerm67);
                        float lightTerm76 = min(lightTerm72, 0.000000);
                        float lightTerm77 = min(lightTerm73, 0.000000);
                        float lightTerm78 = min(lightTerm74, 0.000000);
                        float lightTerm79 = min(lightTerm75, 0.000000);
                        bool lightTest14 = (lightTerm68 > 0.000000);
                        bool lightTest15 = (lightTerm70 > 0.000000);
                        bool lightTest16 = (lightTerm71 > 0.000000);
                        bool lightTest17 = (lightTerm67 > 0.000000);
                        bool lightTest18 = (lightTerm68 < 0.000000);
                        bool lightTest19 = (lightTerm70 < 0.000000);
                        bool lightTest20 = (lightTerm71 < 0.000000);
                        bool lightTest21 = (lightTerm67 < 0.000000);
                        int lightIndex24 = (int)(lightTest14);
                        int lightIndex25 = (int)(lightTest15);
                        int lightIndex26 = (int)(lightTest16);
                        int lightIndex27 = (int)(lightTest17);
                        int lightIndex28 = (int)(lightTest18);
                        int lightIndex29 = (int)(lightTest19);
                        int lightIndex30 = (int)(lightTest20);
                        int lightIndex31 = (int)(lightTest21);
                        int lightIndex32 = lightIndex24 - lightIndex28;
                        int lightIndex33 = lightIndex25 - lightIndex29;
                        int lightIndex34 = lightIndex26 - lightIndex30;
                        int lightIndex35 = lightIndex27 - lightIndex31;
                        float lightTerm80 = (float)(lightIndex32);
                        float lightTerm81 = (float)(lightIndex33);
                        float lightTerm82 = (float)(lightIndex34);
                        float lightTerm83 = (float)(lightIndex35);
                        float lightTerm84 = lightTerm76 * lightTerm80;
                        float lightTerm85 = lightTerm77 * lightTerm81;
                        float lightTerm86 = lightTerm78 * lightTerm82;
                        float lightTerm87 = lightTerm79 * lightTerm83;
                        float lightTerm88 = 1.52590e-05 - lightTerm29;
                        float lightTerm89 = lightTerm88 + lightSample14.x;
                        float lightTerm90 = lightTerm89 + lightTerm84;
                        float lightTerm91 = lightTerm88 + lightSample14.y;
                        float lightTerm92 = lightTerm91 + lightTerm85;
                        float lightTerm93 = lightTerm88 + lightSample14.z;
                        float lightTerm94 = lightTerm93 + lightTerm86;
                        float lightTerm95 = lightTerm88 + lightSample14.w;
                        float lightTerm96 = lightTerm95 + lightTerm87;
                        float lightTerm97 = lightTerm90 * 65535.0;
                        float lightTerm98 = lightTerm92 * 65535.0;
                        float lightTerm99 = lightTerm94 * 65535.0;
                        float lightTerm100 = lightTerm96 * 65535.0;
                        float lightTerm101 = lightTerm97 + 1.00000;
                        float lightTerm102 = lightTerm98 + 1.00000;
                        float lightTerm103 = lightTerm99 + 1.00000;
                        float lightTerm104 = lightTerm100 + 1.00000;
                        float lightTerm105 = saturate(lightTerm101);
                        float lightTerm106 = saturate(lightTerm102);
                        float lightTerm107 = saturate(lightTerm103);
                        float lightTerm108 = saturate(lightTerm104);
                        float lightTerm109 = (lightTerm59 + 1.50000) - lightTerm55;
                        float lightTerm110 = dot(float2(lightTerm109, lightTerm66), float2(lightTerm45, lightTerm47));
                        float lightTerm111 = lightTerm110 + lightTerm47;
                        float lightTerm112 = lightTerm69 + lightTerm110;
                        float lightTerm113 = lightTerm110 + lightTerm45;
                        float4 lightSample15 = GatherWaterShadowMapRed(lightIndex20, float2(lightTerm63, lightTerm64), int2(2, 0));
                        float lightTerm114 = abs(lightTerm111);
                        float lightTerm115 = abs(lightTerm112);
                        float lightTerm116 = abs(lightTerm113);
                        float lightTerm117 = abs(lightTerm110);
                        float lightTerm118 = min(lightTerm114, 0.000000);
                        float lightTerm119 = min(lightTerm115, 0.000000);
                        float lightTerm120 = min(lightTerm116, 0.000000);
                        float lightTerm121 = min(lightTerm117, 0.000000);
                        bool lightTest22 = (lightTerm111 > 0.000000);
                        bool lightTest23 = (lightTerm112 > 0.000000);
                        bool lightTest24 = (lightTerm113 > 0.000000);
                        bool lightTest25 = (lightTerm110 > 0.000000);
                        bool lightTest26 = (lightTerm111 < 0.000000);
                        bool lightTest27 = (lightTerm112 < 0.000000);
                        bool lightTest28 = (lightTerm113 < 0.000000);
                        bool lightTest29 = (lightTerm110 < 0.000000);
                        int lightIndex36 = (int)(lightTest22);
                        int lightIndex37 = (int)(lightTest23);
                        int lightIndex38 = (int)(lightTest24);
                        int lightIndex39 = (int)(lightTest25);
                        int lightIndex40 = (int)(lightTest26);
                        int lightIndex41 = (int)(lightTest27);
                        int lightIndex42 = (int)(lightTest28);
                        int lightIndex43 = (int)(lightTest29);
                        int lightIndex44 = lightIndex36 - lightIndex40;
                        int lightIndex45 = lightIndex37 - lightIndex41;
                        int lightIndex46 = lightIndex38 - lightIndex42;
                        int lightIndex47 = lightIndex39 - lightIndex43;
                        float lightTerm122 = (float)(lightIndex44);
                        float lightTerm123 = (float)(lightIndex45);
                        float lightTerm124 = (float)(lightIndex46);
                        float lightTerm125 = (float)(lightIndex47);
                        float lightTerm126 = lightTerm118 * lightTerm122;
                        float lightTerm127 = lightTerm119 * lightTerm123;
                        float lightTerm128 = lightTerm120 * lightTerm124;
                        float lightTerm129 = lightTerm121 * lightTerm125;
                        float lightTerm130 = lightTerm88 + lightSample15.x;
                        float lightTerm131 = lightTerm130 + lightTerm126;
                        float lightTerm132 = lightTerm88 + lightSample15.y;
                        float lightTerm133 = lightTerm132 + lightTerm127;
                        float lightTerm134 = lightTerm88 + lightSample15.z;
                        float lightTerm135 = lightTerm134 + lightTerm128;
                        float lightTerm136 = lightTerm88 + lightSample15.w;
                        float lightTerm137 = lightTerm136 + lightTerm129;
                        float lightTerm138 = lightTerm131 * 65535.0;
                        float lightTerm139 = lightTerm133 * 65535.0;
                        float lightTerm140 = lightTerm135 * 65535.0;
                        float lightTerm141 = lightTerm137 * 65535.0;
                        float lightTerm142 = lightTerm138 + 1.00000;
                        float lightTerm143 = lightTerm139 + 1.00000;
                        float lightTerm144 = lightTerm140 + 1.00000;
                        float lightTerm145 = lightTerm141 + 1.00000;
                        float lightTerm146 = saturate(lightTerm142);
                        float lightTerm147 = saturate(lightTerm143);
                        float lightTerm148 = saturate(lightTerm144);
                        float lightTerm149 = saturate(lightTerm145);
                        float lightTerm150 = (lightTerm60 + 1.50000) - lightTerm56;
                        float lightTerm151 = dot(float2(lightTerm65, lightTerm150), float2(lightTerm45, lightTerm47));
                        float lightTerm152 = lightTerm151 + lightTerm47;
                        float lightTerm153 = lightTerm69 + lightTerm151;
                        float lightTerm154 = lightTerm151 + lightTerm45;
                        float4 lightSample16 = GatherWaterShadowMapRed(lightIndex20, float2(lightTerm63, lightTerm64), int2(0, 2));
                        float lightTerm155 = abs(lightTerm152);
                        float lightTerm156 = abs(lightTerm153);
                        float lightTerm157 = abs(lightTerm154);
                        float lightTerm158 = abs(lightTerm151);
                        float lightTerm159 = min(lightTerm155, 0.000000);
                        float lightTerm160 = min(lightTerm156, 0.000000);
                        float lightTerm161 = min(lightTerm157, 0.000000);
                        float lightTerm162 = min(lightTerm158, 0.000000);
                        bool lightTest30 = (lightTerm152 > 0.000000);
                        bool lightTest31 = (lightTerm153 > 0.000000);
                        bool lightTest32 = (lightTerm154 > 0.000000);
                        bool lightTest33 = (lightTerm151 > 0.000000);
                        bool lightTest34 = (lightTerm152 < 0.000000);
                        bool lightTest35 = (lightTerm153 < 0.000000);
                        bool lightTest36 = (lightTerm154 < 0.000000);
                        bool lightTest37 = (lightTerm151 < 0.000000);
                        int lightIndex48 = (int)(lightTest30);
                        int lightIndex49 = (int)(lightTest31);
                        int lightIndex50 = (int)(lightTest32);
                        int lightIndex51 = (int)(lightTest33);
                        int lightIndex52 = (int)(lightTest34);
                        int lightIndex53 = (int)(lightTest35);
                        int lightIndex54 = (int)(lightTest36);
                        int lightIndex55 = (int)(lightTest37);
                        int lightIndex56 = lightIndex48 - lightIndex52;
                        int lightIndex57 = lightIndex49 - lightIndex53;
                        int lightIndex58 = lightIndex50 - lightIndex54;
                        int lightIndex59 = lightIndex51 - lightIndex55;
                        float lightTerm163 = (float)(lightIndex56);
                        float lightTerm164 = (float)(lightIndex57);
                        float lightTerm165 = (float)(lightIndex58);
                        float lightTerm166 = (float)(lightIndex59);
                        float lightTerm167 = lightTerm159 * lightTerm163;
                        float lightTerm168 = lightTerm160 * lightTerm164;
                        float lightTerm169 = lightTerm161 * lightTerm165;
                        float lightTerm170 = lightTerm162 * lightTerm166;
                        float lightTerm171 = lightTerm88 + lightSample16.x;
                        float lightTerm172 = lightTerm171 + lightTerm167;
                        float lightTerm173 = lightTerm88 + lightSample16.y;
                        float lightTerm174 = lightTerm173 + lightTerm168;
                        float lightTerm175 = lightTerm88 + lightSample16.z;
                        float lightTerm176 = lightTerm175 + lightTerm169;
                        float lightTerm177 = lightTerm88 + lightSample16.w;
                        float lightTerm178 = lightTerm177 + lightTerm170;
                        float lightTerm179 = lightTerm172 * 65535.0;
                        float lightTerm180 = lightTerm174 * 65535.0;
                        float lightTerm181 = lightTerm176 * 65535.0;
                        float lightTerm182 = lightTerm178 * 65535.0;
                        float lightTerm183 = lightTerm179 + 1.00000;
                        float lightTerm184 = lightTerm180 + 1.00000;
                        float lightTerm185 = lightTerm181 + 1.00000;
                        float lightTerm186 = lightTerm182 + 1.00000;
                        float lightTerm187 = saturate(lightTerm183);
                        float lightTerm188 = saturate(lightTerm184);
                        float lightTerm189 = saturate(lightTerm185);
                        float lightTerm190 = saturate(lightTerm186);
                        float lightTerm191 = dot(float2(lightTerm109, lightTerm150), float2(lightTerm45, lightTerm47));
                        float lightTerm192 = lightTerm191 + lightTerm47;
                        float lightTerm193 = lightTerm69 + lightTerm191;
                        float lightTerm194 = lightTerm191 + lightTerm45;
                        float4 lightSample17 = GatherWaterShadowMapRed(lightIndex20, float2(lightTerm63, lightTerm64), int2(2, 2));
                        float lightTerm195 = abs(lightTerm192);
                        float lightTerm196 = abs(lightTerm193);
                        float lightTerm197 = abs(lightTerm194);
                        float lightTerm198 = abs(lightTerm191);
                        float lightTerm199 = min(lightTerm195, 0.000000);
                        float lightTerm200 = min(lightTerm196, 0.000000);
                        float lightTerm201 = min(lightTerm197, 0.000000);
                        float lightTerm202 = min(lightTerm198, 0.000000);
                        bool lightTest38 = (lightTerm192 > 0.000000);
                        bool lightTest39 = (lightTerm193 > 0.000000);
                        bool lightTest40 = (lightTerm194 > 0.000000);
                        bool lightTest41 = (lightTerm191 > 0.000000);
                        bool lightTest42 = (lightTerm192 < 0.000000);
                        bool lightTest43 = (lightTerm193 < 0.000000);
                        bool lightTest44 = (lightTerm194 < 0.000000);
                        bool lightTest45 = (lightTerm191 < 0.000000);
                        int lightIndex60 = (int)(lightTest38);
                        int lightIndex61 = (int)(lightTest39);
                        int lightIndex62 = (int)(lightTest40);
                        int lightIndex63 = (int)(lightTest41);
                        int lightIndex64 = (int)(lightTest42);
                        int lightIndex65 = (int)(lightTest43);
                        int lightIndex66 = (int)(lightTest44);
                        int lightIndex67 = (int)(lightTest45);
                        int lightIndex68 = lightIndex60 - lightIndex64;
                        int lightIndex69 = lightIndex61 - lightIndex65;
                        int lightIndex70 = lightIndex62 - lightIndex66;
                        int lightIndex71 = lightIndex63 - lightIndex67;
                        float lightTerm203 = (float)(lightIndex68);
                        float lightTerm204 = (float)(lightIndex69);
                        float lightTerm205 = (float)(lightIndex70);
                        float lightTerm206 = (float)(lightIndex71);
                        float lightTerm207 = lightTerm199 * lightTerm203;
                        float lightTerm208 = lightTerm200 * lightTerm204;
                        float lightTerm209 = lightTerm201 * lightTerm205;
                        float lightTerm210 = lightTerm202 * lightTerm206;
                        float lightTerm211 = lightTerm88 + lightSample17.x;
                        float lightTerm212 = lightTerm211 + lightTerm207;
                        float lightTerm213 = lightTerm88 + lightSample17.y;
                        float lightTerm214 = lightTerm213 + lightTerm208;
                        float lightTerm215 = lightTerm88 + lightSample17.z;
                        float lightTerm216 = lightTerm215 + lightTerm209;
                        float lightTerm217 = lightTerm88 + lightSample17.w;
                        float lightTerm218 = lightTerm217 + lightTerm210;
                        float lightTerm219 = lightTerm212 * 65535.0;
                        float lightTerm220 = lightTerm214 * 65535.0;
                        float lightTerm221 = lightTerm216 * 65535.0;
                        float lightTerm222 = lightTerm218 * 65535.0;
                        float lightTerm223 = lightTerm219 + 1.00000;
                        float lightTerm224 = lightTerm220 + 1.00000;
                        float lightTerm225 = lightTerm221 + 1.00000;
                        float lightTerm226 = lightTerm222 + 1.00000;
                        float lightTerm227 = saturate(lightTerm223);
                        float lightTerm228 = saturate(lightTerm224);
                        float lightTerm229 = saturate(lightTerm225);
                        float lightTerm230 = saturate(lightTerm226);
                        float lightTerm231 = 1.00000 - lightTerm61;
                        float lightTerm232 = lightTerm108 * lightTerm231;
                        float lightTerm233 = lightTerm105 * lightTerm231;
                        float lightTerm234 = lightTerm190 * lightTerm231;
                        float lightTerm235 = lightTerm187 * lightTerm231;
                        float lightTerm236 = lightTerm232 + lightTerm107;
                        float lightTerm237 = lightTerm233 + lightTerm106;
                        float lightTerm238 = lightTerm234 + lightTerm189;
                        float lightTerm239 = lightTerm235 + lightTerm188;
                        float lightTerm240 = lightTerm236 + lightTerm149;
                        float lightTerm241 = dot(float4((lightTerm240 + (lightTerm148 * lightTerm61)), ((lightTerm237 + lightTerm146) + (lightTerm147 * lightTerm61)), ((lightTerm238 + lightTerm230) + (lightTerm229 * lightTerm61)), ((lightTerm239 + lightTerm227) + (lightTerm228 * lightTerm61))), float4((1.00000 - lightTerm62), 1.00000, 1.00000, lightTerm62));

                        localShadowVisibility = (lightTerm241 * 0.111111);
                    }
                    else
                    {
                        localShadowVisibility = 1.00000;
                    }
                }
                else
                {
                    localShadowVisibility = 1.00000;
                }

                int lightIndex72 = lightIndex10 + 0;
                int lightIndex73 = lightIndex6 & 262144;
                bool lightTest46 = (lightIndex73 == 0);
                float lightTerm242 = lightSample2.x - surface.WorldPosition.x;
                float lightTerm243 = lightSample2.y - surface.WorldPosition.y;
                float lightTerm244 = lightSample2.z - surface.WorldPosition.z;
                float lightTerm245 = dot(float3(lightTerm242, lightTerm243, lightTerm244), float3(lightTerm242, lightTerm243, lightTerm244));
                bool lightTest47 = (lightTerm245 > 0.000100000);

                if (lightTest47)
                {
                    float lightTerm246 = rsqrt(lightTerm245);
                    lightVector = float3((lightTerm246 * lightTerm242), (lightTerm246 * lightTerm243), (lightTerm246 * lightTerm244));
                }
                else
                {
                    lightVector = float3(0.0, 0.0, 0.0);
                }

                float lightTerm247 = lightSample7.x * lightSample7.x;
                bool lightTest48 = (lightTerm245 >= lightTerm247);
                float lightTerm248 = (lightSample2.w * lightSample2.w) * lightTerm245;
                float lightTerm249 = saturate((1.00000 - (lightTerm248 * lightTerm248)));

                if (lightTest2)
                {
                    float lightTerm250 = dot(float3(lightVector.x, lightVector.y, lightVector.z), float3(lightSample4.x, lightSample4.y, lightSample4.z));
                    float lightTerm251 = saturate(((lightTerm250 - lightSample5.x) * lightSample5.y));

                    lightTerm3 = (lightTerm251 * lightTerm251);
                }
                else
                {
                    lightTerm3 = 1.00000;
                }

                float lightTerm252 = ((lightTerm249 * lightTerm249) * ((float)(uint)(lightTest48))) * lightTerm3;

                if (lightTest46)
                {
                    shadowedLightColor = float3(1.00000, 1.00000, 1.00000);
                }
                else
                {
                    float lightTerm253 = -0.000000 - lightSample4.y;
                    float lightTerm254 = -0.000000 - lightSample4.z;
                    float lightTerm255 = -0.000000 - lightSample4.x;
                    float lightTerm256 = lightSample6.z * lightTerm253;
                    float lightTerm257 = lightSample6.y * lightTerm254;
                    float lightTerm258 = lightTerm256 - lightTerm257;
                    float lightTerm259 = lightSample6.x * lightTerm254;
                    float lightTerm260 = lightSample6.z * lightTerm255;
                    float lightTerm261 = -0.000000 - lightVector.x;
                    float lightTerm262 = -0.000000 - lightVector.y;
                    float lightTerm263 = -0.000000 - lightVector.z;
                    float lightTerm264 = mad(lightTerm254, lightTerm263, (mad(lightTerm253, lightTerm262, (lightVector.x * lightSample4.x))));
                    float lightTerm265 = mad(lightSample6.z, lightTerm263, (mad(lightSample6.y, lightTerm262, (lightSample6.x * lightTerm261))));
                    float localLightArcTangentValue = atan(((-0.000000 - lightTerm265) / (-0.000000 - lightTerm264)));
                    bool lightTest49 = (lightTerm264 > -0.000000);
                    bool lightTest50 = (lightTerm264 == -0.000000);
                    bool lightTest51 = (lightTerm265 <= -0.000000);
                    bool lightTest52 = (lightTerm265 > -0.000000);
                    bool lightTest53 = lightTest49 & lightTest51;
                    float lightTerm266 = lightTest53 ? (localLightArcTangentValue + 3.14159) : localLightArcTangentValue;
                    bool lightTest54 = lightTest49 & lightTest52;
                    float lightTerm267 = lightTest54 ? (localLightArcTangentValue + -3.14159) : lightTerm266;
                    bool lightTest55 = lightTest50 & lightTest52;
                    bool lightTest56 = lightTest50 & lightTest51;
                    float lightTerm268 = lightTest55 ? 0.750000 : ((lightTerm267 * 0.159155) + 1.00000);
                    float lightTerm269 = lightTest56 ? 1.25000 : lightTerm268;
                    float4 localLightViewSharedBilinearWrappedSamplerData = ForwardLightProfileTextures[NonUniformResourceIndex(lightIndex72)].Sample(View_SharedBilinearWrappedSampler, float2((frac(lightTerm269)), (((asin((-0.000000 - (mad(((lightSample6.y * lightTerm255) - (lightSample6.x * lightTerm253)), lightTerm263, (mad((lightTerm259 - lightTerm260), lightTerm262, (lightTerm258 * lightTerm261)))))))) * 0.318310) + 0.500000)), int2(0, 0));

                    shadowedLightColor = float3((localLightViewSharedBilinearWrappedSamplerData[0]), (localLightViewSharedBilinearWrappedSamplerData[1]), (localLightViewSharedBilinearWrappedSamplerData[2]));
                }

                bool lightTest57 = (lightTerm252 > 0.000000);
                evaluateDetailedLocalLight = false;

                if (lightTest57)
                {
                    if (lightTest1)
                    {
                        bool lightTest58 = (localShadowVisibility > 0.000000);

                        if (lightTest58)
                        {
                            lightTerm4 = localShadowVisibility;
                            evaluateDetailedLocalLight = true;
                        }
                        else
                        {
                            evaluatedDiffuse = float3(0.0, 0.0, 0.0);
                            evaluatedTransmission = float3(0.0, 0.0, 0.0);
                            evaluatedSubsurface = float3(0.0, 0.0, 0.0);
                            evaluatedSpecular = float3(0.0, 0.0, 0.0);
                        }
                    }
                    else
                    {
                        lightTerm4 = 1.00000;
                        evaluateDetailedLocalLight = true;
                    }

                    if (evaluateDetailedLocalLight)
                    {
                        float lightTerm270 = max(0.000100000, (max(1.00000, lightTerm245)));
                        float lightTerm271 = 1.00000 / lightTerm270;
                        float lightTerm272 = dot(float3(surface.Normal.x, surface.Normal.y, surface.Normal.z), float3(lightVector.x, lightVector.y, lightVector.z));
                        bool lightTest59 = (lightSample7.x > 0.000000);

                        if (lightTest59)
                        {
                            float lightTerm273 = sqrt((saturate((lightTerm247 * lightTerm271))));
                            bool lightTest60 = (lightTerm273 > (max(1.00000e-05, lightTerm272)));

                            if (lightTest60)
                            {
                                float lightTerm274 = (max((-0.000000 - lightTerm273), lightTerm272)) + lightTerm273;
                                lightTerm5 = ((lightTerm274 * lightTerm274) * (0.250000 / lightTerm273));
                            }
                            else
                            {
                                lightTerm5 = lightTerm272;
                            }
                        }
                        else
                        {
                            lightTerm5 = lightTerm272;
                        }

                        float lightTerm275 = max(0.000000, lightTerm5);
                        float lightTerm276 = surfaceTerm85 * surfaceTerm85;
                        float lightTerm277 = saturate((((1.00000 - lightTerm276) * lightSample7.x) * (rsqrt(lightTerm270))));
                        float lightTerm278 = dot(float3((-surface.CameraToPixelDirection.x), (-surface.CameraToPixelDirection.y), (-surface.CameraToPixelDirection.z)), float3(lightVector.x, lightVector.y, lightVector.z));
                        bool lightTest61 = (lightTerm277 > 0.000000);

                        if (lightTest61)
                        {
                            float lightTerm279 = sqrt((max(0.000000, (1.00000 - (lightTerm277 * lightTerm277)))));
                            float lightTerm280 = ((lightTerm272 * 2.00000) * normalViewDot) - lightTerm278;
                            bool lightTest62 = (isnan(lightTerm280) || isnan(lightTerm279) || (lightTerm280 < lightTerm279));

                            if (lightTest62)
                            {
                                float lightTerm281 = (rsqrt((max(1.00000e-05, (1.00000 - (lightTerm280 * lightTerm280)))))) * lightTerm277;

                                lightTerm6 = ((lightTerm281 * ((((normalViewDot * normalViewDot) * 2.00000) + -1.00000) - (lightTerm280 * lightTerm278))) + (lightTerm279 * lightTerm278));
                                lightTerm7 = ((lightTerm281 * (normalViewDot - (lightTerm280 * lightTerm272))) + (lightTerm279 * lightTerm272));
                                lightTerm8 = lightTerm6 * 2.00000;
                                lightTerm9 = lightTerm8 + 2.00000;
                                lightTerm10 = max(1.00000e-05, lightTerm9);
                                lightTerm11 = rsqrt(lightTerm10);
                                lightTerm12 = lightTerm7 + normalViewDot;
                                lightTerm13 = lightTerm11 * lightTerm12;
                                lightTerm14 = saturate(lightTerm13);
                                lightTerm15 = lightTerm6 + 1.00000;
                                lightTerm16 = lightTerm11 * lightTerm15;
                                lightTerm17 = min(lightTerm16, 1.00000);

                                normalizedLightVector = float3(lightTerm17, lightTerm14, lightTerm6);
                            }
                            else
                            {
                                normalizedLightVector = float3(surfaceAbsoluteValue, 1.00000, lightTerm278);
                            }
                        }
                        else
                        {
                            lightTerm6 = lightTerm278;
                            lightTerm7 = lightTerm272;
                            lightTerm8 = lightTerm6 * 2.00000;
                            lightTerm9 = lightTerm8 + 2.00000;
                            lightTerm10 = max(1.00000e-05, lightTerm9);
                            lightTerm11 = rsqrt(lightTerm10);
                            lightTerm12 = lightTerm7 + normalViewDot;
                            lightTerm13 = lightTerm11 * lightTerm12;
                            lightTerm14 = saturate(lightTerm13);
                            lightTerm15 = lightTerm6 + 1.00000;
                            lightTerm16 = lightTerm11 * lightTerm15;
                            lightTerm17 = min(lightTerm16, 1.00000);

                            normalizedLightVector = float3(lightTerm17, lightTerm14, lightTerm6);
                        }

                        float lightTerm282 = max(1.00000e-05, normalizedLightVector.x);
                        float lightTerm283 = sqrt((max(0.000000, (1.00000 - ((1.0 - normalViewDot * normalViewDot) * 0.444444)))));
                        float lightTerm284 = (reflectionBasis.x * 0.666667) - (lightTerm283 * surface.Normal.x);
                        float lightTerm285 = (reflectionBasis.y * 0.666667) - (lightTerm283 * surface.Normal.y);
                        float lightTerm286 = (reflectionBasis.z * 0.666667) - (lightTerm283 * surface.Normal.z);
                        float lightTerm287 = dot(float3(lightTerm284, lightTerm285, lightTerm286), float3(lightTerm284, lightTerm285, lightTerm286));
                        float lightTerm288 = rsqrt(lightTerm287);
                        float lightTerm289 = dot(float3((-0.000000 - (lightTerm288 * lightTerm284)), (-0.000000 - (lightTerm288 * lightTerm285)), (-0.000000 - (lightTerm288 * lightTerm286))), float3(lightVector.x, lightVector.y, lightVector.z));
                        float lightTerm290 = lightTerm276 * lightTerm276;
                        float lightTerm291 = lightTest61 ? ((1.00000 / ((((lightTerm277 * 0.250000) * (lightTerm277 + (lightTerm276 * 3.00000))) * (1.00000 / lightTerm282)) + lightTerm290)) * lightTerm290) : 1.00000;
                        float lightTerm292 = ((normalizedLightVector.y * normalizedLightVector.y) * (lightTerm290 + -1.00000)) + 1.00000;
                        float lightTerm293 = (lightTerm275 * 2.00000) * surfaceTerm19;
                        float lightTerm294 = 1.00000 - lightTerm282;
                        float lightTerm295 = lightTerm294 * lightTerm294;
                        float lightTerm296 = dot(float3(dielectricF0, dielectricF0, dielectricF0), float3(16.6667, 16.6667, 16.6667));
                        float lightTerm297 = saturate(((((min(lightTerm296, 1.00000)) - dielectricF0) * ((lightTerm295 * lightTerm295) * lightTerm294)) + dielectricF0));
                        float lightTerm298 = max(0.0400000, surfaceTerm85);
                        float lightTerm299 = max(1.00000e-06, (1.00000 - (saturate(((((lightTerm298 * lightTerm275) + 0.0266916) / (lightTerm275 + 0.466495)) * (exp2((((1.00000 / lightTerm298) * lightTerm275) * (log2(lightTerm298))))))))));
                        float lightTerm300 = 1.00000 - lightTerm275;
                        float lightTerm301 = lightTerm300 * lightTerm300;
                        float lightTerm302 = ((lightTerm301 * lightTerm301) * lightTerm300) * (exp2(((log2((((exp2(((lightTerm298 * 4.77030) * (log2(lightTerm275))))) * 2.36651) + 0.0387332))) * lightTerm298)));
                        float lightTerm303 = (((1.00000 / lightTerm299) * (1.00000 - lightTerm299)) * dielectricF0) + 1.00000;
                        float lightTerm304 = lightTerm303 * (((lightTerm299 - lightTerm302) * dielectricF0) + lightTerm302);
                        float lightTerm305 = dot(float3(lightTerm304, lightTerm304, lightTerm304), float3(0.333333, 0.333333, 0.333333));
                        float lightTerm306 = saturate((1.00000 - lightTerm305));
                        float lightTerm307 = ((((lightTerm290 * 0.318310) * lightTerm275) * (1.00000 / (lightTerm292 * lightTerm292))) * lightTerm291) * (0.500000 / ((lightTerm276 * ((surfaceTerm19 + lightTerm275) - lightTerm293)) + lightTerm293));
                        float lightTerm308 = (((lightTerm289 * lightTerm289) + 1.00000) * 0.0323705) * (exp2(((log2((max(0.000000, ((lightTerm289 * 1.20000) + 1.36000))))) * -1.50000)));
                        float lightTerm309 = ((normalizedLightVector.z * normalizedLightVector.z) + 1.00000) * 0.0596831;
                        float lightTerm310 = saturate((lightTerm275 * 1000.00000));
                        float lightTerm311 = min(lightTerm4, (lightTerm310 * lightTerm310));
                        float lightTerm312 = min(lightTerm4, 1.00000);
                        float lightTerm313 = (lightTerm252 * lightSample3.x) * shadowedLightColor.x;
                        float lightTerm314 = (lightTerm252 * lightSample3.y) * shadowedLightColor.y;
                        float lightTerm315 = (lightTerm252 * lightSample3.z) * shadowedLightColor.z;
                        float lightTerm316 = lightTerm271 * lightTerm313;
                        float lightTerm317 = lightTerm271 * lightTerm314;
                        float lightTerm318 = lightTerm271 * lightTerm315;

                        evaluatedDiffuse = float3(
                            ((lightTerm316 * lightTerm309) * lightTerm312),
                            ((lightTerm317 * lightTerm309) * lightTerm312),
                            ((lightTerm318 * lightTerm309) * lightTerm312));
                        evaluatedTransmission = float3(
                            ((lightTerm316 * lightTerm308) * lightTerm312),
                            ((lightTerm317 * lightTerm308) * lightTerm312),
                            ((lightTerm318 * lightTerm308) * lightTerm312));
                        evaluatedSubsurface = float3(
                            ((((lightTerm316 * lightTerm297) * lightTerm307) * lightTerm303) * lightTerm311),
                            ((((lightTerm317 * lightTerm307) * lightTerm297) * lightTerm303) * lightTerm311),
                            ((((lightTerm318 * lightTerm307) * lightTerm297) * lightTerm303) * lightTerm311));
                        evaluatedSpecular = float3(
                            (((((lightTerm313 * 0.257831) * lightTerm271) * lightTerm275) * lightTerm306) * lightTerm311),
                            (((((lightTerm314 * 0.257831) * lightTerm271) * lightTerm275) * lightTerm306) * lightTerm311),
                            (((((lightTerm315 * 0.257831) * lightTerm271) * lightTerm275) * lightTerm306) * lightTerm311));
                    }
                }
                else
                {
                    evaluatedDiffuse = float3(0.0, 0.0, 0.0);
                    evaluatedTransmission = float3(0.0, 0.0, 0.0);
                    evaluatedSubsurface = float3(0.0, 0.0, 0.0);
                    evaluatedSpecular = float3(0.0, 0.0, 0.0);
                }

                totalDiffuse = float3((evaluatedDiffuse.x + localDiffuse.x), (evaluatedDiffuse.y + localDiffuse.y), (evaluatedDiffuse.z + localDiffuse.z));
                totalTransmission = float3((evaluatedTransmission.x + localTransmission.x), (evaluatedTransmission.y + localTransmission.y), (evaluatedTransmission.z + localTransmission.z));
                totalSubsurface = float3((evaluatedSubsurface.x + localSubsurface.x), (evaluatedSubsurface.y + localSubsurface.y), (evaluatedSubsurface.z + localSubsurface.z));
                totalSpecular = float3((evaluatedSpecular.x + localSpecular.x), (evaluatedSpecular.y + localSpecular.y), (evaluatedSpecular.z + localSpecular.z));
                totalDiffuseSecondary = float3(localDiffuseSecondary.x, localDiffuseSecondary.y, localDiffuseSecondary.z);
                totalSpecularSecondary = float3(localSpecularSecondary.x, localSpecularSecondary.y, localSpecularSecondary.z);
            }
            else
            {
                float lightTerm319 = lightSample2.x - surface.WorldPosition.x;
                float lightTerm320 = lightSample2.y - surface.WorldPosition.y;
                float lightTerm321 = lightSample2.z - surface.WorldPosition.z;
                float lightTerm322 = dot(float3(lightTerm319, lightTerm320, lightTerm321), float3(lightTerm319, lightTerm320, lightTerm321));
                float lightTerm323 = (lightSample2.w * lightSample2.w) * lightTerm322;
                float lightTerm324 = lightTerm323 * lightTerm323;
                float lightTerm325 = lightTerm324 * lightTerm324;
                float lightTerm326 = saturate((1.00000 - (lightTerm325 * lightTerm325)));
                float lightTerm327 = lightTerm326 * lightTerm326;
                float lightTerm328 = lightTerm327 * lightTerm327;
                float lightTerm329 = lightTerm328 * lightTerm328;
                bool lightTest63 = (lightTerm329 > 0.000000);

                if (lightTest63)
                {
                    float lightTerm330 = dot(float3(surface.CameraToPixelDirection.x, surface.CameraToPixelDirection.y, surface.CameraToPixelDirection.z), float3(surface.Normal.x, surface.Normal.y, surface.Normal.z));
                    float lightTerm331 = lightTerm330 * 2.00000;
                    float lightTerm332 = surface.CameraToPixelDirection.x - (lightTerm331 * surface.Normal.x);
                    float lightTerm333 = surface.CameraToPixelDirection.y - (lightTerm331 * surface.Normal.y);
                    float lightTerm334 = surface.CameraToPixelDirection.z - (lightTerm331 * surface.Normal.z);
                    bool lightTest64 = (lightSample7.y == 0.000000);

                    if (lightTest64)
                    {
                        bool lightTest65 = (lightTerm322 > 0.000100000);

                        if (lightTest65)
                        {
                            float lightTerm335 = rsqrt(lightTerm322);
                            simpleLightColor = float3((lightTerm335 * lightTerm319), (lightTerm335 * lightTerm320), (lightTerm335 * lightTerm321));
                        }
                        else
                        {
                            simpleLightColor = float3(0.0, 0.0, 0.0);
                        }

                        float lightTerm336 = 1.00000 / (max(0.000100000, (max(1.00000, lightTerm322))));

                        simpleDiffuse = float3(lightTerm336, ((lightSample7.x * lightSample7.x) * lightTerm336), simpleLightColor.x);
                        simpleTransmission = float3(simpleLightColor.y, simpleLightColor.z, simpleLightColor.x);
                        simpleSpecular = float3(simpleLightColor.y, simpleLightColor.z, simpleLightColor.x);
                        lightTerm18 = simpleLightColor.y;
                        lightTerm19 = simpleLightColor.z;
                    }
                    else
                    {
                        float lightTerm337 = lightSample7.y * 0.500000;
                        float lightTerm338 = lightTerm337 * lightSample4.x;
                        float lightTerm339 = lightTerm337 * lightSample4.y;
                        float lightTerm340 = lightTerm337 * lightSample4.z;
                        float lightTerm341 = lightTerm319 - lightTerm338;
                        float lightTerm342 = lightTerm320 - lightTerm339;
                        float lightTerm343 = lightTerm321 - lightTerm340;
                        float lightTerm344 = lightTerm338 + lightTerm319;
                        float lightTerm345 = lightTerm339 + lightTerm320;
                        float lightTerm346 = lightTerm340 + lightTerm321;
                        float lightTerm347 = dot(float3(lightTerm341, lightTerm342, lightTerm343), float3(lightTerm341, lightTerm342, lightTerm343));
                        float lightTerm348 = dot(float3(lightTerm344, lightTerm345, lightTerm346), float3(lightTerm344, lightTerm345, lightTerm346));
                        float lightTerm349 = rsqrt((max(1.00000e-05, lightTerm347)));
                        float lightTerm350 = rsqrt((max(1.00000e-05, lightTerm348)));
                        float lightTerm351 = lightTerm350 * lightTerm349;
                        float lightTerm352 = dot(float3(lightTerm341, lightTerm342, lightTerm343), float3(lightTerm344, lightTerm345, lightTerm346));
                        float lightTerm353 = (1.00000 / (max(0.000100000, (max(lightTerm351, (((lightTerm352 * 0.500000) * lightTerm351) + 0.500000)))))) * lightTerm351;
                        float lightTerm354 = (lightSample4.x * 2.00000) * lightTerm337;
                        float lightTerm355 = (lightSample4.y * 2.00000) * lightTerm337;
                        float lightTerm356 = (lightSample4.z * 2.00000) * lightTerm337;
                        float lightTerm357 = dot(float3(lightTerm354, lightTerm355, lightTerm356), float3(lightTerm354, lightTerm355, lightTerm356));
                        bool lightTest66 = (lightTerm357 > 0.000100000);

                        if (lightTest66)
                        {
                            float lightTerm358 = dot(float3(lightTerm332, lightTerm333, lightTerm334), float3(lightTerm354, lightTerm355, lightTerm356));
                            float lightTerm359 = dot(float3(lightTerm341, lightTerm342, lightTerm343), float3(((lightTerm358 * lightTerm332) - lightTerm354), ((lightTerm358 * lightTerm333) - lightTerm355), ((lightTerm358 * lightTerm334) - lightTerm356)));
                            float lightTerm360 = saturate(((1.00000 / (lightTerm357 - (lightTerm358 * lightTerm358))) * lightTerm359));
                            float lightTerm361 = (lightTerm360 * lightTerm354) + lightTerm341;
                            float lightTerm362 = (lightTerm360 * lightTerm355) + lightTerm342;
                            float lightTerm363 = (lightTerm360 * lightTerm356) + lightTerm343;
                            float lightTerm364 = dot(float3(lightTerm361, lightTerm362, lightTerm363), float3(lightTerm361, lightTerm362, lightTerm363));
                            float lightTerm365 = rsqrt(lightTerm364);

                            simpleLightVector = float3((lightTerm361 * lightTerm365), (lightTerm362 * lightTerm365), (lightTerm363 * lightTerm365));
                        }
                        else
                        {
                            simpleLightVector = float3(lightTerm341, lightTerm342, lightTerm343);
                        }

                        simpleDiffuse = float3(lightTerm353, ((lightSample7.x * lightSample7.x) * lightTerm353), (lightTerm349 * lightTerm341));
                        simpleTransmission = float3((lightTerm349 * lightTerm342), (lightTerm349 * lightTerm343), (lightTerm350 * lightTerm344));
                        simpleSpecular = float3((lightTerm350 * lightTerm345), (lightTerm350 * lightTerm346), simpleLightVector.x);
                        lightTerm18 = simpleLightVector.y;
                        lightTerm19 = simpleLightVector.z;
                    }

                    float lightTerm366 = dot(float3(surface.Normal.x, surface.Normal.y, surface.Normal.z), float3(simpleDiffuse.z, simpleTransmission.x, simpleTransmission.y));
                    float lightTerm367 = dot(float3(surface.Normal.x, surface.Normal.y, surface.Normal.z), float3(simpleTransmission.z, simpleSpecular.x, simpleSpecular.y));
                    float lightTerm368 = simpleDiffuse.y + 1.00000;
                    float lightTerm369 = simpleSpecular.z - surface.CameraToPixelDirection.x;
                    float lightTerm370 = lightTerm18 - surface.CameraToPixelDirection.y;
                    float lightTerm371 = lightTerm19 - surface.CameraToPixelDirection.z;
                    float lightTerm372 = dot(float3(lightTerm369, lightTerm370, lightTerm371), float3(lightTerm369, lightTerm370, lightTerm371));
                    float lightTerm373 = rsqrt(lightTerm372);
                    float lightTerm374 = lightTerm373 * lightTerm369;
                    float lightTerm375 = lightTerm373 * lightTerm370;
                    float lightTerm376 = lightTerm373 * lightTerm371;
                    float lightTerm377 = dot(float3((-surface.CameraToPixelDirection.x), (-surface.CameraToPixelDirection.y), (-surface.CameraToPixelDirection.z)), float3(lightTerm374, lightTerm375, lightTerm376));
                    float lightTerm378 = dot(float3(surface.Normal.x, surface.Normal.y, surface.Normal.z), float3(lightTerm374, lightTerm375, lightTerm376));
                    float lightTerm379 = saturate(lightTerm378);
                    float lightTerm380 = surfaceTerm85 * surfaceTerm85;
                    float lightTerm381 = saturate((((1.00000 - lightTerm380) * lightSample7.x) * (sqrt(simpleDiffuse.x))));
                    float localLightExponentialValue = exp2(((log2((saturate(((((lightTerm381 * 0.250000) * (lightTerm381 + (lightTerm380 * 3.00000))) * (1.00000 / (max(0.000100000, lightTerm377)))) + (lightTerm380 * lightTerm380)))))) * 0.250000));
                    float lightTerm382 = dot(float3(simpleDiffuse.z, simpleTransmission.x, simpleTransmission.y), float3(simpleTransmission.z, simpleSpecular.x, simpleSpecular.y));
                    float lightTerm383 = saturate(lightTerm382);
                    bool lightTest67 = (lightTerm383 < 1.00000);
                    float lightTerm384 = localLightExponentialValue * localLightExponentialValue;
                    float lightTerm385 = lightTerm384 * lightTerm384;

                    if (lightTest67)
                    {
                        float lightTerm386 = sqrt((saturate(((1.00000 / (lightTerm383 + 1.00000)) * (1.00000 - lightTerm383)))));

                        lightTerm20 = ((1.00000 / (saturate((((lightTerm386 * 0.250000) * (lightTerm386 + (lightTerm384 * 3.00000))) + lightTerm385)))) * lightTerm385);
                    }
                    else
                    {
                        lightTerm20 = 1.00000;
                    }

                    float lightTerm387 = ((lightTerm379 * lightTerm379) * (lightTerm385 + -1.00000)) + 1.00000;
                    float lightTerm388 = (max(0.000000, ((((lightTerm367 + lightTerm366) * 0.500000) + simpleDiffuse.y) * (1.00000 / (lightTerm368 * lightTerm368))))) * 0.318310;
                    float lightTerm389 = ((lightTerm388 * lightTerm385) * lightTerm20) * (1.00000 / (lightTerm387 * lightTerm387));
                    float lightTerm390 = (lightTerm329 * lightSample3.x) * simpleDiffuse.x;
                    float lightTerm391 = (lightTerm329 * lightSample3.y) * simpleDiffuse.x;
                    float lightTerm392 = (lightTerm329 * lightSample3.z) * simpleDiffuse.x;

                    nextLocalDiffuse = float3((lightTerm389 * lightTerm390), (lightTerm389 * lightTerm391), (lightTerm389 * lightTerm392));
                    nextLocalSpecular = float3((lightTerm388 * lightTerm390), (lightTerm388 * lightTerm391), (lightTerm388 * lightTerm392));
                }
                else
                {
                    nextLocalDiffuse = float3(0.0, 0.0, 0.0);
                    nextLocalSpecular = float3(0.0, 0.0, 0.0);
                }

                totalDiffuse = float3(localDiffuse.x, localDiffuse.y, localDiffuse.z);
                totalTransmission = float3(localTransmission.x, localTransmission.y, localTransmission.z);
                totalSubsurface = float3(localSubsurface.x, localSubsurface.y, localSubsurface.z);
                totalSpecular = float3(localSpecular.x, localSpecular.y, localSpecular.z);
                totalDiffuseSecondary = float3((nextLocalDiffuse.x + localDiffuseSecondary.x), (nextLocalDiffuse.y + localDiffuseSecondary.y), (nextLocalDiffuse.z + localDiffuseSecondary.z));
                totalSpecularSecondary = float3((nextLocalSpecular.x + localSpecularSecondary.x), (nextLocalSpecular.y + localSpecularSecondary.y), (nextLocalSpecular.z + localSpecularSecondary.z));
            }

            int lightIndex74 = localLightLoopIndex + 1;
            bool lightTest68 = (lightIndex74 == lightTerm24);

            if (lightTest68)
            {
                break;
            }
            else
            {
                localDiffuse = float3(totalDiffuse.x, totalDiffuse.y, totalDiffuse.z);
                localTransmission = float3(totalTransmission.x, totalTransmission.y, totalTransmission.z);
                localSubsurface = float3(totalSubsurface.x, totalSubsurface.y, totalSubsurface.z);
                localSpecular = float3(totalSpecular.x, totalSpecular.y, totalSpecular.z);
                localDiffuseSecondary = float3(totalDiffuseSecondary.x, totalDiffuseSecondary.y, totalDiffuseSecondary.z);
                localSpecularSecondary = float3(totalSpecularSecondary.x, totalSpecularSecondary.y, totalSpecularSecondary.z);
                localLightLoopIndex = lightIndex74;
                continue;
            }
        }

        directDiffuseLighting = float3(totalDiffuse.x, totalDiffuse.y, totalDiffuse.z);
        directTransmissionLighting = float3(totalTransmission.x, totalTransmission.y, totalTransmission.z);
        directSubsurfaceLighting = float3(totalSubsurface.x, totalSubsurface.y, totalSubsurface.z);
        directSpecularLighting = float3(totalSpecular.x, totalSpecular.y, totalSpecular.z);
        secondaryDiffuseLighting = float3(totalDiffuseSecondary.x, totalDiffuseSecondary.y, totalDiffuseSecondary.z);
        secondarySpecularLighting = float3(totalSpecularSecondary.x, totalSpecularSecondary.y, totalSpecularSecondary.z);
    }

    DirectLightingResult result;
    result.Primary.Diffuse = directDiffuseLighting;
    result.Primary.Transmission = directTransmissionLighting;
    result.Primary.Subsurface = directSubsurfaceLighting;
    result.Primary.Specular = directSpecularLighting;
    result.SecondaryDiffuse = secondaryDiffuseLighting;
    result.SecondarySpecular = secondarySpecularLighting;
    return result;
}

struct ReflectionCaptureLighting
{
    float3 Diffuse;
    float3 Specular;
    float3 SecondaryDiffuse;
    float3 SecondarySpecular;
};

ReflectionCaptureLighting EvaluateReflectionCaptures(
    SurfaceState surface,
    int clusterCellIndex,
    float captureWeight)
{
    int surfaceIndex22 = clusterCellIndex;
    float reflectionCaptureProduct = captureWeight;
    float3 captureDiffuse;
    float3 captureSpecular;
    float3 captureSecondaryDiffuse;
    float3 captureSecondarySpecular;
    float captureTerm0;
    int reflectionCaptureLoopIndex;
    float3 accumulatedCaptureDiffuse;
    float3 accumulatedCaptureSpecular;
    float3 accumulatedCaptureSecondaryDiffuse;
    float3 accumulatedCaptureSecondarySpecular;
    float captureTerm1;
    float3 indirectDiffuseLighting;
    float3 indirectSpecularLighting;
    float3 indirectSecondaryDiffuse;
    float3 indirectSecondarySpecular;

    int4 reflectionCaptureTranslucentbasepassShared_Forward_NumLocalLightsData = float4(TranslucentBasePass_Shared_Forward_NumLocalLights, TranslucentBasePass_Shared_Forward_NumReflectionCaptures, TranslucentBasePass_Shared_Forward_HasDirectionalLight, TranslucentBasePass_Shared_Forward_NumGridCells);
    int captureIndex0 = reflectionCaptureTranslucentbasepassShared_Forward_NumLocalLightsData.w + surfaceIndex22;
    int captureIndex1 = captureIndex0 << 1;
    int4 captureSample0 = TranslucentBasePass_Shared_Forward_NumCulledLightsGrid.Load(captureIndex1);
    int reflectionCaptureMinimumValue = (int)min((uint)(captureSample0.x), (uint)(reflectionCaptureTranslucentbasepassShared_Forward_NumLocalLightsData.y));
    int captureIndex2 = captureIndex1 | 1;
    int4 captureSample1 = TranslucentBasePass_Shared_Forward_NumCulledLightsGrid.Load(captureIndex2);
    float reflectionCaptureSaturatedValue = saturate(surface.FilteredRoughness);
    float reflectionCubemapMip = max(0.000000, (min(((7.00000 - (reflectionCaptureSaturatedValue * 3.00000)) * reflectionCaptureSaturatedValue), 4.00000)));
    bool captureTest0 = (reflectionCaptureMinimumValue == 0);

    if (captureTest0)
    {
        indirectDiffuseLighting = float3(0.0, 0.0, 0.0);
        indirectSpecularLighting = float3(0.0, 0.0, 0.0);
        indirectSecondaryDiffuse = float3(0.0, 0.0, 0.0);
        indirectSecondarySpecular = float3(0.0, 0.0, 0.0);
    }
    else
    {
        captureDiffuse = float3(0.0, 0.0, 0.0);
        captureSpecular = float3(0.0, 0.0, 0.0);
        captureSecondaryDiffuse = float3(0.0, 0.0, 0.0);
        captureSecondarySpecular = float3(0.0, 0.0, 0.0);
        captureTerm0 = 0.000000;
        reflectionCaptureLoopIndex = 0;

        // Reflection-capture cubemap accumulation
        while (true)
        {
            int captureIndex3 = reflectionCaptureLoopIndex + captureSample1.x;
            int4 reflectionCaptureCulledLightDataGridLoadResult = TranslucentBasePass_Shared_Forward_CulledLightDataGrid.Load(captureIndex3);
            float4 reflectionCaptureReflectioncapturePositionAndRadiusData = ReflectionCapture_PositionAndRadius[reflectionCaptureCulledLightDataGridLoadResult.x];
            float4 reflectionCaptureReflectioncaptureCapturePropertiesData = ReflectionCapture_CaptureProperties[reflectionCaptureCulledLightDataGridLoadResult.x];
            float reflectionCaptureExponentialValue = exp2((reflectionCaptureReflectioncaptureCapturePropertiesData[0]));
            float captureTerm2 = surface.WorldPosition.x - (reflectionCaptureReflectioncapturePositionAndRadiusData[0]);
            float captureTerm3 = surface.WorldPosition.y - (reflectionCaptureReflectioncapturePositionAndRadiusData[1]);
            float captureTerm4 = surface.WorldPosition.z - (reflectionCaptureReflectioncapturePositionAndRadiusData[2]);
            float reflectionCaptureInfluence = saturate((1.00000 - ((1.00000 / (max(1.00000, (reflectionCaptureReflectioncapturePositionAndRadiusData[3])))) * (sqrt((((captureTerm2 * captureTerm2) + (captureTerm3 * captureTerm3)) + (captureTerm4 * captureTerm4)))))));
            bool captureTest1 = (reflectionCaptureInfluence > 0.000000);

            if (captureTest1)
            {
                int reflectionCaptureIntegerBits = asint((uint)((reflectionCaptureReflectioncaptureCapturePropertiesData[1])));
                int captureIndex4 = reflectionCaptureIntegerBits + 0;
                float4 reflectionCaptureViewSharedTrilinearClampedSamplerData = ReflectionCubemaps[NonUniformResourceIndex(captureIndex4)].SampleLevel(View_SharedTrilinearClampedSampler, float3(surface.ReflectionDirection.x, surface.ReflectionDirection.y, surface.ReflectionDirection.z), reflectionCubemapMip);
                float4 captureSample2 = ReflectionCubemaps[NonUniformResourceIndex(captureIndex4)].SampleLevel(View_SharedBilinearClampedSampler, float3(surface.Normal.x, surface.Normal.y, surface.Normal.z), 4.00000);
                float4 captureSample3 = ReflectionCubemaps[NonUniformResourceIndex(captureIndex4)].SampleLevel(View_SharedBilinearClampedSampler, float3(surface.ReflectionDirection.x, surface.ReflectionDirection.y, surface.ReflectionDirection.z), 8.00000);
                float4 captureSample4 = ReflectionCubemaps[NonUniformResourceIndex(captureIndex4)].SampleLevel(View_SharedBilinearClampedSampler, float3(surface.Normal.x, surface.Normal.y, surface.Normal.z), 8.00000);
                accumulatedCaptureDiffuse = float3(
                    (((log2((max(1.00000e-09, ((reflectionCaptureViewSharedTrilinearClampedSamplerData[0]) * reflectionCaptureExponentialValue))))) * reflectionCaptureInfluence) + captureDiffuse.x),
                    (((log2((max(1.00000e-09, ((reflectionCaptureViewSharedTrilinearClampedSamplerData[1]) * reflectionCaptureExponentialValue))))) * reflectionCaptureInfluence) + captureDiffuse.y),
                    (((log2((max(1.00000e-09, ((reflectionCaptureViewSharedTrilinearClampedSamplerData[2]) * reflectionCaptureExponentialValue))))) * reflectionCaptureInfluence) + captureDiffuse.z));
                accumulatedCaptureSpecular = float3(
                    (((log2((max(1.00000e-09, ((captureSample2[0]) * reflectionCaptureExponentialValue))))) * reflectionCaptureInfluence) + captureSpecular.x),
                    (((log2((max(1.00000e-09, ((captureSample2[1]) * reflectionCaptureExponentialValue))))) * reflectionCaptureInfluence) + captureSpecular.y),
                    (((log2((max(1.00000e-09, ((captureSample2[2]) * reflectionCaptureExponentialValue))))) * reflectionCaptureInfluence) + captureSpecular.z));
                accumulatedCaptureSecondaryDiffuse = float3(
                    (((log2((max(1.00000e-09, ((captureSample3[0]) * reflectionCaptureExponentialValue))))) * reflectionCaptureInfluence) + captureSecondaryDiffuse.x),
                    (((log2((max(1.00000e-09, ((captureSample3[1]) * reflectionCaptureExponentialValue))))) * reflectionCaptureInfluence) + captureSecondaryDiffuse.y),
                    (((log2((max(1.00000e-09, ((captureSample3[2]) * reflectionCaptureExponentialValue))))) * reflectionCaptureInfluence) + captureSecondaryDiffuse.z));
                accumulatedCaptureSecondarySpecular = float3(
                    (((log2((max(1.00000e-09, ((captureSample4[0]) * reflectionCaptureExponentialValue))))) * reflectionCaptureInfluence) + captureSecondarySpecular.x),
                    (((log2((max(1.00000e-09, ((captureSample4[1]) * reflectionCaptureExponentialValue))))) * reflectionCaptureInfluence) + captureSecondarySpecular.y),
                    (((log2((max(1.00000e-09, ((captureSample4[2]) * reflectionCaptureExponentialValue))))) * reflectionCaptureInfluence) + captureSecondarySpecular.z));
                captureTerm1 = (reflectionCaptureInfluence + captureTerm0);
            }
            else
            {
                accumulatedCaptureDiffuse = float3(captureDiffuse.x, captureDiffuse.y, captureDiffuse.z);
                accumulatedCaptureSpecular = float3(captureSpecular.x, captureSpecular.y, captureSpecular.z);
                accumulatedCaptureSecondaryDiffuse = float3(captureSecondaryDiffuse.x, captureSecondaryDiffuse.y, captureSecondaryDiffuse.z);
                accumulatedCaptureSecondarySpecular = float3(captureSecondarySpecular.x, captureSecondarySpecular.y, captureSecondarySpecular.z);
                captureTerm1 = captureTerm0;
            }

            int captureIndex5 = reflectionCaptureLoopIndex + 1;
            bool captureTest2 = (captureIndex5 == reflectionCaptureMinimumValue);

            if (captureTest2)
            {
                break;
            }
            else
            {
                captureDiffuse = float3(accumulatedCaptureDiffuse.x, accumulatedCaptureDiffuse.y, accumulatedCaptureDiffuse.z);
                captureSpecular = float3(accumulatedCaptureSpecular.x, accumulatedCaptureSpecular.y, accumulatedCaptureSpecular.z);
                captureSecondaryDiffuse = float3(accumulatedCaptureSecondaryDiffuse.x, accumulatedCaptureSecondaryDiffuse.y, accumulatedCaptureSecondaryDiffuse.z);
                captureSecondarySpecular = float3(accumulatedCaptureSecondarySpecular.x, accumulatedCaptureSecondarySpecular.y, accumulatedCaptureSecondarySpecular.z);
                captureTerm0 = captureTerm1;
                reflectionCaptureLoopIndex = captureIndex5;
                continue;
            }
        }

        bool captureTest3 = (captureTerm1 > 0.000100000);

        if (captureTest3)
        {
            float reflectionCaptureRatio = 1.00000 / captureTerm1;
            indirectDiffuseLighting = float3(
                (exp2((reflectionCaptureRatio * accumulatedCaptureDiffuse.x))),
                (exp2((reflectionCaptureRatio * accumulatedCaptureDiffuse.y))),
                (exp2((reflectionCaptureRatio * accumulatedCaptureDiffuse.z))));
            indirectSpecularLighting = float3(
                (exp2((reflectionCaptureRatio * accumulatedCaptureSpecular.x))),
                (exp2((reflectionCaptureRatio * accumulatedCaptureSpecular.y))),
                (exp2((reflectionCaptureRatio * accumulatedCaptureSpecular.z))));
            indirectSecondaryDiffuse = float3(
                (exp2((reflectionCaptureRatio * accumulatedCaptureSecondaryDiffuse.x))),
                (exp2((reflectionCaptureRatio * accumulatedCaptureSecondaryDiffuse.y))),
                (exp2((reflectionCaptureRatio * accumulatedCaptureSecondaryDiffuse.z))));
            indirectSecondarySpecular = float3(
                (exp2((reflectionCaptureRatio * accumulatedCaptureSecondarySpecular.x))),
                (exp2((reflectionCaptureRatio * accumulatedCaptureSecondarySpecular.y))),
                (exp2((reflectionCaptureRatio * accumulatedCaptureSecondarySpecular.z))));
        }
        else
        {
            indirectDiffuseLighting = float3(accumulatedCaptureDiffuse.x, accumulatedCaptureDiffuse.y, accumulatedCaptureDiffuse.z);
            indirectSpecularLighting = float3(accumulatedCaptureSpecular.x, accumulatedCaptureSpecular.y, accumulatedCaptureSpecular.z);
            indirectSecondaryDiffuse = float3(accumulatedCaptureSecondaryDiffuse.x, accumulatedCaptureSecondaryDiffuse.y, accumulatedCaptureSecondaryDiffuse.z);
            indirectSecondarySpecular = float3(accumulatedCaptureSecondarySpecular.x, accumulatedCaptureSecondarySpecular.y, accumulatedCaptureSecondarySpecular.z);
        }
    }


    ReflectionCaptureLighting result;
    result.Diffuse = indirectDiffuseLighting;
    result.Specular = indirectSpecularLighting;
    result.SecondaryDiffuse = indirectSecondaryDiffuse;
    result.SecondarySpecular = indirectSecondarySpecular;
    return result;
}

struct DirectionalLightingResult
{
    LightingComponents Lighting;
    float Visibility;
};

DirectionalLightingResult EvaluateDirectionalLighting(
    SurfaceState surface,
    float pixelDepth,
    float normalViewDot,
    float clampedNormalViewDot,
    float minimumRoughness,
    float dielectricF0,
    float3 reflectionBasis,
    float4 blueNoiseSample,
    bool ignoreViewLightModifier)
{
    bool surfaceTest4 = ignoreViewLightModifier;
    float surfaceTerm19 = clampedNormalViewDot;
    float surfaceTerm85 = minimumRoughness;
    float materialValue[4];
    float directionalLightVisibility;
    float shadowTerm6;
    int fogContextScanIndex;
    float fogValue;
    int selectedFogContextIndex;
    float fogContextWeight;
    float3 directionalDiffuse;
    float3 directionalTransmission;
    float3 directionalSubsurface;
    float3 directionalSpecular;

    bool surfaceTest10 = (TranslucentBasePass_Shared_Forward_HasDirectionalLight == 0);
    float4 directionalShadowTranslucentbasepassShared_Forward_DirectionalLightColorData = float4(TranslucentBasePass_Shared_Forward_DirectionalLightColor.x, TranslucentBasePass_Shared_Forward_DirectionalLightColor.y, TranslucentBasePass_Shared_Forward_DirectionalLightColor.z, PrePadding_TranslucentBasePass_Shared_Forward_76);
    float4 directionalShadowTranslucentbasepassShared_Forward_DirectionalLightDirectionData = float4(TranslucentBasePass_Shared_Forward_DirectionalLightDirection.x, TranslucentBasePass_Shared_Forward_DirectionalLightDirection.y, TranslucentBasePass_Shared_Forward_DirectionalLightDirection.z, PrePadding_TranslucentBasePass_Shared_Forward_92);
    float4 shadowVectorTerm0 = float4(TranslucentBasePass_Shared_Forward_DirectionalLightDistanceFadeMAD.x, TranslucentBasePass_Shared_Forward_DirectionalLightDistanceFadeMAD.y, TranslucentBasePass_Shared_Forward_NumDirectionalLightCascades, PrePadding_TranslucentBasePass_Shared_Forward_124);

    if (surfaceTest10)
    {
        directionalDiffuse = float3(0.0, 0.0, 0.0);
        directionalTransmission = float3(0.0, 0.0, 0.0);
        directionalSubsurface = float3(0.0, 0.0, 0.0);
        directionalSpecular = float3(0.0, 0.0, 0.0);
    }
    else
    {
        directionalLightVisibility = EvaluateDirectionalLightVisibility(surface, pixelDepth, blueNoiseSample);

        if (surfaceTest4)
        {
            shadowTerm6 = 1.00000;
        }
        else
        {
            float4 directionalShadowViewLightModifierEnvironmentLightData = float4(View_LightModifierEnvironmentLight, View_LightModifierDirectionalLight, View_MotionBlurNormalizedToPixel, View_bSubsurfacePostprocessEnabled);

            shadowTerm6 = (directionalShadowViewLightModifierEnvironmentLightData[1]);
        }

        int4 fogTerm0 = float4(View_FogContextMediumIOR, View_NumberOfFogContext, PrePadding_View_3528, PrePadding_View_3532);
        bool fogTest0 = (fogTerm0.y != 1);
        int fogIndex0 = (int)(fogTest0);
        bool fogTest1 = (fogTerm0.y > 1);

        if (fogTest1)
        {
            materialValue[0] = 0.000000;
            materialValue[1] = 0.000000;
            materialValue[2] = 0.000000;
            materialValue[3] = 0.000000;

            fogContextScanIndex = 0;

            // Fog-context sphere intersection scan
            while (true)
            {
                int4 fogTerm1 = float4(View_FogContextMediumIOR, View_NumberOfFogContext, PrePadding_View_3528, PrePadding_View_3532);
                bool fogTest2 = (fogContextScanIndex < fogTerm1.y);

                if (fogTest2)
                {
                    float4 fogViewFogContextRegionContextData = View_FogContextRegionContext[fogContextScanIndex];
                    float4 fogViewWorldCameraOriginData = float4(View_WorldCameraOrigin.x, View_WorldCameraOrigin.y, View_WorldCameraOrigin.z, PrePadding_View_1212);
                    float fogTerm2 = (fogViewFogContextRegionContextData[0]) - (fogViewWorldCameraOriginData[0]);
                    float fogTerm3 = (fogViewFogContextRegionContextData[1]) - (fogViewWorldCameraOriginData[1]);
                    float fogTerm4 = (fogViewFogContextRegionContextData[2]) - (fogViewWorldCameraOriginData[2]);
                    float fogTerm5 = dot(float3(fogTerm2, fogTerm3, fogTerm4), float3(surface.CameraToPixelDirection.x, surface.CameraToPixelDirection.y, surface.CameraToPixelDirection.z));
                    float fogTerm6 = dot(float3(fogTerm2, fogTerm3, fogTerm4), float3(fogTerm2, fogTerm3, fogTerm4));
                    float fogTerm7 = fogTerm6 - (fogViewFogContextRegionContextData.w * fogViewFogContextRegionContextData.w);
                    float fogTerm8 = (fogTerm5 * fogTerm5) - fogTerm7;
                    bool fogTest3 = (fogTerm7 < 0.000000);
                    bool fogTest4 = (isnan(fogTerm8) || isnan(0.000000) || (fogTerm8 >= 0.000000));
                    bool fogTest5 = fogTest3 & fogTest4;

                    if (fogTest5)
                    {
                        float fogTerm9 = sqrt(fogTerm8);
                        fogValue = (min((abs((fogTerm9 + fogTerm5))), (abs((fogTerm5 - fogTerm9)))));
                    }
                    else
                    {
                        fogValue = 0.000000;
                    }

                    materialValue[fogContextScanIndex] = fogValue;
                }

                int fogIndex1 = fogContextScanIndex + 1;
                bool fogTest6 = (fogIndex1 == 4);

                if (fogTest6)
                    break;
                else
                {
                    fogContextScanIndex = fogIndex1;
                    continue;
                }
            }

            float fogTerm10 = dot(float4(materialValue[0], materialValue[1], materialValue[2], materialValue[3]), float4(1.00000, 1.00000, 1.00000, 1.00000));
            float fogTerm11 = (blueNoiseSample.x * 0.999900) * fogTerm10;
            bool fogTest7 = (materialValue[0] > fogTerm11);

            if (fogTest7)
            {
                selectedFogContextIndex = 0;
            }
            else
            {
                float fogTerm12 = materialValue[1] + materialValue[0];
                bool fogTest8 = (fogTerm12 > fogTerm11);

                if (fogTest8)
                {
                    selectedFogContextIndex = 1;
                }
                else
                {
                    float outputCombinedValue = fogTerm12 + materialValue[2];
                    bool outputTest0 = (outputCombinedValue > fogTerm11);

                    if (outputTest0)
                    {
                        selectedFogContextIndex = 2;
                    }
                    else
                    {
                        bool outputTest1 = ((outputCombinedValue + materialValue[3]) > fogTerm11);

                        if (outputTest1)
                            selectedFogContextIndex = 3;
                        else
                            selectedFogContextIndex = fogIndex0;
                    }
                }
            }
        }
        else
        {
            selectedFogContextIndex = fogIndex0;
        }

        bool fogTest9 = (selectedFogContextIndex == -1);

        if (fogTest9)
        {
            fogContextWeight = shadowTerm6;
        }
        else
        {
            float4 fogViewFogContextLightContextData = View_FogContextLightContext[selectedFogContextIndex];

            fogContextWeight = ((fogViewFogContextLightContextData[0]) * shadowTerm6);
        }

        bool fogTest10 = (fogContextWeight > 0.000000);

        if (fogTest10)
        {
            float fogTerm13 = saturate(((shadowVectorTerm0.x * pixelDepth) + shadowVectorTerm0.y));
            float fogTerm14 = ((fogTerm13 * fogTerm13) * (1.00000 - directionalLightVisibility)) + directionalLightVisibility;
            bool fogTest11 = (fogTerm14 > 0.000000);

            if (fogTest11)
            {
                float fogTerm15 = dot(float3(surface.Normal.x, surface.Normal.y, surface.Normal.z), float3(directionalShadowTranslucentbasepassShared_Forward_DirectionalLightDirectionData.x, directionalShadowTranslucentbasepassShared_Forward_DirectionalLightDirectionData.y, directionalShadowTranslucentbasepassShared_Forward_DirectionalLightDirectionData.z));
                float fogTerm16 = max(0.000000, fogTerm15);
                float fogTerm17 = dot(float3((-surface.CameraToPixelDirection.x), (-surface.CameraToPixelDirection.y), (-surface.CameraToPixelDirection.z)), float3(directionalShadowTranslucentbasepassShared_Forward_DirectionalLightDirectionData.x, directionalShadowTranslucentbasepassShared_Forward_DirectionalLightDirectionData.y, directionalShadowTranslucentbasepassShared_Forward_DirectionalLightDirectionData.z));
                float fogTerm18 = rsqrt((max(1.00000e-05, ((fogTerm17 * 2.00000) + 2.00000))));
                float fogTerm19 = saturate((fogTerm18 * (normalViewDot + fogTerm15)));
                float fogTerm20 = sqrt((max(0.000000, (1.00000 - ((1.0 - normalViewDot * normalViewDot) * 0.444444)))));
                float fogTerm21 = (reflectionBasis.x * 0.666667) - (fogTerm20 * surface.Normal.x);
                float fogTerm22 = (reflectionBasis.y * 0.666667) - (fogTerm20 * surface.Normal.y);
                float fogTerm23 = (reflectionBasis.z * 0.666667) - (fogTerm20 * surface.Normal.z);
                float fogTerm24 = dot(float3(fogTerm21, fogTerm22, fogTerm23), float3(fogTerm21, fogTerm22, fogTerm23));
                float fogTerm25 = rsqrt(fogTerm24);
                float fogTerm26 = dot(float3((-0.000000 - (fogTerm25 * fogTerm21)), (-0.000000 - (fogTerm25 * fogTerm22)), (-0.000000 - (fogTerm25 * fogTerm23))), float3(directionalShadowTranslucentbasepassShared_Forward_DirectionalLightDirectionData.x, directionalShadowTranslucentbasepassShared_Forward_DirectionalLightDirectionData.y, directionalShadowTranslucentbasepassShared_Forward_DirectionalLightDirectionData.z));
                float fogTerm27 = surfaceTerm85 * surfaceTerm85;
                float fogTerm28 = fogTerm27 * fogTerm27;
                float fogTerm29 = ((fogTerm19 * fogTerm19) * (fogTerm28 + -1.00000)) + 1.00000;
                float fogTerm30 = (fogTerm16 * 2.00000) * surfaceTerm19;
                float fogTerm31 = 1.00000 - (max(1.00000e-05, (min((fogTerm18 * (fogTerm17 + 1.00000)), 1.00000))));
                float fogTerm32 = fogTerm31 * fogTerm31;
                float fogTerm33 = dot(float3(dielectricF0, dielectricF0, dielectricF0), float3(16.6667, 16.6667, 16.6667));
                float fogTerm34 = saturate(((((min(fogTerm33, 1.00000)) - dielectricF0) * ((fogTerm32 * fogTerm32) * fogTerm31)) + dielectricF0));
                float fogTerm35 = max(0.0400000, surfaceTerm85);
                float fogTerm36 = max(1.00000e-06, (1.00000 - (saturate(((((fogTerm35 * fogTerm16) + 0.0266916) / (fogTerm16 + 0.466495)) * (exp2((((1.00000 / fogTerm35) * fogTerm16) * (log2(fogTerm35))))))))));
                float fogTerm37 = 1.00000 - fogTerm16;
                float fogTerm38 = fogTerm37 * fogTerm37;
                float fogTerm39 = ((fogTerm38 * fogTerm38) * fogTerm37) * (exp2(((log2((((exp2(((fogTerm35 * 4.77030) * (log2(fogTerm16))))) * 2.36651) + 0.0387332))) * fogTerm35)));
                float fogTerm40 = (((1.00000 / fogTerm36) * (1.00000 - fogTerm36)) * dielectricF0) + 1.00000;
                float fogTerm41 = fogTerm40 * (((fogTerm36 - fogTerm39) * dielectricF0) + fogTerm39);
                float fogTerm42 = saturate((1.00000 - (dot(float3(fogTerm41, fogTerm41, fogTerm41), float3(0.333333, 0.333333, 0.333333)))));
                float fogTerm43 = (((fogTerm28 * 0.318310) * fogTerm16) * (1.00000 / (fogTerm29 * fogTerm29))) * (0.500000 / ((fogTerm27 * ((surfaceTerm19 + fogTerm16) - fogTerm30)) + fogTerm30));
                float fogTerm44 = (((fogTerm26 * fogTerm26) + 1.00000) * 0.0323705) * (exp2(((log2((max(0.000000, ((fogTerm26 * 1.20000) + 1.36000))))) * -1.50000)));
                float fogTerm45 = ((fogTerm17 * fogTerm17) + 1.00000) * 0.0596831;
                float fogTerm46 = saturate((fogTerm16 * 1000.00000));
                float fogTerm47 = min(fogTerm14, (fogTerm46 * fogTerm46));
                float fogTerm48 = min(fogTerm14, 1.00000);
                float fogTerm49 = fogContextWeight * directionalShadowTranslucentbasepassShared_Forward_DirectionalLightColorData.x;
                float fogTerm50 = fogContextWeight * directionalShadowTranslucentbasepassShared_Forward_DirectionalLightColorData.y;
                float fogTerm51 = fogContextWeight * directionalShadowTranslucentbasepassShared_Forward_DirectionalLightColorData.z;

                directionalDiffuse = float3(
                    ((fogTerm45 * fogTerm49) * fogTerm48),
                    ((fogTerm45 * fogTerm50) * fogTerm48),
                    ((fogTerm45 * fogTerm51) * fogTerm48));
                directionalTransmission = float3(
                    ((fogTerm44 * fogTerm49) * fogTerm48),
                    ((fogTerm44 * fogTerm50) * fogTerm48),
                    ((fogTerm44 * fogTerm51) * fogTerm48));
                directionalSubsurface = float3(
                    ((((fogTerm43 * fogTerm49) * fogTerm34) * fogTerm40) * fogTerm47),
                    ((((fogTerm43 * fogTerm50) * fogTerm34) * fogTerm40) * fogTerm47),
                    ((((fogTerm43 * fogTerm51) * fogTerm34) * fogTerm40) * fogTerm47));
                directionalSpecular = float3(
                    ((((fogTerm49 * 0.257831) * fogTerm16) * fogTerm42) * fogTerm47),
                    ((((fogTerm50 * 0.257831) * fogTerm16) * fogTerm42) * fogTerm47),
                    ((((fogTerm51 * 0.257831) * fogTerm16) * fogTerm42) * fogTerm47));
            }
            else
            {
                directionalDiffuse = float3(0.0, 0.0, 0.0);
                directionalTransmission = float3(0.0, 0.0, 0.0);
                directionalSubsurface = float3(0.0, 0.0, 0.0);
                directionalSpecular = float3(0.0, 0.0, 0.0);
            }
        }
        else
        {
            directionalDiffuse = float3(0.0, 0.0, 0.0);
            directionalTransmission = float3(0.0, 0.0, 0.0);
            directionalSubsurface = float3(0.0, 0.0, 0.0);
            directionalSpecular = float3(0.0, 0.0, 0.0);
        }
    }


    DirectionalLightingResult result;
    result.Lighting.Diffuse = directionalDiffuse;
    result.Lighting.Transmission = directionalTransmission;
    result.Lighting.Subsurface = directionalSubsurface;
    result.Lighting.Specular = directionalSpecular;
    result.Visibility = directionalLightVisibility;
    return result;
}

float4 EvaluateScreenSpaceReflection(
    SurfaceState surface,
    float pixelDepth,
    float randomOffset)
{
    float4 hzbUvFactorAndInverse = TranslucentBasePass_HZBUvFactorAndInvFactor;

    float3 reflectionRayEndTranslated = mad(surface.ReflectionDirection, pixelDepth, surface.TranslatedWorldPosition);
    float4 reflectionStartClip = mad(surface.TranslatedWorldPosition.z, View_TranslatedWorldToClip[2], mad(surface.TranslatedWorldPosition.y, View_TranslatedWorldToClip[1], surface.TranslatedWorldPosition.x * View_TranslatedWorldToClip[0])) + View_TranslatedWorldToClip[3];
    float4 reflectionEndClip = mad(reflectionRayEndTranslated.z, View_TranslatedWorldToClip[2], mad(reflectionRayEndTranslated.y, View_TranslatedWorldToClip[1], reflectionRayEndTranslated.x * View_TranslatedWorldToClip[0])) + View_TranslatedWorldToClip[3];

    float inverseStartW = rcp(reflectionStartClip.w);
    float inverseEndW = rcp(reflectionEndClip.w);
    float roughnessRayScale = 10.0 / max(1.0e-5, surface.RoughnessSquared);
    float3 reflectionStartNdc = reflectionStartClip.xyz * inverseStartW;
    reflectionStartNdc.z += 0.05 * inverseStartW;
    float3 reflectionEndNdc = reflectionEndClip.xyz * inverseEndW;

    float projectedRayW = View_ViewToClip[0].w * roughnessRayScale + reflectionStartClip.w;
    float2 projectedRayXY = (View_ViewToClip[0].xy * roughnessRayScale + reflectionStartClip.xy) / projectedRayW;
    float projectedRayDepth = mad(pixelDepth, View_ViewToClip[2].z, reflectionStartClip.z) / mad(pixelDepth, View_ViewToClip[2].w, reflectionStartClip.w);

    HZBTraceResult reflectionHit = TraceHierarchicalZ(
        reflectionStartNdc,
        reflectionEndNdc,
        projectedRayXY,
        projectedRayDepth,
        hzbUvFactorAndInverse.xy,
        randomOffset);

    float reflectionHitDeviceZ = reflectionHit.DeviceZ;
    float reflectionHitConfidence = reflectionHit.Confidence;

    float4 screenSpaceReflection = 0.0;
    float2 reflectionHitNdc = float2(saturate(reflectionHit.HZBUv.x * hzbUvFactorAndInverse.z) * 2.0 - 1.0, 1.0 - saturate(reflectionHit.HZBUv.y * hzbUvFactorAndInverse.w) * 2.0);

    if (reflectionHitConfidence > 0.0)
    {
        float3 reflectionHitTranslatedWorld = ReconstructTranslatedWorldFromNdc(reflectionHitNdc, DeviceZToWorldDepth(reflectionHitDeviceZ));
        float3 reflectionHitSeparation = surface.TranslatedWorldPosition - reflectionHitTranslatedWorld;

        float2 correctedHitNdc = clamp(reflectionHitNdc - 2.0 * sign(reflectionHitNdc) * max(0.0, abs(reflectionHitNdc) - 1.0), -1.0, 1.0);

        uint4 sceneColorDimensions;

        TranslucentBasePass_SceneColorCopyTexture.GetDimensions(0, sceneColorDimensions.x, sceneColorDimensions.y, sceneColorDimensions.z);

        float2 sceneColorUv = View_ScreenPositionScaleBias.xy * correctedHitNdc + View_ScreenPositionScaleBias.wz;
        int2 viewportMin = int2(View_ViewRectMin.xy);
        int2 viewportMax = int2(View_ViewRectMin.xy + View_ViewSizeAndInvSize.xy) - 1;
        int2 hitTexel = clamp(int2(float2(sceneColorDimensions.xy) * sceneColorUv), viewportMin, viewportMax);

        float sceneDepth = TranslucentBasePass_SceneTextures_SceneDepthTexture.Load(int3(hitTexel, 0)).x;
        float3 reflectedSceneColor = TranslucentBasePass_SceneColorCopyTexture.Load(int3(hitTexel, 0)).rgb;
        screenSpaceReflection.rgb = View_OneOverPreExposure * min(reflectedSceneColor, 65504.0);
        screenSpaceReflection.a = (float)(uint)(sceneDepth != 0.0) * reflectionHitConfidence * saturate(rcp(max(1.0e-5, surface.RoughnessSquared * surface.RoughnessSquared * dot(reflectionHitSeparation, reflectionHitSeparation))));
    }

    return screenSpaceReflection;
}

struct WaterSurfaceEvaluation
{
    SurfaceState Surface;
    float4 BlueNoise;
    float SceneIntersectionFade;
    bool IgnoreViewLightModifier;
    float NormalViewDot;
    float AbsNormalViewDot;
    float3 ReflectionBasis;
    float ClampedNormalViewDot;
    float MinimumRoughness;
    float DielectricF0;
    float ParticipatingMediumMask;
    int ClusterCellIndex;
    float SurfaceCoverage;
    float OpenWaterMask;
    float ReferenceUnitTransmittance;
    float3 UnitTransmittance;
    float3 TransmissionTint;
    float TransmissionEnergy;
    float SurfaceDensityBlend;
    float ReferencePathTransmittance;
    float3 PathTransmittance;
    float DeepReferenceTransmittance;
    float3 DeepExtinctionLog;
    float AverageFresnelResponse;
    float MultipleScatteringWeight;
    float TransmissionWeight;
    float2 ScreenUv;
    float2 RefractionUv;
    float RefractionReceiverDeviceDepth;
    float RefractionReceiverLinearDepth;
    float3 RefractionReceiverWorldPosition;
    float RefractionWaterDepth;
};

float2 ClampWaterARefractionUv(float2 uv)
{
    float2 uvMin = (View_ViewRectMin.xy + 0.5) * View_BufferSizeAndInvSize.zw;
    float2 uvMax = (View_ViewRectMin.xy + View_ViewSizeAndInvSize.xy - 0.5) * View_BufferSizeAndInvSize.zw;
    return clamp(uv, uvMin, uvMax);
}

int2 WaterARefractionUvToTexel(float2 uv)
{
    int2 viewMin = int2(View_ViewRectMin.xy);
    int2 viewMax = int2(View_ViewRectMin.xy + View_ViewSizeAndInvSize.xy) - 1;
    return clamp(int2(uv * View_BufferSizeAndInvSize.xy), viewMin, viewMax);
}

bool WaterARefractionTexelIsBelowWater(int2 texel, float waterSurfaceWorldZ)
{
    float deviceDepth = TranslucentBasePass_SceneTextures_SceneDepthTexture.Load(int3(texel, 0)).x;

    if (deviceDepth == 0.0)
        return false;

    float2 receiverPixel = float2(texel) + 0.5;
    float3 receiverTranslatedWorldPosition = ReconstructTranslatedWorldPosition(receiverPixel, deviceDepth);
    float3 receiverWorldPosition = receiverTranslatedWorldPosition - View_PreViewTranslation.xyz;
    return receiverWorldPosition.z <= waterSurfaceWorldZ + WATERA_REFRACTION_WATER_PLANE_Z_BIAS;
}

bool WaterARefractionUvFootprintIsBelowWater(float2 uv, float waterSurfaceWorldZ)
{
    uv = ClampWaterARefractionUv(uv);

    float2 texelPosition = uv * View_BufferSizeAndInvSize.xy - 0.5;
    int2 baseTexel = int2(floor(texelPosition));
    int2 viewMin = int2(View_ViewRectMin.xy);
    int2 viewMax = int2(View_ViewRectMin.xy + View_ViewSizeAndInvSize.xy) - 1;

    int2 texel00 = clamp(baseTexel,               viewMin, viewMax);
    int2 texel10 = clamp(baseTexel + int2(1, 0), viewMin, viewMax);
    int2 texel01 = clamp(baseTexel + int2(0, 1), viewMin, viewMax);
    int2 texel11 = clamp(baseTexel + int2(1, 1), viewMin, viewMax);

    return
        WaterARefractionTexelIsBelowWater(texel00, waterSurfaceWorldZ) &&
        WaterARefractionTexelIsBelowWater(texel10, waterSurfaceWorldZ) &&
        WaterARefractionTexelIsBelowWater(texel01, waterSurfaceWorldZ) &&
        WaterARefractionTexelIsBelowWater(texel11, waterSurfaceWorldZ);
}

void GetWaterADepthBlurOffsets(
    float waterDepth,
    float4 blueNoise,
    out float depthBlur,
    out float2 blurOffsetA,
    out float2 blurOffsetB)
{
    depthBlur = saturate(
        (waterDepth - WATERA_DEPTH_BLUR_START_CM) /
        max(WATERA_DEPTH_BLUR_FULL_CM - WATERA_DEPTH_BLUR_START_CM, 1.0));

    float2 blurAxis = blueNoise.xy * 2.0 - 1.0;
    blurAxis *= rsqrt(max(dot(blurAxis, blurAxis), 1.0e-4));
    float2 blurPerpendicular = float2(-blurAxis.y, blurAxis.x);
    float blurRadiusPixels = WATERA_DEPTH_BLUR_MAX_RADIUS_PIXELS * depthBlur;
    blurOffsetA = blurAxis * blurRadiusPixels * View_BufferSizeAndInvSize.zw;
    blurOffsetB = blurPerpendicular * (blurRadiusPixels * 0.73) * View_BufferSizeAndInvSize.zw;
}

bool WaterARefractionCandidateIsSafe(
    float2 refractedUv,
    float2 unrefractedUv,
    float waterSurfaceWorldZ,
    float candidateWaterDepth,
    float4 blueNoise)
{
    bool isSafe = WaterARefractionUvFootprintIsBelowWater(refractedUv, waterSurfaceWorldZ);

#if defined(ENABLE_REFRACTION_CHROMATIC_DISPERSION)
    float2 dispersionOffset = (refractedUv - unrefractedUv) * REFRACTION_DISPERSION_STRENGTH;
    isSafe =
        isSafe &&
        WaterARefractionUvFootprintIsBelowWater(refractedUv + dispersionOffset, waterSurfaceWorldZ) &&
        WaterARefractionUvFootprintIsBelowWater(refractedUv - dispersionOffset, waterSurfaceWorldZ);
#endif

    float depthBlur;
    float2 blurOffsetA;
    float2 blurOffsetB;
    GetWaterADepthBlurOffsets(candidateWaterDepth, blueNoise, depthBlur, blurOffsetA, blurOffsetB);

    if (depthBlur > 0.0)
    {
        isSafe =
            isSafe &&
            WaterARefractionUvFootprintIsBelowWater(refractedUv + blurOffsetA, waterSurfaceWorldZ) &&
            WaterARefractionUvFootprintIsBelowWater(refractedUv - blurOffsetA, waterSurfaceWorldZ) &&
            WaterARefractionUvFootprintIsBelowWater(refractedUv + blurOffsetB, waterSurfaceWorldZ) &&
            WaterARefractionUvFootprintIsBelowWater(refractedUv - blurOffsetB, waterSurfaceWorldZ);
    }

    return isSafe;
}

WaterSurfaceEvaluation EvaluateWaterSurface(InputStruct _IN)
{
    SurfaceState surface = (SurfaceState)0;
    float2 pixelXY = _IN.SV_Position.xy;

#if defined(GAME_VERSION_1_0_0_4)
    float2 baseUv = _IN.TEXCOORD[0];
    float2 detailUv = _IN.TEXCOORD[1];
    float2 secondaryFlowUv = _IN.TEXCOORD[2];
    float2 flowVector = _IN.TEXCOORD[3];
    uint coverageContext = _IN.COVERAGE_CONTEXT;
    uint primitiveIndex = _IN.PRIMITIVE_ID;
#else
    float2 baseUv = _IN.TEXCOORD0.xy;
    float2 detailUv = _IN.TEXCOORD0.zw;
    float2 secondaryFlowUv = _IN.TEXCOORD1.xy;
    float2 flowVector = _IN.TEXCOORD1.zw;
    uint coverageContext = (uint)round(_IN.CONSTANT_CONTEXT.x);
    uint primitiveIndex = (uint)round(_IN.CONSTANT_CONTEXT.y);
#endif

    float materialBias = View_TemporalSamplerBias.y;

    surface.MaterialUV = baseUv;
    surface.TranslatedWorldPosition = ReconstructTranslatedWorldPosition(pixelXY, _IN.SV_Position.z);
    surface.WorldPosition = surface.TranslatedWorldPosition - View_PreViewTranslation.xyz;
    surface.CameraToPixelDirection = SafeNormalize(surface.TranslatedWorldPosition);

    int noiseSlice = ((int)coverageContext + (int)View_StateFrameIndex) & 63;
    int3 blueNoiseTexel = int3(int2(pixelXY) & 127, noiseSlice);
    float4 blueNoiseSample = View_SpatiotemporalBlueNoiseVolumeTexture.Load(int4(blueNoiseTexel, 0));

    float4 primitiveReserved = View_PrimitiveSceneData[primitiveIndex * 28u + 27u];
    float dynamicCoverageThreshold = blueNoiseSample.a * 0.996094 + 0.00195313;

    if (primitiveReserved.w <= dynamicCoverageThreshold)
        discard;

    int primitivePixelAttribute = asint(primitiveReserved.z);
    bool ignoreViewLightModifier = (primitivePixelAttribute & 2) == 0;

    float flowPhaseTexture = Material_Texture2D_16.SampleBias(View_SharedAnisotropic4XWrappedSampler, detailUv, materialBias).r;
    float flowScaleTexture = Material_Texture2D_17.SampleBias(View_SharedAnisotropic4XWrappedSampler, detailUv, materialBias).r;

    //slow speed down a bit
	flowScaleTexture *= 0.5f;

    float phaseA = frac(flowPhaseTexture + Material_VectorExpressions[15].z * Material_VectorExpressions[16].x);
    float phaseB = frac(flowPhaseTexture + Material_VectorExpressions[15].z * Material_VectorExpressions[17].x);
    float2 flowVectorA = flowScaleTexture * Material_VectorExpressions[16].y * flowVector;
    float2 flowVectorB = flowScaleTexture * Material_VectorExpressions[17].y * flowVector;
    float2 flowUvA0 = baseUv - phaseA * flowVectorA;
    float2 flowUvA1 = baseUv + (1.0 - phaseA) * flowVectorA;
    float2 flowUvB0 = secondaryFlowUv - phaseB * flowVectorB;
    float2 flowUvB1 = secondaryFlowUv + (1.0 - phaseB) * flowVectorB;
    float flowBlendA = phaseA * phaseA * (3.0 - 2.0 * phaseA);
    float flowBlendB = phaseB * phaseB * (3.0 - 2.0 * phaseB);

    //bump up normals because water often looks too sparse
	flowUvA0 *= 12;
	flowUvA1 *= 12;
    flowUvB0 *= 6;
	flowUvB1 *= 6;

    float2 normalEncodedA0 = Material_Texture2D_2.SampleBias(View_SharedAnisotropic4XWrappedSampler, flowUvA0, materialBias).rg;
    float2 normalEncodedA1 = Material_Texture2D_2.SampleBias(View_SharedAnisotropic4XWrappedSampler, flowUvA1, materialBias).rg;
    float2 normalXYA = lerp(normalEncodedA0, normalEncodedA1, flowBlendA) * 2.0 - 1.0;

    float3 tangentNormalA = SafeNormalize(float3(normalXYA, sqrt(max(0.0, 1.0 - dot(normalXYA, normalXYA)))));

	//tangentNormalA += View_SpatiotemporalBlueNoiseVolumeTexture.SampleLevel(View_SharedBilinearWrappedSampler, surface.WorldPosition.xyz * 2, 0).r;
	//tangentNormalA = normalize(tangentNormalA);

    float2 normalEncodedB0 = Material_Texture2D_20.SampleBias(View_SharedAnisotropic4XWrappedSampler, flowUvB0, materialBias).rg;
    float2 normalEncodedB1 = Material_Texture2D_20.SampleBias(View_SharedAnisotropic4XWrappedSampler, flowUvB1, materialBias).rg;
    float2 normalXYB = lerp(normalEncodedB0, normalEncodedB1, flowBlendB) * 2.0 - 1.0;
    float3 tangentNormalB = SafeNormalize(float3(normalXYB, sqrt(max(0.0, 1.0 - dot(normalXYB, normalXYB)))));

    float secondaryNormalMask = saturate(Material_Texture2D_7.SampleBias(View_SharedAnisotropic4XWrappedSampler, baseUv, materialBias).r);
    float3 reorientedNormal = SafeNormalize(float3(tangentNormalA.xy + tangentNormalB.xy, max(1.0e-4, tangentNormalA.z * tangentNormalB.z)));
    float3 tangentNormal = SafeNormalize(lerp(tangentNormalA, reorientedNormal, secondaryNormalMask));

    float2 enhancedNormalXY = tangentNormal.xy * WATERA_WAVE_NORMAL_STRENGTH;
    float enhancedNormalLengthSq = dot(enhancedNormalXY, enhancedNormalXY);
    enhancedNormalXY *= min(1.0, sqrt(0.98 / max(enhancedNormalLengthSq, 1.0e-6)));
    tangentNormal = float3(enhancedNormalXY, sqrt(max(0.0, 1.0 - dot(enhancedNormalXY, enhancedNormalXY))));

    float3 tangentAxis = SafeNormalize(_IN.TEXCOORD10.xyz);
    float3 geometricNormal = SafeNormalize(_IN.TEXCOORD11.xyz);
    float3 bitangentAxis = SafeNormalize(-cross(tangentAxis, geometricNormal) * _IN.TEXCOORD11.w);
    surface.Normal = SafeNormalize(tangentAxis * tangentNormal.x + bitangentAxis * tangentNormal.y + geometricNormal * tangentNormal.z);

    float roughnessA0 = Material_Texture2D_3.SampleBias(View_SharedAnisotropic4XWrappedSampler, flowUvA0, materialBias).r;
    float roughnessA1 = Material_Texture2D_3.SampleBias(View_SharedAnisotropic4XWrappedSampler, flowUvA1, materialBias).r;
    float roughnessB0 = Material_Texture2D_21.SampleBias(View_SharedAnisotropic4XWrappedSampler, flowUvB0, materialBias).r;
    float roughnessB1 = Material_Texture2D_21.SampleBias(View_SharedAnisotropic4XWrappedSampler, flowUvB1, materialBias).r;
    float roughnessA = sqrt(saturate(lerp(roughnessA0 * roughnessA0, roughnessA1 * roughnessA1, flowBlendA)));
    float roughnessB = sqrt(saturate(lerp(roughnessB0 * roughnessB0, roughnessB1 * roughnessB1, flowBlendB)));
    float materialRoughness = saturate(lerp(roughnessA, sqrt(saturate(roughnessA * roughnessA + roughnessB * roughnessB)), secondaryNormalMask));

    float foamA0 = Material_Texture2D_15.SampleBias(View_SharedAnisotropic4XWrappedSampler, flowUvA0, materialBias).r;
    float foamA1 = Material_Texture2D_15.SampleBias(View_SharedAnisotropic4XWrappedSampler, flowUvA1, materialBias).r;
    float foamB0 = Material_Texture2D_23.SampleBias(View_SharedAnisotropic4XWrappedSampler, flowUvB0, materialBias).r;
    float foamB1 = Material_Texture2D_23.SampleBias(View_SharedAnisotropic4XWrappedSampler, flowUvB1, materialBias).r;
    float foamA = saturate(lerp(foamA0, foamA1, flowBlendA));
    float foamB = saturate(lerp(foamB0, foamB1, flowBlendB));
    float foamGate = saturate(Material_Texture2D_18.SampleBias(View_SharedAnisotropic4XWrappedSampler, detailUv, materialBias).r);
    float surfaceCoverage = saturate((foamA + (1.0 - foamA) * foamB * secondaryNormalMask) * foamGate);

    float3 normalDx = ddx_fine(surface.Normal);
    float3 normalDy = ddy_fine(surface.Normal);
    float normalVariance = dot(normalDx, normalDx) + dot(normalDy, normalDy);
    surface.FilteredRoughness = sqrt(saturate(min(normalVariance * 0.159155, 0.18) + materialRoughness * materialRoughness));

    bool hasParticipatingMedium = View_FogContextMediumIOR > 1.0;
    float participatingMediumMask = saturate((float)(uint)hasParticipatingMedium);
    float dielectricF0 = 0.0203732 - participatingMediumMask * 0.0203569;

    float3 viewDirection = -surface.CameraToPixelDirection;
    float normalViewDot = dot(surface.Normal, viewDirection);
    float absoluteNormalViewDot = abs(normalViewDot);
    float clampedNormalViewDot = max(1.0e-5, absoluteNormalViewDot);
    float3 reflectionBasis = normalViewDot * surface.Normal + surface.CameraToPixelDirection;
    float3 reflectedDirection = reflect(-viewDirection, surface.Normal);
    float transmittedCosine = sqrt(max(0.0, 1.0 - (1.0 - normalViewDot * normalViewDot) * 0.562781));
    float3 refractedDirection = SafeNormalize(0.750188 * reflectionBasis - transmittedCosine * surface.Normal);

    surface.RoughnessSquared = surface.FilteredRoughness * surface.FilteredRoughness;
    surface.RoughnessCubed = surface.RoughnessSquared * surface.FilteredRoughness;
    surface.ReflectionVector = reflectedDirection;
    surface.ReflectionDirection = SafeNormalize(reflectedDirection);
    surface.TransmissionDirection = SafeNormalize(lerp(refractedDirection, -surface.Normal, surface.RoughnessCubed));

    float2 screenUv = pixelXY * View_BufferSizeAndInvSize.zw;
    int2 sourceTexel = int2(pixelXY);
    float sourceDeviceDepth = TranslucentBasePass_SceneTextures_SceneDepthTexture.Load(int3(sourceTexel, 0)).x;
    float sourceLinearDepth = DeviceZToWorldDepth(sourceDeviceDepth);
    float sourceWaterDepth = max(0.0, sourceLinearDepth - _IN.SV_Position.w);
    float3 refractedView = mul(float4(refractedDirection, 0.0), View_TranslatedWorldToView).xyz;
    float refractionStrength = saturate(sourceWaterDepth * 0.02) * (1.0 - 0.65 * surface.FilteredRoughness);
    float2 refractionUv = screenUv + refractedView.xy * (0.025 * WATERA_REFRACTION_STRENGTH * refractionStrength / max(abs(refractedView.z), 0.25));

    float depthNoiseFade = saturate(sourceWaterDepth * WATERA_REFRACTION_NOISE_DEPTH_SCALE);
    float2 refractionNoisePosition = surface.WorldPosition.xy * 0.02 + float2(View_RealTime * 0.37, -View_RealTime * 0.29);
    float refractionNoiseX = WrappedValueNoise2D(refractionNoisePosition);
    float refractionNoiseY = WrappedValueNoise2D(RotateDensityOctave(refractionNoisePosition * 1.73 + float2(37.17, 91.73)));
    float2 coherentRefractionNoise = float2(refractionNoiseX, refractionNoiseY) * 2.0 - 1.0;
    coherentRefractionNoise = lerp(coherentRefractionNoise, tangentNormal.xy, 0.38);
    refractionUv += coherentRefractionNoise * WATERA_REFRACTION_NOISE_STRENGTH * lerp(0.35, 1.0, depthNoiseFade);

    float jitterPixels = lerp(1.0, 5.0, depthNoiseFade) * refractionStrength;
    refractionUv += (blueNoiseSample.xy - 0.5) * View_BufferSizeAndInvSize.zw * jitterPixels;

    refractionUv = ClampWaterARefractionUv(refractionUv);

    int2 candidateReceiverTexel = WaterARefractionUvToTexel(refractionUv);
    float candidateReceiverDeviceDepth = TranslucentBasePass_SceneTextures_SceneDepthTexture.Load(int3(candidateReceiverTexel, 0)).x;
    float candidateReceiverLinearDepth = DeviceZToWorldDepth(candidateReceiverDeviceDepth);
    float3 candidateReceiverWorldPosition = surface.WorldPosition;

    if (candidateReceiverDeviceDepth != 0.0)
    {
        candidateReceiverWorldPosition = ReconstructTranslatedWorldPosition(float2(candidateReceiverTexel) + 0.5, candidateReceiverDeviceDepth) - View_PreViewTranslation.xyz;
    }

    bool candidateHasReceiver = candidateReceiverDeviceDepth != 0.0 && candidateReceiverLinearDepth > _IN.SV_Position.w && candidateReceiverWorldPosition.z <= surface.WorldPosition.z + WATERA_REFRACTION_WATER_PLANE_Z_BIAS;
    float candidateWaterDepth = candidateHasReceiver ? max(candidateReceiverLinearDepth - _IN.SV_Position.w, 0.0) : 0.0;

    bool useRefractedUv = WaterARefractionCandidateIsSafe(refractionUv, screenUv, surface.WorldPosition.z, candidateWaterDepth, blueNoiseSample);

    refractionUv = useRefractedUv ? refractionUv : screenUv;

    //fog, extinction, caustics, and final scene color all use the resolved UV.
    int2 receiverTexel = WaterARefractionUvToTexel(refractionUv);
    float receiverDeviceDepth = TranslucentBasePass_SceneTextures_SceneDepthTexture.Load(int3(receiverTexel, 0)).x;
    float receiverLinearDepth = DeviceZToWorldDepth(receiverDeviceDepth);
    float3 receiverWorldPosition = surface.WorldPosition;

    if (receiverDeviceDepth != 0.0)
        receiverWorldPosition = ReconstructTranslatedWorldPosition(float2(receiverTexel) + 0.5, receiverDeviceDepth) - View_PreViewTranslation.xyz;

    bool hasReceiver = receiverDeviceDepth != 0.0 && receiverLinearDepth > _IN.SV_Position.w && receiverWorldPosition.z <= surface.WorldPosition.z + WATERA_REFRACTION_WATER_PLANE_Z_BIAS;
    float receiverWaterDepth = hasReceiver ? min(max(receiverLinearDepth - _IN.SV_Position.w, 0.0), 6.55040e+06) : 0.0;

    float3 transmissionTint = Material_Texture2D_25.SampleBias(View_SharedAnisotropic4XClampedSampler, baseUv, materialBias).rgb;
    float referenceSample = saturate(Material_Texture2D_26.SampleBias(View_SharedAnisotropic4XClampedSampler, baseUv, materialBias).r);
    float referenceExponent = rcp(max(1.0e-6, Material_Texture2D_27.SampleBias(View_SharedAnisotropic4XClampedSampler, baseUv, materialBias).r * 100.0));
    float referenceUnitTransmittance = exp2(log2(max(referenceSample, 1.0e-6)) * referenceExponent);
    float3 unitSample = saturate(Material_Texture2D_28.SampleBias(View_SharedAnisotropic4XClampedSampler, baseUv, materialBias).rgb);
    float spectralExponent = rcp(max(1.0e-6, Material_Texture2D_29.SampleBias(View_SharedAnisotropic4XClampedSampler, baseUv, materialBias).r * 100.0));
    float3 unitTransmittance = exp2(log2(max(unitSample, 1.0e-6)) * spectralExponent);

    float rayThickness = max(0.001, receiverWaterDepth);
    float verticalThickness = hasReceiver ? max(0.001, min(max(surface.WorldPosition.z - receiverWorldPosition.z, 0.0), 6.55040e+06)) : 0.001;
    float referenceExtinctionLog = log2(max(referenceUnitTransmittance, 1.0e-6));
    float3 extinctionLog = log2(max(unitTransmittance, 1.0e-6));
    float referencePathTransmittance = exp2(referenceExtinctionLog * rayThickness);
    float3 pathTransmittance = exp2(extinctionLog * rayThickness);
    float deepReferenceTransmittance = exp2(referenceExtinctionLog * verticalThickness);
    float3 deepExtinctionLog = extinctionLog * verticalThickness;

    float oneMinusRoughness = 1.0 - surface.FilteredRoughness;
    float fresnelResponse = dielectricF0 + (1.0 - dielectricF0) * pow(1.0 - clampedNormalViewDot, 5.0);
    float transmissionEnergy = saturate((1.0 - fresnelResponse) * (0.75 + 0.25 * oneMinusRoughness));
    float multipleScatteringWeight = saturate(fresnelResponse + surface.FilteredRoughness * 0.15);
    float transmissionWeight = saturate((1.0 - fresnelResponse) * (1.0 - 0.5 * surface.FilteredRoughness));
    float minimumRoughness = max(0.04, surface.FilteredRoughness);

    int2 gridPixel = int2(View_LightProbeSizeRatioAndInvSizeRatio.zw * pixelXY - View_ViewRectMin.xy);
    float gridDepth = max(0.0, log2(max(TranslucentBasePass_Shared_Forward_LightGridZParams.x * _IN.SV_Position.w + TranslucentBasePass_Shared_Forward_LightGridZParams.y, 1.0e-6)) * TranslucentBasePass_Shared_Forward_LightGridZParams.z);
    int gridZ = min((int)gridDepth, TranslucentBasePass_Shared_Forward_CulledGridSize.z - 1);
    int gridShift = TranslucentBasePass_Shared_Forward_LightGridPixelSizeShift & 31;
    int clusterCellIndex = ((gridZ * TranslucentBasePass_Shared_Forward_CulledGridSize.y + (gridPixel.y >> gridShift)) * TranslucentBasePass_Shared_Forward_CulledGridSize.x) + (gridPixel.x >> gridShift);

    WaterSurfaceEvaluation result = (WaterSurfaceEvaluation)0;
    result.Surface = surface;
    result.BlueNoise = blueNoiseSample;
    result.SceneIntersectionFade = hasReceiver ? saturate(receiverWaterDepth * 0.02) : 0.0;
    result.IgnoreViewLightModifier = ignoreViewLightModifier;
    result.NormalViewDot = normalViewDot;
    result.AbsNormalViewDot = absoluteNormalViewDot;
    result.ReflectionBasis = reflectionBasis;
    result.ClampedNormalViewDot = clampedNormalViewDot;
    result.MinimumRoughness = minimumRoughness;
    result.DielectricF0 = dielectricF0;
    result.ParticipatingMediumMask = participatingMediumMask;
    result.ClusterCellIndex = clusterCellIndex;
    result.SurfaceCoverage = surfaceCoverage;
    result.OpenWaterMask = 1.0 - surfaceCoverage;
    result.ReferenceUnitTransmittance = referenceUnitTransmittance;
    result.UnitTransmittance = unitTransmittance;
    result.TransmissionTint = transmissionTint;
    result.TransmissionEnergy = transmissionEnergy;
    result.SurfaceDensityBlend = saturate(receiverWaterDepth * 0.02) * surfaceCoverage;
    result.ReferencePathTransmittance = referencePathTransmittance;
    result.PathTransmittance = pathTransmittance;
    result.DeepReferenceTransmittance = deepReferenceTransmittance;
    result.DeepExtinctionLog = deepExtinctionLog;
    result.AverageFresnelResponse = fresnelResponse;
    result.MultipleScatteringWeight = multipleScatteringWeight;
    result.TransmissionWeight = transmissionWeight;
    result.ScreenUv = screenUv;
    result.RefractionUv = refractionUv;
    result.RefractionReceiverDeviceDepth = receiverDeviceDepth;
    result.RefractionReceiverLinearDepth = receiverLinearDepth;
    result.RefractionReceiverWorldPosition = receiverWorldPosition;
    result.RefractionWaterDepth = receiverWaterDepth;
    return result;
}

float3 ApplyProjectedWorleyCaustics(
    float intersectionCausticsDepth,
    float3 backgroundSceneColor,
    float receiverDeviceDepth,
    float3 receiverWorldPosition,
    float waterToReceiverDepth,
    float directionalLightVisibility)
{
    // Reuse the exact refracted receiver already sampled for water fog.
    float validReceiver = (float)(uint)(receiverDeviceDepth != 0.0 && waterToReceiverDepth > 0.0);
    receiverWorldPosition += View_RealTime * 35.1;

    float causticsScale = CAUSTICS_WORLD_SCALE;
    //causticsScale -= saturate(intersectionCausticsDepth * 0.001f) * 0.001f;

    float3 causticLightDirection = SafeNormalize(TranslucentBasePass_Shared_Forward_DirectionalLightDirection);
    float3 causticReferenceAxis = abs(causticLightDirection.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(0.0, 1.0, 0.0);
    float3 causticBasisX = SafeNormalize(cross(causticReferenceAxis, causticLightDirection));
    float3 causticBasisY = cross(causticLightDirection, causticBasisX);
    float2 causticProjection = float2(dot(receiverWorldPosition, causticBasisX), dot(receiverWorldPosition, causticBasisY)) * causticsScale;

    // A rotated second layer breaks up the recognizably regular Voronoi cells.
    float2 causticProjection2 = float2(mad(causticProjection.x, 0.798636, -causticProjection.y * 0.601815), mad(causticProjection.x, 0.601815,  causticProjection.y * 0.798636));
    causticProjection2 = causticProjection2 * 1.73 + float2(17.13, 41.71);

    float causticTime = View_RealTime * CAUSTICS_ANIMATION_SPEED;
    float causticEdge1 = WorleyEdgeDistance(causticProjection, causticTime);
    float causticEdge2 = WorleyEdgeDistance(causticProjection2, -causticTime * 0.73);
    float causticAA1 = max(fwidth(causticEdge1), 0.003);
    float causticAA2 = max(fwidth(causticEdge2), 0.003);
    float causticLayer1 = 1.0 - smoothstep(CAUSTICS_EDGE_WIDTH - causticAA1, CAUSTICS_EDGE_WIDTH + causticAA1, causticEdge1);
    float causticLayer2 = 1.0 - smoothstep(CAUSTICS_EDGE_WIDTH * 0.72 - causticAA2, CAUSTICS_EDGE_WIDTH * 0.72 + causticAA2, causticEdge2);
    float causticPattern = saturate(causticLayer1 * 0.78 + causticLayer2 * 0.55 - 0.36);
    causticPattern *= causticPattern;

    // Fade grazing receivers to avoid extreme projection stretching. abs()
    // makes the result independent of reconstructed derivative winding.
    float3 receiverDx = ddx_fine(receiverWorldPosition);
    float3 receiverDy = ddy_fine(receiverWorldPosition);
    float3 receiverNormal = SafeNormalize(cross(receiverDx, receiverDy));
    float receiverFacing = saturate(abs(dot(receiverNormal, causticLightDirection)) * 1.5);
    float causticDepthMask = saturate(waterToReceiverDepth * CAUSTICS_SHALLOW_FADE) * exp2(-waterToReceiverDepth * CAUSTICS_DEPTH_FALLOFF);
    float hasDirectionalLight = (float)(uint)(TranslucentBasePass_Shared_Forward_HasDirectionalLight != 0);
    float causticVisibility = validReceiver * hasDirectionalLight * receiverFacing * causticDepthMask * saturate(directionalLightVisibility);

    //float3 causticRadiance = causticPattern * CAUSTICS_INTENSITY * causticVisibility * directionalShadowTranslucentbasepassShared_Forward_DirectionalLightColorData.xyz * View_OneOverPreExposure;
    float3 causticRadiance = causticPattern * CAUSTICS_INTENSITY * causticVisibility;
    backgroundSceneColor *= 1.0f + causticRadiance;
    return backgroundSceneColor;
}

float3 SampleRefractedSceneColor(float2 refractedUv, float2 unrefractedUv, float waterDepth, float4 blueNoise)
{
    float3 refractedColor;

#if defined(ENABLE_REFRACTION_CHROMATIC_DISPERSION)
    float2 dispersionOffset = (refractedUv - unrefractedUv) * REFRACTION_DISPERSION_STRENGTH;
    float red = TranslucentBasePass_SceneColorCopyTexture.SampleLevel(View_SharedBilinearClampedSampler, ClampWaterARefractionUv(refractedUv + dispersionOffset), 0.0).r;
    float green = TranslucentBasePass_SceneColorCopyTexture.SampleLevel(View_SharedBilinearClampedSampler, ClampWaterARefractionUv(refractedUv), 0.0).g;
    float blue = TranslucentBasePass_SceneColorCopyTexture.SampleLevel(View_SharedBilinearClampedSampler, ClampWaterARefractionUv(refractedUv - dispersionOffset), 0.0).b;
    refractedColor = float3(red, green, blue);
#else
    refractedColor = TranslucentBasePass_SceneColorCopyTexture.SampleLevel(View_SharedBilinearClampedSampler, ClampWaterARefractionUv(refractedUv), 0.0).rgb;
#endif

    //stochastic cross blur
    float depthBlur;
    float2 blurOffsetA;
    float2 blurOffsetB;
    GetWaterADepthBlurOffsets(waterDepth, blueNoise, depthBlur, blurOffsetA, blurOffsetB);

    //native-UV fallback must remain completely untouched: no depth blur.
    bool useResolvedRefraction = any(abs(refractedUv - unrefractedUv) > 1.0e-7);

    if (useResolvedRefraction && depthBlur > 0.0)
    {
        float3 blurCenter = TranslucentBasePass_SceneColorCopyTexture.SampleLevel(View_SharedBilinearClampedSampler, ClampWaterARefractionUv(refractedUv), 0.0).rgb;
        float3 blurA0 = TranslucentBasePass_SceneColorCopyTexture.SampleLevel(View_SharedBilinearClampedSampler, ClampWaterARefractionUv(refractedUv + blurOffsetA), 0.0).rgb;
        float3 blurA1 = TranslucentBasePass_SceneColorCopyTexture.SampleLevel(View_SharedBilinearClampedSampler, ClampWaterARefractionUv(refractedUv - blurOffsetA), 0.0).rgb;
        float3 blurB0 = TranslucentBasePass_SceneColorCopyTexture.SampleLevel(View_SharedBilinearClampedSampler, ClampWaterARefractionUv(refractedUv + blurOffsetB), 0.0).rgb;
        float3 blurB1 = TranslucentBasePass_SceneColorCopyTexture.SampleLevel(View_SharedBilinearClampedSampler, ClampWaterARefractionUv(refractedUv - blurOffsetB), 0.0).rgb;
        float3 depthBlurredColor = blurCenter * 0.28 + (blurA0 + blurA1 + blurB0 + blurB1) * 0.18;

        refractedColor = lerp(refractedColor, depthBlurredColor, depthBlur * 0.88);
    }

    return View_OneOverPreExposure * refractedColor;
}

OutputStruct main(InputStruct _IN)
{
    OutputStruct output = (OutputStruct)0;
    float2 pixelPosition = _IN.SV_Position.xy;

    WaterSurfaceEvaluation material = EvaluateWaterSurface(_IN);
    SurfaceState surface = material.Surface;

    DirectionalLightingResult directional = EvaluateDirectionalLighting(
        surface,
        _IN.SV_Position.w,
        material.NormalViewDot,
        material.ClampedNormalViewDot,
        material.MinimumRoughness,
        material.DielectricF0,
        material.ReflectionBasis,
        material.BlueNoise,
        material.IgnoreViewLightModifier);

    DirectLightingResult direct = EvaluateClusteredLocalLighting(
        surface,
        material.ClusterCellIndex,
        material.NormalViewDot,
        material.AbsNormalViewDot,
        material.ClampedNormalViewDot,
        material.MinimumRoughness,
        material.DielectricF0,
        material.ReflectionBasis,
        material.BlueNoise,
        directional.Lighting);

    float reflectionCaptureWeight = material.ReferencePathTransmittance * material.TransmissionEnergy * (1.0 - (material.SurfaceDensityBlend * material.OpenWaterMask + material.SurfaceCoverage));
    ReflectionCaptureLighting captures = EvaluateReflectionCaptures(surface, material.ClusterCellIndex, reflectionCaptureWeight);

    TranslucencyAmbientDice ambientDice = SampleTranslucencyAmbientDice(surface.WorldPosition);
    float3 ambientColor = View_OneOverPreExposure * ambientDice.BaseColor;
    float ambientLuminance = Luminance(ambientDice.BaseColor);
    float inverseAmbientLuminance = ambientLuminance > 0.0 ? rcp(ambientLuminance) : 0.0;

    float reflectionAmbient = EvaluateAmbientDice(surface.ReflectionDirection, ambientDice.PositiveAxisWeights, ambientDice.NegativeAxisWeights);
    float normalAmbient = EvaluateAmbientDice(surface.Normal, ambientDice.PositiveAxisWeights, ambientDice.NegativeAxisWeights);
    float3 reflectionIrradiance = ambientColor * (reflectionAmbient * inverseAmbientLuminance);
    float3 normalIrradiance = ambientColor * (normalAmbient * inverseAmbientLuminance);

    float indirectDiffuseLuminance = Luminance(captures.Diffuse);
    float indirectSpecularLuminance = Luminance(captures.Specular);
    float reflectionIrradianceLuminance = Luminance(reflectionIrradiance);
    float normalIrradianceLuminance = Luminance(normalIrradiance);
    float secondaryDiffuseLuminance = Luminance(captures.SecondaryDiffuse);
    float secondarySpecularLuminance = Luminance(captures.SecondarySpecular);

    float secondaryDiffuseScale = (secondaryDiffuseLuminance > 0.0 ? rcp(secondaryDiffuseLuminance) : 0.0) * reflectionIrradianceLuminance;
    float secondarySpecularScale = (secondarySpecularLuminance > 0.0 ? rcp(secondarySpecularLuminance) : 0.0) * normalIrradianceLuminance;
    float diffuseScaleError = secondaryDiffuseScale - 1.0;
    float specularScaleError = secondarySpecularScale - 1.0;
    float diffuseEnergyBlend = saturate(diffuseScaleError * diffuseScaleError / (diffuseScaleError * diffuseScaleError + 0.25));
    float specularEnergyBlend = saturate(specularScaleError * specularScaleError / (specularScaleError * specularScaleError + 0.25));

    float3 matchedIndirectDiffuse = reflectionIrradiance * indirectDiffuseLuminance / max(reflectionIrradianceLuminance, 1.0e-30);
    float3 matchedIndirectSpecular = normalIrradiance * indirectSpecularLuminance / max(normalIrradianceLuminance, 1.0e-30);
    float3 balancedIndirectDiffuse = lerp(captures.Diffuse, matchedIndirectDiffuse, diffuseEnergyBlend);
    float3 balancedIndirectSpecular = lerp(captures.Specular, matchedIndirectSpecular, specularEnergyBlend);

    // Reconstruct spectral transmission from the per-channel extinction ratios.
    float3 spectralTransmissionWeight = 0.0;

    if (material.ReferenceUnitTransmittance < 1.0)
    {
        float referenceExtinction = log2(material.ReferenceUnitTransmittance);
        float3 channelExtinction = log2(material.UnitTransmittance);
        spectralTransmissionWeight = material.TransmissionTint / (channelExtinction / referenceExtinction + 1.0);
    }

    float transmissionAmbient = EvaluateAmbientDice(surface.TransmissionDirection, ambientDice.PositiveAxisWeights, ambientDice.NegativeAxisWeights);

    float3 channelAttenuation = saturate(1.0 - material.PathTransmittance * material.ReferencePathTransmittance);
    float3 ambientTransmission = ambientColor + direct.Primary.Transmission + (ambientColor * (transmissionAmbient * inverseAmbientLuminance) - ambientColor) * 0.648;
    float3 transmittedDiffuse = spectralTransmissionWeight * material.TransmissionEnergy * channelAttenuation * ambientTransmission;

    //darken water fog
    //NOTE: while this does improve the look quite a bit, in some areas like gongaga water reactor this ends up destroying the look of the "mako" too much
    //so it's best to leave this off
    //transmittedDiffuse *= lerp(0.15, 1.0, saturate(directional.Visibility));

    float3 baseDiffuse = ambientColor + direct.Primary.Diffuse;
    float3 diffuseLighting = lerp(transmittedDiffuse, baseDiffuse * 0.979627, material.SurfaceDensityBlend) * saturate(material.OpenWaterMask);

    float3 secondarySpecular = balancedIndirectSpecular * secondarySpecularScale + direct.SecondarySpecular;
    float specularStrength = saturate(1.0 - material.AverageFresnelResponse) * 0.81 * max(1.0, 1.00001 - material.ParticipatingMediumMask * 4.57652e-6);
    float3 specularLighting = (specularStrength * secondarySpecular + direct.Primary.Specular) * material.SurfaceCoverage;

    float4 screenSpaceReflection = EvaluateScreenSpaceReflection(surface, _IN.SV_Position.w, material.BlueNoise.x);
    float3 safeReflectionColor = max(1.0e-9, screenSpaceReflection.rgb);
    float3 safeTransmissionColor = max(1.0e-9, balancedIndirectDiffuse * secondaryDiffuseScale);

    //occlude reflections in shadow
    safeTransmissionColor *= lerp(0.15, 1.0, saturate(directional.Visibility));

    //SSR contribution should be very strong, because it's the most accurate reflection data we have access to
    screenSpaceReflection.a = saturate(screenSpaceReflection.a * 1000000);

    float3 blendedTransmissionColor = exp2(lerp(log2(safeTransmissionColor), log2(safeReflectionColor), screenSpaceReflection.a));

    float2 refractionUv = material.RefractionUv;
    float3 backgroundSceneColor = SampleRefractedSceneColor(refractionUv, material.ScreenUv, material.RefractionWaterDepth, material.BlueNoise);

    //backgroundSceneColor = ApplyProjectedWorleyCaustics(backgroundSceneColor, material.RefractionReceiverDeviceDepth, material.RefractionReceiverWorldPosition, material.RefractionWaterDepth, directional.Visibility);

    float causticsSceneDeviceDepth = TranslucentBasePass_SceneTextures_SceneDepthTexture.Load(int3(pixelPosition, 0)).x;
    float causticsSceneLinearDepth = DeviceZToWorldDepth(causticsSceneDeviceDepth);
    float causticsFade = max(0.0, causticsSceneLinearDepth - _IN.SV_Position.w);
    backgroundSceneColor = ApplyProjectedWorleyCaustics(causticsFade, backgroundSceneColor, material.RefractionReceiverDeviceDepth, material.RefractionReceiverWorldPosition, material.RefractionWaterDepth, directional.Visibility);

    float3 mediumTransmittance = saturate(reflectionCaptureWeight * material.PathTransmittance * material.DeepReferenceTransmittance * exp2(material.DeepExtinctionLog));
    float3 refractedSceneColor = backgroundSceneColor * mediumTransmittance * saturate(material.OpenWaterMask);

    float reflectionFresnelWeight = saturate(material.AverageFresnelResponse);
    float3 reflectionContribution = reflectionFresnelWeight * material.MultipleScatteringWeight * (blendedTransmissionColor + direct.SecondaryDiffuse);

    float3 accumulatedColor = specularLighting + direct.Primary.Subsurface + diffuseLighting + refractedSceneColor + reflectionContribution;
    float3 finalColor = View_PreExposure * accumulatedColor;

    //water-surface edge test: intentionally stays at the original pixel and is not a refracted-receiver lookup.
    float sceneDeviceDepth = TranslucentBasePass_SceneTextures_SceneDepthTexture.Load(int3(pixelPosition, 0)).x;
    float sceneLinearDepth = DeviceZToWorldDepth(sceneDeviceDepth);
    float intersectionFade = saturate(max(0.0, sceneLinearDepth - _IN.SV_Position.w) * 0.02);
    float3 originalSceneColor = TranslucentBasePass_SceneColorCopyTexture.SampleLevel(View_SharedBilinearClampedSampler, pixelPosition * View_BufferSizeAndInvSize.zw, 0).rgb;

    finalColor = lerp(originalSceneColor, finalColor, saturate(intersectionFade * 2.0));

    output.SV_Target0 = float4(finalColor, 1.0);
    output.SV_Target1 = float4(1.0, 1.0, 1.0, 0.0);
    return output;
}
