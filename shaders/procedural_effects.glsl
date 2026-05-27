// Lightweight procedural effects for Layer 0/1 pipeline
// Each effect uses: uTime, uTempo, uEnergy, uBass, uMid, uHigh, uPrimaryColor, uSecondaryColor

// ── helpers ──────────────────────────────────────────────────────────────────

float peHash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float peNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = peHash(i);
    float b = peHash(i + vec2(1.0, 0.0));
    float c = peHash(i + vec2(0.0, 1.0));
    float d = peHash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float peFbm(vec2 p, int octaves) {
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < octaves; ++i) {
        v += a * peNoise(p);
        p *= 2.0;
        a *= 0.5;
    }
    return v;
}

// ── Mode 2: Plasma Wave ──────────────────────────────────────────────────────

vec3 renderPlasmaWave(vec2 st) {
    vec2 uv = (st - 0.5) * vec2(ubo.resolution.x / max(ubo.resolution.y, 1.0), 1.0);
    float t = uTime * (0.3 + uTempo * 0.5);
    float audioBoost = uEnergy * 2.0 + uBass * 3.0;

    float v1 = sin(uv.x * 3.0 + t + audioBoost * 0.5);
    float v2 = sin(uv.y * 4.0 - t * 0.7 + uMid * 2.0);
    float v3 = sin((uv.x + uv.y) * 2.5 + t * 1.3 + uHigh * 3.0);
    float v4 = sin(length(uv) * 5.0 - t * 2.0 + uBass * 4.0);

    float pattern = (v1 + v2 + v3 + v4) * 0.25;
    pattern = pattern * 0.5 + 0.5;

    // Nearly black background (never depends on palette)
    vec3 color = vec3(0.01, 0.01, 0.02);

    // Bright plasma blended on top (gentle mix)
    float hueShift = fract(pattern + t * 0.05 + uEnergy * 0.3);
    vec3 plasma = mix(uPrimaryColor, uSecondaryColor, hueShift);
    plasma *= 0.25 + pattern * 0.35; // hard cap: max ~0.6 per channel
    color = mix(color, plasma, pattern * (0.25 + uEnergy * 0.25));

    return clamp(color, 0.0, 0.85);
}

// ── Mode 3: Radial Burst ─────────────────────────────────────────────────────

vec3 renderRadialBurst(vec2 st) {
    vec2 uv = (st - 0.5) * vec2(ubo.resolution.x / max(ubo.resolution.y, 1.0), 1.0);
    float t = uTime * (0.2 + uTempo * 0.8);
    float dist = length(uv);
    float angle = atan(uv.y, uv.x);

    // Rotating rays that never stop
    float rays = sin(angle * 8.0 + t * 2.0 + uEnergy * 5.0);
    float rings = sin(dist * 15.0 - t * 3.0 - uBass * 6.0);

    float pattern = rays * rings * 0.5 + 0.5;
    pattern += peNoise(uv * 5.0 + t) * 0.2;
    pattern = clamp(pattern, 0.0, 1.0);

    // Dark background base
    vec3 color = vec3(0.02, 0.02, 0.03);

    // Color cycling with time
    float colorCycle = fract(pattern * 0.5 + t * 0.1 + uMid * 0.3);
    vec3 burst = mix(uPrimaryColor, uSecondaryColor, colorCycle);
    burst *= 0.25 + pattern * 0.5; // hard cap
    color = mix(color, burst, pattern * (0.4 + uEnergy * 0.25));

    // Center glow that breathes continuously
    float glow = exp(-dist * (4.0 + uBass * 4.0)) * (0.5 + sin(t * 2.0) * 0.3);
    color += mix(uPrimaryColor, uSecondaryColor, sin(t)) * glow * 0.25;

    return clamp(color, 0.0, 0.85);
}

// ── Mode 4: Grid Pulse ───────────────────────────────────────────────────────

vec3 renderGridPulse(vec2 st) {
    vec2 uv = (st - 0.5) * vec2(ubo.resolution.x / max(ubo.resolution.y, 1.0), 1.0);
    float t = uTime * (0.25 + uTempo * 0.6);

    // Grid lines that warp with bass
    float gridX = abs(fract(uv.x * 6.0 + uBass * sin(uv.y * 3.0 + t)) - 0.5);
    float gridY = abs(fract(uv.y * 6.0 + uBass * cos(uv.x * 3.0 + t * 0.7)) - 0.5);

    float lineX = smoothstep(0.02 + uMid * 0.03, 0.0, gridX);
    float lineY = smoothstep(0.02 + uMid * 0.03, 0.0, gridY);

    float pattern = max(lineX, lineY);

    // Intersection glow that pulses with time
    float intersections = lineX * lineY;
    intersections *= 1.0 + sin(t * 4.0 + uEnergy * 6.0) * 0.5;

    // Dark background base
    vec3 color = vec3(0.02, 0.02, 0.04);

    // Grid lines blended on top
    float colorMix = fract(t * 0.08 + uEnergy * 0.2 + intersections * 0.3);
    vec3 lineColor = mix(uPrimaryColor, uSecondaryColor, colorMix);
    lineColor *= 0.3 + pattern * 0.5; // hard cap
    color = mix(color, lineColor, pattern * (0.5 + uEnergy * 0.2));

    // Intersection glow
    color += mix(uPrimaryColor, uSecondaryColor, sin(t * 3.0)) * intersections * (0.15 + uHigh * 0.15);

    return clamp(color, 0.0, 0.85);
}

