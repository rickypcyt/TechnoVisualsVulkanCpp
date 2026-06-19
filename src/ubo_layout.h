#pragma once

#include <glm/glm.hpp>

// ============================================================================
// GlobalParamsUBO layout
// ---------------------------------------------------------------------------
//  * Single source of truth for CPU + GPU uniform layout.
//  * This file is consumed directly by C++ and parsed by tools/gen_ubo_glsl.py
//    to generate the GLSL uniform block (shared_ubo.glsl).
//  * KEEP TYPES SIMPLE: only use POD types and glm vectors/matrices.
//  * Order, types and explicit alignments must stay in sync with shaders.
// ============================================================================
struct GlobalParamsUBO {
    // FrameUBO
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
    alignas(16) glm::vec2 resolution;
    alignas(16) glm::vec2 videoResolution;
    alignas(4) float time;
    alignas(4) float tempo;
    alignas(4) float energy;
    alignas(4) float bass;
    alignas(4) float mid;
    alignas(4) float high;
    alignas(4) float audioReactiveDrive;

    // Audio reactivity tuning
    alignas(4) float audioWarpResponse;
    alignas(4) float audioFeedbackResponse;
    alignas(4) float audioBlurResponse;
    alignas(4) float audioColorResponse;
    alignas(4) float audioGlitchResponse;
    alignas(4) float audioBeatSync;
    alignas(4) float audioLfoRate;

    // ColorPassUBO
    alignas(16) glm::vec4 primaryColor;
    alignas(16) glm::vec4 secondaryColor;
    alignas(4) float colorBlend;
    alignas(4) int mode;
    alignas(16) glm::vec3 colorBalance;
    alignas(4) float gradeBrightness;
    alignas(4) float gradeContrast;
    alignas(4) float gradeSaturation;
    alignas(4) float gradeHueShift;
    alignas(4) float gradeGamma;
    alignas(4) int colorLUTIndex;
    alignas(4) float splitToneBalance;
    alignas(16) glm::vec3 splitToneShadows;
    alignas(16) glm::vec3 splitToneHighlights;
    alignas(4) float grayscaleAmount;

    // CRTPassUBO
    alignas(4) float crtCurvature;
    alignas(4) float crtHorizontalCurvature;
    alignas(4) float crtScanlineIntensity;
    alignas(4) float crtMaskIntensity;
    alignas(4) float crtVignette;
    alignas(4) float crtFishEye;
    alignas(4) float analogScanlineFocus;
    alignas(4) float analogMaskBalance;
    alignas(4) int enablePostCrtCurvature;
    alignas(4) int enablePostScanMask;
    alignas(4) int enablePostVignette;
    alignas(4) int enablePostFishEye;

    // GlitchPassUBO
    alignas(4) float glitchAmount;
    alignas(4) float glitchDatamosh;
    alignas(4) float glitchRGBSplit;
    alignas(4) float glitchScanlineBreak;
    alignas(4) float glitchJitter;
    alignas(4) float glitchTearing;
    alignas(4) float glitchPixelSort;
    alignas(4) float glitchBufferCorruption;
    alignas(4) float aberrationAmount;
    alignas(4) int enablePostGlitch;
    alignas(4) int enablePostAberration;

    // TemporalPassUBO
    alignas(4) float feedbackAmount;
    alignas(4) float trailStrength;
    alignas(4) float temporalAccumulation;
    alignas(4) float feedbackDecay;
    alignas(4) float recursiveBlend;
    alignas(4) float frameAccumulation;
    alignas(4) float slowMotionFactor;
    alignas(4) float temporalInterpolation;
    alignas(4) int enableFeedback;
    alignas(4) int enableTemporal;

    // BloomPassUBO
    alignas(4) float bloomIntensity;
    alignas(4) float bloomThreshold;
    alignas(4) int enablePostBloom;

    // DistortionPassUBO
    alignas(4) float uvWarpStrength;
    alignas(4) float rippleStrength;
    alignas(4) float rippleFrequency;
    alignas(4) float swirlStrength;
    alignas(4) float displacementAmount;
    alignas(4) float kaleidoSegments;
    alignas(4) float tunnelDepth;
    alignas(4) float tunnelCurvature;
    alignas(4) float bendAmount;
    alignas(4) int enableDistortion;
    alignas(4) int enablePostBend;

