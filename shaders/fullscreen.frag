 #version 450

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform UBO {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec2 resolution;
    vec2 videoResolution;
    float time;
    float tempo;
    float energy;
    float bass;
    float mid;
    float high;
    vec4 primaryColor;
    vec4 secondaryColor;
    float colorBlend;
    int mode;
    float videoMix;
    float videoAvailable;
    float grayscaleAmount;
    float sharpenAmount;
    float upscaleEnabled;
    float crtCurvature;
    float crtHorizontalCurvature;
    float crtScanlineIntensity;
    float crtMaskIntensity;
    float crtVignette;
    float crtFishEye;
    float bloomIntensity;
    float bloomThreshold;
    float aberrationAmount;
    float grainStrength;
    float bendAmount;
    float glitchAmount;
    vec3 colorBalance;
    float gradeBrightness;
    float gradeContrast;
    float gradeSaturation;
    float gradeHueShift;
    float gradeGamma;
    int colorLUTIndex;
    float splitToneBalance;
    vec3 splitToneShadows;
    vec3 splitToneHighlights;
    float feedbackAmount;
    float trailStrength;
    float temporalAccumulation;
    float feedbackDecay;
    float recursiveBlend;
    float uvWarpStrength;
    float rippleStrength;
    float rippleFrequency;
    float swirlStrength;
    float displacementAmount;
    float kaleidoSegments;
    float tunnelDepth;
    float tunnelCurvature;
    float gaussianBlur;
    float directionalBlur;
    float directionalBlurAngle;
    float zoomBlur;
    float motionBlur;
    float temporalBlur;
    float unsharpMask;
    float casAmount;
    float localContrast;
    float glitchDatamosh;
    float glitchRGBSplit;
    float glitchScanlineBreak;
    float glitchJitter;
    float glitchTearing;
    float glitchPixelSort;
    float glitchBufferCorruption;
    int blendModeProcedural;
    int blendModeVideo;
    int blendModeFeedback;
    float blendProceduralMix;
    float blendVideoMix;
    float blendFeedbackMix;
    float analogScanlineFocus;
    float analogMaskBalance;
    float analogNoise;
    float analogBloom;
    float vhsDistortion;
    float analogChromaticAberration;
    float audioWarpResponse;
    float audioFeedbackResponse;
    float audioBlurResponse;
    float audioColorResponse;
    float audioGlitchResponse;
    float audioBeatSync;
    float audioLfoRate;
    float temporalInterpolation;
    float temporalBlendStrength;
    float slowMotionFactor;
    float frameAccumulation;
    // NLE Effect Chain parameters
    int nleOutputWidth;
    int nleOutputHeight;
    float nleGrayscale;
    float nleBrightness;
    float nleContrast;
    float nleSaturation;
} ubo;

layout(set = 0, binding = 1) uniform sampler2D videoTex;

const float PI = 3.1415926535;

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 345.45));
    p += dot(p, p + 34.345);
    return fract(p.x * p.y);
}

vec3 unsharpMask(vec2 st, vec3 color) {
    float strength = clamp(ubo.unsharpMask + ubo.sharpenAmount, 0.0, 1.0);
    if (strength <= 0.0001) {
        return color;
    }
    vec2 texel = 1.0 / vec2(textureSize(videoTex, 0));
    vec3 blur = vec3(0.0);
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 offset = vec2(x, y) * texel;
            blur += texture(videoTex, clamp(st + offset, 0.0, 1.0)).rgb;
        }
    }
    blur /= 9.0;
    vec3 highFreq = color - blur;
    // Increased strength multiplier for more visible effect
    return color + highFreq * strength * 3.0;
}

float radialLength(vec2 p) {
    return length(p);
}

vec3 sortComponents(vec3 v) {
    vec3 s = v;
    if (s.x > s.y) {
        s.xy = s.yx;
    }
    if (s.y > s.z) {
        s.yz = s.zy;
    }
    if (s.x > s.y) {
        s.xy = s.yx;
    }
    return s;
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(a, b, u.x) + (c - a) * u.y * (1.0 - u.x) + (d - b) * u.x * u.y;
}

