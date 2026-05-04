 #version 450

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform UBO {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec2 resolution;
    float time;
    float tempo;
    float energy;
    float bass;
    float mid;
    float high;
    vec4 primaryColor;
    vec4 secondaryColor;
    float colorBlend;
    int mode;
} ubo;

vec4 renderMode0(vec2 st) {
    return vec4(1.0, 0.2, 0.2, 1.0);
}

vec4 renderMode1(vec2 st) {
    return vec4(st, 0.5 + 0.5 * sin(ubo.time), 1.0);
}

vec3 voxelField(vec3 ro, vec3 rd) {
    vec3 p = ro + rd * 10.0;
    float d = fract(p.x * p.y * p.z);
    return vec3(smoothstep(0.0, 1.0, d));
}

vec4 renderVoxel(vec2 st) {
    vec3 ro = vec3(0.0, 2.0, -5.0);
    vec3 rd = normalize(vec3(st, 1.0));
    float d = length(voxelField(ro, rd));
    float shade = exp(-d * 0.2);
    return vec4( vec3(shade), 1.0 );
}

vec4 dispatchMode(int m, vec2 st) {
    if (m == 0) return renderMode0(st);
    if (m == 1) return renderMode1(st);
    if (m == 2) return renderVoxel(st);
    return vec4(0.0);
}

void main() {
    outColor = dispatchMode(ubo.mode, uv);
}
