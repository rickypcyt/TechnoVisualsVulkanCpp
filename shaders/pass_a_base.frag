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

    float cameraZoom;
    float cameraPanX;
    float cameraPanY;
    float cameraRotation;
    int enableCameraMovement;
} ubo;

layout(set = 1, binding = 0) uniform sampler2D videoTex;
layout(set = 1, binding = 1) uniform sampler2D videoTexPrev;

const float PI = 3.1415926535;

float luminance(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

vec2 applyCamera(vec2 uv) {
    if (ubo.enableCameraMovement == 0) return uv;
    vec2 p = uv - 0.5;
    float c = cos(ubo.cameraRotation);
    float s = sin(ubo.cameraRotation);
    p = vec2(c * p.x - s * p.y, s * p.x + c * p.y);
    p /= max(ubo.cameraZoom, 0.0001);
    p += vec2(ubo.cameraPanX, ubo.cameraPanY);
    return p + 0.5;
}

vec3 sampleVideo(vec2 p) {
    return texture(videoTex, clamp(p, 0.0, 1.0)).rgb;
}

vec3 sampleBilinear(vec2 p) {
    return sampleVideo(p);
}

vec3 sharpen3x3(vec2 p, float amount) {
    vec2 t = 1.0 / max(ubo.resolution, vec2(1.0));
    vec3 c  = sampleVideo(p);
    vec3 n  = sampleVideo(p + vec2( 0.0,  1.0) * t);
    vec3 s  = sampleVideo(p + vec2( 0.0, -1.0) * t);
    vec3 e  = sampleVideo(p + vec2( 1.0,  0.0) * t);
    vec3 w  = sampleVideo(p + vec2(-1.0,  0.0) * t);
    vec3 lap = (n + s + e + w) - 4.0 * c;
    return clamp(c - lap * amount, 0.0, 1.0);
}

vec3 renderMode0(vec2 st) {
    return mix(ubo.primaryColor.rgb, ubo.secondaryColor.rgb, clamp(ubo.colorBlend, 0.0, 1.0));
}

vec3 renderMode1(vec2 st) {
    vec2 centered = st - 0.5;
    float r = length(centered);
    float a = atan(centered.y, centered.x);

    float spin = ubo.time * (1.0 + ubo.bass * 4.0 + ubo.energy * 2.0);
    float arms = 3.0 + floor(clamp(ubo.mid, 0.0, 1.0) * 6.0);
    float phase = a * arms + r * 25.0 - spin * 3.0;

    float thickness = 0.15 + ubo.energy * 0.25;
    float spiral = smoothstep(thickness, 0.0, abs(sin(phase)));
    float ring = smoothstep(0.05, 0.0, abs(r - (0.2 + ubo.bass * 0.3) - fract(ubo.time * 0.5) * 0.4));

    vec3 spiralColor = mix(ubo.primaryColor.rgb, ubo.secondaryColor.rgb,
                           0.5 + 0.5 * sin(ubo.time * 2.0 + r * 15.0));
    vec3 ringColor = ubo.secondaryColor.rgb * (0.5 + ubo.high * 0.8);

    vec3 color = vec3(0.0);
    color = mix(color, ringColor, ring * 0.6);
    color = mix(color, spiralColor, spiral);

    float glow = exp(-r * 8.0) * (0.4 + ubo.bass * 0.6);
    color += ubo.primaryColor.rgb * glow;
    color *= 1.0 - smoothstep(0.48, 0.7, r);

    return clamp(color, 0.0, 1.0);
}

vec4 dispatchMode(int m, vec2 st) {
    if (m == 0) return vec4(renderMode0(st), 1.0);
    if (m == 1) return vec4(renderMode1(st), 1.0);
    return vec4(0.0);
}

void main() {
    vec2 procUV = applyCamera(uv);
    vec3 videoColor = sampleVideo(uv);
    vec4 procColor = dispatchMode(ubo.mode, procUV);

    vec3 color = mix(procColor.rgb, videoColor, clamp(ubo.videoMix * ubo.videoAvailable, 0.0, 1.0));

    if (ubo.enableSharpen == 1 && ubo.sharpenAmount > 0.0001) {
        color = mix(color, sharpen3x3(uv, ubo.sharpenAmount), clamp(ubo.casAmount + ubo.unsharpMask, 0.0, 1.0));
    }

    if (ubo.enablePostGrain == 1 && ubo.grainStrength > 0.0001) {
        float g = fract(sin(dot(uv * ubo.resolution + ubo.time * 60.0, vec2(12.9898, 78.233))) * 43758.5453) - 0.5;
        color += g * ubo.grainStrength * 0.08;
    }

    if (ubo.grayscaleAmount > 0.0001) {
        float l = luminance(color);
        color = mix(color, vec3(l), clamp(ubo.grayscaleAmount, 0.0, 1.0));
    }

    if (ubo.enablePostColorBalance == 1) {
        color *= ubo.colorBalance;
    }

    outColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}