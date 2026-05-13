// @EFFECT name="Kaleidoscopic Flow" index=48 desc="Fractal kaleidoscopic pattern with flowing colors" author="ShaderToy"

vec4 renderKaleidoscopicFlow(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    vec2 p = st * 4.0;
    float t = time * 0.4 + length(p + vec2(sin(time), cos(time)) * 4.0) * 0.1;
    
    vec3 c = vec3(0.0);
    
    c += uPrimaryColor * fract((p.x + p.y + fract(t * 0.5)) * 5.0);
    c *= uSecondaryColor * 2.0 * fract((sin(t * 6.0) * p.x - p.y + fract(t * 0.05)) * 5.0);
    c *= (p.x * p.y);
    
    // Audio reactivity
    c *= (0.5 + energy * 0.5);
    c += uSecondaryColor * bass * 0.3;
    
    c = clamp(c, 0.0, 1.0);
    float alpha = clamp(0.4 + energy * 0.4, 0.0, 1.0);
    
    return vec4(c, alpha);
}
