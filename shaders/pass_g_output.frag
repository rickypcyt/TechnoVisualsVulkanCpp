#version 450

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0, std140) uniform GlobalParamsUBO {
    mat4 model; mat4 view; mat4 proj;
    vec2 resolution; vec2 videoResolution;
    float time; float tempo; float energy; float bass; float mid; float high;
    float audioWarpResponse; float audioFeedbackResponse; float audioBlurResponse;
    float audioColorResponse; float audioGlitchResponse; float audioBeatSync;
    float audioLfoRate;

    vec4 primaryColor; vec4 secondaryColor; float colorBlend; int mode;
    vec3 colorBalance; float gradeBrightness; float gradeContrast; float gradeSaturation;
    float gradeHueShift; float gradeGamma; int colorLUTIndex; float splitToneBalance;
    vec3 splitToneShadows; vec3 splitToneHighlights; float grayscaleAmount;

    float crtCurvature; float crtHorizontalCurvature; float crtScanlineIntensity;
    float crtMaskIntensity; float crtVignette; float crtFishEye; float analogScanlineFocus;
    float analogMaskBalance; int enablePostCrtCurvature; int enablePostScanMask;
    int enablePostVignette; int enablePostFishEye;

    float glitchAmount; float glitchDatamosh; float glitchRGBSplit; float glitchScanlineBreak;
    float glitchJitter; float glitchTearing; float glitchPixelSort; float glitchBufferCorruption;
    float aberrationAmount; int enablePostGlitch; int enablePostAberration;

    float feedbackAmount; float trailStrength; float temporalAccumulation; float feedbackDecay;
    float recursiveBlend; float frameAccumulation; float slowMotionFactor; float temporalInterpolation;
    int enableFeedback; int enableTemporal;

    float bloomIntensity; float bloomThreshold; int enablePostBloom;

    float uvWarpStrength; float rippleStrength; float rippleFrequency; float swirlStrength;
    float displacementAmount; float kaleidoSegments; float tunnelDepth; float tunnelCurvature;
    float bendAmount; int enableDistortion; int enablePostBend;

    float gaussianBlur; float directionalBlur; float directionalBlurAngle; float zoomBlur;
    float motionBlur; float temporalBlur; int enableBlurMotion;

    float unsharpMask; float casAmount; float localContrast; float sharpenAmount; int enableSharpen;

    float videoMix; float videoAvailable; int blendModeVideo; float blendVideoMix;

    float video2Mix; float video2Available; int video2BlendMode;

    int blendModeProcedural; int blendModeFeedback; float blendProceduralMix; float blendFeedbackMix;
    int enableBlending;

    float grainStrength; int enablePostGrain;

    float upscaleEnabled; int enablePostColorBalance; int enableColorGrading; int enableAnalog;
    int enableAudioReactive;

    float pixelateAmount; float strobeSpeed; float thresholdLevel; float slowZoomAmount;
    int enablePixelate; int enableStrobe; int enableThreshold; int enableSlowZoom;
    int enableEdgeDetect; float edgeStrength; float edgeThreshold; float edgeBlend; vec3 edgeColor;
    int enableMirror; int enableInvert; int enablePosterize; int enableInfrared; int enableZoomPulse;
    int enableRGBShift; float mirrorAmount; float posterizeLevels; float zoomPulseAmount; float rgbShiftAmount;

    int nleOutputWidth; int nleOutputHeight; float nleGrayscale; float nleBrightness; float nleContrast; float nleSaturation;

    int enableFXAA; float fxaaQualitySubpix; float fxaaQualityEdgeThreshold; float fxaaQualityEdgeThresholdMin;

    float cameraZoom; float cameraPanX; float cameraPanY; float cameraRotation; int enableCameraMovement;

    int enableGrid; int gridMode; int gridCount; int gridRows; int gridColumns; int gridMirrorCells;
    vec3 rgbOverlay; int enableRgbOverlay;
} ubo;

layout(set = 1, binding = 0) uniform sampler2D inputTex;
layout(set = 1, binding = 1) uniform sampler2D proceduralTex;

const float PI = 3.1415926535;

