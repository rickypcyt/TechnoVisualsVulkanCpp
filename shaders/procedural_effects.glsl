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

    // Pure black background
    vec3 color = vec3(0.0);

    // Plasma blended on top (no white washout)
    float hueShift = fract(pattern + t * 0.05 + uEnergy * 0.3);
    vec3 plasma = mix(uPrimaryColor, uSecondaryColor, hueShift);
    plasma *= 0.15 + pattern * 0.25; // lower cap to avoid bright washout
    color += plasma * pattern * (0.35 + uEnergy * 0.2);

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

    // Pure black background
    vec3 color = vec3(0.0);

    // Color cycling with time
    float colorCycle = fract(pattern * 0.5 + t * 0.1 + uMid * 0.3);
    vec3 burst = mix(uPrimaryColor, uSecondaryColor, colorCycle);
    burst *= 0.15 + pattern * 0.35; // lower cap to avoid bright washout
    color += burst * pattern * (0.35 + uEnergy * 0.2);

    // Center glow that breathes continuously (dimmed)
    float glow = exp(-dist * (4.0 + uBass * 4.0)) * (0.3 + sin(t * 2.0) * 0.2);
    color += mix(uPrimaryColor, uSecondaryColor, sin(t)) * glow * 0.15;

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

    // Pure black background so colors pop
    vec3 color = vec3(0.0);

    // Flowing noise — much more saturated and visible
    vec3 flowColor = mix(uPrimaryColor, uSecondaryColor, pattern);
    flowColor *= 0.6 + n1 * 0.8; // brighter, more saturated
    color += flowColor * clamp(n1 * 0.8 + uEnergy * 0.4, 0.0, 1.0);

    // Secondary flow layer for depth
    vec3 flowColor2 = mix(uSecondaryColor, uPrimaryColor, fract(pattern + 0.5));
    flowColor2 *= 0.4 + n2 * 0.6;
    color += flowColor2 * clamp(n2 * 0.5 + uMid * 0.3, 0.0, 1.0);

    // Moving highlights (brighter)
    float highlight = peNoise(uv * 8.0 + t * 2.0) * uHigh;
    color += mix(uPrimaryColor, uSecondaryColor, sin(t * 3.0)) * highlight * 0.5;

    // Energy-driven glow overlay
    float glow = peFbm(uv * 4.0 + flow * 0.3, 3) * uEnergy;
    color += mix(uPrimaryColor, uSecondaryColor, sin(t * 0.7)) * glow * 0.4;

    return clamp(color, 0.0, 1.2);
}

// ── Mode 6: Audio Grid (moving 1:1 squares) ──────────────────────────────────

