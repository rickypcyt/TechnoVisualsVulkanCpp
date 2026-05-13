// @EFFECT name="Pouet Grid" index=42 desc="Audio-reactive UV distortion homage" author="Danilo Guanabara"
// Pouet Grid Redux - audio-reactive UV distortion homage (Danilo Guanabara)
const float kPouetPi = 3.14159265359;
const float kPouetTau = kPouetPi * 2.0;

mat2 pouetRot(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, s, -s, c);
}

vec3 pouetPalette(float bass, float mid, float high) {
    // Internal colors (cyan/magenta/yellow for RGB grid effect)
    vec3 internalColor1 = vec3(0.0, 0.8, 1.0);  // Cyan
    vec3 internalColor2 = vec3(1.0, 0.3, 0.6);  // Magenta
    vec3 internalBase = mix(internalColor1, internalColor2, 0.5 + 0.5 * sin(uTime * 0.3));
    vec3 lift = vec3(0.6 + bass * 0.4, 0.5 + mid * 0.3, 0.7 + high * 0.5);
    vec3 color = clamp(internalBase * lift, 0.0, 1.5);

    // User colors as tint only (30%)
    vec3 userTint = mix(uPrimaryColor, uSecondaryColor, 0.5 + 0.5 * sin(uTime * 0.3));
    color = mix(color, color * userTint * 2.0, 0.3);

    return color;
}

vec4 renderPouetGrid(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    vec3 color = vec3(0.0);
    float z = time * mix(1.5, 2.5, clamp(tempo * 0.3 + energy * 0.4, 0.0, 1.0));

    vec2 fragCoord = st * uResolution.xy;

    for (int i = 0; i < 3; ++i) {
        vec2 uv;
        vec2 p = fragCoord / uResolution.xy;
        uv = p;
        
        // Apply exact raymarched object coordinate system
        p.x *= uResolution.x / max(uResolution.y, 1.0);
        // No manual offset - keep neutral coordinates like raymarched object

        float l = length(p) + 1e-4;
        float wobble = sin(z * 3.0) + 1.0;  // Changed from sin(z * 2.0) to sin(z * 3.0) for even faster wobble
        float pulse = abs(sin(l * (9.0 + high * 4.0) - z - z));
        vec2 ripple = p / l * wobble * pulse * mix(0.4, 0.8, clamp(energy, 0.0, 1.0));

        vec2 extra = vec2(0.0);
        extra.x = sin(p.y * 3.0 + time * 1.2) * 0.05 * bass;
        extra.y = cos(p.x * 4.0 - time * 0.9) * 0.04 * mid;

        uv += ripple + extra;
        uv += 0.03 * vec2(sin(z + float(i)), cos(z - float(i))) * clamp(high * 0.6, 0.0, 1.0);

        vec2 cell = mod(uv, 1.0) - 0.5;
        float dist = length(cell);
        float channel = 0.01 / max(dist, 1e-3);
        color[i] = channel;
        z += 0.07 + high * 0.02;
    }

    vec3 palette = pouetPalette(bass, mid, high);
    color *= palette;

    float vignette = smoothstep(1.4, 0.3, length(st * 2.0 - 1.0));
    color *= vignette;

    float alpha = clamp(0.5 + length(color) * 0.4 + energy * 0.25, 0.0, 1.0);
    return vec4(clamp(color, 0.0, 1.0), alpha);
}
