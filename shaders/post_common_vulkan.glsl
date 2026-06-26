#include "shared_ubo.glsl"

layout(local_size_x = 8, local_size_y = 8) in;

layout(set = 1, binding = 0) uniform sampler2D inputTex;
layout(set = 1, binding = 1, rgba8) uniform writeonly image2D outputImage;

// Compatibility macros for the old post_effects/*.glsl OpenGL interface
#define uScene      inputTex
#define uResolution ubo.resolution
#define uTime       ubo.time
#define uStrength   ubo.postEffectStrength
#define uIntensity  ubo.postEffectIntensity
#define uBassLevel  ubo.postEffectBass
#define uRgbAdjust  ubo.postEffectRgbAdjust
#define uMirrorMode ubo.postEffectMode
#define uEdgeThres  ubo.postEffectStrength
#define uEdgeThres2 ubo.postEffectIntensity

// vUV was a fragment input in the OpenGL version; in compute it is computed
// per invocation by the converter script inside main().
vec2 vUV;

// Common utility functions (ported from post_common.glsl)
const float THRESH = 0.10;
const float PI = 3.14159265359;

vec3 toGrayscale(vec3 color) {
    float luminance = dot(color, vec3(0.299, 0.587, 0.114));
    return vec3(luminance);
}

vec3 applyFilmic(vec3 x) {
    const vec3 A = vec3(0.15, 0.15, 0.15);
    const vec3 B = vec3(0.50, 0.50, 0.50);
    const vec3 C = vec3(0.10, 0.10, 0.10);
    const vec3 D = vec3(0.20, 0.20, 0.20);
    const vec3 E = vec3(0.02, 0.02, 0.02);
    const vec3 F = vec3(0.30, 0.30, 0.30);
    return ((x * (A * x + B)) / (x * (C * x + D) + E)) - F;
}

vec3 chromaticAberration(vec2 uv, float strength) {
    float offset = strength * 0.003;
    vec3 col;
    col.r = texture(inputTex, uv + vec2(offset, 0.0)).r;
    col.g = texture(inputTex, uv).g;
    col.b = texture(inputTex, uv - vec2(offset, 0.0)).b;
    return col;
}

float vignette(vec2 uv, float intensity) {
    vec2 centered = uv - 0.5;
    float dist = dot(centered, centered);
    return smoothstep(0.75, intensity, dist);
}

float filmGrain(vec2 uv, float time, float intensity) {
    float noise = fract(sin(dot(uv * 5.0 + time, vec2(12.9898, 78.233))) * 43758.5453);
    return mix(0.5, noise, intensity);
}

vec3 radialBlur(vec2 uv, float amount) {
    vec2 center = vec2(0.5);
    vec2 dir = uv - center;
    float radius = mix(0.25, 1.2, amount);
    float samples = 10.0;
    vec3 acc = vec3(0.0);
    for (int i = 0; i < 10; ++i) {
        float t = float(i) / (samples - 1.0);
        vec2 sampleUV = center + dir * t * radius;
        acc += texture(inputTex, clamp(sampleUV, 0.0, 1.0)).rgb;
    }
    return acc / samples;
}

vec2 kaleido(vec2 uv, float segments, vec2 resolution) {
    vec2 centered = uv - 0.5;
    float aspect = resolution.x / max(resolution.y, 1.0);
    centered.x *= aspect;

    float r = length(centered);
    float a = atan(centered.y, centered.x);
    float sector = (2.0 * PI) / max(segments, 1.0);
    a = mod(a, sector);
    a = abs(a - sector * 0.5);

    vec2 result = vec2(cos(a), sin(a)) * r;
    result.x /= aspect;
    return result + 0.5;
}

float rand(vec2 co) {
    return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}

vec3 digitalGlitch(vec2 uv, float strength, float time) {
    float lines = 160.0;
    float line = floor(uv.y * lines);
    float r = rand(vec2(line, floor(time * 12.0)));
    vec2 displaced = uv;
    if (r < 0.12 * strength) {
        displaced.x += (r - 0.06) * 0.35 * strength;
    }
    displaced.x = clamp(displaced.x, 0.0, 1.0);
    return texture(inputTex, displaced).rgb;
}

vec3 pixelate(vec2 uv, float strength) {
    float size = mix(800.0, 40.0, strength);
    vec2 grid = floor(uv * size) / size;
    return texture(inputTex, grid).rgb;
}

vec3 pixelateRetro(vec2 uv, float cells, float levels) {
    vec2 grid = floor(uv * cells) / cells;
    vec3 sampled = texture(inputTex, grid).rgb;
    float steps = max(levels - 1.0, 1.0);
    sampled = floor(sampled * steps + 0.5) / steps;
    return sampled;
}

vec2 lensDistort(vec2 uv, float power) {
    vec2 c = uv - 0.5;
    float r2 = dot(c, c);
    c *= 1.0 + r2 * power;
    return c + 0.5;
}

vec2 rotatingLensDistort(vec2 uv, float power, float time) {
    vec2 c = uv - 0.5;
    float rotationAngle = time * 0.5;
    float cos_r = cos(rotationAngle);
    float sin_r = sin(rotationAngle);
    vec2 rotated = vec2(
        c.x * cos_r - c.y * sin_r,
        c.x * sin_r + c.y * cos_r
    );
    float r2 = dot(rotated, rotated);
    rotated *= 1.0 + r2 * power;
    cos_r = cos(-rotationAngle);
    sin_r = sin(-rotationAngle);
    vec2 result = vec2(
        rotated.x * cos_r - rotated.y * sin_r,
        rotated.x * sin_r + rotated.y * cos_r
    );
    return result + 0.5;
}

vec3 plasmaOverlay(vec2 uv, float time) {
    float plasma = sin(uv.x * 12.0 + time)
                 + sin(uv.y * 10.0 + time * 1.3)
                 + sin((uv.x + uv.y) * 8.0 + time * 0.7);
    plasma = plasma * 0.5 + 0.5;
    vec3 color = vec3(
        sin(plasma * PI),
        sin(plasma * PI + 2.0),
        sin(plasma * PI + 4.0)
    );
    return color * 0.5 + 0.5;
}

vec3 recursiveEnergy(vec2 uv, float strength) {
    vec2 offset = uv - 0.5;
    vec2 pos = offset;
    vec3 base = texture(inputTex, clamp(vec2(0.5) + pos, 0.0, 1.0)).rbb;
    float depth = 0.0;
    float intensity = mix(0.3, 1.4, strength);

    for (int i = 0; i < 50; ++i) {
        pos *= 0.98;
        vec2 sampleUV = clamp(vec2(0.5) + pos, 0.0, 1.0);
        vec4 sampleTex = texture(inputTex, sampleUV);
        float influence = pow(max(0.0, 0.5 - length(sampleTex.rg)), 2.0) * exp(-float(i) * 0.1);
        depth += influence * intensity;
    }

    vec3 energy = base * base + depth;
    return clamp(energy, 0.0, 1.0);
}
