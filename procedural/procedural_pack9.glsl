// @EFFECT name="Gyroid Reflections" index=28 desc="Gyroid reflection shader with audio/tempo modulation" author="System"
// Gyroid reflection shader adapted from Shadertoy (CC0) with audio/tempo modulation

const float kGyroidPi = acos(-1.0);
const float kGyroidBpm = 125.0;

float gyroidBeatTime;
float gyroidTimeValue;
vec3 gyroidSpherePos;
vec3 gyroidPalette;
float gyroidEnergyValue;
float gyroidTempoValue;

float gyroidSaturate(float x) {
    return clamp(x, 0.0, 1.0);
}

vec3 gyroidSaturate(vec3 x) {
    return clamp(x, vec3(0.0), vec3(1.0));
}

mat2 gyroidRot(float a) {
    float s = sin(a);
    float c = cos(a);
    return mat2(c, s, -s, c);
}

void gyroidRotPiOver4(inout vec2 v) {
    v = vec2(v.x + v.y, -v.x + v.y) / sqrt(2.0);
}

float gyroidSqWave(float x) {
    float i = floor(x);
    float s = 0.1;
    float oddCurrent = step(1.0, mod(i, 2.0));
    float oddNext = step(1.0, mod(i + 1.0, 2.0));
    return mix(oddCurrent, oddNext, smoothstep(0.5 - s, 0.5 + s, fract(x)));
}

float gyroidSmin(float a, float b, float k) {
    float h = max(k - abs(a - b), 0.0);
    return min(a, b) - h * h * 0.25 / k;
}

vec3 gyroidPosition;
float gyroidD1, gyroidD2, gyroidD3;

float gyroidMap(vec3 p) {
    float d;
    vec3 q = p;
    q.y = 0.7 - abs(q.y);

    d = q.y;
    q.zx = fract(q.zx) - 0.5;

    vec2 dq = mix(vec2(-0.2), vec2(0.27, 0.1), gyroidSqWave(gyroidBeatTime * 0.15));
    float scale = 1.0;

    for (int i = 0; i < 4; ++i) {
        vec3 v = q;
        v.zx = abs(v.zx);
        if (v.z > v.x) {
            v.zx = v.xz;
        }
        float branch = max(v.x - 0.1, (v.x * 2.0 + v.y) / sqrt(5.0) - 0.3);
        d = min(d, branch / scale);

        q.zx = abs(q.zx);
        gyroidRotPiOver4(q.xz);
        q.xy -= dq;
        gyroidRotPiOver4(q.yx);

        q *= 2.0;
        scale *= 2.0;
    }

    vec3 g = p - gyroidSpherePos;
    float t = length(g) - 0.3;
    g *= 15.0;
    g.xy = gyroidRot(gyroidTimeValue * 1.3) * g.xy;
    g.yz = gyroidRot(gyroidTimeValue * 1.7) * g.yz;
    float gyroidPattern = abs(dot(sin(g), cos(g.yzx)));
    t = max(t, (gyroidPattern - 0.2) / 15.0);

    gyroidD1 = t;
    gyroidD2 = d;

    return gyroidSmin(d, t, 0.3);
}

vec3 gyroidCalcNormal(vec3 p) {
    vec2 e = vec2(0.001, 0.0);
    float dx = gyroidMap(p + e.xyy) - gyroidMap(p - e.xyy);
    float dy = gyroidMap(p + e.yxy) - gyroidMap(p - e.yxy);
    float dz = gyroidMap(p + e.yyx) - gyroidMap(p - e.yyx);
    return normalize(vec3(dx, dy, dz));
}

vec3 gyroidHSV(float h, float s, float v) {
    vec3 res = fract(h + vec3(0.0, 2.0, 1.0) / 3.0) * 6.0 - 3.0;
    res = gyroidSaturate(abs(res) - 1.0);
    res = (res - 1.0) * s + 1.0;
    res *= v;
    return res;
}

