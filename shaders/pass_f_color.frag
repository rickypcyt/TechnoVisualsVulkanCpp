#version 450

// PASS F — Color pipeline
// Responsibilities: grading (contrast/sat/gamma/LUT), split tone, hue shift

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
    float thresholdLevel;
} ubo;

layout(set = 0, binding = 1) uniform sampler2D inputTex;

const float PI = 3.1415926535;

vec3 rgb2yiq(vec3 c) {
    mat3 m = mat3(
        0.299,     0.587,      0.114,
        0.595716, -0.274453, -0.321263,
        0.211456, -0.522591,  0.311135
    );
    return m * c;
}

vec3 yiq2rgb(vec3 c) {
    mat3 m = mat3(
        1.0,  0.9563,  0.6210,
        1.0, -0.2721, -0.6474,
        1.0, -1.1070,  1.7046
    );
    return m * c;
}

vec3 hueShift(vec3 color, float degrees) {
    if (abs(degrees) <= 0.0001) {
        return color;
    }
    vec3 yiq = rgb2yiq(color);
    float angle = radians(degrees);
    float cosA = cos(angle);
    float sinA = sin(angle);
    mat3 rot = mat3(
        1.0, 0.0, 0.0,
        0.0, cosA, -sinA,
        0.0, sinA,  cosA
    );
    yiq = rot * yiq;
    return clamp(yiq2rgb(yiq), 0.0, 2.0);
}

vec3 applyLUT(vec3 color, int index) {
    if (index == 1) {
        color = pow(color, vec3(0.85, 0.95, 1.05));
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
    if (balance <= 0.0001) {
        return color;
    }
    float lum = dot(color, vec3(0.299, 0.587, 0.114));
    vec3 tone = mix(ubo.splitToneShadows, ubo.splitToneHighlights, smoothstep(0.0, 1.0, lum));
    return mix(color, tone * color, balance);
}

void main() {
    vec3 color = texture(inputTex, uv).rgb;

    // Grayscale (apply first)
    if (ubo.grayscaleAmount > 0.0001) {
        float gray = dot(color, vec3(0.299, 0.587, 0.114));
        color = mix(color, vec3(gray), ubo.grayscaleAmount);
    }

    // Early return if color grading is disabled
    bool hasBrightness = abs(ubo.gradeBrightness) > 0.0001;
    bool hasContrast = abs(ubo.gradeContrast - 1.0) > 0.0001;
    bool hasSaturation = abs(ubo.gradeSaturation - 1.0) > 0.0001;
    bool hasHue = abs(ubo.gradeHueShift) > 0.0001;
    bool hasGamma = abs(ubo.gradeGamma - 1.0) > 0.0001;
    bool hasLUT = ubo.colorLUTIndex > 0;
    bool hasSplitTone = ubo.splitToneBalance > 0.0001;

    if (!hasBrightness && !hasContrast && !hasSaturation && !hasHue && !hasGamma && !hasLUT && !hasSplitTone) {
        color *= ubo.colorBalance;
        outColor = vec4(color, 1.0);
        return;
    }

    // Brightness
    color += ubo.gradeBrightness;
    
    // Contrast
    color = (color - 0.5) * ubo.gradeContrast + 0.5;
    
    // Saturation
    float lum = dot(color, vec3(0.299, 0.587, 0.114));
    color = mix(vec3(lum), color, ubo.gradeSaturation);
    
    // Hue shift with audio response
    float audioResponse = clamp(ubo.energy * 0.3, 0.0, 1.0);
    color = hueShift(color, ubo.gradeHueShift + audioResponse * 45.0);
    
    // Gamma
    color = pow(max(color, vec3(0.0)), vec3(1.0 / max(ubo.gradeGamma, 0.05)));
    
    // LUT
    color = applyLUT(color, ubo.colorLUTIndex);
    
    // Split tone
    color = applySplitTone(color);

    // Threshold effect (black & white threshold)
    if (ubo.thresholdLevel > 0.0001) {
        float lum = dot(color, vec3(0.299, 0.587, 0.114));
        float threshold = clamp(ubo.thresholdLevel, 0.0, 1.0);
        color = mix(vec3(lum), vec3(step(threshold, lum)), 0.8);
    }

    // Color balance
    color *= ubo.colorBalance;

    color = clamp(color, 0.0, 2.0);
    
    outColor = vec4(color, 1.0);
}
