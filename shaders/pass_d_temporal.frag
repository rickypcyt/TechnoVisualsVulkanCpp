#version 450

// PASS D — Temporal domain
// Responsibilities: feedback, trail accumulation, motion blur

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0, std140) uniform GlobalUBO {
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
    float frameAccumulation;
    float slowMotionFactor;
    float temporalInterpolation;
    int nleOutputWidth;
    int nleOutputHeight;
    float nleGrayscale;
    float nleBrightness;
    float nleContrast;
    float nleSaturation;
} ubo;

layout(set = 0, binding = 1) uniform sampler2D inputTex;
layout(set = 0, binding = 2) uniform sampler2D prevFrameTex;

void main() {
    vec2 centered = uv * 2.0 - 1.0;
    vec3 color = texture(inputTex, uv).rgb;
    
    float amount = clamp(ubo.feedbackAmount * (1.0 + ubo.energy * 0.5), 0.0, 1.0);
    
    if (amount <= 0.0001 && ubo.trailStrength <= 0.0001 && ubo.temporalAccumulation <= 0.0001) {
        outColor = vec4(color, 1.0);
        return;
    }
    
    vec3 accum = color;
    float decay = clamp(1.0 - ubo.feedbackDecay, 0.0, 1.0);
    
    // Spatial feedback with trails
    if (ubo.trailStrength > 0.0001 || ubo.temporalAccumulation > 0.0001) {
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
    if (ubo.recursiveBlend > 0.0001) {
        vec3 prevColor = texture(prevFrameTex, uv).rgb;
        accum = mix(accum, prevColor, ubo.recursiveBlend * 0.3);
    }
    
    // Frame accumulation (for long-exposure effects)
    if (ubo.frameAccumulation > 0.0001) {
        vec3 prevColor = texture(prevFrameTex, uv).rgb;
        accum = mix(accum, prevColor, ubo.frameAccumulation * 0.5);
    }
    
    outColor = vec4(accum, 1.0);
}
