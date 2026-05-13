#include "post_common.glsl"

void main() {
    vec2 uv = vUV;
    float aspect = uResolution.x / uResolution.y;
    
    // Audio-reactive parameters
    float ring = 5.0 + uBassLevel * 3.0;
    float div = 0.5;
    float t = uTime * 0.05 + uBassLevel * 0.1;
    
    vec2 p = vec2(uv.x * aspect, uv.y);
    
    // Center point
    vec2 center = vec2(0.5, 0.5);
    
    float r = distance(p, center * vec2(aspect, 1.0));
    r -= t;
    r = fract(r * ring) / div;
    
    // Apply distortion
    uv = -1.0 + 2.0 * uv;
    uv *= r;
    uv = uv * 0.5 + 0.5;
    
    // Apply strength control
    uv = mix(vUV, uv, uStrength);
    
    // Sample
    vec4 color = texture(uScene, uv);
    
    FragColor = color;
}
