//DeferredDirectionalLightPS.hlsl

//|||||||||||||||||||||||||||||||||| CONFIGURATION - BRDF ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| CONFIGURATION - BRDF ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| CONFIGURATION - BRDF ||||||||||||||||||||||||||||||||||
//here are parameters that are wired up for easy tweakin...

//this is the default (physically inaccurate) shading model for the game
//if you are not a fan of the other shading models just disable them and enable this to return to the original shading
//#define SHADING_DEFAULT_LIT_LAMBERT //<--- original game

//this is a physically accurate shading model, making diffuse materials more "diffuse"-like
//#define SHADING_DEFAULT_LIT_BURLEY

//this is a physically accurate shading model, making diffuse materials more "diffuse"-like
#define SHADING_DEFAULT_LIT_OREN_NAYAR

//|||||||||||||||||||||||||||||||||| CONFIGURATION - MICRO SHADOWS ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| CONFIGURATION - MICRO SHADOWS ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| CONFIGURATION - MICRO SHADOWS ||||||||||||||||||||||||||||||||||
//here are parameters that are wired up for easy tweakin...

//simulates micro-level shadowing on materials (using material ao) super cheap and performant! (from uncharted 4)
//but this can lead to some materials/objects looking much darker than usual.
//if this is not desired you can just disable to revert to (mostly) original shading
#define ENABLE_MICRO_SHADOWS

//applies microshadows to skin, generally shadowing terms should be mostly unified, however this can lead to an overly dark appearance characters in certain conditions and areas
//I would leave it off if you desire characters to stay true to their original game shading
//#define ENABLE_MICRO_SHADOWS_SKIN

//applies microshadows to hair, generally shadowing terms should be mostly unified, however this can lead to an overly dark appearance characters in certain conditions and areas
//I would leave it off if you desire characters to stay true to their original game shading
//#define ENABLE_MICRO_SHADOWS_HAIR

//requested feature for control over strength of shadows, the uncharted 4 micro shadow application at full intensity can be quite strong
//leaving some places a little too dark when in direct sunlight, but here you have control over how strong it is
//RANGE: this should be between [0.0 <---> 1.0]
//DEFAULT: 1
#define MICRO_SHADOWS_STRENGTH 1.0

//|||||||||||||||||||||||||||||||||| CONFIGURATION - SSAO ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| CONFIGURATION - SSAO ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| CONFIGURATION - SSAO ||||||||||||||||||||||||||||||||||
//here are parameters that are wired up for easy tweakin...

//apply SSAO to the direct lighting for more contrast
//NOTE: I don't recomend this, as it's not only physically in-accurate, but can create too much "shadowing" for things that are supposed to be in direct light
//but... it's here if you prefer the look
//#define APPLY_SSAO_IN_DIRECT_LIGHT

//[APPLY_SSAO_IN_DIRECT_LIGHT ONLY!] This only works if APPLY_SSAO_IN_DIRECT_LIGHT is enabled!
//this controls the "contrast" of the SSAO term when applied to direct light
//RANGE: this should be between [0.0 <---> 8.0]
//DEFAULT: 1.0
#define SSAO_POWER 1.0

//[APPLY_SSAO_IN_DIRECT_LIGHT ONLY!] This only works if APPLY_SSAO_IN_DIRECT_LIGHT is enabled!
//this controls the "brightness" of the SSAO term when applied to direct light, I suggest using this in tandem with SSAO_POWER
//RANGE: this should be between [1.0 <---> 8.0]
//DEFAULT: 1.0
#define SSAO_BRIGHTNESS 1.0

//|||||||||||||||||||||||||||||||||| CONFIGURATION - CONTACT SHADOWS ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| CONFIGURATION - CONTACT SHADOWS ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| CONFIGURATION - CONTACT SHADOWS ||||||||||||||||||||||||||||||||||
//here are parameters that are wired up for easy tweakin...

//this changes the noise pattern every frame, which with Temporal Anti-Aliasing (or DLSS or anything related)
//you want to do, that way samples change every frame and results get blended together over time for a better final apperance
#define RANDOM_ANIMATE_NOISE

//less "noisy" pattern, more stable and slightly better performing
//NOTE 1: for animated noise, I recomend this as it's more stable
#define RANDOM_INTERLEAVED_GRADIENT_NOISE

//more "noisy" pattern, less stable and slightly worse perfoming
//NOTE 1: for NON-animated noise, I recomend using this (because it's sample pattern has better distribution across a single frame)
//NOTE 2: game uses this by default
//#define RANDOM_BLUE_NOISE

//the main attraction, contact shadows!
//in short it raymarches against the scene depth buffer to estimate shadows
//significantly improves overall shadow quality
#define ENABLE_CONTACT_SHADOWS

//this directly controls the quality of the contact shadows, the more the better!
//RANGE: this should be between [4 <---> 128]
//DEFAULT: 8
//HIGHER VALUES: better quality shadows, less noise (more stable), and denser but expensive performance
//LOWER VALUES: lower quality shadows, more noise (less stable), and lighter but cheaper performance
#define CONTACT_SHADOWS_SAMPLES 16

//this controls how far out the shadows go in screen space (and also can make shadows appear darker/denser or lighter depending on sample count)
//RANGE: this should be between [10.0 <---> 200.0]
//DEFAULT: 50.0
//HIGHER VALUES: shadows can reach farther, but can become noiser, and more expensive (more screen area)
//LOWER VALUES: shadows don't reach as far, but can become more stable, and cheaper (less screen area)
#define CONTACT_SHADOWS_RAY_LENGTH 75.0

//this controls how "thick" objects are in the depth buffer
//RANGE: this should be between [0.1 <---> 1.0]
//DEFAULT: 0.325
//HIGHER VALUES: larger volume of shadow, but can lead to alot of wierd false shadowing. objects up close can cast shadows onto objects far behind it which can look odd.
//LOWER VALUES: smaller volume of shadow, less false shadowing, but they can appear less dense and might be too thin
#define CONTACT_SHADOWS_THICKNESS 0.325

//this is a small bias factor to minimize contact shadow acne on sloped surfaces
//RANGE: this should be between [0.0 <---> 0.1]
#define CONTACT_SHADOWS_BIAS 0.0001

//this is a small bias factor to minimize contact shadow acne on sloped surfaces using surface normal
//RANGE: this should be between [0.0 <---> 1.0]
#define CONTACT_SHADOWS_NORMAL_BIAS 0.001

//OPTIMIZATION: this avoids calculating contact shadows for sky pixels
//has no effect visually, but can save you quite a bit of frametime especially the more you look up :P 
//honestly no reason you should turn this off unless you want to suffer in vain...
#define CONTACT_SHADOW_EARLY_SKY_OUT

//OPTIMIZATION: this calculates contact shadows for every other pixel in a checkerboard like pattern that switches every frame
//it saves a small bit of frametime, but does have a quality degredation with more visible shimmering at distances
//disable if you want sharper true per-pixel contact shadows (at a bit of a perf hit)
//#define CONTACT_SHADOW_CHECKERBOARD

//[CONTACT_SHADOW_CHECKERBOARD ONLY!] This only works if checkerboarding is enabled!
//this tries to fill in the gaps intelligently during checkerboard rendering minimize holes where no shadow is calculated 
#define CONTACT_SHADOW_CHECKERBOARD_QUAD_RECONSTRUCTION

//[CONTACT_SHADOW_CHECKERBOARD and CONTACT_SHADOW_CHECKERBOARD_QUAD_RECONSTRUCTION ONLY!] This only works if checkerboarding and quad reconstruction is enabled!
//sharper | checks within the 2x2 checker block and if there is a shadow pixel it just copies it
//#define CONTACT_SHADOW_CHECKERBOARD_QUAD_RECONSTRUCTION_TYPE_MIN

//smoother | takes the values within the 2x2 checker block and averages them together for a slightly softer apperance
#define CONTACT_SHADOW_CHECKERBOARD_QUAD_RECONSTRUCTION_TYPE_AVG

//this was requested by quite a few people who wanted to control the strength of the shadows
//physical accuracy is at 1.0 full power, and natrually global illumination (ambient, bounce, transmission light) fills in shadows
//however the games GI term is far from perfect and can leave some areas quite dark so here you can control it
//reduction in shadow strength can create an odd 90s style look where you can see a semblance of light leaking through to "fill in" for the shadow.
//not a fan of it, and it's not accurate, but hey to each their own!
//RANGE: this should be between [0.0 <---> 1.0]
//DEFAULT: 1
#define CONTACT_SHADOWS_STRENGTH 1.0

//this was requested by a couple of users who wanted to selectively disable contact shadows for specific material types
//while this could create visual inconsistencies, I've wired them up anyway so you can use them at your own descretion
//these both are used essentially for all materials within the game
//#define DISABLE_CONTACT_SHADOWS_FOR_DEFAULT_LIT
//#define DISABLE_CONTACT_SHADOWS_FOR_CLOTH //<-- this does get used on characters but it's not common, default_lit

//all these 4 below generally cover a majority of the character
//#define DISABLE_CONTACT_SHADOWS_FOR_PREINTEGRATED_SKIN
//#define DISABLE_CONTACT_SHADOWS_FOR_SUBSURFACE_PROFILE
//#define DISABLE_CONTACT_SHADOWS_FOR_HAIR
//#define DISABLE_CONTACT_SHADOWS_FOR_EYE

//|||||||||||||||||||||||||||||||||| MACROS ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| MACROS ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| MACROS ||||||||||||||||||||||||||||||||||
//DONT TOUCH THESE IF YOU DON'T KNOW WHAT YOU ARE DOING!!!

//these help tell us what KIND of shading to do for a material
#define SHADINGMODELID_UNLIT                 0
#define SHADINGMODELID_DEFAULT_LIT           1
#define SHADINGMODELID_PREINTEGRATED_SKIN    3
#define SHADINGMODELID_SUBSURFACE_PROFILE    5
#define SHADINGMODELID_HAIR                  7
#define SHADINGMODELID_CLOTH                 8
#define SHADINGMODELID_EYE                   9

//these seemingly are unused in the game
//#define SHADINGMODELID_SUBSURFACE			2
//#define SHADINGMODELID_CLEAR_COAT			4
//#define SHADINGMODELID_TWOSIDED_FOLIAGE	6
//#define SHADINGMODELID_NUM				10
//#define SHADINGMODELID_MASK					0xF		

//|||||||||||||||||||||||||||||||||| RESOURCES ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| RESOURCES ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| RESOURCES ||||||||||||||||||||||||||||||||||
//resources passed in to the shader

//SamplerComparisonState View_SharedPointClampedSampler : register(s0); // can't disambiguate
//SamplerComparisonState View_SharedBilinearClampedSampler : register(s1); // can't disambiguate
//SamplerComparisonState LightAttenuationTextureSampler : register(s2); // can't disambiguate

SamplerState View_SharedPointClampedSampler : register(s0); // can't disambiguate
SamplerState View_SharedBilinearClampedSampler : register(s1); // can't disambiguate
SamplerState LightAttenuationTextureSampler : register(s2); // can't disambiguate

