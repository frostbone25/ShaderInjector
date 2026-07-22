//ReflectionEnvironment.hlsl

//library includes
//NOTE: this is where we have various useful shader functions
#include "LibraryMath.hlsl"
#include "LibraryRandom.hlsl"
#include "LibraryColor.hlsl"
#include "LibraryGBuffer.hlsl"

//||||||||||||||||||||||||||||||| CONFIGURATION - GENERAL |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| CONFIGURATION - GENERAL |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| CONFIGURATION - GENERAL |||||||||||||||||||||||||||||||

//this controls the brightness of the final calculated ambient term that this shader calculates
//NOTE: this does not affect direct lighting
//DEFAULT: 1.0
#define AMBIENT_BRIGHTNESS 1.0

//this controls the brightness of the final combined ambient + direct light that this shader ultimately returns
//DEFAULT: 1.0
#define FINAL_BRIGHTNESS 1.0

//calculates a fresh direct lighting term for a "light source" that is placed right where the camera is (capcom style)
//this lighs up the diffuse/specular terms in ambient areas
#define CAMERA_AMBIENT_LIGHT

//in (cm) units
#define CAMERA_AMBIENT_LIGHT_RANGE 250000.0
#define CAMERA_AMBIENT_LIGHT_BRIGHTNESS 1.0
#define CAMERA_AMBIENT_LIGHT_SPECULAR_BOOST 0.5

//||||||||||||||||||||||||||||||| CONFIGURATION - GLOBAL ILLUMINATION (DIFFUSE / IRRADIANCE) |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| CONFIGURATION - GLOBAL ILLUMINATION (DIFFUSE / IRRADIANCE) |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| CONFIGURATION - GLOBAL ILLUMINATION (DIFFUSE / IRRADIANCE) |||||||||||||||||||||||||||||||

//#define DISABLE_IRRADIANCE

//this is the default diffuse/irradiance global illumination the game does.
//they sample the regular baked GI irradiance volumes, and then also try to use the "cubemap reflections" resampling them for "irradiance" and combine the two together
//cool idea in theory... but doesn't really work that well in practice mostly because the cubemap reflection volumes they have scattered throughout the game world are really sparse
//not to mention in many areas this combination of the irradiance volume and the reflection probes leads to a loss of color and some accuracy, contrast is better though but ehh I think it's better to just sample the irradiance volume directly
//NOTE TO SELF: to improve on their original idea I think we should calculate an "influence distance" since technically the closer we get to a reflection probe the more accurate the irradiance is at that point (more so than the irradiance volume)
//#define IRRADIANCE_BLENDING_DEFAULT

//this makes it so skin/eye/hair materials on characters only sample from the irradiance volume rather than all the other shenangins
//this flattens out their face in ambient light, but good occlusion will take care of shading them and occluding details for the most part
//plus... I think everyone prefers them looking flat rather than bug-eyed in ambient light
//the downside is less contrast/shading on their face, and even less color in certain spots, but generally overall this improves their faces during gameplay significantly.
//far less of a "frog face" look.
#define FLATTEN_CHARACTER_AMBIENT

//controls how strong the AO term is in ambient light (use in tandem with SSAO_BRIGHTNESS)
//NOTE: original game SSAO is much weaker than the SSGI AO, if your flipping back and fourth you'll need to readjust the values
#define SSAO_POWER 1.0

//controls how bright the AO term is in ambient light (use in tandem with SSAO_POWER)
//NOTE: original game SSAO is much weaker than the SSGI AO, if your flipping back and fourth you'll need to readjust the values
#define SSAO_BRIGHTNESS 1.0

//controls for a new SSGI solution, ground-truth visibility bitmask ambient occlusion
// - intended to replace the games original SSAO
// - also is used to add local bounce light
//this is among the best that exist (or that I can find), and the implementation also factors in bounce/indirect lighting
//NOTE: this can be quite expensive, so in an attempt to keep performance reasonable this is hardcoded to only be 1 sample per pixel
//while theroetically we can calculate multiple samples of the SSGI, it gets VERY expensive very fast
//so to keep costs low it is kept to 1 sample, and the animated noise pattern will blend results over time for better quality for FAR cheaper.
//the only negative, is that you will wind up with more noise in your image especially in low light areas unfortunately
//ideally also in the future this would be relegated to it's own special new render pass, where we can render at a much lower resolution
//and in addition do our own filtering/upsampling passes to not only clean up the noise but also make it MUCH more performant

//changes the noise pattern every frame (HIGHLY recomended, so the TAA in the game natrually blends results)
#define RANDOM_ANIMATE_NOISE

//how many SSGI/AO rays we fire
//IMPORTANT NOTE: this settings is insanely expensive, if you want to send your GPU to heaven in the name of less noise and better quality... be my geust!
//DEFAULT: 1
//RANGE: [1, 32]
#define SSGI_RAY_COUNT 1

//how many raymarching samples we do
//higher samples = better quality / less noise / worse performance
//lower samples = worse quality / more noise / better performance
//DEFAULT: 16
//RANGE: [1 <-> 128]
#define SSGI_RAYMARCHING_STEP_COUNT 16

//how far out does the SSGI max screen-pixel reach
//NOTE: affects both the SSGI AO and bounce light
#define SSGI_RAYMARCHING_WIDTH 512.0 

//world-space depth thickness
//NOTE: affects both the SSGI AO and bounce light
#define SSGI_THICKNESS 75.0

#define SSGI_NORMAL_BIAS 0.0005
#define SSGI_NORMAL_BIAS_HAIR 0.1

//calculate ground truth visibility based ambient occlusion
//this heavier than the original games SSAO, but it's far higher quality
//the added bonus is that this sets up alot of infrastructure for the bounce light
#define SSGI_AMBIENT_OCCLUSION

//calculate bounce light portion for SSGI pass, reusing the same information as the ambient occlusion
//this dramatically improves the lighting quality. adding bounce light from the sun, catching emissives or materials in the games that previously were not emitting light
//this DOES impact performance quite a bit, needing to sample the color buffer in order to bounce light around
//and also in its current state, there is no filtering so this can contribute to quite a bit noise in the final image
#define SSGI_BOUNCE_LIGHT

//shade half of the expensive SSGI rays each frame and reconstruct the missing half from the two computed neighbors in the same 2x2 pixel quad
//NOTE: checkerboard rendering is a definite WIN for performance especially at high resolutions, I think it's wise to leave this on even at the expense of quality
//#define SSGI_CHECKERBOARD
#define SSGI_CHECKERBOARD_QUAD_RECONSTRUCTION

//||||||||||||||||||||||||||||||| CONFIGURATION - GLOBAL ILLUMINATION (REFLECTION / RADIANCE) |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| CONFIGURATION - GLOBAL ILLUMINATION (REFLECTION / RADIANCE) |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| CONFIGURATION - GLOBAL ILLUMINATION (REFLECTION / RADIANCE) |||||||||||||||||||||||||||||||

//#define DISABLE_RADIANCE

//this disables SSR blending and instead fully relies on the reflection probes / irradiance volume the game uses for reflections
//NOTE: this does not give you a performance boost, SSR pass still runs, it just wont be used (if you do want a boost, find the SSR pass hlsl file and there is also a disable macro written there)
//#define DISABLE_SSR

#define CORRECT_REFLECTION_DIRECTION

//this is the default reflection/radiance global illumination the game does.
//they sample the regular baked GI irradiance volumes, and try to blend them with the "cubemap reflections" to combine the two together
//cool idea in theory... but doesn't really work that well in practice mostly because the cubemap reflection volumes they have scattered throughout the game world are really sparse
//not to mention in many areas this combination of the irradiance volume and the reflection probes leads to a loss of color and some accuracy, contrast is better though but ehh I think it's better to just sample the radiance volume directly
//NOTE TO SELF: to improve on their original idea I think we should calculate an "influence distance" since technically the closer we get to a reflection probe the more accurate the irradiance is at that point (more so than the irradiance volume)
//#define RADIANCE_BLENDING_DEFAULT

//#define REFLECTIONS_DISABLE_CUBEMAP
//#define REFLECTIONS_DISABLE_RADIANCE_VOLUME
//#define REFLECTIONS_COMBINED

//controls how much of the SSR is visible
//higher values = SSR more visible
//lower vlaues = SSR less visible
//DEFAULT: 100000 (why? because I modified the SSR shader and now its way higher quality. reflection quality is better + way less specular leaks... and plus the original game reflection cubemaps are really ugly and bad. SSR is the only hope of retaining some dignity for good quality reflections)
#define SSR_CONTRIBUTION_MULTIPLIER 100000

//this is an artistic tweak that darkens only rough cubemap-based reflections (not SSR reflections)
//this is because a significant amount of specular/light leak comes from these imprecise reflection sources
//so to mitigate that we go in and use the direct light buffer (which already has all direct light shading) and effectively derive a "non direct light" mask
//using that mask we darken rough reflections that are not in direct light of anything
#define SPECULAR_OCCLUSION_DIRECT_LIGHT_SHADOW
#define SPECULAR_OCCLUSION_DIRECT_LIGHT_SHADOW_FACTOR 0.95

//this is mostly artistic, but this adds an additional highlight for character eyes that comes from the camera to give them an extra specular kick
#define SPECULAR_EYE_HIGHLIGHT
#define SPECULAR_EYE_HIGHLIGHT_BOOST 1.0

//|||||||||||||||||||||||||||||||||| RESOURCES ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| RESOURCES ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| RESOURCES ||||||||||||||||||||||||||||||||||
//resources passed in to the shader

//SamplerComparisonState View_SharedPointWrappedSampler : register(s0); // can't disambiguate
//SamplerComparisonState View_SharedPointClampedSampler : register(s1); // can't disambiguate
//SamplerComparisonState View_SharedBilinearWrappedSampler : register(s2); // can't disambiguate
//SamplerComparisonState View_SharedBilinearClampedSampler : register(s3); // can't disambiguate
//SamplerComparisonState View_SharedTrilinearClampedSampler : register(s4); // can't disambiguate

SamplerState View_SharedPointWrappedSampler : register(s0); // can't disambiguate
SamplerState View_SharedPointClampedSampler : register(s1); // can't disambiguate
SamplerState View_SharedBilinearWrappedSampler : register(s2); // can't disambiguate
SamplerState View_SharedBilinearClampedSampler : register(s3); // can't disambiguate
SamplerState View_SharedTrilinearClampedSampler : register(s4); // can't disambiguate

TextureCube<float4> View_EnvironmentLightFallbackTexture : register(t0);

Texture2D<float4> View_PreIntegratedGF : register(t1);
Texture2D<float4> View_ThinFilmTableTexture : register(t2);
Texture2D<float4> GBufferATexture : register(t3);
Texture2D<float4> GBufferBTexture : register(t4);
Texture2D<float4> GBufferCTexture : register(t5);
Texture2D<float4> GBufferDTexture : register(t6);
Texture2D<float4> GBufferETexture : register(t7);
Texture2D<float4> SceneDepthTexture : register(t8);
Texture2D<float4> ScreenSpaceReflectionsTexture : register(t9);
Texture2D<float4> AmbientOcclusionTexture : register(t10);
Texture2D<float4> AmbiguousLightATexture : register(t11);
Texture2D<float4> AmbiguousLightBTexture : register(t12);
Texture2D<float4> EnvironmentIrradianceATexture : register(t13);
Texture2D<float4> EnvironmentIrradianceBTexture : register(t14);

