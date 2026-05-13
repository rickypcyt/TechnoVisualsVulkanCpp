#include "post_common.glsl"

void main() {
    vec2 uv = vUV;
    
    // Fixed 50/50 split
    float splitPos = 0.5;
    
    vec3 tc;
    
    if (uv.x < splitPos) {
        // 3-level thresholding / color quantization
        vec3 pixcol = texture(uScene, uv).rgb;
        vec3 colors[3];
        colors[0] = vec3(0.0, 0.0, 1.0);  // Blue (dark)
        colors[1] = vec3(1.0, 1.0, 0.0);  // Yellow (mid)
        colors[2] = vec3(1.0, 0.0, 0.0);  // Red (bright)
        
        float lum = (pixcol.r + pixcol.g + pixcol.b) / 3.0;
        int ix = (lum < 0.5) ? 0 : 1;
        tc = mix(colors[ix], colors[ix + 1], (lum - float(ix) * 0.5) / 0.5);
    } else {
        tc = texture(uScene, uv).rgb;
    }
    
    FragColor = vec4(tc, 1.0);
}