Texture3D<float4> View_SpatiotemporalBlueNoiseVolumeTexture : register(t0);

Texture2D<float4> SceneTexturesStruct_SceneDepthTexture : register(t2);
Texture2D<float4> SceneTexturesStruct_GBufferATexture : register(t3);
Texture2D<float4> SceneTexturesStruct_GBufferBTexture : register(t4);
Texture2D<float4> SceneTexturesStruct_GBufferCTexture : register(t5);
Texture2D<float4> SceneTexturesStruct_GBufferDTexture : register(t6);
Texture2D<float4> SceneTexturesStruct_GBufferETexture : register(t7);
Texture2D<float4> SceneTexturesStruct_ScreenSpaceAOTexture : register(t8);
Texture2D<float4> LightAttenuationTexture : register(t9);
Texture2D<float4> View_ThinFilmTableTexture : register(t1);

//|||||||||||||||||||||||||||||||||| CONSTANT BUFFERS ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| CONSTANT BUFFERS ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| CONSTANT BUFFERS ||||||||||||||||||||||||||||||||||
//game data passed in to the shader

cbuffer _Globals : register(b0) 
{
	float4 HZBCoordinateContext : packoffset(c0.x);
};

cbuffer View : register(b1) 
{
	row_major float4x4 View_TranslatedWorldToClip : packoffset(c0.x);

	float4x4 View_WorldToOrthographicClip : packoffset(c4.x);
	float4x4 View_TranslatedWorldToOrthographicClip : packoffset(c8.x);
	float4x4 View_WorldToClip : packoffset(c12.x);
	float4x4 View_ClipToWorld : packoffset(c16.x);
	float4x4 View_TranslatedWorldToView : packoffset(c20.x);
	float4x4 View_ViewToTranslatedWorld : packoffset(c24.x);
	float4x4 View_TranslatedWorldToCameraView : packoffset(c28.x);
	float4x4 View_CameraViewToTranslatedWorld : packoffset(c32.x);
	row_major float4x4 View_ViewToClip : packoffset(c36.x);
	float4x4 View_ViewToClipNoAA : packoffset(c40.x);
	float4x4 View_ClipToView : packoffset(c44.x);
	float4x4 View_ClipToTranslatedWorld : packoffset(c48.x);
	float4x4 View_SVPositionToTranslatedWorld : packoffset(c52.x);
	row_major float4x4 View_ScreenToWorld : packoffset(c56.x);
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

cbuffer DeferredLightUniforms : register(b2) 
{
	float2 DeferredLightUniforms_DistanceFadeMAD : packoffset(c0.x);
	int DeferredLightUniforms_LightContextBitField : packoffset(c0.z);
	float PrePadding_DeferredLightUniforms_12 : packoffset(c0.w);
	float3 DeferredLightUniforms_Position : packoffset(c1.x);
	float DeferredLightUniforms_InvRadius : packoffset(c1.w);
	float3 DeferredLightUniforms_Color : packoffset(c2.x);
	float DeferredLightUniforms_Reserved0 : packoffset(c2.w);
	float3 DeferredLightUniforms_Direction : packoffset(c3.x);
	float DeferredLightUniforms_Reserved1 : packoffset(c3.w);
	float3 DeferredLightUniforms_Tangent : packoffset(c4.x);
	float DeferredLightUniforms_Reserved2 : packoffset(c4.w);
	float4 DeferredLightUniforms_SpotAngles : packoffset(c5.x);
	float DeferredLightUniforms_SourceRadius : packoffset(c6.x);
	float DeferredLightUniforms_SourceLength : packoffset(c6.y);
	float DeferredLightUniforms_Reserved3 : packoffset(c6.z);
	float DeferredLightUniforms_Reserved4 : packoffset(c6.w);
};

//|||||||||||||||||||||||||||||||||| STRUCTS ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| STRUCTS ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| STRUCTS ||||||||||||||||||||||||||||||||||

struct PSInput
{
    float2 ScreenUV   : TEXCOORD0;
    float3 CameraRay  : TEXCOORD1;
    noperspective float4 SvPosition : SV_Position;
};

struct PSOutput
{
    float4 Color : SV_Target0;
};

struct FGBufferData
{
    float4 GBufferA;
    float4 GBufferB;
    float4 GBufferC;
    float4 GBufferD;
    float4 GBufferE;
    float  DeviceDepth;
    float  ScreenAO;
    float3 WorldNormal;
    float3 SecondaryNormalOrTangent;
    float3 BaseColor;
    float  Metallic;
    float  Specular;
    float  Roughness;
    float  MaterialAO;
    float4 CustomData;
    int    ShadingModelID;
    float  SelectiveOutputMaskHighNibble;
    float3 DiffuseColor;
    float3 SpecularColor;
    float3 ShadingDiffuseColor;
    float  EyeMetallicPayload;
    float  EyePayloadFromAO;
};

struct FDeferredLightData
{
    float2 DistanceFadeMAD;
    float3 Position;
    float  InvRadius;
    float3 Color;
    float3 Direction;
    float4 SpotAngles;
    float  SourceRadius;
    float  SourceLength;
    int    LightContextBitField;
};

struct FResolvedPixel
{
    float2 ScreenUV;
    int2   PixelPos;
    float3 WorldPosition;
    float3 CameraVector;
    float3 LightVector;
    float  LightDistanceSq;
    float  DistanceAttenuation;
    float  ShadowedLightAttenuation;
};

struct FAreaLobe
{
    float NoL;
    float NoVAbs;
    float VoH;
    float NoH;
    float SourceSin;
    bool  HasSourceAngle;
};

struct FLightingTerms
{
    float3 Diffuse;
    float3 Specular;
    float3 Extra;
};

//|||||||||||||||||||||||||||||||||| MATH ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| MATH ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| MATH ||||||||||||||||||||||||||||||||||

#define PI                 3.14159265
#define INV_PI             0.318310
#define INV_4PI            0.0795775

float Pow2(float v) { return v * v; }
float Pow4(float v) { float v2 = v * v; return v2 * v2; }
float Pow5(float v) { return Pow4(v) * v; }

float3 SafeNormalize(float3 v)
{
    return v * rsqrt(max(dot(v, v), 1.0e-20));
}

float3 DecodeUnitVector(float3 packedVector)
{
    return SafeNormalize(packedVector * 2.0 - 1.0);
}

//||||||||||||||||||||||||||||||| NOISE |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| NOISE |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| NOISE |||||||||||||||||||||||||||||||

//more deteriministic than blue noise and generally cleaner
//rebirth has a ton of damn aliasing already
float InterleavedGradientNoise(float2 pixCoord, int frameCount)
{
	const float3 magic = float3(0.06711056f, 0.00583715f, 52.9829189f);
	const float2 frameMagicScale = float2(2.083f, 4.867f);

    #if defined(RANDOM_ANIMATE_NOISE)
	    pixCoord += frameCount * frameMagicScale;
    #endif

	return frac(magic.z * frac(dot(pixCoord, magic.xy)));
}

float LoadSpatiotemporalBlueNoise(PSInput input)
{
    #if defined(RANDOM_ANIMATE_NOISE)
        int frameSlice = View_StateFrameIndex & 7;
    #else
        int frameSlice = 0;
    #endif

    int2 blueNoiseXY = int2(input.SvPosition.xy) & 127;
    return View_SpatiotemporalBlueNoiseVolumeTexture.Load(int4(blueNoiseXY, frameSlice, 0)).r;
}

//||||||||||||||||||||||||||||||| SPACE CONVERSIONS |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| SPACE CONVERSIONS |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| SPACE CONVERSIONS |||||||||||||||||||||||||||||||

float LinearizeSceneDepth(float deviceZ)
{
    return View_InvDeviceZToWorldZTransform.x * deviceZ + View_InvDeviceZToWorldZTransform.y + rcp(View_InvDeviceZToWorldZTransform.z * deviceZ - View_InvDeviceZToWorldZTransform.w);
}

float2 ResolveScreenUV(float2 screenUV)
{
    return screenUV;
}

int2 ResolvePixelPos(float2 screenUV)
{
    return int2(screenUV * View_BufferSizeAndInvSize.xy); //this has a problem on a different user machine
	//return int2(screenUV * View_ViewSizeAndInvSize.xy); //this actually is an improvement?
	//return int2(screenUV * View_ViewSizeAndInvSize.xy); //this actually is an improvement?
}

float3 ReconstructDirectionalWorldPosition(float3 cameraRay, float sceneDepth)
{
    return View_WorldCameraOrigin + cameraRay * LinearizeSceneDepth(sceneDepth);
}

//||||||||||||||||||||||||||||||| GET LIGHT DATA |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| GET LIGHT DATA |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| GET LIGHT DATA |||||||||||||||||||||||||||||||

FDeferredLightData GetDeferredLight()
{
    FDeferredLightData light;

    //float4 LightPositionAndInvRadius;
    light.Position = DeferredLightUniforms_Position;
    light.InvRadius = DeferredLightUniforms_InvRadius;

	//float4 LightColorAndFalloffExponent; (no falloff exponent)
    light.Color = DeferredLightUniforms_Color;

	//float3 LightDirection;
    light.Direction = DeferredLightUniforms_Direction;

	//float4 SpotAnglesAndSourceRadius;
    light.SpotAngles = DeferredLightUniforms_SpotAngles; //float2
    light.SourceRadius = DeferredLightUniforms_SourceRadius;
    light.SourceLength = DeferredLightUniforms_SourceLength;

	//float2 DistanceFadeMAD;
    light.DistanceFadeMAD = DeferredLightUniforms_DistanceFadeMAD;

	//custom
    light.LightContextBitField = DeferredLightUniforms_LightContextBitField;

	//these from the base UE4 shader don't seem to exist
	//float MinRoughness;
	//float ContactShadowLength;
	//float4 ShadowMapChannelMask;
	//bool bInverseSquared;
	//bool bRadialLight;
	//bool bSpotLight;
	//uint ShadowedBits;

    return light;
}

//||||||||||||||||||||||||||||||| GBUFFER DECODING |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| GBUFFER DECODING |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| GBUFFER DECODING |||||||||||||||||||||||||||||||

float DecodeSpecularScalar(float encodedSpecular)
{
    float s = saturate(encodedSpecular);
    float decoded = (s < 0.666667) ? (s * 0.06) : (s * 0.36 - 0.2);
    return clamp(decoded, 0.0, 0.16);
}

float3 WetnessAndIORAdjustSpecular(float3 specularColor, float selectiveMask)
{
    float wetnessEnabled = (View_WetnessIntensity > 0.0) ? 1.0 : 0.0;
    float fogMediumEnabled = (View_FogContextMediumIOR > 1.0) ? 1.0 : 0.0;
    float wetnessOrFog = saturate(selectiveMask * wetnessEnabled + fogMediumEnabled);

    float3 shifted = max(0.0, specularColor - 0.02) * 1.02041;
    shifted = exp2(log2(shifted) * 1.4);
    return lerp(specularColor, shifted, wetnessOrFog);
}

float3 MaybeThinFilmSpecular(float3 specularColor, float voH, float customX, float customY)
{
    float3 thinFilm = View_ThinFilmTableTexture.SampleLevel(View_SharedBilinearClampedSampler, float2(voH, customX), 0.0).rgb - 0.5;
    float thinFilmWeight = saturate(customX * 128.0) * saturate(customY) * 2.5;
    float oneMinusVoH = 1.0 - voH;
    float fresnelWeight = Pow5(oneMinusVoH);
    float whiteClamp = min(dot(specularColor, float3(16.6667, 16.6667, 16.6667)), 1.0);
    float3 fresnelColor = specularColor + (whiteClamp - specularColor) * fresnelWeight;

    return saturate(fresnelColor + thinFilm * specularColor * thinFilmWeight);
}

FGBufferData DecodeGBuffer(int2 pixelPos)
{
    FGBufferData gbufferData;
    gbufferData.GBufferA = SceneTexturesStruct_GBufferATexture.Load(int3(pixelPos, 0));
    gbufferData.GBufferB = SceneTexturesStruct_GBufferBTexture.Load(int3(pixelPos, 0));
    gbufferData.GBufferC = SceneTexturesStruct_GBufferCTexture.Load(int3(pixelPos, 0));
    gbufferData.GBufferD = SceneTexturesStruct_GBufferDTexture.Load(int3(pixelPos, 0));
    gbufferData.GBufferE = SceneTexturesStruct_GBufferETexture.Load(int3(pixelPos, 0));
    gbufferData.DeviceDepth = SceneTexturesStruct_SceneDepthTexture.Load(int3(pixelPos, 0)).r;
    gbufferData.ScreenAO = SceneTexturesStruct_ScreenSpaceAOTexture.Load(int3(pixelPos, 0)).r;

    int packedModel = (int)round(gbufferData.GBufferB.a * 255.0);
    gbufferData.ShadingModelID = packedModel & 15;

    int packedMask = (int)round(saturate(gbufferData.GBufferE.a) * 255.0);
    gbufferData.SelectiveOutputMaskHighNibble = (float)((packedMask >> 4) & 15) * 0.0666667;

    bool isEye = (gbufferData.ShadingModelID == SHADINGMODELID_EYE);

    gbufferData.WorldNormal = DecodeUnitVector(gbufferData.GBufferA.rgb);
    gbufferData.SecondaryNormalOrTangent = DecodeUnitVector(gbufferData.GBufferE.rgb);
    gbufferData.BaseColor = gbufferData.GBufferC.rgb;
    gbufferData.Roughness = gbufferData.GBufferB.b;
    gbufferData.Metallic = isEye ? 0.0 : gbufferData.GBufferB.r;
    gbufferData.MaterialAO = isEye ? 1.0 : gbufferData.GBufferC.a;
    gbufferData.CustomData = gbufferData.GBufferD;

    //eye repurposes standard channels
    gbufferData.EyeMetallicPayload = isEye ? gbufferData.GBufferB.r : 0.0;
    gbufferData.EyePayloadFromAO = isEye ? gbufferData.GBufferC.a : 0.0;

    float specularScalar = DecodeSpecularScalar(gbufferData.GBufferA.a);
    gbufferData.DiffuseColor = (1.0 - gbufferData.Metallic) * gbufferData.BaseColor;
    gbufferData.SpecularColor = WetnessAndIORAdjustSpecular(lerp(float3(specularScalar, specularScalar, specularScalar), gbufferData.BaseColor, gbufferData.Metallic), gbufferData.SelectiveOutputMaskHighNibble);

    bool usesScalarDiffuse = isEye || (gbufferData.ShadingModelID == SHADINGMODELID_SUBSURFACE_PROFILE) || (gbufferData.ShadingModelID == SHADINGMODELID_PREINTEGRATED_SKIN);
	gbufferData.ShadingDiffuseColor = usesScalarDiffuse ? float3(1.0 - gbufferData.Metallic, 1.0 - gbufferData.Metallic, 1.0 - gbufferData.Metallic) : gbufferData.DiffuseColor;
    return gbufferData;
}

//||||||||||||||||||||||||||||||| LIGHTING |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| LIGHTING |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| LIGHTING |||||||||||||||||||||||||||||||

float ApplySourceRadiusToNoL(float noL, float sourceRadius, float invDistSq)
{
    if (sourceRadius > 0.0)
    {
        float sinTheta = sqrt(saturate(sourceRadius * sourceRadius * invDistSq));
        if (sinTheta > max(1.0e-5, noL))
        {
            float shifted = max(-sinTheta, noL) + sinTheta;
            noL = shifted * shifted * (0.25 / sinTheta);
        }
    }
    return max(0.0, noL);
}

FAreaLobe BuildAreaLobe(float3 normal, float3 viewDir, float3 lightDir, float distSq, float roughness, float sourceRadius)
{
    FAreaLobe lobe;
    float invDist = rsqrt(max(0.0001, max(1.0, distSq)));
    float noLRaw = dot(normal, lightDir);
    lobe.NoL = ApplySourceRadiusToNoL(noLRaw, sourceRadius, rcp(max(0.0001, max(1.0, distSq))));

    float a = roughness * roughness;
    lobe.SourceSin = saturate((1.0 - a) * sourceRadius * invDist);
    lobe.HasSourceAngle = (lobe.SourceSin > 0.0);

    float noV = dot(normal, viewDir);
    float voL = dot(viewDir, lightDir);

    if (lobe.HasSourceAngle)
    {
        float sourceCos = sqrt(max(0.0, 1.0 - lobe.SourceSin * lobe.SourceSin));
        float closest = 2.0 * noLRaw * noV - voL;

        if (closest < sourceCos)
        {
            float invClosestSin = rsqrt(max(1.0e-5, 1.0 - closest * closest));
            float tangentScale = invClosestSin * lobe.SourceSin;
            float newNoV = tangentScale * (noV - closest * noLRaw) + sourceCos * noLRaw;
            float newVoL = tangentScale * ((2.0 * noV * noV - 1.0) - closest * voL) + sourceCos * voL;
            float halfDenom = rsqrt(max(1.0e-5, 2.0 * newVoL + 2.0));
            lobe.NoH = saturate((newNoV + noV) * halfDenom);
            lobe.VoH = min((newVoL + 1.0) * halfDenom, 1.0);
            lobe.NoVAbs = abs(noV);
            return lobe;
        }

        lobe.NoVAbs = abs(noV);
        lobe.VoH = max(1.0e-5, lobe.NoVAbs);
        lobe.NoH = 1.0;
        return lobe;
    }

    float halfDenomNoArea = rsqrt(max(1.0e-5, 2.0 * voL + 2.0));
    lobe.NoH = saturate((noLRaw + noV) * halfDenomNoArea);
    lobe.VoH = min((voL + 1.0) * halfDenomNoArea, 1.0);
    lobe.NoVAbs = abs(noV);
    return lobe;
}

FLightingTerms MakeTerms(float3 diffuse, float3 specular, float3 extra)
{
    FLightingTerms terms;
    terms.Diffuse = diffuse;
    terms.Specular = specular;
    terms.Extra = extra;
    return terms;
}

//||||||||||||||||||||||||||||||| BRDF |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| BRDF |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| BRDF |||||||||||||||||||||||||||||||


float GGXSpecularScalar(float roughness, FAreaLobe lobe)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float areaNorm = 1.0;
    if (lobe.HasSourceAngle)
    {
        areaNorm = a2 / (lobe.SourceSin * 0.25 * (lobe.SourceSin + a * 3.0) + a2);
    }

    float dDenom = lobe.NoH * lobe.NoH * (a2 - 1.0) + 1.0;
    float d = a2 * INV_PI / (dDenom * dDenom);

    float noV = max(1.0e-5, lobe.NoVAbs);
    float noL = max(0.0, lobe.NoL);
    float visDenom = a * (noV + noL - 2.0 * noV * noL) + 2.0 * noV * noL;
    float vis = 0.5 / visDenom;
    return noL * d * areaNorm * vis;
}

