// @EFFECT name="Newton RGB Fractal" index=105 desc="Newton fractal z^3-1 with RGB rotation colors" author="System"

#ifndef COMPLEX_OPS_DEFINED
#define COMPLEX_OPS_DEFINED

vec2 cmul(vec2 a, vec2 b) {
    return vec2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
}

vec2 cdiv(vec2 a, vec2 b) {
    return vec2(a.x * b.x + a.y * b.y, -a.x * b.y + a.y * b.x) / (b.x * b.x + b.y * b.y);
}

#endif

vec2 fn(vec2 z) {
    return cmul(z, cmul(z, z)) - vec2(1, 0);
}

vec2 dfn(vec2 z) {
    return cmul(vec2(3, 0), cmul(z, z));
}

vec4 renderNewtonRGBFractal(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    float aspect = uResolution.x / max(uResolution.y, 1.0);
    vec2 coord = (2.0 * st - 1.0) * vec2(aspect, 1.0);

    // Use animated zoom with time
    float zoom = pow(sin(time * 2.0 + 2.) + 1.05, 2.);
    vec2 z = coord / zoom;

    vec2 zPrev = z;
    float threshold = 0.00001;
    float i;

    for (i = 0.; i < 100.; i++) {
        z -= cdiv(fn(z), dfn(z));
        if (length(z - zPrev) < threshold) break;
        zPrev = z;
    }

    float theta = atan(z.y, z.x);
    float rotation = mod(theta / 6.2832 + 1., 1.);

    // Smooth RGB gradient instead of hard thresholds
    vec3 color;
    float r = smoothstep(0.0, 0.33, rotation) * (1.0 - smoothstep(0.33, 0.66, rotation));
    float g = smoothstep(0.33, 0.66, rotation) * (1.0 - smoothstep(0.66, 1.0, rotation));
    float b = smoothstep(0.66, 1.0, rotation) + smoothstep(0.0, 0.33, rotation);
    color = vec3(r, g, b);

    float intensity = 1. / log(i + 1.0);

    return vec4(color * intensity, 1.0);
}
