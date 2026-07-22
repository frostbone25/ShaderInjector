//LibraryColor.hlsl

#ifndef LIBRARY_COLOR
#define LIBRARY_COLOR

//reference - https://github.com/chendi-YU/UnrealEngine/blob/master/Engine/Shaders/Common.usf
//this is old but fairly common... though in our case the other rec709 is more appropriate
float Luminance(float3 linearColor)
{
    return dot(linearColor, float3(0.3f, 0.59f, 0.11f));
}

//should be using this one
float LuminanceRec709(float3 linearColor)
{
    return dot(linearColor, float3(0.212639f, 0.715169f, 0.072192f));
}

float3 Rec709ToRec2020(float3 rgb)
{
    const float3x3 M = float3x3(
        0.6274040, 0.3292820, 0.0433136,
        0.0690970, 0.9195400, 0.0113612,
        0.0163916, 0.0880132, 0.8955950
    );

    return mul(M, rgb);
}

float3 Rec2020ToRec709(float3 rgb)
{
    const float3x3 M = float3x3(
         1.6604910, -0.5876411, -0.0728499,
        -0.1245505,  1.1328999, -0.0083494,
        -0.0181508, -0.1005789,  1.1187297
    );

    return mul(M, rgb);
}

//Gamma20
float Gamma20ToLinear (float  c) { return c * c; }
float3 Gamma20ToLinear(float3 c) { return c.rgb * c.rgb; }
float4 Gamma20ToLinear(float4 c) { return float4(Gamma20ToLinear(c.rgb), c.a); }

float LinearToGamma20 (float  c) { return sqrt(c); }
float3 LinearToGamma20(float3 c) { return sqrt(c.rgb); }
float4 LinearToGamma20(float4 c) { return float4(LinearToGamma20(c.rgb), c.a); }

//Gamma22
float Gamma22ToLinear (float  c) { return PositivePow(c, 2.2); }
float3 Gamma22ToLinear(float3 c) { return PositivePow(c.rgb, float3(2.2, 2.2, 2.2)); }
float4 Gamma22ToLinear(float4 c) { return float4(Gamma22ToLinear(c.rgb), c.a); }

float LinearToGamma22 (float  c) { return PositivePow(c, 0.454545454545455); }
float3 LinearToGamma22(float3 c) { return PositivePow(c.rgb, float3(0.454545454545455, 0.454545454545455, 0.454545454545455)); }
float4 LinearToGamma22(float4 c) { return float4(LinearToGamma22(c.rgb), c.a); }

//sRGB
float SRGBToLinear(float c)
{
    float linearRGBLo  = c / 12.92;
    float linearRGBHi  = PositivePow((c + 0.055) / 1.055, 2.4);
    float linearRGB    = (c <= 0.04045) ? linearRGBLo : linearRGBHi;
    return linearRGB;
}

float3 SRGBToLinear(float3 c)
{
    float3 linearRGBLo  = c / 12.92;
    float3 linearRGBHi  = PositivePow((c + 0.055) / 1.055, float3(2.4, 2.4, 2.4));
	float3 linearRGB;
	linearRGB.r = c.r <= 0.04045 ? linearRGBLo.r : linearRGBHi.r;
	linearRGB.g = c.g <= 0.04045 ? linearRGBLo.g : linearRGBHi.g;
	linearRGB.b = c.b <= 0.04045 ? linearRGBLo.b : linearRGBHi.b;
    return linearRGB;
}

float4 SRGBToLinear(float4 c)
{
    return float4(SRGBToLinear(c.rgb), c.a);
}

float LinearToSRGB(float c)
{
    float sRGBLo = c * 12.92;
    float sRGBHi = (PositivePow(c, 1.0/2.4) * 1.055) - 0.055;
    float sRGB   = (c <= 0.0031308) ? sRGBLo : sRGBHi;
    return sRGB;
}

float3 LinearToSRGB(float3 c)
{
    float3 sRGBLo = c * 12.92;
    float3 sRGBHi = (PositivePow(c, float3(1.0/2.4, 1.0/2.4, 1.0/2.4)) * 1.055) - 0.055;
	float3 sRGB;
	sRGB.r = c.r <= 0.0031308 ? sRGBLo.r : sRGBHi.r;
	sRGB.g = c.g <= 0.0031308 ? sRGBLo.g : sRGBHi.g;
	sRGB.b = c.b <= 0.0031308 ? sRGBLo.b : sRGBHi.b;
    return sRGB;
}

float4 LinearToSRGB(float4 c)
{
    return float4(LinearToSRGB(c.rgb), c.a);
}

#endif //LIBRARY_COLOR