// @EFFECT name="Ikeda Data Stream" index=68 desc="Scrolling data stream with RGB offset" author="Patricio Gonzalez Vivo"

float randomDataStream(in float x) {
    return fract(sin(x) * 1e4);
}

float randomDataStream(in vec2 st) {
    return fract(sin(dot(st.xy, vec2(12.9898, 78.233))) * 43758.5453123);
}

float patternDataStream(vec2 st, vec2 v, float t) {
    vec2 p = floor(st + v);
    return step(t, randomDataStream(100.0 + p * 0.000001) + randomDataStream(p.x) * 0.5);
}

vec4 renderIkedaDataStream(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    st.x *= uResolution.x / uResolution.y;

    // Grid density controlled by energy
    vec2 grid = vec2(mix(50.0, 150.0, energy), mix(25.0, 75.0, energy));
    st *= grid;

    vec2 ipos = floor(st);  // integer
    vec2 fpos = fract(st);  // fraction

    // Speed controlled by tempo and bass
    vec2 vel = vec2(time * 2.0 * max(grid.x, grid.y) * (1.0 + tempo));
    vel *= vec2(-1.0, 0.0) * randomDataStream(1.0 + ipos.y + bass); // direction

    // Assign a random value base on the integer coord
    vec2 offset = vec2(0.1, 0.0);

    // Use mid/high for threshold variation
    float threshold = 0.5 + mid * 0.3 + high * 0.2;

    vec3 color = vec3(0.0);
    color.r = patternDataStream(st + offset, vel, threshold);
    color.g = patternDataStream(st, vel, threshold);
    color.b = patternDataStream(st - offset, vel, threshold);

    // Margins
    color *= step(0.2, fpos.y);

    // Apply white color to active pixels, black background
    color = vec3(step(0.1, color.r + color.g + color.b));
    color *= vec3(1.0); // White data stream
    
    // Add subtle palette tint on high energy
    color = mix(color, uPrimaryColor, high * 0.3);

    return vec4(color, 1.0);
}
