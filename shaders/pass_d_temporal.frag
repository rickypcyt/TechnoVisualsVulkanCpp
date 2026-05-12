#version 450

// PASS D — VJAY BASICS LAYER: Spatial VJAY (sin UV warp)
// Responsibilities: ripple, swirl, displacement, kaleidoscope, tunnel (depth/curvature)
// CAPA 2 - VJAY BASICS (medio): Efectos VJAY sobre BASE

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

layout(set = 1, binding = 0) uniform sampler2D inputTex;
layout(set = 1, binding = 1) uniform sampler2D prevFrameTex;

void main() {
    vec2 centered = uv * 2.0 - 1.0;
    vec3 color = texture(inputTex, uv).rgb;

    // Only apply temporal effects if enabled
    if (ubo.enableFeedback == 0 && ubo.enableTemporal == 0) {
        outColor = vec4(color, 1.0);
        return;
    }

    float amount = clamp(ubo.feedbackAmount * (1.0 + ubo.energy * 0.5), 0.0, 1.0);

    if (amount <= 0.0001 && ubo.trailStrength <= 0.0001 && ubo.temporalAccumulation <= 0.0001) {
        outColor = vec4(color, 1.0);
        return;
    }

    vec3 accum = color;
    float decay = clamp(1.0 - ubo.feedbackDecay, 0.0, 1.0);

    // Spatial feedback with trails
    if (ubo.enableFeedback == 1 && (ubo.trailStrength > 0.0001 || ubo.temporalAccumulation > 0.0001)) {
        for (int i = 1; i <= 3; ++i) {
            float t = float(i) / 3.0;

            vec2 offset = vec2(0.0);
            if (ubo.trailStrength > 0.0001) {
                offset += centered * t * ubo.trailStrength * 0.15;
            }
            if (ubo.temporalAccumulation > 0.0001) {
                offset += vec2(0.0, t * 0.03 * ubo.temporalAccumulation);
            }

            if (length(offset) < 0.0001) {
                continue;
            }

            vec2 offsetUV = clamp(uv - offset, 0.0, 1.0);
            vec3 sampleColor = texture(inputTex, offsetUV).rgb;

            float mixStrength = amount * (1.0 - t * 0.5) * decay;
            accum = mix(accum, sampleColor, mixStrength);
        }
    }

    // Recursive blend
    if (ubo.enableFeedback == 1 && ubo.recursiveBlend > 0.0001) {
        vec3 prevColor = texture(prevFrameTex, uv).rgb;
        accum = mix(accum, prevColor, ubo.recursiveBlend * 0.3);
    }

    // Frame accumulation (for long-exposure effects)
    if (ubo.enableTemporal == 1 && ubo.frameAccumulation > 0.0001) {
        vec3 prevColor = texture(prevFrameTex, uv).rgb;
        accum = mix(accum, prevColor, ubo.frameAccumulation * 0.5);
    }

    outColor = vec4(accum, 1.0);
}
