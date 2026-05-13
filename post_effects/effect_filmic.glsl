#include "post_common.glsl"

void main() {
    vec4 sceneColor = texture(uScene, vUV);
    vec3 filmic = applyFilmic(sceneColor.rgb * (1.5 + uStrength));
    filmic = clamp(filmic, 0.0, 1.0);
    float vig = vignette(vUV, 0.35 + uStrength * 0.3);
    float grain = filmGrain(vUV, uTime * 24.0, uStrength * 0.6);
    vec3 result = mix(sceneColor.rgb, filmic, uStrength * 0.7);
    result *= mix(1.0, 1.0 - vig, uStrength * 0.5);
    result = mix(result, result * grain, uStrength * 0.4);
    FragColor = vec4(result, sceneColor.a);
}
