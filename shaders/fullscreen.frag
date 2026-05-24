#version 450
#extension GL_GOOGLE_include_directive : require

#include "shared_ubo.glsl"

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 1) uniform sampler2D inputTexture;

vec2 mirrorCell(vec2 p, vec2 gridSize, int mirrorCells) {
    vec2 cell = floor(p * gridSize);
    vec2 f = fract(p * gridSize);

    if (mirrorCells == 1) {
        if (int(mod(cell.x, 2.0)) != 0) f.x = 1.0 - f.x;
        if (int(mod(cell.y, 2.0)) != 0) f.y = 1.0 - f.y;
    }

    return f;
}

vec2 applyGrid(vec2 p) {
    if (ubo.enableGrid != 1) return p;

    if (ubo.gridMode == 0 && ubo.gridCount > 1) {
        // Duplicar horizontalmente (N columnas)
        float x = p.x * float(ubo.gridCount);
        float cellX = floor(x);
        p.x = fract(x);
        if (ubo.gridMirrorCells == 1 && int(cellX) % 2 != 0)
            p.x = 1.0 - p.x;

    } else if (ubo.gridMode == 1 && ubo.gridCount > 1) {
        // Duplicar verticalmente (N filas)
        float y = p.y * float(ubo.gridCount);
        float cellY = floor(y);
        p.y = fract(y);
        if (ubo.gridMirrorCells == 1 && int(cellY) % 2 != 0)
            p.y = 1.0 - p.y;

    } else if (ubo.gridMode == 2 && ubo.gridRows > 0 && ubo.gridColumns > 0) {
        // Matriz de filas × columnas
        float x = p.x * float(ubo.gridColumns);
        float y = p.y * float(ubo.gridRows);
        float cellX = floor(x);
        float cellY = floor(y);
        p.x = fract(x);
        p.y = fract(y);
        if (ubo.gridMirrorCells == 1) {
            if (int(cellX) % 2 != 0) p.x = 1.0 - p.x;
            if (int(cellY) % 2 != 0) p.y = 1.0 - p.y;
        }
    }

    return clamp(p, 0.0, 1.0);
}

void main() {
    vec2 sampleUV = uv;

    if (ubo.enableGrid == 1) {
        if (ubo.gridMode == 0 && ubo.gridCount > 1) {
            float x = uv.x * float(ubo.gridCount);
            sampleUV.x = fract(x);
            if (ubo.gridMirrorCells == 1 && int(floor(x)) % 2 != 0)
                sampleUV.x = 1.0 - sampleUV.x;

        } else if (ubo.gridMode == 1 && ubo.gridCount > 1) {
            float y = uv.y * float(ubo.gridCount);
            sampleUV.y = fract(y);
            if (ubo.gridMirrorCells == 1 && int(floor(y)) % 2 != 0)
                sampleUV.y = 1.0 - sampleUV.y;

        } else if (ubo.gridMode == 2 && ubo.gridRows > 0 && ubo.gridColumns > 0) {
            float x = uv.x * float(ubo.gridColumns);
            float y = uv.y * float(ubo.gridRows);
            sampleUV.x = fract(x);
            sampleUV.y = fract(y);
            if (ubo.gridMirrorCells == 1) {
                if (int(floor(x)) % 2 != 0) sampleUV.x = 1.0 - sampleUV.x;
                if (int(floor(y)) % 2 != 0) sampleUV.y = 1.0 - sampleUV.y;
            }
        }
    }

    fragColor = texture(inputTexture, clamp(sampleUV, 0.0, 1.0));
}