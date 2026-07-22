//LibraryTonemaps.hlsl

#ifndef LIBRARY_TONEMAPS
#define LIBRARY_TONEMAPS

//operators for each of the tonemaps

//Defines the SDR reference white level used in our tone mapping (typically 250 nits).
//#define GRAN_TURISMO_SDR_PAPER_WHITE = 250.0 // cd/m^2
#define TONEMAP_GRAN_TURISMO_7_SDR_PAPER_WHITE 100.0 //NOTE TO SELF: game has 100 by default

#define TONEMAP_REINHARD2_WHITE 4.0

#define TONEMAP_UCHIMURA_MAX_DISPLAY_BRIGHTNESS 1.0
#define TONEMAP_UCHIMURA_CONTRAST 1.0
#define TONEMAP_UCHIMURA_LINEAR_SECTION_START 0.22
#define TONEMAP_UCHIMURA_LINEAR_SECTION_LENGTH 0.4
#define TONEMAP_UCHIMURA_BLACK 1.33
#define TONEMAP_UCHIMURA_PEDESTAL 0.0

#define TONEMAP_UNCHARTED2_WHITE_POINT 11.2
#define TONEMAP_UNCHARTED2_EXPOSURE_BIAS 2.0

//|||||||||||||||||||||||||||||||||||||||||||| ACES (2015) TONEMAP ||||||||||||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||||||||||||| ACES (2015) TONEMAP ||||||||||||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||||||||||||| ACES (2015) TONEMAP ||||||||||||||||||||||||||||||||||||||||||||
//source - https://github.com/dmnsgn/glsl-tone-map/blob/main/aces.glsl

float3 ApplyTonemap_Aces2015(float3 color)
{
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return saturate((color * (a * color + b)) / (color * (c * color + d) + e));
}

//|||||||||||||||||||||||||||||||||||||||||||| ACES FITTED TONEMAP ||||||||||||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||||||||||||| ACES FITTED TONEMAP ||||||||||||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||||||||||||| ACES FITTED TONEMAP ||||||||||||||||||||||||||||||||||||||||||||
//source - https://github.com/TheRealMJP/BakingLab/blob/master/BakingLab/ACES.hlsl

// sRGB => XYZ => D65_2_D60 => AP1 => RRT_SAT
static const float3x3 ACESInputMat =
{
    {0.59719, 0.35458, 0.04823},
    {0.07600, 0.90834, 0.01566},
    {0.02840, 0.13383, 0.83777}
};

// ODT_SAT => XYZ => D60_2_D65 => sRGB
static const float3x3 ACESOutputMat =
{
    { 1.60475, -0.53108, -0.07367},
    {-0.10208,  1.10813, -0.00605},
    {-0.00327, -0.07276,  1.07602}
};

float3 RRTAndODTFit(float3 v)
{
    float3 a = v * (v + 0.0245786f) - 0.000090537f;
    float3 b = v * (0.983729f * v + 0.4329510f) + 0.238081f;
    return a / b;
}

float3 ApplyTonemap_AcesFitted(float3 color)
{
    color = mul(ACESInputMat, color);
    color = RRTAndODTFit(color);
    color = mul(ACESOutputMat, color);

    return saturate(color); //still linear, we have SRGB conversion later
}

//|||||||||||||||||||||||||||||||||||||||||||| AGX TONEMAP ||||||||||||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||||||||||||| AGX TONEMAP ||||||||||||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||||||||||||| AGX TONEMAP ||||||||||||||||||||||||||||||||||||||||||||
// https://iolite-engine.com/blog_posts/minimal_agx_implementation

// Inputs and outputs are encoded as Linear-sRGB.

static const float3x3 LINEAR_REC2020_TO_LINEAR_SRGB = transpose(float3x3(
    float3(1.6605, -0.1246, -0.0182), 
    float3(-0.5876, 1.1329, -0.1006), 
    float3(-0.0728, -0.0083, 1.1187))
);

static const float3x3 LINEAR_SRGB_TO_LINEAR_REC2020 = transpose(float3x3(
    float3(0.6274, 0.0691, 0.0164), 
    float3(0.3293, 0.9195, 0.088), 
    float3(0.0433, 0.0113, 0.8956))
);

static const float3x3 AgXInsetMatrix = transpose(float3x3(
    float3(0.85662717, 0.13731897, 0.11189821), 
    float3(0.09512124, 0.761242, 0.076799415), 
    float3(0.048251607, 0.10143904, 0.81130236)
));

static const float3x3 AgXOutsetMatrix = transpose(float3x3(
    float3(1.1271006, -0.14132977, -0.14132977), 
    float3(-0.11060664, 1.1578237, -0.11060664), 
    float3(-0.016493939, -0.016493939, 1.2519364)
));

