// @EFFECT name="Eiyeron Deform" index=46 desc="DEMOS AND COLORS plane deformation by Eiyeron" author="Eiyeron" zoom=1.2

/**
DEMOS AND COLORS
By @Eiyeron
    Based on Illogical from Matrefeytontias, plane deformations on TI-83/84
    And some tunnel effects.

Use : Comment/Uncomment the defines as you wants, they'll enable/disable various effects in the shader.
**/

const float EIY_SPEED = 0.25;
const float EIY_PI = 3.141592653589793;
const float EIY_TAU = EIY_PI * 2.0;

vec3 eiyeronRainbow(vec2 p) {
    vec2 shifted = fract(abs(p) * 0.16666);
    shifted = min(shifted, 1.0 - shifted);
    float r = smoothstep(0.166666, 0.333333, shifted.x + 0.05);
    float g = smoothstep(0.166666, 0.333333, shifted.x + 0.25);
    float b = smoothstep(0.166666, 0.333333, shifted.x + 0.45);
    return clamp(vec3(r, g, b), 0.0, 1.0);
}

float eiyeronChecker(vec2 p, float t) {
    float xpos = floor(20.0 * p.x);
    float ypos = floor(10.0 * p.y);
    float col = mod(xpos, 2.0);
    float phase = xpos * ypos + t * 5.0;
    return (mod(ypos, 2.0) > 0.0) ? cos(phase) : sin(phase);
}

vec2 eiyeronNormalize(vec2 fragCoord) {
    vec2 position = 2.0 * fragCoord / uResolution - 1.0;
    position.x *= uResolution.x / uResolution.y;
    return position;
}

vec3 eiyeronCore(vec2 fragCoord, float t, float energy, float bass, float mid, float high) {
    vec2 position = eiyeronNormalize(fragCoord);

    float r = length(position);
    float angle = atan(position.y, position.x);
    float factor = sin(t) * 0.5 + 0.5;

    float uPlane = position.x / max(abs(position.y), 0.001);
    float vPlane = 1.0 / max(abs(position.y), 0.001);
    float uTunnel = angle;
    float vTunnel = 1.0 / max(r, 0.001);

    float u = mix(uPlane, uTunnel, 1.0 - factor);
    float v = mix(vPlane, vTunnel, 1.0 - factor);

    vec2 p = vec2(u, v);
    p += vec2(EIY_SPEED * cos(t), EIY_SPEED * t);
    p += vec2(0.08 * bass, 0.05 * high);

    vec3 baseColor = vec3(cos(p.x), sin(p.y), 1.0 - 0.5 * cos(t));
    vec3 rainbow = eiyeronRainbow(p);
    float checker = eiyeronChecker(p, t);

    vec3 color = baseColor * rainbow * checker;

    float motif = 0.0;
    for (float i = 0.0; i < 5.0; i += 1.0) {
        float ang = i * (EIY_TAU / 5.0) * 61.95;
        motif += cos(EIY_TAU * (p.y * cos(ang) + p.x * sin(ang) + sin(t * 0.004) * 100.0));
    }
    color *= vec3(motif / 3.0);

    color *= clamp(1.0 / (abs(v) + 0.1), 0.0, 4.0);
    color *= clamp(2.0 - r, 0.0, 2.0);

    vec3 palette = mix(uPrimaryColor, uSecondaryColor, clamp(uColorBlend + high * 0.2, 0.0, 1.0));
    color *= palette;

    vec3 bandGlow = vec3(bass * 0.2, mid * 0.15, high * 0.25);
    color = color * (0.8 + energy * 0.7 + high * 0.4) + bandGlow;

    return color;
}

vec4 renderEiyeronDeform(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    vec2 fragCoord = (st * 0.5 + 0.5) * uResolution;
    float t = time * (1.0 + tempo * 0.05) + bass * 0.2;

    vec3 color = eiyeronCore(fragCoord, t, energy, bass, mid, high);
    color = clamp(color, 0.0, 1.0);

    // Calculate brightness for mixing with black background
    float brightness = dot(color, vec3(0.299, 0.587, 0.114));

    // Black background - prioritize dark areas
    vec3 bgColor = vec3(0.0);

    // Mix with black based on brightness - only show colors where brightness is high
    float mixFactor = smoothstep(0.15, 0.5, brightness);
    color = mix(bgColor, color, mixFactor);

    // Further darken to ensure black dominance
    color *= 0.7;

    return vec4(color, 1.0);
}
