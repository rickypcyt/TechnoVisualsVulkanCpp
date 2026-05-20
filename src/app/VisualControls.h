#pragma once
#include <glm/glm.hpp>

// VisualControls - pure data struct for visual effect parameters
// This struct contains all the adjustable parameters for the video effects
struct VisualControls {
    float animationSpeed = 1.0f;
    float animationTargetSeconds = 1.0f;
    float tempo = 1.0f;
    float energy = 0.5f;
    float bass = 0.3f;
    float mid = 0.3f;
    float high = 0.3f;
    float colorBlend = 0.5f;
    glm::vec4 primaryColor = glm::vec4(0.9f, 0.4f, 0.1f, 1.0f);
    glm::vec4 secondaryColor = glm::vec4(0.1f, 0.5f, 0.8f, 1.0f);
    // Auto randomize colors with smooth interpolation
    bool autoRandomizeColors = false;
    float colorRandomizeInterval = 1.0f;
    float colorRandomizeElapsed = 0.0f;
    glm::vec4 primaryColorTarget = glm::vec4(0.9f, 0.4f, 0.1f, 1.0f);
    glm::vec4 secondaryColorTarget = glm::vec4(0.1f, 0.5f, 0.8f, 1.0f);
    int activeMode = 1;
    float videoMix = 1.0f;
    float videoPlaybackRate = 1.0f;
    float videoDecodeOversample = 1.0f;
    bool autoScaleVideo = true;

