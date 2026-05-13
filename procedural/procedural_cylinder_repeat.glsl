// @EFFECT name="Cylinder Repeat" index=43 desc="Volumetric raymarch with audio-reactive glow" author="System"
// Cylinder Repeat Redux - volumetric raymarch with audio-reactive glow
const float CYL_NEAR_CLIP = 2.8;
const float CYL_FAR_CLIP = 30.0;
const int   CYL_MAX_STEPS = 128;
const float CYL_STEP_MIN = 0.01;
const float CYL_STEP_MULT = 32.0 / float(CYL_MAX_STEPS);
const float CYL_REPEAT_SCALE = 10.0;
const float CYL_HALF_OFFSET = -0.05;
const float CYL_GLOW_GAIN = 10.0;
const float CYL_ASPECT_A = 2.35;
const float CYL_ASPECT_B = 16.0 / 9.0;
const vec3  CYL_CAM_EYE = vec3(7.0, 8.0, 9.0);
const vec3  CYL_CAM_TARGET = vec3(0.0, -10.0, 0.0);
const vec3  CYL_CAM_UP = vec3(0.2, 1.0, 0.0);
const float CYL_JITTER_DIST = 0.5;

float cylRand(vec2 n) {
    return fract(sin(dot(n, vec2(12.9898, 78.233))) * 43758.5453);
}

vec3 cylHash3(vec3 p) {
    p = vec3(dot(p, vec3(127.1, 311.7, 74.7)),
             dot(p, vec3(269.5, 183.3, 246.1)),
             dot(p, vec3(113.5, 271.9, 124.6)));
    return -1.0 + 2.0 * fract(sin(p) * 43758.5453123);
}

vec3 cylTruncate(vec3 p, vec3 levels) {
    return floor(p * levels) / levels;
}

vec2 cylRotate(vec2 v, float angle) {
    float s = sin(angle);
    float c = cos(angle);
    return vec2(v.x * c - v.y * s, v.x * s + v.y * c);
}

float cylSaturateScalar(float v) {
    return clamp(v, 0.0, 1.0);
}

vec3 cylSaturateVec(vec3 v) {
    return clamp(v, vec3(0.0), vec3(1.0));
}

float cylSdSphere(vec3 p, float r) {
    return length(p) - r;
}

vec2 cylHalfCircle(vec3 p, float bass, float mid, float high) {
    vec3 ofs = cylHash3(cylTruncate(p, vec3(13.0)));
    vec3 h2 = cylHash3(cylTruncate(p, vec3(32.0) + ofs));
    vec3 h3 = cylHash3(cylTruncate(p, vec3(16.0)));
    p += 0.02 * h2 + 0.01 * h3;

    float radius = 1.0 + bass * 0.35;
    float d = cylSdSphere(p, radius);
    d = max(d, -cylSdSphere(p, radius * 0.9));
    d = max(d,  p.x - 0.1);
    d = max(d, -p.x - 0.1);

    vec2 result;
    result.x = d + CYL_HALF_OFFSET;
    result.y = step(0.75, min(max(h3.x, h2.x), max(ofs.x, h2.y)) + high * 0.1);
    return result;
}

vec2 cylMinPair(vec2 a, vec2 b) {
    return (a.x < b.x) ? a : b;
}

vec4 cylRadialRepeat(vec3 p, float time, float bass, float mid, float high) {
    vec2 best = vec2(CYL_FAR_CLIP, 0.0);
    vec3 dp = p;

    vec2 rotXZ = cylRotate(dp.xz, 0.3 * time);
    dp.x = rotXZ.x;
    dp.z = rotXZ.y;
    best = cylMinPair(best, cylHalfCircle(dp, bass, mid, high));

    vec2 rotXY = cylRotate(dp.xy, -0.7 * time);
    dp.x = rotXY.x;
    dp.y = rotXY.y;
    best = cylMinPair(best, cylHalfCircle(dp * 1.3, bass, mid, high));

    vec2 rotXZ2 = cylRotate(vec2(dp.x, dp.z), 1.1 * time);
    dp.x = rotXZ2.x;
    dp.z = rotXZ2.y;
    best = cylMinPair(best, cylHalfCircle(dp * 1.8, bass, mid, high));

    vec2 rotXZ3 = cylRotate(vec2(dp.x, dp.z), -1.3 * time);
    dp.x = rotXZ3.x;
    dp.z = rotXZ3.y;
    best = cylMinPair(best, cylHalfCircle(dp * 2.8, bass, mid, high));

    return vec4(best, 0.0, 0.0);
}

vec4 cylScene(vec3 p, float time, float bass, float mid, float high) {
    vec3 local = (p - CYL_CAM_TARGET) / CYL_REPEAT_SCALE;
    vec4 repeat = cylRadialRepeat(local, time, bass, mid, high);
    repeat.x *= CYL_REPEAT_SCALE;
    return repeat;
}

vec4 cylMarchRay(inout vec3 pos, vec3 dir, float time, float bass, float mid, float high,
                 out int stepsTaken, out float minDist) {
    vec4 sample = vec4(0.0);
    float travelled = 0.0;
    minDist = 1e6;
    stepsTaken = 0;
    for (int i = 0; i < CYL_MAX_STEPS; ++i) {
        sample = cylScene(pos, time, bass, mid, high);
        stepsTaken++;
        minDist = min(minDist, sample.x);
        if (sample.x < 0.0 || travelled > CYL_FAR_CLIP) {
            break;
        }
        float stepLen = CYL_STEP_MIN + CYL_STEP_MULT * sample.x;
        pos += dir * stepLen;
        travelled += stepLen;
    }
    return sample;
}

