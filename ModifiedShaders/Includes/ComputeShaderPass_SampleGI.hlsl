//SampleGI.hlsl

//library includes
//NOTE: this is where we have various useful shader functions
#include "LibraryMath.hlsl"
#include "LibraryRandom.hlsl"
#include "LibraryColor.hlsl"
#include "LibraryGBuffer.hlsl"

//#define DEFAULT_GAME_SHADING

#define CORRECT_REFLECTION_DIRECTION

//by default it appears that the irradiance volume data is basically ambient cube
//it's efficent but not the best quality and has it's problems
//but we can project those irradiance coefficents into SH to get the data in spherical harmonics.
#define CONVERT_TO_SPHERICAL_HARMONICS

//derrive a specular highlight based on the dominant direction of ambient light
//this helps give specular materials a very nice and plausible/accurate kick of reflection that is based on the baked ambient global illumination
//I HIGHLY recomend keeping this on
#define SPHERICAL_HARMONICS_DOMINANT_SPECULAR_HIGHLIGHT
#define SPHERICAL_HARMONICS_DOMINANT_SPECULAR_HIGHLIGHT_BOOST 2.0

//#define SPHERICAL_HARMONICS_DOMINANT_DIRECTION_DIFFUSE

//in plain english: this is more accurate, and leads to less contrast
//technical yapping: this evaluates the converted spherical harmonics "irradiance/diffuse" global illumination term using zonal harmonics
//where the coefficents we reconstructed are effectively order 2, but with zonal harmonics we can "hallucinate" a 3rd order leading to less ringing
#define SPHERICAL_HARMONICS_IRRADIANCE_ZH3

//in plain english: this is more accurate, and leads to less contrast
//technical yapping: this evaluates the converted spherical harmonics "radiance/reflection" global illumination term using zonal harmonics
//where the coefficents we reconstructed are effectively order 2, but with zonal harmonics we can "hallucinate" a 3rd order leading to less ringing
#define SPHERICAL_HARMONICS_RADIANCE_ZH3

//derrive a specular highlight from the camera
//this is more artistic admittedly, and is not entirely accurate/plausible unless there is a light at camera
//with that said this also like the dominant SH specular highlight gives a very nice kick to the reflection term for the baked ambient global illumination
#define CAMERA_VIEW_SPECULAR_HIGHLIGHT

//|||||||||||||||||||||||||||||||||| CONFIGURATION - CONTACT SHADOWS ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| CONFIGURATION - CONTACT SHADOWS ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| CONFIGURATION - CONTACT SHADOWS ||||||||||||||||||||||||||||||||||
//here are parameters that are wired up for easy tweakin...

//this changes the noise pattern every frame, which with Temporal Anti-Aliasing (or DLSS or anything related)
//you want to do, that way samples change every frame and results get blended together over time for a better final apperance
#define RANDOM_ANIMATE_NOISE

//the main attraction, contact shadows!
//in short it raymarches against the scene depth buffer to estimate shadows
//significantly improves overall shadow quality
//#define ENABLE_CONTACT_SHADOWS

//this directly controls the quality of the contact shadows, the more the better!
//RANGE: this should be between [4 <---> 128]
//DEFAULT: 8
//HIGHER VALUES: better quality shadows, less noise (more stable), and denser but expensive performance
//LOWER VALUES: lower quality shadows, more noise (less stable), and lighter but cheaper performance
#define CONTACT_SHADOWS_SAMPLES 8

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
//high values = reduced acne but can introduce visual issues where shadows appear less grounded
//low values = potentially increased acne but keeps shadows grounded
//RANGE: this should be between [0.0 <---> 5.0]
//DEFAULT: 0.1
#define CONTACT_SHADOWS_BIAS 0.1

//this is a small bias factor to minimize contact shadow acne on sloped surfaces for hair specifically
//RANGE: this should be between [0.0 <---> 5.0]
//high values = reduced acne but can introduce visual issues where shadows appear less grounded
//low values = potentially increased acne but keeps shadows grounded
//RANGE: this should be between [0.0 <---> 5.0]
//DEFAULT: 0.1
#define CONTACT_SHADOWS_BIAS_HAIR 0.1

//this is a small bias factor to minimize contact shadow acne on sloped surfaces using surface normal
//RANGE: this should be between [0.0 <---> 5.0]
//high values = reduced acne but can introduce visual issues where shadows appear less grounded
//low values = potentially increased acne but keeps shadows grounded
//RANGE: this should be between [0.0 <---> 5.0]
//DEFAULT: 0.1
#define CONTACT_SHADOWS_NORMAL_BIAS 0.1

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

//another requested feature...
//this is an added effect that will gradually "fade" contact shadows as it goes further out
//this does have a bit of a perf hit with (extra instructions per iteration in the loop now)
//I've done my best to optimize and keep it light, but it just requires more instructions to achieve
//still, in the grand scheme of things this should be pretty light and enabled if you really want to mitigate some of shortcomings (atleast its not another texture sample)
#define CONTACT_SHADOWS_FALLOFF

//this shapes the falloff
//higher values = sharper/darker shadow further out
//lower values = softer/lighter shadow further out
//DEFAULT: 4
#define CONTACT_SHADOWS_FALLOFF_CONTRAST 3.0

//|||||||||||||||||||||||||||||||||| RESOURCES ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| RESOURCES ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| RESOURCES ||||||||||||||||||||||||||||||||||
//resources passed in to the shader

SamplerState View_SharedBilinearClampedSampler : register(s0, space0);

Texture2D<float4> GBufferATexture   : register(t0, space0);
Texture2D<float4> GBufferBTexture   : register(t1, space0);
Texture2D<float4> GBufferDTexture   : register(t2, space0);
Texture2D<float4> SceneDepthTexture : register(t3, space0);

RWTexture2D<float4> RWEnvironmentIrradianceATexture : register(u0, space0);
RWTexture2D<float4> RWEnvironmentIrradianceBTexture : register(u1, space0);

//|||||||||||||||||||||||||||||||||| CONSTANT BUFFERS ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| CONSTANT BUFFERS ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| CONSTANT BUFFERS ||||||||||||||||||||||||||||||||||
//game data passed in to the shader

