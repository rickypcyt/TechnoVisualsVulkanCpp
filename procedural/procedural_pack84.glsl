// @EFFECT name="Stylized Oscilloscope" index=110 desc="Oscilloscope with grid pattern and audio wave visualization" author="System"

const float OSC_PI = 3.14159;
const float OSC_TAU = 6.2831;
const float OSC_A4 = 440.0;
const float OSC_A3 = 220.0;

mat2 oscRotationMatrix(float angle) {
    angle *= OSC_PI / 180.0;
    float s = sin(angle), c = cos(angle);
    return mat2(c, -s, s, c);
}

float oscPitch(float i) {
    return 415.0 * pow(2.0, (i - 60.0) / 12.0);
}

float oscWave(float f, float a, float time) {
    float w = 2.0 * OSC_PI * f * time;
    float v = pow(max(0.0, (sin(w) + 1.0) / 2.0), 2.0) - 0.5;
    return a * v;
}

vec2 oscFreq(float time) {
    time *= 5.0;
    float f = OSC_A3;
    
    vec2 fr = vec2(
        oscWave(OSC_A3 * 2.0, 0.5, time) + oscWave(OSC_A4 * 4.0, 0.5, time),
        oscWave(OSC_A3 * (2.0 + OSC_PI / 5000.0), 0.5, time) + oscWave(OSC_A3 * 1.0, 0.5, time)
    );
    return fr * oscRotationMatrix(time * 40.0) * 0.5;
}

float sdBox(vec2 p, vec2 b) {
    vec2 d = abs(p) - b;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

float sdSegment(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h);
}

const float OSC_SAMPLES = 600.0;

float sdSound(vec2 uv, float time) {
    float hits = 0.0;
    float t = time;
    vec2 f = oscFreq(t);
    vec2 prev;
    float dt = 0.016 / OSC_SAMPLES; // Approximate delta time
    
    for (float i = 1.0; i < OSC_SAMPLES; i++) {
        t += dt;
        prev = f;
        f = oscFreq(t);
        hits += min(1.0, 1.0 / (sdSegment(uv * (1.0 - 0.5), prev, f) * 2500.0));
    }
    
    return 200.0 * hits / OSC_SAMPLES;
}

vec2 oscCube(vec2 uv) {
    return mod((uv + 0.5) * 8.0, vec2(1.0)) - 0.5;
}

vec4 renderStylizedOscilloscope(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    vec2 uv = st - 0.5;
    uv.x *= uResolution.x / max(uResolution.y, 1.0);
    
    vec3 col = vec3(0.0, 0.0, 0.0);
    col = mix(col, vec3(0.0, 0.0, 0.0), 1.0 - length(uv));
    col = mix(col, vec3(0.031, 0.031, 0.031), float(sdBox(oscCube(uv), vec2(0.49)) <= 0.0));
    
    // Use audio energy to modulate the sound visualization
    float energyMod = 1.0 + energy * 2.0;
    col = mix(col, vec3(0.404, 0.984, 0.396), sdSound(uv * 3.0 * energyMod, time));
    
    vec2 puv = st;
    puv *= 1.0 - puv.yx;
    col *= pow(puv.x * puv.y * 30.0, 0.5);
    
    // Apply blue tint as in original
    vec3 finalCol = col * vec3(0.0, 0.667, 1.0);
    
    // Add audio-reactive glow
    finalCol += vec3(0.0, 0.5, 1.0) * energy * 0.3;
    
    return vec4(clamp(finalCol, 0.0, 1.0), 1.0);
}
