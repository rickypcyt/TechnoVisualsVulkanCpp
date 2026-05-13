// @EFFECT name="Corona Virus" index=72 desc="Raymarched virus particles with lipid bilayer" author="Martijn Steinrucken"

mat2 RotCV(float a) {
    float s = sin(a);
    float c = cos(a);
    return mat2(c, -s, s, c);
}

float sminCV(float a, float b, float k) {
    float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) - k * h * (1.0 - h);
}

float sdCapsuleCV(vec3 p, vec3 a, vec3 b, float r) {
    vec3 ab = b - a;
    vec3 ap = p - a;
    float t = dot(ab, ap) / dot(ab, ab);
    t = clamp(t, 0.0, 1.0);
    vec3 c = a + t * ab;
    return length(p - c) - r;
}

float N31CV(vec3 p) {
    vec3 a = fract(vec3(p) * vec3(213.897, 653.453, 253.098));
    a += dot(a, a + 79.76);
    return fract(a.x * a.y * a.z);
}

float N21CV(vec2 p) {
    vec3 a = fract(vec3(p.xyx) * vec3(213.897, 653.453, 253.098));
    a += dot(a, a.yzx + 79.76);
    return fract((a.x + a.y) * a.z);
}

vec3 WorldToCubeCV(vec3 p) {
    vec3 ap = abs(p);
    vec3 sp = sign(p);
    float m = max(ap.x, max(ap.y, ap.z));
    vec3 st;
    if (m == ap.x)
        st = vec3(p.zy, 1.0 * sp.x);
    else if (m == ap.y)
        st = vec3(p.zx, 2.0 * sp.y);
    else
        st = vec3(p.xy, 3.0 * sp.z);
    st.xy /= m;
    st.xy *= (1.45109572583 - 0.451095725826 * abs(st.xy));
    return st;
}

float LipidCV(vec3 p, float twist, float scale) {
    vec3 n = sin(p * 20.0) * 0.2;
    p *= scale;
    p.xz *= RotCV(p.y * 0.3 * twist);
    p.x = abs(p.x);
    float d = length(p + n) - 2.0;
    float s = length(p.xz - vec2(1.5, 0.0)) - 0.05 * scale + max(0.4, p.y);
    d = sminCV(d, s * 0.9, 0.4);
    return d / scale;
}

float sdTentacleCV(vec3 p) {
    float offs = sin(p.x * 50.0) * sin(p.y * 30.0) * sin(p.z * 20.0);
    p.x += sin(p.y * 10.0 + uTime) * 0.02;
    p.y *= 0.2;
    float d = sdCapsuleCV(p, vec3(0.0, 0.1, 0.0), vec3(0.0, 0.8, 0.0), 0.04);
    p.xz = abs(p.xz);
    d = min(d, sdCapsuleCV(p, vec3(0.0, 0.8, 0.0), vec3(0.1, 0.9, 0.1), 0.01));
    d += offs * 0.01;
    return d;
}

float ParticleCV(vec3 p, float scale, float amount, float time) {
    vec3 st = WorldToCubeCV(p);
    vec3 cPos = vec3(st.x, length(p), st.y);
    vec3 tPos = cPos;
    cPos.xz *= scale;
    vec2 uv = fract(cPos.xz) - 0.5;
    vec2 id = floor(cPos.xz);
    float n = N21CV(id);
    float t = (time + st.z + n * 123.32) * 1.3;
    float wobble = sin(t) + sin(1.3 * t) * 0.4;
    wobble /= 1.4;
    wobble = pow(abs(wobble), 3.0);
    wobble *= amount / scale;
    vec3 ccPos = vec3(uv.x, cPos.y, uv.y);
    vec3 sPos = vec3(0.0, 3.5 + wobble, 0.0);
    vec3 pos = ccPos - sPos;
    pos.y *= scale / 2.0;
    float d = LipidCV(pos, n, 10.0) / scale;
    d = min(d, length(p) - 0.2 * scale);
    float tent = sdTentacleCV(tPos);
    d = min(d, tent);
    return d;
}

