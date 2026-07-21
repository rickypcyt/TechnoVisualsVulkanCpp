// Combined procedural effects from procedural/ folder
// This file includes only simple pack files that use basic helper functions
// Note: These functions return vec4 (with alpha), so we wrap them to vec3 for pass_a_base

// Common helper functions from procedural_header.glsl
// (cannot include directly due to #version directive)
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

vec2 hash2(vec2 p) {
    p = vec2(dot(p, vec2(127.1, 311.7)),
             dot(p, vec2(269.5, 183.3)));
    return fract(sin(p) * 43758.5453);
}

// Voronoi function for Voronoi Cells effect
float voronoi(vec2 p, out float edge, out float cellSeed) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    
    float minDist = 1.0;
    float secondMinDist = 1.0;
    vec2 minPoint = vec2(0.0);
    
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec2 neighbor = vec2(float(x), float(y));
            vec2 point = neighbor + hash2(i + neighbor);
            vec2 diff = point - f;
            float dist = length(diff);
            
            if (dist < minDist) {
                secondMinDist = minDist;
                minDist = dist;
                minPoint = point;
            } else if (dist < secondMinDist) {
                secondMinDist = dist;
            }
        }
    }
    
    edge = secondMinDist - minDist;
    cellSeed = hash2(i + minPoint).x;
    return minDist;
}

vec4 renderVoronoiCells(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    float scale = mix(1.4, 3.8, clamp(bass * 0.9, 0.0, 1.0));
    vec2 p = st * (3.0 + energy * 1.2);
    p *= scale;
    float edge;
    float cellSeed;
    float distance = voronoi(p + time * 0.12, edge, cellSeed);
    float interior = smoothstep(0.0, 0.9, distance * 1.5);
    float edgeGlow = smoothstep(0.02, 0.2, 1.0 - edge);

    // Dark background
    vec3 bgColor = vec3(0.05, 0.05, 0.08);

    // Darker cell colors
    vec3 cellColor = mix(uPrimaryColor * (0.2 + bass * 0.2),
                         uSecondaryColor * (0.3 + high * 0.25),
                         interior);
    cellColor += vec3(0.08, 0.12, 0.1) * cellSeed * 0.3;
    cellColor += vec3(0.15, 0.2, 0.25) * edgeGlow * (0.2 + high * 0.35);
    cellColor = clamp(cellColor, 0.0, 1.0);

    // Mix with dark background
    cellColor = mix(bgColor, cellColor, 0.7);

    float alpha = clamp(0.25 + interior * 0.5 + edgeGlow * (0.35 + high * 0.2) + energy * 0.2, 0.0, 1.0);
    return vec4(cellColor, alpha);
}

// 2D rotation helper
vec2 rotate(vec2 p, float angle) {
    float s = sin(angle);
    float c = cos(angle);
    return mat2(c, -s, s, c) * p;
}

// 3D rotation helper for raymarching
float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(a, b, u.x) + (c - a) * u.y * (1.0 - u.x) + (d - b) * u.x * u.y;
}

float fbm(vec2 p) {
    float value = 0.0;
    float amp = 0.5;
    mat2 m = mat2(1.7, 1.2, -1.2, 1.7);
    for (int i = 0; i < 5; ++i) {
        value += amp * noise(p);
        p = m * p + vec2(0.21, 0.17);
        amp *= 0.5;
    }
    return value;
}

vec3 rotateX(vec3 p, float angle) {
    float c = cos(angle);
    float s = sin(angle);
    return vec3(p.x, p.y * c - p.z * s, p.y * s + p.z * c);
}

vec3 rotateY(vec3 p, float angle) {
    float c = cos(angle);
    float s = sin(angle);
    return vec3(p.x * c + p.z * s, p.y, -p.x * s + p.z * c);
}

vec3 rotateZ(vec3 p, float angle) {
    float c = cos(angle);
    float s = sin(angle);
    return vec3(p.x * c - p.y * s, p.x * s + p.y * c, p.z);
}