// Mean error^2: 3.6705141e-06
float3 agxDefaultContrastApprox(float3 x) 
{ 
    float3 x2 = x * x;
    float3 x4 = x2 * x2;
    float3 x6 = x4 * x2;
    return  - 17.86f    * x6 * x
            + 78.01f    * x6
            - 126.7f    * x4 * x
            + 92.06f    * x4
            - 28.72f    * x2 * x
            + 4.361f    * x2
            - 0.1718f   * x
            + 0.002857f;
}

float3 agx(float3 val)
{
	// LOG2_MIN      = -10.0
	// LOG2_MAX      =  +6.5
	// MIDDLE_GRAY   =  0.18
	const float AgxMinEv = - 12.47393;  // log2( pow( 2, LOG2_MIN ) * MIDDLE_GRAY )
	const float AgxMaxEv = 4.026069;    // log2( pow( 2, LOG2_MAX ) * MIDDLE_GRAY )

    // Input transform (inset);
    val = mul(LINEAR_SRGB_TO_LINEAR_REC2020, val);
    val = mul(AgXInsetMatrix, val);
    
    // Log2 space encoding
    val = max(val, 1e-10); // avoid 0 or negative numbers for log2
    val = log2(val);
	val = ( val - AgxMinEv ) / ( AgxMaxEv - AgxMinEv );

    val = saturate(val);
    
    // Apply sigmoid function approximation
    val = agxDefaultContrastApprox(val);

    return val;
}

float3 agxEotf(float3 val) 
{   
    // Inverse input transform (outset)
    val = mul(AgXOutsetMatrix, val);
    
    // sRGB IEC 61966-2-1 2.2 Exponent Reference EOTF Display
    // NOTE: We're linearizing the output here. Comment/adjust when
    // *not* using a sRGB render target
    val = max(val, 0); 
    val = pow(val, 2.2);
    val = mul(LINEAR_REC2020_TO_LINEAR_SRGB, val);

    return val;
}

float3 ApplyTonemap_Agx(float3 color)
{
    color = agx(color);

    const float3 lw = float3(0.2126, 0.7152, 0.0722);
    float luma = dot(color, lw);
        
    float3 offset = (0.0);
    float3 slope =  (1.0);
    float3 power =  (1.0);
    float sat = 1.0;

	//this as been tuned to match pretty closely to the original game look in terms of brightness/contrast
	//offset = 0.0f; //brightness
    //slope = 1.2; //gamma
    //power = 1.3; //contrast
    //sat = 1.0; //saturation
        
    // ASC CDL
    color = pow(color * slope + offset, power);
    color = luma + sat * (color - luma);

    color = agxEotf(color);
    return color;
}

//|||||||||||||||||||||||||||||||||||||||||||| FILMIC TONEMAP ||||||||||||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||||||||||||| FILMIC TONEMAP ||||||||||||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||||||||||||| FILMIC TONEMAP ||||||||||||||||||||||||||||||||||||||||||||
//source - http://filmicworlds.com/blog/filmic-tonemapping-operators/

float3 ApplyTonemap_Filmic(float3 color)
{
    float3 X = max(float3(0.0f, 0.0f, 0.0f), color - 0.004f);
    float3 result = (X * (6.2f * X + 0.5f)) / (X * (6.2f * X + 1.7f) + 0.06f);
    return pow(result, float3(2.2f, 2.2f, 2.2f));
}

//|||||||||||||||||||||||||||||||||||||||||||| GRAN TURISMO 7 TONEMAP ||||||||||||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||||||||||||| GRAN TURISMO 7 TONEMAP ||||||||||||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||||||||||||| GRAN TURISMO 7 TONEMAP ||||||||||||||||||||||||||||||||||||||||||||
//source - https://blog.selfshadow.com/publications/s2025-shading-course/pdi/supplemental/gt7_tone_mapping.cpp

//"GT Tone Mapping" curve with convergent shoulder.
struct GTToneMappingCurveV2
{
    float peakIntensity_;
    float alpha_;
    float midPoint_;
    float linearSection_;
    float toeStrength_;
    float kA_, kB_, kC_;
};

//GT7 Tone Mapping struct and functions.
struct GT7ToneMapping
{
    float sdrCorrectionFactor_;
    float framebufferLuminanceTarget_;
    float framebufferLuminanceTargetUcs_; // Target luminance in UCS space
    GTToneMappingCurveV2 curve_;
    float blendRatio_;
    float fadeStart_;
    float fadeEnd_;
};

// Mode options.
#define TONE_MAPPING_UCS_ICTCP  0
#define TONE_MAPPING_UCS_JZAZBZ 1

#define TONE_MAPPING_UCS TONE_MAPPING_UCS_ICTCP

