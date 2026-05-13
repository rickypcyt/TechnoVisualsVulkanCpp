#include "post_common.glsl"

void main() {
    vec4 sceneColor = texture(uScene, vUV);
    FragColor = sceneColor;
}
