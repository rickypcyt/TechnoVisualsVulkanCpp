// @EFFECT name="Celestial Ribbon Bloom" index=51 desc="Layered spherical ribbons with volumetric glow" author="tigrou"

vec4 tigrouPortalLayer(vec2 px, float depth, float timeSign) {
    float l = PI;
    float k = timeSign * sign(depth);
    float x = px.x * 320.0 * 0.0065 * depth;
    float y = px.y * 240.0 * 0.0060 * depth;
    float c = sqrt(x * x + y * y);
    if (c > 1.0) {
        return vec4(0.0);
    }

    float u = -0.4 * sign(depth) + sin(k * 0.5);
    float v = sqrt(max(1.0 - x * x - y * y, 0.0));
    float q = y * sin(u) - v * cos(u);
    y = y * cos(u) + v * sin(u);
    v = acos(clamp(y, -1.0, 1.0));
    float sv = sin(v);
    float invSv = (abs(sv) < 1e-4) ? 0.0 : (x / sv);
    invSv = clamp(invSv, -1.0, 1.0);
    u = acos(invSv) / (2.0 * l) * 120.0 * sign(q) - k;
    v = v * 60.0 / l;
    q = cos(floor(v / l));
    float wave = float(int((u + l / 2.0) / l));
    c = pow(abs(cos(u) * sin(v)), 0.2) * 0.1 /
        (q + sin(wave + k * 0.6 + cos(q * 25.0))) * pow(1.0 - c, 0.9);

    vec4 res;
    if (c < 0.0) {
        res = vec4(-c / 2.0 * abs(cos(k * 0.1)), 0.0, 0.0, 1.0);
    } else {
        res = vec4(c, c * 2.0, c * 2.0, 1.0);
    }
    return res;
}

vec4 renderCelestialRibbonBloom(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    vec2 fragCoord = (st * 0.5 + 0.5) * uResolution;
    vec2 p = -1.0 + 2.0 * fragCoord / uResolution;

    vec4 accum = vec4(0.0);
    for (int i = 8; i > 0; --i) {
        float depth = 1.0 - float(i) / 80.0;
        accum += tigrouPortalLayer(p, depth, time) * (0.008 - float(i) * 0.00005);
    }

    vec4 detail = tigrouPortalLayer(p, 1.0, time);
    vec4 layer = detail + sqrt(max(accum, 0.0));

    // Soft back layer hint from original shader
    if (detail.a == 77.0) {
        layer += tigrouPortalLayer(p, -0.2, time) * 0.02;
    }

    vec3 palette = mix(uPrimaryColor, uSecondaryColor,
                       clamp(0.45 + 0.35 * sin(mid * 2.0 + time * 0.5), 0.0, 1.0));
    vec3 ribbon = layer.rgb * palette;
    ribbon *= 0.6 + energy * 0.5;
    ribbon += uSecondaryColor * (bass * 0.15 + high * 0.1);
    ribbon = clamp(ribbon, 0.0, 1.0);

    float glowMask = clamp(length(layer.rgb) * 0.9, 0.0, 1.0);
    vec3 background = vec3(0.015, 0.02, 0.04);
    vec3 color = mix(background, ribbon, glowMask);

    return vec4(color, 1.0);
}
