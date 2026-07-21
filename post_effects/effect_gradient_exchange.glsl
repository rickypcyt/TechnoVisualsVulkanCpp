#include "post_common.glsl"

/*
  Gradient Exchange — complex pixel swapping effect.
  Uses gradient-based displacement and multi-channel swapping between neighbors.
  Creates organic pixel migration with complex color exchanges.
*/

vec4 T(vec2 U) {
    return texture(uScene, clamp(U / uResolution.xy, 0.0, 1.0));
}

// Channel exchange function: swap C with neighbor based on channel comparison
void xch(inout vec4 C, inout vec4 othera, inout vec4 otherb, float positiona, float positionb) {
    if (C[int(positiona)] < othera[int(positionb)]) {
        vec4 temp = C;
        C = othera;
        othera = temp;
    } else if (C[int(positiona)] > otherb[int(positionb)]) {
        vec4 temp = C;
        C = otherb;
        otherb = temp;
    }
}

// Get gradient using specific channel indices
vec2 getGradient(vec2 U, vec4 indices) {
    float range = 2.0;
    vec4 n = T(U + vec2(0.0, 1.0) * range);
    vec4 s = T(U - vec2(0.0, 1.0) * range);
    vec4 e = T(U + vec2(1.0, 0.0) * range);
    vec4 w = T(U - vec2(1.0, 0.0) * range);

    return vec2(n[int(indices.x)] - s[int(indices.y)], e[int(indices.z)] - w[int(indices.w)]);
}

void main() {
    vec2 U = vUV * uResolution.xy;

    float strength = clamp(uStrength, 0.0, 1.0);
    float timeMod = uTime * 2.0;

    // === Gradient-based displacement ===
    vec2 grad = getGradient(U, vec4(2.0, 2.0, 2.0, 2.0));
    U -= grad * (1.0 + sin(uTime * 2.0) * 2.0) * strength;

    vec4 C = T(U);

    // === Multi-channel exchange with neighbors ===
    float m = mod(floor(uTime * 10.0), 3.0);
    float range = 1.0 + m * 1.0;

    vec4 n = T(U + vec2(0.0, 1.0) * range);
    vec4 s = T(U - vec2(0.0, 1.0) * range);
    vec4 e = T(U + vec2(1.0, 0.0) * range);
    vec4 w = T(U - vec2(1.0, 0.0) * range);

    // Perform complex channel exchanges
    xch(C, e, n, 0.0 + m, 1.0);
    xch(C, n, w, 3.0 - m, 2.0 - m);
    xch(C, e, s, 1.0 + m, 1.0 - m);
    xch(C, n, w, 1.0 + m, 4.0);
    xch(C, w, e, 2.0 - m, 1.0 + m);
    xch(C, w, n, 2.0 - m, 3.0);
    xch(C, e, w, 1.0 + m, 4.0);
    xch(C, n, s, 3.0 - m, 4.0);
    xch(C, s, e, 2.0 - m, 2.0);

    // === Low-gradient areas: add texture displacement ===
    if (length(grad) < 0.001) {
        vec2 offsetUV = U + vec2(sin(uTime * 3.0), cos(uTime * 2.0)) * 5.0;
        C = mix(C, T(offsetUV), 0.3 * strength);
    }

    // === Subtle color quantization for digital feel ===
    C.rgb = mix(C.rgb, floor(C.rgb * 16.0) / 16.0, 0.05 * strength);

    // === Periodic reset to prevent drift ===
    if (mod(uTime, 3.0) < 0.1) {
        C = mix(C, T(U), 0.5);
    }

    FragColor = vec4(clamp(C.rgb, 0.0, 1.0), 1.0);
}