RWTexture2D<float4> OutTextureColor : register(u0);

//|||||||||||||||||||||||||||||||||| CONSTANT BUFFERS ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| CONSTANT BUFFERS ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| CONSTANT BUFFERS ||||||||||||||||||||||||||||||||||
//game data passed in to the shader

cbuffer _Globals : register(b0) 
{
	float AOObjectMaxDistance : packoffset(c0.x);
	float AOStepScale : packoffset(c0.y);
	float AOStepExponentScale : packoffset(c0.z);
	float AOMaxViewDistance : packoffset(c0.w);
	float DistanceFadeScale : packoffset(c1.x);
	float AOGlobalMaxOcclusionDistance : packoffset(c1.y);
	int2 TileListGroupSize : packoffset(c1.z);
	int CurrentLevelDownsampleFactor : packoffset(c2.x);
	float2 AOBufferSize : packoffset(c2.y);
	int DownsampleFactorToBaseLevel : packoffset(c2.w);
	float2 BaseLevelTexelSize : packoffset(c3.x);
	float BentNormalNormalizeFactor : packoffset(c3.z);
	float2 AOBufferBilinearUVMax : packoffset(c4.x);
	int2 ScreenGridConeVisibilitySize : packoffset(c4.z);
	float2 JitterOffset : packoffset(c5.x);
	int bSceneLightingChannelsValid : packoffset(c5.z);
	int4 StaticShadowDepthTileSize : packoffset(c6.x);
	int4 StaticShadowDepthTileRange : packoffset(c7.x);
	float4 HZBCoordinateContext : packoffset(c8.x);
	float4 DistantViewEnvironmentContext : packoffset(c9.x);
	int NumPrecomputedLightEnvironments : packoffset(c10.x);
	float4 DebugContext : packoffset(c11.x);
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
    uint  HierarchyDepth;
    row_major float4x4 WorldToGrid;
    row_major float4x4 GridToWorld;
};

struct FPrecomputedLightEnvironmentProbe
{
    float4 PositionAndUnused;
    int    Unused0;
    int    ReflectionIndex;
    int    DistanceCubeIndex;
    int    Unused1;
};

struct FPrecomputedLightEnvironmentProbeBound
{
    int  ProbeIndices[8];
    int4 MinGridAndOccluderData;
};

StructuredBuffer<FPrecomputedLightEnvironmentProbeBoundsInfo> PrecomputedLightProbeBoundsInfoSRVs[]    : register(t0, space20);
StructuredBuffer<FPrecomputedLightEnvironmentProbe>           PrecomputedLightProbeSRVs[]              : register(t0, space5);
StructuredBuffer<FPrecomputedLightEnvironmentProbeBound>      PrecomputedLightProbeBoundsSRVs[]        : register(t0, space7);
StructuredBuffer<int>                                         PrecomputedLightProbeBoundsIndicesSRVs[] : register(t0, space8);
TextureCubeArray<float4>                                      DistanceCubeArray[]                       : register(t0, space10);
Buffer<float>                                                 OccluderTrianglesSRVs[]                   : register(t0, space11);
Buffer<uint>                                                  OccluderIndicesSRVs[]                     : register(t0, space12);
TextureCube<float4>                                           ReflectionCubemaps[]                      : register(t0, space1);

//|||||||||||||||||||||||||||||||||| STRUCTS ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| STRUCTS ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| STRUCTS ||||||||||||||||||||||||||||||||||

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
    float Metallic;
    float Specular;
    float Roughness;
    float MaterialAO;
    float4 CustomData;
    uint ShadingModelID;
    uint PackedFlags;
    //float SelectiveOutputMaskHighNibble;

    float3 DiffuseColor;
    float3 F0; //float3 SpecularColor;
    float3 EnergyColor; //float3 ShadingDiffuseColor;

    float3 CoatNormalData;
    float Coat;
    float CoatRoughness;
    float CoatTransmission;
    float WetnessOrIOR;

    //float EyeMetallicPayload;
    //float EyePayloadFromAO;
};

struct ProbeSelection
{
    int Environment;
    int Bound;
    float3 LocalPosition;
    float3 GridPosition;
};

struct EnvironmentSamples
{
    float3 CubemapIrradianceRough;
    float3 CubemapReflectionRough;
    float3 CubemapIrradiance;
    float3 CubemapReflection;
};

//|||||||||||||||||||||||||||||||||| SPACE CONVERSIONS ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| SPACE CONVERSIONS ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| SPACE CONVERSIONS ||||||||||||||||||||||||||||||||||

// View_ScreenPositionScaleBias maps clip NDC into buffer UV.  Its bias is
// stored in .wz for this shader permutation (not .zw).  Using the inverse of
// that exact mapping keeps reconstruction valid when the view occupies only a
// portion of the render buffer, including dynamic resolution and odd sizes.
float2 BufferUVToNDC(float2 bufferUV)
{
    return (bufferUV - View_ScreenPositionScaleBias.wz) / View_ScreenPositionScaleBias.xy;
}

float2 ViewPixelToBufferUV(float2 viewPixel)
{
    float2 bufferPixel = viewPixel + View_ViewRectMin.xy;
    return bufferPixel * View_BufferSizeAndInvSize.zw;
}

float3 ReconstructWorldPosition(float2 svPosition, float deviceZ)
{
    float2 bufferUV = svPosition * View_BufferSizeAndInvSize.zw;
    float2 ndc = BufferUVToNDC(bufferUV);

    float4 worldPosition = mul(View_ClipToWorld, float4(ndc, deviceZ, 1.0f));
    return worldPosition.xyz / worldPosition.w;
}

float ConvertFromDeviceZ(float DeviceZ)
{
    return mad(View_InvDeviceZToWorldZTransform.x, DeviceZ, View_InvDeviceZToWorldZTransform.y)
        + rcp(mad(View_InvDeviceZToWorldZTransform.z, DeviceZ, -View_InvDeviceZToWorldZTransform.w));
}

// World -> TranslatedWorld
float3 WorldToTranslatedWorld(float3 WorldPos)
{
    return WorldPos + View_PreViewTranslation;
}

// TranslatedWorld -> ViewSpace
float3 TranslatedWorldToViewSpace(float3 TWPos)
{
    return mul(View_TranslatedWorldToView, float4(TWPos, 1.0f)).xyz;
}

// World -> ViewSpace (combines the two above)
float3 WorldToViewSpace(float3 WorldPos)
{
    return TranslatedWorldToViewSpace(WorldToTranslatedWorld(WorldPos));
}

// ViewSpace direction (no translation needed)
float3 WorldDirToViewDir(float3 WorldDir)
{
    return mul((float3x3)View_TranslatedWorldToView, WorldDir);
}

uint2 ScreenPixelToBufferPixel(float2 PixelXY)
{
    return (uint2)floor(PixelXY) + uint2(View_ViewRectMin.xy);
}

float3 ScreenPixelToViewRayZ1(float2 PixelXY)
{
    float2 NDC = BufferUVToNDC(ViewPixelToBufferUV(PixelXY));

    float4 ViewRay = mul(View_ClipToView, float4(NDC, 1.0f, 1.0f));
    ViewRay.xyz /= ViewRay.w;

    float RayZ = (abs(ViewRay.z) > 1.0e-6f) ? ViewRay.z : ((ViewRay.z < 0.0f) ? -1.0e-6f : 1.0e-6f);
    return ViewRay.xyz / RayZ;
}

float3 SampleSceneRadiance(float2 PixelXY)
{
    uint2 BufferPixel = ScreenPixelToBufferPixel(PixelXY);
    return OutTextureColor[BufferPixel].rgb * View_OneOverPreExposure;
}

//||||||||||||||||||||||||||||||| GBUFFER DECODING |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| GBUFFER DECODING |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| GBUFFER DECODING |||||||||||||||||||||||||||||||

float3 DecodeGBufferNormal(float3 EncodedNormal)
{
    //return normalize(EncodedNormal * 2.0f - 1.0f);
	return EncodedNormal * 2.0f - 1.0f; //don't think normalize is necessary because everything should already be in range anyway
}

uint DecodeShadingModelId(float GBufferBA)
{
    return (uint)round(saturate(GBufferBA) * 255.0f) & 0x0fu;
}

uint DecodePackedShadingModelByte(float GBufferBA)
{
    return (uint)round(saturate(GBufferBA) * 255.0f);
}

uint DecodeCustomStencilHighNibble(float GBufferEA)
{
    return ((uint)round(saturate(GBufferEA) * 255.0f) >> 4u) & 0x0fu;
}

float DecodeSpecularScalar(float encodedSpecular)
{
    float s = saturate(encodedSpecular);
    float decoded = (s < 0.666667) ? (s * 0.06) : (s * 0.36 - 0.2);
    return clamp(decoded, 0.0, 0.16);
}

//NOTE: we have an bug where object shading looks "wet"
FGBufferData DecodeGBuffer(uint2 vector_uvInt)
{
    FGBufferData gbufferData;
    gbufferData.GBufferA = GBufferATexture.Load(int3(vector_uvInt, 0));
    gbufferData.GBufferB = GBufferBTexture.Load(int3(vector_uvInt, 0));
    gbufferData.GBufferC = GBufferCTexture.Load(int3(vector_uvInt, 0));
    gbufferData.GBufferD = GBufferDTexture.Load(int3(vector_uvInt, 0));
    gbufferData.GBufferE = GBufferETexture.Load(int3(vector_uvInt, 0));
	gbufferData.DeviceDepth = SceneDepthTexture.Load(int3(vector_uvInt, 0)).r;
	gbufferData.ScreenAO = AmbientOcclusionTexture.Load(int3(vector_uvInt, 0)).r;

    gbufferData.PackedFlags = (uint)round(gbufferData.GBufferB.a * 255.0);
    gbufferData.ShadingModelID = gbufferData.PackedFlags & 15u;

	int packedMask = (int)round(saturate(gbufferData.GBufferE.a) * 255.0);
    gbufferData.WetnessOrIOR = (packedMask >> 4u & 15u) * (1.0 / 15.0);

	bool isEye = (gbufferData.ShadingModelID == SHADINGMODELID_EYE);

    gbufferData.WorldNormal = DecodeGBufferNormal(gbufferData.GBufferA.xyz);
    gbufferData.SecondaryNormalOrTangent = DecodeGBufferNormal(gbufferData.GBufferE.xyz);
	gbufferData.BaseColor = gbufferData.GBufferC.rgb;
    gbufferData.Roughness = (gbufferData.ShadingModelID == SHADINGMODELID_PREINTEGRATED_SKIN) ? gbufferData.GBufferB.b * 0.93655
                          : (gbufferData.ShadingModelID == SHADINGMODELID_HAIR) ? 0.48 : gbufferData.GBufferB.b;
    gbufferData.Metallic = isEye ? 0.0 : gbufferData.GBufferB.r;
    gbufferData.MaterialAO = isEye ? 1.0 : gbufferData.GBufferC.a;    
	gbufferData.CustomData = gbufferData.GBufferD;

    gbufferData.Coat = isEye ? gbufferData.GBufferB.r : 0.0;
    gbufferData.CoatRoughness = isEye ? gbufferData.GBufferB.g : 0.0;
    gbufferData.CoatTransmission = isEye ? gbufferData.GBufferC.a : 0.0;

    float packedSpecular = saturate(gbufferData.GBufferA.a);
    float dielectric = clamp(packedSpecular < (2.0 / 3.0)
                           ? packedSpecular * 0.06
                           : packedSpecular * 0.36 - 0.20,
                             0.0, 0.16);

    gbufferData.DiffuseColor = (1.0 - gbufferData.Metallic) * gbufferData.GBufferC.rgb;
    gbufferData.F0 = dielectric + gbufferData.Metallic * (gbufferData.GBufferC.rgb - dielectric);

    //float medium = saturate(gbufferData.WetnessOrIOR * (View_WetnessIntensity > 0.0 ? 1.0 : 0.0) + (View_FogContextMediumIOR > 1.0 ? 1.0 : 0.0));
    float medium = 0.0f;
    float3 shifted = pow(max((gbufferData.F0 - 0.02) * (1.0 / 0.98), 0.0), 1.4);
    gbufferData.F0 = lerp(gbufferData.F0, shifted, medium);
    gbufferData.EnergyColor = gbufferData.F0 * 0.45 + gbufferData.DiffuseColor;
    gbufferData.CoatNormalData = gbufferData.GBufferD.xyz;
    return gbufferData;
}