vec3 renderCellularVoronoi(vec2 st) {
    vec2 uv = (st - 0.5) * vec2(ubo.resolution.x / max(ubo.resolution.y, 1.0), 1.0);
    float t = uTime * (0.3 + uTempo * 0.6);

    float drive = max(0.1, ubo.audioReactiveDrive);
    float bass  = clamp(uBass * drive, 0.0, 1.0);
    float mid   = clamp(uMid  * drive, 0.0, 1.0);
    float high  = clamp(uHigh * drive, 0.0, 1.0);
    float energy= clamp(uEnergy* drive, 0.0, 1.0);

    // Grid density: 6 to 20 cells, driven by energy + bass
    float gridCount = 6.0 + energy * 10.0 + bass * 4.0;

    // The grid "breathes" — cells shift position over time
    // Audio makes the shift more dramatic
    float shiftX = sin(t * 0.4 + bass * 2.0) * (0.1 + bass * 0.25);
    float shiftY = cos(t * 0.3 + energy * 1.5) * (0.1 + energy * 0.2);

    // Apply shifting to UV before grid division
    vec2 shiftedUV = uv + vec2(shiftX, shiftY);

    // Also a subtle rotation when bass hits
    float rotAngle = bass * 0.3 * sin(t * 0.7);
    float cR = cos(rotAngle);
    float sR = sin(rotAngle);
    shiftedUV = vec2(
        shiftedUV.x * cR - shiftedUV.y * sR,
        shiftedUV.x * sR + shiftedUV.y * cR
    );

    // Grid coordinates
    vec2 cellCoord = shiftedUV * gridCount;
    vec2 cellId = floor(cellCoord);
    vec2 cellFract = fract(cellCoord);

    // Each cell has its own "life" driven by audio + time
    float cellHash = peHash(cellId + 50.0);
    float cellHash2 = peHash(cellId + 150.0);

    // Cell activation: some cells light up based on audio thresholds
    float activation = sin(t * (0.5 + cellHash * 3.0) + bass * 4.0 + cellHash2 * 6.28) * 0.5 + 0.5;
    activation = mix(activation, 1.0, bass * 0.4); // bass boosts activation
    activation = mix(activation, 0.0, (1.0 - energy) * 0.3); // low energy dims

    // Cell size "breathing" — cells grow/shrink with audio
    float cellBreath = 0.75 + activation * 0.35 + mid * 0.2;
    vec2 cellCenter = vec2(0.5);
    vec2 distFromCenter = abs(cellFract - cellCenter) * 2.0;
    float inCell = step(max(distFromCenter.x, distFromCenter.y), cellBreath);

    // Smooth edges for anti-aliased look
    float edgeSmooth = 0.04 + high * 0.03;
    float cellMask = 1.0 - smoothstep(cellBreath - edgeSmooth, cellBreath, max(distFromCenter.x, distFromCenter.y));

    // Cell color: cycles between primary/secondary based on cell hash + time
    float hue = fract(cellHash + t * 0.05 + energy * 0.3);
    vec3 cellColor = mix(uPrimaryColor, uSecondaryColor, hue);

    // Brightness: activated cells glow brighter
    float brightness = 0.2 + activation * 0.8 + bass * 0.3;
    cellColor *= brightness;

    // Pure black background
    vec3 color = vec3(0.0);

    // Draw cells
    color += cellColor * cellMask * (0.6 + energy * 0.4);

    // Grid lines between cells (bright, thin)
    float pixelSize = 1.0 / max(ubo.resolution.y, 1.0);
    float lineW = pixelSize * 1.5 + 0.003 + bass * 0.004;
    float gridLineX = 1.0 - smoothstep(0.0, lineW, cellFract.x) + smoothstep(1.0 - lineW, 1.0, cellFract.x);
    float gridLineY = 1.0 - smoothstep(0.0, lineW, cellFract.y) + smoothstep(1.0 - lineW, 1.0, cellFract.y);
    float gridLine = clamp(gridLineX + gridLineY, 0.0, 1.0);
    color += mix(uPrimaryColor, uSecondaryColor, fract(t * 0.1 + high * 0.3)) * gridLine * (0.5 + mid * 0.4);

    // Active cells get a glow halo
    float halo = exp(-length(cellFract - cellCenter) * (6.0 + bass * 10.0)) * activation;
    color += mix(uPrimaryColor, uSecondaryColor, fract(cellHash * 3.0 + t * 0.2)) * halo * 0.4;

    // Subtle shimmer on top
    float shimmer = peNoise(cellId * 3.0 + t * 2.0) * high * 0.1;
    color += mix(uPrimaryColor, uSecondaryColor, sin(t * 1.5)) * shimmer;

    return clamp(color, 0.0, 1.2);
}

// ── Mode 7: Mandala Spin ───────────────────────────────────────────────────

