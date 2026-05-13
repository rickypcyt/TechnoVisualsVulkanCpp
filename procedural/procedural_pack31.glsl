// @EFFECT name="Hello World Grid" index=56 desc="Minimal HELLO, WORLD! marquee with palette tint" author="Lygia-inspired"

// ─── Glyph atlas ──────────────────────────────────────────────────────────────
// 10 glyphs × 7 rows, 5-bit bitmask per row (bit 4 = leftmost column)
// Order: H E L O , [space] W R D !

int glyphRow(int g, int r) {
    if (r < 0 || r >= 7) return 0;
    const int ATLAS[70] = int[](
        17, 17, 31, 17, 17, 17, 17,  // H
        31, 16, 16, 31, 16, 16, 31,  // E
        16, 16, 16, 16, 16, 16, 31,  // L
        14, 17, 17, 17, 17, 17, 14,  // O
         0,  0,  0,  0,  0, 12,  8,  // ,
         0,  0,  0,  0,  0,  0,  0,  // [space]
        17, 17, 21, 21, 21, 31, 17,  // W
        31, 17, 17, 31, 20, 18, 17,  // R
        30, 17, 17, 17, 17, 17, 30,  // D
         4,  4,  4,  4,  4,  0,  4   // !
    );
    return ATLAS[g * 7 + r];
}

// ─── Message ──────────────────────────────────────────────────────────────────
const int MESSAGE_LEN = 13;
const int MESSAGE[13] = int[](0, 1, 2, 2, 3, 4, 5, 6, 3, 7, 2, 8, 9);
// H  E  L  L  O  ,  _  W  O  R  L  D  !

// LITN marquee glyph data -----------------------------------------------------
const int LITN_MESSAGE_LEN = 5;
const int LITN_GLYPH_L = 11;
const int LITN_GLYPH_I = 8;
const int LITN_GLYPH_T = 19;
const int LITN_GLYPH_N = 13;
const int LITN_GLYPH_SPACE = 26;

// A-Z (5×7 bitmap rows) + space
const int LITN_ATLAS[189] = int[](
    14, 17, 17, 31, 17, 17, 17,  // A
    30, 17, 17, 30, 17, 17, 30,  // B
    14, 17, 16, 16, 16, 17, 14,  // C
    30, 17, 17, 17, 17, 17, 30,  // D
    31, 16, 16, 30, 16, 16, 31,  // E
    31, 16, 16, 30, 16, 16, 16,  // F
    14, 17, 16, 23, 17, 17, 15,  // G
    17, 17, 17, 31, 17, 17, 17,  // H
    31,  4,  4,  4,  4,  4, 31,  // I
     1,  1,  1,  1, 17, 17, 14,  // J
    17, 18, 20, 24, 20, 18, 17,  // K
    16, 16, 16, 16, 16, 16, 31,  // L
    17, 27, 21, 21, 17, 17, 17,  // M
    17, 25, 21, 21, 19, 17, 17,  // N
    14, 17, 17, 17, 17, 17, 14,  // O
    30, 17, 17, 30, 16, 16, 16,  // P
    14, 17, 17, 17, 21, 18, 13,  // Q
    30, 17, 17, 30, 20, 18, 17,  // R
    15, 16, 16, 14,  1,  1, 30,  // S
    31,  4,  4,  4,  4,  4,  4,  // T
    17, 17, 17, 17, 17, 17, 14,  // U
    17, 17, 17, 17, 17, 10,  4,  // V
    17, 17, 17, 21, 21, 21, 10,  // W
    17, 17, 10,  4, 10, 17, 17,  // X
    17, 17, 10,  4,  4,  4,  4,  // Y
    31,  1,  2,  4,  8, 16, 31,  // Z
     0,  0,  0,  0,  0,  0,  0   // space
);

const int LITN_MESSAGE[LITN_MESSAGE_LEN] = int[](
    LITN_GLYPH_L,
    LITN_GLYPH_I,
    LITN_GLYPH_T,
    LITN_GLYPH_N,
    LITN_GLYPH_SPACE
);

