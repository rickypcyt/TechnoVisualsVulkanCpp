#version 450

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform GlobalUBO {
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
} ubo;

vec3 getRayDir(vec2 uv) {
    vec2 p = uv * 2.0 - 1.0;
    p.x *= ubo.resolution.x / ubo.resolution.y;

    vec3 forward = vec3(0.0, 0.0, -1.0);
    vec3 right = vec3(1.0, 0.0, 0.0);
    vec3 up = vec3(0.0, 1.0, 0.0);

    return normalize(forward + p.x * right + p.y * up);
}

float intersectPlane(vec3 ro, vec3 rd) {
    return -ro.y / rd.y;
}

void main() {
    vec2 uv = gl_FragCoord.xy / ubo.resolution;
    vec3 ro = vec3(0.0, 2.0, 5.0);
    vec3 rd = getRayDir(uv);

    vec3 col = vec3(0.02, 0.02, 0.05);
    float step = 0.25;
    float t = 0.0;
    for (int i = 0; i < 64; ++i) {
        vec3 pos = ro + rd * t;
        vec3 cell = fract(pos * 0.5);
        float density = step * abs(sin(pos.x * 3.0) * sin(pos.y * 3.0) * sin(pos.z * 3.0));
        if (density > 0.45) {
            float shade = exp(-t * 0.15);
            col = mix(vec3(0.9, 0.4, 0.2), vec3(0.1, 0.5, 0.9), shade);
            break;
        }
        t += step;
        if (t > 20.0) break;
    }

    outColor = vec4(col, 1.0);
}
