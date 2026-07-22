//LibraryMath.hlsl

#ifndef LIBRARY_MATH
#define LIBRARY_MATH

static const float MATH_FLT_EPSILON   = 1.192092896e-07; //smallest positive number
static const float MATH_FLT_MIN       = 1.175494351e-38; //minimum representable positive floating-point number
static const float MATH_FLT_MAX       = 3.402823466e+38; //maximum representable floating-point number
static const float MATH_PI            = 3.14159265359;
static const float MATH_PI_TWO        = 6.28318530717;
static const float MATH_PI_INV        = 0.31830988618;
static const float MATH_PI_INV2       = 0.1591549431;
static const float MATH_PI_INV4       = 0.0795775;
static const float MATH_MIN_ROUGHNESS = 0.04;

float Pow2(float v) { return v * v; }
float Pow4(float v) { float v2 = v * v; return v2 * v2; }
float Pow5(float v) { return Pow4(v) * v; }

float  PositivePow(float base, float power)   { return pow(abs(base), power); }
float3 PositivePow(float3 base, float3 power) { return pow(abs(base), power); }

float3 SafeNormalize(float3 v)
{
    return v * rsqrt(dot(v, v));
}

float3 DecodeUnitVector(float3 packedVector)
{
    return SafeNormalize(packedVector * 2.0 - 1.0);
}

float FastACosPos(float inX)
{
    float x = abs(inX);
    float res = (0.0468878 * x + -0.203471) * x + 1.570796; // p(x)
    res *= sqrt(1.0 - x);

    return res;
}

float FastACos(float inX)
{
    float res = FastACosPos(inX);
    return (inX >= 0) ? res : MATH_PI - res; // Undo range reduction
}

uint BitFieldInsert(uint mask, uint src, uint dst)
{
    return (src & mask) | (dst & ~mask);
}

float CopySign(float x, float s, bool ignoreNegZero = true)
{
    if (ignoreNegZero)
    {
        return (s >= 0) ? abs(x) : -abs(x);
    }
    else
    {
        uint negZero = 0x80000000u;
        uint signBit = negZero & asuint(s);
        return asfloat(BitFieldInsert(negZero, signBit, asuint(x)));
    }
}

float FastSign(float s, bool ignoreNegZero = true)
{
    return CopySign(1.0, s, ignoreNegZero);
}

float3x3 GetLocalFrame(float3 localZ)
{
    float x = localZ.x;
    float y = localZ.y;
    float z = localZ.z;
    float sz = FastSign(z);
    float a = 1 / (sz + z);
    float ya = y * a;
    float b = x * ya;
    float c = x * sz;

    float3 localX = float3(c * x * a - 1, sz * b, c);
    float3 localY = float3(b, y * ya - sz, y);

    return float3x3(localX, localY, localZ);
}

#endif //LIBRARY_MATH