float DiffuseEnergyCompensation(float roughness, float noL, float3 specularColor)
{
    // Fast approximation of UE's multi-scatter diffuse energy term.  The
    // original used nested log2/exp2 expressions; this keeps the same intent:
    // reduce diffuse when the specular lobe is strong, especially near grazing.
    float specLum = dot(specularColor, float3(0.333333, 0.333333, 0.333333));
    float grazing = Pow5(1.0 - saturate(noL));
    float roughWeight = lerp(0.45, 0.12, saturate(roughness));
    return saturate(1.0 - specLum * roughWeight * (1.0 + grazing));
}

float3 SpecularEnergyScale(float roughness, float noL, float3 specularColor)
{
    // Companion approximation for the specular energy boost.  This is cheaper
    // and smoother than the recovered expression, while keeping colored F0.
    float grazing = Pow5(1.0 - saturate(noL));
    float boost = lerp(0.18, 0.04, saturate(roughness)) * grazing;
    return 1.0 + specularColor * boost;
}

float3 ShadingColorOrScalarForSSS(FGBufferData g)
{
    return g.ShadingDiffuseColor;
}

float3 CommonLambertTerm(float3 diffuseColor, float invDistSq, float noL, float energy, float diffuseShadow)
{
    return diffuseColor * invDistSq * (noL * INV_PI) * energy * diffuseShadow;
}

float3 CommonTransmissionTerm(float3 diffuseColor, float invDistSq, float customZ, float energy, float transmissionMask)
{
    return diffuseColor * (customZ * INV_4PI) * invDistSq * energy * transmissionMask;
}

//||||||||||||||||||||||||||||||| UNREAL DIFFUSE BRDF |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| UNREAL DIFFUSE BRDF |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| UNREAL DIFFUSE BRDF |||||||||||||||||||||||||||||||
//reference - https://github.com/chendi-YU/UnrealEngine/blob/master/Engine/Shaders/BRDF.usf

float3 Diffuse_Lambert(float3 DiffuseColor)
{
	//return DiffuseColor * (1 / PI);
	return DiffuseColor * INV_PI;
}

// [Burley 2012, "Physically-Based Shading at Disney"]
float3 Diffuse_Burley(float3 DiffuseColor, float Roughness, float NoV, float NoL, float VoH)
{
	float FD90 = 0.5 + 2 * VoH * VoH * Roughness;
	float FdV = 1 + (FD90 - 1) * Pow5(1 - NoV);
	float FdL = 1 + (FD90 - 1) * Pow5(1 - NoL);
	return DiffuseColor * ((1 / PI) * FdV * FdL);
}

