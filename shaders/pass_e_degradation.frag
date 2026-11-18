#version 450
#extension GL_GOOGLE_include_directive : require

#include "shared_ubo.glsl"

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform sampler2D inputTex;

float hash21(vec2 p) {
    p = fract(p * vec2(443.897, 441.423));
    p += dot(p, p.yx + 19.19);
    return fract(p.x * p.y);
}

vec3 rgbSortApprox(vec3 v) {
    if (v.x > v.y) v.xy = v.yx;
    if (v.y > v.z) v.yz = v.zy;
    if (v.x > v.y) v.xy = v.yx;
    return v;
}

vec3 applyAberration(vec2 uv, vec3 color, float amt) {
    vec2 c = uv * 2.0 - 1.0;
    float r2 = dot(c, c);
    vec2 off = c * r2 * amt * 0.01;
    float rr = texture(inputTex, clamp(uv + off, 0.0, 1.0)).r;
    float gg = texture(inputTex, clamp(uv, 0.0, 1.0)).g;
    float bb = texture(inputTex, clamp(uv - off, 0.0, 1.0)).b;
    return vec3(rr, gg, bb);
}

void main() {
    float timeScale = max(ubo.slowMotionFactor, 0.1);
    float t = ubo.time / timeScale;
    vec2 c = uv * 2.0 - 1.0;

    vec3 base = texture(inputTex, uv).rgb;
    if (ubo.enablePostGlitch == 0 && ubo.enablePostAberration == 0) {
        outColor = vec4(base, 1.0);
        return;
    }

    // The strongest glitch slider decides how much of the glitched image shows (it used to be a sum of
    // ~0.1 weights, so even all sliders at 1.0 blended in only a few percent of the effect).
    float strongest = max(max(max(ubo.glitchDatamosh, ubo.glitchJitter), max(ubo.glitchRGBSplit, ubo.glitchScanlineBreak)),
                          max(max(ubo.glitchTearing, ubo.glitchPixelSort), ubo.glitchBufferCorruption));
    float glitchWeight = smoothstep(0.0, 0.35, max(strongest, ubo.glitchAmount));
    
    if (ubo.enableAudioReactive == 1) {
        glitchWeight *= (1.0 + ubo.energy * ubo.audioGlitchResponse);
    }

    vec3 color = base;

    if (ubo.enablePostGlitch == 1 && glitchWeight > 0.0001) {
        vec2 p = uv;

        if (ubo.glitchDatamosh > 0.0001) {
            p.x += sin(c.y * 40.0 + t * 8.0) * ubo.glitchDatamosh * 0.05 * (0.5 + 0.5 * sin(t * 1.3));
        }

        if (ubo.glitchJitter > 0.0001) {
            p += (hash21(uv * 400.0 + t) - 0.5) * ubo.glitchJitter * 0.03;
        }

        vec3 g = texture(inputTex, clamp(p, 0.0, 1.0)).rgb;

        if (ubo.glitchRGBSplit > 0.0001) {
            float osc = sin(t * 2.0) * 0.5 + 0.5;
            vec2 off = vec2(ubo.glitchRGBSplit * (0.4 + 0.6 * osc) * 0.025, 0.0);
            g.r = texture(inputTex, clamp(p + off, 0.0, 1.0)).r;
            g.b = texture(inputTex, clamp(p - off, 0.0, 1.0)).b;
        }

        if (ubo.glitchScanlineBreak > 0.0001) {
            float line = step(0.90, fract(uv.y * 90.0 + t * 5.0));
            g *= 1.0 - line * ubo.glitchScanlineBreak * 0.85;
        }

        if (ubo.glitchTearing > 0.0001) {
            // horizontal bands that slide sideways
            float band = floor(uv.y * 22.0);
            float tear = step(0.84, hash21(vec2(band, floor(t * 6.0))));
            float shift = (hash21(vec2(band + 3.0, floor(t * 6.0))) - 0.5) * 0.30 * ubo.glitchTearing;
            g = mix(g, texture(inputTex, clamp(vec2(p.x + shift, p.y), 0.0, 1.0)).rgb, tear);
        }

        if (ubo.glitchPixelSort > 0.0001) {
            g = mix(g, rgbSortApprox(g), clamp(ubo.glitchPixelSort * 0.9, 0.0, 1.0));
        }

        if (ubo.glitchBufferCorruption > 0.0001) {
            // random blocks get inverted / replaced by noise
            vec2 cell = floor(uv * vec2(48.0, 27.0));
            float bk = hash21(cell + floor(t * 8.0));
            if (bk > 1.0 - ubo.glitchBufferCorruption * 0.22) {
                g = (hash21(cell * 1.7 + t) > 0.5) ? (1.0 - g) : vec3(hash21(cell + 9.0 + t));
            }
        }

        if (ubo.grainStrength > 0.0001) {
            float n = hash21(uv * 800.0 + t * 60.0) - 0.5;
            g += n * ubo.grainStrength * 0.06;
        }

        color = mix(base, g, glitchWeight);
    }

    if (ubo.enablePostAberration == 1 && ubo.aberrationAmount > 0.0001) {
        float osc = sin(t * 0.5) * 0.5 + 0.5;
        color = applyAberration(uv, color, ubo.aberrationAmount * osc);
    }

    outColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}