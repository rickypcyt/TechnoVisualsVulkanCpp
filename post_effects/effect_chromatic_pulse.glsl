#include "post_common.glsl"

void main() {
    vec4 sceneColor = texture(uScene, vUV);
    vec3 aberration = chromaticAberration(vUV, uStrength * 1.5);
    float wave = sin((vUV.x + vUV.y) * 25.0 + uTime * 4.0) * 0.5 + 0.5;
    vec3 pulseColor = mix(vec3(0.2, 0.4, 0.9), vec3(0.9, 0.4, 0.2), wave);
    vec3 result = mix(sceneColor.rgb, aberration, uStrength * 0.5);
    result += pulseColor * uStrength * 0.25;
    FragColor = vec4(result, sceneColor.a);
}
