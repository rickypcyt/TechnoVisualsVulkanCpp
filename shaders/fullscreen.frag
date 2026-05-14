#version 450

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;

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

    // FXAAPassUBO
    int enableFXAA;
    float fxaaQualitySubpix;
    float fxaaQualityEdgeThreshold;
    float fxaaQualityEdgeThresholdMin;

    // GridPassUBO: Grid / Mirroring
    int enableGrid;
    int gridMode;
    int gridCount;
    int gridRows;
    int gridColumns;
} ubo;

layout(set = 0, binding = 1) uniform sampler2D inputTexture;

void main() {
    vec2 sampleUV = uv;

    // Apply grid/mirroring if enabled
    if (ubo.enableGrid == 1) {
        if (ubo.gridMode == 0) {
            // Vertical grid: split horizontally (side by side)
            if (ubo.gridCount > 1) {
                sampleUV.x = fract(uv.x * float(ubo.gridCount));
            }
        } else if (ubo.gridMode == 1) {
            // Horizontal grid: split vertically (stacked)
            if (ubo.gridCount > 1) {
                sampleUV.y = fract(uv.y * float(ubo.gridCount));
            }
        } else if (ubo.gridMode == 2) {
            // Matrix grid: 2D grid with rows and columns
            if (ubo.gridRows > 0 && ubo.gridColumns > 0) {
                sampleUV.x = fract(uv.x * float(ubo.gridColumns));
                sampleUV.y = fract(uv.y * float(ubo.gridRows));
            }
        }
    }

    fragColor = texture(inputTexture, sampleUV);
}
