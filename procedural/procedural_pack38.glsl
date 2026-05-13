// @EFFECT name="Ikeda Digits" index=64 desc="Falling digital numbers like Ikeda data stream" author="Patricio Gonzalez Vivo"

float randomIkeda(in float x) { 
    return fract(sin(x) * 43758.5453); 
}

float randomIkeda(vec2 p) { 
    return fract(1e4 * sin(17.0 * p.x + p.y * 0.1) * (0.1 + abs(sin(p.y * 13.0 + p.x)))); 
}

float binIkeda(vec2 ipos, float n) {
    float remain = mod(n, 33554432.0);
    for(float i = 0.0; i < 25.0; i++) {
        if (floor(i / 3.0) == ipos.y && mod(i, 3.0) == ipos.x) {
            return step(1.0, mod(remain, 2.0));
        }
        remain = ceil(remain / 2.0);
    }
    return 0.0;
}

float charIkeda(vec2 st, float n) {
    st.x = st.x * 2.0 - 0.5;
    st.y = st.y * 1.2 - 0.1;

    vec2 grid = vec2(3.0, 5.0);

    vec2 ipos = floor(st * grid);
    vec2 fpos = fract(st * grid);

    n = floor(mod(n, 10.0));
    float digit = 0.0;
    if (n < 1.0) { digit = 31600.0; }
    else if (n < 2.0) { digit = 9363.0; }
    else if (n < 3.0) { digit = 31184.0; }
    else if (n < 4.0) { digit = 31208.0; }
    else if (n < 5.0) { digit = 23525.0; }
    else if (n < 6.0) { digit = 29672.0; }
    else if (n < 7.0) { digit = 29680.0; }
    else if (n < 8.0) { digit = 31013.0; }
    else if (n < 9.0) { digit = 31728.0; }
    else if (n < 10.0) { digit = 31717.0; }
    float pct = binIkeda(ipos, digit);

    vec2 borders = vec2(1.0);
    borders *= step(0.0, st) * step(0.0, 1.0 - st);

    return step(0.5, 1.0 - pct) * borders.x * borders.y;
}

vec4 renderIkedaDigits(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    st.x *= uResolution.x / uResolution.y;

    // Rows controlled by energy
    float rows = mix(10.0, 40.0, energy);
    vec2 ipos = floor(st * rows);
    vec2 fpos = fract(st * rows);

    // Animation speed controlled by tempo and bass
    float speed = 20.0 * (1.0 + tempo + bass * 2.0);
    ipos += vec2(0.0, floor(time * speed * randomIkeda(ipos.x + 1.0)));
    
    float pct = randomIkeda(ipos);
    vec3 color = vec3(charIkeda(fpos, 100.0 * pct));
    
    // Highlight effect
    color = mix(color, vec3(color.r, 0.0, 0.0), step(0.99, pct));

    // Use user colors instead of hardcoded green/black
    vec3 digitColor = mix(uPrimaryColor, uSecondaryColor, 0.5);
    vec3 bgColor = uSecondaryColor * 0.3;

    color *= digitColor;
    color += bgColor * (1.0 - length(color));
    
    return vec4(color, 1.0);
}
