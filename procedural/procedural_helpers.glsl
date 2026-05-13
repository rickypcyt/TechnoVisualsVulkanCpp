const float kVoxelFallInterval = 100.0;
const float kVoxelHeightRangeBase = 5.0;
const float kVoxelBumpFactorBase = 2.0;
const float kVoxelFovDegrees = 60.0;
const float kVoxelBpm = 114.0;
const int kVoxelSamples = 3;
const int kVoxelMaxDepth = 3;

float hash13(vec3 p) {
    return fract(sin(dot(p, vec3(12.9898, 78.233, 37.719))) * 43758.5453);
}

float hash11f(float x) {
    return fract(sin(x) * 43758.5453);
}

float hash12f(vec2 p) {
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

float voxelRandom(inout float seed) {
    seed = fract(seed * 43758.5453123 + 0.12345);
    return seed;
}

mat2 voxelRotate(float a) {
    float s = sin(a);
    float c = cos(a);
    return mat2(c, -s, s, c);
}

vec3 voxelHSV(float h, float s, float v) {
    vec3 res = fract(h + vec3(0.0, 2.0, 1.0) / 3.0);
    res = clamp(abs(res * 6.0 - 3.0) - 1.0, 0.0, 1.0);
    res = (res - 1.0) * s + 1.0;
    res *= v;
    return res;
}

float voxelMap(vec3 p, vec3 camPos, float time, float fallSpeed, float heightRange, out vec3 ID) {
    float res = 1.0;
    ID = floor(p);
    float h = hash12f(ID.xz);
    ID.y -= floor(max(ID.x * ID.x * 0.1, 5.0) - h * heightRange);
    float T = time * fallSpeed / kVoxelFallInterval + hash11f(h);
    if (ID.y > 0.0) {
        float tmp = ID.y / kVoxelFallInterval + T;
        if (fract(tmp) * kVoxelFallInterval > 1.0) {
            res = 0.0;
        }
        ID.y = floor(tmp);
    } else {
        ID.y += floor(T);
    }

    vec3 offset = p + 0.5 - camPos;
    if (dot(offset, offset) < 1.0) {
        res = 0.0;
    }
    return res;
}

float voxelCastRay(vec3 ro, vec3 rd, int itr, vec3 camPos, float time, float fallSpeed, float heightRange,
                   out vec3 ID, out vec3 normal, out vec3 pos, out float seed) {
    pos = floor(ro);
    vec3 ri = 1.0 / rd;
    vec3 rs = sign(rd);
    vec3 dis = (pos - ro + 0.5 + rs * 0.5) * ri;

    float res = -1.0;
    vec3 stepDir = vec3(0.0);

    for (int i = 0; i < itr; ++i) {
        if (voxelMap(pos, camPos, time, fallSpeed, heightRange, ID) > 0.5) {
            res = 1.0;
            break;
        }
        vec3 mm = step(dis.xyz, dis.yzx) * step(dis.xyz, dis.zxy);
        stepDir = mm * rs;
        dis += stepDir * ri;
        pos += stepDir;
    }

    normal = -stepDir;
    vec3 mini = (pos - ro + 0.5 - 0.5 * rs) * ri;
    float t = max(mini.x, max(mini.y, mini.z));

    seed = hash13(ID);

    return t * res;
}

float voxelEmission(float seed) {
    return step(hash11f(seed), 0.08) * 10.0;
}

vec3 voxelObjectColor(float seed) {
    return voxelHSV(hash11f(seed), 0.6, 1.0);
}

float voxelBumpFunc(vec3 p, vec3 n, float seed) {
    float nSeed = dot(abs(n), vec3(1.0, 2.0, 4.0));
    vec2 uv = abs(n.x) > 0.5 ? p.yz : (abs(n.y) > 0.5 ? p.xz : p.xy);
    uv = fract(uv) - 0.5;

    if (hash11f(seed + nSeed) < 0.5) {
        uv.y = -uv.y;
    }
    if (uv.y < -uv.x) {
        uv = -uv.yx;
    }
    float d = abs(length(uv - 0.5) - 0.5);
    const float w = 0.15;
    float tmp = w * w - d * d;
    return tmp > 0.0 ? -sqrt(tmp) : 0.0;
}

vec3 voxelBumpMap(vec3 p, vec3 n, float seed, float bumpFactor) {
    const vec2 e = vec2(0.002, 0.0);
    float ref = voxelBumpFunc(p, n, seed);
    vec3 grad = (vec3(voxelBumpFunc(p - e.xyy, n, seed),
                      voxelBumpFunc(p - e.yxy, n, seed),
                      voxelBumpFunc(p - e.yyx, n, seed)) - ref) / e.x;
    grad -= n * dot(n, grad);
    return normalize(n + grad * bumpFactor);
}

vec3 voxelJitter(vec3 v, float phi, float sinTheta, float cosTheta) {
    vec3 xAxis = normalize(cross(abs(v.yzx) + 0.001, v));
    vec3 yAxis = cross(v, xAxis);
    vec3 zAxis = v;
    return (xAxis * cos(phi) + yAxis * sin(phi)) * sinTheta + zAxis * cosTheta;
}

vec3 voxelPathTrace(vec3 ro, vec3 rd, float time, float fallSpeed, float heightRange, float bumpFactor,
                    float bass, float mid, float high, inout float pathSeed) {
    vec3 acc = vec3(0.0);
    vec3 mask = vec3(1.0);

    vec3 ID;
    vec3 normal;
    vec3 pos;
    float seed;

    float t = voxelCastRay(ro, rd, 60, ro, time, fallSpeed, heightRange, ID, normal, pos, seed);
    if (t < 0.0) {
        return vec3(0.0);
    }
    ro += t * rd;

    vec3 f = ro - pos - 0.5;
    vec3 n = normalize(f * pow(abs(f), vec3(8.0)) + 0.0001);
    n = normalize(n + voxelBumpMap(ro, normal, seed, bumpFactor));

    acc += mask * voxelEmission(seed) * float(kVoxelSamples);
    mask *= voxelObjectColor(seed);

    vec3 ro0 = ro + n * 0.0008;
    vec3 n0 = n;
    vec3 mask0 = mask;
    for (int i = 0; i < kVoxelSamples; ++i) {
        ro = ro0;
        n = n0;
        mask = mask0;
        for (int depth = 1; depth < kVoxelMaxDepth; ++depth) {
            float ur = voxelRandom(pathSeed);
            rd = voxelJitter(n, voxelRandom(pathSeed) * kTwoPI, sqrt(1.0 - ur), sqrt(ur));

            t = voxelCastRay(ro, rd, 15, ro, time, fallSpeed, heightRange, ID, normal, pos, seed);
            if (t < 0.0) {
                break;
            }
            ro += t * rd;

            f = ro - pos - 0.5;
            n = normalize(f * pow(abs(f), vec3(8.0)) + 0.0001);

            float emissionBoost = 1.0 + bass * 1.2 + high * 0.8;
            acc += mask * voxelEmission(seed) * emissionBoost;
            mask *= voxelObjectColor(seed);
            ro += n * 0.0008;
        }
    }

    acc /= float(kVoxelSamples);
    return clamp(acc, 0.0, 1.0);
}

const float kVolIterations = 17.0;
const int kVolSteps = 20;
const float kVolStepSize = 0.1;
const float kVolZoom = 0.8;
const float kVolTile = 0.85;
const float kVolSpeed = 0.01;
const float kVolBrightness = 0.0015;
const float kVolDarkMatter = 0.3;
const float kVolDistFading = 0.73;
const float kVolSaturation = 0.85;
const float kVolFormuParam = 0.53;
const int kSnowLayers = 8;
const vec2 kSnowOffset = vec2(0.02, -0.2);

float happyStar(vec2 uv, float anim) {
    uv = abs(uv);
    vec2 denom = max(abs(uv.yx), vec2(0.0005));
    vec2 pos = min(uv.xy / denom, vec2(anim));
    float p = 2.0 - pos.x - pos.y;
    float denomSum = max(uv.x + uv.y, 0.0005);
    return (2.0 + p * (p * p - 1.5)) / denomSum;
}

float voronoi(vec2 p, out float edge, out float cellSeed) {
    vec2 n = floor(p);
    vec2 f = fract(p);
    float min1 = 10.0;
    float min2 = 10.0;
    vec2 best = vec2(0.0);

    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 g = vec2(float(x), float(y));
            vec2 offset = hash2(n + g);
            vec2 r = g + offset - f;
            float d = dot(r, r);
            if (d < min1) {
                min2 = min1;
                min1 = d;
                best = g + offset;
            } else if (d < min2) {
                min2 = d;
            }
        }
    }

    edge = min2 - min1;
    cellSeed = hash(n + best);
    return sqrt(min1);
}

