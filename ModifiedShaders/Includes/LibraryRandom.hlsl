//LibraryRandom.hlsl

#ifndef LIBRARY_RANDOM
//[NO CONFIG]
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

//||||||||||||||||||||||||||||||| HILBERT R2 NOISE |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| HILBERT R2 NOISE |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| HILBERT R2 NOISE |||||||||||||||||||||||||||||||

//reference - https://github.com/GameTechDev/XeGTAO (MIT), based on https://www.shadertoy.com/view/3tB3z3
//position of a pixel along a hilbert curve that covers a 64x64 tile
uint HilbertIndex(uint2 pixCoord)
{
	uint index = 0u;

	for (uint curLevel = 32u; curLevel > 0u; curLevel /= 2u)
	{
		uint regionX = (pixCoord.x & curLevel) > 0u;
		uint regionY = (pixCoord.y & curLevel) > 0u;
		index += curLevel * curLevel * ((3u * regionX) ^ regionY);

		if (regionY == 0u)
		{
			if (regionX == 1u)
				pixCoord = 63u - pixCoord;

			pixCoord = pixCoord.yx;
		}
	}

	return index;
}

//walking the hilbert curve through the R2 sequence gives neighboring pixels evenly spread values (a cheap stand-in for a blue noise texture)
//to animate it, add a different irrational step per channel each frame (golden ratio, silver ratio...) and frac
//reference - http://extremelearning.com.au/unreasonable-effectiveness-of-quasirandom-sequences/
float2 HilbertR2Noise(uint2 pixCoord)
{
	return frac(0.5f + HilbertIndex(pixCoord % 64u) * float2(0.75487766624669276005f, 0.5698402909980532659114f));
}

//||||||||||||||||||||||||||||||| ANIMATED INTERLEAVED GRADIENT NOISE |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| ANIMATED INTERLEAVED GRADIENT NOISE |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| ANIMATED INTERLEAVED GRADIENT NOISE |||||||||||||||||||||||||||||||

//moving the pattern 5.588238 pixels every frame also makes each pixel step through evenly spread values over time,
//so TAA/DLSS blends it away much faster than white noise (or the frameMagicScale variant above)
//reference - https://blog.demofox.org/2022/01/01/interleaved-gradient-noise-a-different-kind-of-low-discrepancy-sequence/
float InterleavedGradientNoiseAnimated(float2 pixCoord, int frameCount)
{
	const float3 magic = float3(0.06711056f, 0.00583715f, 52.9829189f);
	pixCoord += 5.588238f * (float)(frameCount & 63);

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