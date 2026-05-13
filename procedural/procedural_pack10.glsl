// @EFFECT name="Metal Gyroid Hall" index=30 desc="Metal gyroid hall with audio-reactive tweaks" author="System"
// @EFFECT name="Hex Kaleidoscope" index=31 desc="Hexagonal kaleidoscope pattern with audio reactivity" author="System"
// @EFFECT name="HSV Color Shift" index=32 desc="HSV color shifting with audio-reactive movement" author="System"
// Metal gyroid hall shader adapted from a Shadertoy snippet with audio-reactive tweaks

const vec3 kMetalLightDirection = normalize(vec3(-1.0, 2.0, 4.0));
const vec3 kMetalAmbientColor = vec3(0.2);
const float kMetalFogDensityBase = 0.05;
const float kMetallicBase = 0.8;
const float kMetalF0 = 0.8;
const float kMetalFov = radians(80.0);
const float kMetalPi2 = 2.0 * PI;

mat3 metalRotate3D(float angle, vec3 axis) {
    vec3 a = normalize(axis);
    float s = sin(angle);
    float c = cos(angle);
    float r = 1.0 - c;
    return mat3(
        a.x * a.x * r + c,
        a.y * a.x * r + a.z * s,
        a.z * a.x * r - a.y * s,
        a.x * a.y * r - a.z * s,
        a.y * a.y * r + c,
        a.z * a.y * r + a.x * s,
        a.x * a.z * r + a.y * s,
        a.y * a.z * r - a.x * s,
        a.z * a.z * r + c
    );
}

float metalSdGyroid(vec3 p) {
    return dot(sin(p), cos(p.yzx)) + 1.3;
}

float metalMap(vec3 p) {
    float d = metalSdGyroid(p);
    d = min(d, metalSdGyroid(p + vec3(PI, 0.0, 0.0)));
    d = min(d, metalSdGyroid(p + vec3(PI, PI, 0.0)));
    return d;
}

vec3 metalCalcNormal(vec3 p) {
    vec2 e = vec2(0.001, 0.0);
    return normalize(vec3(
        metalMap(p + e.xyy) - metalMap(p - e.xyy),
        metalMap(p + e.yxy) - metalMap(p - e.yxy),
        metalMap(p + e.yyx) - metalMap(p - e.yyx)
    ));
}

float metalCalcAO(vec3 position, vec3 normal) {
    float total = 0.0;
    float scale = 1.0;
    for (int i = 0; i < 10; ++i) {
        float hr = 0.01 + 0.02 * float(i * i);
        vec3 samplePos = position + normal * hr;
        float dd = metalMap(samplePos);
        float ao = clamp(hr - dd, 0.0, 1.0);
        total += ao * scale;
        scale *= 0.75;
    }
    return 1.0 - clamp(0.5 * total, 0.0, 1.0);
}

float metalCalcShadow(vec3 position, vec3 direction) {
    float h = 0.0;
    float t = 0.001;
    float res = 1.0;
    const float shadowCoef = 0.5;
    for (int i = 0; i < 12; ++i) {
        h = metalMap(position + direction * t);
        if (h < 0.0005) {
            return shadowCoef;
        }
        res = min(res, h * 16.0 / t);
        t += h;
        if (t > 4.0) break;
    }
    return 1.0 - shadowCoef + res * shadowCoef;
}

vec3 metalObjectColor(vec3 p) {
    float thresh = 0.5;
    if (metalSdGyroid(p) < thresh) {
        return vec3(1.0, 0.1, 0.1);
    } else if (metalSdGyroid(p + vec3(PI, 0.0, 0.0)) < thresh) {
        return vec3(0.1, 1.0, 0.1);
    } else if (metalSdGyroid(p + vec3(PI, PI, 0.0)) < thresh) {
        return vec3(0.1, 0.1, 1.0);
    }
    return vec3(0.4, 0.5, 0.7);
}

float metalFresnel(float baseF0, float cosTheta) {
    return baseF0 + (1.0 - baseF0) * pow(1.0 - cosTheta, 5.0);
}

float metalFogFactor(float distance, float density) {
    float s = distance * density;
    return exp(-s * s);
}

