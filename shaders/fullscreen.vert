#version 450

layout(location = 0) out vec2 uv;

layout(set = 0, binding = 0) uniform UBO {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec2 resolution;
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
    float videoMix;
    float videoAvailable;
    float grayscaleAmount;
    float sharpenAmount;
    float upscaleEnabled;
    float crtCurvature;
    float crtHorizontalCurvature;
    float crtScanlineIntensity;
    float crtMaskIntensity;
    float crtVignette;
    float crtFishEye;
    float bloomIntensity;
    float bloomThreshold;
    float aberrationAmount;
    float grainStrength;
    float bendAmount;
    float glitchAmount;
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
    float feedbackAmount;
    float trailStrength;
    float temporalAccumulation;
    float feedbackDecay;
    float recursiveBlend;
    float uvWarpStrength;
    float rippleStrength;
    float rippleFrequency;
    float swirlStrength;
    float displacementAmount;
    float kaleidoSegments;
    float tunnelDepth;
    float tunnelCurvature;
    float gaussianBlur;
    float directionalBlur;
    float directionalBlurAngle;
    float zoomBlur;
    float motionBlur;
    float temporalBlur;
    float unsharpMask;
    float casAmount;
    float localContrast;
    float glitchDatamosh;
    float glitchRGBSplit;
    float glitchScanlineBreak;
    float glitchJitter;
    float glitchTearing;
    float glitchPixelSort;
    float glitchBufferCorruption;
    int blendModeProcedural;
    int blendModeVideo;
    int blendModeFeedback;
    float blendProceduralMix;
    float blendVideoMix;
    float blendFeedbackMix;
    float analogScanlineFocus;
    float analogMaskBalance;
    float analogNoise;
    float analogBloom;
    float vhsDistortion;
    float analogChromaticAberration;
    float audioWarpResponse;
    float audioFeedbackResponse;
    float audioBlurResponse;
    float audioColorResponse;
    float audioGlitchResponse;
    float audioBeatSync;
    float audioLfoRate;
    float temporalInterpolation;
    float temporalBlendStrength;
    float slowMotionFactor;
    float frameAccumulation;
    // NLE Effect Chain parameters
    int nleOutputWidth;
    int nleOutputHeight;
    float nleGrayscale;
    float nleBrightness;
    float nleContrast;
    float nleSaturation;
} ubo;

const vec2 positions[3] = vec2[](
    vec2(-1.0, -1.0),
    vec2( 3.0, -1.0),
    vec2(-1.0,  3.0)
);

void main() {
    vec2 pos = positions[gl_VertexIndex];
    gl_Position = vec4(pos, 0.0, 1.0);
    
    // Apply NLE aspect ratio scaling
    vec2 baseUV = pos * 0.5 + 0.5;
    
    if (ubo.nleOutputWidth > 0 && ubo.nleOutputHeight > 0) {
        // Calculate aspect ratio correction
        float targetAspect = float(ubo.nleOutputWidth) / float(ubo.nleOutputHeight);
        float currentAspect = ubo.resolution.x / ubo.resolution.y;
        
        vec2 scale = vec2(1.0);
        if (targetAspect > currentAspect) {
            scale.x = currentAspect / targetAspect;
        } else {
            scale.y = targetAspect / currentAspect;
        }
        
        // Center the scaled UV
        vec2 offset = (1.0 - scale) * 0.5;
        uv = baseUV * scale + offset;
    } else {
        uv = baseUV;
    }
}
