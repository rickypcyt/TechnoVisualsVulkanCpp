// @EFFECT name="Liquid Kali Fractal" index=103 desc="Newton fractal z3-1 with infinite zoom, red lines on black" author="System"

#ifndef COMPLEX_OPS_DEFINED
#define COMPLEX_OPS_DEFINED

// Complex multiplication
vec2 cmul(vec2 a, vec2 b) {
    return vec2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
}

// Complex division
vec2 cdiv(vec2 a, vec2 b) {
    return vec2(a.x * b.x + a.y * b.y, -a.x * b.y + a.y * b.x) / (b.x * b.x + b.y * b.y);
}

#endif

vec4 renderLiquidKaliFractal(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    float aspect = uResolution.x / max(uResolution.y, 1.0);
    vec2 coord = (2.0 * st - 1.0) * vec2(aspect, 1.0);

    // === Infinite seamless zoom — power-of-2 cycle (independent of audio) ===
    float zoomSpeed = 0.5; // Increased for visible movement
    float zoomCycle = mod(time * zoomSpeed, 1.0);
    float zoom = exp(zoomCycle * 2.7726); // ln(16) for seamless 16x
    zoom *= 0.5 + sin(time * 0.3) * 0.2; // Faster breathing

    vec2 z = coord * zoom; // Zoom in

    // === Continuous rotation (independent of audio) ===
    float rotAngle = time * 0.3; // Much faster rotation
    float ca = cos(rotAngle);
    float sa = sin(rotAngle);
    z = mat2(ca, -sa, sa, ca) * z;

    // === Continuous pan (independent of audio) ===
    z += vec2(
        sin(time * 0.2) * 0.5, // Faster pan
        cos(time * 0.25) * 0.5
    );

    // === Domain deformation (independent of audio) ===
    float defTime = time * 0.5; // Faster deformation
    float warp1 = sin(z.x * 3.0 + defTime) * cos(z.y * 2.0 - defTime * 0.7);
    float warp2 = cos(z.x * 1.5 - defTime * 0.5) * sin(z.y * 3.5 + defTime);
    z.x += warp1 * 0.3;
    z.y += warp2 * 0.25;

    // === Drifting constant c (independent of audio) ===
    vec2 c = vec2(1.0, 0.0) + vec2(
        sin(time * 0.2) * 0.5, // Faster drift
        cos(time * 0.25) * 0.5
    );

    // === Newton iteration: z = z - f(z)/f'(z), f(z) = z^3 - c ===
    vec2 zPrev = z;
    float iterCount = 0.0;
    float maxIter = 60.0; // Fixed, independent of audio
    float threshold = 0.0001;

    // Track orbit for line effects
    float minDist = 1e10;
    float totalPath = 0.0;
    vec2 lastZ = z;

    for (float i = 0.0; i < 100.0; i += 1.0) {
        if (i >= maxIter) break;

        // f(z) = z^3 - c
        vec2 z2 = cmul(z, z);
        vec2 z3 = cmul(z2, z);
        vec2 fz = z3 - c;

        // f'(z) = 3*z^2
        vec2 dfz = cmul(vec2(3.0, 0.0), z2);

        // Newton step
        z -= cdiv(fz, dfz);

        // Track path length for line effects
        totalPath += length(z - lastZ);
        lastZ = z;

        // Track closest approach to origin
        minDist = min(minDist, length(z));

        // Convergence check
        if (length(z - zPrev) < threshold) {
            iterCount = i;
            break;
        }
        zPrev = z;
        iterCount = i;
    }

    // === Root classification: which root did we converge to? ===
    // For z^3 - 1, roots are at angles 0, 120, 240 degrees
    float theta = atan(z.y, z.x);
    float rotation = mod(theta / 6.2832 + 1.0, 1.0); // [0,1]

    // === Line effect: use path length and iteration count ===
    // Lines emerge from the boundary between root basins
    float lineIntensity = 1.0 / log(iterCount + 1.5);
    float pathLines = smoothstep(0.0, 3.0, totalPath) * (1.0 - smoothstep(3.0, 15.0, totalPath));

    // Edge detection between basins: sample neighbors
    float edgeSharp = pow(lineIntensity, 0.5);

    // === Red and black palette — much more black, very little red ===
    vec3 col = vec3(0.0); // Pure black base

    // Deep red lines following the fractal boundaries — much darker
    float lineMask = pathLines * edgeSharp;
    col += vec3(0.4, 0.0, 0.0) * lineMask * (0.5 + bass * 0.2);

    // Brighter red at basin centers (fast convergence) — much darker
    float fastConv = exp(-iterCount * 0.15);
    col += vec3(0.3, 0.0, 0.0) * fastConv * 0.15;

    // Orbit trap glow: thin red lines where z passes close to origin — much darker
    float trapGlow = exp(-minDist * 10.0);
    col += vec3(0.4, 0.0, 0.0) * trapGlow * (0.25 + energy * 0.1);

    // === Deformation lines: contour-like bands that move with time — much darker ===
    float bands = abs(fract(rotation * 12.0 + time * 0.3) - 0.5) * 2.0;
    bands = 1.0 - smoothstep(0.85, 1.0, bands);
    col += vec3(0.15, 0.0, 0.0) * bands * lineIntensity * 0.15;

    // === Extra deformation: angular lines that rotate — much darker ===
    float angularLines = abs(sin(theta * 6.0 + time * 0.6));
    angularLines = 1.0 - smoothstep(0.90, 0.97, angularLines);
    col += vec3(0.2, 0.0, 0.0) * angularLines * (0.1 + mid * 0.1);

    // === Radial lines from center that rotate — much darker ===
    float radial = atan(coord.y, coord.x);
    float radialLines = abs(sin(radial * 8.0 + time * 0.4));
    radialLines = 1.0 - smoothstep(0.92, 0.98, radialLines);
    col += vec3(0.1, 0.0, 0.0) * radialLines * 0.1;

    // === Moving scan bands for extra animation — much darker ===
    float scanBand = sin(st.y * 20.0 + time * 4.0) * sin(st.x * 15.0 - time * 3.0);
    scanBand = smoothstep(0.7, 0.95, scanBand);
    col += vec3(0.15, 0.0, 0.0) * scanBand * 0.04;

    // === Audio reactivity: only enhances, never dims ===
    col *= 1.0 + bass * 0.2 + energy * 0.15;

    // === Vignette for cave depth ===
    float depth = 1.0 - smoothstep(0.5, 1.2, length(st - 0.5) * 2.0) * 0.4;
    col *= depth;

    // === Subtle scanlines ===
    float scan = sin(st.y * uResolution.y * 0.5) * 0.01;
    col -= scan;

    col = clamp(col, 0.0, 1.0);
    float alpha = clamp(0.9 + energy * 0.1, 0.0, 1.0);

    return vec4(col, alpha);
}
