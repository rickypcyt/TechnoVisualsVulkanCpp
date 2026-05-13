#include "post_common.glsl"

void main() {
    vec2 p = vUV - 0.5;
    vec3 o = texture(uScene, vUV + 0.5).rbb;
    
    float accum = 0.0;
    for (float i = 0.0; i < 100.0; i++) {
        vec2 sampleUV = p * 0.992 + 0.5;
        vec3 texColor = texture(uScene, sampleUV).rgb;
        accum += pow(max(0.0, 0.5 - length(texColor.rg)), 2.0) * exp(-i * 0.08);
        p *= 0.992;
    }
    
    vec3 result = o * o + accum * uStrength;
    FragColor = vec4(result, 1.0);
}
