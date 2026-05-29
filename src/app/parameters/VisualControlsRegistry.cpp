#include "VisualControlsRegistry.h"
#include "../VisualControls.h"

void VisualControlsRegistry::build(ParameterRegistry& r, VisualControls& c) {
    auto& playback = c.playback;
    auto& color = c.color;
    auto& fx = c.fx;
    auto& post = c.post;
    auto& audio = c.audio;
    auto& temporal = c.temporal;
    auto& blending = c.blending;
    auto& camera = c.camera;
    auto& grid = c.grid;
    auto& system = c.system;
    auto& locks = c.locks;
    // CORE / PLAYBACK
    r.registerFloat("animationSpeed", &playback.animationSpeed, 0.0f, 10.0f);
    r.registerFloat("animationTargetSeconds", &playback.animationTargetSeconds, 0.0f, 10.0f);
    r.registerFloat("tempo", &playback.tempo, 0.0f, 8.0f);
    r.registerBool("enableTempoLfo", &playback.enableTempoLfo);
    r.registerFloat("tempoLfoSpeed", &playback.tempoLfoSpeed, 0.0f, 10.0f);
    r.registerFloat("tempoLfoDepth", &playback.tempoLfoDepth, 0.0f, 2.0f);
    r.registerFloat("tempoLfoPhase", &playback.tempoLfoPhase, 0.0f, 6.28f);
    r.registerInt("activeMode", &playback.activeMode, 0, 40);

    // Audio analysis snapshot (exposed for monitoring)
    r.registerFloat("energy", &audio.energy, 0.0f, 1.0f);
    r.registerFloat("bass", &audio.bass, 0.0f, 1.0f);
    r.registerFloat("mid", &audio.mid, 0.0f, 1.0f);
    r.registerFloat("high", &audio.high, 0.0f, 1.0f);

    // COLORS
    r.registerFloat("colorBlend", &color.colorBlend, 0.0f, 1.0f);
    r.registerVec4("primaryColor", &color.primaryColor);
    r.registerVec4("secondaryColor", &color.secondaryColor);
    r.registerBool("autoRandomizeColors", &color.autoRandomizeColors);
    r.registerFloat("colorRandomizeInterval", &color.colorRandomizeInterval, 0.1f, 60.0f);
    r.registerFloat("colorRandomizeElapsed", &color.colorRandomizeElapsed, 0.0f, 60.0f);
    r.registerVec4("primaryColorTarget", &color.primaryColorTarget);
    r.registerVec4("secondaryColorTarget", &color.secondaryColorTarget);

    // VIDEO
    r.registerFloat("videoMix", &playback.videoMix, 0.0f, 1.0f);
    r.registerFloat("videoPlaybackRate", &playback.videoPlaybackRate, 0.0f, 5.0f);
    r.registerFloat("videoDecodeOversample", &playback.videoDecodeOversample, 1.0f, 4.0f);
    r.registerBool("autoScaleVideo", &playback.autoScaleVideo);

    // DUAL VIDEO
    r.registerBool("enableDualVideo", &playback.enableDualVideo);
    r.registerFloat("video2Mix", &playback.video2Mix, 0.0f, 1.0f);
    r.registerInt("video2BlendMode", &playback.video2BlendMode, 0, 4);
    r.registerInt("selectedVideo2Asset", &playback.selectedVideo2Asset, 0, 1000);
    r.registerString("selectedVideo2Folder", &playback.selectedVideo2Folder);
    r.registerFloat("video2PlaybackRate", &playback.video2PlaybackRate, 0.0f, 5.0f);
    r.registerBool("randomVideo2Start", &playback.randomVideo2Start);
    r.registerFloat("randomJumpInterval2", &playback.randomJumpInterval2, 0.1f, 60.0f);
    r.registerBool("enableRandomJumpInterval2", &playback.enableRandomJumpInterval2);
    r.registerString("selectedVideoFolder", &playback.selectedVideoFolder);

    // POST PROCESSING BASICS
    r.registerFloat("grayscaleAmount", &playback.grayscaleAmount, 0.0f, 1.0f);
    r.registerFloat("sharpenAmount", &playback.sharpenAmount, 0.0f, 2.0f);
    r.registerBool("upscaleEnabled", &playback.upscaleEnabled);
    r.registerFloat("loopBlendSeconds", &playback.loopBlendSeconds, 0.0f, 5.0f);
    r.registerInt("forcedFpsIndex", &playback.forcedFpsIndex, 0, 10);

    // CRT / POST
    r.registerFloat("crtCurvature", &post.crtCurvature, 0.0f, 1.0f);
    r.registerFloat("crtHorizontalCurvature", &post.crtHorizontalCurvature, 0.0f, 1.0f);
    r.registerFloat("crtScanlineIntensity", &post.crtScanlineIntensity, 0.0f, 5.0f);
    r.registerFloat("crtMaskIntensity", &post.crtMaskIntensity, 0.0f, 5.0f);
    r.registerFloat("crtVignette", &post.crtVignette, 0.0f, 2.0f);
    r.registerFloat("crtFishEye", &post.crtFishEye, -1.0f, 1.0f);
    r.registerBool("enablePostCrtCurvature", &post.enablePostCrtCurvature);
    r.registerBool("enablePostScanMask", &post.enablePostScanMask);
    r.registerBool("enablePostVignette", &post.enablePostVignette);
    r.registerBool("enablePostFishEye", &post.enablePostFishEye);

    // BLOOM / ABERRATION / GRAIN / BEND
    r.registerBool("enablePostBloom", &post.enablePostBloom);
    r.registerFloat("bloomIntensity", &post.bloomIntensity, 0.0f, 5.0f);
    r.registerFloat("bloomThreshold", &post.bloomThreshold, 0.0f, 5.0f);
    r.registerBool("enablePostAberration", &post.enablePostAberration);
    r.registerFloat("aberrationAmount", &post.aberrationAmount, 0.0f, 0.1f);
    r.registerBool("enablePostGrain", &post.enablePostGrain);
    r.registerFloat("grainStrength", &post.grainStrength, 0.0f, 1.0f);
    r.registerBool("enablePostBend", &post.enablePostBend);
    r.registerFloat("bendAmount", &fx.bendAmount, 0.0f, 0.5f);

    // GLITCH / RANDOM VIDEO
    r.registerBool("enablePostGlitch", &post.enablePostGlitch);
    r.registerFloat("glitchAmount", &fx.glitchAmount, 0.0f, 1.0f);
    r.registerBool("randomVideoStart", &playback.randomVideoStart);
    r.registerFloat("randomJumpInterval", &playback.randomJumpInterval, 0.1f, 60.0f);
    r.registerBool("enableRandomJumpInterval", &playback.enableRandomJumpInterval);

    // COLOR BALANCE / RGB OVERLAY
    r.registerBool("enablePostColorBalance", &post.enablePostColorBalance);
    r.registerVec3("colorBalance", &color.colorBalance);
    r.registerVec3("rgbOverlay", &color.rgbOverlay);
    r.registerBool("enableRgbOverlay", &color.enableRgbOverlay);

    // PARAMETER LOCKS
    r.registerBool("lockColorBalance", &locks.lockColorBalance);
    r.registerBool("lockThreshold", &locks.lockThreshold);
    r.registerBool("lockGrid", &locks.lockGrid);

    // COLOR GRADING
    r.registerBool("enableColorGrading", &color.enableColorGrading);
    r.registerFloat("gradeBrightness", &color.gradeBrightness, -1.0f, 1.0f);
    r.registerFloat("gradeContrast", &color.gradeContrast, 0.0f, 2.0f);
    r.registerFloat("gradeSaturation", &color.gradeSaturation, 0.0f, 2.0f);
    r.registerFloat("gradeHueShift", &color.gradeHueShift, 0.0f, 6.28f);
    r.registerFloat("gradeGamma", &color.gradeGamma, 0.1f, 3.0f);
    r.registerInt("colorLUTIndex", &color.colorLUTIndex, 0, 10);
    r.registerFloat("splitToneBalance", &color.splitToneBalance, 0.0f, 1.0f);
    r.registerVec3("splitToneShadows", &color.splitToneShadows);
    r.registerVec3("splitToneHighlights", &color.splitToneHighlights);

    // TEMPORAL / FEEDBACK
    r.registerBool("enableFeedback", &temporal.enableFeedback);
    r.registerFloat("feedbackAmount", &temporal.feedbackAmount, 0.0f, 1.0f);
    r.registerFloat("trailStrength", &temporal.trailStrength, 0.0f, 1.0f);
    r.registerFloat("temporalAccumulation", &temporal.temporalAccumulation, 0.0f, 1.0f);
    r.registerFloat("feedbackDecay", &temporal.feedbackDecay, 0.0f, 1.0f);
    r.registerFloat("recursiveBlend", &temporal.recursiveBlend, 0.0f, 1.0f);
    r.registerBool("enableTemporal", &temporal.enableTemporal);
    r.registerFloat("temporalInterpolation", &playback.temporalInterpolation, 0.0f, 1.0f);
    r.registerFloat("temporalBlendStrength", &playback.temporalBlendStrength, 0.0f, 1.0f);
    r.registerFloat("slowMotionFactor", &playback.slowMotionFactor, 0.1f, 2.0f);
    r.registerFloat("frameAccumulation", &playback.frameAccumulation, 0.0f, 1.0f);

    // SPATIAL DISTORTION
    r.registerBool("enableDistortion", &fx.enableDistortion);
    r.registerFloat("uvWarpStrength", &fx.uvWarpStrength, 0.0f, 5.0f);
    r.registerFloat("rippleStrength", &fx.rippleStrength, 0.0f, 5.0f);
    r.registerFloat("rippleFrequency", &fx.rippleFrequency, 0.1f, 10.0f);
    r.registerFloat("swirlStrength", &fx.swirlStrength, 0.0f, 5.0f);
    r.registerFloat("displacementAmount", &fx.displacementAmount, 0.0f, 2.0f);
    r.registerFloat("kaleidoSegments", &fx.kaleidoSegments, 0.0f, 16.0f);
    r.registerFloat("tunnelDepth", &fx.tunnelDepth, 0.0f, 2.0f);
    r.registerFloat("tunnelCurvature", &fx.tunnelCurvature, 0.0f, 2.0f);

    // BLUR / MOTION
    r.registerBool("enableBlurMotion", &fx.enableBlurMotion);
    r.registerFloat("gaussianBlur", &fx.gaussianBlur, 0.0f, 10.0f);
    r.registerFloat("directionalBlur", &fx.directionalBlur, 0.0f, 10.0f);
    r.registerFloat("directionalBlurAngle", &fx.directionalBlurAngle, 0.0f, 6.28f);
    r.registerFloat("zoomBlur", &fx.zoomBlur, 0.0f, 10.0f);
    r.registerFloat("motionBlur", &fx.motionBlur, 0.0f, 10.0f);
    r.registerFloat("temporalBlur", &fx.temporalBlur, 0.0f, 10.0f);

    // SHARPENING
    r.registerBool("enableSharpen", &fx.enableSharpen);
    r.registerFloat("unsharpMask", &fx.unsharpMask, 0.0f, 2.0f);
    r.registerFloat("casAmount", &fx.casAmount, 0.0f, 2.0f);
    r.registerFloat("localContrast", &fx.localContrast, 0.0f, 2.0f);

    // GLITCH ADVANCED
    r.registerBool("enableGlitch", &fx.enableGlitch);
    r.registerFloat("glitchDatamosh", &fx.glitchDatamosh, 0.0f, 1.0f);
    r.registerFloat("glitchRGBSplit", &fx.glitchRGBSplit, 0.0f, 1.0f);
    r.registerFloat("glitchScanlineBreak", &fx.glitchScanlineBreak, 0.0f, 1.0f);
    r.registerFloat("glitchJitter", &fx.glitchJitter, 0.0f, 1.0f);
    r.registerFloat("glitchTearing", &fx.glitchTearing, 0.0f, 1.0f);
    r.registerFloat("glitchPixelSort", &fx.glitchPixelSort, 0.0f, 1.0f);
    r.registerFloat("glitchBufferCorruption", &fx.glitchBufferCorruption, 0.0f, 1.0f);

    // COMPOSITING
    r.registerBool("enableBlending", &blending.enableBlending);
    r.registerInt("blendModeProcedural", &blending.blendModeProcedural, 0, 10);
    r.registerInt("blendModeVideo", &blending.blendModeVideo, 0, 10);
    r.registerInt("blendModeFeedback", &blending.blendModeFeedback, 0, 10);
    r.registerFloat("blendProceduralMix", &blending.blendProceduralMix, 0.0f, 1.0f);
    r.registerFloat("blendVideoMix", &blending.blendVideoMix, 0.0f, 1.0f);
    r.registerFloat("blendFeedbackMix", &blending.blendFeedbackMix, 0.0f, 1.0f);

    // ANALOG / CRT BOOSTER
    r.registerBool("enableAnalog", &post.enableAnalog);
    r.registerFloat("analogScanlineFocus", &post.analogScanlineFocus, 0.0f, 1.0f);
    r.registerFloat("analogMaskBalance", &post.analogMaskBalance, 0.0f, 1.0f);
    r.registerFloat("analogNoise", &post.analogNoise, 0.0f, 1.0f);
    r.registerFloat("analogBloom", &post.analogBloom, 0.0f, 1.0f);
    r.registerFloat("vhsDistortion", &post.vhsDistortion, 0.0f, 1.0f);
    r.registerFloat("analogChromaticAberration", &post.analogChromaticAberration, 0.0f, 0.1f);

    // AUDIO REACTIVE (enableAudioReactive is always ON, not registered)
    r.registerFloat("audioWarpResponse", &audio.warpResponse, 0.0f, 2.0f);
    r.registerFloat("audioFeedbackResponse", &audio.feedbackResponse, 0.0f, 2.0f);
    r.registerFloat("audioBlurResponse", &audio.blurResponse, 0.0f, 2.0f);
    r.registerFloat("audioColorResponse", &audio.colorResponse, 0.0f, 2.0f);
    r.registerFloat("audioGlitchResponse", &audio.glitchResponse, 0.0f, 2.0f);
    r.registerFloat("audioBeatSync", &audio.beatSync, 0.0f, 1.0f);
    r.registerFloat("audioLfoRate", &audio.lfoRate, 0.0f, 5.0f);
    r.registerFloat("audioInputGain", &audio.inputGain, 0.0f, 3.0f);
    r.registerFloat("audioBassGain", &audio.bassGain, 0.0f, 4.0f);
    r.registerFloat("audioMidGain", &audio.midGain, 0.0f, 4.0f);
    r.registerFloat("audioHighGain", &audio.highGain, 0.0f, 4.0f);
    r.registerFloat("audioReactiveDrive", &audio.reactiveDrive, 0.0f, 10.0f);

    // EXTRA EFFECTS / EDGE
    r.registerFloat("pixelateAmount", &fx.pixelateAmount, 0.0f, 1.0f);
    r.registerFloat("strobeSpeed", &fx.strobeSpeed, 0.0f, 50.0f);
    r.registerFloat("thresholdLevel", &fx.thresholdLevel, 0.0f, 1.0f);
    r.registerFloat("slowZoomAmount", &fx.slowZoomAmount, 0.0f, 1.0f);
    r.registerBool("enableEdgeDetect", &fx.enableEdgeDetect);
    r.registerFloat("edgeStrength", &fx.edgeStrength, 0.0f, 5.0f);
    r.registerFloat("edgeThreshold", &fx.edgeThreshold, 0.0f, 1.0f);
    r.registerFloat("edgeBlend", &fx.edgeBlend, 0.0f, 1.0f);
    r.registerVec3("edgeColor", &fx.edgeColor);
    r.registerBool("enablePixelate", &fx.enablePixelate);
    r.registerBool("enableStrobe", &fx.enableStrobe);
    r.registerBool("enableThreshold", &fx.enableThreshold);
    r.registerBool("enableSlowZoom", &fx.enableSlowZoom);

    // VJAY EXTRA / FX toggles
    r.registerBool("enableMirror", &fx.enableMirror);
    r.registerBool("enableInvert", &fx.enableInvert);
    r.registerBool("enablePosterize", &fx.enablePosterize);
    r.registerBool("enableInfrared", &fx.enableInfrared);
    r.registerBool("enableZoomPulse", &fx.enableZoomPulse);
    r.registerBool("enableRGBShift", &fx.enableRGBShift);
    r.registerFloat("mirrorAmount", &fx.mirrorAmount, 0.0f, 1.0f);
    r.registerFloat("posterizeLevels", &fx.posterizeLevels, 2.0f, 32.0f);
    r.registerFloat("zoomPulseAmount", &fx.zoomPulseAmount, 0.0f, 1.0f);
    r.registerFloat("rgbShiftAmount", &fx.rgbShiftAmount, 0.0f, 0.1f);

    // FXAA / SYSTEM
    r.registerBool("enableFXAA", &system.enableFXAA);
    r.registerFloat("fxaaQualitySubpix", &system.fxaaQualitySubpix, 0.0f, 1.0f);
    r.registerFloat("fxaaQualityEdgeThreshold", &system.fxaaQualityEdgeThreshold, 0.0f, 0.5f);
    r.registerFloat("fxaaQualityEdgeThresholdMin", &system.fxaaQualityEdgeThresholdMin, 0.0f, 0.2f);

    // GRID / MIRRORING
    r.registerBool("enableGrid", &grid.enabled);
    r.registerInt("gridMode", &grid.mode, 0, 2);
    r.registerInt("gridCount", &grid.count, 1, 10);
    r.registerInt("gridRows", &grid.rows, 1, 10);
    r.registerInt("gridColumns", &grid.columns, 1, 10);
    r.registerBool("gridMirrorCells", &grid.mirrorCells);
    r.registerBool("gridShowLines", &grid.showLines);
    r.registerFloat("gridLineWidth", &grid.lineWidth, 0.0f, 0.01f);
    r.registerFloat("gridLineIntensity", &grid.lineIntensity, 0.0f, 1.0f);
    r.registerVec3("gridLineColor", &grid.lineColor);

    // CAMERA
    r.registerFloat("cameraZoom", &camera.zoom, 0.01f, 5.0f);
    r.registerFloat("cameraPanX", &camera.panX, -1.0f, 1.0f);
    r.registerFloat("cameraPanY", &camera.panY, -1.0f, 1.0f);
    r.registerFloat("cameraRotation", &camera.rotation, -3.14f, 3.14f);
    r.registerBool("enableCameraMovement", &camera.enableMovement);
}
