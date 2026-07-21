// @EFFECT name="Melting Red Fractal" index=102 desc="Newton fractal with infinite zoom" author="System"

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

vec4 renderMeltingRedFractal(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
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

    // === Newton iteration: z = z - f(z)/f'(z), f(z) = z^3 - c ===
    // Drifting constant c (independent of audio)
    vec2 c = vec2(1.0, 0.0) + vec2(
        sin(time * 0.2) * 0.5, // Faster drift
        cos(time * 0.25) * 0.5
    );

    vec2 zPrev = z;
    float iterCount = 0.0;
    float maxIter = 60.0; // Fixed, independent of audio

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

        // Track path length
        totalPath += length(z - lastZ);
        lastZ = z;

        // Track closest approach to origin
        minDist = min(minDist, length(z));

        // Convergence check
        if (length(z - zPrev) < 0.001) {
            iterCount = i;
            break;
        }
        zPrev = z;
        iterCount = i;
    }

    // === Red and black palette based on iteration ===
    vec3 col = vec3(0.0); // Pure black base

    // Red intensity based on iteration count
    float iterNorm = iterCount / maxIter;
    float redIntensity = 1.0 - iterNorm;
    redIntensity = pow(redIntensity, 1.5);

    // Deep red to bright red gradient
    vec3 deepRed = vec3(0.15, 0.0, 0.0);
    vec3 vividRed = vec3(0.6, 0.0, 0.0);
    vec3 brightRed = vec3(0.9, 0.0, 0.0);

    col = mix(deepRed, vividRed, redIntensity);
    col = mix(col, brightRed, pow(redIntensity, 3.0));

    // === Line effects from orbit ===
    float pathLines = smoothstep(0.0, 2.0, totalPath) * (1.0 - smoothstep(2.0, 8.0, totalPath));
    col += vividRed * pathLines * 0.4;

    // Orbit trap glow
    float trapGlow = exp(-minDist * 5.0);
    col += brightRed * trapGlow * 0.3;

    // === Domain deformation lines ===
    float theta = atan(z.y, z.x);
    float angleLines = abs(sin(theta * 8.0 + time * 0.2));
    angleLines = 1.0 - smoothstep(0.9, 0.97, angleLines);
    col += vividRed * angleLines * 0.3;

    // === Radial lines from center ===
    float radial = atan(coord.y, coord.x);
    float radialLines = abs(sin(radial * 6.0 + time * 0.15));
    radialLines = 1.0 - smoothstep(0.92, 0.98, radialLines);
    col += vec3(0.4, 0.0, 0.0) * radialLines * 0.2;

    // === Moving bands for extra animation ===
    float bands = sin(z.x * 10.0 + time * 0.5) * sin(z.y * 10.0 - time * 0.4);
    bands = smoothstep(0.7, 0.95, bands);
    col += vividRed * bands * 0.15;

    // === Vignette for depth ===
    float depth = 1.0 - smoothstep(0.5, 1.2, length(st - 0.5) * 2.0) * 0.3;
    col *= depth;

    col = clamp(col, 0.0, 1.0);
    float alpha = clamp(0.9 + energy * 0.1, 0.0, 1.0);
    return vec4(col, alpha);
}
