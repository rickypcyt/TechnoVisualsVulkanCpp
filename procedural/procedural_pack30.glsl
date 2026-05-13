// @EFFECT name="Recursive Cube Bloom" index=55 desc="Pulsing cubic lattice with ray-marched glow" author="John Ao (ShaderToy)"

const float RCC_EPS = 0.001;
const float RCC_R = 0.07;
const int RCC_MAX_STEPS = 80;
const int RCC_AA = 12;

float rcc_sdf(vec3 p) {
    p = fract(p + 0.5);
    p = min(p, 1.0 - p);
    p *= p;
    return sqrt(p.x < p.y ? p.x + min(p.y, p.z) : p.y + min(p.x, p.z)) - RCC_R;
}

float rcc_sum3(vec3 x) {
    return x.x + x.y + x.z;
}

float rcc_max3(vec3 x) {
    return max(x.x, max(x.y, x.z));
}

float rcc_min3(vec3 x) {
    return min(x.x, min(x.y, x.z));
}

float rcc_cube_intersect(vec3 o, vec3 d) {
    vec3 a = 1.0 / d;
    vec3 b = -o * a;
    vec3 c = abs(a) * 0.5;
    float t1 = rcc_max3(b - c);
    float t2 = rcc_min3(b + c);
    return (0.0 < t1 && t1 < t2) ? t1 : -1.0;
}

float rcc_random2(vec2 seed) {
    return fract(1000.0 * sin(seed.x * 12345.0 + seed.y) * sin(seed.y * 1234.0 + seed.x));
}

vec4 renderRecursiveCubeBloom(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    vec2 fragCoord = (st * 0.5 + 0.5) * uResolution;
    vec2 localResolution = uResolution;
    float animTime = time * (0.8 + tempo * 0.05) + bass * 0.4;

    vec3 camDir = normalize(vec3(sin(animTime), cos(animTime), sin(animTime * 0.7 + 1.0)));
    vec3 xAxis = normalize(cross(vec3(0.0, 0.0, 1.0), camDir));
    if (length(xAxis) < 0.001) {
        xAxis = vec3(1.0, 0.0, 0.0);
    }
    vec3 yAxis = cross(camDir, xAxis);
    vec3 cam = camDir * (3.0 + energy * 0.6);

    float col = 0.0;
    float minRes = min(localResolution.x, localResolution.y);

    for (int i = 0; i < RCC_AA; ++i) {
        vec2 jitter = vec2(
            rcc_random2(vec2(float(i), animTime * 0.001 + bass)),
            rcc_random2(vec2(float(i) + 19.0, animTime * 0.002 + mid))
        ) - 0.5;
        vec2 sampleCoord = fragCoord + jitter;
        vec2 uv = 0.8 * (2.0 * sampleCoord - localResolution.xy) / minRes;

        vec3 rayDir = normalize(uv.x * xAxis + uv.y * yAxis - cam);
        float r = rcc_cube_intersect(cam, rayDir);
        if (r > 0.0) {
            float dist = rcc_sdf(cam + r * rayDir);
            if (dist > RCC_EPS) {
                r += dist;
                for (int j = 0; j < RCC_MAX_STEPS; ++j) {
                    if (dist <= RCC_EPS) break;
                    dist = rcc_sdf(cam + r * rayDir);
                    r += dist;
                }
                col += pow(0.7, rcc_sum3(abs(floor(cam + r * rayDir + 0.5))));
            }
        } else {
            col += 0.12;
        }
    }

    col /= float(RCC_AA);
    float audioBoost = 0.8 + energy * 0.6 + bass * 0.4;
    vec3 palette = mix(uPrimaryColor, uSecondaryColor, clamp(uColorBlend + high * 0.3, 0.0, 1.0));
    float rayMask = smoothstep(0.2, 0.8, col);
    vec3 rayColor = palette * pow(vec3(clamp(col, 0.0, 1.0)), vec3(1.2));
    vec3 color = mix(vec3(0.0), rayColor, rayMask);
    color *= audioBoost;
    color = clamp(color, 0.0, 1.0);

    return vec4(color, 1.0);
}
