#version 450
#extension GL_GOOGLE_include_directive : require

#include "shared_ubo.glsl"

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 1) uniform sampler2D inputTexture;

layout(push_constant) uniform PushConstants {
    int previewOverlay;
} pc;

// 5×7 bitmap atlas (A-Z + space) --------------------------------------------------------
const int ATLAS[189] = int[](
    14, 17, 17, 31, 17, 17, 17,  // A (0)
    30, 17, 17, 30, 17, 17, 30,  // B (1)
    14, 17, 16, 16, 16, 17, 14,  // C (2)
    30, 17, 17, 17, 17, 17, 30,  // D (3)
    31, 16, 16, 30, 16, 16, 31,  // E (4)
    31, 16, 16, 30, 16, 16, 16,  // F (5)
    14, 17, 16, 23, 17, 17, 15,  // G (6)
    17, 17, 17, 31, 17, 17, 17,  // H (7)
    31,  4,  4,  4,  4,  4, 31,  // I (8)
     1,  1,  1,  1, 17, 17, 14,  // J (9)
    17, 18, 20, 24, 20, 18, 17,  // K (10)
    16, 16, 16, 16, 16, 16, 31,  // L (11)
    17, 27, 21, 21, 17, 17, 17,  // M (12)
    17, 25, 21, 21, 19, 17, 17,  // N (13)
    14, 17, 17, 17, 17, 17, 14,  // O (14)
    30, 17, 17, 30, 16, 16, 16,  // P (15)
    14, 17, 17, 17, 17, 21, 18,  // Q (16)
    30, 17, 17, 30, 20, 18, 17,  // R (17)
    15, 16, 16, 14,  1,  1, 30,  // S (18)
    31,  4,  4,  4,  4,  4,  4,  // T (19)
    17, 17, 17, 17, 17, 17, 14,  // U (20)
    17, 17, 17, 17, 17, 10,  4,  // V (21)
    17, 17, 17, 21, 21, 21, 10,  // W (22)
    17, 17, 10,  4, 10, 17, 17,  // X (23)
    17, 17, 10,  4,  4,  4,  4,  // Y (24)
    31,  1,  2,  4,  8, 16, 31,  // Z (25)
     0,  0,  0,  0,  0,  0,  0   // space (26)
);

// P=15, R=17, E=4, V=21, I=8, E=4, W=22
const int PREVIEW_GLYPHS[7] = int[](15, 17, 4, 21, 8, 4, 22);
// P=15, A=0, U=20, S=18, E=4, D=3
const int PAUSED_GLYPHS[6] = int[](15, 0, 20, 18, 4, 3);

int glyphRow(int g, int r) {
    if (r < 0 || r >= 7) return 0;
    return ATLAS[g * 7 + r];
}

float sampleGlyph(int g, vec2 uv) {
    const float HMARGIN = 0.08;
    const float VMARGIN = 0.05;
    vec2 inner = (uv - vec2(HMARGIN, VMARGIN))
               / vec2(1.0 - 2.0 * HMARGIN, 1.0 - 2.0 * VMARGIN);
    if (any(lessThan(inner, vec2(0.0))) || any(greaterThan(inner, vec2(1.0)))) {
        return 0.0;
    }

    vec2  p    = inner * vec2(5.0, 7.0);
    ivec2 cell = ivec2(floor(p));
    if (cell.x < 0 || cell.x >= 5 || cell.y < 0 || cell.y >= 7) {
        return 0.0;
    }

    int   mask = glyphRow(g, cell.y);
    int   bit  = 1 << (4 - cell.x);
    float on   = (mask & bit) != 0 ? 1.0 : 0.0;

    vec2  frc = fract(p);
    float aa  = smoothstep(0.01, 0.10, frc.x)
              * smoothstep(0.01, 0.10, frc.y)
              * smoothstep(0.01, 0.10, 1.0 - frc.x)
              * smoothstep(0.01, 0.10, 1.0 - frc.y);
    return on * aa;
}

float drawPreviewLabel(vec2 uvCoord) {
    // Banner at top-left: 7 letters × 6 px wide + spacing
    vec2 origin = vec2(0.008, 0.91);
    float glyphW = 0.011;   // width per letter in UV
    float glyphH = 0.048;   // height of label in UV
    vec2 p = (uvCoord - origin) / vec2(glyphW, glyphH);

    int letter = int(p.x);
    vec2 local = vec2(fract(p.x), p.y);

    if (letter < 0 || letter > 6) return 0.0;
    if (p.y < 0.0 || p.y > 1.0) return 0.0;

    return sampleGlyph(PREVIEW_GLYPHS[letter], local);
}

float drawPausedLabel(vec2 uvCoord) {
    // Banner at top-left: 6 letters
    vec2 origin = vec2(0.008, 0.91);
    float glyphW = 0.011;
    float glyphH = 0.048;
    vec2 p = (uvCoord - origin) / vec2(glyphW, glyphH);

    int letter = int(p.x);
    vec2 local = vec2(fract(p.x), p.y);

    if (letter < 0 || letter > 5) return 0.0;
    if (p.y < 0.0 || p.y > 1.0) return 0.0;

    return sampleGlyph(PAUSED_GLYPHS[letter], local);
}

vec2 applyOutputAspect(vec2 uv) {
    if (ubo.outputAspectRatio == 0) return uv;

    float targetAspect = 0.0;
    if (ubo.outputAspectRatio == 1) targetAspect = 4.0 / 3.0;
    else if (ubo.outputAspectRatio == 2) targetAspect = 16.0 / 9.0;
    else if (ubo.outputAspectRatio == 3) targetAspect = 19.0 / 10.0;
    else return uv;

    float screenAspect = ubo.resolution.x / max(ubo.resolution.y, 1.0);
    vec2 scale = vec2(1.0);
    if (screenAspect > targetAspect) {
        scale.x = targetAspect / screenAspect;
    } else {
        scale.y = screenAspect / targetAspect;
    }
    return (uv - 0.5) / scale + 0.5;
}

vec3 sampleInput(vec2 uv) {
    vec2 p = applyOutputAspect(uv);
    return texture(inputTexture, clamp(p, 0.0, 1.0)).rgb;
}

void main() {
    if (pc.previewOverlay == 2) {
        // Paused: black background + red PAUSED label
        fragColor = vec4(0.0, 0.0, 0.0, 1.0);

        float label = drawPausedLabel(uv);
        if (label > 0.5) {
            fragColor = mix(fragColor, vec4(1.0, 0.3, 0.3, 1.0), 0.85);
        }
        return;
    }

    fragColor = vec4(sampleInput(uv), 1.0);

    if (pc.previewOverlay == 1) {
        // Cyan border
        float b = 0.012;
        if (uv.x < b || uv.x > 1.0 - b || uv.y < b || uv.y > 1.0 - b) {
            fragColor = mix(fragColor, vec4(0.0, 0.8, 1.0, 1.0), 0.5);
        }

        // "PREVIEW" label
        float label = drawPreviewLabel(uv);
        if (label > 0.5) {
            fragColor = mix(fragColor, vec4(0.0, 0.9, 1.0, 1.0), 0.85);
        }
    }
}