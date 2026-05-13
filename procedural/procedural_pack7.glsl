// @EFFECT name="Chromatic Swirl" index=26 desc="Sine-based chromatic swirl" author="System"
// Sine-based chromatic swirl shader

vec4 renderChromaticSwirl(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    vec2 uv = st + 0.5;
    vec2 centered = uv - 0.5;

    vec3 phaseShift = vec3(0.5, 0.2, 0.3) + vec3(0.35 * bass, 0.25 * mid, 0.4 * high);
    vec3 a = sin(time * phaseShift);
    vec3 b = vec3(-500.0, 200.0, 500.0);
    vec3 c = vec3(0.2, 1.4, 0.4) + vec3(tempo * 0.4);

    float angle = atan(centered.y, centered.x);
    vec3 angular = vec3(angle + energy * 0.6 + uIntensity * 0.3);

    vec3 base = 0.2 + sin(0.5 + c * time) + sin(angular * b * a);
    vec3 palette = mix(uPrimaryColor, uSecondaryColor, clamp(uColorBlend, 0.0, 1.0));
    float vignette = smoothstep(0.9, 0.2, length(centered) * 1.6);

    vec3 color = mix(palette, base, clamp(0.4 + energy * 0.5 + tempo * 0.3, 0.0, 1.0));
    color *= vignette;
    color = clamp(color, 0.0, 1.0);

    float alpha = clamp(0.3 + vignette * 0.6 + energy * 0.2, 0.0, 1.0);
    return vec4(color, alpha);
}
