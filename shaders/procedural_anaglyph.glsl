// @EFFECT name="Anaglyph Assembly" index=40 desc="Stereoscopic anaglyph with assembly/disassembly" author="Leon Denise"
// Audio-reactive stereoscopic anaglyph inspired by Leon Denise's "Anaglyph Quick Sketch"
// Adapted to the Cascade procedural pipeline with assembly/disassembly behaviour similar to the head shader.

const int kAnaglyphLayerCount = 3;
const int kAnaglyphMarchSteps = 32;
const float kAnaglyphRange = 1.0;
const float kAnaglyphRadius = 0.3;
const float kAnaglyphBlend = 1.5;
const float kAnaglyphBalance = 1.5;
const float kAnaglyphFalloff = 1.9;
const float kAnaglyphDivergence = 0.08;
const float kAnaglyphFieldOfView = 1.2;

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

float anaglyphAudioEnergy() {
    // Stronger audio response with bass emphasis, but keep base shape visible
    float energy = uBass * 0.6 + uMid * 0.3 + uHigh * 0.15;
    return max(energy, 0.22); // minimum ensures the object is always partially formed
}

float anaglyphAssemblyFactor() {
    // More dramatic assembly/disassembly range
    return smoothstep(0.05, 0.95, anaglyphAudioEnergy());
}

vec3 anaglyphApplyCamera(vec3 pos) {
    // Much stronger audio-reactive camera movement
    float audioDrive = anaglyphAudioEnergy() * 2.0;
    float tiltY = -PI * 0.25 + (sin(uTime * 0.35) + uMid * 1.5 + audioDrive) * 0.35;
    float tiltX = -PI * 0.5 + (cos(uTime * 0.27) + uBass * 2.0 + audioDrive * 0.5) * 0.35;
    float twist = sin(uTime * 0.18 + uHigh * 3.0 + uBass * 2.0) * 0.6;

    pos.yz *= anaglyphRot(tiltY);
    pos.xz *= anaglyphRot(tiltX);
    pos.xy *= anaglyphRot(twist);
    return pos;
}

float anaglyphCoreGeometry(vec3 pos) {
    pos = anaglyphApplyCamera(pos);
    float a = 1.0;
    float scene = 1.0;
    float t = uTime * 0.2;
    float wave = 1.0 + 0.35 * sin(t * 8.0 - length(pos) * 2.0 + anaglyphAudioEnergy() * 4.0) + uBass * 0.3;
    t = floor(t) + pow(fract(t), 0.5);

    for (int i = kAnaglyphLayerCount; i > 0; --i) {
        float rotSeed = cos(t) * kAnaglyphBalance / a + a * 2.0 + t;
        pos.xy *= anaglyphRot(rotSeed);
        pos.zy *= anaglyphRot(sin(t) * kAnaglyphBalance / a + a * 2.0 + t);
        pos = abs(pos) - kAnaglyphRange * a * wave;
        scene = anaglyphSmoothMin(scene, length(pos) - kAnaglyphRadius * a, kAnaglyphBlend * a);
        a /= kAnaglyphFalloff;
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
    float dither = anaglyphRandom(ray.xy + fract(vec2(uTime)));
    float travel = 0.02 + dither * 0.05;

    for (int i = 0; i < kAnaglyphMarchSteps; ++i) {
        vec3 pos = eye + ray * travel;
        float dist = anaglyphMap(pos);

        if (dist < 0.005) {
            vec3 normal = anaglyphCalcNormal(pos);
            vec3 lightDir = normalize(vec3(-0.6, 0.8, 0.4));
            float diff = max(dot(normal, lightDir), 0.0);

            float assemblyFactor = anaglyphAssemblyFactor();
            vec3 basePalette = mix(uPrimaryColor, uSecondaryColor, clamp(0.35 + assemblyFactor * 0.5, 0.0, 1.0));

            vec3 color = basePalette * (0.3 + diff * (0.9 + uEnergy * 0.4));

            // Removed fog effect for better sharpness/quality

            float alpha = clamp(0.5 + diff * 0.3 + assemblyFactor * 0.3, 0.0, 1.0);
            return vec4(clamp(color, 0.0, 1.0), alpha);
        }

        travel += dist * 0.9;
        if (travel > 12.0) {
            break;
        }
    }

    float assemblyFactor = anaglyphAssemblyFactor();
    float horizon = clamp(anchor.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 bg = mix(uPrimaryColor * 0.1, uSecondaryColor * 0.25, horizon);
    bg += vec3(0.05, 0.08, 0.12) * (0.8 - clamp(length(anchor), 0.0, 1.2)) * (0.3 + assemblyFactor * 0.5);

    return vec4(clamp(bg, 0.0, 1.0), 0.15 + assemblyFactor * 0.2);
}

vec4 renderAnaglyphAssembly(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    vec2 anchor = st * 2.0;
    vec3 target = vec3(0.0);

    // Audio-reactive eye divergence — more bass = more stereo separation
    float audioDivergence = kAnaglyphDivergence * (1.0 + uBass * 2.0);
    vec3 eyeLeft = vec3(-audioDivergence, 0.0, 5.0 + uMid * 0.5);
    vec3 eyeRight = vec3(audioDivergence, 0.0, 5.0 + uMid * 0.5);

    vec3 rayLeft = anaglyphLook(eyeLeft, target, anchor, kAnaglyphFieldOfView);
    vec3 rayRight = anaglyphLook(eyeRight, target, anchor, kAnaglyphFieldOfView);

    vec4 leftSample = anaglyphShadeEye(eyeLeft, rayLeft, anchor);
    vec4 rightSample = anaglyphShadeEye(eyeRight, rayRight, anchor);

    vec3 color = vec3(leftSample.r, rightSample.g, rightSample.b);

    float assemblyFactor = anaglyphAssemblyFactor();
    // Audio-reactive color boost
    color += vec3(0.12, 0.08, 0.15) * assemblyFactor * (0.3 + uEnergy * 0.5);
    color = clamp(color, 0.0, 1.0);
    color = pow(color, vec3(1.0 / 2.2));

    float alpha = clamp(max(leftSample.a, rightSample.a), 0.0, 1.0);
    return vec4(color, alpha);
}
