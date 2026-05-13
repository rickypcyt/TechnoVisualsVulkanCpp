// @EFFECT name="IChing Hexagrams" index=66 desc="Ancient IChing hexagram patterns with animated transitions" author="Patricio Gonzalez Vivo"

float shapeIChing(vec2 st, float N) {
    st = st * 2.0 - 1.0;
    float a = atan(st.x, st.y) + 3.14159265359;
    float r = 6.28318530718 / N;
    return abs(cos(floor(0.5 + a / r) * r - a) * length(st));
}

float boxIChing(vec2 st, vec2 size) {
    return shapeIChing(st * size, 4.0);
}

float rectIChing(vec2 _st, vec2 _size) {
    _size = vec2(0.5) - _size * 0.5;
    vec2 uv = smoothstep(_size, _size + vec2(1e-4), _st);
    uv *= smoothstep(_size, _size + vec2(1e-4), vec2(1.0) - _st);
    return uv.x * uv.y;
}

float hexIChing(vec2 st, float a, float b, float c, float d, float e, float f) {
    st = st * vec2(2.0, 6.0);

    vec2 fpos = fract(st);
    vec2 ipos = floor(st);

    if (ipos.x == 1.0) fpos.x = 1.0 - fpos.x;
    if (ipos.y < 1.0) {
        return mix(boxIChing(fpos, vec2(0.84, 1.0)), boxIChing(fpos - vec2(0.03, 0.0), vec2(1.0)), a);
    } else if (ipos.y < 2.0) {
        return mix(boxIChing(fpos, vec2(0.84, 1.0)), boxIChing(fpos - vec2(0.03, 0.0), vec2(1.0)), b);
    } else if (ipos.y < 3.0) {
        return mix(boxIChing(fpos, vec2(0.84, 1.0)), boxIChing(fpos - vec2(0.03, 0.0), vec2(1.0)), c);
    } else if (ipos.y < 4.0) {
        return mix(boxIChing(fpos, vec2(0.84, 1.0)), boxIChing(fpos - vec2(0.03, 0.0), vec2(1.0)), d);
    } else if (ipos.y < 5.0) {
        return mix(boxIChing(fpos, vec2(0.84, 1.0)), boxIChing(fpos - vec2(0.03, 0.0), vec2(1.0)), e);
    } else if (ipos.y < 6.0) {
        return mix(boxIChing(fpos, vec2(0.84, 1.0)), boxIChing(fpos - vec2(0.03, 0.0), vec2(1.0)), f);
    }
    return 0.0;
}

float hexIChing(vec2 st, float N) {
    float b[6];
    float remain = floor(mod(N, 64.0));
    for(int i = 0; i < 6; i++) {
        b[i] = 0.0;
        b[i] = step(1.0, mod(remain, 2.0));
        remain = ceil(remain / 2.0);
    }
    return hexIChing(st, b[0], b[1], b[2], b[3], b[4], b[5]);
}

float randomIChing(in vec2 _st) { 
    return fract(sin(dot(_st.xy, vec2(12.9898, 78.233))) * 43758.5453123);
}

vec4 renderIChingHexagrams(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    st.y *= uResolution.y / uResolution.x;

    // Grid density controlled by energy
    float gridScale = mix(5.0, 15.0, energy);
    st *= gridScale;
    vec2 fpos = fract(st);
    vec2 ipos = floor(st);

    // Animation speed controlled by tempo and bass
    float t = time * (5.0 + tempo * 5.0 + bass * 3.0);
    float df = 1.0;
    df = hexIChing(fpos, ipos.x + ipos.y + t * randomIChing(ipos)) + (1.0 - rectIChing(fpos, vec2(0.7)));

    vec3 color = mix(vec3(0.0), uPrimaryColor, step(0.7, df));
    
    // Add secondary color accents on high frequencies
    color = mix(color, uSecondaryColor, high * step(0.8, df) * 0.5);
    
    return vec4(color, 1.0);
}