cbuffer _Globals : register(b0) 
{
	float4 DistantViewEnvironmentContext : packoffset(c0.x);
	int NumPrecomputedLightEnvironments : packoffset(c1.x);
	int bSceneLightingChannelsValid : packoffset(c1.y);
	float4 HZBCoordinateContext : packoffset(c2.x);
	float3 TranslucentLightingVolumeMin : packoffset(c3.x);
	float TranslucentLightingVolumeVoxelSize : packoffset(c3.w);
	int TranslucentLightingVolumeDim : packoffset(c4.x);
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

cbuffer PLESceneInfo : register(b2) 
{
	int PLESceneInfo_ReflectionOffsets[256] : packoffset(c0.x);
	int PrePadding_PLESceneInfo_4084 : packoffset(c255.y);
	int PrePadding_PLESceneInfo_4088 : packoffset(c255.z);
	int PrePadding_PLESceneInfo_4092 : packoffset(c255.w);
	int PLESceneInfo_NumProbeBounds[256] : packoffset(c256.x);
	int PrePadding_PLESceneInfo_8180 : packoffset(c511.y);
	int PrePadding_PLESceneInfo_8184 : packoffset(c511.z);
	int PrePadding_PLESceneInfo_8188 : packoffset(c511.w);
	float4 PLESceneInfo_ReflectionProperties[256] : packoffset(c512.x);
	float4 PLESceneInfo_PLEPositions[256] : packoffset(c768.x);
};

//|||||||||||||||||||||||||||||||||| ARRAY RESOURCES ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| ARRAY RESOURCES ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| ARRAY RESOURCES ||||||||||||||||||||||||||||||||||
//array resources passed in to the shader

struct FPrecomputedLightEnvironmentProbeBoundsInfo
{
    uint3 GridSize;
    uint  HierarchyLog2;

    //stored as the four rows of a row-vector transform. 
	//only xyz of the transformed position is used.
    float4 LocalToGridRow0;
    float4 LocalToGridRow1;
    float4 LocalToGridRow2;
    float4 LocalToGridRow3;

    //present in the 144-byte source structure but unused by this shader?
    float4 UnknownTransformRow0;
    float4 UnknownTransformRow1;
    float4 UnknownTransformRow2;
    float4 UnknownTransformRow3;
};

struct FPrecomputedLightEnvironmentProbe
{
    float4 PositionAndUnknown;
    int    Unknown0;
    int    Unknown1;
    int    DistanceCubeSlice;
    int    Unknown2;
};

struct FPrecomputedLightEnvironmentProbeBound
{
    int   ProbeIndices[8];
    uint3 GridOrigin;
    uint  PackedCellSize;
};

StructuredBuffer<FPrecomputedLightEnvironmentProbeBoundsInfo> PrecomputedLightProbeBoundsInfoSRVs[] : register(t0, space20);
StructuredBuffer<float> PrecomputedLightDataSRVs[] : register(t0, space3);
StructuredBuffer<FPrecomputedLightEnvironmentProbe> PrecomputedLightProbeSRVs[] : register(t0, space5);
StructuredBuffer<FPrecomputedLightEnvironmentProbeBound> PrecomputedLightProbeBoundsSRVs[] : register(t0, space7);
StructuredBuffer<int> PrecomputedLightProbeBoundsIndicesSRVs[] : register(t0, space8);

TextureCubeArray<float4> DistanceCubeArray[] : register(t0, space10);

Buffer<float> OccluderTrianglesSRVs[] : register(t0, space11);
Buffer<uint> OccluderIndicesSRVs[] : register(t0, space12);

//|||||||||||||||||||||||||||||||||| STRUCTS ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| STRUCTS ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| STRUCTS ||||||||||||||||||||||||||||||||||

struct FProbeDirectionalField
{
    float3 BaseColor;
    float3 PositiveAxisLobes;
    float3 NegativeAxisLobes;
};

//CUSTOM
//NOTE TO SELF: because the original probe data is essentially ambient cube
//this pretty much means that we can only get so much resolution out of what is basically diffuse/irradiance
//so for this we should only need up to order 2 spherical harmonics
struct SphericalHarmonicsData
{
    float3 Coefficient[9];
};

//|||||||||||||||||||||||||||||||||| MATH ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| MATH ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| MATH ||||||||||||||||||||||||||||||||||

static const float PI            = 3.14159265359;
static const float PI_TWO        = 6.28318530717;
static const float INV_PI        = 0.31830988618;
static const float INV_4PI       = 0.0795775;
static const float MIN_ROUGHNESS = 0.04;

static const uint INVALID_INDEX_28 = 0x0fffffffu;
static const uint INDEX_MASK_28    = 0x0fffffffu;

//|||||||||||||||||||||||||||||||||| SPACE CONVERSIONS ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| SPACE CONVERSIONS ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| SPACE CONVERSIONS ||||||||||||||||||||||||||||||||||

float ConvertFromDeviceZ(float deviceZ)
{
    return deviceZ * View_InvDeviceZToWorldZTransform.x + View_InvDeviceZToWorldZTransform.y + rcp(deviceZ * View_InvDeviceZToWorldZTransform.z - View_InvDeviceZToWorldZTransform.w);
}

float3 ReconstructWorldPosition(uint2 localPixel, float worldDepth)
{
    float2 pixelCenter = float2(localPixel) + 0.5f;
    float2 screenPosition;
    screenPosition.x = pixelCenter.x * (2.0f * View_ViewSizeAndInvSize.z) - 1.0f;
    screenPosition.y = 1.0f - pixelCenter.y * (2.0f * View_ViewSizeAndInvSize.w);

    //perspective projections need x/y multiplied by linear depth. For an orthographic projection ViewToClip[3][3] is 1, so the multiplier is 1.
    float perspectiveMultiplier = View_ViewToClip[3][3] < 1.0f ? worldDepth : 1.0f;
    float4 screenVector = float4(screenPosition * perspectiveMultiplier, worldDepth, 1.0f);

    return mul(screenVector, View_ScreenToWorld).xyz;
}

float3 TransformEnvironmentPositionToGrid(
    float3 environmentLocalPosition,
    FPrecomputedLightEnvironmentProbeBoundsInfo info)
{
    float4 p = float4(environmentLocalPosition, 1.0f);
    return float3(
        dot(p, float4(info.LocalToGridRow0.x, info.LocalToGridRow1.x, info.LocalToGridRow2.x, info.LocalToGridRow3.x)),
        dot(p, float4(info.LocalToGridRow0.y, info.LocalToGridRow1.y, info.LocalToGridRow2.y, info.LocalToGridRow3.y)),
        dot(p, float4(info.LocalToGridRow0.z, info.LocalToGridRow1.z, info.LocalToGridRow2.z, info.LocalToGridRow3.z)));
}

bool FindProbeBounds(
    float3 worldPosition,
    out uint environmentIndex,
    out uint boundsIndex,
    out float3 environmentLocalPosition,
    out float3 gridPosition)
{
    environmentIndex = 0xffffffffu;
    boundsIndex = INVALID_INDEX_28;
    environmentLocalPosition = 0.0f;
    gridPosition = 0.0f;

    [loop]
    for (uint candidate = 0; candidate < (uint)NumPrecomputedLightEnvironments; ++candidate)
    {
        // -2 in the bit representation of w marks an inactive environment.
        float4 environment = PLESceneInfo_PLEPositions[candidate];

        if (asint(environment.w) == -2)
            continue;

        uint descriptor = NonUniformResourceIndex(candidate);
        FPrecomputedLightEnvironmentProbeBoundsInfo info = PrecomputedLightProbeBoundsInfoSRVs[descriptor][0];

        float3 localPosition = worldPosition - environment.xyz;
        float3 candidateGridPosition = TransformEnvironmentPositionToGrid(localPosition, info);

        if (any(candidateGridPosition < 0.0f) || any(candidateGridPosition >= float3(info.GridSize)))
        {
            continue;
        }

        uint3 integerGridPosition = (uint3)floor(candidateGridPosition);

        // The first indirection indexes a coarse 3-D grid. Its high nibble
        // tells how many hierarchy bits must be resolved by a second lookup.
        uint coarseShift = (info.HierarchyLog2 - 1u) & 31u;
        uint3 coarseCell = integerGridPosition >> coarseShift;
        uint cellWidth = 1u << coarseShift;
        uint3 coarseDimensions = (info.GridSize + cellWidth - 1u) >> coarseShift;
        uint coarseLinearIndex = (coarseCell.z * coarseDimensions.y + coarseCell.y) * coarseDimensions.x + coarseCell.x;

        int packedEntry = PrecomputedLightProbeBoundsIndicesSRVs[descriptor][coarseLinearIndex];
        
		if (packedEntry == -1)
            continue;

        uint packedEntryBits = (uint)packedEntry;
        uint entryIndex = packedEntryBits & INDEX_MASK_28;
        uint refinementBits = packedEntryBits >> 28u;

        if (refinementBits != 0u)
        {
            uint fineShift = (refinementBits - 1u) & 31u;
            uint remainingShift = (info.HierarchyLog2 - refinementBits) & 31u;
            uint3 positionWithinCoarseCell = integerGridPosition - (coarseCell << coarseShift);
            uint3 fineCell = positionWithinCoarseCell >> fineShift;

            uint fineRowPitch = 1u << remainingShift;
            uint fineSlicePitch = fineRowPitch * fineRowPitch;
            uint fineIndex = entryIndex + fineCell.x + fineCell.y * fineRowPitch + fineCell.z * fineSlicePitch;

            packedEntry = PrecomputedLightProbeBoundsIndicesSRVs[descriptor][fineIndex];

            if (packedEntry == -1)
            {
                // Unlike an empty coarse cell, an empty refinement entry ends
                // the search in the captured shader and produces black.
                return false;
            }

            entryIndex = (uint)packedEntry & INDEX_MASK_28;
        }

        if (entryIndex == INVALID_INDEX_28)
            return false;

        environmentIndex = candidate;
        boundsIndex = entryIndex;
        environmentLocalPosition = localPosition;
        gridPosition = candidateGridPosition;
        return true;
    }

    return false;
}

float EvaluateDistanceCubeVisibility(
    uint descriptor,
    int distanceCubeSlice,
    float3 directionFromProbeToPoint,
    float probeDistance)
{
    if (distanceCubeSlice == -1)
        return 1.0f;

    float2 distanceMoments = DistanceCubeArray[descriptor].SampleLevel(View_SharedBilinearClampedSampler, float4(directionFromProbeToPoint, (float)distanceCubeSlice), 0.0f).rg;

    // The capture stores distances and squared distances in hundredths.
    float meanDistance = distanceMoments.x * 100.0f;
    float secondMoment = distanceMoments.y * 100.0f;
    float biasedMean = meanDistance + 1.0f;

    if (probeDistance < biasedMean)
        return 1.0f;

    float variance = abs(meanDistance * meanDistance - secondMoment) + 1.0f;
    float delta = probeDistance - biasedMean;
    return variance / (delta * delta + variance);
}

bool IntersectsTriangleSegment(
    float3 rayOrigin,
    float3 rayDirection,
    float rayLength,
    float3 vertex0,
    float3 vertex1,
    float3 vertex2)
{
    // Moller-Trumbore intersection, matching the DXIL's epsilon and bounds.
    float3 edge1 = vertex1 - vertex0;
    float3 edge2 = vertex2 - vertex0;
    float3 p = cross(rayDirection, edge2);
    float determinant = dot(edge1, p);

    if (abs(determinant) < 1.0e-6f)
        return false;

    float inverseDeterminant = rcp(determinant);
    float3 t = rayOrigin - vertex0;
    float u = dot(t, p) * inverseDeterminant;
    if (u < 0.0f || u > 1.0f)
        return false;

    float3 q = cross(t, edge1);
    float v = dot(rayDirection, q) * inverseDeterminant;
    if (v < 0.0f || u + v > 1.0f)
        return false;

    float hitDistance = dot(edge2, q) * inverseDeterminant;
    return hitDistance >= 1.0e-6f && hitDistance < rayLength;
}

float EvaluateTriangleVisibility(
    uint environmentIndex,
    uint descriptor,
    uint boundsIndex,
    float3 rayOrigin,
    float3 rayDirection,
    float rayLength)
{
    uint packedOccluderRange = OccluderIndicesSRVs[descriptor][boundsIndex];
    uint triangleCount = packedOccluderRange & 0xffu;
    uint firstTriangleIndex = packedOccluderRange >> 8u;

    if (packedOccluderRange == 0u || triangleCount == 0u)
        return 1.0f;

    uint indexTableBase = (uint)PLESceneInfo_NumProbeBounds[environmentIndex];

    [loop]
    for (uint triangleOffset = 0; triangleOffset < triangleCount; ++triangleOffset)
    {
        uint triangleIndex = OccluderIndicesSRVs[descriptor][indexTableBase + firstTriangleIndex + triangleOffset];
        uint firstFloat = triangleIndex * 9u;

        float3 vertex0 = float3(
            OccluderTrianglesSRVs[descriptor][firstFloat + 0u],
            OccluderTrianglesSRVs[descriptor][firstFloat + 1u],
            OccluderTrianglesSRVs[descriptor][firstFloat + 2u]);
        float3 vertex1 = float3(
            OccluderTrianglesSRVs[descriptor][firstFloat + 3u],
            OccluderTrianglesSRVs[descriptor][firstFloat + 4u],
            OccluderTrianglesSRVs[descriptor][firstFloat + 5u]);
        float3 vertex2 = float3(
            OccluderTrianglesSRVs[descriptor][firstFloat + 6u],
            OccluderTrianglesSRVs[descriptor][firstFloat + 7u],
            OccluderTrianglesSRVs[descriptor][firstFloat + 8u]);

        if (IntersectsTriangleSegment(rayOrigin, rayDirection, rayLength, vertex0, vertex1, vertex2))
        {
            return 0.0f;
        }
    }

    return 1.0f;
}

FProbeDirectionalField BlendProbeField(
    uint environmentIndex,
    uint boundsIndex,
    float3 environmentLocalPosition,
    float3 gridPosition)
{
    uint descriptor = NonUniformResourceIndex(environmentIndex);
    FPrecomputedLightEnvironmentProbeBound bounds = PrecomputedLightProbeBoundsSRVs[descriptor][boundsIndex];

    float cellSize = (float)(bounds.PackedCellSize & 0xffu);
    float3 cellFraction = saturate((gridPosition - float3(bounds.GridOrigin)) / cellSize);

    float accumulatedLogCoefficients[9] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    float totalWeight = 0.0f;

    [unroll]
    for (uint corner = 0; corner < 8u; ++corner)
    {
        int probeIndex = bounds.ProbeIndices[corner];

        if (probeIndex == -1)
            continue;

        FPrecomputedLightEnvironmentProbe probe = PrecomputedLightProbeSRVs[descriptor][probeIndex];

        float3 vectorToProbe = probe.PositionAndUnknown.xyz - environmentLocalPosition;
        float probeDistance = length(vectorToProbe);
        float3 directionToProbe = vectorToProbe / probeDistance;

        float3 cornerSelector = float3((corner >> 0u) & 1u, (corner >> 1u) & 1u, (corner >> 2u) & 1u);
        float3 cornerWeights = lerp(1.0f - cellFraction, cellFraction, cornerSelector);
        float trilinearWeight = cornerWeights.x * cornerWeights.y * cornerWeights.z;

        float distanceVisibility = EvaluateDistanceCubeVisibility(descriptor, probe.DistanceCubeSlice, -directionToProbe, probeDistance);
        float triangleVisibility = EvaluateTriangleVisibility(environmentIndex, descriptor, boundsIndex, environmentLocalPosition, directionToProbe, probeDistance);
        float weight = trilinearWeight * distanceVisibility * triangleVisibility;

        uint coefficientBase = (uint)probeIndex * 9u;

        [unroll]
        for (uint coefficient = 0; coefficient < 9u; ++coefficient)
        {
            float value = PrecomputedLightDataSRVs[descriptor][coefficientBase + coefficient];
            accumulatedLogCoefficients[coefficient] += log2(max(value, 1.0e-9f)) * weight;
        }

        totalWeight += weight;
    }

    FProbeDirectionalField field = (FProbeDirectionalField)0;

    if (totalWeight > 0.0f)
    {
        float inverseWeight = rcp(totalWeight);
        float coefficients[9];

        [unroll]
        for (uint coefficient = 0; coefficient < 9u; ++coefficient)
            coefficients[coefficient] = exp2(accumulatedLogCoefficients[coefficient] * inverseWeight);

		//NOTE TO SELF: it looks like they are doing ambient cube... what the hell? why not SH?
        field.BaseColor         = float3(coefficients[0], coefficients[1], coefficients[2]);
        field.PositiveAxisLobes = float3(coefficients[3], coefficients[4], coefficients[5]);
        field.NegativeAxisLobes = float3(coefficients[6], coefficients[7], coefficients[8]);
    }

    return field;
}

float3 EvaluateDirectionalField(FProbeDirectionalField field, float3 direction)
{
    float3 positiveDirection = saturate(direction);
    float3 negativeDirection = saturate(-direction);
    float directionalIntensity = dot(field.PositiveAxisLobes, positiveDirection * positiveDirection) + dot(field.NegativeAxisLobes, negativeDirection * negativeDirection);
    float baseLuminance = dot(field.BaseColor, float3(0.2126f, 0.7152f, 0.0722f));
    float inverseBaseLuminance = baseLuminance > 0.0f ? rcp(baseLuminance) : 0.0f;
    return field.BaseColor * (directionalIntensity * inverseBaseLuminance);
}

float3 ComputeDominantReflectionDirection(
    float3 worldNormal,
    float3 directionToCamera,
    float roughness,
    uint shadingModelId)
{
    #if defined(CORRECT_REFLECTION_DIRECTION)
	    //im not sure why on earth the game decides to stretch reflections with roughness
	    return normalize(reflect(-directionToCamera, worldNormal));
    #else
        //these two material-specific roughness adjustments are explicit in DXIL for some reason
        if (shadingModelId == SHADINGMODELID_PREINTEGRATED_SKIN)
            roughness *= 0.93655f;
        else if (shadingModelId == SHADINGMODELID_HAIR)
            roughness = 0.48f;
        float normalDotView = dot(worldNormal, directionToCamera);
        float3 reflectedDirection = normalize(2.0f * (normalDotView * worldNormal - directionToCamera) + abs(normalDotView) * worldNormal);
        return normalize(lerp(reflectedDirection, worldNormal, roughness * roughness * roughness));
    #endif
}

//||||||||||||||||||||||||||||||| SPHERICAL HARMONICS |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| SPHERICAL HARMONICS |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| SPHERICAL HARMONICS |||||||||||||||||||||||||||||||
//NOTE: it looks like unfortunately probe data is very similar to ambient cube?
//this is perplexing and unfortunately means I can't do some cool shading stuff... but....
//we can convert this "ambient cube" into spherical harmonics by reprojecting ambient cube into SH coefficents
//this will allow us to do stuff with spherical harmonics!

SphericalHarmonicsData AmbientCubeToSH2(FProbeDirectionalField field)
{
    static const float Y00  = 0.2820947918f;
    static const float Y1   = 0.4886025119f;
    static const float Y20  = 0.3153915653f;
    static const float Y22  = 0.5462742153f;

    // Symmetric and signed portions of each axis pair.
    float3 axisAverage = 0.5f * (field.PositiveAxisLobes + field.NegativeAxisLobes);

    float3 axisDifference = 0.5f * (field.PositiveAxisLobes - field.NegativeAxisLobes);

    float scalarSH[9];

	//order 0
    scalarSH[0] = Y00 * (4.0f * PI / 3.0f) * (axisAverage.x + axisAverage.y + axisAverage.z);

	//order 1
    scalarSH[1] = Y1 * PI * axisDifference.y;
    scalarSH[2] = Y1 * PI * axisDifference.z;
    scalarSH[3] = Y1 * PI * axisDifference.x;

	//order 2
    scalarSH[4] = 0.0f;
    scalarSH[5] = 0.0f;
    scalarSH[6] = Y20 * (8.0f * PI / 15.0f) * (2.0f * axisAverage.z - axisAverage.x - axisAverage.y);
    scalarSH[7] = 0.0f;
    scalarSH[8] = Y22 * (8.0f * PI / 15.0f) * (axisAverage.x - axisAverage.y);

    float baseLuminance = dot(field.BaseColor, float3(0.2126f, 0.7152f, 0.0722f));
    float3 chromaticity = baseLuminance > 1e-6f ? field.BaseColor / baseLuminance : 0.0f;

    SphericalHarmonicsData result;

    [unroll]
    for (uint i = 0; i < 9; ++i)
        result.Coefficient[i] = chromaticity * scalarSH[i];

    return result;
}

//||||||||||||||||||||||||||||||| SPHERICAL HARMONICS IRRADIANCE |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| SPHERICAL HARMONICS IRRADIANCE |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| SPHERICAL HARMONICS IRRADIANCE |||||||||||||||||||||||||||||||
//technically the "ambient cube" probe data that we have should already be convolved for irradiance

float3 EvaluateIrradiance(SphericalHarmonicsData sh, float3 direction)
{
	float3 color = float3(0, 0, 0);

	float basis[9] =
    {
        0.2820947918f,
        0.4886025119f * direction.y,
        0.4886025119f * direction.z,
        0.4886025119f * direction.x,
        1.0925484306f * direction.x * direction.y,
        1.0925484306f * direction.y * direction.z,
        0.3153915653f * (3.0f * direction.z * direction.z - 1.0f),
        1.0925484306f * direction.x * direction.z,
        0.5462742153f * (direction.x * direction.x - direction.y * direction.y)
    };

    [unroll]
    for (uint i = 0; i < 9; ++i)
        color += sh.Coefficient[i] * basis[i];

	return color;
}

//||||||||||||||||||||||||||||||| SPHERICAL HARMONICS RADIANCE |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| SPHERICAL HARMONICS RADIANCE |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| SPHERICAL HARMONICS RADIANCE |||||||||||||||||||||||||||||||
//technically the "ambient cube" probe data that we have should already be convolved for irradiance

struct FSHDominantLight
{
    float3 Direction;
    float3 PeakRadiance;
    float3 HighlightRadiance;
    float  Directionality;
};

FSHDominantLight ExtractDominantLight(SphericalHarmonicsData radianceSH)
{
    static const float Y00 = 0.2820947918f;
    static const float Y1  = 0.4886025119f;

    float3 firstMoment = float3(
        LuminanceRec709(radianceSH.Coefficient[3]), // X
        LuminanceRec709(radianceSH.Coefficient[1]), // Y
        LuminanceRec709(radianceSH.Coefficient[2])  // Z
    );

    float momentLength = length(firstMoment);

    FSHDominantLight light;
    light.Direction = momentLength > 1e-6f ? firstMoment / momentLength : float3(0.0f, 0.0f, 1.0f);

    float dcCoefficient = max(LuminanceRec709(radianceSH.Coefficient[0]), 1e-6f);

    // Ratio between the directional moment and total spherical energy.
    // For a nonnegative radiance distribution this should lie in [0,1].
    light.Directionality = saturate((Y00 / Y1) * momentLength / dcCoefficient);

    light.PeakRadiance = max(EvaluateIrradiance(radianceSH, light.Direction), 0.0f);

    float3 averageRadiance = max(radianceSH.Coefficient[0] * Y00, 0.0f);

    // Remove the omnidirectional component so uniform lighting does not
    // manufacture an artificial directional highlight.
    light.HighlightRadiance = max(light.PeakRadiance - averageRadiance, 0.0f) * light.Directionality;

    return light;
}

SphericalHarmonicsData DeconvolveLambertianIrradiance(SphericalHarmonicsData irradianceSH)
{
    SphericalHarmonicsData radianceSH;

    // L0
	//runtime: coefficent / PI
	//precomputed: coefficent * INV_PI
    radianceSH.Coefficient[0] = irradianceSH.Coefficient[0] * INV_PI;

    // L1
	//runtime: coefficent * (3.0 / (2.0 * PI)) 
	//precomputed: 0.47746482927568600730665129011754
	radianceSH.Coefficient[1] = irradianceSH.Coefficient[1] * 0.47746482927568;
	radianceSH.Coefficient[2] = irradianceSH.Coefficient[2] * 0.47746482927568;
	radianceSH.Coefficient[3] = irradianceSH.Coefficient[3] * 0.47746482927568;

    // L2
	//runtime: coefficent * (4.0 / PI)
	//precomputed: 1.2732395447351626861510701069801
	radianceSH.Coefficient[4] = irradianceSH.Coefficient[4] * 1.273239544735162;
	radianceSH.Coefficient[5] = irradianceSH.Coefficient[5] * 1.273239544735162;
	radianceSH.Coefficient[6] = irradianceSH.Coefficient[6] * 1.273239544735162;
	radianceSH.Coefficient[7] = irradianceSH.Coefficient[7] * 1.273239544735162;
	radianceSH.Coefficient[8] = irradianceSH.Coefficient[8] * 1.273239544735162;

    return radianceSH;
}

float DistributionGGX(float NoH, float roughness)
{
    float alpha  = roughness * roughness;
    float alpha2 = alpha * alpha;

    float denominator = NoH * NoH * (alpha2 - 1.0f) + 1.0f;

    return alpha2 / max(PI * denominator * denominator, 1e-6f);
}

float GeometrySchlickGGX(float NoX, float roughness)
{
    float k = roughness + 1.0f;
    k = (k * k) * 0.125f;

    return NoX / max(NoX * (1.0f - k) + k, 1e-6f);
}

float3 EvaluateRadiance(SphericalHarmonicsData sh, float3 direction)
{
	float3 color = float3(0, 0, 0);

	float basis[9] =
    {
        0.2820947918f,
        0.4886025119f * direction.y,
        0.4886025119f * direction.z,
        0.4886025119f * direction.x,
        1.0925484306f * direction.x * direction.y,
        1.0925484306f * direction.y * direction.z,
        0.3153915653f * (3.0f * direction.z * direction.z - 1.0f),
        1.0925484306f * direction.x * direction.z,
        0.5462742153f * (direction.x * direction.x - direction.y * direction.y)
    };

	sh = DeconvolveLambertianIrradiance(sh);

    [unroll]
    for (uint i = 0; i < 9; ++i)
        color += sh.Coefficient[i] * basis[i];

	return color;
}

float3 EvaluateDominantSHHighlight(
    FSHDominantLight light,
    float3 normal,
    float3 directionToCamera,
    float roughness)
{
	//NOTE TO SELF: we ignore F0 and fresnel, as this is handled in a later pass when we combine specular
	//we only want a sharp highlight that scales with surface roughness
	//because in theory, the "radiance" should be just a cubemap

    float3 N = normal;
    float3 V = directionToCamera;
    float3 L = light.Direction;

    float NoL = saturate(dot(N, L));
    float NoV = saturate(dot(N, V));

    if (NoL <= 0.0f || NoV <= 0.0f)
        return 0.0f;

    float3 H = normalize(V + L);

    float NoH = saturate(dot(N, H));
    float VoH = saturate(dot(V, H));

    //roughness /= light.Directionality;

    float D = DistributionGGX(NoH, roughness);
    float G = GeometrySchlickGGX(NoV, roughness) * GeometrySchlickGGX(NoL, roughness);

    float3 specularBRDF = (D * G) / max(4.0f * NoV * NoL, 1e-6f);

    return light.HighlightRadiance * specularBRDF * NoL;
	//return light.HighlightRadiance * specularBRDF * NoL * MATH_PI_TWO;
    //return light.HighlightRadiance * specularBRDF * NoL * MATH_PI_TWO * (1.0f + light.Directionality);
    //return light.HighlightRadiance * specularBRDF * NoL * PI * (1.0f + rcp(light.Directionality));
}

float3 CalculateViewSpecularHighlight(
    float3 normal,
    float3 directionToCamera,
    float roughness)
{
	//NOTE TO SELF: we ignore F0 and fresnel, as this is handled in a later pass when we combine specular
	//we only want a sharp highlight that scales with surface roughness
	//because in theory, the "radiance" should be just a cubemap

    float3 N = normal;
    float3 V = directionToCamera;
    float3 L = directionToCamera;

    float NoL = saturate(dot(N, L));
    float NoV = saturate(dot(N, V));

    if (NoL <= 0.0f || NoV <= 0.0f)
        return 0.0f;

    float3 H = normalize(V + L);

    float NoH = saturate(dot(N, H));
    float VoH = saturate(dot(V, H));

    //roughness /= light.Directionality;

    float D = DistributionGGX(NoH, roughness);
    float G = GeometrySchlickGGX(NoV, roughness) * GeometrySchlickGGX(NoL, roughness);

    float3 specularBRDF = (D * G) / max(4.0f * NoV * NoL, 1e-6f);

    return specularBRDF * NoL;
}









//||||||||||||||||||||||||||||||| CONTACT SHADOWS |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| CONTACT SHADOWS |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| CONTACT SHADOWS |||||||||||||||||||||||||||||||

float LinearEyeDepth(float sceneDepth)
{
    return View_InvDeviceZToWorldZTransform.z / (sceneDepth - View_InvDeviceZToWorldZTransform.w);
}

float LinearizeSceneDepth(float deviceZ)
{
    return View_InvDeviceZToWorldZTransform.x * deviceZ + View_InvDeviceZToWorldZTransform.y + rcp(View_InvDeviceZToWorldZTransform.z * deviceZ - View_InvDeviceZToWorldZTransform.w);
}

void ClipAgainstPlane(float startDistance, float endDistance, inout float exitT)
{
    if (endDistance < 0.0)
    {
        float denominator = startDistance - endDistance;

        if (denominator > 1e-6)
            exitT = min(exitT, startDistance / denominator);
    }
}

float FastContactShadowClipSpace(
    float3 worldPosition, 
    float3 worldNormal,
    float3 lightDirection, 
    float random,
    float rawDepth,
	int shadingModelID)
{
    const float invSamples = rcp((float)CONTACT_SHADOWS_SAMPLES);

    //approximation of how big a pixel is
    //this is because at low resolutions biasing / noise issues get really bad
    //but intrestingly at higher and higher resolutions the biasing/noise issues go away
    //so this means that for the most part we should factor in the pixel scale of the render target
    //for inv size, natrually lower resolutions will have a larger number (higher res lower)
    //so we can use this to scale our set bias factor
    float pixelSize = max(View_BufferSizeAndInvSize.z, View_BufferSizeAndInvSize.w);
    pixelSize *= 100.0f; //this 100 is arbitrary

    float contactShadowBias = pixelSize * CONTACT_SHADOWS_BIAS;

    if(shadingModelID == SHADINGMODELID_HAIR)
		contactShadowBias = pixelSize * CONTACT_SHADOWS_BIAS_HAIR;

	//apply normal bias to help mitigate self-shadowing issues
    worldPosition += worldNormal * (pixelSize * CONTACT_SHADOWS_NORMAL_BIAS);

    float3 rayOrigin = worldPosition + lightDirection * contactShadowBias;
    float3 rayEnd    = rayOrigin + lightDirection * CONTACT_SHADOWS_RAY_LENGTH;

    float4 clipStart = mul(View_WorldToClip, float4(rayOrigin, 1.0));
    float4 clipEnd   = mul(View_WorldToClip, float4(rayEnd, 1.0));

    //FIX: do not trace rays behind camera
    if (clipStart.w <= 1e-5)
        return 1.0;

    //FIX: stop rays from tracing outside of the camera viewports visible region
    //this is to reduce wierd funky self shadowing artifacts when we get too close to light sources
    float rayExitT = 1.0;
    ClipAgainstPlane(clipStart.w - 1e-5, clipEnd.w - 1e-5, rayExitT); //in front of the camera.
    ClipAgainstPlane(clipStart.x + clipStart.w, clipEnd.x + clipEnd.w, rayExitT); //left: x >= -w.
    ClipAgainstPlane(clipStart.w - clipStart.x, clipEnd.w - clipEnd.x, rayExitT); //right: x <= w.
    ClipAgainstPlane(clipStart.y + clipStart.w, clipEnd.y + clipEnd.w, rayExitT); //bottom: y >= -w.
    ClipAgainstPlane(clipStart.w - clipStart.y, clipEnd.w - clipEnd.y, rayExitT); //top: y <= w.

    if (rayExitT <= 1e-4)
        return 1.0;

    clipEnd = lerp(clipStart, clipEnd, saturate(rayExitT));

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
	//IMPORTANT NOTE 2: WATCH THAT SWIZZLE! it needs to be wz not zw... otherwise we get scaling issues at non standard resolutions
	float2 uvStart = mad(ndcStart.xy, View_ScreenPositionScaleBias.xy, View_ScreenPositionScaleBias.wz);
	float2 uvEnd   = mad(ndcEnd.xy,   View_ScreenPositionScaleBias.xy, View_ScreenPositionScaleBias.wz);

    float2 rayPixelDelta = (uvEnd - uvStart) * View_BufferSizeAndInvSize.xy;
    float rayPixelLength = length(rayPixelDelta);

    // Screen-space depth cannot represent this ray reliably.
    if (rayPixelLength < 0.75f)
        return 1.0f;

	float2 uvStep  = (uvEnd - uvStart) * invSamples;
	float2 uv      = mad(uvStep, random, uvStart);

	float occlusion = 1.0f;

    [unroll]
    for (int i = 0; i < CONTACT_SHADOWS_SAMPLES; ++i)
    {
		//OPTIMIZATION: early out when our sample UV goes past screen edges
        if (any(uv < 0.0) || any(uv > 1.0))
            break;

        //FIX: it looks like when we look towards the light direction
        //at a certain distance (and this lessens with higher resolutions) it seems like the rays start to intersect geometry uniformly and we wind up getting a universal darkening of shadows when there realistically should be no occlusion
        //to fix that, we just make sure that the rays we are tracing are not starting at the same point
        float2 sampleOffsetPixels = (uv - uvStart) * View_BufferSizeAndInvSize.xy;  
        bool leftOriginPixel = dot(sampleOffsetPixels, sampleOffsetPixels) >= 0.25f;

        if (leftOriginPixel)
        {
            //NOTE: CHANGE TO POINT
            float deviceDepth = SceneDepthTexture.SampleLevel(View_SharedBilinearClampedSampler, uv, 0.0).r;
            float sceneDepth = LinearEyeDepth(deviceDepth);
            float penetration = rayDepth - sceneDepth;

            #if defined(CONTACT_SHADOWS_FALLOFF)
                if (penetration > contactShadowBias && penetration < CONTACT_SHADOWS_THICKNESS)
                {
                    //how far along the ray are we? (we are going from point towards the light)
                    float rayProgress = i * invSamples;
                    float thicknessFade = 1.0 - saturate((penetration - contactShadowBias) / (CONTACT_SHADOWS_THICKNESS - contactShadowBias));
                    float distanceFade = 1.0 - saturate(rayProgress);
                    float sampleShadow = 1.0 - distanceFade;
                    sampleShadow *= sampleShadow;

                    occlusion = min(occlusion, sampleShadow);
                }
            #else
                //NOTE TO SELF: while this is simple and fast, leaves a harsh cutoff
                //for thickness we can calculate a "weight" to do a smoother falloff out from shadow
                if (penetration > contactShadowBias && penetration < CONTACT_SHADOWS_THICKNESS)
                    return 0.0;
            #endif
        }

        rayDepth += rayDepthStep;
        uv += uvStep;
    }

	//when introducing the falloff shadows can appear a little too light
	//to compensate especially near contacts we have a contrast factor here
	#if defined(CONTACT_SHADOWS_FALLOFF)
		occlusion = pow(occlusion, CONTACT_SHADOWS_FALLOFF_CONTRAST);
	#endif

    return occlusion;
}

float CalculateContactShadows(int2 pixelPos, float3 worldPosition, float3 worldNormal, float3 lightDirection, float rawDepth, float random, int shadingModelID)
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
        contactShadow = FastContactShadowClipSpace(worldPosition, worldNormal, lightDirection, random, rawDepth, shadingModelID);
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

//||||||||||||||||||||||||||||||| HALLUCINATED ZH3 |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| HALLUCINATED ZH3 |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| HALLUCINATED ZH3 |||||||||||||||||||||||||||||||

struct FHallucinatedZH3
{
    float3 Axis;
    float3 RadianceL2Coefficient;
    float3 RadianceL0;
    float3 RadianceL1Y;
    float3 RadianceL1Z;
    float3 RadianceL1X;
};

float3 EvaluateLinearSH(SphericalHarmonicsData sh, float3 direction)
{
    return sh.Coefficient[0] * 0.2820947918f
         + sh.Coefficient[1] * (0.4886025119f * direction.y)
         + sh.Coefficient[2] * (0.4886025119f * direction.z)
         + sh.Coefficient[3] * (0.4886025119f * direction.x);
}

FHallucinatedZH3 BuildHallucinatedZH3(SphericalHarmonicsData irradianceSH)
{
    //Only L0/L1 are needed. Recover their radiance coefficients from the
    //stored irradiance without deconvolving the unused quadratic band.
    FHallucinatedZH3 zh;
    zh.RadianceL0  = irradianceSH.Coefficient[0] * INV_PI;
    zh.RadianceL1Y = irradianceSH.Coefficient[1] * 0.47746482927568f;
    zh.RadianceL1Z = irradianceSH.Coefficient[2] * 0.47746482927568f;
    zh.RadianceL1X = irradianceSH.Coefficient[3] * 0.47746482927568f;

    //Use one luminance-derived axis for RGB to avoid color fringing.
    float3 luminanceMoment = float3(
        LuminanceRec709(zh.RadianceL1X), // X
        LuminanceRec709(zh.RadianceL1Y), // Y
        LuminanceRec709(zh.RadianceL1Z)  // Z
    );

    float momentLength = length(luminanceMoment);

    zh.Axis = momentLength > 1e-6f ? luminanceMoment / momentLength : float3(0.0f, 0.0f, 1.0f);

    //Project each channel's L1 vector onto the shared zonal axis. The
    //absolute value follows the published shared-axis reconstruction.
    float3 zonalL1Coefficient = abs(
        zh.RadianceL1X * zh.Axis.x
      + zh.RadianceL1Y * zh.Axis.y
      + zh.RadianceL1Z * zh.Axis.z);

    float3 ratio = zonalL1Coefficient / max(zh.RadianceL0, 1e-6f);

    //sqrt(3) is the L1/DC ratio bound for a nonnegative radiance signal.
    //Clamping keeps deconvolution noise or malformed probes from exploding
    //the fitted quadratic term.
    ratio = min(ratio, 1.7320508076f);

    //Least-squares fit from production lighting data in "ZH3: Quadratic
    //Zonal Harmonics". This produces the local-frame radiance Y20 coefficient.
    zh.RadianceL2Coefficient = zh.RadianceL0 * (0.08f * ratio + 0.6f * ratio * ratio);

    return zh;
}

float EvaluateZonalL2Basis(float3 axis, float3 direction)
{
    float z = dot(axis, direction);
    return 0.3153915653f * (3.0f * z * z - 1.0f);
}

float3 EvaluateIrradianceZH3Hallucinated(SphericalHarmonicsData irradianceSH, FHallucinatedZH3 zh, float3 direction)
{
    //original input already contains the Lambert-convolved L0/L1 bands.
    float3 color = EvaluateLinearSH(irradianceSH, direction);

    //unnormalized clamped-cosine transfer for l = 2 is PI / 4
    color += zh.RadianceL2Coefficient * (0.25f * PI) * EvaluateZonalL2Basis(zh.Axis, direction);

    return color;
}

float3 EvaluateRadianceZH3Hallucinated(FHallucinatedZH3 zh, float3 direction)
{
    float3 color = zh.RadianceL0 * 0.2820947918f
         + zh.RadianceL1Y * (0.4886025119f * direction.y)
         + zh.RadianceL1Z * (0.4886025119f * direction.z)
         + zh.RadianceL1X * (0.4886025119f * direction.x);

    color += zh.RadianceL2Coefficient * EvaluateZonalL2Basis(zh.Axis, direction);

    return color;
}

float3 EvaluateIrradianceZH3Hallucinated(SphericalHarmonicsData irradianceSH, float3 direction)
{
    FHallucinatedZH3 zh = BuildHallucinatedZH3(irradianceSH);
    return EvaluateIrradianceZH3Hallucinated(irradianceSH, zh, direction);
}

float3 EvaluateRadianceZH3Hallucinated(SphericalHarmonicsData irradianceSH, float3 direction)
{
    FHallucinatedZH3 zh = BuildHallucinatedZH3(irradianceSH);
    return EvaluateRadianceZH3Hallucinated(zh, direction);
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 localPixel = dispatchThreadId.xy;

    if (any(float2(localPixel) >= View_ViewSizeAndInvSize.xy))
        return;

    uint2 outputPixel = uint2(float2(localPixel) + 0.5f + View_ViewRectMin.xy);

    float3 encodedNormal = GBufferATexture.Load(int3(outputPixel, 0)).xyz;
    float4 gBufferB = GBufferBTexture.Load(int3(outputPixel, 0));
    float environmentLobeBlend = GBufferDTexture.Load(int3(outputPixel, 0)).z;
    float deviceZ = SceneDepthTexture.Load(int3(outputPixel, 0)).x;

    uint packedShadingModel = (uint)round(gBufferB.a * 255.0f);
    uint shadingModelId = packedShadingModel & 0x0fu;
    float roughness = gBufferB.b;

    float3 radiance = 0.0f;
    float3 irradiance = 0.0f;
    float outputAlpha = 0.0f;

    if (shadingModelId != SHADINGMODELID_UNLIT)
    {
        float3 worldNormal = normalize(encodedNormal * 2.0f - 1.0f);
        float worldDepth = ConvertFromDeviceZ(deviceZ);
        float3 worldPosition = ReconstructWorldPosition(localPixel, worldDepth);
        float3 directionToCamera = normalize(View_WorldCameraOrigin - worldPosition);

        float3 reflectionDirection = ComputeDominantReflectionDirection(worldNormal, directionToCamera, gBufferB.z, shadingModelId);

        uint environmentIndex;
        uint boundsIndex;
        float3 environmentLocalPosition;
        float3 gridPosition;

        if (FindProbeBounds(worldPosition, environmentIndex, boundsIndex, environmentLocalPosition, gridPosition))
        {
            FProbeDirectionalField field = BlendProbeField(environmentIndex, boundsIndex, environmentLocalPosition, gridPosition);
            //FProbeDirectionalField field = BlendProbeField(environmentIndex, boundsIndex, View_WorldCameraOrigin, View_WorldCameraOrigin); //quick debug check just to see raw probe cells and colors

            #if defined(DEFAULT_GAME_SHADING)
			    radiance = EvaluateDirectionalField(field, reflectionDirection);
			    irradiance = EvaluateDirectionalField(field, worldNormal);

                //the captured default-lit path uses GBufferD.z to blend the normal-evaluated result back toward the field's base color
                if (shadingModelId == SHADINGMODELID_DEFAULT_LIT)
                    irradiance = lerp(irradiance, field.BaseColor, environmentLobeBlend);

                outputAlpha = 1.0f;
            #else
                SphericalHarmonicsData sphericalHarmonics = AmbientCubeToSH2(field);

                //now that our probe data is in spherical harmonics
                //we can use this to derive a dominant direction of light, and with this direction, we can use it to calculate a fresh specular highlight!
                //now even if our coefficents were spherical harmonics (and not ambient cube) trying to get sharp rough reflections is not possible
                //so the best thing we can do is to approximate a sharp specular highlight that varies with roughness, this will aid in our final "rough reflection" quality
                FSHDominantLight dominantLight = ExtractDominantLight(sphericalHarmonics);

                //diffuse lighting
                #if defined(SPHERICAL_HARMONICS_IRRADIANCE_ZH3)
                    irradiance = EvaluateIrradianceZH3Hallucinated(sphericalHarmonics, worldNormal);
                #else
                    irradiance = EvaluateIrradiance(sphericalHarmonics, worldNormal);
                #endif
        
                //specular lighting
                //NOTE: now I have some things to say about this... while normally in theory we shouldn't at all be sampling from this irradiance volume for reflections/specular at all
                //given the size of the game, and the sparse nature of reflection probes, and their quality, the best thing we can do is actually combine them which is a smart idea the game does
                //however there is a problem, because this probe data is diffuse/irradiance, not specular/radiance. so the resulting value is actually brighter and more blurry than it should be for reflections
                //for radiance it should be darker and a little more contrasty, to a point you should almost be able to make out some broad highlights
                //soooo, with that in mind, the neat thing is that now that we converted these coeffiecnts into SH, we can utilize some tricks to make this more plausible/accurate
                //the assumption is that these coefficents are already convolved for irradiance/diffuse, but thanks to spherical harmonic math it's possible to deconvolve these coefficents back into radiance
                //and that, is what we want for reflections!
                #if defined(SPHERICAL_HARMONICS_RADIANCE_ZH3)
                    radiance = EvaluateRadianceZH3Hallucinated(sphericalHarmonics, reflectionDirection);
                #else
                    radiance = EvaluateRadiance(sphericalHarmonics, reflectionDirection); //resamples coefficents again, but using reflection direction, and also followed by a de-convolution step
                #endif

                //experiment: since we know the dominant light direction
                //we can try to use this to occlude specular reflections that are away from the dominant source of light
                //radiance = saturate(dot(reflectionDirection, dominantLight.Direction)) * View_OneOverPreExposure;
                //radiance = saturate(dot(reflectionDirection, dominantLight.Direction) * 0.5f + 0.5f) * View_OneOverPreExposure;
                //radiance = lerp(1.0f, saturate(dot(reflectionDirection, dominantLight.Direction) * 0.5f + 0.5f), dominantLight.Directionality) * View_OneOverPreExposure;
                //radiance *= saturate(dot(reflectionDirection, normalize(dominantLight.Direction)) * 0.5f + 0.5f);
                //radiance *= lerp(1.0f, saturate(dot(reflectionDirection, normalize(dominantLight.Direction)) * 0.5f + 0.5f), dominantLight.Directionality);

                float domiantHighlightLuminance = 0.0f;

                #if defined(SPHERICAL_HARMONICS_DOMINANT_SPECULAR_HIGHLIGHT)
                    float3 dominantHighlight = EvaluateDominantSHHighlight(dominantLight, worldNormal, directionToCamera, roughness);

                    //boost specular highlight on non character surfaces
                    if (shadingModelId != SHADINGMODELID_UNLIT ||
                        shadingModelId != SHADINGMODELID_SUBSURFACE_PROFILE ||
                        shadingModelId != SHADINGMODELID_PREINTEGRATED_SKIN ||
                        shadingModelId != SHADINGMODELID_EYE ||
                        shadingModelId != SHADINGMODELID_HAIR)
                    {
                        dominantHighlight *= MATH_PI * SPHERICAL_HARMONICS_DOMINANT_SPECULAR_HIGHLIGHT_BOOST; 
                        dominantHighlight *= (1.0f + dominantLight.Directionality);
                    }

                    radiance += dominantHighlight;
                    domiantHighlightLuminance = LuminanceRec709(dominantHighlight);
                #endif

                #if defined(CAMERA_VIEW_SPECULAR_HIGHLIGHT)
                    float viewSpecularHighlight = CalculateViewSpecularHighlight(worldNormal, directionToCamera, roughness);
                    radiance += radiance * viewSpecularHighlight / MATH_PI;
                    //domiantHighlightLuminance = saturate(domiantHighlightLuminance + viewSpecularHighlight);
                #endif

                #if defined(SPHERICAL_HARMONICS_DOMINANT_DIRECTION_DIFFUSE)
                    float random = InterleavedGradientNoise(outputPixel, View_FrameNumber);
                    float contactShadow = CalculateContactShadows(outputPixel, worldPosition, worldNormal, dominantLight.Direction, deviceZ, random, shadingModelId);

                    float dominantDiffuseShading = saturate(dot(worldNormal, dominantLight.Direction));
                    //float dominantDiffuseShading = saturate(dot(worldNormal, dominantLight.Direction) * 0.5f + 0.5f);
                    dominantDiffuseShading *= contactShadow;

                    irradiance *= lerp(dominantDiffuseShading, 1.0f, 0.25f);
                    radiance *= lerp(dominantDiffuseShading, 1.0f, 0.0f);
                #endif

                if (shadingModelId == SHADINGMODELID_HAIR || 
                    shadingModelId == SHADINGMODELID_EYE || 
                    shadingModelId == SHADINGMODELID_PREINTEGRATED_SKIN)
                {
                    float skinDominantDiffuseContrast = saturate(dot(worldNormal, dominantLight.Direction) * 0.5f + 0.5f);
                    irradiance *= lerp(1.0f, skinDominantDiffuseContrast, 0.25f);
                }

                //the captured default-lit path uses GBufferD.z to blend the normal-evaluated result back toward the field's base color
                if (shadingModelId == SHADINGMODELID_DEFAULT_LIT)
                    irradiance = lerp(irradiance, field.BaseColor, environmentLobeBlend);

                //outputAlpha = 1.0f;
                outputAlpha = saturate(0.0001f + domiantHighlightLuminance);
            #endif
        }
    }
	
	//downside with SH is negative values can happen, so make sure we don't output them
	irradiance = max(0.004f, irradiance);
	radiance = max(0.004f, radiance);

    RWEnvironmentIrradianceATexture[outputPixel] = float4(radiance * View_PreExposure, outputAlpha);
    RWEnvironmentIrradianceBTexture[outputPixel] = float4(irradiance * View_PreExposure, outputAlpha);
}