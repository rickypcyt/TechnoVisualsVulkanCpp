// @EFFECT name="Hyper Pulse" index=27 desc="Hyper-dimensional raymarch with audio modulation" author="Blackle Mori"
// Hyper-dimensional raymarch shader adapted from Blackle Mori (CC0)

float hyper_comp(vec3 p) {
    p = acos(sin(p) * 0.9);
    return length(p) - 1.0;
}

vec3 hyper_erot(vec3 p, vec3 axis, float angle) {
    return mix(dot(p, axis) * axis, p, cos(angle)) + sin(angle) * cross(axis, p);
}

float hyper_smin(float a, float b, float k) {
    float h = max(0.0, k - abs(b - a)) / k;
    return min(a, b) - h * h * h * k / 6.0;
}

vec4 hyper_wrot(vec4 p) {
    return vec4(dot(p, vec4(1.0)), p.yzw + p.zwy - p.wyz - p.xxx) * 0.5;
}

float hyper_d1, hyper_d2, hyper_d3;
float hyper_lazors, hyper_doodad;
vec3 hyper_p2;
float hyper_timeScaled;

const float kHyperBpm = 125.0;

float hyper_scene(vec3 p, float time, float tempo, float energy, float bass, float mid, float high) {
    hyper_p2 = hyper_erot(p, vec3(0.0, 1.0, 0.0), hyper_timeScaled);
    hyper_p2 = hyper_erot(hyper_p2, vec3(0.0, 0.0, 1.0), hyper_timeScaled / 3.0);
    hyper_p2 = hyper_erot(hyper_p2, vec3(1.0, 0.0, 0.0), hyper_timeScaled / 5.0);

    float beatTime = time / 60.0 * kHyperBpm;
    vec4 p4 = vec4(hyper_p2, 0.0);
    p4 = mix(p4, hyper_wrot(p4), smoothstep(-0.5, 0.5, sin(beatTime / 4.0)));
    p4 = abs(p4);
    p4 = mix(p4, hyper_wrot(p4), smoothstep(-0.5, 0.5, sin(beatTime)));

    float beatFactor = smoothstep(-0.5, 0.5, cos(beatTime / 2.0));
    float beatFactor2 = smoothstep(0.9, 1.0, cos(beatTime / 16.0));
    float extrusion = mix(0.05, 0.07, beatFactor);
    vec4 coreShape = abs(p4) - extrusion;
    coreShape = max(coreShape, 0.0);
    hyper_doodad = length(coreShape + mix(-0.1, 0.2, beatFactor)) - mix(0.15, 0.55, beatFactor * beatFactor) + beatFactor2;

    p.x += asin(sin(hyper_timeScaled / 80.0) * 0.99) * 80.0;

    hyper_lazors = length(asin(sin(hyper_erot(p, vec3(1.0, 0.0, 0.0), hyper_timeScaled * 0.2).yz * 0.5 + 1.0)) / 0.5) - 0.1;
    hyper_d1 = hyper_comp(p);
    hyper_d2 = hyper_comp(hyper_erot(p + 5.0, normalize(vec3(1.0, 3.0, 4.0)), 0.4));
    hyper_d3 = hyper_comp(hyper_erot(p + 10.0, normalize(vec3(1.0, 2.0, 3.0)), 1.0));

    float structural = hyper_smin(hyper_smin(hyper_d1, hyper_d2, 0.05), hyper_d3, 0.05);
    structural = 0.3 - structural;
    float reactive = mix(0.25, 0.15, clamp(energy, 0.0, 1.0));
    float lazorMix = mix(hyper_lazors, reactive, clamp(high, 0.0, 1.0));
    return min(hyper_doodad, min(lazorMix, structural));
}

vec3 hyper_norm(vec3 p, float time, float tempo, float energy, float bass, float mid, float high) {
    float epsilon = length(p) < 1.0 ? 0.005 : 0.01;
    vec3 offset = vec3(epsilon, 0.0, 0.0);
    float sx = hyper_scene(p + offset.xyy, time, tempo, energy, bass, mid, high) -
               hyper_scene(p - offset.xyy, time, tempo, energy, bass, mid, high);
    float sy = hyper_scene(p + offset.yxy, time, tempo, energy, bass, mid, high) -
               hyper_scene(p - offset.yxy, time, tempo, energy, bass, mid, high);
    float sz = hyper_scene(p + offset.yyx, time, tempo, energy, bass, mid, high) -
               hyper_scene(p - offset.yyx, time, tempo, energy, bass, mid, high);
    return normalize(vec3(sx, sy, sz));
}