float hash21(vec2 p) {
    p = fract(p * vec2(443.897, 441.423));
    p += dot(p, p.yx + 19.19);
    return fract(p.x * p.y);
}

float luminance(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

vec2 applyGrid(vec2 p) {
    vec2 uvGrid = p;
    if (ubo.enableGrid == 1) {
        if (ubo.gridMode == 0 && ubo.gridCount > 1) {
            float x = uvGrid.x * float(ubo.gridCount);
            uvGrid.x = fract(x);
            if (ubo.gridMirrorCells == 1 && int(floor(x)) % 2 != 0) {
                uvGrid.x = 1.0 - uvGrid.x;
            }
        } else if (ubo.gridMode == 1 && ubo.gridCount > 1) {
            float y = uvGrid.y * float(ubo.gridCount);
            uvGrid.y = fract(y);
            if (ubo.gridMirrorCells == 1 && int(floor(y)) % 2 != 0) {
                uvGrid.y = 1.0 - uvGrid.y;
            }
        } else if (ubo.gridMode == 2 && ubo.gridRows > 0 && ubo.gridColumns > 0) {
            vec2 gridSize = vec2(float(ubo.gridColumns), float(ubo.gridRows));
            vec2 scaled = uvGrid * gridSize;
            vec2 cell = floor(scaled);
            vec2 f = fract(scaled);
            if (ubo.gridMirrorCells == 1) {
                if (int(mod(cell.x, 2.0)) != 0) f.x = 1.0 - f.x;
                if (int(mod(cell.y, 2.0)) != 0) f.y = 1.0 - f.y;
            }
            uvGrid = f;
        }
    }
    return clamp(uvGrid, 0.0, 1.0);
}

vec3 sampleInput(vec2 p) {
    return texture(inputTex, applyGrid(p)).rgb;
}

/* blur 9-tap */
vec3 blur3x3(vec2 p) {
    vec2 t = 1.0 / max(ubo.resolution, vec2(1.0));
    vec3 c = vec3(0.0);
    c += sampleInput(p + t*vec2(-1,-1));
    c += sampleInput(p + t*vec2( 0,-1));
    c += sampleInput(p + t*vec2( 1,-1));
    c += sampleInput(p + t*vec2(-1, 0));
    c += sampleInput(p);
    c += sampleInput(p + t*vec2( 1, 0));
    c += sampleInput(p + t*vec2(-1, 1));
    c += sampleInput(p + t*vec2( 0, 1));
    c += sampleInput(p + t*vec2( 1, 1));
    return c / 9.0;
}

/* FXAA (reads neighborhood via sampleInput) */
vec3 fxaa_compose(vec2 uv) {
    vec2 texel = 1.0 / max(ubo.resolution, vec2(1.0));
    vec3 rgbNW = sampleInput(uv + vec2(-1.0, -1.0) * texel);
    vec3 rgbNE = sampleInput(uv + vec2( 1.0, -1.0) * texel);
    vec3 rgbSW = sampleInput(uv + vec2(-1.0,  1.0) * texel);
    vec3 rgbSE = sampleInput(uv + vec2( 1.0,  1.0) * texel);
    vec3 rgbM  = sampleInput(uv);
    float lumaNW = luminance(rgbNW);
    float lumaNE = luminance(rgbNE);
    float lumaSW = luminance(rgbSW);
    float lumaSE = luminance(rgbSE);
    float lumaM  = luminance(rgbM);
    float lumaMin = min(lumaM, min(min(lumaNW,lumaNE), min(lumaSW,lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW,lumaNE), max(lumaSW,lumaSE)));
    if ((lumaMax - lumaMin) < max(ubo.fxaaQualityEdgeThresholdMin, lumaMax * ubo.fxaaQualityEdgeThreshold)) return rgbM;
    vec2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));
    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * 0.25 * ubo.fxaaQualitySubpix, 0.125);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcpDirMin, vec2(-8.0), vec2(8.0));
    vec3 rgbA = 0.5 * (sampleInput(uv + dir * texel * (1.0/3.0 - 0.5)) + sampleInput(uv + dir * texel * (2.0/3.0 - 0.5)));
    vec3 rgbB = rgbA * 0.5 + 0.25 * (sampleInput(uv + dir * texel * -0.5) + sampleInput(uv + dir * texel * 0.5));
    float lumaB = luminance(rgbB);
    return (lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB;
}