vec3 renderMandalaSpin(vec2 st) {
    vec2 uv = (st - 0.5) * vec2(ubo.resolution.x / max(ubo.resolution.y, 1.0), 1.0);
    float t = uTime * (0.3 + uTempo * 0.6);
    float dist = length(uv);
    float angle = atan(uv.y, uv.x);

    // Audio drive
    float drive = max(0.1, ubo.audioReactiveDrive);
    float bass  = clamp(uBass * drive, 0.0, 1.0);
    float mid   = clamp(uMid  * drive, 0.0, 1.0);
    float high  = clamp(uHigh * drive, 0.0, 1.0);
    float energy= clamp(uEnergy* drive, 0.0, 1.0);

    // Pure black background
    vec3 color = vec3(0.0);

    // Infinite fractal mandala: multiple layers with different scales and speeds
    for (int i = 0; i < 5; ++i) {
        float fi = float(i);

        // Each layer has more petals and spins at different speed
        float petals = 3.0 + fi * 2.0 + energy * 3.0;
        float spin = t * (1.0 + fi * 0.4) * (1.0 + bass * 0.5);
        float sector = petals * (angle + spin) + bass * 2.0;

        // Sharp thin lines (high exponent makes them thin, not bright areas)
        float line = abs(sin(sector * 0.5));
        line = pow(line, 18.0 + fi * 4.0 + high * 8.0);

        // Expanding rings that pulse inward/outward for infinite depth
        float ringSpeed = t * (1.2 + fi * 0.3) * (1.0 + mid * 0.5);
        float ringFreq = 6.0 + fi * 4.0 + energy * 4.0;
        float ring = sin(dist * ringFreq - ringSpeed);
        ring = pow(abs(ring), 16.0 + fi * 2.0);

        // Combine: only where line AND ring intersect -> thin colorful threads
        float pattern = line * ring;

        // Hue shifts per layer for colorful fractal effect
        float hue = fract(angle / 6.283 + t * 0.07 + fi * 0.17 + energy * 0.2 + dist * 0.25);
        vec3 lineColor = mix(uPrimaryColor, uSecondaryColor, hue);

        // Dimmer as layers go deeper (falloff)
        float layerIntensity = 0.35 / (1.0 + fi * 0.6);
        color += lineColor * pattern * layerIntensity * (0.6 + energy * 0.4);
    }

    // Infinite spiral vortex: thin colored threads spiraling into the center
    float spiral = sin(angle * 6.0 + dist * 12.0 - t * 3.0);
    float spiralLine = pow(abs(spiral), 24.0);
    float spiralMask = exp(-dist * (2.0 + bass * 2.0));
    vec3 spiralColor = mix(uSecondaryColor, uPrimaryColor, fract(t * 0.2 + dist * 2.0));
    color += spiralColor * spiralLine * spiralMask * (0.15 + high * 0.15);

    // Very dim center glow only on bass hits (keeps center dark most of the time)
    float vortex = exp(-dist * (4.0 + bass * 3.0)) * (0.05 + bass * 0.12);
    color += mix(uPrimaryColor, uSecondaryColor, sin(t * 3.0)) * vortex;

    return clamp(color, 0.0, 0.9);
}

// ── Mode 8: Cellular Automata (audio-reactive) ──────────────────────────────

