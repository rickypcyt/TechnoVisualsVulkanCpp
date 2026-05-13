#include "post_common.glsl"

void main() {
    vec4 sceneColor = texture(uScene, vUV);
    vec3 plasma = plasmaOverlay(vUV, uTime * 1.2);
    vec3 result = mix(sceneColor.rgb, plasma, uStrength * 0.35);
    FragColor = vec4(result, sceneColor.a);
}