vec3 metalToneMapACES(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 metalRaymarch(vec3 camPos, vec3 rayDir, int maxSteps, float fogDensity, vec3 lightClr, inout vec3 attenuation, out bool hit) {
    vec3 accum = vec3(0.0);
    float dist = 0.0;
    hit = false;
    vec3 origin = camPos;

    for (int i = 0; i < maxSteps; ++i) {
        float sceneDist = metalMap(camPos);
        if (abs(sceneDist) < 1e-4) {
            hit = true;
            break;
        }
        camPos += rayDir * sceneDist;
        dist += sceneDist;
        if (dist > 30.0) {
            break;
        }
    }

    vec3 albedo = metalObjectColor(camPos);
    vec3 normal = metalCalcNormal(camPos);
    vec3 reflectionDir = reflect(rayDir, normal);

    float diffuse = max(dot(normal, kMetalLightDirection), 0.0);
    float specular = pow(max(dot(reflect(kMetalLightDirection, normal), rayDir), 0.0), 10.0);
    float ao = metalCalcAO(camPos, normal);
    float shadow = metalCalcShadow(camPos + normal * 0.005, kMetalLightDirection);

    accum += albedo * diffuse * shadow * (1.0 - kMetallicBase) * lightClr;
    accum += albedo * specular * shadow * kMetallicBase * lightClr;
    accum += albedo * ao * kMetalAmbientColor;

    float fog = metalFogFactor(distance(origin, camPos), fogDensity);
    accum = mix(vec3(1.0), accum, fog);

    float fresnel = metalFresnel(kMetalF0, dot(reflectionDir, normal));
    attenuation *= albedo * fresnel * fog;
    camPos += normal * 0.01;
    rayDir = reflectionDir;

    return accum;
}

vec4 renderMetalGyroidHall(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    vec2 aspect = vec2(uResolution.x / max(uResolution.y, 1.0), 1.0);
    vec2 fragCoord = (st / aspect + 0.5) * uResolution.xy;
    vec2 uv = (fragCoord * 2.0 - uResolution.xy) / max(uResolution.x, uResolution.y);

    float tempoBoost = clamp(tempo * 0.6 + bass * 0.4, 0.0, 1.5);
    float dynLightPower = mix(12.0, 24.0, clamp(energy + tempoBoost * 0.5, 0.0, 1.0));
    vec3 lightColor = vec3(1.0) * dynLightPower;
    float fogDensity = kMetalFogDensityBase * mix(0.6, 1.4, clamp(high + energy * 0.5, 0.0, 1.2));

    vec3 cameraPos = vec3(0.0, 0.0, -fract(time / kMetalPi2) * kMetalPi2);
    vec3 cameraDir = vec3(0.0, 0.0, -1.0);
    vec3 cameraSide = normalize(cross(cameraDir, vec3(0.0, 1.0, 0.0)));
    vec3 cameraUp = normalize(cross(cameraSide, cameraDir));

    vec3 rayDir = normalize(uv.x * cameraSide + uv.y * cameraUp + cameraDir / tan(kMetalFov * 0.5));
    float rotationSpeed = time * 0.07 * PI * mix(0.8, 1.4, clamp(tempo + high * 0.5, 0.0, 1.5));
    rayDir = metalRotate3D(rotationSpeed, normalize(vec3(5.0, 3.0, 1.0))) * rayDir;

    vec3 rayPos = cameraPos;
    bool hit = false;
    vec3 attenuation = vec3(1.0);
    vec3 color = vec3(0.0);

    color += metalRaymarch(rayPos, rayDir, 100, fogDensity, lightColor, attenuation, hit);
    for (int i = 0; i < 2; ++i) {
        if (!hit) break;
        color += attenuation * metalRaymarch(rayPos, rayDir, 60, fogDensity, lightColor, attenuation, hit);
    }

    // Ensure minimum brightness to prevent black screen
    color = max(color, vec3(0.15));

    color = metalToneMapACES(color * 1.2); // Increased from 0.8
    color = pow(color, vec3(1.0 / 2.2));

    vec3 palette = mix(uPrimaryColor, uSecondaryColor, clamp(uColorBlend, 0.0, 1.0));
    color = mix(palette, color, clamp(0.5 + energy * 0.4 + tempo * 0.2, 0.0, 1.0));
    color *= mix(0.8, 1.2, clamp(high + mid * 0.5, 0.0, 1.0));
    color = clamp(color, 0.0, 1.0);

    float alpha = clamp(0.35 + length(color) * 0.2, 0.0, 1.0);
    return vec4(color, alpha);
}

vec4 renderHexKaleidoscope(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    vec2 uv = st * 2.0 - 1.0;
    uv.x *= uResolution.x / max(uResolution.y, 1.0);
    
    float angle = atan(uv.y, uv.x);
    float radius = length(uv);
    
    // Hexagonal symmetry (6 segments)
    float segment = PI / 3.0;
    angle = mod(angle, segment);
    if (angle > segment * 0.5) {
        angle = segment - angle;
    }
    
    uv = vec2(cos(angle), sin(angle)) * radius;
    
    // Add rotation based on audio
    float rotation = time * 0.5 + tempo * 0.2;
    float s = sin(rotation);
    float c = cos(rotation);
    uv = vec2(uv.x * c - uv.y * s, uv.x * s + uv.y * c);
    
    // Create hexagonal pattern
    vec2 hex = vec2(0.5, sqrt(3.0) * 0.5);
    vec2 hexCoord = uv / hex;
    vec2 hexIndex = floor(hexCoord);
    hexCoord = fract(hexCoord) - 0.5;
    
    float hexDist = length(hexCoord);
    
    // Audio-reactive parameters
    float pulse = 1.0 + bass * 0.3 + energy * 0.2;
    float colorShift = mid * 0.5 + high * 0.3;
    
    // Create pattern
    float pattern = sin(hexDist * 10.0 * pulse - time * 2.0) * 0.5 + 0.5;
    pattern *= 1.0 - smoothstep(0.4, 0.6, hexDist);
    
    // Color based on audio and position
    vec3 color = vec3(0.0);
    color.r = pattern * (0.5 + sin(time + colorShift) * 0.5);
    color.g = pattern * (0.5 + cos(time * 1.3 + colorShift * 1.5) * 0.5);
    color.b = pattern * (0.5 + sin(time * 0.7 + colorShift * 2.0) * 0.5);
    
    // Apply palette
    vec3 palette = mix(uPrimaryColor, uSecondaryColor, uColorBlend);
    color = mix(palette, color, pattern);
    
    // Add glow effect for high energy
    float glow = exp(-hexDist * 2.0) * energy;
    color += vec3(glow * 0.2, glow * 0.3, glow * 0.4);
    
    return vec4(color, pattern);
}

vec3 rgb2hsv(vec3 c) {
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));

    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

