//LibraryRandom.hlsl

#ifndef LIBRARY_RANDOM
#define LIBRARY_RANDOM

//||||||||||||||||||||||||||||||| INTERLEAVED GRADIENT NOISE |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| INTERLEAVED GRADIENT NOISE |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| INTERLEAVED GRADIENT NOISE |||||||||||||||||||||||||||||||

//more deteriministic than blue noise and generally cleaner
//rebirth has a ton of damn aliasing already
float InterleavedGradientNoise(float2 pixCoord, int frameCount)
{
	const float3 magic = float3(0.06711056f, 0.00583715f, 52.9829189f);
	const float2 frameMagicScale = float2(2.083f, 4.867f);
    pixCoord += frameCount * frameMagicScale;
    
	return frac(magic.z * frac(dot(pixCoord, magic.xy)));
}

//more deteriministic than blue noise and generally cleaner
//rebirth has a ton of damn aliasing already
float InterleavedGradientNoise(float2 pixCoord)
{
	const float3 magic = float3(0.06711056f, 0.00583715f, 52.9829189f);
	const float2 frameMagicScale = float2(2.083f, 4.867f);

	return frac(magic.z * frac(dot(pixCoord, magic.xy)));
}

//||||||||||||||||||||||||||||||| WHITE NOISE |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| WHITE NOISE |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| WHITE NOISE |||||||||||||||||||||||||||||||

//reference - https://github.com/needle-mirror/com.unity.render-pipelines.core/blob/master/ShaderLibrary/Random.hlsl

uint JenkinsHash(uint x)
{
    x += (x << 10u);
    x ^= (x >>  6u);
    x += (x <<  3u);
    x ^= (x >> 11u);
    x += (x << 15u);
    return x;
}

uint JenkinsHash(uint2 v)
{
    return JenkinsHash(v.x ^ JenkinsHash(v.y));
}

uint JenkinsHash(uint3 v)
{
    return JenkinsHash(v.x ^ JenkinsHash(v.yz));
}

uint JenkinsHash(uint4 v)
{
    return JenkinsHash(v.x ^ JenkinsHash(v.yzw));
}

float ConstructFloat(int m) 
{
    const int ieeeMantissa = 0x007FFFFF; // Binary FP32 mantissa bitmask
    const int ieeeOne      = 0x3F800000; // 1.0 in FP32 IEEE

    m &= ieeeMantissa;                   // Keep only mantissa bits (fractional part)
    m |= ieeeOne;                        // Add fractional part to 1.0

    float  f = asfloat(m);               // Range [1, 2)
    return f - 1;                        // Range [0, 1)
}

float ConstructFloat(uint m)
{
    return ConstructFloat(asint(m));
}

float GenerateHashedRandomFloat(uint x)
{
    return ConstructFloat(JenkinsHash(x));
}

float GenerateHashedRandomFloat(uint2 v)
{
    return ConstructFloat(JenkinsHash(v));
}

float GenerateHashedRandomFloat(uint3 v)
{
    return ConstructFloat(JenkinsHash(v));
}

float GenerateHashedRandomFloat(uint4 v)
{
    return ConstructFloat(JenkinsHash(v));
}

#endif //LIBRARY_RANDOM