    // BlurPassUBO
    alignas(4) float gaussianBlur;
    alignas(4) float directionalBlur;
    alignas(4) float directionalBlurAngle;
    alignas(4) float zoomBlur;
    alignas(4) float motionBlur;
    alignas(4) float temporalBlur;
    alignas(4) int enableBlurMotion;

    // SharpenPassUBO
    alignas(4) float unsharpMask;
    alignas(4) float casAmount;
    alignas(4) float localContrast;
    alignas(4) float sharpenAmount;
    alignas(4) int enableSharpen;

    // VideoPassUBO
    alignas(4) float videoMix;
    alignas(4) float videoAvailable;
    alignas(4) int blendModeVideo;
    alignas(4) float blendVideoMix;
    alignas(4) float video2Mix;
    alignas(4) float video2Available;
    alignas(4) int video2BlendMode;
    alignas(4) float video3Mix;
    alignas(4) float video3Available;
    alignas(4) int video3BlendMode;

    // BlendingPassUBO
    alignas(4) int blendModeProcedural;
    alignas(4) int blendModeFeedback;
    alignas(4) float blendProceduralMix;
    alignas(4) float blendFeedbackMix;
    alignas(4) int enableBlending;

    // GrainPassUBO
    alignas(4) float grainStrength;
    alignas(4) int enablePostGrain;

    // PostFXPassUBO
    alignas(4) float upscaleEnabled;
    alignas(4) int enablePostColorBalance;
    alignas(4) int enableColorGrading;
    alignas(4) int enableAnalog;
    alignas(4) int enableAudioReactive;

    // ExtraEffectsPassUBO
    alignas(4) float pixelateAmount;
    alignas(4) float strobeSpeed;
    alignas(4) float thresholdLevel;
    alignas(4) float slowZoomAmount;
    alignas(4) int enablePixelate;
    alignas(4) int enableStrobe;
    alignas(4) int enableThreshold;
    alignas(4) int enableSlowZoom;
    alignas(4) int enableEdgeDetect;
    alignas(4) float edgeStrength;
    alignas(4) float edgeThreshold;
    alignas(4) float edgeBlend;
    alignas(16) glm::vec3 edgeColor;
    alignas(4) int enableMirror;
    alignas(4) int enableInvert;
    alignas(4) int enablePosterize;
    alignas(4) int enableInfrared;
    alignas(4) int enableZoomPulse;
    alignas(4) int enableRGBShift;
    alignas(4) float mirrorAmount;
    alignas(4) float posterizeLevels;
    alignas(4) float zoomPulseAmount;
    alignas(4) float rgbShiftAmount;

    // NLEExportPassUBO
    alignas(4) int nleOutputWidth;
    alignas(4) int nleOutputHeight;
    alignas(4) float nleGrayscale;
    alignas(4) float nleBrightness;
    alignas(4) float nleContrast;
    alignas(4) float nleSaturation;

    // FXAAPassUBO
    alignas(4) int enableFXAA;
    alignas(4) float fxaaQualitySubpix;
    alignas(4) float fxaaQualityEdgeThreshold;
    alignas(4) float fxaaQualityEdgeThresholdMin;

    // CameraMovementPassUBO
    alignas(4) float cameraZoom;
    alignas(4) float cameraPanX;
    alignas(4) float cameraPanY;
    alignas(4) float cameraRotation;
    alignas(4) int enableCameraMovement;

    // GridPassUBO
    alignas(4) int enableGrid;
    alignas(4) int gridMode;
    alignas(4) int gridCount;
    alignas(4) int gridRows;
    alignas(4) int gridColumns;
    alignas(4) int gridMirrorCells;
    alignas(4) int gridShowLines;
    alignas(4) float gridLineWidth;
    alignas(4) float gridLineIntensity;
    alignas(16) glm::vec3 gridLineColor;

    // Final RGB overlay
    alignas(16) glm::vec3 rgbOverlay;
    alignas(4) int enableRgbOverlay;

    // Master brightness (independent from color grading)
    alignas(4) float masterBrightness;

    // Video transition crossfade (0.0 = old video, 1.0 = new video)
    alignas(4) float transitionProgress;
};
