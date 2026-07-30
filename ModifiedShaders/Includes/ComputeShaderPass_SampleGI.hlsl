//SampleGI.hlsl

//library includes
//NOTE: this is where we have various useful shader functions
#include "LibraryMath.hlsl"
#include "LibraryRandom.hlsl"
#include "LibraryColor.hlsl"
#include "LibraryGBuffer.hlsl"

//by default it appears that the irradiance volume data is basically ambient cube
//it's efficent but not the best quality and has it's problems
//but we can project those irradiance coefficents into SH to get the data in spherical harmonics.
//#define DEFAULT_GAME_SHADING

//|||||||||||||||||||||||||||||||||| CONFIGURATION - SPHERICAL HARMONICS IRRADIANCE/DIFFUSE ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| CONFIGURATION - SPHERICAL HARMONICS IRRADIANCE/DIFFUSE ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| CONFIGURATION - SPHERICAL HARMONICS IRRADIANCE/DIFFUSE ||||||||||||||||||||||||||||||||||

//in plain english: this is more accurate, and leads to less contrast
//technical yapping: this evaluates the converted spherical harmonics "irradiance/diffuse" global illumination term using zonal harmonics
//where the coefficents we reconstructed are effectively order 2, but with zonal harmonics we can "hallucinate" a 3rd order leading to less ringing
#define SPHERICAL_HARMONICS_IRRADIANCE_ZH3

//this uses the dominant direction of baked global illumination to help give extra shading/contrast to characters when in ambient light (half-lambert)
//this is not physically accurate at all, just purely an artistic effect
#define CHARACTER_DOMINANT_DIRECTION_SHADING

//when CHARACTER_DOMINANT_DIRECTION_SHADING is active, this controlshow strong the contrast is for character materials when in ambient light (half-lambert)
//[CONFIG TYPE]: float
//[CONFIG DEFAULT]: 0.25
//[CONFIG RANGE]: [0, 1]
#define CHARACTER_DOMINANT_DIRECTION_SHADING_AMOUNT 0.25

//this is an experimental feature, that uses the dominant direction and treats it like a light source to get additional shading/contrast in ambient areas.
//it also uses contact shadows to help give a more grounded look to the shading, and to help mitigate some of the shortcomings of baked global illumination
//this is not physically accurate at all, this is purely just an artistic effect.
//#define SPHERICAL_HARMONICS_DOMINANT_DIRECTION_DIFFUSE

//(CONTACT SHADOWS) this changes the noise pattern every frame, which with Temporal Anti-Aliasing (or DLSS or anything related)
//you want to do, that way samples change every frame and results get blended together over time for a better final apperance
#define RANDOM_ANIMATE_NOISE

//(SPHERICAL_HARMONICS_DOMINANT_DIRECTION_DIFFUSE ONLY)
//calculates contact shadows for dominant direction of ambient light
//in short it raymarches against the scene depth buffer to estimate shadows
//significantly improves overall shadow quality
#define ENABLE_CONTACT_SHADOWS

//(SPHERICAL_HARMONICS_DOMINANT_DIRECTION_DIFFUSE ONLY)
//this directly controls the quality of the contact shadows, the more the better!
//HIGHER VALUES: better quality shadows, less noise (more stable), and denser but expensive performance
//LOWER VALUES: lower quality shadows, more noise (less stable), and lighter but cheaper performance
//[CONFIG TYPE]: int
//[CONFIG DEFAULT]: 8
//[CONFIG RANGE]: [4, 128]
#define CONTACT_SHADOWS_SAMPLES 8

//(SPHERICAL_HARMONICS_DOMINANT_DIRECTION_DIFFUSE ONLY)
//this controls how far out the shadows go in screen space (and also can make shadows appear darker/denser or lighter depending on sample count)
//HIGHER VALUES: shadows can reach farther, but can become noiser, and more expensive (more screen area)
//LOWER VALUES: shadows don't reach as far, but can become more stable, and cheaper (less screen area)
//[CONFIG TYPE]: float
//[CONFIG DEFAULT]: 50.0
#define CONTACT_SHADOWS_RAY_LENGTH 75.0

//(SPHERICAL_HARMONICS_DOMINANT_DIRECTION_DIFFUSE ONLY)
//this controls how "thick" objects are in the depth buffer
//HIGHER VALUES: larger volume of shadow, but can lead to alot of wierd false shadowing. objects up close can cast shadows onto objects far behind it which can look odd.
//LOWER VALUES: smaller volume of shadow, less false shadowing, but they can appear less dense and might be too thin
//[CONFIG TYPE]: float
//[CONFIG DEFAULT]: 0.325
#define CONTACT_SHADOWS_THICKNESS 0.325

