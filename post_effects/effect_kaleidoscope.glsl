#include "post_common.glsl"

void main() {
    vec4 sceneColor = texture(uScene, vUV);
    float segments = 6.0 + uStrength * 10.0;
    vec2 uv = kaleido(vUV, segments, uResolution);
    vec4 kaleidoColor = texture(uScene, uv);
    vec3 result = kaleidoColor.rgb;
    // Alpha 1.0 para que el kaleidoscope siempre sea visible
    FragColor = vec4(result, 1.0);
}