float fbm(vec2 p) {
    float value = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 4; ++i) {
        value += noise(p) * amp;
        p *= 2.0;
        amp *= 0.5;
    }
    return value;
}

float catmullRom(float x) {
    x = abs(x);
    if (x <= 1.0) {
        return 1.5 * x * x * x - 2.5 * x * x + 1.0;
    } else if (x < 2.0) {
        return -0.5 * x * x * x + 2.5 * x * x - 4.0 * x + 2.0;
    }
    return 0.0;
}

vec4 sampleBicubic(vec2 st) {
    vec2 texSize = vec2(textureSize(videoTex, 0));
    vec2 coord = st * texSize - 0.5;
    vec2 base = floor(coord);
    vec2 f = coord - base;

    vec3 color = vec3(0.0);
    for (int j = -1; j <= 2; ++j) {
        float wy = catmullRom(float(j) - f.y);
        for (int i = -1; i <= 2; ++i) {
            float wx = catmullRom(float(i) - f.x);
            vec2 sampleCoord = (base + vec2(i, j) + 0.5) / texSize;
            color += texture(videoTex, sampleCoord).rgb * wx * wy;
        }
    }
    return vec4(color, 1.0);
}

vec3 rgb2yiq(vec3 c) {
    mat3 m = mat3(
        0.299,     0.587,      0.114,
        0.595716, -0.274453, -0.321263,
        0.211456, -0.522591,  0.311135
    );
    return m * c;
}

vec3 yiq2rgb(vec3 c) {
    mat3 m = mat3(
        1.0,  0.9563,  0.6210,
        1.0, -0.2721, -0.6474,
        1.0, -1.1070,  1.7046
    );
    return m * c;
}

vec3 hueShift(vec3 color, float degrees) {
    if (abs(degrees) <= 0.0001) {
        return color;
    }
    vec3 yiq = rgb2yiq(color);
    float angle = radians(degrees);
    float cosA = cos(angle);
    float sinA = sin(angle);
    mat3 rot = mat3(
        1.0, 0.0, 0.0,
        0.0, cosA, -sinA,
        0.0, sinA,  cosA
    );
    yiq = rot * yiq;
    return clamp(yiq2rgb(yiq), 0.0, 2.0);
}

vec3 applyLUT(vec3 color, int index) {
    if (index == 1) {
        color = pow(color, vec3(0.85, 0.95, 1.05));
    } else if (index == 2) {
        color = vec3(color.r * 1.2, color.g * 0.6, color.b * 1.4);
    } else if (index == 3) {
        float gray = dot(color, vec3(0.333));
        color = mix(vec3(gray * 0.9), vec3(gray * 1.1), vec3(0.8, 0.9, 1.1));
    } else if (index == 4) {
        color = vec3(color.r * 1.3, color.g * 0.9, color.b * 0.7);
    } else if (index == 5) {
        color = mix(color, vec3(color.g, color.b, color.r), 0.25);
    }
    return clamp(color, 0.0, 2.0);
}

vec3 applySplitTone(vec3 color) {
    float balance = clamp(ubo.splitToneBalance, 0.0, 1.0);
    if (balance <= 0.0001) {
        return color;
    }
    float lum = dot(color, vec3(0.299, 0.587, 0.114));
    vec3 tone = mix(ubo.splitToneShadows, ubo.splitToneHighlights, smoothstep(0.0, 1.0, lum));
    return mix(color, tone * color, balance);
}

vec2 kaleido(vec2 st, float segments) {
    if (segments <= 2.5) {
        return st;
    }
    vec2 p = st * 2.0 - 1.0;
    float radius = length(p);
    float angle = atan(p.y, p.x);
    float seg = 2.0 * PI / max(segments, 3.0);
    angle = mod(angle, seg);
    angle = abs(angle - seg * 0.5);
    vec2 dir = vec2(cos(angle), sin(angle));
    return dir * radius * 0.5 + 0.5;
}

