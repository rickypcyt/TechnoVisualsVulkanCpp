// @EFFECT name="Glass Refraction Field" index=62 desc="Raymarched glass cubes with refraction and reflection effects" author="Matthias Hurrle"

vec3 hueGlass(float a) {
    return 0.5 + 0.2 * cos(10.3 * a + vec3(0.0, 23.0, 21.0));
}

mat2 rotGlass(float a) {
    return mat2(cos(a), -sin(a), sin(a), cos(a));
}

float sylGlass(vec2 p, float r) {
    return length(p) - r;
}

vec3 dflameGlass(vec2 uv, float time) {
    vec2 n = vec2(0.0);
    vec2 q = vec2(0.0);
    
    uv *= 0.875;
    
    float d = dot(uv, uv);
    float s = 9.0;
    float a = 0.02;
    float b = sin(time * 0.4 - d * 4.0) * 0.9;
    float t = time * 4.0;
    
    uv *= rotGlass(sin(6.0 + t * 0.05) * 0.8 - 0.567);
    uv.y -= t * 0.05;
    
    mat2 m = mat2(0.6, 1.2, -1.2, 0.6);
    for (float i = 0.0; i < 30.0; i++) {
        n *= m;
        q = uv * s - t + b + i + n;
        a += dot(cos(q) / s, vec2(0.2));
        n += sin(q);
        s *= 1.2;
    }
    
    vec3 col = vec3(4.0, 2.0, 1.0) * (a + 0.2) + a + a - d;
    col = exp(-col * 8.0);
    col = abs(col);
    col = sqrt(col);
    col = exp(-col * 4.0);
    
    return col;
}

float tickGlass(float t, float e) {
    return floor(t) + pow(smoothstep(0.0, 1.0, fract(t)), e);
}

float boxGlass(vec3 p, vec3 s, float r) {
    p = abs(p) - s;
    return length(max(p, 0.0)) + min(0.0, max(max(p.x, p.y), p.z)) - r;
}

float mapGlass(vec3 p, float time) {
    const float n = 5.5;
    p.yz = (fract(p.yz / n) - 0.5) * n;
    p.xz = (p.xz - n * clamp(round(p.xz / n), -10.0, 10.0));
    
    float T = mod(time, 90.0) * 0.45;
    p.yz *= rotGlass(sin(tickGlass(T, 1.0)));
    p.xz *= rotGlass(sin(tickGlass(T, 1.0)));
    
    float d = 1e5;
    float bx = boxGlass(p, vec3(0.85), 0.125);
    d = min(d, bx);
    
    return d;
}

vec3 normGlass(vec3 p, float time) {
    vec2 e = vec2(1e-2, 0.0);
    float d = mapGlass(p, time);
    vec3 n = d - vec3(
        mapGlass(p - e.xyy, time),
        mapGlass(p - e.yxy, time),
        mapGlass(p - e.yyx, time)
    );
    return normalize(n);
}

vec3 dirGlass(vec2 uv, vec3 ro, vec3 t, float z) {
    vec3 up = vec3(0.0, 1.0, 0.0);
    vec3 f = normalize(t - ro);
    vec3 r = normalize(cross(up, f));
    vec3 u = cross(f, r);
    vec3 c = f * z;
    vec3 i = c + uv.x * r + uv.y * u;
    return normalize(i);
}

vec4 renderGlassRefractionField(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    vec2 uv = st - 0.5;
    uv.x *= uResolution.x / uResolution.y;
    
    float T = mod(time + energy * 2.0, 90.0) * 0.45;
    
    vec3 col = vec3(0.0);
    vec3 tg = vec3(0.0, 0.0, T * 10.0);
    vec3 ro = vec3(0.0, 0.0, tg.z - 20.0);
    vec3 rd = dirGlass(uv, ro, tg, 1.0);
    
    // Camera rotation based on bass
    float bassRot = cos(T * 0.2) * 3.14159 * (0.5 + bass * 0.5);
    ro.xy *= rotGlass(bassRot);
    rd.xy *= rotGlass(bassRot);
    
    // Secondary rotation based on mid
    float midRot = sin(T * 0.2) * 3.14159 * mid;
    rd.xz *= rotGlass(midRot);
    
    vec3 l = normalize(ro - vec3(1.0, 2.0, 3.0));
    vec3 p = ro;
    
    const float steps = 60.0;
    const float maxd = 30.0;
    
    float i = 0.0;
    float dd = 0.0;
    float side = 1.0;
    float e = 1.0;
    
    for (; i < steps; i++) {
        float d = mapGlass(p, time) * side;
        
        if (d < 1e-3) {
            vec3 n = normGlass(p, time) * side;
            // Removed fog effect for better sharpness
            float diff = max(0.0, dot(normalize(ro - p), n));
            float fres = clamp(dot(-rd, n), 0.0, 1.0);
            
            vec3 h = normalize(l - rd);
            vec3 pal = hueGlass(diff);
            
            col += e
                * (1.0 - max(0.0, i / 200.0))
                * diff
                * (5.0 * pow(max(0.0, dot(n, h)), 64.0) +
                   0.5 * pow(max(0.0, fres), 32.0))
                * pal;
            
            side = -side;
            vec3 rdo = refract(rd, n, 1.0 + side * 0.45);
            
            if (dot(rdo, rdo) == 0.0) {
                rdo = reflect(rd, n);
            }
            
            rd = rdo;
            d = 9e-2;
            e *= 0.925;
        }
        
        if (dd > maxd) {
            dd = maxd;
            break;
        }
        
        p += rd * d;
        dd += d;
    }
    
    p = ro + rd * maxd;
    float ends = pow(abs(rd.z), 7.0);
    col += ends * dflameGlass(abs(p.xy * 0.05), time);
    
    // Apply palette and energy
    col *= 2.0;
    col *= mix(uPrimaryColor, vec3(1.0), 0.3);
    col += uSecondaryColor * energy * 0.2;
    col = clamp(col, 0.0, 1.0);
    
    return vec4(col, 1.0);
}
