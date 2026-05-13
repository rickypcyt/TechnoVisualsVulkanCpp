#include "post_common.glsl"

void main() {
    vec4 sceneColor = texture(uScene, vUV);
    vec3 result = mix(sceneColor.rgb, digitalGlitch(vUV, uStrength, uTime), uStrength * 0.9);
    FragColor = vec4(result, sceneColor.a);
}