vec2 applySpatialEffects(vec2 st, vec2 centered, float audioWarpMod) {
    vec2 uvOut = st;
    float warp = ubo.uvWarpStrength * audioWarpMod;
    if (warp > 0.0001) {
        uvOut += sin((uvOut.yx + ubo.time) * 8.0) * warp * 0.01;
    }

    if (ubo.rippleStrength > 0.0001) {
        float radius = length(centered);
        float wave = sin(radius * max(ubo.rippleFrequency, 0.1) * 12.0 - ubo.time * 4.0);
        uvOut += normalize(centered + 0.0001) * wave * ubo.rippleStrength * 0.01;
    }

    if (abs(ubo.swirlStrength) > 0.0001) {
        float angle = ubo.swirlStrength * length(centered) * 5.0;
        float c = cos(angle);
        float s = sin(angle);
        vec2 rotated = vec2(centered.x * c - centered.y * s, centered.x * s + centered.y * c);
        uvOut = rotated * 0.5 + 0.5;
    }

    if (ubo.kaleidoSegments > 0.0) {
        uvOut = kaleido(uvOut, ubo.kaleidoSegments);
    }

    if (ubo.displacementAmount > 0.0001) {
        vec2 n = vec2(fbm(centered * 5.0 + ubo.time), fbm(centered * 7.0 - ubo.time));
        uvOut += (n - 0.5) * ubo.displacementAmount * 0.05;
    }

    if (ubo.tunnelDepth > 0.0001) {
        vec2 p = centered;
        float radius = length(p) + 0.0001;
        float depth = pow(radius, 1.0 - clamp(ubo.tunnelDepth, 0.0, 1.0));
        float curve = 1.0 + ubo.tunnelCurvature * radius * radius;
        p = normalize(p) * depth * curve;
        uvOut = p * 0.5 + 0.5;
    }

    return clamp(uvOut, 0.0, 1.0);
}

vec3 applyGaussianBlur(vec2 st, vec3 baseColor) {
    float strength = clamp(ubo.gaussianBlur, 0.0, 1.0);
    if (strength <= 0.0001) {
        return baseColor;
    }
    vec2 texel = 1.5 / vec2(textureSize(videoTex, 0));
    vec3 acc = baseColor;
    float weight = 1.0;
    const vec2 offsets[4] = vec2[4](
        vec2(1.0, 0.0), vec2(-1.0, 0.0), vec2(0.0, 1.0), vec2(0.0, -1.0)
    );
    for (int i = 0; i < 4; ++i) {
        vec3 sampleCol = texture(videoTex, clamp(st + offsets[i] * texel, 0.0, 1.0)).rgb;
        acc += sampleCol * 0.5;
        weight += 0.5;
    }
    return mix(baseColor, acc / weight, strength);
}

vec3 applyDirectionalBlur(vec2 st, vec3 color) {
    float amount = clamp(ubo.directionalBlur, 0.0, 1.0);
    if (amount <= 0.0001) {
        return color;
    }
    vec2 texel = 1.0 / vec2(textureSize(videoTex, 0));
    float angle = radians(ubo.directionalBlurAngle);
    vec2 dir = vec2(cos(angle), sin(angle));
    vec3 acc = color;
    float weight = 1.0;
    for (int i = 1; i <= 3; ++i) {
        float w = 1.0 / float(i + 1);
        vec2 offset = dir * texel * float(i) * 2.0;
        acc += texture(videoTex, clamp(st + offset, 0.0, 1.0)).rgb * w;
        acc += texture(videoTex, clamp(st - offset, 0.0, 1.0)).rgb * w;
        weight += 2.0 * w;
    }
    return mix(color, acc / weight, amount);
}

