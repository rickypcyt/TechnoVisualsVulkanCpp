// @EFFECT name="Fractal Runway" index=24 desc="Temporal fractal runway raymarch" author="etiennejcb"
// Temporal fractal runway shader adapted from Shadertoy snippet by @etiennejcb

const float runway_defaultStep = 0.025;
const float runway_defaultMaxDist = 15.0;
const float runway_referencePixels = 1280.0 * 720.0;

float runway_time = 0.0;
vec3 runway_lightDir = vec3(0.0);
vec3 runway_colorAccum = vec3(0.0);
vec2 runway_fragCoord = vec2(0.0);
bool runway_accumulateColor = true;
float runway_resolutionPressure = 1.0;
float runway_fractalIterationLimit = 6.0;
float runway_marchIterationLimit = 120.0;

mat2 runway_rot(float a) {
    float s = sin(a);
    float c = cos(a);
    return mat2(c, s, -s, c);
}

vec3 runway_fractal(vec2 p) {
    vec2 pos = p;
    float d = 0.0;
    float ml = 100.0;
    vec2 mc = vec2(100.0);
    p = abs(fract(p * 0.1) - 0.5);
    vec2 c = p;
    for (int i = 0; i < 7; ++i) {
        if (float(i) >= runway_fractalIterationLimit) {
            break;
        }
        d = dot(p, p);
        p = abs(p + 1.0) - abs(p - 1.0) - p;
        p = p * -1.5 / clamp(d, 0.5, 1.0) - c;
        mc = min(mc, abs(p));
        if (i > 2) {
            ml = min(ml * (1.0 + float(i) * 0.1), abs(p.y - 0.5));
        }
    }
    mc = max(vec2(0.0), 1.0 - mc);
    float mcLen = length(mc);
    if (mcLen > 0.0) {
        mc = (mc / mcLen) * 0.8;
    }
    ml = pow(max(0.0, 1.0 - ml), 6.0);
    float wave = step(0.7, fract(d * 0.1 + runway_time * 0.5 + pos.x * 0.2));
    return (vec3(mc, d * 0.4) * ml * wave) - ml * 0.1;
}

float runway_map(vec2 p) {
    if (runway_accumulateColor) {
        runway_colorAccum += runway_fractal(p);
    }

    float t = runway_time;
    vec2 p2 = abs(0.5 - fract(p * 8.0 + 4.0));

    float h = sin(length(p) + t);
    vec2 cell = floor(p * 2.0 + 1.0);
    float l = length(p2 * p2);
    h += (cos(cell.x + t) + sin(cell.y + t)) * 0.5;
    h += max(0.0, 5.0 - length(cell - vec2(18.0, 0.0))) * 1.5;
    h += max(0.0, 5.0 - length(cell + vec2(18.0, 0.0))) * 1.5;
    cell = cell * 2.0 + 0.2345;
    t *= 0.5;
    h += (cos(cell.x + t) + sin(cell.y + t)) * 0.3;
    return h;
}

vec3 runway_normal(vec2 p) {
    vec2 eps = vec2(0.0, 0.001);
    bool previous = runway_accumulateColor;
    runway_accumulateColor = false;
    float nx = runway_map(p + eps.yx) - runway_map(p - eps.yx);
    float ny = runway_map(p + eps.xy) - runway_map(p - eps.xy);
    runway_accumulateColor = previous;
    return normalize(vec3(nx, 2.0 * eps.y, ny));
}

vec2 runway_hit(vec3 p) {
    float h = runway_map(p.xz);
    return vec2(step(p.y, h), h);
}

vec3 runway_bsearch(vec3 from, vec3 dir, float td, inout float step, out vec2 hitInfo) {
    vec3 pos = from;
    float previous = 1.0;
    step *= -0.5;
    td += step;
    for (int i = 0; i < 12; ++i) {
        pos = from + td * dir;
        hitInfo = runway_hit(pos);
        if (abs(hitInfo.x - previous) > 0.001) {
            step *= -0.5;
            previous = hitInfo.x;
        }
        td += step;
    }
    return pos;
}

vec3 runway_shade(vec3 p, vec3 dir, float surfaceHeight, float travel) {
    vec3 normal = runway_normal(p.xz);
    float diffuse = max(0.0, dot(runway_lightDir, -normal));
    vec3 reflection = reflect(runway_lightDir, dir);
    float specular = pow(max(0.0, dot(reflection, -normal)), 8.0);
    vec3 base = runway_colorAccum * 0.25;
    // Use darker palette colors instead of white for specular
    vec3 specColor = mix(uPrimaryColor * 0.6, uSecondaryColor * 0.6, uColorBlend);
    vec3 lighting = (diffuse * 0.5 + 0.2 + specular * specColor) * 0.2;
    return base + lighting;
}

