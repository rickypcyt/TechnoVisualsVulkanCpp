#version 450

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform GlobalUBO {
    vec4 resolutionTime;
} ubo;

void main() {
    float time = ubo.resolutionTime.z;
    vec2 st = uv;
    vec3 col = vec3(st, 0.5 + 0.5 * sin(time));
    outColor = vec4(col, 1.0);
}
