#version 450
#extension GL_GOOGLE_include_directive : require

#include "shared_ubo.glsl"

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform sampler2D inputTex;

const float PI = 3.1415926535;

float hash21(vec2 p) {
    p = fract(p * vec2(443.897, 441.423));
    p += dot(p, p.yx + 19.19);
    return fract(p.x * p.y);
}

// Normalised disc blur (the old 5-tap cross had weights summing to 1.5, which made the image brighter
// and turned the unsharp mask into a "darken to black"). 12 golden-angle taps + centre, weights sum to 1.
vec3 blurDisc(vec2 uv, float radiusPx) {
    vec2 t = 1.0 / max(ubo.resolution, vec2(1.0));
    vec3 acc = texture(inputTex, uv).rgb;
    for (int i = 0; i < 12; ++i) {
        float a = float(i) * 2.399963;
        float r = sqrt((float(i) + 0.5) / 12.0) * radiusPx;
        acc += texture(inputTex, clamp(uv + vec2(cos(a), sin(a)) * r * t, 0.0, 1.0)).rgb;
    }
    return acc / 13.0;
}

// Line blur along a direction, lengthPx pixels long (9 equal taps).
vec3 blurLine(vec2 uv, vec2 dir, float lengthPx) {
    vec2 t = 1.0 / max(ubo.resolution, vec2(1.0));
    vec3 acc = vec3(0.0);
    for (int i = -4; i <= 4; ++i) {
        vec2 o = dir * t * (float(i) / 4.0) * lengthPx * 0.5;
        acc += texture(inputTex, clamp(uv + o, 0.0, 1.0)).rgb;
    }
    return acc / 9.0;
}

// Radial blur towards the centre; `amount` is the fraction of the radius that gets smeared.
vec3 blurZoom(vec2 uv, float amount) {
    vec2 toCentre = vec2(0.5) - uv;
    vec3 acc = vec3(0.0);
    for (int i = 0; i < 8; ++i) {
        float s = (float(i) / 7.0) * amount;
        acc += texture(inputTex, clamp(uv + toCentre * s, 0.0, 1.0)).rgb;
    }
    return acc / 8.0;
}

vec3 unsharp(vec2 uv, vec3 color) {
    float s = clamp(ubo.unsharpMask + ubo.sharpenAmount, 0.0, 1.0);
    if (s <= 0.0001) return color;
    vec3 blurred = blurDisc(uv, 1.5);
    return clamp(color + (color - blurred) * s * 2.5, 0.0, 1.0);
}

// Real local contrast: push each pixel away from its wider neighbourhood average.
vec3 applyLocalContrast(vec2 uv, vec3 color) {
    float k = ubo.localContrast;
    if (k <= 0.0001) return color;
    vec3 blurred = blurDisc(uv, 8.0);
    return clamp(color + (color - blurred) * k * 1.5, 0.0, 1.0);
}

vec3 applyGrain(vec2 uv, vec3 color) {
    if (ubo.enablePostGrain == 0 || ubo.grainStrength <= 0.0001) return color;
    float g = hash21(uv * ubo.resolution + ubo.time * 60.0) - 0.5;
    return clamp(color + g * ubo.grainStrength * 0.08, 0.0, 1.0);
}

vec3 applyVignette(vec2 uv, vec3 color) {
    if (ubo.enablePostVignette == 0) return color;
    vec2 c = uv * 2.0 - 1.0;
    float r = length(c);
    float v = 1.0 - smoothstep(0.35, 1.0, r) * clamp(ubo.crtVignette, 0.0, 1.0);
    return color * v;
}

vec3 applyScanMask(vec2 uv, vec3 color) {
    if (ubo.enablePostScanMask == 0) return color;
    float freq = 240.0 * (ubo.resolution.y / 480.0);
    float s = 0.5 + 0.5 * sin(uv.y * PI * freq);
    float m = mix(1.0, s, clamp(ubo.crtScanlineIntensity, 0.0, 1.0));
    return color * m;
}

