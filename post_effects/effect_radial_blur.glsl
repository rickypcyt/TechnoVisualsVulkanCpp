#include "post_common.glsl"

void main() {
    vec4 sceneColor = texture(uScene, vUV);
    vec3 blurred = radialBlur(vUV, uStrength);
    vec3 result = mix(sceneColor.rgb, blurred, uStrength);
    FragColor = vec4(result, sceneColor.a);
}