vec3 renderTerrainScan(vec2 st) {
    vec2 uv = (st - 0.5) * vec2(ubo.resolution.x / max(ubo.resolution.y, 1.0), 1.0);
    float t = uTime * (0.3 + uTempo * 0.5);

    float drive = max(0.1, ubo.audioReactiveDrive);
    float bass  = clamp(uBass * drive, 0.0, 1.0);
    float mid   = clamp(uMid  * drive, 0.0, 1.0);
    float high  = clamp(uHigh * drive, 0.0, 1.0);
    float energy= clamp(uEnergy* drive, 0.0, 1.0);

    // Grid density reacts to energy (8 to 24 cells across)
    float gridDensity = 8.0 + energy * 16.0 + bass * 6.0;
    vec2 cellCoord = uv * gridDensity;
    vec2 cellId = floor(cellCoord);
    vec2 cellFract = fract(cellCoord);

    // Per-cell hash for deterministic randomness
    float cellRand = peHash(cellId + 100.0);
    float cellRand2 = peHash(cellId + 200.0);

    // Audio-reactive cell activation rules
    // Cells "live" when their hash matches an audio threshold
    float birthThreshold = 0.25 + bass * 0.35;
    float surviveThreshold = 0.15 + mid * 0.25;

    // Time-based pulse per cell (creates organic evolution)
    float pulse = sin(t * (0.5 + cellRand * 2.0) + cellRand2 * 6.28 + bass * 4.0) * 0.5 + 0.5;
    float alive = step(birthThreshold, cellRand * pulse);
    float aliveSurvive = step(surviveThreshold, cellRand2 * pulse);
    float cellState = max(alive, aliveSurvive * 0.6);

    // Neighbor count for Conway-like smoothness (3x3 kernel)
    float neighbors = 0.0;
    for (int ny = -1; ny <= 1; ++ny) {
        for (int nx = -1; nx <= 1; ++nx) {
            if (nx == 0 && ny == 0) continue;
            vec2 nId = cellId + vec2(float(nx), float(ny));
            float nRand = peHash(nId + 100.0);
            float nPulse = sin(t * (0.5 + nRand * 2.0) + peHash(nId + 200.0) * 6.28 + bass * 4.0) * 0.5 + 0.5;
            neighbors += step(birthThreshold, nRand * nPulse);
        }
    }
    neighbors /= 8.0;

    // Game-of-Life-inspired state: born with 3 neighbors, survive with 2-3
    float golState = 0.0;
    if (cellState > 0.5 && neighbors >= 0.25 && neighbors <= 0.5) {
        golState = 1.0; // survive
    } else if (cellState < 0.5 && neighbors >= 0.3 && neighbors <= 0.45) {
        golState = 1.0; // birth
    }
    // Blend original cellState with GOL for organic feel
    cellState = mix(cellState, golState, 0.4);

    // Resolution-aware smooth cell borders
    float pixelSize = 1.0 / max(ubo.resolution.y, 1.0);
    float gridLineWidth = 0.015 + pixelSize * 1.5 + bass * 0.008;

    // Distance to nearest cell edge (for smooth grid lines)
    vec2 edgeDist = min(cellFract, 1.0 - cellFract);
    float edge = smoothstep(0.0, gridLineWidth, min(edgeDist.x, edgeDist.y));
    // Invert: line is HIGH at edges, LOW inside cell
    float gridLine = 1.0 - edge;

    // Cell fill intensity (colored interior)
    float fill = cellState * (0.25 + energy * 0.3);

    // Pure black background
    vec3 color = vec3(0.0);

    // Cell interior glow (dimmed, colored)
    float cellHue = fract(cellRand + t * 0.02 + energy * 0.3);
    vec3 cellColor = mix(uPrimaryColor, uSecondaryColor, cellHue);
    color += cellColor * fill * 0.4;

    // Grid lines (bright, anti-aliased, audio-reactive)
    float lineIntensity = 0.5 + bass * 0.4 + mid * 0.3;
    vec3 lineColor = mix(uPrimaryColor, uSecondaryColor, fract(t * 0.1 + high * 0.3));
    color += lineColor * gridLine * lineIntensity * (0.8 + energy * 0.4);

    // Active cells pulse with neighbors (border glow)
    float activeGlow = cellState * neighbors * exp(-min(edgeDist.x, edgeDist.y) * (20.0 + bass * 30.0));
    color += mix(uPrimaryColor, uSecondaryColor, fract(cellRand * 2.0 + t * 0.15)) * activeGlow * 0.5;

    // Subtle organic noise on top (not glitch)
    float organicNoise = peNoise(uv * 20.0 + t * 0.5) * 0.02 * energy;
    color += mix(uPrimaryColor, uSecondaryColor, sin(t * 0.3)) * organicNoise;

    return clamp(color, 0.0, 1.0);
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

        // Thickness: thicker core line with resolution-aware anti-aliasing
        float pixelSize = 1.0 / max(ubo.resolution.y, 1.0);
        float thick = 0.012 + band * 0.020 + fi * 0.004;
        // Smooth line with at least 1 pixel of anti-aliasing on each side
        float line = 1.0 - smoothstep(0.0, thick + pixelSize * 1.5, abs(y));
        line = pow(line, 1.5); // slightly sharpen the core while keeping soft edges

        // Stronger glow around the line, modulated by band
        float glow = exp(-y * y * (80.0 + band * 120.0)) * (0.35 + band * 0.45);

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

// ── Mode 11: Corner X ────────────────────────────────────────────────────────

vec3 renderCornerX(vec2 st) {
    vec2 uv = st;
    float t = uTime * (0.3 + uTempo * 0.6);

    float drive = max(0.1, ubo.audioReactiveDrive);
    float bass  = clamp(uBass * drive, 0.0, 1.0);
    float mid   = clamp(uMid  * drive, 0.0, 1.0);
    float high  = clamp(uHigh * drive, 0.0, 1.0);
    float energy= clamp(uEnergy* drive, 0.0, 1.0);

    // Diagonal distances (corner-to-corner lines)
    float d1 = abs(uv.y - uv.x) * 0.7071;           // bottom-left to top-right
    float d2 = abs(uv.y + uv.x - 1.0) * 0.7071;     // top-left to bottom-right

    // Audio-reactive line thickness
    float baseThick = 0.008 + bass * 0.012;
    float line1 = 1.0 - smoothstep(0.0, baseThick, d1);
    float line2 = 1.0 - smoothstep(0.0, baseThick, d2);
    float lines = max(line1, line2);

    // Glow around lines
    float glow1 = exp(-d1 * d1 * (2000.0 + bass * 2000.0));
    float glow2 = exp(-d2 * d2 * (2000.0 + bass * 2000.0));
    float glow = max(glow1, glow2);

    // Cross intersection pulse at center
    float centerDist = length(uv - 0.5);
    float centerPulse = exp(-centerDist * centerDist * (40.0 + energy * 80.0));
    centerPulse *= 0.5 + 0.5 * sin(t * 4.0 + bass * 6.0);

    // Pure black background
    vec3 color = vec3(0.0);

    // Line color cycling with time
    float hue = fract(t * 0.1 + energy * 0.3);
    vec3 lineCol = mix(uPrimaryColor, uSecondaryColor, hue);
    vec3 glowCol = mix(uSecondaryColor, uPrimaryColor, fract(hue + 0.5));

    color += lineCol * lines * (0.8 + bass * 0.6);
    color += glowCol * glow * (0.25 + energy * 0.35);
    color += mix(uPrimaryColor, uSecondaryColor, sin(t * 2.0)) * centerPulse * (0.3 + high * 0.3);

    // Corner sparkles when high hits
    float cornerSparkle = 0.0;
    vec2 corners[4] = vec2[](vec2(0.0,0.0), vec2(1.0,0.0), vec2(0.0,1.0), vec2(1.0,1.0));
    for (int i = 0; i < 4; ++i) {
        float cd = length(uv - corners[i]);
        cornerSparkle += exp(-cd * cd * (150.0 + high * 300.0)) * 0.15;
    }
    color += mix(uPrimaryColor, vec3(1.0), high) * cornerSparkle * (0.4 + high * 0.6);

    return clamp(color, 0.0, 1.2);
}

// ── Mode 12: Electric Rays ─────────────────────────────────────────────────
// Bass = big bolts from bottom, Mid = medium sideways bolts, High = thin top bolts

float electricBolt(vec2 p, vec2 origin, vec2 target, float thickness, float jitters, float seed, float t) {
    vec2 dir = target - origin;
    float len = length(dir);
    if (len < 0.001) return 0.0;
    vec2 ndir = dir / len;
    vec2 perp = vec2(-ndir.y, ndir.x);

    float proj = dot(p - origin, ndir);
    float along = clamp(proj / len, 0.0, 1.0);

    // Jitter offsets at multiple points
    float jit = 0.0;
    for (float i = 0.0; i < 6.0; i += 1.0) {
        float fi = i / 6.0;
        float phase = t * (3.0 + jitters) + seed * 10.0 + i * 3.7;
        jit += sin(phase) * (0.5 - abs(fi - 0.5)) * 2.0;
    }
    jit *= 0.15 * jitters;

    // Discretize jitter segments
    float seg = floor(along * 8.0) / 8.0;
    float segJitter = sin(seg * 47.0 + seed * 23.0 + t * 5.0) * 0.12 * jitters;

    vec2 boltPos = origin + ndir * (along * len) + perp * (jit + segJitter) * len;
    float dist = length(p - boltPos);

    // Main core + glow
    float core = 1.0 - smoothstep(0.0, thickness, dist);
    float glow = exp(-dist * dist * (80.0 + jitters * 60.0));
    return core * 0.7 + glow * 0.4;
}

vec3 renderElectricRays(vec2 st) {
    vec2 uv = st;
    float t = uTime * (0.5 + uTempo * 0.8);

    float drive = max(0.1, ubo.audioReactiveDrive);
    float bass  = clamp(uBass * drive, 0.0, 1.0);
    float mid   = clamp(uMid  * drive, 0.0, 1.0);
    float high  = clamp(uHigh * drive, 0.0, 1.0);
    float energy= clamp(uEnergy* drive, 0.0, 1.0);

    vec3 color = vec3(0.0);

    // ── BASS: 3 big bolts from bottom ──
    for (int i = 0; i < 3; ++i) {
        float fi = float(i);
        float seed = fi * 17.0 + 1.0;
        vec2 origin = vec2(0.2 + fi * 0.3 + sin(t * 0.5 + fi) * 0.05, 0.0);
        vec2 target = vec2(0.3 + fi * 0.2 + sin(t * 0.7 + fi * 2.0) * 0.15, 0.4 + bass * 0.35);
        float thick = 0.008 + bass * 0.018;
        float bolt = electricBolt(uv, origin, target, thick, 1.0 + bass * 2.0, seed, t);
        vec3 boltCol = mix(uPrimaryColor, vec3(1.0, 0.9, 0.7), bass * 0.5);
        color += boltCol * bolt * (0.6 + bass * 0.8);
    }

    // ── MID: 4 medium bolts from left & right sides ──
    for (int i = 0; i < 4; ++i) {
        float fi = float(i);
        float seed = fi * 13.0 + 50.0;
        bool fromLeft = mod(fi, 2.0) < 1.0;
        float sideX = fromLeft ? 0.0 : 1.0;
        vec2 origin = vec2(sideX, 0.2 + fi * 0.2 + cos(t * 0.6 + fi) * 0.05);
        vec2 target = vec2(fromLeft ? (0.35 + mid * 0.25) : (0.65 - mid * 0.25),
                           0.3 + sin(t * 0.8 + fi * 1.5) * 0.15);
        float thick = 0.004 + mid * 0.010;
        float bolt = electricBolt(uv, origin, target, thick, 0.8 + mid * 1.5, seed, t);
        vec3 boltCol = mix(uSecondaryColor, uPrimaryColor, mid);
        color += boltCol * bolt * (0.5 + mid * 0.7);
    }

    // ── HIGH: 5 thin bolts from top ──
    for (int i = 0; i < 5; ++i) {
        float fi = float(i);
        float seed = fi * 11.0 + 100.0;
        vec2 origin = vec2(0.15 + fi * 0.17 + sin(t * 0.9 + fi * 2.3) * 0.04, 1.0);
        vec2 target = vec2(0.2 + fi * 0.15 + cos(t * 1.1 + fi) * 0.1, 0.55 - high * 0.2);
        float thick = 0.002 + high * 0.006;
        float bolt = electricBolt(uv, origin, target, thick, 0.6 + high * 2.5, seed, t);
        vec3 boltCol = mix(vec3(0.7, 0.9, 1.0), uSecondaryColor, high);
        color += boltCol * bolt * (0.4 + high * 0.9);
    }

    // Global energy flash
    float flash = exp(-length(uv - 0.5) * 2.0) * energy * 0.15;
    color += mix(uPrimaryColor, uSecondaryColor, sin(t * 2.0)) * flash;

    return clamp(color, 0.0, 1.5);
}