/* Sobel edge (returns scalar strength in .r) */
float sobelEdgeStrength(vec2 p) {
    vec2 t = 1.0 / max(ubo.resolution, vec2(1.0));
    float tl = luminance(sampleInput(p + t * vec2(-1.0,  1.0)));
    float  t0 = luminance(sampleInput(p + t * vec2( 0.0,  1.0)));
    float tr = luminance(sampleInput(p + t * vec2( 1.0,  1.0)));
    float  l = luminance(sampleInput(p + t * vec2(-1.0,  0.0)));
    float  r = luminance(sampleInput(p + t * vec2( 1.0,  0.0)));
    float bl = luminance(sampleInput(p + t * vec2(-1.0, -1.0)));
    float  b = luminance(sampleInput(p + t * vec2( 0.0, -1.0)));
    float br = luminance(sampleInput(p + t * vec2( 1.0, -1.0)));
    float gx = -tl - 2.0 * l - bl + tr + 2.0 * r + br;
    float gy = -tl - 2.0 * t0 - tr + bl + 2.0 * b + br;
    return length(vec2(gx, gy)) * 0.125;
}

/* Blend modes (kept simple) */
vec3 blendMode(vec3 base, vec3 layer, int mode) {
    if (mode == 0) return clamp(base + layer, 0.0, 1.5);
    if (mode == 1) return 1.0 - (1.0 - base) * (1.0 - layer);
    if (mode == 2) return base * layer;
    if (mode == 3) return mix(base, 2.0 * base * layer + base * base * (1.0 - 2.0 * layer), 0.5);
    if (mode == 4) return abs(base - layer);
    if (mode == 5) {
        vec3 low = 2.0 * base * layer + base * base * (1.0 - 2.0 * layer);
        vec3 high = sqrt(max(base, vec3(0.0))) * (2.0 * layer - 1.0) + 2.0 * base * (1.0 - layer);
        return mix(low, high, step(vec3(0.5), layer));
    }
    return layer;
}

