#include "post_common.glsl"

void main() {
    vec4 sceneColor = texture(uScene, vUV);
    vec3 adjust = max(uRgbAdjust, vec3(0.0));
    float radius = uStrength * 0.01;
    float phase = uTime * 0.8;
    vec2 rOffset = vec2(cos(phase), sin(phase)) * radius * adjust.r;
    vec2 gOffset = vec2(cos(phase + 2.0943951), sin(phase + 2.0943951)) * radius * adjust.g;
    vec2 bOffset = vec2(cos(phase + 4.1887902), sin(phase + 4.1887902)) * radius * adjust.b;
    vec2 uvR = clamp(vUV + rOffset, 0.0, 1.0);
    vec2 uvG = clamp(vUV + gOffset, 0.0, 1.0);
    vec2 uvB = clamp(vUV + bOffset, 0.0, 1.0);
    vec3 shifted;
    shifted.r = texture(uScene, uvR).r;
    shifted.g = texture(uScene, uvG).g;
    shifted.b = texture(uScene, uvB).b;
    vec3 channelStrength = clamp(adjust, 0.0, 1.0) * uStrength;
    vec3 result;
    result.r = mix(sceneColor.r, shifted.r, channelStrength.r);
    result.g = mix(sceneColor.g, shifted.g, channelStrength.g);
    result.b = mix(sceneColor.b, shifted.b, channelStrength.b);
    FragColor = vec4(result, sceneColor.a);
}
