// @EFFECT name="Sphere Raytrace" index=73 desc="Raytraced spheres with fractal textures and reflections" author="Jordan Duty"

#define ptpi 1385.4557313670110891409199368797
#define pipi 36.462159692
#define picu 31.006276680299820175476315067101
#define pepi 23.140692632779269005729086367949
#define chpi 11.59195327552152062775175205256
#define shpi 11.548739357257748377977334315388
#define pisq 9.8696044010893586188344909998762
#define twpi 6.2831853075286766559
#define pi 3.1415926535897932384626433832795
#define sqpi 1.7724538509055160272981674833411
#define hfpi 1.5707963267948966192313216916398
#define cupi 1.4645918875615232630201425272638
#define prpi 1.4396194958475906883364908049738
#define lnpi 1.1447298858494001741434273513531
#define trpi 1.0471975511965977461542144610932
#define thpi 0.99627207622074994426469058001254
#define lgpi 0.4971498726941338543512682882909
#define rcpi 0.31830988618379067153776752674503
#define rcpipi 0.0274256931232981061195562708591

#define useFractal fractal2ColorRT

#define iterationsRT 10
#define formuparamRT 0.42
#define volstepsRT 10
#define stepsizeRT 0.120
#define zoomRT 0.1000
#define tileRT 0.1120
#define speedRT 0.00100
#define brightnessRT 0.001
#define darkmatterRT 0.500
#define distfadingRT 0.120
#define saturationRT -0.900

vec3 universeFractalRT(vec2 surfacePos, float time) {
    vec2 uv = surfacePos;
    vec3 dir = vec3(uv * zoomRT, 10.0);
    float a2 = time / 10.0 * speedRT;
    float a1 = 10.0;
    mat2 rot1 = mat2(cos(a1), sin(a1), -sin(a1), cos(a1));
    mat2 rot2 = rot1;
    dir.xz *= rot1;
    dir.xy *= rot2;
    vec3 from = vec3(0.0, 0.0, 0.0);
    from += vec3(0.001 * time, 0.001 * time, -2.0);
    from.xz *= rot1;
    from.xy *= rot2;
    float s = 0.4, fade = 0.2;
    vec3 v = vec3(0.4);
    for (int r = 0; r < volstepsRT; r++) {
        vec3 p = from + s * dir * 0.5;
        p = abs(vec3(tileRT) - mod(p, vec3(tileRT * 2.0)));
        float pa, a = pa = 0.0;
        for (int i = 0; i < iterationsRT; i++) {
            p = abs(p) / dot(p, p) - 1.1 * formuparamRT;
            a += abs(length(p) - pa);
            pa = length(p);
        }
        float dm = max(0.0, darkmatterRT - a * a * 0.001);
        a *= a * a * 2.0;
        if (r > 3) fade *= 1.0 - dm;
        v += fade;
        v += vec3(s, s * s, s * s * s * s) * a * brightnessRT * fade;
        fade *= distfadingRT;
        s += stepsizeRT;
    }
    v = mix(vec3(length(v)), v, saturationRT);
    return vec3(v * 0.01);
}

#define fractal_detailsRT 10
#define zoomoutRT 1.0

vec3 fractal2ColorRT(vec2 surfacePos, float time) {
    vec2 p = surfacePos * zoomoutRT;
    vec3 c = vec3(0.0);
    vec2 fractal;
    float deepfade = 1.0;
    for (int i = 0; i < fractal_detailsRT; i++) {
        deepfade *= 0.5;
        fractal = abs(p) / dot(p, p) - 1.0 + sin(time * 0.5) * 0.5;
        vec2 pdiff = fractal - p;
        c.rg += pdiff * deepfade;
        c.b += abs(length(pdiff)) * deepfade;
        p = fractal;
    }
    return c;
}

struct RayRT {
    vec3 Dir;
    vec3 Pos;
};

struct SphereRT {
    vec3 Pos;
    vec3 Color;
    float Rad;
    float Reflection;
};

vec3 LightPosRT = vec3(0.0, -3.0, 10.0);