// [Gotanda 2012, "Beyond a Simple Physically Based Blinn-Phong Model in Real-Time"]
float3 Diffuse_OrenNayar(float3 DiffuseColor, float Roughness, float NoV, float NoL, float VoH)
{
	float a = Roughness * Roughness;
	float s = a;// / ( 1.29 + 0.5 * a );
	float s2 = s * s;
	float VoL = 2 * VoH * VoH - 1;		// double angle identity
	float Cosri = VoL - NoV * NoL;
	float C1 = 1 - 0.5 * s2 / (s2 + 0.33);
	float C2 = 0.45 * s2 / (s2 + 0.09) * Cosri * (Cosri >= 0 ? rcp(max(NoL, NoV)) : 1);
	return DiffuseColor / PI * (C1 + C2) * (1 + Roughness * 0.5);
}

//||||||||||||||||||||||||||||||| CUSTOM BRDF |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| CUSTOM BRDF |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| CUSTOM BRDF |||||||||||||||||||||||||||||||
//these are different (or some my own) implementations of BRDF rather than using what unreal has

//const static float constant1_FON = 0.5f - 2.0f / (3.0f * PI);
//const static float constant2_FON = 2.0f / 3.0f - 28.0f / (15.0f * PI);
const static float constant1_FON = 0.287793409210806; //precomputed
const static float constant2_FON = 0.0724882124569239; //precomputed

//NOTE 1: this is the approximated variant of oren nayar EON, I went and did comparisons with the exact, and the approximation is almost identical
//NOTE 2: this was also modified to be an isotropic variant (to save on calculating a TBN matrix and shifting vectors), which looks slightly different but good enough
//NOTE 3: this version dropped the multi-scatter lobe as it literally had zero effect on the final results for this isotropic variant (when comparing direct light on a BRDF 5x5 grid metallic v. smoothness)
float3 DiffuseEnergyPreservingOrenNayarSimplified(float3 diffuse, float roughness, float3 vector_lightDirection, float3 vector_viewDirection, float NdotL, float NdotV)
{
    float s = dot(vector_lightDirection, vector_viewDirection) - NdotL * NdotV;
    float t = max(NdotL, NdotV);
    float invT = rcp(t);
    float sovertF = (s > 0.0f) ? s * invT : s;
    float invDenom = 1.0f - constant1_FON * roughness;
    float scalar = invDenom * (1.0f + roughness * sovertF);
    return diffuse * (scalar * NdotL * INV_PI);
}

//||||||||||||||||||||||||||||||| MICRO SHADOWS |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| MICRO SHADOWS |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| MICRO SHADOWS |||||||||||||||||||||||||||||||

//NEW: from uncharted 4
float Uncharted4_MicroShadowing(float AO, float NdotL, float opacity)
{
    float aperture = 2.0 * AO * AO;
    float microshadow = saturate(NdotL + aperture - 1.0);
	return lerp(1.0f, microshadow, opacity);
}

//apparently game seems to have some kind of microshadowing? it's much more muted than the uncharted 4 method... and more complicated

float ComputeMicroShadow(float screenAO, float materialAO)
{
    float product = screenAO * materialAO;
    float lower = min(screenAO, materialAO);
    float t = product + 1.0 - lower;
    float q = 1.0 - Pow5(t);
    return lower + Pow5(q) * (product - lower);
}

float DiffuseMicroShadow(float noL, float microAO, float attenuation)
{
    float visible = saturate(noL / max(0.001, sqrt(max(0.0, 1.0 - microAO))));
    return min(attenuation, visible * visible);
}

float SpecularMicroShadow(float noL, float noVAbs, float roughness, float microAO, float attenuation)
{
    float aoRemainder = sqrt(max(0.0, 1.0 - microAO));
    float roughMask = saturate(roughness * 3.5 - 0.5);
    float roughWeight = saturate(1.0 - Pow5(1.0 - roughMask));
    float grazing = Pow4(1.0 - noVAbs) * aoRemainder * roughWeight;
    float visible = saturate(noL / max(0.001, grazing));
    return min(attenuation, visible * visible);
}

//||||||||||||||||||||||||||||||| CONTACT SHADOWS |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| CONTACT SHADOWS |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| CONTACT SHADOWS |||||||||||||||||||||||||||||||

float LinearEyeDepth(float sceneDepth)
{
    return View_InvDeviceZToWorldZTransform.z / (sceneDepth - View_InvDeviceZToWorldZTransform.w);
}

/*
//NOTE: this implementation seems to solve the view direction towards light and normals at grazing angles over self shadow occlusion

#define CONTACT_SHADOWS_START_BIAS     0.0
#define CONTACT_SHADOWS_Z_EXTENT_SCALE 1.0
#define CONTACT_SHADOWS_MIN_Z_EXTENT   1e-5
#define CONTACT_SHADOWS_MAX_Z_EXTENT   0.001
#define CONTACT_SHADOWS_Z_BIAS         1e-5

float FastContactShadowClipSpace(
    float3 worldPosition,
    float3 worldNormal,
    float3 lightDirection,
    float rawDepth,
    float random)
{
    const float sampleCount = (float)CONTACT_SHADOWS_SAMPLES;
    const float invSamplesPlusOne = rcp(sampleCount + 1.0);

    float3 rayOrigin = worldPosition + lightDirection * CONTACT_SHADOWS_START_BIAS;
    float3 rayEnd = rayOrigin + lightDirection * CONTACT_SHADOWS_RAY_LENGTH;

    float4 clipStart = mul(View_WorldToClip, float4(rayOrigin, 1.0));
    float4 clipEnd   = mul(View_WorldToClip, float4(rayEnd, 1.0));

    if (clipStart.w <= 0.0 || clipEnd.w <= 0.0)
    {
        return 1.0;
    }

    float3 ndcStart = clipStart.xyz / clipStart.w;
    float3 ndcEnd   = clipEnd.xyz / clipEnd.w;

    float rayDz = ndcEnd.z - ndcStart.z;

    float2 uvStart = mad(ndcStart.xy, View_ScreenPositionScaleBias.xy, View_ScreenPositionScaleBias.zw);
    float2 uvEnd   = mad(ndcEnd.xy,   View_ScreenPositionScaleBias.xy, View_ScreenPositionScaleBias.zw);
    float2 uvDelta = uvEnd - uvStart;

    float previousT = 0.0;

    [loop]
    for (int i = 0; i < CONTACT_SHADOWS_SAMPLES; ++i)
    {
        float t = Pow2((random + 0.316228 + (float)i) * invSamplesPlusOne);

        float2 uv = uvStart + uvDelta * t;

        if (any(uv < 0.0) || any(uv > 1.0))
        {
            break;
        }

        float rayZ = ndcStart.z + rayDz * t;

        float closest = SceneTexturesStruct_SceneDepthTexture.SampleLevel(View_SharedPointClampedSampler, uv, 0.0).r;

        float sampleSpan = max(t - previousT, invSamplesPlusOne);

        float zExtent = abs(rayDz) * sampleSpan;
        zExtent = max(zExtent, abs(rawDepth - ndcStart.z));
        zExtent = clamp(
            zExtent * CONTACT_SHADOWS_Z_EXTENT_SCALE,
            CONTACT_SHADOWS_MIN_Z_EXTENT,
            CONTACT_SHADOWS_MAX_Z_EXTENT);

        // UE reversed Z: closer scene depth is larger than ray depth.
        float dz = closest - rayZ;

        if (dz > CONTACT_SHADOWS_Z_BIAS && dz < 2.0 * zExtent)
        {
            return 0.0;
        }

        previousT = t;
    }

    return 1.0;
}
*/

float FastContactShadowClipSpace(
    float3 worldPosition, 
    float3 worldNormal,
    float3 lightDirection, 
    float random,
    float rawDepth)
{
    const float invSamples = rcp((float)CONTACT_SHADOWS_SAMPLES);

	worldPosition += worldNormal * CONTACT_SHADOWS_NORMAL_BIAS;

    float3 rayOrigin = worldPosition + lightDirection * 1.0f;
    float3 rayEnd    = rayOrigin + lightDirection * CONTACT_SHADOWS_RAY_LENGTH;

    float4 clipStart = mul(View_WorldToClip, float4(rayOrigin, 1.0));
    float4 clipEnd   = mul(View_WorldToClip, float4(rayEnd, 1.0));

    //float4 clipStart = mul(View_WorldToClip, float4(worldPosition + lightDirection * 1, 1.0));
    //float4 clipEnd = mul(View_WorldToClip, float4(worldPosition + lightDirection * CONTACT_SHADOWS_RAY_LENGTH, 1.0));

    float3 ndcStart = clipStart.xyz / clipStart.w;
    float3 ndcEnd = clipEnd.xyz / clipEnd.w;

    float rayStartDepth = LinearEyeDepth(ndcStart.z);
    float rayEndDepth = LinearEyeDepth(ndcEnd.z);
    float rayDepth = lerp(rayStartDepth, rayEndDepth, random * invSamples);
    float rayDepthStep = (rayEndDepth - rayStartDepth) * invSamples;

	//IMPORTANT NOTE: make sure we use View_ScreenPositionScaleBias instead of hardcoded constants.
	// xy = scale (0.5, -0.5 on D3D), zw = bias (0.5, 0.5 + viewport offset + TAA jitter)
	// if we don't we can (and have) end up in a case where due to some resolution mismatching
	// contact shadows can have a lot of artifacts and seemingly appear "offset" or behind for some user graphics configs
	float2 uvStart = mad(ndcStart.xy, View_ScreenPositionScaleBias.xy, View_ScreenPositionScaleBias.zw);
	float2 uvEnd   = mad(ndcEnd.xy,   View_ScreenPositionScaleBias.xy, View_ScreenPositionScaleBias.zw);
	float2 uvStep  = (uvEnd - uvStart) * invSamples;
	float2 uv      = mad(uvStep, random, uvStart);

    [unroll]
    for (int i = 0; i < CONTACT_SHADOWS_SAMPLES; ++i)
    {
        if (any(uv < 0.0) || any(uv > 1.0))
        {
            break;
        }

        float deviceDepth = SceneTexturesStruct_SceneDepthTexture.SampleLevel(View_SharedPointClampedSampler, uv, 0.0).r;
        float sceneDepth = LinearEyeDepth(deviceDepth);
        float penetration = rayDepth - sceneDepth;

        if (penetration > CONTACT_SHADOWS_BIAS && penetration < CONTACT_SHADOWS_THICKNESS)
        {
            return 0.0;
        }

        rayDepth += rayDepthStep;
        uv += uvStep;
    }

    return 1.0;
}

