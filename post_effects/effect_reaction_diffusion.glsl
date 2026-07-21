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
  Reaction Diffusion post-effect — motion detection with noise-based diffusion.
  Detects movement between frames and propagates it to neighbors with noise.
  Based on Shadertoy reaction-diffusion shader.
*/

const float punch = 0.1;
const float odds = 0.4;
const float timeMod = 0.01;
const float amp = 0.8;

float rand(float n) {
    return fract(sin(n) * 43758.5453123);
}

float rand(vec2 co) {
    float a = 12.9898;
    float b = 78.233;
    float c = 43758.5453;
    float dt = dot(co.xy, vec2(a, b));
    float sn = mod(dt, 3.14);
    return fract(sin(sn) * c);
}

float noise(float p) {
    float fl = floor(p);
    float fc = fract(p);
    return mix(rand(fl), rand(fl + 1.0), fc);
}

float noise(vec2 n) {
    const vec2 d = vec2(0.0, 1.0);
    vec2 b = floor(n), f = smoothstep(vec2(0.0), vec2(1.0), fract(n));
    return mix(mix(rand(b), rand(b + d.yx), f.x), mix(rand(b + d.xy), rand(b + d.yy), f.x), f.y);
}

void main() {
    vec2 tc = gl_GlobalInvocationID.xy / uResolution.xy;
    vec2 res = uResolution.xy;
    float time = uTime * 50.0;
    vec2 offs = 1.0 / res;
    
    // Sample current frame
    vec4 tex = texture(uScene, tc);
    
    // Sample neighbors for diffusion
    vec4 n = texture(uScene, tc + vec2(0.0, -offs.y));
    vec4 e = texture(uScene, tc + vec2(offs.x, 0.0));
    vec4 s = texture(uScene, tc + vec2(0.0, offs.y));
    vec4 w = texture(uScene, tc + vec2(-offs.x, 0.0));
    
    vec4 ne = texture(uScene, tc + vec2(offs.x, -offs.y));
    vec4 nw = texture(uScene, tc + vec2(-offs.x, -offs.y));
    vec4 se = texture(uScene, tc + vec2(offs.x, offs.y));
    vec4 sw = texture(uScene, tc + vec2(-offs.x, offs.y));
    
    // Position-based noise
    vec2 pos = tc * res;
    float p = pos.y * res.x + pos.x;
    p /= (res.x * res.y);
    
    // Apply noise-based diffusion to neighbors
    n = mix(vec4(0.0), n, floor(noise(1000.0 + time * 0.05 + tc.x * res.x * punch) + odds));
    e = mix(vec4(0.0), e, floor(noise(123.0 + time * 0.05 + tc.y * res.y * punch) + odds));
    s = mix(vec4(0.0), s, floor(noise(-78.0 + time * 0.05 + tc.x * res.x * punch) + odds));
    w = mix(vec4(0.0), w, floor(noise(42.0 + time * 0.05 + tc.y * res.y * punch) + odds));
    
    ne = mix(vec4(0.0), ne, floor(noise(1000.0 + time * 0.05 + tc.x * res.x * punch + tc.y) + odds));
    nw = mix(vec4(0.0), nw, floor(noise(123.0 + time * 0.05 + tc.y * res.y * punch + tc.x) + odds));
    se = mix(vec4(0.0), se, floor(noise(-78.0 + time * 0.05 + tc.x * res.x * punch + tc.y) + odds));
    sw = mix(vec4(0.0), sw, floor(noise(42.0 + time * 0.05 + tc.y * res.y * punch + tc.x) + odds));
    
    // Accumulate neighbor contributions
    tex += (n + e + s + w);
    tex += (ne + nw + se + sw);
    
    // Seed initialization in center of screen (every few seconds)
    int frame = int(uTime * 60.0);
    if (frame < 1 || frame % 1500 == 0) {
        if (tc.x > 0.49 && tc.x < 0.5 && tc.y > 0.49 && tc.y < 0.5) {
            tex = vec4(1.0);
        } else {
            tex = vec4(0.0, 0.0, 0.0, 1.0);
        }
    }
    
    // Apply color noise diffusion on bright areas
    if (tex.r >= 0.75) {
        vec4 draw = tex;
        
        draw.r = sin(n.r * amp + w.r * amp + e.r * amp + s.r * amp + noise(402.0 + tc * p + time * timeMod * 1.01));
        draw.g = sin(e.r * amp + w.r * amp + s.r * amp + n.r * amp + noise(10.4 + tc * p + time * timeMod * 2.1));
        draw.b = sin(s.r * amp + w.r * amp + n.r * amp + e.r * amp + noise(32.0 + tc * p + time * timeMod));
        
        tex = mix(tex, draw, uStrength);
    }
    
    // Mix with original based on strength
    vec4 original = texture(uScene, tc);
    vec4 result = mix(original, tex, uStrength);
    
    imageStore(outputImage, ivec2(gl_GlobalInvocationID.xy), vec4(result.rgb, 1.0));
}
