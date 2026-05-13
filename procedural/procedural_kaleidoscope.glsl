#version 330 core

out vec4 FragColor;

uniform vec2 uResolution;
uniform float uTime;
uniform float uTempo;
uniform float uEnergy;
uniform float uBass;
uniform float uMid;
uniform float uHigh;
uniform vec3 uPrimaryColor;
uniform vec3 uSecondaryColor;
uniform float uColorBlend;

#define AA 3
const float SQRT3 = 1.73205080757;
const float PI = 3.14159265359;
const float PI2 = 6.28318530718;

float hash12(vec2 v) { return fract(sin(dot(v, vec2(12.9898, 78.233))) * 43758.5453); }

mat2 rotate2D(float a) {
    float s = sin(a);
    float c = cos(a);
    return mat2(c, s, -s, c);
}

vec3 renderPattern(vec2 p, float time, float tempo, float energy, float bass, float mid, float high,
                   vec3 colorA, vec3 colorB, float blend) {
    vec3 col = vec3(0.0);

    float scale = 3.0;

    vec3 palette = mix(colorA, colorB, clamp(blend, 0.0, 1.0));
    float drive = clamp(0.4 + energy * 0.6 + bass * 0.4, 0.2, 1.6);

    p *= rotate2D(time * 0.12 + tempo * 0.08);
    p = vec2(log(length(p) + 1e-3) - time * (0.5 + tempo * 0.15), atan(p.y, p.x));
    p *= SQRT3 / PI * scale * 0.5;

    vec2 c = normalize(vec2(1.0, SQRT3));
    vec2 h = c * 0.5;
    vec2 a = mod(p, c) - h;
    vec2 b = mod(p - h, c) - h;
    vec2 g = dot(a, a) < dot(b, b) ? a : b;

    p = p - g + 1e-4;
    p.y = mod(p.y, SQRT3 * scale);
    vec2 ID = floor(p / h);

    float n = floor(hash12(ID) * 3.0);
    g *= rotate2D(n * PI / 3.0);
    g.y = abs(g.y);
    float d = g.y;
    g.y -= 0.5 / SQRT3;

    float e;
    if (g.y > -0.375 / SQRT3) {
        e = atan(g.y, g.x) * 9.0;
    } else {
        e = (0.75 - g.x * 10.0) * PI2;
    }

    d = min(d, abs(length(g) - 0.25 / SQRT3));
    float edge = smoothstep(0.12, 0.0, d * (1.1 - high * 0.4));

    float dir = sign(mod(n, 2.0) - 0.5);
    float motion = sin(e - dir * time * (9.0 + tempo * 1.3));
    float pulse = sin(time * (1.5 + energy * 0.8) + hash12(ID + time) * PI2);

    vec3 base = palette * (0.35 + drive * 0.4);
    vec3 accent = mix(vec3(0.9, 0.8, 1.1), palette.zyx, clamp(high * 0.6, 0.0, 1.0));

    col += base * edge;
    col *= motion * 0.4 + 0.6;
    col += accent * edge * (0.2 + high * 0.6) * clamp(pulse * 0.5 + 0.5, 0.0, 1.2);

    return clamp(col, 0.0, 1.2);
}

vec3 compositeLayers(vec2 uv, float time, float tempo, float energy, float bass, float mid,
                     float high, vec3 colorA, vec3 colorB, float blend) {
    vec3 layer1 = renderPattern(uv, time, tempo, energy, bass, mid, high, colorA, colorB, blend);
    vec3 layer2 = renderPattern(uv * rotate2D(PI / 3.0) * 0.92, time + 1.3, tempo * 1.1, energy,
                                bass, mid, high, colorB, colorA, 1.0 - blend);
    vec3 layer3 = renderPattern(uv * rotate2D(-PI / 6.0) * 1.08, time - 0.8, tempo * 0.85, energy,
                                mid, bass, high, mix(colorA, vec3(1.0), 0.3),
                                mix(colorB, vec3(0.2), 0.4), blend * 0.6 + 0.2);

    vec3 combined = layer1 * 0.55 + layer2 * 0.3 + layer3 * 0.25;
    combined += layer1 * layer2 * 0.2;
    combined = pow(combined, vec3(0.95));
    return clamp(combined, 0.0, 1.0);
}

void main() {
    vec2 fragCoord = gl_FragCoord.xy;
    float minDim = min(uResolution.x, uResolution.y);
    vec2 uv = (fragCoord * 2.0 - uResolution.xy) / minDim;

    vec3 accum = vec3(0.0);
    for (int m = 0; m < AA; ++m) {
        for (int n = 0; n < AA; ++n) {
            vec2 of = vec2(float(m), float(n)) / float(AA) - 0.5;
            vec2 sampleCoord = ((fragCoord + of) * 2.0 - uResolution.xy) / minDim;
            accum += compositeLayers(sampleCoord, uTime, uTempo, uEnergy, uBass, uMid, uHigh,
                                     uPrimaryColor, uSecondaryColor, uColorBlend);
        }
    }

    vec3 color = accum / float(AA * AA);
    float vignette = smoothstep(1.3, 0.2, length(uv) * (1.0 + uEnergy * 0.4));
    color *= vignette * (0.8 + uEnergy * 0.4);
    color = pow(color, vec3(0.85));

    FragColor = vec4(color, 1.0);
}