float mapScene(vec3 p, float time, float energy, float bass, float mid, float high) {
    vec3 q = p;
    q.xz = rotate(q.xz, time * 0.4 + bass * 0.8);
    q.xy = rotate(q.xy, time * 0.25 + mid * 0.5);
    float displacement = fbm(q.xz * (2.2 + high * 0.6) + time * 0.3) * (0.12 + energy * 0.15);
    float sphere = length(q) - (0.65 + bass * 0.35) - displacement;
    float torus = length(vec2(length(q.xz) - (0.9 + mid * 0.4), q.y)) - (0.23 + high * 0.12) - displacement * 0.5;
    return min(sphere, torus);
}

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

// Common helper functions for procedural shaders
float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float hash31(vec3 p) {
    p = fract(p * vec3(123.34, 456.21, 789.32));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y * p.z);
}

float map(float value, float min1, float max1, float min2, float max2) {
    return min2 + (value - min1) * (max2 - min2) / (max1 - min1);
}

vec3 hsb2rgb(vec3 c) {
    vec3 rgb = clamp(abs(mod(c.x * 6.0 + vec3(0.0, 4.0, 2.0), 6.0) - 3.0) - 1.0, 0.0, 1.0);
    rgb = rgb * rgb * (3.0 - 2.0 * rgb);
    return c.z * mix(vec3(1.0), rgb, c.y);
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

vec2 rotate2D(vec2 p, float angle) {
    float c = cos(angle);
    float s = sin(angle);
    return vec2(p.x * c - p.y * s, p.x * s + p.y * c);
}

vec3 sphericalToRectangular(float rho, float theta, float phi) {
    float phiRad = radians(phi);
    float thetaRad = radians(theta);
    return vec3(
        rho * sin(phiRad) * sin(thetaRad),
        rho * cos(phiRad),
        rho * sin(phiRad) * cos(thetaRad)
    );
}

vec3 permute(vec3 x) { return mod(((x*34.0)+1.0)*x, 289.0); }

vec4 taylorInvSqrt(vec4 r) {
    return 1.79284291400159 - 0.85373472095314 * r;
}

float snoise(vec2 v) {
    const vec4 C = vec4(0.211324865405187, 0.366025403784439,
            -0.577350269189626, 0.024390243902439);
    vec2 i  = floor(v + dot(v, C.yy) );
    vec2 x0 = v -   i + dot(i, C.xx);
    vec2 i1;
    i1 = (x0.x > x0.y) ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
    vec4 x12 = x0.xyxy + C.xxzz;
    x12.xy -= i1;
    i = mod(i, 289.0);
    vec3 p = permute( permute( i.y + vec3(0.0, i1.y, 1.0 ))
        + i.x + vec3(0.0, i1.x, 1.0 ));
    vec3 m = max(0.5 - vec3(dot(x0,x0), dot(x12.xy,x12.xy), dot(x12.zw,x12.zw)), 0.0);
    m = m*m ;
    m = m*m ;
    vec3 x = 2.0 * fract(p * C.www) - 1.0;
    vec3 h = abs(x) - 0.5;
    vec3 ox = floor(x + 0.5);
    vec3 a0 = x - ox;
    m *= 1.79284291400159 - 0.85373472095314 * ( a0*a0 + h*h );
    vec3 g;
    g.x  = a0.x  * x0.x  + h.x  * x0.y;
    g.yz = a0.yz * x12.xz + h.yz * x12.yw;
    return 130.0 * dot(m, g);
}
