#version 450

// PASS E — Signal degradation / stylization
// Responsibilities: glitch, analog/VHS effects, chromatic aberration

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
    float pixelateAmount;
    float strobeSpeed;
    float thresholdLevel;
    float slowZoomAmount;
} ubo;

layout(set = 0, binding = 1) uniform sampler2D inputTex;

const float PI = 3.1415926535;

float hash21(vec2 p) {
    p = fract(p * vec2(443.897, 441.423));
    p += dot(p, p.yx + 19.19);
    return fract(p.x * p.y);
}

vec3 sortComponents(vec3 v) {
    vec3 s = v;
    if (s.x > s.y) {
        s.xy = s.yx;
    }
    if (s.y > s.z) {
        s.yz = s.zy;
    }
    if (s.x > s.y) {
        s.xy = s.yx;
    }
    return s;
}

void main() {
    float timeScale = max(ubo.slowMotionFactor, 0.1);
    float effectTime = ubo.time / timeScale;
    
    vec2 centered = uv * 2.0 - 1.0;
    vec2 uvOut = uv;
    vec3 color = texture(inputTex, uv).rgb;
    
    // Check if any degradation effect is active
    bool hasGlitch = ubo.glitchDatamosh > 0.0001 || 
                     ubo.glitchJitter > 0.0001 || 
                     ubo.glitchRGBSplit > 0.0001 || 
                     ubo.glitchScanlineBreak > 0.0001 || 
                     ubo.glitchTearing > 0.0001 || 
                     ubo.glitchPixelSort > 0.0001 || 
                     ubo.glitchBufferCorruption > 0.0001 ||
                     ubo.grainStrength > 0.0001;

    if (!hasGlitch && ubo.aberrationAmount <= 0.0001) {
        outColor = vec4(color, 1.0);
        return;
    }

    // Glitch datamosh
    if (ubo.glitchDatamosh > 0.0) {
        uvOut.x += sin(centered.y * 80.0 + effectTime * 10.0) * ubo.glitchDatamosh * 0.02;
    }

    // Glitch jitter
    if (ubo.glitchJitter > 0.0) {
        uvOut += (hash21(uv * 400.0 + effectTime) - 0.5) * ubo.glitchJitter * 0.02;
    }

    uvOut = clamp(uvOut, 0.0, 1.0);
    
    // Sample with glitched UVs
    vec3 glitched = texture(inputTex, uvOut).rgb;
    
    // RGB split (glitch version)
    if (ubo.glitchRGBSplit > 0.0) {
        vec2 texel = ubo.glitchRGBSplit * 0.008 / ubo.resolution;
        glitched.r = texture(inputTex, clamp(uvOut + texel, 0.0, 1.0)).r;
        glitched.b = texture(inputTex, clamp(uvOut - texel, 0.0, 1.0)).b;
    }

    // Scanline break
    if (ubo.glitchScanlineBreak > 0.0) {
        float line = step(0.95, fract(uv.y * 200.0 + effectTime * 5.0));
        glitched *= 1.0 - line * ubo.glitchScanlineBreak;
    }

    // Tearing
    if (ubo.glitchTearing > 0.0) {
        float tear = step(0.7, hash21(vec2(uv.y * 10.0, effectTime)));
        glitched = mix(glitched, color, 1.0 - tear * ubo.glitchTearing);
    }

    // Pixel sort
    if (ubo.glitchPixelSort > 0.0) {
        glitched = mix(glitched, sortComponents(glitched), ubo.glitchPixelSort * 0.5);
    }

    // Buffer corruption
    if (ubo.glitchBufferCorruption > 0.0) {
        float rnd = hash21(uv * 600.0 + effectTime * 20.0);
        glitched = mix(glitched, vec3(rnd), ubo.glitchBufferCorruption * 0.3);
    }

    // Analog noise
    if (ubo.grainStrength > 0.0001) {
        float n = hash21(uv * 800.0 + effectTime * 60.0) - 0.5;
        glitched += n * ubo.grainStrength * 0.15;
    }

    // Chromatic aberration (main)
    if (ubo.aberrationAmount > 0.0001) {
        vec2 texel = ubo.aberrationAmount * centered * 0.01;
        glitched.r = texture(inputTex, clamp(uv + texel, 0.0, 1.0)).r;
        glitched.b = texture(inputTex, clamp(uv - texel, 0.0, 1.0)).b;
    }

    // Apply audio modulation to glitch intensity
    float intensity = clamp(ubo.glitchAmount + ubo.energy * 0.3, 0.0, 1.0);
    color = mix(color, glitched, intensity);
    
    outColor = vec4(color, 1.0);
}
