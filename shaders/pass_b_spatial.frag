#version 450

// PASS B — BASE LAYER: Post FX CRT + Spatial
// Responsibilities: CRT curvature, fish eye, screen bend, UV warp
// CAPA 1 - BASE (inferior): Procedural Controls + Post FX

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

    // --- Enable/Disable flags for VJAY EXTRA ---
    int enablePixelate;
    int enableStrobe;
    int enableThreshold;
    int enableSlowZoom;

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

const float PI = 3.1415926535;

float hash21(vec2 p) {
    p = fract(p * vec2(443.897, 441.423));
    p += dot(p, p.yx + 19.19);
    return fract(p.x * p.y);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(a, b, u.x) + (c - a) * u.y * (1.0 - u.x) + (d - b) * u.x * u.y;
}

float fbm(vec2 p) {
    float value = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 2; ++i) {
        value += noise(p) * amp;
        p *= 2.0;
        amp *= 0.5;
    }
    return value;
}

vec2 kaleido(vec2 st, float segments) {
    if (segments <= 2.5) {
        return st;
    }
    vec2 p = st * 2.0 - 1.0;
    float radius = length(p);
    float angle = atan(p.y, p.x);
    float seg = 2.0 * PI / max(segments, 3.0);
    angle = mod(angle, seg);
    angle = abs(angle - seg * 0.5);
    vec2 dir = vec2(cos(angle), sin(angle));
    return dir * radius * 0.5 + 0.5;
}

vec2 curve(vec2 uv) {
    vec2 p = uv * 2.0 - 1.0;
    
    // CRT curvature
    if (ubo.enablePostCrtCurvature == 1) {
        float curvatureY = clamp(ubo.crtCurvature, 0.0, 0.8);
        float curvatureX = clamp(ubo.crtHorizontalCurvature, 0.0, 0.8);
        
        if (curvatureY > 0.0001 || curvatureX > 0.0001) {
            p.x = p.x / (1.0 + curvatureX * p.x * p.x);
            p.y = p.y / (1.0 + curvatureY * p.y * p.y);
        }
    }
    
    // Fish eye
    if (ubo.enablePostFishEye == 1) {
        float radius = length(p);
        if (ubo.crtFishEye != 0.0) {
            p *= 1.0 + ubo.crtFishEye * radius * radius;
        }
    }
    
    return p * 0.5 + 0.5;
}

void main() {
    float timeScale = max(ubo.slowMotionFactor, 0.1);
    float effectTime = ubo.time / timeScale;

    float audioEnv = clamp(ubo.energy, 0.0, 1.0);
    float lfo = sin(ubo.time * 0.05 * PI * 2.0);

    vec2 centered = uv * 2.0 - 1.0;
    vec2 uvOut = uv;

    // Only apply distortion effects if enabled
    if (ubo.enableDistortion == 1) {
        // UV warp
        float warp = ubo.uvWarpStrength * (1.0 + audioEnv * 0.5);
        if (warp > 0.0001) {
            uvOut += sin((uvOut.yx + ubo.time) * 8.0) * warp * 0.01;
        }

        // Ripple effect
        if (ubo.rippleStrength > 0.0001) {
            float radius = length(centered);
            float wave = sin(radius * max(ubo.rippleFrequency, 0.1) * 12.0 - ubo.time * 4.0);
            uvOut += normalize(centered + 0.0001) * wave * ubo.rippleStrength * 0.01;
        }

        // Swirl effect
        if (abs(ubo.swirlStrength) > 0.0001) {
            float angle = ubo.swirlStrength * length(centered) * 5.0;
            float c = cos(angle);
            float s = sin(angle);
            vec2 rotated = vec2(centered.x * c - centered.y * s, centered.x * s + centered.y * c);
            uvOut = rotated * 0.5 + 0.5;
        }

        // Kaleidoscope
        if (ubo.kaleidoSegments > 0.0) {
            uvOut = kaleido(uvOut, ubo.kaleidoSegments);
        }

        // Displacement
        if (ubo.displacementAmount > 0.0001) {
            vec2 n = vec2(fbm(centered * 5.0 + ubo.time), fbm(centered * 7.0 - ubo.time));
            uvOut += (n - 0.5) * ubo.displacementAmount * 0.05;
        }

        // Tunnel effect
        if (ubo.tunnelDepth > 0.0001) {
            vec2 p = centered;
            float radius = length(p) + 0.0001;
            float depth = pow(radius, 1.0 - clamp(ubo.tunnelDepth, 0.0, 1.0));
            float curve = 1.0 + ubo.tunnelCurvature * radius * radius;
            p = normalize(p) * depth * curve;
            uvOut = p * 0.5 + 0.5;
        }
    }

    // Bend effect
    if (ubo.enablePostBend == 1 && ubo.bendAmount > 0.0001) {
        float chaos = sin((centered.x + centered.y) * 60.0 + effectTime * 20.0);
        uvOut += vec2(chaos, cos(effectTime * 10.0 + centered.y * 50.0)) * ubo.bendAmount * 0.05;
    }

    // CRT curvature
    uvOut = curve(uvOut);

    // Pixelate effect
    if (ubo.enablePixelate == 1 && ubo.pixelateAmount > 0.0001) {
        float pixels = max(ubo.pixelateAmount * 100.0, 2.0);
        vec2 pixelatedUV = floor(uvOut * pixels) / pixels;
        uvOut = pixelatedUV;
    }

    // Slow zoom effect
    if (ubo.enableSlowZoom == 1 && ubo.slowZoomAmount > 0.0001) {
        vec2 zoomCenter = vec2(0.5);
        float zoomFactor = 1.0 - ubo.slowZoomAmount * 0.3;
        uvOut = zoomCenter + (uvOut - zoomCenter) * zoomFactor;
    }

    // Sample from input texture with transformed UVs
    uvOut = clamp(uvOut, 0.0, 1.0);
    vec3 color = texture(inputTex, uvOut).rgb;
    
    outColor = vec4(color, 1.0);
}