//(SPHERICAL_HARMONICS_DOMINANT_DIRECTION_DIFFUSE ONLY)
//this is a small bias factor to minimize contact shadow acne on sloped surfaces
//high values = reduced acne but can introduce visual issues where shadows appear less grounded
//low values = potentially increased acne but keeps shadows grounded
//[CONFIG TYPE]: float
//[CONFIG DEFAULT]: 0.1
#define CONTACT_SHADOWS_BIAS 0.1

//(SPHERICAL_HARMONICS_DOMINANT_DIRECTION_DIFFUSE ONLY)
//this is a small bias factor to minimize contact shadow acne on sloped surfaces for hair specifically
//high values = reduced acne but can introduce visual issues where shadows appear less grounded
//low values = potentially increased acne but keeps shadows grounded
//[CONFIG TYPE]: float
//[CONFIG DEFAULT]: 0.1
#define CONTACT_SHADOWS_BIAS_HAIR 0.1

//(SPHERICAL_HARMONICS_DOMINANT_DIRECTION_DIFFUSE ONLY)
//this is a small bias factor to minimize contact shadow acne on sloped surfaces using surface normal
//high values = reduced acne but can introduce visual issues where shadows appear less grounded
//low values = potentially increased acne but keeps shadows grounded
//[CONFIG TYPE]: float
//[CONFIG DEFAULT]: 0.325
#define CONTACT_SHADOWS_NORMAL_BIAS 0.1

//(SPHERICAL_HARMONICS_DOMINANT_DIRECTION_DIFFUSE ONLY)
//OPTIMIZATION: this avoids calculating contact shadows for sky pixels
//has no effect visually, but can save you quite a bit of frametime especially the more you look up :P 
//honestly no reason you should turn this off unless you want to suffer in vain...
#define CONTACT_SHADOW_EARLY_SKY_OUT

//(SPHERICAL_HARMONICS_DOMINANT_DIRECTION_DIFFUSE ONLY)
//OPTIMIZATION: this calculates contact shadows for every other pixel in a checkerboard like pattern that switches every frame
//it saves a small bit of frametime, but does have a quality degredation with more visible shimmering at distances
//disable if you want sharper true per-pixel contact shadows (at a bit of a perf hit)
// #define CONTACT_SHADOW_CHECKERBOARD

//(SPHERICAL_HARMONICS_DOMINANT_DIRECTION_DIFFUSE ONLY)
//[CONTACT_SHADOW_CHECKERBOARD ONLY!] This only works if checkerboarding is enabled!
//this tries to fill in the gaps intelligently during checkerboard rendering minimize holes where no shadow is calculated 
#define CONTACT_SHADOW_CHECKERBOARD_QUAD_RECONSTRUCTION

//(SPHERICAL_HARMONICS_DOMINANT_DIRECTION_DIFFUSE ONLY)
//[CONTACT_SHADOW_CHECKERBOARD and CONTACT_SHADOW_CHECKERBOARD_QUAD_RECONSTRUCTION ONLY!] This only works if checkerboarding and quad reconstruction is enabled!
//sharper | checks within the 2x2 checker block and if there is a shadow pixel it just copies it
// #define CONTACT_SHADOW_CHECKERBOARD_QUAD_RECONSTRUCTION_TYPE_MIN

//(SPHERICAL_HARMONICS_DOMINANT_DIRECTION_DIFFUSE ONLY)
//smoother | takes the values within the 2x2 checker block and averages them together for a slightly softer apperance
#define CONTACT_SHADOW_CHECKERBOARD_QUAD_RECONSTRUCTION_TYPE_AVG

//(SPHERICAL_HARMONICS_DOMINANT_DIRECTION_DIFFUSE ONLY)
//this was requested by quite a few people who wanted to control the strength of the shadows
//physical accuracy is at 1.0 full power, and natrually global illumination (ambient, bounce, transmission light) fills in shadows
//however the games GI term is far from perfect and can leave some areas quite dark so here you can control it
//reduction in shadow strength can create an odd 90s style look where you can see a semblance of light leaking through to "fill in" for the shadow.
//not a fan of it, and it's not accurate, but hey to each their own!
//[CONFIG TYPE]: float
//[CONFIG DEFAULT]: 1.0
//[CONFIG RANGE]: [0, 1]
#define CONTACT_SHADOWS_STRENGTH 1.0

//this was requested by a couple of users who wanted to selectively disable contact shadows for specific material types
//while this could create visual inconsistencies, I've wired them up anyway so you can use them at your own descretion
//these both are used essentially for all materials within the game

