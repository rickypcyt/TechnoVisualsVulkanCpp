#version 450

// PASS G — Output post
// Responsibilities: bloom (threshold + blur), CRT scanlines/mask/vignette, grain, final mix

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
    float strobeSpeed;
} ubo;

layout(set = 0, binding = 1) uniform sampler2D inputTex;
layout(set = 0, binding = 2) uniform sampler2D proceduralTex;

const float PI = 3.1415926535;

float hash21(vec2 p) {
    p = fract(p * vec2(443.897, 441.423));
    p += dot(p, p.yx + 19.19);
    return fract(p.x * p.y);
}

// Simple 3x3 blur for bloom
vec3 blur3x3(sampler2D tex, vec2 uv) {
    vec3 c = vec3(0.0);
    vec2 t = 1.0 / ubo.resolution;
    
    c += texture(tex, uv + t*vec2(-1,-1)).rgb;
    c += texture(tex, uv + t*vec2( 0,-1)).rgb;
    c += texture(tex, uv + t*vec2( 1,-1)).rgb;
    
    c += texture(tex, uv + t*vec2(-1, 0)).rgb;
    c += texture(tex, uv).rgb;
    c += texture(tex, uv + t*vec2( 1, 0)).rgb;
    
    c += texture(tex, uv + t*vec2(-1, 1)).rgb;
    c += texture(tex, uv + t*vec2( 0, 1)).rgb;
    c += texture(tex, uv + t*vec2( 1, 1)).rgb;
    
    return c / 9.0;
}

// Blend modes
vec3 blendMode(vec3 base, vec3 layer, int mode) {
    if (mode == 0) {
        return clamp(base + layer, 0.0, 1.5);
    } else if (mode == 1) {
        return 1.0 - (1.0 - base) * (1.0 - layer);
    } else if (mode == 2) {
        return base * layer;
    } else if (mode == 3) {
        return mix(base, 2.0 * base * layer + base * base * (1.0 - 2.0 * layer), 0.5);
    } else if (mode == 4) {
        return abs(base - layer);
    } else if (mode == 5) {
        vec3 exprLow = 2.0 * base * layer + base * base * (1.0 - 2.0 * layer);
        vec3 exprHigh = sqrt(base) * (2.0 * layer - 1.0) + 2.0 * base * (1.0 - layer);
        vec3 selector = step(vec3(0.5), layer);
        return mix(exprLow, exprHigh, selector);
    }
    return layer;
}

void main() {
    float timeScale = max(ubo.slowMotionFactor, 0.1);
    float effectTime = ubo.time / timeScale;
    
    vec2 centered = uv * 2.0 - 1.0;
    vec3 color = texture(inputTex, uv).rgb;
    
    // Bloom
    if (ubo.bloomIntensity > 0.0001) {
        float bright = max(max(color.r, color.g), color.b);
        float threshold = clamp(ubo.bloomThreshold, 0.0, 1.0);
        float mask = smoothstep(threshold, 1.0, bright);
        vec3 bloom = blur3x3(inputTex, uv);
        color += bloom * mask * ubo.bloomIntensity * 0.4;
    }
    
    // Analog bloom (use bloomIntensity as fallback)
    if (ubo.bloomIntensity > 0.0001) {
        vec3 bloom = blur3x3(inputTex, uv);
        color = mix(color, bloom, clamp(ubo.bloomIntensity * 0.4, 0.0, 1.0));
    }
    
    // CRT scanlines
    float scanlineFreq = 240.0 * (ubo.resolution.y / 480.0);
    float scanline = mix(1.0, 0.5 + 0.5 * sin((uv.y + effectTime * 0.2) * PI * scanlineFreq), clamp(ubo.crtScanlineIntensity, 0.0, 1.0));
    color *= scanline;
    
    // Analog scanline focus
    if (ubo.analogScanlineFocus > 0.0001) {
        float lfo = sin(effectTime * 0.01);
        float scan = mix(1.0, 0.4 + 0.6 * sin((uv.y + lfo * 0.01) * PI * 400.0), ubo.analogScanlineFocus);
        color *= scan;
    }
    
    // CRT mask pattern
    float maskPattern = mix(1.0,
                            (0.8 + 0.2 * sin(uv.x * PI * ubo.resolution.x)) *
                            (0.8 + 0.2 * cos(uv.y * PI * ubo.resolution.y)),
                            clamp(ubo.crtMaskIntensity, 0.0, 1.0));
    color *= maskPattern;
    
    // Analog mask balance
    if (ubo.analogMaskBalance > 0.0001) {
        float mask = mix(1.0,
                         (0.7 + 0.3 * sin(uv.x * PI * 960.0)) *
                         (0.7 + 0.3 * cos(uv.y * PI * 540.0)),
                         ubo.analogMaskBalance);
        color *= mask;
    }
    
    // Vignette
    float radius = length(centered);
    color = mix(color,
               color * (1.0 - pow(clamp(radius, 0.0, 1.0), 2.0)),
               clamp(ubo.crtVignette, 0.0, 1.0));
    
    // Grain
    if (ubo.grainStrength > 0.0001) {
        float g = hash21(uv * 2000.0 + effectTime * 30.0) - 0.5;
        color += g * ubo.grainStrength * 0.08;
    }
    
    // Blend with procedural
    vec3 procColor = texture(proceduralTex, uv).rgb;
    vec3 blendProc = blendMode(color, procColor, ubo.blendModeProcedural);
    vec3 blendVideo = blendMode(procColor, color, ubo.blendModeVideo);
    
    color = mix(color, blendProc, clamp(ubo.blendProceduralMix, 0.0, 1.0));
    color = mix(color, blendVideo, clamp(ubo.blendVideoMix, 0.0, 1.0));
    
    // Strobe effect
    if (ubo.strobeSpeed > 0.0001) {
        float strobe = step(0.5, sin(effectTime * ubo.strobeSpeed * PI * 2.0));
        color = mix(color, vec3(0.0), strobe * 0.8);
    }

    // Final grayscale
    float finalLuma = dot(color, vec3(0.299, 0.587, 0.114));
    color = mix(color, vec3(finalLuma), clamp(ubo.grayscaleAmount, 0.0, 1.0));

    color = clamp(color, 0.0, 1.5);
    
    outColor = vec4(color, 1.0);
}
