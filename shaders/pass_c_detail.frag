#version 450

// PASS C — BASE LAYER: Post FX Output
// Responsibilities: bloom, CRT scanlines/mask, vignette, film grain, chromatic aberration, color balance
// CAPA 1 - BASE (inferior): Procedural Controls + Post FX

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

    // --- Enable/Disable flags for post FX ---
    int enablePostCrtCurvature;
    int enablePostScanMask;
    int enablePostVignette;
    int enablePostFishEye;
    int enablePostBloom;
    int enablePostAberration;
    int enablePostGrain;
    int enablePostBend;
    int enablePostGlitch;
    int enablePostColorBalance;

    // --- Enable/Disable flags for VJAY BASICS ---
    int enableColorGrading;
    int enableFeedback;
    int enableDistortion;
    int enableBlurMotion;
    int enableSharpen;
    int enableGlitch;
    int enableBlending;
    int enableAnalog;
    int enableAudioReactive;
    int enableTemporal;

    // --- Enable/Disable flags for VJAY EXTRA ---
    int enablePixelate;
    int enableStrobe;
    int enableThreshold;
    int enableSlowZoom;

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

    float pixelateAmount;
    float strobeSpeed;
    float thresholdLevel;
    float slowZoomAmount;
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
} ubo;

layout(set = 0, binding = 1) uniform sampler2D inputTex;

const float PI = 3.1415926535;

float hash21(vec2 p) {
    p = fract(p * vec2(443.897, 441.423));
    p += dot(p, p.yx + 19.19);
    return fract(p.x * p.y);
}

// Optimized 5-sample cross blur (better performance than 9-sample box blur)
vec3 blur3x3(sampler2D tex, vec2 uv) {
    vec3 c = vec3(0.0);
    vec2 t = 1.0 / ubo.resolution;

    c += texture(tex, uv + t*vec2( 0,-1)).rgb * 0.25;
    c += texture(tex, uv + t*vec2(-1, 0)).rgb * 0.25;
    c += texture(tex, uv).rgb * 0.5;
    c += texture(tex, uv + t*vec2( 1, 0)).rgb * 0.25;
    c += texture(tex, uv + t*vec2( 0, 1)).rgb * 0.25;

    return c;
}

// Directional blur (uses same kernel but with directional sampling)
vec3 blurDirectional(vec2 uv, float angle) {
    vec3 acc = texture(inputTex, uv).rgb;
    float weight = 1.0;
    
    vec2 dir = vec2(cos(angle), sin(angle));
    
    for (int i = 1; i <= 2; ++i) {
        float w = 1.0 / float(i + 1);
        vec2 offset = dir * (1.0 / ubo.resolution) * float(i);
        acc += texture(inputTex, clamp(uv + offset, 0.0, 1.0)).rgb * w;
        acc += texture(inputTex, clamp(uv - offset, 0.0, 1.0)).rgb * w;
        weight += 2.0 * w;
    }
    
    return acc / weight;
}

// Zoom blur (radial sampling)
vec3 blurZoom(vec2 uv, vec2 centered) {
    vec3 acc = texture(inputTex, uv).rgb;
    float weight = 1.0;
    
    for (int i = 1; i <= 2; ++i) {
        vec2 offset = centered * float(i) * 0.05;
        acc += texture(inputTex, clamp(uv + offset, 0.0, 1.0)).rgb;
        weight += 1.0;
    }
    
    return acc / weight;
}

// Unsharp mask sharpening
vec3 unsharpMask(vec2 st, vec3 color) {
    float strength = clamp(ubo.unsharpMask + ubo.sharpenAmount, 0.0, 1.0);
    vec3 blurred = blur3x3(inputTex, st);
    vec3 sharpened = color + (color - blurred) * strength * 2.0;
    return mix(color, sharpened, strength);
}

// Local contrast enhancement
vec3 localContrast(vec3 color) {
    if (ubo.localContrast <= 0.0001) {
        return color;
    }
    
    float localLum = dot(color, vec3(0.299, 0.587, 0.114));
    return color + (color - vec3(localLum)) * ubo.localContrast;
}

void main() {
    vec2 centered = uv * 2.0 - 1.0;
    vec3 color = texture(inputTex, uv).rgb;

    // Check if any blur is active
    bool hasBlur = ubo.gaussianBlur > 0.0001 ||
                   ubo.directionalBlur > 0.0001 ||
                   ubo.zoomBlur > 0.0001 ||
                   ubo.motionBlur > 0.0001 ||
                   ubo.temporalBlur > 0.0001;

    // Apply blur if enabled and active
    if (ubo.enableBlurMotion == 1 && hasBlur) {
        float audioBlurMod = clamp(ubo.energy * 0.3, 0.0, 1.0);

        if (ubo.gaussianBlur > 0.0001) {
            color = mix(color, blur3x3(inputTex, uv), ubo.gaussianBlur);
        }

        if (ubo.directionalBlur > 0.0001) {
            float angle = radians(ubo.directionalBlurAngle);
            color = mix(color, blurDirectional(uv, angle), ubo.directionalBlur);
        }

        if (ubo.zoomBlur > 0.0001) {
            color = mix(color, blurZoom(uv, centered), ubo.zoomBlur);
        }

        if (ubo.motionBlur > 0.0001) {
            float lfo = sin(ubo.time * 0.5);
            vec2 dir = normalize(vec2(sin(ubo.time * 0.5 + lfo), cos(ubo.time * 0.35 - lfo)));
            color = mix(color, blurDirectional(uv, atan(dir.y, dir.x)), ubo.motionBlur);
        }

        if (ubo.temporalBlur > 0.0001) {
            float jitter = hash21(uv + ubo.time) - 0.5;
            vec2 offset = (1.0 / ubo.resolution) * jitter * 2.0;
            color = mix(color, texture(inputTex, clamp(uv + offset, 0.0, 1.0)).rgb, ubo.temporalBlur);
        }
    }

    // Apply sharpening if enabled
    if (ubo.enableSharpen == 1) {
        color = unsharpMask(uv, color);
    }

    // Apply local contrast if enabled
    if (ubo.enableSharpen == 1) {
        color = localContrast(color);
    }

    outColor = vec4(color, 1.0);
}