//(SPHERICAL_HARMONICS_DOMINANT_DIRECTION_DIFFUSE ONLY)
// #define DISABLE_CONTACT_SHADOWS_FOR_DEFAULT_LIT

//(SPHERICAL_HARMONICS_DOMINANT_DIRECTION_DIFFUSE ONLY)
//this does get used on characters but it's not common, default_lit
// #define DISABLE_CONTACT_SHADOWS_FOR_CLOTH

//all these 4 below generally cover a majority of the character

//(SPHERICAL_HARMONICS_DOMINANT_DIRECTION_DIFFUSE ONLY)
// #define DISABLE_CONTACT_SHADOWS_FOR_PREINTEGRATED_SKIN

//(SPHERICAL_HARMONICS_DOMINANT_DIRECTION_DIFFUSE ONLY)
// #define DISABLE_CONTACT_SHADOWS_FOR_SUBSURFACE_PROFILE

//(SPHERICAL_HARMONICS_DOMINANT_DIRECTION_DIFFUSE ONLY)
// #define DISABLE_CONTACT_SHADOWS_FOR_HAIR

//(SPHERICAL_HARMONICS_DOMINANT_DIRECTION_DIFFUSE ONLY)
// #define DISABLE_CONTACT_SHADOWS_FOR_EYE

//(SPHERICAL_HARMONICS_DOMINANT_DIRECTION_DIFFUSE ONLY)
//another requested feature...
//this is an added effect that will gradually "fade" contact shadows as it goes further out
//this does have a bit of a perf hit with (extra instructions per iteration in the loop now)
//I've done my best to optimize and keep it light, but it just requires more instructions to achieve
//still, in the grand scheme of things this should be pretty light and enabled if you really want to mitigate some of shortcomings (atleast its not another texture sample)
#define CONTACT_SHADOWS_FALLOFF

//(SPHERICAL_HARMONICS_DOMINANT_DIRECTION_DIFFUSE ONLY)
//this shapes the falloff
//higher values = sharper/darker shadow further out
//lower values = softer/lighter shadow further out
//[CONFIG TYPE]: float
//[CONFIG DEFAULT]: 3.0
#define CONTACT_SHADOWS_FALLOFF_CONTRAST 3.0

//|||||||||||||||||||||||||||||||||| CONFIGURATION - SPHERICAL HARMONICS RADIANCE/REFLECTION ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| CONFIGURATION - SPHERICAL HARMONICS RADIANCE/REFLECTION ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| CONFIGURATION - SPHERICAL HARMONICS RADIANCE/REFLECTION ||||||||||||||||||||||||||||||||||

//game doing a stretched reflection vector based on roughness for some reason? it looks ugly and wrong
#define CORRECT_REFLECTION_DIRECTION

//derrive a specular highlight based on the dominant direction of ambient light
//this helps give specular materials a very nice and plausible/accurate kick of reflection that is based on the baked ambient global illumination
//I HIGHLY recomend keeping this on
#define SPHERICAL_HARMONICS_DOMINANT_SPECULAR_HIGHLIGHT

//[CONFIG TYPE]: float
//[CONFIG DEFAULT]: 2.0
#define SPHERICAL_HARMONICS_DOMINANT_SPECULAR_HIGHLIGHT_BOOST 2.0

//for an extra "artistic" specular kick we can calculate another specular highlight but coming from the opposite dominant direction
//this can help give an extra shine in areas of shadow or facing away from the main dominant direction
#define SPHERICAL_HARMONICS_DOMINANT_SPECULAR_HIGHLIGHT_DUAL

//[CONFIG TYPE]: float
//[CONFIG DEFAULT]: 0.05
#define SPHERICAL_HARMONICS_DOMINANT_SPECULAR_HIGHLIGHT_DUAL_BOOST 0.05

//derrive a specular highlight from the camera
//this is more artistic admittedly, and is not entirely accurate/plausible unless there is a light at camera
//with that said this also like the dominant SH specular highlight gives a very nice kick to the reflection term for the baked ambient global illumination
#define CAMERA_VIEW_SPECULAR_HIGHLIGHT

//calculate a specular occlusion term (half lambert) that darkens reflections on the dominant direction of ambient light
#define SPHERICAL_HARMONICS_DOMINANT_DIRECTION_SPECULAR_OCCLUSION

