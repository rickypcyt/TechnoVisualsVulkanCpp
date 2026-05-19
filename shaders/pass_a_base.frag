#version 450

// PASS A — BASE LAYER: Base sampling
// Responsibilities: video/procedural mixing, temporal interpolation, grayscale, sharpen, upscaling
// CAPA 1 - BASE (inferior): Procedural Controls + Post FX

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

// Unified UBO - all parameters in single binding
layout(set = 0, binding = 0, std140) uniform GlobalParamsUBO {
    // FrameUBO
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

    // ColorPassUBO
    vec4 primaryColor;
    vec4 secondaryColor;
    float colorBlend;
    int mode;
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
    float grayscaleAmount;

    // CRTPassUBO
    float crtCurvature;
    float crtHorizontalCurvature;
    float crtScanlineIntensity;
    float crtMaskIntensity;
    float crtVignette;
    float crtFishEye;
    float analogScanlineFocus;
    float analogMaskBalance;
    int enablePostCrtCurvature;
    int enablePostScanMask;
    int enablePostVignette;
    int enablePostFishEye;

    // GlitchPassUBO
    float glitchAmount;
    float glitchDatamosh;
    float glitchRGBSplit;
    float glitchScanlineBreak;
    float glitchJitter;
    float glitchTearing;
    float glitchPixelSort;
    float glitchBufferCorruption;
    float aberrationAmount;
    int enablePostGlitch;
    int enablePostAberration;

    // TemporalPassUBO
    float feedbackAmount;
    float trailStrength;
    float temporalAccumulation;
    float feedbackDecay;
    float recursiveBlend;
    float frameAccumulation;
    float slowMotionFactor;
    float temporalInterpolation;
    int enableFeedback;
    int enableTemporal;

    // BloomPassUBO
    float bloomIntensity;
    float bloomThreshold;
    int enablePostBloom;

    // DistortionPassUBO
    float uvWarpStrength;
    float rippleStrength;
    float rippleFrequency;
    float swirlStrength;
    float displacementAmount;
    float kaleidoSegments;
    float tunnelDepth;
    float tunnelCurvature;
    float bendAmount;
    int enableDistortion;
    int enablePostBend;

    // BlurPassUBO
    float gaussianBlur;
    float directionalBlur;
    float directionalBlurAngle;
    float zoomBlur;
    float motionBlur;
    float temporalBlur;
    int enableBlurMotion;

    // SharpenPassUBO
    float unsharpMask;
    float casAmount;
    float localContrast;
    float sharpenAmount;
    int enableSharpen;

    // VideoPassUBO
    float videoMix;
    float videoAvailable;
    int blendModeVideo;
    float blendVideoMix;

    // BlendingPassUBO
    int blendModeProcedural;
    int blendModeFeedback;
    float blendProceduralMix;
    float blendFeedbackMix;
    int enableBlending;

    // GrainPassUBO
    float grainStrength;
    int enablePostGrain;

    // PostFXPassUBO
    float upscaleEnabled;
    int enablePostColorBalance;
    int enableColorGrading;
    int enableAnalog;
    int enableAudioReactive;

    // ExtraEffectsPassUBO
    float pixelateAmount;
    float strobeSpeed;
    float thresholdLevel;
    float slowZoomAmount;
    int enablePixelate;
    int enableStrobe;
    int enableThreshold;
    int enableSlowZoom;
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

    // NLEExportPassUBO
    int nleOutputWidth;
    int nleOutputHeight;
    float nleGrayscale;
    float nleBrightness;
    float nleContrast;
    float nleSaturation;
} ubo;

layout(set = 1, binding = 0) uniform sampler2D videoTex;
layout(set = 1, binding = 1) uniform sampler2D videoTexPrev;

const float PI = 3.1415926535;

// Catmull-Rom bicubic interpolation
float catmullRom(float x) {
    x = abs(x);
    if (x <= 1.0) {
        return 1.5 * x * x * x - 2.5 * x * x + 1.0;
    } else if (x < 2.0) {
        return -0.5 * x * x * x + 2.5 * x * x - 4.0 * x + 2.0;
    }
    return 0.0;
}

// FSR RCAS helpers
float fsrMin3(float a, float b, float c) {
    return min(a, min(b, c));
}

float fsrMax3(float a, float b, float c) {
    return max(a, max(b, c));
}

float fsrRcas(float c, float a, float b, float d, float e, float sharpness) {
    float mn = fsrMin3(a, b, d);
    float mx = fsrMax3(a, b, d);
    float amp = clamp(min(mn, 2.0 - mx) / max(mx, 0.001), 0.0, 1.0);
    float peak = -1.0 / mix(8.0, 5.0, clamp(sharpness, 0.0, 1.0));
    float w = amp / peak;
    float r = (-mx) * w + c;
    r = min(w + r, mx);
    r = max(r, mn);
    return r;
}

