#version 450
#extension GL_GOOGLE_include_directive : require

#include "shared_ubo.glsl"

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 1) uniform sampler2D inputTexture;

vec2 applyGrid(vec2 uv) {
    if (ubo.enableGrid != 1) return uv;

    vec2 gridSize = vec2(1.0);

    // Select grid layout
    if (ubo.gridMode == 0) {
        gridSize = vec2(float(max(ubo.gridCount, 1)), 1.0);
    }
    else if (ubo.gridMode == 1) {
        gridSize = vec2(1.0, float(max(ubo.gridCount, 1)));
    }
    else { 
        gridSize = vec2(
            float(max(ubo.gridColumns, 1)),
            float(max(ubo.gridRows, 1))
        );
    }

    // Scale into grid space
    vec2 cellUV = uv * gridSize;

    vec2 cellID = floor(cellUV);
    vec2 localUV = fract(cellUV);

    if (ubo.gridMirrorCells == 1) {
        vec2 mirrorMask = mod(cellID, 2.0);
        localUV = mix(localUV, 1.0 - localUV, step(0.5, mirrorMask));
    }

    return clamp(localUV, 0.0, 1.0);
}

void main() {
    vec2 sampleUV = applyGrid(uv);
    fragColor = texture(inputTexture, sampleUV);
}