// Scene SDF for raymarched object
float mapScene(vec3 p, float time, float energy, float bass, float mid, float high) {
    vec3 q = p;
    vec2 xzRot = rotate(q.xz, time * 0.4 + bass * 0.8);
    q.x = xzRot.x;
    q.z = xzRot.y;
    vec2 xyRot = rotate(q.xy, time * 0.25 + mid * 0.5);
    q.x = xyRot.x;
    q.y = xyRot.y;
    float displacement = fbm(q.xz * (2.2 + high * 0.6) + time * 0.3) * (0.12 + energy * 0.15);
    float sphere = length(q) - (0.65 + bass * 0.35) - displacement;
    float torus = length(vec2(length(q.xz) - (0.9 + mid * 0.4), q.y)) - (0.23 + high * 0.12) - displacement * 0.5;
    return min(sphere, torus);
}

// Normal estimation for raymarching
vec3 estimateNormal(vec3 p, float time, float energy, float bass, float mid, float high) {
    float eps = 0.0015;
    vec3 ex = vec3(eps, 0.0, 0.0);
    vec3 ey = vec3(0.0, eps, 0.0);
    vec3 ez = vec3(0.0, 0.0, eps);
    float dx = mapScene(p + ex, time, energy, bass, mid, high) - mapScene(p - ex, time, energy, bass, mid, high);
    float dy = mapScene(p + ey, time, energy, bass, mid, high) - mapScene(p - ey, time, energy, bass, mid, high);
    float dz = mapScene(p + ez, time, energy, bass, mid, high) - mapScene(p - ez, time, energy, bass, mid, high);
    return normalize(vec3(dx, dy, dz));
}

vec4 renderRaymarchedObject(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    vec3 ro = vec3(0.0, 0.0, 3.0);
    vec3 target = vec3(0.0);
    vec3 forward = normalize(target - ro);
    vec3 right = normalize(vec3(forward.z, 0.0, -forward.x));
    vec3 up = normalize(cross(right, forward));
    vec3 rd = normalize(forward + right * st.x * 1.4 + up * st.y * 1.0);
    float t = 0.0;
    float d = 0.0;
    bool hit = false;
    for (int i = 0; i < 48; ++i) {
        vec3 pos = ro + rd * t;
        d = mapScene(pos, time, energy, bass, mid, high);
        if (d < 0.0015) {
            hit = true;
            break;
        }
        t += d * 0.85;
        if (t > 12.0) {
            break;
        }
    }

    vec3 color;
    float alpha;
    if (hit) {
        vec3 pos = ro + rd * t;
        vec3 normal = estimateNormal(pos, time, energy, bass, mid, high);
        vec3 lightDir = normalize(vec3(0.6, 0.8, -0.4));
        float diff = max(dot(normal, lightDir), 0.0);
        float spec = pow(max(dot(reflect(-lightDir, normal), -rd), 0.0), 24.0);
        float rim = pow(1.0 - max(dot(normal, -rd), 0.0), 3.0);
        vec3 base = mix(uPrimaryColor, uSecondaryColor, 0.45 + high * 0.35);
        color = base * (0.25 + diff * (0.9 + energy * 0.4));
        color += vec3(0.6, 0.4, 1.0) * spec * (0.3 + high * 0.6);
        color += base.bgr * rim * (0.3 + energy * 0.5);
        color = clamp(color, 0.0, 1.0);
        alpha = clamp(0.35 + diff * 0.4 + rim * 0.4 + energy * 0.25, 0.0, 1.0);
    } else {
        float fade = clamp(1.0 - t / 12.0, 0.0, 1.0);
        vec3 bg = mix(uPrimaryColor * 0.15, uSecondaryColor * 0.45, fade);
        bg += vec3(0.08, 0.1, 0.14) * fbm(st * 3.0 + time * 0.2);
        color = clamp(bg, 0.0, 1.0);
        alpha = clamp(fade * 0.4, 0.0, 0.6);
    }
    return vec4(color, alpha);
}

