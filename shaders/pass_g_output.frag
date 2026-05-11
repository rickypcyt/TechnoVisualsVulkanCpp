#version 450

// PASS G — VJAY EXTRA LAYER: Combined
// Responsibilities: Detail VJAY (blur, sharpen, CAS), Audio VJAY (audio-reactive effects), Extra FX (pixelate, strobe, threshold, mirror, etc), Analog VJAY, Glitch VJAY, Blending VJAY
// CAPA 3 - VJAY EXTRA (superior): Efectos extra sobre VJAY BASICS

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
    // --- Post FX basicos ---
    float grayscaleAmount;
    float sharpenAmount;
    float upscaleEnabled;

    // --- Enable/Disable flags for post FX ---
    int enablePostCrtCurvature;
    int enablePostScanMask;
    int enablePostVignette;
    int enablePostFishEye;
    int enablePostBloom;
    int enablePostAberration;
    int enablePostGrain;
    int enablePostBend;
    int enablePostGlitch;
    int enablePostColorBalance;

    // --- Enable/Disable flags for VJAY BASICS ---
    int enableColorGrading;
    int enableFeedback;
    int enableDistortion;
    int enableBlurMotion;
    int enableSharpen;
    int enableGlitch;
    int enableBlending;
    int enableAnalog;
    int enableAudioReactive;
    int enableTemporal;
    int enablePixelate;
    int enableStrobe;
    int enableThreshold;
    int enableSlowZoom;

    // --- CRT ---
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
    int enableEdgeDetect;
    float edgeStrength;
    float edgeThreshold;
    float edgeBlend;
    vec3 edgeColor;

    int enableMirror;
    int enableInvert;
    int enablePosterize;
    int enableInfrared;
    int enableZoomPulse;
    int enableRGBShift;
    float mirrorAmount;
    float posterizeLevels;
    float zoomPulseAmount;
    float rgbShiftAmount;
} ubo;

layout(set = 0, binding = 1) uniform sampler2D inputTex;
layout(set = 0, binding = 2) uniform sampler2D proceduralTex;

const float PI = 3.1415926535;

float hash21(vec2 p) {
    p = fract(p * vec2(443.897, 441.423));
    p += dot(p, p.yx + 19.19);
    return fract(p.x * p.y);
}