float CalculateContactShadows(int2 pixelPos, float3 worldPosition, float3 worldNormal, float3 lightDirection, float rawDepth, float random)
{
    #if defined(CONTACT_SHADOW_EARLY_SKY_OUT)
        if (LinearizeSceneDepth(rawDepth) >= 1000000.0)
        {
            return 1.0;
        }
    #endif

    float contactShadow = 1.0;

    #if defined(CONTACT_SHADOW_CHECKERBOARD)
        bool checkerboardTest = ((pixelPos.x + pixelPos.y + (int)View_TemporalAAParams.x) & 1) != 0;
        if (checkerboardTest)
    #endif
    {
        contactShadow = FastContactShadowClipSpace(worldPosition, worldNormal, lightDirection, random, rawDepth);
    }

    #if defined(CONTACT_SHADOW_CHECKERBOARD) && defined(CONTACT_SHADOW_CHECKERBOARD_QUAD_RECONSTRUCTION)
        float lane0 = QuadReadLaneAt(contactShadow, 0);
        float lane1 = QuadReadLaneAt(contactShadow, 1);
        float lane2 = QuadReadLaneAt(contactShadow, 2);
        float lane3 = QuadReadLaneAt(contactShadow, 3);

        if (!checkerboardTest)
        {
            #if defined(CONTACT_SHADOW_CHECKERBOARD_QUAD_RECONSTRUCTION_TYPE_MIN)
                contactShadow = min(min(lane0, lane1), min(lane2, lane3));
            #elif defined(CONTACT_SHADOW_CHECKERBOARD_QUAD_RECONSTRUCTION_TYPE_AVG)
                contactShadow = saturate((lane0 + lane1 + lane2 + lane3) * 0.5 - 1.0);
            #endif
        }
    #endif

    return contactShadow;
}

//||||||||||||||||||||||||||||||| SHADING - DEFAULT LIT |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| SHADING - DEFAULT LIT |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| SHADING - DEFAULT LIT |||||||||||||||||||||||||||||||

FLightingTerms ShadeDefaultLit(FGBufferData gbufferData, FResolvedPixel resolvedPixel, FDeferredLightData light)
{
    float roughness = max(0.04, gbufferData.Roughness);
    float distSq = max(0.0001, max(1.0, resolvedPixel.LightDistanceSq));
    float invDistSq = rcp(distSq);
    FAreaLobe lobe = BuildAreaLobe(gbufferData.WorldNormal, resolvedPixel.CameraVector, resolvedPixel.LightVector, resolvedPixel.LightDistanceSq, roughness, light.SourceRadius);

    float voH = max(1.0e-5, lobe.VoH);
    float noVAbs = max(1.0e-5, lobe.NoVAbs);
    float specScalar = GGXSpecularScalar(roughness, lobe);
    float3 specColor = MaybeThinFilmSpecular(gbufferData.SpecularColor, voH, gbufferData.CustomData.x, gbufferData.CustomData.y);
    float energy = DiffuseEnergyCompensation(roughness, lobe.NoL, gbufferData.SpecularColor);

    #if defined(ENABLE_MICRO_SHADOWS)
        float unchartedMicroShadow = Uncharted4_MicroShadowing(gbufferData.MaterialAO, lobe.NoL, MICRO_SHADOWS_STRENGTH);
        //float unchartedMicroShadow = Uncharted4_MicroShadowing(gbufferData.MaterialAO * gbufferData.ScreenAO, lobe.NoL, MICRO_SHADOWS_STRENGTH); //too much contrast, and we get SSAO haloing which looks awkward

        float specShadow = unchartedMicroShadow * resolvedPixel.ShadowedLightAttenuation;
        float diffuseShadow = unchartedMicroShadow * resolvedPixel.ShadowedLightAttenuation;
        float transmissionShadow = min(resolvedPixel.ShadowedLightAttenuation, ComputeMicroShadow(gbufferData.ScreenAO, gbufferData.MaterialAO));
    #else
        float microAO = ComputeMicroShadow(gbufferData.ScreenAO, gbufferData.MaterialAO);
        float specShadow = SpecularMicroShadow(lobe.NoL, noVAbs, roughness, microAO, resolvedPixel.ShadowedLightAttenuation);
        float diffuseShadow = DiffuseMicroShadow(lobe.NoL, microAO, resolvedPixel.ShadowedLightAttenuation);
        float transmissionShadow = min(resolvedPixel.ShadowedLightAttenuation, microAO);
    #endif

    float3 specular = specScalar * invDistSq * specColor * SpecularEnergyScale(roughness, lobe.NoL, gbufferData.SpecularColor) * specShadow;

    float diffuseScale = 1.0 - gbufferData.CustomData.z;

	#if defined(SHADING_DEFAULT_LIT_BURLEY)
		float3 diffuse = Diffuse_Burley(gbufferData.ShadingDiffuseColor * diffuseScale, roughness, noVAbs, lobe.NoL, voH) * lobe.NoL * diffuseShadow;
	#elif defined(SHADING_DEFAULT_LIT_OREN_NAYAR)
		float3 diffuse = Diffuse_OrenNayar(gbufferData.ShadingDiffuseColor * diffuseScale, roughness, noVAbs, lobe.NoL, voH) * lobe.NoL * diffuseShadow;
	#else //default game
		float3 diffuse = CommonLambertTerm(gbufferData.ShadingDiffuseColor * diffuseScale, invDistSq, lobe.NoL, energy, diffuseShadow);
	#endif

    float3 extra = CommonTransmissionTerm(gbufferData.ShadingDiffuseColor, invDistSq, gbufferData.CustomData.z, energy, transmissionShadow);
    return MakeTerms(diffuse, specular, extra);
}

//||||||||||||||||||||||||||||||| SHADING - SUBSURFACE PROFILE |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| SHADING - SUBSURFACE PROFILE |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| SHADING - SUBSURFACE PROFILE |||||||||||||||||||||||||||||||

FLightingTerms ShadeSubsurfaceProfile(FGBufferData gbufferData, FResolvedPixel resolvedPixel, FDeferredLightData light)
{
    float roughness = max(0.04, gbufferData.Roughness);
    float distSq = max(0.0001, max(1.0, resolvedPixel.LightDistanceSq));
    float invDistSq = rcp(distSq);
    FAreaLobe lobe = BuildAreaLobe(gbufferData.WorldNormal, resolvedPixel.CameraVector, resolvedPixel.LightVector, resolvedPixel.LightDistanceSq, roughness, light.SourceRadius);

    float voH = max(1.0e-5, lobe.VoH);
    float noVAbs = max(1.0e-5, lobe.NoVAbs);
    float specScalar = GGXSpecularScalar(roughness, lobe);
    float3 specColor = MaybeThinFilmSpecular(gbufferData.SpecularColor, voH, gbufferData.CustomData.x, gbufferData.CustomData.y);
    float energy = DiffuseEnergyCompensation(roughness, lobe.NoL, gbufferData.SpecularColor);

    #if defined(ENABLE_MICRO_SHADOWS_SKIN)
        float unchartedMicroShadow = Uncharted4_MicroShadowing(gbufferData.MaterialAO, lobe.NoL, MICRO_SHADOWS_STRENGTH);
        //float unchartedMicroShadow = Uncharted4_MicroShadowing(gbufferData.MaterialAO * gbufferData.ScreenAO, lobe.NoL, MICRO_SHADOWS_STRENGTH); //too much contrast, and we get SSAO haloing which looks awkward

        float specShadow = unchartedMicroShadow * resolvedPixel.ShadowedLightAttenuation;
        float diffuseShadow = unchartedMicroShadow * resolvedPixel.ShadowedLightAttenuation;
        float transmissionShadow = min(resolvedPixel.ShadowedLightAttenuation, ComputeMicroShadow(gbufferData.ScreenAO, gbufferData.MaterialAO));
    #else
        float microAO = ComputeMicroShadow(gbufferData.ScreenAO, gbufferData.MaterialAO);
        float specShadow = SpecularMicroShadow(lobe.NoL, noVAbs, roughness, microAO, resolvedPixel.ShadowedLightAttenuation);
        float diffuseShadow = DiffuseMicroShadow(lobe.NoL, microAO, resolvedPixel.ShadowedLightAttenuation);
        float transmissionShadow = min(resolvedPixel.ShadowedLightAttenuation, microAO);
    #endif

    float3 specular = specScalar * invDistSq * specColor * SpecularEnergyScale(roughness, lobe.NoL, gbufferData.SpecularColor) * specShadow;
    float diffuseScale = 1.0 - gbufferData.CustomData.z;
    float3 diffuse = CommonLambertTerm(ShadingColorOrScalarForSSS(gbufferData) * diffuseScale, invDistSq, lobe.NoL, energy, diffuseShadow);
    float3 extra = CommonTransmissionTerm(ShadingColorOrScalarForSSS(gbufferData), invDistSq, gbufferData.CustomData.z, energy, transmissionShadow);
    return MakeTerms(diffuse, specular, extra);
}

//||||||||||||||||||||||||||||||| SHADING - PREINTEGRATED SKIN |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| SHADING - PREINTEGRATED SKIN |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| SHADING - PREINTEGRATED SKIN |||||||||||||||||||||||||||||||

FLightingTerms ShadePreintegratedSkin(FGBufferData gbufferData, FResolvedPixel resolvedPixel, FDeferredLightData light)
{
    float roughnessNarrow = max(0.04, gbufferData.Roughness * 0.577);
    float roughnessWide = max(0.04, gbufferData.Roughness);
    float mixedRoughness = lerp(roughnessNarrow, roughnessWide, 0.85);
    float distSq = max(0.0001, max(1.0, resolvedPixel.LightDistanceSq));
    float invDistSq = rcp(distSq);

    FAreaLobe lobe = BuildAreaLobe(gbufferData.WorldNormal, resolvedPixel.CameraVector, resolvedPixel.LightVector, resolvedPixel.LightDistanceSq, mixedRoughness, light.SourceRadius);

    float noVAbs = max(1.0e-5, lobe.NoVAbs);
    float aNarrow = roughnessNarrow * roughnessNarrow;
    float a2Narrow = aNarrow * aNarrow;
    float aWide = roughnessWide * roughnessWide;
    float a2Wide = aWide * aWide;

    float areaNarrow = 1.0;
    float areaWide = 1.0;

    if (lobe.HasSourceAngle)
    {
        areaNarrow = a2Narrow / (lobe.SourceSin * 0.25 * (lobe.SourceSin + aNarrow * 3.0) + a2Narrow);
        areaWide = a2Wide / (lobe.SourceSin * 0.25 * (lobe.SourceSin + aWide * 3.0) + a2Wide);
    }

    float dNarrowDenom = lobe.NoH * lobe.NoH * (a2Narrow - 1.0) + 1.0;
    float dWideDenom = lobe.NoH * lobe.NoH * (a2Wide - 1.0) + 1.0;
    float dNarrow = a2Narrow * INV_PI * areaNarrow / (dNarrowDenom * dNarrowDenom);
    float dWide = a2Wide * INV_PI * areaWide / (dWideDenom * dWideDenom);

    float wetnessEnabled = (View_WetnessIntensity > 0.0) ? 1.0 : 0.0;
    float fogMediumEnabled = (View_FogContextMediumIOR > 1.0) ? 1.0 : 0.0;
    float wetnessOrFog = saturate(gbufferData.SelectiveOutputMaskHighNibble * wetnessEnabled + fogMediumEnabled);
    float skinF0 = 0.0465206 - wetnessOrFog * 0.0401335;

    float dSkin = lerp(dNarrow, dWide, 0.85);
    float visDenom = mixedRoughness * mixedRoughness * (noVAbs + lobe.NoL - 2.0 * noVAbs * lobe.NoL) + 2.0 * noVAbs * lobe.NoL;
    float vis = 0.5 / visDenom;

    float roughFresnel = skinF0 + Pow5(1.0 - max(1.0e-5, lobe.VoH)) * (min(2.32603 - wetnessOrFog * 2.00668, 1.0) - skinF0);
    float specScalar = lobe.NoL * vis * dSkin * roughFresnel * invDistSq;

    float skinEnergy = saturate(1.0 - dot(float3(skinF0, skinF0, skinF0), float3(0.333333, 0.333333, 0.333333)));

    #if defined(ENABLE_MICRO_SHADOWS_SKIN)
        float unchartedMicroShadow = Uncharted4_MicroShadowing(gbufferData.MaterialAO, lobe.NoL, MICRO_SHADOWS_STRENGTH);
        //float unchartedMicroShadow = Uncharted4_MicroShadowing(gbufferData.MaterialAO * gbufferData.ScreenAO, lobe.NoL, MICRO_SHADOWS_STRENGTH); //too much contrast, and we get SSAO haloing which looks awkward

        float specShadow = unchartedMicroShadow * resolvedPixel.ShadowedLightAttenuation;
        float diffuseShadow = unchartedMicroShadow * resolvedPixel.ShadowedLightAttenuation;
    #else
        float microAO = ComputeMicroShadow(gbufferData.ScreenAO, gbufferData.MaterialAO);
        float specShadow = SpecularMicroShadow(lobe.NoL, noVAbs, mixedRoughness, microAO, resolvedPixel.ShadowedLightAttenuation);
        float diffuseShadow = DiffuseMicroShadow(lobe.NoL, microAO, resolvedPixel.ShadowedLightAttenuation);
    #endif

    float3 specular = specScalar * specShadow;
    float3 diffuse = ShadingColorOrScalarForSSS(gbufferData) * invDistSq * (lobe.NoL * INV_PI) * skinEnergy * diffuseShadow;
    return MakeTerms(diffuse, specular, float3(0, 0, 0));
}