// Gran Turismo luminance-scale conversion helpers.
// In Gran Turismo, 1.0f in the linear frame-buffer space corresponds to
// REFERENCE_LUMINANCE cd/m^2 of physical luminance (typically 100 cd/m^2).
static const float REFERENCE_LUMINANCE = 100.0f; // cd/m^2 <-> 1.0f

float frameBufferValueToPhysicalValue(float fbValue)
{
    // Converts linear frame-buffer value to physical luminance (cd/m^2)
    // where 1.0 corresponds to REFERENCE_LUMINANCE (e.g., 100 cd/m^2).
    return fbValue * REFERENCE_LUMINANCE;
}

float physicalValueToFrameBufferValue(float physical)
{
    // Converts physical luminance (cd/m^2) to a linear frame-buffer value
    // where 1.0 corresponds to REFERENCE_LUMINANCE (e.g., 100 cd/m^2).
    return physical / REFERENCE_LUMINANCE;
}

//Utility functions.
float chromaCurve(float x, float a, float b)
{
    // Note: The original C++ smoothStep is identical to the HLSL intrinsic.
    return 1.0f - smoothstep(a, b, x);
}

// "GT Tone Mapping" curve with convergent shoulder.
void InitializeCurve(inout GTToneMappingCurveV2 curve,
                     float monitorIntensity,
                     float alpha,
                     float grayPoint,
                     float linearSection,
                     float toeStrength)
{
    curve.peakIntensity_ = monitorIntensity;
    curve.alpha_         = alpha;
    curve.midPoint_      = grayPoint;
    curve.linearSection_ = linearSection;
    curve.toeStrength_   = toeStrength;

    // Pre-compute constants for the shoulder region.
    float k = (linearSection - 1.0f) / (alpha - 1.0f);
    curve.kA_ = monitorIntensity * linearSection + monitorIntensity * k;
    curve.kB_ = -monitorIntensity * k * exp(linearSection / k);
    curve.kC_ = -1.0f / (k * monitorIntensity);
}

float EvaluateCurve(in GTToneMappingCurveV2 curve, float x)
{
    if (x < 0.0f)
    {
        return 0.0f;
    }

    float weightLinear = smoothstep(0.0f, curve.midPoint_, x);
    float weightToe    = 1.0f - weightLinear;

    // Shoulder mapping for highlights.
    float shoulder = curve.kA_ + curve.kB_ * exp(x * curve.kC_);

    if (x < curve.linearSection_ * curve.peakIntensity_)
    {
        float toeMapped = curve.midPoint_ * pow(x / curve.midPoint_, curve.toeStrength_);
        return weightToe * toeMapped + weightLinear * x;
    }
    else
    {
        return shoulder;
    }
}

// EOTF / inverse-EOTF for ST-2084 (PQ).
// Note: Introduce exponentScaleFactor to allow scaling of the exponent in the EOTF for Jzazbz.
float eotfSt2084(float n, float exponentScaleFactor = 1.0)
{
    n = clamp(n, 0.0f, 1.0f);

    // Base functions from SMPTE ST 2084:2014
    // Converts from normalized PQ (0-1) to absolute luminance in cd/m^2 (linear light)
    // Assumes float input; does not handle integer encoding (Annex)
    // Assumes full-range signal (0-1)
    const float m1  = 0.1593017578125f;                // (2610 / 4096) / 4
    const float m2  = 78.84375f * exponentScaleFactor; // (2523 / 4096) * 128
    const float c1  = 0.8359375f;                      // 3424 / 4096
    const float c2  = 18.8515625f;                     // (2413 / 4096) * 32
    const float c3  = 18.6875f;                        // (2392 / 4096) * 32
    const float pqC = 10000.0f;                        // Maximum luminance supported by PQ (cd/m^2)

    // Does not handle signal range from 2084 - assumes full range (0-1)
    float np = pow(n, 1.0f / m2);
    float l  = max(0.0f, np - c1);
    
    l = l / (c2 - c3 * np);
    l = pow(l, 1.0f / m1);

    // Convert absolute luminance (cd/m^2) into the frame-buffer linear scale.
    return physicalValueToFrameBufferValue(l * pqC);
}

float inverseEotfSt2084(float v, float exponentScaleFactor = 1.0)
{
    const float m1  = 0.1593017578125f;
    const float m2  = 78.84375f * exponentScaleFactor;
    const float c1  = 0.8359375f;
    const float c2  = 18.8515625f;
    const float c3  = 18.6875f;
    const float pqC = 10000.0f;

    // Convert the frame-buffer linear scale into absolute luminance (cd/m^2).
    float physical = frameBufferValueToPhysicalValue(v);
    float y        = physical / pqC; // Normalize for the ST-2084 curve

    float ym = pow(y, m1);
    float numerator = c1 + c2 * ym;
    float denominator = 1.0f + c3 * ym;
    return pow(numerator / denominator, m2);
}