vec3 IntersectsRT(SphereRT s, RayRT r) {
    vec3 l = s.Pos - r.Pos;
    float tca = dot(l, r.Dir);
    if (tca < 0.0) return vec3(0.0, 0.0, -1.0);
    float d2 = dot(l, l) - tca * tca;
    if (d2 > s.Rad * s.Rad) return vec3(0.0, 0.0, -1.0);
    float thc = sqrt((s.Rad * s.Rad) - d2);
    return vec3(tca - thc, tca + thc, 1.0);
}

vec3 Trace3RT(RayRT r, SphereRT spheres[7], float time) {
    vec3 Color = vec3(0.0, 0.0, 0.0);
    SphereRT s;
    bool col = false;
    float tnear = 1e8;
    for (int i = 0; i < 7; i++) {
        vec3 intTest = IntersectsRT(spheres[i], r);
        if (intTest.z != -1.0) {
            if (intTest.x < tnear) {
                tnear = intTest.x;
                s = spheres[i];
                col = true;
            }
        }
    }
    if (col == false) return vec3(0.0, 0.0, 0.0);
    vec3 phit = r.Pos + r.Dir * tnear;
    float spaceScale = 0.1;
    Color += (useFractal(phit.xy * spaceScale, time) + useFractal(phit.xz * spaceScale, time) + useFractal(phit.yz * spaceScale, time)) / 3.0;
    vec3 nhit = phit - s.Pos;
    nhit = normalize(nhit);
    vec3 lightDir = LightPosRT - phit;
    bool blocked = false;
    lightDir = normalize(lightDir);
    float DiffuseFactor = dot(nhit, lightDir);
    vec3 diffuseColor = vec3(0.0, 0.0, 0.0);
    vec3 ambientColor = vec3(s.Color * 0.2);
    for (int n = 0; n < 7; n++) {
        RayRT rl;
        rl.Pos = phit;
        rl.Dir = lightDir;
        vec3 intTestL = IntersectsRT(spheres[n], rl);
        if (intTestL.z != -1.0) {
            if (intTestL.x < length(LightPosRT - phit)) {
                blocked = true;
            }
        }
    }
    if (!blocked) {
        if (DiffuseFactor > 0.0) {
            diffuseColor = vec3(1.0, 1.0, 1.0) * DiffuseFactor;
            Color += s.Color * diffuseColor + ambientColor;
        } else {
            Color += ambientColor;
        }
    } else {
        Color += ambientColor;
    }
    return Color;
}

vec3 Trace2RT(RayRT r, SphereRT spheres[7], float time) {
    vec3 Color = vec3(0.0, 0.0, 0.0);
    SphereRT s;
    bool col = false;
    float tnear = 1e8;
    for (int i = 0; i < 7; i++) {
        vec3 intTest = IntersectsRT(spheres[i], r);
        if (intTest.z != -1.0) {
            if (intTest.x < tnear) {
                tnear = intTest.x;
                s = spheres[i];
                col = true;
            }
        }
    }
    if (col == false) return vec3(0.0, 0.0, 0.0);
    vec3 phit = r.Pos + r.Dir * tnear;
    vec3 nhit = phit - s.Pos;
    nhit = normalize(nhit);
    if (dot(r.Dir, nhit) > 0.0) nhit *= -1.0;
    if (s.Reflection > 0.0) {
        float facingratio = dot((r.Dir * -1.0), nhit);
        vec3 refldir = r.Dir - nhit * 2.0 * dot(r.Dir, nhit);
        refldir = normalize(refldir);
        RayRT rd;
        rd.Pos = phit;
        rd.Dir = refldir;
        vec3 refl = Trace3RT(rd, spheres, time);
        float param1 = (1.0 - s.Reflection);
        float param2 = s.Reflection;
        Color.x = (param1 * Color.x + param2 * refl.x);
        Color.y = (param1 * Color.y + param2 * refl.y);
        Color.z = (param1 * Color.z + param2 * refl.z);
    }
    vec3 lightDir = LightPosRT - phit;
    bool blocked = false;
    lightDir = normalize(lightDir);
    float DiffuseFactor = dot(nhit, lightDir);
    vec3 diffuseColor = vec3(0.0, 0.0, 0.0);
    vec3 ambientColor = vec3(s.Color * 0.2);
    for (int n = 0; n < 7; n++) {
        RayRT rl;
        rl.Pos = phit;
        rl.Dir = lightDir;
        vec3 intTestL = IntersectsRT(spheres[n], rl);
        if (intTestL.z != -1.0) {
            if (intTestL.x < length(LightPosRT - phit))
                blocked = true;
        }
    }
    if (!blocked) {
        if (DiffuseFactor > 0.0) {
            diffuseColor = vec3(1.0, 1.0, 1.0) * DiffuseFactor;
            Color += s.Color * diffuseColor + ambientColor;
        } else {
            Color += ambientColor;
        }
    } else {
        Color += ambientColor;
    }
    return Color;
}