vec3 applyChromaticAberration(vec2 uv, vec3 color) {
    if (ubo.enablePostAberration == 0 || ubo.aberrationAmount <= 0.0001) return color;
    vec2 c = uv * 2.0 - 1.0;
    float r = dot(c, c);
    vec2 o = c * r * ubo.aberrationAmount * 0.003;
    float rr = texture(inputTex, clamp(uv + o, 0.0, 1.0)).r;
    float gg = texture(inputTex, clamp(uv, 0.0, 1.0)).g;
    float bb = texture(inputTex, clamp(uv - o, 0.0, 1.0)).b;
    return vec3(rr, gg, bb);
}

void main() {
    vec2 uv0 = uv;
    vec3 color = texture(inputTex, uv0).rgb;

    bool hasBlur = ubo.gaussianBlur > 0.0001 ||
                   ubo.directionalBlur > 0.0001 ||
                   ubo.zoomBlur > 0.0001 ||
                   ubo.motionBlur > 0.0001 ||
                   ubo.temporalBlur > 0.0001;

    if (ubo.enableBlurMotion == 1 && hasBlur) {
        // Each slider scales the blur RADIUS (so 0 = nothing, 1 = strong) instead of only the mix factor.
        float blurAmount = ubo.gaussianBlur;
        if (ubo.enableAudioReactive == 1) {
            blurAmount *= (1.0 + ubo.energy * ubo.audioBlurResponse);
        }
        blurAmount = clamp(blurAmount, 0.0, 1.0);
        if (blurAmount > 0.0001) {
            color = mix(color, blurDisc(uv0, 0.5 + blurAmount * 14.0), min(1.0, blurAmount * 4.0));
        }
        float dirAmt = clamp(ubo.directionalBlur, 0.0, 1.0);
        if (dirAmt > 0.0001) {
            float ang = radians(ubo.directionalBlurAngle);
            color = mix(color, blurLine(uv0, vec2(cos(ang), sin(ang)), dirAmt * 60.0), min(1.0, dirAmt * 4.0));
        }
        float zoomAmt = clamp(ubo.zoomBlur, 0.0, 1.0);
        if (zoomAmt > 0.0001) {
            color = mix(color, blurZoom(uv0, zoomAmt * 0.18), min(1.0, zoomAmt * 4.0));
        }
        float motAmt = clamp(ubo.motionBlur, 0.0, 1.0);
        if (motAmt > 0.0001) {
            float a = ubo.time * 0.5;
            color = mix(color, blurLine(uv0, normalize(vec2(sin(a), cos(a * 0.7))), motAmt * 48.0), min(1.0, motAmt * 4.0));
        }
        float tmpAmt = clamp(ubo.temporalBlur, 0.0, 1.0);
        if (tmpAmt > 0.0001) {
            // jittered sampling: a soft, shimmering blur that changes every frame
            vec2 t = 1.0 / max(ubo.resolution, vec2(1.0));
            vec3 acc = color;
            for (int i = 0; i < 4; ++i) {
                vec2 j = vec2(hash21(uv0 * 977.0 + ubo.time + float(i)), hash21(uv0 * 313.0 - ubo.time + float(i) * 7.0)) - 0.5;
                acc += texture(inputTex, clamp(uv0 + j * 2.0 * tmpAmt * 10.0 * t, 0.0, 1.0)).rgb;
            }
            color = mix(color, acc / 5.0, min(1.0, tmpAmt * 4.0));
        }
    }

    if (ubo.enableSharpen == 1) {
        color = unsharp(uv0, color);
    }

    if (ubo.localContrast > 0.0001) {
        color = applyLocalContrast(uv0, color);
    }

    color = applyChromaticAberration(uv0, color);
    color = applyScanMask(uv0, color);
    color = applyVignette(uv0, color);
    color = applyGrain(uv0, color);

    outColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}