float luminance(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
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
    if (ubo.enablePostBloom == 1 && ubo.bloomIntensity > 0.0001) {
        float bright = max(max(color.r, color.g), color.b);
        float threshold = clamp(ubo.bloomThreshold, 0.0, 1.0);
        float mask = smoothstep(threshold, 1.0, bright);
        vec3 bloom = blur3x3(inputTex, uv);
        color += bloom * mask * ubo.bloomIntensity * 0.4;
    }

    // Analog bloom (use bloomIntensity as fallback)
    if (ubo.enableAnalog == 1 && ubo.bloomIntensity > 0.0001) {
        vec3 bloom = blur3x3(inputTex, uv);
        color = mix(color, bloom, clamp(ubo.bloomIntensity * 0.4, 0.0, 1.0));
    }

    // CRT scanlines
    if (ubo.enablePostScanMask == 1) {
        float scanlineFreq = 240.0 * (ubo.resolution.y / 480.0);
        float scanline = mix(1.0, 0.5 + 0.5 * sin((uv.y + effectTime * 0.2) * PI * scanlineFreq), clamp(ubo.crtScanlineIntensity, 0.0, 1.0));
        color *= scanline;
    }
    
    // Analog scanline focus
    if (ubo.enableAnalog == 1 && ubo.analogScanlineFocus > 0.0001) {
        float lfo = sin(effectTime * 0.01);
        float scan = mix(1.0, 0.4 + 0.6 * sin((uv.y + lfo * 0.01) * PI * 400.0), ubo.analogScanlineFocus);
        color *= scan;
    }

    // Analog mask balance
    if (ubo.enableAnalog == 1 && ubo.analogMaskBalance > 0.0001) {
        float mask = mix(1.0,
                         (0.7 + 0.3 * sin(uv.x * PI * 960.0)) *
                         (0.7 + 0.3 * cos(uv.y * PI * 540.0)),
                         ubo.analogMaskBalance);
        color *= mask;
    }

    // Vignette
    if (ubo.enablePostVignette == 1) {
        float radius = length(centered);
        color = mix(color,
                   color * (1.0 - pow(clamp(radius, 0.0, 1.0), 2.0)),
                   clamp(ubo.crtVignette, 0.0, 1.0));
    }

    // Grain
    if (ubo.enablePostGrain == 1 && ubo.grainStrength > 0.0001) {
        float g = hash21(uv * 2000.0 + effectTime * 30.0) - 0.5;
        color += g * ubo.grainStrength * 0.08;
    }
    
    // Edge detection overlay (Sobel)
    if (ubo.enableEdgeDetect == 1 && (ubo.edgeStrength > 0.0001 || ubo.edgeBlend > 0.0001)) {
        vec2 texel = vec2(1.0) / max(ubo.resolution, vec2(1.0));
        float tl = luminance(texture(inputTex, uv + texel * vec2(-1.0,  1.0)).rgb);
        float  t = luminance(texture(inputTex, uv + texel * vec2( 0.0,  1.0)).rgb);
        float tr = luminance(texture(inputTex, uv + texel * vec2( 1.0,  1.0)).rgb);
        float  l = luminance(texture(inputTex, uv + texel * vec2(-1.0,  0.0)).rgb);
        float  r = luminance(texture(inputTex, uv + texel * vec2( 1.0,  0.0)).rgb);
        float bl = luminance(texture(inputTex, uv + texel * vec2(-1.0, -1.0)).rgb);
        float  b = luminance(texture(inputTex, uv + texel * vec2( 0.0, -1.0)).rgb);
        float br = luminance(texture(inputTex, uv + texel * vec2( 1.0, -1.0)).rgb);

        float gx = -tl - 2.0 * l - bl + tr + 2.0 * r + br;
        float gy = -tl - 2.0 * t - tr + bl + 2.0 * b + br;
        float edge = length(vec2(gx, gy)) * 0.125;
        edge = clamp(edge * ubo.edgeStrength, 0.0, 5.0);
        float threshold = clamp(ubo.edgeThreshold, 0.0, 1.0);
        edge = smoothstep(threshold, threshold + 0.5, edge);
        float blend = clamp(ubo.edgeBlend, 0.0, 1.0);
        vec3 edgeTint = mix(vec3(edge), ubo.edgeColor * edge, blend);
        color = mix(color, edgeTint, edge);
    }

    // Blend with procedural
    vec3 procColor = texture(proceduralTex, uv).rgb;
    vec3 blendProc = blendMode(color, procColor, ubo.blendModeProcedural);
    vec3 blendVideo = blendMode(procColor, color, ubo.blendModeVideo);

    color = mix(color, blendProc, clamp(ubo.blendProceduralMix, 0.0, 1.0));
    color = mix(color, blendVideo, clamp(ubo.blendVideoMix, 0.0, 1.0));
    
    // Strobe effect (uses color grading brightness as amplitude)
    if (ubo.enableStrobe == 1 && ubo.strobeSpeed > 0.0001) {
        float wave = sin(effectTime * ubo.strobeSpeed * PI * 2.0);
        float pulse = step(0.0, wave);
        float intensity = max(abs(ubo.gradeBrightness), 0.15);
        float brightnessScale = mix(1.0 - intensity, 1.0 + intensity, pulse);
        color *= clamp(brightnessScale, 0.0, 2.5);
    }

    // Final grayscale
    float finalLuma = dot(color, vec3(0.299, 0.587, 0.114));
    color = mix(color, vec3(finalLuma), clamp(ubo.grayscaleAmount, 0.0, 1.0));

    // Color balance (RGB Mix) - applied at very end, on top of all effects
    if (ubo.enablePostColorBalance == 1) {
        color *= ubo.colorBalance;
    }

    color = clamp(color, 0.0, 1.5);
    
    outColor = vec4(color, 1.0);
}