// Bicubic interpolation
vec4 fsrBicubic(vec2 st) {
    vec2 texSize = max(vec2(textureSize(videoTex, 0)), vec2(1.0));
    vec2 texel = 1.0 / texSize;
    
    vec2 coord = st * texSize - 0.5;
    vec2 f = fract(coord);
    vec2 i = floor(coord);
    
    vec2 w0 = -0.5 * f * f * f + f * f - 0.5 * f;
    vec2 w1 =  1.5 * f * f * f - 2.5 * f * f + 1.0;
    vec2 w2 = -1.5 * f * f * f + 2.0 * f * f + 0.5 * f;
    vec2 w3 =  0.5 * f * f * f - 0.5 * f * f;
    
    vec2 s0 = w0 + w2;
    vec2 s1 = w1 + w3;
    
    vec2 offset0 = w2 / (w0 + w2 + 0.0001);
    vec2 offset1 = w3 / (w1 + w3 + 0.0001);
    
    vec2 samplePos0 = (i - 1.0 + offset0) * texel;
    vec2 samplePos1 = (i + 0.0 + offset1) * texel;
    
    vec3 col00 = texture(videoTex, clamp(samplePos0 + vec2(0.0, 0.0) * texel, 0.0, 1.0)).rgb;
    vec3 col01 = texture(videoTex, clamp(samplePos0 + vec2(1.0, 0.0) * texel, 0.0, 1.0)).rgb;
    vec3 col02 = texture(videoTex, clamp(samplePos1 + vec2(0.0, 0.0) * texel, 0.0, 1.0)).rgb;
    vec3 col03 = texture(videoTex, clamp(samplePos1 + vec2(1.0, 0.0) * texel, 0.0, 1.0)).rgb;
    vec3 col10 = texture(videoTex, clamp(samplePos0 + vec2(0.0, 1.0) * texel, 0.0, 1.0)).rgb;
    vec3 col11 = texture(videoTex, clamp(samplePos0 + vec2(1.0, 1.0) * texel, 0.0, 1.0)).rgb;
    vec3 col12 = texture(videoTex, clamp(samplePos1 + vec2(0.0, 1.0) * texel, 0.0, 1.0)).rgb;
    vec3 col13 = texture(videoTex, clamp(samplePos1 + vec2(1.0, 1.0) * texel, 0.0, 1.0)).rgb;
    
    vec3 row0 = mix(col00, col01, s0.x) + mix(col02, col03, s1.x);
    vec3 row1 = mix(col10, col11, s0.x) + mix(col12, col13, s1.x);
    
    vec3 bicubic = (mix(row0, row1, s0.y) + mix(row0, row1, s1.y)) / 2.0;
    
    return vec4(bicubic, 1.0);
}

// Edge-aware sampling with RCAS
vec4 sampleEdgeAware(vec2 st) {
    vec4 bicubic = fsrBicubic(st);

    float sharpness = clamp(ubo.sharpenAmount * 0.5 + 0.2, 0.0, 1.0);

    if (sharpness <= 0.001) {
        return bicubic;
    }

    vec2 texSize = max(vec2(textureSize(videoTex, 0)), vec2(1.0));
    vec2 texel = 1.0 / texSize;

    vec3 c = bicubic.rgb;
    vec3 t  = texture(videoTex, clamp(st + vec2( 0.0, -1.0) * texel, 0.0, 1.0)).rgb;
    vec3 b  = texture(videoTex, clamp(st + vec2( 0.0,  1.0) * texel, 0.0, 1.0)).rgb;
    vec3 l  = texture(videoTex, clamp(st + vec2(-1.0,  0.0) * texel, 0.0, 1.0)).rgb;
    vec3 r  = texture(videoTex, clamp(st + vec2( 1.0,  0.0) * texel, 0.0, 1.0)).rgb;

    vec3 result;
    result.r = fsrRcas(c.r, t.r, b.r, l.r, r.r, sharpness);
    result.g = fsrRcas(c.g, t.g, b.g, l.g, r.g, sharpness);
    result.b = fsrRcas(c.b, t.b, b.b, l.b, r.b, sharpness);

    return vec4(result, 1.0);
}

// Procedural rendering modes
vec4 renderMode0(vec2 st) {
    return mix(ubo.primaryColor, ubo.secondaryColor, ubo.colorBlend);
}

