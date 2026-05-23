#version 450

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0, std140) uniform GlobalParamsUBO {
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

    float audioWarpResponse;
    float audioFeedbackResponse;
    float audioBlurResponse;
    float audioColorResponse;
    float audioGlitchResponse;
    float audioBeatSync;
    float audioLfoRate;

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

    float bloomIntensity;
    float bloomThreshold;
    int enablePostBloom;

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

    float gaussianBlur;
    float directionalBlur;
    float directionalBlurAngle;
    float zoomBlur;
    float motionBlur;
    float temporalBlur;
    int enableBlurMotion;

    float unsharpMask;
    float casAmount;
    float localContrast;
    float sharpenAmount;
    int enableSharpen;

    float videoMix;
    float videoAvailable;
    int blendModeVideo;
    float blendVideoMix;

    float video2Mix;
    float video2Available;
    int video2BlendMode;

    int blendModeProcedural;
    int blendModeFeedback;
    float blendProceduralMix;
    float blendFeedbackMix;
    int enableBlending;

    float grainStrength;
    int enablePostGrain;

    float upscaleEnabled;
    int enablePostColorBalance;
    int enableColorGrading;
    int enableAnalog;
    int enableAudioReactive;

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

    int nleOutputWidth;
    int nleOutputHeight;
    float nleGrayscale;
    float nleBrightness;
    float nleContrast;
    float nleSaturation;
} ubo;

layout(set = 1, binding = 0) uniform sampler2D inputTex;

const float PI = 3.1415926535;

float hash21(vec2 p) {
    p = fract(p * vec2(443.897, 441.423));
    p += dot(p, p.yx + 19.19);
    return fract(p.x * p.y);
}

vec3 blurCross5(vec2 uv) {
    vec2 t = 1.0 / max(ubo.resolution, vec2(1.0));
    vec3 c = texture(inputTex, uv).rgb * 0.5;
    c += texture(inputTex, clamp(uv + t * vec2( 0.0, -1.0), 0.0, 1.0)).rgb * 0.25;
    c += texture(inputTex, clamp(uv + t * vec2(-1.0,  0.0), 0.0, 1.0)).rgb * 0.25;
    c += texture(inputTex, clamp(uv + t * vec2( 1.0,  0.0), 0.0, 1.0)).rgb * 0.25;
    c += texture(inputTex, clamp(uv + t * vec2( 0.0,  1.0), 0.0, 1.0)).rgb * 0.25;
    return c;
}

vec3 blurDirectional(vec2 uv, float angle) {
    vec2 dir = vec2(cos(angle), sin(angle));
    vec2 t = 1.0 / max(ubo.resolution, vec2(1.0));
    vec3 acc = texture(inputTex, uv).rgb;
    float wsum = 1.0;

    for (int i = 1; i <= 2; ++i) {
        float w = 1.0 / float(i + 1);
        vec2 o = dir * t * float(i);
        acc += texture(inputTex, clamp(uv + o, 0.0, 1.0)).rgb * w;
        acc += texture(inputTex, clamp(uv - o, 0.0, 1.0)).rgb * w;
        wsum += 2.0 * w;
    }

    return acc / wsum;
}

vec3 blurZoom(vec2 uv) {
    vec2 centered = uv * 2.0 - 1.0;
    vec3 acc = texture(inputTex, uv).rgb;
    float wsum = 1.0;

    for (int i = 1; i <= 2; ++i) {
        vec2 o = centered * float(i) * 0.05;
        acc += texture(inputTex, clamp(uv + o, 0.0, 1.0)).rgb;
        wsum += 1.0;
    }

    return acc / wsum;
}

vec3 unsharp(vec2 uv, vec3 color) {
    float s = clamp(ubo.unsharpMask + ubo.sharpenAmount, 0.0, 1.0);
    vec3 blurred = blurCross5(uv);
    return mix(color, clamp(color + (color - blurred) * s * 2.0, 0.0, 1.0), s);
}

vec3 applyLocalContrast(vec3 color) {
    if (ubo.localContrast <= 0.0001) return color;
    float l = dot(color, vec3(0.299, 0.587, 0.114));
    return clamp(color + (color - vec3(l)) * ubo.localContrast, 0.0, 1.0);
}