// ICtCp conversion.
// Reference: ITU-T T.302 (https://www.itu.int/rec/T-REC-T.302/en)
float3 rgbToICtCp(float3 rgb) // Input: linear Rec.2020
{
    float l = (rgb.r * 1688.0f + rgb.g * 2146.0f + rgb.b * 262.0f) / 4096.0f;
    float m = (rgb.r * 683.0f + rgb.g * 2951.0f + rgb.b * 462.0f) / 4096.0f;
    float s = (rgb.r * 99.0f + rgb.g * 309.0f + rgb.b * 3688.0f) / 4096.0f;

    float lPQ = inverseEotfSt2084(l);
    float mPQ = inverseEotfSt2084(m);
    float sPQ = inverseEotfSt2084(s);

    float I = (2048.0f * lPQ + 2048.0f * mPQ) / 4096.0f;
    float Ct = (6610.0f * lPQ - 13613.0f * mPQ + 7003.0f * sPQ) / 4096.0f;
    float Cp = (17933.0f * lPQ - 17390.0f * mPQ - 543.0f * sPQ) / 4096.0f;
    
    return float3(I, Ct, Cp);
}

float3 iCtCpToRgb(float3 ictCp) // Output: linear Rec.2020
{
    float l = ictCp.x + 0.00860904f * ictCp.y + 0.11103f * ictCp.z;
    float m = ictCp.x - 0.00860904f * ictCp.y - 0.11103f * ictCp.z;
    float s = ictCp.x + 0.560031f * ictCp.y - 0.320627f * ictCp.z;

    float lLin = eotfSt2084(l);
    float mLin = eotfSt2084(m);
    float sLin = eotfSt2084(s);

    float r = max(0.0f, 3.43661f * lLin - 2.50645f * mLin + 0.0698454f * sLin);
    float g = max(0.0f, -0.79133f * lLin + 1.9836f * mLin - 0.192271f * sLin);
    float b = max(0.0f, -0.0259499f * lLin - 0.0989137f * mLin + 1.12486f * sLin);

    return float3(r, g, b);
}

// Jzazbz conversion.
// Reference:
// Muhammad Safdar, Guihua Cui, Youn Jin Kim, and Ming Ronnier Luo,
// "Perceptually uniform color space for image signals including high dynamic
// range and wide gamut," Opt. Express 25, 15131-15151 (2017)
// Note: Coefficients adjusted for linear Rec.2020
static const float JZAZBZ_EXPONENT_SCALE_FACTOR = 1.7f;

float3 rgbToJzazbz(float3 rgb) // Input: linear Rec.2020
{
    float l = rgb.r * 0.530004f + rgb.g * 0.355704f + rgb.b * 0.086090f;
    float m = rgb.r * 0.289388f + rgb.g * 0.525395f + rgb.b * 0.157481f;
    float s = rgb.r * 0.091098f + rgb.g * 0.147588f + rgb.b * 0.734234f;

    float lPQ = inverseEotfSt2084(l, JZAZBZ_EXPONENT_SCALE_FACTOR);
    float mPQ = inverseEotfSt2084(m, JZAZBZ_EXPONENT_SCALE_FACTOR);
    float sPQ = inverseEotfSt2084(s, JZAZBZ_EXPONENT_SCALE_FACTOR);

    float iz = 0.5f * lPQ + 0.5f * mPQ;

    float jz = (0.44f * iz) / (1.0f - 0.56f * iz) - 1.6295499532821566e-11f;
    float az = 3.524000f * lPQ - 4.066708f * mPQ + 0.542708f * sPQ;
    float bz = 0.199076f * lPQ + 1.096799f * mPQ - 1.295875f * sPQ;
    
    return float3(jz, az, bz);
}

float3 jzazbzToRgb(float3 jab) // Output: linear Rec.2020
{
    float jz = jab.x + 1.6295499532821566e-11f;
    float iz = jz / (0.44f + 0.56f * jz);
    float a  = jab.y;
    float b  = jab.z;

    float l = iz + a * 1.386050432715393e-1f + b * 5.804731615611869e-2f;
    float m = iz - a * 1.386050432715393e-1f - b * 5.804731615611869e-2f;
    float s = iz - a * 9.601924202631895e-2f - b * 8.118918960560390e-1f;

    float lLin = eotfSt2084(l, JZAZBZ_EXPONENT_SCALE_FACTOR);
    float mLin = eotfSt2084(m, JZAZBZ_EXPONENT_SCALE_FACTOR);
    float sLin = eotfSt2084(s, JZAZBZ_EXPONENT_SCALE_FACTOR);

    float r = lLin * 2.990669f + mLin * -2.049742f + sLin * 0.088977f;
    float g = lLin * -1.634525f + mLin * 3.145627f + sLin * -0.483037f;
    float b_ = lLin * -0.042505f + mLin * -0.377983f + sLin * 1.448019f;
    
    return float3(r, g, b_);
}

