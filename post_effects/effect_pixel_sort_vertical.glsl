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
  Pixel Sort Vertical post-effect — alternating vertical sorting by luminance.
  Compares pixel brightness with neighbors and swaps based on frame parity.
  Creates a "bubble sort" effect that alternates direction each frame.
*/

void main() {
    vec2 uv = gl_GlobalInvocationID.xy / uResolution.xy;
    vec2 texel = 1.0 / uResolution.xy;
    
    float step_y = texel.y;
    vec2 s = vec2(0.0, -step_y);
    vec2 n = vec2(0.0, step_y);

    vec4 im_n = texture(uScene, uv + n);
    vec4 im = texture(uScene, uv);
    vec4 im_s = texture(uScene, uv + s);
    
    float len_n = length(im_n);
    float len = length(im);
    float len_s = length(im_s);
    
    // Alternate direction based on frame (using time as frame counter)
    int frame = int(uTime * 60.0); // Approximate frame count
    vec4 result = im;
    
    if (mod(float(frame) + gl_GlobalInvocationID.y, 2.0) < 1.0) {
        if (len_s > len) { 
            result = im_s;    
        }
    } else {
        if (len_n < len) { 
            result = im_n;    
        }   
    }
    
    // Initialize with original image for first few frames
    if (uTime < 0.2) {
        result = im;
    }
    
    // Mix with original based on strength
    vec4 finalColor = mix(im, result, uStrength);
    
    imageStore(outputImage, ivec2(gl_GlobalInvocationID.xy), finalColor);
}