vec3 applyZoomBlur(vec2 st, vec3 color, vec2 centered) {
    float amount = clamp(ubo.zoomBlur, 0.0, 1.0);
    if (amount <= 0.0001) {
        return color;
    }
    vec3 acc = color;
    float weight = 1.0;
    for (int i = 1; i <= 4; ++i) {
        float t = float(i) / 4.0;
        vec2 offset = centered * t * 0.1;
        acc += texture(videoTex, clamp(st + offset, 0.0, 1.0)).rgb;
        weight += 1.0;
    }
    return mix(color, acc / weight, amount);
}

vec3 applyMotionBlur(vec2 st, vec3 color, float lfo) {
    float amount = clamp(ubo.motionBlur, 0.0, 1.0);
    if (amount <= 0.0001) {
        return color;
    }
    vec2 texel = 1.0 / vec2(textureSize(videoTex, 0));
    vec2 dir = normalize(vec2(sin(ubo.time * 0.5 + lfo), cos(ubo.time * 0.35 - lfo)));
    vec3 acc = color;
    float weight = 1.0;
    for (int i = 1; i <= 4; ++i) {
        float w = 1.0 / float(i + 1);
        vec2 offset = dir * texel * float(i) * 2.5;
        acc += texture(videoTex, clamp(st + offset, 0.0, 1.0)).rgb * w;
        weight += w;
    }
    return mix(color, acc / weight, amount);
}

vec3 applyTemporalBlur(vec2 st, vec3 color) {
    float amount = clamp(ubo.temporalBlur, 0.0, 1.0);
    if (amount <= 0.0001) {
        return color;
    }
    float jitter = hash21(st + ubo.time) - 0.5;
    vec2 texel = 1.0 / vec2(textureSize(videoTex, 0));
    vec3 sampleColor = texture(videoTex, clamp(st + texel * jitter * 4.0, 0.0, 1.0)).rgb;
    return mix(color, sampleColor, amount);
}

vec3 applyBlurPipeline(vec2 st, vec3 color, vec2 centered, float audioBlurMod, float lfo) {
    vec3 blurred = color;
    blurred = applyGaussianBlur(st, blurred);
    blurred = applyDirectionalBlur(st, blurred);
    blurred = applyZoomBlur(st, blurred, centered);
    blurred = applyMotionBlur(st, blurred, lfo);
    blurred = applyTemporalBlur(st, blurred);
    
    // Check if any blur effect is actually active (independent of audio)
    bool hasBlur = ubo.gaussianBlur > 0.0001 || 
                   ubo.directionalBlur > 0.0001 || 
                   ubo.zoomBlur > 0.0001 || 
                   ubo.motionBlur > 0.0001 || 
                   ubo.temporalBlur > 0.0001;
    
    if (!hasBlur) {
        return color;
    }
    
    // Mix with audio modulation if audio is enabled, otherwise use full blur
    float blurMix = hasBlur ? 1.0 : clamp(audioBlurMod, 0.0, 1.0);
    return mix(color, blurred, blurMix);
}

vec3 applyFeedback(vec2 st, vec3 color, vec2 centered, float audioFeedbackMod) {
    float amount = clamp(ubo.feedbackAmount * audioFeedbackMod, 0.0, 1.0);
    if (amount <= 0.0001) {
        return color;
    }
    
    vec3 accum = color;
    float decay = clamp(1.0 - ubo.feedbackDecay, 0.0, 1.0);
    vec2 texel = 1.0 / vec2(textureSize(videoTex, 0));
    
    // Spatial feedback with increased offsets for better visibility
    for (int i = 1; i <= 6; ++i) {
        float t = float(i) / 6.0;
        
        // Radial trail effect
        vec2 radialOffset = centered * t * ubo.trailStrength * 0.2;
        
        // Vertical temporal accumulation (creates motion blur trails)
        vec2 temporalOffset = vec2(0.0, t * 0.05 * ubo.temporalAccumulation);
        
        // Combine offsets
        vec2 offset = radialOffset + temporalOffset;
        
        // Sample from video texture with offset
        vec3 sampleColor = texture(videoTex, clamp(st - offset, 0.0, 1.0)).rgb;
        
        // Mix with decay - stronger influence from closer samples
        float mixStrength = amount * (1.0 - t * 0.5) * decay;
        accum = mix(accum, sampleColor, mixStrength);
    }
    
    // Apply recursive blend for more intense feedback
    if (ubo.recursiveBlend > 0.0001) {
        // Create additional spatial samples for recursive effect
        vec2 recursiveOffset = centered * ubo.recursiveBlend * 0.3;
        vec3 recursiveSample = texture(videoTex, clamp(st - recursiveOffset, 0.0, 1.0)).rgb;
        accum = mix(accum, recursiveSample, ubo.recursiveBlend * 0.5);
    }
    
    return accum;
}