// Unified color space (UCS): ICtCp or Jzazbz.
#if TONE_MAPPING_UCS == TONE_MAPPING_UCS_ICTCP
	float3 rgbToUcs(float3 rgb) { return rgbToICtCp(rgb); }
	float3 ucsToRgb(float3 ucs) { return iCtCpToRgb(ucs); }
#elif TONE_MAPPING_UCS == TONE_MAPPING_UCS_JZAZBZ
	float3 rgbToUcs(float3 rgb) { return rgbToJzazbz(rgb); }
	float3 ucsToRgb(float3 ucs) { return jzazbzToRgb(ucs); }
#else
	#error "Unsupported TONE_MAPPING_UCS value. Please define TONE_MAPPING_UCS as either TONE_MAPPING_UCS_ICTCP or TONE_MAPPING_UCS_JZAZBZ."
#endif

// GT7 Tone Mapping struct and functions.
// Initializes the tone mapping curve and related parameters based on the target display luminance.
// This method should not be called directly. Use initializeAsHDR() or initializeAsSDR() instead.
void InitializeParameters(inout GT7ToneMapping tm, float physicalTargetLuminance)
{
    tm.framebufferLuminanceTarget_ = physicalValueToFrameBufferValue(physicalTargetLuminance);
    InitializeCurve(tm.curve_, tm.framebufferLuminanceTarget_, 0.25f, 0.538f, 0.444f, 1.280f);
    tm.blendRatio_ = 0.6f;
    tm.fadeStart_  = 0.98f;
    tm.fadeEnd_    = 1.16f;

    float3 ucs = rgbToUcs(float3(tm.framebufferLuminanceTarget_, tm.framebufferLuminanceTarget_, tm.framebufferLuminanceTarget_));
    tm.framebufferLuminanceTargetUcs_ = ucs.x; // Use the first UCS component (I or Jz) as luminance
}

// Initialize for HDR (High Dynamic Range) display.
// Input: target display peak luminance in nits (range: 250 to 10,000)
// Note: The lower limit is 250 because the parameters for GTToneMappingCurveV2
//       were determined based on an SDR paper white assumption of 250 nits (TONEMAP_GRAN_TURISMO_7_SDR_PAPER_WHITE).
void InitializeAsHDR(inout GT7ToneMapping tm, float physicalTargetLuminance)
{
    tm.sdrCorrectionFactor_ = 1.0f;
    InitializeParameters(tm, physicalTargetLuminance);
}

// Initialize for SDR (Standard Dynamic Range) display.
void InitializeAsSDR(inout GT7ToneMapping tm)
{
    // Regarding SDR output:
    // First, in GT (Gran Turismo), it is assumed that a maximum value of 1.0 in SDR output
    // corresponds to GRAN_TURISMO_SDR_PAPER_WHITE (typically 250 nits).
    // Therefore, tone mapping for SDR output is performed based on TONEMAP_GRAN_TURISMO_7_SDR_PAPER_WHITE.
    // However, in the sRGB standard, 1.0f corresponds to 100 nits,
    // so we need to "undo" the tone-mapped values accordingly.
    // To match the sRGB range, the tone-mapped values are scaled using sdrCorrectionFactor_.
    //
    // * These adjustments ensure that the visual appearance (in terms of brightness)
    //   stays generally consistent across both HDR and SDR outputs for the same rendered content.
    tm.sdrCorrectionFactor_ = 1.0f / physicalValueToFrameBufferValue(TONEMAP_GRAN_TURISMO_7_SDR_PAPER_WHITE);
    InitializeParameters(tm, TONEMAP_GRAN_TURISMO_7_SDR_PAPER_WHITE);
}

