// @EFFECT name="Color Process 5-Point" index=106 desc="Complex color processing with 5-point sampling" author="System"

const float pi = 3.14159;

vec3 samp(vec2 p) {
    // Generate procedural pattern instead of texture sampling
    p = fract(p / uResolution.xy);
    float r = abs(sin(p.x * 10.0 + uTime * 5.0));
    float g = abs(sin(p.y * 8.0 + uTime * 4.0));
    float b = abs(sin((p.x + p.y) * 12.0 + uTime * 3.0));
    return vec3(r, g, b);
}

vec3 fivept(vec2 uv, float d) {
    const vec3 p = vec3(1., -1., 0.);
    vec3 c = samp(uv);
    vec3 e = samp(uv + d * p.zx);
    vec3 w = samp(uv + d * p.zy);
    vec3 n = samp(uv + d * p.xz);
    vec3 s = samp(uv + d * p.yz);
    vec3 mu = (c + e + s + n + w) / 5.;
    vec3 d_c = (c - mu) * (c - mu);
    vec3 d_e = (e - mu) * (e - mu);
    vec3 d_w = (w - mu) * (w - mu);
    vec3 d_n = (n - mu) * (n - mu);
    vec3 d_s = (s - mu) * (s - mu);
    vec3 sigma = sqrt((d_c + d_e + d_w + d_n + d_s) / 4.);
    return (2. * (c - mu) * (.5 - sigma) + mu);
}

vec3 color_mid2max(vec3 x) {
    vec3 t = x;
    for (int i = 0; i < 3; i++) {
        if (t.r > t.g && t.r > t.b) {
            if (t.g < t.b) {
                x.r += x.b;
                x.b = 0.;
            } else {
                x.r += x.g;
                x.g = 0.;
            }
        }
        x = x.grb;
        t = t.grb;
    }
    return x;
}

vec4 renderColorProcess5Point(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    float ff = 1.75;
    float fb = -1.;
    float fsv = 0.01;
    float m2 = sin(pi * time * 2.0) * .4 + .6;
    float m1 = 0.003;
    float m0 = 0.995;
    float octaves = 1.;

    vec2 uv = st * uResolution.xy;
    vec3 sv = sin(vec3(1., 2., 1.) * pi * uv.yyx / uResolution.yyx) - 1.;
    vec3 c = vec3(0.);

    if (time < 2.) {
        return vec4(c, 1.);
    }

    // Generate base patterns
    vec3 c0 = samp(uv);
    vec3 c1 = samp(uv + cos(2. * pi * vec2(c0.g, c0.g + 0.25)));
    vec3 c2 = samp(uv + cos(2. * pi * vec2(c0.r + 0.75, c0.r + 0.5)));

    // Apply 5-point processing
    c = fivept(uv, pow(2., c0.b * octaves));
    c = c.gbr;
    c = mix(c2, c, m2);
    c = fract(ff * c + fb * c1 + fsv * sv);
    c = color_mid2max(c);
    c = mix(c1, c, m1);
    c = mix(c0, c, m0);

    return vec4(c, 1.);
}
