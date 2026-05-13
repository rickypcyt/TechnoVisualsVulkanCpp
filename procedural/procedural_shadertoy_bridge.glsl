// Shadertoy compatibility bridge and helper snippets

#ifndef SHADERTOY_BRIDGE_APPLIED
#define SHADERTOY_BRIDGE_APPLIED

#ifndef iTime
#define iTime uTime
#endif

#ifndef iResolution
#define iResolution vec3(uResolution, 1.0)
#endif

#ifndef iChannel0
uniform sampler2D iChannel0;
#endif

#ifndef iChannel1
uniform sampler2D iChannel1;
#endif

#ifndef backbuffer
#define backbuffer iChannel0
#endif
#ifndef resolution
#define resolution (iResolution.xy)
#endif
#ifndef time
#define time iTime
#endif
#ifndef saturate
#define saturate(x) clamp(x, 0.0, 1.0)
#endif
#ifndef linearstep
#define linearstep(edge0, edge1, x) saturate(((x) - (edge0)) / ((edge1) - (edge0)))
#endif
#ifndef remap
#define remap(x, a, b, c, d) mix(c, d, ((x) - (a)) / ((b) - (a)))
#endif
#ifndef remapc
#define remapc(x, a, b, c, d) mix(c, d, linearstep(a, b, x))
#endif

float font(vec2 uv, vec2 id) {
    uv = remapc(uv, vec2(0.0), vec2(1.0), id, id + 1.0) / 16.0;
    const float w = 0.2;
    return smoothstep(0.55, 0.45, textureGrad(iChannel1, uv, dFdx(uv), dFdy(uv)).a);
}

float pinieon(vec2 uv) {
    uv = fract(uv);
    uv = 0.5 + (uv - 0.5) * 0.8;
    vec2 ruv = uv * 5.0;
    vec2 fuv = fract(ruv);
    vec2 iuv = floor(ruv);
    int idx = int(ruv.x + 2.0) % 3;
    vec2 id;
    if (idx == 0) id = vec2(1.0, 12.0);
    else if (idx == 1) id = vec2(15.0, 13.0);
    else id = vec2(2.0, 12.0);
    return font(0.5 + (fuv - 0.5) * 0.7, id) * float(iuv.y == 2.0) * step(0.5, iuv.x) * step(iuv.x, 3.5);
}

float hjct(vec2 uv) {
    uv = fract(uv);
    uv = 0.5 + (uv - 0.5) * 0.8;
    vec2 ruv = uv * 5.0;
    vec2 fuv = fract(ruv);
    vec2 iuv = floor(ruv);
    int idx = int(ruv.x + 2.0) % 3;
    vec2 id;
    if (idx == 0) id = vec2(1.0, 12.0);
    else if (idx == 1) id = vec2(15.0, 13.0);
    else id = vec2(2.0, 12.0);
    return font(0.5 + (fuv - 0.5) * 0.7, id) * float(iuv.y == 2.0) * step(0.5, iuv.x) * step(iuv.x, 3.5);
}

#define sat(x) clamp(x, 0.0, 1.0)
#define norm(x) normalize(x)
#define rep(i, n) for (int i = 0; i < (n); ++i)
const float pi = acos(-1.0);
const float tau = 2.0 * pi;

vec3 hash(vec3 x) {
    uvec3 v = floatBitsToUint(x);
    v = v * 20240413u + 1212121212u;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v ^= v >> 16u;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    return vec3(v) / float(-1u);
}

mat2 rot(float a) {
    float s = sin(a);
    float c = cos(a);
    return mat2(c, s, -s, c);
}

mat3 bnt(vec3 T) {
    T = norm(T);
    vec3 N = vec3(0.0, 1.0, 0.0);
    vec3 B = norm(cross(N, T));
    N = norm(cross(T, B));
    return mat3(B, N, T);
}

float iplane(vec3 ro, vec3 rd, vec3 pd, float w) {
    pd = norm(pd);
    float l = -(dot(ro, pd) + w) / dot(rd, pd);
    return (l < 0.0 ? 1e5 : l);
}

float bpm = 125.0;
float alt, lt, tr, bt;
#define sc(x) hash(vec3(1.2, x, bt))

