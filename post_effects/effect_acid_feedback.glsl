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
  Acid Feedback post-effect — psychedelic RGB displacement with feedback.
  Creates swirling, trippy color separation and distortion effects.
  Based on Shadertoy acid-style feedback shaders.
*/

#define rate 1.5
#define push_RGB 10.09
#define billow 9.10
#define push_RGB2 1.9209
#define startFrame 100
#define leak 1.50
#define scale 0.201
#define didYOUeatALLthatACID 1.09729
#define feedback 0.45

void main() {
    vec2 coord = gl_GlobalInvocationID.xy / uResolution.xy;
    
    vec4 sceneColor = texture(uScene, coord);
    
    // Initialize with original image for first few frames
    if (uTime < 1.0) {
        imageStore(outputImage, ivec2(gl_GlobalInvocationID.xy), sceneColor);
        return;
    }
    
    vec2 texel = rate / uResolution.xy;
    vec3 uv3 = texture(uScene, coord * (1.0 + leak * 0.005)).xyz;
    
    float gt = mod(uTime * coord.x * coord.y, billow * 6.1415) * scale;
    
    vec2 d1 = vec2(uv3.x * vec2(texel.x * cos(gt * uv3.z), texel.y * sin(gt * uv3.y)));
    vec2 d2 = vec2(uv3.y * vec2(texel.x * cos(gt * uv3.x), texel.y * sin(gt * uv3.y)));
    vec2 d3 = vec2(uv3.z * vec2(texel.x * cos(gt * uv3.y), texel.y * sin(gt * uv3.y)));
    
    float bright = (uv3.x + uv3.y + uv3.z) / push_RGB + push_RGB2;
    
    float r = texture(uScene, coord + d1 * bright).x;
    float g = texture(uScene, coord + d2 * bright).y;
    float b = texture(uScene, coord + d3 * bright).z;
    
    vec3 uvMix = mix(uv3, vec3(r, g, b), didYOUeatALLthatACID);
    
    vec3 orig = texture(uScene, coord).xyz;
    
    vec4 result = vec4(mix(uvMix, orig, 0.50 - feedback), 0.5);
    
    // Mix with original based on strength
    vec4 finalColor = mix(sceneColor, result, uStrength);
    
    imageStore(outputImage, ivec2(gl_GlobalInvocationID.xy), finalColor);
}
