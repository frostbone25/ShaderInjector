//LibraryMicroShadows.hlsl

#ifndef LIBRARY_MICRO_SHADOWS
#define LIBRARY_MICRO_SHADOWS

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

#endif //LIBRARY_MICRO_SHADOWS