void main() {
    float timeScale = max(ubo.slowMotionFactor, 0.1);
    float t = ubo.time / timeScale;
    vec2 centered = uv * 2.0 - 1.0;

    // Base samples
    vec3 color = sampleInput(uv);
    vec3 procColor = texture(proceduralTex, uv).rgb;

    // BLOOM (cheap): bright mask -> blur -> add
    if (ubo.enablePostBloom == 1 && ubo.bloomIntensity > 0.0001) {
        float bright = max(max(color.r, color.g), color.b);
        float mask = smoothstep(clamp(ubo.bloomThreshold, 0.0, 1.0), 1.0, bright);
        vec3 b = blur3x3(uv);
        color += b * mask * clamp(ubo.bloomIntensity * 0.4, 0.0, 2.0);
    }

    // ANALOG scanlines / mask
    if (ubo.enableAnalog == 1) {
        if (ubo.analogScanlineFocus > 0.0001) {
            float scan = 0.4 + 0.6 * sin((uv.y + sin(t * 0.01) * 0.01) * PI * 400.0);
            color *= mix(1.0, scan, ubo.analogScanlineFocus);
        }
        if (ubo.analogMaskBalance > 0.0001) {
            float mask = (0.7 + 0.3 * sin(uv.x * PI * 960.0)) * (0.7 + 0.3 * cos(uv.y * PI * 540.0));
            color *= mix(1.0, mask, ubo.analogMaskBalance);
        }
    }

    // CRT scanline mask
    if (ubo.enablePostScanMask == 1) {
        float scanlineFreq = 240.0 * (ubo.resolution.y / 480.0);
        float scan = 0.5 + 0.5 * sin((uv.y + t * 0.2) * PI * scanlineFreq);
        color *= mix(1.0, scan, clamp(ubo.crtScanlineIntensity, 0.0, 1.0));
    }

    // EDGE detect overlay
    if (ubo.enableEdgeDetect == 1 && (ubo.edgeStrength > 0.0001 || ubo.edgeBlend > 0.0001)) {
        float edge = sobelEdgeStrength(uv) * ubo.edgeStrength;
        float thr = clamp(ubo.edgeThreshold, 0.0, 1.0);
        float e = smoothstep(thr, thr + 0.5, edge);
        vec3 edgeTint = mix(vec3(e), ubo.edgeColor * e, clamp(ubo.edgeBlend, 0.0, 1.0));
        color = mix(color, edgeTint, e);
    }

    // Blend with procedural (two-stage modes)
    vec3 blendProc = blendMode(color, procColor, ubo.blendModeProcedural);
    vec3 blendVideo = blendMode(procColor, color, ubo.blendModeVideo);
    color = mix(color, blendProc, clamp(ubo.blendProceduralMix, 0.0, 1.0));
    color = mix(color, blendVideo, clamp(ubo.blendVideoMix, 0.0, 1.0));

    // Strobe (brightness pulsar)
    if (ubo.enableStrobe == 1 && ubo.strobeSpeed > 0.0001) {
        float freq = max(ubo.strobeSpeed, 0.0001);
        float wave = sin(t * freq * PI * 2.0);
        float pulse = step(0.0, wave);
        float strength = clamp(freq / 10.0, 0.1, 1.0);
        vec3 dark = mix(color, vec3(0.0), strength);
        vec3 flash = clamp(color * (1.0 + strength * 4.0), 0.0, 3.0);
        color = mix(dark, flash, pulse);
    }

    // Final grayscale mix
    float luma = dot(color, vec3(0.299, 0.587, 0.114));
    color = mix(color, vec3(luma), clamp(ubo.grayscaleAmount, 0.0, 1.0));

    // Post color balance (optional)
    if (ubo.enablePostColorBalance == 1) {
        color *= ubo.colorBalance;
    }

    // Clamp before FXAA to keep samples stable
    color = clamp(color, 0.0, 1.5);

    // FXAA: if enabled, apply to the composed color by sampling the same input neighborhood.
    // Note: for perfect results FXAA should run on final render target; this approximates by
    // using sampleInput which reads inputTex neighborhood — acceptable for single-pass setups.
    if (ubo.enableFXAA == 1) {
        // Write temporary into texture memory is not possible here; we approximate by using fxaa_compose on uv
        vec3 aa = fxaa_compose(uv);
        color = mix(color, aa, 0.9); // blend to avoid oversharpening; tune as desired
    }

    // Zoom pulse (temporal zoom in/out)
    if (ubo.enableZoomPulse == 1 && ubo.zoomPulseAmount > 0.0001) {
        float wave = sin(t * 2.0 * PI);
        float amt = clamp(ubo.zoomPulseAmount, 0.0, 1.0);
        float zoom = 1.0 - amt * 0.4 * wave;
        zoom = max(zoom, 0.2);
        vec2 zoomUV = (uv - 0.5) / zoom + 0.5;
        vec3 zoomColor = sampleInput(zoomUV);
        color = mix(color, zoomColor, clamp(abs(wave) * amt * 1.5, 0.0, 1.0));
    }

    // RGB shift (chromatic aberration style)
    if (ubo.enableRGBShift == 1 && ubo.rgbShiftAmount > 0.0001) {
        float shift = clamp(ubo.rgbShiftAmount, -0.5, 0.5);
        vec2 texel = 1.0 / max(ubo.resolution, vec2(1.0));
        vec2 offset = vec2(shift) * texel * 50.0;
        float r = texture(inputTex, applyGrid(uv + offset)).r;
        float g = texture(inputTex, applyGrid(uv)).g;
        float b = texture(inputTex, applyGrid(uv - offset)).b;
        color = mix(color, vec3(r, g, b), clamp(abs(shift) * 8.0, 0.0, 1.0));
    }

    // Posterize
    if (ubo.enablePosterize == 1 && ubo.posterizeLevels > 1.0) {
        float levels = clamp(ubo.posterizeLevels, 2.0, 64.0);
        color = floor(color * levels) / levels;
    }

    // Final RGB overlay
    if (ubo.enableRgbOverlay == 1) {
        color *= clamp(ubo.rgbOverlay, 0.0, 2.0);
    }

    outColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}