vec4 renderHyperPulse(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    vec2 aspect = vec2(uResolution.x / max(uResolution.y, 1.0), 1.0);
    vec2 fragCoord = (st / aspect + 0.5) * uResolution.xy;
    vec2 uv = (fragCoord - 0.5 * uResolution.xy) / max(uResolution.y, 1.0);

    // Early exit for pixels outside viewport (optimization)
    float distFromCenter = length(uv);
    if (distFromCenter > 1.2) {
        return vec4(0.0, 0.0, 0.0, 0.0);
    }

    float beatTime = time / 60.0 * kHyperBpm;
    float bpmBlend = mix(pow(sin(fract(beatTime) * 3.14159 / 2.0), 20.0) + floor(beatTime), beatTime, 0.4);
    hyper_timeScaled = bpmBlend;

    vec3 camDir = normalize(vec3(0.8 + sin(bpmBlend * 3.14159 / 4.0) * 0.3, uv.y, uv.x));
    vec3 camPos = vec3(-1.5 + sin(bpmBlend * 3.14159) * 0.2, 0.0, 0.0) + camDir * 0.2;

    camPos = hyper_erot(camPos, vec3(0.0, 1.0, 0.0), sin(bpmBlend * 0.2) * 0.4);
    camPos = hyper_erot(camPos, vec3(0.0, 0.0, 1.0), cos(bpmBlend * 0.2) * 0.4);
    camDir = hyper_erot(camDir, vec3(0.0, 1.0, 0.0), cos(bpmBlend * 0.2) * 0.4);
    camDir = hyper_erot(camDir, vec3(0.0, 0.0, 1.0), sin(bpmBlend * 0.2) * 0.4);

    vec3 rayPos = camPos;
    vec3 rayDir = camDir;

    bool hit = false;
    float attenuation = 1.0;
    float travel = 0.0;
    float glow = 0.0;
    float doodadGlow = 0.0;
    float fogAccum = 0.0;

    for (int i = 0; i < 40 && !hit; ++i) {
        float dist = hyper_scene(rayPos, time, tempo, energy, bass, mid, high);
        hit = dist * dist < 1e-6;
        glow += 0.2 / (1.0 + hyper_lazors * hyper_lazors * 20.0) * attenuation;
        doodadGlow += 0.2 / (1.0 + hyper_doodad * hyper_doodad * 20.0) * attenuation;

        bool reflective = (sin(hyper_d3 * 45.0) < -0.4 && dist != hyper_doodad) ||
                          (dist == hyper_doodad && cos(pow(length(hyper_p2 * hyper_p2 * hyper_p2), 0.3) * 120.0) > 0.4);
        bool lazorHit = dist == hyper_lazors;

        if (hit && reflective && !lazorHit) {
            vec3 normal = hyper_norm(rayPos, time, tempo, energy, bass, mid, high);
            attenuation *= 1.0 - abs(dot(rayDir, normal)) * 0.98;
            rayDir = reflect(rayDir, normal);
            dist = 0.1;
            hit = false;
        }

        rayPos += rayDir * dist;
        travel += dist;
        fogAccum += dist * attenuation / 30.0;
        if (travel > 18.0) break;
    }

    fogAccum = smoothstep(0.0, 1.0, fogAccum);
    vec3 fogColor = mix(vec3(0.45, 0.7, 1.1), vec3(0.3, 0.5, 0.85), length(uv));

    vec3 finalColor = vec3(0.0);
    if (hit) {
        vec3 normal = hyper_norm(rayPos, time, tempo, energy, bass, mid, high);
        vec3 reflected = reflect(rayDir, normal);
        float spec = length(sin(reflected * 3.0) * 0.5 + 0.5) / sqrt(3.0) * 0.7 + 0.3;
        vec3 material = mix(vec3(0.9, 0.4, 0.3), vec3(0.3, 0.4, 0.8), smoothstep(-1.0, 1.0, float(sin(hyper_d1 * 5.0 + time * 2.0))));
        material = mix(material, vec3(0.5, 0.4, 1.0), smoothstep(0.0, 1.0, float(sin(hyper_d2 * 5.0 + time * 2.0))));
        if (hyper_doodad == hyper_scene(rayPos, time, tempo, energy, bass, mid, high)) {
            material = mix(vec3(1.0), material, 0.1) * 0.2 + 0.1;
        }
        finalColor = material * spec + pow(spec, 10.0);
    }

    finalColor = finalColor * attenuation - glow * glow - fogColor * glow;
    finalColor = mix(finalColor, fogColor, fogAccum);

    vec3 palette = mix(uPrimaryColor, uSecondaryColor, clamp(uColorBlend, 0.0, 1.0));
    finalColor = mix(palette, finalColor, clamp(0.5 + energy * 0.4 + tempo * 0.2, 0.0, 1.0));

    if (hyper_doodad == hyper_scene(rayPos, time, tempo, energy, bass, mid, high)) {
        finalColor += doodadGlow * doodadGlow * 0.12 * vec3(0.4, 0.6, 0.9);
    }

    finalColor = sqrt(max(finalColor, 0.0));
    finalColor = smoothstep(vec3(0.0), vec3(1.2), finalColor);
    float alpha = clamp(0.35 + fogAccum * 0.4, 0.0, 1.0);
    return vec4(finalColor, alpha);
}
