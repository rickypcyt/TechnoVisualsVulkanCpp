#version 450

// PASS A — Base sampling (video / procedural / temporal)
// Responsibilities: video fetch, temporal interpolation, upscaling

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0, std140) uniform GlobalUBO {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec2 resolution;
    vec2 videoResolution;
    float time;
    float tempo;
    float energy;
    float bass;
    float mid;
    float high;
    vec4 primaryColor;
    vec4 secondaryColor;
    float colorBlend;
    int mode;
    float videoMix;
    float videoAvailable;
    float grayscaleAmount;
    float sharpenAmount;
    float upscaleEnabled;
    float crtCurvature;
    float crtHorizontalCurvature;
    float crtScanlineIntensity;
    float crtMaskIntensity;
    float crtVignette;
    float crtFishEye;
    float bloomIntensity;
    float bloomThreshold;
    float aberrationAmount;
    float grainStrength;
    float bendAmount;
    float glitchAmount;
    vec3 colorBalance;
    float gradeBrightness;
    float gradeContrast;
    float gradeSaturation;
    float gradeHueShift;
    float gradeGamma;
    int colorLUTIndex;
    float splitToneBalance;
    vec3 splitToneShadows;
    vec3 splitToneHighlights;
    float feedbackAmount;
    float trailStrength;
    float temporalAccumulation;
    float feedbackDecay;
    float recursiveBlend;
    float uvWarpStrength;
    float rippleStrength;
    float rippleFrequency;
    float swirlStrength;
    float displacementAmount;
    float kaleidoSegments;
    float tunnelDepth;
    float tunnelCurvature;
    float gaussianBlur;
    float directionalBlur;
    float directionalBlurAngle;
    float zoomBlur;
    float motionBlur;
    float temporalBlur;
    float unsharpMask;
    float casAmount;
    float localContrast;
    float glitchDatamosh;
    float glitchRGBSplit;
    float glitchScanlineBreak;
    float glitchJitter;
    float glitchTearing;
    float glitchPixelSort;
    float glitchBufferCorruption;
    int blendModeProcedural;
    int blendModeVideo;
    int blendModeFeedback;
    float blendProceduralMix;
    float blendVideoMix;
    float blendFeedbackMix;
    float analogScanlineFocus;
    float analogMaskBalance;
    float frameAccumulation;
    float slowMotionFactor;
    float temporalInterpolation;
    int nleOutputWidth;
    int nleOutputHeight;
    float nleGrayscale;
    float nleBrightness;
    float nleContrast;
    float nleSaturation;
} ubo;

layout(set = 0, binding = 1) uniform sampler2D videoTex;
layout(set = 0, binding = 2) uniform sampler2D videoTexPrev;

const float PI = 3.1415926535;

// Catmull-Rom bicubic interpolation
float catmullRom(float x) {
    x = abs(x);
    if (x <= 1.0) {
        return 1.5 * x * x * x - 2.5 * x * x + 1.0;
    } else if (x < 2.0) {
        return -0.5 * x * x * x + 2.5 * x * x - 4.0 * x + 2.0;
    }
    return 0.0;
}

// FSR RCAS helpers
float fsrMin3(float a, float b, float c) {
    return min(a, min(b, c));
}

float fsrMax3(float a, float b, float c) {
    return max(a, max(b, c));
}

float fsrRcas(float c, float a, float b, float d, float e, float sharpness) {
    float mn = fsrMin3(a, b, d);
    float mx = fsrMax3(a, b, d);
    float amp = clamp(min(mn, 2.0 - mx) / max(mx, 0.001), 0.0, 1.0);
    float peak = -1.0 / mix(8.0, 5.0, clamp(sharpness, 0.0, 1.0));
    float w = amp / peak;
    float r = (-mx) * w + c;
    r = min(w + r, mx);
    r = max(r, mn);
    return r;
}

