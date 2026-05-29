#version 450
#extension GL_GOOGLE_include_directive : require

#include "shared_ubo.glsl"

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 1) uniform sampler2D inputTexture;

void main() {
    fragColor = texture(inputTexture, uv);
}