// ── Mode 5: Noise Flow ───────────────────────────────────────────────────────

vec3 renderNoiseFlow(vec2 st) {
    vec2 uv = (st - 0.5) * vec2(ubo.resolution.x / max(ubo.resolution.y, 1.0), 1.0);
    float t = uTime * (0.15 + uTempo * 0.4);

    // Flowing fbm that never freezes
    vec2 flow = vec2(t, t * 0.7);
    float n1 = peFbm(uv * 2.0 + flow + uBass, 4);
    float n2 = peFbm(uv * 3.0 - flow * 0.5 + uMid, 4);
    float n3 = peFbm(uv * 1.5 + vec2(-t, t * 0.3) + uHigh, 4);

    float pattern = n1 * 0.5 + n2 * 0.3 + n3 * 0.2;
    pattern = fract(pattern + t * 0.05 + uEnergy * 0.2);

    // Dark background base
    vec3 color = mix(uPrimaryColor * 0.05, uSecondaryColor * 0.08, sin(t * 0.07) * 0.5 + 0.5);

    // Flowing noise blended on top
    vec3 flowColor = mix(uPrimaryColor, uSecondaryColor, pattern);
    flowColor *= 0.25 + n1 * 0.4; // hard cap
    color = mix(color, flowColor, clamp(n1 * 0.35 + uEnergy * 0.25, 0.0, 0.7));

    // Moving highlights
    float highlight = peNoise(uv * 8.0 + t * 2.0) * uHigh;
    color += mix(uPrimaryColor, uSecondaryColor, sin(t * 3.0)) * highlight * 0.2;

    return clamp(color, 0.0, 0.85);
}

// ── Mode 6: Cellular Voronoi ─────────────────────────────────────────────────

vec3 renderCellularVoronoi(vec2 st) {
    vec2 uv = (st - 0.5) * vec2(ubo.resolution.x / max(ubo.resolution.y, 1.0), 1.0);
    float t = uTime * (0.2 + uTempo * 0.5);

    float minDist = 10.0;
    float secondDist = 10.0;
    vec2 nearestCell = vec2(0.0);

    vec2 cellId = floor(uv * 4.0);
    vec2 cellFract = fract(uv * 4.0);

    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 neighbor = vec2(float(x), float(y));
            vec2 cell = cellId + neighbor;
            // Each cell has a moving point inside
            float phase = peHash(cell) * 6.283;
            vec2 point = neighbor + vec2(
                0.5 + sin(t + phase) * (0.35 + uEnergy * 0.15),
                0.5 + cos(t * 0.7 + phase * 1.3) * (0.35 + uBass * 0.15)
            );
            float d = length(cellFract - point);
            if (d < minDist) {
                secondDist = minDist;
                minDist = d;
                nearestCell = cell;
            } else if (d < secondDist) {
                secondDist = d;
            }
        }
    }

    // Edge pattern (difference between nearest and second-nearest)
    float edge = secondDist - minDist;
    float pattern = smoothstep(0.02 + uMid * 0.03, 0.15 + uMid * 0.05, edge);

    // Dark background base
    vec3 color = mix(uPrimaryColor * 0.04, uSecondaryColor * 0.06, sin(t * 0.05) * 0.5 + 0.5);

    // Cell color varies with cell ID + time
    float cellHue = fract(peHash(nearestCell) + t * 0.03 + uEnergy * 0.3);
    vec3 cellColor = mix(uPrimaryColor, uSecondaryColor, cellHue);
    cellColor *= 0.25 + pattern * 0.35 + (1.0 - pattern) * 0.25; // hard cap ~0.6
    color = mix(color, cellColor, 0.4 + pattern * 0.25);

    // Pulsing cell borders
    float border = 1.0 - smoothstep(0.0, 0.08 + uHigh * 0.05, edge);
    color += mix(uPrimaryColor, uSecondaryColor, sin(t * 2.0)) * border * (0.12 + uEnergy * 0.15);

    return clamp(color, 0.0, 0.85);
}