vec3 applyGlitch(vec2 st, vec3 color, vec2 centered, float audioGlitchMod) {
    // Check if any glitch effect is actually active (independent of audio)
    bool hasGlitch = ubo.glitchDatamosh > 0.0001 || 
                     ubo.glitchJitter > 0.0001 || 
                     ubo.glitchRGBSplit > 0.0001 || 
                     ubo.glitchScanlineBreak > 0.0001 || 
                     ubo.glitchTearing > 0.0001 || 
                     ubo.glitchPixelSort > 0.0001 || 
                     ubo.glitchBufferCorruption > 0.0001;
    
    if (!hasGlitch) {
        return color;
    }
    
    vec2 uv = st;
    // Increased multipliers for more visible effects
    if (ubo.glitchDatamosh > 0.0) {
        uv.x += sin(centered.y * 80.0 + ubo.time * 10.0) * ubo.glitchDatamosh * 0.03;
    }
    if (ubo.glitchJitter > 0.0) {
        uv += (hash21(st * 400.0 + ubo.time) - 0.5) * ubo.glitchJitter * 0.03;
    }
    vec3 glitched = texture(videoTex, clamp(uv, 0.0, 1.0)).rgb;
    if (ubo.glitchRGBSplit > 0.0) {
        vec2 texel = ubo.glitchRGBSplit * 0.01 * vec2(1.0 / textureSize(videoTex, 0));
        glitched.r = texture(videoTex, clamp(uv + texel, 0.0, 1.0)).r;
        glitched.b = texture(videoTex, clamp(uv - texel, 0.0, 1.0)).b;
    }
    if (ubo.glitchScanlineBreak > 0.0) {
        float line = step(0.95, fract(st.y * 200.0 + ubo.time * 5.0));
        glitched *= 1.0 - line * ubo.glitchScanlineBreak;
    }
    if (ubo.glitchTearing > 0.0) {
        float tear = step(0.7, hash21(vec2(st.y * 10.0, ubo.time)));
        glitched = mix(glitched, color, 1.0 - tear * ubo.glitchTearing);
    }
    if (ubo.glitchPixelSort > 0.0) {
        glitched = mix(glitched, sortComponents(glitched), ubo.glitchPixelSort * 0.7);
    }
    if (ubo.glitchBufferCorruption > 0.0) {
        float rnd = hash21(st * 600.0 + ubo.time * 20.0);
        glitched = mix(glitched, vec3(rnd), ubo.glitchBufferCorruption * 0.5);
    }
    
    // Use audio modulation if available, otherwise full intensity
    float intensity = hasGlitch ? 1.0 : clamp(ubo.glitchAmount + ubo.audioGlitchResponse * audioGlitchMod, 0.0, 1.0);
    return mix(color, glitched, intensity);
}

