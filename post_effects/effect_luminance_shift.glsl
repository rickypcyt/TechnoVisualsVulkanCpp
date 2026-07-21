#version 430 core

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(binding = 0) uniform sampler2D uScene;
layout(binding = 1, rgba16f) uniform writeonly image2D outputImage;

layout(std140, binding = 2) uniform UBO {
    vec2 uResolution;
    float uTime;
    float uStrength;
    float uBassLevel;
};

/*
  Luminance Shift post-effect — random vertical displacement based on luminance.
  Creates glitchy vertical shifts that alternate direction based on brightness.
  Based on Shadertoy luminance-based displacement shader.
*/

#define MAX_OFFSET 80.0

float rand(float co) {
    return fract(sin(co * 91.3458) * 47453.5453);
}

void main() {
    vec2 uv = gl_GlobalInvocationID.xy / uResolution.xy;
    vec2 texel = 1.0 / uResolution.xy;
    
    vec4 img = texture(uScene, uv);
    
    // Modulate offset with time and randomness
    float step_y = texel.y * (rand(uv.x) * MAX_OFFSET) * (sin(sin(uTime * 0.5)) * 2.0 + 1.3);
    step_y += rand(uv.x * uv.y * uTime) * 0.025 * sin(uTime);
    step_y = mix(step_y, step_y * rand(uv.x * uTime) * 0.5, sin(uTime));
    
    // Scale offset by strength
    step_y *= uStrength;
    
    // Calculate luminance
    float luminance = dot(img, vec4(0.299, 0.587, 0.114, 0.0));
    float threshold = 1.2 * (sin(uTime) * 0.325 + 0.50);
    
    // Shift up or down based on luminance threshold
    if (luminance > threshold) {
        uv.y += step_y;
    } else {
        uv.y -= step_y;
    }
    
    // Sample with shifted UV
    vec4 result = texture(uScene, uv);
    
    imageStore(outputImage, ivec2(gl_GlobalInvocationID.xy), result);
}
