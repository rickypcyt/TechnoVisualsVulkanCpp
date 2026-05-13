// @EFFECT name="Flopine" index=45 desc="Geometric scene with audio-reactive primitives" author="Flopine"
// Code by Flopine
// Thanks to wsmind, leon, XT95, lsdlive, lamogui, 
// Coyhot, Alkama,YX, NuSan, slerpy and wwrighter for teaching me
// Thanks LJ for giving me the spark :3
// Thanks to the Cookie Collective, which build a cozy and safe environment for me 
// and other to sprout :)  https://twitter.com/CookieDemoparty

#define FLOPINE_PI acos(-1.)
#define TAU 6.283581
#define ITER 80.

#define rot(a) mat2(cos(a),sin(a),-sin(a),cos(a))
#define crep(p,c,l) p=p-c*clamp(round(p/c),-l,l)

#define dt(sp,off) fract((uTime+off)*sp)
#define bouncy(sp,off) sqrt(sin(dt(sp,off)*PI))

struct obj
{
  float d;
  vec3 cs; 
  vec3 cl;
};

obj minobj (obj a, obj b)
{
  if (a.d<b.d) return a;
  else return b;
}

float stmin(float a, float b, float k, float n)
{
  float st = k/n;
  float u = b-k;
  return min(min(a,b),0.5*(u+a+abs(mod(u-a+st,2.*st)-st)));
}

void mo (inout vec2 p, vec2 d)
{
  p = abs(p)-d;
  if(p.y>p.x) p = p.yx;
}

float box (vec3 p, vec3 c)
{
  vec3 q = abs(p)-c;
  return min(0.,max(q.x,max(q.y,q.z)))+length(max(q,0.));
}

float flopine_sc (vec3 p, float d)
{
  p=abs(p);
  p=max(p,p.yzx);
  return min(p.x,min(p.y,p.z))-d;
}

obj prim1 (vec3 p, float bass, float mid, float high)
{
  p.x = abs(p.x)-3.;
  float per = 0.9;
  float id = round(p.y/per);
  p.xz *= rot(sin(dt(0.8,id*1.2)*TAU) + bass * 0.5);
  crep(p.y, per,4.);
  mo(p.xz,vec2(0.3));
  p.x += bouncy(2.,0.)*0.8;
  float pd = box(p,vec3(1.5,0.2,0.2));
  vec3 cs = uPrimaryColor;
  vec3 cl = uSecondaryColor;
  return obj(pd, cs, cl);
}

obj prim2 (vec3 p, float bass, float mid, float high)
{
  p.y = abs(p.y)-6.;
  p.z = abs(p.z)-4.;
  mo(p.xz, vec2(1.));
  vec3 pp = p;
  mo(p.yz, vec2(0.5));
  p.y -= 0.5;
  float p2d = max(-flopine_sc(p,0.7),box(p,vec3(1.)));
  p = pp;
  p2d = min(p2d, max(box(p,vec3(bouncy(2.,0.)*4. + mid * 2.)),flopine_sc(p,0.2)));
  vec3 cs = uPrimaryColor * 0.4;
  vec3 cl = uSecondaryColor;
  return obj(p2d, cs, cl);
}

obj prim3 (vec3 p, float bass, float mid, float high)
{
  p.z = abs(p.z)-9.;
  float per = 0.8;
  vec2 id = round(p.xy/per)-.5;
  float height = 1.*bouncy(2.,sin(length(id*0.05))) + high * 0.5;
  float p3d = box(p,vec3(2.,2.,0.2));
  crep(p.xy,per,2.);
  p3d = stmin(p3d,box(p+vec3(0.,0.,height*0.9),vec3(0.15,.15,height)),0.2,3.);
  vec3 cs = uPrimaryColor;
  vec3 cl = uSecondaryColor;
  return obj (p3d, cs, cl);
}

