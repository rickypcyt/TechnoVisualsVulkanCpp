#pragma once

#include <glm/glm.hpp>

// ============================================================================
// DOMAIN-SPECIFIC UBO STRUCTS
// Cada struct representa un dominio visual independiente.
// Estos permiten que cada shader reciba solo los parámetros que necesita.
// ============================================================================

// --- FrameUBO: Estado global del frame ---
struct FrameUBO {
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
};

// --- ColorPassUBO: Color grading y corrección de color ---
struct ColorPassUBO {
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
};

// --- CRTPassUBO: Efectos CRT y analógicos ---
struct CRTPassUBO {
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
};

// --- GlitchPassUBO: Efectos glitch y distorsión digital ---
struct GlitchPassUBO {
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
};

// --- TemporalPassUBO: Efectos temporales y feedback ---
struct TemporalPassUBO {
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
};

// --- BloomPassUBO: Efectos de bloom ---
struct BloomPassUBO {
    alignas(4) float bloomIntensity;
    alignas(4) float bloomThreshold;
    alignas(4) int enablePostBloom;
};

// --- DistortionPassUBO: Distorsión espacial ---
struct DistortionPassUBO {
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
};

// --- BlurPassUBO: Efectos de blur ---
struct BlurPassUBO {
    alignas(4) float gaussianBlur;
    alignas(4) float directionalBlur;
    alignas(4) float directionalBlurAngle;
    alignas(4) float zoomBlur;
    alignas(4) float motionBlur;
    alignas(4) float temporalBlur;
    alignas(4) int enableBlurMotion;
};

// --- SharpenPassUBO: Efectos de sharpening ---
struct SharpenPassUBO {
    alignas(4) float unsharpMask;
    alignas(4) float casAmount;
    alignas(4) float localContrast;
    alignas(4) float sharpenAmount;
    alignas(4) int enableSharpen;
};

// --- VideoPassUBO: Parámetros de video ---
struct VideoPassUBO {
    alignas(4) float videoMix;
    alignas(4) float videoAvailable;
    alignas(4) int blendModeVideo;
    alignas(4) float blendVideoMix;
};

// --- BlendingPassUBO: Modos de blending y compositing ---
struct BlendingPassUBO {
    alignas(4) int blendModeProcedural;
    alignas(4) int blendModeFeedback;
    alignas(4) float blendProceduralMix;
    alignas(4) float blendFeedbackMix;
    alignas(4) int enableBlending;
};

// --- GrainPassUBO: Efectos de grano ---
struct GrainPassUBO {
    alignas(4) float grainStrength;
    alignas(4) int enablePostGrain;
};

// --- PostFXPassUBO: Post FX básicos ---
struct PostFXPassUBO {
    alignas(4) float upscaleEnabled;
    alignas(4) int enablePostColorBalance;
    alignas(4) int enableColorGrading;
    alignas(4) int enableAnalog;
    alignas(4) int enableAudioReactive;
};

// --- ExtraEffectsPassUBO: Efectos extra VJAY ---
struct ExtraEffectsPassUBO {
    alignas(4) float pixelateAmount;
    alignas(4) float strobeSpeed;
    alignas(4) float thresholdLevel;
    alignas(4) float slowZoomAmount;
    alignas(4) int enablePixelate;
    alignas(4) int enableStrobe;
    alignas(4) int enableThreshold;
    alignas(4) int enableSlowZoom;

    // Edge detection
    alignas(4) int enableEdgeDetect;
    alignas(4) float edgeStrength;
    alignas(4) float edgeThreshold;
    alignas(4) float edgeBlend;
    alignas(16) glm::vec3 edgeColor;

    // Extra avanzados
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
};

// --- NLEExportPassUBO: Parámetros de exportación NLE ---
struct NLEExportPassUBO {
    alignas(4) int nleOutputWidth;
    alignas(4) int nleOutputHeight;
    alignas(4) float nleGrayscale;
    alignas(4) float nleBrightness;
    alignas(4) float nleContrast;
    alignas(4) float nleSaturation;
};

// ============================================================================
// PACKED GlobalParamsUBO: Single unified UBO for all pass parameters
// This consolidates all domain-specific UBOs into a single descriptor binding
// to avoid descriptor set complexity and ensure consistency across all passes.
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
};

// ============================================================================
// LEGACY GlobalUBO: Agregado para compatibilidad con shaders existentes
// Este struct mantiene el layout original para no romper shaders actuales.
// FASE 1: Mantener esto mientras se migran shaders a structs específicos.
// ============================================================================
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

    // --- Enable/Disable flags for VJAY EXTRA ---
    alignas(4) int enablePixelate;
    alignas(4) int enableStrobe;
    alignas(4) int enableThreshold;
    alignas(4) int enableSlowZoom;

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

    // --- NLE export parameters ---
    alignas(4) int   nleOutputWidth;
    alignas(4) int   nleOutputHeight;
    alignas(4) float nleGrayscale;
    alignas(4) float nleBrightness;
    alignas(4) float nleContrast;
    alignas(4) float nleSaturation;

    // --- Efectos extra (VJAY EXTRA) ---
    alignas(4) float pixelateAmount;
    alignas(4) float strobeSpeed;
    alignas(4) float thresholdLevel;
    alignas(4) float slowZoomAmount;
    alignas(4) int   enableEdgeDetect;
    alignas(4) float edgeStrength;
    alignas(4) float edgeThreshold;
    alignas(4) float edgeBlend;
    alignas(16) glm::vec3 edgeColor;

    // --- VJAY EXTRA avanzados ---
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
};