float fui(vec2 suv, float s) {
    s = 1.2;
    suv -= alt * 0.1;
    suv *= rot(floor(sc(3).x * 4.0) * pi / 2.0);
    vec2 ruv = suv;
    rep(i, 4) {
        if (hash(vec3(floor(ruv) + s, i)).x < 0.5) {
            ruv *= 2.0;
        } else {
            break;
        }
    }

    vec3 h = hash(vec3(floor(ruv) + 1.2, s));
    vec2 fuv = fract(ruv);
    float c = 0.0;
    float b = sc(0).x;
    vec2 au = abs((fuv * 2.0 - 1.0) * rot(floor(h.z * 4.0) * pi / 4.0));
    if (b < 0.2) {
        c = pinieon(suv);
    } else if (b < 0.4) {
        c = step(fract(dot(vec2(1.0), suv)), 0.1);
    } else {
        c = step(max(au.x, au.y), 0.4) * step(min(au.x, au.y), 0.05) * step(h.x, 0.5);
    }
    return c;
}

float march(vec3 ro, vec3 rd) {
    int n = 16;
    float l = 1e9;
    rep(i, n) {
        float fi = (float(i) + 0.5) / float(n);
        vec3 pd = norm(tan(hash(vec3(bt, i, 1.2)) * 2.0 - 1.0));
        if (sc(0).z < 0.3) {
            pd = vec3(0.0, 0.0, 1.0);
        }
        float w = mix(-5.0, 5.0, fi);
        float d = iplane(ro, rd, pd, w);
        vec3 rp = rd * d + ro;
        vec2 uv = (rp.x * pd.zy + rp.y * pd.xz + rp.z * pd.xy) * 0.5;
        float s = fui(uv, w);
        if (s > 0.0) {
            l = min(l, d);
        }
    }

    return l;
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 fc = fragCoord;
    vec2 res = resolution;
    vec2 uv = fc / res;
    vec2 asp = res / min(res.x, res.y);
    vec2 asp2 = res / max(res.x, res.y);

    alt = lt = time * bpm / 60.0;
    tr = 1.0 - exp(-3.0 * fract(lt));
    bt = floor(lt);
    lt = tr + bt;

    bool tomaru = int(bt / 4.0) % 2 == 0;
    if (tomaru) {
        alt = lt = time * bpm / 60.0 / 4.0;
        tr = fract(lt);
        bt = floor(lt);
    }

    vec2 suv = (uv * 2.0 - 1.0) * asp;

    vec3 ro = vec3(0.0, 0.0, -3.0);
    vec3 dir;
    vec3 rd;
    dir = -ro;
    float sc1y = sc(1).y;
    if (sc1y < 0.3) {
        ro = vec3(mix(-1.0, 1.0, fract(alt)), 0.0, -3.0);
        dir = vec3(0.0, 0.0, 1.0);
    } else if (sc1y < 0.6) {
        ro = vec3(0.0, 0.0, mix(-5.0, -3.0, tr));
        dir = vec3(0.0, 0.0, 1.0);
    } else {
        float a = alt * 0.5;
        ro = vec3(cos(a), 0.0, sin(a)) * 2.0;
        dir = -ro;
    }
    float z = 0.5;
    if (sc(1).x < 0.3) {
        z = mix(0.3, 1.7, tr);
    }
    rd = norm(bnt(dir) * vec3(suv, z));
    float l = march(ro, rd);
    float c = exp(-0.2 * l);

    if (sc(0).y < 0.2) {
        c += hjct(suv * 0.5 + 0.5 + vec2(alt * 0.5, 0.0)) * step(fract(alt * 4.0), 0.5);
        c *= step(abs(suv.y), 0.5);
    }
    if (sc(1).z < 0.3) {
        vec2 ruv = suv * 4.0;
        vec3 h = hash(vec3(floor(ruv), floor(alt * 4.0)));
        vec2 fuv = fract(ruv);
        vec2 au = abs((fuv * 2.0 - 1.0) * rot(floor(h.z * 4.0) * pi / 4.0));
        c += step(max(au.x, au.y), 0.3) * step(min(au.x, au.y), 0.05) * step(h.x, 0.1);
    }
    if (tomaru) {
        float len = length(suv) - mix(0.2, 0.8, tr);
        float nya = step(abs(len), 0.005);
        if (len < 0.0) {
            c = 1.0 - c * 1.5;
        }
        c += nya;
    }
    float ema = 0.7;
    vec3 back = texture(backbuffer, uv).rgb;
    vec3 col = mix(vec3(c, back.rg * 1.5), back, ema);
    fragColor = vec4(col, 1.0);
}

#endif // SHADERTOY_BRIDGE_APPLIED
