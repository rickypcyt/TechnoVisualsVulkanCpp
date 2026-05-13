#include "post_common.glsl"

void main() {
    vec4 sceneColor = texture(uScene, vUV);
    vec2 uv = lensDistort(vUV, uStrength * 1.5);
    vec3 result = texture(uScene, clamp(uv, 0.0, 1.0)).rgb;
    FragColor = vec4(result, sceneColor.a);
}