vec3 gyroidMarch(inout vec3 rayPos, inout vec3 rayDir, inout vec3 attenuation, out bool hitSomething) {
    vec3 accumulated = vec3(0.0);
    hitSomething = false;
    float t = 0.0;

    for (int i = 0; i < 70; ++i) {
        float dist = gyroidMap(rayPos + rayDir * t);
        if (abs(dist) < 0.0001) {
            hitSomething = true;
            break;
        }
        t += dist;
        if (t > 40.0) {
            break;
        }
    }

    rayPos += rayDir * t;

    vec3 lightDir = normalize(-rayPos);
    vec3 normal = gyroidCalcNormal(rayPos);
    vec3 reflection = reflect(rayDir, normal);

    float diffuse = max(dot(lightDir, normal), 0.0);
    float specular = pow(max(dot(reflect(lightDir, normal), rayDir), 0.0), 20.0);
    float fog = exp(-t * t * 0.2);

    float materialParam = smoothstep(0.01, 0.1, length(rayPos - gyroidSpherePos) - 0.3);
    float phase = length(rayPos) * 4.0 - gyroidTimeValue * 2.0;
    // Use user colors with layered effect based on phase
    float phaseLayer = fract(phase / (kGyroidPi * 2.0));
    vec3 albedo = mix(uPrimaryColor, uSecondaryColor, phaseLayer);
    albedo = mix(vec3(0.9), albedo, materialParam);

    float f0 = mix(0.01, 0.8, materialParam);
    float metalness = mix(0.01, 0.9, materialParam);
    float fresnel = f0 + (1.0 - f0) * pow(1.0 - dot(reflection, normal), 5.0);
    float lightPower = 3.0 / max(abs(sin(phase)) + 1e-3, 1e-3);

    vec3 color = vec3(0.0);
    color += albedo * diffuse * (1.0 - metalness) * lightPower;
    color += albedo * specular * metalness * lightPower;
    color = mix(vec3(0.0), color, fog);
    color *= attenuation;

    attenuation *= albedo * fresnel * fog;
    rayPos += normal * 0.01;
    rayDir = reflection;

    return color;
}

vec4 renderGyroidReflections(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    vec2 aspect = vec2(uResolution.x / max(uResolution.y, 1.0), 1.0);
    vec2 fragCoord = (st / aspect + 0.5) * uResolution.xy;
    
    // Dynamic resolution scaling for performance
    float pixelCount = uResolution.x * uResolution.y;
    float resolutionScale = 1.0;
    if (pixelCount > 1920.0 * 1080.0) {
        resolutionScale = 0.5; // 2x resolution reduction for 4K+
    } else if (pixelCount > 1280.0 * 720.0) {
        resolutionScale = 0.7; // Moderate reduction for 1080p+
    }
    
    vec2 uv = vec2(fragCoord.x / uResolution.x, fragCoord.y / uResolution.y);
    uv -= 0.5;
    uv /= vec2(uResolution.y / max(uResolution.x, 1.0), 1.0) * 0.5;
    uv *= resolutionScale; // Scale down sampling resolution

    gyroidTimeValue = time;
    float bpmMod = kGyroidBpm * clamp(tempo * 0.9 + 0.3, 0.6, 1.4);
    gyroidBeatTime = time / 60.0 * bpmMod;
    gyroidSpherePos = sin(vec3(13.0, 0.0, 7.0) * time * 0.1 + vec3(bass * 0.5, mid * 0.2, high * 0.4));
    gyroidPalette = mix(uPrimaryColor, uSecondaryColor, clamp(uColorBlend, 0.0, 1.0));
    gyroidEnergyValue = energy;
    gyroidTempoValue = tempo;

    vec3 color = vec3(0.0);
    vec3 rayOrigin = vec3(0.0, -0.3, 1.9);
    rayOrigin.zx *= gyroidRot(time * 0.1);

    vec3 rayDir = normalize(vec3(uv, -2.0));
    rayDir.zx *= gyroidRot(time * 0.1);

    vec3 attenuation = vec3(1.0);
    bool hit = false;

    color += gyroidMarch(rayOrigin, rayDir, attenuation, hit);
    if (hit) {
        color += gyroidMarch(rayOrigin, rayDir, attenuation, hit);
    }

    color = pow(color, vec3(1.0 / 2.2));
    color = mix(gyroidPalette, color, clamp(0.4 + energy * 0.5 + tempo * 0.3, 0.0, 1.0));
    color = clamp(color, 0.0, 1.0);

    float alpha = clamp(0.4 + energy * 0.3 + length(color) * 0.1, 0.0, 1.0);
    return vec4(color, alpha);
}
