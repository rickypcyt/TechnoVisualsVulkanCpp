// Code by Flopine
// Thanks to wsmind, leon, XT95, lsdlive, lamogui, 
// Coyhot, Alkama,YX, NuSan and slerpy for teaching me
// Thanks LJ for giving me the spark :3
// Thanks to the Cookie Collective, which build a cozy and safe environment for me 
// and other to sprout :)  https://twitter.com/CookieDemoparty

#define FLOPINE2_PI acos(-1.)
#define BPM (135./60.)
#define flopine2_dt(speed) fract(uTime*speed)

#define flopine2_bouncy(speed) sqrt(abs(sin(flopine2_dt(speed)*PI)))
#define switchanim(speed) floor(sin(flopine2_dt(speed)*2.*PI)+1.)

struct obj2 
{
    float d;
    float m;
    vec3 c;
};

mat2 rot (float a)
{return mat2(cos(a),sin(a),-sin(a),cos(a));}

void mo (inout vec2 p, vec2 d)
{
    p = abs(p)-d;
    if (p.y>p.x) p = p.yx;
}

vec3 pal (float t, vec3 c)
{
    return vec3(0.5)+vec3(0.5)*cos(2.*PI*(c*t+vec3(0.,0.37,0.63)));
}

obj2 strucmin (obj2 a, obj2 b)
{
    if (a.d<b.d) return a;
    else return b;
}

float box (vec3 p, vec3 c)
{
    vec3 q = abs(p)-c;
    return min(0., max(q.x,max(q.y,q.z)))+length(max(q,0.));
}

float flopine2_sc (vec3 p, float d)
{
    p = abs(p);
    p = max(p,p.yzx);
    return min(p.x,min(p.y,p.z))-d;
}  

obj2 cages (vec3 p, float bass, float mid, float high)
{
    mo(p.xz, vec2(0.5));
    p.x -= 0.8;

    mo(p.yz, vec2(1.));
    p.y -= 1.+flopine2_bouncy(BPM/4.) + bass * 0.3;

    mo(p.xz, vec2(2.));
    p.x -= 1.5;

    float anim = (PI/2.)*(floor(uTime*(BPM/2.))+pow(flopine2_dt(BPM/2.),5.));
    p.xz += vec2(cos(anim),sin(anim)) + mid * 0.2;

    return obj2(max(-flopine2_sc(p,0.9-flopine2_bouncy(BPM)*0.1),box(p,vec3(1.)))-0.02,0.,pal(length(p),vec3(1.)));
}

obj2 gem (vec3 p, float bass, float mid, float high)
{
    p.xz *= rot(flopine2_dt(BPM/5.)*PI + high * 0.5);
    return obj2 (dot(p,normalize(sign(p)))-1., 1., vec3(1.));
}

obj2 SDF2 (vec3 p, float bass, float mid, float high)
{
    p.yz *= rot(-atan(1./sqrt(2.)));
    p.xz *= rot(PI/4.0);
    return strucmin(gem(p, bass, mid, high),cages(p, bass, mid, high));
}

vec3 getnorm2(vec3 p, float bass, float mid, float high)
{
    vec2 eps = vec2(0.001,0.);
    return normalize(SDF2(p, bass, mid, high).d-vec3(SDF2(p-eps.xyy, bass, mid, high).d,SDF2(p-eps.yxy, bass, mid, high).d,SDF2(p-eps.yyx, bass, mid, high).d));
}

float mask(vec2 uv)
{
    return smoothstep(0.1,0.4, sin(fract(length(uv))-uTime*(BPM/4.)));
}

vec4 renderFlopine2(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    // Apply raymarched object style centering (neutral coordinates)
    vec2 uv = st;
    uv -= 0.5;
    uv /= vec2(uResolution.y / uResolution.x, 1);

    vec2 uu = floor(uv*30.)/30.;
    uv *= 0.95+mask(uu);
    
    vec3 ro = vec3(uv*8., -50.);
    vec3 rd = vec3(0., 0., 1.);
    vec3 l = normalize(vec3(1., 2., -2.));
    vec3 p = ro;
    vec3 col = vec3(0.0); // Black background

    bool hit = false; 
    obj2 O;

    for (float i = 0.; i < 64.; i++)
    {
        O = SDF2(p, bass, mid, high);
        if (O.d < 0.001)
        {
            hit = true; 
            break;
        }
        p += O.d * rd;
    }

    if (hit)
    {
        vec3 n = getnorm2(p, bass, mid, high);
        float lighting = max(dot(n, l), 0.);
        if (O.m == 0.) col = O.c * lighting;
        if (O.m == 1.) col = mix(vec3(0.1, 0.2, 0.8), vec3(1., 0.8, 0.8), lighting);
    }
    
    float alpha = clamp(0.7 + length(col) * 0.3 + energy * 0.2, 0.0, 1.0);
    return vec4(sqrt(col), alpha);
}