int litnGlyphRow(int g, int r) {
    if (r < 0 || r >= 7) return 0;
    return LITN_ATLAS[g * 7 + r];
}

float sampleLitnGlyph(int g, vec2 uv) {
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

    int   mask = litnGlyphRow(g, cell.y);
    int   bit  = 1 << (4 - cell.x);
    float on   = (mask & bit) != 0 ? 1.0 : 0.0;

    vec2  frc = fract(p);
    float aa  = smoothstep(0.01, 0.03, frc.x)
              * smoothstep(0.01, 0.03, frc.y)
              * smoothstep(0.01, 0.03, 1.0 - frc.x)
              * smoothstep(0.01, 0.03, 1.0 - frc.y);
    return on * aa;
}

float marqueeLitnChar(vec2 uv, float scrollX, out int col) {
    float cx = fract(uv.x - scrollX / float(LITN_MESSAGE_LEN)) * float(LITN_MESSAGE_LEN);
    col = int(floor(cx)) % LITN_MESSAGE_LEN;
    if (col < 0) col += LITN_MESSAGE_LEN;
    vec2 gUV = vec2(fract(cx), uv.y);
    return sampleLitnGlyph(LITN_MESSAGE[col], gUV);
}

// @EFFECT name="LITN Grid" index=58 desc="Multi-row white marquee that spells LITN" author="System" zoom=3.43
vec4 renderLitnGrid(
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
    float alpha = 0.0;

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
        float scrollX = mod(scrollPhase, float(LITN_MESSAGE_LEN));

        int col = 0;
        float glyphX = fract((uv.x - 0.02) * 1.35 + 10.0);
        vec2 glyphUV = vec2(glyphX, 1.0 - band);
        float charVal = marqueeLitnChar(glyphUV, scrollX, col);
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

// ─── Glyph sampler ────────────────────────────────────────────────────────────
// Pixel grid is mapped with a narrow dead-zone on each edge so adjacent
// characters have a visible gap, and the feather is kept very tight so
// pixels read as crisp on-screen even at low resolutions.
//
// SPACING trick: instead of the full [0,1] UV range we use [MARGIN, 1-MARGIN]
// for the active glyph area.  The rest is always 0 → pure black gap.

float sampleGlyph(int g, vec2 uv) {
    // Character cell margin — increases inter-character gap without changing
    // the glyph data.  0.08 = ~8% dead-zone on each horizontal side.
    const float HMARGIN = 0.08;  // horizontal gap between characters
    const float VMARGIN = 0.04;  // small top/bottom breathing room

    // Remap uv into the active area, return 0 outside it
    vec2 inner = (uv - vec2(HMARGIN, VMARGIN))
               / vec2(1.0 - 2.0 * HMARGIN, 1.0 - 2.0 * VMARGIN);
    if (any(lessThan(inner, vec2(0.0))) || any(greaterThan(inner, vec2(1.0))))
        return 0.0;

    vec2  p    = inner * vec2(5.0, 7.0);
    ivec2 cell = ivec2(floor(p));
    // Clamp to valid range (remap guarantees this, but guard anyway)
    if (cell.x < 0 || cell.x >= 5 || cell.y < 0 || cell.y >= 7) return 0.0;

    int   mask = glyphRow(g, cell.y);
    int   bit  = 1 << (4 - cell.x);
    float on   = (mask & bit) != 0 ? 1.0 : 0.0;

    // Very tight feather — keeps pixels crisp while removing the hard 1-pixel
    // aliasing step.  Use a narrower range than before (0.03–0.08 vs 0.04–0.12).
    vec2  frc = fract(p);
    float aa  = smoothstep(0.03, 0.08, frc.x)
              * smoothstep(0.03, 0.08, frc.y)
              * smoothstep(0.03, 0.08, 1.0 - frc.x)
              * smoothstep(0.03, 0.08, 1.0 - frc.y);
    return on * aa;
}

// ─── Infinite marquee ─────────────────────────────────────────────────────────

float marqueeChar(vec2 uv, float scrollX, out int col) {
    float cx = fract(uv.x - scrollX / float(MESSAGE_LEN)) * float(MESSAGE_LEN);
    col = int(floor(cx)) % MESSAGE_LEN;
    if (col < 0) col += MESSAGE_LEN;
    vec2 gUV = vec2(fract(cx), uv.y);
    return sampleGlyph(MESSAGE[col], gUV);
}

// ─── Main entry point ─────────────────────────────────────────────────────────

vec4 renderHelloWorldGrid(
    vec2  st,
    float time,
    float tempo,
    float energy,
    float bass,
    float mid,
    float high)
{
    // ── Aspect-correct UV ────────────────────────────────────────────────────
    vec2 uv  = st * 0.5 + 0.5;
    uv.x    *= uResolution.y / max(uResolution.x, 1.0);

    // ── Multi-row layout ─────────────────────────────────────────────────────
    // bandH is taller so each glyph row has more pixels → clearer letterforms.
    // rowSpacing adds extra breathing room between rows.
    const int ROW_COUNT  = 6;
    float bandH          = 0.18 + energy * 0.02;   // taller band → bigger glyphs
    float rowSpacing     = bandH + 0.06;             // fixed gap, not energy-driven
    float baseY          = 0.5  + mid * 0.02;        // gentler vertical float

    vec3  bg    = mix(vec3(0.02, 0.02, 0.04),
                      uSecondaryColor * 0.05,
                      clamp(uColorBlend, 0.0, 1.0));
    vec3  color = bg;
    float alpha = 0.0;

    for (int row = 0; row < ROW_COUNT; ++row) {
        float offset = (float(row) - 0.5 * float(ROW_COUNT - 1)) * rowSpacing;
        float bandY  = baseY + offset;
        float band   = (uv.y - bandY) / bandH;

        // Wider fade ramp so the top/bottom of each band is never hard-clipped
        float edgeFade = smoothstep(0.0, 0.12, band)
                       * smoothstep(1.0, 0.88, band);
        if (edgeFade <= 0.001) continue;

        // Scan-line: very subtle — just enough to hint at a pixel-grid feel
        float scanRow  = fract(band * 7.0);
        float scanLine = 1.0 - 0.10 * (1.0 - smoothstep(0.88, 1.0, scanRow));

        // Alternating scroll direction, each row slightly different speed
        float dir         = (row % 2 == 0) ? 1.0 : -1.0;
        float scrollSpeed = 0.8 * (tempo / 120.0) * (1.0 + 0.10 * float(row)) * dir;
        float scrollPhase = time * scrollSpeed + float(row) * 0.6;
        float scrollX     = fract(scrollPhase / float(MESSAGE_LEN)) * float(MESSAGE_LEN);

        int   col     = 0;
        vec2  glyphUV = vec2(uv.x, 1.0 - band);
        float charVal = marqueeChar(glyphUV, scrollX, col);
        if (charVal <= 0.0001) continue;

        // ── Per-character pulse ──────────────────────────────────────────────
        float pulse = 0.75 + 0.25 * sin(time * 2.2
                              + float(col) * 0.45
                              + bass  * 1.5
                              + float(row) * 0.7);

        // ── Letter brightness — boosted base, no longer drifts too dark ──────
        // Using 1.15 minimum so pixels are always clearly white/bright even
        // at low energy.  energy and high add shimmer on top.
        float brightness = 1.15 + energy * 0.35 + high * 0.25;
        vec3  letterColor = vec3(brightness) * pulse;

        // ── Bloom — tighter radius, lower amplitude → less smearing ──────────
        // glowAmt drives a small halo; keeping it at ≤0.25 prevents bloom from
        // washing out the letterform edges.
        float glowAmt = charVal * clamp(0.15 + bass * 0.25 + high * 0.15, 0.0, 0.25)
                      * scanLine * edgeFade;
        vec3  glowCol = vec3(1.0) * (0.40 + high * 0.1);

        // ── Composite this row into accumulated color ────────────────────────
        float maskVal = charVal * edgeFade * scanLine;
        color  = mix(color, letterColor, maskVal);
        color += glowAmt * glowCol * 0.4;

        alpha = clamp(alpha + maskVal * (0.35 + energy * 0.15), 0.0, 1.0);
    }

    return vec4(color, alpha);
}