vec3 cylEstimateNormal(vec3 pos, float dist, float time, float bass, float mid, float high) {
    const float eps = 0.35;
    float dx = cylScene(pos + vec3(eps, 0.0, 0.0), time, bass, mid, high).x;
    float dy = cylScene(pos + vec3(0.0, eps, 0.0), time, bass, mid, high).x;
    float dz = cylScene(pos + vec3(0.0, 0.0, eps), time, bass, mid, high).x;
    return normalize(vec3(dx - dist, dy - dist, dz - dist));
}

vec3 cylHsvToRgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

vec3 cylRgbToHsv(vec3 c) {
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));

    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

vec3 cylHsvToRgbSafe(vec3 hsv) {
    return cylSaturateVec(cylHsvToRgb(hsv));
}

vec3 cylRgbToHsvSafe(vec3 rgb) {
    return cylRgbToHsv(cylSaturateVec(rgb));
}

vec4 renderCylinderRepeat(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    // Apply exact raymarched object coordinate system like Walker
    vec2 fragCoord = st * uResolution.xy;

    vec3 forward = normalize(CYL_CAM_TARGET - CYL_CAM_EYE);
    vec3 right = normalize(cross(forward, CYL_CAM_UP));
    vec3 up = normalize(cross(right, forward));

    vec2 centered = fragCoord / uResolution.xy;  // Remove -vec2(0.5) like Walker
    float aspect = mix(CYL_ASPECT_A, CYL_ASPECT_B, 1.0);

    // Apply raymarched object style centering (no Y adjustment)
    vec3 camPos = CYL_CAM_EYE;  // Neutral camera position like raymarched object
    vec3 dir = normalize(forward + right * (centered.x * aspect) + up * centered.y);  // No Y adjustment
    vec3 origin = camPos + dir * CYL_NEAR_CLIP;

    float jitter = cylRand(centered + vec2(fract(time * 0.37)));
    origin -= dir * (4.0 * CYL_JITTER_DIST * jitter);

    vec3 marchPos = origin;
    int steps = 0;
    float minDist;
    vec4 sample = cylMarchRay(marchPos, dir, time, bass, mid, high, steps, minDist);

    bool hit = sample.x < 0.0;
    if (!hit) {
        // Internal colors for background
        vec3 internalBg = mix(vec3(0.02, 0.04, 0.08), vec3(0.06, 0.08, 0.15), cylSaturateScalar(high * 0.6));
        vec3 userTint = mix(uPrimaryColor * 0.1, uSecondaryColor * 0.2, cylSaturateScalar(high * 0.6));
        vec3 bg = mix(internalBg, internalBg * userTint * 3.0, 0.3);
        return vec4(bg, 0.3);
    }

    vec3 normal = cylEstimateNormal(marchPos, sample.x, time, bass, mid, high);

    // Internal colors for base (blue/purple gradient)
    vec3 internalColor1 = vec3(0.2, 0.4, 0.9);
    vec3 internalColor2 = vec3(0.5, 0.2, 0.8);
    vec3 internalBase = mix(internalColor1, internalColor2, cylSaturateScalar(sample.y));
    vec3 baseColor = mix(vec3(0.05, 0.07, 0.09), internalBase, cylSaturateScalar(sample.y));

    float shade = cylSaturateScalar(dot(normal, -dir) * 0.5 + 0.5);
    vec3 color = mix(vec3(0.05, 0.07, 0.09), baseColor, shade);

    float iterationRatio = (float(steps) + 0.5 - jitter) / float(CYL_MAX_STEPS);
    float glow = CYL_GLOW_GAIN * pow(iterationRatio, 2.3);

    // Internal colors for glow
    vec3 internalGlow = mix(vec3(0.3, 0.5, 1.0), vec3(0.6, 0.3, 1.0), cylSaturateScalar(0.5 + high * 0.4));
    vec3 userGlowTint = mix(uPrimaryColor, uSecondaryColor, cylSaturateScalar(0.5 + high * 0.4));
    vec3 glowColor = mix(internalGlow, internalGlow * userGlowTint * 2.0, 0.3);
    color += glow * glowColor * 0.18;

    float vignette = pow(1.5 - length(centered * vec2(2.35, 1.0)), 2.0);
    color *= cylSaturateScalar(vignette);

    vec3 hsv = cylRgbToHsvSafe(color);
    hsv.x = fract(hsv.x - 0.1 * pow(0.9 - st.y, 2.0) - tempo * 0.03);
    hsv.y = clamp(hsv.y + bass * 0.2, 0.0, 1.0);
    hsv.z *= 1.0 + energy * 0.3;
    color = cylHsvToRgbSafe(hsv);

    color *= 1.0 + vec3(bass * 0.25, mid * 0.2, high * 0.35);
    color = cylSaturateVec(color);

    float alpha = clamp(0.6 + length(color) * 0.35 + energy * 0.2, 0.0, 1.0);
    return vec4(color, alpha);
}