//controls how strong the specular occlusion term is, higher values darken reflections facing away from the dominant direction of ambient light
//[CONFIG TYPE]: float
//[CONFIG DEFAULT]: 0.5
//[CONFIG RANGE]: [0, 1]
#define SPHERICAL_HARMONICS_DOMINANT_DIRECTION_SPECULAR_OCCLUSION_FACTOR 0.5

//DONT TOUCH
//NOTE TO SELF: since this shader does get a little heavier with the extra specular highlights
//this is our attempt at reducing the cost, since most of the terms are based on dominant direction
#if defined(SPHERICAL_HARMONICS_DOMINANT_SPECULAR_HIGHLIGHT) || defined(CAMERA_VIEW_SPECULAR_HIGHLIGHT) || defined(CHARACTER_DOMINANT_DIRECTION_SHADING)

	//[NO CONFIG]
	#define REQUIRE_NOV

#endif

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

//the games ambient-cube projection has one shared chromaticity and only six non-zero scalar coefficients.
//normally i'd keep this fleshed out... but that's not entirely optimal performance wise
//lugging around 9 float3 coefficents will kind of inflate things
struct SimplifiedSH2
{
	float3 Chromaticity;
	float Coefficient0;
	float Coefficient1;
	float Coefficient2;
	float Coefficient3;
	float Coefficient6;
	float Coefficient8;
};

//|||||||||||||||||||||||||||||||||| MATH ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| MATH ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| MATH ||||||||||||||||||||||||||||||||||

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

float3 TransformEnvironmentPositionToGrid(float3 environmentLocalPosition, FPrecomputedLightEnvironmentProbeBoundsInfo info)
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
        //-2 in the bit representation of w marks an inactive environment.
        float4 environment = PLESceneInfo_PLEPositions[candidate];

        if (asint(environment.w) == -2)
            continue;

        uint descriptor = NonUniformResourceIndex(candidate);
        FPrecomputedLightEnvironmentProbeBoundsInfo info = PrecomputedLightProbeBoundsInfoSRVs[descriptor][0];

        float3 localPosition = worldPosition - environment.xyz;
        float3 candidateGridPosition = TransformEnvironmentPositionToGrid(localPosition, info);

        if (any(candidateGridPosition < 0.0f) || any(candidateGridPosition >= float3(info.GridSize)))
            continue;

        uint3 integerGridPosition = (uint3)floor(candidateGridPosition);

        //the first indirection indexes a coarse 3-D grid.
		//its high nibble tells how many hierarchy bits must be resolved by a second lookup.
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

			//unlike an empty coarse cell, an empty refinement entry ends the search in the captured shader and produces black.
            if (packedEntry == -1)
                return false;

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

float EvaluateDistanceCubeVisibility(uint descriptor, int distanceCubeSlice, float3 directionFromProbeToPoint, float probeDistance)
{
    if (distanceCubeSlice == -1)
        return 1.0f;

    float2 distanceMoments = DistanceCubeArray[descriptor].SampleLevel(View_SharedBilinearClampedSampler, float4(directionFromProbeToPoint, (float)distanceCubeSlice), 0.0f).rg;

    //the capture stores distances and squared distances in hundredths.
    float meanDistance = distanceMoments.x * 100.0f;
    float secondMoment = distanceMoments.y * 100.0f;
    float biasedMean = meanDistance + 1.0f;

    if (probeDistance < biasedMean)
        return 1.0f;

    float variance = abs(meanDistance * meanDistance - secondMoment) + 1.0f;
    float delta = probeDistance - biasedMean;
    return variance / (delta * delta + variance);
}

bool IntersectsTriangleSegment(float3 rayOrigin, float3 rayDirection, float rayLength, float3 vertex0, float3 vertex1, float3 vertex2)
{
    //moller-trumbore intersection, matching the DXIL's epsilon and bounds.
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

float EvaluateTriangleVisibility(uint environmentIndex, uint descriptor, uint boundsIndex, float3 rayOrigin, float3 rayDirection, float rayLength)
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
            return 0.0f;
    }

    return 1.0f;
}

FProbeDirectionalField BlendProbeField(uint environmentIndex, uint boundsIndex, float3 environmentLocalPosition, float3 gridPosition)
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
	float baseLuminance = LuminanceRec709(field.BaseColor);
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
	    //the normal correct way
	    //N and V are already normalized, so reflect() is unit length apart
	    //from insignificant floating-point drift. Avoid a redundant rsqrt.
	    return reflect(-directionToCamera, worldNormal);
    #else
        //these two material-specific roughness adjustments are explicit in DXIL for some reason
        if (shadingModelId == SHADINGMODELID_PREINTEGRATED_SKIN)
            roughness *= 0.93655f;
        else if (shadingModelId == SHADINGMODELID_HAIR)
            roughness = 0.48f;

		//im not sure why on earth the game decides to stretch reflections with roughness
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

