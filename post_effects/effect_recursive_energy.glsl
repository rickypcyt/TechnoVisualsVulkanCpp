#include "post_common.glsl"

void main() {
    vec4 sceneColor = texture(uScene, vUV);
    vec3 energy = recursiveEnergy(vUV, uStrength);
    vec3 result = mix(sceneColor.rgb, energy, uStrength);
    FragColor = vec4(result, sceneColor.a);
}
