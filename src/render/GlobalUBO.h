#pragma once

#include <glm/glm.hpp>

// Shader uniform buffer object.
// IMPORTANTE: el layout de este struct debe coincidir EXACTAMENTE
// con el uniform block en los shaders GLSL.
// Regla: no reordenar campos sin actualizar los shaders.
// Cada alignas() refleja las reglas de std140 de Vulkan/GLSL.
struct GlobalUBO {
    // --- Matrices de transformacion ---
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;

    // --- Resolucion ---
    alignas(16) glm::vec2 resolution;
    alignas(16) glm::vec2 videoResolution;

    // --- Tiempo y audio ---
    alignas(4) float time;
    alignas(4) float tempo;
    alignas(4) float energy;
    alignas(4) float bass;
    alignas(4) float mid;
    alignas(4) float high;

    // --- Colores procedurales ---
    alignas(16) glm::vec4 primaryColor;
    alignas(16) glm::vec4 secondaryColor;
    alignas(4)  float colorBlend;
    alignas(4)  int   mode;

    // --- Video ---
    alignas(4) float videoMix;
    alignas(4) float videoAvailable;

    // --- Post FX basicos ---
    alignas(4) float grayscaleAmount;
    alignas(4) float sharpenAmount;
    alignas(4) float upscaleEnabled;

    // --- Enable/Disable flags for post FX ---
    alignas(4) int enablePostCrtCurvature;
    alignas(4) int enablePostScanMask;
    alignas(4) int enablePostVignette;
    alignas(4) int enablePostFishEye;
    alignas(4) int enablePostBloom;
    alignas(4) int enablePostAberration;
    alignas(4) int enablePostGrain;
    alignas(4) int enablePostBend;
    alignas(4) int enablePostGlitch;
    alignas(4) int enablePostColorBalance;

    // --- Enable/Disable flags for VJAY BASICS ---
    alignas(4) int enableColorGrading;
    alignas(4) int enableFeedback;
    alignas(4) int enableDistortion;
    alignas(4) int enableBlurMotion;
    alignas(4) int enableSharpen;
    alignas(4) int enableGlitch;
    alignas(4) int enableBlending;
    alignas(4) int enableAnalog;
    alignas(4) int enableAudioReactive;
    alignas(4) int enableTemporal;

    // --- CRT ---
    alignas(4) float crtCurvature;
    alignas(4) float crtHorizontalCurvature;
    alignas(4) float crtScanlineIntensity;
    alignas(4) float crtMaskIntensity;
    alignas(4) float crtVignette;
    alignas(4) float crtFishEye;

    // --- Bloom ---
    alignas(4) float bloomIntensity;
    alignas(4) float bloomThreshold;

    // --- Aberracion / grano / bend / glitch ---
    alignas(4) float aberrationAmount;
    alignas(4) float grainStrength;
    alignas(4) float bendAmount;
    alignas(4) float glitchAmount;

    // --- Color grading ---
    alignas(16) glm::vec3 colorBalance;
    alignas(4)  float gradeBrightness;
    alignas(4)  float gradeContrast;
    alignas(4)  float gradeSaturation;
    alignas(4)  float gradeHueShift;
    alignas(4)  float gradeGamma;
    alignas(4)  int   colorLUTIndex;
    alignas(4)  float splitToneBalance;
    alignas(16) glm::vec3 splitToneShadows;
    alignas(16) glm::vec3 splitToneHighlights;

    // --- Feedback temporal ---
    alignas(4) float feedbackAmount;
    alignas(4) float trailStrength;
    alignas(4) float temporalAccumulation;
    alignas(4) float feedbackDecay;
    alignas(4) float recursiveBlend;

    // --- Distorsion espacial ---
    alignas(4) float uvWarpStrength;
    alignas(4) float rippleStrength;
    alignas(4) float rippleFrequency;
    alignas(4) float swirlStrength;
    alignas(4) float displacementAmount;
    alignas(4) float kaleidoSegments;
    alignas(4) float tunnelDepth;
    alignas(4) float tunnelCurvature;

    // --- Blur y motion ---
    alignas(4) float gaussianBlur;
    alignas(4) float directionalBlur;
    alignas(4) float directionalBlurAngle;
    alignas(4) float zoomBlur;
    alignas(4) float motionBlur;
    alignas(4) float temporalBlur;

    // --- Sharpening ---
    alignas(4) float unsharpMask;
    alignas(4) float casAmount;
    alignas(4) float localContrast;

    // --- Glitch detallado ---
    alignas(4) float glitchDatamosh;
    alignas(4) float glitchRGBSplit;
    alignas(4) float glitchScanlineBreak;
    alignas(4) float glitchJitter;
    alignas(4) float glitchTearing;
    alignas(4) float glitchPixelSort;
    alignas(4) float glitchBufferCorruption;

    // --- Blending / compositing ---
    alignas(4) int   blendModeProcedural;
    alignas(4) int   blendModeVideo;
    alignas(4) int   blendModeFeedback;
    alignas(4) float blendProceduralMix;
    alignas(4) float blendVideoMix;
    alignas(4) float blendFeedbackMix;

    // --- Analog / CRT avanzado ---
    alignas(4) float analogScanlineFocus;
    alignas(4) float analogMaskBalance;

    // --- Temporal ---
    alignas(4) float frameAccumulation;
    alignas(4) float slowMotionFactor;
    alignas(4) float temporalInterpolation;

    // --- Efectos extra (VJAY EXTRA) ---
    alignas(4) int enablePixelate;
    alignas(4) int enableStrobe;
    alignas(4) int enableThreshold;
    alignas(4) int enableSlowZoom;
    alignas(4) int enableMirror;
    alignas(4) int enableInvert;
    alignas(4) int enablePosterize;
    alignas(4) int enableInfrared;
    alignas(4) int enableZoomPulse;
    alignas(4) int enableRGBShift;
    alignas(4) float pixelateAmount;
    alignas(4) float strobeSpeed;
    alignas(4) float thresholdLevel;
    alignas(4) float slowZoomAmount;
    alignas(4) float mirrorAmount;
    alignas(4) float posterizeLevels;
    alignas(4) float zoomPulseAmount;
    alignas(4) float rgbShiftAmount;
};