vec4 renderHSVColorShift(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    vec2 uv = st;
    vec2 tc = uv;
    float aspect = uResolution.x / uResolution.y;
    
    uv = uv * 2.0 - 1.0;
    uv.x *= aspect;
    
    // Audio-reactive movement
    float audioMovement = (bass * 0.5 + mid * 0.3) * 0.05;
    uv += vec2(sin(time + audioMovement), cos(time + audioMovement)) * 0.025;
    
    float s = 1.0 - smoothstep(0.0, 0.6, length(max(abs(uv.x), abs(uv.y))));
    s = sin(s);
    
    tc = 2.0 * tc - 1.0;
    tc *= 0.999;
    tc = tc * 0.5 + 0.5;
    
    // Create a base pattern using previous frame or generated texture
    vec3 h = vec3(0.5 + 0.5 * sin(time + tc.x * 10.0), 
                  0.5 + 0.5 * cos(time + tc.y * 10.0), 
                  0.5 + 0.5 * sin(time * 1.3 + length(tc) * 5.0));
    h = rgb2hsv(h);
    
    // Audio-reactive hue shift
    h.r += time * 0.1 + energy * 0.2;
    
    vec2 texel = 1.0 / uResolution.xy;
    vec2 d = texel * 0.75;
    vec3 p = vec3(0.5 + 0.5 * sin(h.r * 6.283 + time),
                  0.5 + 0.5 * cos(h.r * 6.283 + time * 1.3),
                  0.5 + 0.5 * sin(h.r * 6.283 + time * 0.7));
    
    vec3 c = vec3(s, 0.0, 0.0) + p;
    c = clamp(c, vec3(0.0), vec3(1.0));
    
    c = rgb2hsv(c);
    c.r += 0.002 + high * 0.01; // Audio-reactive hue shift
        
    c = hsv2rgb(c);
    c = c.r > 0.99 ? fract(c.rgb) : c;
    
    // Apply audio-reactive brightness
    float brightness = 1.0 + energy * 0.3 + bass * 0.2;
    c *= brightness;
    
    // Apply palette
    vec3 palette = mix(uPrimaryColor, uSecondaryColor, uColorBlend);
    c = mix(palette, c, 0.7);
    
    return vec4(c, 1.0);
}

