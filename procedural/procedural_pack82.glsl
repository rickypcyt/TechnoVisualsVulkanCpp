// @EFFECT name="CFD Fluid Simulation" index=108 desc="Computational fluid dynamics with multi-scale rotation" author="System"

const float PI2 = PI * 2.0;

const int RotNum = 5;
const float ang = PI2 / float(RotNum);

mat2 rotationMatrix(float angle) {
    float c = cos(angle);
    float s = sin(angle);
    return mat2(c, s, -s, c);
}

// Procedural texture sampling instead of channel feedback
vec4 proceduralSample(vec2 uv, float time) {
    float r = abs(sin(uv.x * 8.0 + time * 0.5));
    float g = abs(sin(uv.y * 6.0 + time * 0.3));
    float b = abs(sin((uv.x + uv.y) * 10.0 + time * 0.7));
    float a = 0.5 + 0.5 * sin(uv.x * 12.0 + uv.y * 8.0 + time);
    return vec4(r, g, b, a);
}

// Multi-scale rotation calculation for velocity field
float getRot(vec2 pos, vec2 b, float time) {
    float l = log2(dot(b, b)) * sqrt(0.125) * 0.0;
    vec2 p = b;
    float rot = 0.0;
    mat2 m = rotationMatrix(ang);
    
    for (int i = 0; i < 5; i++) {
        vec4 texel = proceduralSample((pos + p) / uResolution.xy, time);
        vec2 texelXY = texel.xy - vec2(0.5);
        rot += dot(texelXY, p.yx * vec2(1.0, -1.0));
        p = m * p;
    }
    return rot / 5.0 / dot(b, b);
}

vec4 renderCFDFluid(vec2 st, float time, float tempo, float energy, float bass, float mid, float high) {
    vec2 pos = st * uResolution.xy;
    vec2 b = cos(time * 3.0 - vec2(0.0, 1.57));
    vec2 v = vec2(0.0);
    
    float bbMax = 0.5 * uResolution.y;
    bbMax *= bbMax;
    
    mat2 m = rotationMatrix(ang);
    mat2 mh = rotationMatrix(ang * 0.5);
    
    // Multi-scale velocity calculation
    for (int l = 0; l < 20; l++) {
        if (dot(b, b) > bbMax) break;
        vec2 p = b;
        for (int i = 0; i < 5; i++) {
            v += p.yx * getRot(pos + p, -mh * b, time);
            p = m * p;
        }
        b *= 2.0;
    }
    
    // Advection with procedural sampling
    vec2 advectedPos = fract((pos - v * vec2(-1.0, 1.0) * 5.0 * sqrt(uResolution.x / 600.0)) / uResolution.xy);
    vec4 fragColor = proceduralSample(advectedPos, time);
    
    // Self-consistency: mix calculated velocity back
    fragColor.xy = mix(fragColor.xy, v * vec2(-1.0, 1.0) * sqrt(0.125) * 0.9, 0.025);
    
    // Add "motor" - rotating current in center
    vec2 center = uResolution * 0.5;
    vec2 scr = fract((pos - center) / uResolution.x + 0.5) - 0.5;
    fragColor.xy += 0.003 * cos(time * 0.3 - vec2(0.0, 1.57)) / (dot(scr, scr) / 0.05 + 0.05);
    
    // Add procedural "drops"
    vec2 dropUV1 = pos / uResolution.xy * 0.35;
    vec2 dropUV2 = pos / uResolution.xy * 0.7;
    vec4 drop1 = proceduralSample(dropUV1, time * 1.5);
    vec4 drop2 = proceduralSample(dropUV2, time * 2.0);
    fragColor.zw += (drop1.zw - 0.5) * 0.002;
    fragColor.zw += (drop2.zw - 0.5) * 0.001;
    
    // Calculate normal from gradient
    float delta = 1.4 / uResolution.x;
    vec2 d = vec2(delta, 0.0);
    float val = length(fragColor.xyz);
    float gradX = (length(proceduralSample(st + d.xy, time).xyz) - length(proceduralSample(st - d.xy, time).xyz)) / delta;
    float gradY = (length(proceduralSample(st + d.yx, time).xyz) - length(proceduralSample(st - d.yx, time).xyz)) / delta;
    vec3 n = normalize(vec3(-vec2(gradX, gradY) * 0.02, 1.0));
    
    // Environmental reflection simulation
    vec2 sc = (st - 0.5) * vec2(uResolution.x / uResolution.y, 1.0);
    vec3 dir = normalize(vec3(sc, -1.0));
    vec3 R = reflect(dir, n);
    vec3 refl = abs(sin(R * 3.0 + time * 0.5));
    
    // Mix velocity field into color
    vec4 col = fragColor + 0.5;
    col = mix(vec4(1.0), col, 0.35);
    col.xyz *= 0.95 + -0.05 * n;
    
    // Final color with reflection
    vec3 finalColor = col.xyz * refl;
    
    return vec4(clamp(finalColor, 0.0, 1.0), 1.0);
}
