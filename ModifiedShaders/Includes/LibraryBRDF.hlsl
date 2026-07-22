//LibraryBRDF.hlsl

#include "LibraryMath.hlsl"

#ifndef LIBRARY_BRDF
#define LIBRARY_BRDF

//reference - https://github.com/chendi-YU/UnrealEngine/blob/master/Engine/Shaders/BRDF.usf

float3 Diffuse_Lambert(float3 DiffuseColor)
{
	return DiffuseColor * MATH_PI_INV;
}

// [Burley 2012, "Physically-Based Shading at Disney"]
float3 Diffuse_Burley(float3 DiffuseColor, float Roughness, float NoV, float NoL, float VoH)
{
	float FD90 = 0.5 + 2 * VoH * VoH * Roughness;
	float FdV = 1 + (FD90 - 1) * Pow5(1 - NoV);
	float FdL = 1 + (FD90 - 1) * Pow5(1 - NoL);
	return DiffuseColor * (MATH_PI_INV * FdV * FdL);
}

// [Gotanda 2012, "Beyond a Simple Physically Based Blinn-Phong Model in Real-Time"]
float3 Diffuse_OrenNayar(float3 DiffuseColor, float Roughness, float NoV, float NoL, float VoH)
{
	float a = Roughness * Roughness;
	float s = a; // / ( 1.29 + 0.5 * a );
	float s2 = s * s;
	float VoL = 2 * VoH * VoH - 1; // double angle identity
	float Cosri = VoL - NoV * NoL;
	float C1 = 1 - 0.5 * s2 / (s2 + 0.33);
	float C2 = 0.45 * s2 / (s2 + 0.09) * Cosri * (Cosri >= 0 ? rcp(max(NoL, NoV)) : 1);
	return DiffuseColor * MATH_PI_INV * (C1 + C2) * (1 + Roughness * 0.5);
}

//||||||||||||||||||||||||||||||| CUSTOM BRDF |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| CUSTOM BRDF |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| CUSTOM BRDF |||||||||||||||||||||||||||||||

//const static float constant1_FON = 0.5f - 2.0f / (3.0f * MATH_PI);
//const static float constant2_FON = 2.0f / 3.0f - 28.0f / (15.0f * MATH_PI);
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
    return diffuse * (scalar * NdotL * MATH_PI_INV);
}

#endif //LIBRARY_BRDF