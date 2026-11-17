// @EFFECT name="Anaglyph Assembly" index=40 desc="Stereoscopic anaglyph with assembly/disassembly" author="Leon Denise"
// Audio-reactive stereoscopic anaglyph inspired by Leon Denise's "Anaglyph Quick Sketch"
// Adapted to the Cascade procedural pipeline with assembly/disassembly behaviour similar to the head shader.

const int kAnaglyphLayerCount = 3;        // keep it light on GPU
const int kAnaglyphMarchSteps = 32;       // original step count
const float kAnaglyphRange = 2.0;         // larger structure
const float kAnaglyphRadius = 0.55;       // thicker shapes
const float kAnaglyphBlend = 1.8;
const float kAnaglyphBalance = 1.8;
const float kAnaglyphFalloff = 1.7;
const float kAnaglyphDivergence = 0.12;
const float kAnaglyphFieldOfView = 2.0;   // wider FOV = fills more screen

float anaglyphRandom(vec2 p) {
    return fract(1e4 * sin(17.0 * p.x + p.y * 0.1) * (0.1 + abs(sin(p.y * 13.0 + p.x))));
}

mat2 anaglyphRot(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, -s, s, c);
}

float anaglyphSmoothMin(float a, float b, float r) {
    float h = clamp(0.5 + 0.5 * (b - a) / r, 0.0, 1.0);
    return mix(b, a, h) - r * h * (1.0 - h);
}

float anaglyphSimpleNoise(vec3 p) {
    return fract(sin(dot(p, vec3(12.9898, 78.233, 45.164))) * 43758.5453);
}

// Audio-independent: the anaglyph is driven only by time (always moving).
#define aTime (uTime * 3.0)
#define aBass   (0.5 + 0.5 * sin(aTime * 0.9))
#define aMid    (0.5 + 0.5 * sin(aTime * 0.6 + 1.7))
#define aHigh   (0.5 + 0.5 * sin(aTime * 1.3 + 3.1))
#define aEnergy (0.5 + 0.5 * sin(aTime * 0.4 + 0.8))

float anaglyphAudioEnergy() {
    // Synthetic energy in ~[0.35, 0.85] so the object is always formed and breathing
    return 0.35 + 0.5 * (0.5 + 0.5 * sin(aTime * 0.4));
}

// ── Per-pixel precomputed state ─────────────────────────────────────────────
// Everything below depends only on time, so it is computed ONCE per pixel in
// anaglyphPrecompute() instead of on every SDF evaluation (was ~40 sin/cos per step).
float gAnaAssembly;
mat2  gAnaCamY, gAnaCamX, gAnaCamTwist;
vec3  gAnaStretch;
float gAnaWaveArg, gAnaWaveAdd;
float gAnaDynRange, gAnaDynRadius, gAnaDynBlend, gAnaDynFalloff;
mat2  gAnaRotA[kAnaglyphLayerCount];
mat2  gAnaRotB[kAnaglyphLayerCount];

