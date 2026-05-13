// @EFFECT name="Fractal Rotation Field" index=47 desc="Fractal rotation field with accumulated transforms" author="Shadertoy" zoom=0.8

mat2 rotate2D_fractal(float t) {
    return mat2(cos(t), sin(t), -sin(t), cos(t));
}

vec4 renderFractalRotation(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    vec2 r = uResolution.xy;
    vec2 n = vec2(0.0), N = vec2(0.0), q;
    vec2 p = st * 2.0;
    vec4 o = vec4(0.0);
    float S = 5.0, a = 0.0, j = 0.0;
    float t = time;
    
    mat2 m = rotate2D_fractal(5.0);
    for(; j < 30.0; j++, S *= 1.2) {
        p *= m;
        n *= m;
        q = p * S + j + n + t * 4.0 + sin(t * 4.0) * 0.8;
        a += dot(cos(q) / S, vec2(1.0));
        q = sin(q);
        n += q;
        N += q / (S + 20.0);
    }
    
    o += 0.1 - a * 0.1;
    o.r *= 5.0 + bass * 2.0;
    o += min(0.7, 0.001 / length(N + 0.0001));
    o -= o * dot(p, p) * 0.7;
    
    vec3 paletteMix = mix(uPrimaryColor, uSecondaryColor, clamp(uColorBlend + mid * 0.2, 0.0, 1.0));
    float paletteInfluence = clamp(0.45 + uColorBlend * 0.4 + high * 0.25, 0.0, 1.0);
    o.rgb = mix(o.rgb, paletteMix, paletteInfluence);
    
    float intensity = 1.0 + energy * 0.5;
    o.rgb *= intensity;
    
    o = clamp(o, 0.0, 1.0);
    return o;
}