vec4 renderMode1(vec2 st) {
    float baseTime = ubo.time;
    float fastPhase = baseTime * 6.2831853;          // wrap roughly every second (since CPU already wraps time)
    float microPhase = fract(baseTime * 0.125) * 6.2831853;
    float uvPhase = dot(st, vec2(11.37, 17.91));

    vec2 centered = st - 0.5;
    float radius = length(centered);
    float angle = atan(centered.y, centered.x) + fastPhase * 0.08;

    // Flowing UV space
    vec2 flow = centered;
    flow += 0.12 * vec2(sin(fastPhase + st.y * 18.0), cos(fastPhase + st.x * 18.0));
    flow += 0.05 * vec2(cos(microPhase + uvPhase * 6.0), sin(microPhase - uvPhase * 5.0));

    float spiral = 0.5 + 0.5 * sin(angle * 10.0 + radius * 30.0 + fastPhase * 0.4);
    float rings = 0.5 + 0.5 * cos((flow.x + flow.y) * 20.0 - fastPhase * 0.6);
    float twirl = 0.5 + 0.5 * sin(radius * 55.0 - microPhase * 5.0 + uvPhase * 10.0);

    vec3 color = vec3(spiral, rings, twirl);
    color *= 1.0 - smoothstep(0.48, 0.75, radius);

    return vec4(color, 1.0);
}

// Anaglyph Assembly - Mode 2
const int kAnaglyphLayerCount = 3;
const int kAnaglyphMarchSteps = 32;
const float kAnaglyphRange = 1.2;
const float kAnaglyphRadius = 0.4;
const float kAnaglyphBlend = 1.5;
const float kAnaglyphBalance = 1.5;
const float kAnaglyphFalloff = 1.9;
const float kAnaglyphDivergence = 0.05;
const float kAnaglyphFieldOfView = 1.5;

float anaglyphRandom(vec2 p) {
    return fract(1e4 * sin(17.0 * p.x + p.y * 0.1) * (0.1 + abs(sin(p.y * 13.0 + p.x))));
}

mat2 anaglyphRot(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, -s, s, c);
}

float anaglyphSmoothMin(float a, float b, float r) {
    float h = clamp(0.5 + 0.5 * (b - a) / r, 0.0, 1.0);
    return mix(b, a, h) - r * h * (1.0 - h);
}

float anaglyphSimpleNoise(vec3 p) {
    return fract(sin(dot(p, vec3(12.9898, 78.233, 45.164))) * 43758.5453);
}

float anaglyphAudioEnergy() {
    float energy = ubo.bass * 0.5 + ubo.mid * 0.3 + ubo.high * 0.2;
    // Add time-based animation when audio energy is low
    float timePulse = 0.5 + 0.5 * sin(ubo.time * 2.0);
    energy = max(energy, timePulse * 0.4);
    return max(energy, 0.25);
}

float anaglyphAssemblyFactor() {
    return smoothstep(0.12, 0.85, anaglyphAudioEnergy());
}

vec3 anaglyphApplyCamera(vec3 pos) {
    float tiltY = -PI * 0.25 + (sin(ubo.time * 0.35) + ubo.mid * 0.8) * 0.25;
    float tiltX = -PI * 0.5 + (cos(ubo.time * 0.27) + ubo.bass * 1.2) * 0.2;
    float twist = sin(ubo.time * 0.18 + ubo.high * 1.8) * 0.35;

    pos.yz *= anaglyphRot(tiltY);
    pos.xz *= anaglyphRot(tiltX);
    pos.xy *= anaglyphRot(twist);
    return pos;
}

float anaglyphCoreGeometry(vec3 pos) {
    pos = anaglyphApplyCamera(pos);
    float a = 1.0;
    float scene = 1.0;
    float t = ubo.time * 0.2;
    float wave = 1.0 + 0.2 * sin(t * 8.0 - length(pos) * 2.0 + anaglyphAudioEnergy() * 2.5);
    t = floor(t) + pow(fract(t), 0.5);

    for (int i = kAnaglyphLayerCount; i > 0; --i) {
        float rotSeed = cos(t) * kAnaglyphBalance / a + a * 2.0 + t;
        pos.xy *= anaglyphRot(rotSeed);
        pos.zy *= anaglyphRot(sin(t) * kAnaglyphBalance / a + a * 2.0 + t);
        pos = abs(pos) - kAnaglyphRange * a * wave;
        scene = anaglyphSmoothMin(scene, length(pos) - kAnaglyphRadius * a, kAnaglyphBlend * a);
        a /= kAnaglyphFalloff;
    }

    return scene;
}

float anaglyphZoneThreshold(vec3 pos, float assemblyFactor) {
    float normalizedHeight = clamp((pos.y + 2.5) / 5.0, 0.0, 1.0);
    float threshold = 0.08 + normalizedHeight * 0.4;
    return threshold;
}

float anaglyphMap(vec3 pos) {
    float assemblyFactor = anaglyphAssemblyFactor();
    float threshold = anaglyphZoneThreshold(pos, assemblyFactor);

    if (assemblyFactor <= threshold) {
        return 1000.0;
    }

    float core = anaglyphCoreGeometry(pos);
    float transition = smoothstep(threshold - 0.1, threshold + 0.1, assemblyFactor);
    return mix(1000.0, core, transition);
}

