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

// ── 3D rotation helpers ──────────────────────────────────────────────────────

mat3 peRotX(float a) {
    float c = cos(a), s = sin(a);
    return mat3(1.0, 0.0, 0.0,
                0.0,   c, -s,
                0.0,   s,  c);
}

mat3 peRotY(float a) {
    float c = cos(a), s = sin(a);
    return mat3(  c, 0.0,  s,
                0.0, 1.0, 0.0,
                 -s, 0.0,  c);
}

mat3 peRotZ(float a) {
    float c = cos(a), s = sin(a);
    return mat3(  c, -s, 0.0,
                  s,  c, 0.0,
                0.0, 0.0, 1.0);
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

    float lineX = 1.0 - smoothstep(0.0, 0.02 + uMid * 0.03, gridX);
    float lineY = 1.0 - smoothstep(0.0, 0.02 + uMid * 0.03, gridY);

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
    float scanLine = 1.0 - smoothstep(0.0, 0.015, abs(uv.y - (scanY - 0.5) * 1.2));

    // Contour lines at terrain height levels
    float contour = abs(fract(height * 10.0 + uBass * 2.0) - 0.5);
    float contourLine = 1.0 - smoothstep(0.0, 0.02 + uMid * 0.02, contour);

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

// ── Mode 9: Wireframe Cube 3D ────────────────────────────────────────────────

vec3 renderWireCube(vec2 st) {
    vec2 uv = (st - 0.5) * vec2(ubo.resolution.x / max(ubo.resolution.y, 1.0), 1.0);
    float t = uTime * (0.4 + uTempo * 0.6);

    // Smaller cube that always fits on screen
    float s = 0.35 + uEnergy * 0.2 + uBass * 0.15;

    // 8 cube corners
    vec3 v[8] = vec3[](
        vec3(-s, -s, -s), vec3(s, -s, -s), vec3(s, s, -s), vec3(-s, s, -s),
        vec3(-s, -s,  s), vec3(s, -s,  s), vec3(s, s,  s), vec3(-s, s,  s)
    );

    // 12 edges as flat int pairs (avoids ivec2 array issues)
    const int edgePairs[24] = int[](
        0,1, 1,2, 2,3, 3,0,
        4,5, 5,6, 6,7, 7,4,
        0,4, 1,5, 2,6, 3,7
    );

    // Rotation driven by audio
    float ax = t * 0.37 + uHigh * 2.0;
    float ay = t * 0.53 + uMid * 2.0;
    float az = t * 0.71 + uBass * 2.0;
    mat3 rot = peRotZ(az) * peRotY(ay) * peRotX(ax);

    // Perspective projection
    float camZ = 3.0;
    vec2 proj[8];
    float depth[8];

    for (int i = 0; i < 8; ++i) {
        vec3 p = rot * v[i];
        float z = p.z + camZ;
        float w = 2.0 / max(z, 0.5);
        proj[i] = p.xy * w;
        depth[i] = z;
    }

    // Find nearest edge to this pixel
    float minDist = 999.0;

    for (int e = 0; e < 12; ++e) {
        int i0 = edgePairs[e * 2];
        int i1 = edgePairs[e * 2 + 1];
        vec2 a = proj[i0];
        vec2 b = proj[i1];
        vec2 ab = b - a;
        float abLenSq = dot(ab, ab);
        if (abLenSq < 0.00001) continue;
        vec2 ap = uv - a;
        float h = clamp(dot(ap, ab) / abLenSq, 0.0, 1.0);
        vec2 closest = a + ab * h;
        float d = length(uv - closest);
        if (d < minDist) minDist = d;
    }

    // Line rendering — CORRECT smoothstep order (edge0 < edge1)
    float thick = 0.018 + uBass * 0.012;
    float line = 1.0 - smoothstep(0.0, thick, minDist);

    // Glow around lines
    float glow = exp(-minDist * minDist * 40.0) * 0.4;

    // Color cycling with time
    float hue = fract(t * 0.15 + uEnergy * 0.3);
    vec3 lineCol = mix(uPrimaryColor, uSecondaryColor, hue) * (line + glow * 0.5);
    vec3 glowCol = mix(uSecondaryColor, uPrimaryColor, fract(t * 0.2 + 0.5)) * glow;

    // Corner dots
    float dots = 0.0;
    for (int i = 0; i < 8; ++i) {
        float d = length(uv - proj[i]);
        dots += exp(-d * d * 100.0) * 0.35;
    }
    vec3 dotCol = mix(uPrimaryColor, vec3(1.0), 0.3) * dots;

    // Dark background so cube is visible against black
    vec3 color = vec3(0.02, 0.015, 0.025);
    color += lineCol + glowCol * 0.5 + dotCol;
    return clamp(color, 0.0, 0.85);
}

// ── Mode 10: Oscilloscope ──────────────────────────────────────────────────

vec3 renderOscilloscope(vec2 st) {
    vec2 uv = (st - 0.5) * vec2(ubo.resolution.x / max(ubo.resolution.y, 1.0), 1.0);
    float t = uTime * (0.3 + uTempo * 0.4);

    float drive = max(0.1, ubo.audioReactiveDrive);
    float bass = clamp(ubo.bass * drive, 0.0, 1.0);
    float mid  = clamp(ubo.mid * drive, 0.0, 1.0);
    float high = clamp(ubo.high * drive, 0.0, 1.0);
    float energy = clamp(ubo.energy * drive, 0.0, 1.0);

    // Dark background with subtle grid
    vec3 color = vec3(0.005, 0.005, 0.008);
    float gridX = abs(fract(uv.x * 10.0) - 0.5);
    float gridY = abs(fract(uv.y * 8.0) - 0.5);
    color += vec3(0.01) * (smoothstep(0.02, 0.0, gridX) + smoothstep(0.02, 0.0, gridY));

    // Each trace reacts to a different frequency band
    // trace 0 = bass, 1 = mid, 2 = high, 3 = energy
    float bands[4] = float[4](bass, mid, high, energy);

    for (int trace = 0; trace < 4; ++trace) {
        float fi = float(trace);
        float band = bands[trace];

        // Time offset per trace, modulated by its band
        float tt = t * (0.5 + fi * 0.3 + band * 0.5) + fi * 1.57;
        // Frequency strongly driven by band
        float freq = 2.0 + fi * 2.5 + band * 6.0 + sin(tt * 0.3) * 2.0;

        // Complex waveform — each harmonic scaled by different band
        float shape = sin(uv.x * freq + tt) * (0.35 + mid * 0.35);
        shape += sin(uv.x * freq * 2.37 + tt * 0.73 + bass * 2.0) * (0.25 + bass * 0.25);
        shape += sin(uv.x * freq * 3.13 + tt * 1.19 - high * 3.0) * (0.18 + high * 0.2);
        shape += cos(uv.x * freq * 1.73 + tt * 0.47 + mid * 4.0) * (0.12 + energy * 0.15);
        // Add a fast chaotic harmonic for "hair"
        shape += sin(uv.x * freq * 5.91 + tt * 1.7) * (0.05 + band * 0.08);

        // Amplitude driven PRIMARILY by the trace's band, not just energy
        float amp = 0.15 + band * 0.5 + bass * 0.15;
        float y = uv.y - shape * amp;

        // Thickness: much thicker core line + pronounced glow
        float thick = 0.004 + band * 0.012 + fi * 0.002;
        // Sharper core line using pow for pronounced look
        float line = pow(1.0 - smoothstep(0.0, thick, abs(y)), 2.0);

        // Stronger glow around the line, modulated by band
        float glow = exp(-y * y * (120.0 + band * 200.0)) * (0.25 + band * 0.35);

        // Color per trace, strongly tied to its band
        float hue = fract(t * 0.15 + fi * 0.25 + band * 0.3);
        vec3 traceCol = mix(uPrimaryColor, uSecondaryColor, hue);

        // Intensity boosted by band
        float intensity = 0.7 - fi * 0.1 + band * 0.4;
        color += traceCol * line * intensity;
        color += traceCol * glow * 0.6;
    }

    // Central vertical beam reacting to bass
    float beamWidth = 0.008 + bass * 0.025;
    float beam = exp(-abs(uv.x) * abs(uv.x) * (80.0 + bass * 300.0)) * (0.15 + bass * 0.5);
    color += mix(uPrimaryColor, vec3(1.0), high * 0.6) * beam;

    // Horizontal sweep line reacting to tempo
    float sweep = fract(t * 0.5 * uTempo);
    float sweepLine = exp(-abs(uv.x - (sweep - 0.5) * 1.8) * 80.0) * 0.08;
    color += uSecondaryColor * sweepLine;

    // Lissajous dots when energy is high
    if (energy > 0.25) {
        float lissX = sin(t * 0.7 + uv.x * 4.0 + bass * 3.0) * 0.35;
        float lissY = cos(t * 0.5 + uv.y * 3.0 + high * 2.0) * 0.35;
        float liss = length(uv - vec2(lissX, lissY));
        float lissDot = exp(-liss * liss * 100.0) * (energy - 0.25) * 1.2;
        color += mix(uSecondaryColor, uPrimaryColor, sin(t + mid * 3.0)) * lissDot;
    }

    return clamp(color, 0.0, 1.2);
}