obj prim4 (vec3 p, float bass, float mid, float high)
{
  p.y = abs(p.y)-5.;
  mo(p.xz, vec2(1.));
  float scale = 1.5;
  p *= scale;
  float per = 2.*(bouncy(0.5,0.) + bass * 0.3);
  crep(p.xz,per,2.);
  float p4d = max(box(p,vec3(0.9)),flopine_sc(p,0.25));
  vec3 cs = uPrimaryColor * 0.5;
  vec3 cl = uSecondaryColor;
  return obj (p4d/scale, cs, cl);
}

float squared (vec3 p,float s)
{
  mo(p.zy,vec2(s));
  return box(p,vec3(0.2,10.,0.2));
}

obj prim5 (vec3 p, float bass, float mid, float high)
{
  p.x = abs(p.x)-8.;
  float id = round(p.z/7.);
  crep(p.z,7.,2.);
  float scarce = 3.;
  float p5d=1e10;
  for(int i=0;i<4; i++)
  {
    p.x += bouncy(1.,id*0.9)*0.6 + mid * 0.4;
    p5d = min(p5d,squared(p,scarce));
    p.yz *= rot(PI/4.);
    scarce -= 1.;    
  }
  vec3 cs = uPrimaryColor;
  vec3 cl = uSecondaryColor;
  return obj(p5d, cs, cl);
}

obj SDF (vec3 p, float bass, float mid, float high)
{
  p.yz *= rot(-atan(1./sqrt(2.)));
  p.xz *= rot(PI/4.);

  obj scene = prim1(p, bass, mid, high);
  scene = minobj(scene,prim2(p, bass, mid, high));
  scene = minobj(scene,prim3(p, bass, mid, high));
  scene = minobj(scene,prim4(p, bass, mid, high));
  scene = minobj(scene, prim5(p, bass, mid, high));
  return scene;
}

vec3 getnorm (vec3 p, float bass, float mid, float high)
{
  vec2 eps = vec2(0.001,0.);
  return normalize(SDF(p, bass, mid, high).d-vec3(SDF(p-eps.xyy, bass, mid, high).d,SDF(p-eps.yxy, bass, mid, high).d,SDF(p-eps.yyx, bass, mid, high).d));
}

vec4 renderFlopine(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    // Apply raymarched object style centering (neutral coordinates)
    vec2 uv = (st * 2.0 - 1.0);
    uv.x *= uResolution.x / max(uResolution.y, 1.0);

    // Camera centered in the scene looking outward
    vec3 ro = vec3(0.0, 0.0, 0.0);
    
    // Look direction based on UV with slight rotation over time
    float rotTime = uTime * 0.2;
    float cx = cos(rotTime);
    float sx = sin(rotTime);
    
    // Rotate UV coordinates for camera rotation
    vec2 ruv;
    ruv.x = uv.x * cx - uv.y * sx;
    ruv.y = uv.x * sx + uv.y * cx;
    
    // Ray direction outward from center
    float fov = 1.0;
    vec3 rd = normalize(vec3(ruv * fov, 1.0));
    
    // Slight camera movement based on bass
    ro += vec3(sin(uTime * 0.5) * bass * 0.5, 
               cos(uTime * 0.3) * bass * 0.3, 
               sin(uTime * 0.4) * bass * 0.2);
    
    vec3 p = ro;
    
    // Use black background instead of white
    vec3 col = vec3(0.0);
    vec3 l = normalize(vec3(1., 1.4, -2.));

    obj O; 
    bool hit = false;

    for (float i = 0.; i < ITER; i++)
    {
        O = SDF(p, bass, mid, high);
        if (O.d < 0.001)
        {
            hit = true; 
            break;
        }
        p += O.d * rd;
    }

    if (hit)
    {
        vec3 n = getnorm(p, bass, mid, high);
        float light = max(dot(n, l), 0.);
        col = mix(O.cs, O.cl, light);
    }
    
    float alpha = clamp(0.7 + length(col) * 0.3 + energy * 0.2, 0.0, 1.0);
    return vec4(sqrt(col), alpha);
}
