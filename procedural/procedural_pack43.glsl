// @EFFECT name="Loop Noise SDF" index=69 desc="Looping noise on radial SDF with smooth transitions" author="Will Stallwood"

vec2 random2SDF(vec2 st) {
    st = vec2(dot(st, vec2(127.1, 311.7)),
              dot(st, vec2(269.5, 183.3)));
    return -1.0 + 2.0 * fract(sin(st) * 43758.5453123);
}

float v_noiseSDF(vec2 st) {
    vec2 i = floor(st);
    vec2 f = fract(st);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(dot(random2SDF(i + vec2(0.0, 0.0)), f - vec2(0.0, 0.0)),
                   dot(random2SDF(i + vec2(1.0, 0.0)), f - vec2(1.0, 0.0)), u.x),
               mix(dot(random2SDF(i + vec2(0.0, 1.0)), f - vec2(0.0, 1.0)),
                   dot(random2SDF(i + vec2(1.0, 1.0)), f - vec2(1.0, 1.0)), u.x), u.y);
}

float v_noiseSDF(vec2 st, float edges) {
    vec2 i = floor(st);
    vec2 f = fract(st);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(dot(random2SDF(i + vec2(0.0, 0.0)), f - vec2(0.0, 0.0)),
                   dot(random2SDF(i + vec2(1.0, 0.0)), f - vec2(1.0, 0.0)), u.x),
               mix(dot(random2SDF(i + vec2(0.0, 1.0)), f - vec2(0.0, 1.0)),
                   dot(random2SDF(i + vec2(1.0, 1.0)), f - vec2(1.0, 1.0)), u.x), u.y);
}

float stepUpDownSDF(float begin, float end, float t) {
    return step(begin, t) - step(end, t);
}

mat2 rotateSDF(float angle) {
    return mat2(cos(angle), -sin(angle), sin(angle), cos(angle));
}

vec3 timeSDF(float u_time) {
    float period = mod(u_time, 5.0);
    vec3 t = vec3(fract(u_time/5.0), period, 1.0-fract(period));
    return t;
}

float loop_noiseSDF(vec2 st, float u_time) {
    float loopLength = 5.0;
    float transitionStart = 5.0 * 0.5;
    float t = mod(u_time, loopLength);
    float v1 = v_noiseSDF(st + t);
    float v2 = v_noiseSDF(st + t - loopLength);
    float transitionProgress = (t - transitionStart) / (loopLength - transitionStart);
    float progress = clamp(transitionProgress, 0.0, 1.0);
    return mix(v1, v2, progress);
}

vec4 renderLoopNoiseSDF(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    float seconds = 5.0;
    vec3 t = timeSDF(time);

    // Center and aspect ratio
    float aspect = uResolution.x / uResolution.y;
    st.x = st.x * aspect - aspect * 0.5 + 0.5;
    st = st * 2.0 - 1.0;

    // Rotation
    st *= rotateSDF(1.57079632675);

    // SDF
    vec2 pos = st - vec2(0.0);
    float r = 0.1;
    float a = atan(pos.y, pos.x);
    r = length(pos);
    r *= abs(cos(abs(a) * 2.0));

    t.x += fract(t.x - 0.5);
    float t1 = smoothstep(0.5, 1.0, t.x);
    float t2 = smoothstep(0.0, 0.5, t.x);

    float n = 0.5 * loop_noiseSDF(vec2(abs(a * 0.50), r) * 20.0, time);
    r += n;

    float sdf = r - 0.3;
    sdf = smoothstep(0.0, 0.001, sdf);

    // Color with palette support
    vec3 color = vec3(0.07);
    color = mix(color, vec3(1.0), sdf);
    color = mix(color, vec3(0.0), abs(st.y / 8.0));
    
    // Add palette colors
    color = mix(color, uPrimaryColor, bass * 0.5);
    color = mix(color, uSecondaryColor, high * 0.3);

    return vec4(color, 1.0);
}
