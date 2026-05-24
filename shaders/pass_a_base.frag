#version 450
#extension GL_GOOGLE_include_directive : require

#include "shared_ubo.glsl"

const float PI = 3.1415926535;

#define uTime           ubo.time
#define uTempo          ubo.tempo
#define uEnergy         ubo.energy
#define uBass           ubo.bass
#define uMid            ubo.mid
#define uHigh           ubo.high
#define uPrimaryColor   ubo.primaryColor.rgb
#define uSecondaryColor ubo.secondaryColor.rgb

#include "procedural_anaglyph.glsl"

#undef uTime
#undef uTempo
#undef uEnergy
#undef uBass
#undef uMid
#undef uHigh
#undef uPrimaryColor
#undef uSecondaryColor

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform sampler2D videoTex;
layout(set = 1, binding = 1) uniform sampler2D videoTexPrev;
layout(set = 1, binding = 2) uniform sampler2D video2Tex;

float luminance(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

vec2 applyCamera(vec2 uv) {
    if (ubo.enableCameraMovement == 0) return uv;
    vec2 p = uv - 0.5;
    float c = cos(ubo.cameraRotation);
    float s = sin(ubo.cameraRotation);
    p = vec2(c * p.x - s * p.y, s * p.x + c * p.y);
    p /= max(ubo.cameraZoom, 0.0001);
    p += vec2(ubo.cameraPanX, ubo.cameraPanY);
    return p + 0.5;
}

vec3 sampleVideo(vec2 p) {
    return texture(videoTex, clamp(p, 0.0, 1.0)).rgb;
}

vec3 sampleVideo2(vec2 p) {
    return texture(video2Tex, clamp(p, 0.0, 1.0)).rgb;
}

vec3 sampleBilinear(vec2 p) {
    return sampleVideo(p);
}

vec3 blendTwoVideos(vec3 a, vec3 b, int mode, float t) {
    t = clamp(t, 0.0, 1.0);
    if (mode == 0) {
        // Mix / crossfade
        return mix(a, b, t);
    } else if (mode == 1) {
        // Add
        return clamp(a + b * t, 0.0, 1.0);
    } else if (mode == 2) {
        // Multiply
        return mix(a, a * b, t);
    } else if (mode == 3) {
        // Screen
        return mix(a, 1.0 - (1.0 - a) * (1.0 - b), t);
    } else if (mode == 4) {
        // Difference
        return mix(a, abs(a - b), t);
    }
    return mix(a, b, t);
}

vec3 blendProceduralMode(vec3 base, vec3 layer, int mode) {
    if (mode == 0) {
        // Add
        return clamp(base + layer, 0.0, 1.5);
    } else if (mode == 1) {
        // Screen
        return clamp(1.0 - (1.0 - base) * (1.0 - layer), 0.0, 1.0);
    } else if (mode == 2) {
        // Multiply
        return clamp(base * layer, 0.0, 1.0);
    } else if (mode == 3) {
        // Overlay
        vec3 low = 2.0 * base * layer;
        vec3 high = 1.0 - 2.0 * (1.0 - base) * (1.0 - layer);
        return clamp(mix(low, high, step(vec3(0.5), base)), 0.0, 1.0);
    } else if (mode == 4) {
        // Difference
        return clamp(abs(base - layer), 0.0, 1.0);
    } else if (mode == 5) {
        // Soft Light
        vec3 low = 2.0 * base * layer + base * base * (1.0 - 2.0 * layer);
        vec3 high = sqrt(clamp(base, vec3(0.0), vec3(1.0))) * (2.0 * layer - 1.0) + 2.0 * base * (1.0 - layer);
        return clamp(mix(low, high, step(vec3(0.5), layer)), 0.0, 1.0);
    }
    return mix(base, layer, 0.5);
}

vec3 applyProceduralBlend(vec3 base, vec3 layer, int mode, float amount) {
    float mixAmount = clamp(amount, 0.0, 2.0);
    vec3 blended = blendProceduralMode(base, layer, mode);
    vec3 result = mix(base, blended, min(mixAmount, 1.0));
    if (mixAmount > 1.0) {
        float extra = mixAmount - 1.0;
        result = mix(result, blended, clamp(extra, 0.0, 1.0));
    }
    return result;
}

vec3 sharpen3x3(vec2 p, float amount) {
    vec2 t = 1.0 / max(ubo.resolution, vec2(1.0));
    vec3 c  = sampleVideo(p);
    vec3 n  = sampleVideo(p + vec2( 0.0,  1.0) * t);
    vec3 s  = sampleVideo(p + vec2( 0.0, -1.0) * t);
    vec3 e  = sampleVideo(p + vec2( 1.0,  0.0) * t);
    vec3 w  = sampleVideo(p + vec2(-1.0,  0.0) * t);
    vec3 lap = (n + s + e + w) - 4.0 * c;
    return clamp(c - lap * amount, 0.0, 1.0);
}

vec3 renderMode0(vec2 st) {
    vec2 centered = (st - 0.5) * vec2(ubo.resolution.x / max(ubo.resolution.y, 1.0), 1.0);
    float halfSize = 0.35;
    float edgeWidth = 0.03;

    float box = max(abs(centered.x), abs(centered.y));
    float fill = smoothstep(halfSize, halfSize - edgeWidth, box);
    float border = smoothstep(halfSize + edgeWidth, halfSize, box) - fill;

    float drive = max(0.1, ubo.audioReactiveDrive);
    float bassInfluence = clamp(ubo.bass * drive, 0.0, 1.0);
    float midInfluence = clamp(ubo.mid * drive, 0.0, 1.0);
    float highInfluence = clamp(ubo.high * drive, 0.0, 1.0);
    float energyInfluence = clamp(ubo.energy * drive * 0.8, 0.0, 1.0);

    float colorLerp = clamp(0.2f + 0.5f * energyInfluence + 0.3f * midInfluence, 0.0f, 1.0f);

    vec3 bg = mix(ubo.secondaryColor.rgb * 0.1, ubo.primaryColor.rgb * 0.05, energyInfluence);
    vec3 square = mix(ubo.primaryColor.rgb, ubo.secondaryColor.rgb, colorLerp);
    vec3 borderColor = mix(square, vec3(1.0), highInfluence * 0.5);

    square *= 0.8 + 0.4 * bassInfluence;
    borderColor *= 0.7 + 0.3 * highInfluence;

    vec3 color = bg;
    color = mix(color, borderColor, border);
    color = mix(color, square, fill);
    return color;
}

vec3 renderMode1(vec2 st) {
    vec2 centered = (st - 0.5) * vec2(ubo.resolution.x / max(ubo.resolution.y, 1.0), 1.0);
    float drive = max(0.1, ubo.audioReactiveDrive);
    float bassResponse = clamp(ubo.bass * drive, 0.0, 1.0);
    float energyResponse = clamp(ubo.energy * drive, 0.0, 1.0);
    float radius = 0.25 + 0.35 * (0.6 * bassResponse + 0.4 * energyResponse);
    float dist = length(centered);

    float core = smoothstep(radius, radius - 0.02, dist);
    float rim = smoothstep(radius + 0.05, radius, dist) - core;

    float hueShift = clamp(ubo.mid * drive, 0.0, 1.0);
    float bodyMix = clamp(ubo.colorBlend + hueShift * 0.3, 0.0, 1.0);
    vec3 bodyColor = mix(ubo.primaryColor.rgb, ubo.secondaryColor.rgb, bodyMix);
    vec3 rimColor = mix(ubo.secondaryColor.rgb, vec3(1.0, 0.9, 0.8), clamp(ubo.high * drive, 0.0, 1.0));

    float glow = exp(-dist * (3.0 - bassResponse)) * (0.35 + energyResponse * 0.65);
    vec3 color = vec3(0.0);
    color = mix(color, rimColor, rim);
    color = mix(color, bodyColor, core);
    color += glow * ubo.primaryColor.rgb;
    return clamp(color, 0.0, 1.0);
}

vec4 dispatchMode(int m, vec2 st) {
    if (m == 0) return vec4(renderMode0(st), 1.0);
    if (m == 1) return vec4(renderMode1(st), 1.0);
    if (m == 40) {
        vec2 aspectCorrected = (st - 0.5) * vec2(ubo.resolution.x / max(ubo.resolution.y, 1.0), 1.0);
        return renderAnaglyphAssembly(aspectCorrected, ubo.time, ubo.tempo, ubo.energy, ubo.bass, ubo.mid, ubo.high);
    }
    return vec4(0.0);
}

void main() {
    vec2 procUV = applyCamera(uv);
    vec3 video1Color = sampleVideo(uv);
    vec3 video2Color = sampleVideo2(uv);

    float v2Mix = clamp(ubo.video2Mix * ubo.video2Available, 0.0, 1.0);
    vec3 videoColor = blendTwoVideos(video1Color, video2Color, ubo.video2BlendMode, v2Mix);

    vec4 procColor = dispatchMode(ubo.mode, procUV);
    vec3 compositedProcedural = procColor.rgb;
    if (ubo.enableBlending == 1) {
        compositedProcedural = applyProceduralBlend(procColor.rgb, videoColor,
                                                   ubo.blendModeProcedural,
                                                   ubo.blendProceduralMix);
    }

    float videoMixAmount = clamp(ubo.videoMix * ubo.videoAvailable, 0.0, 1.0);
    vec3 color = mix(compositedProcedural, videoColor, videoMixAmount);

    if (ubo.enableSharpen == 1 && ubo.sharpenAmount > 0.0001) {
        color = mix(color, sharpen3x3(uv, ubo.sharpenAmount), clamp(ubo.casAmount + ubo.unsharpMask, 0.0, 1.0));
    }

    if (ubo.enablePostGrain == 1 && ubo.grainStrength > 0.0001) {
        float g = fract(sin(dot(uv * ubo.resolution + ubo.time * 60.0, vec2(12.9898, 78.233))) * 43758.5453) - 0.5;
        color += g * ubo.grainStrength * 0.08;
    }

    if (ubo.grayscaleAmount > 0.0001) {
        float l = luminance(color);
        color = mix(color, vec3(l), clamp(ubo.grayscaleAmount, 0.0, 1.0));
    }

    if (ubo.enablePostColorBalance == 1) {
        color *= ubo.colorBalance;
    }

    outColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}