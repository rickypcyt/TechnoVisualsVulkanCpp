// @EFFECT name="Reactive Twist Field" index=49 desc="Reactive twist vortex adapted from a mouse-driven spiral test" author="ShaderToy"

vec4 renderReactiveTwistField(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    vec2 v = st * (12.0 + high * 6.0);
    float t = time * 4.0;

    float factor = clamp(0.18 + bass * 0.75 + energy * 0.2, 0.05, 1.6);
    float rotation = (mid * 2.0 - 1.0) * 3.14159265 * 0.95 + high * 1.1 + tempo * 0.15;
    float c = cos(rotation);
    float s = sin(rotation);

    for (int i = 1; i <= 40; ++i) {
        float d = float(i + 3) / 40.0;
        float x = v.x;
        float y = v.y + sin(v.x * d * 7.0 + t + bass * 1.5) / d * factor
                      + cos(v.x * d + t + energy) / d * factor;

        v.x = x * c - y * s;
        v.y = x * s + y * c;
        v *= 0.985 + high * 0.002;
    }

    float col = length(v) * 0.25;
    vec3 rgb = 0.5 + 0.5 * vec3(
        cos(col),
        cos(col * 2.0 + time * 0.2),
        cos(col * 4.0 + bass)
    );

    vec3 palette = mix(uPrimaryColor, uSecondaryColor, clamp(0.5 + 0.5 * sin(col * 2.5 + mid * 3.0), 0.0, 1.0));
    rgb *= palette;
    rgb *= 0.55 + energy * 0.65;
    rgb += uSecondaryColor * factor * 0.15;
    rgb = clamp(rgb, 0.0, 1.0);

    return vec4(rgb, 1.0);
}
