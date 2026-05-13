#include "post_common.glsl"

void main() {
    vec4 sceneColor = texture(uScene, vUV);
    vec3 pix = pixelateRetro(vUV, 64.0, 8.0);
    vec3 result = mix(sceneColor.rgb, pix, uStrength);
    FragColor = vec4(result, sceneColor.a);
}