vec4 renderReactionDiffusion(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    vec2 p = st * (3.2 + energy * 1.4);
    vec2 warp = vec2(
        fbm(p + time * 0.35),
        fbm(p + vec2(-4.2, 3.8) - time * 0.27)
    );
    p += warp * (0.7 + mid * 0.7);
    float pattern = fbm(p * (2.6 + tempo * 0.4) + time * 0.12);
    float threshold = mix(0.42, 0.32, clamp(bass * 0.9, 0.0, 1.0));
    float blot = smoothstep(threshold, threshold + 0.18 + high * 0.15, pattern);
    float detail = fbm(p * 5.5 - time * 0.2);
    float veins = smoothstep(0.55, 0.72, detail) * smoothstep(0.35, 0.6, 1.0 - detail);
    vec3 base = mix(uPrimaryColor * 0.5, uSecondaryColor, blot);
    base = mix(base, vec3(0.95, 0.86, 0.68), veins * (0.3 + high * 0.5));
    base += vec3(0.1, 0.06, 0.12) * warp.x * (0.4 + energy * 0.5);
    base = clamp(base, 0.0, 1.0);
    float alpha = clamp(0.3 + blot * 0.5 + veins * 0.3 + energy * 0.2, 0.0, 1.0);
    return vec4(base, alpha);
}

vec4 renderStarfieldWarp(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    vec2 uv = st;
    float speed = 0.35 + tempo * 0.3 + energy * 0.25;
    float stretch = 1.2 + bass * 0.8;
    float accum = 0.0;
    float glow = 0.0;
    for (int i = 0; i < 6; ++i) {
        float depth = fract(float(i) / 6.0 + time * speed * 0.18);
        float fade = smoothstep(0.05, 0.25, depth) * (1.0 - depth);
        vec2 dir = uv / (depth * stretch + 0.25);
        vec2 cell = floor(dir);
        vec2 local = fract(dir) - 0.5;
        float seed = hash(cell + float(i));
        vec2 jitter = (seed - 0.5) * vec2(0.4, 0.2);
        float dist = length(local + jitter);
        float star = smoothstep(0.4, 0.0, dist);
        accum += star * fade;
        glow += star * fade * (0.4 + seed);
    }
    vec3 color = mix(uPrimaryColor, vec3(1.0, 0.95, 0.8), clamp(high + energy * 0.5, 0.0, 1.0));
    color *= accum * (1.2 + high * 0.7);
    color += vec3(0.05, 0.08, 0.12) * (0.8 - length(uv)) * (0.4 + energy * 0.3);
    color += vec3(0.4, 0.5, 0.7) * glow * 0.25;
    color = clamp(color, 0.0, 1.0);
    float alpha = clamp(accum * (0.65 + energy * 0.4), 0.0, 1.0);
    return vec4(color, alpha);
}

vec4 renderPlasmaClassic(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    vec2 p = st;
    float v =
        sin(p.x * (10.0 + tempo * 1.8) + time) +
        sin(p.y * (10.0 + high * 4.0) + time * 1.3) +
        sin((p.x + p.y) * (10.0 + tempo) + time * 0.7);
    v = v / 3.0;
    float wave = sin(time * 1.5 + v * 4.0);
    vec3 base = mix(uPrimaryColor, uSecondaryColor, 0.5 + 0.5 * v);
    base += vec3(0.15, 0.10, 0.20) * wave * (0.4 + mid * 0.4);
    base = clamp(base, 0.0, 1.0);
    float alpha = clamp(0.35 + abs(v) * 0.4 + energy * 0.25, 0.0, 1.0);
    return vec4(base, alpha);
}

vec4 renderDomainWarpedFractal(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    vec2 base = st * (2.4 + energy * 0.8);
    vec2 q = vec2(
        fbm(base * (3.0 + tempo * 0.3) + time * 0.4),
        fbm(base * (3.0 + tempo * 0.3) - time * 0.45)
    );
    vec2 p = base + q * (0.8 + high * 0.5);
    float f = fbm(p * (4.0 + mid * 0.5));
    float ridge = fbm(p * 8.0 - time * 0.3);
    vec3 color = mix(uPrimaryColor, uSecondaryColor, clamp(0.5 + f * 0.5, 0.0, 1.0));
    color += vec3(0.18, 0.10, 0.30) * ridge * (0.4 + high * 0.6);
    color += vec3(0.05, 0.09, 0.12) * q.x;
    color = clamp(color, 0.0, 1.0);
    float alpha = clamp(0.3 + f * 0.5 + ridge * 0.2 + energy * 0.2, 0.0, 1.0);
    return vec4(color, alpha);
}

