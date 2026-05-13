// @EFFECT name="Volumetric Tunnel" index=25 desc="Volumetric streak tunnel with audio reactivity" author="System"
// Volumetric streak tunnel shader adapted from Shadertoy snippet

vec4 renderVolumetricTunnel(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    vec2 aspect = vec2(uResolution.x / max(uResolution.y, 1.0), 1.0);
    vec2 fragCoord = (st / aspect + 0.5) * uResolution.xy;

    float t = time * 0.1;
    vec2 uv = fragCoord / uResolution.xy - 0.5;
    vec2 originalUv = uv;
    uv.x *= uResolution.x / max(uResolution.y, 1.0);

    vec3 rd = normalize(vec3(uv, 2.0));
    float ct = cos(t);
    float stSin = sin(t);
    mat2 rotMat = mat2(ct, stSin, -stSin, ct);
    rd.xy = rotMat * rd.xy;

    vec3 ro = vec3(t + sin(t * 6.53583) * 0.05,
                   0.01 + sin(t * 352.4855) * 0.0015,
                   -t * 3.0);

    vec3 p = ro;
    float v = 0.0;
    float td = -mod(ro.z, 0.005);
    float stepSize = 0.005;
    float falloff = clamp(0.6 + energy * 0.4 + bass * 0.3, 0.1, 2.0);
    float pulse = clamp(0.3 + tempo * 0.4 + high * 0.5, 0.1, 2.5);

    for (int r = 0; r < 150; ++r) {
        float streak = length(abs(0.01 - mod(p, 0.02)));
        float streakIntensity = pow(max(0.0, 0.01 - streak) / 0.01, 10.0);
        float temporal = exp(-falloff * pow((1.0 + td), 2.0));
        v += streakIntensity * temporal;
        p = ro + rd * td;
        td += stepSize;
    }

    float vignette = max(0.0, 1.0 - length(originalUv * originalUv) * 2.5);

    // Tunnel rays - blue dominant instead of red
    vec3 tunnel = vec3(v * v * v, v * v, v) * 8.0 * vignette;  // Blue dominant (v is blue channel)

    float colorMix = clamp(0.4 + energy * 0.5 + uIntensity * 0.3, 0.0, 1.0);
    vec3 palette = mix(uPrimaryColor, uSecondaryColor, clamp(uColorBlend, 0.0, 1.0));
    vec3 finalColor = mix(palette, tunnel, colorMix);

    float alpha = clamp(0.25 + v * 0.3 + energy * 0.2, 0.0, 1.0);
    return vec4(finalColor, alpha);
}