// Applies the GT7 tonemapping curve.
// Input:  linear Rec.2020 RGB (frame buffer values)
// Output: tone-mapped RGB (frame buffer values);
//         - in SDR mode: mapped to [0, 1], ready for sRGB OETF
//         - in HDR mode: mapped to [0, framebufferLuminanceTarget_], ready for PQ inverse-EOTF
// Note: framebufferLuminanceTarget_ represents the display's target peak luminance converted to a frame buffer value.
//       The returned values are suitable for applying the appropriate OETF to generate final output signal.
float3 ApplyToneMapping(in GT7ToneMapping tm, float3 rgb)
{
    // Convert to UCS to separate luminance and chroma.
    float3 ucs = rgbToUcs(rgb);

    // Per-channel tone mapping ("skewed" color).
    float3 skewedRgb = float3(EvaluateCurve(tm.curve_, rgb.r),
                              EvaluateCurve(tm.curve_, rgb.g),
                              EvaluateCurve(tm.curve_, rgb.b));

    float3 skewedUcs = rgbToUcs(skewedRgb);

    float chromaScale = chromaCurve(ucs.x / tm.framebufferLuminanceTargetUcs_, tm.fadeStart_, tm.fadeEnd_);

    float3 scaledUcs = float3(skewedUcs.x, ucs.y * chromaScale, ucs.z * chromaScale);

    // Convert back to RGB.
    float3 scaledRgb = ucsToRgb(scaledUcs);

    // Final blend between per-channel and UCS-scaled results.
    float3 blended = lerp(skewedRgb, scaledRgb, tm.blendRatio_);
    
    // When using SDR, apply the correction factor.
    // When using HDR, sdrCorrectionFactor_ is 1.0f, so it has no effect.
    float3 out_rgb = tm.sdrCorrectionFactor_ * min(blended, tm.framebufferLuminanceTarget_);
    
    return out_rgb;
}

// Helper function for Custom Tonemap. 
float3 ApplyTonemap_GranTurismo7(float3 color)
{
    GT7ToneMapping tm = (GT7ToneMapping)0;
    InitializeAsSDR(tm);
    return ApplyToneMapping(tm, color);
}




//|||||||||||||||||||||||||||||||||||||||||||| KHRONOS NEUTRAL TONEMAP ||||||||||||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||||||||||||| KHRONOS NEUTRAL TONEMAP ||||||||||||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||||||||||||| KHRONOS NEUTRAL TONEMAP ||||||||||||||||||||||||||||||||||||||||||||
//source - https://github.com/KhronosGroup/ToneMapping/blob/main/PBR_Neutral/pbrNeutral.glsl

//Input color is non-negative and resides in the Linear Rec. 709 color space.
//Output color is also Linear Rec. 709, but in the [0, 1] range.
float3 ApplyTonemap_KhronosNeutral(float3 color)
{
    const float startCompression = 0.8f - 0.04f;
    const float desaturation = 0.15f;

    float x = min(color.r, min(color.g, color.b));
    float offset = x < 0.08f ? x - 6.25f * x * x : 0.04f;
    color -= offset;

    float peak = max(color.r, max(color.g, color.b));

    if (peak < startCompression)
        return color;

    const float d = 1.0f - startCompression;
    float newPeak = 1.0f - d * d / (peak + d - startCompression);
    color *= newPeak / peak;

    float g = 1.0f - 1.0f / (desaturation * (peak - newPeak) + 1.0f);
    return lerp(color, float3(newPeak, newPeak, newPeak), g);
}

//|||||||||||||||||||||||||||||||||||||||||||| LOTTES TONEMAP ||||||||||||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||||||||||||| LOTTES TONEMAP ||||||||||||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||||||||||||| LOTTES TONEMAP ||||||||||||||||||||||||||||||||||||||||||||
//source - https://github.com/dmnsgn/glsl-tone-map/blob/main/lottes.glsl

float3 ApplyTonemap_Lottes(float3 color)
{
    const float3 a = float3(1.6f, 1.6f, 1.6f);
    const float3 d = float3(0.977f, 0.977f, 0.977f);
    const float3 hdrMax = float3(8.0f, 8.0f, 8.0f);
    const float3 midIn = float3(0.18f, 0.18f, 0.18f);
    const float3 midOut = float3(0.267f, 0.267f, 0.267f);

    const float3 b =
        (-pow(midIn, a) + pow(hdrMax, a) * midOut) /
        ((pow(hdrMax, a * d) - pow(midIn, a * d)) * midOut);

    const float3 c =
      (pow(hdrMax, a * d) * pow(midIn, a) - pow(hdrMax, a) * pow(midIn, a * d) * midOut) /
      ((pow(hdrMax, a * d) - pow(midIn, a * d)) * midOut);

    return pow(color, a) / (pow(color, a * d) * b + c);
}

//|||||||||||||||||||||||||||||||||||||||||||| REINHARD TONEMAP ||||||||||||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||||||||||||| REINHARD TONEMAP ||||||||||||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||||||||||||| REINHARD TONEMAP ||||||||||||||||||||||||||||||||||||||||||||
//source - https://github.com/dmnsgn/glsl-tone-map/blob/main/reinhard.glsl

float3 ApplyTonemap_Reinhard(float3 color)
{
    return color / (1.0f + color);
}

//|||||||||||||||||||||||||||||||||||||||||||| REINHARD 2 TONEMAP ||||||||||||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||||||||||||| REINHARD 2 TONEMAP ||||||||||||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||||||||||||| REINHARD 2 TONEMAP ||||||||||||||||||||||||||||||||||||||||||||
//source - https://github.com/dmnsgn/glsl-tone-map/blob/main/reinhard2.glsl