vec4 renderLiquidRefraction(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    vec2 p = st;
    vec2 offset = vec2(
        fbm(p * (4.0 + energy) + time * 0.6),
        fbm(p * (4.3 + mid * 1.2) - time * 0.55)
    );
    float strength = (0.12 + energy * 0.12 + bass * 0.05);
    vec2 uv = p + offset * (0.18 + high * 0.12) * strength;
    float bg = fbm(uv * (3.2 + tempo * 0.5) - time * 0.2);
    // Replace dFdx/dFdy with finite differences (compute shader compatible)
    float eps = 0.002;
    float bgX = fbm((uv + vec2(eps, 0.0)) * (3.2 + tempo * 0.5) - time * 0.2);
    float bgY = fbm((uv + vec2(0.0, eps)) * (3.2 + tempo * 0.5) - time * 0.2);
    vec2 grad = vec2(bgX - bg, bgY - bg) / eps;
    vec3 base = mix(uPrimaryColor, uSecondaryColor, clamp(0.5 + bg * 0.5, 0.0, 1.0));
    float caustic = clamp(length(grad) * (0.7 + high * 0.6), 0.0, 1.2);
    vec3 highlight = vec3(0.8, 0.9, 1.1) * caustic;
    vec3 color = base + highlight + vec3(0.05, 0.04, 0.03) * offset.x;
    color = clamp(color, 0.0, 1.0);
    float alpha = clamp(0.25 + bg * 0.3 + caustic * 0.4 + energy * 0.2, 0.0, 1.0);
    return vec4(color, alpha);
}

vec3 palette(float t, vec3 a, vec3 b, vec3 c, vec3 d) {
    return a + b * cos(2.0 * PI * (c * t + d));
}

const float kTwoPI = 6.28318530718;

// Include only simple procedural packs compatible with compute shaders
// Exclude packs using fwidth, dFdx, dFdy (require complex derivative layout qualifiers)
#include "../procedural/procedural_pack1.glsl"
#include "../procedural/procedural_pack2.glsl"
#include "../procedural/procedural_pack5.glsl"
#include "../procedural/procedural_pack6.glsl"
#include "../procedural/procedural_pack7.glsl"
#include "../procedural/procedural_pack8.glsl"
#include "../procedural/procedural_pack9.glsl"
#include "../procedural/procedural_pack10.glsl"
#include "../procedural/procedural_pack11.glsl"
#include "../procedural/procedural_pack13.glsl"
// Skip pack14 (uses fwidth - requires derivative_group layout qualifiers)
#include "../procedural/procedural_pack15.glsl"

// Wrapper functions to convert vec4 to vec3 for pass_a_base compatibility
// These use function names that don't conflict with existing renderMode functions

// Mode 1: ASCII Ocean (renamed to avoid conflict with existing renderMode1)
vec3 renderProceduralMode1(vec2 st) {
    return renderASCIIOcean(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh).rgb;
}

// Mode 2: Sacred Geometry
vec3 renderProceduralMode2(vec2 st) {
    return renderSacredGeometry(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh).rgb;
}

// Mode 3: Glitch Grid
vec3 renderProceduralMode3(vec2 st) {
    return renderGlitchGrid(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh).rgb;
}

// Mode 4: Chemical Flow
vec3 renderProceduralMode4(vec2 st) {
    return renderChemicalFlow(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh).rgb;
}

// Mode 5: Crystal Lattice
vec3 renderProceduralMode5(vec2 st) {
    return renderCrystalLattice(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh).rgb;
}

// Mode 6: Phantom Fractals
vec3 renderProceduralMode6(vec2 st) {
    return renderPhantomFractals(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh).rgb;
}

// Mode 7: Fractal Object
vec3 renderProceduralMode7(vec2 st) {
    return renderFractalObject(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh).rgb;
}

