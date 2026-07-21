// @EFFECT name="Julia Color Fractal" index=104 desc="Julia fractal with animated color palette" author="System"

vec3 juliaPalette(float t, vec3 a, vec3 b, vec3 c, vec3 d) {
    return a + b * cos(6.28318 * (c * t + d));
}

vec2 f(vec2 x, vec2 c) {
    return mat2(x.x, -x.y, x.y, x.x) * x + c;
}

vec4 renderJuliaColorFractal(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    vec2 uv = st;
    uv -= 0.5;
    uv *= 1.3;
    uv += 0.5;

    vec4 col = vec4(1.0);

    int u_maxIterations = 75;

    float r = 0.7885 * (sin((time * 2.0) - 1.57) * 0.2 + 0.85);
    vec2 c = vec2(r * cos((time * 2.0)), r * sin((time * 2.0)));

    vec2 z = vec2(0.);
    z.x = 3.0 * (uv.x - 0.5);
    z.y = 2.0 * (uv.y - 0.5);
    bool escaped = false;
    int iterations;
    for (int i = 0; i < 10000; i++) {
        if (i > u_maxIterations) break;
        iterations = i;
        z = f(z, c);
        if (dot(z, z) > 4.0) {
            escaped = true;
            break;
        }
    }

    vec3 iterationCol = vec3(juliaPalette(float(iterations) / float(u_maxIterations),
                                     vec3(0.5),
                                     vec3(0.5),
                                     vec3(1.0, 1.0, 0.0),
                                     vec3(0.3 + 0.3 * sin(time * 5.0),
                                          0.2 + 0.2 * sin(1. + time * 5.0),
                                          0.2 + 0.2 * sin(1.5 + time * 5.0))));

    vec3 coreCol = vec3(0.);

    float f_ite = float(iterations);
    float f_maxIte = float(u_maxIterations);
    return vec4(escaped ? iterationCol : coreCol, f_ite / f_maxIte);
}