//||||||||||||||||||||||||||||||| SHADING - EYE |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| SHADING - EYE |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| SHADING - EYE |||||||||||||||||||||||||||||||

FLightingTerms ShadeEye(FGBufferData gbufferData, FResolvedPixel resolvedPixel, FDeferredLightData light)
{
    float roughness = max(0.04, gbufferData.Roughness);
    float distSq = max(0.0001, max(1.0, resolvedPixel.LightDistanceSq));
    float invDistSq = rcp(distSq);

    FAreaLobe corneaLobe = BuildAreaLobe(gbufferData.WorldNormal, resolvedPixel.CameraVector, resolvedPixel.LightVector, resolvedPixel.LightDistanceSq, roughness, light.SourceRadius);

    float noLEyeNormal = ApplySourceRadiusToNoL(dot(gbufferData.SecondaryNormalOrTangent, resolvedPixel.LightVector), light.SourceRadius, invDistSq);

    FAreaLobe specLobe = corneaLobe;
    float specScalar = GGXSpecularScalar(roughness, specLobe);

    float wetnessEnabled = (View_WetnessIntensity > 0.0) ? 1.0 : 0.0;
    float fogMediumEnabled = (View_FogContextMediumIOR > 1.0) ? 1.0 : 0.0;
    float wetnessOrFog = saturate(gbufferData.SelectiveOutputMaskHighNibble * wetnessEnabled + fogMediumEnabled);
    float eyeF0 = 0.0387252 - wetnessOrFog * 0.0348016;
    float3 eyeF0Color = eyeF0;

    float irisDepthOrMask = saturate(gbufferData.EyePayloadFromAO);
    float3 irisTangent = DecodeUnitVector(gbufferData.CustomData.xyz);
    float3 refractedLight = normalize(lerp(gbufferData.SecondaryNormalOrTangent, resolvedPixel.LightVector, 0.671141));
    float3 irisBitangent = SafeNormalize(cross(irisTangent, gbufferData.SecondaryNormalOrTangent));

    float tangentX = dot(refractedLight, irisTangent);
    float tangentY = dot(refractedLight, irisBitangent);
    float causticWidth = max(0.01, 1.0 - saturate(irisDepthOrMask * 2.7 * sqrt(tangentX * tangentX + tangentY * tangentY)));
    float causticOffset = sqrt(Pow2(tangentX * 0.780000 + irisDepthOrMask * 2.0) + Pow2(tangentY * 0.78)) * 0.55;
    float causticMask = min(saturate((causticWidth - causticOffset) / causticWidth), 1.0) * max(1.0, rcp(causticWidth));
    float glancingMask = saturate(1.0 - Pow2(1.0 - noLEyeNormal));

    float f0Drop = saturate(((0.08 * max(1.0e-5, specLobe.NoVAbs)) + 0.0266916) / (max(1.0e-5, specLobe.NoVAbs) + 0.466495) * exp2(max(1.0e-5, specLobe.NoVAbs) * -45.5482));
    float energy = max(1.0e-6, 1.0 - f0Drop);
    float retro = Pow5(1.0 - max(1.0e-5, specLobe.NoVAbs)) * exp2(log2(exp2(log2(max(1.0e-5, specLobe.NoVAbs)) * 0.381624) * 2.36651 + 0.0387332) * 0.08);
    float eyeEnergy = saturate(1.0 - ((1.0 + ((1.0 - energy) / energy) * 0.0387252) * (retro + (energy - retro) * 0.0387252)));

    int packedIrisNormal = (int)(gbufferData.CustomData.w * 255.0 + 0.5);
    int irisX = max(1, packedIrisNormal & 15);
    int irisY = max(1, (packedIrisNormal >> 4) & 15);
    float2 irisDisk = (float2(irisX, irisY) - 8.0) * 0.142857;
    float3 irisNormalLocal = SafeNormalize(float3(irisDisk, sqrt(max(0.0, 1.0 - dot(irisDisk, irisDisk)))));
    float3 irisNormal = irisNormalLocal.x * irisTangent + irisNormalLocal.y * irisBitangent + irisNormalLocal.z * gbufferData.SecondaryNormalOrTangent;

    float irisForward = exp2(log2(max(0.0, dot(irisNormal, refractedLight) * 0.5 + 0.5)) * 3.0);
    float irisShadowInput = sqrt(max(0.0, 1.0 - gbufferData.GBufferB.g));
    float irisShadow = saturate(irisForward / max(0.001, irisShadowInput));
    irisShadow *= irisShadow;

    float irisBlend = lerp(irisForward, noLEyeNormal, gbufferData.EyeMetallicPayload);
    float causticEnergy = lerp(causticMask * causticMask * 1.2 * glancingMask * eyeEnergy, 1.0, gbufferData.EyeMetallicPayload);
    //float irisVisibility = lerp(irisShadow, px.EyeOcclusion, g.EyeMetallicPayload);
	float irisVisibility = lerp(irisShadow, 1.0f, gbufferData.EyeMetallicPayload);

    float f0Fresnel = eyeF0 + Pow5(1.0 - max(1.0e-5, specLobe.VoH)) * (min(1.93626 - wetnessOrFog * 1.74008, 1.0) - eyeF0);
    float eyeSpecular = specScalar * f0Fresnel * invDistSq;

    float specShadow = SpecularMicroShadow(corneaLobe.NoL, max(1.0e-5, specLobe.NoVAbs), roughness, ComputeMicroShadow(gbufferData.ScreenAO, gbufferData.ScreenAO), min(resolvedPixel.ShadowedLightAttenuation, 1.0f));
	float diffuseAO = ComputeMicroShadow(gbufferData.ScreenAO, gbufferData.ScreenAO);
    float diffuseShadow = DiffuseMicroShadow(irisBlend, diffuseAO, min(resolvedPixel.ShadowedLightAttenuation, irisVisibility));

    float3 specular = eyeSpecular * specShadow;
    float3 diffuse = gbufferData.ShadingDiffuseColor * causticEnergy * (irisBlend * INV_PI) * eyeEnergy * invDistSq * diffuseShadow;
    return MakeTerms(diffuse, specular, float3(0, 0, 0));
}

//||||||||||||||||||||||||||||||| SHADING - HAIR |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| SHADING - HAIR |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| SHADING - HAIR |||||||||||||||||||||||||||||||

