#version 450
#extension GL_GOOGLE_include_directive : require

#include "shared_ubo.glsl"

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform sampler2D inputTex;
layout(set = 1, binding = 1) uniform sampler2D proceduralTex;

const float PI = 3.1415926535;

float hash21(vec2 p) {
    p = fract(p * vec2(443.897, 441.423));
    p += dot(p, p.yx + 19.19);
    return fract(p.x * p.y);
}

float luminance(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

vec2 applyGrid(vec2 p) {
    if (ubo.enableGrid != 1) return p;

    if (ubo.gridMode == 0 && ubo.gridCount > 1) {
        // Duplicar horizontalmente (N columnas)
        float x = p.x * float(ubo.gridCount);
        float cellX = floor(x);
        p.x = fract(x);
        if (ubo.gridMirrorCells == 1 && int(cellX) % 2 != 0)
            p.x = 1.0 - p.x;

    } else if (ubo.gridMode == 1 && ubo.gridCount > 1) {
        // Duplicar verticalmente (N filas)
        float y = p.y * float(ubo.gridCount);
        float cellY = floor(y);
        p.y = fract(y);
        if (ubo.gridMirrorCells == 1 && int(cellY) % 2 != 0)
            p.y = 1.0 - p.y;

    } else if (ubo.gridMode == 2 && ubo.gridRows > 0 && ubo.gridColumns > 0) {
        // Matriz de filas × columnas
        float x = p.x * float(ubo.gridColumns);
        float y = p.y * float(ubo.gridRows);
        float cellX = floor(x);
        float cellY = floor(y);
        p.x = fract(x);
        p.y = fract(y);
        if (ubo.gridMirrorCells == 1) {
            if (int(cellX) % 2 != 0) p.x = 1.0 - p.x;
            if (int(cellY) % 2 != 0) p.y = 1.0 - p.y;
        }
    }

    return clamp(p, 0.0, 1.0);
}

vec3 sampleInput(vec2 p) {
    return texture(inputTex, applyGrid(p)).rgb;
}

/* blur 9-tap */
vec3 blur3x3(vec2 p) {
    vec2 t = 1.0 / max(ubo.resolution, vec2(1.0));
    vec3 c = vec3(0.0);
    c += sampleInput(p + t*vec2(-1,-1));
    c += sampleInput(p + t*vec2( 0,-1));
    c += sampleInput(p + t*vec2( 1,-1));
    c += sampleInput(p + t*vec2(-1, 0));
    c += sampleInput(p);
    c += sampleInput(p + t*vec2( 1, 0));
    c += sampleInput(p + t*vec2(-1, 1));
    c += sampleInput(p + t*vec2( 0, 1));
    c += sampleInput(p + t*vec2( 1, 1));
    return c / 9.0;
}

/* FXAA (reads neighborhood via sampleInput) */
vec3 fxaa_compose(vec2 uv) {
    vec2 texel = 1.0 / max(ubo.resolution, vec2(1.0));
    vec3 rgbNW = sampleInput(uv + vec2(-1.0, -1.0) * texel);
    vec3 rgbNE = sampleInput(uv + vec2( 1.0, -1.0) * texel);
    vec3 rgbSW = sampleInput(uv + vec2(-1.0,  1.0) * texel);
    vec3 rgbSE = sampleInput(uv + vec2( 1.0,  1.0) * texel);
    vec3 rgbM  = sampleInput(uv);
    float lumaNW = luminance(rgbNW);
    float lumaNE = luminance(rgbNE);
    float lumaSW = luminance(rgbSW);
    float lumaSE = luminance(rgbSE);
    float lumaM  = luminance(rgbM);
    float lumaMin = min(lumaM, min(min(lumaNW,lumaNE), min(lumaSW,lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW,lumaNE), max(lumaSW,lumaSE)));
    if ((lumaMax - lumaMin) < max(ubo.fxaaQualityEdgeThresholdMin, lumaMax * ubo.fxaaQualityEdgeThreshold)) return rgbM;
    vec2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));
    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * 0.25 * ubo.fxaaQualitySubpix, 0.125);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcpDirMin, vec2(-8.0), vec2(8.0));
    vec3 rgbA = 0.5 * (sampleInput(uv + dir * texel * (1.0/3.0 - 0.5)) + sampleInput(uv + dir * texel * (2.0/3.0 - 0.5)));
    vec3 rgbB = rgbA * 0.5 + 0.25 * (sampleInput(uv + dir * texel * -0.5) + sampleInput(uv + dir * texel * 0.5));
    float lumaB = luminance(rgbB);
    return (lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB;
}

/* Sobel edge (returns scalar strength in .r) */
float sobelEdgeStrength(vec2 p) {
    vec2 t = 1.0 / max(ubo.resolution, vec2(1.0));
    float tl = luminance(sampleInput(p + t * vec2(-1.0,  1.0)));
    float  t0 = luminance(sampleInput(p + t * vec2( 0.0,  1.0)));
    float tr = luminance(sampleInput(p + t * vec2( 1.0,  1.0)));
    float  l = luminance(sampleInput(p + t * vec2(-1.0,  0.0)));
    float  r = luminance(sampleInput(p + t * vec2( 1.0,  0.0)));
    float bl = luminance(sampleInput(p + t * vec2(-1.0, -1.0)));
    float  b = luminance(sampleInput(p + t * vec2( 0.0, -1.0)));
    float br = luminance(sampleInput(p + t * vec2( 1.0, -1.0)));
    float gx = -tl - 2.0 * l - bl + tr + 2.0 * r + br;
    float gy = -tl - 2.0 * t0 - tr + bl + 2.0 * b + br;
    return length(vec2(gx, gy)) * 0.125;
}

/* Blend modes (kept simple) */
vec3 blendMode(vec3 base, vec3 layer, int mode) {
    if (mode == 0) return clamp(base + layer, 0.0, 1.5);
    if (mode == 1) return 1.0 - (1.0 - base) * (1.0 - layer);
    if (mode == 2) return base * layer;
    if (mode == 3) return mix(base, 2.0 * base * layer + base * base * (1.0 - 2.0 * layer), 0.5);
    if (mode == 4) return abs(base - layer);
    if (mode == 5) {
        vec3 low = 2.0 * base * layer + base * base * (1.0 - 2.0 * layer);
        vec3 high = sqrt(max(base, vec3(0.0))) * (2.0 * layer - 1.0) + 2.0 * base * (1.0 - layer);
        return mix(low, high, step(vec3(0.5), layer));
    }
    return layer;
}

void main() {
    outColor = vec4(1.0, 0.0, 0.0, 1.0); // rojo puro, sin condiciones
}