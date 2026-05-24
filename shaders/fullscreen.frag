#version 450

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0, std140) uniform GlobalParamsUBO {
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

    float audioWarpResponse;
    float audioFeedbackResponse;
    float audioBlurResponse;
    float audioColorResponse;
    float audioGlitchResponse;
    float audioBeatSync;
    float audioLfoRate;

    vec4 primaryColor;
    vec4 secondaryColor;
    float colorBlend;
    int mode;
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
    float grayscaleAmount;

    float crtCurvature;
    float crtHorizontalCurvature;
    float crtScanlineIntensity;
    float crtMaskIntensity;
    float crtVignette;
    float crtFishEye;
    float analogScanlineFocus;
    float analogMaskBalance;
    int enablePostCrtCurvature;
    int enablePostScanMask;
    int enablePostVignette;
    int enablePostFishEye;

    float glitchAmount;
    float glitchDatamosh;
    float glitchRGBSplit;
    float glitchScanlineBreak;
    float glitchJitter;
    float glitchTearing;
    float glitchPixelSort;
    float glitchBufferCorruption;
    float aberrationAmount;
    int enablePostGlitch;
    int enablePostAberration;

    float feedbackAmount;
    float trailStrength;
    float temporalAccumulation;
    float feedbackDecay;
    float recursiveBlend;
    float frameAccumulation;
    float slowMotionFactor;
    float temporalInterpolation;
    int enableFeedback;
    int enableTemporal;

    float bloomIntensity;
    float bloomThreshold;
    int enablePostBloom;

    float uvWarpStrength;
    float rippleStrength;
    float rippleFrequency;
    float swirlStrength;
    float displacementAmount;
    float kaleidoSegments;
    float tunnelDepth;
    float tunnelCurvature;
    float bendAmount;
    int enableDistortion;
    int enablePostBend;

    float gaussianBlur;
    float directionalBlur;
    float directionalBlurAngle;
    float zoomBlur;
    float motionBlur;
    float temporalBlur;
    int enableBlurMotion;

    float unsharpMask;
    float casAmount;
    float localContrast;
    float sharpenAmount;
    int enableSharpen;

    float videoMix;
    float videoAvailable;
    int blendModeVideo;
    float blendVideoMix;

    float video2Mix;
    float video2Available;
    int video2BlendMode;

    int blendModeProcedural;
    int blendModeFeedback;
    float blendProceduralMix;
    float blendFeedbackMix;
    int enableBlending;

    float grainStrength;
    int enablePostGrain;

    float upscaleEnabled;
    int enablePostColorBalance;
    int enableColorGrading;
    int enableAnalog;
    int enableAudioReactive;

    float pixelateAmount;
    float strobeSpeed;
    float thresholdLevel;
    float slowZoomAmount;
    int enablePixelate;
    int enableStrobe;
    int enableThreshold;
    int enableSlowZoom;
    int enableEdgeDetect;
    float edgeStrength;
    float edgeThreshold;
    float edgeBlend;
    vec3 edgeColor;
    int enableMirror;
    int enableInvert;
    int enablePosterize;
    int enableInfrared;
    int enableZoomPulse;
    int enableRGBShift;
    float mirrorAmount;
    float posterizeLevels;
    float zoomPulseAmount;
    float rgbShiftAmount;

    int nleOutputWidth;
    int nleOutputHeight;
    float nleGrayscale;
    float nleBrightness;
    float nleContrast;
    float nleSaturation;

    int enableFXAA;
    float fxaaQualitySubpix;
    float fxaaQualityEdgeThreshold;
    float fxaaQualityEdgeThresholdMin;

    int enableGrid;
    int gridMode;
    int gridCount;
    int gridRows;
    int gridColumns;
    int gridMirrorCells;
    int gridShowLines;
    float gridLineWidth;
    float gridLineIntensity;
    vec3 gridLineColor;
} ubo;

layout(set = 0, binding = 1) uniform sampler2D inputTexture;

vec2 mirrorCell(vec2 p, vec2 gridSize, int mirrorCells) {
    vec2 cell = floor(p * gridSize);
    vec2 f = fract(p * gridSize);

    if (mirrorCells == 1) {
        if (int(mod(cell.x, 2.0)) != 0) f.x = 1.0 - f.x;
        if (int(mod(cell.y, 2.0)) != 0) f.y = 1.0 - f.y;
    }

    return f;
}

vec2 applyGrid(vec2 p) {
    if (ubo.enableGrid != 1) return p;

    if (ubo.gridMode == 0 && ubo.gridCount > 1) {
        // Duplicar horizontalmente (N columnas)
        float x = p.x * float(ubo.gridCount);
        float cellX = floor(x);
        p.x = fract(x);
        if (ubo.gridMirrorCells == 1 && int(cellX) % 2 != 0)
            p.x = 1.0 - p.x;

    } else if (ubo.gridMode == 1 && ubo.gridCount > 1) {
        // Duplicar verticalmente (N filas)
        float y = p.y * float(ubo.gridCount);
        float cellY = floor(y);
        p.y = fract(y);
        if (ubo.gridMirrorCells == 1 && int(cellY) % 2 != 0)
            p.y = 1.0 - p.y;

    } else if (ubo.gridMode == 2 && ubo.gridRows > 0 && ubo.gridColumns > 0) {
        // Matriz de filas × columnas
        float x = p.x * float(ubo.gridColumns);
        float y = p.y * float(ubo.gridRows);
        float cellX = floor(x);
        float cellY = floor(y);
        p.x = fract(x);
        p.y = fract(y);
        if (ubo.gridMirrorCells == 1) {
            if (int(cellX) % 2 != 0) p.x = 1.0 - p.x;
            if (int(cellY) % 2 != 0) p.y = 1.0 - p.y;
        }
    }

    return clamp(p, 0.0, 1.0);
}

void main() {
    vec2 sampleUV = uv;

    if (ubo.enableGrid == 1) {
        if (ubo.gridMode == 0 && ubo.gridCount > 1) {
            float x = uv.x * float(ubo.gridCount);
            sampleUV.x = fract(x);
            if (ubo.gridMirrorCells == 1 && int(floor(x)) % 2 != 0)
                sampleUV.x = 1.0 - sampleUV.x;

        } else if (ubo.gridMode == 1 && ubo.gridCount > 1) {
            float y = uv.y * float(ubo.gridCount);
            sampleUV.y = fract(y);
            if (ubo.gridMirrorCells == 1 && int(floor(y)) % 2 != 0)
                sampleUV.y = 1.0 - sampleUV.y;

        } else if (ubo.gridMode == 2 && ubo.gridRows > 0 && ubo.gridColumns > 0) {
            float x = uv.x * float(ubo.gridColumns);
            float y = uv.y * float(ubo.gridRows);
            sampleUV.x = fract(x);
            sampleUV.y = fract(y);
            if (ubo.gridMirrorCells == 1) {
                if (int(floor(x)) % 2 != 0) sampleUV.x = 1.0 - sampleUV.x;
                if (int(floor(y)) % 2 != 0) sampleUV.y = 1.0 - sampleUV.y;
            }
        }
    }

    fragColor = texture(inputTexture, clamp(sampleUV, 0.0, 1.0));
}