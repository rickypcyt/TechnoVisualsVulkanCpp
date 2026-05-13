#version 330 core
in vec2 vUV;
out vec4 FragColor;

uniform vec2 uResolution;
uniform float uTime;
uniform float uTempo;
uniform float uEnergy;
uniform float uBass;
uniform float uMid;
uniform float uHigh;
uniform float uIntensity;
uniform int uMode;
uniform vec3 uPrimaryColor;
uniform vec3 uSecondaryColor;
uniform float uColorBlend;
uniform float uCameraZoom;
uniform float uCameraOffsetX;
uniform float uCameraOffsetY;

const float PI = 3.14159265359;

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

vec2 hash2(vec2 p) {
    p = vec2(dot(p, vec2(127.1, 311.7)),
             dot(p, vec2(269.5, 183.3)));
    return fract(sin(p) * 43758.5453);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(a, b, u.x) + (c - a) * u.y * (1.0 - u.x) + (d - b) * u.x * u.y;
}

float fbm(vec2 p) {
    float value = 0.0;
    float amp = 0.5;
    mat2 m = mat2(1.7, 1.2, -1.2, 1.7);
    for (int i = 0; i < 5; ++i) {
        value += amp * noise(p);
        p = m * p + vec2(0.21, 0.17);
        amp *= 0.5;
    }
    return value;
}

vec2 rotate(vec2 p, float angle) {
    float s = sin(angle);
    float c = cos(angle);
    return mat2(c, -s, s, c) * p;
}

vec3 palette(float t, vec3 a, vec3 b, vec3 c, vec3 d) {
    return a + b * cos(2.0 * PI * (c * t + d));
}

const float kTwoPI = 6.28318530718;
