#include "post_common.glsl"

void main() {
    vec4 sceneColor = texture(uScene, vUV);
    vec2 waveUV = vUV + vec2(sin(vUV.y * 40.0 + uTime * 3.0), cos(vUV.x * 30.0 + uTime * 2.0)) * 0.0025 * uStrength;
    vec3 distorted = texture(uScene, waveUV).rgb;
    vec3 aberration = chromaticAberration(vUV, uStrength);
    float scanline = 0.85 + 0.15 * sin(vUV.y * 900.0);
    vec3 result = mix(distorted, aberration, uStrength * 0.6);
    result *= mix(1.0, scanline, uStrength * 0.5);
    FragColor = vec4(result, sceneColor.a);
}