// Bicubic interpolation
vec4 fsrBicubic(vec2 st) {
    vec2 texSize = max(vec2(textureSize(videoTex, 0)), vec2(1.0));
    vec2 texel = 1.0 / texSize;
    
    vec2 coord = st * texSize - 0.5;
    vec2 f = fract(coord);
    vec2 i = floor(coord);
    
    vec2 w0 = -0.5 * f * f * f + f * f - 0.5 * f;
    vec2 w1 =  1.5 * f * f * f - 2.5 * f * f + 1.0;
    vec2 w2 = -1.5 * f * f * f + 2.0 * f * f + 0.5 * f;
    vec2 w3 =  0.5 * f * f * f - 0.5 * f * f;
    
    vec2 s0 = w0 + w2;
    vec2 s1 = w1 + w3;
    
    vec2 offset0 = w2 / (w0 + w2 + 0.0001);
    vec2 offset1 = w3 / (w1 + w3 + 0.0001);
    
    vec2 samplePos0 = (i - 1.0 + offset0) * texel;
    vec2 samplePos1 = (i + 0.0 + offset1) * texel;
    
    vec3 col00 = texture(videoTex, clamp(samplePos0 + vec2(0.0, 0.0) * texel, 0.0, 1.0)).rgb;
    vec3 col01 = texture(videoTex, clamp(samplePos0 + vec2(1.0, 0.0) * texel, 0.0, 1.0)).rgb;
    vec3 col02 = texture(videoTex, clamp(samplePos1 + vec2(0.0, 0.0) * texel, 0.0, 1.0)).rgb;
    vec3 col03 = texture(videoTex, clamp(samplePos1 + vec2(1.0, 0.0) * texel, 0.0, 1.0)).rgb;
    
    vec3 col10 = texture(videoTex, clamp(samplePos0 + vec2(0.0, 1.0) * texel, 0.0, 1.0)).rgb;
    vec3 col11 = texture(videoTex, clamp(samplePos0 + vec2(1.0, 1.0) * texel, 0.0, 1.0)).rgb;
    vec3 col12 = texture(videoTex, clamp(samplePos1 + vec2(0.0, 1.0) * texel, 0.0, 1.0)).rgb;
    vec3 col13 = texture(videoTex, clamp(samplePos1 + vec2(1.0, 1.0) * texel, 0.0, 1.0)).rgb;
    
    vec3 row0 = mix(col00, col01, s0.x) + mix(col02, col03, s1.x);
    vec3 row1 = mix(col10, col11, s0.x) + mix(col12, col13, s1.x);
    
    vec3 bicubic = (mix(row0, row1, s0.y) + mix(row0, row1, s1.y)) / 2.0;
    
    return vec4(bicubic, 1.0);
}

// Edge-aware sampling with RCAS
vec4 sampleEdgeAware(vec2 st) {
    vec4 bicubic = fsrBicubic(st);
    
    float sharpness = clamp(ubo.sharpenAmount * 0.5 + 0.2, 0.0, 1.0);
    
    if (sharpness <= 0.001) {
        return bicubic;
    }
    
    vec2 texSize = max(vec2(textureSize(videoTex, 0)), vec2(1.0));
    vec2 texel = 1.0 / texSize;
    
    vec3 c = bicubic.rgb;
    vec3 t  = texture(videoTex, clamp(st + vec2( 0.0, -1.0) * texel, 0.0, 1.0)).rgb;
    vec3 b  = texture(videoTex, clamp(st + vec2( 0.0,  1.0) * texel, 0.0, 1.0)).rgb;
    vec3 l  = texture(videoTex, clamp(st + vec2(-1.0,  0.0) * texel, 0.0, 1.0)).rgb;
    vec3 r  = texture(videoTex, clamp(st + vec2( 1.0,  0.0) * texel, 0.0, 1.0)).rgb;
    
    vec3 result;
    result.r = fsrRcas(c.r, t.r, b.r, l.r, r.r, sharpness);
    result.g = fsrRcas(c.g, t.g, b.g, l.g, r.g, sharpness);
    result.b = fsrRcas(c.b, t.b, b.b, l.b, r.b, sharpness);
    
    return vec4(result, 1.0);
}

// Procedural rendering modes
vec4 renderMode0(vec2 st) {
    return mix(ubo.primaryColor, ubo.secondaryColor, ubo.colorBlend);
}

vec4 renderMode1(vec2 st) {
    return vec4(st, 0.5 + 0.5 * sin(ubo.time), 1.0);
}

vec4 dispatchMode(int m, vec2 st) {
    if (m == 0) return renderMode0(st);
    if (m == 1) return renderMode1(st);
    return vec4(0.0);
}

void main() {
    // Simple: just sample the video texture directly
    vec3 videoColor = texture(videoTex, uv).rgb;
    outColor = vec4(videoColor, 1.0);
}
