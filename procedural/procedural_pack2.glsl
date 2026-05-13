// @EFFECT name="Fractal Object" index=7 desc="Rotating fractal object" author="System"
// @EFFECT name="Pulsar Tunnel" index=8 desc="Pulsar spiral tunnel" author="System"
// @EFFECT name="Aurora Bloom" index=9 desc="Aurora curtain effect" author="System"
// @EFFECT name="Ribbon Scanlines" index=10 desc="Ribbon scanline distortion" author="System"
// @EFFECT name="Kaleidoscope Fractal" index=12 desc="Kaleidoscope fractal pattern" author="System"

vec4 renderFractalObject(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    vec2 p = rotate(st, time * 0.2);
    float r = length(p);
    float angle = atan(p.y, p.x);
    float warp = sin(angle * 3.0 + time * (1.0 + tempo * 0.2));
    float layers = fbm(p * 5.0 + warp);
    vec3 color = mix(uPrimaryColor, uSecondaryColor, layers);
    color += vec3(0.20, 0.10, 0.35) * warp * high;
    color = clamp(color, 0.0, 1.0);
    float alpha = clamp(0.4 + (1.0 - smoothstep(0.0, 1.2, r)) * (0.5 + energy * 0.3) + layers * 0.3, 0.0, 1.0);
    return vec4(color, alpha);
}

vec4 renderPulsarTunnel(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    float r = length(st);
    float angle = atan(st.y, st.x);
    float spiral = sin(r * 12.0 - time * (2.0 + tempo) + angle * 4.0);
    vec3 color = mix(uPrimaryColor, uSecondaryColor, 0.4 + 0.3 * bass);
    color *= (0.6 + spiral * 0.4);
    color += vec3(0.20, 0.10, 0.30) * (1.0 - r) * (0.6 + energy * 0.4);
    color = clamp(color, 0.0, 1.0);
    float alpha = clamp((1.0 - r) * (0.8 + energy * 0.4) + spiral * 0.1, 0.0, 1.0);
    return vec4(color, alpha);
}

vec4 renderAuroraBloom(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    vec2 p = st;
    float band = sin((p.x + time * (0.6 + tempo * 0.2)) * 6.0);
    float curtain = smoothstep(-0.4 - high * 0.2, 0.4 + high * 0.2, p.y + 0.2 * band);
    vec3 base = mix(uPrimaryColor, vec3(0.10, 0.20, 0.35), 0.5);
    vec3 glow = mix(uSecondaryColor, vec3(0.95, 0.90, 1.0), 0.5 + high * 0.2);
    vec3 color = mix(base, glow, curtain);
    color = clamp(color, 0.0, 1.0);
    float alpha = clamp(curtain * (0.5 + energy * 0.4), 0.0, 1.0);
    return vec4(color, alpha);
}

vec4 renderRibbonScanlines(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    vec2 p = rotate(st, 0.3);
    float stripes = sin(p.y * (80.0 + high * 40.0) + time * (4.0 + tempo)) * 0.5 + 0.5;
    float ribbons = sin(p.x * 6.0 + time * (1.2 + tempo * 0.4)) * 0.5 + 0.5;
    vec3 colorA = mix(uPrimaryColor, uSecondaryColor, stripes);
    vec3 colorB = mix(uSecondaryColor, uPrimaryColor, ribbons);
    vec3 color = mix(colorA, colorB, 0.5);
    color = clamp(color, 0.0, 1.0);
    float alpha = clamp(0.30 + stripes * 0.4 + ribbons * 0.3, 0.0, 1.0);
    return vec4(color, alpha);
}

vec3 anaglyphHsv2Rgb(float h) {
    vec3 k = fract(vec3(h, h + 0.333, h + 0.667)) * 6.0 - 3.0;
    return clamp(abs(k) - 1.0, 0.0, 1.0);
}

vec4 renderKaleidoscopeFractal(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {

    float r = length(st);
    float angle = atan(st.y, st.x);

    angle += time * (0.15 + tempo * 0.08);

    float sectors = 6.0 + floor(mid * 8.0);
    float sectorAngle = 2.0 * PI / max(sectors, 1.0);
    angle = mod(angle, sectorAngle);
    angle = abs(angle - sectorAngle * 0.5);

    vec2 dir = vec2(cos(angle), sin(angle));
    vec2 p = dir * r;

    vec2 warp = vec2(
        fbm(p * (3.2 + high * 2.4) + time * 0.35),
        fbm(p * (2.6 + mid  * 1.8) - time * 0.28)
    );
    p += warp * (0.5 + high * 0.6 + energy * 0.35);

    float n1 = fbm(p * (2.4 + energy * 1.3) + time * 0.25);
    float n2 = fbm(p * 4.8 - time * 0.4 + vec2(bass * 0.5));
    float n3 = fbm(p * 9.0 + time * 0.6 + vec2(mid  * 0.3));

    float hShift = time * 0.12 + bass * 0.4;
    float h1 = fract(n1 * 1.5 + hShift);
    float h2 = fract(n2 * 1.2 + hShift + 0.33);
    float h3 = fract(n3 * 0.9 + hShift + 0.66);

    vec3 c1 = anaglyphHsv2Rgb(h1);
    vec3 c2 = anaglyphHsv2Rgb(h2);
    vec3 c3 = anaglyphHsv2Rgb(h3);

    float w1 = n1;
    float w2 = n2 * (0.6 + mid  * 0.5);
    float w3 = n3 * (0.4 + high * 0.6);
    float wTotal = w1 + w2 + w3 + 0.001;

    vec3 color = (c1 * w1 + c2 * w2 + c3 * w3) / wTotal;

    // Darken the overall color palette - make it less colorful
    color *= 0.35; // Reduce brightness significantly
    color = pow(color, vec3(1.4)); // Increase contrast, darkens mid-tones

    vec3 userTint = mix(uPrimaryColor, uSecondaryColor, uColorBlend) * 0.12;
    color = mix(color, color + userTint, 0.25);

    float radialMask = pow(1.0 - smoothstep(0.0, 1.0, r * 0.85), 1.8);
    float noiseMask  = smoothstep(0.02, 0.35, n1); // Sharper threshold for more black areas
    float mask       = radialMask * noiseMask;

    // Create dark hollows where there's no pattern
    float darkness = 1.0 - smoothstep(0.0, 0.5, n1 * n2);
    color *= mask * (0.4 + 0.6 * darkness); // More darkness in empty areas

    float edgeDist = abs(angle) / (sectorAngle * 0.5);
    float edgeLine = exp(-80.0 * pow(1.0 - edgeDist, 2.0)) * (0.25 + high * 0.3);
    color += c1 * edgeLine * mask * 0.6; // Dimmer edges

    float core = exp(-18.0 * r * r) * (0.25 + bass * 0.4);
    color += mix(c2, vec3(0.7), 0.5) * core * 0.5; // Less bright core

    float spark = step(0.92, n3) * step(0.9, n2) * high * 0.5; // Rare, dim sparks
    color += c3 * spark * mask * 0.7;

    color = clamp(color, 0.0, 1.0);

    // Lower alpha for more transparency in dark areas
    float alpha = clamp(mask * (0.35 + n1 * 0.25) + core * 0.5 + spark * 0.3, 0.0, 0.85);

    return vec4(color, alpha);
}