// @EFFECT name="Crypt Roots" index=33 desc="Ray marched improvised geometry with curvy shapes" author="System"
// Crypt Roots - Ray marching improvised geometry with curvy shapes and rock textures
// Adapted for audio reactivity
#define repeat(p,r) (mod(p,r)-r/2.)
mat2 rotCrypt (float a) { float c=cos(a), s=sin(a); return mat2(c,s,-s,c); }
float smoothmin (float a, float b, float r) { float h = clamp(.5+.5*(b-a)/r, 0., 1.); return mix(b, a, h)-r*h*(1.-h); }
float random (in vec2 st) { return fract(sin(dot(st.xy,vec2(12.9898,78.233)))*43758.5453123); }
float hash_n(float n) { return fract(sin(n) * 1e4); }
float noise(vec3 x) {
    const vec3 step = vec3(110, 241, 171);
    vec3 i = floor(x);
    vec3 f = fract(x);
    float n = dot(i, step);
    vec3 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix( hash_n(n + dot(step, vec3(0, 0, 0))), hash_n(n + dot(step, vec3(1, 0, 0))), u.x),
                   mix( hash_n(n + dot(step, vec3(0, 1, 0))), hash_n(n + dot(step, vec3(1, 1, 0))), u.x), u.y),
               mix(mix( hash_n(n + dot(step, vec3(0, 0, 1))), hash_n(n + dot(step, vec3(1, 0, 1))), u.x),
                   mix( hash_n(n + dot(step, vec3(0, 1, 1))), hash_n(n + dot(step, vec3(1, 1, 1))), u.x), u.y), u.z);
}
float fbm (vec3 p) {
  float amplitude = 0.5;
  float result = 0.0;
  for (float index = 0.0; index <= 3.0; ++index) {
    result += noise(p / amplitude) * amplitude;
    amplitude /= 2.;
  }
  return result;
}
vec3 look (vec3 eye, vec3 target, vec2 anchor) {
    vec3 forward = normalize(target-eye);
    vec3 right = normalize(cross(forward, vec3(0,1,0)));
    vec3 up = normalize(cross(right, forward));
    return normalize(forward + right * anchor.x + up * anchor.y);
}
void moda(inout vec2 p, float repetitions) {
	float angle = 2.*PI/repetitions;
	float a = atan(p.y, p.x) + angle/2.;
	a = mod(a,angle) - angle/2.;
	p = vec2(cos(a), sin(a))*length(p);
}

