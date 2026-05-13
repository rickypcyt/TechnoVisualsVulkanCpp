// @EFFECT name="Cell Rings" index=71 desc="Animated cellular ring patterns with noise" author="Will Stallwood"

float randomCell(vec2 st) {
    return fract(sin(dot(st.xy, vec2(-30.950, -10.810))) * 43758.5453123);
}

vec2 random2Cell(vec2 p) {
    return fract(sin(vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)))) * 43758.5453);
}

float noiseCell(vec2 st) {
    vec2 i = floor(st);
    vec2 f = fract(st);
    float a = randomCell(i);
    float b = randomCell(i + vec2(1.0, 0.0));
    float c = randomCell(i + vec2(0.0, 1.0));
    float d = randomCell(i + vec2(1.0, 1.0));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(a, b, u.x) + (c - a) * u.y * (1.0 - u.x) + (d - b) * u.x * u.y;
}

mat2 rotateCell(float angle) {
    return mat2(cos(angle), -sin(angle), sin(angle), cos(angle));
}

float cellSDF(vec2 st, float time, float energy) {
    float seconds = mix(3.0, 8.0, 1.0 - energy * 0.5);
    float t = time / seconds;
    st = st * 2.0 - 1.0;
    vec2 r = random2Cell(st);
    float angle = 3.14159265359 * r.y;
    st *= rotateCell(angle + 6.283185307 * t * 40.0 * energy);
    float ring_size = mix(0.01 * sin(6.283185307 * t), 0.01 * sin(6.283185307 * t * 2.0) + 0.3 * floor(st.x * 10.0) / 10.0, cos(6.283185307 * t));
    float d = length(st) - 0.1 - 0.5 * sin(6.283185307 * log(t + 0.001));
    d = abs(d + ring_size * -abs(sin(6.283185307 * t * angle + t))) - ring_size;
    float g = st.y;
    d *= pow(abs(g), exp(sin(6.283185307 * t)));
    return d;
}

vec4 renderCellRings(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    // Center and aspect ratio
    float aspect = uResolution.x / uResolution.y;
    st.x = st.x * aspect - aspect * 0.5 + 0.5;

    // Timing
    float seconds = mix(4.0, 10.0, 1.0 - energy);
    float t = fract(time / seconds);

    // SDF
    float sdf = cellSDF(st, time, energy);

    // Color - black background with colored rings
    vec3 color = vec3(0.0);
    color = mix(color, uPrimaryColor, 1.0 - smoothstep(0.0, 0.002 * (1.0 + bass), sdf));

    return vec4(color, 1.0);
}