//|||||||||||||||||||||||||||||||||| PRECOMPUTED LIGHT ENVIRONMENT ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| PRECOMPUTED LIGHT ENVIRONMENT ||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||| PRECOMPUTED LIGHT ENVIRONMENT ||||||||||||||||||||||||||||||||||

//NOTE TO SELF: for reflections we experimented with parallax correction given we already have influence volumes
//however... didn't get good results and just ultimately got repeating tiles all throughout the world
//even when we tried explicitly using the right volumes and got past that, it seems that in actuality reflections are really sparsely populated
//and they cover such a large area, that trying to derrive any kind of box volume to parallax correct them is just pointless

int FindProbeBoundIndexAtGrid(uint environment, float3 gridPosition, FPrecomputedLightEnvironmentProbeBoundsInfo info)
{
    if (any(gridPosition < 0.0) || any(gridPosition >= (float3)info.GridSize))
        return -1;

    uint3 cell = (uint3)floor(gridPosition);
    uint rootShift = (info.HierarchyDepth - 1u) & 31u;
    uint rootSpan = 1u << rootShift;
    uint3 root = cell >> rootShift;
    uint3 rootDims = (info.GridSize + rootSpan - 1u) >> rootShift;
    uint rootIndex = (root.z * rootDims.y + root.y) * rootDims.x + root.x;
    int packedNode = PrecomputedLightProbeBoundsIndicesSRVs[NonUniformResourceIndex(environment)][rootIndex];
    
	if (packedNode == -1)
        return -1;

    uint baseIndex = (uint)packedNode & 0x0fffffffu;
    uint childLevel = (uint)packedNode >> 28u;
    
	if (childLevel == 0u)
        return (int)baseIndex;

    uint childShift = (info.HierarchyDepth - childLevel) & 31u;
    uint3 rootLocal = cell - (root << rootShift);
    uint3 child = rootLocal >> ((childLevel - 1u) & 31u);
    uint childIndex = baseIndex + child.x + (child.y << childShift) + (child.z << (childShift * 2u));
    int leaf = PrecomputedLightProbeBoundsIndicesSRVs[NonUniformResourceIndex(environment)][childIndex];
    
	return leaf == -1 ? -1 : (leaf & 0x0fffffff);
}

ProbeSelection FindProbeBound(float3 worldPosition)
{
    ProbeSelection result;
    result.Environment = -1;
    result.Bound = -1;
    result.LocalPosition = 0.0;
    result.GridPosition = 0.0;

    [loop]
    for (uint environment = 0; environment < NumPrecomputedLightEnvironments; ++environment)
    {
        if (asint(PLESceneInfo_PLEPositions[environment].w) == -2)
            continue;

        FPrecomputedLightEnvironmentProbeBoundsInfo info = PrecomputedLightProbeBoundsInfoSRVs[NonUniformResourceIndex(environment)][0];
        float3 local = worldPosition - PLESceneInfo_PLEPositions[environment].xyz;
        float3 grid = mul(float4(local, 1.0), info.WorldToGrid).xyz;

        if (any(grid < 0.0) || any(grid >= (float3)info.GridSize))
            continue;

        uint3 cell = (uint3)floor(grid);
        uint rootShift = (info.HierarchyDepth - 1u) & 31u;
        uint rootSpan = 1u << rootShift;
        uint3 root = cell >> rootShift;
        uint3 rootDims = (info.GridSize + rootSpan - 1u) >> rootShift;
        uint rootIndex = (root.z * rootDims.y + root.y) * rootDims.x + root.x;
        int packedNode = PrecomputedLightProbeBoundsIndicesSRVs[NonUniformResourceIndex(environment)][rootIndex];
        
		if (packedNode == -1)
            continue;

        uint baseIndex = (uint)packedNode & 0x0fffffffu;
        uint childLevel = (uint)packedNode >> 28u;

        if (childLevel == 0u)
        {
            result.Environment = (int)environment;
            result.Bound = (int)baseIndex;
            result.LocalPosition = local;
            result.GridPosition = grid;
            return result;
        }

        uint childShift = (info.HierarchyDepth - childLevel) & 31u;
        uint childSpan = 1u << childShift;
        uint3 rootLocal = cell - (root << rootShift);
        uint3 child = rootLocal >> ((childLevel - 1u) & 31u);
        uint childIndex = baseIndex + child.x + (child.y << childShift) + (child.z << (childShift * 2u));
        int leaf = PrecomputedLightProbeBoundsIndicesSRVs[NonUniformResourceIndex(environment)][childIndex];
        
		if (leaf != -1)
        {
            result.Environment = (int)environment;
            result.Bound = leaf & 0x0fffffff;
            result.LocalPosition = local;
            result.GridPosition = grid;
            return result;
        }
    }

    return result;
}

bool RayHitsTriangle(float3 origin, float3 direction, float maxDistance, float3 a, float3 b, float3 c)
{
    // Algebra/order follows the scalar Moller-Trumbore sequence in the DXIL.
    float3 e1 = b - a;
    float3 e2 = c - a;
    float3 p = cross(direction, e2);
    float determinant = dot(e1, p);

    if (determinant > -1.0e-6 && determinant < 1.0e-6)
        return false;

    float invDet = rcp(determinant);
    float3 t = origin - a;
    float u = dot(t, p) * invDet;

    if (u < 0.0 || u > 1.0)
        return false;

    float3 q = cross(t, e1);
    float vNumerator = dot(direction, q);
    float v = vNumerator * invDet;

    if (v < 0.0 || (vNumerator + dot(t, p)) * invDet > 1.0)
        return false;

    float distance = dot(e2, q) * invDet;

    return distance >= 1.0e-6 && distance < maxDistance;
}

float ProbeVisibility(uint environment, uint packedOccluder, int distanceCube, float3 localPixel, float3 probePosition, float3 probeDirection, float probeDistance)
{
    float visibility = 1.0;

    if (distanceCube != -1)
    {
        float2 moments = DistanceCubeArray[NonUniformResourceIndex(environment)].SampleLevel(View_SharedBilinearWrappedSampler, float4(-probeDirection, (float)distanceCube), 0.0).xy * 100.0;
        float threshold = moments.x + 1.0;
        float variance = abs(moments.x * moments.x - moments.y) + 1.0;

        if (probeDistance >= threshold)
        {
            float d = probeDistance - threshold;
            visibility = variance / (d * d + variance);
        }
    }

    uint triangleCount = packedOccluder & 255u;
    uint triangleOffset = packedOccluder >> 8u;

    if (triangleCount == 0u)
        return visibility;

    uint baseOffset = (uint)PLESceneInfo_NumProbeBounds[environment];

    [loop]
    for (uint i = 0; i < triangleCount; ++i)
    {
        uint triangleIndex = OccluderIndicesSRVs[NonUniformResourceIndex(environment)][baseOffset + triangleOffset + i];
        uint p = triangleIndex * 9u;
        
		float3 a = float3(OccluderTrianglesSRVs[NonUniformResourceIndex(environment)][p + 0u],
                          OccluderTrianglesSRVs[NonUniformResourceIndex(environment)][p + 1u],
                          OccluderTrianglesSRVs[NonUniformResourceIndex(environment)][p + 2u]);
        float3 b = float3(OccluderTrianglesSRVs[NonUniformResourceIndex(environment)][p + 3u],
                          OccluderTrianglesSRVs[NonUniformResourceIndex(environment)][p + 4u],
                          OccluderTrianglesSRVs[NonUniformResourceIndex(environment)][p + 5u]);
        float3 c = float3(OccluderTrianglesSRVs[NonUniformResourceIndex(environment)][p + 6u],
                          OccluderTrianglesSRVs[NonUniformResourceIndex(environment)][p + 7u],
                          OccluderTrianglesSRVs[NonUniformResourceIndex(environment)][p + 8u]);
        
		if (RayHitsTriangle(localPixel, probeDirection, probeDistance, a, b, c))
            return 0.0;
    }

    return visibility;
}

EnvironmentSamples SampleFallback(float3 vector_reflectionDirection, float3 vector_normalDirection, float roughMip, float axisBlend)
{
    EnvironmentSamples environment = (EnvironmentSamples)0;

    if (View_EnvironmentLightFallbackContext.w == 0.0)
        return environment;

    float scale = exp2(View_EnvironmentLightFallbackContext.x);

    environment.CubemapReflection = scale * View_EnvironmentLightFallbackTexture.SampleLevel(View_SharedTrilinearClampedSampler, vector_reflectionDirection, roughMip).rgb;
    environment.CubemapReflectionRough = scale * View_EnvironmentLightFallbackTexture.SampleLevel(View_SharedBilinearClampedSampler, vector_reflectionDirection, 8.0).rgb;

    environment.CubemapIrradianceRough = scale * View_EnvironmentLightFallbackTexture.SampleLevel(View_SharedBilinearClampedSampler, vector_normalDirection, 8.0).rgb;
	float3 cubemapIrradiance = View_EnvironmentLightFallbackTexture.SampleLevel(View_SharedBilinearClampedSampler, vector_normalDirection, 4.0).rgb;

    //really not sure what they are doing here
    float3 cubemapUnknownSample = View_EnvironmentLightFallbackTexture.SampleLevel(View_SharedPointWrappedSampler, float3(1.0, 0.0, 0.0), 7.0).rgb;

    environment.CubemapIrradiance = scale * lerp(cubemapIrradiance, cubemapUnknownSample, axisBlend);

	return environment;
}