vec3 spukeRT(vec3 pos, float time) {
    vec2 p = ((pos.z) + (sin((((length(sin((pos.xy) + pos.z * pi))) + (cos((pos.z * pi) / pi))))))) + pos.xy * pos.z;
    vec3 col = vec3(0.0, 0.0, 0.0);
    float ca = 0.0;
    for (int j = 1; j < 8; j++) {
        p *= 1.4;
        float jj = float(j);
        for (int i = 1; i < 8; i++) {
            vec2 newp = p * 0.96;
            float ii = float(i);
            newp.x += 1.2 / (ii + jj) * sin(ii * p.y + (p.x * 0.3) + cos(pos.z / pi / pi) * pi * pi + 0.003 * (jj / ii)) + 1.0;
            newp.y += 0.8 / (ii + jj) * cos(ii * p.x + (p.y * 0.3) + sin(pos.z / pi / pi) * pi * pi + 0.003 * (jj / ii)) - 1.0;
            p = newp;
        }
        p *= 0.9;
        col += vec3(0.5 * sin(pi * p.x) + 0.5, 0.5 * sin(pi * p.y) + 0.5, 0.5 * sin(pi * p.x) * cos(pi * p.y) + 0.5) * (0.5 * sin(pos.z * pi) + 0.5);
        ca += 0.7;
    }
    col /= ca;
    return vec3(col * col * col);
}

vec3 TraceRT(RayRT r, SphereRT spheres[7], float time) {
    vec3 Color = vec3(0.0, 0.0, 0.0);
    SphereRT s;
    bool col = false;
    float tnear = 1e8;
    for (int i = 0; i < 7; i++) {
        vec3 intTest = IntersectsRT(spheres[i], r);
        if (intTest.z != -1.0) {
            if (intTest.x < tnear) {
                tnear = intTest.x;
                s = spheres[i];
                col = true;
            }
        }
    }
    if (col == false) return vec3(0.0, 0.0, 0.0);
    vec3 phit = r.Pos + r.Dir * tnear;
    vec3 nhit = phit - s.Pos;
    nhit = normalize(nhit);
    if (dot(r.Dir, nhit) > 0.0) nhit *= -1.0;
    if (s.Reflection > 0.0) {
        float facingratio = dot((r.Dir * -1.0), nhit);
        vec3 refldir = r.Dir - nhit * 2.0 * dot(r.Dir, nhit);
        refldir = normalize(refldir);
        RayRT rd;
        rd.Pos = phit;
        rd.Dir = refldir;
        vec3 refl = Trace2RT(rd, spheres, time);
        float param1 = (1.0 - s.Reflection);
        float param2 = s.Reflection;
        Color.x = (param1 * Color.x + param2 * refl.x);
        Color.y = (param1 * Color.y + param2 * refl.y);
        Color.z = (param1 * Color.z + param2 * refl.z);
    }
    vec3 lightDir = LightPosRT - phit;
    bool blocked = false;
    lightDir = normalize(lightDir);
    float DiffuseFactor = dot(nhit, lightDir);
    vec3 diffuseColor = vec3(0.0, 0.0, 0.0);
    vec3 ambientColor = vec3(s.Color * 0.2);
    for (int n = 0; n < 7; n++) {
        RayRT rl;
        rl.Pos = phit;
        rl.Dir = lightDir;
        vec3 intTestL = IntersectsRT(spheres[n], rl);
        if (intTestL.z != -1.0) {
            if (intTestL.x < length(LightPosRT - phit))
                blocked = true;
        }
    }
    if (!blocked) {
        if (DiffuseFactor > 0.0) {
            diffuseColor = vec3(1.0, 1.0, 1.0) * DiffuseFactor;
            Color += s.Color * diffuseColor + ambientColor;
        } else {
            Color += ambientColor;
        }
    } else {
        Color += ambientColor;
    }
    return mix(Color, spukeRT(Color * pi, time), 0.5 + sin(time / pi) * 0.25);
}