vec3 anaglyphCalcNormal(vec3 pos) {
    const float eps = 0.003;
    vec4 q = vec4(eps, -eps, -eps, 0.0);
    return normalize(vec3(
        anaglyphMap(pos + q.xzz) - anaglyphMap(pos - q.xzz),
        anaglyphMap(pos + q.zxz) - anaglyphMap(pos - q.zxz),
        anaglyphMap(pos + q.zzx) - anaglyphMap(pos - q.zzx)
    ));
}

vec3 anaglyphLook(vec3 eye, vec3 target, vec2 anchor, float fov) {
    vec3 forward = normalize(target - eye);
    vec3 right = normalize(cross(forward, vec3(0.0, 1.0, 0.0)));
    vec3 up = normalize(cross(right, forward));
    return normalize(forward * fov + right * anchor.x + up * anchor.y);
}

vec4 anaglyphShadeEye(vec3 eye, vec3 ray, vec2 anchor) {
    float dither = anaglyphRandom(ray.xy + fract(vec2(ubo.time)));
    float travel = 0.02 + dither * 0.05;

    for (int i = 0; i < kAnaglyphMarchSteps; ++i) {
        vec3 pos = eye + ray * travel;
        float dist = anaglyphMap(pos);

        if (dist < 0.005) {
            vec3 normal = anaglyphCalcNormal(pos);
            vec3 lightDir = normalize(vec3(-0.6, 0.8, 0.4));
            float diff = max(dot(normal, lightDir), 0.0);

            float assemblyFactor = anaglyphAssemblyFactor();
            vec3 basePalette = mix(ubo.primaryColor.rgb, ubo.secondaryColor.rgb, clamp(0.35 + assemblyFactor * 0.5, 0.0, 1.0));

            vec3 color = basePalette * (0.3 + diff * (0.9 + ubo.energy * 0.4));

            float alpha = clamp(0.5 + diff * 0.3 + assemblyFactor * 0.3, 0.0, 1.0);
            return vec4(clamp(color, 0.0, 1.0), alpha);
        }

        travel += dist * 0.9;
        if (travel > 12.0) {
            break;
        }
    }

    float assemblyFactor = anaglyphAssemblyFactor();
    float horizon = clamp(anchor.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 bg = mix(ubo.primaryColor.rgb * 0.1, ubo.secondaryColor.rgb * 0.25, horizon);
    bg += vec3(0.05, 0.08, 0.12) * (0.8 - clamp(length(anchor), 0.0, 1.2)) * (0.3 + assemblyFactor * 0.5);

    return vec4(clamp(bg, 0.0, 1.0), 0.15 + assemblyFactor * 0.2);
}

vec4 renderMode2(vec2 st) {
    vec2 anchor = st * 2.0;
    vec3 target = vec3(0.0);

    vec3 eyeLeft = vec3(-kAnaglyphDivergence, 0.0, 4.0);
    vec3 eyeRight = vec3(kAnaglyphDivergence, 0.0, 4.0);

    vec3 rayLeft = anaglyphLook(eyeLeft, target, anchor, kAnaglyphFieldOfView);
    vec3 rayRight = anaglyphLook(eyeRight, target, anchor, kAnaglyphFieldOfView);

    vec4 leftSample = anaglyphShadeEye(eyeLeft, rayLeft, anchor);
    vec4 rightSample = anaglyphShadeEye(eyeRight, rayRight, anchor);

    vec3 color = vec3(leftSample.r, rightSample.g, rightSample.b);

    float assemblyFactor = anaglyphAssemblyFactor();
    color += vec3(0.08, 0.05, 0.1) * assemblyFactor * 0.2;
    color = clamp(color, 0.0, 1.0);
    color = pow(color, vec3(1.0 / 2.2));

    float alpha = clamp(max(leftSample.a, rightSample.a), 0.0, 1.0);
    return vec4(color, alpha);
}

vec4 dispatchMode(int m, vec2 st) {
    if (m == 0) return renderMode0(st);
    if (m == 1) return renderMode1(st);
    if (m == 2) return renderMode2(st);
    return vec4(0.0);
}

void main() {
    // Sample video texture
    vec4 videoColor = vec4(texture(videoTex, uv).rgb, 1.0);

    // Get procedural rendering
    vec4 proceduralColor = dispatchMode(ubo.mode, uv);

    // Mix video and procedural based on videoMix parameter
    // videoMix = 0.0: only procedural, videoMix = 1.0: only video
    vec4 mixedColor = mix(proceduralColor, videoColor, ubo.videoMix * ubo.videoAvailable);

    outColor = mixedColor;
}