float GetDistCV(vec3 p, float time, float energy, float bass) {
    float scale = 8.0;
    p.z += time * (0.5 + bass);
    vec3 id = floor(p / 10.0);
    p = mod(p, vec3(10.0)) - 5.0;
    float n = N21CV(id.xz);
    p.xz *= RotCV(time * 0.2 * (n - 0.5));
    p.yz *= RotCV(time * 0.2 * (N21CV(id.zx) - 0.5));
    scale = mix(4.0, 16.0, N21CV(id.xz));
    n = N31CV(id);
    float filledCells = 0.3 + energy * 0.4;
    if (n > filledCells) {
        return max(0.0, 5.0 - max(p.x, max(p.y, p.z))) + 0.1;
    }
    p += sin(p.x + time) * 0.1 + sin(p.y * p.z + time) * 0.05;
    float surf = sin(scale + time * 0.2) * 0.5 + 0.5;
    surf *= surf;
    surf *= 4.0;
    surf += 2.0;
    float d = ParticleCV(p, scale, surf, time);
    p.xz *= RotCV(0.78 + time * 0.08);
    p.zy *= RotCV(0.5);
    d = sminCV(d, ParticleCV(p, scale, surf, time), 0.02);
    return d;
}

float RayMarchCV(vec3 ro, vec3 rd, float time, float energy, float bass) {
    float dO = 0.0;
    float cone = 0.0005;
    for (int i = 0; i < 100; i++) {
        vec3 p = ro + rd * dO;
        float dS = GetDistCV(p, time, energy, bass);
        dO += dS;
        if (dO > 40.0 || abs(dS) < 0.01 + dO * cone) break;
    }
    return dO;
}

vec3 GetNormalCV(vec3 p, float time, float energy, float bass) {
    float d = GetDistCV(p, time, energy, bass);
    vec2 e = vec2(0.001, 0.0);
    vec3 n = d - vec3(
        GetDistCV(p - e.xyy, time, energy, bass),
        GetDistCV(p - e.yxy, time, energy, bass),
        GetDistCV(p - e.yyx, time, energy, bass));
    return normalize(n);
}

vec3 RCV(vec2 uv, vec3 p, vec3 l, vec3 up, float z) {
    vec3 f = normalize(l - p);
    vec3 r = normalize(cross(up, f));
    vec3 u = cross(f, r);
    vec3 c = p + f * z;
    vec3 i = c + uv.x * r + uv.y * u;
    vec3 d = normalize(i - p);
    return d;
}

vec4 renderCoronaVirus(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    vec2 uv = (st - 0.5) * vec2(uResolution.x / uResolution.y, 1.0);
    
    float t = time * (1.0 + tempo);
    
    vec3 col = vec3(0.0);
    
    vec3 ro = vec3(0.0, 0.0, -1.0);
    ro.yz *= RotCV(-0.0);
    ro.xz *= RotCV(time * 0.05 * energy);
    
    vec3 up = vec3(0.0, 1.0, 0.0);
    up.xy *= RotCV(sin(t * 0.1));
    vec3 rd = RCV(uv, ro, vec3(0.0, 0.0, 0.0), up, 0.5);
    
    ro.x += 5.0;
    ro.xy *= RotCV(t * 0.1);
    ro.xy -= 5.0;
    
    float d = RayMarchCV(ro, rd, t, energy, bass);
    
    float bg = rd.y * 0.5 + 0.3;
    float poleDist = length(rd.xz);
    float poleMask = smoothstep(0.5, 0.0, poleDist);
    bg += sign(rd.y) * poleMask;
    
    float a = atan(rd.x, rd.z);
    bg += (sin(a * 5.0 + t + rd.y * 2.0) + sin(a * 7.0 - t + rd.y * 2.0)) * 0.2;
    float rays = (sin(a * 5.0 + t * 2.0 + rd.y * 2.0) * sin(a * 37.0 - t + rd.y * 2.0)) * 0.5 + 0.5;
    bg *= mix(1.0, rays, 0.25 * poleDist * (sin(t * 0.1) * 0.5 + 0.5));
    col += bg;
    
    if (d < 40.0) {
        vec3 p = ro + rd * d;
        vec3 n = GetNormalCV(p, t, energy, bass);
        p = mod(p, vec3(10.0)) - 5.0;
        float ao = smoothstep(2.96, 3.7, length(p));
        col += (n.y * 0.5 + 0.5) * ao * 2.0;
        col *= smoothstep(-1.0, 6.0, p.y);
        
        // Add palette color on high frequencies
        col = mix(col, uPrimaryColor, high * 0.3);
    }
    
    col = mix(col, vec3(bg), smoothstep(0.0, 40.0, d));
    
    // Color grading
    col *= mix(vec3(1.0, 0.9, 0.8), uSecondaryColor, mid * 0.3);
    
    return vec4(col, 1.0);
}
