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

vec3 rgb2yiq(vec3 c) {
    return mat3(
        0.299,     0.587,      0.114,
        0.595716, -0.274453, -0.321263,
        0.211456, -0.522591,  0.311135
    ) * c;
}

vec3 yiq2rgb(vec3 c) {
    return mat3(
        1.0,  0.9563,  0.6210,
        1.0, -0.2721, -0.6474,
        1.0, -1.1070,  1.7046
    ) * c;
}

vec3 hueShift(vec3 color, float degrees) {
    if (abs(degrees) <= 0.0001) return color;
    vec3 yiq = rgb2yiq(color);
    float a = radians(degrees);
    float c = cos(a);
    float s = sin(a);
    vec2 iq = yiq.yz;
    yiq.y = iq.x * c - iq.y * s;
    yiq.z = iq.x * s + iq.y * c;
    return clamp(yiq2rgb(yiq), 0.0, 2.0);
}

vec3 applyLUT(vec3 color, int index) {
    if (index == 1) {
        color = pow(max(color, vec3(0.0)), vec3(0.85, 0.95, 1.05));
    } else if (index == 2) {
        color = vec3(color.r * 1.2, color.g * 0.6, color.b * 1.4);
    } else if (index == 3) {
        float gray = dot(color, vec3(0.333));
        color = mix(vec3(gray * 0.9), vec3(gray * 1.1), vec3(0.8, 0.9, 1.1));
    } else if (index == 4) {
        color = vec3(color.r * 1.3, color.g * 0.9, color.b * 0.7);
    } else if (index == 5) {
        color = mix(color, vec3(color.g, color.b, color.r), 0.25);
    }
    return clamp(color, 0.0, 2.0);
}

vec3 applySplitTone(vec3 color) {
    float balance = clamp(ubo.splitToneBalance, 0.0, 1.0);
    if (balance <= 0.0001) return color;
    float lum = dot(color, vec3(0.299, 0.587, 0.114));
    vec3 tone = mix(ubo.splitToneShadows, ubo.splitToneHighlights, smoothstep(0.15, 0.85, lum));
    return mix(color, color * tone, balance);
}

void main() {
    vec3 color = texture(inputTex, uv).rgb;

    if (ubo.grayscaleAmount > 0.0001) {
        float gray = dot(color, vec3(0.299, 0.587, 0.114));
        color = mix(color, vec3(gray), clamp(ubo.grayscaleAmount, 0.0, 1.0));
    }

    if (ubo.enableColorGrading == 1) {
        color += ubo.gradeBrightness;
        color = (color - 0.5) * ubo.gradeContrast + 0.5;

        float lum = dot(color, vec3(0.299, 0.587, 0.114));
        color = mix(vec3(lum), color, ubo.gradeSaturation);

        float audioResponse = clamp(ubo.energy * 0.3, 0.0, 1.0);
        color = hueShift(color, ubo.gradeHueShift + audioResponse * 45.0);

        color = pow(max(color, vec3(0.0)), vec3(1.0 / max(ubo.gradeGamma, 0.05)));
        color = applyLUT(color, ubo.colorLUTIndex);
        color = applySplitTone(color);
    }

    if (ubo.enableThreshold == 1 && ubo.thresholdLevel > 0.0001) {
        float lum = dot(color, vec3(0.299, 0.587, 0.114));
        float t = clamp(ubo.thresholdLevel, 0.0, 1.0);
        color = mix(vec3(lum), vec3(step(t, lum)), 0.8);
    }

    if (ubo.enablePostColorBalance == 1) {
        color *= ubo.colorBalance;
    }

    outColor = vec4(clamp(color, 0.0, 2.0), 1.0);
}