float mapCryptRoots (vec3 pos, float time, float bass, float mid, float high) {
  // Audio-reactive parameters
  float audioScale = 1.0 + bass * 0.3 + mid * 0.2;
  float audioDistortion = high * 0.1;
  
  float chilly = noise(pos * 2. * audioScale);
  float salty = fbm(pos*20. * audioScale);
  
  pos.z -= salty*.04 + audioDistortion * 0.1;
  salty = smoothstep(.3, 1., salty);
  pos.z += salty*.04;
  pos.xy -= (chilly*2.-1.) * (.2 + bass * 0.1);
    
  vec3 p = pos;
  vec2 cell = vec2(1., .5);
  vec2 id = floor(p.xz/cell);
  p.xy *= rotCrypt(id.y * .5 + time * 0.1 + bass * 0.2);
  p.y += sin(p.x + .5 + time * 2.0 + mid * 0.5);
  p.xz = repeat(p.xz, cell);
    
  vec3 pp = p;
  moda(p.yz, 5.0 + high * 2.0);
  p.y -= .1;
  float scene = length(p.yz)-.02;
    
  vec3 ppp = pos;
  pp.xz *= rotCrypt(pp.y * 5. + time * 0.5);
  ppp = repeat(ppp, .1);
  moda(pp.xz, 3.0 + bass);
  pp.x -= .04 + .02*sin(pp.y*5. + time * 3.0 + mid);
  scene = smoothmin(length(pp.xz)-.01, scene, .2);

  p = pos;
  p.xy *= rotCrypt(-p.z + time * 0.2 + high * 0.3);
  moda(p.xy, 8.0 + mid);
  p.x -= .7 + bass * 0.1;
  p.xy *= rotCrypt(p.z*8. + time);
  p.xy = abs(p.xy)-.02;
  scene = smoothmin(scene, length(p.xy)-.005, .2);

  return scene;
}

vec3 getNormalCryptRoots (vec3 pos, float time, float bass, float mid, float high) {
  vec2 e = vec2(1.0,-1.0)*0.5773*0.0005;
  return normalize( e.xyy*mapCryptRoots( pos + e.xyy, time, bass, mid, high ) + 
                    e.yyx*mapCryptRoots( pos + e.yyx, time, bass, mid, high ) + 
                    e.yxy*mapCryptRoots( pos + e.yxy, time, bass, mid, high ) + 
                    e.xxx*mapCryptRoots( pos + e.xxx, time, bass, mid, high ) );
}

vec4 renderCryptRoots(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
  vec2 uv = (st - 0.5) / 0.5;  // Normalize to -1 to 1 range
  
  // Audio-reactive camera movement
  vec3 eye = vec3(.1 + bass * 0.1, .1 + mid * 0.05, -time * 0.1 - 4. + high * 0.2);
  vec3 at = vec3(0, 0, eye.z - 2.0 + sin(time * 0.5 + bass) * 0.5);
  vec3 ray = look(eye, at, uv);
  vec3 pos = eye;
  
  float dither = random(uv + fract(time));
  float total = dither * .2;
  float shade = 0.0;
  const float count = 60.0;
  
  for (float index = count; index > 0.0; --index) {
    pos = eye + ray * total;
    float dist = mapCryptRoots(pos, time, bass, mid, high);
    if (dist < 0.001 + total * .003) {
      shade = index / count;
      break;
    }
    dist *= 0.5 + 0.1 * dither;
    total += dist;
  }
  
  vec3 normal = getNormalCryptRoots(pos, time, bass, mid, high);
  vec3 color = vec3(0);
  
  // Audio-enhanced colors
  color += smoothstep(.3, .6, fbm(pos*100.)) * (.2 + energy * 0.3);
  color += vec3(0.839, 1, 1) * pow(clamp(dot(normal, normalize(vec3(0,2,1))), 0.0, 1.0), 4.) * (1.0 + high * 0.5);
  color += vec3(1, 0.725, 0.580) * pow(clamp(dot(normal, -normalize(pos-at)), 0.0, 1.0), 4.) * (1.0 + mid * 0.3);
  color += vec3(0.972, 1, 0.839) * pow(clamp(dot(normal, normalize(vec3(4,0,1))), 0.0, 1.0), 4.) * (1.0 + bass * 0.4);
  color += vec3(0.972, 1, 0.839) * pow(clamp(dot(normal, normalize(vec3(-5,0,1)))*.5+.5, 0.0, 1.0), 4.) * (1.0 + energy * 0.2);
  
  color = mix(vec3(0), color, clamp(dot(normal, -ray), 0.0, 1.0));
  color *= pow(shade, 1.0/1.2);
  
  // Apply palette
  vec3 palette = mix(uPrimaryColor, uSecondaryColor, uColorBlend);
  color = mix(palette, color, 0.6);
  
  // Audio-reactive brightness
  color *= 1.0 + energy * 0.3;
  
  return vec4(color, 1.0);
}
