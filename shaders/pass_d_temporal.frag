#version 450
#extension GL_GOOGLE_include_directive : require

#include "shared_ubo.glsl"

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform sampler2D inputTex;
layout(set = 1, binding = 1) uniform sampler2D prevFrameTex;

float hash21(vec2 p) {
    p = fract(p * vec2(443.897, 441.423));
    p += dot(p, p.yx + 19.19);
    return fract(p.x * p.y);
}

float edgeFade(vec2 p) {
    vec2 e = smoothstep(vec2(0.02), vec2(0.10), p) * smoothstep(vec2(0.02), vec2(0.10), 1.0 - p);
    return e.x * e.y;
}

void main() {
    vec2 centered = uv * 2.0 - 1.0;
    vec3 base = texture(inputTex, uv).rgb;

    if (ubo.enableFeedback == 0 && ubo.enableTemporal == 0) {
        outColor = vec4(base, 1.0);
        return;
    }

    vec3 accum = base;
    vec2 texel = 1.0 / max(ubo.resolution, vec2(1.0));

    float feedbackMix = clamp(ubo.feedbackAmount, 0.0, 1.0);
    float trailMix = clamp(ubo.trailStrength, 0.0, 1.0);
    float temporalMix = clamp(ubo.temporalAccumulation, 0.0, 1.0);
    float recursiveMix = clamp(ubo.recursiveBlend, 0.0, 1.0);
    float frameMix = clamp(ubo.frameAccumulation, 0.0, 1.0);
    float decay = clamp(1.0 - ubo.feedbackDecay, 0.0, 1.0);

    float edge = edgeFade(uv);
    float audio = clamp(ubo.energy, 0.0, 1.0);
    
    if (ubo.enableAudioReactive == 1) {
        feedbackMix *= (1.0 + audio * ubo.audioFeedbackResponse);
    }

    if (ubo.enableFeedback == 1 && (trailMix > 0.0001 || feedbackMix > 0.0001)) {
        for (int i = 1; i <= 3; ++i) {
            float t = float(i) / 3.0;
            vec2 off = vec2(0.0);

            off += centered * t * trailMix * 0.10;
            off += vec2(0.0, t * 0.02 * temporalMix);

            vec2 sampleUV = clamp(uv - off * texel * 20.0, 0.0, 1.0);
            vec3 s = texture(inputTex, sampleUV).rgb;

            float w = feedbackMix * decay * (1.0 - t * 0.35) * edge;
            accum = mix(accum, s, w);
        }
    }

    if (ubo.enableFeedback == 1 && recursiveMix > 0.0001) {
        vec3 prev = texture(prevFrameTex, uv).rgb;
        accum = mix(accum, prev, recursiveMix * decay * edge);
    }

    if (ubo.enableTemporal == 1 && frameMix > 0.0001) {
        vec3 prev = texture(prevFrameTex, uv).rgb;
        float temporalWeight = frameMix * (0.5 + audio * 0.25);
        accum = mix(accum, prev, temporalWeight * edge);
    }

    outColor = vec4(clamp(accum, 0.0, 1.0), 1.0);
}