#version 450
#extension GL_GOOGLE_include_directive : require

#include "shared_ubo.glsl"

const float PI = 3.1415926535;

#define uTime           ubo.time
#define uTempo          ubo.tempo
#define uEnergy         ubo.energy
#define uBass           ubo.bass
#define uMid            ubo.mid
#define uHigh           ubo.high
#define uPrimaryColor   ubo.primaryColor.rgb
#define uSecondaryColor ubo.secondaryColor.rgb

#include "procedural_anaglyph.glsl"
#include "procedural_effects.glsl"

#undef uTime
#undef uTempo
#undef uEnergy
#undef uBass
#undef uMid
#undef uHigh
#undef uPrimaryColor
#undef uSecondaryColor

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform sampler2D videoTex;
layout(set = 1, binding = 1) uniform sampler2D videoTexPrev;
layout(set = 1, binding = 2) uniform sampler2D video2Tex;

float luminance(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

vec2 applyCamera(vec2 uv) {
    if (ubo.enableCameraMovement == 0) return uv;
    vec2 p = uv - 0.5;
    float c = cos(ubo.cameraRotation);
    float s = sin(ubo.cameraRotation);
    p = vec2(c * p.x - s * p.y, s * p.x + c * p.y);
    p /= max(ubo.cameraZoom, 0.0001);
    p += vec2(ubo.cameraPanX, ubo.cameraPanY);
    return p + 0.5;
}

vec3 sampleVideo(vec2 p) {
    return texture(videoTex, clamp(p, 0.0, 1.0)).rgb;
}

vec3 sampleVideo2(vec2 p) {
    return texture(video2Tex, clamp(p, 0.0, 1.0)).rgb;
}

vec3 sampleBilinear(vec2 p) {
    return sampleVideo(p);
}

vec3 blendTwoVideos(vec3 a, vec3 b, int mode, float t) {
    t = clamp(t, 0.0, 1.0);
    if (mode == 0) {
        // Mix / crossfade
        return mix(a, b, t);
    } else if (mode == 1) {
        // Add
        return clamp(a + b * t, 0.0, 1.0);
    } else if (mode == 2) {
        // Multiply
        return mix(a, a * b, t);
    } else if (mode == 3) {
        // Screen
        return mix(a, 1.0 - (1.0 - a) * (1.0 - b), t);
    } else if (mode == 4) {
        // Difference
        return mix(a, abs(a - b), t);
    }
    return mix(a, b, t);
}

vec3 blendProceduralMode(vec3 base, vec3 layer, int mode) {
    if (mode == 0) {
        // Add
        return clamp(base + layer, 0.0, 1.5);
    } else if (mode == 1) {
        // Screen
        return clamp(1.0 - (1.0 - base) * (1.0 - layer), 0.0, 1.0);
    } else if (mode == 2) {
        // Multiply
        return clamp(base * layer, 0.0, 1.0);
    } else if (mode == 3) {
        // Overlay
        vec3 low = 2.0 * base * layer;
        vec3 high = 1.0 - 2.0 * (1.0 - base) * (1.0 - layer);
        return clamp(mix(low, high, step(vec3(0.5), base)), 0.0, 1.0);
    } else if (mode == 4) {
        // Difference
        return clamp(abs(base - layer), 0.0, 1.0);
    } else if (mode == 5) {
        // Soft Light
        vec3 low = 2.0 * base * layer + base * base * (1.0 - 2.0 * layer);
        vec3 high = sqrt(clamp(base, vec3(0.0), vec3(1.0))) * (2.0 * layer - 1.0) + 2.0 * base * (1.0 - layer);
        return clamp(mix(low, high, step(vec3(0.5), layer)), 0.0, 1.0);
    }
    return mix(base, layer, 0.5);
}

vec3 applyProceduralBlend(vec3 base, vec3 layer, int mode, float amount) {
    float mixAmount = clamp(amount, 0.0, 2.0);
    vec3 blended = blendProceduralMode(base, layer, mode);
    vec3 result = mix(base, blended, min(mixAmount, 1.0));
    if (mixAmount > 1.0) {
        float extra = mixAmount - 1.0;
        result = mix(result, blended, clamp(extra, 0.0, 1.0));
    }
    return result;
}

vec3 sharpen3x3(vec2 p, float amount) {
    vec2 t = 1.0 / max(ubo.resolution, vec2(1.0));
    vec3 c  = sampleVideo(p);
    vec3 n  = sampleVideo(p + vec2( 0.0,  1.0) * t);
    vec3 s  = sampleVideo(p + vec2( 0.0, -1.0) * t);
    vec3 e  = sampleVideo(p + vec2( 1.0,  0.0) * t);
    vec3 w  = sampleVideo(p + vec2(-1.0,  0.0) * t);
    vec3 lap = (n + s + e + w) - 4.0 * c;
    return clamp(c - lap * amount, 0.0, 1.0);
}

vec3 renderMode0(vec2 st) {
    vec2 uv = (st - 0.5) * vec2(ubo.resolution.x / max(ubo.resolution.y, 1.0), 1.0);
    float t = ubo.time * (0.5 + ubo.tempo * 0.8);

    float drive = max(0.1, ubo.audioReactiveDrive);
    float bass = clamp(ubo.bass * drive, 0.0, 1.0);
    float mid  = clamp(ubo.mid * drive, 0.0, 1.0);
    float high = clamp(ubo.high * drive, 0.0, 1.0);
    float energy = clamp(ubo.energy * drive, 0.0, 1.0);

    // Escalating multi-axis rotation — accumulates speed with audio energy
    float rotSpeed = 0.15 + bass * 2.5 + energy * 1.5;
    float rotPhase = t * rotSpeed;
    // Multiple harmonic frequencies prevent looping
    float angle1 = rotPhase + bass * 4.0;
    float angle2 = rotPhase * 0.67 + mid * 3.0 + sin(t * 0.3) * 2.0;
    float c1 = cos(angle1);
    float s1 = sin(angle1);
    float c2 = cos(angle2);
    float s2 = sin(angle2);
    // Apply dual rotation — axis 1 then axis 2 (pseudo-3D feel)
    vec2 rotated = vec2(
        uv.x * c1 - uv.y * s1,
        uv.x * s1 + uv.y * c1
    );
    // Second axis adds complexity
    rotated = vec2(
        rotated.x * c2 - rotated.y * s2,
        rotated.x * s2 + rotated.y * c2
    );

    // Square size pulses with bass
    float halfSize = 0.25 + energy * 0.15 + bass * 0.12;
    float edgeWidth = 0.025 + high * 0.02;

    float box = max(abs(rotated.x), abs(rotated.y));

    // Outer border glow that pulses outward with bass
    float outerGlow = smoothstep(halfSize + 0.25 + bass * 0.2, halfSize, box);

    float fill = smoothstep(halfSize, halfSize - edgeWidth, box);
    float border = smoothstep(halfSize + edgeWidth, halfSize, box) - fill;

    // Inner grid pattern that reacts to mid
    float grid = 0.0;
    if (box < halfSize) {
        float gridX = abs(fract(rotated.x * (8.0 + mid * 8.0)) - 0.5);
        float gridY = abs(fract(rotated.y * (8.0 + mid * 8.0)) - 0.5);
        grid = 1.0 - smoothstep(0.0, 0.04 + high * 0.03, max(gridX, gridY));
    }

    float colorLerp = clamp(0.2 + 0.5 * energy + 0.3 * mid, 0.0, 1.0);

    // Background with slow color shift and bass-driven vignette
    vec3 bg = mix(ubo.secondaryColor.rgb * 0.08, ubo.primaryColor.rgb * 0.05, energy);
    bg *= 1.0 + sin(t * 0.3) * 0.05;

    vec3 square = mix(ubo.primaryColor.rgb, ubo.secondaryColor.rgb, colorLerp);
    vec3 borderColor = mix(square, vec3(1.0), high * 0.6);

    // Bass makes the square brighter and redder
    square *= 0.7 + bass * 0.5;
    borderColor *= 0.8 + bass * 0.4;

    vec3 color = bg;
    // Outer glow
    vec3 glowCol = mix(ubo.secondaryColor.rgb, ubo.primaryColor.rgb, sin(t * 0.5) * 0.5 + 0.5);
    color += glowCol * outerGlow * (0.15 + bass * 0.25);
    color = mix(color, borderColor, border);
    color = mix(color, square, fill);
    color += grid * mix(ubo.primaryColor.rgb, vec3(1.0), high * 0.5) * (0.15 + mid * 0.15);

    // High-frequency sparkle on edges
    float sparkle = smoothstep(0.48, 0.5, box) * high * 0.3;
    color += vec3(1.0, 0.9, 0.8) * sparkle;

    return clamp(color, 0.0, 0.85);
}

vec3 renderMode1(vec2 st) {
    vec2 centered = (st - 0.5) * vec2(ubo.resolution.x / max(ubo.resolution.y, 1.0), 1.0);
    float drive = max(0.1, ubo.audioReactiveDrive);
    float bassResponse = clamp(ubo.bass * drive, 0.0, 1.0);
    float energyResponse = clamp(ubo.energy * drive, 0.0, 1.0);
    float midResponse = clamp(ubo.mid * drive, 0.0, 1.0);
    float highResponse = clamp(ubo.high * drive, 0.0, 1.0);

    float radius = 0.25 + 0.25 * (0.6 * bassResponse + 0.4 * energyResponse);
    float dist = length(centered);
    float angle = atan(centered.y, centered.x);

    // ── Circle mask ──
    float circleEdge = smoothstep(radius + 0.01, radius - 0.01, dist);
    float rim = smoothstep(radius + 0.06, radius, dist) - circleEdge;

    // ── Inside the circle: glowing orb content ──
    float hueShift = clamp(ubo.mid * drive, 0.0, 1.0);
    float bodyMix = clamp(ubo.colorBlend + hueShift * 0.3, 0.0, 1.0);
    vec3 bodyColor = mix(ubo.primaryColor.rgb, ubo.secondaryColor.rgb, bodyMix);
    vec3 rimColor = mix(ubo.secondaryColor.rgb, vec3(1.0, 0.9, 0.8), clamp(ubo.high * drive, 0.0, 1.0));

    // Animated inner swirl
    float swirl = sin(angle * 3.0 + ubo.time * 2.0 + bassResponse * 4.0) * 0.5 + 0.5;
    float innerGlow = exp(-dist * dist * (8.0 + bassResponse * 10.0)) * (0.5 + energyResponse * 0.5);

    vec3 innerColor = mix(bodyColor, rimColor, swirl * 0.4);
    innerColor += innerGlow * ubo.primaryColor.rgb;

    // ── Outside: light rays shooting from behind the circle ──
    vec3 outsideColor = vec3(0.0);

    // Only draw rays outside the circle
    if (dist > radius * 0.9) {
        float t = ubo.time * (1.0 + ubo.tempo * 1.5);

        // Radial rays emanating from circle edge
        int numRays = 6 + int(energyResponse * 10.0);
        for (int i = 0; i < 16; ++i) {
            if (i >= numRays) break;
            float fi = float(i);
            float rayAngle = (fi / float(numRays)) * 6.283 + t * 0.3 + bassResponse * 2.0;
            float angleDiff = abs(fract((angle - rayAngle) / 6.283 + 0.5) - 0.5) * 6.283;

            // Ray width narrows as it goes out
            float rayWidth = 0.15 + fi * 0.02;
            float ray = exp(-angleDiff * angleDiff / (rayWidth * rayWidth));

            // Ray length: longer with bass, fades with distance
            float rayLength = 1.0 - smoothstep(radius, radius + 0.3 + bassResponse * 0.4, dist);
            ray *= rayLength;

            // Pulse intensity with audio
            float pulse = sin(t * 2.0 + fi * 1.3) * 0.5 + 0.5;
            pulse = mix(pulse, 1.0, bassResponse * 0.5);

            // Color per ray
            float hue = fract(fi / float(numRays) + t * 0.05 + energyResponse * 0.3);
            vec3 rayCol = mix(ubo.primaryColor.rgb, ubo.secondaryColor.rgb, hue);
            outsideColor += rayCol * ray * pulse * (0.3 + energyResponse * 0.4);
        }

        // Sparkle / particle burst at the edge
        float burst = exp(-(dist - radius) * (dist - radius) * (30.0 + bassResponse * 50.0));
        vec2 sparkleCoord = vec2(floor(angle * 20.0), floor(dist * 30.0)) + t * 2.0;
        float sparkle = fract(sin(dot(sparkleCoord, vec2(127.1, 311.7))) * 43758.5453);
        sparkle = step(0.7, sparkle) * highResponse;
        outsideColor += mix(ubo.primaryColor.rgb, vec3(1.0), highResponse) * burst * sparkle * 0.8;

        // Ambient glow around the circle edge
        float ambientGlow = exp(-abs(dist - radius) * 10.0) * 0.15;
        outsideColor += mix(ubo.primaryColor.rgb, ubo.secondaryColor.rgb, sin(t * 0.5)) * ambientGlow;
    }

    // ── Compose: circle in front, rays behind ──
    vec3 color = outsideColor;
    color = mix(color, rimColor * 0.8, rim * 0.6);
    color = mix(color, innerColor, circleEdge);

    return clamp(color, 0.0, 1.2);
}

vec4 dispatchMode(int m, vec2 st) {
    if (m == 0) return vec4(renderMode0(st), 1.0);
    if (m == 1) return vec4(renderMode1(st), 1.0);
    if (m == 2) return vec4(renderPlasmaWave(st), 1.0);
    if (m == 3) return vec4(renderRadialBurst(st), 1.0);
    if (m == 4) return vec4(renderGridPulse(st), 1.0);
    if (m == 5) return vec4(renderNoiseFlow(st), 1.0);
    if (m == 6) return vec4(renderCellularVoronoi(st), 1.0);
    if (m == 7) return vec4(renderMandalaSpin(st), 1.0);
    if (m == 8) return vec4(renderTerrainScan(st), 1.0);
    if (m == 9) return vec4(renderWireCube(st), 1.0);
    if (m == 10) return vec4(renderOscilloscope(st), 1.0);
    if (m == 40) {
        vec2 aspectCorrected = (st - 0.5) * vec2(ubo.resolution.x / max(ubo.resolution.y, 1.0), 1.0);
        return renderAnaglyphAssembly(aspectCorrected, ubo.time, ubo.tempo, ubo.energy, ubo.bass, ubo.mid, ubo.high);
    }
    return vec4(0.0);
}

void main() {
    vec2 procUV = applyCamera(uv);
    vec3 video1Color = sampleVideo(uv);
    vec3 video2Color = sampleVideo2(uv);

    float v2Mix = clamp(ubo.video2Mix * ubo.video2Available, 0.0, 1.0);
    vec3 videoColor = blendTwoVideos(video1Color, video2Color, ubo.video2BlendMode, v2Mix);

    // Real crossfade transition: mix between frozen previous frame (old video)
    // and current frame (new video) during a video swap
    if (ubo.transitionProgress < 1.0 && ubo.videoAvailable > 0.0) {
        vec3 oldColor = texture(videoTexPrev, clamp(uv, 0.0, 1.0)).rgb;
        videoColor = mix(oldColor, videoColor, ubo.transitionProgress);
    }

    vec4 procColor = dispatchMode(ubo.mode, procUV);
    vec3 compositedProcedural = procColor.rgb;
    if (ubo.enableBlending == 1) {
        compositedProcedural = applyProceduralBlend(procColor.rgb, videoColor,
                                                   ubo.blendModeProcedural,
                                                   ubo.blendProceduralMix);
    }

    float videoMixAmount = clamp(ubo.videoMix * ubo.videoAvailable, 0.0, 1.0);
    vec3 color = mix(compositedProcedural, videoColor, videoMixAmount);

    if (ubo.enableSharpen == 1 && ubo.sharpenAmount > 0.0001) {
        color = mix(color, sharpen3x3(uv, ubo.sharpenAmount), clamp(ubo.casAmount + ubo.unsharpMask, 0.0, 1.0));
    }

    if (ubo.enablePostGrain == 1 && ubo.grainStrength > 0.0001) {
        float g = fract(sin(dot(uv * ubo.resolution + ubo.time * 60.0, vec2(12.9898, 78.233))) * 43758.5453) - 0.5;
        color += g * ubo.grainStrength * 0.08;
    }

    if (ubo.grayscaleAmount > 0.0001) {
        float l = luminance(color);
        color = mix(color, vec3(l), clamp(ubo.grayscaleAmount, 0.0, 1.0));
    }

    if (ubo.enablePostColorBalance == 1) {
        color *= ubo.colorBalance;
    }

    outColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}