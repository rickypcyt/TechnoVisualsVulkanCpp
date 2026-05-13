// @EFFECT name="Reflected Turbulence" index=67 desc="Mirrored turbulence noise patterns" author="kyndinfo"

vec3 mod289Turb(vec3 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
vec2 mod289Turb(vec2 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
vec3 permuteTurb(vec3 x) { return mod289Turb(((x*34.0)+1.0)*x); }

float snoiseTurb(vec2 v) {
    const vec4 C = vec4(0.211324865405187,  // (3.0-sqrt(3.0))/6.0
                        0.366025403784439,  // 0.5*(sqrt(3.0)-1.0)
                        -0.577350269189626,  // -1.0 + 2.0 * C.x
                        0.024390243902439); // 1.0 / 41.0
    vec2 i  = floor(v + dot(v, C.yy) );
    vec2 x0 = v -   i + dot(i, C.xx);
    vec2 i1;
    i1 = (x0.x > x0.y) ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
    vec4 x12 = x0.xyxy + C.xxzz;
    x12.xy -= i1;
    i = mod289Turb(i);
    vec3 p = permuteTurb( permuteTurb( i.y + vec3(0.0, i1.y, 1.0 ))
        + i.x + vec3(0.0, i1.x, 1.0 ));

    vec3 m = max(0.5 - vec3(dot(x0,x0), dot(x12.xy,x12.xy), dot(x12.zw,x12.zw)), 0.0);
    m = m*m ;
    m = m*m ;
    vec3 x = 2.0 * fract(p * C.www) - 1.0;
    vec3 h = abs(x) - 0.5;
    vec3 ox = floor(x + 0.5);
    vec3 a0 = x - ox;
    m *= 1.79284291400159 - 0.85373472095314 * ( a0*a0 + h*h );
    vec3 g;
    g.x  = a0.x  * x0.x  + h.x  * x0.y;
    g.yz = a0.yz * x12.xz + h.yz * x12.yw;
    return 130.0 * dot(m, g);
}

float turbulenceReflect(vec2 st, float octaves) {
    float value = 0.0;
    float amplitude = 1.0;
    for (int i = 0; i < 6; i++) {
        if (float(i) >= octaves) break;
        value += amplitude * abs(snoiseTurb(st));
        st *= 2.0;
        amplitude *= 0.5;
    }
    return value;
}

vec4 renderReflectedTurbulence(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    st.x *= uResolution.x / uResolution.y;
    
    // Reflection based on energy
    float reflectThreshold = mix(0.3, 0.7, energy);
    st.x = (st.x > reflectThreshold) ? st.x : 1.0 - st.x;
    st.y = (st.y > reflectThreshold) ? st.y : 1.0 - st.y;
    
    // Turbulence displace controlled by tempo and bass
    float displacement = mix(0.2, 0.8, tempo + bass);
    st.x += turbulenceReflect(st, 4.0 + mid * 2.0) * displacement * 0.5;
    st.y += turbulenceReflect(st + vec2(1.0), 4.0 + high * 2.0) * displacement * 0.2;
    
    // Scale controlled by energy
    float scale = mix(3.0, 10.0, energy);
    float v = turbulenceReflect(st * scale, 6.0);
    
    // Color mixing with palette
    vec3 color = mix(uSecondaryColor, uPrimaryColor, v);
    color += vec3(high * 0.3, mid * 0.2, bass * 0.3) * v;
    
    return vec4(color, 1.0);
}
