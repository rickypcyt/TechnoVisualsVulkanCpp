 #version 450

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform UBO {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec2 resolution;
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
    vec3 colorBalance;
} ubo;

layout(set = 0, binding = 1) uniform sampler2D videoTex;

float catmullRom(float x) {
    x = abs(x);
    if (x <= 1.0) {
        return 1.5 * x * x * x - 2.5 * x * x + 1.0;
    } else if (x < 2.0) {
        return -0.5 * x * x * x + 2.5 * x * x - 4.0 * x + 2.0;
    }
    return 0.0;
}

vec4 sampleBicubic(vec2 uv) {
    vec2 texSize = vec2(textureSize(videoTex, 0));
    vec2 coord = uv * texSize - 0.5;
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

vec3 unsharpMask(vec2 uv, vec3 color) {
    float strength = clamp(ubo.sharpenAmount, 0.0, 1.0);
    if (strength <= 0.0001) {
        return color;
    }

    vec2 texel = 1.0 / vec2(textureSize(videoTex, 0));
    vec3 blur = vec3(0.0);
    blur += sampleBicubic(uv + texel * vec2(-1, -1)).rgb;
    blur += sampleBicubic(uv + texel * vec2( 0, -1)).rgb;
    blur += sampleBicubic(uv + texel * vec2( 1, -1)).rgb;
    blur += sampleBicubic(uv + texel * vec2(-1,  0)).rgb;
    blur += color;
    blur += sampleBicubic(uv + texel * vec2( 1,  0)).rgb;
    blur += sampleBicubic(uv + texel * vec2(-1,  1)).rgb;
    blur += sampleBicubic(uv + texel * vec2( 0,  1)).rgb;
    blur += sampleBicubic(uv + texel * vec2( 1,  1)).rgb;
    blur /= 9.0;

    vec3 highFreq = color - blur;
    return color + highFreq * strength * 1.5;
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
    vec2 centered = uv * 2.0 - 1.0;
    float curvatureY = clamp(ubo.crtCurvature, 0.0, 0.8);
    float curvatureX = clamp(ubo.crtHorizontalCurvature, 0.0, 0.8);
    float radius = length(centered);
    vec2 distorted = centered;
    if (curvatureY > 0.0001 || curvatureX > 0.0001) {
        distorted.x = centered.x / (1.0 + curvatureX * centered.x * centered.x);
        distorted.y = centered.y / (1.0 + curvatureY * centered.y * centered.y);
    }
    float fish = clamp(ubo.crtFishEye, -1.0, 1.0);
    if (abs(fish) > 0.0001) {
        float factor = 1.0 + fish * radius * radius;
        distorted *= factor;
    }
    vec2 crtUV = (distorted + 1.0) * 0.5;

    vec4 procedural = dispatchMode(ubo.mode, crtUV);
    vec3 baseVideo = mix(texture(videoTex, crtUV).rgb,
                         sampleBicubic(crtUV).rgb,
                         clamp(ubo.upscaleEnabled, 0.0, 1.0));
    baseVideo = unsharpMask(crtUV, baseVideo);

    float scanline = mix(1.0, 0.5 + 0.5 * sin((crtUV.y + ubo.time * 0.5) * 3.14159 * 240.0), clamp(ubo.crtScanlineIntensity, 0.0, 1.0));
    float maskPattern = mix(1.0,
                            (0.8 + 0.2 * sin(crtUV.x * 3.14159 * 640.0)) *
                            (0.8 + 0.2 * cos(crtUV.y * 3.14159 * 480.0)),
                            clamp(ubo.crtMaskIntensity, 0.0, 1.0));
    vec3 video = baseVideo * scanline * maskPattern;

    float vignette = mix(1.0, 1.0 - pow(clamp(radius, 0.0, 1.0), 2.0), clamp(ubo.crtVignette, 0.0, 1.0));
    video *= vignette;

    vec3 balanced = video * ubo.colorBalance;
    float luminance = dot(balanced, vec3(0.299, 0.587, 0.114));
    vec3 videoFx = mix(balanced, vec3(luminance), clamp(ubo.grayscaleAmount, 0.0, 1.0));
    vec3 videoMix = mix(procedural.rgb, videoFx, clamp(ubo.videoMix, 0.0, 1.0));
    vec3 mixed = mix(procedural.rgb, videoMix, clamp(ubo.videoAvailable, 0.0, 1.0));
    outColor = vec4(mixed, 1.0);
}
