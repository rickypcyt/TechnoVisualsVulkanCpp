#version 450

// PASS A — BASE LAYER: Base sampling
// Responsibilities: video/procedural mixing, temporal interpolation, grayscale, sharpen, upscaling
// CAPA 1 - BASE (inferior): Procedural Controls + Post FX

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

// Unified UBO - all parameters in single binding
layout(set = 0, binding = 0, std140) uniform GlobalParamsUBO {
    // FrameUBO
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

    // ColorPassUBO
    vec4 primaryColor;
    vec4 secondaryColor;
    float colorBlend;
    int mode;
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
    float grayscaleAmount;

    // CRTPassUBO
    float crtCurvature;
    float crtHorizontalCurvature;
    float crtScanlineIntensity;
    float crtMaskIntensity;
    float crtVignette;
    float crtFishEye;
    float analogScanlineFocus;
    float analogMaskBalance;
    int enablePostCrtCurvature;
    int enablePostScanMask;
    int enablePostVignette;
    int enablePostFishEye;

    // GlitchPassUBO
    float glitchAmount;
    float glitchDatamosh;
    float glitchRGBSplit;
    float glitchScanlineBreak;
    float glitchJitter;
    float glitchTearing;
    float glitchPixelSort;
    float glitchBufferCorruption;
    float aberrationAmount;
    int enablePostGlitch;
    int enablePostAberration;

    // TemporalPassUBO
    float feedbackAmount;
    float trailStrength;
    float temporalAccumulation;
    float feedbackDecay;
    float recursiveBlend;
    float frameAccumulation;
    float slowMotionFactor;
    float temporalInterpolation;
    int enableFeedback;
    int enableTemporal;

    // BloomPassUBO
    float bloomIntensity;
    float bloomThreshold;
    int enablePostBloom;

    // DistortionPassUBO
    float uvWarpStrength;
    float rippleStrength;
    float rippleFrequency;
    float swirlStrength;
    float displacementAmount;
    float kaleidoSegments;
    float tunnelDepth;
    float tunnelCurvature;
    float bendAmount;
    int enableDistortion;
    int enablePostBend;

    // BlurPassUBO
    float gaussianBlur;
    float directionalBlur;
    float directionalBlurAngle;
    float zoomBlur;
    float motionBlur;
    float temporalBlur;
    int enableBlurMotion;

    // SharpenPassUBO
    float unsharpMask;
    float casAmount;
    float localContrast;
    float sharpenAmount;
    int enableSharpen;

    // VideoPassUBO
    float videoMix;
    float videoAvailable;
    int blendModeVideo;
    float blendVideoMix;

    // BlendingPassUBO
    int blendModeProcedural;
    int blendModeFeedback;
    float blendProceduralMix;
    float blendFeedbackMix;
    int enableBlending;

    // GrainPassUBO
    float grainStrength;
    int enablePostGrain;

    // PostFXPassUBO
    float upscaleEnabled;
    int enablePostColorBalance;
    int enableColorGrading;
    int enableAnalog;
    int enableAudioReactive;

    // ExtraEffectsPassUBO
    float pixelateAmount;
    float strobeSpeed;
    float thresholdLevel;
    float slowZoomAmount;
    int enablePixelate;
    int enableStrobe;
    int enableThreshold;
    int enableSlowZoom;
    int enableEdgeDetect;
    float edgeStrength;
    float edgeThreshold;
    float edgeBlend;
    vec3 edgeColor;
    int enableMirror;
    int enableInvert;
    int enablePosterize;
    int enableInfrared;
    int enableZoomPulse;
    int enableRGBShift;
    float mirrorAmount;
    float posterizeLevels;
    float zoomPulseAmount;
    float rgbShiftAmount;

    // NLEExportPassUBO
    int nleOutputWidth;
    int nleOutputHeight;
    float nleGrayscale;
    float nleBrightness;
    float nleContrast;
    float nleSaturation;
} ubo;

layout(set = 1, binding = 0) uniform sampler2D videoTex;
layout(set = 1, binding = 1) uniform sampler2D videoTexPrev;

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
    float baseTime = ubo.time;
    float fastPhase = baseTime * 6.2831853;          // wrap roughly every second (since CPU already wraps time)
    float microPhase = fract(baseTime * 0.125) * 6.2831853;
    float uvPhase = dot(st, vec2(11.37, 17.91));

    vec2 centered = st - 0.5;
    float radius = length(centered);
    float angle = atan(centered.y, centered.x) + fastPhase * 0.08;

    // Flowing UV space
    vec2 flow = centered;
    flow += 0.12 * vec2(sin(fastPhase + st.y * 18.0), cos(fastPhase + st.x * 18.0));
    flow += 0.05 * vec2(cos(microPhase + uvPhase * 6.0), sin(microPhase - uvPhase * 5.0));

    float spiral = 0.5 + 0.5 * sin(angle * 10.0 + radius * 30.0 + fastPhase * 0.4);
    float rings = 0.5 + 0.5 * cos((flow.x + flow.y) * 20.0 - fastPhase * 0.6);
    float twirl = 0.5 + 0.5 * sin(radius * 55.0 - microPhase * 5.0 + uvPhase * 10.0);

    vec3 color = vec3(spiral, rings, twirl);
    color *= 1.0 - smoothstep(0.48, 0.75, radius);

    return vec4(color, 1.0);
}

vec4 dispatchMode(int m, vec2 st) {
    if (m == 0) return renderMode0(st);
    if (m == 1) return renderMode1(st);
    return vec4(0.0);
}

void main() {
    // Sample video texture
    vec4 videoColor = vec4(texture(videoTex, uv).rgb, 1.0);

    // Get procedural rendering
    vec4 proceduralColor = dispatchMode(ubo.mode, uv);

    // Mix video and procedural based on videoMix parameter
    // videoMix = 0.0: only procedural, videoMix = 1.0: only video
    vec4 mixedColor = mix(proceduralColor, videoColor, ubo.videoMix * ubo.videoAvailable);

    outColor = mixedColor;
}