float3 ApplyTonemap_Reinhard2(float3 color)
{
    return (color * (1.0f + color / (TONEMAP_REINHARD2_WHITE * TONEMAP_REINHARD2_WHITE))) / (1.0f + color);
}

//|||||||||||||||||||||||||||||||||||||||||||| UCHIMURA TONEMAP ||||||||||||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||||||||||||| UCHIMURA TONEMAP ||||||||||||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||||||||||||| UCHIMURA TONEMAP ||||||||||||||||||||||||||||||||||||||||||||
//source - https://github.com/dmnsgn/glsl-tone-map/blob/main/uchimura.glsl

float3 uchimura(float3 x, float P, float a, float m, float l, float c, float b)
{
    float l0 = ((P - m) * l) / a;
    float L0 = m - m / a;
    float L1 = m + (1.0f - m) / a;
    float S0 = m + l0;
    float S1 = m + a * l0;
    float C2 = (a * P) / (P - S1);
    float CP = -C2 / P;

    float3 w0 = float3(1.0f - smoothstep(0.0f, m, x));
    float3 w2 = float3(step(m + l0, x));
    float3 w1 = float3(1.0f - w0 - w2);

    float3 T = float3(m * pow(x / m, float3(c, c, c)) + b);
    float3 S = float3(P - (P - S1) * exp(CP * (x - S0)));
    float3 L = float3(m + a * (x - m));

    return T * w0 + L * w1 + S * w2;
}

float3 ApplyTonemap_Uchimura(float3 color)
{
    return uchimura(
		color, 
		TONEMAP_UCHIMURA_MAX_DISPLAY_BRIGHTNESS, 
		TONEMAP_UCHIMURA_CONTRAST, 
		TONEMAP_UCHIMURA_LINEAR_SECTION_START, 
		TONEMAP_UCHIMURA_LINEAR_SECTION_LENGTH, 
		TONEMAP_UCHIMURA_BLACK, 
		TONEMAP_UCHIMURA_PEDESTAL);
}

//|||||||||||||||||||||||||||||||||||||||||||| UNCHARTED 2 TONEMAP ||||||||||||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||||||||||||| UNCHARTED 2 TONEMAP ||||||||||||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||||||||||||| UNCHARTED 2 TONEMAP ||||||||||||||||||||||||||||||||||||||||||||
//source - https://github.com/dmnsgn/glsl-tone-map/blob/main/uncharted2.glsl