vec4 renderSphereRaytrace(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    SphereRT spheres[7];
    spheres[0].Pos = vec3(5.0, 0.0, -0.0);
    spheres[0].Color = uPrimaryColor;
    spheres[0].Rad = 4.0;
    spheres[0].Reflection = 0.4;
    spheres[1].Pos = vec3(-5.0, 0.0, -0.0);
    spheres[1].Color = uSecondaryColor;
    spheres[1].Rad = 4.0;
    spheres[1].Reflection = 0.3;
    spheres[2].Pos = vec3(-5.0, 1004.0, -0.0);
    spheres[2].Color = vec3(0.0, 0.0, 0.0);
    spheres[2].Rad = 1000.0;
    spheres[2].Reflection = 0.5;
    spheres[3].Pos = vec3(-5.0, 0.0, -1040.0);
    spheres[3].Color = vec3(0.9, 0.0, 0.0);
    spheres[3].Rad = 1000.0;
    spheres[3].Reflection = 0.505;
    spheres[4].Pos = vec3(1020.0, 0.0, -0.0);
    spheres[4].Color = vec3(0.0, 0.9, 0.0);
    spheres[4].Rad = 1000.0;
    spheres[4].Reflection = 0.505;
    spheres[5].Pos = vec3(-1020.0, 0.0, -0.0);
    spheres[5].Color = vec3(0.0, 0.0, 0.7);
    spheres[5].Rad = 1000.0;
    spheres[5].Reflection = 0.505;
    spheres[6].Pos = vec3(-5.0, 0.0, 1040.0);
    spheres[6].Color = vec3(0.9, 0.9, 0.0);
    spheres[6].Rad = 1000.0;
    spheres[6].Reflection = 0.505;
    
    float invWidth = 1.0 / uResolution.x;
    float invHeight = 1.0 / uResolution.y;
    float fov = 60.0 + energy * 20.0;
    float aspectratio = uResolution.x / uResolution.y;
    float angle = tan(pi * 0.5 * fov / 180.0);
    
    vec2 coord = st * uResolution.xy;
    float camSpeed = 0.5 * (1.0 + tempo);
    vec3 camTrans = vec3(20.0 * cos(camSpeed * time), -5.0, 20.0 * sin(camSpeed * time));
    vec3 camDir = camTrans - vec3(0.0);
    LightPosRT.y = -10.0;
    mat3 rot;
    vec3 f = normalize(camTrans);
    vec3 u = vec3(0.0, 1.0, 0.0);
    vec3 s = normalize(cross(f, u));
    u = cross(s, f);
    rot[0][0] = s.x; rot[1][0] = s.y; rot[2][0] = s.z;
    rot[0][1] = u.x; rot[1][1] = u.y; rot[2][1] = u.z;
    rot[0][2] = f.x; rot[1][2] = f.y; rot[2][2] = f.z;
    RayRT R;
    float xx = (2.0 * ((coord.x + 0.5) * invWidth) - 1.0) * angle * aspectratio;
    float yy = (1.0 - 2.0 * ((coord.y + 0.5) * invHeight)) * angle;
    R.Pos = camTrans;
    R.Dir = vec3(xx, yy, -1.0) * rot;
    R.Dir = normalize(R.Dir);
    
    vec3 color = TraceRT(R, spheres, time);
    
    // Add audio reactivity
    color += uPrimaryColor * bass * 0.2;
    color = mix(color, uSecondaryColor, high * 0.15);
    
    return vec4(color, 1.0);
}