// Mode 8: Pulsar Tunnel
vec3 renderProceduralMode8(vec2 st) {
    return renderPulsarTunnel(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh).rgb;
}

// Mode 9: Aurora Bloom
vec3 renderProceduralMode9(vec2 st) {
    return renderAuroraBloom(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh).rgb;
}

// Mode 10: Ribbon Scanlines
vec3 renderProceduralMode10(vec2 st) {
    return renderRibbonScanlines(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh).rgb;
}

// Mode 11: Nebula
vec3 renderProceduralMode11(vec2 st) {
    return renderNebula(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh).rgb;
}

// Mode 12: Kaleidoscope Fractal
vec3 renderProceduralMode12(vec2 st) {
    return renderKaleidoscopeFractal(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh).rgb;
}

// Mode 13: Voronoi Cells
vec3 renderVoronoiCellsWrapper(vec2 st) {
    return renderVoronoiCells(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh).rgb;
}

// Mode 14: Raymarched Object
vec3 renderRaymarchedObjectWrapper(vec2 st) {
    return renderRaymarchedObject(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh).rgb;
}

// Mode 15: Reaction Diffusion
vec3 renderReactionDiffusionWrapper(vec2 st) {
    return renderReactionDiffusion(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh).rgb;
}

// Mode 16: Liquid Refraction
vec3 renderLiquidRefractionWrapper(vec2 st) {
    return renderLiquidRefraction(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh).rgb;
}

// Mode 17: Starfield Warp
vec3 renderStarfieldWarpWrapper(vec2 st) {
    return renderStarfieldWarp(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh).rgb;
}

// Mode 18: Plasma Classic
vec3 renderPlasmaClassicWrapper(vec2 st) {
    return renderPlasmaClassic(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh).rgb;
}

// Mode 19: Domain Warped Fractal
vec3 renderDomainWarpedFractalWrapper(vec2 st) {
    return renderDomainWarpedFractal(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh).rgb;
}

#include "../procedural/procedural_pack76.glsl"

// Mode 102: Melting Red Fractal
vec3 renderMeltingRedFractalWrapper(vec2 st) {
    return renderMeltingRedFractal(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh).rgb;
}

#include "../procedural/procedural_pack77.glsl"

// Mode 103: Liquid Kali Fractal
vec3 renderLiquidKaliFractalWrapper(vec2 st) {
    return renderLiquidKaliFractal(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh).rgb;
}

#include "../procedural/procedural_pack78.glsl"

// Mode 104: Julia Color Fractal
vec3 renderJuliaColorFractalWrapper(vec2 st) {
    return renderJuliaColorFractal(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh).rgb;
}

#include "../procedural/procedural_pack79.glsl"

// Mode 105: Newton RGB Fractal
vec3 renderNewtonRGBFractalWrapper(vec2 st) {
    return renderNewtonRGBFractal(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh).rgb;
}

#include "../procedural/procedural_pack80.glsl"

// Mode 106: Color Process 5-Point
vec3 renderColorProcess5PointWrapper(vec2 st) {
    return renderColorProcess5Point(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh).rgb;
}

#include "../procedural/procedural_pack81.glsl"

// Mode 107: Cellular Simulation
vec3 renderCellularSimulationWrapper(vec2 st) {
    return renderCellularSimulation(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh).rgb;
}

#include "../procedural/procedural_pack82.glsl"

// Mode 108: CFD Fluid Simulation
vec3 renderCFDFluidWrapper(vec2 st) {
    return renderCFDFluid(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh).rgb;
}

#include "../procedural/procedural_pack83.glsl"

// Mode 109: Pixel Sort Luminance
vec3 renderPixelSortLuminanceWrapper(vec2 st) {
    return renderPixelSortLuminance(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh).rgb;
}

#include "../procedural/procedural_pack84.glsl"

// Mode 110: Stylized Oscilloscope
vec3 renderStylizedOscilloscopeWrapper(vec2 st) {
    return renderStylizedOscilloscope(st, uTime, uTempo, uEnergy, uBass, uMid, uHigh).rgb;
}
