// @EFFECT name="CRISTIAN CAMILO Grid" index=98 desc="Bottom marquee that spells CRISTIAN CAMILO" author="System" zoom=3.43

// CRISTIAN CAMILO marquee glyph data -----------------------------------------------------
const int CRISTIAN_MESSAGE_LEN = 16;
const int CRISTIAN_GLYPH_C = 2;   // C is index 2
const int CRISTIAN_GLYPH_R = 17;  // R is index 17
const int CRISTIAN_GLYPH_I = 8;   // I is index 8
const int CRISTIAN_GLYPH_S = 18;  // S is index 18
const int CRISTIAN_GLYPH_T = 19;  // T is index 19
const int CRISTIAN_GLYPH_A = 0;   // A is index 0
const int CRISTIAN_GLYPH_N = 13;  // N is index 13
const int CRISTIAN_GLYPH_M = 12;  // M is index 12
const int CRISTIAN_GLYPH_L = 11;  // L is index 11
const int CRISTIAN_GLYPH_O = 14;  // O is index 14
const int CRISTIAN_GLYPH_SPACE = 26; // space is index 26

// A-Z (5×7 bitmap rows) + space
const int CRISTIAN_ATLAS[189] = int[](
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
    31, 14, 17, 17, 17, 21, 18,  // Q (16)
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

const int CRISTIAN_MESSAGE[CRISTIAN_MESSAGE_LEN] = int[](
    CRISTIAN_GLYPH_C,
    CRISTIAN_GLYPH_R,
    CRISTIAN_GLYPH_I,
    CRISTIAN_GLYPH_S,
    CRISTIAN_GLYPH_T,
    CRISTIAN_GLYPH_I,
    CRISTIAN_GLYPH_A,
    CRISTIAN_GLYPH_N,
    CRISTIAN_GLYPH_SPACE,
    CRISTIAN_GLYPH_C,
    CRISTIAN_GLYPH_A,
    CRISTIAN_GLYPH_M,
    CRISTIAN_GLYPH_I,
    CRISTIAN_GLYPH_L,
    CRISTIAN_GLYPH_O,
    CRISTIAN_GLYPH_SPACE  // Extra space after CAMILO
);

int cristianGlyphRow(int g, int r) {
    if (r < 0 || r >= 7) return 0;
    return CRISTIAN_ATLAS[g * 7 + r];
}

float sampleCristianGlyph(int g, vec2 uv) {
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

    int   mask = cristianGlyphRow(g, cell.y);
    int   bit  = 1 << (4 - cell.x);
    float on   = (mask & bit) != 0 ? 1.0 : 0.0;

    vec2  frc = fract(p);
    float aa  = smoothstep(0.01, 0.03, frc.x)
              * smoothstep(0.01, 0.03, frc.y)
              * smoothstep(0.01, 0.03, 1.0 - frc.x)
              * smoothstep(0.01, 0.03, 1.0 - frc.y);
    return on * aa;
}

float marqueeCristianChar(vec2 uv, float scrollX, out int col) {
    float cx = fract(uv.x - scrollX / float(CRISTIAN_MESSAGE_LEN)) * float(CRISTIAN_MESSAGE_LEN);
    col = int(floor(cx)) % CRISTIAN_MESSAGE_LEN;
    if (col < 0) col += CRISTIAN_MESSAGE_LEN;
    vec2 gUV = vec2(fract(cx), uv.y);
    return sampleCristianGlyph(CRISTIAN_MESSAGE[col], gUV);
}

vec4 renderCristianCamiloGrid(
    vec2  st,
    float time,
    float tempo,
    float energy,
    float bass,
    float mid,
    float high)
{
    vec2 uv = st * 0.5 + 0.5;
    uv.x *= uResolution.y / max(uResolution.x, 1.0);
    uv += vec2(-0.35, -0.48);

    const int ROW_COUNT = 1;
    float bandH = 0.26;
    float baseY = 0.008;

    vec3 bg = mix(vec3(0.015, 0.015, 0.025),
                  uSecondaryColor * 0.04,
                  clamp(uColorBlend, 0.0, 1.0));
    vec3 color = bg;
    float alpha = 0.0f;

    for (int row = 0; row < ROW_COUNT; ++row) {
        float bandY = baseY;
        float wobble = 0.006 * sin(time * 3.2 + uv.x * 8.0);
        float band = (uv.y - bandY + wobble) / bandH;

        float edgeFade = smoothstep(0.0, 0.12, band)
                       * smoothstep(1.0, 0.88, band);
        if (edgeFade <= 0.001) {
            continue;
        }

        float scanRow = fract(band * 7.0);
        float scanLine = 1.0 - 0.05 * (1.0 - smoothstep(0.9, 1.0, scanRow));
        float gridMask = sin((uv.x + time * 0.6) * 320.0) * sin((uv.y + time * 0.6) * 180.0);
        float ledPulse = 0.5 + 0.5 * gridMask;

        const float scrollSpeed = 0.32;
        float scrollPhase = time * scrollSpeed;
        float scrollX = mod(scrollPhase, float(CRISTIAN_MESSAGE_LEN));

        int col = 0;
        float glyphX = fract((uv.x - 0.02) * 1.35 + 10.0);
        vec2 glyphUV = vec2(glyphX, 1.0 - band);
        float charVal = marqueeCristianChar(glyphUV, scrollX, col);
        if (charVal <= 0.0001) {
            continue;
        }

        // Bright white letters with high intensity
        vec3 letterColor = vec3(2.5);
        
        // Strong glow halo around letters
        float glowAmt = charVal * 0.8 * edgeFade;
        vec3 glowCol = vec3(1.5) * 0.8;
        
        // Full brightness where letters exist
        float letterMask = charVal * edgeFade;
        color = mix(color, letterColor, letterMask);
        color += glowAmt * glowCol;
        
        // Very high alpha for letter visibility
        alpha = max(alpha, charVal * edgeFade * 1.5);
    }

    return vec4(color, alpha);
}