vec3 applyAnalog(vec2 st, vec2 centered, vec3 color, float lfo) {
    vec3 analogColor = color;
    if (ubo.vhsDistortion > 0.0001) {
        float wiggle = sin(st.y * 400.0 + ubo.time * 8.0) * ubo.vhsDistortion * 0.002;
        analogColor = texture(videoTex, clamp(st + vec2(wiggle, 0.0), 0.0, 1.0)).rgb;
    }
    if (ubo.analogNoise > 0.0001) {
        float n = hash21(st * 800.0 + ubo.time * 60.0) - 0.5;
        analogColor += n * ubo.analogNoise * 0.2;
    }
    if (ubo.analogBloom > 0.0001) {
        vec3 bloom = applyGaussianBlur(st, analogColor);
        analogColor = mix(analogColor, bloom, clamp(ubo.analogBloom * 0.5, 0.0, 1.0));
    }
    if (ubo.analogChromaticAberration > 0.0001) {
        vec2 texel = ubo.analogChromaticAberration * centered;
        analogColor.r = texture(videoTex, clamp(st + texel, 0.0, 1.0)).r;
        analogColor.b = texture(videoTex, clamp(st - texel, 0.0, 1.0)).b;
    }
    float scanFocus = clamp(ubo.analogScanlineFocus, 0.0, 1.0);
    if (scanFocus > 0.0001) {
        float scan = mix(1.0, 0.4 + 0.6 * sin((st.y + lfo * 0.01) * 3.14159 * 400.0), scanFocus);
        analogColor *= scan;
    }
    if (ubo.analogMaskBalance > 0.0001) {
        float mask = mix(1.0,
                         (0.7 + 0.3 * sin(st.x * 3.14159 * 960.0)) *
                         (0.7 + 0.3 * cos(st.y * 3.14159 * 540.0)),
                         ubo.analogMaskBalance);
        analogColor *= mask;
    }
    return analogColor;
}

vec3 applyColorGrade(vec3 color, float audioColorMod) {
    // Early return if color grading is effectively disabled
    bool hasBrightness = abs(ubo.gradeBrightness) > 0.0001;
    bool hasContrast = abs(ubo.gradeContrast - 1.0) > 0.0001;
    bool hasSaturation = abs(ubo.gradeSaturation - 1.0) > 0.0001;
    bool hasHue = abs(ubo.gradeHueShift) > 0.0001;
    bool hasGamma = abs(ubo.gradeGamma - 1.0) > 0.0001;
    bool hasLUT = ubo.colorLUTIndex > 0;
    bool hasSplitTone = ubo.splitToneBalance > 0.0001;
    
    if (!hasBrightness && !hasContrast && !hasSaturation && !hasHue && !hasGamma && !hasLUT && !hasSplitTone) {
        return color;
    }
    
    color += ubo.gradeBrightness;
    color = (color - 0.5) * ubo.gradeContrast + 0.5;
    float lum = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3(lum), color, ubo.gradeSaturation);
    color = hueShift(color, ubo.gradeHueShift + ubo.audioColorResponse * audioColorMod * 45.0);
    color = pow(max(color, vec3(0.0)), vec3(1.0 / max(ubo.gradeGamma, 0.05)));
    color = applyLUT(color, ubo.colorLUTIndex);
    color = applySplitTone(color);
    return clamp(color, 0.0, 2.0);
}

vec3 applySharpen(vec2 st, vec3 color) {
    vec3 sharpened = color;
    sharpened = unsharpMask(st, sharpened);
    if (ubo.casAmount > 0.0001) {
        sharpened = mix(color, sharpened, clamp(ubo.casAmount, 0.0, 1.0));
    }
    if (ubo.localContrast > 0.0001) {
        float localLum = dot(sharpened, vec3(0.299, 0.587, 0.114));
        // Increased multiplier for more visible local contrast effect
        sharpened += (sharpened - vec3(localLum)) * ubo.localContrast * 2.0;
    }
    return sharpened;
}

vec3 blendMode(vec3 base, vec3 layer, int mode) {
    if (mode == 0) {
        return base + layer;
    } else if (mode == 1) {
        return 1.0 - (1.0 - base) * (1.0 - layer);
    } else if (mode == 2) {
        return base * layer;
    } else if (mode == 3) {
        return mix(base, 2.0 * base * layer + base * base * (1.0 - 2.0 * layer), 0.5);
    } else if (mode == 4) {
        return abs(base - layer);
    } else if (mode == 5) {
        vec3 exprLow = 2.0 * base * layer + base * base * (1.0 - 2.0 * layer);
        vec3 exprHigh = sqrt(base) * (2.0 * layer - 1.0) + 2.0 * base * (1.0 - layer);
        vec3 selector = step(vec3(0.5), layer);
        return mix(exprLow, exprHigh, selector);
    }
    return layer;
}

