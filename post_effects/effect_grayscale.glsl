#include "post_common.glsl"

void main() {
    vec4 sceneColor = texture(uScene, vUV);
    vec3 gray = toGrayscale(sceneColor.rgb);
    vec3 result = mix(sceneColor.rgb, gray, uStrength);
    FragColor = vec4(result, sceneColor.a);
}