//precomputed constants
static const float Y00  = 0.2820947918f;
static const float Y1   = 0.4886025119f;
static const float Y20  = 0.3153915653f;
static const float Y21  = 1.0925484306f;
static const float Y22  = 0.5462742153f;

static const float Y00_A = Y00 * 4.1887902f; //4.1887902f = (4.0f * PI / 3.0f)
static const float Y1_PI = Y1 * MATH_PI;
static const float Y20_A = Y20 * 1.675516f; //1.675516f = (8.0f * PI / 15.0f)
static const float Y22_A = Y22 * 1.675516f; //1.675516f = (8.0f * PI / 15.0f)

SimplifiedSH2 AmbientCubeToSH2(FProbeDirectionalField field)
{
    float3 axisAverage = 0.5f * (field.PositiveAxisLobes + field.NegativeAxisLobes);
    float3 axisDifference = 0.5f * (field.PositiveAxisLobes - field.NegativeAxisLobes);
    float baseLuminance = LuminanceRec709(field.BaseColor);
    float validProbe = baseLuminance > 1e-6f ? 1.0f : 0.0f;

    SimplifiedSH2 result;
    result.Chromaticity = validProbe > 0.0f ? field.BaseColor / baseLuminance : 0.0f;

	//order 0
	result.Coefficient0 = validProbe * Y00_A * (axisAverage.x + axisAverage.y + axisAverage.z);

	//order 1
	result.Coefficient1 = validProbe * Y1_PI * axisDifference.y;
	result.Coefficient2 = validProbe * Y1_PI * axisDifference.z;
	result.Coefficient3 = validProbe * Y1_PI * axisDifference.x;

	//order 2
	result.Coefficient6 = validProbe * Y20_A * (2.0f * axisAverage.z - axisAverage.x - axisAverage.y);
	result.Coefficient8 = validProbe * Y22_A * (axisAverage.x - axisAverage.y);

    return result;
}

float EvaluateSphericalHarmonicsScalar(SimplifiedSH2 sh, float3 direction)
{
	float value = sh.Coefficient0 * Y00;
	value += Y1 * dot(float3(sh.Coefficient3, sh.Coefficient1, sh.Coefficient2), direction);
	value += sh.Coefficient6 * Y20 * (3.0f * direction.z * direction.z - 1.0f);
	value += sh.Coefficient8 * Y22 * (direction.x * direction.x - direction.y * direction.y);
	return value;
}

float3 EvaluateSphericalHarmonics(SimplifiedSH2 sh, float3 direction)
{
	return sh.Chromaticity * EvaluateSphericalHarmonicsScalar(sh, direction);
}

//reference - https://highperformancegraphics.org/untracked/2025/presentations/Pa5_1_hpg_2025_slides.pdf
//equation 12 in the HPG 2025 paper attenuates SH band l by exp(-l(l+1)/(2*kappa)) with kappa = 1/alpha.
//this gives the raw exp(-0.5*l(l+1)*alpha) weight.
float2 GetGlossySHBandWeights(float alpha)
{
	float l1RawWeight = exp(-alpha);
	float l2RawWeight = l1RawWeight * l1RawWeight * l1RawWeight;

	//x: exp(-1), y: exp(-3)
	const float2 fullyRoughWeight = float2(0.3678794412f, 0.0497870684f);
	const float2 inverseWeightRange = float2(1.5819767069f, 1.0523956965f);
	return saturate((float2(l1RawWeight, l2RawWeight) - fullyRoughWeight) * inverseWeightRange);
}

float3 EvaluateRoughnessFilteredRadiance(SimplifiedSH2 irradianceSH, float3 direction, float alpha)
{
	float2 bandWeights = GetGlossySHBandWeights(alpha);

	float value = irradianceSH.Coefficient0 * MATH_PI_INV * Y00;
	value += bandWeights.x * 0.47746482927568f * Y1 * dot(float3(irradianceSH.Coefficient3, irradianceSH.Coefficient1, irradianceSH.Coefficient2), direction);
	value += bandWeights.y * 1.273239544735162f * (irradianceSH.Coefficient6 * Y20 * (3.0f * direction.z * direction.z - 1.0f) + irradianceSH.Coefficient8 * Y22 * (direction.x * direction.x - direction.y * direction.y));

	return irradianceSH.Chromaticity * value;
}

