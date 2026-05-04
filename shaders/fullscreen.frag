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

void main() {
    vec4 col;

    if (ubo.mode == 0) {
        col = vec4(1.0, 0.0, 0.0, 1.0);
    } else if (ubo.mode == 1) {
        col = vec4(0.0, 1.0, 0.0, 1.0);
    } else {
        col = vec4(0.0, 0.0, 1.0, 1.0);
    }

    outColor = col;
}