void anaglyphPrecompute() {
    float bass = aBass, mid = aMid, high = aHigh;
    float audio = anaglyphAudioEnergy();
    float tm = aTime;

    // Assembly: fixed high base so the object is always fully formed
    // (zone thresholds top out at ~0.58, so >= 0.62 is always complete).
    float o1 = sin(tm * 0.28 + audio * 3.0) * 0.15;
    float o2 = sin(tm * 0.43 + bass * 4.0) * 0.10;
    float o3 = cos(tm * 0.19 + mid * 3.0) * 0.08;
    float o4 = sin(tm * 0.67 + high * 5.0) * 0.05;
    gAnaAssembly = clamp(0.72 + o1 + o2 + o3 + o4, 0.62, 0.92);

    // Continuous chaotic camera rotation
    float tiltY = sin(tm * 0.06) * 0.5 + cos(tm * 0.11 + bass) * 0.3 + mid * 0.15;
    float tiltX = cos(tm * 0.08) * 0.4 + sin(tm * 0.17 + high) * 0.25 + audio * 0.15;
    float twist = sin(tm * 0.05 + bass * 1.5) * 0.5 + cos(tm * 0.12 + mid) * 0.3;
    gAnaCamY = anaglyphRot(tiltY);
    gAnaCamX = anaglyphRot(tiltX);
    gAnaCamTwist = anaglyphRot(twist);

    // Stretch
    float stretch = 1.0 + bass * 1.2 + sin(tm * 0.7) * 0.2;
    float squash  = 1.0 / (1.0 + mid * 0.5 + cos(tm * 0.5) * 0.15);
    gAnaStretch = vec3(stretch * 1.3, squash * 1.1,
                       mix(stretch, squash, sin(tm * 0.3) * 0.5 + 0.5) * 1.2);

    // Wave / SDF parameters
    float t = tm * 0.18;
    gAnaWaveArg = t * 2.0 + audio * 2.0;
    gAnaWaveAdd = bass * 0.3 + mid * 0.15;
    t = floor(t) + pow(fract(t), 0.5);

    gAnaDynRange   = kAnaglyphRange * (1.0 + bass * 0.3);
    gAnaDynRadius  = kAnaglyphRadius * (1.0 + mid * 0.2);
    gAnaDynBlend   = kAnaglyphBlend * (0.8 + high * 0.4);
    gAnaDynFalloff = kAnaglyphFalloff + sin(t * 0.5) * 0.2;

    float a = 1.0;
    for (int k = 0; k < kAnaglyphLayerCount; ++k) {
        float i = float(kAnaglyphLayerCount - k);
        gAnaRotA[k] = anaglyphRot(cos(t * 0.5 + bass * i) * kAnaglyphBalance / a + a * 2.0 + t * 0.5);
        gAnaRotB[k] = anaglyphRot(sin(t * 0.5 + mid * i) * kAnaglyphBalance / a + a * 2.0 + t * 0.5);
        a /= gAnaDynFalloff;
    }
}

float anaglyphAssemblyFactor() { return gAnaAssembly; }

float anaglyphCoreGeometry(vec3 pos) {
    pos.yz *= gAnaCamY;
    pos.xz *= gAnaCamX;
    pos.xy *= gAnaCamTwist;
    pos *= gAnaStretch;

    float wave = 1.0 + 0.3 * sin(gAnaWaveArg - length(pos) * 2.0) + gAnaWaveAdd;

    float a = 1.0;
    float scene = 1.0;
    for (int k = 0; k < kAnaglyphLayerCount; ++k) {
        pos.xy *= gAnaRotA[k];
        pos.zy *= gAnaRotB[k];
        pos = abs(pos) - gAnaDynRange * a * wave;
        scene = anaglyphSmoothMin(scene, length(pos) - gAnaDynRadius * a, gAnaDynBlend * a);
        a /= gAnaDynFalloff;
    }
    return scene;
}

float anaglyphZoneThreshold(vec3 pos, float assemblyFactor) {
    float normalizedHeight = clamp((pos.y + 2.5) / 5.0, 0.0, 1.0);
    float threshold = 0.08 + normalizedHeight * 0.4;
    return threshold;
}

float anaglyphMap(vec3 pos) {
    float assemblyFactor = anaglyphAssemblyFactor();
    float threshold = anaglyphZoneThreshold(pos, assemblyFactor);

    if (assemblyFactor <= threshold) {
        return 1000.0;
    }

    float core = anaglyphCoreGeometry(pos);
    float transition = smoothstep(threshold - 0.1, threshold + 0.1, assemblyFactor);
    return mix(1000.0, core, transition);
}

