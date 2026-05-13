#include "post_common.glsl"
#include "mirror_common.glsl"

void main() {
    vec2 uv = vUV;
    vec4 result = applyMirrorEffect(uv, 0, uStrength, uTime);
    FragColor = vec4(result.rgb, 1.0);
}