    // Dual video source controls
    bool enableDualVideo = false;
    float video2Mix = 0.0f;
    int video2BlendMode = 0;  // 0 = mix, 1 = add, 2 = multiply, 3 = screen, 4 = difference
    int selectedVideo2Asset = 0;
    std::string selectedVideo2Folder = "";
    float video2PlaybackRate = 1.0f;
    std::string selectedVideoFolder = "";
    float grayscaleAmount = 0.0f;
    float sharpenAmount = 0.35f;
    bool upscaleEnabled = true;
    float loopBlendSeconds = 0.0f;
    int forcedFpsIndex = 0;
    float crtCurvature = 0.15f;
    float crtHorizontalCurvature = 0.15f;
    float crtScanlineIntensity = 0.35f;
    float crtMaskIntensity = 0.35f;
    float crtVignette = 0.55f;
    float crtFishEye = 0.0f;
    float bloomIntensity = 0.45f;
    float bloomThreshold = 0.7f;
    float aberrationAmount = 0.02f;
    float grainStrength = 0.15f;
    float bendAmount = 0.0f;
    float glitchAmount = 0.0f;
    bool randomVideoStart = false;
    float randomJumpInterval = 5.0f;
    bool enableRandomJumpInterval = false;
    glm::vec3 colorBalance = glm::vec3(1.0f);
    bool enablePostCrtCurvature = true;
    bool enablePostScanMask = true;
    bool enablePostVignette = true;
    bool enablePostFishEye = true;
    bool enablePostBloom = true;
    bool enablePostAberration = true;
    bool enablePostGrain = true;
    bool enablePostBend = true;
    bool enablePostGlitch = true;
    bool enablePostColorBalance = true;
    // Color grading core
    float gradeBrightness = 0.0f;
    float gradeContrast = 1.0f;
    float gradeSaturation = 1.0f;
    float gradeHueShift = 0.0f;
    float gradeGamma = 1.0f;
    int colorLUTIndex = 0;
    float splitToneBalance = 0.5f;
    glm::vec3 splitToneShadows = glm::vec3(0.85f, 0.4f, 0.3f);
    glm::vec3 splitToneHighlights = glm::vec3(1.1f, 1.0f, 0.85f);
    // Temporal feedback
    float feedbackAmount = 0.4f;
    float trailStrength = 0.35f;
    float temporalAccumulation = 0.5f;
    float feedbackDecay = 0.25f;
    float recursiveBlend = 0.4f;
    // Spatial distortion
    float uvWarpStrength = 0.0f;
    float rippleStrength = 0.0f;
    float rippleFrequency = 1.0f;
    float swirlStrength = 0.0f;
    float displacementAmount = 0.0f;
    float kaleidoSegments = 6.0f;
    float tunnelDepth = 0.0f;
    float tunnelCurvature = 0.0f;
    // Blur / motion
    float gaussianBlur = 0.0f;
    float directionalBlur = 0.0f;
    float directionalBlurAngle = 0.0f;
    float zoomBlur = 0.0f;
    float motionBlur = 0.0f;
    float temporalBlur = 0.0f;
    // Sharpening / detail
    float unsharpMask = 0.0f;
    float casAmount = 0.0f;
    float localContrast = 0.0f;
    // Glitch / corruption
    float glitchDatamosh = 0.0f;
    float glitchRGBSplit = 0.0f;
    float glitchScanlineBreak = 0.0f;
    float glitchJitter = 0.0f;
    float glitchTearing = 0.0f;
    float glitchPixelSort = 0.0f;
    float glitchBufferCorruption = 0.0f;
    // Compositing / blending
    int blendModeProcedural = 0;
    int blendModeVideo = 1;
    int blendModeFeedback = 2;
    float blendProceduralMix = 1.0f;
    float blendVideoMix = 1.0f;
    float blendFeedbackMix = 0.5f;
    // Analog / CRT booster
    float analogScanlineFocus = 0.5f;
    float analogMaskBalance = 0.5f;
    float analogNoise = 0.2f;
    float analogBloom = 0.3f;
    float vhsDistortion = 0.0f;
    float analogChromaticAberration = 0.02f;
    bool enableColorGrading = true;
    bool enableFeedback = true;
    bool enableDistortion = true;
    bool enableBlurMotion = true;
    bool enableSharpen = true;
    bool enableGlitch = true;
    bool enableBlending = true;
    bool enableAnalog = true;
    bool enableAudioReactive = true;
    bool enableTemporal = true;
    // Audio reactivity
    float audioWarpResponse = 0.8f;
    float audioFeedbackResponse = 0.8f;
    float audioBlurResponse = 0.4f;
    float audioColorResponse = 0.6f;
    float audioGlitchResponse = 0.5f;
    float audioBeatSync = 1.0f;
    float audioLfoRate = 0.5f;
    // Temporal processing
    float temporalInterpolation = 0.0f;
    float temporalBlendStrength = 0.0f;
    float slowMotionFactor = 1.0f;
    float frameAccumulation = 0.0f;
    // Extra effects (Pixelate, Strobe, Threshold, Slow Zoom)
    float pixelateAmount = 0.0f;
    float strobeSpeed = 0.0f;
    float thresholdLevel = 0.5f;
    float slowZoomAmount = 0.0f;
    bool enableEdgeDetect = false;
    float edgeStrength = 1.0f;
    float edgeThreshold = 0.2f;
    float edgeBlend = 1.0f;
    glm::vec3 edgeColor = glm::vec3(1.0f);
    bool enablePixelate = false;
    bool enableStrobe = false;
    bool enableThreshold = false;
    bool enableSlowZoom = false;
    // VJAY EXTRA nuevos
    bool  enableMirror      = false;
    bool  enableInvert      = false;
    bool  enablePosterize   = false;
    bool  enableInfrared    = false;
    bool  enableZoomPulse   = false;
    bool  enableRGBShift    = false;
    float mirrorAmount      = 0.0f;
    float posterizeLevels   = 4.0f;
    float zoomPulseAmount   = 0.5f;
    float rgbShiftAmount    = 0.02f;
    // FXAA (Fast Approximate Anti-Aliasing)
    bool  enableFXAA             = true;
    float fxaaQualitySubpix      = 0.75f;
    float fxaaQualityEdgeThreshold = 0.125f;
    float fxaaQualityEdgeThresholdMin = 0.0625f;
    // Grid / Mirroring
    bool  enableGrid             = false;
    int   gridMode              = 0;  // 0 = vertical, 1 = horizontal, 2 = matrix
    int   gridCount             = 2;  // Number of grid cells (for vertical/horizontal)
    int   gridRows              = 2;  // Number of rows (for matrix mode)
    int   gridColumns           = 2;  // Number of columns (for matrix mode)
    bool  gridMirrorCells       = false; // Mirror alternate cells so edges line up seamlessly

    // Camera movement (2D layer camera)
    float cameraZoom            = 1.0f;   // 1.0 = no zoom, >1 = zoom in, <1 = zoom out
    float cameraPanX            = 0.0f;   // Horizontal offset (-1 to 1)
    float cameraPanY            = 0.0f;   // Vertical offset (-1 to 1)
    float cameraRotation        = 0.0f;   // Rotation in radians
    bool  enableCameraMovement  = true;   // Master toggle
};