vec3 anaglyphCalcNormal(vec3 pos) {
    const float eps = 0.003;
    vec4 q = vec4(eps, -eps, -eps, 0.0);
    return normalize(vec3(
        anaglyphMap(pos + q.xzz) - anaglyphMap(pos - q.xzz),
        anaglyphMap(pos + q.zxz) - anaglyphMap(pos - q.zxz),
        anaglyphMap(pos + q.zzx) - anaglyphMap(pos - q.zzx)
    ));
}

vec3 anaglyphLook(vec3 eye, vec3 target, vec2 anchor, float fov) {
    vec3 forward = normalize(target - eye);
    vec3 right = normalize(cross(forward, vec3(0.0, 1.0, 0.0)));
    vec3 up = normalize(cross(right, forward));
    return normalize(forward * fov + right * anchor.x + up * anchor.y);
}

vec4 anaglyphShadeEye(vec3 eye, vec3 ray, vec2 anchor) {
    float dither = anaglyphRandom(ray.xy + fract(vec2(aTime)));
    float travel = 0.02 + dither * 0.05;

    for (int i = 0; i < kAnaglyphMarchSteps; ++i) {
        vec3 pos = eye + ray * travel;
        float dist = anaglyphMap(pos);

        if (dist < 0.005) {
            vec3 normal = anaglyphCalcNormal(pos);
            vec3 lightDir = normalize(vec3(-0.6, 0.8, 0.4));
            float diff = max(dot(normal, lightDir), 0.0);

            float assemblyFactor = anaglyphAssemblyFactor();
            vec3 basePalette = mix(uPrimaryColor, uSecondaryColor, clamp(0.25 + assemblyFactor * 0.6, 0.0, 1.0));

            // Brighter, more saturated base color
            vec3 color = basePalette * (0.35 + diff * (0.7 + aEnergy * 0.35));

            // Specular highlight for extra vibrancy
            vec3 viewDir = normalize(-pos);
            vec3 halfDir = normalize(lightDir + viewDir);
            float spec = pow(max(dot(normal, halfDir), 0.0), 16.0);
            color += mix(uPrimaryColor, uSecondaryColor, sin(aTime * 0.5)) * spec * (0.3 + aHigh * 0.4);

            float alpha = clamp(0.6 + diff * 0.3 + assemblyFactor * 0.25, 0.0, 1.0);
            return vec4(clamp(color, 0.0, 1.0), alpha);
        }

        travel += dist * 0.9;
        if (travel > 20.0) {
            break;
        }
    }

    // Pure black background — no color tinting, ever
    return vec4(0.0, 0.0, 0.0, 0.0);
}

vec4 renderAnaglyphAssembly(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    anaglyphPrecompute();
    vec2 anchor = st * 2.0;
    vec3 target = vec3(0.0);

    // Audio-reactive eye divergence — more bass = more stereo separation
    float audioDivergence = kAnaglyphDivergence * (1.0 + aBass * 3.0);
    // Camera closer = object fills more of screen
    vec3 eyeLeft = vec3(-audioDivergence, 0.0, 3.0 + aMid * 0.3);
    vec3 eyeRight = vec3(audioDivergence, 0.0, 3.0 + aMid * 0.3);

    vec3 rayLeft = anaglyphLook(eyeLeft, target, anchor, kAnaglyphFieldOfView);
    vec3 rayRight = anaglyphLook(eyeRight, target, anchor, kAnaglyphFieldOfView);

    vec4 leftSample = anaglyphShadeEye(eyeLeft, rayLeft, anchor);
    vec4 rightSample = anaglyphShadeEye(eyeRight, rayRight, anchor);

    // Build anaglyph: left eye = red channel, right eye = cyan (green+blue)
    vec3 color = vec3(leftSample.r, rightSample.g, rightSample.b);

    // No extra tinting — background is pure black so colors stay clean
    color = clamp(color, 0.0, 1.0);
    // Slightly punchier gamma for more vivid colors (lift midtones)
    color = pow(color, vec3(0.85));

    float alpha = clamp(max(leftSample.a, rightSample.a), 0.0, 1.0);
    return vec4(color, alpha);
}
