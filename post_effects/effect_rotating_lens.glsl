#include "post_common.glsl"

void main() {
    vec4 sceneColor = texture(uScene, vUV);
    
    // Apply rotating lens distortion
    vec2 uv = rotatingLensDistort(vUV, uStrength * 1.5, uTime);
    
    // Sample the scene with the distorted coordinates
    vec3 result = texture(uScene, clamp(uv, 0.0, 1.0)).rgb;
    
    // Apply subtle color shift based on rotation
    float rotationPhase = fract(uTime * 0.5 / (2.0 * PI));
    vec3 colorShift = vec3(
        sin(rotationPhase * 2.0 * PI) * 0.05,
        sin(rotationPhase * 2.0 * PI + 2.094) * 0.05, // 120 degrees offset
        sin(rotationPhase * 2.0 * PI + 4.189) * 0.05  // 240 degrees offset
    );
    
    result += colorShift * uStrength;
    
    FragColor = vec4(result, sceneColor.a);
}
