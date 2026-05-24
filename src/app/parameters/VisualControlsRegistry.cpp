#include "VisualControlsRegistry.h"
#include "../VisualControls.h"

void VisualControlsRegistry::build(ParameterRegistry& r, VisualControls& c) {
    // CORE
    r.registerFloat("animationSpeed", &c.animationSpeed, 0.0f, 10.0f);
    r.registerFloat("animationTargetSeconds", &c.animationTargetSeconds, 0.0f, 10.0f);
    r.registerFloat("tempo", &c.tempo, 20.0f, 300.0f);
    r.registerBool("enableTempoLfo", &c.enableTempoLfo);
    r.registerFloat("tempoLfoSpeed", &c.tempoLfoSpeed, 0.0f, 10.0f);
    r.registerFloat("tempoLfoDepth", &c.tempoLfoDepth, 0.0f, 2.0f);
    r.registerFloat("tempoLfoPhase", &c.tempoLfoPhase, 0.0f, 6.28f);
    r.registerFloat("energy", &c.energy, 0.0f, 1.0f);
    r.registerFloat("bass", &c.bass, 0.0f, 1.0f);
    r.registerFloat("mid", &c.mid, 0.0f, 1.0f);
    r.registerFloat("high", &c.high, 0.0f, 1.0f);

    // COLORS
    r.registerFloat("colorBlend", &c.colorBlend, 0.0f, 1.0f);
    r.registerVec4("primaryColor", &c.primaryColor);
    r.registerVec4("secondaryColor", &c.secondaryColor);
    r.registerBool("autoRandomizeColors", &c.autoRandomizeColors);
    r.registerFloat("colorRandomizeInterval", &c.colorRandomizeInterval, 0.1f, 60.0f);
    r.registerFloat("colorRandomizeElapsed", &c.colorRandomizeElapsed, 0.0f, 60.0f);
    r.registerVec4("primaryColorTarget", &c.primaryColorTarget);
    r.registerVec4("secondaryColorTarget", &c.secondaryColorTarget);
    r.registerInt("activeMode", &c.activeMode, 0, 10);

    // VIDEO
    r.registerFloat("videoMix", &c.videoMix, 0.0f, 1.0f);
    r.registerFloat("videoPlaybackRate", &c.videoPlaybackRate, 0.1f, 4.0f);
    r.registerFloat("videoDecodeOversample", &c.videoDecodeOversample, 1.0f, 4.0f);
    r.registerBool("autoScaleVideo", &c.autoScaleVideo);

    // DUAL VIDEO
    r.registerBool("enableDualVideo", &c.enableDualVideo);
    r.registerFloat("video2Mix", &c.video2Mix, 0.0f, 1.0f);
    r.registerInt("video2BlendMode", &c.video2BlendMode, 0, 4);
    r.registerInt("selectedVideo2Asset", &c.selectedVideo2Asset, 0, 1000);
    r.registerString("selectedVideo2Folder", &c.selectedVideo2Folder);
    r.registerFloat("video2PlaybackRate", &c.video2PlaybackRate, 0.1f, 4.0f);
    r.registerBool("randomVideo2Start", &c.randomVideo2Start);
    r.registerFloat("randomJumpInterval2", &c.randomJumpInterval2, 0.1f, 60.0f);
    r.registerBool("enableRandomJumpInterval2", &c.enableRandomJumpInterval2);
    r.registerString("selectedVideoFolder", &c.selectedVideoFolder);

    // POST PROCESSING
    r.registerFloat("grayscaleAmount", &c.grayscaleAmount, 0.0f, 1.0f);
    r.registerFloat("sharpenAmount", &c.sharpenAmount, 0.0f, 2.0f);
    r.registerBool("upscaleEnabled", &c.upscaleEnabled);
    r.registerFloat("loopBlendSeconds", &c.loopBlendSeconds, 0.0f, 5.0f);
    r.registerInt("forcedFpsIndex", &c.forcedFpsIndex, 0, 10);

    // CRT
    r.registerFloat("crtCurvature", &c.crtCurvature, 0.0f, 1.0f);
    r.registerFloat("crtHorizontalCurvature", &c.crtHorizontalCurvature, 0.0f, 1.0f);
    r.registerFloat("crtScanlineIntensity", &c.crtScanlineIntensity, 0.0f, 5.0f);
    r.registerFloat("crtMaskIntensity", &c.crtMaskIntensity, 0.0f, 5.0f);
    r.registerFloat("crtVignette", &c.crtVignette, 0.0f, 2.0f);
    r.registerFloat("crtFishEye", &c.crtFishEye, 0.0f, 1.0f);
    r.registerBool("enablePostCrtCurvature", &c.enablePostCrtCurvature);
    r.registerBool("enablePostScanMask", &c.enablePostScanMask);
    r.registerBool("enablePostVignette", &c.enablePostVignette);
    r.registerBool("enablePostFishEye", &c.enablePostFishEye);

    // BLOOM
    r.registerBool("enablePostBloom", &c.enablePostBloom);
    r.registerFloat("bloomIntensity", &c.bloomIntensity, 0.0f, 5.0f);
    r.registerFloat("bloomThreshold", &c.bloomThreshold, 0.0f, 5.0f);

    // ABERRATION
    r.registerBool("enablePostAberration", &c.enablePostAberration);
    r.registerFloat("aberrationAmount", &c.aberrationAmount, 0.0f, 0.1f);

    // GRAIN
    r.registerBool("enablePostGrain", &c.enablePostGrain);
    r.registerFloat("grainStrength", &c.grainStrength, 0.0f, 1.0f);

    // BEND
    r.registerBool("enablePostBend", &c.enablePostBend);
    r.registerFloat("bendAmount", &c.bendAmount, 0.0f, 1.0f);

    // GLITCH
    r.registerBool("enablePostGlitch", &c.enablePostGlitch);
    r.registerFloat("glitchAmount", &c.glitchAmount, 0.0f, 1.0f);
    r.registerBool("randomVideoStart", &c.randomVideoStart);
    r.registerFloat("randomJumpInterval", &c.randomJumpInterval, 0.1f, 60.0f);
    r.registerBool("enableRandomJumpInterval", &c.enableRandomJumpInterval);

    // COLOR BALANCE
    r.registerBool("enablePostColorBalance", &c.enablePostColorBalance);
    r.registerVec3("colorBalance", &c.colorBalance);

    // COLOR GRADING
    r.registerBool("enableColorGrading", &c.enableColorGrading);
    r.registerFloat("gradeBrightness", &c.gradeBrightness, -1.0f, 1.0f);
    r.registerFloat("gradeContrast", &c.gradeContrast, 0.0f, 2.0f);
    r.registerFloat("gradeSaturation", &c.gradeSaturation, 0.0f, 2.0f);
    r.registerFloat("gradeHueShift", &c.gradeHueShift, 0.0f, 6.28f);
    r.registerFloat("gradeGamma", &c.gradeGamma, 0.1f, 3.0f);
    r.registerInt("colorLUTIndex", &c.colorLUTIndex, 0, 10);
    r.registerFloat("splitToneBalance", &c.splitToneBalance, 0.0f, 1.0f);
    r.registerVec3("splitToneShadows", &c.splitToneShadows);
    r.registerVec3("splitToneHighlights", &c.splitToneHighlights);

    // TEMPORAL FEEDBACK
    r.registerBool("enableFeedback", &c.enableFeedback);
    r.registerFloat("feedbackAmount", &c.feedbackAmount, 0.0f, 1.0f);
    r.registerFloat("trailStrength", &c.trailStrength, 0.0f, 1.0f);
    r.registerFloat("temporalAccumulation", &c.temporalAccumulation, 0.0f, 1.0f);
    r.registerFloat("feedbackDecay", &c.feedbackDecay, 0.0f, 1.0f);
    r.registerFloat("recursiveBlend", &c.recursiveBlend, 0.0f, 1.0f);
    r.registerBool("enableTemporal", &c.enableTemporal);
    r.registerFloat("temporalInterpolation", &c.temporalInterpolation, 0.0f, 1.0f);
    r.registerFloat("temporalBlendStrength", &c.temporalBlendStrength, 0.0f, 1.0f);
    r.registerFloat("slowMotionFactor", &c.slowMotionFactor, 0.1f, 2.0f);
    r.registerFloat("frameAccumulation", &c.frameAccumulation, 0.0f, 1.0f);

    // SPATIAL DISTORTION
    r.registerBool("enableDistortion", &c.enableDistortion);
    r.registerFloat("uvWarpStrength", &c.uvWarpStrength, 0.0f, 5.0f);
    r.registerFloat("rippleStrength", &c.rippleStrength, 0.0f, 5.0f);
    r.registerFloat("rippleFrequency", &c.rippleFrequency, 0.1f, 10.0f);
    r.registerFloat("swirlStrength", &c.swirlStrength, 0.0f, 5.0f);
    r.registerFloat("displacementAmount", &c.displacementAmount, 0.0f, 2.0f);
    r.registerFloat("kaleidoSegments", &c.kaleidoSegments, 0.0f, 16.0f);
    r.registerFloat("tunnelDepth", &c.tunnelDepth, 0.0f, 2.0f);
    r.registerFloat("tunnelCurvature", &c.tunnelCurvature, 0.0f, 2.0f);

    // BLUR / MOTION
    r.registerBool("enableBlurMotion", &c.enableBlurMotion);
    r.registerFloat("gaussianBlur", &c.gaussianBlur, 0.0f, 10.0f);
    r.registerFloat("directionalBlur", &c.directionalBlur, 0.0f, 10.0f);
    r.registerFloat("directionalBlurAngle", &c.directionalBlurAngle, 0.0f, 6.28f);
    r.registerFloat("zoomBlur", &c.zoomBlur, 0.0f, 10.0f);
    r.registerFloat("motionBlur", &c.motionBlur, 0.0f, 10.0f);
    r.registerFloat("temporalBlur", &c.temporalBlur, 0.0f, 10.0f);

    // SHARPENING
    r.registerBool("enableSharpen", &c.enableSharpen);
    r.registerFloat("unsharpMask", &c.unsharpMask, 0.0f, 2.0f);
    r.registerFloat("casAmount", &c.casAmount, 0.0f, 2.0f);
    r.registerFloat("localContrast", &c.localContrast, 0.0f, 2.0f);

    // GLITCH ADVANCED
    r.registerFloat("glitchDatamosh", &c.glitchDatamosh, 0.0f, 1.0f);
    r.registerFloat("glitchRGBSplit", &c.glitchRGBSplit, 0.0f, 1.0f);
    r.registerFloat("glitchScanlineBreak", &c.glitchScanlineBreak, 0.0f, 1.0f);
    r.registerFloat("glitchJitter", &c.glitchJitter, 0.0f, 1.0f);
    r.registerFloat("glitchTearing", &c.glitchTearing, 0.0f, 1.0f);
    r.registerFloat("glitchPixelSort", &c.glitchPixelSort, 0.0f, 1.0f);
    r.registerFloat("glitchBufferCorruption", &c.glitchBufferCorruption, 0.0f, 1.0f);

    // COMPOSITING
    r.registerBool("enableBlending", &c.enableBlending);
    r.registerInt("blendModeProcedural", &c.blendModeProcedural, 0, 10);
    r.registerInt("blendModeVideo", &c.blendModeVideo, 0, 10);
    r.registerInt("blendModeFeedback", &c.blendModeFeedback, 0, 10);
    r.registerFloat("blendProceduralMix", &c.blendProceduralMix, 0.0f, 1.0f);
    r.registerFloat("blendVideoMix", &c.blendVideoMix, 0.0f, 1.0f);
    r.registerFloat("blendFeedbackMix", &c.blendFeedbackMix, 0.0f, 1.0f);

    // ANALOG / CRT BOOSTER
    r.registerBool("enableAnalog", &c.enableAnalog);
    r.registerFloat("analogScanlineFocus", &c.analogScanlineFocus, 0.0f, 1.0f);
    r.registerFloat("analogMaskBalance", &c.analogMaskBalance, 0.0f, 1.0f);
    r.registerFloat("analogNoise", &c.analogNoise, 0.0f, 1.0f);
    r.registerFloat("analogBloom", &c.analogBloom, 0.0f, 1.0f);
    r.registerFloat("vhsDistortion", &c.vhsDistortion, 0.0f, 1.0f);
    r.registerFloat("analogChromaticAberration", &c.analogChromaticAberration, 0.0f, 0.1f);

    // AUDIO REACTIVE
    r.registerBool("enableAudioReactive", &c.enableAudioReactive);
    r.registerFloat("audioWarpResponse", &c.audioWarpResponse, 0.0f, 2.0f);
    r.registerFloat("audioFeedbackResponse", &c.audioFeedbackResponse, 0.0f, 2.0f);
    r.registerFloat("audioBlurResponse", &c.audioBlurResponse, 0.0f, 2.0f);
    r.registerFloat("audioColorResponse", &c.audioColorResponse, 0.0f, 2.0f);
    r.registerFloat("audioGlitchResponse", &c.audioGlitchResponse, 0.0f, 2.0f);
    r.registerFloat("audioBeatSync", &c.audioBeatSync, 0.0f, 1.0f);
    r.registerFloat("audioLfoRate", &c.audioLfoRate, 0.0f, 5.0f);
    r.registerFloat("audioHighGain", &c.audioHighGain, 0.0f, 5.0f);
    r.registerFloat("audioReactiveDrive", &c.audioReactiveDrive, 0.0f, 10.0f);

    // EXTRA EFFECTS
    r.registerFloat("pixelateAmount", &c.pixelateAmount, 0.0f, 1.0f);
    r.registerFloat("strobeSpeed", &c.strobeSpeed, 0.0f, 50.0f);
    r.registerFloat("thresholdLevel", &c.thresholdLevel, 0.0f, 1.0f);
    r.registerFloat("slowZoomAmount", &c.slowZoomAmount, 0.0f, 1.0f);
    r.registerBool("enableEdgeDetect", &c.enableEdgeDetect);
    r.registerFloat("edgeStrength", &c.edgeStrength, 0.0f, 5.0f);
    r.registerFloat("edgeThreshold", &c.edgeThreshold, 0.0f, 1.0f);
    r.registerFloat("edgeBlend", &c.edgeBlend, 0.0f, 1.0f);
    r.registerVec3("edgeColor", &c.edgeColor);
    r.registerBool("enablePixelate", &c.enablePixelate);
    r.registerBool("enableStrobe", &c.enableStrobe);
    r.registerBool("enableThreshold", &c.enableThreshold);
    r.registerBool("enableSlowZoom", &c.enableSlowZoom);

    // VJAY EXTRA
    r.registerBool("enableMirror", &c.enableMirror);
    r.registerBool("enableInvert", &c.enableInvert);
    r.registerBool("enablePosterize", &c.enablePosterize);
    r.registerBool("enableInfrared", &c.enableInfrared);
    r.registerBool("enableZoomPulse", &c.enableZoomPulse);
    r.registerBool("enableRGBShift", &c.enableRGBShift);
    r.registerFloat("mirrorAmount", &c.mirrorAmount, 0.0f, 1.0f);
    r.registerFloat("posterizeLevels", &c.posterizeLevels, 2.0f, 32.0f);
    r.registerFloat("zoomPulseAmount", &c.zoomPulseAmount, 0.0f, 1.0f);
    r.registerFloat("rgbShiftAmount", &c.rgbShiftAmount, 0.0f, 0.1f);

    // FXAA
    r.registerBool("enableFXAA", &c.enableFXAA);
    r.registerFloat("fxaaQualitySubpix", &c.fxaaQualitySubpix, 0.0f, 1.0f);
    r.registerFloat("fxaaQualityEdgeThreshold", &c.fxaaQualityEdgeThreshold, 0.0f, 0.5f);
    r.registerFloat("fxaaQualityEdgeThresholdMin", &c.fxaaQualityEdgeThresholdMin, 0.0f, 0.2f);

    // GRID / MIRRORING
    r.registerBool("enableGrid", &c.enableGrid);
    r.registerInt("gridMode", &c.gridMode, 0, 2);
    r.registerInt("gridCount", &c.gridCount, 1, 10);
    r.registerInt("gridRows", &c.gridRows, 1, 10);
    r.registerInt("gridColumns", &c.gridColumns, 1, 10);
    r.registerBool("gridMirrorCells", &c.gridMirrorCells);
    r.registerBool("gridShowLines", &c.gridShowLines);
    r.registerFloat("gridLineWidth", &c.gridLineWidth, 0.0f, 0.01f);
    r.registerFloat("gridLineIntensity", &c.gridLineIntensity, 0.0f, 1.0f);
    r.registerVec3("gridLineColor", &c.gridLineColor);

    // CAMERA
    r.registerFloat("cameraZoom", &c.cameraZoom, 0.1f, 5.0f);
    r.registerFloat("cameraPanX", &c.cameraPanX, -1.0f, 1.0f);
    r.registerFloat("cameraPanY", &c.cameraPanY, -1.0f, 1.0f);
    r.registerFloat("cameraRotation", &c.cameraRotation, -3.14f, 3.14f);
    r.registerBool("enableCameraMovement", &c.enableCameraMovement);

    // RGB OVERLAY
    r.registerVec3("rgbOverlay", &c.rgbOverlay);
    r.registerBool("enableRgbOverlay", &c.enableRgbOverlay);
}
