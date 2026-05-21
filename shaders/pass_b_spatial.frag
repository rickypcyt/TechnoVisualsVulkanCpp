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

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(a, b, u.x) + (c - a) * u.y * (1.0 - u.x) + (d - b) * u.x * u.y;
}

float fbm(vec2 p) {
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 2; ++i) {
        v += noise(p) * a;
        p *= 2.0;
        a *= 0.5;
    }
    return v;
}

vec2 safeSampleUV(vec2 p) {
    return clamp(p, 0.0, 1.0);
}

vec2 applyCurvature(vec2 p) {
    if (ubo.enablePostCrtCurvature == 0) return p;

    vec2 c = p * 2.0 - 1.0;
    float ky = clamp(ubo.crtCurvature, 0.0, 0.8);
    float kx = clamp(ubo.crtHorizontalCurvature, 0.0, 0.8);

    if (kx > 0.0001) c.x /= (1.0 + kx * c.x * c.x);
    if (ky > 0.0001) c.y /= (1.0 + ky * c.y * c.y);

    return c * 0.5 + 0.5;
}

vec2 applyFishEye(vec2 p) {
    if (ubo.enablePostFishEye == 0 || abs(ubo.crtFishEye) < 0.0001) return p;

    vec2 c = p * 2.0 - 1.0;
    float r = length(c);
    c *= 1.0 + ubo.crtFishEye * r * r;
    return c * 0.5 + 0.5;
}

void main() {
    float timeScale = max(ubo.slowMotionFactor, 0.1);
    float t = ubo.time / timeScale;
    vec2 p = uv;
    vec2 centered = uv * 2.0 - 1.0;

    if (ubo.enableDistortion == 1) {
        float warpMask = clamp(ubo.uvWarpStrength, 0.0, 1.0);
        if (warpMask > 0.0001) {
            vec2 w = sin((p.yx + t) * 8.0) * warpMask * 0.01;
            p += w * (1.0 + ubo.energy * 0.5);
        }

        if (ubo.rippleStrength > 0.0001) {
            float r = length(centered);
            float wave = sin(r * max(ubo.rippleFrequency, 0.1) * 12.0 - t * 4.0);
            p += normalize(centered + 0.0001) * wave * ubo.rippleStrength * 0.01;
        }

        if (abs(ubo.swirlStrength) > 0.0001) {
            float a = ubo.swirlStrength * length(centered) * 5.0;
            float c = cos(a);
            float s = sin(a);
            vec2 q = vec2(centered.x * c - centered.y * s, centered.x * s + centered.y * c);
            p = q * 0.5 + 0.5;
        }

        if (ubo.displacementAmount > 0.0001) {
            vec2 n = vec2(fbm(centered * 5.0 + t), fbm(centered * 7.0 - t));
            p += (n - 0.5) * ubo.displacementAmount * 0.05;
        }

        if (ubo.tunnelDepth > 0.0001) {
            vec2 q = centered;
            float r = max(length(q), 0.0001);
            float d = pow(r, 1.0 - clamp(ubo.tunnelDepth, 0.0, 1.0));
            float c = 1.0 + ubo.tunnelCurvature * r * r;
            p = normalize(q) * d * c * 0.5 + 0.5;
        }
    }

    if (ubo.enablePostBend == 1 && ubo.bendAmount > 0.0001) {
        float chaos = sin((centered.x + centered.y) * 60.0 + t * 20.0);
        p += vec2(chaos, cos(t * 10.0 + centered.y * 50.0)) * ubo.bendAmount * 0.05;
    }

    p = applyCurvature(p);
    p = applyFishEye(p);

    if (ubo.enablePixelate == 1 && ubo.pixelateAmount > 0.0001) {
        float pixels = max(ubo.pixelateAmount * 100.0, 2.0);
        p = floor(p * pixels) / pixels;
    }

    if (ubo.enableSlowZoom == 1 && ubo.slowZoomAmount > 0.0001) {
        vec2 z = vec2(0.5);
        float zoomFactor = 1.0 - ubo.slowZoomAmount * 0.3;
        p = z + (p - z) * zoomFactor;
    }

    p = safeSampleUV(p);
    vec3 color = texture(inputTex, p).rgb;

    outColor = vec4(color, 1.0);
}