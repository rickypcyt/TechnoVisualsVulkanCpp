// @EFFECT name="Matrix Digital Rain" index=63 desc="Matrix-style falling digital characters" author="Patricio Gonzalez Vivo"

float randomMatrix(in float x) { 
    return fract(sin(x) * 43758.5453); 
}

float randomMatrix(in vec2 st) { 
    return fract(sin(dot(st.xy, vec2(12.9898, 78.233))) * 43758.5453); 
}

float randomCharMatrix(vec2 outer, vec2 inner) {
    float grid = 5.0;
    vec2 margin = vec2(0.2, 0.05);
    vec2 borders = step(margin, inner) * step(margin, 1.0 - inner);
    vec2 ipos = floor(inner * grid);
    vec2 fpos = fract(inner * grid);
    return step(0.5, randomMatrix(outer * 64.0 + ipos)) * borders.x * borders.y * step(0.01, fpos.x) * step(0.01, fpos.y);
}

vec4 renderMatrixDigitalRain(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    // Adjust aspect ratio like original
    st.y *= uResolution.y / uResolution.x;
    
    vec3 color = vec3(0.0);
    
    // Rows controlled by energy (1 to 24)
    float rows = mix(1.0, 24.0, energy);
    
    vec2 ipos = floor(st * rows);
    vec2 fpos = fract(st * rows);
    
    // Animation speed controlled by tempo and bass
    float speed = 20.0 * (1.0 + tempo + bass * 2.0);
    ipos += vec2(0.0, floor(time * speed * randomMatrix(ipos.x + 1.0)));
    
    float pct = 1.0;
    pct *= randomCharMatrix(ipos, fpos);
    
    color = vec3(pct);

    // Use user colors instead of hardcoded green
    vec3 baseColor = mix(uPrimaryColor, uSecondaryColor, 0.5);
    color *= baseColor;
    
    return vec4(color, 1.0);
}