//||||||||||||||||||||||||||||||| HALLUCINATED ZH3 |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| HALLUCINATED ZH3 |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| HALLUCINATED ZH3 |||||||||||||||||||||||||||||||

float3 EvaluateIrradianceZH3Hallucinated(
	SimplifiedSH2 irradianceSH,
	float3 direction,
	float3 dominantAxis,
	float momentLength)
{
    //original input already contains the Lambert-convolved L0/L1 bands.
	float value = irradianceSH.Coefficient0 * Y00;
	value += Y1 * dot(float3(
		irradianceSH.Coefficient3,
		irradianceSH.Coefficient1,
		irradianceSH.Coefficient2), direction);

	float radianceL0 = irradianceSH.Coefficient0 * MATH_PI_INV;
	float zonalL1Coefficient = momentLength * 0.47746482927568f;
	float ratio = min(zonalL1Coefficient / max(radianceL0, 1e-6f), 1.7320508076f);
	float radianceL2Coefficient = radianceL0 * (0.08f * ratio + 0.6f * ratio * ratio);

	float z = dot(dominantAxis, direction);
	value += radianceL2Coefficient * MATH_PI_INV4 * Y20 * (3.0f * z * z - 1.0f);

	return irradianceSH.Chromaticity * value;
}

//||||||||||||||||||||||||||||||| SPHERICAL HARMONICS DOMINANT DIRECTION |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| SPHERICAL HARMONICS DOMINANT DIRECTION |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| SPHERICAL HARMONICS DOMINANT DIRECTION |||||||||||||||||||||||||||||||

struct FSHDominantLight
{
    float3 Direction;
    float3 HighlightRadiance;
    float  Directionality;
	float  MomentLength;
};

FSHDominantLight ExtractDominantLight(SimplifiedSH2 irradianceSH)
{
    float3 firstMoment = float3(
        irradianceSH.Coefficient3, // X
        irradianceSH.Coefficient1, // Y
        irradianceSH.Coefficient2  // Z
    );

    float momentLength = length(firstMoment);
    float dcCoefficient = max(irradianceSH.Coefficient0, 1e-6f);
    float averageRadiance = max(irradianceSH.Coefficient0 * Y00, 0.0f);

    FSHDominantLight light;
    light.Direction = momentLength > 1e-6f ? firstMoment / momentLength : float3(0.0f, 0.0f, 1.0f);
    light.Directionality = saturate((Y00 / Y1) * momentLength / dcCoefficient);
	light.MomentLength = momentLength;

	float peakRadiance = max(EvaluateSphericalHarmonicsScalar(irradianceSH, light.Direction), 0.0f);
    light.HighlightRadiance = irradianceSH.Chromaticity * (max(peakRadiance - averageRadiance, 0.0f) * light.Directionality);

    return light;
}

//||||||||||||||||||||||||||||||| GGX SPECULAR |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| GGX SPECULAR |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| GGX SPECULAR |||||||||||||||||||||||||||||||

float DistributionGGX(float NoH, float alpha2)
{
    float denominator = NoH * NoH * (alpha2 - 1.0f) + 1.0f;
    return alpha2 / max(MATH_PI * denominator * denominator, 1e-6f);
}

float GeometrySchlickGGX(float NoX, float k)
{
    return NoX / max(NoX * (1.0f - k) + k, 1e-6f);
}

float GGXSpecular(
	float3 normalDirection,
	float3 directionToCamera,
	float3 lightDirection,
	float NoL,
	float NoV,
	float geometryV,
	float alpha2,
	float k)
{
	//NOTE TO SELF: we ignore F0 and fresnel, as this is handled in a later pass when we combine specular
	//we only want a sharp highlight that scales with surface roughness
	//because in theory, the "radiance" should be just a cubemap

    if (NoL <= 1e-5f || NoV <= 1e-5f)
        return 0.0f;

    float3 H = normalize(directionToCamera + lightDirection);
    float NoH = saturate(dot(normalDirection, H));
    float distribution = DistributionGGX(NoH, alpha2);
	float geometryL = GeometrySchlickGGX(NoL, k);

	//the final NoL exactly cancels the BRDF denominator's NoL away from the rejected horizon epsilon.
    return (distribution * geometryV * geometryL) / max(4.0f * NoV, 1e-6f);
}

