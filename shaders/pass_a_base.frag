#version 450
#extension GL_GOOGLE_include_directive : require

#include "shared_ubo.glsl"

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform sampler2D videoTex;
layout(set = 1, binding = 1) uniform sampler2D videoTexPrev;
layout(set = 1, binding = 2) uniform sampler2D video2Tex;

const float PI = 3.1415926535;

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
    return mix(ubo.primaryColor.rgb, ubo.secondaryColor.rgb, clamp(ubo.colorBlend, 0.0, 1.0));
}

vec3 renderMode1(vec2 st) {
    vec2 centered = st - 0.5;
    float r = length(centered);
    float a = atan(centered.y, centered.x);

    float spin = ubo.time * (1.0 + ubo.bass * 4.0 + ubo.energy * 2.0);
    float arms = 3.0 + floor(clamp(ubo.mid, 0.0, 1.0) * 6.0);
    float phase = a * arms + r * 25.0 - spin * 3.0;

    float thickness = 0.15 + ubo.energy * 0.25;
    float spiral = smoothstep(thickness, 0.0, abs(sin(phase)));
    float ring = smoothstep(0.05, 0.0, abs(r - (0.2 + ubo.bass * 0.3) - fract(ubo.time * 0.5) * 0.4));

    vec3 spiralColor = mix(ubo.primaryColor.rgb, ubo.secondaryColor.rgb,
                           0.5 + 0.5 * sin(ubo.time * 2.0 + r * 15.0));
    vec3 ringColor = ubo.secondaryColor.rgb * (0.5 + ubo.high * 0.8);

    vec3 color = vec3(0.0);
    color = mix(color, ringColor, ring * 0.6);
    color = mix(color, spiralColor, spiral);

    float glow = exp(-r * 8.0) * (0.4 + ubo.bass * 0.6);
    color += ubo.primaryColor.rgb * glow;
    color *= 1.0 - smoothstep(0.48, 0.7, r);

    return clamp(color, 0.0, 1.0);
}

vec4 dispatchMode(int m, vec2 st) {
    if (m == 0) return vec4(renderMode0(st), 1.0);
    if (m == 1) return vec4(renderMode1(st), 1.0);
    return vec4(0.0);
}

void main() {
    vec2 procUV = applyCamera(uv);
    vec3 video1Color = sampleVideo(uv);
    vec3 video2Color = sampleVideo2(uv);

    float v2Mix = clamp(ubo.video2Mix * ubo.video2Available, 0.0, 1.0);
    vec3 videoColor = blendTwoVideos(video1Color, video2Color, ubo.video2BlendMode, v2Mix);

    vec4 procColor = dispatchMode(ubo.mode, procUV);

    vec3 color = mix(procColor.rgb, videoColor, clamp(ubo.videoMix * ubo.videoAvailable, 0.0, 1.0));

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