FLightingTerms ShadeHair(FGBufferData gbufferData, FResolvedPixel resolvedPixel, FDeferredLightData light)
{
    float roughness = max(0.04, gbufferData.Roughness);
    float distSq = max(0.0001, max(1.0, resolvedPixel.LightDistanceSq));
    float invDistSq = rcp(distSq);

    //hair constructs a tangent frame from the view-projected normal, then evaluates the light in that frame. 
	//NOTE: GBufferD.xyz looks to be a decoded strand anisotropy vector?
    float baseNoV = dot(gbufferData.WorldNormal, resolvedPixel.CameraVector);
    float3 strandTangent = SafeNormalize(gbufferData.WorldNormal * baseNoV - resolvedPixel.CameraVector);
    float tangentLight = dot(strandTangent, resolvedPixel.LightVector);
    float3 lightPlaneNormal = SafeNormalize(resolvedPixel.LightVector - strandTangent * tangentLight);
    float3 strandVector = DecodeUnitVector(gbufferData.CustomData.xyz);

    float strandDotLight = dot(strandVector, resolvedPixel.LightVector);
    float voL = dot(resolvedPixel.CameraVector, resolvedPixel.LightVector);
    float tangentView = dot(strandTangent, resolvedPixel.CameraVector);
    float planeNoLRaw = dot(lightPlaneNormal, resolvedPixel.LightVector);
    float planeNoV = dot(lightPlaneNormal, resolvedPixel.CameraVector);
    float normalDotPlane = dot(gbufferData.WorldNormal, lightPlaneNormal);

    float3 hairBitangent = SafeNormalize(cross(lightPlaneNormal, strandTangent));
    float bitangentV = dot(hairBitangent, resolvedPixel.CameraVector);
    float bitangentL = dot(hairBitangent, resolvedPixel.LightVector);

    float planeNoL = ApplySourceRadiusToNoL(planeNoLRaw, light.SourceRadius, invDistSq);

    float sourceSin = saturate(light.SourceRadius * 0.7696 * rsqrt(distSq));
    bool hasSourceAngle = (sourceSin > 0.0);

    float adjustedVoL = voL;
    float adjustedNoLForHalf = planeNoLRaw;
    float areaHalfN = 1.0;
    float areaHalfT = 0.0;
    float areaHalfB = 0.0;
    float voH = abs(planeNoV);

    if (hasSourceAngle)
    {
        float sourceCos = sqrt(max(0.0, 1.0 - sourceSin * sourceSin));
        float closest = 2.0 * planeNoLRaw * planeNoV - voL;

        if (closest < sourceCos)
        {
            float tangentScale = sourceSin * rsqrt(max(1.0e-5, 1.0 - closest * closest));
            adjustedNoLForHalf = tangentScale * (planeNoV - closest * planeNoLRaw) + sourceCos * planeNoLRaw;
            adjustedVoL = tangentScale * ((2.0 * planeNoV * planeNoV - 1.0) - closest * voL) + sourceCos * voL;

            float invHalfLen = rsqrt(max(1.0e-5, 2.0 * adjustedVoL + 2.0));
            areaHalfN = (adjustedNoLForHalf + planeNoV) * invHalfLen;
            areaHalfT = (tangentView + tangentLight) * invHalfLen;
            areaHalfB = (bitangentL + bitangentV) * invHalfLen;
            voH = min((adjustedVoL + 1.0) * invHalfLen, 1.0);
        }
    }
    else
    {
        float invHalfLen = rsqrt(max(1.0e-5, 2.0 * voL + 2.0));
        areaHalfN = (planeNoLRaw + planeNoV) * invHalfLen;
        areaHalfT = (tangentView + tangentLight) * invHalfLen;
        areaHalfB = (bitangentL + bitangentV) * invHalfLen;
        voH = min((voL + 1.0) * invHalfLen, 1.0);
    }

    float invRegularHalfLen = rsqrt(max(1.0e-5, 2.0 * adjustedVoL + 2.0));
    float regularHalfN = (adjustedNoLForHalf + planeNoV) * invRegularHalfLen;
    float regularHalfT = (tangentView + tangentLight) * invRegularHalfLen;
    float regularHalfB = (bitangentL + bitangentV) * invRegularHalfLen;

    float noVAbs = saturate(max(1.0e-5, abs(planeNoV)));
    voH = max(1.0e-5, voH);

    float f0Mask = saturate(-strandDotLight);
    float rim = exp2(log2(max(0.0, 1.0 - f0Mask * f0Mask)) * 24.0);

    float wetnessEnabled = (View_WetnessIntensity > 0.0) ? 1.0 : 0.0;
    float fogMediumEnabled = (View_FogContextMediumIOR > 1.0) ? 1.0 : 0.0;
    float wetnessOrFog = saturate(gbufferData.SelectiveOutputMaskHighNibble * wetnessEnabled + fogMediumEnabled);
    float hairF0 = 0.0465206 - wetnessOrFog * 0.0401335;
    float fresnel = hairF0 + Pow5(1.0 - voH) * (min(2.32603 - wetnessOrFog * 2.00668, 1.0) - hairF0);
    float energy = saturate(1.0 - hairF0);

    float rough2 = roughness * roughness;
    float rough4 = rough2 * rough2;
    float longitudinalV = sqrt(Pow2(tangentView * rough2 * 0.55) + Pow2(bitangentV * rough2 * 1.45) + Pow2(noVAbs));
    float longitudinalL = sqrt(Pow2(tangentLight * rough2 * 0.55) + Pow2(bitangentL * rough2 * 1.45) + Pow2(planeNoL));
    float longitudinal = 0.5 / (longitudinalV * planeNoL + longitudinalL * noVAbs);

    float primaryDenom = Pow2(areaHalfT) * 2.63636 + Pow2(areaHalfB) * 0.379310 + Pow2(areaHalfN) * rough4 * 0.797500;

    float secondaryDenom0 = Pow2(regularHalfT) * 2.63636 + Pow2(regularHalfB) * 0.379310 + Pow2(regularHalfN) * 0.199375;
    float secondaryDenom1 = Pow2(regularHalfT) * 2.63636 + Pow2(regularHalfB) * 0.379310 + Pow2(regularHalfN) * 0.797500;
    float dSecondary0 = 0.0634630 / max(1.0e-8, secondaryDenom0 * secondaryDenom0);
    float dSecondary1 = 0.253852 / max(1.0e-8, secondaryDenom1 * secondaryDenom1);

    float areaNorm = hasSourceAngle ? rough4 / (sourceSin * 0.25 * (sourceSin + rough2 * 3.0) + rough4) : 1.0;

    float strandScatter = exp2(-12.0 * strandDotLight * strandDotLight);
    float azimuth = saturate((normalDotPlane + 1.0) * 0.392699);
    float strandSinSq = 1.0 - tangentLight * tangentLight;
    float colorExponent = rsqrt(max(0.0625000, strandSinSq));

    float3 coloredPrimary = exp2(log2(max(gbufferData.ShadingDiffuseColor, float3(1.0e-4, 1.0e-4, 1.0e-4))) * colorExponent);
    float primaryLumaScale = dot(gbufferData.ShadingDiffuseColor, float3(0.212600, 0.715200, 0.0722000)) / max(0.0001, dot(coloredPrimary, float3(0.2126, 0.7152, 0.0722)));

    float primarySpec = rough4 * 0.253852 * planeNoL * rim * longitudinal * (1.0 / max(1.0e-8, primaryDenom * primaryDenom)) * fresnel * areaNorm * azimuth;

    float secondaryDWithoutScatter = dSecondary1 - 0.5 * strandScatter * dSecondary1;
    float secondaryBRDF = lerp(secondaryDWithoutScatter, dSecondary0, 0.15) * azimuth;
    float coloredDiffuseScalar = rim * planeNoL * energy * secondaryBRDF;

    float adjustedVoLForTransmission = adjustedVoL;
    float backscatterBase = max(0.0, adjustedVoLForTransmission * 1.8 + 1.81);
    float backscatterShape = (Pow2(adjustedVoLForTransmission) + 1.0) * 0.00807103 * exp2(log2(backscatterBase) * -1.5);
    float transmissionPhase = INV_4PI + Pow2(strandSinSq) * (backscatterShape - INV_4PI);
    float transmissionScalar = rim * 0.425 * energy * strandScatter * transmissionPhase;

    float3 secondaryColor = exp2(log2(max(gbufferData.ShadingDiffuseColor, float3(1.0e-4, 1.0e-4, 1.0e-4))) * (0.5 * colorExponent));
    float secondaryLumaScale = dot(gbufferData.ShadingDiffuseColor, float3(0.2126, 0.7152, 0.0722)) / max(0.0001, dot(secondaryColor, float3(0.2126, 0.7152, 0.0722)));

    float microAO = ComputeMicroShadow(gbufferData.ScreenAO, gbufferData.MaterialAO);
    float halfWrappedNoL = planeNoL * 0.5 + 0.5;
    float roughMask = saturate(roughness * 3.5 - 0.5);
    float roughWeight = saturate(1.0 - Pow5(1.0 - roughMask));
    float specOcclusionDenom = max(0.001, Pow4(1.0 - saturate(baseNoV)) * sqrt(max(0.0, 1.0 - microAO)) * roughWeight);
    float specVisibilitySq = Pow2(saturate(halfWrappedNoL / specOcclusionDenom));
    float diffuseVisibilitySq = Pow2(saturate(halfWrappedNoL / max(0.001, sqrt(max(0.0, 1.0 - microAO)))));

    float specShadow = min(resolvedPixel.ShadowedLightAttenuation, specVisibilitySq);
    float diffuseShadow = min(resolvedPixel.ShadowedLightAttenuation, diffuseVisibilitySq);
    float transmissionShadow = min(resolvedPixel.ShadowedLightAttenuation, diffuseVisibilitySq * diffuseVisibilitySq);

    float3 specular = primarySpec * invDistSq * specShadow;
    float3 diffuse = coloredDiffuseScalar * invDistSq * coloredPrimary * primaryLumaScale * diffuseShadow;
    float3 extra = transmissionScalar * invDistSq * secondaryColor * secondaryLumaScale * transmissionShadow;

    return MakeTerms(diffuse, specular, extra);
}

//||||||||||||||||||||||||||||||| SHADING - CLOTH |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| SHADING - CLOTH |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| SHADING - CLOTH |||||||||||||||||||||||||||||||

FLightingTerms ShadeCloth(FGBufferData gbufferData, FResolvedPixel resolvedPixel, FDeferredLightData light)
{
    float roughness = max(0.5, gbufferData.Roughness);
    float distSq = max(0.0001, max(1.0, resolvedPixel.LightDistanceSq));
    float invDistSq = rcp(distSq);
    FAreaLobe lobe = BuildAreaLobe(gbufferData.WorldNormal, resolvedPixel.CameraVector, resolvedPixel.LightVector, resolvedPixel.LightDistanceSq, roughness, light.SourceRadius);

    float noVAbs = max(1.0e-5, lobe.NoVAbs);
    float voH = max(1.0e-5, lobe.VoH);
    float a = roughness * roughness;
    float a2 = a * a;
    float areaNorm = lobe.HasSourceAngle ? a2 / (lobe.SourceSin * 0.25 * (lobe.SourceSin + a * 3.0) + a2) : 1.0;

    float dDenom = lobe.NoH * lobe.NoH * (a2 - 1.0) + 1.0;
    float ggxD = a2 * INV_PI / (dDenom * dDenom);
    float clothD = ((rcp(a2) + 1.0) * INV_PI) * exp2(log2(max(0.0078125, 1.0 - lobe.NoH * lobe.NoH)) * rcp(a2));
    float mixedD = clothD + 0.5 * noVAbs * (ggxD - clothD);
    float vis = 0.25 / (noVAbs + lobe.NoL - noVAbs * lobe.NoL);

    float wetnessEnabled = (View_WetnessIntensity > 0.0) ? 1.0 : 0.0;
    float fogMediumEnabled = (View_FogContextMediumIOR > 1.0) ? 1.0 : 0.0;
    float wetnessOrFog = saturate(gbufferData.SelectiveOutputMaskHighNibble * wetnessEnabled + fogMediumEnabled);
    float clothF0 = 0.0672154 - wetnessOrFog * 0.0528935;
    float fresnel = clothF0 + Pow5(1.0 - voH) * (min(3.36077 - wetnessOrFog * 2.64467, 1.0) - clothF0);
    float clothEnergy = saturate(1.0 - clothF0);

    float specScalar = mixedD * areaNorm * vis * fresnel;
    float3 sheenDiffuse = specScalar + gbufferData.ShadingDiffuseColor * (1.0 - gbufferData.CustomData.x) * (clothEnergy * mixedD);
    float lambert = clothEnergy * INV_PI;

    #if defined(ENABLE_MICRO_SHADOWS)
        float unchartedMicroShadow = Uncharted4_MicroShadowing(gbufferData.MaterialAO, lobe.NoL, MICRO_SHADOWS_STRENGTH);
        float specShadow = unchartedMicroShadow * resolvedPixel.ShadowedLightAttenuation;
        float diffuseShadow = unchartedMicroShadow * resolvedPixel.ShadowedLightAttenuation;
    #else
        float microAO = ComputeMicroShadow(gbufferData.ScreenAO, gbufferData.MaterialAO);
        float specShadow = SpecularMicroShadow(lobe.NoL, max(1.0e-5, lobe.NoVAbs), gbufferData.Roughness, microAO, resolvedPixel.ShadowedLightAttenuation);
        float diffuseShadow = DiffuseMicroShadow(lobe.NoL, microAO, resolvedPixel.ShadowedLightAttenuation);
    #endif

    float3 specular = lobe.NoL * invDistSq * sheenDiffuse * specShadow;
    float3 diffuse = gbufferData.ShadingDiffuseColor * gbufferData.CustomData.x * invDistSq * lobe.NoL * lambert * diffuseShadow;

    return MakeTerms(diffuse, specular, float3(0, 0, 0));
}

