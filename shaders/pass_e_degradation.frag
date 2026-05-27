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

    float glitchWeight = clamp(
        ubo.glitchAmount +
        ubo.glitchDatamosh * 0.25 +
        ubo.glitchJitter * 0.25 +
        ubo.glitchRGBSplit * 0.2 +
        ubo.glitchScanlineBreak * 0.15 +
        ubo.glitchTearing * 0.2 +
        ubo.glitchPixelSort * 0.2 +
        ubo.glitchBufferCorruption * 0.15 +
        ubo.grainStrength * 0.08,
        0.0, 1.0
    );
    
    if (ubo.enableAudioReactive == 1) {
        glitchWeight *= (1.0 + ubo.energy * ubo.audioGlitchResponse);
    }

    vec3 color = base;

    if (ubo.enablePostGlitch == 1 && glitchWeight > 0.0001) {
        vec2 p = uv;

        if (ubo.glitchDatamosh > 0.0001) {
            p.x += sin(c.y * 80.0 + t * 10.0) * ubo.glitchDatamosh * 0.008;
        }

        if (ubo.glitchJitter > 0.0001) {
            p += (hash21(uv * 400.0 + t) - 0.5) * ubo.glitchJitter * 0.008;
        }

        vec3 g = texture(inputTex, clamp(p, 0.0, 1.0)).rgb;

        if (ubo.glitchRGBSplit > 0.0001) {
            float osc = sin(t * 2.0) * 0.5 + 0.5;
            vec2 off = (ubo.glitchRGBSplit * osc * 0.003) * c / max(ubo.resolution, vec2(1.0));
            g.r = texture(inputTex, clamp(p + off, 0.0, 1.0)).r;
            g.b = texture(inputTex, clamp(p - off, 0.0, 1.0)).b;
        }

        if (ubo.glitchScanlineBreak > 0.0001) {
            float line = step(0.98, fract(uv.y * 200.0 + t * 5.0));
            g *= 1.0 - line * ubo.glitchScanlineBreak * 0.5;
        }

        if (ubo.glitchTearing > 0.0001) {
            float tear = step(0.85, hash21(vec2(uv.y * 10.0, t)));
            g = mix(g, base, 1.0 - tear * ubo.glitchTearing * 0.6);
        }

        if (ubo.glitchPixelSort > 0.0001) {
            g = mix(g, rgbSortApprox(g), ubo.glitchPixelSort * 0.2);
        }

        if (ubo.glitchBufferCorruption > 0.0001) {
            float rnd = hash21(uv * 600.0 + t * 20.0);
            g = mix(g, vec3(rnd), ubo.glitchBufferCorruption * 0.12);
        }

        if (ubo.grainStrength > 0.0001) {
            float n = hash21(uv * 800.0 + t * 60.0) - 0.5;
            g += n * ubo.grainStrength * 0.15;
        }

        color = mix(base, g, glitchWeight);
    }

    if (ubo.enablePostAberration == 1 && ubo.aberrationAmount > 0.0001) {
        float osc = sin(t * 0.5) * 0.5 + 0.5;
        color = applyAberration(uv, color, ubo.aberrationAmount * osc);
    }

    outColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}