float GGXViewSpecular(float NoV, float geometryV, float alpha2)
{
	if (NoV <= 1e-5f)
		return 0.0f;

	float distribution = DistributionGGX(NoV, alpha2);
	return (distribution * geometryV * geometryV) / max(4.0f * NoV, 1e-6f);
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

//||||||||||||||||||||||||||||||| MAIN |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| MAIN |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| MAIN |||||||||||||||||||||||||||||||

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 localPixel = dispatchThreadId.xy;

    if (any(float2(localPixel) >= View_ViewSizeAndInvSize.xy))
        return;

    uint2 outputPixel = uint2(float2(localPixel) + 0.5f + View_ViewRectMin.xy);

    float4 gBufferB = GBufferBTexture.Load(int3(outputPixel, 0));
    uint packedShadingModel = (uint)round(gBufferB.a * 255.0f);
    uint shadingModelId = packedShadingModel & 0x0fu;

	if (shadingModelId == SHADINGMODELID_UNLIT)
	{
		float4 unlitOutput = float4(0.004f * View_PreExposure.xxx, 0.0f);
		RWEnvironmentIrradianceATexture[outputPixel] = unlitOutput;
		RWEnvironmentIrradianceBTexture[outputPixel] = unlitOutput;
		return;
	}

    float3 encodedNormal = GBufferATexture.Load(int3(outputPixel, 0)).xyz;
    float deviceZ = SceneDepthTexture.Load(int3(outputPixel, 0)).x;
    float roughness = gBufferB.b;

    float3 radiance = 0.0f;
    float3 irradiance = 0.0f;
    float outputAlpha = 0.0f;


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
				{
					float environmentLobeBlend = GBufferDTexture.Load(int3(outputPixel, 0)).z;
                    irradiance = lerp(irradiance, field.BaseColor, environmentLobeBlend);
				}

                outputAlpha = 1.0f;
            #else
				//The ambient-cube projection shares one chromaticity, so keep
				//only its six non-zero scalar SH coefficients.
                SimplifiedSH2 sphericalHarmonicsIrradiance = AmbientCubeToSH2(field);

                //now that our probe data is in spherical harmonics
                //we can use this to derive a dominant direction of light, and with this direction, we can use it to calculate a fresh specular highlight!
                //now even if our coefficents were spherical harmonics (and not ambient cube) trying to get sharp rough reflections is not possible
                //so the best thing we can do is to approximate a sharp specular highlight that varies with roughness, this will aid in our final "rough reflection" quality
                FSHDominantLight dominantLight = ExtractDominantLight(sphericalHarmonicsIrradiance);

                //diffuse lighting
                #if defined(SPHERICAL_HARMONICS_IRRADIANCE_ZH3)
                    irradiance = EvaluateIrradianceZH3Hallucinated(
						sphericalHarmonicsIrradiance,
						worldNormal,
						dominantLight.Direction,
						dominantLight.MomentLength);
                #else
                    irradiance = EvaluateSphericalHarmonics(sphericalHarmonicsIrradiance, worldNormal);
                #endif
        
                //specular lighting
                //NOTE: now I have some things to say about this... while normally in theory we shouldn't at all be sampling from this irradiance volume for reflections/specular at all
                //given the size of the game, and the sparse nature of reflection probes, and their quality, the best thing we can do is actually combine them which is a smart idea the game does
                //however there is a problem, because this probe data is diffuse/irradiance, not specular/radiance. so the resulting value is actually brighter and more blurry than it should be for reflections
                //for radiance it should be darker and a little more contrasty, to a point you should almost be able to make out some broad highlights
                //soooo, with that in mind, the neat thing is that now that we converted these coeffiecnts into SH, we can utilize some tricks to make this more plausible/accurate
                //the assumption is that these coefficents are already convolved for irradiance/diffuse, but thanks to spherical harmonic math it's possible to deconvolve these coefficents back into radiance
                //and that, is what we want for reflections!
				float alpha = roughness * roughness;
				float alpha2 = alpha * alpha;
				radiance = EvaluateRoughnessFilteredRadiance(sphericalHarmonicsIrradiance, reflectionDirection, alpha2);

                //since we know the dominant light direction
                //we can try to use this to occlude specular reflections that are away from the dominant source of light
                //this can certainly help us get a little more dimension and shape out of the radiance data and make it appear somewhat less flat and blurry
                #if defined(SPHERICAL_HARMONICS_DOMINANT_DIRECTION_SPECULAR_OCCLUSION)
                    radiance *= lerp(1.0f, saturate(dot(reflectionDirection, dominantLight.Direction) * 0.5f + 0.5f), SPHERICAL_HARMONICS_DOMINANT_DIRECTION_SPECULAR_OCCLUSION_FACTOR);
                #endif
                
                float domiantHighlightLuminance = 0.0f;

				//calculate NdotV
				#if defined(REQUIRE_NOV)
					float NoV = saturate(dot(worldNormal, directionToCamera));
				#endif

				#if defined(SPHERICAL_HARMONICS_DOMINANT_SPECULAR_HIGHLIGHT) || defined(CAMERA_VIEW_SPECULAR_HIGHLIGHT)
					float k = roughness + 1.0f;
					k = (k * k) * 0.125f;
					float geometryV = GeometrySchlickGGX(NoV, k);
				#endif

                #if defined(SPHERICAL_HARMONICS_DOMINANT_SPECULAR_HIGHLIGHT)
					float3 highlightDirection = dominantLight.Direction;
					float signedDominantNoL = dot(worldNormal, highlightDirection);
					float dominantNoL;
					float highlightScale = 1.0f;

					//for extra artistic specular kick
					#if defined(SPHERICAL_HARMONICS_DOMINANT_SPECULAR_HIGHLIGHT_DUAL)
						bool useDualHighlight = signedDominantNoL < 0.0f;
						highlightDirection = useDualHighlight ? -highlightDirection : highlightDirection;
						dominantNoL = saturate(abs(signedDominantNoL));
						highlightScale = useDualHighlight ? SPHERICAL_HARMONICS_DOMINANT_SPECULAR_HIGHLIGHT_DUAL_BOOST : 1.0f;
					#else
						dominantNoL = saturate(signedDominantNoL);
					#endif

					float dominantSpecular = GGXSpecular(
						worldNormal,
						directionToCamera,
						highlightDirection,
						dominantNoL,
						NoV,
						geometryV,
						alpha2,
						k);

					float3 dominantHighlight = dominantLight.HighlightRadiance * (dominantSpecular * highlightScale);

					dominantHighlight *= MATH_PI * SPHERICAL_HARMONICS_DOMINANT_SPECULAR_HIGHLIGHT_BOOST * (1.0f + dominantLight.Directionality);

                    radiance += dominantHighlight;
                    domiantHighlightLuminance = LuminanceRec709(dominantHighlight);
                #endif

                #if defined(CAMERA_VIEW_SPECULAR_HIGHLIGHT)
					float viewSpecularHighlight = GGXViewSpecular(NoV, geometryV, alpha2);
                    radiance += radiance * viewSpecularHighlight * MATH_PI_INV;
                    //domiantHighlightLuminance = saturate(domiantHighlightLuminance + viewSpecularHighlight);
                #endif

                #if defined(SPHERICAL_HARMONICS_DOMINANT_DIRECTION_DIFFUSE)
                    float random = InterleavedGradientNoise(outputPixel, View_FrameNumber);
                    float contactShadow = CalculateContactShadows(outputPixel, worldPosition, worldNormal, dominantLight.Direction, deviceZ, random, shadingModelId);

                    float dominantDiffuseShading = saturate(dot(worldNormal, dominantLight.Direction));
                    dominantDiffuseShading *= contactShadow;

                    irradiance *= lerp(dominantDiffuseShading, 1.0f, 0.25f);
                    radiance *= lerp(dominantDiffuseShading, 1.0f, 0.0f);
                #endif

				#if defined(CHARACTER_DOMINANT_DIRECTION_SHADING)
					if (shadingModelId == SHADINGMODELID_HAIR || 
						shadingModelId == SHADINGMODELID_EYE || 
						shadingModelId == SHADINGMODELID_PREINTEGRATED_SKIN)
					{
						float skinDominantDiffuseContrast = saturate(dot(worldNormal, dominantLight.Direction) * 0.5f + 0.5f);
						irradiance *= lerp(1.0f, skinDominantDiffuseContrast, CHARACTER_DOMINANT_DIRECTION_SHADING_AMOUNT);
					}
				#endif

                //the captured default-lit path uses GBufferD.z to blend the normal-evaluated result back toward the field's base color
                if (shadingModelId == SHADINGMODELID_DEFAULT_LIT)
				{
					float environmentLobeBlend = GBufferDTexture.Load(int3(outputPixel, 0)).z;
                    irradiance = lerp(irradiance, field.BaseColor, environmentLobeBlend);
				}

                //outputAlpha = 1.0f;
                outputAlpha = saturate(0.0001f + domiantHighlightLuminance);
            #endif
        }

	
	//downside with SH is negative values can happen, so make sure we don't output them
	irradiance = max(0.004f, irradiance);
	radiance = max(0.004f, radiance);

    RWEnvironmentIrradianceATexture[outputPixel] = float4(radiance * View_PreExposure, outputAlpha);
    RWEnvironmentIrradianceBTexture[outputPixel] = float4(irradiance * View_PreExposure, outputAlpha);
}