//||||||||||||||||||||||||||||||| SHADING MAIN |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| SHADING MAIN |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| SHADING MAIN |||||||||||||||||||||||||||||||

FLightingTerms ShadeMaterial(FGBufferData gbufferData, FResolvedPixel resolvedPixel, FDeferredLightData light)
{
    switch (gbufferData.ShadingModelID)
    {
        case SHADINGMODELID_DEFAULT_LIT:
            return ShadeDefaultLit(gbufferData, resolvedPixel, light);
        case SHADINGMODELID_SUBSURFACE_PROFILE:
            return ShadeSubsurfaceProfile(gbufferData, resolvedPixel, light);
        case SHADINGMODELID_PREINTEGRATED_SKIN:
            return ShadePreintegratedSkin(gbufferData, resolvedPixel, light);
        case SHADINGMODELID_EYE:
            return ShadeEye(gbufferData, resolvedPixel, light);
        case SHADINGMODELID_HAIR:
            return ShadeHair(gbufferData, resolvedPixel, light);
        case SHADINGMODELID_CLOTH:
            return ShadeCloth(gbufferData, resolvedPixel, light);
        default:
            return MakeTerms(float3(0, 0, 0), float3(0, 0, 0), float3(0, 0, 0));
    }
}

int SelectDirectionalFogContext(PSInput input, float3 viewRay)
{
    int fallbackContext = (View_NumberOfFogContext != 1) ? 1 : 0;

    if (View_NumberOfFogContext <= 1)
    {
        return fallbackContext;
    }

    float4 distances = 0.0;

    [loop]
    for (int i = 0; i < 4 && i < View_NumberOfFogContext; ++i)
    {
        float4 region = View_FogContextRegionContext[i];
        float3 toCenter = region.xyz - View_WorldCameraOrigin;
        float rayCenter = dot(toCenter, viewRay);
        float cameraInside = dot(toCenter, toCenter) - region.w * region.w;
        float discriminant = rayCenter * rayCenter - cameraInside;

        if (cameraInside < 0.0 && discriminant >= 0.0)
        {
            float root = sqrt(discriminant);
            distances[i] = min(abs(rayCenter - root), abs(rayCenter + root));
        }
    }

    float totalDistance = dot(distances, float4(1, 1, 1, 1));
    float threshold = LoadSpatiotemporalBlueNoise(input) * 0.9999 * totalDistance;

    if (distances.x > threshold) return 0;
    if (distances.x + distances.y > threshold) return 1;
    if (distances.x + distances.y + distances.z > threshold) return 2;
    if (distances.x + distances.y + distances.z + distances.w > threshold) return 3;
    return fallbackContext;
}

float ComputeDirectionalLightContextScale(FGBufferData gbufferData, PSInput input, float3 viewRay)
{
    int packedModelAndMask = (int)round(gbufferData.GBufferB.a * 255.0);
    bool useDirectionalLightModifier = ((packedModelAndMask & 32) != 0);
    float lightScale = useDirectionalLightModifier ? View_LightModifierDirectionalLight : 1.0;

    int fogContext = SelectDirectionalFogContext(input, viewRay);

    if (fogContext != -1)
    {
        lightScale *= View_FogContextLightContext[fogContext].x;
    }

    return lightScale;
}

FResolvedPixel ResolvePixel(PSInput input, FGBufferData gbufferData, FDeferredLightData light)
{
    FResolvedPixel resolvedPixel;
    resolvedPixel.ScreenUV = ResolveScreenUV(input.ScreenUV);
    resolvedPixel.PixelPos = ResolvePixelPos(resolvedPixel.ScreenUV);
    resolvedPixel.WorldPosition = ReconstructDirectionalWorldPosition(input.CameraRay, gbufferData.DeviceDepth);
    resolvedPixel.CameraVector = SafeNormalize(-input.CameraRay);
    resolvedPixel.LightVector = light.Direction;
    resolvedPixel.LightDistanceSq = max(1.0, dot(light.Direction, light.Direction));

    float4 lightMask = LightAttenuationTexture.SampleLevel(LightAttenuationTextureSampler, resolvedPixel.ScreenUV, 0.0);
    float textureLightAttenuation = lightMask.r * lightMask.b;

    bool skipLightAttenuationTexture = ((light.LightContextBitField & 131072) == 0);
    float shadowFade = saturate(light.DistanceFadeMAD.x * LinearizeSceneDepth(gbufferData.DeviceDepth) + light.DistanceFadeMAD.y);
    float fadedTextureAttenuation = textureLightAttenuation + shadowFade * shadowFade * (1.0 - textureLightAttenuation);

    resolvedPixel.ShadowedLightAttenuation = skipLightAttenuationTexture ? 1.0 : fadedTextureAttenuation;
    resolvedPixel.DistanceAttenuation = ComputeDirectionalLightContextScale(gbufferData, input, -resolvedPixel.CameraVector);

    //if the bit is set, labels 21-22 require the faded texture attenuation to be positive before any material branch runs.
    if (!skipLightAttenuationTexture && fadedTextureAttenuation <= 0.0)
    {
        resolvedPixel.DistanceAttenuation = 0.0;
    }

    return resolvedPixel;
}

//|||||||||||||||||||||||||||||||||| MAIN PIXEL SHADER ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| MAIN PIXEL SHADER ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| MAIN PIXEL SHADER ||||||||||||||||||||||||||||||||||
//NOTE: DO NOT CHANGE THE FUNCTION NAME, otherwise we lose the entry point and can't use the shader!

//this is what executes for every pixel, the start of it for this pass
PSOutput main(PSInput input)
{
    PSOutput output;
    float2 screenUV = ResolveScreenUV(input.ScreenUV);
    int2 pixelPos = ResolvePixelPos(screenUV);
    FGBufferData graphicsBuffer = DecodeGBuffer(pixelPos);

    //optimization: early out if the material is supposed to be unlit (no light)
    if (graphicsBuffer.ShadingModelID == SHADINGMODELID_UNLIT)
    {
        output.Color = float4(0, 0, 0, 0);
        return output;
    }

    FDeferredLightData light = GetDeferredLight();
    FResolvedPixel resolvedPixel = ResolvePixel(input, graphicsBuffer, light);

    //optimization: early out if we are too far from a light
    if (resolvedPixel.DistanceAttenuation <= 0.0)
    {
        output.Color = float4(0, 0, 0, 0);
        return output;
    }

    //optimization: early out if we are already in shadow (from the shadowmap)
    if (resolvedPixel.ShadowedLightAttenuation <= 0.0)
    {
        output.Color = float4(0, 0, 0, 0);
        return output;
    }

    FLightingTerms terms = ShadeMaterial(graphicsBuffer, resolvedPixel, light);

    float3 lightScale = resolvedPixel.DistanceAttenuation * light.Color;
    float3 specularLit = terms.Specular * lightScale;
    float3 diffuseLit = terms.Diffuse * lightScale;
    float3 extraLit = terms.Extra * lightScale;

	//IMPORTANT NOTE: the game stores the final luminance color in the alpha channel
	//however IT IS NOT FINAL COLOR (diffuse + specular), it is just diffuse only????
	//if you return final color you end up killing the specular highlights on the
	//final rendered image making them look dead... why the hell can't we return the whole color?
	float3 diffuseLighting = diffuseLit + extraLit;
    float3 totalLighting = diffuseLighting + specularLit;
    float diffuseLuminance = dot(diffuseLighting.rgb, float3(0.2126, 0.7152, 0.0722));

	//combine final color (rgb) and diffuse luma (a), apply exposure to make sure everything is in range
    output.Color = float4(totalLighting, diffuseLuminance) * View_PreExposure;

	//contact shadows!
	#if defined(ENABLE_CONTACT_SHADOWS)
        #if defined(RANDOM_INTERLEAVED_GRADIENT_NOISE)
            float random = InterleavedGradientNoise(pixelPos, View_FrameNumber);
        #elif defined(RANDOM_BLUE_NOISE)
            float random = LoadSpatiotemporalBlueNoise(input);
        #else
            float random = 1.0f;
        #endif

		float contactShadow = CalculateContactShadows(pixelPos, resolvedPixel.WorldPosition, graphicsBuffer.WorldNormal, resolvedPixel.LightVector, graphicsBuffer.DeviceDepth, random);
		
        //===========================================================
        //added controls (some users want to disable contact shadows for specific materials)
        #if defined(DISABLE_CONTACT_SHADOWS_FOR_DEFAULT_LIT)
            if (graphicsBuffer.ShadingModelID == SHADINGMODELID_DEFAULT_LIT)
                contactShadow = 1.0f;
        #endif
        
        #if defined(DISABLE_CONTACT_SHADOWS_FOR_CLOTH)
            if (graphicsBuffer.ShadingModelID == SHADINGMODELID_CLOTH)
                contactShadow = 1.0f;
        #endif

        #if defined(DISABLE_CONTACT_SHADOWS_FOR_PREINTEGRATED_SKIN)
            if (graphicsBuffer.ShadingModelID == SHADINGMODELID_PREINTEGRATED_SKIN)
                contactShadow = 1.0f;
        #endif

        #if defined(DISABLE_CONTACT_SHADOWS_FOR_SUBSURFACE_PROFILE)
            if (graphicsBuffer.ShadingModelID == SHADINGMODELID_SUBSURFACE_PROFILE)
                contactShadow = 1.0f;
        #endif

        #if defined(DISABLE_CONTACT_SHADOWS_FOR_HAIR)
            if (graphicsBuffer.ShadingModelID == SHADINGMODELID_HAIR)
                contactShadow = 1.0f;
        #endif

        #if defined(DISABLE_CONTACT_SHADOWS_FOR_EYE)
            if (graphicsBuffer.ShadingModelID == SHADINGMODELID_EYE)
                contactShadow = 1.0f;
        #endif
        //===========================================================
        
        output.Color *= lerp(1.0f, contactShadow, CONTACT_SHADOWS_STRENGTH);
		//output.Color = lerp(1.0f, contactShadow, CONTACT_SHADOWS_STRENGTH); //debug
	#endif

    //applying screen-space ambient occlusion to the direct light term
    //technically this is inaccurate and inplausible... but artistically some people really like it so lets just give them the power of choice!
    #if defined(APPLY_SSAO_IN_DIRECT_LIGHT)
        output.Color *= pow(graphicsBuffer.ScreenAO, SSAO_POWER);
    #endif

    return output;
}