vec3 runway_march(vec3 from, vec3 dir, float baseStep, float maxDist) {
    vec3 pos = from;
    vec3 color = vec3(0.0);
    float travel = 0.5;
    float step = baseStep;
    vec2 hitInfo = vec2(0.0);
    int dynamicMaxSteps = int(runway_marchIterationLimit);
    dynamicMaxSteps = max(dynamicMaxSteps, 40);

    for (int i = 0; i < 200; ++i) {
        if (i >= dynamicMaxSteps) {
            break;
        }
        pos = from + dir * travel;
        hitInfo = runway_hit(pos);
        if (hitInfo.x > 0.5 || travel > maxDist) {
            break;
        }
        travel += step;
    }

    if (hitInfo.x > 0.5) {
        vec2 refined;
        vec3 surfPos = runway_bsearch(from, dir, travel, step, refined);
        color = runway_shade(surfPos, dir, refined.y, travel);
        travel = length(surfPos - from);
    }

    float fade = pow(clamp(travel / maxDist, 0.0, 1.0), 3.0);
    vec3 scanline = 2.0 * vec3(mod(runway_fragCoord.y, 4.0) * 0.1);
    color = mix(color, scanline, fade);
    // Use darker palette colors instead of white-ish multiplier
    vec3 finalMult = mix(uPrimaryColor * 0.7, uSecondaryColor * 0.7, uColorBlend);
    return color * finalMult;
}

mat3 runway_lookat(vec3 dir, vec3 up) {
    vec3 forward = normalize(dir);
    vec3 right = normalize(cross(forward, normalize(up)));
    return mat3(right, cross(right, forward), forward);
}

vec3 runway_path(float t) {
    return vec3(cos(t) * 5.5, 1.5, sin(t * 2.0)) * 2.5;
}

vec4 renderFractalRunway(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    vec2 aspect = vec2(uResolution.x / max(uResolution.y, 1.0), 1.0);
    vec2 fragCoord = (st / aspect + 0.5) * uResolution.xy;
    runway_fragCoord = fragCoord;

    vec2 uv = (fragCoord - 0.5 * uResolution.xy) / max(uResolution.y, 1.0);
    
    // Dynamic resolution scaling for performance
    float pixelCount = uResolution.x * uResolution.y;
    float resolutionScale = 1.0;
    if (pixelCount > 1920.0 * 1080.0) {
        resolutionScale = 0.5;
    } else if (pixelCount > 1280.0 * 720.0) {
        resolutionScale = 0.7;
    }
    uv *= resolutionScale;

    runway_resolutionPressure = clamp(pixelCount / runway_referencePixels, 1.0, 4.0);
    float highResFactor = clamp((runway_resolutionPressure - 1.0) / 3.0, 0.0, 1.0);
    runway_fractalIterationLimit = mix(6.0, 4.0, highResFactor);
    runway_marchIterationLimit = mix(120.0, 80.0, highResFactor);

    runway_time = time;
    runway_colorAccum = vec3(0.0);
    runway_lightDir = normalize(vec3(0.0, -1.0, -1.0 + high * 0.4));

    bool allowAccumulation = highResFactor < 0.98;
    runway_accumulateColor = allowAccumulation;
    vec3 fallbackFractal = vec3(0.0);
    if (!allowAccumulation) {
        // Sample a lightweight fractal preview so we still get color at ultra high resolution
        vec2 fallbackPos = uv * 1.5 + vec2(time * 0.3, time * 0.17);
        fallbackFractal = abs(runway_fractal(fallbackPos)) * 0.25;
    }

    float tempoInfluence = clamp(tempo * 0.2 + bass * 0.3, 0.0, 1.0);
    float resolutionStepBoost = mix(1.0, 2.2, highResFactor);
    float baseStep = runway_defaultStep * mix(1.2, 0.6, clamp(energy + tempoInfluence, 0.0, 1.0)) * resolutionStepBoost;
    float maxDist = runway_defaultMaxDist * mix(1.0, 1.5, clamp(energy * 0.6, 0.0, 1.0));
    maxDist *= mix(1.0, 0.85, highResFactor);

    float travelTime = time * 0.2;
    vec3 from = runway_path(travelTime);
    vec3 dir = normalize(vec3(uv, 0.7));
    vec3 advance = runway_path(travelTime + 0.1) - from;
    vec3 up = vec3(advance.x * 0.1, 1.0, 0.0);
    dir = runway_lookat(advance + vec3(0.0, -0.2 - (1.0 + sin(travelTime * 2.0)), 0.0), up) * dir;

    vec3 coreColor = runway_march(from, dir, baseStep, maxDist) * 1.5;

    vec3 paletteBase = mix(uPrimaryColor, uSecondaryColor, clamp(uColorBlend, 0.0, 1.0));
    float energyMix = clamp(0.45 + energy * 0.4 + uIntensity * 0.3, 0.0, 1.0);
    vec3 motionGlow = (runway_colorAccum + fallbackFractal) * 0.28;
    vec3 vignette = paletteBase * pow(max(0.0, 1.0 - length(uv) * 0.85), 3.0) * 0.2;

    vec3 finalColor = mix(paletteBase, coreColor, energyMix);
    finalColor += motionGlow + vignette;
    finalColor = clamp(finalColor, 0.0, 1.0);

    float alpha = clamp(0.3 + energyMix * 0.55, 0.0, 1.0);
    return vec4(finalColor, alpha);
}