vec3 applyGrain(vec2 uv, vec3 color) {
    if (ubo.enablePostGrain == 0 || ubo.grainStrength <= 0.0001) return color;
    float g = hash21(uv * ubo.resolution + ubo.time * 60.0) - 0.5;
    return clamp(color + g * ubo.grainStrength * 0.08, 0.0, 1.0);
}

vec3 applyVignette(vec2 uv, vec3 color) {
    if (ubo.enablePostVignette == 0) return color;
    vec2 c = uv * 2.0 - 1.0;
    float r = length(c);
    float v = 1.0 - smoothstep(0.35, 1.0, r) * clamp(ubo.crtVignette, 0.0, 1.0);
    return color * v;
}

vec3 applyScanMask(vec2 uv, vec3 color) {
    if (ubo.enablePostScanMask == 0) return color;
    float freq = 240.0 * (ubo.resolution.y / 480.0);
    float s = 0.5 + 0.5 * sin(uv.y * PI * freq);
    float m = mix(1.0, s, clamp(ubo.crtScanlineIntensity, 0.0, 1.0));
    return color * m;
}

vec3 applyChromaticAberration(vec2 uv, vec3 color) {
    if (ubo.enablePostAberration == 0 || ubo.aberrationAmount <= 0.0001) return color;
    vec2 c = uv * 2.0 - 1.0;
    float r = dot(c, c);
    vec2 o = c * r * ubo.aberrationAmount * 0.003;
    float rr = texture(inputTex, clamp(uv + o, 0.0, 1.0)).r;
    float gg = texture(inputTex, clamp(uv, 0.0, 1.0)).g;
    float bb = texture(inputTex, clamp(uv - o, 0.0, 1.0)).b;
    return vec3(rr, gg, bb);
}

void main() {
    vec2 uv0 = uv;
    vec3 color = texture(inputTex, uv0).rgb;

    bool hasBlur = ubo.gaussianBlur > 0.0001 ||
                   ubo.directionalBlur > 0.0001 ||
                   ubo.zoomBlur > 0.0001 ||
                   ubo.motionBlur > 0.0001 ||
                   ubo.temporalBlur > 0.0001;

    if (ubo.enableBlurMotion == 1 && hasBlur) {
        float blurAmount = ubo.gaussianBlur;
        if (ubo.enableAudioReactive == 1) {
            blurAmount *= (1.0 + ubo.energy * ubo.audioBlurResponse);
        }
        if (blurAmount > 0.0001) {
            color = mix(color, blurCross5(uv0), clamp(blurAmount, 0.0, 1.0));
        }
        if (ubo.directionalBlur > 0.0001) {
            color = mix(color, blurDirectional(uv0, radians(ubo.directionalBlurAngle)), clamp(ubo.directionalBlur, 0.0, 1.0));
        }
        if (ubo.zoomBlur > 0.0001) {
            color = mix(color, blurZoom(uv0), clamp(ubo.zoomBlur, 0.0, 1.0));
        }
        if (ubo.motionBlur > 0.0001) {
            float a = ubo.time * 0.5;
            vec2 dir = normalize(vec2(sin(a), cos(a * 0.7)));
            color = mix(color, blurDirectional(uv0, atan(dir.y, dir.x)), clamp(ubo.motionBlur, 0.0, 1.0));
        }
        if (ubo.temporalBlur > 0.0001) {
            float j = hash21(uv0 + ubo.time) - 0.5;
            vec2 o = vec2(j) / max(ubo.resolution, vec2(1.0));
            color = mix(color, texture(inputTex, clamp(uv0 + o, 0.0, 1.0)).rgb, clamp(ubo.temporalBlur, 0.0, 1.0));
        }
    }

    if (ubo.enableSharpen == 1) {
        color = unsharp(uv0, color);
    }

    if (ubo.localContrast > 0.0001) {
        color = applyLocalContrast(color);
    }

    color = applyChromaticAberration(uv0, color);
    color = applyScanMask(uv0, color);
    color = applyVignette(uv0, color);
    color = applyGrain(uv0, color);

    outColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}