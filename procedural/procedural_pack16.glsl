// @EFFECT name="Weird Creature" index=39 desc="Endless living creature with audio reactivity" author="Leon Denise"
// Weird Endless Living Creature
// Inspired by Inigo Quilez live stream shader deconstruction
// Leon Denise (ponk) 2019.08.28
// Licensed under hippie love conspiracy
// Audio-reactive adaptation for visualizer

// Using code from Inigo Quilez, Morgan McGuire

// tweak zone
const int count = 15;
const float speed = 1.;
const float balance = 1.5;
const float range = 1.4;
const float radius = .6;
const float blend = .3;
const float falloff = 1.2;

// increment it at your own GPU risk
const float motion_frames = 1.;

// toolbox (using unique names to avoid conflicts)
#define repeat(p,r) (mod(p,r)-r/2.)
float weirdRandom(vec2 p) { return fract(1e4 * sin(17.0 * p.x + p.y * 0.1) * (0.1 + abs(sin(p.y * 13.0 + p.x)))); }
mat2 weirdRot(float a) { float c=cos(a),s=sin(a); return mat2(c,-s,s,c); }
float weirdSmoothmin (float a, float b, float r) { float h = clamp(.5+.5*(b-a)/r, 0., 1.); return mix(b, a, h)-r*h*(1.-h); }
float sdSphere (vec3 p, float r) { return length(p)-r; }
vec3 weirdLook (vec3 eye, vec3 target, vec2 anchor, float fov) {
    vec3 forward = normalize(target-eye);
    vec3 right = normalize(cross(forward, vec3(0,1,0)));
    vec3 up = normalize(cross(right, forward));
    return normalize(forward * fov + right * anchor.x + up * anchor.y);
}

float weirdGeometry (vec3 pos, float time) {
    float scene = 1., a = 1.;
    float t = time * .5 + pos.x / 30.;
    t = floor(t)+smoothstep(0.0,.9,pow(fract(t),2.));
    pos.x = repeat(pos.x+time, 5.);
    for (int i = count; i > 0; --i) {
        pos.x = abs(pos.x)-range*a;
        pos.xy *= weirdRot(cos(t)*balance/a+a*2.);
        pos.zy *= weirdRot(sin(t)*balance/a+a*2.);
        scene = weirdSmoothmin(scene, sdSphere(pos,(radius*a)), blend*a);
        a /= falloff;
    }
    return scene;
}

float weirdRaymarch ( vec3 eye, vec3 ray, float time, out float total ) {
    float dither = weirdRandom(ray.xy+fract(time));
    total = 0.0;
    const int count = 20;
    for (int index = count; index > 0; --index) {
        float dist = weirdGeometry(eye+total*ray,time);
        dist *= 0.9+0.1*dither;
        total += dist;
        if (dist < 0.001 * total) {
            return float(index)/float(count);
        }
    }
    return 0.;
}

vec3 weirdCamera (vec3 eye, float audioMod) {
    // Audio-reactive camera movement
    float audioRotation = audioMod * 0.3;
    eye.yz *= weirdRot(audioRotation);
    eye.xz *= weirdRot(audioRotation * 0.7);
    return eye;
}

vec4 renderWeirdCreature(vec2 st, float uTime, float uTempo, float uEnergy, float uBass, float uMid, float uHigh) {
    vec2 uv = st * 2.0; // Convert from -1,1 to -2,2 range
    
    // Audio-reactive parameters
    float audioMod = uBass * 0.5 + uMid * 0.3 + uHigh * 0.2;
    float speedMod = speed * (1.0 + audioMod * 0.5);
    float rangeMod = range * (1.0 + uEnergy * 0.3);
    
    vec3 eye = weirdCamera(vec3(0,0,4), audioMod);
    vec3 ray = weirdLook(eye, vec3(0), uv, 1.);
    float total = 0.0;
    vec4 fragColor = vec4(0);
    
    for (float index = motion_frames; index > 0.; --index) {
        float dither = weirdRandom(ray.xy+fract(uTime+index));
        float time = uTime*speedMod+(dither+index)/10./motion_frames;
        fragColor += vec4(weirdRaymarch(eye, ray, time, total))/motion_frames;
    }
    
    // extra color with audio reactivity
    vec3 baseColor = vec3(.7,.8,.9);
    baseColor.r *= 1.0 + uBass * 0.4;
    baseColor.g *= 1.0 + uMid * 0.4;
    baseColor.b *= 1.0 + uHigh * 0.4;
    
    fragColor.rgb *= baseColor;
    float d = smoothstep(7.,0.,total);
    
    // Audio-reactive glow
    vec3 glowColor = vec3(0.8,.6,.5);
    glowColor.r *= 1.0 + uBass * 0.5;
    glowColor.g *= 1.0 + uMid * 0.3;
    glowColor *= d * (1.0 + uEnergy * 0.5);
    
    fragColor.rgb += glowColor;
    
    // Add some extra brightness based on energy
    fragColor.rgb *= 1.0 + uEnergy * 0.2;
    
    return fragColor;
}