EnvironmentSamples SampleProbeEnvironment(ProbeSelection selection, float3 vector_reflectionDirection, float3 coatNormal, float roughMip, float axisBlend)
{
    if (((uint)selection.Bound & 0x0fffffffu) == 0x0fffffffu)
        return SampleFallback(vector_reflectionDirection, coatNormal, roughMip, axisBlend);

    uint environment = (uint)selection.Environment;
    FPrecomputedLightEnvironmentProbeBound bound = PrecomputedLightProbeBoundsSRVs[NonUniformResourceIndex(environment)][selection.Bound];
    FPrecomputedLightEnvironmentProbeBoundsInfo boundsInfo = PrecomputedLightProbeBoundsInfoSRVs[NonUniformResourceIndex(environment)][0];
    uint divisor = (uint)bound.MinGridAndOccluderData.w & 255u;
    uint packedOccluderIndex = (uint)bound.MinGridAndOccluderData.w >> 8u;
    float3 f = saturate((selection.GridPosition - (float3)bound.MinGridAndOccluderData.xyz) / (float)divisor);

    int reflectionIds[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    float reflectionWeights[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    uint reflectionCount = 0u;
    uint validProbeCount = 0u;
    float3 derivedBoxMinLocal = 1.0e30;
    float3 derivedBoxMaxLocal = -1.0e30;
    uint packedOccluder = OccluderIndicesSRVs[NonUniformResourceIndex(environment)][packedOccluderIndex];

    [unroll]
    for (uint corner = 0; corner < 8u; ++corner)
    {
        int probeIndex = bound.ProbeIndices[corner];

        if (probeIndex == -1)
            continue;

        FPrecomputedLightEnvironmentProbe probe = PrecomputedLightProbeSRVs[NonUniformResourceIndex(environment)][probeIndex];
        derivedBoxMinLocal = min(derivedBoxMinLocal, probe.PositionAndUnused.xyz);
        derivedBoxMaxLocal = max(derivedBoxMaxLocal, probe.PositionAndUnused.xyz);
        ++validProbeCount;
        float3 toProbe = probe.PositionAndUnused.xyz - selection.LocalPosition;
        float probeDistance = length(toProbe);
        float3 probeDirection = toProbe * rsqrt(dot(toProbe, toProbe));
        float3 choose = float3(corner & 1u, (corner >> 1u) & 1u, (corner >> 2u) & 1u);
        float3 cornerFactor = lerp(1.0 - f, f, choose);
        float weight = cornerFactor.x * cornerFactor.y * cornerFactor.z;
        weight *= ProbeVisibility(environment, packedOccluder, probe.DistanceCubeIndex, selection.LocalPosition, probe.PositionAndUnused.xyz, probeDirection, probeDistance);

        if (weight <= 0.0)
            continue;

        bool merged = false;

        [loop]
        for (uint i = 0; i < reflectionCount; ++i)
        {
            if (reflectionIds[i] == probe.ReflectionIndex)
            {
                reflectionWeights[i] += weight;
                merged = true;
                break;
            }
        }

        if (!merged && reflectionCount < 8u)
        {
            reflectionIds[reflectionCount] = probe.ReflectionIndex;
            reflectionWeights[reflectionCount] = weight;
            ++reflectionCount;
        }
    }

    EnvironmentSamples environmentSamples = (EnvironmentSamples)0;

    if (reflectionCount == 0u)
        return environmentSamples;

	float3 reflectionBoxMins[8];
    float3 reflectionBoxMaxs[8];

    [unroll]
    for (uint boxIndex = 0u; boxIndex < 8u; ++boxIndex)
    {
        reflectionBoxMins[boxIndex] = derivedBoxMinLocal;
        reflectionBoxMaxs[boxIndex] = derivedBoxMaxLocal;
    }

    float3 logCubemapReflectionRough = 0.0;
    float3 logCubemapReflection = 0.0;

    float3 logCubemapIrradianceRough = 0.0;
    float3 logCubemapIrradiance = 0.0;

    float totalWeight = 0.0;

    int reflectionOffset = PLESceneInfo_ReflectionOffsets[environment];

    [loop]
    for (uint i = 0; i < reflectionCount; ++i)
    {
        float weight = reflectionWeights[i];

        if (weight <= 0.0)
            continue;

        int cubeIndex = reflectionOffset + reflectionIds[i];
        float scale = exp2(PLESceneInfo_ReflectionProperties[cubeIndex].x);

        float3 cubemapReflection = scale * ReflectionCubemaps[NonUniformResourceIndex(cubeIndex)].SampleLevel(View_SharedTrilinearClampedSampler, vector_reflectionDirection, roughMip).rgb;
        float3 cubemapReflectionRough = scale * ReflectionCubemaps[NonUniformResourceIndex(cubeIndex)].SampleLevel(View_SharedBilinearClampedSampler, vector_reflectionDirection, 8.0).rgb;
        float3 cubemapIrradiance = scale * ReflectionCubemaps[NonUniformResourceIndex(cubeIndex)].SampleLevel(View_SharedBilinearClampedSampler, coatNormal, 4.0).rgb;
        float3 cubemapIrradianceRough = scale * ReflectionCubemaps[NonUniformResourceIndex(cubeIndex)].SampleLevel(View_SharedBilinearClampedSampler, coatNormal, 8.0).rgb;

        //really not sure what they are doing here
        float3 cubemapUnknownSample = scale * ReflectionCubemaps[NonUniformResourceIndex(cubeIndex)].SampleLevel(View_SharedPointWrappedSampler, float3(1.0, 0.0, 0.0), 7.0).rgb;

        logCubemapIrradianceRough += weight * log2(max(cubemapIrradianceRough, 1.0e-9));
        logCubemapReflectionRough += weight * log2(max(cubemapReflectionRough, 1.0e-9));
        logCubemapIrradiance += weight * log2(max(lerp(cubemapIrradiance, cubemapUnknownSample, axisBlend), 1.0e-9));
        logCubemapReflection += weight * log2(max(cubemapReflection, 1.0e-9));
        totalWeight += weight;
    }

    if (totalWeight > 1.0e-4)
    {
        float inverseTotalWeight = rcp(totalWeight);
        environmentSamples.CubemapIrradianceRough = exp2(logCubemapIrradianceRough * inverseTotalWeight);
        environmentSamples.CubemapReflectionRough = exp2(logCubemapReflectionRough * inverseTotalWeight);
        environmentSamples.CubemapIrradiance = exp2(logCubemapIrradiance * inverseTotalWeight);
        environmentSamples.CubemapReflection = exp2(logCubemapReflection * inverseTotalWeight);
    }
    else
    {
        environmentSamples.CubemapIrradianceRough = logCubemapIrradianceRough;
        environmentSamples.CubemapReflectionRough = logCubemapReflectionRough;
        environmentSamples.CubemapIrradiance = logCubemapIrradiance;
        environmentSamples.CubemapReflection = logCubemapReflection;
    }

    return environmentSamples;
}

void CombineIrradianceVolumesAndReflections(inout EnvironmentSamples environment, float4 radianceVolume, float4 irradianceVolume, FGBufferData gbufferData, float3 directLighting)
{
    //NOTE: this is artistic... and not physically really that plausible, however I am very limited in what I can do in regards to specular occlusion
    //so an idea here is to actually leverage the existing OutTextureColor which by this point in the renderer effectively only contains the direct light information
    //now since this shader is primarly intended for handling the non-direct light portions of the image, we can do some tricker
    //for instance anything that is not within direct light... is likely dark/ambient so we will use this information to reduce the specular contribution
    //remake funny enough does a similar thing with their lightmaps in the first game... so we are kinda leveraging that same idea here
    #if defined(SPECULAR_OCCLUSION_DIRECT_LIGHT_SHADOW)
        float directLightContribution = saturate(LuminanceRec709(directLighting));
        float directLightSpecularOcclusion = lerp(1.0f, min(directLightContribution, 1.0f), SPECULAR_OCCLUSION_DIRECT_LIGHT_SHADOW_FACTOR);

        //NOTE: only apply this to the reflection probes, radiance volume and SSR should remain at full intensity as it's the most accurate
        //environment.CubemapReflection *= directLightSpecularOcclusion; 
        //environment.CubemapReflectionRough *= directLightSpecularOcclusion; 
    #endif

    //============================= RADIANCE (REFLECTION) =============================
    #if defined(REFLECTIONS_DISABLE_RADIANCE_VOLUME)
        radianceVolume = 0.0f;
    #endif

    float radianceVolumeLuminance = LuminanceRec709(radianceVolume.rgb);
    float radianceVolumeLuminanceInverse = radianceVolumeLuminance > 0.0 ? rcp(radianceVolumeLuminance) : 0.0;

    float cubemapReflectionLuminance = LuminanceRec709(environment.CubemapReflection);
    float cubemapReflectionLuminanceInverse = cubemapReflectionLuminance > 0.0 ? rcp(cubemapReflectionLuminance) : 0.0;

    float cubemapReflectionRoughLuminance = LuminanceRec709(environment.CubemapReflectionRough);
    float cubemapReflectionRoughLuminanceInverse = cubemapReflectionRoughLuminance > 0.0 ? rcp(cubemapReflectionRoughLuminance) : 0.0;

    float cubemapIrradianceRoughLuminance = LuminanceRec709(environment.CubemapIrradianceRough);
    float cubemapIrradianceRoughLuminanceInverse = cubemapIrradianceRoughLuminance > 0.0 ? rcp(cubemapIrradianceRoughLuminance) : 0.0;

    float cubemapRadianceAndRadianceVolume = cubemapReflectionRoughLuminanceInverse * radianceVolumeLuminance;
    float cubemapRadianceAndRadianceVolumeInvert = cubemapRadianceAndRadianceVolume - 1.0;
    float radianceReject = saturate((cubemapRadianceAndRadianceVolumeInvert * cubemapRadianceAndRadianceVolumeInvert) / (cubemapRadianceAndRadianceVolumeInvert * cubemapRadianceAndRadianceVolumeInvert + 0.25));
    float3 radianceTarget = cubemapReflectionLuminance * radianceVolume.rgb * radianceVolumeLuminanceInverse;
    //float3 radianceTarget = environment.CubemapReflection * radianceVolume.rgb * radianceVolumeLuminanceInverse; //NOTE TO SELF: we lose original cubemap colors too much... gotta be a way to preserve them

    #if defined(REFLECTIONS_DISABLE_CUBEMAP)
        environment.CubemapReflection = 0.0f;
    #endif

    #if defined(RADIANCE_BLENDING_DEFAULT)
        environment.CubemapReflection = lerp(environment.CubemapReflection, radianceTarget, radianceReject) * cubemapRadianceAndRadianceVolume;
    #else
        //environment.CubemapReflection = radianceVolume.rgb;

        float radianceToCubemapBlend = gbufferData.Roughness;

        //knock the range down from [0..1] to [0..0.5]
        radianceToCubemapBlend -= 0.35f;
        radianceToCubemapBlend *= 3.5f;
        radianceToCubemapBlend = saturate(radianceToCubemapBlend);
        //radianceToCubemapBlend = lerp(radianceToCubemapBlend, 1.0f, radianceVolume.a);

        //environment.CubemapReflection = radianceToCubemapBlend * View_OneOverPreExposure;

        //environment.CubemapReflection = lerp(0.0f, radianceVolume.rgb, radianceToCubemapBlend);
        //environment.CubemapReflection = lerp(radianceTarget, radianceVolume.rgb, radianceToCubemapBlend);
        environment.CubemapReflection = lerp(radianceTarget * cubemapRadianceAndRadianceVolume, radianceVolume.rgb, radianceToCubemapBlend);

        //environment.CubemapReflection.rgb *= 1.0f + radianceVolume.a;
    #endif

    //============================= IRRADIANCE (DIFFUSE) =============================

    float irradianceVolumeLuminance = LuminanceRec709(irradianceVolume.rgb);
    float irradianceVolumeLuminanceInverse = irradianceVolumeLuminance > 0.0 ? rcp(irradianceVolumeLuminance) : 0.0;

    float cubemapIrradianceLuminance = LuminanceRec709(environment.CubemapIrradiance);

    float cubemapIrradianceAndIrradianceVolume = cubemapIrradianceRoughLuminanceInverse * irradianceVolumeLuminance;
    float cubemapIrradianceAndIrradianceVolumeInvert = cubemapIrradianceAndIrradianceVolume - 1.0;
    float irradianceReject = saturate((cubemapIrradianceAndIrradianceVolumeInvert * cubemapIrradianceAndIrradianceVolumeInvert) / (cubemapIrradianceAndIrradianceVolumeInvert * cubemapIrradianceAndIrradianceVolumeInvert + 0.25));
    float3 irradianceTarget = cubemapIrradianceLuminance * irradianceVolume.rgb * irradianceVolumeLuminanceInverse;

    #if defined(IRRADIANCE_BLENDING_DEFAULT)
        environment.CubemapIrradiance = lerp(environment.CubemapIrradiance, irradianceTarget, irradianceReject) * cubemapIrradianceAndIrradianceVolume;
    #else
        environment.CubemapIrradiance = irradianceVolume.rgb;

        //debugging
        //environment.CubemapIrradiance = environment.CubemapIrradianceRough;
        //environment.CubemapIrradiance = environment.CubemapReflectionRough;
        //environment.CubemapIrradiance = environment.CubemapReflection;
        //environment.CubemapIrradiance = cubemapIrradianceAndIrradianceVolume * View_OneOverPreExposure;
        //environment.CubemapIrradiance = cubemapIrradianceAndIrradianceVolumeInvert * View_OneOverPreExposure;
        //environment.CubemapIrradiance = irradianceReject * View_OneOverPreExposure;
    #endif


    #if defined(FLATTEN_CHARACTER_AMBIENT)
        if (gbufferData.ShadingModelID == SHADINGMODELID_HAIR || 
            gbufferData.ShadingModelID == SHADINGMODELID_EYE || 
            gbufferData.ShadingModelID == SHADINGMODELID_PREINTEGRATED_SKIN)
            environment.CubemapIrradiance = irradianceVolume;
    #endif

    //note: failed experiments at using irradiance volume and trying to pump colors/contrast
    //we need to factor in the exposure level also and at the most just do a "crush blacks" at the bottom of the current exposure level for the GI to get stronger shadows
    //environment.CubemapIrradiance = irradianceVolume.rgb;
    //environment.CubemapIrradiance = irradianceVolume.rgb * ratioB; //pumps contrast but might be too much
    //environment.CubemapIrradiance = irradianceVolume.rgb * (irradianceVolumeLuminance * irradianceVolumeLuminance) * cubemapIrradianceRoughLuminanceInverse * cubemapIrradianceRoughLuminanceInverse * 4;
}

//||||||||||||||||||||||||||||||| OCCLUSION / SPECULAR OCCLUSION |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| OCCLUSION / SPECULAR OCCLUSION |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| OCCLUSION / SPECULAR OCCLUSION |||||||||||||||||||||||||||||||

float ComputeSpecularOcclusion(float ao, float materialAO, float NoV, float roughness, uint shadingModel)
{
    float adjustedAO = (shadingModel == SHADINGMODELID_EYE) ? ao * ao : materialAO;
    float lo = min(ao, adjustedAO);
    float x = 1.0 - lo + adjustedAO * ao;
    float x5 = x * x * x * x * x;
    float y = 1.0 - x5;
    float bentAO = y * y * y * y * (y * (adjustedAO * ao - lo)) + lo;
    float exponent = exp2(-1.0 - 16.0 * roughness);
    return saturate(exp2(log2(bentAO + max(1.0e-5, abs(NoV))) * exponent) - 1.0 + bentAO);
}

//based on Oat and Sander's 2008 technique area/solidAngle of intersection of two cone
float SphericalCapIntersectionSolidArea(float cosC1, float cosC2, float cosB)
{
    float r1 = FastACos(cosC1);
    float r2 = FastACos(cosC2);
    float rd = FastACos(cosB);
    float area = 0.0;

    if (rd <= max(r1, r2) - min(r1, r2))
    {
        // One cap is completely inside the other
        area = MATH_PI_TWO - MATH_PI_TWO * max(cosC1, cosC2);
    }
    else if (rd >= r1 + r2)
    {
        // No intersection exists
        area = 0.0;
    }
    else
    {
        float diff = abs(r1 - r2);
        float den = r1 + r2 - diff;
        float x = 1.0 - saturate((rd - diff) / max(den, 0.0001));
        area = smoothstep(0.0, 1.0, x);
        area *= MATH_PI_TWO - MATH_PI_TWO * max(cosC1, cosC2);
    }

    return area;
}

// ref: Practical Realtime Strategies for Accurate Indirect Occlusion
// http://blog.selfshadow.com/publications/s2016-shading-course/#course_content
// Original Cone-Cone method with cosine weighted assumption (p129 s2016_pbs_activision_occlusion)
float GetSpecularOcclusionFromBentAO(float3 V, float3 bentNormalWS, float3 normalWS, float ambientOcclusion, float roughness)
{
    // Retrieve cone angle
    // Ambient occlusion is cosine weighted, thus use following equation. See slide 129
    float cosAv = sqrt(1.0 - ambientOcclusion);
    roughness = max(roughness, 0.01); // Clamp to 0.01 to avoid edge cases
    float cosAs = exp2((-log(10.0) / log(2.0)) * (roughness * roughness));
    float cosB = dot(bentNormalWS, reflect(-V, normalWS));
    return SphericalCapIntersectionSolidArea(cosAv, cosAs, cosB) / (MATH_PI_TWO * (1.0 - cosAs));
}

// ref: Practical Realtime Strategies for Accurate Indirect Occlusion
// Update ambient occlusion to colored ambient occlusion based on statitics of how light is bouncing in an object and with the albedo of the object
float3 GTAOMultiBounce(float visibility, float3 albedo)
{
    float3 a =  2.0404 * albedo - 0.3324;
    float3 b = -4.7951 * albedo + 0.6417;
    float3 c =  2.7552 * albedo + 0.6903;

    float x = visibility;
    return max(x, ((x * a + b) * x + c) * x);
}

//||||||||||||||||||||||||||||||| MATERIAL SHADING |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| MATERIAL SHADING |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| MATERIAL SHADING |||||||||||||||||||||||||||||||

float3 BuildReflectionDirection(float3 vector_normalDirection, float3 vector_viewDirection, float roughness)
{
    #if defined(CORRECT_REFLECTION_DIRECTION)
        return normalize(reflect(-vector_viewDirection, vector_normalDirection));
    #else
        //not sure what the hell is going on here
        float NoV = dot(vector_normalDirection, vector_viewDirection);
        float3 fixedReflection = SafeNormalize(2.0 * (NoV * vector_normalDirection - vector_viewDirection) + abs(NoV) * vector_normalDirection);
        return SafeNormalize(lerp(fixedReflection, vector_normalDirection, roughness * roughness * roughness));
    #endif
}

float3 BuildCoatNormal(FGBufferData m)
{
    if (m.ShadingModelID != SHADINGMODELID_EYE)
        return m.WorldNormal;

    float3 base = SafeNormalize(lerp(m.WorldNormal, m.SecondaryNormalOrTangent, 1.49));
    float3 packedAxis = SafeNormalize(m.CoatNormalData * 2.0 - 1.0);
    float3 tangent = SafeNormalize(cross(base, packedAxis));
    uint packed = (uint)(m.CustomData.a * 255.0 + 0.5);
    float2 oct = (float2(max(1u, packed & 15u), max(1u, (packed >> 4u) & 15u)) - 8.0) / 7.0;
    float3 local = SafeNormalize(float3(oct, sqrt(max(0.0, 1.0 - dot(oct, oct)))));
    float3 decoded = local.x * packedAxis + local.y * tangent + local.z * base;
    return SafeNormalize(lerp(decoded, m.WorldNormal, m.Coat));
}

void ApproximateFresnelEnergy(float roughness, float NoV, float3 f0, out float3 specularResponse, out float diffuseResponse)
{
    roughness = saturate(roughness);
    NoV = clamp(NoV, 1.0e-4f, 1.0f);
    f0 = saturate(f0);

    float2 gf = View_PreIntegratedGF.SampleLevel(View_SharedBilinearClampedSampler, float2(NoV, roughness), 0.0f).rg;

    //NEW: retro reflection based on roughness to emulate oren-nayar BRDF
    float diffuseRetroReflection = 0.5f * roughness * (1.0f - NoV);

    diffuseResponse = saturate(rcp(1.0f + 0.5f * roughness) + diffuseRetroReflection);
    specularResponse = saturate(f0 * gf.x + gf.y);
}

void EvaluateMaterialLobes(
	FGBufferData m, 
	float3 baseWeight, 
	float NoV,
	out float3 specularLobe, 
	out float3 diffuseLobe)
{
    specularLobe = 0.0;
    diffuseLobe = 0.0;
    float medium = saturate(m.WetnessOrIOR * (View_WetnessIntensity > 0.0 ? 1.0 : 0.0) + (View_FogContextMediumIOR > 1.0 ? 1.0 : 0.0));

    if (m.ShadingModelID == SHADINGMODELID_DEFAULT_LIT || m.ShadingModelID == SHADINGMODELID_SUBSURFACE_PROFILE)
    {
        float3 thinFilm = View_ThinFilmTableTexture.SampleLevel(View_SharedBilinearClampedSampler, float2(NoV, m.CustomData.x), 0.0).rgb - 0.5;
        float film = saturate(m.CustomData.x * 128.0) * 2.5 * saturate(m.CustomData.y);
        float3 spec;
        float diffuse;
        ApproximateFresnelEnergy(m.Roughness, NoV, m.F0, spec, diffuse);
        specularLobe = saturate(spec + thinFilm * m.F0 * film);
        diffuseLobe = diffuse * baseWeight;
    }
    else if (m.ShadingModelID == SHADINGMODELID_PREINTEGRATED_SKIN)
    {
        float f0 = 0.0465206 - 0.0401335 * medium;
        float3 spec;
        float diffuse;
        ApproximateFresnelEnergy(m.Roughness, NoV, f0.xxx, spec, diffuse);
        
        float transmission = 1.0 + NoV * (m.CustomData.x - 1.0);
        specularLobe = spec.x * transmission;
        diffuseLobe = diffuse * baseWeight;
    }
    else if (m.ShadingModelID == SHADINGMODELID_EYE)
    {
        float f0 = 0.0387252 - 0.0348016 * medium;
        float blend = saturate((saturate(1.0 - saturate(m.CoatTransmission) * 2.0) - 0.65) * 2.85714);
        float x = saturate(1.0 - blend);
        float lo = min(m.CoatRoughness, x);
        float z = 1.0 + x * m.CoatRoughness - lo;
        float z5 = z * z * z * z * z;
        float coatVisibility = lerp((1.0 - z5) * (1.0 - z5) * (1.0 - z5) * (1.0 - z5) * ((1.0 - z5) * (x * m.CoatRoughness - lo)) + lo, 1.0, m.Coat);
        float3 spec;
        float diffuse;
        ApproximateFresnelEnergy(m.Roughness, NoV, f0.xxx, spec, diffuse);
        specularLobe = spec.x;
        diffuseLobe = diffuse * baseWeight * coatVisibility;
    }
    else if (m.ShadingModelID == SHADINGMODELID_HAIR)
    {
        float f0 = 0.0465206 - 0.0401335 * medium;
        float3 spec;
        float diffuse;
        ApproximateFresnelEnergy(m.Roughness, NoV, f0.xxx, spec, diffuse);
        specularLobe = spec.x * 0.785398 + baseWeight * (0.15 * diffuse * 0.785398);
        diffuseLobe = baseWeight * (0.85 * diffuse * 0.785398);
    }
    else if (m.ShadingModelID == SHADINGMODELID_CLOTH)
    {
        //NOTE TO SELF: with all of our shading adjustments in other shaders combined
        //this leads to a problem with the cloth shading where now it appears too dark and almost "wet"
        //so to fix this... just have to unfortunately do away with the original game cloth shading in ambient area and do our own semi-inspired one

        /*
        float f0 = 0.0672154 - 0.0528935 * medium;
        float2 gf = View_PreIntegratedGF.SampleLevel(View_SharedBilinearClampedSampler, float2(NoV, m.Roughness), 0.0).rg;
        float a = gf.x * (0.5 + NoV * 0.315) * f0;
        float b = gf.y * (0.5 + NoV * 0.1) * min(f0 * 50.0, 1.0);
        specularLobe = saturate(a + b);
        float diffuse = saturate(1.0 - dot(specularLobe, 1.0 / 3.0));
        diffuseLobe = baseWeight * m.CustomData.x * diffuse;
        specularLobe += baseWeight * (1.0 - m.CustomData.x) * diffuse;
        //*/

        //*
        float3 spec;
        float diffuse;
        ApproximateFresnelEnergy(m.Roughness, NoV, m.F0, spec, diffuse);
        specularLobe += baseWeight * (1.0 - m.CustomData.x) * diffuse;
        //specularLobe += (m.Roughness * (1.0 - NoV));

        diffuseLobe = baseWeight;
        diffuseLobe /= MATH_PI;
        //diffuseLobe *= baseWeight * 1.0f + (m.Roughness * (1.0 - NoV)) * MATH_PI;
        //*/
    }
}

//||||||||||||||||||||||||||||||| SCREEN SPACE GLOBAL ILLUMINATION / NEW AO |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| SCREEN SPACE GLOBAL ILLUMINATION / NEW AO |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| SCREEN SPACE GLOBAL ILLUMINATION / NEW AO |||||||||||||||||||||||||||||||

static const float SSGI_STEP_SCALE = pow(SSGI_RAYMARCHING_WIDTH, 1.0f / (float)SSGI_RAYMARCHING_STEP_COUNT);

//NOTE TO SELF: the original implementation had an adjustable directions count, this means we could have a really high quality/clean SSGI
//however because we are already doing alot of raymarching, I have removed it so we are effectively doing 1 direction per pixel
//using noise we will just temporally accumulate results, this will lead to the best performance
float4 ComputeGTVBGI(
    float2 PixelXY,         // view-local pixel coordinate
    float3 WorldPos,        // world-space surface position
    float3 WorldNormal,     // world-space surface normal
    out float FallbackMask)
{
    // Degenerate/early-return cases require full fallback.
    FallbackMask = 1.0f;

    const uint MASK_ALL = 0xFFFFFFFFu;
    const float inv32 = rcp(32.0f);
    const float InvStepCount = rcp((float)SSGI_RAYMARCHING_STEP_COUNT);
    const float InvSSGISampleCount = rcp((float)SSGI_RAY_COUNT);

    // ---------- view-space quantities for this pixel ----------
    float3 ViewPos = WorldToViewSpace(WorldPos);
    float3 ViewNormal = normalize(WorldDirToViewDir(WorldNormal));

    // View direction = direction FROM surface TO camera, in view space.
    // In UE4 view space the camera is at the origin.
    float3 ViewDir = normalize(-ViewPos);
    float2 ViewSize = View_ViewSizeAndInvSize.xy;

    // Screen-space origin and one-pixel view-ray derivatives.
    float3 RayOrigin = ScreenPixelToViewRayZ1(PixelXY);
    float3 RayDx = ScreenPixelToViewRayZ1(PixelXY + float2(1.0f, 0.0f)) - RayOrigin;
    float3 RayDy = ScreenPixelToViewRayZ1(PixelXY + float2(0.0f, 1.0f)) - RayOrigin;

    // random.x -> shared rotation for the stratified slice pattern
    // random.y -> ray-step jitter
    // random.z -> arc jitter for backface rejection
    #if defined(RANDOM_ANIMATE_NOISE)
        int frameIndex = View_StateFrameIndex;
    #else
        int frameIndex = 0;
    #endif

    //we do not have blue noise in this shader, so use hashed white noise.
    float3 random = float3(GenerateHashedRandomFloat(uint4(PixelXY, frameIndex, 0x68bc21ebu)), GenerateHashedRandomFloat(uint4(PixelXY, frameIndex, 0x02e5be93u)), GenerateHashedRandomFloat(uint4(PixelXY, frameIndex, 0x03e56253u)));

    float3 AccumulatedGI = 0.0f;
    float AccumulatedOcclusion = 0.0f;
    float AccumulatedScreenGICoverage = 0.0f;

    //do not unroll this outer loop: its body already contains the full two-sided depth march, and unrolling several slices can greatly increase shader size.
    [loop]
    for (uint SSGISampleIndex = 0u; SSGISampleIndex < SSGI_RAY_COUNT; ++SSGISampleIndex)
    {
        // The N slices are evenly spaced over [0, PI), because each slice traces
        // both directions. random.x rotates the complete pattern temporally.
        float SliceAngle = ((float)SSGISampleIndex + random.x) * InvSSGISampleCount * MATH_PI;

        float SinSlice;
        float CosSlice;
        sincos(SliceAngle, SinSlice, CosSlice);
        float2 ScreenSliceDir2D = float2(CosSlice, SinSlice);

        float3 RayStep = RayDx * ScreenSliceDir2D.x + RayDy * ScreenSliceDir2D.y;

        // ---------- slice-plane geometry ----------
        float3 SlicePlaneNormal = normalize(cross(RayOrigin, -RayStep));

        float ViewNormalPlaneDot = dot(ViewNormal, SlicePlaneNormal);
        float3 ProjViewNormal = ViewNormal - SlicePlaneNormal * ViewNormalPlaneDot;

        // Both inputs are normalized, so length(projected N)^2 is 1-dot^2.
        float ProjNormalLen2 = max(1.0f - ViewNormalPlaneDot * ViewNormalPlaneDot, 0.0f);

        // This slice cannot produce a trustworthy projected normal. Treat it as
        // unresolved rather than terminating all of the remaining slices.
        if (ProjNormalLen2 < 1.0e-8f)
            continue;

        // cross(P, projected N) is identical to cross(P, N).
        float3 Tangent = cross(SlicePlaneNormal, ViewNormal);

        // Inverse of an orthonormal world-to-view rotation.
        // mul(viewVector, WorldToViewMatrix) applies its transpose.
        float3 WorldSlicePlaneNormal = mul(SlicePlaneNormal, (float3x3)View_TranslatedWorldToView);
        float3 WorldProjViewNormal = mul(ProjViewNormal, (float3x3)View_TranslatedWorldToView);
        float3 WorldTangent = mul(Tangent, (float3x3)View_TranslatedWorldToView);

        float InvProjNormalLen = rsqrt(ProjNormalLen2);
        float cosN = dot(ProjViewNormal, ViewDir) * InvProjNormalLen;
        float sinN = dot(Tangent, ViewDir) * InvProjNormalLen;

        float BaseHor = 0.5f + 0.5f * sinN;

        // Give every slice a different low-discrepancy jitter while retaining
        // exactly random.z/random.y for SSGISampleIndex == 0.
        float ArcJitter = frac(random.z + (float)SSGISampleIndex * 0.569840296f) * inv32;
        float DirectionStepJitter = frac(random.y + (float)SSGISampleIndex * 0.754877666f);

        float3 SliceGI = float3(0.0f, 0.0f, 0.0f);
        uint OcclusionBits = 0u;
        uint ResolvedGIBits = 0u;

        // March the positive and negative halves of this slice.
        [unroll]
        for (int Side = -1; Side <= 1; Side += 2)
        {
            float SideF = (float)Side;
            float2 HorizonDir = ScreenSliceDir2D * SideF;
            float3 HorizonRayStep = RayStep * SideF;
            float StepJitter = frac(DirectionStepJitter + ((Side < 0) ? 0.0f : 0.5f));

            [loop]
            for (uint StepIdx = 0u; StepIdx < SSGI_RAYMARCHING_STEP_COUNT; ++StepIdx)
            {
                // One jittered sample inside each normalized interval.
                float u = ((float)StepIdx + StepJitter) * InvStepCount;

                // Quadratic distance distribution.
                float SampleT = 1.0f + (SSGI_RAYMARCHING_WIDTH - 1.0f) * (u * u);
                float2 SamplePixel = PixelXY + HorizonDir * SampleT;
                float3 SampleViewRay = RayOrigin + HorizonRayStep * SampleT;

                // Stop when this half-ray leaves the active view.
                if (any(SamplePixel < 0.0f) || any(SamplePixel >= ViewSize))
                    break;

                uint2 SampleBufferPixel = ScreenPixelToBufferPixel(SamplePixel);

                // ---------- sample depth and compute view-space positions ----------
                float DeviceZ = SceneDepthTexture.Load(int3(SampleBufferPixel, 0)).r;
                float SampledDepth = ConvertFromDeviceZ(DeviceZ);

                float3 FrontDelta = SampleViewRay * SampledDepth - ViewPos;
                float3 BackDelta = FrontDelta + SampleViewRay * SSGI_THICKNESS;

                // Project onto the view direction to get cos(horizon angle).
                float2 HorCos = float2(dot(FrontDelta, ViewDir) * rsqrt(max(dot(FrontDelta, FrontDelta), 1.0e-8f)), dot(BackDelta, ViewDir) * rsqrt(max(dot(BackDelta, BackDelta), 1.0e-8f)));

                // Flip endpoint order for the negative half of the slice.
                HorCos = (Side >= 0) ? HorCos.xy : HorCos.yx;

                // Map the horizon cosines into a [0,1] arc on the bitmask circle.
                float d05 = SideF * 0.5f;
                float2 Hor01 = BaseHor + d05 - d05 * HorCos;
                Hor01 = saturate(Hor01 + ArcJitter);

                // Convert the arc to a 32-bit angular occupancy mask.
                uint2 HorInt = (uint2)floor(Hor01 * 32.0f);
                uint mX = (HorInt.x < 32u) ? (MASK_ALL << HorInt.x) : 0u;
                uint mY = (HorInt.y != 0u) ? (MASK_ALL >> (32u - HorInt.y)) : 0u;
                uint SampleOccBits = mX & mY;

                #if defined(SSGI_BOUNCE_LIGHT)
                    // GI contribution from angular sectors first resolved here.
                    uint NewlyVisibleBits = SampleOccBits & (~OcclusionBits);

                    if (NewlyVisibleBits != 0u)
                    {
                        float3 SampledWorldNormal = normalize(GBufferATexture.Load(int3(SampleBufferPixel, 0)).rgb * 2.0f - 1.0f);
                        float SampleNormalPlaneDot = dot(SampledWorldNormal, WorldSlicePlaneNormal);
                        float ProjSampledLen2 = max(1.0f - SampleNormalPlaneDot * SampleNormalPlaneDot, 0.0f);

                        // ---------- backface rejection ----------
                        if (ProjSampledLen2 > 1.0e-8f)
                        {
                            float InvProjSampledLen = rsqrt(ProjSampledLen2);
                            float n = InvProjNormalLen * InvProjSampledLen;

                            float sinPhi = dot(WorldProjViewNormal, SampledWorldNormal) * n;
                            float cosPhi = dot(WorldTangent, SampledWorldNormal) * n;

                            bool FlipT = (cosPhi < 0.0f);

                            if (!FlipT)
                                sinPhi = -sinPhi;

                            bool c = (sinPhi > sinN);
                            float m0 = c ? 1.0f : 0.0f;
                            float m1 = c ? -0.5f : 0.5f;
                            float Hor01Single = m0 + m1 * (cosN * abs(cosPhi) + sinN * sinPhi) + 0.5f * sinN;

                            Hor01Single = saturate(Hor01Single + ArcJitter);
                            uint HorIntSingle = (uint)floor(Hor01Single * 32.0f);
                            uint VisBitsN = (HorIntSingle < 32u) ? (MASK_ALL << HorIntSingle) : 0u;

                            if (!FlipT)
                                VisBitsN = ~VisBitsN;

                            NewlyVisibleBits &= VisBitsN;
                        }

                        if (NewlyVisibleBits != 0u)
                        {
                            // These angular sectors received valid radiance.
                            ResolvedGIBits |= NewlyVisibleBits;

                            float Visibility = (float)countbits(NewlyVisibleBits) * inv32;
                            float3 SampledRadiance = OutTextureColor[SampleBufferPixel].rgb;
                            SliceGI += SampledRadiance * Visibility;
                        }
                    }
                #endif

                OcclusionBits |= SampleOccBits;

                if (OcclusionBits == MASK_ALL)
                    break;
            }

            if (OcclusionBits == MASK_ALL)
                break;
        }

        float SliceOcclusion = (float)countbits(OcclusionBits) * inv32;
        float SliceScreenGICoverage = (float)countbits(ResolvedGIBits) * inv32;

        AccumulatedGI += SliceGI;
        AccumulatedOcclusion += SliceOcclusion;
        AccumulatedScreenGICoverage += SliceScreenGICoverage;
    }

    // Integrate the independently evaluated slices.
    float3 GI = AccumulatedGI * InvSSGISampleCount;
    float Occlusion = AccumulatedOcclusion * InvSSGISampleCount;
    float ScreenGICoverage = AccumulatedScreenGICoverage * InvSSGISampleCount;

    // 0 = screen-space GI fully resolved across all sampled slices.
    // 1 = no valid screen-space GI; use full fallback.
    FallbackMask = 1.0f - ScreenGICoverage;

    return float4(GI * View_OneOverPreExposure, 1.0f - Occlusion);
}


//||||||||||||||||||||||||||||||| SPECULAR HIGHLIGHT |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| SPECULAR HIGHLIGHT |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| SPECULAR HIGHLIGHT |||||||||||||||||||||||||||||||

float DistributionGGX(float NoH, float roughness)
{
    float alpha  = roughness * roughness;
    float alpha2 = alpha * alpha;

    float denominator = NoH * NoH * (alpha2 - 1.0f) + 1.0f;

    return alpha2 / max(MATH_PI * denominator * denominator, 1e-6f);
}

float GeometrySchlickGGX(float NoX, float roughness)
{
    float k = roughness + 1.0f;
    k = (k * k) * 0.125f;

    return NoX / max(NoX * (1.0f - k) + k, 1e-6f);
}

float3 CalculateSpecularHighlight(float3 normal, float3 directionToCamera, float roughness)
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

    float D = DistributionGGX(NoH, roughness);
    float G = GeometrySchlickGGX(NoV, roughness) * GeometrySchlickGGX(NoL, roughness);

    float3 specularBRDF = (D * G) / max(4.0f * NoV * NoL, 1e-6f);

    //return specularBRDF * NoL;
	return specularBRDF * NoL * MATH_PI;
}

//||||||||||||||||||||||||||||||| MAIN |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| MAIN |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| MAIN |||||||||||||||||||||||||||||||

//||||||||||||||||||||||||||||||| VIGNETTE |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| VIGNETTE |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| VIGNETTE |||||||||||||||||||||||||||||||

float Vignette(float2 uv, float radius, float smoothness) 
{
	float diff = radius - distance(uv, float2(0.5, 0.5));
	return smoothstep(-smoothness, smoothness, diff);
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    bool pixelInView = all(dispatchThreadId.xy < (uint2)View_ViewSizeAndInvSize.xy);

    float2 viewPixelPosition = (float2)dispatchThreadId.xy + 0.5;
    float2 vector_svPosition = viewPixelPosition + View_ViewRectMin.xy;
    uint2 vector_uvInt = (uint2)vector_svPosition;
    float2 vector_uvNormalized = vector_uvInt * View_ViewSizeAndInvSize.zw;

    FGBufferData gbufferData = (FGBufferData)0;

    if (pixelInView)
        gbufferData = DecodeGBuffer(vector_uvInt);

    bool supportedShadingModel = pixelInView &&
        (gbufferData.ShadingModelID == SHADINGMODELID_DEFAULT_LIT ||
         gbufferData.ShadingModelID == SHADINGMODELID_PREINTEGRATED_SKIN ||
         gbufferData.ShadingModelID == SHADINGMODELID_SUBSURFACE_PROFILE ||
         gbufferData.ShadingModelID == SHADINGMODELID_HAIR ||
         gbufferData.ShadingModelID == SHADINGMODELID_CLOTH ||
         gbufferData.ShadingModelID == SHADINGMODELID_EYE);

    bool hasSceneDepth = pixelInView && gbufferData.DeviceDepth > 0.0f;
    bool shadePixel = supportedShadingModel && hasSceneDepth;

    float3 worldPosition = 0.0f;

    if (shadePixel)
        worldPosition = ReconstructWorldPosition(vector_svPosition, gbufferData.DeviceDepth);

	float ssgiMask = 1.0f;

	#if defined(SSGI_AMBIENT_OCCLUSION) || defined(SSGI_BOUNCE_LIGHT)
        float4 ssgi = float4(0, 0, 0, 1);

        bool checkerboardTest = true;

        #if defined(SSGI_CHECKERBOARD)
            uint frameParity = (uint)View_StateFrameIndex & 1u;
            checkerboardTest = ((dispatchThreadId.x + dispatchThreadId.y + frameParity) & 1u) == 0u;
        #endif

        if (shadePixel && checkerboardTest)
        {
            float ssgiNormalBias = SSGI_NORMAL_BIAS;

            if (gbufferData.ShadingModelID == SHADINGMODELID_HAIR)
                ssgiNormalBias = SSGI_NORMAL_BIAS_HAIR;

            float3 vector_rayOriginSSGI = worldPosition + gbufferData.WorldNormal * ssgiNormalBias;

            ssgi = ComputeGTVBGI(
                viewPixelPosition,
                vector_rayOriginSSGI,
                gbufferData.WorldNormal,
                ssgiMask);
        }

        #if defined(SSGI_CHECKERBOARD) && defined(SSGI_CHECKERBOARD_QUAD_RECONSTRUCTION)
            float computedSample = (shadePixel && checkerboardTest) ? 1.0f : 0.0f;
            float4 horizontalSsgi = QuadReadAcrossX(ssgi);
            float4 verticalSsgi = QuadReadAcrossY(ssgi);
            float horizontalMask = QuadReadAcrossX(ssgiMask);
            float verticalMask = QuadReadAcrossY(ssgiMask);
            float horizontalValid = QuadReadAcrossX(computedSample);
            float verticalValid = QuadReadAcrossY(computedSample);

            if (shadePixel && !checkerboardTest)
            {
                float reconstructionWeight = horizontalValid + verticalValid;

                if (reconstructionWeight > 0.0f)
                {
                    float inverseWeight = rcp(reconstructionWeight);
                    ssgi = (horizontalSsgi * horizontalValid + verticalSsgi * verticalValid) * inverseWeight;
                    ssgiMask = (horizontalMask * horizontalValid + verticalMask * verticalValid) * inverseWeight;
                }
            }
        #endif

	#endif

    //these returns occur after the quad operations so helper lanes remain defined during checkerboard reconstruction.
    if (!pixelInView || !supportedShadingModel)
        return;

    if (!hasSceneDepth)
    {
        OutTextureColor[vector_uvInt] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    float3 vector_viewDirection = SafeNormalize(View_WorldCameraOrigin - worldPosition);
    float3 vector_reflectionDirection = BuildReflectionDirection(gbufferData.WorldNormal, vector_viewDirection, gbufferData.Roughness);
    float3 coatNormal = BuildCoatNormal(gbufferData);
    float NoV = max(1.0e-4, abs(dot(gbufferData.WorldNormal, vector_viewDirection)));

    float roughMip = max(0.0, min((7.0 - saturate(gbufferData.Roughness) * 3.0) * saturate(gbufferData.Roughness), 4.0));
    //float roughMip = 0;

    float axisBlend = (gbufferData.ShadingModelID == SHADINGMODELID_DEFAULT_LIT) ? gbufferData.GBufferD.b : 0.0;

    float3 baseWeight = (
		gbufferData.ShadingModelID == SHADINGMODELID_PREINTEGRATED_SKIN || 
		gbufferData.ShadingModelID == SHADINGMODELID_SUBSURFACE_PROFILE || 
		gbufferData.ShadingModelID == SHADINGMODELID_EYE) ? (1.0 - gbufferData.Metallic).xxx : gbufferData.DiffuseColor;
    
    //baseWeight = 1.0f;

    float3 specularLobe;
	float3 diffuseLobe;
    EvaluateMaterialLobes(gbufferData, baseWeight, NoV, specularLobe, diffuseLobe);

	#if defined(DISABLE_SSR)
		float4 ssr = float4(0, 0, 0, 0);
	#else
		float ssrMaskForRoughness = (gbufferData.ShadingModelID == SHADINGMODELID_DEFAULT_LIT) ? gbufferData.GBufferD.b : 0.0;
		float4 ssr = ScreenSpaceReflectionsTexture.Load(int3(vector_uvInt, 0));
		ssr.rgb *= View_OneOverPreExposure;

        ssr.a *= SSR_CONTRIBUTION_MULTIPLIER;

        //make sure no SSR on eyes
        if (gbufferData.ShadingModelID == SHADINGMODELID_EYE)
            ssr.a = 0.0f; 
	#endif

    float4 radianceVolume = EnvironmentIrradianceATexture.Load(int3(vector_uvInt, 0));
    float4 irradianceVolume = EnvironmentIrradianceBTexture.Load(int3(vector_uvInt, 0));
    radianceVolume.rgb *= View_OneOverPreExposure;
    irradianceVolume.rgb *= View_OneOverPreExposure;

    float ambientOcclusion = 1.0f;

	//apply screen space ao
	#if defined(SSGI_AMBIENT_OCCLUSION)
		ambientOcclusion *= saturate(pow(ssgi.a, SSAO_POWER) * SSAO_BRIGHTNESS);
	#else //NOTE: original game SSAO is quite a bit weaker than the SSGI AO
		//manual adjustment to get the original game SSAO to match the strength of the SSGI AO

        //strengthen AO for all but eyes, eyes become too dark with the original game SSAO
        if (gbufferData.ShadingModelID != SHADINGMODELID_EYE || gbufferData.ShadingModelID != SHADINGMODELID_HAIR)
		    gbufferData.ScreenAO = saturate(pow(gbufferData.ScreenAO, 2) * 1.0);

        if (gbufferData.ShadingModelID == SHADINGMODELID_EYE)
		    gbufferData.ScreenAO = lerp(gbufferData.ScreenAO, 1.0f, 0.5f);

		ambientOcclusion *= saturate(pow(gbufferData.ScreenAO, SSAO_POWER) * SSAO_BRIGHTNESS);
	#endif

	//apply material AO
    if (gbufferData.ShadingModelID == SHADINGMODELID_EYE)
        ambientOcclusion *= 1.0f - gbufferData.GBufferC.a; 
    else
        ambientOcclusion *= gbufferData.GBufferC.a;

    //for SSGI AO, the AO is quite strong on characters, but problem is that this can lead to an overdarkening
    //so knock the SSGI ao down a bit for characters
    #if defined(SSGI_AMBIENT_OCCLUSION)
        if (gbufferData.ShadingModelID == SHADINGMODELID_HAIR || 
            gbufferData.ShadingModelID == SHADINGMODELID_EYE || 
            gbufferData.ShadingModelID == SHADINGMODELID_PREINTEGRATED_SKIN)
            ambientOcclusion = lerp(ambientOcclusion, 1.0f, 0.1f);
    #else
        //intrestingly we have the opposite issue with the regular game AO... its not strong enough on faces or characters at all
        if (gbufferData.ShadingModelID == SHADINGMODELID_HAIR || 
            gbufferData.ShadingModelID == SHADINGMODELID_EYE ||
            gbufferData.ShadingModelID == SHADINGMODELID_PREINTEGRATED_SKIN)
            ambientOcclusion = pow(ambientOcclusion, 1.75);
    #endif

    //NOTE TO SELF: this is intended to be used with bent normals... but the game doesn't have nor use bent normals for any kind of object shading (except maybe for hair?)
    //but with that said, this can still be used as a pretty solid form of specular occlusion even with regular normals.
	//it factors in roughness and view direction to modulate the occlusion term and its physically plausible... so lets use it!
    float specularOcclusion = GetSpecularOcclusionFromBentAO(vector_viewDirection, gbufferData.WorldNormal, gbufferData.WorldNormal, ambientOcclusion, gbufferData.Roughness);

    //SSR reflections are the most accurate that we have access too, because of that they should recieve essentially no specular occlusion
    //the most problematic reflections are the rougher cubemap/irradiance ones, because they are the most imprecise and blobby
    //the consequence of that lack of precision/quality leads to alot of "glowing" apperances and light/specular leaking
    //so to help mitigate that we will try to use all the tools we can to mitigate those leaks and problems by apply specular occlusion ONLY to the rough reflections
    //again the SSR is the most accurate, so it should remain untouched
    //specularOcclusion = lerp(specularOcclusion, 1.0f, ssr.a);
    specularOcclusion = lerp(specularOcclusion, 1.0f, saturate(ssr.a) * gbufferData.GBufferC.a);

    ProbeSelection selection = FindProbeBound(worldPosition);

    // Reproduce the divergent-wave descriptor serialization from the DXIL.
    int batchEnvironment;
    int batchBound;
    [allow_uav_condition]
    [loop]
    for (;;)
    {
        batchEnvironment = WaveReadLaneFirst(selection.Environment);
        batchBound = WaveReadLaneFirst(selection.Bound);

        if (selection.Environment == batchEnvironment && selection.Bound == batchBound)
            break;
    }

    EnvironmentSamples environment;

    if (((uint)batchBound & 0x0fffffffu) == 0x0fffffffu)
        environment = SampleFallback(vector_reflectionDirection, coatNormal, roughMip, axisBlend);
    else
        environment = SampleProbeEnvironment(selection, vector_reflectionDirection, coatNormal, roughMip, axisBlend);


    float3 directLightContribution = SampleSceneRadiance(viewPixelPosition);

    if (radianceVolume.a != 0.0 && irradianceVolume.a != 0.0)
        CombineIrradianceVolumesAndReflections(environment, radianceVolume, irradianceVolume, gbufferData, directLightContribution);

    float environmentModifier = ((gbufferData.PackedFlags & 16u) != 0u) ? View_LightModifierEnvironmentLight : 1.0;
    float3 globalIlluminationRadiance = environmentModifier * environment.CubemapReflection;
    float3 globalIlluminationIrradiance = environmentModifier * environment.CubemapIrradiance;

	#if !defined(DISABLE_SSR)
		globalIlluminationRadiance = exp2(lerp(log2(max(globalIlluminationRadiance, 1.0e-9)), log2(max(ssr.rgb, 1.0e-9)), saturate(ssr.a)));
	#endif

    float3 ambiguousA = View_OneOverPreExposure * AmbiguousLightATexture.Load(int3(vector_uvInt, 0)).rgb;
    float3 ambiguousB = View_OneOverPreExposure * AmbiguousLightBTexture.Load(int3(vector_uvInt, 0)).rgb;

    float3 specular = ambiguousA + globalIlluminationRadiance;
    float3 diffuse = ambiguousB + globalIlluminationIrradiance;

    #if defined(CAMERA_AMBIENT_LIGHT)
        float distanceToCamera = distance(View_WorldCameraOrigin, worldPosition);
        float falloff = rcp(distanceToCamera);
        falloff *= falloff; //inverse square
        float cameraAmbientLight = falloff;
        cameraAmbientLight = clamp(cameraAmbientLight * CAMERA_AMBIENT_LIGHT_RANGE, 0.0f, 1.0f) * CAMERA_AMBIENT_LIGHT_BRIGHTNESS;
        cameraAmbientLight *= saturate(dot(gbufferData.WorldNormal, vector_viewDirection));
        cameraAmbientLight *= Vignette(vector_uvNormalized, 0.1f, 0.45);

        diffuse += cameraAmbientLight * globalIlluminationIrradiance;
        specular += globalIlluminationIrradiance * cameraAmbientLight * CalculateSpecularHighlight(gbufferData.WorldNormal, vector_viewDirection, gbufferData.Roughness) * CAMERA_AMBIENT_LIGHT_SPECULAR_BOOST;
    #endif

    diffuse *= GTAOMultiBounce(ambientOcclusion, gbufferData.BaseColor);
    specular *= GTAOMultiBounce(specularOcclusion, gbufferData.BaseColor);
    //diffuse *= GTAOMultiBounce(ambientOcclusion, gbufferData.EnergyColor);
    //specular *= GTAOMultiBounce(specularOcclusion, gbufferData.EnergyColor);

    //NOTE TO SELF: apply SSGI AFTER applying AO!
	//the AO needs to natrually bring down the original GI term
	//that way we can simply add in the SSGI ontop and it will fill in the shadows correctly.
    //if you apply SSGI before then the AO will arbitrarily darken the SSGI contribution incorrectly
    //bounce light is additive! its already factoring in occlusion!
	#if defined(SSGI_BOUNCE_LIGHT)
		float ssgiBoost = MATH_PI; //arbitrary boost to the GI

		if (gbufferData.ShadingModelID == SHADINGMODELID_HAIR || 
			gbufferData.ShadingModelID == SHADINGMODELID_EYE || 
			gbufferData.ShadingModelID == SHADINGMODELID_PREINTEGRATED_SKIN)
			ssgiBoost = 1.0f; //reduce SSGI for these shading models (characters specifically, looks too weird)

		//diffuse = 0; //debug to see SSGI only
		diffuse *= ssgiMask;
		diffuse += max(0.0002, ssgi.rgb * ssgiBoost);
	#endif

    #if defined(SPECULAR_EYE_HIGHLIGHT)
        //eyes can look a little dead, so just an artistic touch is to do a specular highlight that always lives in the eye
        if (gbufferData.ShadingModelID == SHADINGMODELID_EYE)
            specular += CalculateSpecularHighlight(gbufferData.WorldNormal, vector_viewDirection, gbufferData.Roughness) * specular * SPECULAR_EYE_HIGHLIGHT_BOOST;
    #endif

	//apply specular/diffuse BRDFs
    specular *= specularLobe;
    diffuse *= diffuseLobe;

    #if defined(DISABLE_IRRADIANCE)
        diffuse = 0.0f;
    #endif

    #if defined(DISABLE_RADIANCE)
        specular = 0.0f;
    #endif

    float3 result = specular + diffuse;

    float alpha = LuminanceRec709(diffuse);
	float4 finalColor = View_PreExposure * float4(result, alpha);

    //prevents NAN firefly madness
    finalColor = max(0.0002, finalColor);

    //artistic adjustment users can do for those complaining about darkness... (little do they know most of them are too used to seeing the games inaccurate bright light leaks in shadows)
    //this is also an attempt at giving users a potential solution for being able to configure image brightness if they are in HDR since there is no post process HDR variant (at the time of writing)
    finalColor *= AMBIENT_BRIGHTNESS;

    OutTextureColor[vector_uvInt] += finalColor;
	//OutTextureColor[vector_uvInt] = finalColor;
    //OutTextureColor[vector_uvInt] = saturate(ssr.a) * View_OneOverPreExposure;

    //artistic adjusmtent
    //this is also an attempt at giving users a potential solution for being able to configure image brightness if they are in HDR since there is no post process HDR variant (at the time of writing)
    OutTextureColor[vector_uvInt] *= FINAL_BRIGHTNESS;

    //debug
    //OutTextureColor[vector_uvInt] = float4(ambientOcclusion, ambientOcclusion, ambientOcclusion, 1);
    //OutTextureColor[vector_uvInt] = float4(specularOcclusion, specularOcclusion, specularOcclusion, 1);
	//OutTextureColor[vector_uvInt] = float4(ssgi.a, ssgi.a, ssgi.a, 1);
	//OutTextureColor[vector_uvInt] = float4(ssgiMask, ssgiMask, ssgiMask, 1);
    //OutTextureColor[vector_uvInt] = float4(ssr.rgb, 1);
	//OutTextureColor[vector_uvInt] = float4(gbufferData.ScreenAO, gbufferData.ScreenAO, gbufferData.ScreenAO, 1);
    //OutTextureColor[vector_uvInt] = float4(diffuseLobe, 1.0f);
}