vec4 renderMode0(vec2 st) {
    return mix(ubo.primaryColor, ubo.secondaryColor, ubo.colorBlend);
}

vec4 renderMode1(vec2 st) {
    return vec4(st, 0.5 + 0.5 * sin(ubo.time), 1.0);
}

vec4 dispatchMode(int m, vec2 st) {
    if (m == 0) return renderMode0(st);
    if (m == 1) return renderMode1(st);
    return vec4(0.0);
}

void main() {
    float audioEnv = clamp(ubo.energy, 0.0, 1.0);
    float audioWarpMod = 1.0 + ubo.audioWarpResponse * audioEnv;
    float audioFeedbackMod = 1.0 + ubo.audioFeedbackResponse * audioEnv;
    float audioBlurMod = clamp(ubo.audioBlurResponse * audioEnv, 0.0, 1.0);
    float audioColorMod = clamp(ubo.audioColorResponse * audioEnv, 0.0, 1.0);
    float audioGlitchMod = clamp(ubo.audioGlitchResponse * audioEnv, 0.0, 1.0);
    float lfo = sin(ubo.time * max(ubo.audioLfoRate, 0.05) * PI * 2.0 + ubo.audioBeatSync * ubo.tempo);

    float timeScale = max(ubo.slowMotionFactor, 0.1);
    float effectTime = ubo.time / timeScale;

    vec2 centered = uv * 2.0 - 1.0;

    // Stretch to fit: video always fills full screen without letterboxing
    vec2 letterboxUV = uv;
    
    vec2 spatialUV = applySpatialEffects(letterboxUV, centered, audioWarpMod);
    vec2 spatialCentered = spatialUV * 2.0 - 1.0;

    float curvatureY = clamp(ubo.crtCurvature, 0.0, 0.8);
    float curvatureX = clamp(ubo.crtHorizontalCurvature, 0.0, 0.8);
    vec2 distorted = spatialCentered;
    if (curvatureY > 0.0001 || curvatureX > 0.0001) {
        distorted.x = spatialCentered.x / (1.0 + curvatureX * spatialCentered.x * spatialCentered.x);
        distorted.y = spatialCentered.y / (1.0 + curvatureY * spatialCentered.y * spatialCentered.y);
    }
    float radius = length(spatialCentered);
    if (ubo.crtFishEye != 0.0) {
        distorted *= 1.0 + ubo.crtFishEye * radius * radius;
    }
    if (ubo.bendAmount > 0.0001) {
        float chaos = sin((spatialCentered.x + spatialCentered.y) * 60.0 + effectTime * 20.0);
        distorted += vec2(chaos, cos(effectTime * 10.0 + spatialCentered.y * 50.0)) * ubo.bendAmount * 0.05;
    }
    vec2 crtUV = clamp(distorted * 0.5 + 0.5, 0.0, 1.0);

    vec4 procedural = dispatchMode(ubo.mode, crtUV);
    vec3 videoColor = mix(texture(videoTex, crtUV).rgb,
                          sampleBicubic(crtUV).rgb,
                          clamp(ubo.upscaleEnabled, 0.0, 1.0));

    videoColor = applySharpen(crtUV, videoColor);
    vec3 blurred = applyBlurPipeline(crtUV, videoColor, spatialCentered, audioBlurMod, lfo);
    videoColor = mix(videoColor, blurred, clamp(ubo.temporalBlendStrength, 0.0, 1.0));

    vec3 feedbackColor = applyFeedback(crtUV, videoColor, spatialCentered, audioFeedbackMod);
    vec3 glitchedColor = applyGlitch(crtUV, feedbackColor, spatialCentered, audioGlitchMod);
    vec3 analogColor = applyAnalog(crtUV, spatialCentered, glitchedColor, lfo);
    vec3 gradedColor = applyColorGrade(analogColor, audioColorMod);

    float bright = max(max(gradedColor.r, gradedColor.g), gradedColor.b);
    if (ubo.bloomIntensity > 0.0001) {
        float threshold = clamp(ubo.bloomThreshold, 0.0, 1.0);
        float mask = smoothstep(threshold, 1.0, bright);
        vec3 bloom = applyGaussianBlur(crtUV, gradedColor);
        gradedColor += bloom * mask * ubo.bloomIntensity * 0.5;
    }

    float scanline = mix(1.0, 0.5 + 0.5 * sin((crtUV.y + effectTime * 0.2) * PI * 240.0), clamp(ubo.crtScanlineIntensity, 0.0, 1.0));
    float maskPattern = mix(1.0,
                            (0.8 + 0.2 * sin(crtUV.x * PI * 640.0)) *
                            (0.8 + 0.2 * cos(crtUV.y * PI * 480.0)),
                            clamp(ubo.crtMaskIntensity, 0.0, 1.0));
    gradedColor *= scanline * maskPattern;

    if (abs(ubo.aberrationAmount) > 0.0001) {
        vec2 texel = ubo.aberrationAmount * spatialCentered;
        gradedColor.r = texture(videoTex, clamp(crtUV + texel, 0.0, 1.0)).r;
        gradedColor.b = texture(videoTex, clamp(crtUV - texel, 0.0, 1.0)).b;
    }

    gradedColor = mix(gradedColor,
                      gradedColor * (1.0 - pow(clamp(radius, 0.0, 1.0), 2.0)),
                      clamp(ubo.crtVignette, 0.0, 1.0));

    if (ubo.grainStrength > 0.0001) {
        float g = hash21(crtUV * 2000.0 + effectTime * 30.0) - 0.5;
        gradedColor += g * ubo.grainStrength * 0.1;
    }

    vec3 videoAvailableColor = mix(procedural.rgb, gradedColor, clamp(ubo.videoMix * ubo.videoAvailable, 0.0, 1.0));

    // Apply NLE Effect Chain parameters
    if (ubo.nleGrayscale > 0.5) {
        float luma = dot(videoAvailableColor, vec3(0.299, 0.587, 0.114));
        videoAvailableColor = mix(videoAvailableColor, vec3(luma), 1.0);
    }
    if (ubo.nleBrightness != 0.0 || ubo.nleContrast != 1.0 || ubo.nleSaturation != 1.0) {
        videoAvailableColor += ubo.nleBrightness;
        videoAvailableColor = (videoAvailableColor - 0.5) * ubo.nleContrast + 0.5;
        float lum = dot(videoAvailableColor, vec3(0.2126, 0.7152, 0.0722));
        videoAvailableColor = mix(vec3(lum), videoAvailableColor, ubo.nleSaturation);
    }

    vec3 blendProc = blendMode(videoAvailableColor, procedural.rgb, ubo.blendModeProcedural);
    vec3 blendVideo = blendMode(procedural.rgb, gradedColor, ubo.blendModeVideo);
    vec3 blendFeedback = blendMode(videoAvailableColor, feedbackColor, ubo.blendModeFeedback);

    vec3 layered = mix(videoAvailableColor, blendProc, clamp(ubo.blendProceduralMix, 0.0, 1.0));
    layered = mix(layered, blendVideo, clamp(ubo.blendVideoMix, 0.0, 1.0));
    layered = mix(layered, blendFeedback, clamp(ubo.blendFeedbackMix + ubo.frameAccumulation, 0.0, 1.0));

    vec3 finalColor = mix(procedural.rgb, layered, clamp(ubo.videoAvailable, 0.0, 1.0));
    finalColor *= ubo.colorBalance;
    finalColor = clamp(finalColor, 0.0, 1.5);

    float finalLuma = dot(finalColor, vec3(0.299, 0.587, 0.114));
    finalColor = mix(finalColor, vec3(finalLuma), clamp(ubo.grayscaleAmount, 0.0, 1.0));

    outColor = vec4(finalColor, 1.0);
}
