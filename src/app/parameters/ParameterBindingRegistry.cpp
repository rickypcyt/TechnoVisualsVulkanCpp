#include "ParameterBindingRegistry.h"

namespace {

template <typename MemberPtr>
std::function<float&(VisualControls&)> makeFloatGetter(MemberPtr ptr) {
    return [ptr](VisualControls& c) -> float& { return c.*ptr; };
}

template <typename MemberPtr>
std::function<int&(VisualControls&)> makeIntGetter(MemberPtr ptr) {
    return [ptr](VisualControls& c) -> int& { return c.*ptr; };
}

template <typename MemberPtr>
std::function<bool&(VisualControls&)> makeBoolGetter(MemberPtr ptr) {
    return [ptr](VisualControls& c) -> bool& { return c.*ptr; };
}

template <typename MemberPtr>
std::function<glm::vec3&(VisualControls&)> makeVec3Getter(MemberPtr ptr) {
    return [ptr](VisualControls& c) -> glm::vec3& { return c.*ptr; };
}

template <typename MemberPtr>
std::function<glm::vec4&(VisualControls&)> makeVec4Getter(MemberPtr ptr) {
    return [ptr](VisualControls& c) -> glm::vec4& { return c.*ptr; };
}

} // namespace

const std::unordered_map<std::string, ParamBinding>& ParameterBindingRegistry::get() {
    static const std::unordered_map<std::string, ParamBinding> map = {
        // ============================================================================
        // PLAYBACK DOMAIN
        // ============================================================================
        { "playback.animationSpeed", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.playback.animationSpeed; }} },
        { "playback.animationTargetSeconds", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.playback.animationTargetSeconds; }} },
        { "playback.tempo", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.playback.tempo; }} },
        { "playback.enableTempoLfo", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.playback.enableTempoLfo; }} },
        { "playback.tempoLfoSpeed", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.playback.tempoLfoSpeed; }} },
        { "playback.tempoLfoDepth", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.playback.tempoLfoDepth; }} },
        { "playback.tempoLfoPhase", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.playback.tempoLfoPhase; }} },
        { "playback.videoPlaybackRate", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.playback.videoPlaybackRate; }} },
        { "playback.videoDecodeOversample", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.playback.videoDecodeOversample; }} },
        { "playback.loopBlendSeconds", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.playback.loopBlendSeconds; }} },
        { "playback.forcedFpsIndex", {ParamBinding::Type::Int, nullptr, [](VisualControls& c) -> int& { return c.playback.forcedFpsIndex; }} },
        { "playback.temporalInterpolation", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.playback.temporalInterpolation; }} },
        { "playback.temporalBlendStrength", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.playback.temporalBlendStrength; }} },
        { "playback.slowMotionFactor", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.playback.slowMotionFactor; }} },
        { "playback.frameAccumulation", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.playback.frameAccumulation; }} },
        { "playback.activeMode", {ParamBinding::Type::Int, nullptr, [](VisualControls& c) -> int& { return c.playback.activeMode; }} },
        { "playback.videoMix", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.playback.videoMix; }} },
        { "playback.autoScaleVideo", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.playback.autoScaleVideo; }} },
        { "playback.outputAspectRatio", {ParamBinding::Type::Int, nullptr, [](VisualControls& c) -> int& { return c.playback.outputAspectRatio; }} },
        { "playback.grayscaleAmount", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.playback.grayscaleAmount; }} },
        { "playback.sharpenAmount", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.playback.sharpenAmount; }} },
        { "playback.upscaleEnabled", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.playback.upscaleEnabled; }} },
        { "playback.enableDualVideo", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.playback.enableDualVideo; }} },
        { "playback.video2Mix", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.playback.video2Mix; }} },
        { "playback.video2BlendMode", {ParamBinding::Type::Int, nullptr, [](VisualControls& c) -> int& { return c.playback.video2BlendMode; }} },
        { "playback.selectedVideo2Asset", {ParamBinding::Type::Int, nullptr, [](VisualControls& c) -> int& { return c.playback.selectedVideo2Asset; }} },
        { "playback.video2PlaybackRate", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.playback.video2PlaybackRate; }} },
        { "playback.randomVideoStart", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.playback.randomVideoStart; }} },
        { "playback.randomJumpInterval", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.playback.randomJumpInterval; }} },
        { "playback.enableRandomJumpInterval", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.playback.enableRandomJumpInterval; }} },
        { "playback.randomVideo2Start", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.playback.randomVideo2Start; }} },
        { "playback.randomJumpInterval2", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.playback.randomJumpInterval2; }} },
        { "playback.enableRandomJumpInterval2", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.playback.enableRandomJumpInterval2; }} },
        { "playback.video3Mix", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.playback.video3Mix; }} },
        { "playback.video3BlendMode", {ParamBinding::Type::Int, nullptr, [](VisualControls& c) -> int& { return c.playback.video3BlendMode; }} },
        { "playback.selectedVideo3Asset", {ParamBinding::Type::Int, nullptr, [](VisualControls& c) -> int& { return c.playback.selectedVideo3Asset; }} },
        { "playback.video3PlaybackRate", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.playback.video3PlaybackRate; }} },
        { "playback.randomVideo3Start", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.playback.randomVideo3Start; }} },
        { "playback.randomJumpInterval3", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.playback.randomJumpInterval3; }} },
        { "playback.enableRandomJumpInterval3", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.playback.enableRandomJumpInterval3; }} },

        // Legacy flat names (for backward compatibility)
        { "animationSpeed", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.playback.animationSpeed; }} },
        { "tempo", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.playback.tempo; }} },
        { "videoPlaybackRate", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.playback.videoPlaybackRate; }} },
        { "activeMode", {ParamBinding::Type::Int, nullptr, [](VisualControls& c) -> int& { return c.playback.activeMode; }} },
        { "videoMix", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.playback.videoMix; }} },
        { "enableTempoLfo", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.playback.enableTempoLfo; }} },
        { "tempoLfoSpeed", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.playback.tempoLfoSpeed; }} },
        { "tempoLfoDepth", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.playback.tempoLfoDepth; }} },
        { "tempoLfoPhase", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.playback.tempoLfoPhase; }} },
        { "temporalInterpolation", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.playback.temporalInterpolation; }} },
        { "temporalBlendStrength", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.playback.temporalBlendStrength; }} },
        { "slowMotionFactor", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.playback.slowMotionFactor; }} },
        { "frameAccumulation", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.playback.frameAccumulation; }} },
        { "enableDualVideo", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.playback.enableDualVideo; }} },
        { "video2Mix", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.playback.video2Mix; }} },
        { "video2BlendMode", {ParamBinding::Type::Int, nullptr, [](VisualControls& c) -> int& { return c.playback.video2BlendMode; }} },
        { "selectedVideo2Asset", {ParamBinding::Type::Int, nullptr, [](VisualControls& c) -> int& { return c.playback.selectedVideo2Asset; }} },
        { "video2PlaybackRate", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.playback.video2PlaybackRate; }} },
        { "randomVideo2Start", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.playback.randomVideo2Start; }} },
        { "randomJumpInterval2", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.playback.randomJumpInterval2; }} },
        { "enableRandomJumpInterval2", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.playback.enableRandomJumpInterval2; }} },
        { "video3Mix", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.playback.video3Mix; }} },
        { "video3BlendMode", {ParamBinding::Type::Int, nullptr, [](VisualControls& c) -> int& { return c.playback.video3BlendMode; }} },
        { "selectedVideo3Asset", {ParamBinding::Type::Int, nullptr, [](VisualControls& c) -> int& { return c.playback.selectedVideo3Asset; }} },
        { "video3PlaybackRate", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.playback.video3PlaybackRate; }} },
        { "randomVideo3Start", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.playback.randomVideo3Start; }} },
        { "randomJumpInterval3", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.playback.randomJumpInterval3; }} },
        { "enableRandomJumpInterval3", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.playback.enableRandomJumpInterval3; }} },
        { "grayscaleAmount", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.playback.grayscaleAmount; }} },
        { "sharpenAmount", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.playback.sharpenAmount; }} },
        { "upscaleEnabled", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.playback.upscaleEnabled; }} },
        { "loopBlendSeconds", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.playback.loopBlendSeconds; }} },
        { "forcedFpsIndex", {ParamBinding::Type::Int, nullptr, [](VisualControls& c) -> int& { return c.playback.forcedFpsIndex; }} },
        { "randomVideoStart", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.playback.randomVideoStart; }} },
        { "randomJumpInterval", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.playback.randomJumpInterval; }} },
        { "enableRandomJumpInterval", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.playback.enableRandomJumpInterval; }} },
        { "autoScaleVideo", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.playback.autoScaleVideo; }} },

        // ============================================================================
        // COLOR DOMAIN
        // ============================================================================
        { "color.primaryColor", {ParamBinding::Type::Vec4, nullptr, nullptr, nullptr, nullptr, [](VisualControls& c) -> glm::vec4& { return c.color.primaryColor; }} },
        { "color.secondaryColor", {ParamBinding::Type::Vec4, nullptr, nullptr, nullptr, nullptr, [](VisualControls& c) -> glm::vec4& { return c.color.secondaryColor; }} },
        { "color.colorBlend", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.color.colorBlend; }} },
        { "color.autoRandomizeColors", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.color.autoRandomizeColors; }} },
        { "color.colorRandomizeInterval", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.color.colorRandomizeInterval; }} },
        { "color.colorRandomizeElapsed", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.color.colorRandomizeElapsed; }} },
        { "color.primaryColorTarget", {ParamBinding::Type::Vec4, nullptr, nullptr, nullptr, nullptr, [](VisualControls& c) -> glm::vec4& { return c.color.primaryColorTarget; }} },
        { "color.secondaryColorTarget", {ParamBinding::Type::Vec4, nullptr, nullptr, nullptr, nullptr, [](VisualControls& c) -> glm::vec4& { return c.color.secondaryColorTarget; }} },
        { "color.gradeBrightness", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.color.gradeBrightness; }} },
        { "color.gradeContrast", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.color.gradeContrast; }} },
        { "color.gradeSaturation", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.color.gradeSaturation; }} },
        { "color.gradeHueShift", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.color.gradeHueShift; }} },
        { "color.gradeGamma", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.color.gradeGamma; }} },
        { "color.colorLUTIndex", {ParamBinding::Type::Int, nullptr, [](VisualControls& c) -> int& { return c.color.colorLUTIndex; }} },
        { "color.splitToneBalance", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.color.splitToneBalance; }} },
        { "color.splitToneShadows", {ParamBinding::Type::Vec3, nullptr, nullptr, nullptr, [](VisualControls& c) -> glm::vec3& { return c.color.splitToneShadows; }} },
        { "color.splitToneHighlights", {ParamBinding::Type::Vec3, nullptr, nullptr, nullptr, [](VisualControls& c) -> glm::vec3& { return c.color.splitToneHighlights; }} },
        { "color.colorBalance", {ParamBinding::Type::Vec3, nullptr, nullptr, nullptr, [](VisualControls& c) -> glm::vec3& { return c.color.colorBalance; }} },
        { "color.rgbOverlay", {ParamBinding::Type::Vec3, nullptr, nullptr, nullptr, [](VisualControls& c) -> glm::vec3& { return c.color.rgbOverlay; }} },
        { "color.enableRgbOverlay", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.color.enableRgbOverlay; }} },
        { "color.enableColorGrading", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.color.enableColorGrading; }} },
        { "locks.lockColorBalance", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.locks.lockColorBalance; }} },
        { "locks.lockThreshold", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.locks.lockThreshold; }} },
        { "locks.lockGrid", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.locks.lockGrid; }} },

        // Legacy flat names
        { "colorBlend", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.color.colorBlend; }} },
        { "primaryColor", {ParamBinding::Type::Vec4, nullptr, nullptr, nullptr, nullptr, [](VisualControls& c) -> glm::vec4& { return c.color.primaryColor; }} },
        { "secondaryColor", {ParamBinding::Type::Vec4, nullptr, nullptr, nullptr, nullptr, [](VisualControls& c) -> glm::vec4& { return c.color.secondaryColor; }} },
        { "autoRandomizeColors", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.color.autoRandomizeColors; }} },
        { "colorRandomizeInterval", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.color.colorRandomizeInterval; }} },
        { "colorRandomizeElapsed", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.color.colorRandomizeElapsed; }} },
        { "primaryColorTarget", {ParamBinding::Type::Vec4, nullptr, nullptr, nullptr, nullptr, [](VisualControls& c) -> glm::vec4& { return c.color.primaryColorTarget; }} },
        { "secondaryColorTarget", {ParamBinding::Type::Vec4, nullptr, nullptr, nullptr, nullptr, [](VisualControls& c) -> glm::vec4& { return c.color.secondaryColorTarget; }} },
        { "gradeBrightness", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.color.gradeBrightness; }} },
        { "gradeContrast", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.color.gradeContrast; }} },
        { "gradeSaturation", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.color.gradeSaturation; }} },
        { "gradeHueShift", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.color.gradeHueShift; }} },
        { "gradeGamma", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.color.gradeGamma; }} },
        { "colorLUTIndex", {ParamBinding::Type::Int, nullptr, [](VisualControls& c) -> int& { return c.color.colorLUTIndex; }} },
        { "splitToneBalance", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.color.splitToneBalance; }} },
        { "splitToneShadows", {ParamBinding::Type::Vec3, nullptr, nullptr, nullptr, [](VisualControls& c) -> glm::vec3& { return c.color.splitToneShadows; }} },
        { "splitToneHighlights", {ParamBinding::Type::Vec3, nullptr, nullptr, nullptr, [](VisualControls& c) -> glm::vec3& { return c.color.splitToneHighlights; }} },
        { "colorBalance", {ParamBinding::Type::Vec3, nullptr, nullptr, nullptr, [](VisualControls& c) -> glm::vec3& { return c.color.colorBalance; }} },
        { "rgbOverlay", {ParamBinding::Type::Vec3, nullptr, nullptr, nullptr, [](VisualControls& c) -> glm::vec3& { return c.color.rgbOverlay; }} },
        { "enableRgbOverlay", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.color.enableRgbOverlay; }} },
        { "enableColorGrading", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.color.enableColorGrading; }} },

        // ============================================================================
        // FX DOMAIN
        // ============================================================================
        { "fx.uvWarpStrength", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.uvWarpStrength; }} },
        { "fx.rippleStrength", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.rippleStrength; }} },
        { "fx.rippleFrequency", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.rippleFrequency; }} },
        { "fx.swirlStrength", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.swirlStrength; }} },
        { "fx.displacementAmount", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.displacementAmount; }} },
        { "fx.bendAmount", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.bendAmount; }} },
        { "fx.kaleidoSegments", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.kaleidoSegments; }} },
        { "fx.tunnelDepth", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.tunnelDepth; }} },
        { "fx.tunnelCurvature", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.tunnelCurvature; }} },
        { "fx.gaussianBlur", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.gaussianBlur; }} },
        { "fx.directionalBlur", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.directionalBlur; }} },
        { "fx.directionalBlurAngle", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.directionalBlurAngle; }} },
        { "fx.zoomBlur", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.zoomBlur; }} },
        { "fx.motionBlur", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.motionBlur; }} },
        { "fx.temporalBlur", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.temporalBlur; }} },
        { "fx.unsharpMask", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.unsharpMask; }} },
        { "fx.casAmount", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.casAmount; }} },
        { "fx.localContrast", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.localContrast; }} },
        { "fx.glitchAmount", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.glitchAmount; }} },
        { "fx.glitchDatamosh", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.glitchDatamosh; }} },
        { "fx.glitchRGBSplit", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.glitchRGBSplit; }} },
        { "fx.glitchScanlineBreak", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.glitchScanlineBreak; }} },
        { "fx.glitchJitter", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.glitchJitter; }} },
        { "fx.glitchTearing", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.glitchTearing; }} },
        { "fx.glitchPixelSort", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.glitchPixelSort; }} },
        { "fx.glitchBufferCorruption", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.glitchBufferCorruption; }} },
        { "fx.pixelateAmount", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.pixelateAmount; }} },
        { "fx.strobeSpeed", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.strobeSpeed; }} },
        { "fx.thresholdLevel", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.thresholdLevel; }} },
        { "fx.slowZoomAmount", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.slowZoomAmount; }} },
        { "fx.enableEdgeDetect", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.fx.enableEdgeDetect; }} },
        { "fx.edgeStrength", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.edgeStrength; }} },
        { "fx.edgeThreshold", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.edgeThreshold; }} },
        { "fx.edgeBlend", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.edgeBlend; }} },
        { "fx.edgeColor", {ParamBinding::Type::Vec3, nullptr, nullptr, nullptr, [](VisualControls& c) -> glm::vec3& { return c.fx.edgeColor; }} },
        { "fx.enableMirror", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.fx.enableMirror; }} },
        { "fx.enableInvert", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.fx.enableInvert; }} },
        { "fx.enablePosterize", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.fx.enablePosterize; }} },
        { "fx.enableInfrared", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.fx.enableInfrared; }} },
        { "fx.enableZoomPulse", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.fx.enableZoomPulse; }} },
        { "fx.enableRGBShift", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.fx.enableRGBShift; }} },
        { "fx.mirrorAmount", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.mirrorAmount; }} },
        { "fx.posterizeLevels", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.posterizeLevels; }} },
        { "fx.zoomPulseAmount", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.zoomPulseAmount; }} },
        { "fx.rgbShiftAmount", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.rgbShiftAmount; }} },
        { "fx.enablePixelate", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.fx.enablePixelate; }} },
        { "fx.enableStrobe", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.fx.enableStrobe; }} },
        { "fx.enableThreshold", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.fx.enableThreshold; }} },
        { "fx.enableSlowZoom", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.fx.enableSlowZoom; }} },
        { "fx.enableDistortion", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.fx.enableDistortion; }} },
        { "fx.enableBlurMotion", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.fx.enableBlurMotion; }} },
        { "fx.enableSharpen", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.fx.enableSharpen; }} },
        { "fx.enableGlitch", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.fx.enableGlitch; }} },

        // Legacy flat names for FX
        { "uvWarpStrength", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.uvWarpStrength; }} },
        { "rippleStrength", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.rippleStrength; }} },
        { "rippleFrequency", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.rippleFrequency; }} },
        { "swirlStrength", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.swirlStrength; }} },
        { "displacementAmount", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.displacementAmount; }} },
        { "kaleidoSegments", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.kaleidoSegments; }} },
        { "tunnelDepth", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.tunnelDepth; }} },
        { "tunnelCurvature", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.tunnelCurvature; }} },
        { "gaussianBlur", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.gaussianBlur; }} },
        { "directionalBlur", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.directionalBlur; }} },
        { "directionalBlurAngle", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.directionalBlurAngle; }} },
        { "zoomBlur", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.zoomBlur; }} },
        { "motionBlur", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.motionBlur; }} },
        { "temporalBlur", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.temporalBlur; }} },
        { "unsharpMask", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.unsharpMask; }} },
        { "casAmount", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.casAmount; }} },
        { "localContrast", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.localContrast; }} },
        { "glitchDatamosh", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.glitchDatamosh; }} },
        { "glitchRGBSplit", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.glitchRGBSplit; }} },
        { "glitchScanlineBreak", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.glitchScanlineBreak; }} },
        { "glitchJitter", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.glitchJitter; }} },
        { "glitchTearing", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.glitchTearing; }} },
        { "glitchPixelSort", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.glitchPixelSort; }} },
        { "glitchBufferCorruption", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.glitchBufferCorruption; }} },
        { "glitchAmount", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.glitchAmount; }} },
        { "pixelateAmount", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.pixelateAmount; }} },
        { "strobeSpeed", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.strobeSpeed; }} },
        { "thresholdLevel", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.thresholdLevel; }} },
        { "slowZoomAmount", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.slowZoomAmount; }} },
        { "edgeStrength", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.edgeStrength; }} },
        { "edgeThreshold", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.edgeThreshold; }} },
        { "edgeBlend", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.edgeBlend; }} },
        { "edgeColor", {ParamBinding::Type::Vec3, nullptr, nullptr, nullptr, [](VisualControls& c) -> glm::vec3& { return c.fx.edgeColor; }} },
        { "enableEdgeDetect", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.fx.enableEdgeDetect; }} },
        { "enablePixelate", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.fx.enablePixelate; }} },
        { "enableStrobe", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.fx.enableStrobe; }} },
        { "enableThreshold", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.fx.enableThreshold; }} },
        { "enableSlowZoom", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.fx.enableSlowZoom; }} },
        { "enableMirror", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.fx.enableMirror; }} },
        { "enableInvert", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.fx.enableInvert; }} },
        { "enablePosterize", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.fx.enablePosterize; }} },
        { "enableInfrared", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.fx.enableInfrared; }} },
        { "enableZoomPulse", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.fx.enableZoomPulse; }} },
        { "enableRGBShift", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.fx.enableRGBShift; }} },
        { "mirrorAmount", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.mirrorAmount; }} },
        { "posterizeLevels", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.posterizeLevels; }} },
        { "zoomPulseAmount", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.zoomPulseAmount; }} },
        { "rgbShiftAmount", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.rgbShiftAmount; }} },
        { "enableDistortion", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.fx.enableDistortion; }} },
        { "enableBlurMotion", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.fx.enableBlurMotion; }} },
        { "enableSharpen", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.fx.enableSharpen; }} },
        { "enableGlitch", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.fx.enableGlitch; }} },

        // ============================================================================
        // POST DOMAIN
        // ============================================================================
        { "post.crtCurvature", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.post.crtCurvature; }} },
        { "post.crtHorizontalCurvature", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.post.crtHorizontalCurvature; }} },
        { "post.crtScanlineIntensity", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.post.crtScanlineIntensity; }} },
        { "post.crtMaskIntensity", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.post.crtMaskIntensity; }} },
        { "post.crtVignette", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.post.crtVignette; }} },
        { "post.crtFishEye", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.post.crtFishEye; }} },
        { "post.bloomIntensity", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.post.bloomIntensity; }} },
        { "post.bloomThreshold", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.post.bloomThreshold; }} },
        { "post.aberrationAmount", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.post.aberrationAmount; }} },
        { "post.grainStrength", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.post.grainStrength; }} },
        { "post.analogScanlineFocus", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.post.analogScanlineFocus; }} },
        { "post.analogMaskBalance", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.post.analogMaskBalance; }} },
        { "post.analogNoise", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.post.analogNoise; }} },
        { "post.analogBloom", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.post.analogBloom; }} },
        { "post.vhsDistortion", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.post.vhsDistortion; }} },
        { "post.analogChromaticAberration", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.post.analogChromaticAberration; }} },
        { "post.enablePostCrtCurvature", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.post.enablePostCrtCurvature; }} },
        { "post.enablePostScanMask", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.post.enablePostScanMask; }} },
        { "post.enablePostVignette", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.post.enablePostVignette; }} },
        { "post.enablePostFishEye", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.post.enablePostFishEye; }} },
        { "post.enablePostBloom", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.post.enablePostBloom; }} },
        { "post.enablePostAberration", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.post.enablePostAberration; }} },
        { "post.enablePostGrain", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.post.enablePostGrain; }} },
        { "post.enablePostBend", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.post.enablePostBend; }} },
        { "post.enablePostGlitch", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.post.enablePostGlitch; }} },
        { "post.enablePostColorBalance", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.post.enablePostColorBalance; }} },
        { "post.enableAnalog", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.post.enableAnalog; }} },

        // Legacy flat names for Post
        { "crtCurvature", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.post.crtCurvature; }} },
        { "crtHorizontalCurvature", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.post.crtHorizontalCurvature; }} },
        { "crtScanlineIntensity", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.post.crtScanlineIntensity; }} },
        { "crtMaskIntensity", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.post.crtMaskIntensity; }} },
        { "crtVignette", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.post.crtVignette; }} },
        { "crtFishEye", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.post.crtFishEye; }} },
        { "bloomIntensity", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.post.bloomIntensity; }} },
        { "bloomThreshold", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.post.bloomThreshold; }} },
        { "aberrationAmount", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.post.aberrationAmount; }} },
        { "grainStrength", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.post.grainStrength; }} },
        { "bendAmount", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.fx.bendAmount; }} },
        { "analogScanlineFocus", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.post.analogScanlineFocus; }} },
        { "analogMaskBalance", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.post.analogMaskBalance; }} },
        { "analogNoise", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.post.analogNoise; }} },
        { "analogBloom", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.post.analogBloom; }} },
        { "vhsDistortion", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.post.vhsDistortion; }} },
        { "analogChromaticAberration", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.post.analogChromaticAberration; }} },
        { "enablePostCrtCurvature", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.post.enablePostCrtCurvature; }} },
        { "enablePostScanMask", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.post.enablePostScanMask; }} },
        { "enablePostVignette", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.post.enablePostVignette; }} },
        { "enablePostFishEye", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.post.enablePostFishEye; }} },
        { "enablePostBloom", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.post.enablePostBloom; }} },
        { "enablePostAberration", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.post.enablePostAberration; }} },
        { "enablePostGrain", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.post.enablePostGrain; }} },
        { "enablePostBend", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.post.enablePostBend; }} },
        { "enablePostGlitch", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.post.enablePostGlitch; }} },
        { "enablePostColorBalance", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.post.enablePostColorBalance; }} },
        { "enableAnalog", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.post.enableAnalog; }} },

        // ============================================================================
        // AUDIO DOMAIN
        // ============================================================================
        { "audio.energy", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.audio.energy; }} },
        { "audio.bass", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.audio.bass; }} },
        { "audio.mid", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.audio.mid; }} },
        { "audio.high", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.audio.high; }} },
        { "audio.warpResponse", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.audio.warpResponse; }} },
        { "audio.feedbackResponse", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.audio.feedbackResponse; }} },
        { "audio.blurResponse", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.audio.blurResponse; }} },
        { "audio.colorResponse", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.audio.colorResponse; }} },
        { "audio.glitchResponse", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.audio.glitchResponse; }} },
        { "audio.beatSync", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.audio.beatSync; }} },
        { "audio.lfoRate", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.audio.lfoRate; }} },
        { "audio.inputGain", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.audio.inputGain; }} },
        { "audio.bassGain", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.audio.bassGain; }} },
        { "audio.midGain", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.audio.midGain; }} },
        { "audio.highGain", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.audio.highGain; }} },
        { "audio.reactiveDrive", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.audio.reactiveDrive; }} },
        { "audio.smoothAmount", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.audio.smoothAmount; }} },
        { "audio.enabled", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.audio.enabled; }} },

        // Legacy flat names for Audio
        { "energy", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.audio.energy; }} },
        { "bass", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.audio.bass; }} },
        { "mid", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.audio.mid; }} },
        { "high", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.audio.high; }} },
        { "audioWarpResponse", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.audio.warpResponse; }} },
        { "audioFeedbackResponse", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.audio.feedbackResponse; }} },
        { "audioBlurResponse", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.audio.blurResponse; }} },
        { "audioColorResponse", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.audio.colorResponse; }} },
        { "audioGlitchResponse", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.audio.glitchResponse; }} },
        { "audioBeatSync", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.audio.beatSync; }} },
        { "audioLfoRate", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.audio.lfoRate; }} },
        { "audioInputGain", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.audio.inputGain; }} },
        { "audioBassGain", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.audio.bassGain; }} },
        { "audioMidGain", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.audio.midGain; }} },
        { "audioHighGain", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.audio.highGain; }} },
        { "audioReactiveDrive", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.audio.reactiveDrive; }} },
        { "audioSmoothAmount", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.audio.smoothAmount; }} },

        // ============================================================================
        // TEMPORAL DOMAIN
        // ============================================================================
        { "temporal.feedbackAmount", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.temporal.feedbackAmount; }} },
        { "temporal.trailStrength", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.temporal.trailStrength; }} },
        { "temporal.temporalAccumulation", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.temporal.temporalAccumulation; }} },
        { "temporal.feedbackDecay", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.temporal.feedbackDecay; }} },
        { "temporal.recursiveBlend", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.temporal.recursiveBlend; }} },
        { "temporal.enableFeedback", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.temporal.enableFeedback; }} },
        { "temporal.enableTemporal", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.temporal.enableTemporal; }} },

        // Legacy flat names for Temporal
        { "feedbackAmount", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.temporal.feedbackAmount; }} },
        { "trailStrength", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.temporal.trailStrength; }} },
        { "temporalAccumulation", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.temporal.temporalAccumulation; }} },
        { "feedbackDecay", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.temporal.feedbackDecay; }} },
        { "recursiveBlend", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.temporal.recursiveBlend; }} },
        { "enableFeedback", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.temporal.enableFeedback; }} },
        { "enableTemporal", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.temporal.enableTemporal; }} },

        // ============================================================================
        // BLENDING DOMAIN
        // ============================================================================
        { "blending.blendModeProcedural", {ParamBinding::Type::Int, nullptr, [](VisualControls& c) -> int& { return c.blending.blendModeProcedural; }} },
        { "blending.blendModeVideo", {ParamBinding::Type::Int, nullptr, [](VisualControls& c) -> int& { return c.blending.blendModeVideo; }} },
        { "blending.blendModeFeedback", {ParamBinding::Type::Int, nullptr, [](VisualControls& c) -> int& { return c.blending.blendModeFeedback; }} },
        { "blending.blendProceduralMix", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.blending.blendProceduralMix; }} },
        { "blending.blendVideoMix", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.blending.blendVideoMix; }} },
        { "blending.blendFeedbackMix", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.blending.blendFeedbackMix; }} },
        { "blending.enableBlending", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.blending.enableBlending; }} },

        // Legacy flat names for Blending
        { "blendModeProcedural", {ParamBinding::Type::Int, nullptr, [](VisualControls& c) -> int& { return c.blending.blendModeProcedural; }} },
        { "blendModeVideo", {ParamBinding::Type::Int, nullptr, [](VisualControls& c) -> int& { return c.blending.blendModeVideo; }} },
        { "blendModeFeedback", {ParamBinding::Type::Int, nullptr, [](VisualControls& c) -> int& { return c.blending.blendModeFeedback; }} },
        { "blendProceduralMix", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.blending.blendProceduralMix; }} },
        { "blendVideoMix", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.blending.blendVideoMix; }} },
        { "blendFeedbackMix", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.blending.blendFeedbackMix; }} },
        { "enableBlending", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.blending.enableBlending; }} },

        // ============================================================================
        // CAMERA DOMAIN
        // ============================================================================
        { "camera.zoom", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.camera.zoom; }} },
        { "camera.panX", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.camera.panX; }} },
        { "camera.panY", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.camera.panY; }} },
        { "camera.rotation", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.camera.rotation; }} },
        { "camera.enableMovement", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.camera.enableMovement; }} },

        // Legacy flat names for Camera
        { "cameraZoom", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.camera.zoom; }} },
        { "cameraPanX", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.camera.panX; }} },
        { "cameraPanY", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.camera.panY; }} },
        { "cameraRotation", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.camera.rotation; }} },
        { "enableCameraMovement", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.camera.enableMovement; }} },

        // ============================================================================
        // GRID DOMAIN
        // ============================================================================
        { "grid.enabled", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.grid.enabled; }} },
        { "grid.mode", {ParamBinding::Type::Int, nullptr, [](VisualControls& c) -> int& { return c.grid.mode; }} },
        { "grid.count", {ParamBinding::Type::Int, nullptr, [](VisualControls& c) -> int& { return c.grid.count; }} },
        { "grid.rows", {ParamBinding::Type::Int, nullptr, [](VisualControls& c) -> int& { return c.grid.rows; }} },
        { "grid.columns", {ParamBinding::Type::Int, nullptr, [](VisualControls& c) -> int& { return c.grid.columns; }} },
        { "grid.mirrorCells", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.grid.mirrorCells; }} },
        { "grid.showLines", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.grid.showLines; }} },
        { "grid.lineWidth", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.grid.lineWidth; }} },
        { "grid.lineIntensity", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.grid.lineIntensity; }} },
        { "grid.lineColor", {ParamBinding::Type::Vec3, nullptr, nullptr, nullptr, [](VisualControls& c) -> glm::vec3& { return c.grid.lineColor; }} },

        // Legacy flat names for Grid
        { "enableGrid", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.grid.enabled; }} },
        { "gridMode", {ParamBinding::Type::Int, nullptr, [](VisualControls& c) -> int& { return c.grid.mode; }} },
        { "gridCount", {ParamBinding::Type::Int, nullptr, [](VisualControls& c) -> int& { return c.grid.count; }} },
        { "gridRows", {ParamBinding::Type::Int, nullptr, [](VisualControls& c) -> int& { return c.grid.rows; }} },
        { "gridColumns", {ParamBinding::Type::Int, nullptr, [](VisualControls& c) -> int& { return c.grid.columns; }} },
        { "gridMirrorCells", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.grid.mirrorCells; }} },
        { "gridShowLines", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.grid.showLines; }} },
        { "gridLineWidth", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.grid.lineWidth; }} },
        { "gridLineIntensity", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.grid.lineIntensity; }} },
        { "gridLineColor", {ParamBinding::Type::Vec3, nullptr, nullptr, nullptr, [](VisualControls& c) -> glm::vec3& { return c.grid.lineColor; }} },

        // ============================================================================
        // SYSTEM DOMAIN
        // ============================================================================
        { "system.enableFXAA", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.system.enableFXAA; }} },
        { "system.fxaaQualitySubpix", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.system.fxaaQualitySubpix; }} },
        { "system.fxaaQualityEdgeThreshold", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.system.fxaaQualityEdgeThreshold; }} },
        { "system.fxaaQualityEdgeThresholdMin", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.system.fxaaQualityEdgeThresholdMin; }} },
        // enableAudioReactive is always ON, not exposed

        // Legacy flat names for System
        { "enableFXAA", {ParamBinding::Type::Bool, nullptr, nullptr, [](VisualControls& c) -> bool& { return c.system.enableFXAA; }} },
        { "fxaaQualitySubpix", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.system.fxaaQualitySubpix; }} },
        { "fxaaQualityEdgeThreshold", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.system.fxaaQualityEdgeThreshold; }} },
        { "fxaaQualityEdgeThresholdMin", {ParamBinding::Type::Float, [](VisualControls& c) -> float& { return c.system.fxaaQualityEdgeThresholdMin; }} },
    };

    return map;
}
