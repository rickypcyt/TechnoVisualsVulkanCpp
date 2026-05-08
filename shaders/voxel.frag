  #version 450

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform GlobalUBO {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 resolutionTime;
    float tempo;
    float energy;
    float bass;
    float mid;
    float high;
    vec4 primaryColor;
    vec4 secondaryColor;
    float colorBlend;
    int mode;
} ubo;

const float kVoxelBpm = 120.0;
const float kVoxelHeightRangeBase = 4.5;
const float kVoxelBumpFactorBase = 1.2;
const float kVoxelFovDegrees = 45.0;

mat2 voxelRotate(float angle) {
    float c = cos(angle);
    float s = sin(angle);
    return mat2(c, -s, s, c);
}

float hash12f(vec2 p) {
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

bool traceVoxels(vec3 ro, vec3 rd, out vec3 hitPos, out vec3 hitNormal) {
    vec3 pos = floor(ro);
    vec3 stepDir = sign(rd);
    vec3 safeRd = max(abs(rd), vec3(1e-5));
    vec3 tDelta = abs(1.0 / safeRd);
    vec3 tMax;

    for (int i = 0; i < 3; ++i) {
        if (rd[i] > 0.0) {
            tMax[i] = (ceil(ro[i]) - ro[i]) / rd[i];
        } else {
            tMax[i] = (ro[i] - floor(ro[i])) / -rd[i];
        }
    }

    for (int i = 0; i < 128; ++i) {
        vec3 cell = pos;
        float occupancy = step(0.5, fract(sin(cell.x * 12.989 + cell.z * 78.233) * 43758.5453));
        if (cell.y < 0.0) {
            occupancy = 1.0;
        }
        if (occupancy > 0.5) {
            float step = min(tMax.x, min(tMax.y, tMax.z));
            hitPos = ro + rd * step;
            hitNormal = vec3(0.0);
            if (tMax.x < tMax.y && tMax.x < tMax.z) hitNormal = vec3(-stepDir.x, 0.0, 0.0);
            else if (tMax.y < tMax.z) hitNormal = vec3(0.0, -stepDir.y, 0.0);
            else hitNormal = vec3(0.0, 0.0, -stepDir.z);
            return true;
        }

        if (tMax.x < tMax.y) {
            if (tMax.x < tMax.z) {
                pos.x += stepDir.x;
                tMax.x += tDelta.x;
            } else {
                pos.z += stepDir.z;
                tMax.z += tDelta.z;
            }
        } else {
            if (tMax.y < tMax.z) {
                pos.y += stepDir.y;
                tMax.y += tDelta.y;
            } else {
                pos.z += stepDir.z;
                tMax.z += tDelta.z;
            }
        }
    }

    return false;
}

vec3 voxelPathTrace(vec3 camPos, vec3 rd, float time, float fallSpeed, float heightRange, float bumpFactor,
                    float bass, float mid, float high, float seed) {
    vec3 hitPos;
    vec3 normal;
    if (traceVoxels(camPos, rd, hitPos, normal)) {
        vec3 lightDir = normalize(vec3(0.4, 1.0, 0.3));
        float diff = max(dot(normal, lightDir), 0.0);
        float dist = length(hitPos - camPos);
        float fog = 1.0 / (1.0 + dist * 0.05);

        return vec3(0.2, 0.8, 0.3) * diff * fog + vec3(0.05, 0.07, 0.1) * (1.0 - diff);
    }
    return vec3(0.0);
}

vec4 renderVoxelPathTracer(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    vec2 uv = st;

    vec2 res = ubo.resolutionTime.xy;

    float tempoFactor = max(0.4, tempo * 0.8 + 0.4);
    float fallSpeed = (kVoxelBpm / 60.0) * (0.6 + tempoFactor * 0.6 + energy * 0.4);
    float heightRange = kVoxelHeightRangeBase * (1.0 + energy * 0.6 + high * 0.5);
    float bumpFactor = kVoxelBumpFactorBase * (1.0 + high * 0.7);

    vec3 camPos = vec3(
        0.5 + sin(time * 0.25) * tempoFactor * 0.35,
        6.0 + energy * 3.0,
        -time * fallSpeed * 1.4 - 3.0
    );

    camPos.x += cos(time * 0.3) * bass * 1.4;
    camPos.z += sin(time * 0.21) * mid * 0.9;

    vec2 p = (gl_FragCoord.xy / res) * 2.0 - 1.0;
    p.x *= res.x / res.y;

    vec3 dir = normalize(vec3(0.3, -0.4, -1.0));
    dir.xy = voxelRotate(time * 0.18 + tempoFactor * 0.3) * dir.xy;
    vec3 side = normalize(cross(dir, vec3(0.0, 1.0, 0.0)));
    vec3 up = cross(side, dir);
    float fovScale = 1.0 / tan(radians(kVoxelFovDegrees) * 0.5);
    vec3 rd = normalize(p.x * side + p.y * up + dir * fovScale);

    float pathSeed = hash12f(p * res + time * 13.37) * 500.0;

    vec3 col = voxelPathTrace(camPos, rd, time, fallSpeed, heightRange, bumpFactor, bass, mid, high, pathSeed);
    col = pow(col, vec3(0.4545));

    vec3 base = mix(ubo.primaryColor.rgb, ubo.secondaryColor.rgb, clamp(ubo.colorBlend, 0.0, 1.0));
    col = mix(base, col, 0.7);
    float luminance = clamp(dot(col, vec3(0.3333)), 0.0, 1.0);
    float alpha = clamp(0.35 + luminance * 0.4 + energy * 0.2, 0.0, 1.0);
    return vec4(col, alpha);
}

void main() {
    vec2 st = gl_FragCoord.xy / ubo.resolutionTime.xy;
    outColor = renderVoxelPathTracer(st, ubo.resolutionTime.z, ubo.tempo, ubo.energy, ubo.bass, ubo.mid, ubo.high);
}