// ── Mode 7: Mandala Spin ───────────────────────────────────────────────────

vec3 renderMandalaSpin(vec2 st) {
    vec2 uv = (st - 0.5) * vec2(ubo.resolution.x / max(ubo.resolution.y, 1.0), 1.0);
    float t = uTime * (0.3 + uTempo * 0.6);
    float dist = length(uv);
    float angle = atan(uv.y, uv.x);

    int petals = 6 + int(uEnergy * 8.0); // 6 to 14 petals
    float sector = float(petals) * angle + t * 2.0 + uBass * 4.0;
    float petalPattern = abs(sin(sector * 0.5));

    // Rings that expand/contract with bass
    float ringCount = 8.0 + uMid * 6.0;
    float ring = sin(dist * ringCount * 6.283 - t * 3.0 - uBass * 5.0);
    ring = ring * 0.5 + 0.5;

    float pattern = petalPattern * ring;

    // Dark background base
    vec3 color = vec3(0.02, 0.015, 0.025);

    // Color rotates continuously with time and energy
    float hue = fract(angle / 6.283 + t * 0.05 + uEnergy * 0.25 + dist * 0.3);
    vec3 petalColor = mix(uPrimaryColor, uSecondaryColor, hue);
    petalColor *= 0.18 + pattern * 0.4; // hard cap
    color = mix(color, petalColor, pattern * (0.4 + uEnergy * 0.2));

    // Center vortex
    float vortex = exp(-dist * (3.0 + uBass * 3.0));
    color += mix(uPrimaryColor, uSecondaryColor, sin(t * 4.0)) * vortex * (0.12 + uEnergy * 0.15);

    // Outer rim glow
    float rim = smoothstep(0.4, 0.5, dist) * (1.0 - smoothstep(0.5, 0.6, dist));
    color += uSecondaryColor * rim * uHigh * sin(t * 6.0 + angle * 3.0) * 0.25;

    return clamp(color, 0.0, 0.85);
}

// ── Mode 8: Terrain Scan ─────────────────────────────────────────────────────

vec3 renderTerrainScan(vec2 st) {
    vec2 uv = (st - 0.5) * vec2(ubo.resolution.x / max(ubo.resolution.y, 1.0), 1.0);
    float t = uTime * (0.2 + uTempo * 0.4);

    // Generate terrain height from layered noise
    float height = peFbm(uv * 3.0 + vec2(t * 0.3, 0.0), 5);
    height += peFbm(uv * 6.0 - vec2(t * 0.15, t * 0.1), 4) * 0.5;
    height = height * 0.5 + 0.5;

    // Scan line that moves vertically with time, speed reacts to tempo
    float scanSpeed = 0.5 + uTempo * 0.5;
    float scanY = fract(t * scanSpeed * 0.15);
    float scanLine = smoothstep(0.015, 0.0, abs(uv.y - (scanY - 0.5) * 1.2));

    // Contour lines at terrain height levels
    float contour = abs(fract(height * 10.0 + uBass * 2.0) - 0.5);
    float contourLine = smoothstep(0.02 + uMid * 0.02, 0.0, contour);

    // Dark background base
    vec3 color = mix(uPrimaryColor * 0.03, uSecondaryColor * 0.05, sin(t * 0.04) * 0.5 + 0.5);

    // Height-based color
    float colorMix = fract(height + t * 0.03 + uEnergy * 0.2);
    vec3 terrain = mix(uPrimaryColor * 0.35, uSecondaryColor * 0.7, colorMix);
    terrain *= 0.35 + height * 0.4; // hard cap
    color = mix(color, terrain, 0.4 + height * 0.25);

    // Contour lines
    color += contourLine * mix(uPrimaryColor, uSecondaryColor, sin(t * 2.0)) * (0.15 + uEnergy * 0.15);

    // Scan line reveals brighter colors
    vec3 scanColor = mix(uPrimaryColor, uSecondaryColor, peNoise(uv * 4.0 + t)) * 0.8;
    color = mix(color, scanColor, scanLine * (0.3 + uEnergy * 0.2));

    // Topographic shadow
    float shadow = smoothstep(0.0, 0.5, height) * (1.0 - smoothstep(0.5, 1.0, height));
    color *= 0.75 + shadow * 0.25;

    // Scan dot at current position
    float scanDot = exp(-length(vec2(uv.x, uv.y - (scanY - 0.5) * 1.2)) * (20.0 + uHigh * 20.0));
    color += mix(uPrimaryColor, vec3(1.0), uHigh) * scanDot * (0.12 + uEnergy * 0.15);

    return clamp(color, 0.0, 0.85);
}
