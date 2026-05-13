// @EFFECT name="Loop Noise Rays" index=70 desc="Looping noise radial rays on black background" author="Will Stallwood"

vec2 random2Rays(vec2 st) {
    st = vec2(dot(st, vec2(127.1, 311.7)),
              dot(st, vec2(269.5, 183.3)));
    return -1.0 + 2.0 * fract(sin(st) * 43758.5453123);
}

float v_noiseRays(vec2 st) {
    vec2 i = floor(st);
    vec2 f = fract(st);
    vec2 u = f * f * (3.0 - 2.0 * f);

    return mix(mix(dot(random2Rays(i + vec2(0.0, 0.0)), f - vec2(0.0, 0.0)),
                   dot(random2Rays(i + vec2(1.0, 0.0)), f - vec2(1.0, 0.0)), u.x),
               mix(dot(random2Rays(i + vec2(0.0, 1.0)), f - vec2(0.0, 1.0)),
                   dot(random2Rays(i + vec2(1.0, 1.0)), f - vec2(1.0, 1.0)), u.x), u.y);
}

float loop_noiseRays(vec2 st, float seconds, float time) {
    float loopLength = seconds;
    float transitionStart = seconds * 0.5;
    float t = mod(time, loopLength);

    float v1 = v_noiseRays(st + t);
    float v2 = v_noiseRays(st + t - loopLength);

    float transitionProgress = (t - transitionStart) / (loopLength - transitionStart);
    float progress = clamp(transitionProgress, 0.0, 1.0);
    return mix(v1, v2, progress);
}

mat2 rotateRays(float angle) {
    return mat2(cos(angle), -sin(angle), sin(angle), cos(angle));
}

vec4 renderLoopNoiseRays(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    // Center and aspect ratio
    float aspect = uResolution.x / uResolution.y;
    st.x = st.x * aspect - aspect * 0.5 + 0.5;

    // Space
    st = st * 2.0 - 1.0;

    // Rotation based on tempo and time
    st *= rotateRays(1.57079632675 + time * 0.2 * tempo);

    // SDF
    vec2 pos = st;

    float r = 0.1;
    float a = atan(pos.y, pos.x);
    r = length(pos);
    r *= abs(cos(abs(a) * 2.0));

    // Noise controlled by energy
    float seconds = mix(3.0, 8.0, 1.0 - energy * 0.5);
    float n = 0.5 * loop_noiseRays(vec2(abs(a * 0.50), r) * mix(10.0, 30.0, energy), seconds, time);
    r += n;

    // SDF value - inverted for black background
    float sdf = r - mix(0.2, 0.5, bass);
    sdf = 1.0 - smoothstep(0.0, 0.001, sdf); // INVERTED: white rays on black

    // Color - black background with white rays
    vec3 color = vec3(0.0); // Black background
    color = mix(color, vec3(1.0), sdf); // White where active
    
    // Add palette color accents on high frequencies
    color = mix(color, uPrimaryColor, high * 0.5 * sdf);
    
    // Edge fade
    color = mix(color, vec3(0.0), abs(st.y / 8.0) * (1.0 - sdf));

    return vec4(color, 1.0);
}