float3 uncharted2Tonemap(float3 x)
{
    float A = 0.15f;
    float B = 0.50f;
    float C = 0.10f;
    float D = 0.20f;
    float E = 0.02f;
    float F = 0.30f;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

float3 ApplyTonemap_Uncharted2(float3 color)
{
    float3 curr = uncharted2Tonemap(TONEMAP_UNCHARTED2_EXPOSURE_BIAS * color);
    float3 whiteScale = 1.0f / uncharted2Tonemap(float3(TONEMAP_UNCHARTED2_WHITE_POINT, TONEMAP_UNCHARTED2_WHITE_POINT, TONEMAP_UNCHARTED2_WHITE_POINT));
    return curr * whiteScale;
}

//|||||||||||||||||||||||||||||||||||||||||||| UNREAL 3 TONEMAP ||||||||||||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||||||||||||| UNREAL 3 TONEMAP ||||||||||||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||||||||||||| UNREAL 3 TONEMAP ||||||||||||||||||||||||||||||||||||||||||||
//source - https://github.com/dmnsgn/glsl-tone-map/blob/main/unreal.glsl

float3 ApplyTonemap_Unreal3(float3 color)
{
    return color / (color + 0.155f) * 1.019f;
}

//|||||||||||||||||||||||||||||||||||||||||||| EXPONENTIAL TONEMAP ||||||||||||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||||||||||||| EXPONENTIAL TONEMAP ||||||||||||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||||||||||||| EXPONENTIAL TONEMAP ||||||||||||||||||||||||||||||||||||||||||||

float3 ApplyTonemap_Exponential(float3 color)
{
    return 1.0f - exp(-color);
}

float3 ApplyTonemap_ExponentialSquared(float3 color)
{
    return sqrt(1.0f - exp(-(color * color)));
}

//|||||||||||||||||||||||||||||||||||||||||||| MGSV TONEMAP ||||||||||||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||||||||||||| MGSV TONEMAP ||||||||||||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||||||||||||| MGSV TONEMAP ||||||||||||||||||||||||||||||||||||||||||||

float3 MGSV(float3 x, float A, float B)
{
    float3 t = step(float3(A, A, A), x);
    float3 y = min(float3(1.0, 1.0, 1.0), A + B - B*B / (x - A + B));
    return lerp(x, y, t);
}

float3 ApplyTonemap_MGSV(float3 color)
{
    const float A = 0.6; // Linear section length
    const float B = 0.45; // Contrast
    return MGSV(color, A, B);
}

//|||||||||||||||||||||||||||||||||||||||||||| TONY-MC-MAP-FACE TONEMAP FAST ||||||||||||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||||||||||||| TONY-MC-MAP-FACE TONEMAP FAST ||||||||||||||||||||||||||||||||||||||||||||
//|||||||||||||||||||||||||||||||||||||||||||| TONY-MC-MAP-FACE TONEMAP FAST ||||||||||||||||||||||||||||||||||||||||||||
//source - https://www.shadertoy.com/view/s32XRK
//original non fast - https://www.shadertoy.com/view/wXcXz4
//NOTE TO SELF: unfortunatly after adapting both variants this tonemap seems to mess with the colors a little too much
//there is a warm hue present in the tonemap that ideally there shouldn't be, all the other tonemappers are much cleaner

static const float4x4 W_H0 = float4x4(0.39586228f, -0.03972201f, 0.80481762f, 0.04644348f, 1.58664536f, 1.66902244f, 2.44403410f, 0.55288339f, 0.36988291f, 0.15207146f, 1.64132059f, -1.13008571f, -5.94598198f, -2.82862377f, -5.82778120f, -1.80443943f);
static const float4x4 W_H1 = float4x4(0.18353821f, 0.32969889f, -1.00356138f, 0.36945057f, 0.95604730f, 1.08331323f, 0.56690210f, 1.08915603f, 0.02231202f, -0.36653680f, -0.09584079f, 0.05614884f, -3.45653129f, -3.48821163f, -1.57088459f, -3.57599139f);
static const float4x4 W_H2 = float4x4(-1.63317251f, 0.13153945f, -0.94357085f, -1.13281119f, -0.26234260f, 0.82354331f, -3.42180729f, -1.63760138f, -1.29429603f, -0.32949299f, 0.10173334f, -0.43876860f, 2.70939469f, 2.06048155f, 3.54249549f, 4.33876848f);
static const float4 B_H0 = float4(0.66067636f, 0.29956132f, 1.76998234f, 0.17330712f);
static const float4 B_H1 = float4(0.63329625f, 1.36817801f, -0.14686975f, 0.42374569f);
static const float4 B_H2 = float4(-0.50533009f, 0.80425847f, -1.44877112f, -2.47578382f);
static const float4x3 W_O0 = float4x3(-0.70704323f, 0.01483737f, 0.37019095f, -0.05342029f, 0.05530520f, -2.54849434f, 0.41999695f, 0.02310869f, 0.01786434f, -3.08490849f, -0.21992514f, 0.03777298f);
static const float4x3 W_O1 = float4x3(2.50645280f, 3.12841654f, 0.46637687f, -2.77187896f, 0.31183574f, 0.15295352f, -0.44559595f, -0.54871750f, -4.36423111f, 2.30986452f, -3.21602535f, 0.97358066f);
static const float4x3 W_O2 = float4x3(-0.22230430f, -0.03942725f, -0.42500046f, 0.95312279f, 0.07827633f, 0.26239559f, -0.94722545f, -0.05049668f, -0.10606785f, 2.15497899f, -0.03251265f, 6.17893267f);
static const float3 B_O = float3(-2.90981388f, 0.13765818f, 0.26697257f);

//NOTE: because the dev behind the tony mc map face never release the code for his tonemap, only a DDS LUT
//looks like this author managed to train a neural network on the DDS texture LUT to derrive the formulas
float3 ApplyTonemap_TonyMcMapFaceNeuralNetwork(float3 hdrColor)
{
	//NOTE: there is some funky stuff happening with the shadertoy for some reason
	//to fix the rotate colors reswizzling seems to work
	hdrColor = hdrColor.zyx;

	float luma = LuminanceRec709(hdrColor);
    float luma_rein = luma / (1.0 + luma);
    
    float4 X = float4(hdrColor / max(luma, 1e-6f), luma_rein);
    
	float4 h0 = mul(X, W_H0) + B_H0;
    float4 h1 = mul(X, W_H1) + B_H1; 
    float4 h2 = mul(X, W_H2) + B_H2;

	h0 = h0 / (1.0 + exp(-h0)); // SiLU
	h1 = h1 / (1.0 + exp(-h1)); // SiLU
	h2 = h2 / (1.0 + exp(-h2)); // SiLU
    
	float3 scale = mul(h0, W_O0) + mul(h1, W_O1) + mul(h2, W_O2) + B_O;
    scale = log(1.0 + exp(scale));
    
    return luma_rein * scale;
}

#endif //LIBRARY_TONEMAPS